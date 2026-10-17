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
    "stats.json",    "effects.json", "skills.json", "statuses.json", "classes.json",
    "enemies.json",  "encounters.json", "items.json", "shop.json",   "combat.json",
    "progress.json",
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
    std::vector<EffectDef> Effects;
    std::vector<SkillDef> Skills;
    std::vector<StatusDef> Statuses;
    std::vector<ClassDef> Classes;
    std::vector<EnemyDef> Enemies;
    std::vector<EncounterDef> Encounters;
    std::vector<ItemDef> Items;
    ShopDef Shop;
    CombatRules Rules;
    ProgressRules Progress;
};

bool ParseAim(std::string_view text, EffectAim& out)
{
    if (text == "self")
        out = EffectAim::Self;
    else if (text == "target")
        out = EffectAim::Target;
    else if (text == "allEnemies")
        out = EffectAim::AllEnemies;
    else if (text == "randomEnemy")
        out = EffectAim::RandomEnemy;
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

bool ParseTrigger(std::string_view text, EffectTrigger& out)
{
    if (text == "always")
        out = EffectTrigger::Always;
    else if (text == "onUse")
        out = EffectTrigger::OnUse;
    else if (text == "onHit")
        out = EffectTrigger::OnHit;
    else if (text == "onBlock")
        out = EffectTrigger::OnBlock;
    else if (text == "onTurnStart")
        out = EffectTrigger::OnTurnStart;
    else if (text == "onDamaged")
        out = EffectTrigger::OnDamaged;
    else
        return false;
    return true;
}

bool ParseOp(std::string_view text, EffectOp& out)
{
    if (text == "damage")
        out = EffectOp::Damage;
    else if (text == "heal")
        out = EffectOp::Heal;
    else if (text == "guard")
        out = EffectOp::Guard;
    else if (text == "reflect")
        out = EffectOp::Reflect;
    else if (text == "empower")
        out = EffectOp::Empower;
    else if (text == "harden")
        out = EffectOp::Harden;
    else if (text == "stat")
        out = EffectOp::Stat;
    else if (text == "statPerMissingHp")
        out = EffectOp::StatPerMissingHp;
    else if (text == "grantFocus")
        out = EffectOp::GrantFocus;
    else if (text == "applyStatus")
        out = EffectOp::ApplyStatus;
    else if (text == "stunIfWindingUp")
        out = EffectOp::StunIfWindingUp;
    else
        return false;
    return true;
}

bool TriggerAllows(EffectTrigger trigger, EffectOp op)
{
    if (op == EffectOp::Stat || op == EffectOp::StatPerMissingHp)
        return trigger == EffectTrigger::Always;
    if (op == EffectOp::Damage || op == EffectOp::Guard || op == EffectOp::Reflect ||
        op == EffectOp::Empower || op == EffectOp::Harden)
        return trigger == EffectTrigger::OnUse;
    if (op == EffectOp::Heal)
        return trigger == EffectTrigger::OnUse || trigger == EffectTrigger::OnTurnStart;
    if (op == EffectOp::GrantFocus)
        return trigger == EffectTrigger::OnUse || trigger == EffectTrigger::OnBlock ||
               trigger == EffectTrigger::OnTurnStart || trigger == EffectTrigger::OnDamaged;
    if (op == EffectOp::ApplyStatus)
        return trigger == EffectTrigger::OnUse || trigger == EffectTrigger::OnHit ||
               trigger == EffectTrigger::OnDamaged;
    return trigger == EffectTrigger::OnHit;
}

// Fields a fight actually reads for this pair. A field some other pair uses is still rejected
// here: onTurnStart heal ignores scaling, and onDamaged applyStatus ignores every.
bool FieldUsed(EffectTrigger trigger, EffectOp op, std::string_view key)
{
    if (trigger == EffectTrigger::Always && op == EffectOp::Stat)
        return key == "stat" || key == "amount";
    if (trigger == EffectTrigger::Always && op == EffectOp::StatPerMissingHp)
        return key == "stat" || key == "amount" || key == "per" || key == "max";
    if (trigger == EffectTrigger::OnUse && op == EffectOp::Damage)
        return key == "power" || key == "scaling" || key == "target";
    if (trigger == EffectTrigger::OnUse && op == EffectOp::Heal)
        return key == "power" || key == "scaling";
    if (trigger == EffectTrigger::OnTurnStart && op == EffectOp::Heal)
        return key == "power";
    if (trigger == EffectTrigger::OnUse && op == EffectOp::Guard)
        return key == "amount";
    if (trigger == EffectTrigger::OnUse && op == EffectOp::Reflect)
        return key == "percent" || key == "power" || key == "scaling";
    if (trigger == EffectTrigger::OnUse && op == EffectOp::Empower)
        return key == "amount";
    if (trigger == EffectTrigger::OnUse && op == EffectOp::Harden)
        return key == "amount" || key == "turns";
    if (op == EffectOp::GrantFocus &&
        (trigger == EffectTrigger::OnUse || trigger == EffectTrigger::OnBlock ||
         trigger == EffectTrigger::OnTurnStart || trigger == EffectTrigger::OnDamaged))
        return key == "amount";
    if (op == EffectOp::ApplyStatus &&
        (trigger == EffectTrigger::OnUse || trigger == EffectTrigger::OnHit))
        return key == "status" || key == "every" || key == "target";
    if (trigger == EffectTrigger::OnDamaged && op == EffectOp::ApplyStatus)
        return key == "status" || key == "target";
    if (trigger == EffectTrigger::OnHit && op == EffectOp::StunIfWindingUp)
        return key == "status" || key == "target";
    return false;
}

bool EffectKeysOk(const Json& object, EffectTrigger trigger, EffectOp op, std::string& unknown)
{
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.key() == "id" || it.key() == "trigger" || it.key() == "op")
            continue;
        if (!FieldUsed(trigger, op, it.key())) {
            unknown = it.key();
            return false;
        }
    }
    return true;
}

