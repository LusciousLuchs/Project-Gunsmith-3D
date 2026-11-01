# Changelog

Versions are `MAJOR.MINOR.PATCH-stage`, for example `0.1.0-alpha`.

- **Stage** is how finished a version is: `dev` (in progress, things change daily), then `alpha`, `beta`, `rc` (release candidate), then no label for a full release.
- **MINOR** goes up for each release with new features while the game is in early access (0.x). **PATCH** goes up for fixes to a released version. **MAJOR** becomes 1 at the first full release.
- **Blueprint format** is a separate number saved inside every blueprint. It only goes up when older versions of the game would load a design wrongly, so blueprints keep working across most updates. Opening a blueprint from a newer version shows a warning.

The version is set in one place, at the top of `CMakeLists.txt`.

## 0.1.0-dev (in progress, towards 0.1 alpha)

0.1 alpha will be declared once the editor and the test bench are worked out.

### Editor (Build mode)
- Parts are drawn as polygons, rectangles or circles in side view and extruded to their thickness. Corners can be rounded and edges bowed, and the physics uses the curved outline.
- Sprocket-style shaping:
  - Drag vertices, Shift-click or box-select several and move them together, or type exact coordinates.
  - Drag an edge's diamond to move it, Shift-drag to extrude it.
  - The knife cuts parts into separate pieces. Mirror makes a reflected copy. Flip, duplicate (Ctrl+D) and split all edges are on the part.
  - Split one edge in half: right-click or double-click its diamond, or use *Split edge*. A bowed edge splits on its curve and keeps its shape.
  - *Smoothness* (0-100%) rounds a part's front and back edges for grips, stocks and dehorned slides. It's for looks only.
  - The tools moved to a labelled rail down the left of the view, grouped as Draw, Shape and Join, with icons only in short windows. Undo and redo sit in the top bar, and redo is new (Ctrl+Y or Ctrl+Shift+Z).
  - Arrow keys nudge (Shift for ten times the step). The snap step can be set from off to 5 mm.
- Joints: pin, weld, track (cam slots, rails, bushings), helical cam, locking lugs, catch, and magazine well.
- Springs draw much faster: each one is a single mesh that is reshaped in place, one draw call instead of about 110. On the example pistols that cuts the scene from about 625 draw calls a frame to about 70. Coils now show their real number of turns with a closed coil at each end, the wire stays round and smooth as it compresses, and springs sit at the depth of the part they push instead of on the centre line.
- Springs: coil springs need a guide rod past four diameters or they buckle, and go solid at their solid height. Leaf springs suit tight spaces.
- Controls: buttons, levers and triggers bound to keys. A trigger must actually travel before the gun fires, and a magazine catch can be released by a drawn button.
- Collision layers (A, B, C, and combinations) decide which parts touch.

### Test bench (Test mode)
- Real-time simulation at 8 kHz. Grab and drag any moving part to rack slides, work bolts and pull magazines.
- Each shot drops into slow motion briefly. There's a motion trace, ejected cases, muzzle smoke and synthesised sound.
- Loose rounds are physical objects. Drag them into an open action and close it to chamber them. Opening a loaded action ejects the live round onto the bench, where you can pick it up again. Hand-loaded designs start with rounds on the bench, and L adds more.
- Bolt handles are dragged directly: one drag lifts and draws the bolt, limited by hand strength. The end-view dial is gone.
- Magazines drop free, latch when pushed home, and spare magazines can be put on the bench. Single shots load by hand.
- Fire control is either built in or drawn. Feeding, the bolt and extraction are always real parts.
  - **Built in:** no hammer or striker parts, but the action must open a set distance to cock it again. It has semi, full-auto and burst modes, optional double action and a lock time. The bench shows whether it's cocked.
  - **Drawn parts:** the round fires when your striker or firing pin hits the primer fast enough.
  - The reliability test reports a *failure to cock*, and now also works for built-in designs that have a drawn trigger.
- Magazines and cleanup on the bench:
  - Load a magazine one round at a time by dragging a loose round onto it.
  - T (or *Top up magazines*) fills them all.
  - Spent cases are cleared after 10 seconds on the bench, and empty dropped magazines after 30 seconds untouched.
- Feed lips and magazine spring settings on magazines:
  - Lip height: too low and the breech rides over the rim.
  - Spring strength: new top rounds rise from the stack under the spring, so a weak spring causes failures to feed in fast actions. With a 30% spring, the blowback example jams 23 times in 40 rounds and the bolt action never does.
  - Single- or double-position feed for double-stack magazines:
    - Single position feeds every round from the centre, but the taper costs about 20% of the spring.
    - Double position alternates sides. Each round enters the chamber at a sideways angle, which only becomes a problem for short, fat rounds.
  - The defaults are unchanged and feed cleanly.
