#include "Ascension/Map/Handoff.h"

#include "Ascension/Combat/Battle.h"
#include "Ascension/Data/ArtPack.h"
#include "Ascension/Map/Walk.h"

namespace Ascension {

namespace {

bool SameTile(MapTile left, MapTile right)
{
    return left.X == right.X && left.Y == right.Y;
}

bool TileOpen(const Database& data, const MapDef& map, MapTile tile, const ArtPack* art)
{
    const std::string* cell = map.Cell(tile.X, tile.Y);
    if (cell == nullptr)
        return false;
    const TileDef* def = data.FindTile(*cell);
    return def != nullptr && !def->Solid && !FootprintBlocks(map, tile.X, tile.Y, art);
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
MapTile FleeTile(const Database& data, const MapDef& map, MapTile spawn, ContactQuery query,
                 const ArtPack* art)
{
    if (query.HasFrom && TileOpen(data, map, query.From, art) && !SameTile(query.From, spawn))
        return query.From;
    const WalkDir order[] = {WalkDir::North, WalkDir::East, WalkDir::South, WalkDir::West};
    for (const WalkDir direction : order) {
        MapTile next;
        if (Shift(spawn, direction, next) && TileOpen(data, map, next, art))
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

MapTile LossTile(const Database& data, const MapDef& map, const ArtPack* art)
{
    MapTile around = map.Start;
    bool haveRest = false;
    for (const MapPoint& point : map.Points) {
        if (point.Kind != MapPointKind::Rest)
            continue;
        if (!haveRest) {
            around = point.Tile;
            haveRest = true;
        }
        if (TileOpen(data, map, point.Tile, art))
            return point.Tile;
    }
    if (TileOpen(data, map, map.Start, art))
        return map.Start;
    // The named tiles sit inside a footprint. An open neighbor still gets the hero home.
    const WalkDir order[] = {WalkDir::North, WalkDir::East, WalkDir::South, WalkDir::West};
    for (const WalkDir direction : order) {
        MapTile next;
        if (Shift(around, direction, next) && TileOpen(data, map, next, art))
            return next;
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

namespace {

i32 TileGap(i32 left, i32 right)
{
    const i32 gap = left - right;
    return gap < 0 ? -gap : gap;
}

// The greater of the two axis gaps. A radius of 1 is the eight neighboring tiles.
i32 TileDistance(MapTile left, MapTile right)
{
    const i32 east = TileGap(left.X, right.X);
    const i32 south = TileGap(left.Y, right.Y);
    return east > south ? east : south;
}

MapTile TileNow(const MapSpawn& spawn, std::span<const SpawnPlace> places)
{
    for (const SpawnPlace& place : places) {
        if (place.Id == spawn.Id)
            return place.Tile;
    }
    return spawn.Tile;
}

bool AlreadySeated(const std::vector<PartyFoe>& foes, std::string_view spawnId)
{
    for (const PartyFoe& foe : foes) {
        if (foe.SpawnId == spawnId)
            return true;
    }
    return false;
}

void AppendSpawn(const Database& data, const MapSpawn& spawn, std::vector<PartyFoe>& out)
{
    const EncounterDef* encounter = data.FindEncounter(spawn.Encounter);
    if (encounter == nullptr)
        return;
    for (const std::string& id : encounter->Enemies) {
        PartyFoe foe;
        foe.SpawnId = spawn.Id;
        foe.Id = id;
        out.push_back(std::move(foe));
    }
}

bool StatusListed(const std::vector<std::string>& statuses, std::string_view id)
{
    for (const std::string& have : statuses) {
        if (have == id)
            return true;
    }
    return false;
}

} // namespace

i32 StoredWound(const FledWounds& wounds, i32 index, std::string_view id)
{
    if (index < 0 || static_cast<usize>(index) >= wounds.Foes.size())
        return -1;
    const FoeWound& wound = wounds.Foes[static_cast<usize>(index)];
    if (wound.Id != id)
        return -1;
    return wound.Hp;
}

PartySplit SplitParty(std::span<const PartyFoe> ordered, i32 maxFoes)
{
    PartySplit split;
    i32 cap = maxFoes;
    if (cap < 0)
        cap = 0;
    for (const PartyFoe& foe : ordered) {
        if (static_cast<i32>(split.Seated.size()) < cap)
            split.Seated.push_back(foe);
        else
            split.Queued.push_back(foe);
    }
    return split;
}

void ApplyMapAuras(const Database&, const MapDef& map, const MapMemory& memory,
                   std::span<const SpawnPlace> places, std::vector<PartyFoe>& foes)
{
    for (PartyFoe& foe : foes) {
        foe.Statuses.clear();
        const MapSpawn* who = map.FindSpawn(foe.SpawnId);
        if (who == nullptr)
            continue;
        const MapTile foeTile = TileNow(*who, places);
        for (const MapSpawn& auraSpawn : map.Spawns) {
            if (!auraSpawn.Aura.Listed || auraSpawn.Id == foe.SpawnId)
                continue;
            if (IsCleared(memory, auraSpawn.Id))
                continue;
            if (TileDistance(foeTile, TileNow(auraSpawn, places)) > auraSpawn.Aura.Radius)
                continue;
            if (StatusListed(foe.Statuses, auraSpawn.Aura.Status))
                continue;
            foe.Statuses.push_back(auraSpawn.Aura.Status);
        }
    }
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
                           const MapMemory& memory, std::span<const SpawnPlace> places,
                           const ArtPack* art)
{
    MapFightRequest request;
    const MapSpawn* spawn = nullptr;
    for (const MapSpawn& candidate : map.Spawns) {
        if (SameTile(TileNow(candidate, places), query.Tile))
            spawn = &candidate;
    }
    if (spawn == nullptr || IsCleared(memory, spawn->Id))
        return request;
    const EncounterDef* encounter = data.FindEncounter(spawn->Encounter);
    if (encounter == nullptr)
        return request;

    std::vector<PartyFoe> ordered;
    AppendSpawn(data, *spawn, ordered);
    for (const std::string& id : query.Joining) {
        if (AlreadySeated(ordered, id))
            continue;
        const MapSpawn* extra = map.FindSpawn(id);
        if (extra == nullptr || IsCleared(memory, extra->Id))
            continue;
        AppendSpawn(data, *extra, ordered);
    }
    const PartySplit split = SplitParty(ordered, data.GetRules().MaxFoes);
    request.Seats = split.Seated;
    request.Queue = split.Queued;
    ApplyMapAuras(data, map, memory, places, request.Seats);
    for (const PartyFoe& foe : request.Seats) {
        request.Foes.push_back(foe.Id);
        for (const std::string& status : foe.Statuses) {
            if (!StatusListed(request.StartStatuses, status))
                request.StartStatuses.push_back(status);
        }
    }

    request.Active = true;
    request.SpawnId = spawn->Id;
    request.EncounterId = encounter->Id;
    request.MapId = map.Id;
    request.SpawnTile = TileNow(*spawn, places);
    request.Once = spawn->Once;
    request.AllowFlee = FleeAllowed(data, *spawn);
    request.ReturnTile = FleeTile(data, map, request.SpawnTile, query, art);
    return request;
}

LossSpot FindLossReturn(const Database& data, std::string_view foughtMapId, const ArtPack* art)
{
    LossSpot spot;
    const MapDef* hub = HubMap(data);
    const MapDef* map = hub != nullptr ? hub : data.FindMap(foughtMapId);
    if (map == nullptr)
        return spot;
    const MapTile tile = LossTile(data, *map, art);
    if (!TileOpen(data, *map, tile, art))
        return spot;
    spot.Ok = true;
    spot.Hub = hub != nullptr;
    spot.MapId = map->Id;
    spot.Tile = tile;
    return spot;
}

MapReturn ResolveReturn(const Database& data, const ReturnQuery& query, const ArtPack* art)
{
    MapReturn back;
    if (query.Outcome == MapOutcome::Lost) {
        const LossSpot spot = FindLossReturn(data, query.FoughtMapId, art);
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
        if (!TileOpen(data, *map, back.Tile, art) || SameTile(back.Tile, query.SpawnTile)) {
            ContactQuery beside;
            beside.Tile = query.SpawnTile;
            back.Tile = FleeTile(data, *map, query.SpawnTile, beside, art);
        }
        back.KeepWounds = true;
        return back;
    }

    back.Tile = query.SpawnTile;
    if (!TileOpen(data, *map, back.Tile, art))
        back.Tile = map->Start;
    back.ClearSpawn = query.Once;
    return back;
}

bool PlaceNear(Walker& walker, const Database& data, std::string_view mapId, MapTile tile,
               MapTile around, MapTile avoid, bool hasAvoid, const ArtPack* art)
{
    const MapDef* map = data.FindMap(mapId);
    if (map == nullptr)
        return false;
    auto same = [](MapTile left, MapTile right) {
        return left.X == right.X && left.Y == right.Y;
    };
    auto fits = [&](MapTile candidate) {
        if (hasAvoid && same(candidate, avoid))
            return false;
        return walker.Place(data, map->Id, candidate, art);
    };
    if (fits(tile))
        return true;
    const WalkDir order[] = {WalkDir::North, WalkDir::East, WalkDir::South, WalkDir::West};
    for (const WalkDir direction : order) {
        MapTile next = around;
        if (direction == WalkDir::North)
            --next.Y;
        else if (direction == WalkDir::East)
            ++next.X;
        else if (direction == WalkDir::South)
            ++next.Y;
        else if (direction == WalkDir::West)
            --next.X;
        else
            continue;
        if (same(next, tile))
            continue;
        if (fits(next))
            return true;
    }
    if (!same(map->Start, tile) && fits(map->Start))
        return true;
    return false;
}

MapSettle ApplyOutcome(const Database& data, const ReturnQuery& query, MapMemory& memory,
                       std::span<const FoeWound> wounds, const ArtPack* art)
{
    MapSettle settled;
    settled.Return = ResolveReturn(data, query, art);
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
