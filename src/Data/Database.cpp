// Reading data/*.json. nlohmann::json stays in this file: the public headers only have the structs.

#include "Ascension/Data/Database.h"

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <utility>

#include "Ascension/Data/Talk.h"

#include <nlohmann/json.hpp>

#include "Emerald/Core/Log.h"

namespace Ascension {

namespace {

using Json = nlohmann::json;
using FileTime = std::optional<std::filesystem::file_time_type>;

constexpr const char* kFileNames[] = {
    "stats.json",   "effects.json", "skills.json",     "statuses.json",
    "classes.json", "enemies.json", "encounters.json", "items.json",
    "shop.json",    "combat.json",  "progress.json",   "rarities.json",
    "tiles.json",
};

// Dev builds read the source assets/icons when that sheet is there. A packaged build, where the
// source path is not on disk, reads assets/icons next to the data directory.
std::filesystem::path IconSheetFor(const std::filesystem::path& dataDirectory)
{
#ifdef ASCENSION_ASSETS_DIR
    const std::filesystem::path source =
        std::filesystem::path(ASCENSION_ASSETS_DIR) / "icons" / "icons_sheet.json";
    std::error_code missing;
    if (std::filesystem::is_regular_file(source, missing))
        return source;
#endif
    return dataDirectory.parent_path() / "assets" / "icons" / "icons_sheet.json";
}

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

// Missing key leaves `out` unchanged. A present value must be true or false.
bool OptionalBool(const Json& object, const char* key, bool& out, std::string& error)
{
    const auto it = object.find(key);
    if (it == object.end())
        return true;
    if (!it->is_boolean()) {
        error = std::string("'") + key + "' must be true or false";
        return false;
    }
    out = it->get<bool>();
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
    std::vector<RarityDef> Rarities;
    std::vector<IconDef> Icons;
    ShopDef Shop;
    std::vector<DialogueDef> Dialogues;
    std::vector<TileDef> Tiles;
    std::vector<MapDef> Maps;
    CombatRules Rules;
    ProgressRules Progress;
    std::vector<std::string> HigherFields;
    std::vector<std::string> LowerFields;
};

// Numeric fields a tune may set. The direction is not stored here: effects.json "better"
// names it once per field, and a tunable numeric field with no direction is rejected.
// Non-numeric tune fields (target, stat, scaling, status) are absent on purpose. When two
// sources grant one skill, those keep the earliest source.
struct EffectNumber {
    const char* Name;
    std::optional<i32> EffectDef::*Slot;
};

struct SkillNumber {
    const char* Name;
    i32 SkillDef::*Slot;
};

constexpr EffectNumber kEffectNumbers[] = {
    {"power", &EffectDef::Power}, {"amount", &EffectDef::Amount}, {"percent", &EffectDef::Percent},
    {"per", &EffectDef::Per},     {"max", &EffectDef::Max},       {"turns", &EffectDef::Turns},
    {"every", &EffectDef::Every},
};

constexpr SkillNumber kSkillNumbers[] = {
    {"focusCost", &SkillDef::FocusCost},
};

bool DirectionOf(const std::vector<std::string>& higher, const std::vector<std::string>& lower,
                 std::string_view field, bool& higherWins)
{
    for (const std::string& name : higher) {
        if (name == field) {
            higherWins = true;
            return true;
        }
    }
    for (const std::string& name : lower) {
        if (name == field) {
            higherWins = false;
            return true;
        }
    }
    return false;
}

bool IncomingWins(i32 incoming, i32 heldValue, bool higher)
{
    if (higher)
        return incoming > heldValue;
    return incoming < heldValue;
}

void MergeEffects(EffectDef& destination, const EffectDef& incoming,
                  const std::vector<std::string>& higher, const std::vector<std::string>& lower)
{
    for (const EffectNumber& field : kEffectNumbers) {
        std::optional<i32>& slot = destination.*(field.Slot);
        const std::optional<i32>& next = incoming.*(field.Slot);
        if (!slot.has_value() || !next.has_value())
            continue;
        bool higherWins = false;
        if (!DirectionOf(higher, lower, field.Name, higherWins))
            continue;
        if (IncomingWins(*next, *slot, higherWins))
            slot = *next;
    }
}

// Numeric fields take the better value. Everything else (name, target, stat, scaling, status,
// windup) stays on `held`, which is the earliest source.
void MergeSkill(SkillDef& held, const SkillDef& incoming, const std::vector<std::string>& higher,
                const std::vector<std::string>& lower)
{
    for (EffectDef& destination : held.Effects) {
        const EffectDef* incomingEffect = nullptr;
        for (const EffectDef& candidate : incoming.Effects) {
            if (candidate.Id == destination.Id)
                incomingEffect = &candidate;
        }
        if (incomingEffect != nullptr)
            MergeEffects(destination, *incomingEffect, higher, lower);
    }
    for (const SkillNumber& field : kSkillNumbers) {
        i32& slot = held.*(field.Slot);
        const i32 next = incoming.*(field.Slot);
        bool higherWins = false;
        if (!DirectionOf(higher, lower, field.Name, higherWins))
            continue;
        if (IncomingWins(next, slot, higherWins))
            slot = next;
    }
}

void PushOrigin(HeroGrant& held, GrantSource source, const std::string& sourceId)
{
    for (const GrantOrigin& origin : held.Origins) {
        if (origin.Source == source && origin.Id == sourceId)
            return;
    }
    GrantOrigin origin;
    origin.Source = source;
    origin.Id = sourceId;
    held.Origins.push_back(std::move(origin));
}

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
    else if (text == "lowestHpAlly")
        out = EffectAim::LowestHpAlly;
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

template <typename T> const T* FindId(std::span<const T> items, std::string_view id)
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
    if (key == "ifTargetHas" || key == "consume")
        return trigger == EffectTrigger::OnUse && op == EffectOp::Damage;
    if (trigger == EffectTrigger::Always && op == EffectOp::Stat)
        return key == "stat" || key == "amount";
    if (trigger == EffectTrigger::Always && op == EffectOp::StatPerMissingHp)
        return key == "stat" || key == "amount" || key == "per" || key == "max";
    if (trigger == EffectTrigger::OnUse && op == EffectOp::Damage)
        return key == "power" || key == "scaling" || key == "target";
    if (trigger == EffectTrigger::OnUse && op == EffectOp::Heal)
        return key == "power" || key == "scaling" || key == "target";
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
    if (key == "consume") {
        if (!fieldValue.is_boolean()) {
            error = "'consume' must be true or false";
            return false;
        }
        effect.Consume = fieldValue.get<bool>();
        return true;
    }
    if (key == "ifTargetHas") {
        if (!fieldValue.is_string()) {
            error = "'ifTargetHas' must be a status id";
            return false;
        }
        const std::string text = fieldValue.get<std::string>();
        if (!IsId(text)) {
            error = "'ifTargetHas' must be a status id";
            return false;
        }
        effect.IfTargetHas = text;
        return true;
    }
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
            error = "'target' must be \"self\", \"target\", \"allEnemies\", \"randomEnemy\" "
                    "or \"lowestHpAlly\"";
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
    if (effect.Consume && !effect.IfTargetHas.has_value()) {
        error = "'consume' needs 'ifTargetHas'";
        return false;
    }
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

bool IsSkillTuneKey(std::string_view key)
{
    return key == "focusCost" || key == "target";
}

bool EffectNamed(const SkillDef& skill, std::string_view id)
{
    for (const EffectDef& effect : skill.Effects)
        if (effect.Id == id)
            return true;
    return false;
}

// focusCost and target sit on the skill. An effect that happens to use that id still wins,
// and the value is read as that effect's tune.
bool AssignSkillTune(SkillDef& skill, const std::string& key, const Json& value, std::string& error)
{
    if (key == "focusCost") {
        i32 cost = 0;
        std::string why;
        if (!ReadInt(value, 0, 99, cost, why)) {
            error = "'focusCost' " + why;
            return false;
        }
        skill.FocusCost = cost;
        return true;
    }
    if (skill.Kind != SkillKind::Active) {
        error = "tune field " + Quote(key) + " is not used on a passive";
        return false;
    }
    if (!value.is_string()) {
        error = "'target' must be text";
        return false;
    }
    const std::string text = value.get<std::string>();
    if (text == "self")
        skill.Target = SkillTarget::Self;
    else if (text == "enemy")
        skill.Target = SkillTarget::Enemy;
    else if (text == "allEnemies")
        skill.Target = SkillTarget::AllEnemies;
    else {
        error = "'target' must be \"self\", \"enemy\" or \"allEnemies\"";
        return false;
    }
    return true;
}

// Layer 2 (a skill) or layer 3 (a grant). Only fields the effect already has can be set.
// A key of focusCost or target, when no effect uses that id, sets the skill itself.
bool ApplyTune(SkillDef& skill, const Json& tune, std::string& error)
{
    if (!tune.is_object()) {
        error = "'tune' must be an object of effect ids";
        return false;
    }
    for (auto it = tune.begin(); it != tune.end(); ++it) {
        const std::string effectId = it.key();
        if (IsSkillTuneKey(effectId) && !EffectNamed(skill, effectId)) {
            if (!AssignSkillTune(skill, effectId, *it, error))
                return false;
            continue;
        }
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

bool CheckSkillShape(const SkillDef& skill, std::string& error)
{
    if (skill.Kind != SkillKind::Active)
        return true;
    bool selfOp = false;
    bool foeOp = false;
    for (const EffectDef& effect : skill.Effects) {
        if (IsSelfOp(effect.Op))
            selfOp = true;
        if (IsFoeOp(effect.Op))
            foeOp = true;
    }
    if (selfOp && foeOp) {
        error = "a skill cannot both guard itself and hit a foe";
        return false;
    }
    if (selfOp && skill.Target != SkillTarget::Self) {
        error = "'target' must be \"self\"";
        return false;
    }
    if (foeOp && skill.Target == SkillTarget::Self) {
        error = "'target' must be \"enemy\" or \"allEnemies\"";
        return false;
    }
    if (skill.Windup > 0 && !HasOp(skill, EffectOp::Damage)) {
        error = "'windup' needs a damage effect";
        return false;
    }
    return true;
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
        if (found != entry.end()) {
            tune = &(*found);
            out.Tuned = true;
        }
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
    if (!CheckSkillShape(out.Resolved, error))
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

bool RequireDirection(const Json& table, const char* name, Parsed& parsed, std::string& error)
{
    const auto found = table.find(name);
    if (found == table.end()) {
        error = "tunable field " + Quote(name) + " has no better direction";
        return false;
    }
    if (!found->is_string()) {
        error = "tunable field " + Quote(name) + " better must be \"higher\" or \"lower\"";
        return false;
    }
    const std::string direction = found->get<std::string>();
    if (direction == "higher")
        parsed.HigherFields.emplace_back(name);
    else if (direction == "lower")
        parsed.LowerFields.emplace_back(name);
    else {
        error = "tunable field " + Quote(name) + " better must be \"higher\" or \"lower\"";
        return false;
    }
    return true;
}

bool KnownTunable(std::string_view name)
{
    for (const EffectNumber& field : kEffectNumbers) {
        if (name == field.Name)
            return true;
    }
    for (const SkillNumber& field : kSkillNumbers) {
        if (name == field.Name)
            return true;
    }
    return false;
}

// "better" is the only place a field is called higher or lower. Every numeric tune field
// has to be listed. A missing name is a load error, not a guess.
bool LoadBetter(const Json& root, Parsed& parsed, std::string& error)
{
    const auto table = root.find("better");
    if (table == root.end() || !table->is_object()) {
        error = "'better' must be an object of field directions";
        return false;
    }
    for (const EffectNumber& field : kEffectNumbers) {
        if (!RequireDirection(*table, field.Name, parsed, error))
            return false;
    }
    for (const SkillNumber& field : kSkillNumbers) {
        if (!RequireDirection(*table, field.Name, parsed, error))
            return false;
    }
    for (auto it = table->begin(); it != table->end(); ++it) {
        if (!KnownTunable(it.key())) {
            error = "unknown tunable field " + Quote(it.key());
            return false;
        }
    }
    return true;
}

bool LoadEffects(const Json& root, Parsed& parsed, std::string& error)
{
    if (!LoadBetter(root, parsed, error))
        return false;
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

        std::string shape;
        if (!CheckSkillShape(skill, shape))
            return fail(shape);

        if (!RequireString(entry, "name", skill.Name, error) ||
            !RequireString(entry, "description", skill.Description, error))
            return fail(error);
        parsed.Skills.push_back(std::move(skill));
    }
    return true;
}

bool GrantsId(const std::vector<SkillGrant>& grants, std::string_view id)
{
    for (const SkillGrant& grant : grants) {
        if (grant.Id == id)
            return true;
    }
    return false;
}

bool OfferedBy(const std::vector<SkillPick>& picks, std::string_view id)
{
    for (const SkillPick& pick : picks) {
        if (GrantsId(pick.Options, id))
            return true;
    }
    return false;
}

// An option the class or its parent already grants must carry a tune, or keeping it adds
// nothing. The same skill id is not offered twice along the class chain.
bool CheckPickOptions(const ClassDef& klass, const ClassDef* parent, const std::string& where,
                      std::string& error)
{
    for (const SkillPick& pick : klass.Picks) {
        for (const SkillGrant& option : pick.Options) {
            const bool held = GrantsId(klass.Skills, option.Id) ||
                              (parent != nullptr && GrantsId(parent->Skills, option.Id));
            if (held && !option.Tuned) {
                error = where + ": pick at level " + std::to_string(pick.Level) + ": " +
                        Quote(option.Id) + " is already a class skill; give the option a tune";
                return false;
            }
            if (parent != nullptr && OfferedBy(parent->Picks, option.Id)) {
                error = where + ": pick at level " + std::to_string(pick.Level) + ": " +
                        Quote(option.Id) + " is offered by an earlier pick";
                return false;
            }
        }
    }
    return true;
}

// "picks": [{ "level": 6, "options": ["riposte", { "skill": "shield_wall", "tune": {...} }] }].
// Levels rise, and an evolution's picks come after its evolve level. Each pick offers two or
// three active grants. An id appears in one pick of this class only. The tune check against
// the parent waits for the whole file (CheckPickOptions), since the parent may come later.
bool ReadPicks(const Parsed& parsed, const Json& entry, ClassDef& klass, std::string& error)
{
    const auto list = entry.find("picks");
    if (list == entry.end())
        return true;
    if (!list->is_array()) {
        error = "'picks' must be a list";
        return false;
    }
    i32 lastLevel = klass.IsEvolution() ? klass.EvolveLevel : 1;
    for (const Json& pickEntry : *list) {
        if (!pickEntry.is_object()) {
            error = "a pick must be an object";
            return false;
        }
        std::string unknown;
        if (!AllowKeys(pickEntry, {"level", "options"}, unknown)) {
            error = "pick: unknown field " + Quote(unknown);
            return false;
        }
        SkillPick pick;
        if (!RequireInt(pickEntry, "level", lastLevel + 1, kMaxLevel, pick.Level, error)) {
            error = "pick: " + error + " (picks rise in level, after the evolve level)";
            return false;
        }
        const std::string where = "pick at level " + std::to_string(pick.Level) + ": ";
        if (!ReadGrantList(parsed, pickEntry, "options", SkillKind::Active, true, true,
                           pick.Options, error)) {
            error = where + error;
            return false;
        }
        if (pick.Options.size() < 2 || pick.Options.size() > 3) {
            error = where + "'options' must list two or three skills";
            return false;
        }
        for (const SkillGrant& option : pick.Options) {
            if (OfferedBy(klass.Picks, option.Id)) {
                error = where + Quote(option.Id) + " is offered by an earlier pick";
                return false;
            }
        }
        lastLevel = pick.Level;
        klass.Picks.push_back(std::move(pick));
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
                            "growth", "skills", "passives", "picks", "starter"},
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
                            "picks", "starter"},
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
        if (!ReadPicks(parsed, entry, klass, why))
            return fail(why);

        if (!RequireString(entry, "name", klass.Name, error) ||
            !RequireString(entry, "description", klass.Description, error))
            return fail(error);
        parsed.Classes.push_back(std::move(klass));
    }
    return true;
}

bool OptionalRange(const Json& object, const char* key, i32 min, i32 max, std::optional<i32>& out,
                   std::string& error)
{
    const auto it = object.find(key);
    if (it == object.end())
        return true;
    i32 value = 0;
    if (!ReadInt(*it, min, max, value, error)) {
        error = std::string("'") + key + "' " + error;
        return false;
    }
    out = value;
    return true;
}

bool OptionalPhase(const Json& object, std::optional<i32>& out, std::string& error)
{
    const auto it = object.find("phase");
    if (it == object.end())
        return true;
    i32 value = 0;
    if (!ReadInt(*it, 1, 2, value, error)) {
        error = "'phase' " + error;
        return false;
    }
    out = value;
    return true;
}

bool OptionalStatusId(const Parsed& parsed, const Json& object, const char* key, std::string& out,
                      std::string& error)
{
    if (!OptionalId(object, key, out, error))
        return false;
    if (out.empty() || FindId(parsed.Statuses, out) != nullptr)
        return true;
    error = std::string("'") + key + "' status " + Quote(out) + " does not exist";
    return false;
}

bool GrantHas(const std::vector<SkillGrant>& skills, std::string_view id)
{
    for (const SkillGrant& grant : skills) {
        if (grant.Id == id)
            return true;
    }
    return false;
}

bool PatternHas(const std::vector<std::string>& pattern, std::string_view id)
{
    for (const std::string& step : pattern) {
        if (step == id)
            return true;
    }
    return false;
}

// `ai` is optional. Each rule is an ordered interrupt. The loader checks the skill and
// status ids here; the fight only reads the list.
bool ReadAi(const Parsed& parsed, const Json& entry, EnemyDef& enemy, std::string& error)
{
    const auto ai = entry.find("ai");
    if (ai == entry.end())
        return true;
    if (!ai->is_array()) {
        error = "'ai' must be a list";
        return false;
    }
    i32 index = 0;
    for (const Json& ruleJson : *ai) {
        ++index;
        const std::string where = "'ai' rule " + std::to_string(index);
        if (!ruleJson.is_object()) {
            error = where + " must be an object";
            return false;
        }
        std::string unknown;
        if (!AllowKeys(ruleJson, {"if", "do", "skill"}, unknown)) {
            error = where + ": unknown field " + Quote(unknown);
            return false;
        }
        const auto condition = ruleJson.find("if");
        if (condition == ruleJson.end() || !condition->is_object()) {
            error = where + ": 'if' must be an object";
            return false;
        }
        if (!AllowKeys(*condition,
                       {"selfHpAtMost", "selfHpAtLeast", "targetHpAtMost", "targetHpAtLeast",
                        "allyHpAtMost", "allyCountAtMost", "allyCountAtLeast", "selfStatus",
                        "selfStatusAbsent", "targetStatus", "targetStatusAbsent",
                        "targetGuardAtLeast", "selfHardenAtLeast", "phase", "next"},
                       unknown)) {
            error = where + ": unknown field " + Quote(unknown);
            return false;
        }
        AiRule rule;
        if (!OptionalRange(*condition, "selfHpAtMost", 0, 100, rule.If.SelfHpAtMost, error) ||
            !OptionalRange(*condition, "selfHpAtLeast", 0, 100, rule.If.SelfHpAtLeast, error) ||
            !OptionalRange(*condition, "targetHpAtMost", 0, 100, rule.If.TargetHpAtMost, error) ||
            !OptionalRange(*condition, "targetHpAtLeast", 0, 100, rule.If.TargetHpAtLeast, error) ||
            !OptionalRange(*condition, "allyHpAtMost", 0, 100, rule.If.AllyHpAtMost, error) ||
            !OptionalRange(*condition, "allyCountAtMost", 0, 9, rule.If.AllyCountAtMost, error) ||
            !OptionalRange(*condition, "allyCountAtLeast", 0, 9, rule.If.AllyCountAtLeast,
                           error) ||
            !OptionalRange(*condition, "targetGuardAtLeast", 0, 100, rule.If.TargetGuardAtLeast,
                           error) ||
            !OptionalRange(*condition, "selfHardenAtLeast", 0, 9999, rule.If.SelfHardenAtLeast,
                           error) ||
            !OptionalPhase(*condition, rule.If.Phase, error) ||
            !OptionalStatusId(parsed, *condition, "selfStatus", rule.If.SelfStatus, error) ||
            !OptionalStatusId(parsed, *condition, "selfStatusAbsent", rule.If.SelfStatusAbsent,
                              error) ||
            !OptionalStatusId(parsed, *condition, "targetStatus", rule.If.TargetStatus, error) ||
            !OptionalStatusId(parsed, *condition, "targetStatusAbsent", rule.If.TargetStatusAbsent,
                              error) ||
            !OptionalId(*condition, "next", rule.If.Next, error)) {
            error = where + ": " + error;
            return false;
        }
        if (!rule.If.Next.empty() && !PatternHas(enemy.Pattern, rule.If.Next)) {
            error = where + ": 'next' skill " + Quote(rule.If.Next) + " is not in the pattern";
            return false;
        }
        std::string action;
        if (!RequireString(ruleJson, "do", action, error)) {
            error = where + ": " + error;
            return false;
        }
        if (action == "pattern")
            rule.Action = AiAction::Pattern;
        else if (action == "skill")
            rule.Action = AiAction::Skill;
        else {
            error = where + ": 'do' must be \"skill\" or \"pattern\"";
            return false;
        }
        if (rule.Action == AiAction::Skill) {
            if (!RequireString(ruleJson, "skill", rule.Skill, error)) {
                error = where + ": " + error;
                return false;
            }
            if (!IsId(rule.Skill)) {
                error = where + ": 'skill' must be an id";
                return false;
            }
            if (!GrantHas(enemy.Skills, rule.Skill)) {
                error = where + ": skill " + Quote(rule.Skill) + " is not in skills";
                return false;
            }
        } else if (ruleJson.contains("skill")) {
            error = where + ": 'skill' is only used when 'do' is \"skill\"";
            return false;
        }
        enemy.Ai.push_back(std::move(rule));
    }
    return true;
}

bool ParseColor(std::string_view text, u8& red, u8& green, u8& blue, std::string& error);

// A drops entry is an object: item is required, message is an optional victory log line.
// A missing message stays empty, and the fight log uses "Found <item name>."
bool ReadDrops(const Json& entry, std::vector<DropDef>& out, std::string& error)
{
    const auto it = entry.find("drops");
    if (it == entry.end()) {
        error = "'drops' is required";
        return false;
    }
    if (!it->is_array()) {
        error = "'drops' must be a list";
        return false;
    }
    out.clear();
    for (const Json& dropJson : *it) {
        if (!dropJson.is_object()) {
            error = "'drops' entries must be objects";
            return false;
        }
        std::string unknown;
        if (!AllowKeys(dropJson, {"item", "message"}, unknown)) {
            error = "'drops' unknown field " + Quote(unknown);
            return false;
        }
        DropDef drop;
        if (!RequireString(dropJson, "item", drop.Item, error)) {
            error = "'drops' " + error;
            return false;
        }
        if (!IsId(drop.Item)) {
            error = "'drops' item " + Quote(drop.Item) +
                    " is not an id (letters, digits and _, starting with a letter)";
            return false;
        }
        for (const DropDef& existing : out) {
            if (existing.Item == drop.Item) {
                error = "'drops' " + Quote(drop.Item) + " is listed twice";
                return false;
            }
        }
        const auto message = dropJson.find("message");
        if (message != dropJson.end()) {
            if (!message->is_string()) {
                error = "'drops' 'message' must be a string";
                return false;
            }
            drop.Message = message->get<std::string>();
            if (drop.Message.empty()) {
                error = "'drops' 'message' must not be empty";
                return false;
            }
        }
        out.push_back(std::move(drop));
    }
    if (out.empty()) {
        error = "'drops' must list at least one item";
        return false;
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
                        "gold", "boss", "unique", "color", "pattern", "phaseAtPercent",
                        "phaseStatus", "summon", "ai", "drops"},
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
        if (entry.contains("drops")) {
            std::string dropError;
            if (!ReadDrops(entry, enemy.Drops, dropError))
                return fail(dropError);
        }
        const auto boss = entry.find("boss");
        if (boss != entry.end()) {
            if (!boss->is_boolean())
                return fail("'boss' must be true or false");
            enemy.Boss = boss->get<bool>();
        }
        const auto unique = entry.find("unique");
        if (unique != entry.end()) {
            if (!unique->is_boolean())
                return fail("'unique' must be true or false");
            enemy.Unique = unique->get<bool>();
        }
        const auto color = entry.find("color");
        if (color != entry.end()) {
            if (!color->is_string())
                return fail("'color' must be #RRGGBB");
            std::string colorError;
            if (!ParseColor(color->get<std::string>(), enemy.Red, enemy.Green, enemy.Blue,
                            colorError))
                return fail(colorError);
            enemy.HasColor = true;
        }
        if (entry.contains("pattern")) {
            std::string patternError;
            if (!ReadIdList(entry, "pattern", enemy.Pattern, patternError, true))
                return fail(patternError);
        }
        if (!ReadAi(parsed, entry, enemy, error))
            return fail(error);
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
            if (!AllowKeys(*summon, {"enemy", "every", "ifBlocked"}, summonUnknown))
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
            const auto blocked = summon->find("ifBlocked");
            if (blocked != summon->end()) {
                if (!blocked->is_string())
                    return fail("'ifBlocked' must be \"act\" or \"fizzle\"");
                const std::string mode = blocked->get<std::string>();
                if (mode == "act")
                    enemy.IfBlocked = BlockedSummon::Act;
                else if (mode == "fizzle")
                    enemy.IfBlocked = BlockedSummon::Fizzle;
                else
                    return fail("'ifBlocked' must be \"act\" or \"fizzle\"");
            }
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
        if (!AllowKeys(entry, {"id", "name", "enemies", "zone"}, unknown))
            return fail("unknown field " + Quote(unknown));
        const auto zoneField = entry.find("zone");
        if (zoneField != entry.end()) {
            if (!zoneField->is_string())
                return fail("'zone' must be a string");
            encounter.Zone = zoneField->get<std::string>();
            if (encounter.Zone.empty())
                return fail("'zone' must not be empty");
        }
        std::string listed;
        if (!ReadIdList(entry, "enemies", encounter.Enemies, listed, true))
            return fail(listed);
        if (encounter.Enemies.empty())
            return fail("'enemies' must list at least one enemy");
        if (encounter.Enemies.size() > 3)
            return fail("'enemies' cannot list more than 3");
        for (usize index = 0; index < encounter.Enemies.size(); ++index) {
            const std::string& enemyId = encounter.Enemies[index];
            const EnemyDef* foe = FindId(parsed.Enemies, enemyId);
            if (foe == nullptr)
                return fail("enemy " + Quote(enemyId) + " does not exist");
            if (!foe->Unique)
                continue;
            for (usize earlier = 0; earlier < index; ++earlier) {
                if (encounter.Enemies[earlier] == enemyId)
                    return fail("enemy " + Quote(enemyId) + " can appear at most once");
            }
        }
        if (!RequireString(entry, "name", encounter.Name, error))
            return fail(error);
        parsed.Encounters.push_back(std::move(encounter));
    }
    return true;
}

bool ReadRequires(const Parsed& parsed, const Json& entry, ItemDef& item, std::string& error)
{
    const auto requirement = entry.find("requires");
    if (requirement == entry.end())
        return true;
    if (!requirement->is_object()) {
        error = "'requires' must be an object";
        return false;
    }
    std::string unknown;
    if (!AllowKeys(*requirement, {"defeated"}, unknown)) {
        error = "'requires' unknown field " + Quote(unknown);
        return false;
    }
    if (!ReadIdList(*requirement, "defeated", item.RequiresDefeated, error))
        return false;
    if (item.RequiresDefeated.empty()) {
        error = "'requires.defeated' must list at least one foe";
        return false;
    }
    for (const std::string& foe : item.RequiresDefeated) {
        if (FindId(parsed.Enemies, foe) == nullptr) {
            error = "foe " + Quote(foe) + " does not exist";
            return false;
        }
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
                       {"id", "name", "description", "icon", "rarity", "slot", "bonus", "value",
                        "twoHanded", "heal", "focus", "grants", "requires"},
                       unknown))
            return fail("unknown field " + Quote(unknown));
        if (!RequireString(entry, "icon", item.Icon, error) ||
            !RequireString(entry, "rarity", item.Rarity, error))
            return fail(error);
        if (!IsId(item.Icon))
            return fail("icon must be letters, digits and _, starting with a letter");
        if (!IsId(item.Rarity))
            return fail("rarity must be letters, digits and _, starting with a letter");
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
                !Absent(entry, "twoHanded", "a consumable", error) ||
                !Absent(entry, "grants", "a consumable", error))
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
        std::string requiresError;
        if (!ReadRequires(parsed, entry, item, requiresError))
            return fail(requiresError);
        std::string grantError;
        if (!ReadGrantList(parsed, entry, "grants", SkillKind::Active, false, false, item.Grants,
                           grantError))
            return fail(grantError);
        if (item.Slot != GearSlot::Consumable) {
            i32 activeGrants = 0;
            for (const SkillGrant& listed : item.Grants) {
                if (listed.Resolved.Kind == SkillKind::Active)
                    ++activeGrants;
            }
            if (activeGrants > 1)
                return fail("an item grants at most one active skill");
        }
        if (!RequireString(entry, "name", item.Name, error) ||
            !RequireString(entry, "description", item.Description, error))
            return fail(error);
        parsed.Items.push_back(std::move(item));
    }
    return true;
}

int HexNibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool ParseColor(std::string_view text, u8& red, u8& green, u8& blue, std::string& error)
{
    if (text.size() != 7 || text[0] != '#') {
        error = "'color' must be #RRGGBB";
        return false;
    }
    const int r0 = HexNibble(text[1]);
    const int r1 = HexNibble(text[2]);
    const int g0 = HexNibble(text[3]);
    const int g1 = HexNibble(text[4]);
    const int b0 = HexNibble(text[5]);
    const int b1 = HexNibble(text[6]);
    if (r0 < 0 || r1 < 0 || g0 < 0 || g1 < 0 || b0 < 0 || b1 < 0) {
        error = "'color' must be #RRGGBB";
        return false;
    }
    red = static_cast<u8>(r0 * 16 + r1);
    green = static_cast<u8>(g0 * 16 + g1);
    blue = static_cast<u8>(b0 * 16 + b1);
    return true;
}

bool LoadRarities(const Json& root, Parsed& parsed, std::string& error)
{
    const auto list = root.find("rarities");
    if (list == root.end() || !list->is_array()) {
        error = "'rarities' must be a list";
        return false;
    }
    for (const Json& entry : *list) {
        if (!entry.is_object()) {
            error = "rarity entries must be objects";
            return false;
        }
        RarityDef rarity;
        if (!RequireString(entry, "id", rarity.Id, error))
            return false;
        const std::string where = "rarity " + Quote(rarity.Id);
        if (!IsId(rarity.Id)) {
            error = where + ": id must be letters, digits and _, starting with a letter";
            return false;
        }
        if (FindId(parsed.Rarities, rarity.Id) != nullptr) {
            error = where + ": there is another rarity with this id";
            return false;
        }
        std::string unknown;
        if (!AllowKeys(entry, {"id", "name", "color", "uniquePerType"}, unknown)) {
            error = where + ": unknown field " + Quote(unknown);
            return false;
        }
        std::string color;
        if (!RequireString(entry, "name", rarity.Name, error) ||
            !RequireString(entry, "color", color, error)) {
            error = where + ": " + error;
            return false;
        }
        std::string why;
        if (!ParseColor(color, rarity.Red, rarity.Green, rarity.Blue, why)) {
            error = where + ": " + why;
            return false;
        }
        const auto unique = entry.find("uniquePerType");
        if (unique != entry.end()) {
            if (!unique->is_boolean()) {
                error = where + ": 'uniquePerType' must be true or false";
                return false;
            }
            rarity.UniquePerType = unique->get<bool>();
        }
        parsed.Rarities.push_back(std::move(rarity));
    }
    return true;
}

