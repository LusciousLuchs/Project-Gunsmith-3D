// Gunsmith3D - Sound effects: the owner's recordings for shots and the action (assets/sounds), mixed for each gun by
// core/shotsound.cpp; synthesised clicks for the rest.
#pragma once
#include "app/app.hpp"
#include "shotsound.hpp"

struct Sfx { Sound bang{}, clack{}, click{}, tick{}; bool ok = false; bool recorded = false; };  // tick: interface
void uiTick();  // the soft interface click
extern Sfx SFX;

void initSfx();
// A shot from this gun's chamber k (its cartridge and barrel); vol scales it (distance, say)
void playShot(const Design& d, int chamber = 0, float vol = 1);
void playShotMix(const ShotMix& m, float vol = 1);
// The action: a part of mass kg (its whole welded assembly) slamming home or throwing a case; speed m/s, <0 unknown
void playAction(double massKg, double speed = -1, float vol = 1);
double barrelLength(const Design& d, int chamber = 0);   // face to muzzle, mm (-1 without a muzzle)
double actionMass(const sim::Sim& S);
void actionEventSound(const sim::Sim& S, sim::Ev kind);   // a case out, a round chambered: by the action's speed (hand: the bolt recordings)
void actionRollSound(const sim::Sim& S);                  // every frame: a turning bolt's lift and lock clicks                    // the breech's whole welded assembly, kg (-1 if none)
