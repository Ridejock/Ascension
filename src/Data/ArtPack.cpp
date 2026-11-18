#include "Ascension/Data/ArtPack.h"

#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>

#include <nlohmann/json.hpp>

#include "Emerald/IO/Pak.h"

namespace Ascension {

namespace {

using Json = nlohmann::json;

constexpr const char* kTilesKey = "tiles";
constexpr const char* kFoesKey = "foes";
constexpr const char* kHeroesKey = "heroes";
constexpr const char* kNpcsKey = "npcs";
constexpr const char* kThemesKey = "themes";
constexpr const char* kMarkersKey = "markers";
constexpr const char* kPropsKey = "props";

// The same span a map accepts. A prop picture is at most this many cells on a side.
constexpr i32 kMaxPropSpan = 256;

constexpr i32 kMaxFrames = 256;
constexpr i32 kMaxFrameWidth = 16384;
constexpr i32 kMaxFrameDurationMs = 60000;
constexpr i32 kMaxVariants = 64;

constexpr unsigned char kPngSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

// A loose directory, or an open pak. Picture paths in a pak stay relative.
struct ByteSource {
    std::filesystem::path Root;
    std::shared_ptr<Emerald::Pak> Pak;
};

// The markers the map already draws. These names are the manifest contract, the same
// kind of token as "tiles". They are not ids from data/.
struct MarkerRole {
    const char* Id;
    ArtSlot Slot;
};

constexpr MarkerRole kMarkerRoles[] = {
    {"hero", ArtSlot::HeroMarker},
    {"rest", ArtSlot::RestMarker},
    {"shop", ArtSlot::ShopMarker},
    {"exit", ArtSlot::ExitMarker},
};

struct ListedTheme {
    std::string Theme;
    std::string Map;
};

enum class Section : u8 { Tiles, Foes, Heroes, Npcs };

bool IsId(std::string_view id)
{
    if (id.empty() || !((id[0] >= 'a' && id[0] <= 'z') || (id[0] >= 'A' && id[0] <= 'Z')))
        return false;
    for (const char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_'))
            return false;
    }
    return true;
}

std::string Quote(std::string_view text)
{
    return "'" + std::string(text) + "'";
}

std::string ManifestError(std::string_view where, std::string_view message)
{
    std::string out(kArtPackManifestName);
    if (!where.empty()) {
        out += ": ";
        out += where;
    }
    if (!message.empty()) {
        out += ": ";
        out += message;
    }
    return out;
}

std::string EntryWhere(const char* kind, std::string_view id)
{
    return std::string(kind) + " " + Quote(id);
}

const char* SectionKey(Section section)
{
    if (section == Section::Foes)
        return kFoesKey;
    if (section == Section::Heroes)
        return kHeroesKey;
    if (section == Section::Npcs)
        return kNpcsKey;
    return kTilesKey;
}

const char* SectionKind(Section section)
{
    if (section == Section::Foes)
        return "foe";
    if (section == Section::Heroes)
        return "hero";
    if (section == Section::Npcs)
        return "npc";
    return "tile";
}

const MarkerRole* FindMarkerRole(std::string_view id)
{
    for (const MarkerRole& role : kMarkerRoles) {
        if (id == role.Id)
            return &role;
    }
    return nullptr;
}

bool ReadText(const std::filesystem::path& path, std::string& out, std::string& error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "could not be read";
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (!out.empty() && static_cast<unsigned char>(out[0]) == 0xEF) {
        if (out.size() >= 3 && static_cast<unsigned char>(out[1]) == 0xBB &&
            static_cast<unsigned char>(out[2]) == 0xBF)
            out.erase(0, 3);
    }
    return true;
}

bool ParseRoot(std::string_view text, Json& root, std::string& error)
{
    root = Json::parse(text.begin(), text.end(), nullptr, false);
    if (root.is_discarded()) {
        error = "not valid JSON";
        return false;
    }
    if (!root.is_object()) {
        error = "the file must be a JSON object";
        return false;
    }
    return true;
}

bool AllowKeys(const Json& object, std::initializer_list<const char*> allowed, std::string& unknown)
{
    for (auto it = object.begin(); it != object.end(); ++it) {
        bool known = false;
        for (const char* key : allowed) {
            if (it.key() == key)
                known = true;
        }
        if (!known) {
            unknown = it.key();
            return false;
        }
    }
    return true;
}

bool ReadString(const Json& object, const char* key, bool required, std::string& out,
                std::string& error)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        if (!required) {
            out.clear();
            return true;
        }
        error = std::string("'") + key + "' is required";
        return false;
    }
    if (!it->is_string()) {
        error = std::string("'") + key + "' must be a string";
        return false;
    }
    out = it->get<std::string>();
    if (out.empty()) {
        error = std::string("'") + key + "' must not be empty";
        return false;
    }
    return true;
}

bool ReadInt(const Json& value, i32 min, i32 max, i32& out, std::string& error)
{
    if (!value.is_number_integer()) {
        error = "must be an integer";
        return false;
    }
    const i64 number = value.get<i64>();
    if (number < min || number > max) {
        error = "must be between " + std::to_string(min) + " and " + std::to_string(max);
        return false;
    }
    out = static_cast<i32>(number);
    return true;
}

bool OptionalInt(const Json& object, const char* key, i32 min, i32 max, i32& out,
                 std::string& error)
{
    const auto it = object.find(key);
    if (it == object.end())
        return true;
    if (!ReadInt(*it, min, max, out, error)) {
        error = std::string("'") + key + "' " + error;
        return false;
    }
    return true;
}

bool IdExists(Section section, const Database& data, std::string_view id)
{
    if (section == Section::Tiles)
        return data.FindTile(id) != nullptr;
    if (section == Section::Foes)
        return data.FindEnemy(id) != nullptr;
    if (section == Section::Heroes)
        return data.FindClass(id) != nullptr;
    return data.FindDialogue(id) != nullptr;
}

bool SlotInSection(Section section, ArtSlot slot)
{
    if (section == Section::Tiles)
        return slot == ArtSlot::Tile;
    if (section == Section::Foes)
        return slot == ArtSlot::FoeOverland || slot == ArtSlot::FoeBattle;
    if (section == Section::Heroes)
        return slot == ArtSlot::HeroBattle;
    return slot == ArtSlot::NpcSprite || slot == ArtSlot::NpcPortrait;
}

bool AlreadyListed(const std::vector<ArtImage>& images, Section section, std::string_view id)
{
    for (const ArtImage& image : images) {
        if (image.Id == id && SlotInSection(section, image.Slot))
            return true;
    }
    return false;
}

bool TileScopeTaken(const std::vector<ArtImage>& images, std::string_view id,
                    std::string_view map, std::string_view theme)
{
    for (const ArtImage& image : images) {
        if (image.Slot != ArtSlot::Tile || image.Id != id)
            continue;
        if (image.Map == map && image.Theme == theme)
            return true;
    }
    return false;
}

bool PathEscapes(const std::filesystem::path& relative)
{
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
        relative.has_root_directory())
        return true;
    const std::filesystem::path normal = relative.lexically_normal();
    if (normal.empty() || normal.is_absolute() || normal.has_root_name() ||
        normal.has_root_directory())
        return true;
    for (const std::filesystem::path& part : normal) {
        if (part == "..")
            return true;
    }
    return false;
}

std::string LowerExtension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    for (char& code : extension) {
        if (code >= 'A' && code <= 'Z')
            code = static_cast<char>(code - 'A' + 'a');
    }
    return extension;
}

u32 ReadBe32(const unsigned char* bytes)
{
    return (static_cast<u32>(bytes[0]) << 24) | (static_cast<u32>(bytes[1]) << 16) |
           (static_cast<u32>(bytes[2]) << 8) | static_cast<u32>(bytes[3]);
}

