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

1. **One breech, chamber and bore per gun.** Should be any number of chambers, each with its own bore, breech face and muzzle, placeable on any part (doubles, pepperboxes, revolver cylinders, volley guns). Parts should rotate about **any axis the player places**, not only the bore.
2. **Metal-cased, single-bullet cartridges only.** Add shotgun shells (hull, brass head, crimp, pellets or slug), rimfire, caseless, and any number of projectiles. Any chamber can be cut for any cartridge.
3. **Chamber pressure on the breech is the only power source.** Add gas ports on any bore that drive pistons the player draws (long stroke, short stroke and direct impingement all emerge from the drawing).
4. **Fixed feed types** (detachable box, built-in counter, by hand). Feed devices should be building blocks: box or drum, tube, belt, cylinder, clips. The parts that move rounds (lifters, pawls, carriers) are always drawn by the player. Allow several per gun.
5. **One- or two-handed classification** from grip and stock markers the player places.
6. **Rebuild and polish the examples** on these blocks, with no special code. Feeding has been made robust: the bolt action's ramp is longer (all bullet shapes feed when worked by hand), and the built-in cartridges now have real shapes. Still open: on the tilting-barrel pistol, racking out a live round is a knife-edge (26 of 30 shape and nose-length combinations), because a live round only leaves the extractor once it swings past 18 degrees (`cartridges.cpp`). A physics fix there would help every design, not just the example.

Hard limit to keep in mind: the physics works in the side-view plane plus depth. Things that move *across* the gun (swing-out cylinders, side-mounted magazines, cross-bolt safeties) need a simplified treatment. Do these last, and be upfront about it.

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
- Blueprints are JSON. Older blueprints must keep loading and behaving the same: give new fields defaults that reproduce the old behaviour.
