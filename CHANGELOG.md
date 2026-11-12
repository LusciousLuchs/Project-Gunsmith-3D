# Changelog

Versions are `MAJOR.MINOR.PATCH-stage`, for example `0.1.0-alpha`.

- **Stage** is how finished a version is: `dev` (in progress, things change daily), then `alpha`, `beta`, `rc` (release candidate), then no label for a full release.
- **MINOR** goes up for each release with new features while the game is in early access (0.x). **PATCH** goes up for fixes to a released version. **MAJOR** becomes 1 at the first full release.
- **Blueprint format** is a separate number saved inside every blueprint. It only goes up when older versions of the game would load a design wrongly, so blueprints keep working across most updates. Opening a blueprint from a newer version shows a warning.

The version is set in one place, at the top of `CMakeLists.txt`.

## 0.7.2-alpha

### Rotation rings
- **Rings turn things exactly as far as you drag.** In Pose, the body's turn and tilt and a hand's roll used to go 20-35% too far or not far enough; now every ring matches the angle it shows. A finger's spread stops at the finger's real limit.

## 0.7.1-alpha

### Rotation rings
- **Turn joints and parts with rings.** Pick something and red, green and blue circles appear round it: drag along one to turn it about that axis. Hold **Ctrl** for 15° steps. The angle shows while you turn.
  - **NPC editor and character maker:** the picked bone, at its joint: bend, twist and swing. Everything hanging off it comes along. Your character keeps the angles you give it.
  - **Pose and animations:** a hand on the gun (its angle and its roll), an elbow (swung round the arm), a finger (curl and spread), the body (turned and tilted) and, in an animation, the gun itself (muzzle up, turn, roll).
  - **Designer:** the selected part (Select tool): turn it in the side view, or lean it toward or away from you (Tilt X and Y).

## 0.7.0-alpha

### Career mode: your shop in 3D
- **You walk round your shop now.** Career mode starts on foot in your workshop. Walk up to things and press **F**:
  - the **office computer**: your online store, contracts, staff and building (and the designer)
  - a **workbench** or other workstation: your guns: rate them, build copies
  - the **counter**: the customer in front of you: their form and ID, the background check, the sale
  - the **front door**: the day's news, and **close up for the day**
- **Customers queue at your counter** and **your staff stand at their workstations**: real people you can see.
- **B is build mode:** the level editor's tools on your shop (walls, lights, and the Workstation tool, which now also places the office computer and the front door). B again and you're back on foot where you stood.
- Older workshops get a computer, a front door and two more lights added.

### Updates
- **Check for updates** button on the main menu (and Settings > Interface > Updates).

## 0.6.4-alpha

- **Click through holes:** where you've cut a hole or slot through a part, clicking there picks the part behind it, as you'd expect. Clicking the solid part still picks it (Alt-click for the part behind).
- Updates now come from the public download page (Gunsmith3D-Releases), so they reach everyone while the game's code stays private.

## 0.6.3-alpha

### Fixed
- **Parts see-through in X-ray when you never asked:** a new design's frame started out see-through, and every part made from it (Duplicate, Mirror, Knife, Copy to the other side, extruding a face) copied that. New designs now start solid, and new parts made from another start solid too. The **See-through** box is now near the top of a part's panel (under Fixed), so you can see and change it. Parts already marked in your saved designs keep it: untick the box.
- A part cut with the Knife again is named "Frame 3", not "Frame 2 2".

## 0.6.2-alpha

- **The game updates itself.** When it starts it looks for a newer version online, downloads it and installs it in the background. You'll see "Updated to ..." at the bottom of the main menu: the new version runs next time, or press Restart now. Your designs, maps, character and settings are never touched. Switch it off in Settings > Interface > Updates.
- **This is the last time you need to download a zip by hand:** updates after this one arrive on their own.

## 0.6.1-alpha

### Magazines
- **Round angle** and **Feed direction** (magazine panel, under Rounds inside): tick one to set it by hand. Round angle is how the top round lies (0 = parallel to the bore, + nose up); feed direction is the way the follower pushes the rounds out (90 = straight up). Unticked, they're worked out from the magazine as before. Rounds lying nose down stub on the way in, so a raked magazine usually wants its rounds set level or a little nose up.
- **Double stack on any magazine**, including ones you reshaped or drew yourself. You no longer have to switch back to Fit to cartridge.

### Fixed
- **Cuts in parts didn't count for the round being fed:** a slot or pocket you cut into a bolt, a barrel's end or a ramp was still solid to the top round on its way to the chamber. It's open now, as it is for everything else.

## 0.6.0-alpha

### New: the rest of career mode
- **Your workshop:** Workshop & staff > Build your workshop opens it in the level editor: walls, lights, and the new Workstation tool for workbenches, lathes, milling machines, test benches and your shop counter. Walk around it on foot too. Office (top left) takes you back.
- **Your building holds only so many workstations:** a garage 3, a small shop 6, a workshop 10, a factory floor 16. Move up when you can afford it.
- **Hire gunsmiths.** Each has a wage, a skill level and a speciality:
  - Machinists work fastest at a lathe or a mill.
  - Assemblers work fastest at a bench.
  - Finishers at a test bench check copies and catch most flaws.

  Everyone needs a workstation; you take a bench. Staff get better with the days.
- **Flawed copies:** green hands make more of them (in a test: 12.5% from an unskilled crew, 1.5% from a skilled one, 0.8% with a finisher checking). A flawed copy jams more, and its buyer says so in their review.
- **The counter:** customers come in each day, each after something (a first handgun, a deer rifle...) with a budget. Read their form and ID, run the background check, then sell or send them away. About one in a hundred can't buy: the check denies them, or their ID doesn't match the form, or it's expired (the check won't catch those two: you have to). Refuse and call the police. Selling to them anyway costs a fine and some reputation.
- **Reputation has names**, from garage gunsmith to defense supplier. Police orders come at 35, the military at 60: tens of guns at a time, which needs staff.
- **Range demonstrations:** big orders pass the bench test, then you shoot a course for them on the range: 12 hits on paper in 20 rounds. Miss, and you can try again before the deadline.

### Fixed
- **A career from 0.5 had no counter or bench:** it now starts with both, plus people to hire and customers.
- **Women's chests** were two round balls stuck on the shirt. They're now shaped into the body, in the same faceted style.
- **Guns with a tilting or hinged barrel shot about 2° high on the range:** the shot left along the barrel as it was unlocking, not as it fired. Now the shot leaves along the barrel as drawn, in battery.

## 0.5.1-alpha