bool KnownEffectField(std::string_view key)
{
    return key == "power" || key == "amount" || key == "percent" || key == "per" || key == "max" ||
           key == "turns" || key == "every" || key == "stat" || key == "scaling" ||
           key == "status" || key == "target";
}

bool HasEffectField(const EffectDef& effect, std::string_view key)
{
    if (key == "power")
        return effect.Power.has_value();
    if (key == "amount")
        return effect.Amount.has_value();
    if (key == "percent")
        return effect.Percent.has_value();
    if (key == "per")
        return effect.Per.has_value();
    if (key == "max")
        return effect.Max.has_value();
    if (key == "turns")
        return effect.Turns.has_value();
    if (key == "every")
        return effect.Every.has_value();
    if (key == "stat")
        return effect.Stat.has_value();
    if (key == "scaling")
        return effect.Scaling.has_value();
    if (key == "target")
        return effect.Aim.has_value();
    return key == "status" && effect.Status.has_value();
}

bool FieldBounds(EffectOp op, std::string_view key, i32& min, i32& max)
{
    if (key == "power") {
        min = 0;
        max = 9999;
        return true;
    }
    if (key == "amount") {
        if (op == EffectOp::Stat) {
            min = -999;
            max = 999;
        } else if (op == EffectOp::Guard) {
            min = 0;
            max = 100;
        } else if (op == EffectOp::GrantFocus || op == EffectOp::StatPerMissingHp) {
            min = 0;
            max = 99;
        } else if (op == EffectOp::Harden) {
            min = 0;
            max = 999;
        } else {
            min = 0;
            max = 9999;
        }
        return true;
    }
    if (key == "percent") {
        min = 0;
        max = 100;
        return true;
    }
    if (key == "per") {
        min = 1;
        max = 100;
        return true;
    }
    if (key == "max") {
        min = 0;
        max = 99;
        return true;
    }
    if (key == "turns") {
        min = 1;
        max = 99;
        return true;
    }
    if (key == "every") {
        min = 1;
        max = 99;
        return true;
    }
    return false;
}