bool PngSizeOf(const unsigned char* bytes, usize count, i32& width, i32& height)
{
    if (bytes == nullptr || count < 24)
        return false;
    for (int index = 0; index < 8; ++index) {
        if (bytes[index] != kPngSignature[index])
            return false;
    }
    if (bytes[12] != 'I' || bytes[13] != 'H' || bytes[14] != 'D' || bytes[15] != 'R')
        return false;
    const u32 wide = ReadBe32(bytes + 16);
    const u32 high = ReadBe32(bytes + 20);
    if (wide == 0 || high == 0 || wide > static_cast<u32>(kMaxFrameWidth) ||
        high > static_cast<u32>(kMaxFrameWidth))
        return false;
    width = static_cast<i32>(wide);
    height = static_cast<i32>(high);
    return true;
}

bool ReadSource(const ByteSource& source, const std::string& relative, std::vector<unsigned char>& out,
                std::string& error)
{
    if (source.Pak) {
        const std::optional<std::vector<unsigned char>> bytes = source.Pak->Read(relative, &error);
        if (!bytes) {
            if (error.empty())
                error = Quote(relative) + " does not exist";
            return false;
        }
        out = *bytes;
        error.clear();
        return true;
    }
    const std::filesystem::path full = source.Root / std::filesystem::path(relative);
    std::ifstream in(full, std::ios::binary);
    if (!in) {
        error = Quote(relative) + " does not exist";
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    error.clear();
    return true;
}

bool InsideDirectory(const std::filesystem::path& root, const std::filesystem::path& file)
{
    std::error_code error;
    const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, error);
    if (error || canonicalRoot.empty())
        return false;
    error.clear();
    const std::filesystem::path canonicalFile = std::filesystem::weakly_canonical(file, error);
    if (error || canonicalFile.empty())
        return false;
    const std::filesystem::path nested = canonicalFile.lexically_relative(canonicalRoot);
    if (nested.empty() || nested == ".")
        return false;
    return !PathEscapes(nested);
}

bool FitStrip(ArtPicture& picture, i32 width, i32 height, std::string& error)
{
    picture.FrameHeight = height;
    if (picture.FrameWidth == 0) {
        if (width % picture.Frames != 0) {
            error = "width is not divisible by 'frames'";
            return false;
        }
        picture.FrameWidth = width / picture.Frames;
        return true;
    }
    const i64 span = static_cast<i64>(picture.FrameWidth) * static_cast<i64>(picture.Frames);
    if (span > width) {
        error = "'frameWidth' does not fit in the image";
        return false;
    }
    return true;
}

// `text` is the path as written in the manifest. A loose pack stores an absolute
// png. A pak stores the relative path.
bool ResolvePng(const ByteSource& source, const std::string& text, ArtPicture& picture,
                std::string& error)
{
    if (text.find('\\') != std::string::npos) {
        error = Quote(text) + " must use '/' and stay inside the pack";
        return false;
    }
    const std::filesystem::path relative(text);
    if (PathEscapes(relative) || LowerExtension(relative) != ".png") {
        error = Quote(text) + " must be a .png file inside the pack";
        return false;
    }
    const std::string key = relative.lexically_normal().generic_string();
    std::vector<unsigned char> bytes;
    if (source.Pak) {
        if (!ReadSource(source, key, bytes, error))
            return false;
    } else {
        const std::filesystem::path full = source.Root / relative.lexically_normal();
        std::error_code missing;
        if (!std::filesystem::is_regular_file(full, missing) || missing) {
            error = Quote(text) + " does not exist";
            return false;
        }
        if (!InsideDirectory(source.Root, full)) {
            error = Quote(text) + " must stay inside the pack";
            return false;
        }
        if (!ReadSource(source, key, bytes, error)) {
            error = Quote(text) + " does not exist";
            return false;
        }
    }
    i32 width = 0;
    i32 height = 0;
    if (!PngSizeOf(bytes.data(), bytes.size(), width, height)) {
        error = Quote(text) + " is not a png";
        return false;
    }
    if (!FitStrip(picture, width, height, error))
        return false;
    error.clear();
    if (source.Pak) {
        picture.File = key;
        return true;
    }
    const std::filesystem::path full = source.Root / relative.lexically_normal();
    std::error_code canonicalError;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(full, canonicalError);
    picture.File = canonicalError ? full : canonical;
    return true;
}

bool ReadPicture(const Json& value, const ByteSource& source, ArtPicture& picture,
                 std::string& error)
{
    picture = {};
    picture.Frames = 1;
    std::string text;
    if (value.is_string()) {
        text = value.get<std::string>();
        if (text.empty()) {
            error = "must not be empty";
            return false;
        }
        return ResolvePng(source, text, picture, error);
    }
    if (!value.is_object()) {
        error = "must be a string or an object";
        return false;
    }
    std::string unknown;
    if (!AllowKeys(value, {"file", "frames", "frameWidth", "frameDuration"}, unknown)) {
        error = "unknown field " + Quote(unknown);
        return false;
    }
    if (!ReadString(value, "file", true, text, error))
        return false;
    if (!OptionalInt(value, "frames", 1, kMaxFrames, picture.Frames, error) ||
        !OptionalInt(value, "frameWidth", 1, kMaxFrameWidth, picture.FrameWidth, error) ||
        !OptionalInt(value, "frameDuration", 1, kMaxFrameDurationMs, picture.FrameDurationMs,
                     error))
        return false;
    return ResolvePng(source, text, picture, error);
}

void AddImage(std::vector<ArtImage>& images, ArtSlot slot, const std::string& id,
              const std::string& map, const std::string& theme, std::vector<ArtPicture> pictures,
              i32 cellsWide = 0, i32 cellsHigh = 0)
{
    ArtImage image;
    image.Slot = slot;
    image.Id = id;
    image.Map = map;
    image.Theme = theme;
    image.Pictures = std::move(pictures);
    image.CellsWide = cellsWide;
    image.CellsHigh = cellsHigh;
    images.push_back(std::move(image));
}

bool AddOnePicture(std::vector<ArtImage>& images, ArtSlot slot, const std::string& id,
                   const Json& value, const ByteSource& root, std::string& error)
{
    ArtPicture picture;
    if (!ReadPicture(value, root, picture, error))
        return false;
    std::vector<ArtPicture> pictures;
    pictures.push_back(std::move(picture));
    AddImage(images, slot, id, {}, {}, std::move(pictures));
    return true;
}

bool LoadThemes(const Json& root, const Database& data, std::vector<ListedTheme>& themes,
                std::string& error)
{
    const auto list = root.find(kThemesKey);
    if (list == root.end())
        return true;
    if (!list->is_array()) {
        error = ManifestError({}, "'themes' must be a list");
        return false;
    }
    for (const Json& entry : *list) {
        if (!entry.is_object()) {
            error = ManifestError(kThemesKey, "entries must be objects");
            return false;
        }
        std::string unknown;
        if (!AllowKeys(entry, {"id", "maps"}, unknown)) {
            error = ManifestError(kThemesKey, "unknown field " + Quote(unknown));
            return false;
        }
        std::string id;
        std::string why;
        if (!ReadString(entry, "id", true, id, why)) {
            error = ManifestError(kThemesKey, why);
            return false;
        }
        const std::string where = EntryWhere("theme", id);
        if (!IsId(id)) {
            error = ManifestError(where, "id must be letters, digits and _, starting with a letter");
            return false;
        }
        for (const ListedTheme& held : themes) {
            if (held.Theme == id) {
                error = ManifestError(where, "there is another theme with this id");
                return false;
            }
        }
        const auto maps = entry.find("maps");
        if (maps == entry.end() || !maps->is_array()) {
            error = ManifestError(where, "'maps' must be a list");
            return false;
        }
        if (maps->empty()) {
            error = ManifestError(where, "'maps' must name at least one map");
            return false;
        }
        for (const Json& mapValue : *maps) {
            if (!mapValue.is_string()) {
                error = ManifestError(where, "'maps' entries must be strings");
                return false;
            }
            const std::string mapId = mapValue.get<std::string>();
            if (!IsId(mapId) || data.FindMap(mapId) == nullptr) {
                error = ManifestError(where, "map " + Quote(mapId) + " does not exist");
                return false;
            }
            for (const ListedTheme& held : themes) {
                if (held.Map == mapId) {
                    error = ManifestError(where, "map " + Quote(mapId) + " is already in a theme");
                    return false;
                }
            }
            ListedTheme use;
            use.Theme = id;
            use.Map = mapId;
            themes.push_back(std::move(use));
        }
    }
    return true;
}

