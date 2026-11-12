// Gunsmith3D - career mode on foot: you walk round your shop in first person (the level editor's walking, on your
// workshop map). Walk up to something and press F: the office computer (online store, contracts, staff, building),
// a workstation (your guns: rate them, build copies), the counter (the customer in front of you) or the front door
// (close up and end the day) opens its screen. Customers stand at the counter and your staff at their workstations,
// as people. B switches to build mode (the level editor's tools on the same map) and back.
#pragma once

bool shopActive();          // career mode, in your shop (walking or building)
bool shopWalking();         // ...and on foot
void shopEnter();           // (after the workshop map is opened, on foot) the computer and door added where missing
bool shopWalkUpdate(float dt);   // every frame on foot, before moving: true if a screen is open (no walking then)
void shopDrawPeople();      // in the 3D pass: customers at the counter, staff at their stations
void shopHud();             // after the 3D pass: day, money, what F does here
void shopWindows();         // in the ImGui pass: the open thing's screen
void shopBuildMode();       // to building (the level editor), remembering where you stood
void shopWalk();            // back on foot where you were (the workstations counted again)
void shopOpen(int thing);   // open a screen (0 computer, 1 workstation, 2 counter, 3 door)
void shopCloseThing();      // close whatever screen is open
void shopNewDay();          // after the day ends: a word about it
void cursorGuard();         // every frame: hide the cursor only while looking around on foot, show it everywhere else
