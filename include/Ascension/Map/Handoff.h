#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/Database.h"

namespace Ascension {

class Battle;

// What a spawn fight did. Won clears a spawn whose `once` is set. Lost sends the
// hero to the loss tile at full HP and does not spend gold. Fled steps back beside
// the spawn, keeps the hero's pools, and leaves the spawn up, wounds included.
enum class MapOutcome : u8 { Won, Lost, Fled };

// One foe's health after a flee. Index in the list is the seat the fight started
// with. A later contact writes it back only when that seat still has this id.
struct FoeWound {
    std::string Id;
    i32 Hp = 0;
};

struct FledWounds {
    std::string SpawnId;
    std::vector<FoeWound> Foes;
};

// Cleared spawn ids and the wounds a flee left. The save writes both, by spawn id.
struct MapMemory {
    std::vector<std::string> Cleared;
    std::vector<FledWounds> Wounds;
};

// The tile the hero just stepped onto, and the tile they stepped from when this
// step stayed on the same map. HasFrom is false for a link arrival. Joining is
// extra spawn ids that enter this fight after the spawn on the tile, in that
// order. Empty means the tile's spawn alone, which is every fight the hub starts.
struct ContactQuery {
    MapTile Tile;
    MapTile From;
    bool HasFrom = false;
    std::vector<std::string> Joining;
};

// One foe a contact can seat. SpawnId is the map spawn. Id is the enemy. Statuses
// is filled for a foe who is seated, from auras on the map at that moment. A queued
// foe leaves Statuses empty; the next offer samples the auras again.
struct PartyFoe {
    std::string SpawnId;
    std::string Id;
    std::vector<std::string> Statuses;
};

// Seated is at most maxFoes. Queued is the rest, in the same order. Call SplitParty
// on Queued for the follow-up fight. Nobody joins a seat that opens during a fight.
struct PartySplit {
    std::vector<PartyFoe> Seated;
    std::vector<PartyFoe> Queued;
};

// The fight a tile offers. Active is false when the tile has no spawn, that spawn
// is already cleared, or its encounter is gone. Foes is the seated enemy ids. For
// one spawn whose encounter fits in maxFoes, that is the encounter's own list.
// Seats is one entry per id in Foes. Queue is who did not fit. StartStatuses is
// the unique status ids across Seats, in the order they first appear. AllowFlee
// is false when the spawn on the tile sets noFlee, or when a foe sets boss.
struct MapFightRequest {
    bool Active = false;
    std::string SpawnId;
    std::string EncounterId;
    std::string MapId;
    MapTile SpawnTile;
    MapTile ReturnTile;
    bool Once = true;
    bool AllowFlee = false;
    std::vector<std::string> Foes;
    std::vector<PartyFoe> Seats;
    std::vector<PartyFoe> Queue;
    std::vector<std::string> StartStatuses;
};

// Where a loss stands. Hub is true when a map sets hub. The tile is that map's
// rest point, or its start when it has no rest. With no hub, it is the fought
// map's rest, then that map's start. Both tiles come from the map file.
struct LossSpot {
    bool Ok = false;
    bool Hub = false;
    std::string MapId;
    MapTile Tile;
};

struct ReturnQuery {
    MapOutcome Outcome = MapOutcome::Won;
    std::string SpawnId;
    std::string FoughtMapId;
    MapTile SpawnTile;
    MapTile ReturnTile;
    bool Once = true;
};

// Where the hero stands after the fight. FillPools is a loss: both pools full.
// A win and a flee leave the pools to the caller. Gold is not a field here, so
// this return cannot spend it. A win's gold is the fight's own award.
struct MapReturn {
    bool Ok = false;
    bool Hub = false;
    std::string MapId;
    MapTile Tile;
    bool ClearSpawn = false;
    bool KeepWounds = false;
    bool FillPools = false;
};

struct MapSettle {
    MapReturn Return;
    bool Cleared = false;
};

[[nodiscard]] bool IsCleared(const MapMemory& memory, std::string_view spawnId);

// Adds the id when `once` is set and it was not already cleared. Drops any wounds
// that spawn held. False when the spawn stays on the map.
bool RememberClear(MapMemory& memory, std::string_view spawnId, bool once);

void ForgetWounds(MapMemory& memory, std::string_view spawnId);
void RememberWounds(MapMemory& memory, std::string_view spawnId, std::span<const FoeWound> foes);

[[nodiscard]] const FledWounds* WoundsOf(const MapMemory& memory, std::string_view spawnId);

// The stored HP for that seat, or -1 when the index is past the list or the id
// changed. The caller leaves a -1 seat at the health the new fight rolled.
[[nodiscard]] i32 StoredWound(const FledWounds& wounds, i32 index, std::string_view id);

// False when the spawn sets noFlee, or when its encounter lists a foe with boss set.
// The roster boss is that flag (Varn today). A later foe gets it by the same field.
[[nodiscard]] bool FleeAllowed(const Database& data, const MapSpawn& spawn);

// First `maxFoes` stay in Seated. The rest wait in Queued. A cap below 1 queues
// everyone. Order is kept. Statuses are copied, not sampled here.
[[nodiscard]] PartySplit SplitParty(std::span<const PartyFoe> ordered, i32 maxFoes);

// Writes each foe's Statuses from auras on this map. A foe's own aura does not
// count, and a cleared aura does not grant. `places` overrides a spawn's file tile
// when that id is listed; an empty span uses the file. Calling this again replaces
// the lists, so a spawn that has walked out of the radius loses the status.
void ApplyMapAuras(const Database& data, const MapDef& map, const MapMemory& memory,
                   std::span<const SpawnPlace> places, std::vector<PartyFoe>& foes);

// `places` is the same overlay as ApplyMapAuras. Empty uses the file tiles, which
// is where an idle mover is standing.
[[nodiscard]] MapFightRequest OfferSpawn(const Database& data, const MapDef& map,
                                         ContactQuery query, const MapMemory& memory,
                                         std::span<const SpawnPlace> places = {});

[[nodiscard]] LossSpot FindLossReturn(const Database& data, std::string_view foughtMapId);

[[nodiscard]] MapReturn ResolveReturn(const Database& data, const ReturnQuery& query);

// Writes the clear and the wounds. Does not move the hero and does not touch gold.
[[nodiscard]] MapSettle ApplyOutcome(const Database& data, const ReturnQuery& query,
                                     MapMemory& memory, std::span<const FoeWound> wounds);

void CaptureFoeWounds(const Battle& battle, std::vector<FoeWound>& out);
void ApplyFoeWounds(Battle& battle, const FledWounds& wounds);

} // namespace Ascension
