#include "Ascension/Data/ArtPack.h"

#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iterator>

#include <nlohmann/json.hpp>

namespace Ascension {

namespace {

using Json = nlohmann::json;

constexpr const char* kTilesKey = "tiles";
constexpr const char* kFoesKey = "foes";
constexpr const char* kHeroesKey = "heroes";
constexpr const char* kNpcsKey = "npcs";
constexpr const char* kThemesKey = "themes";
constexpr const char* kMarkersKey = "markers";

constexpr i32 kMaxFrames = 256;
constexpr i32 kMaxFrameWidth = 16384;
constexpr i32 kMaxFrameDurationMs = 60000;
constexpr i32 kMaxVariants = 64;

constexpr unsigned char kPngSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

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

bool ReadPngSize(const std::filesystem::path& path, i32& width, i32& height)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    unsigned char bytes[24] = {};
    in.read(reinterpret_cast<char*>(bytes), 24);
    if (in.gcount() != static_cast<std::streamsize>(24))
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

// `text` is the path as written in the manifest. The result is an absolute png inside `root`.
bool ResolvePng(const std::filesystem::path& root, const std::string& text, ArtPicture& picture,
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
    const std::filesystem::path full = root / relative.lexically_normal();
    std::error_code missing;
    if (!std::filesystem::is_regular_file(full, missing) || missing) {
        error = Quote(text) + " does not exist";
        return false;
    }
    if (!InsideDirectory(root, full)) {
        error = Quote(text) + " must stay inside the pack";
        return false;
    }
    i32 width = 0;
    i32 height = 0;
    if (!ReadPngSize(full, width, height)) {
        error = Quote(text) + " is not a png";
        return false;
    }
    if (!FitStrip(picture, width, height, error))
        return false;
    error.clear();
    std::error_code canonicalError;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(full, canonicalError);
    picture.File = canonicalError ? full : canonical;
    return true;
}

bool ReadPicture(const Json& value, const std::filesystem::path& root, ArtPicture& picture,
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
        return ResolvePng(root, text, picture, error);
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
    return ResolvePng(root, text, picture, error);
}

void AddImage(std::vector<ArtImage>& images, ArtSlot slot, const std::string& id,
              const std::string& map, const std::string& theme, std::vector<ArtPicture> pictures)
{
    ArtImage image;
    image.Slot = slot;
    image.Id = id;
    image.Map = map;
    image.Theme = theme;
    image.Pictures = std::move(pictures);
    images.push_back(std::move(image));
}

bool AddOnePicture(std::vector<ArtImage>& images, ArtSlot slot, const std::string& id,
                   const Json& value, const std::filesystem::path& root, std::string& error)
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

bool LoadTile(const Json& entry, const std::filesystem::path& root, const Database& data,
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

bool LoadFoeOrNpc(Section section, const Json& entry, const std::filesystem::path& root,
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

bool LoadHero(const Json& entry, const std::filesystem::path& root, const Database& data,
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

bool LoadMarker(const Json& entry, const std::filesystem::path& root, std::vector<ArtImage>& images,
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

bool LoadMarkers(const Json& root, const std::filesystem::path& directory,
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

bool LoadSection(Section section, const Json& root, const std::filesystem::path& directory,
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

bool ReadManifest(const std::filesystem::path& directory, const Database& data,
                  std::vector<ArtImage>& images, std::vector<ListedTheme>& themes,
                  std::string& error)
{
    const std::filesystem::path manifest = directory / std::string(kArtPackManifestName);
    std::string text;
    std::string why;
    if (!ReadText(manifest, text, why)) {
        error = ManifestError({}, why);
        return false;
    }
    Json root;
    if (!ParseRoot(text, root, why)) {
        error = ManifestError({}, why);
        return false;
    }
    std::string unknown;
    if (!AllowKeys(root, {kTilesKey, kFoesKey, kHeroesKey, kNpcsKey, kThemesKey, kMarkersKey},
                   unknown)) {
        error = ManifestError({}, "unknown field " + Quote(unknown));
        return false;
    }
    if (!LoadThemes(root, data, themes, error))
        return false;
    const Section sections[] = {Section::Tiles, Section::Foes, Section::Heroes, Section::Npcs};
    for (const Section section : sections) {
        if (!LoadSection(section, root, directory, data, themes, images, error))
            return false;
    }
    return LoadMarkers(root, directory, images, error);
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

bool ArtPack::Load(const std::filesystem::path& directory, const Database& data, DataError* error)
{
    std::error_code code;
    if (!std::filesystem::is_directory(directory, code) || code) {
        m_Images.clear();
        m_Themes.clear();
        m_Directory.clear();
        m_Loaded = false;
        m_LastError = "art pack folder '" + directory.string() + "' is not a directory";
        if (error != nullptr)
            error->Message = m_LastError;
        return false;
    }

    std::filesystem::path root = directory;
    code.clear();
    const std::filesystem::path absolute = std::filesystem::absolute(directory, code);
    if (!code)
        root = absolute;

    std::vector<ArtImage> images;
    std::vector<ListedTheme> listed;
    std::string message;
    if (!ReadManifest(root, data, images, listed, message)) {
        m_Images.clear();
        m_Themes.clear();
        m_Directory.clear();
        m_Loaded = false;
        m_LastError = std::move(message);
        if (error != nullptr)
            error->Message = m_LastError;
        return false;
    }

    std::vector<ThemeUse> themes;
    for (const ListedTheme& row : listed) {
        ThemeUse use;
        use.Theme = row.Theme;
        use.Map = row.Map;
        themes.push_back(std::move(use));
    }
    m_Directory = std::move(root);
    m_Images = std::move(images);
    m_Themes = std::move(themes);
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

bool DiscoverArtPack(std::filesystem::path& requested,
                     const std::filesystem::path& executableDirectory, std::string& error)
{
    if (!requested.empty()) {
        std::error_code code;
        if (!std::filesystem::is_directory(requested, code) || code) {
            error = "art pack folder '" + requested.string() + "' is not a directory";
            return false;
        }
        error.clear();
        return true;
    }
    if (executableDirectory.empty()) {
        error.clear();
        return true;
    }
    const std::filesystem::path beside = executableDirectory / std::string(kArtPackDirectoryName);
    std::error_code code;
    if (std::filesystem::is_directory(beside, code) && !code)
        requested = beside;
    error.clear();
    return true;
}

} // namespace Ascension
