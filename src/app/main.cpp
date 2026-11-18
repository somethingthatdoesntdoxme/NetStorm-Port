// NetStorm Port -- clean-room reimplementation of NetStorm: Islands at War.
// Reads all game data from the user's own installation.
//
//   netstorm --data <install-dir> [--set N] [--fullscreen] [--screenshot out.bmp]
#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "app/Viewer.h"
#include "app/WorldView.h"
#include "data/GameData.h"
#include "render/Renderer.h"

int main(int argc, char** argv) {
  std::string dataDir = ".";
  std::string shot;
  int startSet = 0;
  bool worldMode = true;
  std::string mission;
  int autobuild = 0;
  double simulate = 0;
  bool demo = false;
  ns::WindowConfig wc;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--data") dataDir = next();
    else if (a == "--set") {
      startSet = std::atoi(next().c_str());
      worldMode = false;
    } else if (a == "--sprites") worldMode = false;
    else if (a == "--fullscreen") wc.fullscreenDesktop = true;
    else if (a == "--mission") mission = next();
    else if (a == "--autobuild") autobuild = std::atoi(next().c_str());
    else if (a == "--simulate") simulate = std::atof(next().c_str());
    else if (a == "--demo") demo = true;
    else if (a == "--screenshot") shot = next();
    else if (a == "--size") {
      std::string s = next();
      std::sscanf(s.c_str(), "%dx%d", &wc.width, &wc.height);
    } else {
      std::fprintf(stderr, "unknown option %s\n", a.c_str());
      return 1;
    }
  }

  ns::GameData gd;
  std::string err;
  if (!gd.load(dataDir, &err)) {
    std::fprintf(stderr, "Couldn't load NetStorm data from '%s': %s\n", dataDir.c_str(), err.c_str());
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NetStorm",
                             ("Couldn't load NetStorm data.\n\nPoint --data at your NetStorm folder.\n\n" + err).c_str(),
                             nullptr);
    return 1;
  }
  std::printf("Loaded %zu scripts, %zu types, %d sprite sets / %d frames\n", gd.archive.entries().size(),
              gd.types.size(), gd.shapes.setCount(), gd.shapes.totalFrames());

  if (!shot.empty()) {
    wc.hidden = true;
    wc.vsync = false;
  }
  ns::Renderer r;
  if (!r.init(wc, &err)) {
    std::fprintf(stderr, "video init failed: %s\n", err.c_str());
    return 1;
  }
  ns::Viewer v(r, gd);
  v.setSet(startSet);
  ns::WorldView world(r, gd);
  if (!mission.empty() && !world.loadMissionByName(mission))
    std::fprintf(stderr, "mission '%s' not found\n", mission.c_str());
  if (autobuild > 0) std::printf("auto-built %d bridge pieces\n", world.autoBuild(autobuild));
  if (demo) std::printf("demo: started building %s\n", world.demoProduction().c_str());
  if (simulate > 0) {
    float before = world.sim() ? world.sim()->player(world.player()).power : 0;
    for (double t = 0; t < simulate; t += 1.0 / 30) world.update(1.0 / 30);
    if (world.sim()) {
      auto& p = world.sim()->player(world.player());
      // Drop the first ready unit at the first legal spot (tests the drop path).
      if (!p.ready.empty()) {
        bool dropped = false;
        for (int y = 2; y < 200 && !dropped; ++y)
          for (int x = 2; x < 200 && !dropped; ++x)
            if (world.sim()->canDropUnit(gd.registry.byName(p.ready[0]), x, y, world.player()))
              dropped = world.sim()->dropUnit(world.player(), 0, x, y);
        std::printf("dropped ready unit: %s\n", dropped ? "yes" : "no");
      }
      std::printf("simulated %.0fs: player %d storm power %.0f -> %.0f, crystals delivered %d, units ready %zu\n",
                  simulate, world.player(), before, p.power, p.delivered, p.ready.size());
    }
  }

  if (!shot.empty()) {
    if (worldMode) world.draw();
    else v.draw();
    bool ok = r.screenshot(shot);
    std::printf("%s %s\n", ok ? "wrote" : "FAILED to write", shot.c_str());
    return ok ? 0 : 1;
  }

  Uint64 last = SDL_GetPerformanceCounter();
  while (!v.wantsQuit() && !world.wantsQuit()) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_TAB) {
        worldMode = !worldMode;
        continue;
      }
      if (worldMode) world.handle(e);
      else v.handle(e);
    }
    Uint64 now = SDL_GetPerformanceCounter();
    double dt = double(now - last) / SDL_GetPerformanceFrequency();
    last = now;
    if (worldMode) {
      world.update(dt);
      world.draw();
    } else {
      v.update(dt);
      v.draw();
    }
    SDL_RenderPresent(r.sdl());
  }
  return 0;
}
