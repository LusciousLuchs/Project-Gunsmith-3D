# Backlog

Bugs and ideas from playtesters, kept here so nothing gets lost. Newest at the top of each list.

## Bugs (from playtesting, 0.2.1-alpha)

- [~] **Crash: dragging a polygon corner too far.** Dragging a corner far enough to make the part very large crashes the game. (Smiffy) Couldn't reproduce (dragged far in side and 3D views, loaded parts kilometres wide, on the designer and the bench). Made safer: a drag can't throw a corner more than 5 m out or to nowhere when the view looks along the drawing plane. Windows crashes now leave a report in user/crashes to send in.
- [x] **Magazines fall out of the gun after a while** on the test bench. (Smiffy) Found: after about 200 reloads the bench was full of dropped magazines and no new one could appear. Now the oldest clutter (cases, dropped magazines, loose rounds) is cleared to make room.
- [x] **Changing a magazine's capacity flips it round** so it feeds from the wrong end (top feeds). (Smiffy) Found: the magazine re-read its direction from its well, and a well whose drop-out end pointed up (a negative drop-out travel in older builds, or its end dragged up) turned it over. Now only the Well angle turns it (180 for a top magazine, on purpose).

## Editor ideas (Maulwurf)

What's there already is noted; the rest is new work.

- [x] **Extrude**: Shift-drag an edge's diamond; the Faces tool and E extrude any face, Sprocket-style (0.2.3).
- [x] **Bevel**: corners can be rounded (Round corners) or cut flat (Chamfer, 0.2.2); front and back edges softened (Smoothness). Still new: a flat bevel on the front and back edges.
- [x] **Boolean**: Merge, Keep overlap and Cut away between two parts (0.2.2). Carve still makes cuts partway in and holes.
- [x] **Mirror and asymmetry**: the Mirror tool and *Copy to the other side* make live mirrors, with Unlink for asymmetry (0.2.3).
- [x] **Coordinates**: X, Y, Z for corners, cuts, and now a whole part's position (0.2.2). Could add them for joints.
- [x] **Merge**: Weld makes parts move as one; Merge makes two parts one outline (0.2.2).
- [x] **Measurements**: the workbench ruler, and the Measure tool (0.2.2).
- [x] **Thickness**: per part, and by dragging the front or back face with the Faces tool (0.2.3).
- [x] **Copy**: Duplicate (Ctrl+D), and Ctrl+C / Ctrl+V between designs (0.2.2).
- [x] **Merge different parts**: see Merge.
- [x] **Save parts across guns** (like Sprocket's structures): *Save to my parts* and **Parts** in the top bar (0.2.2).

## Workbench idea (Smiffy)

- [x] The designer's bench, dressed up: ammo boxes, a few loose shell casings, a gun mount, gun oil cans, a magazine rack (0.2.2).

## Noticed while testing

- **A mirror image of a gun doesn't behave exactly like the original.** The blowback pistol turned upside down, with gravity turned over too, jams about 9 times in 100 against 2 for the original. Found: where a contact point sits exactly as close to two edges of a part, the physics picks the first edge in the part's list, and mirroring a part reverses its list. Worth making that choice independent of the order some day; it doesn't affect any example.
- **The 50-round test is touchy on a knife-edge design.** The tilting pistol's sear, unchanged but with its corners listed from a different starting corner, can give 2 jams in 50 instead of 0. The run is the same every time for the same design, but tiny differences can tip it. A spec sheet should run a few seeds and give a range.

## Shelved

- **Signing the Windows build** (so it runs without the blue SmartScreen prompt): not for now. Options when wanted: Microsoft Trusted Signing (about $10 a month, identity checks, US/Canada so far) or a code-signing certificate ($100-400 a year).

## Map editing tools: levels and NPCs (the owner's plan, 0.3.9)

The goal: guns, the places to use them, and things to fight, all made by players. Think a flatscreen H3VR: build any
level and any enemy, from realistic soldiers to things from somewhere else. Under **Map editing tools** on the main
menu: the **Level editor** (the map editor) and the **NPC editor**. The tools are generic; what players build with them
is up to them, and the game's own examples stay original.

- [x] **Menu:** Map editing tools, with Level editor and NPC editor (0.3.9).
- [x] **NPC bodies:** any number of bones, each hanging off another anywhere along it; length, thickness at each end,
  shape (rounded, box, ball), colour; posed by bend, turn and twist or by dragging a bone's end; mirror a limb to the
  other side; weak spots and armour per bone. Starter bodies: human, hound, six-legged crawler, blank. Saved in user/npcs (0.3.9).
- [x] **Fight settings saved with it:** health, speed, attack (up close, with a gun, explodes), damage, reach, sight,
  behaviour (stand, wander, chase, guard), team (0.3.9). Not used yet.
- [x] **NPCs on maps:** the NPC tool in the level editor (which NPC, how many); a map file carries copies of the NPCs it
  uses, installed into user/npcs when it's opened or played; on the range they stand where placed and bullets report
  which bone they hit (0.4.0). Still to do: when they appear (at the start, in waves, by triggers).
- [x] **NPC guns:** Firing > Made for NPCs (rate of fire, shots per pull, spread; no reloading, never runs out, not
  simulated); kept out of the player's gun lists; chosen in the NPC editor (0.4.0); NPCs fire them (0.4.1).
- [x] **Inventory and gun spawners:** carry 4 guns (wheel, G drops, F uses), guns dropped on the ground keep their state;
  the gun spawner stand on any map (0.4.0). Still to do: the dropped gun drawn as itself, not a case; ammo pickups.
- [x] **Groups:** save selected brushes and an object as a group in user/groups; place it again in any map (0.4.0).
  Still to do: spawning groups during play (from a spawner).
- [x] **NPCs alive on the range:** walk (the player's movement, sized to them), go round what's in their way, see the
  player (sight distance, facing, line of sight), hear gunshots (50 m) and alert each other, act on their behaviour,
  attack (melee, explode, or shoot their NPC gun through the same ballistics as the player's) (0.4.1). Path-finding
  over the whole map, upper floors, stairs and drops, a grid per NPC size (0.4.2). Still to do: jumping up onto
  things, doors, NPCs keeping out of each other's way (they can bunch up in one spot), taking cover, attack animations.
- [x] **Getting hit:** bullets hit bones (each bone a capsule), damage by the bullet's energy (weak spots x3, armour),
  death: the NPC falls over stiffly (0.4.1). Still to do: a ragdoll (the bones going limp), bodies cleared after a while.
- [x] **The player can be hurt:** 100 health, a red flash, death and restart; NPC guns' *Damage to you* (0.4.1).
  Still to do: health pickups, healing.
- [ ] **Movement from the pose tools:** walk, run and attack cycles as keyframes on the bones (the animation timeline
  from the hand animations), with automatic leg stepping for any number of legs as a default.
- [ ] **Hands and held guns for NPCs:** a hand on any bone, holding a blueprint the way the player's pose holds it.
- [ ] **Level logic:** waves (rounds of spawns that grow), triggers (walk into an area, open a door), doors, pickups
  (ammo, guns, health), objectives, a score.
- [ ] **Level editor additions:** props and lights, more brush shapes, sounds, sharing maps and NPCs as files.
- [ ] **NPC editor additions:** textures on bones (the same pictures as parts), bones shaped like parts (draw an outline,
  like the gun designer), eyes and glows, sounds.

## Alpha 0.4.20: a better player character (the owner's notes, after attachments stage 4) — done in 0.4.20

- **Fingers**: the character's hands get fingers, three cylinders each, coming from the hand, moved by the poser's finger
  skeleton (the animations already pose them; nothing draws them on the character yet).
- **A simpler character editor**: remove setting a weak spot and an armour level per piece (armour is the armour bar now);
  fewer, plainer controls.
- **Male or female**; a female body has a protruding chest.
- **Hair styles**, **eyes** on the head, a **nose** (a trapezoid will do). Less low-poly overall.
- **Clothing**: long-sleeve shirt, short-sleeve shirt, tank top; long or short pants.
- **Extras**: belt, hat, headphones, glasses.
- The owner may have proper player models made later: keep the character's parts easy to swap for a model.

## Next

- **Firing range**: first person, walking the range with your gun, using its poses; weighty handling (inertia, sway, time to aim), holstering, recoil from the bench, live-fire jams from the measured jam rate.
