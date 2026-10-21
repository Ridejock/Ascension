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
// focus kills the shaman next.
[[nodiscard]] std::vector<GoblinSimRow> SimulateGoblinPack(const Database& data);

[[nodiscard]] std::string FormatGoblinSim(const std::vector<GoblinSimRow>& rows);

// --sim. The goblin pack with no --class prints the matrix above. Any other setup
// plays that one fight with the same policy. 0 when every row was built. Losses are
// still 0: the table is the result.
[[nodiscard]] int RunPlaytestSim(const PlaytestOptions& options);

} // namespace Ascension
