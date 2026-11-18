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
// debug font draws the word. A terrain hint sign replaces this word while
// that sign matches.
struct DangerHint {
    i32 At = 1;
    std::string Text;
};

// What a sign draws. Tracks are small squares on the hero's tile. Howl is a
// line of text for a few steps, with no sound. Hint replaces the generic word
// with a meaner one for this ground.
enum class DangerSignKind : u8 { Tracks, Howl, Hint };

// One square of a tracks sign, in percent of the tile. 0 is the top-left.
struct DangerMark {
    i32 X = 0;
    i32 Y = 0;
    i32 Size = 1;
};

// One cue. Map empty matches every map. Tile empty matches every tile. A sign
// that names a map wins over a tile-only sign of the same kind when both match.
// The greatest At that still fits the meter is the one of that kind that shows.
struct DangerSign {
    std::string Id;
    DangerSignKind Kind = DangerSignKind::Hint;
    i32 At = 1;
    std::string Map;
    std::string Tile;
    std::string Text;
    // Howl only. How many committed steps the line stays up, including the
    // step that crosses At.
    i32 Steps = 1;
    bool HasColor = false;
    u8 Red = 0;
    u8 Green = 0;
    u8 Blue = 0;
    std::vector<DangerMark> Marks;
};

// One square to draw. TileX and TileY are the cell. X, Y, and Size are
// percents of that cell, the same numbers the sign stored.
struct DangerMarkSpot {
    std::string MapId;
    i32 TileX = 0;
    i32 TileY = 0;
    u8 Red = 0;
    u8 Green = 0;
    u8 Blue = 0;
    i32 X = 0;
    i32 Y = 0;
    i32 Size = 1;
};

// A howl that is still on screen. Left is committed steps still to show.
// Spent is the howl ids that already played while the meter stayed at or
// above their At. Not saved: a loaded game settles them instead of howling
// again for a threshold it has already passed.
struct DangerHowlHold {
    std::string SignId;
    i32 Left = 0;
};

struct DangerSignsState {
    std::vector<DangerHowlHold> Howls;
    std::vector<std::string> Spent;
};

// Joins the hint, the tracks word, and a live howl when danger.json omits cueJoin.
inline constexpr std::string_view kDefaultCueJoin = " - ";

// The numbers from danger.json. Cap is full. GraceSteps is how many steps
// after a fight, and after leaving a 0-rated tile, add danger and do not roll.
struct DangerRules {
    i32 Cap = 100;
    i32 GraceSteps = 3;
    DangerCurve Curve = DangerCurve::Square;
    std::string CueJoin = std::string(kDefaultCueJoin);
    std::vector<DangerRating> Ratings;
    std::vector<DangerTable> Tables;
    std::vector<DangerHint> Hints;
    std::vector<DangerSign> Signs;
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

// The line under the pools. Empty on a 0-rated tile, at 0, and when nothing
// matches. A terrain hint replaces the generic hint. The tracks word and a
// live howl are joined with CueJoin. No sound.
[[nodiscard]] std::string DangerCueLine(const DangerRules& rules, const DangerSignsState& state,
                                        i32 value, std::string_view mapId, std::string_view tileId);

// Squares for the tracks sign that matches this tile. Empty when the tile
// rates 0, the meter is 0, or that sign has no marks.
void CollectDangerMarks(const DangerRules& rules, i32 value, std::string_view mapId,
                        std::string_view tileId, i32 tileX, i32 tileY,
                        std::vector<DangerMarkSpot>& out);

// One committed step. A howl starts when the meter is at or above its At on
// matching ground, stays for its Steps, and does not start again until the
// meter drops below that At. A 0-rated step still counts a live howl down.
void AdvanceDangerSigns(DangerSignsState& state, const DangerRules& rules, i32 value,
                        std::string_view mapId, std::string_view tileId);

// Marks howls the meter has already passed on this tile, without showing them.
// A continue does not howl for ground the save was already standing on.
void SettleDangerSigns(DangerSignsState& state, const DangerRules& rules, i32 value,
                       std::string_view mapId, std::string_view tileId);

// Drops a live howl and the memory of which howls already played.
void ClearDangerSigns(DangerSignsState& state);

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
