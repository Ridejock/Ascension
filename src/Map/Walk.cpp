#include "Ascension/Map/Walk.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Ascension {

namespace {

constexpr f32 kAlign = 0.0001f;

f32 Absf(f32 value)
{
    return value < 0.0f ? -value : value;
}

bool Nearly(f32 value, f32 target)
{
    return Absf(value - target) <= kAlign;
}

i32 RoundTile(f32 value)
{
    return static_cast<i32>(std::floor(static_cast<double>(value) + 0.5));
}

f32 Ceilf(f32 value)
{
    return static_cast<f32>(std::ceil(static_cast<double>(value)));
}

f32 Floorf(f32 value)
{
    return static_cast<f32>(std::floor(static_cast<double>(value)));
}

f32 SecondsOf(i32 milliseconds)
{
    if (milliseconds < 1)
        milliseconds = 1;
    return static_cast<f32>(milliseconds) / 1000.0f;
}

f32 ApplyEase(WalkEase ease, f32 t)
{
    if (t < 0.0f)
        t = 0.0f;
    if (t > 1.0f)
        t = 1.0f;
    if (ease == WalkEase::Smooth)
        return t * t * (3.0f - 2.0f * t);
    return t;
}

// The next integer in front of `value`. A point sitting on an integer has
// reached that tile, so the following one is a full tile further. A point just
// short of an integer has not reached it yet.
f32 AxisAhead(f32 value)
{
    const f32 floor = Floorf(value);
    if (Nearly(value, floor))
        return floor + 1.0f;
    return Ceilf(value);
}

f32 AxisBehind(f32 value)
{
    const f32 top = Ceilf(value);
    if (Nearly(value, top))
        return top - 1.0f;
    return Floorf(value);
}

bool Opposite(WalkDir left, WalkDir right)
{
    if (left == WalkDir::North)
        return right == WalkDir::South;
    if (left == WalkDir::South)
        return right == WalkDir::North;
    if (left == WalkDir::East)
        return right == WalkDir::West;
    if (left == WalkDir::West)
        return right == WalkDir::East;
    return false;
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

bool PlaceOnStart(WalkPlace& place, const Database& data, std::string_view mapId)
{
    const MapDef* map = data.FindMap(mapId);
    if (map == nullptr)
        return false;
    place.MapId = map->Id;
    place.Tile = map->Start;
    return true;
}

WalkStep BlockedStep(WalkDir direction)
{
    WalkStep step;
    step.Kind = WalkStepKind::Blocked;
    step.Direction = direction;
    return step;
}

void GlideToward(f32& x, f32& y, f32 targetX, f32 targetY, f32 budget)
{
    const f32 dx = targetX - x;
    const f32 dy = targetY - y;
    const f32 adx = Absf(dx);
    const f32 ady = Absf(dy);
    const f32 span = adx > ady ? adx : ady;
    if (span <= budget || span <= kAlign) {
        x = targetX;
        y = targetY;
        return;
    }
    const f32 scale = budget / span;
    x += dx * scale;
    y += dy * scale;
}

} // namespace

void Walker::SyncPose()
{
    m_X = static_cast<f32>(m_Place.Tile.X);
    m_Y = static_cast<f32>(m_Place.Tile.Y);
    m_Motion = WalkMotion::Idle;
    m_Dir = WalkDir::None;
    m_Queue = WalkDir::None;
    m_Nudge = false;
    m_SettleElapsed = 0.0f;
    m_SettleDuration = 0.0f;
}

void Walker::Halt()
{
    SyncPose();
}

WalkPoint Walker::DrawTile() const
{
    WalkPoint point;
    point.X = m_X;
    point.Y = m_Y;
    return point;
}

bool Walker::Aligned() const
{
    return Nearly(m_X, static_cast<f32>(m_Place.Tile.X)) &&
           Nearly(m_Y, static_cast<f32>(m_Place.Tile.Y));
}

f32 Walker::Along(WalkDir direction) const
{
    if (direction == WalkDir::East || direction == WalkDir::West)
        return m_X;
    return m_Y;
}

f32 Walker::Boundary(WalkDir direction) const
{
    if (direction == WalkDir::East)
        return AxisAhead(m_X);
    if (direction == WalkDir::West)
        return AxisBehind(m_X);
    if (direction == WalkDir::South)
        return AxisAhead(m_Y);
    return AxisBehind(m_Y);
}

void Walker::SetAlong(WalkDir direction, f32 value)
{
    if (direction == WalkDir::East || direction == WalkDir::West)
        m_X = value;
    else
        m_Y = value;
}

void Walker::Nudge(WalkDir direction, f32 step)
{
    if (direction == WalkDir::East)
        m_X += step;
    else if (direction == WalkDir::West)
        m_X -= step;
    else if (direction == WalkDir::South)
        m_Y += step;
    else if (direction == WalkDir::North)
        m_Y -= step;
}

MapTile Walker::TileAhead() const
{
    f32 x = m_X;
    f32 y = m_Y;
    if (m_Dir == WalkDir::East)
        x = AxisAhead(m_X);
    else if (m_Dir == WalkDir::West)
        x = AxisBehind(m_X);
    else if (m_Dir == WalkDir::South)
        y = AxisAhead(m_Y);
    else if (m_Dir == WalkDir::North)
        y = AxisBehind(m_Y);
    MapTile tile;
    tile.X = RoundTile(x);
    tile.Y = RoundTile(y);
    return tile;
}

bool Walker::CanLand(const Database& data, MapTile tile) const
{
    const MapDef* map = data.FindMap(m_Place.MapId);
    if (map == nullptr || !TileWalkable(data, *map, tile))
        return false;
    if (const MapLink* link = LinkOn(*map, tile)) {
        const MapDef* target = data.FindMap(link->Map);
        return target != nullptr && TileWalkable(data, *target, link->Arrival);
    }
    return true;
}

bool Walker::Enter(const Database& data, std::string_view mapId)
{
    if (!PlaceOnStart(m_Place, data, mapId))
        return false;
    SyncPose();
    return true;
}

bool Walker::Place(const Database& data, std::string_view mapId, MapTile tile)
{
    const MapDef* map = data.FindMap(mapId);
    if (map == nullptr || !TileWalkable(data, *map, tile))
        return false;
    m_Place.MapId = map->Id;
    m_Place.Tile = tile;
    SyncPose();
    return true;
}

bool Walker::Adopt(const Database& data, std::string_view fallbackMapId)
{
    const MapDef* map = data.FindMap(m_Place.MapId);
    if (map != nullptr && TileWalkable(data, *map, m_Place.Tile)) {
        SyncPose();
        return true;
    }
    if (map != nullptr && PlaceOnStart(m_Place, data, map->Id)) {
        SyncPose();
        return true;
    }
    if (PlaceOnStart(m_Place, data, fallbackMapId)) {
        SyncPose();
        return true;
    }
    if (!data.GetMaps().empty() && PlaceOnStart(m_Place, data, data.GetMaps().front().Id)) {
        SyncPose();
        return true;
    }
    m_Place = {};
    SyncPose();
    return false;
}

WalkStep Walker::CommitIfArrived(const Database& data)
{
    WalkStep none;
    if (!Nearly(m_X, static_cast<f32>(RoundTile(m_X))) ||
        !Nearly(m_Y, static_cast<f32>(RoundTile(m_Y))))
        return none;
    MapTile tile;
    tile.X = RoundTile(m_X);
    tile.Y = RoundTile(m_Y);
    if (tile.X == m_Place.Tile.X && tile.Y == m_Place.Tile.Y)
        return none;
    if (!CanLand(data, tile)) {
        m_X = static_cast<f32>(m_Place.Tile.X);
        m_Y = static_cast<f32>(m_Place.Tile.Y);
        return BlockedStep(m_Dir);
    }
    return CommitLanding(data, tile);
}

WalkStep Walker::CommitLanding(const Database& data, MapTile tile)
{
    WalkStep step;
    step.Direction = m_Dir;
    if (tile.X == m_Place.Tile.X && tile.Y == m_Place.Tile.Y)
        return step;
    const MapDef* map = data.FindMap(m_Place.MapId);
    if (map == nullptr) {
        step.Kind = WalkStepKind::Blocked;
        return step;
    }
    if (const MapLink* link = LinkOn(*map, tile)) {
        const MapDef* target = data.FindMap(link->Map);
        if (target == nullptr || !TileWalkable(data, *target, link->Arrival)) {
            step.Kind = WalkStepKind::Blocked;
            return step;
        }
        m_Place.MapId = target->Id;
        m_Place.Tile = link->Arrival;
        m_X = static_cast<f32>(m_Place.Tile.X);
        m_Y = static_cast<f32>(m_Place.Tile.Y);
        step.Kind = WalkStepKind::Exited;
        step.Arrival = m_Place;
        return step;
    }
    m_Place.Tile = tile;
    m_X = static_cast<f32>(tile.X);
    m_Y = static_cast<f32>(tile.Y);
    step.Kind = WalkStepKind::Moved;
    return step;
}

void Walker::BeginSettle(const Database& data, const WalkRules& rules)
{
    f32 toX = static_cast<f32>(m_Place.Tile.X);
    f32 toY = static_cast<f32>(m_Place.Tile.Y);
    if (m_Dir != WalkDir::None) {
        const MapTile ahead = TileAhead();
        if (CanLand(data, ahead)) {
            toX = static_cast<f32>(ahead.X);
            toY = static_cast<f32>(ahead.Y);
        }
    }
    m_FromX = m_X;
    m_FromY = m_Y;
    m_ToX = toX;
    m_ToY = toY;
    m_SettleElapsed = 0.0f;
    const f32 distance = Absf(toX - m_X) + Absf(toY - m_Y);
    m_SettleDuration = distance * SecondsOf(rules.SettleMilliseconds);
    if (m_SettleDuration <= kAlign) {
        m_X = toX;
        m_Y = toY;
        m_Motion = WalkMotion::Idle;
        m_Nudge = false;
        return;
    }
    m_Motion = WalkMotion::Settle;
}

void Walker::ReleaseTravel(const Database& data, const WalkRules& rules)
{
    if (!Aligned()) {
        BeginSettle(data, rules);
        return;
    }
    m_X = static_cast<f32>(RoundTile(m_X));
    m_Y = static_cast<f32>(RoundTile(m_Y));
    if (m_Nudge && m_Dir != WalkDir::None && CanLand(data, TileAhead())) {
        m_Nudge = false;
        BeginSettle(data, rules);
        return;
    }
    m_Nudge = false;
    m_Queue = WalkDir::None;
    m_Motion = WalkMotion::Idle;
}

void Walker::ResolveInput(const Database& data, const WalkRules& rules, WalkInput input)
{
    if (input.Direction == WalkDir::None) {
        if (m_Motion == WalkMotion::Travel)
            ReleaseTravel(data, rules);
        return;
    }
    if (m_Motion == WalkMotion::Idle || m_Dir == WalkDir::None) {
        m_Dir = input.Direction;
        m_Motion = WalkMotion::Travel;
        m_Queue = WalkDir::None;
        m_Nudge = false;
        return;
    }
    if (input.Direction == m_Dir) {
        if (m_Motion == WalkMotion::Settle)
            m_Motion = WalkMotion::Travel;
        return;
    }
    if (Opposite(input.Direction, m_Dir)) {
        m_Dir = input.Direction;
        m_Queue = WalkDir::None;
        m_Nudge = false;
        m_Motion = WalkMotion::Travel;
        return;
    }
    m_Queue = input.Direction;
    if (!Aligned())
        return;
    m_Dir = input.Direction;
    m_Queue = WalkDir::None;
    m_Nudge = true;
    m_Motion = WalkMotion::Travel;
}

WalkStep Walker::FinishArrival(const Database& data, const WalkRules& rules, WalkInput input,
                               WalkStep step)
{
    if (step.Kind == WalkStepKind::Blocked) {
        m_Motion = WalkMotion::Idle;
        m_Nudge = false;
        return step;
    }
    if (step.Kind == WalkStepKind::Exited) {
        m_Queue = WalkDir::None;
        m_Nudge = false;
        if (input.Direction == WalkDir::None)
            m_Motion = WalkMotion::Idle;
        else {
            m_Dir = input.Direction;
            m_Motion = WalkMotion::Travel;
        }
        return step;
    }
    if (m_Queue != WalkDir::None) {
        const WalkDir next = m_Queue;
        m_Queue = WalkDir::None;
        const WalkDir previous = m_Dir;
        m_Dir = next;
        if (!CanLand(data, TileAhead())) {
            m_Dir = previous;
            m_Nudge = false;
            m_Motion = input.Direction == WalkDir::None ? WalkMotion::Idle : WalkMotion::Travel;
            return step;
        }
        if (input.Direction == next) {
            m_Nudge = true;
            m_Motion = WalkMotion::Travel;
        } else {
            m_Nudge = false;
            BeginSettle(data, rules);
        }
        return step;
    }
    m_Nudge = false;
    if (input.Direction == WalkDir::None)
        m_Motion = WalkMotion::Idle;
    else
        m_Motion = WalkMotion::Travel;
    return step;
}

WalkStep Walker::AdvanceTravel(const Database& data, const WalkRules& rules, WalkInput input,
                               f32 distance)
{
    WalkStep crossed;
    bool blocked = false;
    f32 left = distance;
    if (left < 0.0f)
        left = 0.0f;
    const f32 startX = m_X;
    const f32 startY = m_Y;
    for (i32 guard = 0; guard < 4 && left > kAlign && crossed.Kind == WalkStepKind::None && !blocked;
         ++guard) {
        const f32 boundary = Boundary(m_Dir);
        f32 room = boundary - Along(m_Dir);
        if (m_Dir == WalkDir::West || m_Dir == WalkDir::North)
            room = Along(m_Dir) - boundary;
        if (room < 0.0f)
            room = 0.0f;
        if (room <= kAlign) {
            // The boundary is where the anchor sits on the next tile. Reject a
            // wall before the picture steps onto it; a sliver still on this tile
            // just closes up so the next pass measures a full tile.
            f32 landedX = m_X;
            f32 landedY = m_Y;
            if (m_Dir == WalkDir::East || m_Dir == WalkDir::West)
                landedX = boundary;
            else
                landedY = boundary;
            MapTile landing;
            landing.X = RoundTile(landedX);
            landing.Y = RoundTile(landedY);
            const bool leaving =
                landing.X != m_Place.Tile.X || landing.Y != m_Place.Tile.Y;
            if (!leaving) {
                SetAlong(m_Dir, boundary);
                continue;
            }
            if (!CanLand(data, landing)) {
                blocked = true;
                break;
            }
            SetAlong(m_Dir, boundary);
            crossed = CommitLanding(data, landing);
            break;
        }
        MapTile landing;
        if (m_Dir == WalkDir::East || m_Dir == WalkDir::West) {
            landing.X = RoundTile(boundary);
            landing.Y = RoundTile(m_Y);
        } else {
            landing.X = RoundTile(m_X);
            landing.Y = RoundTile(boundary);
        }
        const bool leaving = landing.X != m_Place.Tile.X || landing.Y != m_Place.Tile.Y;
        if (leaving && !CanLand(data, landing)) {
            blocked = true;
            break;
        }
        f32 step = left;
        if (step > room)
            step = room;
        const bool land = room - step <= kAlign;
        if (step > kAlign)
            m_Nudge = false;
        if (land) {
            SetAlong(m_Dir, boundary);
            left -= room;
        } else {
            Nudge(m_Dir, step);
            left = 0.0f;
        }
        if (land && leaving)
            crossed = CommitLanding(data, landing);
    }
    if (crossed.Kind != WalkStepKind::None)
        return FinishArrival(data, rules, input, crossed);
    if (blocked && Nearly(m_X, startX) && Nearly(m_Y, startY)) {
        if (Aligned())
            m_Motion = WalkMotion::Idle;
        return BlockedStep(m_Dir);
    }
    return crossed;
}

WalkStep Walker::AdvanceSettle(const Database& data, const WalkRules& rules, WalkInput input, f32 dt)
{
    if (dt < 0.0f)
        dt = 0.0f;
    m_SettleElapsed += dt;
    f32 t = 1.0f;
    if (m_SettleDuration > kAlign)
        t = m_SettleElapsed / m_SettleDuration;
    if (t > 1.0f)
        t = 1.0f;
    const f32 eased = ApplyEase(rules.Easing, t);
    m_X = m_FromX + (m_ToX - m_FromX) * eased;
    m_Y = m_FromY + (m_ToY - m_FromY) * eased;
    if (t < 1.0f)
        return {};
    m_X = m_ToX;
    m_Y = m_ToY;
    MapTile tile;
    tile.X = RoundTile(m_X);
    tile.Y = RoundTile(m_Y);
    WalkStep step = CommitLanding(data, tile);
    m_Motion = WalkMotion::Idle;
    m_Nudge = false;
    if (step.Kind == WalkStepKind::Exited) {
        m_Queue = WalkDir::None;
        if (input.Direction != WalkDir::None) {
            m_Dir = input.Direction;
            m_Motion = WalkMotion::Travel;
        }
        return step;
    }
    if (m_Queue != WalkDir::None) {
        m_Dir = m_Queue;
        m_Queue = WalkDir::None;
        if (CanLand(data, TileAhead()))
            BeginSettle(data, rules);
        else
            m_Motion = WalkMotion::Idle;
    }
    return step;
}

WalkStep Walker::Advance(const Database& data, const WalkRules& rules, WalkInput input, f32 dt)
{
    WalkStep none;
    if (!Placed())
        return none;
    if (dt < 0.0f)
        dt = 0.0f;
    const WalkStep pending = CommitIfArrived(data);
    if (pending.Kind != WalkStepKind::None)
        return FinishArrival(data, rules, input, pending);
    ResolveInput(data, rules, input);
    if (m_Motion == WalkMotion::Settle)
        return AdvanceSettle(data, rules, input, dt);
    if (m_Motion == WalkMotion::Travel) {
        const f32 distance = dt / SecondsOf(rules.MoveMilliseconds);
        return AdvanceTravel(data, rules, input, distance);
    }
    return none;
}

void SpawnGlides::Snap(std::span<const SpawnPlace> places)
{
    m_Entries.clear();
    for (const SpawnPlace& place : places) {
        Entry entry;
        entry.Id = place.Id;
        entry.X = static_cast<f32>(place.Tile.X);
        entry.Y = static_cast<f32>(place.Tile.Y);
        m_Entries.push_back(std::move(entry));
    }
}

void SpawnGlides::Advance(std::span<const SpawnPlace> places, f32 dt, f32 moveSeconds)
{
    if (moveSeconds < kAlign)
        moveSeconds = kAlign;
    if (dt < 0.0f)
        dt = 0.0f;
    const f32 budget = dt / moveSeconds;
    std::vector<Entry> next;
    for (const SpawnPlace& place : places) {
        Entry entry;
        entry.Id = place.Id;
        entry.X = static_cast<f32>(place.Tile.X);
        entry.Y = static_cast<f32>(place.Tile.Y);
        for (const Entry& older : m_Entries) {
            if (older.Id != place.Id)
                continue;
            entry.X = older.X;
            entry.Y = older.Y;
            break;
        }
        GlideToward(entry.X, entry.Y, static_cast<f32>(place.Tile.X),
                    static_cast<f32>(place.Tile.Y), budget);
        next.push_back(std::move(entry));
    }
    m_Entries = std::move(next);
}

WalkPoint SpawnGlides::Sample(std::string_view id, MapTile fallback) const
{
    WalkPoint point;
    point.X = static_cast<f32>(fallback.X);
    point.Y = static_cast<f32>(fallback.Y);
    for (const Entry& entry : m_Entries) {
        if (entry.Id != id)
            continue;
        point.X = entry.X;
        point.Y = entry.Y;
        break;
    }
    return point;
}

WalkFrame FrameMap(const MapDef& map, f32 heroX, f32 heroY, f32 tilesWide, f32 tilesHigh)
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
        const f32 heroCenterX = heroX + 0.5f;
        frame.OriginX = std::clamp(heroCenterX - tilesWide * 0.5f, 0.0f, mapWide - tilesWide);
    }
    if (mapHigh <= tilesHigh) {
        frame.OriginY = (mapHigh - tilesHigh) * 0.5f;
    } else {
        const f32 heroCenterY = heroY + 0.5f;
        frame.OriginY = std::clamp(heroCenterY - tilesHigh * 0.5f, 0.0f, mapHigh - tilesHigh);
    }
    return frame;
}

WalkFrame FrameMap(const MapDef& map, MapTile hero, f32 tilesWide, f32 tilesHigh)
{
    return FrameMap(map, static_cast<f32>(hero.X), static_cast<f32>(hero.Y), tilesWide, tilesHigh);
}

} // namespace Ascension
