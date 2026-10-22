#pragma once

#include "Ascension/Progress/Hero.h"
#include "Ascension/Progress/Inventory.h"

namespace Ascension {

// HP and focus between fights. Max is not stored: it is read from the hero, the worn
// gear, and the combat rules whenever a fight, a rest, or a level-up needs it. Known
// is false until the first fill, which is full health. ReturnToHub is set by a loss.
// The walk back to the hub rest is a later step. Gold is not a field here, so a loss
// cannot spend it.
struct Vitals {
    bool Known = false;
    i32 Hp = 0;
    i32 Focus = 0;
    bool ReturnToHub = false;
};

// The pools a fight would give this hero right now. MaxHp is vitality times
// hpPerVitality. MaxFocus is the focus stat plus focusBase. Always-on stat grants
// are already in the totals, the same line Battle builds.
struct PoolCaps {
    i32 MaxHp = 1;
    i32 MaxFocus = 0;
};

struct PoolPair {
    i32 Hp = 0;
    i32 Focus = 0;
};

// Attributes, worn gear, then always-on stat grants. An unknown class is 1 HP and
// no focus.
[[nodiscard]] PoolCaps HeroPools(const Database& data, const Hero& hero, const Inventory& bag);

// Both pools full. Clears ReturnToHub. This is a new hero, and a rest.
void FillVitals(Vitals& vitals, PoolCaps caps);

// A win. Stores the fight's pools, clamped to the caps that fight used. Clears
// ReturnToHub: the hero is not being sent home.
void KeepPools(Vitals& vitals, i32 hp, i32 focus, PoolCaps caps);

// A loss. Fills both pools and sets ReturnToHub. Does not read gold.
void LoseToHub(Vitals& vitals, PoolCaps caps);

// The level-up rule from progress.json. oldMaxHp is the max before the levels,
// newMaxHp the max after. Focus is left alone; call ClampFocus for the new cap.
void ApplyLevelHp(Vitals& vitals, i32 oldMaxHp, i32 newMaxHp, LevelHpRule rule);

void ClampFocus(Vitals& vitals, i32 maxFocus);

// What the next fight starts from. Unknown vitals are the caps. A known wound is
// clamped into the caps, so a higher max (a class bonus, a new piece) does not
// heal, and a lower max cannot leave HP past it.
[[nodiscard]] PoolPair PoolsFrom(const Vitals& vitals, PoolCaps caps);

// True when `tile` is a rest point. Fills both pools and clears ReturnToHub.
// A miss leaves `vitals` alone.
[[nodiscard]] bool RestOn(const MapDef& map, MapTile tile, Vitals& vitals, PoolCaps caps);

} // namespace Ascension
