#pragma once

#include <string>
#include <vector>

#include "Ascension/Data/Database.h"

namespace Ascension {

struct PlaytestOptions;

// One focus-fire run of the goblin pack. FocusId is the foe the policy kills first.
// FirstDown is the first foe that actually fell, which can differ if a guard kills
// someone else. Ok is false when the hero could not be built.
struct GoblinSimRow {
    std::string Build;
    std::string FocusId;
    bool Fang = false;
    bool Ok = false;
    bool Victory = false;
    bool TimedOut = false;
    i32 Hp = 0;
    i32 MaxHp = 0;
    i32 PlayerTurns = 0;
    std::string FirstDown;
    std::string Error;
};

// Level 6, rusty sword and oak shield, the level-6 pick already kept, and the uses a
// hero would have from the fights before this one. Each build is run with and without
// Alpha's Fang. Shaman focus kills the grunt next, so the chief is left alone. Chief
// focus kills the shaman next. One fight each: damage and foe AI do not use a seed.
[[nodiscard]] std::vector<GoblinSimRow> SimulateGoblinPack(const Database& data);

[[nodiscard]] std::string FormatGoblinSim(const std::vector<GoblinSimRow>& rows);

// One row of a seeded rate table. Focus "default" marks a uniform living foe every
// turn. Any other focus is the kill order named in the plan. Runs is how many distinct
// fights were played: a named focus with slip 0, and any fight with one enemy, is one
// outcome, so Runs stays 1 however large the request is. HP left on a loss or a timeout
// is 0. P10Hp is the sorted sample at index (runs - 1) * 10 / 100.
struct SimRateRow {
    std::string Encounter;
    std::string Build;
    std::string Focus;
    bool Fang = false;
    i32 Runs = 0;
    i32 Wins = 0;
    i32 Timeouts = 0;
    i64 SumHp = 0;
    i64 SumTurns = 0;
    i32 P10Hp = 0;
    bool Ok = false;
    std::string Error;
};

// Runs is the request. The harness plays that many only when the seed can change the
// mark. Seed 0 is treated as 1. Slip is the percent of player turns that ignore a named
// focus and mark a random living foe instead. 0 keeps the kill order exact.
struct SimRateQuery {
    std::string EncounterId;
    i32 Runs = 1000;
    i32 Seed = 1;
    i32 Slip = 0;
};

// Goblin pack: shaman, chief, grunt, then default. Wolf pack: dire wolf, wolf, then
// default. A one-enemy encounter is a single row. Other encounters walk their member
// ids and add default when more than one foe can be marked.
[[nodiscard]] std::vector<SimRateRow> SimulateEncounterRates(const Database& data,
                                                            const SimRateQuery& query,
                                                            std::string& error);

[[nodiscard]] std::string FormatSimRates(const std::vector<SimRateRow>& rows,
                                         const SimRateQuery& query);

// --sim. No --class prints the rate table for --enemy. A class or a focus plays that
// one row. 0 when every row was built. Losses are still 0: the table is the result.
[[nodiscard]] int RunPlaytestSim(const PlaytestOptions& options);

} // namespace Ascension