bool AssignEffectField(EffectDef& effect, const std::string& key, const Json& fieldValue,
                       std::string& error)
{
    if (key == "stat" || key == "scaling" || key == "status") {
        if (!fieldValue.is_string()) {
            error = "'" + key + "' must be text";
            return false;
        }
        const std::string text = fieldValue.get<std::string>();
        if (key == "status") {
            if (!IsId(text)) {
                error = "'status' must be an id";
                return false;
            }
            effect.Status = text;
            return true;
        }
        if (FindStatValue(StatBlock{}, text) == nullptr) {
            error = "'" + key + "' " + Quote(text) + " does not exist";
            return false;
        }
        if (key == "stat")
            effect.Stat = text;
        else
            effect.Scaling = text;
        return true;
    }
    if (key == "target") {
        if (!fieldValue.is_string()) {
            error = "'target' must be text";
            return false;
        }
        const std::string text = fieldValue.get<std::string>();
        EffectAim aim = EffectAim::Target;
        if (!ParseAim(text, aim)) {
            error = "'target' must be \"self\", \"target\", \"allEnemies\" or \"randomEnemy\"";
            return false;
        }
        effect.Aim = aim;
        return true;
    }
    i32 min = 0;
    i32 max = 0;
    if (!FieldBounds(effect.Op, key, min, max)) {
        error = "unknown field " + Quote(key);
        return false;
    }
    i32 number = 0;
    std::string why;
    if (!ReadInt(fieldValue, min, max, number, why)) {
        if (effect.Op == EffectOp::Guard && key == "amount")
            error = "'amount' for a guard must be between 0 and 100";
        else
            error = "'" + key + "' " + why;
        return false;
    }
    if (key == "power")
        effect.Power = number;
    else if (key == "amount")
        effect.Amount = number;
    else if (key == "percent")
        effect.Percent = number;
    else if (key == "per")
        effect.Per = number;
    else if (key == "max")
        effect.Max = number;
    else if (key == "turns")
        effect.Turns = number;
    else
        effect.Every = number;
    return true;
}

bool RequireEffectFields(const EffectDef& effect, std::string& error)
{
    auto need = [&](bool present, const char* key) {
        if (present)
            return true;
        error = std::string("'") + key + "' is required";
        return false;
    };
    if (effect.Op == EffectOp::Stat)
        return need(effect.Stat.has_value(), "stat") && need(effect.Amount.has_value(), "amount");
    if (effect.Op == EffectOp::StatPerMissingHp)
        return need(effect.Stat.has_value(), "stat") && need(effect.Amount.has_value(), "amount") &&
               need(effect.Per.has_value(), "per") && need(effect.Max.has_value(), "max");
    if (effect.Op == EffectOp::GrantFocus || effect.Op == EffectOp::Empower ||
        effect.Op == EffectOp::Guard)
        return need(effect.Amount.has_value(), "amount");
    if (effect.Op == EffectOp::Damage || effect.Op == EffectOp::Heal)
        return need(effect.Power.has_value(), "power");
    if (effect.Op == EffectOp::Reflect)
        return need(effect.Percent.has_value(), "percent") &&
               need(effect.Power.has_value(), "power");
    if (effect.Op == EffectOp::Harden)
        return need(effect.Amount.has_value(), "amount") && need(effect.Turns.has_value(), "turns");
    return need(effect.Status.has_value(), "status");
}

// A complete effect object: the recipe in effects.json, or an inline effect on a skill.
bool ParseEffectBody(const Json& entry, EffectDef& effect, std::string& error)
{
    if (!entry.is_object()) {
        error = "effect entries must be objects";
        return false;
    }
    if (!RequireString(entry, "id", effect.Id, error))
        return false;
    if (!IsId(effect.Id)) {
        error = "id must be letters, digits and _, starting with a letter";
        return false;
    }
    std::string triggerText;
    std::string opText;
    if (!RequireString(entry, "trigger", triggerText, error))
        return false;
    if (!ParseTrigger(triggerText, effect.Trigger)) {
        error = "unknown trigger " + Quote(triggerText);
        return false;
    }
    if (!RequireString(entry, "op", opText, error))
        return false;
    if (!ParseOp(opText, effect.Op)) {
        error = "unknown op " + Quote(opText);
        return false;
    }
    if (!TriggerAllows(effect.Trigger, effect.Op)) {
        error = std::string("trigger ") + Quote(triggerText) + " does not use op " + Quote(opText);
        return false;
    }
    std::string unknown;
    if (!EffectKeysOk(entry, effect.Trigger, effect.Op, unknown)) {
        error = "unknown field " + Quote(unknown);
        return false;
    }
    for (auto it = entry.begin(); it != entry.end(); ++it) {
        if (it.key() == "id" || it.key() == "trigger" || it.key() == "op")
            continue;
        if (!AssignEffectField(effect, it.key(), it.value(), error))
            return false;
    }
    return RequireEffectFields(effect, error);
}

