# Gunsmith3D

Version 0.1.0-dev (working towards 0.1 alpha). See [CHANGELOG.md](CHANGELOG.md).

A gunsmithing sandbox. Design firearm actions part by part, join them with pins, tracks, springs and welds, then fire and watch the mechanism cycle in slow motion. Nothing is animated by hand: every part, every round and every empty case is a physical object, so the action works, or jams, because of the geometry you draw. It's the groundwork for a shooter where the guns you design are only as reliable as your design.

A desktop app in C++ with raylib (3D) and Dear ImGui (editor panels). It is built for Windows as a single .exe with no installer. On Linux, run that same .exe with Wine-GE or Proton-GE.

## Run it

- **Windows 10/11:** run `gunsmith3d.exe`. It is unsigned, so SmartScreen may ask you to confirm the first time.
- **Linux (Wine-GE or Proton-GE):** there is no separate Linux build; the Windows .exe runs under Wine.
  - *Proton-GE through Steam:* Add a Game > Add a Non-Steam Game > browse to `gunsmith3d.exe`. Then Properties > Compatibility > force *GE-Proton* (install it with ProtonUp-Qt if it isn't listed).
  - *Wine-GE directly, or through Lutris or Bottles:* `wine gunsmith3d.exe` from the folder it's in. Point Lutris or Bottles at the .exe with a Wine-GE runner.
  - It draws with OpenGL 3.3, which Wine passes straight to your system driver, so DXVK or VKD3D aren't involved. Autosaves and blueprints are written next to the .exe, so keep it in a folder you can write to.

Your work autosaves to `gunsmith3d_autosave.json` next to where you start it. Named saves go in `blueprints/`. If you used it when it was called Action Lab 3D, your last autosave is picked up the first time you run it.

## The menu

The game opens on the main menu:
- **Sandbox** opens the designer, with your last design.
- **Test bench** puts any gun on the bench: your sandbox design, an example, or a saved blueprint.
- **Map editor** builds your own ranges and lets you walk them in first person (see below).
- **Settings.**
- **Career** and **Firing range** are greyed out until they exist.

Esc in the workshop opens a small menu: resume, settings, back to the main menu, quit.

## Settings

Saved to `gunsmith3d_settings.json` next to the game. None of them change how a gun behaves: the simulation always runs at full rate with the same rules. If your computer can't keep up, the bench slows down rather than cutting corners.
- **Display:** windowed, borderless or fullscreen, resolution, monitor (with a 15-second "keep these settings?" check), vertical sync, frame-rate limit, anti-aliasing, interface scale.
- **Workspace:** the background behind the gun (Blueprint, Drafting paper, Dark workshop, Daylight studio or a solid colour of your choice) and the drafting grid. The palette button in the top bar changes it on the spot.
- **Audio:** master, gunfire, mechanical and interface volume; mute in the background.
- **Interface:**
  - Metric or Imperial units everywhere a number has units (mm or in, g or grains for bullets, g or oz for parts, m/s or ft/s, J or ft·lbf, N or lbf, N/mm or lb/in).
  - How much statistics detail the bench shows: Minimal (the default), Standard or Engineering.
  - Tooltips, how-to hints, colour-blind-friendly colours.
- **Designer:** default snap, autosave interval, which mouse button orbits, zoom speed and direction.
- **Test bench:** normal speed, slow motion on each shot (how slow, how long), default rounds for the reliability test.
- **Controls:** every key can be rebound.

## Your files

Everything you make lives in the game folder, under *user*: blueprints in *user/guns*, maps in *user/maps*, and your own pictures in *user/textures*. Copy the *user* folder into a new version's folder to bring your work along. Ctrl+S saves the gun from anywhere.

Textures: put .png, .jpg, .bmp or .tga pictures in *user/textures*, click *Add a picture...*, or drag pictures onto the game window. A part's *Surface* section puts one on the part (the part's colour tints it); the map editor lists them after its built-in materials.

## Tutorials

