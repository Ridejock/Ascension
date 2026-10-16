// Reading data/*.json. nlohmann::json stays in this file: the public headers only have the structs.

#include "Ascension/Data/Database.h"

#include <fstream>
#include <initializer_list>
#include <iterator>
#include <utility>

#include <nlohmann/json.hpp>

#include "Emerald/Core/Log.h"

namespace Ascension {

namespace {

using Json = nlohmann::json;
using FileTime = std::optional<std::filesystem::file_time_type>;

constexpr const char* kFileNames[] = {
    "stats.json", "skills.json", "classes.json", "enemies.json", "items.json",
};

constexpr i32 kMinLevel = 1;
constexpr i32 kMaxLevel = 99;

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

// "classes.json: class 'berserker': skill 'smash' does not exist"
std::string Err(std::string_view file, std::string_view where, std::string_view message)
{
    std::string out(file);
    if (!where.empty()) {
        out += ": ";
        out += where;
    }
    out += ": ";
    out += message;
    return out;
}

void LogMessage(const std::string& message, bool failure)
{
    if (!Emerald::Log::Client())
        return;
    if (failure)
        EM_ERROR("{}", message);
    else
        EM_INFO("{}", message);
}

FileTime ReadStamp(const std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error)
        return std::nullopt;
    const auto time = std::filesystem::last_write_time(path, error);
    if (error)
        return std::nullopt;
    return time;
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
        // A UTF-8 BOM from a Windows editor. The rest of the file is the JSON.
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

// Rejects keys that are not in `allowed`, so a typo ("strenght") is an error instead of a
// silently ignored field.
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

bool RequireString(const Json& object, const char* key, std::string& out, std::string& error)
{
    const auto it = object.find(key);
    if (it == object.end()) {
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

bool RequireInt(const Json& object, const char* key, i32 min, i32 max, i32& out, std::string& error)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        error = std::string("'") + key + "' is required";
        return false;
    }
    if (!ReadInt(*it, min, max, out, error)) {
        error = std::string("'") + key + "' " + error;
        return false;
    }
    return true;
}

// A stat object. `requireAll` means every kStatIds key must be present (a class's level-1 stats).
// Otherwise a missing key is zero (a bonus). Unknown keys and non-integers always fail.
bool ReadStatBlock(const Json& object, bool requireAll, StatBlock& out, std::string& error)
{
    if (!object.is_object()) {
        error = "must be an object of stat ids";
        return false;
    }
    std::string unknown;
    if (!AllowKeys(object, {"vitality", "strength", "defense", "agility", "focus"}, unknown)) {
        error = "unknown stat " + Quote(unknown);
        return false;
    }
    out = {};
    for (const std::string_view id : kStatIds) {
        const auto it = object.find(std::string(id));
        if (it == object.end()) {
            if (requireAll) {
                error = "missing stat " + Quote(id);
                return false;
            }
            continue;
        }
        i32 value = 0;
        if (!ReadInt(*it, -999, 9999, value, error)) {
            error = std::string(id) + " " + error;
            return false;
        }
        out.Set(id, value);
    }
    return true;
}

bool ReadIdList(const Json& object, const char* key, std::vector<std::string>& out,
                std::string& error)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        error = std::string("'") + key + "' is required";
        return false;
    }
    if (!it->is_array()) {
        error = std::string("'") + key + "' must be a list";
        return false;
    }
    out.clear();
    for (const Json& entry : *it) {
        if (!entry.is_string()) {
            error = std::string("'") + key + "' entries must be strings";
            return false;
        }
        const std::string id = entry.get<std::string>();
        if (!IsId(id)) {
            error = Quote(id) + " is not an id (letters, digits and _, starting with a letter)";
            return false;
        }
        for (const std::string& existing : out) {
            if (existing == id) {
                error = Quote(id) + " is listed twice";
                return false;
            }
        }
        out.push_back(id);
    }
    return true;
}

struct Parsed {
    std::vector<StatDef> Stats;
    std::vector<SkillDef> Skills;
    std::vector<ClassDef> Classes;
    std::vector<EnemyDef> Enemies;
    std::vector<ItemDef> Items;
};

template <typename T> const T* FindId(const std::vector<T>& items, std::string_view id)
{
    for (const T& item : items) {
        if (item.Id == id)
            return &item;
    }
    return nullptr;
}