bool ThemeDefined(const std::vector<ListedTheme>& themes, std::string_view id)
{
    for (const ListedTheme& use : themes)
        if (use.Theme == id)
            return true;
    return false;
}

bool LoadTile(const Json& entry, const ByteSource& root, const Database& data,
              const std::vector<ListedTheme>& themes, std::vector<ArtImage>& images,
              std::string& error)
{
    std::string unknown;
    if (!AllowKeys(entry, {"id", "file", "variants", "map", "theme"}, unknown)) {
        error = ManifestError(kTilesKey, "unknown field " + Quote(unknown));
        return false;
    }
    std::string id;
    std::string why;
    if (!ReadString(entry, "id", true, id, why)) {
        error = ManifestError(kTilesKey, why);
        return false;
    }
    const std::string where = EntryWhere("tile", id);
    if (!IsId(id)) {
        error = ManifestError(where, "id must be letters, digits and _, starting with a letter");
        return false;
    }
    if (!IdExists(Section::Tiles, data, id)) {
        error = ManifestError(where, "id does not exist");
        return false;
    }
    std::string map;
    std::string theme;
    if (!ReadString(entry, "map", false, map, why) || !ReadString(entry, "theme", false, theme, why)) {
        error = ManifestError(where, why);
        return false;
    }
    if (!map.empty() && !theme.empty()) {
        error = ManifestError(where, "name 'map' or 'theme', not both");
        return false;
    }
    if (!map.empty() && (!IsId(map) || data.FindMap(map) == nullptr)) {
        error = ManifestError(where, "map " + Quote(map) + " does not exist");
        return false;
    }
    if (!theme.empty() && (!IsId(theme) || !ThemeDefined(themes, theme))) {
        error = ManifestError(where, "theme " + Quote(theme) + " does not exist");
        return false;
    }
    if (TileScopeTaken(images, id, map, theme)) {
        if (!map.empty())
            error = ManifestError(where, "there is another tile with this id on this map");
        else if (!theme.empty())
            error = ManifestError(where, "there is another tile with this id for this theme");
        else
            error = ManifestError(where, "there is another tile with this id");
        return false;
    }

    const bool hasFile = entry.find("file") != entry.end();
    const auto variants = entry.find("variants");
    const bool hasVariants = variants != entry.end();
    if (hasFile == hasVariants) {
        error = ManifestError(where, "name 'file' or 'variants'");
        return false;
    }
    std::vector<ArtPicture> pictures;
    if (hasFile) {
        const auto file = entry.find("file");
        ArtPicture picture;
        if (!ReadPicture(*file, root, picture, why)) {
            error = ManifestError(where, why);
            return false;
        }
        pictures.push_back(std::move(picture));
    } else {
        if (!variants->is_array()) {
            error = ManifestError(where, "'variants' must be a list");
            return false;
        }
        if (variants->empty() || variants->size() > static_cast<usize>(kMaxVariants)) {
            error = ManifestError(where, "'variants' must name between 1 and " +
                                              std::to_string(kMaxVariants) + " images");
            return false;
        }
        for (const Json& variant : *variants) {
            ArtPicture picture;
            if (!ReadPicture(variant, root, picture, why)) {
                error = ManifestError(where, why);
                return false;
            }
            pictures.push_back(std::move(picture));
        }
    }
    AddImage(images, ArtSlot::Tile, id, map, theme, std::move(pictures));
    return true;
}

bool LoadFoeOrNpc(Section section, const Json& entry, const ByteSource& root,
                  const Database& data, std::vector<ArtImage>& images, std::string& error)
{
    const char* key = SectionKey(section);
    const char* kind = SectionKind(section);
    const bool foe = section == Section::Foes;
    std::string unknown;
    const bool keysOk = foe ? AllowKeys(entry, {"id", "overland", "battle"}, unknown)
                            : AllowKeys(entry, {"id", "sprite", "portrait"}, unknown);
    if (!keysOk) {
        error = ManifestError(key, "unknown field " + Quote(unknown));
        return false;
    }
    std::string id;
    std::string why;
    if (!ReadString(entry, "id", true, id, why)) {
        error = ManifestError(key, why);
        return false;
    }
    const std::string where = EntryWhere(kind, id);
    if (!IsId(id)) {
        error = ManifestError(where, "id must be letters, digits and _, starting with a letter");
        return false;
    }
    if (AlreadyListed(images, section, id)) {
        error = ManifestError(where, "there is another " + std::string(kind) + " with this id");
        return false;
    }
    if (!IdExists(section, data, id)) {
        error = ManifestError(where, "id does not exist");
        return false;
    }
    const char* firstKey = foe ? "overland" : "sprite";
    const char* secondKey = foe ? "battle" : "portrait";
    const ArtSlot firstSlot = foe ? ArtSlot::FoeOverland : ArtSlot::NpcSprite;
    const ArtSlot secondSlot = foe ? ArtSlot::FoeBattle : ArtSlot::NpcPortrait;
    const auto first = entry.find(firstKey);
    const auto second = entry.find(secondKey);
    if (first == entry.end() && second == entry.end()) {
        error = ManifestError(where, std::string("name an '") + firstKey + "' image, a '" +
                                         secondKey + "' image, or both");
        return false;
    }
    if (first != entry.end() && !AddOnePicture(images, firstSlot, id, *first, root, why)) {
        error = ManifestError(where, why);
        return false;
    }
    if (second != entry.end() && !AddOnePicture(images, secondSlot, id, *second, root, why)) {
        error = ManifestError(where, why);
        return false;
    }
    return true;
}

bool LoadHero(const Json& entry, const ByteSource& root, const Database& data,
              std::vector<ArtImage>& images, std::string& error)
{
    std::string unknown;
    if (!AllowKeys(entry, {"id", "battle"}, unknown)) {
        error = ManifestError(kHeroesKey, "unknown field " + Quote(unknown));
        return false;
    }
    std::string id;
    std::string why;
    if (!ReadString(entry, "id", true, id, why)) {
        error = ManifestError(kHeroesKey, why);
        return false;
    }
    const std::string where = EntryWhere("hero", id);
    if (!IsId(id)) {
        error = ManifestError(where, "id must be letters, digits and _, starting with a letter");
        return false;
    }
    if (AlreadyListed(images, Section::Heroes, id)) {
        error = ManifestError(where, "there is another hero with this id");
        return false;
    }
    if (!IdExists(Section::Heroes, data, id)) {
        error = ManifestError(where, "id does not exist");
        return false;
    }
    const auto battle = entry.find("battle");
    if (battle == entry.end()) {
        error = ManifestError(where, "'battle' is required");
        return false;
    }
    if (!AddOnePicture(images, ArtSlot::HeroBattle, id, *battle, root, why)) {
        error = ManifestError(where, why);
        return false;
    }
    return true;
}

