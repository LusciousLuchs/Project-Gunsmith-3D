# Gunsmith3D: read this first

## The vision

Gunsmith3D is a firearm design sandbox, with a career mode to come later. The designer is a **blank workshop, not a catalogue of gun types**.

- **Anything a player can imagine should be buildable**, however impractical or "cursed": a shotgun-shell pistol, a bolt-action machine gun, a revolver with three barrels. If it can be drawn, it should be possible.
- **The game never decides what a gun "is".** The only classification is **one-handed or two-handed**. How a gun feeds, locks, fires and handles comes entirely from the parts the player made.
- **Usefulness is the player's problem.** Bad designs are allowed. They just perform badly: they kick hard, jam, or are unwieldy. Never block a design for being impractical.
- **Examples are inspiration, not templates.** The example guns (blowback pistol, tilting barrel, bolt action...) show what's possible and should be polished. But nothing in the code may be special-cased around them, and no feature should assume they're the kinds of firearm that exist.

An earlier version of this project was built as if the examples were the only kinds of gun. That was a misunderstanding of the intent. Don't repeat it.

## Design principles

- **Simulate physical primitives, never gun types.** Mechanisms should emerge from parts, joints, springs, contacts, pressure and cartridges, not from code for "a revolver" or "a pump action".
- **What players must design vs. what can be abstracted** (the owner's rule):
  - Always real, designed parts: **feeding, the bolt and breech, and extraction**.
  - Optional abstraction, with an advanced drawn alternative: **fire control** (trigger, hammer or striker, sear, disconnector). Built-in fire control still has to be cocked by the action.
  - Fine detail that's tiny, hidden or fiddly to draw (feed lips, magazine springs) becomes **settings with good defaults that have real consequences** in the physics. Don't add a setting that does nothing: measure its effect first, and if it has none, leave it out and say so.
- **Unreliable designs jam for real.** On the bench through the physics, and later in live fire by rolling against the gun's measured jam rate. Clearing a jam means running the action.

## Known assumptions still to remove

These are what currently lock the designer. They are the roadmap, roughly in this order:

1. **Several chambers.** Done: `BreechDef::more` (further chambers, each with its own breech part, chamber part, face and muzzle) and `fireOrder` (turn or together). In the simulation, chamber 0's state lives in Sim's usual fields and `useChamber(S, k)` swaps another in (`Sim::chambers`); rounds carry `slot`. The over-under derringer example (hinged, tips up). Still to do: revolver cylinders and pepperboxes (chambers that index round to one barrel: parts turning about **any axis the player places**, not only the bore; out of the side-view plane, so a simplified treatment), a magazine feeding a chamber other than the first, and a drawn striker per chamber.
2. **Kinds of ammunition.** Done: `Cartridge::kind` (centerfire, rimfire, shotshell, caseless), `projectiles`, pellets (`shotDia`), brass head and hull colour; the pump shotgun example. Rimfire only matters to a drawn striker (it must hit the rim); caseless leaves no case. Still to do: any chamber cut for its own cartridge (comes with item 1), and pellets as separate projectiles once the firing range exists.
3. **Chamber pressure on the breech is the only power source.** Add gas ports on any bore that drive pistons the player draws (long stroke, short stroke and direct impingement all emerge from the drawing).
4. **Feed devices as building blocks.** Done so far: any part can be a removable or built-in magazine, with a drawn round path (side by side or nose to tail) and a shaped insertion path (bends, rock-in tilt); tubes feed physically (the spring pushes each round out of the mouth as a real round, onto whatever the player drew: see the pump-action example, with its lifter, action lock and lost motion). Belts: `BodyDef::feedPawl`/`pawlStroke`, the pawl catches the next link at the end of its stroke and pushes the belt on its way back, only into an empty tray (belt-fed blowback example; the belt itself is drawn across the gun, out of plane). Clips: `clip` stripper/enbloc (`fillMagazines`, `Ev::ClipOut`); the clip itself isn't a physical part yet. Still to do: revolver cylinders (with item 1), and several feed devices per gun (the simulation uses the first latched or built-in magazine).
5. **How it's held.** Done: `Design::pose` (`HandHold` firing, support, butt on parts; `BodyHold` aim, hip, lowered: eye in gun coordinates, body yaw, muzzle tilt; `holdOf()` fills unposed holds), `heldWith()`. Edited in the bench's Pose mode (`app/pose.cpp`: upper-body mannequin, two-bone IK arms that go red when short). The owner chose: the pose replaces the designer's grip markers (old `grips` load into the pose), three holds posed separately, an upper-body mannequin. Next (dev25, then alpha 0.2): first-person shooting using the pose, Tarkov-like weighty handling, holstering.
6. **Rebuild and polish the examples** on these blocks, with no special code. Feeding has been made robust: the bolt action's ramp is longer (all bullet shapes feed when worked by hand), and the built-in cartridges now have real shapes. Still open: on the tilting-barrel pistol, racking out a live round is a knife-edge (26 of 30 shape and nose-length combinations). Measured cause: the rim meets the ejector only at the very end of the slide's travel, so a long live round is tipped about 14 degrees (it needs 18 to be let go, `cartridges.cpp`) before the slide bounces off its stop and pushes it back in. Tried and rejected: letting rounds go at the same front-end offset as a case (12/30: released low, they miss the port) and moving the ejector forward (it catches the feeding round: no feeding at all). The real fix is reworking that example's slide travel and port, not the physics.

Round (turned) parts (`src/core/round.cpp`, `latheMesh` in `app/gfx.cpp`): the outline spun about an axis, with sides, turn and sweep; the physics uses the same depth slabs as carving, each the round shape's outline at that depth.

Carving (`src/core/carve.cpp`, `csg.cpp`, `app/carvetool.cpp`): parts can be cut in 3D from any direction. The display cuts the real solid (own BSP code; adding an outside geometry library was blocked). The physics slices a carved part through its depth into up to 16 welded slabs, each the outline minus the cuts there. Not yet in the physics: cuts on magazines and on parts that turn on the bore.

Hard limit to keep in mind: the physics works in the side-view plane plus depth. Shapes can now be fully 3D (carving), but parts still only move in that plane. Things that move *across* the gun (swing-out cylinders, side-mounted magazines, cross-bolt safeties) need a simplified treatment. Do these last, and be upfront about it.

Later: gun spec sheet from bench data, then firing range mode (first person, ballistics, live-fire jams), then the move from raylib/OpenGL to Vulkan (SDL3 GPU API; the owner wants Vulkan and no licensing fees), then career mode.

## Working with the owner

- The owner is the **game designer, not a software engineer**, and is new to GitHub. Talk in game terms, keep replies short and about what changed in the game, and make the technical decisions yourself. Only ask about real design choices.
- Be honest about limits and results. Measure before claiming something works (reliability sweeps on the examples, before and after).
- At the end of a piece of work: commit, push, and send a **Windows build zip** (the owner plays on Windows) plus screenshots where they help.

## Technical notes

- C++17, CMake. Layers: `src/core` (physics, designs, cartridges, reliability, maps; no graphics) → `src/app` (raylib rendering, editor, bench) → `src/ui` (Dear ImGui panels) → `src/main.cpp`. See README "Source layout".
- Headless core and tests: `cmake -S . -B build && cmake --build build -j && ./build/gunsmith3d_tests`
- Native Linux app for screenshots (developer only): `-DGUNSMITH_NATIVE_APP=ON`, then run under `xvfb-run` with `LIBGL_ALWAYS_SOFTWARE=1`. `--frames N --shot file.png` takes a screenshot (saved in the working directory). See `src/main.cpp` for `--preset`, `--select`, `--test`, `--fire`, `--side`, `--tutorial N`, `--mapedit`, and the `GUNSMITH_TAB` variable.
- Windows build from Linux: MinGW-w64 (`g++-mingw-w64-x86-64-posix` from apt) with a toolchain file setting `CMAKE_SYSTEM_NAME Windows` and the `x86_64-w64-mingw32-*-posix` compilers. The project's `cmake-mingw-toolchain.cmake` expects llvm-mingw instead.
- Example guns live in `src/core/presets.inc`, generated by `tools/make_presets.py`. Edit the script, not the .inc.
- Player files: `user/guns`, `user/maps`, `user/textures` in the game folder (`app/paths.cpp`; old `blueprints/` and `maps/` are moved over on start). Textures: `app/usertex.cpp`; the Windows file box is in `app/sysfiles.cpp` (kept apart from raylib because of windows.h name clashes). Map materials named `user:<file>` are the player's pictures.
- Blueprints are JSON. Older blueprints must keep loading and behaving the same: give new fields defaults that reproduce the old behaviour.