bool LoadIconSheet(const Json& root, Parsed& parsed, std::string& error)
{
    if (!root.is_object()) {
        error = "the file must be a JSON object";
        return false;
    }
    for (auto it = root.begin(); it != root.end(); ++it) {
        IconDef icon;
        icon.Id = it.key();
        const std::string where = "icon " + Quote(icon.Id);
        if (!IsId(icon.Id)) {
            error = where + ": id must be letters, digits and _, starting with a letter";
            return false;
        }
        if (FindId(parsed.Icons, icon.Id) != nullptr) {
            error = where + ": there is another icon with this id";
            return false;
        }
        if (!it->is_object()) {
            error = where + ": must be an object with x, y, w, and h";
            return false;
        }
        std::string unknown;
        if (!AllowKeys(*it, {"x", "y", "w", "h"}, unknown)) {
            error = where + ": unknown field " + Quote(unknown);
            return false;
        }
        std::string why;
        if (!RequireInt(*it, "x", 0, 4096, icon.X, why) ||
            !RequireInt(*it, "y", 0, 4096, icon.Y, why) ||
            !RequireInt(*it, "w", 1, 4096, icon.Width, why) ||
            !RequireInt(*it, "h", 1, 4096, icon.Height, why)) {
            error = where + ": " + why;
            return false;
        }
        parsed.Icons.push_back(std::move(icon));
    }
    if (parsed.Icons.empty()) {
        error = "needs at least one icon";
        return false;
    }
    return true;
}