bool LoadMarker(const Json& entry, const ByteSource& root, std::vector<ArtImage>& images,
                std::string& error)
{
    if (!entry.is_object()) {
        error = ManifestError(kMarkersKey, "entries must be objects");
        return false;
    }
    std::string unknown;
    if (!AllowKeys(entry, {"id", "file"}, unknown)) {
        error = ManifestError(kMarkersKey, "unknown field " + Quote(unknown));
        return false;
    }
    std::string id;
    std::string why;
    if (!ReadString(entry, "id", true, id, why)) {
        error = ManifestError(kMarkersKey, why);
        return false;
    }
    const std::string where = EntryWhere("marker", id);
    const MarkerRole* role = FindMarkerRole(id);
    if (role == nullptr) {
        error = ManifestError(where, "id does not exist");
        return false;
    }
    for (const ArtImage& image : images) {
        if (image.Slot == role->Slot) {
            error = ManifestError(where, "there is another marker with this id");
            return false;
        }
    }
    const auto file = entry.find("file");
    if (file == entry.end()) {
        error = ManifestError(where, "'file' is required");
        return false;
    }
    if (!AddOnePicture(images, role->Slot, id, *file, root, why)) {
        error = ManifestError(where, why);
        return false;
    }
    return true;
}

bool PropPlaced(const Database& data, std::string_view id)
{
    for (const MapDef& map : data.GetMaps()) {
        for (const MapProp& prop : map.Props) {
            if (prop.Id == id)
                return true;
        }
    }
    return false;
}

bool LoadProp(const Json& entry, const ByteSource& root, const Database& data,
              std::vector<ArtImage>& images, std::string& error)
{
    if (!entry.is_object()) {
        error = ManifestError(kPropsKey, "entries must be objects");
        return false;
    }
    std::string unknown;
    if (!AllowKeys(entry, {"id", "file", "w", "h"}, unknown)) {
        error = ManifestError(kPropsKey, "unknown field " + Quote(unknown));
        return false;
    }
    std::string id;
    std::string why;
    if (!ReadString(entry, "id", true, id, why)) {
        error = ManifestError(kPropsKey, why);
        return false;
    }
    const std::string where = EntryWhere("prop", id);
    if (!IsId(id)) {
        error = ManifestError(where, "id must be letters, digits and _, starting with a letter");
        return false;
    }
    for (const ArtImage& image : images) {
        if (image.Slot == ArtSlot::Prop && image.Id == id) {
            error = ManifestError(where, "there is another prop with this id");
            return false;
        }
    }
    if (!PropPlaced(data, id)) {
        error = ManifestError(where, "id does not exist");
        return false;
    }
    const bool hasWidth = entry.contains("w");
    const bool hasHeight = entry.contains("h");
    if (hasWidth != hasHeight) {
        error = ManifestError(where, "name 'w' and 'h', or neither");
        return false;
    }
    i32 cellsWide = 0;
    i32 cellsHigh = 0;
    if (hasWidth && (!OptionalInt(entry, "w", 1, kMaxPropSpan, cellsWide, why) ||
                     !OptionalInt(entry, "h", 1, kMaxPropSpan, cellsHigh, why))) {
        error = ManifestError(where, why);
        return false;
    }
    const auto file = entry.find("file");
    if (file == entry.end()) {
        error = ManifestError(where, "'file' is required");
        return false;
    }
    ArtPicture picture;
    if (!ReadPicture(*file, root, picture, why)) {
        error = ManifestError(where, why);
        return false;
    }
    std::vector<ArtPicture> pictures;
    pictures.push_back(std::move(picture));
    AddImage(images, ArtSlot::Prop, id, {}, {}, std::move(pictures), cellsWide, cellsHigh);
    return true;
}

bool LoadProps(const Json& root, const ByteSource& directory, const Database& data,
               std::vector<ArtImage>& images, std::string& error)
{
    const auto list = root.find(kPropsKey);
    if (list == root.end())
        return true;
    if (!list->is_array()) {
        error = ManifestError({}, "'props' must be a list");
        return false;
    }
    for (const Json& entry : *list) {
        if (!LoadProp(entry, directory, data, images, error))
            return false;
    }
    return true;
}

bool LoadMarkers(const Json& root, const ByteSource& directory,
                 std::vector<ArtImage>& images, std::string& error)
{
    const auto list = root.find(kMarkersKey);
    if (list == root.end())
        return true;
    if (!list->is_array()) {
        error = ManifestError({}, "'markers' must be a list");
        return false;
    }
    for (const Json& entry : *list) {
        if (!LoadMarker(entry, directory, images, error))
            return false;
    }
    return true;
}

bool LoadSection(Section section, const Json& root, const ByteSource& directory,
                 const Database& data, const std::vector<ListedTheme>& themes,
                 std::vector<ArtImage>& images, std::string& error)
{
    const char* key = SectionKey(section);
    const auto list = root.find(key);
    if (list == root.end())
        return true;
    if (!list->is_array()) {
        error = ManifestError({}, std::string("'") + key + "' must be a list");
        return false;
    }
    for (const Json& entry : *list) {
        if (!entry.is_object()) {
            error = ManifestError(key, "entries must be objects");
            return false;
        }
        bool loaded = false;
        if (section == Section::Tiles)
            loaded = LoadTile(entry, directory, data, themes, images, error);
        else if (section == Section::Heroes)
            loaded = LoadHero(entry, directory, data, images, error);
        else
            loaded = LoadFoeOrNpc(section, entry, directory, data, images, error);
        if (!loaded)
            return false;
    }
    return true;
}

constexpr const char* kFontRoles[] = {"ui", "body", "damage", "fallback"};
constexpr const char* kAudioRoles[] = {
    "title",
    "hub",
    "overland",
    "barrow",
    "battle",
    "victory",
    "ambience_hub",
    "ambience_overland",
    "ambience_barrow",
    "ambience_hub_intense",
    "ambience_overland_intense",
    "ambience_barrow_intense",
    "ambience_campfire",
};

struct ExtraArt {
    std::string Credit;
    std::vector<ArtNineSlice> Slices;
    std::vector<std::filesystem::path> UiFiles;
    std::vector<ArtFontRole> Fonts;
    std::vector<ArtNamedFile> Icons;
    std::vector<ArtNamedFile> Items;
    std::vector<ArtNamedFile> Vfx;
    std::vector<ArtBackdrop> Backdrops;
    std::vector<ArtAudioRole> Audio;
    std::vector<std::filesystem::path> AudioFiles;
};

bool KnownRole(const char* const* roles, usize count, std::string_view id)
{
    for (usize index = 0; index < count; ++index) {
        if (id == roles[index])
            return true;
    }
    return false;
}

bool IsPackId(std::string_view id)
{
    if (id.empty() || id.front() == '/' || id.back() == '/')
        return false;
    bool segment = true;
    for (usize index = 0; index < id.size(); ++index) {
        const char c = id[index];
        if (c == '/') {
            if (segment)
                return false;
            segment = true;
            continue;
        }
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_';
        if (!ok)
            return false;
        if (segment && c >= '0' && c <= '9')
            return false;
        segment = false;
    }
    return !segment;
}