bool ReadSkillEffect(const Json& entry, const std::vector<EffectDef>& recipes, EffectDef& out,
                     std::string& error)
{
    if (!entry.is_object()) {
        error = "effect entries must be objects";
        return false;
    }
    if (entry.contains("recipe")) {
        std::string unknown;
        if (!AllowKeys(entry, {"id", "recipe"}, unknown)) {
            error = "unknown field " + Quote(unknown);
            return false;
        }
        std::string localId;
        std::string recipeId;
        if (!RequireString(entry, "id", localId, error) ||
            !RequireString(entry, "recipe", recipeId, error))
            return false;
        if (!IsId(localId)) {
            error = "'id' must be an id";
            return false;
        }
        const EffectDef* recipe = FindId(recipes, recipeId);
        if (recipe == nullptr) {
            error = "effect recipe " + Quote(recipeId) + " does not exist";
            return false;
        }
        out = *recipe;
        out.Id = localId;
        return true;
    }
    std::string why;
    if (!ParseEffectBody(entry, out, why)) {
        if (out.Id.empty())
            error = std::move(why);
        else
            error = "effect " + Quote(out.Id) + ": " + why;
        return false;
    }
    return true;
}

// Layer 2 (a skill) or layer 3 (a grant). Only fields the effect already has can be set.
bool ApplyTune(SkillDef& skill, const Json& tune, std::string& error)
{
    if (!tune.is_object()) {
        error = "'tune' must be an object of effect ids";
        return false;
    }
    for (auto it = tune.begin(); it != tune.end(); ++it) {
        const std::string effectId = it.key();
        EffectDef* effect = nullptr;
        for (EffectDef& candidate : skill.Effects) {
            if (candidate.Id == effectId)
                effect = &candidate;
        }
        if (effect == nullptr) {
            error = "tune effect " + Quote(effectId) + " does not exist";
            return false;
        }
        if (!it->is_object()) {
            error = "tune effect " + Quote(effectId) + " must be an object";
            return false;
        }
        for (auto field = it->begin(); field != it->end(); ++field) {
            const std::string fieldName = field.key();
            if (!KnownEffectField(fieldName) || !HasEffectField(*effect, fieldName) ||
                !FieldUsed(effect->Trigger, effect->Op, fieldName)) {
                error = "tune effect " + Quote(effectId) + ": unknown field " + Quote(fieldName);
                return false;
            }
            std::string why;
            if (!AssignEffectField(*effect, fieldName, field.value(), why)) {
                error = "tune effect " + Quote(effectId) + ": " + why;
                return false;
            }
        }
    }
    return true;
}

bool IsSelfOp(EffectOp op)
{
    return op == EffectOp::Guard || op == EffectOp::Heal || op == EffectOp::Empower ||
           op == EffectOp::Reflect || op == EffectOp::Harden;
}

bool IsFoeOp(EffectOp op)
{
    return op == EffectOp::Damage || op == EffectOp::StunIfWindingUp;
}

bool HasOp(const SkillDef& skill, EffectOp op)
{
    for (const EffectDef& effect : skill.Effects)
        if (effect.Op == op)
            return true;
    return false;
}

bool ReadOneGrant(const Parsed& parsed, const Json& entry, SkillKind expected, bool checkKind,
                  SkillGrant& out, std::string& error)
{
    std::string skillId;
    const Json* tune = nullptr;
    if (entry.is_string()) {
        skillId = entry.get<std::string>();
        if (!IsId(skillId)) {
            error =
                Quote(skillId) + " is not an id (letters, digits and _, starting with a letter)";
            return false;
        }
    } else if (entry.is_object()) {
        std::string unknown;
        if (!AllowKeys(entry, {"skill", "tune"}, unknown)) {
            error = "unknown field " + Quote(unknown);
            return false;
        }
        if (!RequireString(entry, "skill", skillId, error))
            return false;
        if (!IsId(skillId)) {
            error = "'skill' must be an id";
            return false;
        }
        const auto found = entry.find("tune");
        if (found != entry.end())
            tune = &(*found);
    } else {
        error = "a grant must be a skill id or an object";
        return false;
    }
    const SkillDef* catalog = FindId(parsed.Skills, skillId);
    if (catalog == nullptr) {
        error = "skill " + Quote(skillId) + " does not exist";
        return false;
    }
    if (checkKind && catalog->Kind != expected) {
        error = "skill " + Quote(skillId) + " is " + ToString(catalog->Kind) + "; list it under " +
                (expected == SkillKind::Active ? "skills" : "passives");
        return false;
    }
    out.Id = catalog->Id;
    out.Resolved = *catalog;
    if (tune != nullptr && !ApplyTune(out.Resolved, *tune, error))
        return false;
    return true;
}

