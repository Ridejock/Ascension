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
    "stats.json",   "skills.json", "statuses.json", "classes.json",
    "enemies.json", "items.json",  "combat.json",   "progress.json",
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

// Missing key leaves `out` unchanged and succeeds. A present value must be an integer in range.
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

// Missing key leaves `out` empty. A present value must be an id.
bool OptionalId(const Json& object, const char* key, std::string& out, std::string& error)
{
    const auto it = object.find(key);
    if (it == object.end())
        return true;
    if (!it->is_string()) {
        error = std::string("'") + key + "' must be text";
        return false;
    }
    out = it->get<std::string>();
    if (!IsId(out)) {
        error = std::string("'") + key + "' must be an id";
        return false;
    }
    return true;
}

bool Absent(const Json& object, const char* key, const std::string& where, std::string& error)
{
    if (!object.contains(key))
        return true;
    error = where + " does not use '" + key + "'";
    return false;
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
                std::string& error, bool allowDuplicates = false)
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
        if (!allowDuplicates) {
            for (const std::string& existing : out) {
                if (existing == id) {
                    error = Quote(id) + " is listed twice";
                    return false;
                }
            }
        }
        out.push_back(id);
    }
    return true;
}

struct Parsed {
    std::vector<StatDef> Stats;
    std::vector<SkillDef> Skills;
    std::vector<StatusDef> Statuses;
    std::vector<ClassDef> Classes;
    std::vector<EnemyDef> Enemies;
    std::vector<ItemDef> Items;
    CombatRules Rules;
    ProgressRules Progress;
};