bool ResolveStored(const ByteSource& source, const std::string& text,
                   std::initializer_list<const char*> extensions, std::filesystem::path& stored,
                   std::string& error)
{
    if (text.find('\\') != std::string::npos) {
        error = Quote(text) + " must use '/' and stay inside the pack";
        return false;
    }
    const std::filesystem::path relative(text);
    if (PathEscapes(relative)) {
        error = Quote(text) + " must stay inside the pack";
        return false;
    }
    const std::string extension = LowerExtension(relative);
    bool allowed = extensions.size() == 0;
    for (const char* one : extensions) {
        if (extension == one)
            allowed = true;
    }
    if (!allowed) {
        error = Quote(text) + " has the wrong type";
        return false;
    }
    const std::string key = relative.lexically_normal().generic_string();
    std::vector<unsigned char> bytes;
    if (source.Pak) {
        if (!ReadSource(source, key, bytes, error))
            return false;
    } else {
        const std::filesystem::path full = source.Root / relative.lexically_normal();
        std::error_code missing;
        if (!std::filesystem::is_regular_file(full, missing) || missing ||
            !InsideDirectory(source.Root, full)) {
            error = Quote(text) + " does not exist";
            return false;
        }
        if (!ReadSource(source, key, bytes, error)) {
            error = Quote(text) + " does not exist";
            return false;
        }
    }
    if (extension == ".png") {
        i32 width = 0;
        i32 height = 0;
        if (!PngSizeOf(bytes.data(), bytes.size(), width, height)) {
            error = Quote(text) + " is not a png";
            return false;
        }
    }
    if (source.Pak) {
        stored = key;
        return true;
    }
    const std::filesystem::path full = source.Root / relative.lexically_normal();
    std::error_code canonicalError;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(full, canonicalError);
    stored = canonicalError ? full : canonical;
    error.clear();
    return true;
}

void NoteSheet(ArtNamedFile& file, const ArtPicture& picture)
{
    file.File = picture.File;
    file.Frames = picture.Frames;
    file.FrameWidth = picture.FrameWidth;
    file.FrameHeight = picture.FrameHeight;
    if (file.Frames == 1 && file.FrameHeight > 0 && file.FrameWidth > file.FrameHeight &&
        file.FrameWidth % file.FrameHeight == 0) {
        file.Frames = file.FrameWidth / file.FrameHeight;
        file.FrameWidth = file.FrameHeight;
    }
}

bool LoadNamedList(const Json& root, const char* key, const char* kind, const ByteSource& source,
                   std::vector<ArtNamedFile>& out, std::string& error)
{
    const auto list = root.find(key);
    if (list == root.end())
        return true;
    if (!list->is_array()) {
        error = ManifestError({}, std::string("'") + key + "' must be a list");
        return false;
    }
    for (const Json& entry : *list) {
        if (!entry.is_object()) {
            error = ManifestError(key, "entries must be objects");
            return false;
        }
        std::string unknown;
        if (!AllowKeys(entry, {"id", "file"}, unknown)) {
            error = ManifestError(key, "unknown field " + Quote(unknown));
            return false;
        }
        std::string id;
        std::string why;
        if (!ReadString(entry, "id", true, id, why)) {
            error = ManifestError(key, why);
            return false;
        }
        const std::string where = EntryWhere(kind, id);
        if (!IsPackId(id)) {
            error = ManifestError(where, "id must be letters, digits, _, and /, starting with a letter");
            return false;
        }
        for (const ArtNamedFile& held : out) {
            if (held.Id == id) {
                error = ManifestError(where, "there is another entry with this id");
                return false;
            }
        }
        const auto file = entry.find("file");
        if (file == entry.end()) {
            error = ManifestError(where, "'file' is required");
            return false;
        }
        ArtPicture picture;
        if (!ReadPicture(*file, source, picture, why)) {
            error = ManifestError(where, why);
            return false;
        }
        ArtNamedFile named;
        named.Id = std::move(id);
        NoteSheet(named, picture);
        out.push_back(std::move(named));
    }
    return true;
}

bool LoadCredit(const Json& root, std::string& credit, std::string& error)
{
    const auto it = root.find("credit");
    if (it == root.end())
        return true;
    if (!it->is_string() || it->get<std::string>().empty()) {
        error = ManifestError({}, "'credit' must be a string");
        return false;
    }
    credit = it->get<std::string>();
    return true;
}

bool LoadUi(const Json& root, const ByteSource& source, ExtraArt& extra, std::string& error)
{
    const auto ui = root.find("ui");
    if (ui == root.end())
        return true;
    if (!ui->is_object()) {
        error = ManifestError({}, "'ui' must be an object");
        return false;
    }
    std::string unknown;
    if (!AllowKeys(*ui, {"nineslice", "files"}, unknown)) {
        error = ManifestError("ui", "unknown field " + Quote(unknown));
        return false;
    }
    const auto slices = ui->find("nineslice");
    if (slices != ui->end()) {
        if (!slices->is_object()) {
            error = ManifestError("ui", "'nineslice' must be an object");
            return false;
        }
        for (auto it = slices->begin(); it != slices->end(); ++it) {
            const std::string where = EntryWhere("slice", it.key());
            if (!IsId(it.key()) || !it->is_object()) {
                error = ManifestError(where, "must be an object");
                return false;
            }
            if (!AllowKeys(*it, {"file", "border_px", "size"}, unknown)) {
                error = ManifestError(where, "unknown field " + Quote(unknown));
                return false;
            }
            std::string file;
            std::string why;
            if (!ReadString(*it, "file", true, file, why)) {
                error = ManifestError(where, why);
                return false;
            }
            const auto border = it->find("border_px");
            if (border == it->end()) {
                error = ManifestError(where, "'border_px' is required");
                return false;
            }
            i32 margin = 0;
            if (!ReadInt(*border, 0, 4096, margin, why)) {
                error = ManifestError(where, "'border_px' " + why);
                return false;
            }
            i32 wide = 0;
            i32 high = 0;
            const auto size = it->find("size");
            if (size != it->end()) {
                if (!size->is_array() || size->size() != 2) {
                    error = ManifestError(where, "'size' must be two integers");
                    return false;
                }
                if (!ReadInt((*size)[0], 1, kMaxFrameWidth, wide, why) ||
                    !ReadInt((*size)[1], 1, kMaxFrameWidth, high, why)) {
                    error = ManifestError(where, "'size' " + why);
                    return false;
                }
            }
            ArtPicture picture;
            if (!ResolvePng(source, file, picture, why)) {
                error = ManifestError(where, why);
                return false;
            }
            if (wide > 0 && (picture.FrameWidth != wide || picture.FrameHeight != high)) {
                error = ManifestError(where, "'size' does not match the image");
                return false;
            }
            ArtNineSlice slice;
            slice.Id = it.key();
            slice.File = picture.File;
            slice.Border = margin;
            slice.Width = wide;
            slice.Height = high;
            extra.Slices.push_back(std::move(slice));
        }
    }
    const auto files = ui->find("files");
    if (files != ui->end()) {
        if (!files->is_array()) {
            error = ManifestError("ui", "'files' must be a list");
            return false;
        }
        for (const Json& entry : *files) {
            if (!entry.is_string()) {
                error = ManifestError("ui", "'files' entries must be strings");
                return false;
            }
            std::filesystem::path stored;
            std::string why;
            const std::string text = entry.get<std::string>();
            const std::string extension = LowerExtension(std::filesystem::path(text));
            if (extension == ".png") {
                ArtPicture picture;
                if (!ResolvePng(source, text, picture, why)) {
                    error = ManifestError("ui", why);
                    return false;
                }
                stored = picture.File;
            } else if (!ResolveStored(source, text, {}, stored, why)) {
                error = ManifestError("ui", why);
                return false;
            }
            extra.UiFiles.push_back(std::move(stored));
        }
    }
    return true;
}