bool ReadGrantList(const Parsed& parsed, const Json& object, const char* key, SkillKind expected,
                   bool required, bool checkKind, std::vector<SkillGrant>& out, std::string& error)
{
    const auto list = object.find(key);
    if (list == object.end()) {
        if (!required)
            return true;
        error = std::string("'") + key + "' is required";
        return false;
    }
    if (!list->is_array()) {
        error = std::string("'") + key + "' must be a list";
        return false;
    }
    out.clear();
    for (const Json& grantEntry : *list) {
        SkillGrant grant;
        if (!ReadOneGrant(parsed, grantEntry, expected, checkKind, grant, error))
            return false;
        for (const SkillGrant& existing : out) {
            if (existing.Id == grant.Id) {
                error = Quote(grant.Id) + " is listed twice";
                return false;
            }
        }
        out.push_back(std::move(grant));
    }
    return true;
}

bool LoadEffects(const Json& root, Parsed& parsed, std::string& error)
{
    const auto list = root.find("effects");
    if (list == root.end() || !list->is_array()) {
        error = "'effects' must be a list";
        return false;
    }
    for (const Json& entry : *list) {
        EffectDef effect;
        std::string why;
        if (!ParseEffectBody(entry, effect, why)) {
            if (effect.Id.empty())
                error = std::move(why);
            else
                error = "effect " + Quote(effect.Id) + ": " + why;
            return false;
        }
        if (FindId(parsed.Effects, effect.Id) != nullptr) {
            error = "effect " + Quote(effect.Id) + ": there is another effect with this id";
            return false;
        }
        parsed.Effects.push_back(std::move(effect));
    }
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
                           {"id", "name", "description", "kind", "target", "focusCost", "windup",
                            "effects", "tune"},
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
            if (!RequireInt(entry, "focusCost", 0, 99, skill.FocusCost, error))
                return fail(error);
            if (!OptionalInt(entry, "windup", 1, 9, skill.Windup, error))
                return fail(error);
        } else if (!AllowKeys(entry, {"id", "name", "description", "kind", "effects", "tune"},
                              unknown)) {
            return fail("unknown field " + Quote(unknown));
        }

        const auto effects = entry.find("effects");
        if (effects == entry.end() || !effects->is_array() || effects->empty())
            return fail("'effects' must list at least one effect");
        for (const Json& effectEntry : *effects) {
            EffectDef effect;
            std::string why;
            if (!ReadSkillEffect(effectEntry, parsed.Effects, effect, why))
                return fail(why);
            for (const EffectDef& existing : skill.Effects) {
                if (existing.Id == effect.Id)
                    return fail("effect " + Quote(effect.Id) + " is listed twice");
            }
            skill.Effects.push_back(std::move(effect));
        }
        const auto tune = entry.find("tune");
        if (tune != entry.end()) {
            std::string why;
            if (!ApplyTune(skill, *tune, why))
                return fail(why);
        }

        if (skill.Kind == SkillKind::Active) {
            bool selfOp = false;
            bool foeOp = false;
            for (const EffectDef& effect : skill.Effects) {
                if (IsSelfOp(effect.Op))
                    selfOp = true;
                if (IsFoeOp(effect.Op))
                    foeOp = true;
            }
            if (selfOp && foeOp)
                return fail("a skill cannot both guard itself and hit a foe");
            if (selfOp && skill.Target != SkillTarget::Self)
                return fail("'target' must be \"self\"");
            if (foeOp && skill.Target == SkillTarget::Self)
                return fail("'target' must be \"enemy\" or \"allEnemies\"");
            if (skill.Windup > 0 && !HasOp(skill, EffectOp::Damage))
                return fail("'windup' needs a damage effect");
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
                            "growth", "skills", "passives", "starter"},
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
                           {"id", "name", "description", "stats", "growth", "skills", "passives",
                            "starter"},
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

        const auto starterFlag = entry.find("starter");
        if (starterFlag != entry.end()) {
            if (!starterFlag->is_boolean())
                return fail("'starter' must be true or false");
            if (starterFlag->get<bool>() && evolution)
                return fail("an evolution cannot be a starter");
            klass.Starter = starterFlag->get<bool>();
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

        if (!ReadGrantList(parsed, entry, "skills", SkillKind::Active, true, true, klass.Skills,
                           why))
            return fail(why);
        if (!ReadGrantList(parsed, entry, "passives", SkillKind::Passive, true, true,
                           klass.Passives, why))
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
                        "gold", "boss", "pattern", "phaseAtPercent", "phaseStatus", "summon"},
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
        if (!ReadGrantList(parsed, entry, "skills", SkillKind::Active, true, true, enemy.Skills,
                           why))
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
            !OptionalId(entry, "phaseStatus", enemy.PhaseStatus, error))
            return fail(error);
        if (!enemy.PhaseStatus.empty() && enemy.PhaseAtPercent <= 0)
            return fail("'phaseStatus' needs 'phaseAtPercent'");
        const auto summon = entry.find("summon");
        if (summon != entry.end()) {
            if (!summon->is_object())
                return fail("'summon' must be an object");
            std::string summonUnknown;
            if (!AllowKeys(*summon, {"enemy", "every"}, summonUnknown))
                return fail("'summon' unknown field " + Quote(summonUnknown));
            if (!RequireString(*summon, "enemy", enemy.SummonId, error))
                return fail(error);
            if (!IsId(enemy.SummonId))
                return fail("'summon' enemy must be an id");
            if (enemy.SummonId == enemy.Id)
                return fail("'summon' cannot name this enemy");
            if (!RequireInt(*summon, "every", 1, 99, enemy.SummonEvery, error))
                return fail(error);
            if (enemy.PhaseAtPercent <= 0)
                return fail("'summon' needs 'phaseAtPercent'");
        }
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