bool ParseEffect(std::string_view text, SkillEffect& out)
{
    if (text == "damage")
        out = SkillEffect::Damage;
    else if (text == "guard")
        out = SkillEffect::Guard;
    else if (text == "heal")
        out = SkillEffect::Heal;
    else if (text == "empower")
        out = SkillEffect::Empower;
    else if (text == "riposte")
        out = SkillEffect::Riposte;
    else if (text == "harden")
        out = SkillEffect::Harden;
    else
        return false;
    return true;
}

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
                           {"id", "name", "description", "kind", "effect", "target", "power",
                            "focusCost", "scaling", "guard", "reflect", "windup", "duration",
                            "status", "statusEvery", "stunWindup"},
                           unknown))
                return fail("unknown field " + Quote(unknown));
            std::string effect;
            if (!RequireString(entry, "effect", effect, error))
                return fail(error);
            if (!ParseEffect(effect, skill.Effect))
                return fail("'effect' must be \"damage\", \"guard\", \"heal\", \"empower\", "
                            "\"riposte\" or \"harden\"");
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
            const bool selfEffect =
                skill.Effect == SkillEffect::Guard || skill.Effect == SkillEffect::Heal ||
                skill.Effect == SkillEffect::Empower || skill.Effect == SkillEffect::Riposte ||
                skill.Effect == SkillEffect::Harden;
            if (selfEffect && skill.Target != SkillTarget::Self)
                return fail(std::string("'target' for ") + ToString(skill.Effect) +
                            " must be \"self\"");
            if (!selfEffect && skill.Target == SkillTarget::Self)
                return fail(std::string("'target' for ") + ToString(skill.Effect) +
                            " must be \"enemy\" or \"allEnemies\"");
            if (!RequireInt(entry, "power", 0, 9999, skill.Power, error) ||
                !RequireInt(entry, "focusCost", 0, 99, skill.FocusCost, error))
                return fail(error);
            if (skill.Effect == SkillEffect::Guard && skill.Power > 100)
                return fail("'power' for a guard is a percent from 0 to 100");
            if (skill.Effect == SkillEffect::Riposte) {
                if (!RequireInt(entry, "guard", 0, 100, skill.Guard, error) ||
                    !RequireInt(entry, "reflect", 0, 100, skill.Reflect, error))
                    return fail(error);
                if (!Absent(entry, "windup", "a riposte", error) ||
                    !Absent(entry, "duration", "a riposte", error) ||
                    !Absent(entry, "status", "a riposte", error) ||
                    !Absent(entry, "statusEvery", "a riposte", error) ||
                    !Absent(entry, "stunWindup", "a riposte", error))
                    return fail(error);
            } else if (skill.Effect == SkillEffect::Harden) {
                if (!RequireInt(entry, "duration", 1, 99, skill.Duration, error))
                    return fail(error);
                if (!Absent(entry, "guard", "harden", error) ||
                    !Absent(entry, "reflect", "harden", error) ||
                    !Absent(entry, "scaling", "harden", error) ||
                    !Absent(entry, "windup", "harden", error) ||
                    !Absent(entry, "status", "harden", error) ||
                    !Absent(entry, "statusEvery", "harden", error) ||
                    !Absent(entry, "stunWindup", "harden", error))
                    return fail(error);
            } else if (skill.Effect == SkillEffect::Damage) {
                if (!OptionalInt(entry, "windup", 1, 9, skill.Windup, error) ||
                    !OptionalId(entry, "status", skill.Status, error) ||
                    !OptionalId(entry, "stunWindup", skill.StunWindup, error))
                    return fail(error);
                if (entry.contains("statusEvery")) {
                    if (skill.Status.empty())
                        return fail("'statusEvery' needs a 'status'");
                    if (!RequireInt(entry, "statusEvery", 1, 99, skill.StatusEvery, error))
                        return fail(error);
                }
                if (!Absent(entry, "guard", "a damage skill", error) ||
                    !Absent(entry, "reflect", "a damage skill", error) ||
                    !Absent(entry, "duration", "a damage skill", error))
                    return fail(error);
            } else {
                if (!Absent(entry, "guard", ToString(skill.Effect), error) ||
                    !Absent(entry, "reflect", ToString(skill.Effect), error) ||
                    !Absent(entry, "windup", ToString(skill.Effect), error) ||
                    !Absent(entry, "duration", ToString(skill.Effect), error) ||
                    !Absent(entry, "status", ToString(skill.Effect), error) ||
                    !Absent(entry, "statusEvery", ToString(skill.Effect), error) ||
                    !Absent(entry, "stunWindup", ToString(skill.Effect), error))
                    return fail(error);
            }
            const auto scaling = entry.find("scaling");
            if (scaling != entry.end()) {
                if (!scaling->is_string())
                    return fail("'scaling' must be a stat id");
                skill.Scaling = scaling->get<std::string>();
                if (FindStatValue(StatBlock{}, skill.Scaling) == nullptr)
                    return fail("scaling stat " + Quote(skill.Scaling) + " does not exist");
            }
        } else {
            if (!AllowKeys(entry,
                           {"id", "name", "description", "kind", "effect", "stat", "amount",
                            "stepPercent", "max", "focus"},
                           unknown))
                return fail("unknown field " + Quote(unknown));
            std::string effect;
            if (!RequireString(entry, "effect", effect, error))
                return fail(error);
            if (effect == "stat") {
                skill.Passive = PassiveEffect::Stat;
                if (entry.contains("stepPercent") || entry.contains("max") ||
                    entry.contains("focus"))
                    return fail("a stat passive uses stat and amount");
                if (!RequireString(entry, "stat", skill.Stat, error))
                    return fail(error);
                if (FindStatValue(StatBlock{}, skill.Stat) == nullptr)
                    return fail("stat " + Quote(skill.Stat) + " does not exist");
                if (!RequireInt(entry, "amount", -999, 999, skill.Amount, error))
                    return fail(error);
            } else if (effect == "bloodied") {
                skill.Passive = PassiveEffect::Bloodied;
                if (entry.contains("focus"))
                    return fail("bloodied uses stat, amount, stepPercent and max");
                if (!RequireString(entry, "stat", skill.Stat, error))
                    return fail(error);
                if (FindStatValue(StatBlock{}, skill.Stat) == nullptr)
                    return fail("stat " + Quote(skill.Stat) + " does not exist");
                if (!RequireInt(entry, "amount", 0, 99, skill.Amount, error) ||
                    !RequireInt(entry, "stepPercent", 1, 100, skill.StepPercent, error) ||
                    !RequireInt(entry, "max", 0, 99, skill.Max, error))
                    return fail(error);
            } else if (effect == "bulwark") {
                skill.Passive = PassiveEffect::Bulwark;
                if (entry.contains("stepPercent") || entry.contains("max"))
                    return fail("bulwark uses stat, amount and focus");
                if (!RequireString(entry, "stat", skill.Stat, error))
                    return fail(error);
                if (FindStatValue(StatBlock{}, skill.Stat) == nullptr)
                    return fail("stat " + Quote(skill.Stat) + " does not exist");
                if (!RequireInt(entry, "amount", -999, 999, skill.Amount, error) ||
                    !RequireInt(entry, "focus", 0, 99, skill.FocusGain, error))
                    return fail(error);
            } else {
                return fail("'effect' must be \"stat\", \"bloodied\" or \"bulwark\"");
            }
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
        if (!AllowKeys(entry,
                       {"id", "name", "description", "level", "stats", "skills", "experience",
                        "gold", "boss", "pattern", "phaseAtPercent", "phaseStatus", "shardDamage",
                        "shardEvery"},
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
        const auto boss = entry.find("boss");
        if (boss != entry.end()) {
            if (!boss->is_boolean())
                return fail("'boss' must be true or false");
            enemy.Boss = boss->get<bool>();
        }
        if (entry.contains("pattern")) {
            std::string patternError;
            if (!ReadIdList(entry, "pattern", enemy.Pattern, patternError, true))
                return fail(patternError);
        }
        if (!OptionalInt(entry, "phaseAtPercent", 1, 99, enemy.PhaseAtPercent, error) ||
            !OptionalId(entry, "phaseStatus", enemy.PhaseStatus, error) ||
            !OptionalInt(entry, "shardDamage", 0, 9999, enemy.ShardDamage, error) ||
            !OptionalInt(entry, "shardEvery", 1, 99, enemy.ShardEvery, error))
            return fail(error);
        if (!enemy.PhaseStatus.empty() && enemy.PhaseAtPercent <= 0)
            return fail("'phaseStatus' needs 'phaseAtPercent'");
        if (enemy.ShardDamage > 0 && enemy.ShardEvery <= 0)
            return fail("'shardDamage' needs 'shardEvery'");
        if (enemy.ShardEvery > 0 && enemy.PhaseAtPercent <= 0)
            return fail("'shardEvery' needs 'phaseAtPercent'");
        if (!RequireString(entry, "name", enemy.Name, error) ||
            !RequireString(entry, "description", enemy.Description, error))
            return fail(error);
        parsed.Enemies.push_back(std::move(enemy));
    }
    i32 bosses = 0;
    for (const EnemyDef& enemy : parsed.Enemies) {
        if (enemy.Boss)
            ++bosses;
    }
    if (bosses != 1) {
        error = "the roster needs exactly one boss";
        return false;
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

bool LoadCombat(const Json& root, Parsed& parsed, std::string& error)
{
    const auto body = root.find("combat");
    if (body == root.end() || !body->is_object()) {
        error = "'combat' must be an object";
        return false;
    }
    std::string unknown;
    if (!AllowKeys(*body,
                   {"hpPerVitality", "minDamage", "attackNumerator", "attackDenominator",
                    "defenseNumerator", "defenseDenominator", "focusBase", "focusPerTurn",
                    "focusOnWait"},
                   unknown)) {
        error = "unknown field " + Quote(unknown);
        return false;
    }
    CombatRules rules;
    if (!RequireInt(*body, "hpPerVitality", 1, 999, rules.HpPerVitality, error) ||
        !RequireInt(*body, "minDamage", 0, 999, rules.MinDamage, error) ||
        !RequireInt(*body, "attackNumerator", 0, 999, rules.AttackNumerator, error) ||
        !RequireInt(*body, "attackDenominator", 1, 999, rules.AttackDenominator, error) ||
        !RequireInt(*body, "defenseNumerator", 0, 999, rules.DefenseNumerator, error) ||
        !RequireInt(*body, "defenseDenominator", 1, 999, rules.DefenseDenominator, error) ||
        !RequireInt(*body, "focusBase", 0, 99, rules.FocusBase, error) ||
        !RequireInt(*body, "focusPerTurn", 0, 99, rules.FocusPerTurn, error) ||
        !RequireInt(*body, "focusOnWait", 0, 99, rules.FocusOnWait, error))
        return false;
    parsed.Rules = rules;
    return true;
}

bool LoadProgress(const Json& root, Parsed& parsed, std::string& error)
{
    const auto body = root.find("progress");
    if (body == root.end() || !body->is_object()) {
        error = "'progress' must be an object";
        return false;
    }
    std::string unknown;
    if (!AllowKeys(*body, {"xpToReach", "usesPerRank", "maxSkillRank", "powerPerRank"}, unknown)) {
        error = "unknown field " + Quote(unknown);
        return false;
    }
    const auto curve = body->find("xpToReach");
    if (curve == body->end() || !curve->is_array() || curve->empty()) {
        error = "'xpToReach' must be a list of the total XP for each level";
        return false;
    }
    if (curve->size() > 99) {
        error = "'xpToReach' has more than 99 levels";
        return false;
    }
    ProgressRules rules;
    i32 previous = -1;
    usize index = 0;
    for (const Json& entry : *curve) {
        i32 xp = 0;
        if (!ReadInt(entry, 0, 999999, xp, error)) {
            error = "level " + std::to_string(index + 1) + " xp " + error;
            return false;
        }
        if (index == 0 && xp != 0) {
            error = "level 1 xp must be 0";
            return false;
        }
        if (index > 0 && xp <= previous) {
            error = "level " + std::to_string(index + 1) + " xp must be greater than level " +
                    std::to_string(index);
            return false;
        }
        rules.XpToReach.push_back(xp);
        previous = xp;
        ++index;
    }
    const auto uses = body->find("usesPerRank");
    if (uses == body->end()) {
        error = "'usesPerRank' is required";
        return false;
    }
    if (uses->is_number_integer()) {
        i32 step = 0;
        if (!ReadInt(*uses, 1, 999, step, error)) {
            error = "'usesPerRank' " + error;
            return false;
        }
        rules.UsesPerRank.push_back(step);
    } else if (uses->is_array()) {
        if (uses->empty() || uses->size() > 98) {
            error = "'usesPerRank' must list from 1 to 98 thresholds";
            return false;
        }
        i32 previous = 0;
        for (const Json& entry : *uses) {
            i32 threshold = 0;
            if (!ReadInt(entry, 1, 999, threshold, error)) {
                error = "'usesPerRank' " + error;
                return false;
            }
            if (threshold <= previous) {
                error = "'usesPerRank' must be strictly increasing";
                return false;
            }
            rules.UsesPerRank.push_back(threshold);
            previous = threshold;
        }
    } else {
        error = "'usesPerRank' must be an integer or a list of integers";
        return false;
    }
    if (!RequireInt(*body, "maxSkillRank", 1, 99, rules.MaxSkillRank, error) ||
        !RequireInt(*body, "powerPerRank", 0, 999, rules.PowerPerRank, error))
        return false;
    parsed.Progress = std::move(rules);
    return true;
}

bool LoadStatuses(const Json& root, Parsed& parsed, std::string& error)
{
    const auto list = root.find("statuses");
    if (list == root.end() || !list->is_array()) {
        error = "'statuses' must be a list";
        return false;
    }
    for (const Json& entry : *list) {
        if (!entry.is_object()) {
            error = "status entries must be objects";
            return false;
        }
        StatusDef status;
        if (!RequireString(entry, "id", status.Id, error))
            return false;
        const std::string where = "status " + Quote(status.Id);
        auto fail = [&](std::string message) {
            error = where + ": " + std::move(message);
            return false;
        };
        if (!IsId(status.Id))
            return fail("id must be letters, digits and _, starting with a letter");
        if (FindId(parsed.Statuses, status.Id) != nullptr)
            return fail("there is another status with this id");
        std::string unknown;
        if (!AllowKeys(entry,
                       {"id", "name", "description", "damagePerTurn", "turns", "maxStacks",
                        "damageTakenPercent", "damageDealtPercent", "skipAction"},
                       unknown))
            return fail("unknown field " + Quote(unknown));
        if (!OptionalInt(entry, "damagePerTurn", 0, 999, status.DamagePerTurn, error) ||
            !RequireInt(entry, "turns", 1, 999, status.Turns, error) ||
            !RequireInt(entry, "maxStacks", 1, 9, status.MaxStacks, error) ||
            !OptionalInt(entry, "damageTakenPercent", 0, 500, status.DamageTakenPercent, error) ||
            !OptionalInt(entry, "damageDealtPercent", 0, 500, status.DamageDealtPercent, error))
            return fail(error);
        const auto skip = entry.find("skipAction");
        if (skip != entry.end()) {
            if (!skip->is_boolean())
                return fail("'skipAction' must be true or false");
            status.SkipAction = skip->get<bool>();
        }
        if (status.DamagePerTurn <= 0 && status.DamageTakenPercent <= 0 &&
            status.DamageDealtPercent <= 0 && !status.SkipAction)
            return fail("needs damage, a percent, or skipAction");
        if (!RequireString(entry, "name", status.Name, error) ||
            !RequireString(entry, "description", status.Description, error))
            return fail(error);
        parsed.Statuses.push_back(std::move(status));
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
    for (const SkillDef& skill : parsed.Skills) {
        const std::string where = "skill " + Quote(skill.Id);
        if (!skill.Status.empty() && FindId(parsed.Statuses, skill.Status) == nullptr) {
            error = where + ": status " + Quote(skill.Status) + " does not exist";
            return false;
        }
        if (!skill.StunWindup.empty() && FindId(parsed.Statuses, skill.StunWindup) == nullptr) {
            error = where + ": status " + Quote(skill.StunWindup) + " does not exist";
            return false;
        }
    }
    for (const EnemyDef& enemy : parsed.Enemies) {
        const std::string where = "enemy " + Quote(enemy.Id);
        if (!SkillRefs(parsed, enemy.Skills, SkillKind::Active, where, error))
            return false;
        for (const std::string& id : enemy.Pattern) {
            bool listed = false;
            for (const std::string& known : enemy.Skills) {
                if (known == id)
                    listed = true;
            }
            if (!listed) {
                error = where + ": pattern skill " + Quote(id) + " is not in skills";
                return false;
            }
        }
        if (!enemy.PhaseStatus.empty() && FindId(parsed.Statuses, enemy.PhaseStatus) == nullptr) {
            error = where + ": status " + Quote(enemy.PhaseStatus) + " does not exist";
            return false;
        }
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
    const Loader loaders[] = {LoadStats,   LoadSkills, LoadStatuses, LoadClasses,
                              LoadEnemies, LoadItems,  LoadCombat,   LoadProgress};
    const char* names[] = {"stats.json",   "skills.json", "statuses.json", "classes.json",
                           "enemies.json", "items.json",  "combat.json",   "progress.json"};

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
        const char* allowedTop[] = {"stats",   "skills", "statuses", "classes",
                                    "enemies", "items",  "combat",   "progress"};
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
        const char* file = "classes.json";
        if (why.compare(0, 6, "enemy ") == 0)
            file = "enemies.json";
        else if (why.compare(0, 6, "skill ") == 0)
            file = "skills.json";
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
    m_Statuses = std::move(parsed.Statuses);
    m_Classes = std::move(parsed.Classes);
    m_Enemies = std::move(parsed.Enemies);
    m_Items = std::move(parsed.Items);
    m_Rules = parsed.Rules;
    m_Progress = std::move(parsed.Progress);
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

const StatusDef* Database::FindStatus(std::string_view id) const
{
    return FindId(m_Statuses, id);
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