bool LoadFonts(const Json& root, const ByteSource& source, ExtraArt& extra, std::string& error)
{
    const auto fonts = root.find("fonts");
    if (fonts == root.end())
        return true;
    if (!fonts->is_object()) {
        error = ManifestError({}, "'fonts' must be an object");
        return false;
    }
    std::string unknown;
    if (!AllowKeys(*fonts, {"roles", "files"}, unknown)) {
        error = ManifestError("fonts", "unknown field " + Quote(unknown));
        return false;
    }
    const auto roles = fonts->find("roles");
    if (roles != fonts->end()) {
        if (!roles->is_object()) {
            error = ManifestError("fonts", "'roles' must be an object");
            return false;
        }
        for (auto it = roles->begin(); it != roles->end(); ++it) {
            const std::string where = EntryWhere("font", it.key());
            if (!KnownRole(kFontRoles, 4, it.key())) {
                error = ManifestError(where, "unknown field " + Quote(it.key()));
                return false;
            }
            if (!it->is_object()) {
                error = ManifestError(where, "must be an object");
                return false;
            }
            if (!AllowKeys(*it, {"file", "size"}, unknown)) {
                error = ManifestError(where, "unknown field " + Quote(unknown));
                return false;
            }
            std::string file;
            std::string why;
            if (!ReadString(*it, "file", true, file, why)) {
                error = ManifestError(where, why);
                return false;
            }
            i32 size = 0;
            const auto sizeValue = it->find("size");
            if (sizeValue == it->end() || !ReadInt(*sizeValue, 1, 128, size, why)) {
                error = ManifestError(where, sizeValue == it->end() ? "'size' is required"
                                                                    : "'size' " + why);
                return false;
            }
            std::filesystem::path stored;
            if (!ResolveStored(source, file, {".ttf", ".otf"}, stored, why)) {
                error = ManifestError(where, why);
                return false;
            }
            ArtFontRole role;
            role.Role = it.key();
            role.File = std::move(stored);
            role.Size = size;
            extra.Fonts.push_back(std::move(role));
        }
    }
    const auto files = fonts->find("files");
    if (files != fonts->end()) {
        if (!files->is_array()) {
            error = ManifestError("fonts", "'files' must be a list");
            return false;
        }
        for (const Json& entry : *files) {
            if (!entry.is_string()) {
                error = ManifestError("fonts", "'files' entries must be strings");
                return false;
            }
            std::filesystem::path stored;
            std::string why;
            if (!ResolveStored(source, entry.get<std::string>(), {".ttf", ".otf"}, stored, why)) {
                error = ManifestError("fonts", why);
                return false;
            }
        }
    }
    return true;
}

bool LoadBackdrops(const Json& root, const ByteSource& source, ExtraArt& extra, std::string& error)
{
    const auto list = root.find("backdrops");
    if (list == root.end())
        return true;
    if (!list->is_array()) {
        error = ManifestError({}, "'backdrops' must be a list");
        return false;
    }
    for (const Json& entry : *list) {
        if (!entry.is_object()) {
            error = ManifestError("backdrops", "entries must be objects");
            return false;
        }
        std::string unknown;
        if (!AllowKeys(entry, {"id", "flat", "layers"}, unknown)) {
            error = ManifestError("backdrops", "unknown field " + Quote(unknown));
            return false;
        }
        std::string id;
        std::string why;
        if (!ReadString(entry, "id", true, id, why)) {
            error = ManifestError("backdrops", why);
            return false;
        }
        const std::string where = EntryWhere("backdrop", id);
        if (!IsPackId(id)) {
            error = ManifestError(where, "id must be letters, digits and _, starting with a letter");
            return false;
        }
        for (const ArtBackdrop& held : extra.Backdrops) {
            if (held.Id == id) {
                error = ManifestError(where, "there is another backdrop with this id");
                return false;
            }
        }
        ArtBackdrop backdrop;
        backdrop.Id = id;
        const auto flat = entry.find("flat");
        if (flat != entry.end()) {
            if (!flat->is_string()) {
                error = ManifestError(where, "'flat' must be a string");
                return false;
            }
            ArtPicture picture;
            if (!ResolvePng(source, flat->get<std::string>(), picture, why)) {
                error = ManifestError(where, why);
                return false;
            }
            backdrop.Flat = picture.File;
        }
        const auto layers = entry.find("layers");
        if (layers != entry.end()) {
            if (!layers->is_array()) {
                error = ManifestError(where, "'layers' must be a list");
                return false;
            }
            for (const Json& layer : *layers) {
                if (!layer.is_string()) {
                    error = ManifestError(where, "'layers' entries must be strings");
                    return false;
                }
                ArtPicture picture;
                if (!ResolvePng(source, layer.get<std::string>(), picture, why)) {
                    error = ManifestError(where, why);
                    return false;
                }
                backdrop.Layers.push_back(picture.File);
            }
        }
        if (backdrop.Flat.empty() && backdrop.Layers.empty()) {
            error = ManifestError(where, "'flat' or 'layers' is required");
            return false;
        }
        extra.Backdrops.push_back(std::move(backdrop));
    }
    return true;
}

bool LoadAudio(const Json& root, const ByteSource& source, ExtraArt& extra, std::string& error)
{
    const auto audio = root.find("audio");
    if (audio == root.end())
        return true;
    if (!audio->is_object()) {
        error = ManifestError({}, "'audio' must be an object");
        return false;
    }
    std::string unknown;
    if (!AllowKeys(*audio, {"roles", "files"}, unknown)) {
        error = ManifestError("audio", "unknown field " + Quote(unknown));
        return false;
    }
    const auto roles = audio->find("roles");
    if (roles != audio->end()) {
        if (!roles->is_object()) {
            error = ManifestError("audio", "'roles' must be an object");
            return false;
        }
        for (auto it = roles->begin(); it != roles->end(); ++it) {
            const std::string where = EntryWhere("audio", it.key());
            if (!KnownRole(kAudioRoles, sizeof(kAudioRoles) / sizeof(kAudioRoles[0]), it.key())) {
                error = ManifestError(where, "unknown field " + Quote(it.key()));
                return false;
            }
            if (!it->is_string()) {
                error = ManifestError(where, "must be a string");
                return false;
            }
            std::filesystem::path stored;
            std::string why;
            if (!ResolveStored(source, it->get<std::string>(), {".ogg", ".mp3", ".wav"}, stored,
                               why)) {
                error = ManifestError(where, why);
                return false;
            }
            ArtAudioRole role;
            role.Role = it.key();
            role.File = std::move(stored);
            extra.Audio.push_back(std::move(role));
        }
    }
    const auto files = audio->find("files");
    if (files != audio->end()) {
        if (!files->is_array()) {
            error = ManifestError("audio", "'files' must be a list");
            return false;
        }
        for (const Json& entry : *files) {
            if (!entry.is_string()) {
                error = ManifestError("audio", "'files' entries must be strings");
                return false;
            }
            std::filesystem::path stored;
            std::string why;
            if (!ResolveStored(source, entry.get<std::string>(), {".ogg", ".mp3", ".wav"}, stored,
                               why)) {
                error = ManifestError("audio", why);
                return false;
            }
            extra.AudioFiles.push_back(std::move(stored));
        }
    }
    return true;
}

bool LoadLicenses(const Json& root, const ByteSource& source, std::string& error)
{
    const auto list = root.find("licenses");
    if (list == root.end())
        return true;
    if (!list->is_array()) {
        error = ManifestError({}, "'licenses' must be a list");
        return false;
    }
    for (const Json& entry : *list) {
        if (!entry.is_object()) {
            error = ManifestError("licenses", "entries must be objects");
            return false;
        }
        std::string unknown;
        if (!AllowKeys(entry, {"pack", "file"}, unknown)) {
            error = ManifestError("licenses", "unknown field " + Quote(unknown));
            return false;
        }
        std::string pack;
        std::string file;
        std::string why;
        if (!ReadString(entry, "pack", true, pack, why) || !ReadString(entry, "file", true, file, why)) {
            error = ManifestError("licenses", why);
            return false;
        }
        std::filesystem::path stored;
        if (!ResolveStored(source, file, {}, stored, why)) {
            error = ManifestError(EntryWhere("license", pack), why);
            return false;
        }
    }
    return true;
}