bool ParseFlagExpr(std::string_view text, FlagExpr& out, std::string& error)
{
    out = {};
    std::string_view body = text;
    if (!body.empty() && body.front() == '!') {
        out.Negate = true;
        body.remove_prefix(1);
    }
    if (body.empty()) {
        error = "unknown flag syntax " + Quote(text);
        return false;
    }
    usize dot = std::string_view::npos;
    for (usize index = 0; index < body.size(); ++index) {
        const char c = body[index];
        if (c == '.') {
            if (dot != std::string_view::npos || index == 0 || index + 1 >= body.size()) {
                error = "unknown flag syntax " + Quote(text);
                return false;
            }
            dot = index;
            continue;
        }
        const bool idChar = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '_';
        if (!idChar) {
            error = "unknown flag syntax " + Quote(text);
            return false;
        }
    }
    if (dot == std::string_view::npos) {
        out.Head.assign(body);
        if (!IsId(out.Head)) {
            error = "unknown flag syntax " + Quote(text);
            return false;
        }
        return true;
    }
    out.Head.assign(body.substr(0, dot));
    out.Tail.assign(body.substr(dot + 1));
    if (!IsId(out.Head) || !IsId(out.Tail)) {
        error = "unknown flag syntax " + Quote(text);
        return false;
    }
    return true;
}

// Missing key leaves `out` empty. A dotted expression or a negation is not a flag that
// a node can set. Those are reads, and sets rejects them as unknown syntax.
bool ReadFlagSets(const Json& object, const char* key, std::vector<std::string>& out,
                  std::string& error)
{
    const auto it = object.find(key);
    if (it == object.end())
        return true;
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
        const std::string text = entry.get<std::string>();
        FlagExpr expr;
        std::string why;
        if (!ParseFlagExpr(text, expr, why) || expr.Negate || !expr.Tail.empty()) {
            error = "unknown flag syntax " + Quote(text);
            return false;
        }
        out.push_back(std::move(expr.Head));
    }
    return true;
}

bool ReadFlagExprs(const Json& object, const char* key, std::vector<FlagExpr>& out,
                   std::string& error)
{
    const auto it = object.find(key);
    if (it == object.end())
        return true;
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
        FlagExpr expr;
        if (!ParseFlagExpr(entry.get<std::string>(), expr, error))
            return false;
        out.push_back(std::move(expr));
    }
    return true;
}

bool PlainTalkText(std::string_view text, std::string& error)
{
    if (text.find('{') != std::string_view::npos || text.find('}') != std::string_view::npos) {
        error = "must not use a placeholder";
        return false;
    }
    return true;
}

bool LoadDialogueFile(const Json& root, const std::string& stem, Parsed& parsed, std::string& error)
{
    const auto body = root.find("dialogue");
    if (body == root.end() || !body->is_object()) {
        error = "'dialogue' must be an object";
        return false;
    }
    std::string unknown;
    if (!AllowKeys(*body, {"id", "nodes"}, unknown)) {
        error = "unknown field " + Quote(unknown);
        return false;
    }
    DialogueDef talk;
    if (!RequireString(*body, "id", talk.Id, error))
        return false;
    if (!IsId(talk.Id)) {
        error = "dialogue id must be letters, digits and _, starting with a letter";
        return false;
    }
    if (talk.Id != stem) {
        error = "id " + Quote(talk.Id) + " does not match the file name";
        return false;
    }
    if (FindId(parsed.Dialogues, talk.Id) != nullptr) {
        error = "there is another dialogue with this id";
        return false;
    }
    const auto nodes = body->find("nodes");
    if (nodes == body->end() || !nodes->is_array()) {
        error = "'nodes' must be a list";
        return false;
    }
    if (nodes->empty()) {
        error = "'nodes' must list at least one node";
        return false;
    }
    for (const Json& entry : *nodes) {
        if (!entry.is_object()) {
            error = "node entries must be objects";
            return false;
        }
        DialogueNode node;
        if (!RequireString(entry, "id", node.Id, error))
            return false;
        const std::string where = "node " + Quote(node.Id);
        if (!IsId(node.Id)) {
            error = where + ": id must be letters, digits and _, starting with a letter";
            return false;
        }
        if (talk.FindNode(node.Id) != nullptr) {
            error = where + ": there is another node with this id";
            return false;
        }
        if (!AllowKeys(entry, {"id", "line", "note", "requires", "sets", "choices"}, unknown)) {
            error = where + ": unknown field " + Quote(unknown);
            return false;
        }
        if (!RequireString(entry, "line", node.Line, error)) {
            error = where + ": " + error;
            return false;
        }
        std::string why;
        if (!PlainTalkText(node.Line, why)) {
            error = where + ": line " + why;
            return false;
        }
        const auto note = entry.find("note");
        if (note != entry.end()) {
            if (!note->is_string()) {
                error = where + ": 'note' must be a string";
                return false;
            }
            node.Note = note->get<std::string>();
            if (node.Note.empty()) {
                error = where + ": 'note' must not be empty";
                return false;
            }
        }
        if (!ReadFlagExprs(entry, "requires", node.Requires, why) ||
            !ReadFlagSets(entry, "sets", node.Sets, why)) {
            error = where + ": " + why;
            return false;
        }
        const auto choices = entry.find("choices");
        if (choices == entry.end() || !choices->is_array()) {
            error = where + ": 'choices' must be a list";
            return false;
        }
        for (const Json& choiceEntry : *choices) {
            if (!choiceEntry.is_object()) {
                error = where + ": choice entries must be objects";
                return false;
            }
            if (!AllowKeys(choiceEntry, {"text", "next", "end", "action", "requires", "sets"},
                           unknown)) {
                error = where + ": unknown field " + Quote(unknown);
                return false;
            }
            DialogueChoice choice;
            if (!RequireString(choiceEntry, "text", choice.Text, error)) {
                error = where + ": " + error;
                return false;
            }
            if (!PlainTalkText(choice.Text, why)) {
                error = where + ": choice text " + why;
                return false;
            }
            const bool hasNext = choiceEntry.contains("next");
            const bool hasEnd = choiceEntry.contains("end");
            const bool hasAction = choiceEntry.contains("action");
            const int destinations = static_cast<int>(hasNext) + static_cast<int>(hasEnd) +
                                     static_cast<int>(hasAction);
            if (destinations != 1) {
                error = where + ": choice " + Quote(choice.Text) +
                        " needs one of next, end, or action";
                return false;
            }
            if (hasNext) {
                if (!choiceEntry["next"].is_string()) {
                    error = where + ": 'next' must be a node id";
                    return false;
                }
                choice.Kind = TalkChoiceKind::Next;
                choice.Next = choiceEntry["next"].get<std::string>();
                if (!IsId(choice.Next)) {
                    error = where + ": 'next' must be a node id";
                    return false;
                }
            } else if (hasEnd) {
                if (!choiceEntry["end"].is_boolean() || !choiceEntry["end"].get<bool>()) {
                    error = where + ": 'end' must be true";
                    return false;
                }
                choice.Kind = TalkChoiceKind::End;
            } else {
                if (!choiceEntry["action"].is_string()) {
                    error = where + ": 'action' must be an id";
                    return false;
                }
                choice.Action = choiceEntry["action"].get<std::string>();
                if (!IsId(choice.Action) || !KnownTalkAction(choice.Action)) {
                    error = where + ": unknown action " + Quote(choice.Action);
                    return false;
                }
                choice.Kind = TalkChoiceKind::Action;
            }
            if (!ReadFlagExprs(choiceEntry, "requires", choice.Requires, why) ||
                !ReadFlagSets(choiceEntry, "sets", choice.Sets, why)) {
                error = where + ": " + why;
                return false;
            }
            node.Choices.push_back(std::move(choice));
        }
        talk.Nodes.push_back(std::move(node));
    }
    for (const DialogueNode& node : talk.Nodes) {
        const std::string where = "node " + Quote(node.Id);
        for (const DialogueChoice& choice : node.Choices) {
            if (choice.Kind != TalkChoiceKind::Next)
                continue;
            if (talk.FindNode(choice.Next) == nullptr) {
                error = where + ": next " + Quote(choice.Next) + " does not exist";
                return false;
            }
        }
    }
    parsed.Dialogues.push_back(std::move(talk));
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
                   {"id", "name", "keeper", "dialogue", "bagSlots", "stackSize", "sellNumerator",
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
        !RequireString(*body, "keeper", shop.Keeper, error) ||
        !OptionalId(*body, "dialogue", shop.Dialogue, error))
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
                        "damageTakenPercent", "damageDealtPercent", "skipAction", "resists"},
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
        const auto resists = entry.find("resists");
        if (resists != entry.end()) {
            if (!resists->is_array())
                return fail("'resists' must be a list of status ids");
            for (const Json& resisted : *resists) {
                if (!resisted.is_string() || !IsId(resisted.get<std::string>()))
                    return fail("'resists' must be a list of status ids");
                status.Resists.push_back(resisted.get<std::string>());
            }
        }
        if (status.DamagePerTurn <= 0 && status.DamageTakenPercent <= 0 &&
            status.DamageDealtPercent <= 0 && !status.SkipAction && status.Resists.empty())
            return fail("needs damage, a percent, skipAction, or resists");
        if (!RequireString(entry, "name", status.Name, error) ||
            !RequireString(entry, "description", status.Description, error))
            return fail(error);
        parsed.Statuses.push_back(std::move(status));
    }
    // Every status can be resisted, so the ids are checked once the whole file is read.
    for (const StatusDef& loaded : parsed.Statuses) {
        for (const std::string& blockedId : loaded.Resists) {
            if (blockedId == loaded.Id) {
                error = "status " + Quote(loaded.Id) + ": 'resists' cannot list the status itself";
                return false;
            }
            if (FindId(parsed.Statuses, blockedId) == nullptr) {
                error = "status " + Quote(loaded.Id) + ": 'resists' " + Quote(blockedId) +
                        ", which does not exist";
                return false;
            }
        }
    }
    return true;
}