bool LoadStats(const Json& root, Parsed& parsed, std::string& error)
{
    const auto list = root.find("stats");
    if (list == root.end() || !list->is_array()) {
        error = "'stats' must be a list";
        return false;
    }
    for (const Json& entry : *list) {
        if (!entry.is_object()) {
            error = "stat entries must be objects";
            return false;
        }
        std::string unknown;
        if (!AllowKeys(entry, {"id", "name", "description"}, unknown)) {
            error = "unknown field " + Quote(unknown);
            return false;
        }
        StatDef stat;
        if (!RequireString(entry, "id", stat.Id, error) ||
            !RequireString(entry, "name", stat.Name, error) ||
            !RequireString(entry, "description", stat.Description, error))
            return false;
        if (!IsId(stat.Id)) {
            error =
                "id " + Quote(stat.Id) + " must be letters, digits and _, starting with a letter";
            return false;
        }
        bool known = false;
        for (const std::string_view id : kStatIds) {
            if (id == stat.Id)
                known = true;
        }
        if (!known) {
            error = "unknown stat " + Quote(stat.Id) +
                    " (the stats are vitality, strength, defense, agility, focus)";
            return false;
        }
        if (FindId(parsed.Stats, stat.Id) != nullptr) {
            error = "stat " + Quote(stat.Id) + " is listed twice";
            return false;
        }
        parsed.Stats.push_back(std::move(stat));
    }
    if (parsed.Stats.size() != kStatIds.size()) {
        error = "stats must list vitality, strength, defense, agility and focus";
        return false;
    }
    // Canonical order, so the screen does not jump when the file is reordered.
    std::vector<StatDef> ordered;
    ordered.reserve(kStatIds.size());
    for (const std::string_view id : kStatIds)
        ordered.push_back(*FindId(parsed.Stats, id));
    parsed.Stats = std::move(ordered);
    return true;
}

bool LoadSkills(const Json& root, Parsed& parsed, std::string& error)
{
    const auto list = root.find("skills");
    if (list == root.end() || !list->is_array()) {
        error = "'skills' must be a list";
        return false;
    }
    for (const Json& entry : *list) {
        if (!entry.is_object()) {
            error = "skill entries must be objects";
            return false;
        }
        SkillDef skill;
        if (!RequireString(entry, "id", skill.Id, error))
            return false;
        const std::string where = "skill " + Quote(skill.Id);
        auto fail = [&](std::string message) {
            error = where + ": " + std::move(message);
            return false;
        };
        if (!IsId(skill.Id))
            return fail("id must be letters, digits and _, starting with a letter");
        if (FindId(parsed.Skills, skill.Id) != nullptr)
            return fail("there is another skill with this id");

        std::string kind;
        if (!RequireString(entry, "kind", kind, error))
            return fail(error);
        if (kind == "active")
            skill.Kind = SkillKind::Active;
        else if (kind == "passive")
            skill.Kind = SkillKind::Passive;
        else
            return fail("'kind' must be \"active\" or \"passive\"");

        std::string unknown;
        if (skill.Kind == SkillKind::Active) {
            if (!AllowKeys(entry,
                           {"id", "name", "description", "kind", "target", "power", "focusCost",
                            "scaling"},
                           unknown))
                return fail("unknown field " + Quote(unknown));
            std::string target;
            if (!RequireString(entry, "target", target, error))
                return fail(error);
            if (target == "self")
                skill.Target = SkillTarget::Self;
            else if (target == "enemy")
                skill.Target = SkillTarget::Enemy;
            else if (target == "allEnemies")
                skill.Target = SkillTarget::AllEnemies;
            else
                return fail("'target' must be \"self\", \"enemy\" or \"allEnemies\"");
            if (!RequireInt(entry, "power", 0, 9999, skill.Power, error))
                return fail(error);
            if (!RequireInt(entry, "focusCost", 0, 99, skill.FocusCost, error))
                return fail(error);
            const auto scaling = entry.find("scaling");
            if (scaling != entry.end()) {
                if (!scaling->is_string())
                    return fail("'scaling' must be a stat id");
                skill.Scaling = scaling->get<std::string>();
                if (FindStatValue(StatBlock{}, skill.Scaling) == nullptr)
                    return fail("scaling stat " + Quote(skill.Scaling) + " does not exist");
            }
        } else {
            if (!AllowKeys(entry, {"id", "name", "description", "kind", "stat", "amount"}, unknown))
                return fail("unknown field " + Quote(unknown));
            if (!RequireString(entry, "stat", skill.Stat, error))
                return fail(error);
            if (FindStatValue(StatBlock{}, skill.Stat) == nullptr)
                return fail("stat " + Quote(skill.Stat) + " does not exist");
            if (!RequireInt(entry, "amount", -999, 999, skill.Amount, error))
                return fail(error);
        }
        if (!RequireString(entry, "name", skill.Name, error) ||
            !RequireString(entry, "description", skill.Description, error))
            return fail(error);
        parsed.Skills.push_back(std::move(skill));
    }
    return true;
}