bool ReadManifest(const ByteSource& source, const std::string& text, const Database& data,
                  std::vector<ArtImage>& images, std::vector<ListedTheme>& themes, ExtraArt& extra,
                  std::string& error)
{
    Json root;
    std::string why;
    if (!ParseRoot(text, root, why)) {
        error = ManifestError({}, why);
        return false;
    }
    std::string unknown;
    if (!AllowKeys(root,
                   {kTilesKey, kFoesKey, kHeroesKey, kNpcsKey, kThemesKey, kMarkersKey, kPropsKey,
                    "credit", "ui", "fonts", "icons", "items", "vfx", "backdrops", "audio",
                    "licenses"},
                   unknown)) {
        error = ManifestError({}, "unknown field " + Quote(unknown));
        return false;
    }
    if (!LoadThemes(root, data, themes, error))
        return false;
    const Section sections[] = {Section::Tiles, Section::Foes, Section::Heroes, Section::Npcs};
    for (const Section section : sections) {
        if (!LoadSection(section, root, source, data, themes, images, error))
            return false;
    }
    if (!LoadMarkers(root, source, images, error) || !LoadProps(root, source, data, images, error))
        return false;
    return LoadCredit(root, extra.Credit, error) && LoadUi(root, source, extra, error) &&
           LoadFonts(root, source, extra, error) &&
           LoadNamedList(root, "icons", "icon", source, extra.Icons, error) &&
           LoadNamedList(root, "items", "item", source, extra.Items, error) &&
           LoadNamedList(root, "vfx", "vfx", source, extra.Vfx, error) &&
           LoadBackdrops(root, source, extra, error) && LoadAudio(root, source, extra, error) &&
           LoadLicenses(root, source, error);
}

ResolvedArt FromImage(const ArtImage* image, i32 x, i32 y)
{
    if (image == nullptr || image->Pictures.empty())
        return {};
    i32 index = ArtVariantIndex(x, y, static_cast<i32>(image->Pictures.size()));
    if (index < 0 || static_cast<usize>(index) >= image->Pictures.size())
        index = 0;
    ResolvedArt resolved;
    resolved.Origin = ArtOrigin::Pack;
    resolved.Picture = image->Pictures[static_cast<usize>(index)];
    resolved.CellsWide = image->CellsWide;
    resolved.CellsHigh = image->CellsHigh;
    return resolved;
}

} // namespace

i32 ArtFrameIndex(i32 frames, i32 frameDurationMs, f32 seconds)
{
    if (frames <= 1 || seconds <= 0.0f)
        return 0;
    const i32 duration = frameDurationMs > 0 ? frameDurationMs : kArtFrameMilliseconds;
    const double cycle = static_cast<double>(duration) * static_cast<double>(frames);
    const double elapsed = std::fmod(static_cast<double>(seconds) * 1000.0, cycle);
    if (elapsed < 0.0)
        return 0;
    const i32 index = static_cast<i32>(elapsed / static_cast<double>(duration));
    if (index < 0 || index >= frames)
        return 0;
    return index;
}

i32 ArtVariantIndex(i32 x, i32 y, i32 count)
{
    if (count <= 1)
        return 0;
    const u32 mixed = static_cast<u32>(x) * kArtVariantX + static_cast<u32>(y) * kArtVariantY;
    return static_cast<i32>(mixed % static_cast<u32>(count));
}

void ArtPack::Fail(DataError* error, std::string message)
{
    *this = {};
    m_LastError = std::move(message);
    m_Loaded = false;
    if (error != nullptr)
        error->Message = m_LastError;
}

namespace {

bool LowerExtensionIs(const std::filesystem::path& path, std::string_view extension)
{
    std::string text = path.extension().string();
    for (char& code : text) {
        if (code >= 'A' && code <= 'Z')
            code = static_cast<char>(code - 'A' + 'a');
    }
    return text == extension;
}

bool ReadManifestText(const ByteSource& source, const std::filesystem::path& looseRoot,
                      std::string& text, std::string& error)
{
    if (source.Pak) {
        std::vector<unsigned char> bytes;
        if (!ReadSource(source, std::string(kArtPackManifestName), bytes, error)) {
            if (error.find("crc") == std::string::npos)
                error = ManifestError({}, error.empty() ? "could not be read" : error);
            return false;
        }
        text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    } else {
        std::string why;
        if (!ReadText(looseRoot / std::string(kArtPackManifestName), text, why)) {
            error = ManifestError({}, why);
            return false;
        }
    }
    if (!text.empty() && static_cast<unsigned char>(text[0]) == 0xEF && text.size() >= 3 &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);
    return true;
}

} // namespace

bool ArtPack::Load(const std::filesystem::path& directory, const Database& data, DataError* error)
{
    std::error_code code;
    const bool file = std::filesystem::is_regular_file(directory, code) && !code;
    code.clear();
    const bool folder = std::filesystem::is_directory(directory, code) && !code;
    if (!file && !folder) {
        Fail(error, "art pack folder '" + directory.string() + "' is not a directory");
        return false;
    }
    if (file && !LowerExtensionIs(directory, ".pak")) {
        Fail(error, "art pack folder '" + directory.string() + "' is not a directory");
        return false;
    }

    ByteSource source;
    std::filesystem::path root = directory;
    if (folder) {
        code.clear();
        const std::filesystem::path absolute = std::filesystem::absolute(directory, code);
        if (!code)
            root = absolute;
        source.Root = root;
    } else {
        source.Pak = std::make_shared<Emerald::Pak>();
        std::string why;
        if (!source.Pak->Open(directory, &why)) {
            Fail(error, why.empty() ? "could not be read" : why);
            return false;
        }
        root = directory;
    }

    std::string text;
    std::string message;
    if (!ReadManifestText(source, root, text, message)) {
        Fail(error, std::move(message));
        return false;
    }

    std::vector<ArtImage> images;
    std::vector<ListedTheme> listed;
    ExtraArt extra;
    if (!ReadManifest(source, text, data, images, listed, extra, message)) {
        Fail(error, std::move(message));
        return false;
    }

    std::vector<ThemeUse> themes;
    for (const ListedTheme& row : listed) {
        ThemeUse use;
        use.Theme = row.Theme;
        use.Map = row.Map;
        themes.push_back(std::move(use));
    }
    m_Pak = std::move(source.Pak);
    m_Directory = std::move(root);
    m_Images = std::move(images);
    m_Themes = std::move(themes);
    m_Credit = std::move(extra.Credit);
    m_Slices = std::move(extra.Slices);
    m_UiFiles = std::move(extra.UiFiles);
    m_Fonts = std::move(extra.Fonts);
    m_Icons = std::move(extra.Icons);
    m_Items = std::move(extra.Items);
    m_Vfx = std::move(extra.Vfx);
    m_Backdrops = std::move(extra.Backdrops);
    m_Audio = std::move(extra.Audio);
    m_AudioFiles = std::move(extra.AudioFiles);
    m_Loaded = true;
    m_LastError.clear();
    if (error != nullptr)
        error->Message.clear();
    return true;
}

const std::string* ArtPack::ThemeOf(std::string_view mapId) const
{
    for (const ThemeUse& use : m_Themes) {
        if (use.Map == mapId)
            return &use.Theme;
    }
    return nullptr;
}