### New: materials
- **Every part has a material** (the part's panel, above Mass): steel, iron, brass, aluminum, aluminum alloy, wood, polymer or rubber. Parts you've already made stay steel.
- **It changes how the gun works.** A part weighs what its material weighs, so the gun kicks differently and its moving parts move at different speeds. Measured on the blowback pistol with its whole slide changed: steel 9.1 m/s, brass 8.4, aluminum 24.8, polymer 44.1. Light slides slam back hard, which is why real blowback slides are steel.
- **It changes how parts slide on each other:** rubber grips, polymer and brass slip easily, aluminum rubs harder.
- **Career mode:** the material sets each part's cost, and how long it takes to make (wood is slow to shape, polymer is quick).
- **Its usual colour** with one click, and **Whole piece** to give every part welded to it the same material (a slide is often several parts).
- A weight you type in still wins over the material.

### New: your recorded Mosin bolt
- **Actions worked by hand now sound like your Mosin:** the lift click, the clack at the back, the clack as it goes home, and the click as it locks down.
- **Chosen by how the action moves, not by the kind of gun:** anything worked at hand speed gets the bolt sounds; slides and carriers driven by the shot keep the pistol slide recordings.

## 0.5.0-alpha

### New: Career mode (a first, basic version)
- **Run your own gun shop** in a post-NFA America, where every gun is legal to own. Main menu > Career.
- **Design freely:** every tool is yours from day one. Open the designer from your office, save your gun, and come back with **Office** (top left).
- **Rate your guns on the bench:** the test fires each gun 30 times and works out its jam rate, accuracy, recoil and weight, and what it's worth.
- **Build copies:** each copy takes hours of your 8-hour working day, worked out from the gun itself (parts, cuts, turned parts, springs, rifling), and its material is paid up front.
- **Sell online** through GunBench, your online store. Set a price; buyers order at the end of each day, depending on how good the gun is for the money, and leave reviews that say how it really ran.
- **Contracts:** customers write in with a brief (a pocket pistol, a deer rifle, a competition pistol, something cursed for a YouTube channel...). Take one on, build it, and deliver: they test it against their brief before they pay. Bigger orders come with reputation.
- **End the day** when you're done.
- Coming next: employees and workstations in your workshop, a storefront with walk-in customers and background checks, military contracts and range demonstrations.

## 0.4.21-alpha

### New: light and dark
- **Maps can be dark now, lit by their own lights.** Each map has a **Daylight** setting (Level editor > Map > Light): 1 is broad daylight, 0 is night. Maps you've already made stay in daylight; new maps start dark with one lamp.
- **The Light tool** in the Level editor: click a ceiling, a wall or the floor to hang a lamp. Set its colour, brightness and reach, or make it a **spotlight** with a cone you aim. Up to 16 lights shine at once (the nearest). They don't cast shadows yet, so a light shines through walls: keep its reach to its room.
- **The practice range is now an evening range**, lit by lamps under the roof, a light over each paper target, floodlights on the steel and the firing line.
- **Weapon lights are real spotlights:** they light up whatever they point at, as bright as their lumens and beam width say. **Muzzle flashes** light the room for a moment.
- **Level editor view:** the light button at the top right goes round three views: **as played**, a **work light** (dark maps still show while you build), and **fullbright** (every face in its own colour, no shading, no fog: easiest for placing things).

### New: ammunition
- **Magazines never run out; rounds do.** Each gun carries spare ammunition: ten magazines' worth (at least 10 rounds). Reloading takes a full magazine's worth from it. A magazine taken out with rounds in it gives them back. When the spare runs short, the new magazine only gets what's left. The spare count is on screen.
- **Ammo boxes** (Level editor, and by the practice range's start): press F by one to fill the spare ammunition of every gun you carry.
- **Weapon crates:** press F by one to take any of your guns, as from a gun spawner.

### Changed: your character
- **One-piece body:** the torso, neck, arms and legs are one continuous, faceted body that bends at the shoulders, elbows, hips and knees with no gaps, in a low-poly style. Sleeves, a tank top and shorts are colours on it.
- **Ear defenders:** tall cups with a soft cushion against the head, and a wide padded band in one smooth arc over the top.
- **Glasses:** sunglass lenses over the eyes, a bridge, and arms that run back along the head to the ears.
- **T-shirt sleeves** now cover the shoulder. **Belts** hug the waist. **Eyes** sit in the face instead of bulging out.

## 0.4.20-alpha

### New: your character
- **The character maker:** your character (main menu, *Your character*) is now built from choices instead of bone by bone: male or female (a female body has narrower shoulders, wider hips and a chest), height, build, skin colour, eye colour.
- **Faces:** eyes, a nose and a mouth. **Hair:** none, buzz cut, short, long, ponytail or a bun, in any colour.
- **Clothes:** long sleeves, a T-shirt or a tank top; long pants or shorts; shoes. **Extras:** a belt, a cap, ear defenders, glasses, each in its own colour.
- **Fingers:** five on each hand, three pieces each, coming from the palm. In Pose they're the poser's own fingers, so your animations move them.
- **Smoother:** characters and NPCs are rounder (more sides on every piece).
- **Your own gear** (a helmet, a vest, pouches) still goes on any part, and stays when you change the choices. No more weak spots or armour per piece on your character: armour is the armour bar.
- An older character keeps its shape until you press *Use the character maker* (your gear comes along; Undo brings the old one back).

### New (attachments, part 5)
- **Lights** shine a beam and light up what they point at; **lasers** put a dot (red, green, any colour) where they meet something. Set the beam width, brightness and colour in the attachment designer. **T** switches them on and off.
- **Hold Alt on the range** to free the mouse: a list of the gun's attachments comes up with on/off boxes (lights, lasers, a bipod's legs, an optic's lit reticle). The same boxes are on the fitting bench, and loadouts remember them.
- **Bipods:** legs down and aiming with its feet on something (a bench, a wall top, the floor when you're prone), the gun is rested: hardly any sway, and less kick.
- **Bayonets:** **V** stabs (about 1.1 m reach with a long blade; the damage grows with how far it sticks out past the muzzle, or set it yourself). Without a bayonet, V hits with the muzzle.

### New (attachments, part 4: on the range)
- **Attachment crates:** place them in the Level editor (the practice range has one by the start). Walk up, press F, and the gun in your hands goes on the fitting bench; *Back to the range* when you're done. Attachments are fresh on each map: load a saved loadout from the crate.
- **Attachments weigh what they weigh**, in the gun's physics: on a fixed rail they make the gun heavier (it kicks and sways less); on a moving part they slow it down (150 g on a pistol's slide cut its speed from 6.2 to 4.4 m/s).
- **Aiming through an optic:** your eye goes on the optic's sight line, whatever the gun's pose says. A reflex sight shows its dot where it points; its body fades so you look through it.
- **Scopes magnify for real:** the view through the eyepiece is the range seen at the scope's magnification (the rest stays normal), with your reticle in it. Zoom scopes zoom with the mouse wheel while aiming. Reticles that grow with zoom stay true at every magnification.
- **Muzzle devices** (pick the kind in the attachment designer): a brake kicks less, a compensator climbs less, a suppressor is much quieter with no flash and NPCs only hear it close by, a flash hider hides most of the flash.
- **Grips** steady your aim; a **stock or brace** puts a one-handed gun in the shoulder.
- In the attachment designer: **Meets it with** chooses which side of an attachment goes against its mount (underside, top, back end, front end).

## 0.4.19-alpha

### New (attachments, part 3: the fitting bench)
- **Attachments button on the test bench:** pick one from your Attachments folder, then click a rail's slot or a matching attachment point on the gun. A see-through preview shows where it goes: green if it fits, red with the reason if not.
- **The game checks every fit:** the right kind of mount; a clamp that stays on the rail; no running into another attachment or a solid part of the gun; and nothing in the way of a moving part's travel (a slide, a bolt). Clamps click from slot to slot.
- **Rail adapters work:** fit one (a side-rail conversion, say) and its own rails and points become places to mount more.
- **Slide a fitted attachment a slot at a time**, take it off, and see what they all weigh together.
- **Loadouts:** save which attachments go where on a gun, and load them again; the gun's blueprint is never changed. A loaded loadout is checked again, so a gun you've since changed only gets what still fits.
- **In the attachment designer:** choose which side of an attachment meets its mount: underneath (sits on a rail), on top (hangs under one, like a grip), or an end (onto a muzzle).
- Coming next: crates on maps, and attachments doing their job on the range (weight, optics when you aim).

## 0.4.18-alpha

### New (attachments, part 2: the attachment designer)
- **New > A new attachment** opens the designer for an attachment instead of a gun: draw it with all the same tools. Attachments are saved in their own Attachments folder, apart from your guns, and listed separately in Open.
- **The Attachment tab** (in place of Firing): what it is (optic, grip, bipod, bayonet, light, laser, muzzle device, rail adapter, stock, other) and how it mounts: a Picatinny clamp of a set length (how much rail it takes), or an attachment point of a kind you name. The view shows where it meets the mount.
- **Scope designer:** reflex sight or scope; magnification, variable zoom, eye relief, front lens size, reticle growing with zoom or not. The view shows its sight line and where your eye goes.
- **Reticle editor:** draw your own reticle with lines, dots and rings, in milliradians, or start from a dot, dot and ring, crosshair, duplex, chevron or mil-dot; pick its colour and whether it glows.
- **The license agreement can be read again from About.** It's built into the game, not a file next to it.
- Coming next: fitting attachments to guns on a bench, from crates on the range.

## 0.4.17-alpha

### New (attachments, part 1: mounts on your guns)
- **Rail tool:** drag along the top of a part to lay a Picatinny rail on it, any length, slotted to the standard (10.01 mm apart, 21.2 mm wide). It's a real part: it has weight, it's welded to what it sits on, and it works as a ruler. Put it underneath or on either side from its panel; change its length there and the slots are cut again.
- **Mount tool:** click any surface to put an attachment point there: a bayonet lug, a Warsaw Pact side rail, a muzzle thread, or a kind you name yourself (a maker's own mount). Attachments will fit points of the same kind.
- Coming next: the attachment and scope designer, then fitting attachments to guns from crates on the range.

## 0.4.16-alpha

### New
- **Armour bar and armour plates.** Walk over a plate to fill your armour bar: 50 a plate, 150 at most (three plates). While you have armour it soaks 70% of each hit; the rest reaches your health. A full bar leaves plates lying for later.
- **Plates in the Level editor:** a new tool places plate stacks; choose how many plates lie there and whether they come back after a while (for arenas). The practice range has a stack next to where you start.
- **Armoured NPCs:** give an NPC an armour bar in the NPC editor, so heavies take more shots to drop.

## 0.4.15-alpha

### New
- **Pick cuts in the view.** In a part's Carves list, turn on *Pick cuts in the view*: all its cuts show, the one under the mouse lights up with its name, and a click anywhere on it picks it (drag to move it). Works with any tool.
- **Name your cuts** ("Front serrations", "Ejection port"): shown in the list and in the view.
- **Duplicate does what you set:** how many copies, and how far each one steps on from the last in X, Y and Z (0 puts the copy right on top, ready to drag). Rows of slits or holes in one click.
- **Mirror X / Y / Z** makes a mirrored copy of a cut across the middle of the part: slits on the right side become matching slits on the left.

## 0.4.14-alpha

### Changes
- **Punchier gunshots.** Each shot is now your recording plus a made-up deep thump and range echo, both scaled to the round: a .22 pops, a 7.62x54R or a slug booms. The recordings themselves are compressed and pushed harder.
- **No more brass-catcher clang or background voices** in the shots: only the bang itself is kept from each recording, and the echo after it is made up.

## 0.4.13-alpha

### Fixes
- **A cut's Duplicate and Remove buttons work again.** With a cut selected, its Duplicate button could act on the whole part instead (the two buttons shared a name), and a red "programmer error" box popped up. The same mix-up is fixed for the Scale tool's size boxes and the poser's hand and gun position boxes, and that error box never shows for players any more.

## 0.4.12-alpha

### New
- **Real gun sounds**, recorded by the developer on their own Thunder .380 and AR-15, replacing the old made-up bang and clack.
- **Every gun sounds like itself.** The shot is worked out from the cartridge and barrel: bigger rounds are louder and deeper with a longer ring, small ones higher and snappier; pistol rounds use the pistol recordings, rifle rounds the rifle ones, and in-between rounds a mix; a bullet faster than sound adds a crack; a short barrel on a big round adds blast. No two shots in a row sound exactly alike.
- **The action sounds by weight:** a heavy bolt carrier clacks deeper than a light slide.

## 0.4.11-alpha

### New
- **Type your snap step** in the designer's top bar, in any unit: 0.5 mm, 1 cm, 1/32 in, 1 1/2", even 1/4 in + 2 mm. The magnet button turns snapping on and off, and the arrow beside it lists common steps in both systems (down to 1/32 in).
- **mm | in switch on the toolbar** (designer, test bench and level editor). Flip it at any time: it only changes what's shown. It has moved out of Settings.
- **Every measurement box takes any unit.** Click a value and type it in millimetres, centimetres, inches or inch fractions (3/8 in), grams, kilograms, grains, ounces or pounds, whatever is shown. Handy for guns that mix metric and imperial.
- **Measure tool:** a *Keep it shown* tick keeps the measuring line on screen while you move parts with other tools. Lengths are shown in mm, decimal inches and the nearest 1/32 in.

## 0.4.10-alpha

### New
- **Your files now live in Documents.** Blueprints, levels, rounds, your character, NPCs, textures, saved parts and groups go in *Documents/OAB Foundry/Gunsmith/User*, each in its own folder. Settings and the autosave sit one folder up. Files from older versions are moved over the first time you start the game.
- **Choose where your files go** in Settings > Files: open the folder, pick another one (your files are copied there), or go back to the default.
- **Each saved round is its own file** in the Rounds folder, ready to share.

### Fixes
- **F11 switches between windowed and fullscreen** (back to whichever full-screen mode you last used).
- **Borderless windowed no longer stays on top**, so Alt+Tab works again.

## 0.4.9-alpha

### Fixes
- **Open bolts seat against the breech face before they fire.** They used to fire as soon as the bolt came within a millimetre of the barrel, while still running forward. Now the shot waits until the bolt is home (closed up, or stopped on the round it pushed in). Open-bolt guns may fire a little slower: that's the bolt really closing each time.

## 0.4.8-alpha

### Fixes
- **The designer's tools on the left no longer run off the bottom** (the Spring tool was hidden). The list now always stops above the hint line: when the names don't fit it shows icons only, and when even that is too tall it scrolls with the mouse wheel.
- **Automatic interface size follows the game window**, not the monitor: it's worked out from the window's width and height (100% for a 1920 x 1080 window, about 85% for 1440 x 900, about 133% for 2560 x 1440) and updates when you resize the window. A size you choose yourself in Settings stays as you set it.

## 0.4.7-alpha

### Share your cartridges
- **Copy share code** (cartridge designer, at the bottom): the whole cartridge as one line of text to paste anywhere, in Discord or a message. **Paste a share code** brings one in; save it to your cartridges to keep it.
- **Export file:** saves the cartridge to user/cartridges as a .json file (**Open folder** shows it). **Drop a cartridge file on the game window** and it's added to your cartridges.

## 0.4.6-alpha

### Your character
- **Your character** on the main menu: build your own player character with the NPC editor's tools, kept to a human. Shape every bone, colour it, put pictures on it, and **add gear** (a helmet, a vest, pouches, a watch). It's kept as you go.
- **It holds your guns in Pose** (Test bench > Pose) instead of the mannequin, with its own shoulders and arm lengths, gear riding on its arms. With no character, the mannequin stands in as before. **My character** in the Pose panel switches between them.

### A freer poser
- **Elbows swing:** drag the blue dot at an elbow to swing it round, or set **Elbow** in the panel.
- **Wrists roll and lift:** **Wrist roll** turns the hand over or under the grip; **Off the gun's side** loosens the hold.
- **Hands anywhere:** a hand off the gun can be **placed**, not just hung at the side. Drag its blue dot to a pouch, a vest, or up holding a magazine. In an animation, a hand now **travels smoothly** onto and off the gun instead of jumping.
- **Shoulders reach:** an arm that's a little short pushes its shoulder forward toward the hand (up to 7 cm), as people do.
- **Move the gun in animations:** drag the pink **Gun** dot to move it and the **Aim** dot to point it (Shift: roll), for reloads where the gun is brought in and turned for a better grip. The hands on it go with it.
- **Arms at this key:** the elbows, wrists and placed hands of every animation key, in the panel.
- Fingers bend a little further than before.

### Performance overlay
- **Press F3** (or Settings > Interface > Performance overlay) to see the frame rate and a graph of how long each frame takes. F3 again shows full details, a third time hides it.
- **Full details:** a frame rate graph, where each frame's time goes (the gun's physics, NPCs, drawing the 3D view, the panels, the graphics card and screen) with a hint at what's holding it back, and what's on screen: the design's parts and its **heaviest parts for the physics**, or on a map its blocks, NPCs, bullets in flight and path grids. It never gets in the way of clicks.

### About
- **About** on the main menu: the game's version, studio and developer, and what it runs on: the raylib version, the OpenGL version the game uses, and what your PC's graphics driver and card report. **Copy these details** puts it all on the clipboard for bug reports.

### NPCs
- **Up to 64 NPCs on a map at once.** The Level editor shows how many a map has. A map that asks for more brings only the first 64, and the range says so.
- **Big crowds run smoothly:** NPCs take turns planning their routes, so many at once no longer cause stutters.
- **Faster movement:** walking and collision, for you and the NPCs, is about 4 times quicker again.

## 0.4.5-alpha

### Better-looking NPCs
- **Smooth bones:** every bone is now a smooth, finely shaded shape instead of a few stacked rings.
- **More shapes:** Rounded, Cylinder, Box (a rounded square), Ball / egg and Wedge (a box thinning to an edge: feet, blades, beaks).
- **Width, Depth and Bulge** for every bone: flatten a chest or a hand, widen the hips, swell a calf or a bicep, pinch a waist.
- **Pictures on bones:** the gun finishes and your own pictures from user/textures, with a picture size. The colour tints it.
- **Smooth joints:** bones with flat or square ends get a rounded knuckle where they meet the bone they hang off, so elbows and knees bend without a gap.
- **Use this look on its mirror twin** copies a bone's shape, size, colour and picture to its (left) / (right) partner.
- **A new Human starter:** a flattened chest wider at the shoulders, an egg-shaped head, muscles that swell, flat hands and boots. The example maps use it.
- Fallen NPCs keep their looks as they lie.
- Fix: NPCs could fail to find their way up some stairs.

## 0.4.4-alpha

### What's new, on the main menu
- The main menu shows this version's changes in the top right corner. **Earlier updates** opens the ones before.

### Built-in NPCs from your designs
- NPCs made in the NPC editor can now ship with the game as built-in starting points. They're listed first under **New** in the NPC editor, by their own names. Send the NPC files from user/npcs to be added.

## 0.4.3-alpha

### Carves snap
- **Placing a cut snaps it:** to the part's middle (along, up and down, or across its thickness), into line with the part's other cuts, and into line with the part's corners when the cursor is close to one. Otherwise it snaps to the grid (the Snap step in the top bar). A pink guide line shows what it lined up with. Hold **Alt** to place it freely.
- **Move a cut by dragging its dot** (Carve tool, with the part selected). It slides across the face it goes into, with the same snapping. A plain click on the dot picks the cut, to change it in the panel.
- Dragging out a cut's size snaps to the grid step too.

## 0.4.2-alpha

### NPCs find their way
- **Path-finding:** NPCs now plan a route to you across the whole map: round long walls, out of dead ends, through doorways and mazes, **up stairs and ramps to upper floors**, and **off ledges** to drop down to you. They re-plan as you move.
- **Out of reach** (you're on something they can't climb): they come as close as they can and wait there.
- **Routes fit the NPC:** each NPC size gets its own map of where it fits, so a big NPC goes the long way round where a small one takes a narrow gap. Every map works out these routes in a moment when you start playing it.
- **They hunt longer:** after hearing or seeing you they keep looking for 15 seconds (was 7).
- **Small NPCs climb stairs:** every NPC now steps up at least 25 cm. A sprawling NPC's body counts as about half as wide as it is tall, not as wide as its legs reach, so a crawler fits through doors.
- **Fix:** a shooter NPC whose gun can't be found now fights with its hands at arm's length (it used to hit you from its gun's range).
- **Faster:** walking and collision for you and the NPCs is many times quicker on maps with lots of blocks.
- **NPC maze test**, a new example map in user/maps: a maze with a balcony up a flight of stairs. You start on the balcony and two grunts and a crawler have to find their way to you.

## 0.4.1-alpha

### NPCs come alive
- **They walk the map** at the speed you set in the NPC editor, sized to their body, stepping up stairs like you do, and they **go round what's in their way** (a wall, a bench) by trying the nearest clear way to one side.
- **They notice you:** within their sight distance, in front of them (or right behind them, if you're close), with nothing in between. **Gunshots carry:** everything within 50 m hears your shot and comes to look, and an NPC that spots you alerts the others near it.
- **They act on their behaviour:** *stand* holds its spot, *wander* strolls about until it sees you, *chase* comes for you, *guard* won't stray more than 8 m from its post.
- **They attack:** *up close* hits you when you're in reach (about once a second); *with a gun* fires its NPC gun at its rate of fire, shots per pull and spread (wider while it moves), from about 0.4 to 0.8 s after it first sees you; *explode* runs at you, glows and goes off, hurting you and any NPC nearby.

### Damage and death
- **Your bullets hurt them by their energy:** a 9 mm in the chest does about 24 of an NPC's health, three times that on a weak spot; armour stops its share. The hit and the kill show top right ("Killed the Grunt"), and the top of the screen counts the NPCs left.
- **At no health they fall over** toward where the shot came from, and lie there.

### Your health
- **You have 100 health,** shown bottom left. Getting hit flashes the screen red. An NPC's bullets hurt you by their energy too, scaled down for play (about 8 a hit for a 9 mm, 55 for a full-power rifle), or by the gun's new **Damage to you** setting (Firing > Made for NPCs).
- **At zero you die:** click (or Space) to try again, which puts you back at the start with full health and every NPC back in its place.

## 0.4.0-alpha

### Maps bring their NPCs
- **Place NPCs in the Level editor:** the NPC tool (ghost icon) places the NPC chosen in the panel, standing where you click, facing you. Set how many in its panel.
- **A map file carries a copy of every NPC it uses.** Share a map, and its enemies go with it: opening it (or playing it) puts any NPCs you don't have yet into user/npcs, ready in the NPC editor.
- **On the range, NPCs stand where they were placed.** Several in a spawn stand side by side in rows.
- **Bullets hit their bones:** a shot that hits says which NPC and which bone ("Hit the Crawler: Head (weak spot!)"). They don't take damage, move or fight yet: that's next on the to-do list.

### Guns made for NPCs
- **Gun designer > Firing > Made for NPCs:** a gun for NPCs to carry, simple and a bit bad on purpose. It never needs reloading and never runs out. It isn't worked part by part or animated: it fires its cartridge's bullet at a set rate of fire, shots per pull and spread. The parts you draw are just its look.
- **NPC editor > In a fight > With a gun** lists only these guns.
- **Players can't take them:** they're left out of the range's gun list and the gun spawner (shown as *made for NPCs*).

### Your inventory, and gun spawners
- **Carry up to 4 guns on the range.** The mouse wheel changes guns, **G** drops the one in your hands, and **F** uses what's in front of you. Your guns are listed bottom left.
- A dropped gun lies on the ground in a case, just as you left it (loaded, cocked or jammed). **F** picks it up again.
- **Gun spawner** (crate icon in the Level editor): a stand you place on any map. Walk up to it and press **F** to take any of your blueprints or the examples. With 4 guns already, the one in your hands is put down for it.
- New keys (change them in Settings > Controls): **Use (F)** and **Drop the gun (G)**.

### Groups: save objects as one and place them again
- **Level editor > Edit > Groups:** select brushes (Shift-click adds more) and an object (an NPC or a gun spawner), name it, and **Save the selection as a group**. It goes in user/groups.
- **Place** puts a copy where you're looking, already selected so you can drag it into place. It works in any map, and the NPCs it uses come with it.

## 0.3.9-alpha

### Any face, Sprocket-style
- **Every face of the selected part works like its outline does.** Point at any face (top, bottom, an end, a side, the front or the back) and it's washed in yellow. Click to pick it, then press it again and drag to push or pull it.
  - **Shift-drag a face to extrude it** into a new step.
  - A drag anywhere else on the part still moves the part.
  - **Alt-click** picks the part behind (this used to be a plain click).
- **The corners on the far face** (circles) can be picked and dragged too, to shape the part in depth.
- The arrow dots from 0.3.8 are gone: the faces themselves are the handles now.

### Extruding works the way you'd expect
- It always added the step, but the new corners sat in line with the old face, no seam was drawn, and nothing was left selected, so it looked like a plain push.
- Now the seam shows as a line at every corner, and the new face stays picked, so you can push it further straight away. Its two new corners are selected too: drag them, or Scale them, to taper the new step.

### Map editing tools: the Level editor and a new NPC editor
- **Map editing tools** on the main menu opens the **Level editor** (the map editor) and the new **NPC editor**.
- **The NPC editor builds enemies and characters from bones.** Use as many as you like, each hanging off another anywhere along it:
  - Set its length, how thick it is at each end, its shape (rounded, box or ball) and its colour.
  - **Pose a bone** with Bend, Turn and Twist, or click the dot at its end and drag it (Shift stretches it).
  - **Mirror it** copies a limb and everything on it to the other side. Weak spots (triple damage) and armour are set per bone.
- **Start from** a human, a four-legged hound, a six-legged crawler, or a single bone.
- **How it fights** is saved with it: health, speed, attack (up close, with one of your guns, or exploding), damage, reach, how far it sees, behaviour and team.
- Saved in **user/npcs**, with Undo and Redo.
- **Not alive yet:** NPCs can't be placed on maps, move or fight. The plan for that (spawning, walking, attacking, getting hit, waves) is on the to-do list in BACKLOG.md.

## 0.3.8-alpha

### Push and pull any face, right from Select
- **The selected part's faces get arrow dots.** Turn the view so you see the part at an angle, and every face you can see gets an arrow dot in its middle: the top, the bottom, the ends, a slanted side, and the front or back. Click a dot, then drag it.
  - The top, bottom, ends and sides push out or in.
  - The front and back faces make the part thicker or thinner.
  - Hold Shift as you drag a side to extrude it into a new step.
- Before, only the side outline had handles in the Select tool, and the other faces needed the separate Faces tool and three clicks.
- **The Faces tool is quicker too:** the first click on a face picks the part and that face together, then you drag it.
- The dots hide when you look straight at a face (as in the flat side view), so dragging the middle of a part still moves the part.
- Round and tilted parts still can't have their faces pushed: change their outline in the side view, or their thickness and tilt in the panel.

## 0.3.7-alpha

### Scale tool
- **Scale** (in the Shape tools): click a part, then click a handle of the box round it and drag.
  - A **corner** makes it bigger or smaller and keeps its proportions; Shift stretches it freely.
  - An **edge** stretches it one way.
  - **Ctrl** scales it about its middle instead of the opposite side.
- Or **type the size** in the panel: width, height, thickness, or *Scale by* a percentage. *Keep proportions* and *Thickness too* decide whether the other sizes follow.
- **Everything welded to the part scales with it**, and so does everything attached at points on it: pins, track slots, spring ends, the breech face, muzzle, firing pin tip, hands, cuts, engravings and the control's push point.
- A weight you set grows with the part. Springs keep their length and strength.
- With **corners picked** (Shift-click or a box), only those corners are scaled.

### Magazines: easier to resize and line up
- **Every magazine shows its dots when selected**, and each one drags on its own (click a dot, then drag it):
  - **Feeds from here (top round):** where the top round sits for the bolt to strip it.
  - **Rounds lie this way:** the line the rest of the rounds sit along.
  - **Seated here:** where the magazine sits in the gun. Dragging it moves the magazine, its well with it.
  - **Magazine well exit**, as before.
- Until you move a dot, the rounds are laid out automatically, as before.
- **Fitted magazines have a Length and a Width** you can set (*Back to the fitted size* undoes it). They're no longer just as long as their capacity needs.
- **Reshape a fitted magazine by hand** (drag its corners or faces, or scale it) and it keeps that shape: it's no longer refitted over your changes. Its rounds keep their exact place: the top round is presented within 0.005 mm of where it was. *Fit it to the cartridge again* puts it back.
- Moving or turning a fitted magazine no longer makes it jump back on the next change.
- Fixed: dragging a round-path dot deselected the magazine.

## 0.3.6-alpha

### Gas operation
- **Firing > Gas > Add a gas port:** drill a port into any bore and pick the part the gas pushes. Long stroke (a piston welded to the carrier), short stroke (a tappet that hits it) and direct impingement (the carrier itself) all come from what you draw and connect: nothing is special-cased.
- **The settings all matter**, measured on a free piston beside a 20-inch 5.56 bore:
  - **Port at:** 180 mm gives 2.6 N s, 330 mm gives 1.7, 480 mm gives 1.3 (past the muzzle: nothing).
  - **Port size:** 1.5 mm gives 1.2 N s, 3.5 mm gives 2.1.
  - **Piston size** and **Gas stroke** (how far it moves before the gas escapes) push harder as they grow.
  - **Push angle** turns the push off straight back.
- **Tuned against real guns:** an AR-15-like setup gives its 300 g carrier 1.7 N s (5.8 m/s), and an AK-like one gives its 430 g piston and carrier 5.7 m/s. Real ones run at about 5 to 6 m/s.
- The gas only arrives once the bullet passes the port, weakens as the bullet goes down the bore, dies away after it leaves, and thins out as the piston moves.
- **Give the piston a spring** to bring it back: the gun then counts as self-loading on the bench and in the reliability test. The breech still has to be locked and unlocked by what you draw.
- The **gas port** shows on the bore in the designer, with a line to the part it pushes. On the bench, the panel shows what the gas gave the piston on the last shot.
- No example gun uses gas yet.

### Open bolt
- **Firing > Fire control > Open bolt** (built-in fire control): the sear holds the bolt or slide back, open. The trigger lets it go; it strips a round, chambers it and fires it as it closes.
- In **semi** it's caught again on the way back. In **full-auto** it keeps running while you hold the trigger, and is caught when you let go. Burst works too.
- **Sear catches at:** how far open it's held. Set it to 0 for just far enough back to pick up the next round. A bolt that doesn't come back that far after a shot stays closed; the reliability test calls that a failure to cock. Rack it to cock it again.
- The gun starts held open with the chamber empty. The bench shows *bolt held open on the sear*, *bolt running* or *bolt closed*.
- **Drawn parts:** a firing pin welded into the bolt face now fires the round when the bolt slams home, so you can also build an open bolt with your own sear.
- Tested on the long-recoil example made open bolt: in semi, one shot per pull and the bolt held open; in full-auto, it fires while the trigger is held. 20 of 20 rounds in the reliability test, both ways.

### Fixes
- **Barrels sat a little off their bore,** across the gun. To stop overlapping faces flickering, each part was nudged sideways by 0.04 mm per place in the parts list, so a barrel added late to a big gun sat most of a millimetre off. Round parts are no longer nudged, and the nudge for the rest is now capped at 0.12 mm.

## 0.3.5-alpha

### A better mannequin
- **The Pose mannequin is rebuilt:** a shaped torso in a shirt, a head with a nose and ear defenders, a neck, and arms that taper like real arms (short sleeves, bare forearms) instead of grey tubes and balls.

### Lining up the sights
- **A long red line of sight** runs from the aiming eye straight along the gun, far out past the muzzle. Move the body (or the hands) until it passes over your sights.
- **On the firing range, aiming now puts your eye exactly where you posed it** in the Aim hold, so sights you lined up in Pose line up on the range too. (Unposed guns still look just over the top of the gun.)

### Moving the shoulder
- **The shoulder pocket moves:** with *Stock in the shoulder* on, drag the orange **Shoulder** dot, or set *Shoulder forward / down / across* in the Body section. Each hold (aim, hip, lowered) has its own.
- **Put the shoulder on the butt** moves the pocket onto the butt wherever it is, keeping the eye where it is: line the eye up with the sights first, then press it.

### Animations
- **Longer than 2 seconds:** set **Timeline** under the time slider (up to 60 s).
- **Move the gun itself in an animation:** each key has *The gun at this key*: forward, up, right, muzzle up, turn and roll, about the firing hand. The hands go with it, and the gun eases back to the hold at the end. Use it to lift a gun to look at it, or tip it to see into the port during a reload.
- **Add a key here** makes a key at the slider's moment without dragging a hand.
- **Inspect (I):** on the firing range, I plays your Inspect animation, or, with none, a plain look over the gun (brought up, turned, rolled to show the other side). On the bench, I plays your Inspect animation. Rebindable in Settings.

### Barrels
- **Dragging barrel corners is smooth again:** while you edit, a barrel shows without its bore and cuts and rebuilds them 0.6 s after you stop (a rebuild took about 50 ms every frame; it now takes 0.1 ms while dragging).
- **The bore sits in the middle of the barrel** (it was slightly off centre).
- **Rifling turns:** set how many *Turns* the rifling makes along the barrel. The panel says whether that spins the bullet fast enough: good, marginal, or too slow (it tumbles). On the range, a bullet spun too slowly goes wide and slows down; a marginal one groups wider; shot from a rifled barrel spreads more.
- Rifling is drawn as spiral grooves down the bore, and a chambered round points down the bore.
- Carving along a round part's axis snaps onto the axis.

### Fixes
- **Hand-loading a held-open single shot** (the 9 mm carbine): loading a round with the bolt held open no longer flicks it straight out again. R loads it, then your bolt release (1) closes the bolt and it fires.

## 0.3.4-alpha

### Buttons and levers you bind to keys
- **Why a control could do nothing:** every control is pushed one way, its *Push direction*, and it started at 180 degrees (backward), like pulling a trigger. A button that slides down (a bolt or slide release, a magazine catch) can't move backward, so holding its key did nothing.
- **A button now points the way its track goes:** make a part a Button and, if it already has a track, its push direction is set along it. Under Push direction, *Push along its track* does the same at any time.
- **Push direction says which way, in words** (90 degrees *down*, 180 *back*, and so on), and its tip explains it. The arrow on the part in the designer shows it too.
- **The bench tells you when a pressed control won't move:** hold its key and if it hasn't moved after a moment, a message names the part and its push direction, and suggests what may hold it (a stop, a catch or a spring stronger than the finger force).
- A control on a **Fixed** part now gets a warning: fixed parts can't move when pressed.
- Tested: a button on a forward track, pushed backward, moves 0.2 mm; pushed along its track it slides the full 4 mm.

## 0.3.3-alpha

- **Fixed: the pause menu on the firing range.** Its buttons couldn't be clicked: while paused, the mouse was put back in the middle of the screen every frame, so every click landed there. Now Esc opens the menu and every button works: Resume, Tutorials, Settings, **Leave the range** (it says so now, instead of "Main menu") and Quit.
- Esc closes the pause menu again (anywhere in the game, the same press can no longer open and close it at once).
- Clicking Resume no longer fires the gun, and the view doesn't jump when the mouse is taken back for aiming.

## 0.3.2-alpha

### Thickness corner by corner
- **Give any corner its own thickness:** select a part, pick a corner (or several, with Shift-click or a box), and set **Thickness here** in the Shape section. The part tapers evenly from corner to corner, so different thicknesses make wedges, tapers and stranger shapes.
- **Across** moves the middle of the picked corners sideways across the gun, to lean or bend a part in depth.
- It's real in the physics: parts collide by the tapered shape. Tested: a corner thinned to 4 mm on a 20 mm block narrows the part toward it, front and back.
- It works with the corner dragging in the Faces tool (it's the same shaping in depth), and *Straight through again* puts the part back to one thickness. Not for round or tilted parts; edges between corners stay straight.

## 0.3.1-alpha

- **The OAB Foundry logo** is now the launch splash: the studio's own logo on black, fading in and out over about three seconds and drawing slightly closer as it does. A key or a click still skips it.

## 0.3.0-alpha

### The firing range
- **Firing range** on the main menu is open. Choose where to shoot (the practice range, the map in your map editor, or a saved map), then which gun: your sandbox design, an example, or a saved blueprint.
- **Your gun in your hands, working for real.** It's the same simulation as the test bench: the parts move, rounds feed and extract, cases fly out, and a gun that jams on the bench jams here.
- **Controls:** left mouse fires, right mouse aims, R reloads (and racks a round in if the gun was empty), B works the action (your *Work the action* animation, or one made from your parts), M and your control keys as on the bench. WASD, Shift to sprint, C crouch, Z prone, Q/E lean, Esc for the menu.
- **Aiming** uses your pose: the eye where you posed it, looking over the top of the gun. Guns are sighted in at 25 m (held in one hand) or 100 m (against the shoulder).
- **Real shots:** each bullet, or each pellet from a shotshell, flies from the muzzle along the bore at the cartridge's speed, dropping with gravity and slowed by the air, and leaves a mark where it lands. The practice range has paper targets that score rings (*Hit: 9 at 15 m*) and steel plates that ring.
- **Recoil and weight:** the kick comes from the cartridge's push against the gun's weight, and is stronger in one hand than in the shoulder. A heavy gun comes up to aim more slowly and trails behind when you turn.
- Limits for now: no hands or arms are drawn in first person, the gun can poke through walls, and spent cases vanish soon after they fly out.

### Discord
- A **Discord** button on the main menu opens the invite to the community server.

### Opening blueprints
- **Open** now opens a window: your saved blueprints (newest first, with a search box) and the examples. Pick one to see what it is (parts, cartridge, how it's held, when it was saved), then **Open**, **Rename** or **Delete** it (Delete asks first). Double-click opens straight away.

### Tutorials
- **The building tools**: one big lesson replacing the five small ones (drawing, carving, depth, joints, springs). It goes through every tool in turn: Select, Rectangle, Polygon, Circle, Barrel, corners and edges, rounding, the part's settings, round parts, finishes, Faces, shaping in depth, Knife, Mirror, Measure, Carve, Engrave, Pin, Weld, Track, Spring, copying and the parts library.
- **Posing and animations**: a new lesson on Pose mode, the three holds, the fingers, and recording animations: keys, gripping parts, magazine moments, playing them on the gun, and using them with R and B.
- The test bench lesson ends with a pointer to the firing range.

## 0.2.20-alpha

### Gun finishes
- **Three real gun finishes are built into the game:** *Stained wood*, *Black polymer (stippled)* and *Blued steel*. They're always in a part's **Surface** list under *Gun finishes*, above your own pictures. They were made from samples of real firearm finishes, and their edges are blended so they repeat without a seam.
- Picking one sets the part's colour to white, so the finish shows in its true colours, and sets a natural size for the pattern. Pick a colour afterwards to tint it.

### Engraving pen (new Engrave tool, under Shape)
- **Draw on any part:** pick the Engrave tool, click a part to pick it, then draw on it with the mouse, like scrollwork or a signature. It works on any face (sides, top, ends, curved parts): turn the view to reach it.
- **Write text:** switch to *Text*, type it in the panel, and click where it goes; a faint preview follows the mouse first. It's written in single-line engraving letters: capitals, figures and the usual signs (small letters become capitals). Choose the letter height and turn it to any angle.
- **Erase:** click a line or a word to remove it. *Clear this part's engravings* removes them all.
- Line width, and what shows in the cut: bare metal, dark, gold inlay, white or red fill, or your own colour.
- Engravings stay on their part: they go with it when you move, turn, flip or mirror it, and they ride along on the test bench (a slide's markings move with the slide). Reshaping the part leaves them where they are. A copy made for the other side of the gun starts without them.
- They're for looks only: they never change how the gun works.

## 0.2.19-alpha

- **The studio is OAB Foundry:** the launch splash now reads *OAB Foundry* (it said *One Armed Bandit Games*). The developer stays *The One Armed Bandit*, as in the license agreement.
- The game's file on Windows (right-click *gunsmith3d.exe* > Properties > Details) shows *OAB Foundry* as the company, and the copyright as *The One Armed Bandit*.

## 0.2.18-alpha

### Spare magazines don't pile up
- **At most 6 magazines lie out on the bench at once.** A new one past that clears away the oldest one lying out, never the one in the gun or in your hand.
- **Magazines left alone are cleared away:** a magazine out of the gun that nobody touches for 20 seconds goes, full or empty. Before, only empty ones went (after 30 seconds), so full spares piled up for good. Picking one up starts its time again.
- Both are in **Settings > Test bench > Spare magazines** (1 to 20 lying out; 10 seconds to 2 minutes).
- Measured: every magazine lying out adds physics work each frame. On the blowback pistol it takes 4.8 ms a frame with 6 spares, 6.1 ms with 30 and 8.3 ms with 60, before drawing them.

## 0.2.17-alpha

- **The license agreement is now the real one** (*Gunsmith3D – Build and Shoot*, The One Armed Bandit, effective November 3, 2026), word for word. On screen its section headings are bold, its lists have bullets and its dividers are lines. It's also in *EULA.txt* next to the game.
- Players who already agreed to the earlier placeholder aren't asked again.

## 0.2.16-alpha

- **The license agreement is asked only once:** after a player agrees, they're never asked again, even when the agreement's text changes in a later version.

## 0.2.15-alpha

### At launch
- **Studio splash:** the game opens on *One Armed Bandit Games*, fading in and out over about three seconds. A key or a click skips it.
- **License agreement:** then, before the main menu, the End User License Agreement. Tick *I have read and agree*, then *I agree* to go on; *Decline* closes the game. It's asked once: the game remembers which agreement you agreed to, and asks again only when its text changes.
- The agreement is a temporary placeholder for now (a work in progress, no warranty, no liability, and not real engineering). The full one will replace it. The text is also in *EULA.txt* next to the game.

## 0.2.14-alpha

### Pick first, then move: everywhere
- **Every point needs a click before it moves:** a part's corners and edges, a joint's points (spring ends, track and slot points, pins, magazine well points), a magazine's round path points, the breech face and muzzle. The first click only picks it (it shows highlighted, and its numbers show in the panel); press on it again and drag to move it. Shift-click still adds corners to the selection.
- **Faces tool:** click a part, then click a face or corner to pick it, then drag it.
- **Pose mode:** the hand, butt and body dots and the fingertips are picked with one click and dragged with the next.
- **Map editor:** click a brush or a thing to select it, then drag it; a face's square is picked before it resizes.
- Shift-dragging an edge to pull out a step now only adds its new corners once the mouse really moves.

## 0.2.13-alpha

### No more moving things by accident (designer)
- **Click to select, then drag to move,** for everything: parts (as before), and now spring ends, tracks and their slot points, pins, magazine wells, and the breech face and muzzle markers. The first click only picks it (a breech face or muzzle gets a yellow ring); press on it again and drag to move it.
- **Faces tool:** the first click on a part selects it; then drag its faces or corners.
- **A small dead zone:** a press only becomes a drag once the mouse has moved a few pixels, so a click with a slight wobble never nudges a part, corner, face or handle.

## 0.2.12-alpha

### A more mobile hand (Test bench > Pose)
- **Drag a fingertip anywhere** and the digit follows the mouse: all its joints move together to get the tip there, within what a real hand can do. Dragging over the palm brings the thumb down onto the palm; drag it onto a fingertip to make an "OK" sign. **Shift-drag** still just curls it, as before.
- **The thumb swings across the palm** (new *Across the palm* setting under Fingers): it comes round in front of the palm and turns as it goes, so its pad meets the fingers' pads. Tested: the thumb can touch the tip of every finger, the little finger's too.
- **Real joint ranges:** each joint stops where a real one does. A finger's base joint bends from -25 to 95 degrees, its middle joint from 0 to 110. The thumb's root goes from -20 to 55 and its knuckle from -15 to 70. Its spread goes from -20 to 70, and it swings up to 100 across the palm. The fields in the panel keep to these ranges too.
- Animations blend the thumb's swing from key to key. Hands posed before keep their pose: they just have no swing across the palm.

## 0.2.11-alpha

### A real thumb
- **The thumb is built like a real one:** its first bone starts down by the wrist, beside the index finger's, and lies in the ball of the thumb; only its two short bones stand free. Before, it came out of the palm as three equal pieces, like a long fifth finger.
- **It turns the way a thumb does:** its nail faces sideways and it curls across the palm toward the little finger, so it closes against the fingers instead of folding down like them. *Spread* opens it out into an L.
- The ball of the thumb is fat at the wrist and narrows to its knuckle, with a web of skin to the index finger.
- **Holding a pistol grip,** the thumb now wraps round to the far side of the frame, above the fingers, as on a real grip, instead of lying along the near side like a long finger.
- In the finger panel, the thumb's joints are named *Root (by the wrist)*, *Knuckle* and *Tip joint*.
- Hands you posed before keep their bends; the thumb just looks and curls like a thumb now.

## 0.2.10-alpha

### Shaping parts from any view
- **Corners drag in the plane you're looking at**, not only from the side: look from the **top** and a corner moves forward, back and in depth; from the **front or back end** it moves up, down and in depth; from the side, as before. So the top of a part can be a different shape from its bottom: from above, pull the top corners in and leave the bottom ones where they are.
- Where corners sit on top of each other on screen (looking straight down, a top corner hides the bottom one), the one nearest you is picked.
- Z while dragging still moves a corner in depth from any view.

## 0.2.9-alpha

### Parts shaped in depth (Faces tool)
- **A different shape on the front and the back of a part:** with the Faces tool, the selected part shows its corners on both faces: **squares** on the front face, **circles** on the back. Drag one on its own and only that face changes shape; the sides between the faces follow as flat slopes. So a block can be tall at the front and low at the back, a grip can taper, a slide can be bevelled along its top.
- **Hold Z** while dragging a corner to move it in depth (deeper or shallower) instead of up, down, forward and back.
- The **Shape** section of the panel shows the dragged corner's X, Y and Z to type exact numbers, and **Straight through again** undoes all of it.
- **It's real in the physics:** the part collides by its true shape at every depth. Tested: a part pushed at a block that's tall at the front and low at the back stops against it at the front, and passes over it at the back.
- Adding or removing corners keeps the shape; turning the part turns it; mirrors across the gun swap its front and back.
- Limits: on a shaped part corners are sharp and edges straight (rounded corners come back when you make it straight again; *Chamfer* still works). Tilted and round (turned) parts can't be shaped this way yet. An older version of the game opens such a part straight through.
- The example guns behave exactly as before (same results to the last digit; each fires 20 of 20 with no jams).

## 0.2.8-alpha

### Working the action
- **Work the action** (new key **B** on the bench, and a button in the bench panel): a hand works the bolt, slide, lever or break-open, through the physics. With no animation recorded, it's worked out from your parts the way the reliability test works them: the hand takes the part marked *worked by hand* (or the part that carries the breech face), turns it if it turns about the bore, draws it all the way back, and brings it home (a part with a spring that shuts it is let go at the back); a part hinged to the frame is swung open and shut. Nothing is special-cased for any kind of gun: a bolt nobody has drawn before gets worked the same way.
- The firing hand works a part that turns (its handle is on that side); the support hand works the rest.
- Tested on every example: fired, then worked by hand, each has a fresh round in (the bolt turned up 90 degrees and back down), at a brisk hand's pace (a bolt in about 1.8 seconds).
- **Your own:** *Work the action* is a new animation slot. *Start from the automatic one* fills it with those keys to change as you like.

### Animations
- **Turning a bolt in your keys:** when a hand grips a part that turns about the bore, its key gets *Turns it* and *Turned* (degrees, 0 closed). The turn eases from key to key.
- **They follow the gun:** a gripping hand waits for its part to catch up (a slide still running back, a bolt still turning) before the animation moves on, up to a second; a part that's stopped (at its stop, or stuck) doesn't hold it up. The hand stays on the part as it moves.

## 0.2.7-alpha

### Hand animations (Test bench > Pose > Animations)
- **Record animations with keyframes:** *Reload*, *Reload (empty)*, *Inspect*, *Clear a jam*, and one for each control (a magazine catch, a safety, any lever or button). Tick *Edit an animation*, pick one, move the time slider and drag a hand: a key is made there. The hands start and end in the aiming hold and move smoothly through your keys (a curve through the points, not straight jumps); their fingers blend from key to key.
- **Hands work the real gun:** tick *grips its part* on a key and from there that hand holds the part it's on (a slide, a bolt handle, a catch) and moves it through the physics, until a later key lets go. So an animation can only do what the gun really allows: a slide pulled back too short won't chamber a round.
- **Magazine moments:** a key can *drop* the magazine or put a *fresh one in*, so a reload works without animating the magazine itself.
- **Preview** plays the hands alone; **Play on the gun** runs it for real. On the bench, **R** plays your *Reload* (or *Reload (empty)* when the gun is empty) and a control's key plays its animation. Anything not animated just happens, as before: nobody has to animate anything.
- Tested: a reload on the blowback pistol (drop the empty magazine, fresh one in, the support hand racks the slide) leaves it loaded and firing, whether the gun was empty or still loaded.
- Fixed on the way: a live round racked out slowly by hand used to drop onto the magazine's top round and shove it down into the magazine, so the slide closed on nothing (a failure to feed). It now lands on it, as empty cases already did.

## 0.2.6-alpha

### Fixes
- **Bullets and cases were see-through.** Rounds were drawn inside out: only their inside surfaces showed, so from most angles you looked straight into a hollow bullet. They're solid now (cartridges, cases and shotgun shells).

## 0.2.5-alpha

### Magazines anywhere
- **Magazine position** (a fitted magazine's panel): *Below*, *On top*, *Left side* or *Right side*.
- **On top:** the magazine feeds downward. The spring pushes rounds out of the magazine whichever way it's turned, the round still points at the muzzle, and a feed ramp tips its nose down into the chamber instead of up. Tested by turning the whole blowback pistol upside down: it feeds and fires 50 rounds (3 failures to eject: its ejection port is now underneath, and an odd case falls back in).
- **Left or right side** (like a Sten): the magazine sticks out across the gun with its rounds stacked sideways. This is a simplified treatment, because parts only move in the side view: the side view sees just its mouth, where the top round lies in the bolt's path. The breech face strips it forward as usual; anything riding over it pushes it back into the magazine sideways, and it pops out again behind. Released, the magazine slides out sideways and drops once clear; a new one slides in. Tested on the blowback pistol with its magazine moved to either side: 50 of 50, no jams, 6 reloads. Cut an opening in the receiver wall for it (Carve), or it goes through the wall.
- **Extractor** (Firing tab, with physical rounds): which edge of the rim the claw grips, *Top*, *Bottom* or *Automatic* (the edge away from the magazine: the top, unless the magazine is on top). It used to always be the top, so a gun with its magazine on top could never throw its cases out.
- Every example behaves exactly as before.

## 0.2.4-alpha

### Springs
- **Coil springs no longer fold in half.** A spring with nothing inside it (no guide rod) used to count as buckled once it was squeezed a quarter of the way and was longer than four times its width: it was drawn as two straight halves meeting in a sharp kink, and its push suddenly dropped to a third.
- Now it follows the spring makers' standard rule (EN 13906-1, ends seated flat): a spring up to about five times as long as it is wide never buckles; a longer one buckles after a squeeze that depends on how slim it is (about 40% for six widths long, about 12% for ten).
- A buckled spring still pushes: from the point it buckles it just gets much softer. It's drawn bowed out in a smooth arc (orange), still a proper coil, by up to two coil widths.
- The spring's panel says how far it can be squeezed before it buckles, and turns red when it's drawn past that.
- The examples' springs are unchanged (the long ones have guide rods). The blowback pistol with its guide rod taken out still runs: 50 fired, 1 failure to eject.

## 0.2.3-alpha

### Designer
- **Faces tool** (on the left, top of Shape): point at any face of any part, from any side (front, back, top, bottom, the ends) and it lights up. Drag to push or pull it: a side wall moves out or in, the front or back face makes the part thicker or thinner. An arrow shows which way is out; a face that looks straight at you comes out as you move the mouse up.
- **Extrude, like Sprocket:** point at a face and press **E**, move the mouse to draw it out, click to finish (right-click or Esc cancels). A side wall (top, bottom, an end) gets two new corners and grows a step. The front or back face grows a new piece out of it, welded on, as deep as you pull it: reshape its outline afterwards for bosses, ribs and steps. E also works in the Select tool on the selected part, and Shift-drag extrudes in the Faces tool.
- **Handles from either side:** corners and edge handles now sit on whichever face is toward you, so a part can be shaped from behind as well as from the front.
- **Live mirror:** *Copy to the other side* and the Mirror tool now make a linked pair. Change either one and the other follows as you drag: shape, cuts, depth, thickness, tilt and look (how each moves and what it's joined to stay its own). The part's panel shows its partner, *Select the other*, and *Unlink*.
- **Up to 1024 corners a part** (was 32). The physics now only looks at the edges near each touching point, so detailed parts cost little: a frame or slide drawn with 1024 corners runs as fast as before, and even every slide and frame part of a pistol at 1024 corners runs about 2x slower rather than 57x. The examples behave exactly as before.

### Fixes
- The hint line at the bottom of the designer read past the end of its list with the Barrel and Measure tools: it could show garbage or crash. Every tool has its hint now.

## 0.2.2-alpha

Playtester bugs fixed, and the first batch of editor ideas from playtesting.

### Designer
- **Merge, Keep overlap, Cut away** (a part's panel, *Combine with another part*): pick another part, then merge the two into one outline, keep only where they overlap, or cut the other's outline out of this one. The other part is used up unless you tick *Keep the other part*. Corners land exactly where they should (the two outlines' own corners and the points where their edges cross) and rounded corners stay rounded, so a mechanism keeps working: the examples' sears, triggers, slide walls and frames all go through it and still fire 50 rounds without a jam. A merge takes the depth of both parts together, and the game tells you when they were at different depths. A cut that would only leave a hole in the middle says so (an outline can't have holes: use Carve). Ctrl+Z undoes it.
- **Chamfer** (Shape section, under Round corners): cuts corners off flat by the amount you set, back along each edge: all corners, or only the ones you've selected.
- **Copy and paste between guns:** Ctrl+C copies the selected part with its welded piece and the joints between them; Ctrl+V pastes it, in this gun or another one (it goes through the system clipboard, so even between two copies of the game). Also *Copy* in the part's panel.
- **My parts:** *Save to my parts* in a part's panel keeps it (with its welded piece) in your own library, in *user/parts* in the game folder. **Parts** in the top bar drops any of them into the gun you're working on. Share them by sending the files.
- **Measure tool** (on the left, under Mirror): drag between two points for the length, how far across and up, and the angle. It snaps to corners and joints, and the line stays until you measure again.
- **Position X and Y** for a whole part (in its panel), alongside Z: type where its middle is, and its welded piece moves with it.
- **A better dressed workbench:** ammo boxes, a few loose casings, a gun mount under the gun, gun oil cans and a magazine rack.

### Fixes (from playtesting)
- **Magazines no longer fall out after a while.** After about 200 reloads the bench filled up with dropped magazines and no new one could go in. Now the oldest clutter on the bench (empty cases, then dropped magazines, then loose rounds) is cleared away to make room.
- **Changing a magazine's capacity no longer turns it over.** A magazine whose well pointed upward (a negative drop-out travel in older versions, or the well's end dragged above its start) flipped over to feed from the top. Now it only turns when you set its *Well angle* (180 for a magazine on top, on purpose).
- **Dragging a corner far out:** a drag can no longer throw a corner kilometres away (or snap it back to the middle) when the view looks along the drawing plane, and parts stay within 5 m.
- **Crash reports (Windows):** if the game ever crashes it says so and leaves a report in the game folder under *user/crashes* (a short text file and a .dmp). Send both and the exact spot can be found.

## 0.2.1-alpha

The editor and the test bench are worked out (0.1.0-dev, builds dev1 to dev25). Alpha 0.2 adds barrels, rotation in any axis, and first-person shooting.

### Designer
- **New** in the top bar, next to Open: start a new design (Undo brings back what you had).
- **Select first, move second:** the first click on a part only selects it. Drag a part that's already selected to move it.
- **Parts inside other parts:** click a selected part again (without dragging) to select the next part behind it under the cursor, and again for the one behind that. The first click tells you how many are there. (The Outline tab lists every part too.)
- **A workbench for scale:** the gun now sits on a wooden bench in the designer, with a **steel ruler** in front of it (marked in mm with cm numbers, or in 1/16" with inch numbers) and a hammer, a screwdriver and a mug for a sense of real size. Switch it off in the background (palette) menu or Settings > Designer.

### Barrels
- **The Barrel tool** (on the left, under Rectangle): drag from the breech end to the muzzle. You get a turned barrel, thicker over the chamber, with a **chamber cut for its cartridge** at the breech end and a **bore** right through to the muzzle (real holes you can look into).
- In the barrel's panel: **Chambered for** (any cartridge, yours too), **Rifled** or **Smoothbore**, the rifling's **twist** (1 turn in so many inches) and **grooves** (cut into the bore, so you can see them at the muzzle), and *Swap breech and muzzle*. Shotshells start smoothbore. What rifling does to accuracy comes with the firing range.
- **Barrels set the firing system:** each barrel's chamber mouth and muzzle come from the barrel itself, so there's nothing to place. Firing lists the barrels; pick what closes each one's breech. Several barrels make several chambers, fired in turn or all at once.
- **Each barrel can take its own cartridge**: its chamber, its rounds and its recoil follow that cartridge. A magazine feeds the first barrel.
- Guns without barrel parts (all the examples, and anything saved before) keep their placed breech face and muzzle, and work as before.

### Turn and tilt parts
- In a part's panel: **Turn (Z)** turns it in the side view (by 90 or 15 degrees, or any angle you type), with its cuts and whatever is pinned or attached to it; a welded piece turns together.
- **Tilt X** and **Tilt Y** lean it out of the side view (X about the gun's length, Y about the upright), up to 85 degrees. The physics slices the tilted shape through its depth, so it collides as it's drawn. For now a tilted part's own cuts show but don't collide, and its corners are edited as drawn, before the tilt.

## 0.1.0-dev (dev1 to dev25)

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

### Hands with fingers
- In Pose mode each hand is now a real **hand skeleton**: a palm, a thumb and four fingers with three joints each. Every joint bends and each digit can spread.
  - Open a hand's **Fingers** section to pick a preset (wrapped round, finger on the trigger, finger off the trigger, open, fist) and set each joint exactly. **Zoom to this hand** brings the camera in close.
  - In the view, click a hand to show its fingertips, click a fingertip to pick that digit, and drag up or down to curl it.
  - The bones have names (thumb_01 ... pinky_03, palm_index ...), each with a start, an end and an up direction: the framework a modelled hand will attach to. Finger poses save with the gun.
  - The firing hand now starts with its finger curled onto the trigger.

### Views and coordinates
- **Orthographic from any angle:** the cube button in the top bar opens a View menu. Switch *Orthographic* on and turn the view any way you like (no perspective, for lining things up). Or look straight at the right side, left side, top, bottom, front or back.
  - Keys: **V** side view (again: back to 3D); number pad **5** orthographic on/off, **1** front, **3** side, **7** top, with **Ctrl** for the opposite side.
  - Orbiting no longer drops you out of the flat view.
- **X, Y and Z everywhere:** a part's offset across the gun is now **Z (offset)**. A cut's position is **X, Y, Z** (its section is open by default). The pose's eye is Eye X, Eye Y, Eye Z. **Y is shown with + up** (it used to show + down for corners); only the numbers shown change, not your saved guns.

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
