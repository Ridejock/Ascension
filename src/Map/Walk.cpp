#include "Ascension/Map/Walk.h"

#include <algorithm>

namespace Ascension {

namespace {

bool Offset(MapTile from, WalkDir direction, MapTile& out)
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

bool TileWalkable(const Database& data, const MapDef& map, MapTile tile)
{
    const std::string* cell = map.Cell(tile.X, tile.Y);
    if (cell == nullptr)
        return false;
    const TileDef* def = data.FindTile(*cell);
    return def != nullptr && !def->Solid;
}

const MapLink* LinkOn(const MapDef& map, MapTile tile)
{
    for (const MapLink& link : map.Links) {
        if (link.Tile.X == tile.X && link.Tile.Y == tile.Y)
            return &link;
    }
    return nullptr;
}

bool PlaceOnStart(WalkPlace& place, WalkDir& held, f32& wait, const Database& data,
                  std::string_view mapId)
{
    const MapDef* map = data.FindMap(mapId);
    if (map == nullptr)
        return false;
    place.MapId = map->Id;
    place.Tile = map->Start;
    held = WalkDir::None;
    wait = 0.0f;
    return true;
}

} // namespace

bool Walker::Enter(const Database& data, std::string_view mapId)
{
    return PlaceOnStart(m_Place, m_Held, m_Wait, data, mapId);
}

bool Walker::Place(const Database& data, std::string_view mapId, MapTile tile)
{
    const MapDef* map = data.FindMap(mapId);
    if (map == nullptr || !TileWalkable(data, *map, tile))
        return false;
    m_Place.MapId = map->Id;
    m_Place.Tile = tile;
    m_Held = WalkDir::None;
    m_Wait = 0.0f;
    return true;
}

bool Walker::Adopt(const Database& data, std::string_view fallbackMapId)
{
    const MapDef* map = data.FindMap(m_Place.MapId);
    if (map != nullptr && TileWalkable(data, *map, m_Place.Tile)) {
        m_Held = WalkDir::None;
        m_Wait = 0.0f;
        return true;
    }
    if (map != nullptr)
        return PlaceOnStart(m_Place, m_Held, m_Wait, data, map->Id);
    if (PlaceOnStart(m_Place, m_Held, m_Wait, data, fallbackMapId))
        return true;
    if (!data.GetMaps().empty())
        return PlaceOnStart(m_Place, m_Held, m_Wait, data, data.GetMaps().front().Id);
    m_Place = {};
    m_Held = WalkDir::None;
    m_Wait = 0.0f;
    return false;
}

WalkStep Walker::TryStep(const Database& data, WalkDir direction)
{
    WalkStep step;
    step.Direction = direction;
    const MapDef* map = data.FindMap(m_Place.MapId);
    MapTile next;
    if (map == nullptr || !Offset(m_Place.Tile, direction, next) ||
        !TileWalkable(data, *map, next)) {
        step.Kind = WalkStepKind::Blocked;
        return step;
    }
    if (const MapLink* link = LinkOn(*map, next)) {
        const MapDef* target = data.FindMap(link->Map);
        if (target == nullptr || !TileWalkable(data, *target, link->Arrival)) {
            step.Kind = WalkStepKind::Blocked;
            return step;
        }
        m_Place.MapId = target->Id;
        m_Place.Tile = link->Arrival;
        step.Kind = WalkStepKind::Exited;
        step.Arrival = m_Place;
        return step;
    }
    m_Place.Tile = next;
    step.Kind = WalkStepKind::Moved;
    return step;
}

WalkStep Walker::Advance(const Database& data, const WalkRules& rules, WalkInput input, f32 dt)
{
    WalkStep none;
    if (!Placed() || input.Direction == WalkDir::None) {
        m_Held = WalkDir::None;
        m_Wait = 0.0f;
        return none;
    }
    i32 milliseconds = rules.StepMilliseconds;
    if (milliseconds < 1)
        milliseconds = 1;
    const f32 step = static_cast<f32>(milliseconds) / 1000.0f;
    const bool turned = input.Direction != m_Held;
    if (input.Fresh || turned) {
        m_Held = input.Direction;
        m_Wait = step;
        return TryStep(data, input.Direction);
    }
    if (dt < 0.0f)
        dt = 0.0f;
    m_Wait -= dt;
    if (m_Wait > 0.0f)
        return none;
    m_Wait = step;
    return TryStep(data, input.Direction);
}

WalkFrame FrameMap(const MapDef& map, MapTile hero, f32 tilesWide, f32 tilesHigh)
{
    WalkFrame frame;
    frame.TilesWide = tilesWide;
    frame.TilesHigh = tilesHigh;
    if (tilesWide <= 0.0f || tilesHigh <= 0.0f || map.Width <= 0 || map.Height <= 0)
        return frame;
    const f32 mapWide = static_cast<f32>(map.Width);
    const f32 mapHigh = static_cast<f32>(map.Height);
    if (mapWide <= tilesWide) {
        frame.OriginX = (mapWide - tilesWide) * 0.5f;
    } else {
        const f32 heroX = static_cast<f32>(hero.X) + 0.5f;
        frame.OriginX = std::clamp(heroX - tilesWide * 0.5f, 0.0f, mapWide - tilesWide);
    }
    if (mapHigh <= tilesHigh) {
        frame.OriginY = (mapHigh - tilesHigh) * 0.5f;
    } else {
        const f32 heroY = static_cast<f32>(hero.Y) + 0.5f;
        frame.OriginY = std::clamp(heroY - tilesHigh * 0.5f, 0.0f, mapHigh - tilesHigh);
    }
    return frame;
}

} // namespace Ascension
