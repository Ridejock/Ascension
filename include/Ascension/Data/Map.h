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

// A rectangle of cells. X and Y are the top-left. Width grows east and Height
// grows south. Both counts are at least 1 when a file has named the span.
struct MapSpan {
    i32 X = 0;
    i32 Y = 0;
    i32 Width = 1;
    i32 Height = 1;
};

// One picture standing on the map. Id is the art-pack key, shared by every
// placement that uses that picture. Tile is the top-left of the footprint.
// Width and Height are that footprint in cells. 0 means this placement left
// the size unset, and a pack may supply it. Open cells lie inside the
// footprint and are not blocked: a door, a rest, a shop, a talk. The picture
// is drawn from this tile. A picture taller than the footprint hangs north of
// it and does not block those cells.
struct MapProp {
    std::string Id;
    MapTile Tile;
    i32 Width = 0;
    i32 Height = 0;
    std::vector<MapSpan> Open;
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
    std::vector<MapProp> Props;

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

// How a released glide eases onto its tile. Linear holds a constant settle speed.
// Smooth is smoothstep. Held movement does not use this: that speed stays flat,
// so a held direction does not slow down at each tile.
enum class WalkEase : u8 { Linear, Smooth };

// How the hero moves, from walk.json. MoveMilliseconds is the time to cross one
// tile while a direction is held. SettleMilliseconds is the time to glide one tile
// after that direction is released; a shorter gap takes the same fraction of it.
// TilePixels is the drawn size of one cell. HeroInsetPercent is the empty margin on
// each side of the hero square, so 0 fills the cell. The three colors are palette
// rectangles: the hero, the inner square on an exit, and the inner square on a
// spawn, and the inner squares on a rest, a talk, and a shop. The numbers here
// match data/walk.json; the file is what a load uses. A file that omits a point
// color keeps the number below.
struct WalkRules {
    i32 MoveMilliseconds = 180;
    i32 SettleMilliseconds = 180;
    WalkEase Easing = WalkEase::Linear;
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

// True when (x, y) sits in the span. A span with a non-positive side covers nothing.
inline bool SpanCovers(const MapSpan& span, i32 x, i32 y)
{
    if (span.Width < 1 || span.Height < 1 || x < span.X || y < span.Y)
        return false;
    return x - span.X < span.Width && y - span.Y < span.Height;
}

// True when the footprint covers the cell and no open span does. Width or
// height below 1 is an unset footprint, which covers nothing. `width` and
// `height` are the resolved size: the map's, or a pack's when the map left
// them unset.
inline bool PropFootprintCovers(const MapProp& prop, i32 width, i32 height, i32 x, i32 y)
{
    if (width < 1 || height < 1 || x < prop.Tile.X || y < prop.Tile.Y)
        return false;
    if (x - prop.Tile.X >= width || y - prop.Tile.Y >= height)
        return false;
    for (const MapSpan& hole : prop.Open) {
        if (SpanCovers(hole, x, y))
            return false;
    }
    return true;
}

inline bool MapPropBlocks(const MapProp& prop, i32 x, i32 y)
{
    return PropFootprintCovers(prop, prop.Width, prop.Height, x, y);
}

inline bool MapBlocks(const MapDef& map, i32 x, i32 y)
{
    for (const MapProp& prop : map.Props) {
        if (MapPropBlocks(prop, x, y))
            return true;
    }
    return false;
}

// Where a prop picture is drawn, in tile coordinates. The bottom-left of the
// picture sits on the bottom-left of the footprint, so a picture taller than
// the footprint hangs north and SortY stays on the footprint's south row.
// A later row is drawn later. `cellsWide` and `cellsHigh` are the picture.
// Zero uses the footprint, and a footprint that is also unset is one cell.
struct PropFrame {
    f32 X = 0.0f;
    f32 Y = 0.0f;
    f32 Width = 1.0f;
    f32 Height = 1.0f;
    i32 SortY = 0;
};

inline PropFrame FrameProp(const MapProp& prop, i32 cellsWide, i32 cellsHigh)
{
    const i32 footWide = prop.Width > 0 ? prop.Width : (cellsWide > 0 ? cellsWide : 1);
    const i32 footHigh = prop.Height > 0 ? prop.Height : (cellsHigh > 0 ? cellsHigh : 1);
    const i32 drawWide = cellsWide > 0 ? cellsWide : footWide;
    const i32 drawHigh = cellsHigh > 0 ? cellsHigh : footHigh;
    PropFrame frame;
    frame.Width = static_cast<f32>(drawWide);
    frame.Height = static_cast<f32>(drawHigh);
    frame.X = static_cast<f32>(prop.Tile.X);
    frame.Y = static_cast<f32>(prop.Tile.Y + footHigh - drawHigh);
    frame.SortY = prop.Tile.Y + footHigh - 1;
    return frame;
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

// What a packed map asks for on a point. Square is the palette rectangle.
// Npc names a dialogue id. Shop and Rest are markers, not content ids.
enum class PointArt : u8 { Square, Npc, Shop, Rest };

struct PointPicture {
    PointArt Art = PointArt::Square;
    std::string_view Dialogue;
};

// True when a talk point on this map already stands for `dialogue`.
[[nodiscard]] inline bool MapTalks(const MapDef& map, std::string_view dialogue)
{
    if (dialogue.empty())
        return false;
    for (const MapPoint& point : map.Points) {
        if (point.Kind == MapPointKind::Talk && point.Dialogue == dialogue)
            return true;
    }
    return false;
}

// A talk point is that dialogue's sprite. A rest point is the rest marker.
// A shop point is the shop's dialogue sprite only when that id is set and no
// talk point on this map already uses it. Otherwise it is the shop marker,
// including a shop whose dialogue is empty.
[[nodiscard]] inline PointPicture PictureForPoint(const MapDef& map, const MapPoint& point,
                                                  std::string_view shopDialogue)
{
    PointPicture picture;
    if (point.Kind == MapPointKind::Talk) {
        picture.Art = PointArt::Npc;
        picture.Dialogue = point.Dialogue;
        return picture;
    }
    if (point.Kind == MapPointKind::Rest) {
        picture.Art = PointArt::Rest;
        return picture;
    }
    if (point.Kind == MapPointKind::Shop) {
        if (!shopDialogue.empty() && !MapTalks(map, shopDialogue)) {
            picture.Art = PointArt::Npc;
            picture.Dialogue = shopDialogue;
            return picture;
        }
        picture.Art = PointArt::Shop;
    }
    return picture;
}

} // namespace Ascension
