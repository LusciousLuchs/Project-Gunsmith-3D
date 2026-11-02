# Backlog

Bugs and ideas from playtesters, kept here so nothing gets lost. Newest at the top of each list.

## Bugs (from playtesting, 0.2.1-alpha)

- [~] **Crash: dragging a polygon corner too far.** Dragging a corner far enough to make the part very large crashes the game. (Smiffy) Couldn't reproduce (dragged far in side and 3D views, loaded parts kilometres wide, on the designer and the bench). Made safer: a drag can't throw a corner more than 5 m out or to nowhere when the view looks along the drawing plane. Windows crashes now leave a report in user/crashes to send in.
- [x] **Magazines fall out of the gun after a while** on the test bench. (Smiffy) Found: after about 200 reloads the bench was full of dropped magazines and no new one could appear. Now the oldest clutter (cases, dropped magazines, loose rounds) is cleared to make room.
- [x] **Changing a magazine's capacity flips it round** so it feeds from the wrong end (top feeds). (Smiffy) Found: the magazine re-read its direction from its well, and a well whose drop-out end pointed up (a negative drop-out travel in older builds, or its end dragged up) turned it over. Now only the Well angle turns it (180 for a top magazine, on purpose).

## Editor ideas (Maulwurf)

What's there already is noted; the rest is new work.

- **Extrude**: exists for edges (Shift-drag an edge's diamond). Could add extruding a whole face in depth.
- [x] **Bevel**: corners can be rounded (Round corners) or cut flat (Chamfer, 0.2.2); front and back edges softened (Smoothness). Still new: a flat bevel on the front and back edges.
- [x] **Boolean**: Merge, Keep overlap and Cut away between two parts (0.2.2). Carve still makes cuts partway in and holes.
- **Mirror and asymmetry**: the Mirror tool and *Copy to the other side* exist. Still new: a live mirror (edit one side, the other follows, with a switch to break it).
- [x] **Coordinates**: X, Y, Z for corners, cuts, and now a whole part's position (0.2.2). Could add them for joints.
- [x] **Merge**: Weld makes parts move as one; Merge makes two parts one outline (0.2.2).
- [x] **Measurements**: the workbench ruler, and the Measure tool (0.2.2).
- **Thickness**: exists per part. Could add changing it by dragging in the view.
- [x] **Copy**: Duplicate (Ctrl+D), and Ctrl+C / Ctrl+V between designs (0.2.2).
- [x] **Merge different parts**: see Merge.
- [x] **Save parts across guns** (like Sprocket's structures): *Save to my parts* and **Parts** in the top bar (0.2.2).

## Workbench idea (Smiffy)

- [x] The designer's bench, dressed up: ammo boxes, a few loose shell casings, a gun mount, gun oil cans, a magazine rack (0.2.2).

## Noticed while testing

- **The 50-round test is touchy on a knife-edge design.** The tilting pistol's sear, unchanged but with its corners listed from a different starting corner, can give 2 jams in 50 instead of 0. The run is the same every time for the same design, but tiny differences can tip it. A spec sheet should run a few seeds and give a range.

## Shelved

- **Signing the Windows build** (so it runs without the blue SmartScreen prompt): not for now. Options when wanted: Microsoft Trusted Signing (about $10 a month, identity checks, US/Canada so far) or a code-signing certificate ($100-400 a year).

## Next

- **Firing range**: first person, walking the range with your gun, using its poses; weighty handling (inertia, sway, time to aim), holstering, recoil from the bench, live-fire jams from the measured jam rate.
