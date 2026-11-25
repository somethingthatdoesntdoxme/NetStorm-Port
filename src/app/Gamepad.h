// Controller support (Steam Deck, Xbox/PlayStation pads).
//
// The pad drives a mouse cursor and is translated into ordinary mouse and key
// events, so every screen that works with a mouse works with a pad. Things a
// mouse can't express (stick panning, the radial menu) go through hooks.
//
//   Left stick   cursor            A      click          Start   menu (Esc)
//   Right stick  pan               B      back (Esc)     Select  AI lays a bridge piece (B)
//   D-pad        pan               X      rotate (R)     LB      next golem (Tab)
//   LT / RT      zoom out / in     Y      priest (Space)       RB hold radial menu
#pragma once
#include <SDL.h>

#include <functional>

namespace ns {

class Gamepad {
 public:
  ~Gamepad();
  // Feed every SDL event; returns true if the event was a pad event (consumed).
  bool handle(const SDL_Event& e, SDL_Window* win);
  // Once per frame: moves the cursor and calls the pan/zoom hook.
  void update(double dt, SDL_Window* win);
  bool connected() const { return pad_ != nullptr; }

  // Hooks (set while a battle is on screen).
  std::function<void(float dx, float dy, float zoomDelta)> onCamera;  // dx,dy in screen px
  std::function<void(bool open)> onRadial;                            // RB pressed / released
  std::function<void(float x, float y)> onRadialAim;                  // stick while radial is open
  bool radialOpen = false;

 private:
  void key(SDL_Keycode k);
  void mouseButton(SDL_Window* win, Uint8 button, bool down);
  float axis(SDL_GameControllerAxis a) const;
  SDL_GameController* pad_ = nullptr;
  SDL_JoystickID padId_ = -1;
  float cx_ = -1, cy_ = -1;  // cursor (window pixels)
};

}  // namespace ns