bool LoadClasses(const Json& root, Parsed& parsed, std::string& error)
{
    const auto list = root.find("classes");
    if (list == root.end() || !list->is_array() || list->empty()) {
        error = "'classes' must be a list with at least one class";
        return false;
    }
    for (const Json& entry : *list) {
        if (!entry.is_object()) {
            error = "class entries must be objects";
            return false;
        }
        ClassDef klass;
        if (!RequireString(entry, "id", klass.Id, error))
            return false;
        const std::string where = "class " + Quote(klass.Id);
        auto fail = [&](std::string message) {
            error = where + ": " + std::move(message);
            return false;
        };
        if (!IsId(klass.Id))
            return fail("id must be letters, digits and _, starting with a letter");
        if (FindId(parsed.Classes, klass.Id) != nullptr)
            return fail("there is another class with this id");

        const bool evolution = entry.contains("evolvesFrom");
        const bool hasStats = entry.contains("stats");
        if (evolution && hasStats)
            return fail("a class is either a base (stats) or an evolution (evolvesFrom), not both");
        if (!evolution && !hasStats)
            return fail("needs stats, or evolvesFrom for an evolution");

        std::string unknown;
        if (evolution) {
            if (!AllowKeys(entry,
                           {"id", "name", "description", "evolvesFrom", "evolveLevel", "bonus",
                            "growth", "skills", "passives"},
                           unknown))
                return fail("unknown field " + Quote(unknown));
            if (!RequireString(entry, "evolvesFrom", klass.EvolvesFrom, error))
                return fail(error);
            if (!IsId(klass.EvolvesFrom))
                return fail("'evolvesFrom' must be a class id");
            if (!RequireInt(entry, "evolveLevel", 2, kMaxLevel, klass.EvolveLevel, error))
                return fail(error);
            const auto bonus = entry.find("bonus");
            if (bonus != entry.end()) {
                std::string why;
                if (!ReadStatBlock(*bonus, false, klass.Bonus, why))
                    return fail("'bonus' " + why);
            }
        } else {
            if (!AllowKeys(entry,
                           {"id", "name", "description", "stats", "growth", "skills", "passives"},
                           unknown))
                return fail("unknown field " + Quote(unknown));
            std::string why;
            const auto stats = entry.find("stats");
            if (!ReadStatBlock(*stats, true, klass.Stats, why))
                return fail("'stats' " + why);
            for (const std::string_view id : kStatIds) {
                if (klass.Stats.Get(id) < 0)
                    return fail(std::string(id) + " must be zero or more on a base class");
            }
        }

        const auto growth = entry.find("growth");
        if (growth == entry.end())
            return fail("'growth' is required");
        std::string why;
        if (!ReadStatBlock(*growth, !evolution, klass.Growth, why))
            return fail("'growth' " + why);
        for (const std::string_view id : kStatIds) {
            if (klass.Growth.Get(id) < 0)
                return fail("growth " + std::string(id) + " must be zero or more");
        }

        if (!ReadIdList(entry, "skills", klass.Skills, why))
            return fail(why);
        if (!ReadIdList(entry, "passives", klass.Passives, why))
            return fail(why);
        if (evolution && klass.Skills.empty())
            return fail("an evolution must add at least one skill");
        if (evolution && klass.Passives.empty())
            return fail("an evolution must add at least one passive");

        if (!RequireString(entry, "name", klass.Name, error) ||
            !RequireString(entry, "description", klass.Description, error))
            return fail(error);
        parsed.Classes.push_back(std::move(klass));
    }
    return true;
}

