#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/Database.h"

namespace Ascension {

// Folder next to the executable that is used when --art-pack is omitted.
// The name is the on-disk contract. It is not a content id.
inline constexpr std::string_view kArtPackDirectoryName = "artpack";

// The one JSON file in that folder. Same role as a file under data/.
inline constexpr std::string_view kArtPackManifestName = "manifest.json";

// How long one frame stays up when a strip omits frameDuration. Milliseconds.
inline constexpr i32 kArtFrameMilliseconds = 200;

// The cell variant is (x * kArtVariantX + y * kArtVariantY) modulo the count,
// in 32-bit unsigned arithmetic. x grows east and y grows south.
inline constexpr u32 kArtVariantX = 73856093u;
inline constexpr u32 kArtVariantY = 19349663u;

// Which picture a manifest row replaces. The id that goes with a content slot comes
// from data: a tile id, an enemy id, a class id, a dialogue id, or a prop id placed
// on a map. The marker slots are the overland drawings that are not content ids:
// the hero, a rest, a shop, and an exit. Nothing here names a particular tile, foe,
// class, NPC, map, or prop. A prop the manifest leaves out is not drawn: the
// built-in art has no prop pictures.
enum class ArtSlot : u8 {
    Tile,
    FoeOverland,
    FoeBattle,
    HeroBattle,
    NpcSprite,
    NpcPortrait,
    HeroMarker,
    RestMarker,
    ShopMarker,
    ExitMarker,
    Prop,
};

[[nodiscard]] inline bool IsArtMarker(ArtSlot slot)
{
    return slot == ArtSlot::HeroMarker || slot == ArtSlot::RestMarker ||
           slot == ArtSlot::ShopMarker || slot == ArtSlot::ExitMarker;
}

// One PNG. Frames sit in a single horizontal strip. FrameWidth is one frame.
// FrameDurationMs is 0 when the manifest omitted it; playback then uses
// kArtFrameMilliseconds. A one-frame picture uses the whole width.
struct ArtPicture {
    std::filesystem::path File;
    i32 Frames = 1;
    i32 FrameWidth = 0;
    i32 FrameHeight = 0;
    i32 FrameDurationMs = 0;
};

// One manifest row. Map or Theme is set only for a tile that is limited to that
// scope. Pictures holds the one file, or the variants of a tile, in list order.
// CellsWide and CellsHigh are the prop picture in cells. 0 means the row omitted
// them. A tile and a marker leave both at 0.
struct ArtImage {
    ArtSlot Slot = ArtSlot::Tile;
    std::string Id;
    std::string Map;
    std::string Theme;
    std::vector<ArtPicture> Pictures;
    i32 CellsWide = 0;
    i32 CellsHigh = 0;
};

// Where a picture comes from. Builtin is the CC0 art the game already draws.
// Pack is a file from the manifest. Picture is set only for Pack.
enum class ArtOrigin : u8 { Builtin, Pack };

struct ResolvedArt {
    ArtOrigin Origin = ArtOrigin::Builtin;
    ArtPicture Picture;
    // Prop picture size in cells. 0 when the row omitted it, and for every other slot.
    i32 CellsWide = 0;
    i32 CellsHigh = 0;
};

// Optional pictures that replace the built-in CC0 art. The manifest lists only the
// ids a private build wants to swap. Find returns Builtin for every id the manifest
// does not list, and for a pack that was never loaded.
//
// Load checks the file the way Database does: unknown keys, a bad id, a missing
// image, and a path that leaves the pack fail, and the message names the file and
// the field. JSON stays in the .cpp.
class ArtPack {
public:
    // Reads `directory` / manifest. `data` is the catalog the ids are checked against.
    // On failure the pack stays empty (every Find is Builtin) and GetLastError() is set.
    bool Load(const std::filesystem::path& directory, const Database& data,
              DataError* error = nullptr);

    [[nodiscard]] bool IsLoaded() const { return m_Loaded; }
    [[nodiscard]] const std::filesystem::path& GetDirectory() const { return m_Directory; }
    [[nodiscard]] const std::string& GetLastError() const { return m_LastError; }
    [[nodiscard]] std::span<const ArtImage> GetImages() const { return m_Images; }

    // Builtin when this slot and id are not in the manifest. A tile row that names a
    // map or a theme is not this result; FindPlaced is. An empty id matches a marker slot.
    [[nodiscard]] ResolvedArt Find(ArtSlot slot, std::string_view id) const;

    // Tile art for this map cell. A row that names the map wins over a row for the
    // map's theme, which wins over the row that names neither. The picture is the
    // variant for (x, y). Any other slot ignores the map and the cell.
    [[nodiscard]] ResolvedArt FindPlaced(ArtSlot slot, std::string_view id,
                                         std::string_view mapId, i32 x, i32 y) const;

private:
    struct ThemeUse {
        std::string Theme;
        std::string Map;
    };

    [[nodiscard]] const std::string* ThemeOf(std::string_view mapId) const;
    [[nodiscard]] const ArtImage* Select(ArtSlot slot, std::string_view id,
                                         std::string_view mapId) const;

    std::filesystem::path m_Directory;
    std::vector<ArtImage> m_Images;
    std::vector<ThemeUse> m_Themes;
    std::string m_LastError;
    bool m_Loaded = false;
};

// Which frame is showing. Frame 0 when the strip has one frame, or when `seconds` is
// not positive. An omitted duration (0) uses kArtFrameMilliseconds.
[[nodiscard]] i32 ArtFrameIndex(i32 frames, i32 frameDurationMs, f32 seconds);

// The variant for a cell. The same x, y, and count always return the same index.
[[nodiscard]] i32 ArtVariantIndex(i32 x, i32 y, i32 count);

// True when a prop footprint covers this cell. A cell in that prop's open list
// does not. The map's w and h win. When the map left a side unset, `art` supplies
// it. A null pack, or a prop the pack does not size, blocks nothing on that side.
[[nodiscard]] inline bool FootprintBlocks(const MapDef& map, i32 x, i32 y, const ArtPack* art)
{
    for (const MapProp& prop : map.Props) {
        i32 width = prop.Width;
        i32 height = prop.Height;
        if (art != nullptr && (width < 1 || height < 1)) {
            const ResolvedArt resolved = art->Find(ArtSlot::Prop, prop.Id);
            if (width < 1)
                width = resolved.CellsWide;
            if (height < 1)
                height = resolved.CellsHigh;
        }
        if (PropFootprintCovers(prop, width, height, x, y))
            return true;
    }
    return false;
}

// `requested` is the --art-pack path, or empty when the flag was omitted.
// A set path has to be a directory. An empty path uses `executableDirectory/artpack`
// when that directory exists, and otherwise stays empty: the launch keeps the
// built-in art. A missing default folder is not an error.
[[nodiscard]] bool DiscoverArtPack(std::filesystem::path& requested,
                                   const std::filesystem::path& executableDirectory,
                                   std::string& error);

} // namespace Ascension
