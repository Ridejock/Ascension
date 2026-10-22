#pragma once

#include <string>
#include <string_view>

#include "Ascension/Data/Database.h"

namespace Ascension {

// North is decreasing Y. The grid's Y grows south.
enum class WalkDir : u8 { None, North, East, South, West };

struct WalkPlace {
    std::string MapId;
    MapTile Tile;
};

enum class WalkStepKind : u8 { None, Moved, Blocked, Exited };

// Arrival is set when the step took a link. The hero is already standing there.
struct WalkStep {
    WalkStepKind Kind = WalkStepKind::None;
    WalkDir Direction = WalkDir::None;
    WalkPlace Arrival;
};

// One direction for this tick. Fresh is a new press, or the first tick of a new
// direction: step now, then wait WalkRules::StepMilliseconds before another.
struct WalkInput {
    WalkDir Direction = WalkDir::None;
    bool Fresh = false;
};

// The view's top-left, in tile coordinates, and how many tiles it covers.
// A map larger than the view keeps the hero's cell in view and does not slide
// past the map edge. A smaller map is centered, so Origin may be negative.
struct WalkFrame {
    f32 OriginX = 0.0f;
    f32 OriginY = 0.0f;
    f32 TilesWide = 0.0f;
    f32 TilesHigh = 0.0f;
};

// Tile steps, walls, and exits. It stores the map id and the tile, never a
// pointer into Database, so a reload cannot dangle. Drawing is not here.
class Walker {
public:
    // Stands on that map's start tile. False when the id is not loaded.
    bool Enter(const Database& data, std::string_view mapId);

    // After a successful reload. Stays put when that map still has this tile
    // and the tile is walkable. Otherwise that map's start. When the map itself
    // is gone, `fallbackMapId`'s start, then the first loaded map. False when
    // nothing is left to stand on; the place is cleared.
    bool Adopt(const Database& data, std::string_view fallbackMapId);

    // Stands on this tile. False when the map is missing or the tile is not
    // walkable. Does not take a link, including when the tile is itself one.
    bool Place(const Database& data, std::string_view mapId, MapTile tile);

    // At most one step. A wall or the edge of the grid is Blocked and the hero
    // does not move. Stepping onto a link is Exited. Being placed on a link,
    // including an arrival tile that is itself a link, does not take it.
    WalkStep Advance(const Database& data, const WalkRules& rules, WalkInput input, f32 dt);

    [[nodiscard]] const WalkPlace& Place() const { return m_Place; }
    [[nodiscard]] bool Placed() const { return !m_Place.MapId.empty(); }

private:
    WalkStep TryStep(const Database& data, WalkDir direction);

    WalkPlace m_Place;
    WalkDir m_Held = WalkDir::None;
    f32 m_Wait = 0.0f;
};

// `tilesWide` and `tilesHigh` are the view, in tiles. Zero or a negative size
// leaves the origin at 0.
[[nodiscard]] WalkFrame FrameMap(const MapDef& map, MapTile hero, f32 tilesWide, f32 tilesHigh);

} // namespace Ascension
