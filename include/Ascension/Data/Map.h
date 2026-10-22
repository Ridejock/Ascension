#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Emerald/Core/Defines.h"

namespace Ascension {

// One cell. X grows east and Y grows south. Both start at 0 on the first
// character of the first row.
struct MapTile {
    i32 X = 0;
    i32 Y = 0;
};

// One terrain kind from tiles.json. Solid blocks a later step. Color is the
// palette rectangle drawn until a sheet is used. Sheet is an optional id for
// that later art pass and is not opened here.
struct TileDef {
    std::string Id;
    bool Solid = false;
    u8 Red = 0;
    u8 Green = 0;
    u8 Blue = 0;
    std::string Sheet;
};

// Present when a spawn lists `mover`. This slice accepts only an empty object.
// A later step names the step (idle first) on this struct.
struct SpawnMover {
    bool Listed = false;
};

// A foe standing on one tile. Id is unique across every map, so a save can
// record cleared spawns without using a list index. Once means a win clears
// it. NoFlee is the flag a later handoff reads for the boss. Encounter is an
// id from encounters.json.
struct MapSpawn {
    std::string Id;
    MapTile Tile;
    std::string Encounter;
    bool Once = true;
    bool NoFlee = false;
    SpawnMover Mover;
};

// A tile that leaves this map. Arrival is a walkable tile on Map.
struct MapLink {
    MapTile Tile;
    std::string Map;
    MapTile Arrival;
};

enum class MapPointKind : u8 { Talk, Shop, Rest };

// A tile the hero can use without a fight. Talk names a dialogue id. Shop is
// the shop in shop.json. Rest is where a later loss can send the hero back.
struct MapPoint {
    MapPointKind Kind = MapPointKind::Rest;
    MapTile Tile;
    std::string Dialogue;
};

struct MapDef {
    std::string Id;
    // Set on the one map a loss returns to. At most one map sets it.
    bool Hub = false;
    i32 Width = 0;
    i32 Height = 0;
    // Tile ids, row-major, y * Width + x. Length is Width * Height.
    std::vector<std::string> Cells;
    MapTile Start;
    std::vector<MapSpawn> Spawns;
    std::vector<MapLink> Links;
    std::vector<MapPoint> Points;

    [[nodiscard]] bool InBounds(i32 x, i32 y) const;
    // Null when x, y is outside the grid.
    [[nodiscard]] const std::string* Cell(i32 x, i32 y) const;
    [[nodiscard]] const MapSpawn* FindSpawn(std::string_view id) const;
};

// Both pointers belong to a Database and last until its next successful load.
struct MapSpawnRef {
    const MapDef* Map = nullptr;
    const MapSpawn* Spawn = nullptr;
};

[[nodiscard]] const char* ToString(MapPointKind kind);

inline bool MapDef::InBounds(i32 x, i32 y) const
{
    return x >= 0 && y >= 0 && x < Width && y < Height;
}

inline const std::string* MapDef::Cell(i32 x, i32 y) const
{
    if (!InBounds(x, y))
        return nullptr;
    const usize index = static_cast<usize>(y) * static_cast<usize>(Width) + static_cast<usize>(x);
    return &Cells[index];
}

inline const MapSpawn* MapDef::FindSpawn(std::string_view id) const
{
    for (const MapSpawn& spawn : Spawns) {
        if (spawn.Id == id)
            return &spawn;
    }
    return nullptr;
}

inline const char* ToString(MapPointKind kind)
{
    if (kind == MapPointKind::Talk)
        return "talk";
    if (kind == MapPointKind::Shop)
        return "shop";
    return "rest";
}

} // namespace Ascension
