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
void poseDefaultHands();         // put any missing firing hand where a hand would most likely go
