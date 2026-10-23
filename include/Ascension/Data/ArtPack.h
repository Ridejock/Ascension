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

// Which picture a manifest row replaces. The id that goes with the slot comes from
// data: a tile id, an enemy id, a class id, or a dialogue id. Nothing here names
// a particular tile, foe, class, or NPC.
enum class ArtSlot : u8 {
    Tile,
    FoeOverland,
    FoeBattle,
    HeroBattle,
    NpcSprite,
    NpcPortrait,
};

// One image the pack supplies. File is absolute and stays inside the pack directory.
struct ArtImage {
    ArtSlot Slot = ArtSlot::Tile;
    std::string Id;
    std::filesystem::path File;
};

// Where a picture comes from. Builtin is the CC0 art the game already draws.
// Pack is a file from the manifest. File is set only for Pack.
enum class ArtOrigin : u8 { Builtin, Pack };

struct ResolvedArt {
    ArtOrigin Origin = ArtOrigin::Builtin;
    std::filesystem::path File;
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

    // Builtin when this slot and id are not in the manifest. The caller then draws
    // the built-in CC0 art.
    [[nodiscard]] ResolvedArt Find(ArtSlot slot, std::string_view id) const;

private:
    std::filesystem::path m_Directory;
    std::vector<ArtImage> m_Images;
    std::string m_LastError;
    bool m_Loaded = false;
};

// `requested` is the --art-pack path, or empty when the flag was omitted.
// A set path has to be a directory. An empty path uses `executableDirectory/artpack`
// when that directory exists, and otherwise stays empty: the launch keeps the
// built-in art. A missing default folder is not an error.
[[nodiscard]] bool DiscoverArtPack(std::filesystem::path& requested,
                                   const std::filesystem::path& executableDirectory,
                                   std::string& error);

} // namespace Ascension