const ArtImage* ArtPack::Select(ArtSlot slot, std::string_view id, std::string_view mapId) const
{
    const ArtImage* fallback = nullptr;
    const ArtImage* themed = nullptr;
    const ArtImage* mapped = nullptr;
    const std::string* theme = ThemeOf(mapId);
    for (const ArtImage& image : m_Images) {
        if (image.Slot != slot)
            continue;
        if (!id.empty()) {
            if (image.Id != id)
                continue;
        } else if (!IsArtMarker(slot)) {
            continue;
        }
        if (!image.Map.empty()) {
            if (image.Map == mapId)
                mapped = &image;
            continue;
        }
        if (!image.Theme.empty()) {
            if (theme != nullptr && image.Theme == *theme)
                themed = &image;
            continue;
        }
        fallback = &image;
    }
    if (mapped != nullptr)
        return mapped;
    if (themed != nullptr)
        return themed;
    return fallback;
}

ResolvedArt ArtPack::Find(ArtSlot slot, std::string_view id) const
{
    return FindPlaced(slot, id, {}, 0, 0);
}

ResolvedArt ArtPack::FindPlaced(ArtSlot slot, std::string_view id, std::string_view mapId, i32 x,
                                i32 y) const
{
    return FromImage(Select(slot, id, mapId), x, y);
}

namespace {

template <typename T>
const T* FindNamedRow(std::span<const T> rows, std::string_view id)
{
    for (const T& row : rows) {
        if (row.Id == id)
            return &row;
    }
    return nullptr;
}

bool IsPakFile(const std::filesystem::path& path)
{
    std::error_code code;
    if (!std::filesystem::is_regular_file(path, code) || code)
        return false;
    return LowerExtensionIs(path, ".pak");
}

} // namespace

const ArtNineSlice* ArtPack::FindSlice(std::string_view id) const
{
    return FindNamedRow(std::span<const ArtNineSlice>(m_Slices), id);
}

const ArtFontRole* ArtPack::FindFont(std::string_view role) const
{
    for (const ArtFontRole& font : m_Fonts) {
        if (font.Role == role)
            return &font;
    }
    return nullptr;
}

const ArtNamedFile* ArtPack::FindIcon(std::string_view id) const
{
    return FindNamedRow(std::span<const ArtNamedFile>(m_Icons), id);
}

const ArtNamedFile* ArtPack::FindItem(std::string_view id) const
{
    return FindNamedRow(std::span<const ArtNamedFile>(m_Items), id);
}

const ArtNamedFile* ArtPack::FindVfx(std::string_view id) const
{
    return FindNamedRow(std::span<const ArtNamedFile>(m_Vfx), id);
}

const ArtBackdrop* ArtPack::FindBackdrop(std::string_view id) const
{
    return FindNamedRow(std::span<const ArtBackdrop>(m_Backdrops), id);
}

const ArtAudioRole* ArtPack::FindAudio(std::string_view role) const
{
    for (const ArtAudioRole& audio : m_Audio) {
        if (audio.Role == role)
            return &audio;
    }
    return nullptr;
}

bool ArtPack::ReadFile(const std::filesystem::path& file, std::vector<unsigned char>& out,
                       std::string& error) const
{
    if (m_Pak) {
        const std::optional<std::vector<unsigned char>> bytes = m_Pak->Read(file.generic_string(), &error);
        if (!bytes) {
            if (error.empty())
                error = "could not be read";
            return false;
        }
        out = *bytes;
        return true;
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "could not be read";
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    error.clear();
    return true;
}

bool DiscoverArtPack(std::filesystem::path& requested,
                     const std::filesystem::path& executableDirectory, std::string& error)
{
    auto acceptDirectory = [](std::filesystem::path& path) {
        const std::filesystem::path pak = path / std::string(kArtPackFileName);
        if (IsPakFile(pak))
            path = pak;
    };
    if (!requested.empty()) {
        if (IsPakFile(requested)) {
            error.clear();
            return true;
        }
        std::error_code code;
        if (!std::filesystem::is_directory(requested, code) || code) {
            error = "art pack folder '" + requested.string() + "' is not a directory";
            return false;
        }
        acceptDirectory(requested);
        error.clear();
        return true;
    }
    if (executableDirectory.empty()) {
        error.clear();
        return true;
    }
    const std::filesystem::path beside =
        executableDirectory / std::string(kArtPackDirectoryName) / std::string(kArtPackFileName);
    if (IsPakFile(beside)) {
        requested = beside;
        error.clear();
        return true;
    }
    const std::filesystem::path folder = executableDirectory / std::string(kArtPackDirectoryName);
    std::error_code code;
    if (std::filesystem::is_directory(folder, code) && !code)
        requested = folder;
    error.clear();
    return true;
}

bool LoadGalleryBudget(const std::filesystem::path& directory, GalleryBudget& out, std::string& error)
{
    constexpr const char* kSections[] = {
        "fonts", "ui",     "icons", "tiles", "markers", "props",
        "heroes", "npcs",  "foes",  "items", "backdrops", "vfx",
    };
    auto known = [&](std::string_view name) {
        for (const char* section : kSections) {
            if (name == section)
                return true;
        }
        return false;
    };
    auto finish = [&](GalleryBudget& budget) {
        std::vector<std::string> order = budget.Order;
        for (const char* section : kSections) {
            bool found = false;
            for (const std::string& name : order) {
                if (name == section)
                    found = true;
            }
            if (!found)
                order.emplace_back(section);
        }
        budget.Order = std::move(order);
    };

    out = {};
    finish(out);
    const std::filesystem::path path = directory / "gallery.json";
    std::error_code missing;
    if (!std::filesystem::is_regular_file(path, missing) || missing) {
        error.clear();
        return true;
    }
    std::string text;
    std::string why;
    if (!ReadText(path, text, why)) {
        error = "gallery.json: " + why;
        return false;
    }
    Json root;
    if (!ParseRoot(text, root, why)) {
        error = "gallery.json: " + why;
        return false;
    }
    std::string unknown;
    if (!AllowKeys(root, {"gallery"}, unknown)) {
        error = "gallery.json: unknown field " + Quote(unknown);
        return false;
    }
    const auto body = root.find("gallery");
    if (body == root.end() || !body->is_object()) {
        error = "gallery.json: 'gallery' must be an object";
        return false;
    }
    if (!AllowKeys(*body, {"uploadsPerFrame", "budgetMilliseconds", "order"}, unknown)) {
        error = "gallery.json: unknown field " + Quote(unknown);
        return false;
    }
    GalleryBudget parsed;
    parsed.UploadsPerFrame = out.UploadsPerFrame;
    parsed.BudgetMilliseconds = out.BudgetMilliseconds;
    if (!OptionalInt(*body, "uploadsPerFrame", 0, 100000, parsed.UploadsPerFrame, why) ||
        !OptionalInt(*body, "budgetMilliseconds", 0, 1000, parsed.BudgetMilliseconds, why)) {
        error = "gallery.json: " + why;
        return false;
    }
    const auto listed = body->find("order");
    if (listed != body->end()) {
        if (!listed->is_array()) {
            error = "gallery.json: 'order' must be a list";
            return false;
        }
        for (const Json& entry : *listed) {
            if (!entry.is_string()) {
                error = "gallery.json: 'order' entries must be strings";
                return false;
            }
            const std::string name = entry.get<std::string>();
            if (!known(name)) {
                error = "gallery.json: order " + Quote(name) + " is not a section";
                return false;
            }
            for (const std::string& have : parsed.Order) {
                if (have == name) {
                    error = "gallery.json: order " + Quote(name) + " is repeated";
                    return false;
                }
            }
            parsed.Order.push_back(name);
        }
    }
    finish(parsed);
    out = std::move(parsed);
    error.clear();
    return true;
}

} // namespace Ascension
