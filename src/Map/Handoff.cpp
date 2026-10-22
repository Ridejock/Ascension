#include "Ascension/Map/Handoff.h"

#include "Ascension/Combat/Battle.h"
#include "Ascension/Map/Walk.h"

namespace Ascension {

namespace {

bool SameTile(MapTile left, MapTile right)
{
    return left.X == right.X && left.Y == right.Y;
}

bool TileOpen(const Database& data, const MapDef& map, MapTile tile)
{
    const std::string* cell = map.Cell(tile.X, tile.Y);
    if (cell == nullptr)
        return false;
    const TileDef* def = data.FindTile(*cell);
    return def != nullptr && !def->Solid;
}

bool Shift(MapTile from, WalkDir direction, MapTile& out)
{
    out = from;
    if (direction == WalkDir::North)
        --out.Y;
    else if (direction == WalkDir::East)
        ++out.X;
    else if (direction == WalkDir::South)
        ++out.Y;
    else if (direction == WalkDir::West)
        --out.X;
    else
        return false;
    return true;
}

// The tile a flee returns to. The step they came from, when that tile is still
// open and is not the spawn. Otherwise the first open neighbor in north, east,
// south, west order, then the map's start.
MapTile FleeTile(const Database& data, const MapDef& map, MapTile spawn, ContactQuery query)
{
    if (query.HasFrom && TileOpen(data, map, query.From) && !SameTile(query.From, spawn))
        return query.From;
    const WalkDir order[] = {WalkDir::North, WalkDir::East, WalkDir::South, WalkDir::West};
    for (const WalkDir direction : order) {
        MapTile next;
        if (Shift(spawn, direction, next) && TileOpen(data, map, next))
            return next;
    }
    return map.Start;
}

const MapDef* HubMap(const Database& data)
{
    for (const MapDef& map : data.GetMaps()) {
        if (map.Hub)
            return &map;
    }
    return nullptr;
}

MapTile LossTile(const Database& data, const MapDef& map)
{
    for (const MapPoint& point : map.Points) {
        if (point.Kind != MapPointKind::Rest)
            continue;
        if (TileOpen(data, map, point.Tile))
            return point.Tile;
    }
    return map.Start;
}

} // namespace

bool IsCleared(const MapMemory& memory, std::string_view spawnId)
{
    for (const std::string& id : memory.Cleared) {
        if (id == spawnId)
            return true;
    }
    return false;
}

bool RememberClear(MapMemory& memory, std::string_view spawnId, bool once)
{
    if (!once || spawnId.empty() || IsCleared(memory, spawnId))
        return false;
    memory.Cleared.emplace_back(spawnId);
    ForgetWounds(memory, spawnId);
    return true;
}

void ForgetWounds(MapMemory& memory, std::string_view spawnId)
{
    for (usize index = 0; index < memory.Wounds.size(); ++index) {
        if (memory.Wounds[index].SpawnId != spawnId)
            continue;
        memory.Wounds.erase(memory.Wounds.begin() + static_cast<std::ptrdiff_t>(index));
        return;
    }
}

void RememberWounds(MapMemory& memory, std::string_view spawnId, std::span<const FoeWound> foes)
{
    ForgetWounds(memory, spawnId);
    if (spawnId.empty() || foes.empty())
        return;
    FledWounds wounds;
    wounds.SpawnId = std::string(spawnId);
    wounds.Foes.assign(foes.begin(), foes.end());
    memory.Wounds.push_back(std::move(wounds));
}

const FledWounds* WoundsOf(const MapMemory& memory, std::string_view spawnId)
{
    for (const FledWounds& wounds : memory.Wounds) {
        if (wounds.SpawnId == spawnId)
            return &wounds;
    }
    return nullptr;
}

i32 StoredWound(const FledWounds& wounds, i32 index, std::string_view id)
{
    if (index < 0 || static_cast<usize>(index) >= wounds.Foes.size())
        return -1;
    const FoeWound& wound = wounds.Foes[static_cast<usize>(index)];
    if (wound.Id != id)
        return -1;
    return wound.Hp;
}

bool FleeAllowed(const Database& data, const MapSpawn& spawn)
{
    if (spawn.NoFlee)
        return false;
    const EncounterDef* encounter = data.FindEncounter(spawn.Encounter);
    if (encounter == nullptr)
        return true;
    for (const std::string& id : encounter->Enemies) {
        const EnemyDef* enemy = data.FindEnemy(id);
        if (enemy != nullptr && enemy->Boss)
            return false;
    }
    return true;
}

MapFightRequest OfferSpawn(const Database& data, const MapDef& map, ContactQuery query,
                           const MapMemory& memory)
{
    MapFightRequest request;
    const MapSpawn* spawn = nullptr;
    for (const MapSpawn& candidate : map.Spawns) {
        if (SameTile(candidate.Tile, query.Tile))
            spawn = &candidate;
    }
    if (spawn == nullptr || IsCleared(memory, spawn->Id))
        return request;
    const EncounterDef* encounter = data.FindEncounter(spawn->Encounter);
    if (encounter == nullptr)
        return request;
    request.Active = true;
    request.SpawnId = spawn->Id;
    request.EncounterId = encounter->Id;
    request.MapId = map.Id;
    request.SpawnTile = spawn->Tile;
    request.Once = spawn->Once;
    request.AllowFlee = FleeAllowed(data, *spawn);
    request.Foes = encounter->Enemies;
    request.ReturnTile = FleeTile(data, map, spawn->Tile, query);
    return request;
}

LossSpot FindLossReturn(const Database& data, std::string_view foughtMapId)
{
    LossSpot spot;
    const MapDef* hub = HubMap(data);
    const MapDef* map = hub != nullptr ? hub : data.FindMap(foughtMapId);
    if (map == nullptr)
        return spot;
    const MapTile tile = LossTile(data, *map);
    if (!TileOpen(data, *map, tile))
        return spot;
    spot.Ok = true;
    spot.Hub = hub != nullptr;
    spot.MapId = map->Id;
    spot.Tile = tile;
    return spot;
}

MapReturn ResolveReturn(const Database& data, const ReturnQuery& query)
{
    MapReturn back;
    if (query.Outcome == MapOutcome::Lost) {
        const LossSpot spot = FindLossReturn(data, query.FoughtMapId);
        if (!spot.Ok)
            return back;
        back.Ok = true;
        back.Hub = spot.Hub;
        back.MapId = spot.MapId;
        back.Tile = spot.Tile;
        back.FillPools = true;
        return back;
    }

    const MapDef* map = data.FindMap(query.FoughtMapId);
    if (map == nullptr)
        return back;
    back.Ok = true;
    back.MapId = map->Id;
    if (query.Outcome == MapOutcome::Fled) {
        back.Tile = query.ReturnTile;
        if (!TileOpen(data, *map, back.Tile) || SameTile(back.Tile, query.SpawnTile)) {
            ContactQuery beside;
            beside.Tile = query.SpawnTile;
            back.Tile = FleeTile(data, *map, query.SpawnTile, beside);
        }
        back.KeepWounds = true;
        return back;
    }

    back.Tile = query.SpawnTile;
    if (!TileOpen(data, *map, back.Tile))
        back.Tile = map->Start;
    back.ClearSpawn = query.Once;
    return back;
}

MapSettle ApplyOutcome(const Database& data, const ReturnQuery& query, MapMemory& memory,
                       std::span<const FoeWound> wounds)
{
    MapSettle settled;
    settled.Return = ResolveReturn(data, query);
    if (!settled.Return.Ok)
        return settled;
    if (settled.Return.ClearSpawn)
        settled.Cleared = RememberClear(memory, query.SpawnId, true);
    if (settled.Return.KeepWounds)
        RememberWounds(memory, query.SpawnId, wounds);
    else
        ForgetWounds(memory, query.SpawnId);
    return settled;
}

void CaptureFoeWounds(const Battle& battle, std::vector<FoeWound>& out)
{
    out.clear();
    for (const CombatantState& foe : battle.Enemies()) {
        FoeWound wound;
        wound.Id = foe.Id;
        wound.Hp = foe.Hp;
        out.push_back(std::move(wound));
    }
}

void ApplyFoeWounds(Battle& battle, const FledWounds& wounds)
{
    const i32 count = static_cast<i32>(battle.Enemies().size());
    for (i32 index = 0; index < count; ++index) {
        if (battle.Result() != BattleResult::Ongoing)
            return;
        const CombatantState& foe = battle.Enemies()[static_cast<usize>(index)];
        const i32 hp = StoredWound(wounds, index, foe.Id);
        if (hp < 0)
            continue;
        if (!battle.SetTarget(index))
            continue;
        battle.SetEnemyHp(hp);
    }
}

} // namespace Ascension
