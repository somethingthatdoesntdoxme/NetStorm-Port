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
- [ ] `.fort` parser → load a mission layout and render it
- [ ] Exact fringe rules (lit/unlit pieces), `zorder` classes
- [ ] Mouse picking and placement footprints (`foot_x/foot_y`, hotfoot ratios)

## M2 — Core simulation (≈6–8 weeks) — the hard bit
Deterministic fixed-tick simulation (needed for lockstep netplay).
- [ ] Bridges: piece rotation, connection rules, cracking/hard variants
- [ ] Priest, temples, geysers, storm power economy, collectors (golems)
- [ ] Workshops, unit production queue, tech levels / sacrifices / altar
- [ ] Shooters, barricades, missiles and arcs, damage, hp, destruction, falling islands
- [ ] Flyers, balloons, walkers, vortices, spells (bombs: twister, meteor, graviton...)
- [ ] Wind/rain/thunder/sun element rules
- [ ] Win conditions: capture + sacrifice the enemy priest
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
