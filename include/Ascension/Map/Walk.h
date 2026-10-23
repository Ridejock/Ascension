#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

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

// One direction for this tick. Fresh is a new press. A held direction keeps
// Direction set and Fresh false. Direction None is a release.
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

// Drawn point in tile coordinates. Integers sit on a tile's corner, which is
// where a sprite anchored to that tile is drawn.
struct WalkPoint {
    f32 X = 0.0f;
    f32 Y = 0.0f;
};

// Continuous overland motion. Place() is the committed tile. It changes when the
// drawn point reaches the next tile in the direction of travel, and that is when
// a step is reported, so a spawn, a rest, or an exit is not seen early and is not
// skipped. Collision is that next tile. A held direction keeps moving through the
// boundary with no extra wait. Releasing glides from the current point to the next
// tile in the direction of travel, so the picture does not pop. A press during
// travel is kept and taken at that boundary. Saves read Place(), never the fraction.
// Drawing is not here.
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

    // At most one committed tile. A wall or the edge of the grid is Blocked and
    // the drawn point does not enter it. Reaching a link is Exited. Being placed
    // on a link, including an arrival tile that is itself a link, does not take it.
    // dt is seconds. A negative dt is treated as zero. A long frame still commits
    // one tile.
    WalkStep Advance(const Database& data, const WalkRules& rules, WalkInput input, f32 dt);

    // Drops a glide and stands on the committed tile.
    void Halt();

    [[nodiscard]] const WalkPlace& Place() const { return m_Place; }
    [[nodiscard]] bool Placed() const { return !m_Place.MapId.empty(); }
    [[nodiscard]] WalkPoint DrawTile() const;
    [[nodiscard]] bool Moving() const { return m_Motion != WalkMotion::Idle; }

private:
    enum class WalkMotion : u8 { Idle, Travel, Settle };

    void SyncPose();
    void ReleaseTravel(const Database& data, const WalkRules& rules);
    void BeginSettle(const Database& data, const WalkRules& rules);
    void ResolveInput(const Database& data, const WalkRules& rules, WalkInput input);
    WalkStep AdvanceTravel(const Database& data, const WalkRules& rules, WalkInput input,
                           f32 distance);
    WalkStep AdvanceSettle(const Database& data, const WalkRules& rules, WalkInput input, f32 dt);
    WalkStep FinishArrival(const Database& data, const WalkRules& rules, WalkInput input,
                           WalkStep step);
    WalkStep CommitLanding(const Database& data, MapTile tile);
    WalkStep CommitIfArrived(const Database& data);
    [[nodiscard]] bool CanLand(const Database& data, MapTile tile) const;
    [[nodiscard]] bool Aligned() const;
    [[nodiscard]] MapTile TileAhead() const;
    [[nodiscard]] f32 Along(WalkDir direction) const;
    [[nodiscard]] f32 Boundary(WalkDir direction) const;
    void SetAlong(WalkDir direction, f32 value);
    void Nudge(WalkDir direction, f32 step);

    WalkPlace m_Place;
    WalkMotion m_Motion = WalkMotion::Idle;
    WalkDir m_Dir = WalkDir::None;
    WalkDir m_Queue = WalkDir::None;
    // A turn that has reached a tile but has not moved yet. Releasing still glides
    // one tile that way, which is the buffered press. Moving clears it.
    bool m_Nudge = false;
    f32 m_X = 0.0f;
    f32 m_Y = 0.0f;
    f32 m_FromX = 0.0f;
    f32 m_FromY = 0.0f;
    f32 m_ToX = 0.0f;
    f32 m_ToY = 0.0f;
    f32 m_SettleElapsed = 0.0f;
    f32 m_SettleDuration = 0.0f;
};

// Drawn tile of each spawn. Snap stands on the tiles. Advance glides toward the
// current tile at one tile per `moveSeconds`, from wherever the picture already is.
class SpawnGlides {
public:
    void Snap(std::span<const SpawnPlace> places);
    void Advance(std::span<const SpawnPlace> places, f32 dt, f32 moveSeconds);
    [[nodiscard]] WalkPoint Sample(std::string_view id, MapTile fallback) const;

private:
    struct Entry {
        std::string Id;
        f32 X = 0.0f;
        f32 Y = 0.0f;
    };

    std::vector<Entry> m_Entries;
};

// `tilesWide` and `tilesHigh` are the view, in tiles. Zero or a negative size
// leaves the origin at 0. The hero point may sit between tiles.
[[nodiscard]] WalkFrame FrameMap(const MapDef& map, f32 heroX, f32 heroY, f32 tilesWide,
                                 f32 tilesHigh);
[[nodiscard]] WalkFrame FrameMap(const MapDef& map, MapTile hero, f32 tilesWide, f32 tilesHigh);

} // namespace Ascension