**Tutorials** on the main menu (or the graduation-cap button next to the menu button) has sixteen short guided lessons:
- getting around
- **build your first gun:** from a blank frame to a single-shot that fires, throws out its case and is reloaded by hand. You draw a barrel, a heavy bolt on a track and an ejector, then set up the firing system. Every step has a *Do it for me* button that makes the piece where it fits what you've already drawn.
- drawing and shaping parts (corners, edges, the 32-corner limit, typing exact numbers)
- **carving in 3D:** cut holes, slots and pockets into a block from the side and from the top, change a cut, and make one go right through
- thickness, depth and collisions
- joints
- springs
- breech, barrel and cartridge
- feeding and magazines (removable and built-in magazines, round paths)
- tube magazines and lifters (the pump-action example: the tube, the lifter, free play and the action lock)
- shotshells, rimfire and caseless rounds (the pump-action shotgun example)
- several chambers: doubles and volley guns (the over-under derringer example, which tips up on a hinge)
- belts moved by a pawl you choose (the belt-fed blowback example), and stripper or en-bloc clips
- extraction and ejection
- fire control
- locking and operating systems
- the test bench
- reliability testing
- building a range in the map editor

Each lesson opens an example to work on (Undo brings your own design back) and walks through it one small step at a time:
- A glowing outline points at the button or setting each step is about.
- Steps about a part select it, so you can see it.
- When a step asks you to do something, it moves on by itself once you've done it.
- You can always go back or skip. Finished lessons are remembered.

## Two modes

- **Build** is the workshop: draw and shape parts, join them, set up springs, controls and the firing system. Nothing moves.
- **Test** is the bench: the action runs in real time and you work it by hand. Left-drag any moving part to rack slides, work bolts and pull magazines. Each shot drops into slow motion for a moment so you can watch the cycle, then returns to real time. Your design is never changed by testing.

Press **Tab** (or the Designer / Test bench buttons at the top) to switch.

## Controls

| | |
|---|---|
| Right-drag | Orbit the camera |
| Middle-drag | Pan |
| Scroll | Zoom |
| V / F / X / C | Side view, fit view, X-ray, cutaway |
| Tab | Switch Build and Test |
| **Test mode** | |
| Left-drag | Grab and move any moving part |
| Space / FIRE | Pull the trigger (hold for full-auto designs) |
| M | Press the magazine release |
| R / N | Quick reload / put a fresh magazine on the bench |
| L | Put a loose round on the bench |
| T | Top up every magazine |
| 1-4 | Press your own controls |
| P / Backspace | Pause / restart |
| **Build mode** | |
| Del, Ctrl+Z, Enter, Esc | Delete, undo, finish a part or track, cancel |
| Ctrl+D | Duplicate the selected part |
| K | Knife |
| Arrow keys | Nudge the selected vertices, or the whole part, by one snap step (Shift: x10) |
| Shift-click / box-drag | Pick several vertices, then drag one to move them together |
| Drag / Shift-drag a diamond | Move an edge / extrude it |
| F12 | Save a screenshot |

## The designer panel

The panel on the right has four tabs:
- **Inspect:** whatever is selected. Its settings are grouped (Part, Shape, Motion, Control, Magazine, Advanced), and the groups you don't need stay folded.
- **Outline:** every part and joint, with a filter.
- **Firing:** breech and barrel, cartridge, feeding and firing.
- **File:** save, open, and start from an example.

In the view, click anything (a part, a corner, an edge, a spring end, a track point, a pin, the breech face or muzzle, a face, a hand in Pose) to pick it, then drag it to move it: the first click never moves anything. Drag a number sideways to scrub it, or click it without dragging to type an exact value. Sliders take a double-click (or Ctrl+click) to type. Joints that belong to the selected part are drawn clearly in the view; the rest stay faint until you hover them.

## Building blocks

