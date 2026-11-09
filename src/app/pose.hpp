// Gunsmith3D - the pose editor (Test bench > Pose): how a person holds the gun.
// A plain upper-body mannequin stands with the gun. You drag the hands and the stock's butt onto the gun, and for each
// hold (aim, hip, lowered) place the body against it. The arms bend to reach the hands and turn red when they can't.
// What it edits is Design::pose (core/design.hpp); the first-person mode uses the same pose.
#pragma once
#include "raylib.h"
#include <string>

struct PoseReport {
    double rightShort = 0, leftShort = 0;   // mm the arm is short of the hand (0: it reaches)
    bool buttIn = false;                    // the butt is in the shoulder pocket
    double buttOff = 0;                     // mm from the pocket
};

void enterPose();
void leavePose();
void poseView();                 // aim the camera at the mannequin and the gun
Matrix poseGunMatrix();          // gun (bench world) -> pose world: the current hold's muzzle tilt about the eye
void drawPoseBody();             // the mannequin, its arms and hands (call outside the gun's matrix)
void drawPoseOverlay();          // handles and labels
void poseInput(bool overUI);
PoseReport poseReport();
void poseDefaultHands();
void poseBedShoulder();          // move the current hold's shoulder pocket onto the butt
bool viewGun(double g[6]);       // the gun moved by an animation (playing, or on the timeline being edited)
Matrix gunMoveMatrix(const double g[6], Vector3 pivot);
struct Pose; struct Animation; struct HandHold; struct AnimKey;
Pose viewPose();                 // the pose as shown: the hold, or an animation where it has got to
int animKeyAt(const Animation& a, double t);   // the key at this moment on the timeline, or -1
HandHold* editHand(int which);
AnimKey* editKey();              // editing an animation: its key at the timeline's moment (made there if needed)   // the hand dragging edits (making an animation key at the timeline's moment if needed)
void poseFocusHand(int hand);   // close up on a hand (0 firing, 1 support)         // put any missing firing hand where a hand would most likely go
