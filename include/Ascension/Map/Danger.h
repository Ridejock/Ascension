#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Emerald/Core/Defines.h"

namespace Ascension {

class Database;

// How a filled meter becomes a chance. Linear is the meter over the cap.
// Square is that fraction squared, so the first steps off a road stay quiet
// and a full meter is likely. danger.json names which one a run uses.
enum class DangerCurve : u8 { Linear, Square };

// One tile's contribution per committed step. 0 never rolls.
struct DangerRating {
    std::string Tile;
    i32 Rating = 0;
};

// One weighted row of an encounter table. Weight is at least 1.
struct DangerRow {
    std::string Encounter;
    i32 Weight = 1;
};

// Foes for one kind of ground. Map empty matches that tile on every map.
// A table that names a map wins over a tile-only table for the same tile.
struct DangerTable {
    std::string Id;
    std::string Map;
    std::string Tile;
    std::vector<DangerRow> Rows;
};

// A short line under the pools once the meter reaches At. The greatest At
// that is still at or below the meter is the one drawn. No picture: the
// debug font draws the word.
struct DangerHint {
    i32 At = 1;
    std::string Text;
};

// The numbers from danger.json. Cap is full. GraceSteps is how many steps
// after a fight, and after leaving a 0-rated tile, add danger and do not roll.
struct DangerRules {
    i32 Cap = 100;
    i32 GraceSteps = 3;
    DangerCurve Curve = DangerCurve::Square;
    std::vector<DangerRating> Ratings;
    std::vector<DangerTable> Tables;
    std::vector<DangerHint> Hints;
};

// Value is 0..Cap. Grace is steps that still cannot roll. OnRoad is true
// when the last committed step stood on a 0-rated tile, or no step has
// been taken yet. The step that leaves that state arms grace.
struct DangerMeter {
    i32 Value = 0;
    i32 Grace = 0;
    bool OnRoad = true;
};

// Walked by NextDangerRoll. A state of 0 is treated as 1.
struct DangerRng {
    u32 State = 1;
};

// What one committed step did to the meter. Fight is the roll, not yet
// a battle: the caller still has to find a table.
struct DangerStep {
    bool Fight = false;
    i32 Value = 0;
    i32 Grace = 0;
    i32 Rating = 0;
};

// 0 when the tile is not listed.
[[nodiscard]] i32 DangerRatingOf(const DangerRules& rules, std::string_view tileId);

// The map-specific table, else the tile-only table, else null.
[[nodiscard]] const DangerTable* FindDangerTable(const DangerRules& rules, std::string_view mapId,
                                                 std::string_view tileId);

// Walks the weights. An empty table returns an empty id.
[[nodiscard]] std::string PickDangerEncounter(const DangerTable& table, u32 roll);

// The hint with the greatest At that is still <= value, or null.
[[nodiscard]] const DangerHint* DangerHintFor(const DangerRules& rules, i32 value);

// Adds `rating` and maybe rolls. A rating of 0 never fights and does not
// change Value. `roll` is folded into [0, cap). Grace, including the steps
// just after leaving a 0-rated tile, adds danger and does not roll.
[[nodiscard]] DangerStep AdvanceDanger(DangerMeter& meter, const DangerRules& rules, i32 rating,
                                       u32 roll);

// Value back to 0 and Grace to GraceSteps. OnRoad stays false: the hero is
// still on the ground the fight happened on, so the next step is not
// "leaving the road".
void ResetDanger(DangerMeter& meter, const DangerRules& rules);

// Uniform in [0, cap). A cap below 1 returns 0 and still steps the state.
[[nodiscard]] u32 NextDangerRoll(DangerRng& rng, u32 cap);

// False when the encounter lists a foe with boss set. A missing encounter
// still flees; the fight itself fails to open.
[[nodiscard]] bool DangerEncounterFlees(const Database& data, std::string_view encounterId);

[[nodiscard]] const char* ToString(DangerCurve curve);

} // namespace Ascension
