# Backlog

Bugs and ideas from playtesters, kept here so nothing gets lost. Newest at the top of each list.

## Bugs (from playtesting, 0.2.1-alpha)

- [ ] **Crash: dragging a polygon corner too far.** Dragging a corner far enough to make the part very large crashes the game. (Smiffy)
- [ ] **Magazines fall out of the gun after a while** on the test bench. (Smiffy)
- [ ] **Changing a magazine's capacity flips it round** so it feeds from the wrong end (top feeds). (Smiffy)

## Editor ideas (Maulwurf)

What's there already is noted; the rest is new work.

- **Extrude**: exists for edges (Shift-drag an edge's diamond). Could add extruding a whole face in depth.
- **Bevel**: corners can be rounded (Round corners) and front and back edges softened (Smoothness). A true bevel (flat chamfer) on edges is new.
- **Boolean**: Carve subtracts shapes. Adding one part to another (union) and keeping only the overlap (intersect) are new.
- **Mirror and asymmetry**: the Mirror tool and *Copy to the other side* exist. A live mirror (edit one side, the other follows, with a switch to break it) is new.
- **Coordinates**: X, Y, Z fields exist for corners, cuts and part offsets. Could add them for the whole part (its position) and for joints.
- **Merge**: Weld makes parts move as one. Merging two parts into one outline is new.
- **Measurements**: the workbench ruler is there. A measuring tool (drag between two points for length and angle) is new.
- **Thickness**: exists per part. Could add changing it by dragging in the view.
- **Copy**: Duplicate (Ctrl+D) exists. Copy and paste between designs is new.
- **Merge different parts**: see Merge.
- **Save parts across guns** (like Sprocket's structures): a library of your own parts or assemblies (a trigger group, a magazine, a barrel) to drop into any design. New.

## Workbench idea (Smiffy)

The designer's bench, dressed up: ammo boxes, a few loose shell casings, a gun mount (a vise or cradle the gun sits in), gun oil cans, a magazine rack. Today it has a ruler, a hammer, a screwdriver and a mug.

## Shelved

- **Signing the Windows build** (so it runs without the blue SmartScreen prompt): not for now. Options when wanted: Microsoft Trusted Signing (about $10 a month, identity checks, US/Canada so far) or a code-signing certificate ($100-400 a year).

## Next

- **Firing range**: first person, walking the range with your gun, using its poses; weighty handling (inertia, sway, time to aim), holstering, recoil from the bench, live-fire jams from the measured jam rate.