bool LoadEncounters(const Json& root, Parsed& parsed, std::string& error)
{
    const auto list = root.find("encounters");
    if (list == root.end() || !list->is_array() || list->empty()) {
        error = "'encounters' must be a list with at least one encounter";
        return false;
    }
    for (const Json& entry : *list) {
        if (!entry.is_object()) {
            error = "encounter entries must be objects";
            return false;
        }
        EncounterDef encounter;
        if (!RequireString(entry, "id", encounter.Id, error))
            return false;
        const std::string where = "encounter " + Quote(encounter.Id);
        auto fail = [&](std::string message) {
            error = where + ": " + std::move(message);
            return false;
        };
        if (!IsId(encounter.Id))
            return fail("id must be letters, digits and _, starting with a letter");
        if (FindId(parsed.Encounters, encounter.Id) != nullptr)
            return fail("there is another encounter with this id");
        std::string unknown;
        if (!AllowKeys(entry, {"id", "name", "enemies"}, unknown))
            return fail("unknown field " + Quote(unknown));
        std::string listed;
        if (!ReadIdList(entry, "enemies", encounter.Enemies, listed, true))
            return fail(listed);
        if (encounter.Enemies.empty())
            return fail("'enemies' must list at least one enemy");
        if (encounter.Enemies.size() > 3)
            return fail("'enemies' cannot list more than 3");
        for (const std::string& enemyId : encounter.Enemies) {
            if (FindId(parsed.Enemies, enemyId) == nullptr)
                return fail("enemy " + Quote(enemyId) + " does not exist");
        }
        if (!RequireString(entry, "name", encounter.Name, error))
            return fail(error);
        parsed.Encounters.push_back(std::move(encounter));
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
        if (!AllowKeys(entry,
                       {"id", "name", "description", "slot", "bonus", "value", "twoHanded", "heal",
                        "focus", "grants"},
                       unknown))
            return fail("unknown field " + Quote(unknown));
        std::string slot;
        if (!RequireString(entry, "slot", slot, error))
            return fail(error);
        if (slot == "weapon")
            item.Slot = GearSlot::Weapon;
        else if (slot == "offhand")
            item.Slot = GearSlot::Offhand;
        else if (slot == "armor")
            item.Slot = GearSlot::Armor;
        else if (slot == "accessory")
            item.Slot = GearSlot::Accessory;
        else if (slot == "consumable")
            item.Slot = GearSlot::Consumable;
        else
            return fail("'slot' must be \"weapon\", \"offhand\", \"armor\", \"accessory\" or "
                        "\"consumable\"");
        if (item.Slot == GearSlot::Consumable) {
            if (!Absent(entry, "bonus", "a consumable", error) ||
                !Absent(entry, "twoHanded", "a consumable", error))
                return fail(error);
            if (!OptionalInt(entry, "heal", 0, 9999, item.Heal, error) ||
                !OptionalInt(entry, "focus", 0, 99, item.Focus, error))
                return fail(error);
            if (item.Heal <= 0 && item.Focus <= 0)
                return fail("a consumable needs 'heal' or 'focus'");
        } else {
            if (!Absent(entry, "heal", ToString(item.Slot), error) ||
                !Absent(entry, "focus", ToString(item.Slot), error))
                return fail(error);
            if (item.Slot == GearSlot::Weapon) {
                const auto hands = entry.find("twoHanded");
                if (hands != entry.end()) {
                    if (!hands->is_boolean())
                        return fail("'twoHanded' must be true or false");
                    item.TwoHanded = hands->get<bool>();
                }
            } else if (!Absent(entry, "twoHanded", ToString(item.Slot), error)) {
                return fail(error);
            }
            const auto bonus = entry.find("bonus");
            if (bonus == entry.end())
                return fail("'bonus' is required");
            std::string why;
            if (!ReadStatBlock(*bonus, false, item.Bonus, why))
                return fail("'bonus' " + why);
        }
        if (!RequireInt(entry, "value", 0, 999999, item.Value, error))
            return fail(error);
        std::string grantError;
        if (!ReadGrantList(parsed, entry, "grants", SkillKind::Active, false, false, item.Grants,
                           grantError))
            return fail(grantError);
        if (!RequireString(entry, "name", item.Name, error) ||
            !RequireString(entry, "description", item.Description, error))
            return fail(error);
        parsed.Items.push_back(std::move(item));
    }
    return true;
}

