# Changelog

Versions are `MAJOR.MINOR.PATCH-stage`, for example `0.1.0-alpha`.

- **Stage** is how finished a version is: `dev` (in progress, things change daily), then `alpha`, `beta`, `rc` (release candidate), then no label for a full release.
- **MINOR** goes up for each release with new features while the game is in early access (0.x). **PATCH** goes up for fixes to a released version. **MAJOR** becomes 1 at the first full release.
- **Blueprint format** is a separate number saved inside every blueprint. It only goes up when older versions of the game would load a design wrongly, so blueprints keep working across most updates. Opening a blueprint from a newer version shows a warning.

The version is set in one place, at the top of `CMakeLists.txt`.

## 0.1.0-dev (in progress, towards 0.1 alpha)

0.1 alpha will be declared once the editor and the test bench are worked out.

### Editor (Build mode)
- **Tidier tracks:** a track's path is no longer drawn on the bench, and in the designer it's faint unless it belongs to what you've selected (slots in moving parts used to draw as long rods sticking out of the gun).
- **New lesson: Tube magazines and lifters**, on the pump-action example.
- **Physical tube feeding:** with physical rounds, a tube magazine's spring pushes each round out of its mouth, head first, as a real round. Whatever you draw takes it from there: a lifter raising it to the bore, a cartridge stop holding the next one back. A part in the way stops it, and a tube spring too weak for how fast the gun is worked fails to feed. Older blueprints' tubes keep the simple rule (the setting is *Rounds leave* in the Magazine section).
- **New example: pump-action carbine** (.45, tube magazine): a fore-end and action bar with free travel, an action lock, a fork lifter driven by a slot in the bar, guides and an ejector, all drawn parts. 50 rounds in the reliability test, no jams.
- **"Worked by hand" control:** mark the part a shooter grabs to work the action (a pump's fore-end, a charging handle). The reliability test works the gun by it.
- **Action lock:** a catch joint can *hold it closed*: the held part can't be blown back past its catch point until the release part has been drawn back far enough against it. It catches again when it comes home.
- Reliability test: a hand-worked gun that closes on an empty chamber with rounds still in the magazine now counts as a failure to feed (it used to keep pulling the trigger on nothing).
- **Your own textures:** put pictures (.png, .jpg, .bmp, .tga) in *user/textures*, use *Add a picture...*, or drag them onto the game window. Gun parts get a *Surface* section with the picture and its size (the part colour tints it; white shows it as it is). In the map editor, your pictures come after the built-in materials on the Textures tab; maps save them by file name.
- **Your files in one place:** blueprints save to *user/guns* and maps to *user/maps* in the game folder. Files in the old *blueprints* and *maps* folders are moved over the first time the game starts.
- **Ctrl+S** saves the gun from anywhere (designer or bench), named after the design unless you typed a name or opened a saved one.
- **Pins** have a *Diameter*.
- **Welding reworked:** a weld is no longer a joint at all. Pick *Weld*, click one part, then another: they become one solid piece and nothing is added to the gun (no dot, no entry in the joints list). Dragging any part of a welded piece moves all of it, the part panel says what it's one piece with and has *Unweld this part*, and the Outline marks welded parts. Older blueprints' weld joints load as welded pieces (a part welded to the ground becomes fixed), and behave the same.
- **Round parts:** tick *Round* on any part and its outline is spun around its middle line, like bar stock turned on a lathe: a rectangle becomes a round bar or barrel, steps become shoulders. *Sides* turns it down to flats instead (4 is the bar as drawn, 6 a hex, 8 an octagon), *Turned to* rotates the shape about its axis (a square at 45 is a diamond), and *Sweep* makes a wedge, like a locking lug, pointing any way round. *Axis* picks level, upright or along the part. It's real for the physics too: parts collide with the round shape, not a box, and the weight follows.
- **Naming parts:** the name of the part under the cursor shows in the view (and on the bench, with what dragging it does). In the Outline, click a part to pick it out without leaving the list, and double-click to rename it. The name box says "Name this part".
- **Slow motion moved to the bench:** speed and slow motion after each shot are in a *Speed* section on the test bench panel instead of Settings.
- Fixed: the Carve tool's Depth and the part's Depth clashed and showed a "conflicting ID" error box.
- **Tutorials brought up to date:** a new *Carving in 3D* lesson (draw a block, cut it from the side and the top, change a cut, cut right through; every step has *Do it for me*). *Drawing and shaping* now covers extruding edges, the 32-corner limit and typing exact numbers, and points to Carve instead of using the knife for notches. *Feeding and magazines* explains removable and built-in magazines and round paths.
- **Corner limit:** a part can have at most 32 corners, and edge splits have a short cooldown, so hammering *Split all edges* or the edge diamonds can no longer pile up thousands of corners and stall the game. *Split all edges* near the limit splits the longest edges first. The Shape section shows the count, and a part saved with more gets a *Simplify to 32 corners* button.
- **Carving in 3D:** a new Carve tool cuts circles, half circles, rectangles and triangles into any face of a part, to any depth or right through, at any angle. Cuts change how parts collide (an ejector can run in a groove cut in the bolt) and how much they weigh. Each part lists its cuts, and each can be resized, re-aimed, duplicated or removed.
- **Tracks in two clicks:** click the part that should slide, then where it slides to. Hold Shift to add bends. While drawing, the view names the part that slides and the part holding the slot, and a selected part is picked first when parts overlap.
- **Type exact numbers everywhere:** click a number field without dragging to type a value, or double-click (or Ctrl+click) a slider.
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
- Magazines of any shape:
  - Any part can be a **removable** or a **built-in** magazine. Built-in ones (internal boxes, tubes) are loaded in place, by hand or topped up, and the reliability test refills them in place.
  - **Round path:** draw the path rounds follow inside the magazine. They sit side by side (a box) or nose to tail (a tube), and the top round is fed from its first point.
  - **Insertion path:** a removable magazine's well can have bends and a tilt at each point, for magazines that rock in, and the magazine follows it in and out.
  - A selected magazine stays see-through, so you can see its rounds.
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
- **Kinds of round:** centrefire, rimfire, shotshell and caseless.
  - **Shotshells:** a coloured hull on a brass head (set how high the brass goes), crimped shut. Load shot (pellet size and count; the weight comes from the lead) or a slug. A fired hull opens at the crimp.
  - **Rimfire:** the priming is in the rim, so a drawn firing pin has to hit the rim's edge. A centre-striking gun just clicks.
  - **Caseless:** the round is a block of propellant that burns away. Nothing is left to extract.
  - **Several bullets per round** for any cartridge. Recoil and energy count the whole payload.
  - New built-in loads: 12 gauge 00 buck, slug and birdshot, .22 LR, and a 6 mm caseless round.
- **Pump-action shotgun example:** the carbine's action redrawn for 12 gauge buckshot, six in the tube. 50 shells, no jams.
- **Several chambers per gun:** Firing > Chambers > *Add a chamber*. Each one has its own breech face and muzzle, on any part, and can be closed by any part. They all take the same cartridge; a magazine feeds the first.
  - *Fires:* one per pull, in turn (a double), or all at once (a volley gun; the recoil adds up).
  - The bench reliability test loads every chamber, fires them all, then reloads.
- **Over-under derringer example:** two barrels in a block that tips up on a hinge at the top of the standing breech, in .41 rimfire. Tip it up and the empties drop out. 50 rounds, no jams.
- Hinged actions on the bench: the reliability test swings a hinged part marked *worked by hand* open about its pin (tip-ups, break actions), instead of pulling it straight back.
- Extraction: a chamber that swings more than 25 degrees away from its breech face slips the case off the extractor hook, so the chamber carries it (or drops it). A case that never really left the chamber (the action only stirred) no longer counts as a failure to eject.
- New cartridge: .41 Short (rimfire).
- **Belts:** a magazine can be *moved by a pawl* instead of its spring (Inspect > Magazine > Moved by). Choose the part that works it and its stroke. Each time that part travels the stroke and comes back, it pushes the belt on one round into the tray. A round still in the tray stops the belt, and a pawl that can't travel the full stroke never moves it (the gun fires once, then fails to feed). The belt hangs out of the side of the gun.
- **Belt-fed blowback example:** the blowback pistol's action fed from a 50-round belt, its slide working as the pawl. 50 rounds, no jams; with a 36 mm stroke (more than the slide travels) it fails to feed after one shot.
- **Clips** (Magazine > Loaded with): stripper clips (reloading adds one clip at a time) or an en-bloc clip (only a full clip goes in, so no topping up; the empty clip pings out after the last round).
- Removed an out-of-date note in the magazine panel that said tubes only feed the simple way.
- Fix: an action lock now catches before anything else moves, so a heavy shot can't shove the parts linked to the bolt. (The pump carbine's fore-end no longer twitches back when it fires.)

