#include "Ascension/Map/Movers.h"

namespace Ascension {

namespace {

const MoverDef* BehaviorOf(const Database& data, const MapSpawn& spawn)
{
    if (!spawn.Mover.Listed)
        return nullptr;
    return data.FindMover(spawn.Mover.Behavior);
}

} // namespace

void PlaceSpawns(const MapDef& map, const MapMemory& memory, std::vector<SpawnPlace>& out)
{
    out.clear();
    for (const MapSpawn& spawn : map.Spawns) {
        if (IsCleared(memory, spawn.Id))
            continue;
        SpawnPlace place;
        place.Id = spawn.Id;
        place.Tile = spawn.Tile;
        place.Home = spawn.Tile;
        out.push_back(std::move(place));
    }
}

bool StepSpawnMovers(const Database& data, const MapDef& map, const MapMemory& memory,
                     const MoverContext&, std::vector<SpawnPlace>& places)
{
    bool moved = false;
    for (SpawnPlace& place : places) {
        if (IsCleared(memory, place.Id))
            continue;
        const MapSpawn* spawn = map.FindSpawn(place.Id);
        if (spawn == nullptr)
            continue;
        const MoverDef* behavior = BehaviorOf(data, *spawn);
        if (behavior == nullptr)
            continue;
        // Idle stands. Any other kind stands too until a later step teaches it to
        // move. The load rejects those kinds, so this is the same stand.
        if (behavior->Kind == MoverKind::Idle)
            continue;
    }
    return moved;
}

} // namespace Ascension