bool LoadEnemies(const Json& root, Parsed& parsed, std::string& error)
{
    const auto list = root.find("enemies");
    if (list == root.end() || !list->is_array()) {
        error = "'enemies' must be a list";
        return false;
    }
    for (const Json& entry : *list) {
        if (!entry.is_object()) {
            error = "enemy entries must be objects";
            return false;
        }
        EnemyDef enemy;
        if (!RequireString(entry, "id", enemy.Id, error))
            return false;
        const std::string where = "enemy " + Quote(enemy.Id);
        auto fail = [&](std::string message) {
            error = where + ": " + std::move(message);
            return false;
        };
        if (!IsId(enemy.Id))
            return fail("id must be letters, digits and _, starting with a letter");
        if (FindId(parsed.Enemies, enemy.Id) != nullptr)
            return fail("there is another enemy with this id");
        std::string unknown;
        if (!AllowKeys(
                entry,
                {"id", "name", "description", "level", "stats", "skills", "experience", "gold"},
                unknown))
            return fail("unknown field " + Quote(unknown));
        if (!RequireInt(entry, "level", kMinLevel, kMaxLevel, enemy.Level, error))
            return fail(error);
        const auto stats = entry.find("stats");
        if (stats == entry.end())
            return fail("'stats' is required");
        std::string why;
        if (!ReadStatBlock(*stats, true, enemy.Stats, why))
            return fail("'stats' " + why);
        for (const std::string_view id : kStatIds) {
            if (enemy.Stats.Get(id) < 0)
                return fail(std::string(id) + " must be zero or more");
        }
        if (!ReadIdList(entry, "skills", enemy.Skills, why))
            return fail(why);
        if (!RequireInt(entry, "experience", 0, 999999, enemy.Experience, error) ||
            !RequireInt(entry, "gold", 0, 999999, enemy.Gold, error))
            return fail(error);
        if (!RequireString(entry, "name", enemy.Name, error) ||
            !RequireString(entry, "description", enemy.Description, error))
            return fail(error);
        parsed.Enemies.push_back(std::move(enemy));
    }
    return true;
}

bool LoadItems(const Json& root, Parsed& parsed, std::string& error)
{
    const auto list = root.find("items");
    if (list == root.end() || !list->is_array()) {
        error = "'items' must be a list";
        return false;
    }
    for (const Json& entry : *list) {
        if (!entry.is_object()) {
            error = "item entries must be objects";
            return false;
        }
        ItemDef item;
        if (!RequireString(entry, "id", item.Id, error))
            return false;
        const std::string where = "item " + Quote(item.Id);
        auto fail = [&](std::string message) {
            error = where + ": " + std::move(message);
            return false;
        };
        if (!IsId(item.Id))
            return fail("id must be letters, digits and _, starting with a letter");
        if (FindId(parsed.Items, item.Id) != nullptr)
            return fail("there is another item with this id");
        std::string unknown;
        if (!AllowKeys(entry, {"id", "name", "description", "slot", "bonus", "value"}, unknown))
            return fail("unknown field " + Quote(unknown));
        std::string slot;
        if (!RequireString(entry, "slot", slot, error))
            return fail(error);
        if (slot == "weapon")
            item.Slot = GearSlot::Weapon;
        else if (slot == "armor")
            item.Slot = GearSlot::Armor;
        else if (slot == "accessory")
            item.Slot = GearSlot::Accessory;
        else
            return fail("'slot' must be \"weapon\", \"armor\" or \"accessory\"");
        const auto bonus = entry.find("bonus");
        if (bonus == entry.end())
            return fail("'bonus' is required");
        std::string why;
        if (!ReadStatBlock(*bonus, false, item.Bonus, why))
            return fail("'bonus' " + why);
        if (!RequireInt(entry, "value", 0, 999999, item.Value, error))
            return fail(error);
        if (!RequireString(entry, "name", item.Name, error) ||
            !RequireString(entry, "description", item.Description, error))
            return fail(error);
        parsed.Items.push_back(std::move(item));
    }
    return true;
}

bool SkillRefs(const Parsed& parsed, const std::vector<std::string>& ids, SkillKind kind,
               const std::string& where, std::string& error)
{
    for (const std::string& id : ids) {
        const SkillDef* skill = FindId(parsed.Skills, id);
        if (skill == nullptr) {
            error = where + ": skill " + Quote(id) + " does not exist";
            return false;
        }
        if (skill->Kind != kind) {
            error = where + ": skill " + Quote(id) + " is " + ToString(skill->Kind) +
                    "; list it under " + (kind == SkillKind::Active ? "skills" : "passives");
            return false;
        }
    }
    return true;
}

