// Gunsmith3D - Career mode in the app: the career kept in the user folder, your blueprints with their rating sheets,
// and the bench test run in the background (rating a gun, or testing a contract's delivery). The rules are in
// core/career; the screen is ui/panel_career.
#pragma once
#include "career.hpp"

#include <string>
#include <vector>

extern career::Career CAREER;
bool careerExists();                 // a career saved in the user folder
void careerStartNew();
bool careerLoad();
void careerSave();
void enterCareer();                  // the office (Screen::Career), loading or starting the career
bool inCareer();                     // in career mode (the office, or the designer opened from it)
void careerBackToOffice();           // from the designer

// your guns (the blueprints in the user folder), each read once and kept until its file changes
struct CareerGun { std::string name; Design d; career::GunSheet fresh; };   // fresh: the sheet from the design as it is now
const std::vector<CareerGun>& careerGuns();
const CareerGun* careerGun(const std::string& name);

// the bench test in the background: rate a gun (its sheet goes into the career), or test one for a contract
void careerTestStart(const std::string& gun, int contractId = -1);
bool careerTestRunning(float* progress = nullptr, std::string* gun = nullptr);
void careerTestPoll();               // every frame: a finished test is applied (rated, or delivered/turned down)
void careerShutdown();