### Pose (how it's held)
- **Pose mode on the Test bench** (the Pose button in the top bar). A plain upper-body mannequin stands with the gun:
  - drag the **firing hand**, the **support hand** and the **stock's butt** onto any part, and drag the dot at the end of each green line to turn it. Hand styles: pistol grip, rifle wrist, fore-end, vertical grip.
  - pose three holds, **Aim**, **Hip** and **Lowered**: drag the dot over the head to move the body, or set the eye's position, how far the body is turned, and how far the muzzle is lowered. Unposed holds are worked out from the gun.
  - the arms bend to reach the hands and **turn red if they can't**, saying how far short they are. A shoulder pocket shows whether the butt is in the shoulder.
  - A lowered gun pivots on its butt (or on the firing hand).
- This replaces the grip markers in the designer. Guns saved with grips (dev22, dev23) keep them as their pose's hands and butt. The designer now just says how the gun is held.
- These poses are what first-person mode will use.

### Designer
- Fix: with the Carve tool on and a part's cut selected, the two sets of shape buttons clashed (the "conflicting ID" error box).
- **Axis lock while dragging:** press **X** (side to side), **Y** (up and down) or **Z** (depth: move the mouse up and down) while dragging a part to keep it to that axis. Press the same key again to free it. X and Y also work when dragging corners. A coloured guide shows the lock.

### Interface
- **Save, Save as and Open are in the top bar.** Open lists your blueprints and the examples. The right panel no longer has a File tab: it's only for changing the gun (Inspect, Outline, Firing).
- **Interface scale** is now in Settings > Interface, as Auto or 75-200%. Auto sizes it to your screen: 100% at 1080p, 125% at 1440p, 175% at 4K. **Ctrl +** and **Ctrl -** change it anywhere. A scale you had already set is kept.
- Sizes can't go below zero any more (thickness stops at 0.5 mm; diameters, lengths, weights, springs, damping and so on at 0). Positions, depth and lip height can still go either way.

### Where it's held
- Mark where the gun is held: **firing hand** (pistol grip or rifle wrist), **support hand** (fore-end or vertical grip) and **shoulder** (the stock's butt). Place them from Inspect (with nothing selected) or a part's *Held here* section. Each is a green line where the fingers wrap, with a hand-sized zone in 3D; drag its middle to move it and its end to turn it.
- The designer says whether the gun is held with one hand or two. These markers are where the first-person hands and shoulder will go.
- All the examples have them.

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
