// NetStorm Port -- clean-room reimplementation of NetStorm: Islands at War.
// Reads all game data from the user's own installation.
//
//   netstorm --data <install-dir>                 title screen / campaign
//   Test hooks: --mission <fort> | --campaign <id> | --sprites | --set N
//               --autobuild N --simulate SECONDS --demo --ai-both
//               --screenshot out.bmp [--menu title|chapters|missions|brief]
#include <SDL.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "app/Frontend.h"
#include "app/Viewer.h"
#include "app/WorldView.h"
#include "data/GameData.h"
#include "data/MissionInfo.h"
#include "render/Renderer.h"

namespace {
void printStats(ns::WorldView& world, float before, double simulate, const ns::GameData& gd) {
  auto* sim = world.sim();
  if (!sim) return;
  auto& p = sim->player(world.player());
  if (!p.ready.empty()) {
    bool dropped = false;
    for (int y = 2; y < 200 && !dropped; ++y)
      for (int x = 2; x < 200 && !dropped; ++x)
        if (sim->canDropUnit(gd.registry.byName(p.ready[0]), x, y, world.player()))
          dropped = sim->dropUnit(world.player(), 0, x, y);
    std::printf("dropped ready unit: %s\n", dropped ? "yes" : "no");
  }
  int counts[9] = {0};
  for (auto& e : sim->entities())
    if (e.alive && e.owner > 0 && e.owner <= 8) counts[e.owner]++;
  std::printf("alive entities per player:");
  for (int i = 1; i <= 8; ++i)
    if (counts[i]) std::printf(" p%d=%d", i, counts[i]);
  for (int i = 1; i <= 8; ++i)
    if (counts[i])
      std::printf("\n  p%d power=%.0f delivered=%d ready=%zu", i, sim->player(i).power, sim->player(i).delivered,
                  sim->player(i).ready.size());
  std::printf("\n%s", world.debugAi().c_str());
  std::printf("\n  winner=%d outcome(you)=%d\n", sim->winner(), world.outcome());
  for (auto& ev : sim->events()) std::printf("  %s\n", ev.c_str());
  std::printf("simulated %.0fs: player %d storm power %.0f -> %.0f, crystals delivered %d\n", simulate, world.player(),
              before, p.power, p.delivered);
}
}  // namespace

