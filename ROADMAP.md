# Roadmap

Goal: a complete, non-janky, modern NetStorm that plays like the original,
runs natively on Windows/Linux/Steam Deck, scales cleanly to any resolution,
and has proper LAN + internet multiplayer. All game data comes from the
player's own install.

Rough effort, part-time: **4–6 months to a full playable multiplayer game.**

## M0 — Data foundation ✅
- [x] TAFF archive + cipher, all 246 scripts decrypted
- [x] Sprite database: all 4,124 frames decoded natively
- [x] `.type` parser (stats, flags, animation frames)
- [x] C++20 / SDL2 engine skeleton, camera with free zoom, texture cache
- [x] Sprite viewer, `nstool` (info / types / extract-scripts / dump-sprites)
- [x] Map sprite sets → types
- [x] Shadow rendering, base frames + overlays
- [x] Palette confirmed against in-game screenshots
- [x] Player ownership: island rims recoloured to the player colour

## M1 — The world (≈3–4 weeks)
- [x] Grid + coordinate system (16x11 cells)
- [x] Island auto-tiling for all 4 elements, undersides, test layouts
- [x] World view: tiled sky, depth-sorted buildings/units, shadows, zoom/pan
- [x] `.fort` reader: all 32 missions decode; missions load and render (M / [ ] to cycle)
- [ ] Port the terrain builder (exact home-island shapes) and the runtime fort placement
- [ ] Exact fringe rules (lit/unlit pieces), `zorder` classes
- [ ] Mouse picking and placement footprints (`foot_x/foot_y`, hotfoot ratios)

## M2 — Core simulation (≈6–8 weeks) — the hard bit
- [x] Bridge pieces: queue, rotate, legal drops, auto-tiled railings, claiming islands
- [x] Bridge AI v1 (greedy towards nearest unclaimed land)
Deterministic fixed-tick simulation (needed for lockstep netplay).
- [ ] Bridges: piece rotation, connection rules, cracking/hard variants
- [x] Storm Power economy: golems mine geysers on your land and deliver to the temple (pathing over land + bridges)
- [x] Workshops build units for Storm Power, energy check by fury, drop finished units into open sky
- [x] Priest construction: workshops, temples, altars on your own land, walks there and builds
- [ ] Exact build times, energy radius/falloff
- [ ] Production slots per workshop level, tech levels / sacrifices / altar
- [x] Shooters fire (range, hpPerSec, delayBetweenShots), need fury energy, missiles + cannon arcs
- [x] Blockers stop direct fire, cannons break enemy bridges, destruction + explosions
- [x] Unit islands fall when their unit dies (home/geyser islands are permanent); walkers on them fall
- [ ] Air units/anti-air (airrange/airdamage), fences, meltdown, exact damage model
- [ ] Flyers, balloons, walkers, vortices, spells (bombs: twister, meteor, graviton...)
- [ ] Wind/rain/thunder/sun element rules
- [x] Interim win condition: last player with buildings
- [x] Real win condition: immobilise the enemy priest (force field at 50%), capture with a golem,
      chain at your altar, your priest wards 5 runes -> enemy eliminated (scripted test: tests/test_sacrifice.cpp)
- [ ] Knowledge/altar upgrades from sacrifices, priest spells
Method: stats come straight from the `.type` scripts; behaviour is matched by
reading the decompiled engine (`tools/re`) and A/B testing against the original.

