#include "app/Gamepad.h"

#include <algorithm>
#include <cmath>

namespace ns {

Gamepad::~Gamepad() {
  if (pad_) SDL_GameControllerClose(pad_);
}

float Gamepad::axis(SDL_GameControllerAxis a) const {
  if (!pad_) return 0;
  float v = SDL_GameControllerGetAxis(pad_, a) / 32767.0f;
  const float dead = 0.18f;
  if (std::fabs(v) < dead) return 0;
  // Rescale past the dead zone, then square for fine control near the centre.
  float s = (std::fabs(v) - dead) / (1 - dead);
  return std::copysign(std::min(1.0f, s * s), v);
}

void Gamepad::key(SDL_Keycode k) {
  for (Uint32 type : {SDL_KEYDOWN, SDL_KEYUP}) {
    SDL_Event ev{};
    ev.type = type;
    ev.key.state = type == SDL_KEYDOWN ? SDL_PRESSED : SDL_RELEASED;
    ev.key.keysym.sym = k;
    ev.key.keysym.scancode = SDL_GetScancodeFromKey(k);
    ev.key.windowID = 0;
    SDL_PushEvent(&ev);
  }
}

void Gamepad::mouseButton(SDL_Window* win, Uint8 button, bool down) {
  SDL_Event ev{};
  ev.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
  ev.button.windowID = win ? SDL_GetWindowID(win) : 0;
  ev.button.which = 0;  // look like a real mouse so every screen accepts it
  ev.button.button = button;
  ev.button.state = down ? SDL_PRESSED : SDL_RELEASED;
  ev.button.clicks = 1;
  int mx, my;
  SDL_GetMouseState(&mx, &my);
  ev.button.x = mx;
  ev.button.y = my;
  SDL_PushEvent(&ev);
}

bool Gamepad::handle(const SDL_Event& e, SDL_Window* win) {
  switch (e.type) {
    case SDL_CONTROLLERDEVICEADDED:
      if (!pad_) {
        pad_ = SDL_GameControllerOpen(e.cdevice.which);
        if (pad_) padId_ = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad_));
      }
      return true;
    case SDL_CONTROLLERDEVICEREMOVED:
      if (pad_ && e.cdevice.which == padId_) {
        SDL_GameControllerClose(pad_);
        pad_ = nullptr;
        padId_ = -1;
        if (radialOpen && onRadial) onRadial(false);
        radialOpen = false;
      }
      return true;
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
      if (e.cbutton.which != padId_) return true;
      bool down = e.type == SDL_CONTROLLERBUTTONDOWN;
      switch (e.cbutton.button) {
        case SDL_CONTROLLER_BUTTON_A:
          if (!radialOpen) mouseButton(win, SDL_BUTTON_LEFT, down);
          break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
          if (onRadial && down != radialOpen) {
            radialOpen = down;
            onRadial(down);
          }
          break;
        default:
          if (!down) break;
          switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_B:
              if (radialOpen) {  // cancel the radial without choosing
                radialOpen = false;
                if (onRadialAim) onRadialAim(0, 0);
                if (onRadial) onRadial(false);
              } else {
                key(SDLK_ESCAPE);
              }
              break;
            case SDL_CONTROLLER_BUTTON_X: key(SDLK_r); break;
            case SDL_CONTROLLER_BUTTON_Y: key(SDLK_SPACE); break;  // find your priest
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: key(SDLK_TAB); break;
            case SDL_CONTROLLER_BUTTON_START: key(SDLK_ESCAPE); break;
            case SDL_CONTROLLER_BUTTON_BACK: key(SDLK_b); break;
            default: break;
          }
      }
      return true;
    }
    case SDL_CONTROLLERAXISMOTION:
      return true;
    default: return false;
  }
}

void Gamepad::update(double dt, SDL_Window* win) {
  if (!pad_ || !win) return;
  float lx = axis(SDL_CONTROLLER_AXIS_LEFTX), ly = axis(SDL_CONTROLLER_AXIS_LEFTY);
  if (radialOpen) {
    if (onRadialAim) onRadialAim(lx, ly);
  } else if (lx != 0 || ly != 0) {
    int w, h, mx, my;
    SDL_GetWindowSize(win, &w, &h);
    SDL_GetMouseState(&mx, &my);
    if (cx_ < 0 || std::fabs(cx_ - mx) > 2 || std::fabs(cy_ - my) > 2) cx_ = float(mx), cy_ = float(my);  // mouse moved
    float speed = 0.9f * float(std::max(w, h));  // full screen width in a bit over a second
    cx_ = std::clamp(cx_ + lx * speed * float(dt), 0.0f, float(w - 1));
    cy_ = std::clamp(cy_ + ly * speed * float(dt), 0.0f, float(h - 1));
    SDL_WarpMouseInWindow(win, int(cx_), int(cy_));  // generates the motion event hover needs
  }
  float rx = axis(SDL_CONTROLLER_AXIS_RIGHTX), ry = axis(SDL_CONTROLLER_AXIS_RIGHTY);
  if (SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) rx = -1;
  if (SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) rx = 1;
  if (SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_DPAD_UP)) ry = -1;
  if (SDL_GameControllerGetButton(pad_, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) ry = 1;
  float zoom = axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) - axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
  if (onCamera && (rx != 0 || ry != 0 || zoom != 0)) onCamera(rx * 900 * float(dt), ry * 900 * float(dt), zoom * float(dt));
}

}  // namespace ns
