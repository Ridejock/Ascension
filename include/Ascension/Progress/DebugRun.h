#pragma once

#include <string>

#include "Ascension/Data/Database.h"
#include "Ascension/Progress/RunSave.h"
#include "Ascension/Progress/Vitals.h"

namespace Ascension {

// What RespawnFoes did. Busy means a fight is still using the spawn lists, so the
// call changed nothing. Returned cleared defeated foe ids and cleared spawn ids.
enum class FoeRespawn : u8 { Returned, Busy };

// A brand-new run: the career and bag Start() produced, empty progress lists, and
// the hub start when a map sets hub. Pools is full health. The place is empty when
// no map sets hub.
struct FreshStart {
    RunState Run;
    Vitals Pools;
};

// Clears defeated foe ids and cleared spawn ids on every map. Career, bag, gold,
// the map tile, found drops, talk flags, flee wounds, and the carried pools stay.
// `inBattle` leaves `run` unchanged: the fight is still reading those lists.
[[nodiscard]] FoeRespawn RespawnFoes(RunState& run, bool inBattle);

// The same hero and bag a missing save starts: Start() on both, then the hub tile.
// Does not open, write, or delete a save file. The caller places the hero.
[[nodiscard]] bool BeginNewGame(const Database& data, Hero& hero, Inventory& inventory,
                                FreshStart& out, std::string& error);

} // namespace Ascension