int main(int argc, char** argv) {
  std::string dataDir = ".", shot, mission, campaign, menuShot;
  int startSet = -1, autobuild = 0;
  double simulate = 0;
  bool demo = false, aiBoth = false, sprites = false, stopAtNote = false;
  int stopAtAir = 0;
  bool testGift = false;
  bool stopAtBalloon = false;
  bool validate = false, json = false, listMissions = false;
  double hashEvery = 0;
  ns::WindowConfig wc;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--data") dataDir = next();
    else if (a == "--set") startSet = std::atoi(next().c_str()), sprites = true;
    else if (a == "--sprites") sprites = true;
    else if (a == "--fullscreen") wc.fullscreenDesktop = true;
    else if (a == "--screenshot") shot = next();
    else if (a == "--menu") menuShot = next();
    else if (a == "--mission") mission = next();
    else if (a == "--campaign") campaign = next();
    else if (a == "--autobuild") autobuild = std::atoi(next().c_str());
    else if (a == "--simulate") simulate = std::atof(next().c_str());
    else if (a == "--demo") demo = true;
    else if (a == "--stop-at-note") stopAtNote = true;
    else if (a == "--test-gift") testGift = true;
    else if (a == "--stop-at-balloon") stopAtBalloon = true;
    else if (a == "--validate") validate = true;
    else if (a == "--list-missions") listMissions = true;
    else if (a == "--json") json = true;
    else if (a == "--hash-every") hashEvery = std::atof(next().c_str());
    else if (a == "--stop-at-air") stopAtAir = std::atoi(next().c_str());
    else if (a == "--ai-both") aiBoth = true;
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
  if (listMissions) {  // id <tab> title, for test scripts
    for (auto& [name, bytes] : gd.archive.entries()) {
      if (name.size() < 9 || name.compare(name.size() - 8, 8, ".english") != 0) continue;
      std::string id = name.substr(name.rfind('/') + 1);
      id = id.substr(0, id.size() - 8);
      ns::MissionInfo m = ns::loadMission(gd, id);
      if (gd.archive.find("d/" + m.fort + ".fort")) std::printf("%s\t%s\n", id.c_str(), m.title.c_str());
    }
    return 0;
  }
  if (!shot.empty()) wc.hidden = true, wc.vsync = false;

  ns::Renderer r;
  if (!r.init(wc, &err)) {
    std::fprintf(stderr, "video init failed: %s\n", err.c_str());
    return 1;
  }
  SDL_SetWindowTitle(r.window(), "NetStorm");
  ns::Viewer viewer(r, gd);
  if (startSet >= 0) viewer.setSet(startSet);
  ns::WorldView world(r, gd);
  ns::Frontend front(r, gd);

  enum class State { Menu, Game, Sprites } state = sprites ? State::Sprites : State::Menu;
  std::string currentMission;  // campaign mission being played ("" = skirmish/test)
  auto startCampaign = [&](const std::string& id) {
    const ns::MissionInfo* m = front.mission(id);
    if (!m || !world.startMission(*m)) {
      std::fprintf(stderr, "couldn't start mission %s\n", id.c_str());
      return false;
    }
    currentMission = id;
    state = State::Game;
    return true;
  };

  // Test hooks / direct starts.
  if (!mission.empty()) {
    if (!world.loadMissionByName(mission)) std::fprintf(stderr, "mission '%s' not found\n", mission.c_str());
    state = State::Game;
  }
  if (!campaign.empty()) startCampaign(campaign);
  if (!menuShot.empty()) {
    if (menuShot == "chapters") front.handle(SDL_Event{});  // no-op
    SDL_Event e{};
    e.type = SDL_MOUSEBUTTONUP;
    e.button.button = SDL_BUTTON_LEFT;
    // Navigate by clicking through: title -> campaign -> first chapter -> first mission.
    auto clickFirst = [&]() {
      int sw, sh;
      r.outputSize(sw, sh);
      (void)sh;
      e.button.x = sw / 2;
      e.button.y = 0;
      for (int y = 0; y < 2000; y += 4) {  // find the first button by scanning down the centre
        e.button.y = y;
        SDL_Event m{};
        m.type = SDL_MOUSEMOTION;
        m.motion.x = e.button.x;
        m.motion.y = y;
        auto before = front.screen();
        auto act = front.handle(e);
        if (front.screen() != before || act.kind != ns::FrontendAction::None) return act;
      }
      return ns::FrontendAction{};
    };
    if (menuShot != "title") clickFirst();                                         // -> chapters
    if (menuShot == "missions" || menuShot == "brief") clickFirst();               // -> missions
    if (menuShot == "brief") clickFirst();                                         // -> briefing
    state = State::Menu;
  }
  if (aiBoth) world.setAiForMe(true);
  if (testGift) world.debugGrantGift();
  if (autobuild > 0) std::printf("auto-built %d bridge pieces\n", world.autoBuild(autobuild));
  if (demo) std::printf("demo: started building %s\n", world.demoProduction().c_str());
  int violations = 0;
  double endTime = 0;
  std::vector<std::string> firstViolations;
  if (simulate > 0) {
    float before = world.sim() ? world.sim()->player(world.player()).power : 0;
    double nextCheck = 1, nextHash = hashEvery;
    for (double t = 0; t < simulate; t += 1.0 / 30) {
      world.update(1.0 / 30);
      endTime = t;
      if (validate && world.sim() && t >= nextCheck) {
        nextCheck += 1;
        for (auto& v : world.sim()->validate()) {
          ++violations;
          if (firstViolations.size() < 10) firstViolations.push_back("[" + std::to_string(int(t)) + "s] " + v);
        }
      }
      if (hashEvery > 0 && world.sim() && t >= nextHash) {
        nextHash += hashEvery;
        std::printf("HASH %.0f %016llx\n", world.sim()->time(), (unsigned long long)world.sim()->stateHash());
      }
      if (world.outcome() != 0) {
        std::printf("game over after %.0fs: %s\n", t, world.outcome() > 0 ? "WON" : "LOST");
        break;
      }
      // Headless runs: click the first button on any scripted note.
      SDL_Event k{};
      k.type = SDL_KEYDOWN;
      k.key.keysym.sym = SDLK_RETURN;  // picks the harmless option
      if (stopAtNote && world.hasDialog()) break;
      if (stopAtBalloon && world.balloonsAirborne() > 0 && t > 20) {
        static double seen = t;
        if (t - seen > 12) {  // let it get underway
          world.focusOnBalloon();
          break;
        }
      }
      if (stopAtAir > 0 && world.flyersAirborne() >= stopAtAir) {
        world.focusOnAir();
        break;
      }
      while (world.hasDialog()) {
        std::printf("  [%5.0fs] note: %s\n", world.sim()->time(), world.dialogSummary().c_str());
        world.handle(k);
      }
    }
    printStats(world, before, simulate, gd);
    for (auto& v : firstViolations) std::printf("VIOLATION %s\n", v.c_str());
    if (json && world.sim()) {
      auto* sim = world.sim();
      std::string players;
      for (int p = 1; p <= 8; ++p) {
        int n = 0, buildings = 0;
        for (auto& e : sim->entities()) {
          if (!e.alive || e.owner != p) continue;
          ++n;
          buildings += e.role != ns::Role::Golem && e.role != ns::Role::Priest && e.role != ns::Role::Flyer;
        }
        if (!n) continue;
        char buf[200];
        std::snprintf(buf, sizeof buf,
                      "%s\"p%d\":{\"units\":%d,\"buildings\":%d,\"power\":%.0f,\"delivered\":%d,\"out\":%s}",
                      players.empty() ? "" : ",", p, n, buildings, sim->player(p).power, sim->player(p).delivered,
                      sim->eliminated(p) ? "true" : "false");
        players += buf;
      }
      std::printf("RESULT {\"mission\":\"%s\",\"outcome\":%d,\"seconds\":%.0f,\"violations\":%d,"
                  "\"hash\":\"%016llx\",\"players\":{%s}}\n",
                  !campaign.empty() ? campaign.c_str() : "skirmish", world.outcome(), endTime, violations,
                  (unsigned long long)sim->stateHash(), players.c_str());
    }
    if (validate && violations) return 3;
  }

  auto drawFrame = [&]() {
    if (state == State::Sprites) viewer.draw();
    else if (state == State::Game) world.draw();
    else {
      if (front.screen() == ns::Frontend::Screen::Pause) world.draw();
      front.draw();
    }
  };
  if (!shot.empty()) {
    drawFrame();
    bool ok = r.screenshot(shot);
    std::printf("%s %s\n", ok ? "wrote" : "FAILED to write", shot.c_str());
    return ok ? 0 : 1;
  }

  bool running = true;
  Uint64 last = SDL_GetPerformanceCounter();
  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_TAB && state != State::Menu) {
        state = state == State::Sprites ? State::Game : State::Sprites;
        continue;
      }
      if (state == State::Sprites) {
        viewer.handle(e);
        if (viewer.wantsQuit()) running = false;
      } else if (state == State::Game) {
        world.handle(e);
        if (world.menuRequested) {
          world.menuRequested = false;
          world.paused = true;
          front.showPause();
          front.handle(SDL_Event{});
          state = State::Menu;
        }
      } else {
        auto act = front.handle(e);
        switch (act.kind) {
          case ns::FrontendAction::Play: startCampaign(act.mission); break;
          case ns::FrontendAction::Skirmish:
            world.buildTestScene(1);
            currentMission.clear();
            state = State::Game;
            break;
          case ns::FrontendAction::Resume:
            world.paused = false;
            state = State::Game;
            break;
          case ns::FrontendAction::Restart:
            world.paused = false;
            if (!currentMission.empty()) startCampaign(currentMission);
            else world.buildTestScene(1), state = State::Game;
            break;
          case ns::FrontendAction::QuitToMenu:
            world.paused = false;
            front.showTitle();
            break;
          case ns::FrontendAction::Quit: running = false; break;
          default: break;
        }
      }
    }
    Uint64 now = SDL_GetPerformanceCounter();
    double dt = double(now - last) / SDL_GetPerformanceFrequency();
    last = now;
    if (state == State::Game) {
      world.update(dt);
      int oc = world.outcome();
      if (oc != 0 && !currentMission.empty()) {
        front.showResult(currentMission, oc > 0, world.resultText());
        state = State::Menu;
      } else if (world.restartRequested && !currentMission.empty()) {
        startCampaign(currentMission);
      } else if (world.abortRequested) {
        world.abortRequested = false;
        front.showTitle();
        state = State::Menu;
      }
    } else if (state == State::Sprites) {
      viewer.update(dt);
    }
    drawFrame();
    SDL_RenderPresent(r.sdl());
  }
  return 0;
}