bool CrossCheck(const Parsed& parsed, std::string& error)
{
    for (const ClassDef& klass : parsed.Classes) {
        const std::string where = "class " + Quote(klass.Id);
        if (!SkillRefs(parsed, klass.Skills, SkillKind::Active, where, error))
            return false;
        if (!SkillRefs(parsed, klass.Passives, SkillKind::Passive, where, error))
            return false;
        if (!klass.IsEvolution())
            continue;
        const ClassDef* parent = FindId(parsed.Classes, klass.EvolvesFrom);
        if (parent == nullptr) {
            error = where + ": evolvesFrom " + Quote(klass.EvolvesFrom) + " does not exist";
            return false;
        }
        if (parent->IsEvolution()) {
            error = where + ": evolvesFrom " + Quote(klass.EvolvesFrom) +
                    " is an evolution; evolve from the base class";
            return false;
        }
        for (const std::string& id : klass.Skills) {
            for (const std::string& parentId : parent->Skills) {
                if (id == parentId) {
                    error = where + ": " + Quote(parent->Id) + " already has skill " + Quote(id);
                    return false;
                }
            }
        }
        for (const std::string& id : klass.Passives) {
            for (const std::string& parentId : parent->Passives) {
                if (id == parentId) {
                    error = where + ": " + Quote(parent->Id) + " already has passive " + Quote(id);
                    return false;
                }
            }
        }
    }
    for (const EnemyDef& enemy : parsed.Enemies) {
        if (!SkillRefs(parsed, enemy.Skills, SkillKind::Active, "enemy " + Quote(enemy.Id), error))
            return false;
    }
    return true;
}

bool ReadAll(const std::vector<std::filesystem::path>& files, Parsed& parsed,
             std::vector<FileTime>& times, std::string& error)
{
    // Stamp every file first, including ones we never parse because an earlier file failed.
    // The caller records these times, so a broken file is not read again until something changes.
    times.resize(files.size());
    for (usize i = 0; i < files.size(); ++i)
        times[i] = ReadStamp(files[i]);

    using Loader = bool (*)(const Json&, Parsed&, std::string&);
    const Loader loaders[] = {LoadStats, LoadSkills, LoadClasses, LoadEnemies, LoadItems};
    const char* names[] = {"stats.json", "skills.json", "classes.json", "enemies.json",
                           "items.json"};

    for (usize i = 0; i < files.size(); ++i) {
        std::string text;
        std::string why;
        if (!ReadText(files[i], text, why)) {
            error = Err(names[i], {}, why);
            return false;
        }
        Json root;
        if (!ParseRoot(text, root, why)) {
            error = Err(names[i], {}, why);
            return false;
        }
        std::string unknown;
        const char* allowedTop[] = {"stats", "skills", "classes", "enemies", "items"};
        if (!AllowKeys(root, {allowedTop[i]}, unknown)) {
            error = Err(names[i], {}, "unknown field " + Quote(unknown));
            return false;
        }
        if (!loaders[i](root, parsed, why)) {
            error = Err(names[i], {}, why);
            return false;
        }
    }
    std::string why;
    if (!CrossCheck(parsed, why)) {
        const char* file = why.compare(0, 6, "enemy ") == 0 ? "enemies.json" : "classes.json";
        error = Err(file, {}, why);
        return false;
    }
    return true;
}

} // namespace

Database::Database(std::filesystem::path directory) : m_Directory(std::move(directory))
{
    std::error_code error;
    const auto absolute = std::filesystem::absolute(m_Directory, error);
    if (!error)
        m_Directory = absolute;
    for (const char* name : kFileNames) {
        FileWatch watch;
        watch.Path = m_Directory / name;
        m_Watches.push_back(std::move(watch));
    }
}

void Database::NoteFailure(const DataError& error, DataError* out)
{
    if (out != nullptr)
        *out = error;
    if (error.Message == m_LastError)
        return;
    m_LastError = error.Message;
    LogMessage(m_LastError, true);
}

void Database::NoteSuccess(DataError* out)
{
    m_LastError.clear();
    if (out != nullptr)
        out->Message.clear();
    LogMessage("Loaded data revision " + std::to_string(m_Revision) + " from " +
                   m_Directory.string(),
               false);
}

void Database::ApplyTimes(const std::vector<FileTime>& times)
{
    for (usize i = 0; i < m_Watches.size() && i < times.size(); ++i) {
        m_Watches[i].Loaded = times[i];
        m_Watches[i].Seen = times[i];
        m_Watches[i].Waiting = false;
    }
}