- Long sessions no longer run out of room. Every fired round used to take a new slot until the bench stopped feeding: after about 95 shots, a false failure to feed. Removed rounds, cases and magazines now free their slot.

### Tutorials
- **Build your first gun**, a lesson that walks from a blank frame to a working single-shot: barrel, heavy bolt, track, firing system, hand loading and ejector. Firing blows the bolt open and throws the case, and you load the next round by hand and push the bolt shut. Each step recognises your own drawing, and *Do it for me* makes the piece if you're stuck.
- The reliability test now pushes a breech home by hand when no spring closes it (a straight-pull bolt, a sliding breech). It used to just let go, so such guns never reloaded.
- Guided lessons, from the main menu or the graduation-cap button. They cover the camera, drawing and shaping parts, depth and collisions, joints, springs, breech and cartridge, feeding, extraction and ejection, fire control, locking and operating systems, the bench, reliability testing, and the map editor.
- Each lesson opens an example and walks through it in short steps:
  - A glowing outline points at the button or setting each step is about.
  - Steps about a part select it in the view.
  - A step moves on by itself once you've done what it asks.
  - Progress dots show where you are. You can go back or skip, and finished lessons are remembered.

### Ammo designer
- Bullet shapes:
  - spitzer, round nose, flat nose, hollow point, wadcutter, semi-wadcutter, round ball
  - **custom**: drag the outline's corners in the preview
- Shape options: a flat tip, a boat tail, and how deep the bullet is seated.
- The shape is the round's physical outline, so it can change how a gun feeds.
- *Weight from shape* (as jacketed lead), and an estimated ballistic coefficient.
- Built-in cartridges now have real shapes: round noses for 9 mm, .380 and .45, spitzers for 5.56, boat-tail spitzers for the 7.62s. Older blueprints keep the classic shape.

### Example polish
- **Bolt-action rifle:** a longer, gentler feed ramp (13 mm instead of 10). The bolt used to kick rounds into the chamber about 25 degrees nose-up, and working it by hand jammed with most bullet shapes. Now every shape and nose length feeds: 40 of 40 combinations, up from 16.
- **Tilting-barrel pistol:** the ejector sits 1.25 mm further forward. Racking a loaded pistol now throws the live round out in 26 of 30 shape and nose-length combinations, up from 17. Before, it often tipped too little and rode back into the chamber.
- All four examples fire 50 rounds of the reliability test without a jam, using the new bullet shapes.

### Map editor and first person
- **Map editor** on the main menu: build ranges out of brushes, early-2000s Radiant style.
  - Tools: box, ramp, cylinder and stairs, plus move, resize by dragging faces, clip, make hollow, turn, mirror, duplicate, and undo/redo.
  - Each face gets one of 18 materials, with its own scale and offset. The textures are generated by the game, so there are no image files.
  - Map settings for sun, sky and fog.
  - A 3D fly camera and a top-down view.
  - Maps save as plain JSON in `maps/`, free to share and edit, with an autosave.
- **Walk the map in first person (F5):** a floating camera with a body that collides with the map. It walks, sprints, jumps, steps up kerbs and stairs, crouches, goes prone and leans, and stays crouched under low ceilings.
  - New rebindable controls and mouse settings (sensitivity, invert, field of view).
- **Practice range,** a built-in starter map: covered firing line, seven lanes, paper targets at 7, 15 and 25 m, steel plates at 50 and 75 m, and a berm at 100 m.

### Menus and settings
- **Main menu:**
  - Sandbox (the designer) and Test bench, which takes your sandbox design, an example or a saved blueprint.
  - Settings and Quit.
  - Career and Firing range are shown greyed out for later.
  - Your last design turns slowly behind it.
- **Pause menu on Esc:** resume, settings, main menu, quit.
- **Settings, saved between sessions:**
  - display: window mode, resolution, monitor, vsync, frame cap, anti-aliasing, interface scale
  - audio volumes
  - Metric or Imperial units
  - how much statistics detail to show
  - tooltips, hints, colour-blind colours
  - designer camera and autosave
  - bench speed and slow motion
  - fully rebindable keys
- **Designer panel rebuilt:**
  - four tabs: Inspect, Outline, Firing, File
  - grouped settings that fold away, with labels on the left
  - numbers you drag to scrub or double-click to type
  - a part filter in the outline
