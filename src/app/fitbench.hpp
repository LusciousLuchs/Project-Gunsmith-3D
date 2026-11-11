// Gunsmith3D - The fitting bench (on the test bench): attachments from the Attachments folder onto the gun's rails and
// attachment points, checked as they go (core/fitting.cpp); loadouts saved to the Loadouts folder.
#pragma once
#include "app/app.hpp"
#include <string>
#include <vector>

std::vector<std::string> attachmentFiles();          // names in the Attachments folder
bool loadAttachment(const std::string& file, Design& out);
void enterFitting();
void leaveFitting();
void fitInput(bool overUI);
void drawFitOverlay();                               // sites, the picked one's spot and why it can't go
void refitAll();                                     // after the gun changed: put each one back where it was (drop those whose mount is gone)
std::string siteLabel(const std::string& key);       // "Picatinny rail, slot 7" in words
double attachmentMass(const Design& att);            // grams
Matrix fitMatrix(const fit::Item& it);               // its attachment's drawing frame -> the gun's (at rest)
std::string fitCarrier(const fit::Item& it);         // the gun part it rides on (through adapters)
bool saveLoadout(const std::string& name);
bool loadLoadout(const std::string& name);
std::vector<std::string> loadoutFiles();