**Parts** are drawn in side elevation and extruded to their thickness. The editor works like Sprocket's: start from a Rect, Circle or Polygon and shape it.
- *Vertices* (squares): click one to pick it (its X/Y show in the panel), then drag it to move it. Shift-click or box-drag to pick several and move them together, or type an exact X/Y. Double-click an edge to add one.
- *Edges* (diamonds), up to 32 corners per part: click one to pick it, then drag to move the whole edge, or Shift-drag to extrude it into a new step, like pulling out a rail or a lug. Right-click or double-click a diamond to split that edge in half (on its curve, if it's bowed); *Split edge* in the Shape section does the same for the selected edge.
- *Knife*: drag a line across a part to cut it into separate parts. Delete the offcut to carve a notch, or keep both halves: cut a frame block into a frame and a slide, for example. With nothing selected, it cuts every part the line crosses. Curves and rounded corners survive the cut.
- *Mirror*: drag an axis to make a reflected copy of the selected part. *Flip H / Flip V* flip a part in place, *Duplicate* copies it, *Split all edges* adds a vertex mid-way along every edge for finer shaping.
- *Curves*: select a vertex to round that corner or bow the next edge outward or inward, or round every corner at once.
- *Smoothness*: rounds the edges where a part's sides meet its front and back faces, from 0% (sharp) to 100% (round across its thickness). Use it for grips, stocks and dehorned slides. It only changes the look: the physics uses the outline.
- *Round*: in the Part section. Spins the outline around its middle line, like a part turned on a lathe, so a rectangle becomes a round bar or barrel and steps become shoulders. *Sides* turns it down to flats (4 is the bar as drawn, 8 an octagon), *Turned to* rotates the shape, *Sweep* makes a wedge such as a locking lug, and *Axis* picks level, upright or along the part. Round parts collide by their real shape on the bench.
- *Faces*: click a face of a part and drag it to push it out or in; E extrudes it. The selected part shows its corners on both faces (squares at the front, circles at the back): drag one on its own to give the front and the back different shapes, so a block can be tall at the front and low at the back. A corner moves in the plane you look at: from the side up and along, from the top along and in depth, from the front up and in depth, so the top can be a different shape from the bottom. Hold Z to move a corner in depth from any view. *Straight through again* (Shape section) undoes it. Corners on such a part are sharp; tilted and round parts can't be shaped this way yet.
- *Carve*: cut into a part in 3D. Pick the Carve tool, choose a circle, half circle, rectangle or triangle and a depth (or *Right through*), then click a part's surface: the cut goes straight in from that face. Drag instead of clicking to size it. Turn the view to cut from the top, the ends or the far side. Each cut is listed under *Carves* on the part, where you can change its shape, size, depth, aim (*Turn* and *Tilt*, for angled cuts), spin and starting point, duplicate or remove it. Cuts are real for the physics too: a part slides past a block that sits in its groove, and carving takes weight out of the part. On parts that turn on the bore and on magazines, cuts only change the look for now.
- *Snap*: off, 0.1, 0.5, 1, 2 or 5 mm, from the top bar. Undo and redo (Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z) are next to it.

The tools sit in a rail down the left of the view, grouped as *Draw* (polygon, rectangle, circle), *Shape* (knife, mirror, carve) and *Join* (pin, weld, track, spring). Hover one for what it does.

The physics uses the curved outline too, so the shape you see is the shape that collides.

**Depth and collision.** Every part has a depth position and a thickness, and parts touch only where they overlap in depth, the way real parts sit side by side. Tick *Solid* off for parts that are just for looks, such as grip panels. Layers (A, B, C) split parts further: two parts touch only if they also share a layer. Pieces at different depths, such as a slide's walls and rail lips, are separate parts welded into one rigid assembly. *Copy to the other side* mirrors a part across the gun, and the cutaway view (C) makes the near-side parts see-through.

**Joints:** pin (pivot, with a diameter); welding isn't a joint: the Weld tool makes two parts one solid piece (click one, then the other; *Unweld this part* in the part panel undoes it); track (a part sliding in a slot or rail cut in another part: click the part, then where it slides to; Shift-click adds bends), plus helical cams, locking lugs and catches for rotating bolts and long-recoil actions. A catch can *hold it closed*, as an action lock: a shot can't blow the held part back until a release part has been drawn back against it. You don't need tracks for rails: draw a groove and a tongue on the same layer and the parts ride each other, with friction.

**Springs**
- *Coil:* stiffness, free length, coil diameter, wire and turns. Compressed to its solid height it becomes a hard stop. A coil longer than four times its diameter needs a guide rod (draw a rod part and pick it), or it buckles and loses most of its force. Buckled springs show red.
- *Leaf:* a flat spring clamped to one part that pushes on another and never pulls. Use it for triggers, sears and tight spaces where a coil won't fit.

**Controls.** Mark a part as a button, lever or trigger (or *worked by hand*: the part a shooter grabs to work the action, like a pump's fore-end; the reliability test works the gun by it), bind it to a key (Space, M, 1-4), and set where the finger pushes and how hard. Join it with a pin or track and give it a spring so it returns. A trigger bound to FIRE must actually be pulled 2 mm before the gun fires. The magazine catch can be released by a button part instead of by magic.

**Fire control.** Feeding, the bolt and extraction are always parts you design. The hammer or striker can be either:
- *Built in:* nothing to draw. It still has to be cocked: the bolt or slide must open at least *Cocks after* mm between shots, or the next pull does nothing (the reliability test calls that a *failure to cock*). Choose semi-automatic, full-auto or burst, double action (the pull cocks and fires it), and a lock time.
- *Drawn parts (advanced):* the round only goes off when your striker or firing pin reaches the breech face fast enough, so your own hammer, sear, disconnector and trigger decide when it fires, and whether it's semi or full-auto.

A trigger part bound to FIRE works with either: the shot waits until it has been pulled 2 mm.

**Feeding**
- *Magazine:* any part can be one. In its Magazine settings choose:
  - *Removable:* it rides a well (the green line), latches when pushed home, and drops free under gravity when released. The insertion path can have bends, and a tilt at each point for magazines that rock in.
  - *Built in:* part of the gun, like an internal box or a tube. You load it in place: drag rounds to its feed end, or top it up (T).
- *Rounds inside:* draw a round path inside the magazine, starting where rounds are fed from. Rounds sit side by side across it (a box) or nose to tail along it (a tube). Without a path they're laid out automatically. For now a tube feeds the simple way (the round reaches the chamber once the action opens). Feeding through a carrier you draw is next.
- *Built-in supply:* a simple round counter.
- *Single shot:* nothing feeds. Load it by hand (below).

**Physical feeding.** With *Physical feeding* ticked in the firing system (all the new examples use it), every round and case is a real part with its true outline:
- The magazine holds its top round up under the lips.
- The breech face strips it forward over the feed ramp. It enters the chamber only if its nose arrives at the mouth roughly straight.
- When the breech opens, the extractor pulls it out by the rim. The rim can slide a little up and down the breech face, as it does in a real breech-face slot.
- The case pivots on the extractor until it hits the ejector you drew, then flies out through the port you cut, or doesn't.
Nothing about this is scripted. The same rules run every gun, so a design you draw yourself feeds the way its shapes say it will.

**Malfunctions.** When something goes wrong the bench says what and why:
- *Failure to feed:* the breech passes over the round, or the round nose-dives or tips up and wedges.
- *Failure to eject:* the case rides back into the chamber, or is left lying in the action.
- *Stovepipe:* a case caught in the port.
- *Double feed:* two rounds in the action at once.
- *Failure to return to battery.*
- *Light strike:* the striker or hammer falls but the round doesn't fire.
- *Failure to cock:* the built-in hammer or striker was still down when the trigger was pulled, because the action didn't open far enough.
Clear a jam the way you would on a real gun: rack the slide or work the bolt, or grab the stuck case and pull it out. Hover the jam in the Test panel for what usually causes it.

**Reliability test.** In the Test panel, pick 25 to 200 rounds and run it. It fires your design over and over with the variation real shooting has:
- ammunition a few percent hot or weak
- a clean or dirty action (more friction)
- a firm or loose grip, where the frame recoils with the slide and robs it of travel
It clears every jam the way a shooter would and reports the jam rate, one jam every how many rounds, and which kinds. It runs in the background, so the bench stays live. The same seed gives the same run, so you can change one thing and compare.

**Magazines on the bench.** Press T (or *Top up magazines* in the Bench section) to fill every magazine, in the gun and on the bench. To load one round at a time, drag a loose round onto the top of a magazine that's out of the gun. Spent cases are cleared after they've lain on the bench for 10 seconds, and a dropped magazine is cleared once it's empty and nobody has touched it for 30 seconds. Removed rounds, cases and magazines free their room, so a long session never runs out.

**Loose rounds.** Rounds are physical objects. Press L to put one on the bench, or let a hand-loaded design start with five there. Drag one into the open action, in line with the bore and in front of the breech face, and it drops into place. Close the action and the breech face pushes it into the chamber, or push it home with the mouse. Opening a loaded action pulls the round out and throws it onto the bench, so racking a loaded pistol ejects a live round just like the real thing. This works for any design with a breech face and muzzle set: bolt actions, falling blocks, break actions, or loading a single round into an open pistol.

**Bolt handles.** A part that rotates about the bore can have a handle. In Test mode, drag the handle's knob: moving it up and down turns the bolt, and moving it along the bore pulls or pushes it, so lifting and drawing a bolt is one motion. Your hand's strength limits how hard you can turn it, so locked lugs or a stiff cam really resist.

**Feed lips and magazine spring.** Under *Feed lips and spring* in a magazine's settings:
- *Lip height* raises or lowers where the top round is held. Set it too low and the breech face rides over the rim.
- *Spring strength* is the follower spring against a standard one. Each new top round starts one round down in the stack (half a round for a double stack) and the spring lifts it to the lips. A weak spring can't get it there before the slide comes back, so fast-cycling pistols and full-auto guns fail to feed, while a bolt action worked by hand barely notices.

- *Feeds from* (double-stack magazines): *single position* tapers the two columns into one, so every round feeds from the centre, straight in. The taper's friction costs about a fifth of the spring. *Double position* feeds alternately from the left and right lips. It's easy on the spring, but each round comes in at an angle sideways: fine for long rifle rounds, marginal for 9 mm, and too steep for fat short rounds like .45 ACP. (For now the side offset is drawn but doesn't collide. The examples are only built as wide as a single stack, and a properly wide action would make it count.)

The defaults fit the cartridge and feed cleanly.

**Magazine maker.** On a magazine part, tick *Shape it around the cartridge* and choose capacity, single or double stack, curve and wall thickness. The magazine is rebuilt to fit the cartridge, and its well follows. Set 100 rounds and 60° of curve if you like.

**Bullet shapes.** In the cartridge designer, pick a shape:
- spitzer
- round nose
- flat nose
- hollow point
- wadcutter
- semi-wadcutter
- round ball
- **custom**, which turns the current shape into corners you drag around the preview (double-click an edge to add a corner, right-click to remove one)

Spitzers and other pointed shapes can have a flat tip. Most shapes can have a boat tail, and you can set how deep the bullet is seated.

The shape is the round's real outline, so it's what meets the feed ramp and chamber mouth. In a gun with tight feed geometry, a sharp spitzer can fail where a blunt nose feeds. *Weight from shape* works the weight out as jacketed lead, and the designer estimates a G1 ballistic coefficient for the firing range to come. The built-in pistol rounds have round noses, and the rifle rounds spitzers (boat-tailed for the 7.62s). Older blueprints keep the classic shape they were made with.

**Cartridges.** Built in: 9x19mm, .380 ACP, .45 ACP, 5.56x45mm, 7.62x39mm and 7.62x54mmR. Open the designer from the firing system to change any dimension, bullet mass or velocity, preview the profile to scale, and save your own to `cartridges.json`. Recoil impulse is worked out from the bullet's momentum plus a share for the powder gas, and rounds are drawn as proper turned brass and copper.

## Examples

All the main examples are built from real, separately modelled parts, with no rules standing in for a mechanism. They're there to study and to copy from.
- **Blowback pistol** (.380). Frame rails and a rear stop, a fixed barrel with a feed ramp, and a slide made of welded pieces with the ejection port cut in its right wall. It has a recoil spring on a guide rod and a 7-round magazine. The striker fire control is a striker with a cocking lug, a sear, a trigger link, and a fixed connector ramp. The ramp drops the link off the sear once it has released, so holding the trigger fires once, and letting it out resets it.
- **Tilting-barrel pistol** (9 mm, Browning cam lock). The barrel's chamber block locks into the front of the ejection port. Barrel and slide recoil together for 2 mm. Then a frame cross pin, running in a slot in the barrel's cam lug, pulls the breech down off the slide and stops the barrel about 6 mm back, and the slide runs on. A short muzzle bell lets the barrel rock in the slide's bushing. Same fire control, 8-round magazine.
- **Bolt-action rifle** (5.56 mm). A turn bolt with two lugs that lock behind shoulders in the receiver ring. Lift the handle and the lugs swing clear. Closing the bolt pushes the striker's cocking piece onto the sear, so it cocks on close. There's a slot under the bolt head for a fixed ejector, feed ramps, a ported receiver wall, and a 5-round magazine.
- **Single-shot rifle:** the same action with no magazine. Lift and draw the bolt, drag a round from the bench into the action, close it and fire.
- **Long recoil (Chauchat style):** older and simplified. A cam rule and a catch stand in for part of the mechanism.
- The older simplified examples are still in the code, for the tests of those joints.

Save and open blueprints from the designer's **File** tab. There is deliberately no export or copy-as-text: designs stay inside the game.

## Build from source

Needs CMake 3.16+ and a C++17 compiler. All libraries are included in `third_party/`. The game builds for Windows.

**Windows (Visual Studio 2022)**

```bat
cmake -S . -B build
cmake --build build --config Release
build\Release\gunsmith3d.exe
build\Release\gunsmith3d_tests.exe
```

**Windows from Linux (cross-compile)** with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw). This is how the release .exe is made:

```sh
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake-mingw-toolchain.cmake -DMINGW_ROOT=/path/to/llvm-mingw
cmake --build build-win -j
```

**On Linux, natively:** this builds the physics core and its tests only. They need no graphics and check that every example still cycles and jams the way it should:

```sh
cmake -S . -B build && cmake --build build -j && ./build/gunsmith3d_tests
```

(`-DGUNSMITH_NATIVE_APP=ON` also builds the app natively for X11. That's a developer convenience for automated screenshots, not a supported way to play.)

`.github/workflows/build.yml` builds the Windows .exe and runs the core tests on GitHub Actions.

## Map editor

Maps are built the way early-2000s shooters were (Radiant, the idTech editor behind Doom 3, Medal of Honor and Call of Duty): out of **brushes**, solid convex blocks you draw, cut and texture. For now everything is static: no physics props.

- **Tools** (top bar): select, box, ramp, cylinder, stairs, clip, and the player start.
- **Drawing:** pick a shape tool and drag on the ground, or on top of another brush, to draw its footprint. The height comes from the box in the top bar. Ramps and stairs climb away from where you're looking.
- **Selecting and editing:**
  - Click a brush to select it. Shift adds to the selection, Ctrl+click picks a single face.
  - Click a brush to select it, then drag it to move it, or Alt+drag to move it up and down. Drag the little squares on its faces to resize.
  - Arrow keys and Page Up/Down nudge by one grid step.
- **Clip:** click two points on a face to cut along that line. Green shows what stays and red what goes. Tab keeps the other side, Enter cuts, Shift+Enter splits in two. This is how you make angled walls and roofs.
- **Make hollow (H):** turns a block into walls around its inside, which is the quickest way to make a room.
- **Other commands:** turn 90 degrees (R), mirror, duplicate (Ctrl+D), delete, undo and redo (Ctrl+Z / Ctrl+Y).
- **Textures:** 18 built-in materials (concrete, plywood, brick, dirt, grass, gravel, corrugated sheet, painted steel and more). Click one to put it on the selection or the selected face; each face has its own scale and offset. The textures are generated by the game, so there are no image files involved.
- **Map settings:** sun direction and height, sunlight, shade, sky and fog colours, fog distance.
- **Camera:** hold the right mouse button to look around and fly with WASD (Q/E down and up, Shift faster, the wheel sets the speed). The middle button pans and the wheel moves forward. V switches to a top-down view, F focuses on the selection.
- **Walk (F5):** drops you in at the player start, or at the camera with Shift+F5. Controls:
  - WASD to move, Shift to sprint, Space to jump
  - C to crouch, Z to go prone, Q/E to lean
  - Esc goes back to the editor

  All of these can be rebound in Settings > Controls, along with mouse sensitivity and field of view.
- **Files:**
  - Saved to `maps/<name>.gsmap`, next to the game. They're plain JSON, so they're easy to share, edit by hand or generate with a script.
  - An autosave keeps your work between sessions.
  - **Practice range** is a built-in starter map: a covered firing line, seven lanes, paper targets at 7, 15 and 25 m, steel plates at 50 and 75 m, and a backstop berm at 100 m.

## Source layout

The code is in layers. Each layer is its own library and only uses the layers above it in this list, so you can work on one without touching the others.

```
src/core/      the gun itself. No graphics, no UI. The tests use this layer alone.
  design.*       parts, joints, cartridges, magazine maker, knife/mirror geometry, blueprint JSON
  solver.*       mechanism physics: rigid bodies and XPBD constraints at 8 kHz
  cartridges.cpp rounds and cases as physical parts: lips, feed, chamber, extractor, ejection, malfunctions
  reliability.*  the reliability test (headless: shoot N rounds with variation, clear jams, count them)
  map.*          maps: convex brushes, face polygons, clip/hollow, ray and box-sweep collision, map files, the starter range
  player.*       first-person movement: Quake-style slide move, steps, jump, crouch, prone, lean
  presets.inc    the example guns (generated by tools/make_presets.py: edit that)
  version.hpp.in version info (CMake fills it in)
src/app/       the program around the gun. Uses raylib.
  app.*          shared state, undo, modes
  gfx.*          lit shader, meshes, turning outlines into 3D parts
  render.*       the 3D scene: parts, rounds, springs, magazines, effects
  overlay.*      handles and labels drawn over the 3D view
  view.*         camera, screen/world conversion, picking
  editor.*       Build mode: drawing tools and editing operations
  bench.*        Test mode: running the simulation, shot effects
  reltest.*      runs the reliability test on a worker thread
  input.*        camera controls, keyboard shortcuts
  audio.*        synthesised sounds
  scene.*        workspace backgrounds, drafting grid, bench floor
  mapgfx.*       map drawing: generated textures, brush meshes, sun and sky
  mapedit.*      the map editor and walking the map
src/ui/        the panels. Uses Dear ImGui.
  toolbar, panel_build, panel_test, cartridge_designer, panel_map, menu, common (widgets, theme)
src/main.cpp   startup and the main loop
tests/         headless checks: every example cycles, the editing tools work, blueprints load, maps and walking work
```

A new mechanic usually means a joint or rule in `core/solver`, its data in `core/design`, drawing in `app/render` (the backdrop, grid and floor are in `app/scene`), and its settings in `ui/panel_build`.

## Versions

The version is shown in the toolbar and window title, and `gunsmith3d --version` prints it. It's set once at the top of `CMakeLists.txt` (`project(... VERSION 0.1.0)` and `GUNSMITH_STAGE`). See `CHANGELOG.md` for what changed and how the numbers work.

## How the physics works

Parts move in the side-elevation plane (x, y, angle). Rotating parts also carry a roll angle about their own bore axis. Every 1/8000 s the solver predicts motion from forces (springs, chamber pressure, your hand), then corrects positions to satisfy each joint (XPBD), then pushes overlapping parts on the same layer apart. Velocities are taken from the corrected positions.

Parts also have depth: a position across the gun and a thickness. Parts only touch where they overlap in depth, so a sear can work beside a magazine, and a lug welded to a rotating bolt swings round to the side as the bolt turns. Linked pieces (a slide made of a top, two walls and rail lips) are welded into one rigid assembly. Gravity acts on every part. What the fixed parts feel is reported as felt recoil.

Limits: motion is still planar (side elevation) plus roll about the bore, with depth used for who-touches-whom. So ejection, which really spins a case out sideways, is approximated: the case pitches in the side view, and that throw is handed over to sideways motion when it leaves the extractor.

## Third-party code

raylib (zlib), Dear ImGui (MIT), rlImGui (zlib) with Font Awesome Free icons (SIL OFL 1.1), the Inter typeface (SIL OFL 1.1, Latin subset embedded in the program), nlohmann/json (MIT), X11 headers (MIT/X11, developer build only). Licences are in each folder under `third_party/`.