bool LoadShop(const Json& root, Parsed& parsed, std::string& error)
{
    const auto body = root.find("shop");
    if (body == root.end() || !body->is_object()) {
        error = "'shop' must be an object";
        return false;
    }
    std::string unknown;
    if (!AllowKeys(*body,
                   {"id", "name", "keeper", "bagSlots", "stackSize", "sellNumerator",
                    "sellDenominator", "stock"},
                   unknown)) {
        error = "unknown field " + Quote(unknown);
        return false;
    }
    ShopDef shop;
    if (!RequireString(*body, "id", shop.Id, error))
        return false;
    if (!IsId(shop.Id)) {
        error = "shop id must be letters, digits and _, starting with a letter";
        return false;
    }
    if (!RequireString(*body, "name", shop.Name, error) ||
        !RequireString(*body, "keeper", shop.Keeper, error))
        return false;
    if (!RequireInt(*body, "bagSlots", 1, 24, shop.BagSlots, error) ||
        !RequireInt(*body, "stackSize", 1, 99, shop.StackSize, error) ||
        !RequireInt(*body, "sellNumerator", 0, 999, shop.SellNumerator, error) ||
        !RequireInt(*body, "sellDenominator", 1, 999, shop.SellDenominator, error))
        return false;
    if (!ReadIdList(*body, "stock", shop.Stock, error))
        return false;
    if (shop.Stock.empty()) {
        error = "'stock' must list at least one item";
        return false;
    }
    parsed.Shop = std::move(shop);
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
        i32 priorThreshold = 0;
        for (const Json& entry : *uses) {
            i32 threshold = 0;
            if (!ReadInt(entry, 1, 999, threshold, error)) {
                error = "'usesPerRank' " + error;
                return false;
            }
            if (threshold <= priorThreshold) {
                error = "'usesPerRank' must be strictly increasing";
                return false;
            }
            rules.UsesPerRank.push_back(threshold);
            priorThreshold = threshold;
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

bool StatusesExist(const Parsed& parsed, const std::vector<EffectDef>& effects,
                   const std::string& where, std::string& error)
{
    for (const EffectDef& effect : effects) {
        if (!effect.Status.has_value())
            continue;
        if (FindId(parsed.Statuses, *effect.Status) == nullptr) {
            error = where + ": status " + Quote(*effect.Status) + " does not exist";
            return false;
        }
    }
    return true;
}

bool GrantsKnown(const Parsed& parsed, const std::vector<SkillGrant>& grants,
                 const std::string& where, std::string& error)
{
    for (const SkillGrant& grant : grants) {
        if (!StatusesExist(parsed, grant.Resolved.Effects, where, error))
            return false;
    }
    return true;
}

bool CrossCheck(const Parsed& parsed, std::string& error)
{
    for (const EffectDef& recipe : parsed.Effects) {
        if (!recipe.Status.has_value())
            continue;
        if (FindId(parsed.Statuses, *recipe.Status) == nullptr) {
            error = "effect " + Quote(recipe.Id) + ": status " + Quote(*recipe.Status) +
                    " does not exist";
            return false;
        }
    }
    for (const SkillDef& skill : parsed.Skills) {
        if (!StatusesExist(parsed, skill.Effects, "skill " + Quote(skill.Id), error))
            return false;
    }
    for (const ClassDef& klass : parsed.Classes) {
        const std::string classWhere = "class " + Quote(klass.Id);
        if (!GrantsKnown(parsed, klass.Skills, classWhere, error) ||
            !GrantsKnown(parsed, klass.Passives, classWhere, error))
            return false;
        if (!klass.IsEvolution())
            continue;
        const ClassDef* parent = FindId(parsed.Classes, klass.EvolvesFrom);
        if (parent == nullptr) {
            error = classWhere + ": evolvesFrom " + Quote(klass.EvolvesFrom) + " does not exist";
            return false;
        }
        if (parent->IsEvolution()) {
            error = classWhere + ": evolvesFrom " + Quote(klass.EvolvesFrom) +
                    " is an evolution; evolve from the base class";
            return false;
        }
        for (const SkillGrant& grant : klass.Skills) {
            for (const SkillGrant& parentGrant : parent->Skills) {
                if (grant.Id == parentGrant.Id) {
                    error = classWhere + ": " + Quote(parent->Id) + " already has skill " +
                            Quote(grant.Id);
                    return false;
                }
            }
        }
        for (const SkillGrant& grant : klass.Passives) {
            for (const SkillGrant& parentGrant : parent->Passives) {
                if (grant.Id == parentGrant.Id) {
                    error = classWhere + ": " + Quote(parent->Id) + " already has passive " +
                            Quote(grant.Id);
                    return false;
                }
            }
        }
    }
    bool anyStarter = false;
    for (const ClassDef& base : parsed.Classes) {
        if (base.Starter)
            anyStarter = true;
    }
    if (!anyStarter) {
        error = "needs a starter class";
        return false;
    }
    for (const EnemyDef& enemy : parsed.Enemies) {
        const std::string enemyWhere = "enemy " + Quote(enemy.Id);
        if (!GrantsKnown(parsed, enemy.Skills, enemyWhere, error))
            return false;
        for (const std::string& patternId : enemy.Pattern) {
            bool listed = false;
            for (const SkillGrant& grant : enemy.Skills) {
                if (grant.Id == patternId)
                    listed = true;
            }
            if (!listed) {
                error = enemyWhere + ": pattern skill " + Quote(patternId) + " is not in skills";
                return false;
            }
        }
        if (!enemy.PhaseStatus.empty() && FindId(parsed.Statuses, enemy.PhaseStatus) == nullptr) {
            error = enemyWhere + ": status " + Quote(enemy.PhaseStatus) + " does not exist";
            return false;
        }
        if (!enemy.SummonId.empty() && FindId(parsed.Enemies, enemy.SummonId) == nullptr) {
            error = enemyWhere + ": summon " + Quote(enemy.SummonId) + " does not exist";
            return false;
        }
    }
    for (const ItemDef& item : parsed.Items) {
        if (!GrantsKnown(parsed, item.Grants, "item " + Quote(item.Id), error))
            return false;
    }
    for (const std::string& stocked : parsed.Shop.Stock) {
        if (FindId(parsed.Items, stocked) == nullptr) {
            error = "shop stock " + Quote(stocked) + " does not exist";
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
    const Loader loaders[] = {LoadStats,     LoadEffects, LoadSkills,   LoadStatuses, LoadClasses,
                              LoadEnemies,  LoadEncounters, LoadItems, LoadShop,     LoadCombat,
                              LoadProgress};
    const char* names[] = {"stats.json",     "effects.json", "skills.json", "statuses.json",
                           "classes.json",   "enemies.json", "encounters.json", "items.json",
                           "shop.json",      "combat.json",  "progress.json"};

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
        const char* allowedTop[] = {"stats",    "effects", "skills", "statuses", "classes",
                                    "enemies", "encounters", "items", "shop",     "combat",
                                    "progress"};
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
        else if (why.compare(0, 10, "encounter ") == 0)
            file = "encounters.json";
        else if (why.compare(0, 6, "skill ") == 0)
            file = "skills.json";
        else if (why.compare(0, 7, "effect ") == 0)
            file = "effects.json";
        else if (why.compare(0, 5, "item ") == 0)
            file = "items.json";
        else if (why.compare(0, 5, "shop ") == 0)
            file = "shop.json";
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
    m_Encounters = std::move(parsed.Encounters);
    m_Items = std::move(parsed.Items);
    m_Shop = std::move(parsed.Shop);
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

const EncounterDef* Database::FindEncounter(std::string_view id) const
{
    return FindId(m_Encounters, id);
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

std::vector<const ClassDef*> Database::Starters() const
{
    std::vector<const ClassDef*> out;
    for (const ClassDef& klass : m_Classes) {
        if (klass.Starter)
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
    const std::vector<SkillGrant>& grants = passives ? klass.Passives : klass.Skills;
    for (const SkillGrant& grant : grants)
        out.push_back(&grant.Resolved);
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
