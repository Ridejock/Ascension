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

// One named step from movers.json. Idle stands on its tile. The load rejects any
// other kind, so chase, kite, and forage are a later name plus a branch in the step,
// not a behavior this file already runs.
enum class MoverKind : u8 { Idle };

struct MoverDef {
    std::string Id;
    MoverKind Kind = MoverKind::Idle;
};

// Where a spawn is standing. Home is the tile in the map file. Tile is where the
// last step left it. Idle never moves Tile off Home. The save does not record Tile:
// only idle ships, so a load still matches the file.
struct SpawnPlace {
    std::string Id;
    MapTile Tile;
    MapTile Home;
};

// Present when a spawn lists `mover`. Behavior is an id in movers.json.
struct SpawnMover {
    bool Listed = false;
    std::string Behavior;
};

// A status other spawns carry into a fight when they stand within Radius of this
// one. Radius is in tiles: the greater of the east-west gap and the north-south gap,
// so 1 is the eight neighbors. This spawn does not grant the status to itself. A
// cleared spawn does not grant it. The check runs when the fight is offered.
struct SpawnAura {
    bool Listed = false;
    i32 Radius = 0;
    std::string Status;
};

// A foe standing on one tile. Id is unique across every map, so a save can
// record cleared spawns without using a list index. Once means a win clears
// it. NoFlee is the boss-fight flag: that spawn cannot be fled. A foe with
// boss set cannot be fled either. Encounter is an id from encounters.json.
struct MapSpawn {
    std::string Id;
    MapTile Tile;
    std::string Encounter;
    bool Once = true;
    bool NoFlee = false;
    SpawnMover Mover;
    SpawnAura Aura;
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

// How a hero steps, from walk.json. The first press of a direction steps at once.
// StepMilliseconds is the pause before another step while that direction stays held.
// TilePixels is the drawn size of one cell. HeroInsetPercent is the empty margin on
// each side of the hero square, so 0 fills the cell. The three colors are palette
// rectangles: the hero, the inner square on an exit, and the inner square on a
// spawn, and the inner squares on a rest, a talk, and a shop. The numbers here
// match data/walk.json; the file is what a load uses. A file that omits a point
// color keeps the number below.
struct WalkRules {
    i32 StepMilliseconds = 180;
    i32 TilePixels = 64;
    i32 HeroInsetPercent = 22;
    u8 HeroRed = 223;
    u8 HeroGreen = 213;
    u8 HeroBlue = 188;
    u8 LinkRed = 208;
    u8 LinkGreen = 154;
    u8 LinkBlue = 42;
    u8 SpawnRed = 138;
    u8 SpawnGreen = 166;
    u8 SpawnBlue = 190;
    // fol1, skinA1, and leath2 on the project palette.
    u8 RestRed = 58;
    u8 RestGreen = 138;
    u8 RestBlue = 62;
    u8 TalkRed = 208;
    u8 TalkGreen = 138;
    u8 TalkBlue = 104;
    u8 ShopRed = 176;
    u8 ShopGreen = 122;
    u8 ShopBlue = 68;
};

[[nodiscard]] const char* ToString(MapPointKind kind);
[[nodiscard]] const char* ToString(MoverKind kind);

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

inline const char* ToString(MoverKind)
{
    return "idle";
}

} // namespace Ascension
