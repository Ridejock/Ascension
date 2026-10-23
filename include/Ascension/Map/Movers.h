#pragma once

#include <string>
#include <vector>

#include "Ascension/Map/Handoff.h"

namespace Ascension {

// The hero, for one map step. Idle does not read it. A later kind (chase, kite,
// forage) reads the tile and the pools here, on the same call.
struct MoverContext {
    MapTile Hero;
    i32 HeroHp = 0;
    i32 HeroMaxHp = 0;
};

// One place per spawn that is still on the map, in file order. Home is the file
// tile. Cleared spawns are left out.
void PlaceSpawns(const MapDef& map, const MapMemory& memory, std::vector<SpawnPlace>& out);

// One map step, after the hero has moved. Idle stands, so Tile stays put. A spawn
// with no mover stays. A cleared spawn stays. The same context and the same places
// produce the same tiles. True when any tile changed.
bool StepSpawnMovers(const Database& data, const MapDef& map, const MapMemory& memory,
                     const MoverContext& context, std::vector<SpawnPlace>& places);

} // namespace Ascension
