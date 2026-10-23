#include "Ascension/Data/ArtPack.h"

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

constexpr unsigned char kPngSignature[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

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

bool HasPngSignature(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    unsigned char bytes[8] = {};
    in.read(reinterpret_cast<char*>(bytes), 8);
    if (in.gcount() != static_cast<std::streamsize>(8))
        return false;
    for (int index = 0; index < 8; ++index) {
        if (bytes[index] != kPngSignature[index])
            return false;
    }
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

// `text` is the path as written in the manifest. The result is an absolute png inside `root`.
bool ResolvePng(const std::filesystem::path& root, const std::string& text,
                std::filesystem::path& absolute, std::string& error)
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
    if (!HasPngSignature(full)) {
        error = Quote(text) + " is not a png";
        return false;
    }
    error.clear();
    std::error_code canonicalError;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(full, canonicalError);
    absolute = canonicalError ? full : canonical;
    return true;
}

void AddImage(std::vector<ArtImage>& images, ArtSlot slot, const std::string& id,
              const std::filesystem::path& file)
{
    ArtImage image;
    image.Slot = slot;
    image.Id = id;
    image.File = file;
    images.push_back(std::move(image));
}

bool AddResolved(std::vector<ArtImage>& images, ArtSlot slot, const std::string& id,
                 const std::filesystem::path& root, const std::string& text, std::string& error)
{
    std::filesystem::path file;
    if (!ResolvePng(root, text, file, error))
        return false;
    AddImage(images, slot, id, file);
    return true;
}

bool LoadEntry(Section section, const Json& entry, const std::filesystem::path& root,
               const Database& data, std::vector<ArtImage>& images, std::string& error)
{
    const char* key = SectionKey(section);
    const char* kind = SectionKind(section);
    if (!entry.is_object()) {
        error = ManifestError(key, "entries must be objects");
        return false;
    }
    std::string unknown;
    bool keysOk = false;
    if (section == Section::Tiles)
        keysOk = AllowKeys(entry, {"id", "file"}, unknown);
    else if (section == Section::Foes)
        keysOk = AllowKeys(entry, {"id", "overland", "battle"}, unknown);
    else if (section == Section::Heroes)
        keysOk = AllowKeys(entry, {"id", "battle"}, unknown);
    else
        keysOk = AllowKeys(entry, {"id", "sprite", "portrait"}, unknown);
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

    if (section == Section::Tiles) {
        std::string file;
        if (!ReadString(entry, "file", true, file, why)) {
            error = ManifestError(where, why);
            return false;
        }
        if (!AddResolved(images, ArtSlot::Tile, id, root, file, why)) {
            error = ManifestError(where, why);
            return false;
        }
        return true;
    }

    if (section == Section::Heroes) {
        std::string battle;
        if (!ReadString(entry, "battle", true, battle, why)) {
            error = ManifestError(where, why);
            return false;
        }
        if (!AddResolved(images, ArtSlot::HeroBattle, id, root, battle, why)) {
            error = ManifestError(where, why);
            return false;
        }
        return true;
    }

    if (section == Section::Foes) {
        std::string overland;
        std::string battle;
        if (!ReadString(entry, "overland", false, overland, why) ||
            !ReadString(entry, "battle", false, battle, why)) {
            error = ManifestError(where, why);
            return false;
        }
        if (overland.empty() && battle.empty()) {
            error = ManifestError(where, "name an 'overland' image, a 'battle' image, or both");
            return false;
        }
        if (!overland.empty() &&
            !AddResolved(images, ArtSlot::FoeOverland, id, root, overland, why)) {
            error = ManifestError(where, why);
            return false;
        }
        if (!battle.empty() && !AddResolved(images, ArtSlot::FoeBattle, id, root, battle, why)) {
            error = ManifestError(where, why);
            return false;
        }
        return true;
    }

    std::string sprite;
    std::string portrait;
    if (!ReadString(entry, "sprite", false, sprite, why) ||
        !ReadString(entry, "portrait", false, portrait, why)) {
        error = ManifestError(where, why);
        return false;
    }
    if (sprite.empty() && portrait.empty()) {
        error = ManifestError(where, "name a 'sprite' image, a 'portrait' image, or both");
        return false;
    }
    if (!sprite.empty() && !AddResolved(images, ArtSlot::NpcSprite, id, root, sprite, why)) {
        error = ManifestError(where, why);
        return false;
    }
    if (!portrait.empty() &&
        !AddResolved(images, ArtSlot::NpcPortrait, id, root, portrait, why)) {
        error = ManifestError(where, why);
        return false;
    }
    return true;
}

bool LoadSection(Section section, const Json& root, const std::filesystem::path& directory,
                 const Database& data, std::vector<ArtImage>& images, std::string& error)
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
        if (!LoadEntry(section, entry, directory, data, images, error))
            return false;
    }
    return true;
}

bool ReadManifest(const std::filesystem::path& directory, const Database& data,
                  std::vector<ArtImage>& images, std::string& error)
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
    if (!AllowKeys(root, {kTilesKey, kFoesKey, kHeroesKey, kNpcsKey}, unknown)) {
        error = ManifestError({}, "unknown field " + Quote(unknown));
        return false;
    }
    const Section sections[] = {Section::Tiles, Section::Foes, Section::Heroes, Section::Npcs};
    for (const Section section : sections) {
        if (!LoadSection(section, root, directory, data, images, error))
            return false;
    }
    return true;
}

} // namespace

bool ArtPack::Load(const std::filesystem::path& directory, const Database& data, DataError* error)
{
    std::error_code code;
    if (!std::filesystem::is_directory(directory, code) || code) {
        m_Images.clear();
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
    std::string message;
    if (!ReadManifest(root, data, images, message)) {
        m_Images.clear();
        m_Directory.clear();
        m_Loaded = false;
        m_LastError = std::move(message);
        if (error != nullptr)
            error->Message = m_LastError;
        return false;
    }

    m_Directory = std::move(root);
    m_Images = std::move(images);
    m_Loaded = true;
    m_LastError.clear();
    if (error != nullptr)
        error->Message.clear();
    return true;
}

ResolvedArt ArtPack::Find(ArtSlot slot, std::string_view id) const
{
    for (const ArtImage& image : m_Images) {
        if (image.Slot == slot && image.Id == id) {
            ResolvedArt resolved;
            resolved.Origin = ArtOrigin::Pack;
            resolved.File = image.File;
            return resolved;
        }
    }
    return {};
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
    const std::filesystem::path beside =
        executableDirectory / std::string(kArtPackDirectoryName);
    std::error_code code;
    if (std::filesystem::is_directory(beside, code) && !code)
        requested = beside;
    error.clear();
    return true;
}

} // namespace Ascension