## M3 — Interface (≈3 weeks)
- [ ] Production bar, workshop slots, hotkeys (incl. 10.82's Shift+Q/W/A/S, D-key modes)
- [ ] Right-click menus, unit info, minimap, chat
- [ ] Resolution-independent UI scaling, controller support (Steam Deck)
- [ ] Settings: resolution, borderless/fullscreen, UI scale, filtering, audio

## M4 — Multiplayer (≈4 weeks)
- [ ] Deterministic lockstep over UDP (inputs only), rollback-free, with desync checksums
- [ ] LAN discovery (UDP broadcast), direct IP, host migration-safe lobby
- [ ] Internet: lightweight lobby/relay server + NAT punch-through; optional Steam later
- [ ] 2–8 players, teams, AI fill-ins, spectators, replays (free with lockstep)

## M5 — AI + single player (≈3 weeks)
- [x] Skirmish AI v1: purposeful bridging, generators at the front, production, unit drops (I = AI plays for you, --ai-both)
- [ ] Port the skirmish AI behaviour (5 difficulties in 10.82)
- [ ] Tutorials + campaign missions from the `.fort`/`.english` scripts

## M6 — Upscaled art + polish (≈3 weeks, runs in parallel)
- [ ] HD override folder: `hd/set###/frame####.png` drawn at the same world size
- [ ] Batch upscale pipeline (`nstool dump-sprites` → upscaler → HD folder),
      with team-colour masks generated from the remap tables
- [ ] Smacker video playback, sound + music, particles
- [ ] Packaging: Windows installer, AppImage, Steam Deck controller config

## Notes / decisions
- Clean room in the sense that no original code is copied: behaviour is
  reimplemented from observation + decompiled logic.
- No original assets in the repo, ever. The game asks for your install folder.
- Not trying to be wire-compatible with the community servers; ours is its own
  protocol. Could revisit later.

## Single player (done)
- Title screen using the install's own title art, campaign chapters, Easy/Normal/Hard, mission list following the original "Next Mission" chain (all 16 missions), briefings, success/failure screens with Next/Retry.
- Progress saved to the user pref dir (progress.txt); completed missions marked with *.
- ESC in game opens pause menu (Resume / Restart / Quit to menu). Mission HUD shows title + players in their colours.
- Scripted events: timed notes ([@N]), AI priest captured/dead/saved, AI temple half/dead, [Failed]/[Succeeded] combos end the mission with their own text. Notes are modal popups with the original buttons (Tell chains, LeaveBattle = surrender/defeat, MissionAbort, MissionRestart); notes with no harmless button get an Ignore option. Enter/Esc/A = harmless choice, 1-9 picks.
- Missing: tutorials (no forts), tech unlocks via sacrifice.

## AI v2 (campaign-capable)
- Energy rule from the original help: Sun units run on any fury's energy; wind/rain/thunder units need their own fury's generators.
- Priest builds the workshops its Knowledge actually needs (best attack unit's fury + a generator fury that can power it) instead of its temple's fury.
- Production no longer deadlocks waiting for a generator it can't make; a generator-only workshop feeds starved ones.
- Finished units that can't be placed yet are kept (they were being thrown away), production pauses while 2 are waiting.
- Attack bridging: once there's no neutral land left, bridge towards open sky within range of enemy buildings.
- Approximation: temples raise a replacement golem every 30 s up to the mission's collector count (golems have no cost in the data, so the original's replacement rule is still unknown).
- AI-vs-AI results: TheWarBegins won ~10 min, GuardMyBack won ~25 min, RainVsRain lost ~9 min, skirmish ~6.5 min.

## Air units
- Aviaries (sun/rain/wind) launch their fury's flyer (sunFlyer/rainFlyer/windFlyer) at enemies within the aviary's range, one airborne per aviary, relaunch after delayBetweenShots (default 6 s; wind 29 s).
- Flyers fly over everything, circle and strafe their target (hpPerSec), retarget within their aviary's reach, die when lifeSpan runs out or their aviary is destroyed.
- Archers and aviaries can hit flyers; cannons can't. Flyers don't occupy ground cells.
- Drawn in 8 directions, ~28px up, with their ground shadow.
- Exact flyer speed scale is estimated (data speed x4 cells/s).
- Thunder aviary ("Thunder Spire") is cut content in the original: its type has no cost/hp/range/fury and nothing references it, so it stays unbuildable.

## Knowledge from sacrifice (skirmish)
- Start with every buildable unit that has no techBit in the data (Disc Throwers, Sun Cannons, Sun generator...); everything else is earned.
- A finished sacrifice offers a gift: Knowledge of a unit up to the altar's level (consumes the altar), or upgrading the altar (max level 3). Picked from a "Gift of the Furies" panel; the AI picks its fury's best shooter, else its generator, else upgrades.
- Campaign missions keep their own fixed Knowledge lists.
- AI fixes found on the way: aim drops at buildings (not wandering golems), push bridges deeper when the foothold is too thin, send the nearest golem (even mid-haul) to capture, priests finish half-built buildings.
- tests/test_knowledge.cpp covers the rules.

## Balloons (aerial transports)
- Sun Balloon / Air Ship / Cloud Floater do a golem's jobs by air: fly straight (no bridges), mine geysers we hold or unclaimed ones, carry captured priests. Built at their fury's workshop, leave from the temple. Only anti-air (archers, aviaries' flyers) can hit them. Drawn floating above their shadow.
- AI buys one when it knows how and free geysers lie out of reach.
- tests/test_air.cpp: golem can't cross open sky to a geyser, a balloon brings crystals back.

## Workshop slots and upgrades (skirmish rules)
- 2 production slots at level 1; building a new unit type commits a slot for good; +1 slot per upgrade, max 3 upgrades.
- Upgrade cost is an estimate (half the workshop's cost x its level) - the original showed it in-game only.
- Workshop menu shows level, slots (* committed, + would use a slot) and an Upgrade button. AI upgrades when its slots are full and it knows a better unit for that workshop.
- Campaign missions don't use slots (their unit lists can be longer than 2).