bool StatusesExist(const Parsed& parsed, const std::vector<EffectDef>& effects,
                   const std::string& where, std::string& error)
{
    for (const EffectDef& effect : effects) {
        if (effect.IfTargetHas.has_value() &&
            FindId(parsed.Statuses, *effect.IfTargetHas) == nullptr) {
            error = where + ": effect " + Quote(effect.Id) + ": 'ifTargetHas' status " +
                    Quote(*effect.IfTargetHas) + " does not exist";
            return false;
        }
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

// A description may name this entity's own fields in {braces}. There is no arithmetic.
// A brace that is not a known, present field fails the load.

struct TextScope {
    std::span<const StatDef> Stats;
    std::span<const StatusDef> Statuses;
    std::span<const ClassDef> Classes;
    std::span<const EnemyDef> Enemies;
    const SkillDef* Skill = nullptr;
    const StatusDef* Status = nullptr;
    const ClassDef* Class = nullptr;
    const EnemyDef* Enemy = nullptr;
    const ItemDef* Item = nullptr;
    // Added to damage and reflect power. A fight uses the same (rank - 1) * powerPerRank.
    i32 RankBonus = 0;
};

std::optional<std::string> Number(const std::optional<i32>& amount)
{
    if (!amount.has_value())
        return std::nullopt;
    return std::to_string(*amount);
}

std::optional<std::string> StatName(const TextScope& scope, const std::optional<std::string>& id)
{
    if (!id.has_value())
        return std::nullopt;
    const StatDef* stat = FindId(scope.Stats, *id);
    if (stat == nullptr)
        return std::nullopt;
    return stat->Name;
}

// Status fields, shared by a status's own text and by "{effect.status.field}" on a skill.
// A zero percent or per-turn damage is not that field: the placeholder is then an error.
std::optional<std::string> StatusField(const StatusDef& status, std::string_view key)
{
    if (key == "name")
        return status.Name;
    if (key == "turns")
        return std::to_string(status.Turns);
    if (key == "stacks")
        return std::to_string(status.MaxStacks);
    if (key == "perTurn" && status.DamagePerTurn != 0)
        return std::to_string(status.DamagePerTurn);
    if (key == "dealt" && status.DamageDealtPercent != 0)
        return std::to_string(status.DamageDealtPercent);
    if (key == "taken" && status.DamageTakenPercent != 0)
        return std::to_string(status.DamageTakenPercent);
    return std::nullopt;
}

bool ScalesWithRank(const EffectDef& effect)
{
    return effect.Op == EffectOp::Damage || effect.Op == EffectOp::Reflect;
}

std::optional<std::string> EffectField(const TextScope& scope, const EffectDef& effect,
                                       std::string_view key)
{
    if (key == "power") {
        if (!effect.Power.has_value())
            return std::nullopt;
        const i32 bonus = ScalesWithRank(effect) ? scope.RankBonus : 0;
        return std::to_string(*effect.Power + bonus);
    }
    if (key == "amount")
        return Number(effect.Amount);
    if (key == "percent")
        return Number(effect.Percent);
    if (key == "per")
        return Number(effect.Per);
    if (key == "max")
        return Number(effect.Max);
    if (key == "turns")
        return Number(effect.Turns);
    if (key == "every")
        return Number(effect.Every);
    if (key == "stat")
        return StatName(scope, effect.Stat);
    if (key == "scaling")
        return StatName(scope, effect.Scaling);
    if (key == "ifTargetHas") {
        if (!effect.IfTargetHas.has_value())
            return std::nullopt;
        const StatusDef* needed = FindId(scope.Statuses, *effect.IfTargetHas);
        if (needed == nullptr)
            return std::nullopt;
        return needed->Name;
    }
    if (!effect.Status.has_value())
        return std::nullopt;
    const StatusDef* applied = FindId(scope.Statuses, *effect.Status);
    if (applied == nullptr)
        return std::nullopt;
    if (key == "status")
        return applied->Name;
    constexpr std::string_view kPrefix = "status.";
    if (key.substr(0, kPrefix.size()) == kPrefix)
        return StatusField(*applied, key.substr(kPrefix.size()));
    return std::nullopt;
}

std::optional<std::string> SkillField(const TextScope& scope, std::string_view key)
{
    const SkillDef& skill = *scope.Skill;
    if (key == "name")
        return skill.Name;
    if (key == "cost")
        return std::to_string(skill.FocusCost);
    if (key == "windup" && skill.Windup > 0)
        return std::to_string(skill.Windup);
    const usize dot = key.find('.');
    if (dot == std::string_view::npos)
        return std::nullopt;
    const EffectDef* effect = skill.FindEffect(key.substr(0, dot));
    if (effect == nullptr)
        return std::nullopt;
    return EffectField(scope, *effect, key.substr(dot + 1));
}

std::optional<std::string> ClassField(const TextScope& scope, std::string_view key)
{
    const ClassDef& klass = *scope.Class;
    if (key == "name")
        return klass.Name;
    if (klass.IsEvolution()) {
        if (key == "evolveLevel")
            return std::to_string(klass.EvolveLevel);
        if (key == "from") {
            const ClassDef* parent = FindId(scope.Classes, klass.EvolvesFrom);
            if (parent == nullptr)
                return std::nullopt;
            return parent->Name;
        }
        return std::nullopt;
    }
    // A base class with evolutions. Names join as "Berserker or a Guardian" so the
    // description's leading "a " reads "a Berserker or a Guardian". The level counts only
    // when every child is offered at the same level.
    std::string paths;
    i32 level = 0;
    bool seen = false;
    bool agreed = true;
    for (const ClassDef& child : scope.Classes) {
        if (child.EvolvesFrom != klass.Id)
            continue;
        if (!paths.empty())
            paths += " or a ";
        paths += child.Name;
        if (!seen) {
            level = child.EvolveLevel;
            seen = true;
        } else if (child.EvolveLevel != level) {
            agreed = false;
        }
    }
    if (paths.empty())
        return std::nullopt;
    if (key == "paths")
        return paths;
    if (key == "pathLevel" && agreed)
        return std::to_string(level);
    return std::nullopt;
}

std::optional<std::string> EnemyField(const TextScope& scope, std::string_view key)
{
    const EnemyDef& enemy = *scope.Enemy;
    if (key == "name")
        return enemy.Name;
    if (key == "phaseAt" && enemy.PhaseAtPercent > 0)
        return std::to_string(enemy.PhaseAtPercent);
    if (enemy.SummonId.empty())
        return std::nullopt;
    if (key == "summon") {
        const EnemyDef* summoned = FindId(scope.Enemies, enemy.SummonId);
        if (summoned == nullptr)
            return std::nullopt;
        return summoned->Name;
    }
    if (key == "summonEvery")
        return std::to_string(enemy.SummonEvery);
    return std::nullopt;
}

std::optional<std::string> ItemField(const TextScope& scope, std::string_view key)
{
    const ItemDef& item = *scope.Item;
    if (key == "name")
        return item.Name;
    if (key == "value")
        return std::to_string(item.Value);
    if (key == "heal" && item.Heal > 0)
        return std::to_string(item.Heal);
    if (key == "focus" && item.Focus > 0)
        return std::to_string(item.Focus);
    return std::nullopt;
}

std::optional<std::string> Lookup(const TextScope& scope, std::string_view key)
{
    if (scope.Skill != nullptr)
        return SkillField(scope, key);
    if (scope.Status != nullptr)
        return StatusField(*scope.Status, key);
    if (scope.Class != nullptr)
        return ClassField(scope, key);
    if (scope.Enemy != nullptr)
        return EnemyField(scope, key);
    if (scope.Item != nullptr)
        return ItemField(scope, key);
    return std::nullopt;
}

bool ExpandText(std::string_view text, const TextScope& scope, std::string& out, std::string& why)
{
    out.clear();
    usize at = 0;
    while (at < text.size()) {
        const usize open = text.find_first_of("{}", at);
        if (open == std::string_view::npos) {
            out += text.substr(at);
            break;
        }
        out += text.substr(at, open - at);
        const usize close = text[open] == '{' ? text.find('}', open) : std::string_view::npos;
        if (close == std::string_view::npos) {
            why = "stray brace in \"" + std::string(text) + "\"";
            return false;
        }
        const std::string_view key = text.substr(open + 1, close - open - 1);
        const std::optional<std::string> replacement = Lookup(scope, key);
        if (!replacement.has_value()) {
            why = "unknown placeholder {" + std::string(key) + "}";
            return false;
        }
        out += *replacement;
        at = close + 1;
    }
    why.clear();
    return true;
}

TextScope CatalogOf(const Parsed& parsed)
{
    TextScope scope;
    scope.Stats = parsed.Stats;
    scope.Statuses = parsed.Statuses;
    scope.Classes = parsed.Classes;
    scope.Enemies = parsed.Enemies;
    return scope;
}

TextScope CatalogOf(const Database& data)
{
    TextScope scope;
    scope.Stats = data.GetStats();
    scope.Statuses = data.GetStatuses();
    scope.Classes = data.GetClasses();
    scope.Enemies = data.GetEnemies();
    return scope;
}

std::string ExpandOrRaw(std::string_view text, const TextScope& scope)
{
    std::string out;
    std::string why;
    if (!ExpandText(text, scope, out, why))
        return std::string(text);
    return out;
}

i32 RankBonusFor(i32 rank, i32 perRank)
{
    if (rank <= 1 || perRank <= 0)
        return 0;
    const i64 scaled = static_cast<i64>(rank - 1) * perRank;
    if (scaled > 9999)
        return 9999;
    return static_cast<i32>(scaled);
}

bool CheckText(const char* kind, const std::string& id, std::string_view text,
               const TextScope& scope, std::string& error)
{
    std::string out;
    std::string why;
    if (ExpandText(text, scope, out, why))
        return true;
    error = std::string(kind) + " " + Quote(id) + " description: " + why;
    return false;
}

bool CheckSkillText(const TextScope& base, const SkillDef& skill, std::string& error)
{
    TextScope scope = base;
    scope.Skill = &skill;
    return CheckText("skill", skill.Id, skill.Description, scope, error);
}

bool CheckGrants(const TextScope& base, const std::vector<SkillGrant>& grants, std::string& error)
{
    for (const SkillGrant& grant : grants) {
        if (!CheckSkillText(base, grant.Resolved, error))
            return false;
    }
    return true;
}

bool DescriptionsResolve(const Parsed& parsed, std::string& error)
{
    const TextScope base = CatalogOf(parsed);
    for (const SkillDef& skill : parsed.Skills) {
        if (!CheckSkillText(base, skill, error))
            return false;
    }
    for (const StatusDef& loaded : parsed.Statuses) {
        TextScope scope = base;
        scope.Status = &loaded;
        if (!CheckText("status", loaded.Id, loaded.Description, scope, error))
            return false;
    }
    for (const ClassDef& klass : parsed.Classes) {
        TextScope scope = base;
        scope.Class = &klass;
        if (!CheckText("class", klass.Id, klass.Description, scope, error))
            return false;
        if (!CheckGrants(base, klass.Skills, error) || !CheckGrants(base, klass.Passives, error))
            return false;
        for (const SkillPick& pick : klass.Picks) {
            if (!CheckGrants(base, pick.Options, error))
                return false;
        }
    }
    for (const EnemyDef& enemy : parsed.Enemies) {
        TextScope scope = base;
        scope.Enemy = &enemy;
        if (!CheckText("enemy", enemy.Id, enemy.Description, scope, error))
            return false;
        if (!CheckGrants(base, enemy.Skills, error))
            return false;
    }
    for (const ItemDef& item : parsed.Items) {
        TextScope scope = base;
        scope.Item = &item;
        if (!CheckText("item", item.Id, item.Description, scope, error))
            return false;
        if (!CheckGrants(base, item.Grants, error))
            return false;
    }
    return true;
}

// A rarity with UniquePerType may appear on one item of each slot. Slot is the item type.
bool CheckUniquePerType(const Parsed& parsed, std::string& error)
{
    for (const RarityDef& rarity : parsed.Rarities) {
        if (!rarity.UniquePerType)
            continue;
        for (usize later = 0; later < parsed.Items.size(); ++later) {
            const ItemDef& item = parsed.Items[later];
            if (item.Rarity != rarity.Id)
                continue;
            for (usize earlier = 0; earlier < later; ++earlier) {
                const ItemDef& prior = parsed.Items[earlier];
                if (prior.Rarity != rarity.Id || prior.Slot != item.Slot)
                    continue;
                error = "item " + Quote(item.Id) + ": rarity " + Quote(rarity.Id) +
                        " allows one item per slot, and " + Quote(prior.Id) + " is already slot " +
                        Quote(ToString(item.Slot));
                return false;
            }
        }
    }
    return true;
}

bool CrossCheck(const Parsed& parsed, std::string& error)
{
    for (const EffectDef& recipe : parsed.Effects) {
        if (recipe.IfTargetHas.has_value() &&
            FindId(parsed.Statuses, *recipe.IfTargetHas) == nullptr) {
            error = "effect " + Quote(recipe.Id) + ": 'ifTargetHas' status " +
                    Quote(*recipe.IfTargetHas) + " does not exist";
            return false;
        }
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
        for (const SkillPick& pick : klass.Picks) {
            if (!GrantsKnown(parsed, pick.Options, classWhere, error))
                return false;
        }
        if (!klass.IsEvolution()) {
            if (!CheckPickOptions(klass, nullptr, classWhere, error))
                return false;
            continue;
        }
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
        if (!CheckPickOptions(klass, parent, classWhere, error))
            return false;
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
        for (const DropDef& drop : enemy.Drops) {
            if (FindId(parsed.Items, drop.Item) == nullptr) {
                error = enemyWhere + ": drop " + Quote(drop.Item) + " does not exist";
                return false;
            }
        }
    }
    for (const ItemDef& item : parsed.Items) {
        const std::string itemWhere = "item " + Quote(item.Id);
        if (!GrantsKnown(parsed, item.Grants, itemWhere, error))
            return false;
        if (FindId(parsed.Icons, item.Icon) == nullptr) {
            error = itemWhere + ": icon " + Quote(item.Icon) + " does not exist";
            return false;
        }
        if (FindId(parsed.Rarities, item.Rarity) == nullptr) {
            error = itemWhere + ": rarity " + Quote(item.Rarity) + " does not exist";
            return false;
        }
    }
    if (!CheckUniquePerType(parsed, error))
        return false;
    for (const std::string& stocked : parsed.Shop.Stock) {
        if (FindId(parsed.Items, stocked) == nullptr) {
            error = "shop stock " + Quote(stocked) + " does not exist";
            return false;
        }
    }
    if (!parsed.Shop.Dialogue.empty() &&
        FindId(parsed.Dialogues, parsed.Shop.Dialogue) == nullptr) {
        error = "shop dialogue " + Quote(parsed.Shop.Dialogue) + " does not exist";
        return false;
    }
    if (!DescriptionsResolve(parsed, error))
        return false;
    return true;
}

// A map is at most this many tiles on a side. Coordinates in the file use the same cap.
constexpr i32 kMaxMapSpan = 256;

bool LoadTiles(const Json& root, Parsed& parsed, std::string& error)
{
    const auto list = root.find("tiles");
    if (list == root.end() || !list->is_array()) {
        error = "'tiles' must be a list";
        return false;
    }
    if (list->empty()) {
        error = "'tiles' must list at least one tile";
        return false;
    }
    for (const Json& entry : *list) {
        if (!entry.is_object()) {
            error = "tile entries must be objects";
            return false;
        }
        TileDef tile;
        if (!RequireString(entry, "id", tile.Id, error))
            return false;
        const std::string where = "tile " + Quote(tile.Id);
        if (!IsId(tile.Id)) {
            error = where + ": id must be letters, digits and _, starting with a letter";
            return false;
        }
        if (FindId(parsed.Tiles, tile.Id) != nullptr) {
            error = where + ": there is another tile with this id";
            return false;
        }
        std::string unknown;
        if (!AllowKeys(entry, {"id", "solid", "color", "sheet"}, unknown)) {
            error = where + ": unknown field " + Quote(unknown);
            return false;
        }
        const auto solid = entry.find("solid");
        if (solid == entry.end()) {
            error = where + ": 'solid' is required";
            return false;
        }
        if (!solid->is_boolean()) {
            error = where + ": 'solid' must be true or false";
            return false;
        }
        tile.Solid = solid->get<bool>();
        std::string color;
        if (!RequireString(entry, "color", color, error)) {
            error = where + ": " + error;
            return false;
        }
        std::string why;
        if (!ParseColor(color, tile.Red, tile.Green, tile.Blue, why)) {
            error = where + ": " + why;
            return false;
        }
        if (!OptionalId(entry, "sheet", tile.Sheet, why)) {
            error = where + ": " + why;
            return false;
        }
        parsed.Tiles.push_back(std::move(tile));
    }
    return true;
}

std::string Coord(MapTile tile)
{
    return std::to_string(tile.X) + "," + std::to_string(tile.Y);
}

bool SameTile(MapTile left, MapTile right)
{
    return left.X == right.X && left.Y == right.Y;
}

bool ReadCoord(const Json& object, MapTile& out, std::string& error)
{
    if (!object.is_object()) {
        error = "must be an object with x and y";
        return false;
    }
    std::string unknown;
    if (!AllowKeys(object, {"x", "y"}, unknown)) {
        error = "unknown field " + Quote(unknown);
        return false;
    }
    if (!RequireInt(object, "x", 0, kMaxMapSpan - 1, out.X, error) ||
        !RequireInt(object, "y", 0, kMaxMapSpan - 1, out.Y, error))
        return false;
    return true;
}

bool RequireTile(const Json& object, const char* key, MapTile& out, std::string& error)
{
    const auto it = object.find(key);
    if (it == object.end()) {
        error = std::string("'") + key + "' is required";
        return false;
    }
    if (!ReadCoord(*it, out, error)) {
        error = std::string("'") + key + "' " + error;
        return false;
    }
    return true;
}

bool OptionalList(const Json& object, const char* key, const Json*& out, std::string& error)
{
    out = nullptr;
    const auto it = object.find(key);
    if (it == object.end())
        return true;
    if (!it->is_array()) {
        error = std::string("'") + key + "' must be a list";
        return false;
    }
    out = &(*it);
    return true;
}

// True when the object has no keys. The first key is reported when it does.
bool NoKeys(const Json& object, std::string& unknown)
{
    const auto it = object.begin();
    if (it == object.end())
        return true;
    unknown = it.key();
    return false;
}

struct LegendGlyph {
    char Glyph = 0;
    std::string Tile;
};

bool LoadLegend(const Json& body, const Parsed& parsed, std::vector<LegendGlyph>& legend,
                std::string& error)
{
    const auto legendJson = body.find("legend");
    if (legendJson == body.end() || !legendJson->is_object()) {
        error = "'legend' must be an object";
        return false;
    }
    if (legendJson->empty()) {
        error = "'legend' must name at least one character";
        return false;
    }
    for (auto it = legendJson->begin(); it != legendJson->end(); ++it) {
        const std::string key = it.key();
        if (key.size() != 1) {
            error = "legend key " + Quote(key) + " must be one character";
            return false;
        }
        const unsigned char glyph = static_cast<unsigned char>(key[0]);
        if (glyph < 32 || glyph > 126) {
            error = "legend key must be one printable character";
            return false;
        }
        if (!it->is_string()) {
            error = "legend " + Quote(key) + " must be a tile id";
            return false;
        }
        const std::string tileId = it->get<std::string>();
        if (FindId(parsed.Tiles, tileId) == nullptr) {
            error = "legend " + Quote(key) + ": tile " + Quote(tileId) + " does not exist";
            return false;
        }
        LegendGlyph entry;
        entry.Glyph = static_cast<char>(glyph);
        entry.Tile = tileId;
        legend.push_back(std::move(entry));
    }
    return true;
}

const LegendGlyph* FindGlyph(const std::vector<LegendGlyph>& legend, char glyph)
{
    for (const LegendGlyph& entry : legend) {
        if (entry.Glyph == glyph)
            return &entry;
    }
    return nullptr;
}

bool LoadRows(const Json& body, const std::vector<LegendGlyph>& legend, MapDef& map,
              std::string& error)
{
    const auto rows = body.find("rows");
    if (rows == body.end() || !rows->is_array()) {
        error = "'rows' must be a list";
        return false;
    }
    if (rows->empty()) {
        error = "'rows' must list at least one row";
        return false;
    }
    if (rows->size() > static_cast<usize>(kMaxMapSpan)) {
        error = "'rows' is taller than " + std::to_string(kMaxMapSpan);
        return false;
    }
    std::vector<std::string> lines;
    lines.reserve(rows->size());
    for (usize index = 0; index < rows->size(); ++index) {
        const Json& row = (*rows)[index];
        if (!row.is_string()) {
            error = "rows[" + std::to_string(index) + "] must be a string";
            return false;
        }
        lines.push_back(row.get<std::string>());
    }
    if (lines[0].empty()) {
        error = "rows[0] must not be empty";
        return false;
    }
    if (lines[0].size() > static_cast<usize>(kMaxMapSpan)) {
        error = "rows[0] is wider than " + std::to_string(kMaxMapSpan);
        return false;
    }
    const usize width = lines[0].size();
    for (usize index = 1; index < lines.size(); ++index) {
        if (lines[index].size() != width) {
            error = "rows[" + std::to_string(index) + "]: width " +
                    std::to_string(lines[index].size()) + " does not match width " +
                    std::to_string(width);
            return false;
        }
    }
    map.Width = static_cast<i32>(width);
    map.Height = static_cast<i32>(lines.size());
    map.Cells.reserve(width * lines.size());
    for (usize y = 0; y < lines.size(); ++y) {
        for (usize x = 0; x < width; ++x) {
            const char glyph = lines[y][x];
            const LegendGlyph* found = FindGlyph(legend, glyph);
            if (found == nullptr) {
                const std::string shown(1, glyph);
                error = "rows[" + std::to_string(y) + "] column " + std::to_string(x) + ": " +
                        Quote(shown) + " is not in the legend";
                return false;
            }
            map.Cells.push_back(found->Tile);
        }
    }
    return true;
}

// `why` is a sentence fragment: "2,1 is outside the map" or "0,0 is solid tile 'wall'".
bool Walkable(const Parsed& parsed, const MapDef& map, MapTile tile, std::string& why)
{
    if (!map.InBounds(tile.X, tile.Y)) {
        why = Coord(tile) + " is outside the map";
        return false;
    }
    const std::string* cell = map.Cell(tile.X, tile.Y);
    if (cell == nullptr) {
        why = Coord(tile) + " is outside the map";
        return false;
    }
    const TileDef* def = FindId(parsed.Tiles, *cell);
    if (def == nullptr) {
        why = "tile " + Quote(*cell) + " does not exist";
        return false;
    }
    if (def->Solid) {
        why = Coord(tile) + " is solid tile " + Quote(def->Id);
        return false;
    }
    return true;
}

// A rest point may share the start. Every other marker needs a tile of its own.
bool MarkerFree(const MapDef& map, MapTile tile, bool restOnStart, std::string& why)
{
    if (!restOnStart && SameTile(map.Start, tile)) {
        why = "tile " + Coord(tile) + " is the start";
        return false;
    }
    for (const MapSpawn& spawn : map.Spawns) {
        if (SameTile(spawn.Tile, tile)) {
            why = "tile " + Coord(tile) + " already has spawn " + Quote(spawn.Id);
            return false;
        }
    }
    for (const MapLink& link : map.Links) {
        if (SameTile(link.Tile, tile)) {
            why = "tile " + Coord(tile) + " already has a link";
            return false;
        }
    }
    for (const MapPoint& point : map.Points) {
        if (SameTile(point.Tile, tile)) {
            why = "tile " + Coord(tile) + " already has a " + std::string(ToString(point.Kind));
            return false;
        }
    }
    return true;
}

bool SpawnIdFree(const Parsed& parsed, const MapDef& map, std::string_view id)
{
    if (map.FindSpawn(id) != nullptr)
        return false;
    for (const MapDef& other : parsed.Maps) {
        if (other.FindSpawn(id) != nullptr)
            return false;
    }
    return true;
}

bool ParsePointKind(std::string_view text, MapPointKind& out)
{
    if (text == "talk")
        out = MapPointKind::Talk;
    else if (text == "shop")
        out = MapPointKind::Shop;
    else if (text == "rest")
        out = MapPointKind::Rest;
    else
        return false;
    return true;
}

bool LoadSpawns(const Json& body, const Parsed& parsed, MapDef& map, std::string& error)
{
    const Json* spawns = nullptr;
    if (!OptionalList(body, "spawns", spawns, error))
        return false;
    if (spawns == nullptr)
        return true;
    for (const Json& entry : *spawns) {
        if (!entry.is_object()) {
            error = "spawn entries must be objects";
            return false;
        }
        MapSpawn spawn;
        if (!RequireString(entry, "id", spawn.Id, error))
            return false;
        const std::string where = "spawn " + Quote(spawn.Id);
        if (!IsId(spawn.Id)) {
            error = where + ": id must be letters, digits and _, starting with a letter";
            return false;
        }
        if (!SpawnIdFree(parsed, map, spawn.Id)) {
            error = where + ": there is another spawn with this id";
            return false;
        }
        std::string unknown;
        if (!AllowKeys(entry, {"id", "tile", "encounter", "once", "noFlee", "mover"}, unknown)) {
            error = where + ": unknown field " + Quote(unknown);
            return false;
        }
        std::string why;
        if (!RequireTile(entry, "tile", spawn.Tile, why) ||
            !Walkable(parsed, map, spawn.Tile, why) || !MarkerFree(map, spawn.Tile, false, why)) {
            error = where + ": " + why;
            return false;
        }
        if (!RequireString(entry, "encounter", spawn.Encounter, why)) {
            error = where + ": " + why;
            return false;
        }
        if (!IsId(spawn.Encounter)) {
            error = where + ": 'encounter' must be an id";
            return false;
        }
        if (FindId(parsed.Encounters, spawn.Encounter) == nullptr) {
            error = where + ": encounter " + Quote(spawn.Encounter) + " does not exist";
            return false;
        }
        if (!OptionalBool(entry, "once", spawn.Once, why) ||
            !OptionalBool(entry, "noFlee", spawn.NoFlee, why)) {
            error = where + ": " + why;
            return false;
        }
        if (entry.contains("mover")) {
            const Json& mover = entry["mover"];
            if (!mover.is_object()) {
                error = where + ": 'mover' must be an object";
                return false;
            }
            std::string moverUnknown;
            if (!NoKeys(mover, moverUnknown)) {
                error = where + ": 'mover' unknown field " + Quote(moverUnknown);
                return false;
            }
            spawn.Mover.Listed = true;
        }
        map.Spawns.push_back(std::move(spawn));
    }
    return true;
}

bool LoadLinks(const Json& body, const Parsed& parsed, MapDef& map, std::string& error)
{
    const Json* links = nullptr;
    if (!OptionalList(body, "links", links, error))
        return false;
    if (links == nullptr)
        return true;
    for (const Json& entry : *links) {
        if (!entry.is_object()) {
            error = "link entries must be objects";
            return false;
        }
        std::string unknown;
        if (!AllowKeys(entry, {"tile", "map", "arrival"}, unknown)) {
            error = "link: unknown field " + Quote(unknown);
            return false;
        }
        MapLink link;
        std::string why;
        if (!RequireTile(entry, "tile", link.Tile, why)) {
            error = "link: " + why;
            return false;
        }
        const std::string where = "link at " + Coord(link.Tile);
        if (!Walkable(parsed, map, link.Tile, why) || !MarkerFree(map, link.Tile, false, why)) {
            error = where + ": " + why;
            return false;
        }
        if (!RequireString(entry, "map", link.Map, why)) {
            error = where + ": " + why;
            return false;
        }
        if (!IsId(link.Map)) {
            error = where + ": 'map' must be an id";
            return false;
        }
        // The target may be a file that has not been read yet. CheckMapLinks resolves it.
        if (!RequireTile(entry, "arrival", link.Arrival, why)) {
            error = where + ": " + why;
            return false;
        }
        map.Links.push_back(std::move(link));
    }
    return true;
}

bool LoadPoints(const Json& body, const Parsed& parsed, MapDef& map, std::string& error)
{
    const Json* points = nullptr;
    if (!OptionalList(body, "points", points, error))
        return false;
    if (points == nullptr)
        return true;
    for (const Json& entry : *points) {
        if (!entry.is_object()) {
            error = "point entries must be objects";
            return false;
        }
        std::string kindText;
        if (!RequireString(entry, "kind", kindText, error))
            return false;
        MapPoint point;
        if (!ParsePointKind(kindText, point.Kind)) {
            error = "unknown point kind " + Quote(kindText);
            return false;
        }
        std::string unknown;
        const bool talk = point.Kind == MapPointKind::Talk;
        const bool keysOk = talk ? AllowKeys(entry, {"kind", "tile", "dialogue"}, unknown)
                                 : AllowKeys(entry, {"kind", "tile"}, unknown);
        if (!keysOk) {
            error = std::string(ToString(point.Kind)) + ": unknown field " + Quote(unknown);
            return false;
        }
        std::string why;
        if (!RequireTile(entry, "tile", point.Tile, why)) {
            error = std::string(ToString(point.Kind)) + ": " + why;
            return false;
        }
        const std::string where = std::string(ToString(point.Kind)) + " at " + Coord(point.Tile);
        const bool rest = point.Kind == MapPointKind::Rest;
        if (!Walkable(parsed, map, point.Tile, why) ||
            !MarkerFree(map, point.Tile, rest, why)) {
            error = where + ": " + why;
            return false;
        }
        if (talk) {
            if (!RequireString(entry, "dialogue", point.Dialogue, why)) {
                error = where + ": " + why;
                return false;
            }
            if (!IsId(point.Dialogue)) {
                error = where + ": 'dialogue' must be an id";
                return false;
            }
            if (FindId(parsed.Dialogues, point.Dialogue) == nullptr) {
                error = where + ": dialogue " + Quote(point.Dialogue) + " does not exist";
                return false;
            }
        }
        map.Points.push_back(std::move(point));
    }
    return true;
}

bool LoadMapFile(const Json& root, const std::string& stem, Parsed& parsed, std::string& error)
{
    const auto body = root.find("map");
    if (body == root.end() || !body->is_object()) {
        error = "'map' must be an object";
        return false;
    }
    std::string unknown;
    if (!AllowKeys(*body, {"id", "hub", "rows", "legend", "start", "spawns", "links", "points"},
                   unknown)) {
        error = "unknown field " + Quote(unknown);
        return false;
    }
    MapDef map;
    if (!RequireString(*body, "id", map.Id, error))
        return false;
    if (!IsId(map.Id)) {
        error = "map id must be letters, digits and _, starting with a letter";
        return false;
    }
    if (map.Id != stem) {
        error = "id " + Quote(map.Id) + " does not match the file name";
        return false;
    }
    if (FindId(parsed.Maps, map.Id) != nullptr) {
        error = "there is another map with this id";
        return false;
    }
    if (!OptionalBool(*body, "hub", map.Hub, error))
        return false;
    if (map.Hub) {
        for (const MapDef& other : parsed.Maps) {
            if (other.Hub) {
                error = "hub is already set on " + Quote(other.Id);
                return false;
            }
        }
    }
    std::vector<LegendGlyph> legend;
    if (!LoadLegend(*body, parsed, legend, error) || !LoadRows(*body, legend, map, error))
        return false;
    std::string why;
    if (!RequireTile(*body, "start", map.Start, error))
        return false;
    if (!Walkable(parsed, map, map.Start, why)) {
        error = "start: " + why;
        return false;
    }
    if (!LoadSpawns(*body, parsed, map, error) || !LoadLinks(*body, parsed, map, error) ||
        !LoadPoints(*body, parsed, map, error))
        return false;
    parsed.Maps.push_back(std::move(map));
    return true;
}

// Link targets are resolved here so a map may name another file that sorts later.
bool CheckMapLinks(const Parsed& parsed, std::string& error)
{
    for (const MapDef& map : parsed.Maps) {
        const std::string file = "maps/" + map.Id + ".json";
        for (const MapLink& link : map.Links) {
            const std::string where = "link at " + Coord(link.Tile);
            const MapDef* target = FindId(parsed.Maps, link.Map);
            if (target == nullptr) {
                error = Err(file, where, "map " + Quote(link.Map) + " does not exist");
                return false;
            }
            std::string why;
            if (!Walkable(parsed, *target, link.Arrival, why)) {
                error = Err(file, where, "arrival " + why);
                return false;
            }
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

    if (files.size() < std::size(kFileNames) + 1) {
        error = "internal: data file list";
        return false;
    }

    using Loader = bool (*)(const Json&, Parsed&, std::string&);
    const Loader loaders[] = {LoadStats,   LoadEffects, LoadSkills,     LoadStatuses,
                              LoadClasses, LoadEnemies, LoadEncounters, LoadItems,
                              LoadShop,    LoadCombat,  LoadProgress,   LoadRarities,
                              LoadTiles};
    static_assert(std::size(loaders) == std::size(kFileNames));
    const char* names[] = {"stats.json",   "effects.json", "skills.json",     "statuses.json",
                           "classes.json", "enemies.json", "encounters.json", "items.json",
                           "shop.json",    "combat.json",  "progress.json",   "rarities.json",
                           "tiles.json"};
    static_assert(std::size(names) == std::size(kFileNames));

    for (usize i = 0; i < std::size(kFileNames); ++i) {
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
        const char* allowedTop[] = {"stats",   "effects", "skills",     "statuses",
                                    "classes", "enemies", "encounters", "items",
                                    "shop",    "combat",  "progress",   "rarities",
                                    "tiles"};
        static_assert(std::size(allowedTop) == std::size(kFileNames));
        const bool keysOk = std::string_view(names[i]) == "effects.json"
                                ? AllowKeys(root, {"better", "effects"}, unknown)
                                : AllowKeys(root, {allowedTop[i]}, unknown);
        if (!keysOk) {
            error = Err(names[i], {}, "unknown field " + Quote(unknown));
            return false;
        }
        if (!loaders[i](root, parsed, why)) {
            error = Err(names[i], {}, why);
            return false;
        }
    }
    {
        std::string text;
        std::string why;
        const std::filesystem::path& iconPath = files[std::size(kFileNames)];
        if (!ReadText(iconPath, text, why)) {
            error = Err("icons_sheet.json", {}, why);
            return false;
        }
        Json iconRoot;
        if (!ParseRoot(text, iconRoot, why) || !LoadIconSheet(iconRoot, parsed, why)) {
            error = Err("icons_sheet.json", {}, why);
            return false;
        }
    }
    // Dialogue files come before maps, so a talk point can name a dialogue id.
    for (usize fileIndex = std::size(kFileNames) + 1; fileIndex < files.size(); ++fileIndex) {
        const std::filesystem::path& path = files[fileIndex];
        const std::string folder = path.parent_path().filename().string();
        const bool dialogue = folder == "dialogue";
        const bool map = folder == "maps";
        if (!dialogue && !map) {
            error = Err(path.filename().string(), {}, "internal: unexpected data file");
            return false;
        }
        const std::string fileName = folder + "/" + path.filename().string();
        std::string text;
        std::string why;
        if (!ReadText(path, text, why)) {
            error = Err(fileName, {}, why);
            return false;
        }
        Json root;
        if (!ParseRoot(text, root, why)) {
            error = Err(fileName, {}, why);
            return false;
        }
        std::string top;
        const char* topKey = dialogue ? "dialogue" : "map";
        if (!AllowKeys(root, {topKey}, top)) {
            error = Err(fileName, {}, "unknown field " + Quote(top));
            return false;
        }
        const bool loaded = dialogue ? LoadDialogueFile(root, path.stem().string(), parsed, why)
                                     : LoadMapFile(root, path.stem().string(), parsed, why);
        if (!loaded) {
            error = Err(fileName, {}, why);
            return false;
        }
    }
    std::string linkError;
    if (!CheckMapLinks(parsed, linkError)) {
        error = std::move(linkError);
        return false;
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
        else if (why.compare(0, 7, "status ") == 0)
            file = "statuses.json";
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

// JSON files directly inside `directory/folderName`, sorted so a reload sees a stable
// order. A missing folder is an empty list: no talks, or no maps, still loads.
std::vector<std::filesystem::path> JsonInFolder(const std::filesystem::path& directory,
                                                const char* folderName)
{
    std::vector<std::filesystem::path> paths;
    const std::filesystem::path folder = directory / folderName;
    std::error_code error;
    if (!std::filesystem::is_directory(folder, error))
        return paths;
    const std::filesystem::directory_iterator end;
    for (std::filesystem::directory_iterator it(folder, error); !error && it != end;
         it.increment(error)) {
        std::error_code fileError;
        if (!it->is_regular_file(fileError) || fileError)
            continue;
        if (it->path().extension() != ".json")
            continue;
        paths.push_back(it->path());
    }
    std::sort(paths.begin(), paths.end());
    return paths;
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
    m_IconSheet = IconSheetFor(m_Directory);
    FileWatch icons;
    icons.Path = m_IconSheet;
    m_Watches.push_back(std::move(icons));
    // Dialogue first, then maps, matching the order ReadAll parses.
    const char* folders[] = {"dialogue", "maps"};
    for (const char* folder : folders) {
        for (const std::filesystem::path& path : JsonInFolder(m_Directory, folder)) {
            FileWatch watch;
            watch.Path = path;
            m_Watches.push_back(std::move(watch));
        }
    }
}

bool Database::RefreshFolderWatches()
{
    const std::vector<std::filesystem::path> dialogue = JsonInFolder(m_Directory, "dialogue");
    const std::vector<std::filesystem::path> maps = JsonInFolder(m_Directory, "maps");
    std::vector<std::filesystem::path> paths;
    paths.reserve(dialogue.size() + maps.size());
    for (const std::filesystem::path& path : dialogue)
        paths.push_back(path);
    for (const std::filesystem::path& path : maps)
        paths.push_back(path);
    const usize fixed = std::size(kFileNames) + 1;
    bool changed = m_Watches.size() != fixed + paths.size();
    if (!changed) {
        for (usize index = 0; index < paths.size(); ++index) {
            if (m_Watches[fixed + index].Path != paths[index])
                changed = true;
        }
    }
    if (!changed)
        return false;

    std::vector<FileWatch> next;
    next.reserve(fixed + paths.size());
    for (usize index = 0; index < fixed && index < m_Watches.size(); ++index)
        next.push_back(std::move(m_Watches[index]));
    for (const std::filesystem::path& path : paths) {
        FileWatch watch;
        watch.Path = path;
        bool known = false;
        for (const FileWatch& old : m_Watches) {
            if (old.Path == path) {
                watch = old;
                known = true;
                break;
            }
        }
        if (!known) {
            watch.Path = path;
            watch.Waiting = true;
            watch.Seen = std::nullopt;
        }
        next.push_back(std::move(watch));
    }
    m_Watches = std::move(next);
    return true;
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
    m_Rarities = std::move(parsed.Rarities);
    m_Icons = std::move(parsed.Icons);
    m_Shop = std::move(parsed.Shop);
    m_Dialogues = std::move(parsed.Dialogues);
    m_Tiles = std::move(parsed.Tiles);
    m_Maps = std::move(parsed.Maps);
    m_Rules = parsed.Rules;
    m_Progress = std::move(parsed.Progress);
    m_HigherFields = std::move(parsed.HigherFields);
    m_LowerFields = std::move(parsed.LowerFields);
    ApplyTimes(times);
    ++m_Revision;
    NoteSuccess(error);
    return true;
}

u32 Database::CheckForChanges(DataError* error)
{
    const bool listingChanged = RefreshFolderWatches();
    if (listingChanged)
        m_ListingArmed = true;
    bool pending = false;
    bool stable = true;
    if (m_ListingArmed) {
        pending = true;
        if (listingChanged)
            stable = false;
    }
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
    m_ListingArmed = false;
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

const RarityDef* Database::FindRarity(std::string_view id) const
{
    return FindId(m_Rarities, id);
}

const IconDef* Database::FindIcon(std::string_view id) const
{
    return FindId(m_Icons, id);
}

const DialogueDef* Database::FindDialogue(std::string_view id) const
{
    return FindId(m_Dialogues, id);
}

const TileDef* Database::FindTile(std::string_view id) const
{
    return FindId(m_Tiles, id);
}

const MapDef* Database::FindMap(std::string_view id) const
{
    return FindId(m_Maps, id);
}

MapSpawnRef Database::FindSpawn(std::string_view id) const
{
    MapSpawnRef ref;
    for (const MapDef& map : m_Maps) {
        const MapSpawn* spawn = map.FindSpawn(id);
        if (spawn == nullptr)
            continue;
        ref.Map = &map;
        ref.Spawn = spawn;
        return ref;
    }
    return ref;
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

std::vector<const SkillPick*> Database::PicksOf(const ClassDef& klass) const
{
    std::vector<const SkillPick*> out;
    if (klass.IsEvolution()) {
        if (const ClassDef* parent = FindClass(klass.EvolvesFrom))
            out = PicksOf(*parent);
    }
    for (const SkillPick& pick : klass.Picks)
        out.push_back(&pick);
    return out;
}

std::vector<HeroGrant> Database::HeroGrants(const ClassDef& klass,
                                            std::span<const std::string> wornItemIds,
                                            std::span<const std::string> pickedIds) const
{
    std::vector<HeroGrant> out;
    const auto add = [&](const SkillDef& skill, GrantSource source, const std::string& sourceId) {
        for (HeroGrant& held : out) {
            if (held.Skill.Id != skill.Id)
                continue;
            MergeSkill(held.Skill, skill, m_HigherFields, m_LowerFields);
            PushOrigin(held, source, sourceId);
            return;
        }
        HeroGrant created;
        created.Skill = skill;
        PushOrigin(created, source, sourceId);
        out.push_back(std::move(created));
    };
    for (const SkillDef* active : ActiveSkills(klass))
        add(*active, GrantSource::Class, klass.Id);
    for (const SkillPick* pick : PicksOf(klass)) {
        for (const SkillGrant& option : pick->Options) {
            for (const std::string& picked : pickedIds) {
                if (picked == option.Id)
                    add(option.Resolved, GrantSource::Class, klass.Id);
            }
        }
    }
    for (const SkillDef* passive : Passives(klass))
        add(*passive, GrantSource::Class, klass.Id);
    for (const std::string& wornId : wornItemIds) {
        const ItemDef* piece = FindItem(wornId);
        if (piece == nullptr)
            continue;
        for (const SkillGrant& listed : piece->Grants)
            add(listed.Resolved, GrantSource::Item, piece->Id);
    }
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

namespace {

EffectAim AimFor(const SkillDef& skill, const EffectDef& effect)
{
    if (effect.Aim.has_value())
        return *effect.Aim;
    if (skill.Target == SkillTarget::Self)
        return EffectAim::Self;
    if (skill.Target == SkillTarget::AllEnemies)
        return EffectAim::AllEnemies;
    return EffectAim::Target;
}

const char* AimWords(EffectAim aim)
{
    if (aim == EffectAim::Self)
        return "you";
    if (aim == EffectAim::AllEnemies)
        return "every enemy";
    if (aim == EffectAim::RandomEnemy)
        return "a random enemy";
    if (aim == EffectAim::LowestHpAlly)
        return "the most wounded ally";
    return "the marked enemy";
}

// Empty for an op that does not pick someone. Battle aims damage, a status, a stun, and a
// heal that names its own target. A guard and a heal with no target stay on the user, so
// they add no sentence.
std::string EffectSentence(const EffectDef& effect, EffectAim aim)
{
    if (effect.Op == EffectOp::Heal) {
        if (!effect.Aim.has_value() || aim == EffectAim::Self)
            return {};
        return std::string("Heals ") + AimWords(aim) + ".";
    }
    const char* verb = nullptr;
    if (effect.Op == EffectOp::Damage)
        verb = "Hits";
    else if (effect.Op == EffectOp::ApplyStatus)
        verb = "Afflicts";
    else if (effect.Op == EffectOp::StunIfWindingUp)
        verb = "Stuns";
    if (verb == nullptr)
        return {};
    return std::string(verb) + " " + AimWords(aim) + ".";
}

} // namespace

std::string Database::DescribeSkill(const SkillDef& skill, i32 rank) const
{
    TextScope scope = CatalogOf(*this);
    scope.Skill = &skill;
    scope.RankBonus = RankBonusFor(rank, m_Progress.PowerPerRank);
    std::string text = ExpandOrRaw(skill.Description, scope);
    for (const EffectDef& effect : skill.Effects) {
        const std::string sentence = EffectSentence(effect, AimFor(skill, effect));
        if (sentence.empty())
            continue;
        if (!text.empty())
            text += " ";
        text += sentence;
    }
    return text;
}

std::string Database::Describe(const ClassDef& def) const
{
    TextScope scope = CatalogOf(*this);
    scope.Class = &def;
    return ExpandOrRaw(def.Description, scope);
}

std::string Database::Describe(const EnemyDef& def) const
{
    TextScope scope = CatalogOf(*this);
    scope.Enemy = &def;
    return ExpandOrRaw(def.Description, scope);
}

std::string Database::Describe(const StatusDef& def) const
{
    TextScope scope = CatalogOf(*this);
    scope.Status = &def;
    return ExpandOrRaw(def.Description, scope);
}

std::string Database::Describe(const ItemDef& def) const
{
    TextScope scope = CatalogOf(*this);
    scope.Item = &def;
    return ExpandOrRaw(def.Description, scope);
}

} // namespace Ascension
