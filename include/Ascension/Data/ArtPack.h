#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/Database.h"
#include "Emerald/IO/Pak.h"

namespace Ascension {

// Folder next to the executable that is used when --art-pack is omitted.
// The name is the on-disk contract. It is not a content id.
inline constexpr std::string_view kArtPackDirectoryName = "artpack";

// The packed file inside that folder, and the name --art-pack accepts.
inline constexpr std::string_view kArtPackFileName = "ascension.pak";

// The one JSON file in that folder or pak. Same role as a file under data/.
inline constexpr std::string_view kArtPackManifestName = "manifest.json";

// Shown on the title and the pause menu. A pack credit replaces the name.
inline constexpr std::string_view kArtCreditName = "Willosuna";

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

// One 9-slice. Border is the margin, in pixels, on every side. Width and Height
// are 0 when the manifest omitted size.
struct ArtNineSlice {
    std::string Id;
    std::filesystem::path File;
    i32 Border = 0;
    i32 Width = 0;
    i32 Height = 0;
};

// One font role. Size is the pixel size the manifest names.
struct ArtFontRole {
    std::string Role;
    std::filesystem::path File;
    i32 Size = 0;
};

// An icon, an item picture, or a VFX sheet. Frames is 1 when the sheet is one
// picture. A sheet wider than it is tall, with a width that divides by the
// height, is split into square frames.
struct ArtNamedFile {
    std::string Id;
    std::filesystem::path File;
    i32 Frames = 1;
    i32 FrameWidth = 0;
    i32 FrameHeight = 0;
};

// A battle backdrop. Layers are back to front. Flat is the single picture used
// when Layers is empty.
struct ArtBackdrop {
    std::string Id;
    std::filesystem::path Flat;
    std::vector<std::filesystem::path> Layers;
};

// One music or ambience role. File is the path inside the pack.
struct ArtAudioRole {
    std::string Role;
    std::filesystem::path File;
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
    [[nodiscard]] bool HasArchive() const { return static_cast<bool>(m_Pak); }
    [[nodiscard]] const std::filesystem::path& GetDirectory() const { return m_Directory; }
    [[nodiscard]] const std::string& GetLastError() const { return m_LastError; }
    [[nodiscard]] std::span<const ArtImage> GetImages() const { return m_Images; }
    [[nodiscard]] const std::string& Credit() const { return m_Credit; }
    [[nodiscard]] std::span<const ArtNineSlice> Slices() const { return m_Slices; }
    [[nodiscard]] const ArtNineSlice* FindSlice(std::string_view id) const;
    [[nodiscard]] std::span<const std::filesystem::path> UiFiles() const { return m_UiFiles; }
    [[nodiscard]] std::span<const ArtFontRole> Fonts() const { return m_Fonts; }
    [[nodiscard]] const ArtFontRole* FindFont(std::string_view role) const;
    [[nodiscard]] std::span<const ArtNamedFile> Icons() const { return m_Icons; }
    [[nodiscard]] const ArtNamedFile* FindIcon(std::string_view id) const;
    [[nodiscard]] std::span<const ArtNamedFile> Items() const { return m_Items; }
    [[nodiscard]] const ArtNamedFile* FindItem(std::string_view id) const;
    [[nodiscard]] std::span<const ArtNamedFile> Vfx() const { return m_Vfx; }
    [[nodiscard]] const ArtNamedFile* FindVfx(std::string_view id) const;
    [[nodiscard]] std::span<const ArtBackdrop> Backdrops() const { return m_Backdrops; }
    [[nodiscard]] const ArtBackdrop* FindBackdrop(std::string_view id) const;
    [[nodiscard]] std::span<const ArtAudioRole> AudioRoles() const { return m_Audio; }
    [[nodiscard]] const ArtAudioRole* FindAudio(std::string_view role) const;
    [[nodiscard]] std::span<const std::filesystem::path> AudioFiles() const { return m_AudioFiles; }
    // Copies one file the manifest already accepted. A pak path is relative.
    // A loose pack path is the absolute file. False names the failure in `error`.
    [[nodiscard]] bool ReadFile(const std::filesystem::path& file, std::vector<unsigned char>& out,
                                std::string& error) const;

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

    void Fail(DataError* error, std::string message);

    [[nodiscard]] const std::string* ThemeOf(std::string_view mapId) const;
    [[nodiscard]] const ArtImage* Select(ArtSlot slot, std::string_view id,
                                         std::string_view mapId) const;

    std::filesystem::path m_Directory;
    std::shared_ptr<Emerald::Pak> m_Pak;
    std::vector<ArtImage> m_Images;
    std::vector<ThemeUse> m_Themes;
    std::string m_Credit;
    std::vector<ArtNineSlice> m_Slices;
    std::vector<std::filesystem::path> m_UiFiles;
    std::vector<ArtFontRole> m_Fonts;
    std::vector<ArtNamedFile> m_Icons;
    std::vector<ArtNamedFile> m_Items;
    std::vector<ArtNamedFile> m_Vfx;
    std::vector<ArtBackdrop> m_Backdrops;
    std::vector<ArtAudioRole> m_Audio;
    std::vector<std::filesystem::path> m_AudioFiles;
    std::string m_LastError;
    bool m_Loaded = false;
};

// "Art and audio: Willosuna", or the pack's credit when it names one.
[[nodiscard]] inline std::string ArtCreditLine(const ArtPack* pack)
{
    std::string who(kArtCreditName);
    if (pack != nullptr && !pack->Credit().empty())
        who = pack->Credit();
    return "Art and audio: " + who;
}

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

// How many pack pictures the gallery may upload in the background each frame.
// Either number at 0 uploads nothing until a draw needs the picture. `order` is
// the section sequence. A section left out follows the ones that are listed.
struct GalleryBudget {
    i32 UploadsPerFrame = 8;
    i32 BudgetMilliseconds = 4;
    std::vector<std::string> Order;
};

// Reads `directory`/gallery.json. A missing file leaves `out` at the defaults.
// A bad file returns false and names the file and the field.
[[nodiscard]] bool LoadGalleryBudget(const std::filesystem::path& directory, GalleryBudget& out,
                                     std::string& error);

// `requested` is the --art-pack path, or empty when the flag was omitted.
// A set path may be an ascension.pak file, a directory that contains that file,
// or a loose directory whose manifest.json is read directly. An empty path uses
// `executableDirectory/artpack/ascension.pak` when that file exists, then a loose
// `artpack` directory beside the executable. Otherwise it stays empty: the launch
// keeps the built-in art. A missing default file is not an error.
[[nodiscard]] bool DiscoverArtPack(std::filesystem::path& requested,
                                   const std::filesystem::path& executableDirectory,
                                   std::string& error);

} // namespace Ascension