- **Removed:** copy-as-text and paste-from-clipboard (designs stay in the game), and the old written build guide.
- **Workspace backgrounds:** Blueprint (the default), Drafting paper, Dark workshop, Daylight studio (the old sky and studded baseplate) or any solid colour. Pick one from the palette button in the top bar or in Settings > Display.
  - The designer draws a crisp drafting grid behind the parts: 1, 10 and 50 mm lines, or 0.1, 0.5 and 1 inch in Imperial. Finer lines fade in as you zoom closer, and it can be turned off.
  - The test bench gets a matching floor that fades into the background, with a steel stand.
- **Better shading:** highlights on parts and a soft fill light. Rounded edges now look round instead of faceted. Brass and copper shine, springs are steel coloured, and see-through parts are outlined in a colour that reads on the chosen background.
- **3D view:** labels use the interface font, the how-to line is one quiet strip at the bottom, and joints that aren't part of your selection are faint.
- **Quieter interface:**
  - a new dark theme with one accent colour, and the Inter typeface
  - an icon toolbar with tooltips
  - the bench shows the gun's state in one line (Ready, Chamber empty, Jammed...), with more detail only if you ask for it in Settings

### Physical parts everywhere
- Parts have depth (a position across the gun and a thickness) and only touch where they overlap. A cutaway view (C) shows inside.
- Welded pieces become one rigid assembly. Gravity acts on every part, and the frame reports felt recoil (impulse and peak force).
- Rotating bolts lock with real lugs against real shoulders. A lug welded to the bolt swings round with it, with no unlock-angle rule.
- Physical feeding:
  - The magazine holds its top round under the lips, and the breech face strips it over the feed ramp.
  - It enters the chamber only if it arrives straight.
  - The extractor pulls the case out by the rim. The rim can slide a little up and down the breech face, so a tilting barrel can drop away under it.
  - The case swings out when it hits the ejector you drew, and flies sideways through the port you cut.
- Malfunctions emerge from the geometry:
  - failure to feed, failure to eject, stovepipe, double feed, failure to return to battery and light strike
  - each with advice on the usual cause, a toast and a sound
  - clear them by hand: rack the slide, or pull the stuck case out
- **Reliability test:** fires your design 25 to 200 times with varying ammunition power, action friction and grip, clears every jam the way a shooter would, and reports the jam rate and the kinds of jam. It runs in the background.
- The simulation is about twice as fast. It now keeps up with real time on the example guns.

### Cartridges and magazines
- Built-in cartridges: 9x19mm, .380 ACP, .45 ACP, 5.56x45mm, 7.62x39mm and 7.62x54mmR. The cartridge designer saves your own, and recoil comes from the bullet and powder gas.
- Procedural magazine maker: capacity, single or double stack, curve and wall thickness, with the magazine well following the shape.
- Magazines are optional: single shots and internal supplies work too.

### Fixes
- The single-shot rifle could only be loaded with a hidden shortcut, so pressing fire just clicked on an empty chamber. Loading is now done by hand with real rounds.
- A round the breech closes on again after a short stroke is chambered again, instead of being left on the extractor.
- A trigger return spring that went over centre at full pull no longer holds the trigger back.

### Examples
- Rebuilt from real parts, with no rules standing in for mechanisms:
  - **Blowback pistol** (.380). Striker fire control with a sear, trigger link and connector ramp, so holding the trigger fires once and letting it out resets it.
  - **Tilting-barrel pistol** (9 mm). A Browning cam lock: the barrel locks into the ejection port, recoils 2 mm locked, then a cross pin in its cam slot pulls it down off the slide.
  - **Bolt-action rifle** (5.56 mm). A two-lug turn bolt, a cock-on-close striker, a fixed ejector in a bolt slot, and a 5-round magazine.
  - **Single-shot rifle:** the same action, loaded by hand.
- Chauchat-style long recoil (still simplified) and a blank frame. The older simplified examples remain in the code for tests.

### Program
- Windows is the only release build. On Linux, run the .exe with Wine-GE or Proton-GE. The physics core and its tests still build natively on Linux, since they need no graphics.
- Blueprints are JSON, with autosave and named saves. Each blueprint records the version that saved it.
- The version appears in the window title, the toolbar, `gunsmith3d --version`, and on Windows in the .exe's Properties.
- Renamed from Action Lab 3D to Gunsmith3D. The old autosave is picked up automatically.
- Source split into layers (core, app, ui) so each part can be worked on separately.
