// Gunsmith3D - Career mode in the app: the career kept in the user folder, your blueprints with their rating sheets,
// and the bench test run in the background (rating a gun, or testing a contract's delivery). The rules are in
// core/career; the screen is ui/panel_career.
#pragma once
#include "career.hpp"
#include "map.hpp"
#include "raylib.h"

#include <string>
#include <vector>

extern career::Career CAREER;
bool careerExists();                 // a career saved in the user folder
// several careers, each a folder in the user folder's Career/ (its career.json, its workshop, its saved workshop copies)
struct CareerSlot { std::string name; int day = 1; double money = 0, reputation = 0; long long when = 0; };
std::vector<CareerSlot> careerSlots();   // the newest played first
std::string careerName();                // the one being played
void careerContinue(const std::string& name);   // load it and go to your shop
void careerBegin(const std::string& name);      // a new career under this name (made unique) and go to your shop
bool careerDelete(const std::string& name);
// your workshop's saved copies (Career/<name>/Workshops/<copy>.gsmap)
std::vector<std::string> careerWorkshopCopies();
std::string careerWorkshopCopyFile(const std::string& copy);
void careerStartNew();
bool careerLoad();
void careerSave();
void enterCareer();                  // the office (Screen::Career), loading or starting the career
bool inCareer();                     // in career mode (the office, or the designer opened from it)
void careerBackToOffice();           // from the designer
void careerOpenWorkshop(bool walk);  // your workshop in the level editor (walk: straight in, on foot)
void careerWorkshopToOffice();       // back from it: its workstations counted
void careerCountStations();          // every property's stations, store rating and displayed guns, from their map files
void careerNoteMap(const gmap::Map& m, int propertyId);   // ...one property's, from its map as it is now
void careerGoTo(int propertyId);     // travel to another of your properties (on foot there)
std::string careerPropertyFile(int propertyId);
void careerDemoStart(int contractId); // a big order's range demonstration, with its gun
void careerTick();
// a customer's face at your counter: a person from the character maker, their look picked from their number
void careerRenderPortraits();         // (before the panels each frame: draws any not drawn yet)
Texture2D careerPortrait(int walkinId);
struct NpcDef;
const NpcDef& careerPerson(int id, const std::string& name = "");  // a person (a customer by their number, staff by a number from their name), kept
                    // every frame: a finished demonstration takes you back to the office

// your guns (the blueprints in the user folder), each read once and kept until its file changes
struct CareerGun { std::string name; Design d; career::GunSheet fresh; };   // fresh: the sheet from the design as it is now
const std::vector<CareerGun>& careerGuns();
const CareerGun* careerGun(const std::string& name);

// the bench test in the background: rate a gun (its sheet goes into the career), or test one for a contract
void careerTestStart(const std::string& gun, int contractId = -1);
bool careerTestRunning(float* progress = nullptr, std::string* gun = nullptr);
void careerTestPoll();               // every frame: a finished test is applied (rated, or delivered/turned down)
void careerShutdown();