bool Database::Load(DataError* error)
{
    std::vector<std::filesystem::path> files;
    files.reserve(m_Watches.size());
    for (const FileWatch& watch : m_Watches)
        files.push_back(watch.Path);

    Parsed parsed;
    std::vector<FileTime> times;
    std::string message;
    if (!ReadAll(files, parsed, times, message)) {
        DataError failed;
        failed.Message = std::move(message);
        NoteFailure(failed, error);
        ApplyTimes(times);
        return false;
    }

    m_Stats = std::move(parsed.Stats);
    m_Skills = std::move(parsed.Skills);
    m_Classes = std::move(parsed.Classes);
    m_Enemies = std::move(parsed.Enemies);
    m_Items = std::move(parsed.Items);
    ApplyTimes(times);
    ++m_Revision;
    NoteSuccess(error);
    return true;
}

u32 Database::CheckForChanges(DataError* error)
{
    bool pending = false;
    bool stable = true;
    for (FileWatch& watch : m_Watches) {
        const FileTime now = ReadStamp(watch.Path);
        if (!watch.Waiting) {
            if (now != watch.Loaded) {
                watch.Waiting = true;
                watch.Seen = now;
                pending = true;
                stable = false;
            }
        } else {
            pending = true;
            if (now != watch.Seen) {
                watch.Seen = now;
                stable = false;
            }
        }
    }
    if (!pending || !stable)
        return 0;
    const u32 before = m_Revision;
    Load(error);
    return m_Revision != before ? 1u : 0u;
}

const StatDef* Database::FindStat(std::string_view id) const
{
    return FindId(m_Stats, id);
}

const SkillDef* Database::FindSkill(std::string_view id) const
{
    return FindId(m_Skills, id);
}

const ClassDef* Database::FindClass(std::string_view id) const
{
    return FindId(m_Classes, id);
}

const EnemyDef* Database::FindEnemy(std::string_view id) const
{
    return FindId(m_Enemies, id);
}

const ItemDef* Database::FindItem(std::string_view id) const
{
    return FindId(m_Items, id);
}

std::vector<const ClassDef*> Database::EvolutionsOf(std::string_view baseId) const
{
    std::vector<const ClassDef*> out;
    for (const ClassDef& klass : m_Classes) {
        if (klass.EvolvesFrom == baseId)
            out.push_back(&klass);
    }
    return out;
}

void CollectSkills(const Database& database, const ClassDef& klass, bool passives,
                   std::vector<const SkillDef*>& out)
{
    if (klass.IsEvolution()) {
        if (const ClassDef* parent = database.FindClass(klass.EvolvesFrom))
            CollectSkills(database, *parent, passives, out);
    }
    const std::vector<std::string>& ids = passives ? klass.Passives : klass.Skills;
    for (const std::string& id : ids) {
        if (const SkillDef* skill = database.FindSkill(id))
            out.push_back(skill);
    }
}

std::vector<const SkillDef*> Database::ActiveSkills(const ClassDef& klass) const
{
    std::vector<const SkillDef*> out;
    CollectSkills(*this, klass, false, out);
    return out;
}

std::vector<const SkillDef*> Database::Passives(const ClassDef& klass) const
{
    std::vector<const SkillDef*> out;
    CollectSkills(*this, klass, true, out);
    return out;
}

StatBlock Scaled(const StatBlock& block, i32 factor)
{
    StatBlock out;
    out.Vitality = block.Vitality * factor;
    out.Strength = block.Strength * factor;
    out.Defense = block.Defense * factor;
    out.Agility = block.Agility * factor;
    out.Focus = block.Focus * factor;
    return out;
}

StatBlock Database::StatsAt(const ClassDef& klass, i32 level) const
{
    if (level < kMinLevel)
        level = kMinLevel;
    if (level > kMaxLevel)
        level = kMaxLevel;
    if (klass.IsEvolution()) {
        const ClassDef* parent = FindClass(klass.EvolvesFrom);
        if (parent == nullptr)
            return {};
        if (level < klass.EvolveLevel)
            return StatsAt(*parent, level);
        StatBlock stats = StatsAt(*parent, klass.EvolveLevel);
        stats += klass.Bonus;
        stats += Scaled(klass.Growth, level - klass.EvolveLevel);
        return stats;
    }
    return klass.Stats + Scaled(klass.Growth, level - 1);
}

} // namespace Ascension
