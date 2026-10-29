#include "Ascension/Progress/RunSave.h"

#include <string>
#include <vector>

#include "Emerald/Save/Save.h"

namespace Ascension {

namespace {

constexpr i32 kMaxEntries = 256;
constexpr std::string_view kSlot = "run";

bool Listed(const std::vector<std::string>& ids, std::string_view id)
{
    for (const std::string& held : ids) {
        if (held == id)
            return true;
    }
    return false;
}

std::string Key(std::string_view prefix, i32 index, std::string_view field)
{
    std::string key;
    key.reserve(prefix.size() + field.size() + 8);
    key.append(prefix);
    key.push_back('.');
    key += std::to_string(index);
    if (!field.empty()) {
        key.push_back('.');
        key.append(field);
    }
    return key;
}

const char* StandingName(RunStanding standing)
{
    if (standing == RunStanding::Ongoing)
        return "ongoing";
    if (standing == RunStanding::Victory)
        return "victory";
    if (standing == RunStanding::Defeat)
        return "defeat";
    return "none";
}

RunStanding StandingFrom(std::string_view text)
{
    if (text == "ongoing")
        return RunStanding::Ongoing;
    if (text == "victory")
        return RunStanding::Victory;
    if (text == "defeat")
        return RunStanding::Defeat;
    return RunStanding::None;
}

void WriteStats(Emerald::SaveData& data, const std::string& prefix, const StatBlock& stats)
{
    for (const std::string_view id : kStatIds) {
        std::string key = prefix;
        key.push_back('.');
        key.append(id);
        data.SetInt(key, stats.Get(id));
    }
}

StatBlock ReadStats(const Emerald::SaveData& data, const std::string& prefix)
{
    StatBlock stats;
    for (const std::string_view id : kStatIds) {
        std::string key = prefix;
        key.push_back('.');
        key.append(id);
        stats.Set(id, data.GetInt(key, 0));
    }
    return stats;
}

void SplitFoes(Emerald::SaveData& data)
{
    if (!data.Has("foes"))
        return;
    if (data.Has("defeated.count")) {
        data.Remove("foes");
        return;
    }
    const std::string packed = data.GetString("foes");
    data.Remove("foes");
    std::vector<std::string> ids;
    std::string current;
    for (const char c : packed) {
        if (c == ',') {
            if (!current.empty())
                ids.push_back(std::move(current));
            current.clear();
            continue;
        }
        if (c == ' ')
            continue;
        current.push_back(c);
    }
    if (!current.empty())
        ids.push_back(std::move(current));
    data.SetInt("defeated.count", static_cast<i32>(ids.size()));
    for (i32 index = 0; index < static_cast<i32>(ids.size()); ++index)
        data.SetString(Key("defeated", index, {}), ids[static_cast<usize>(index)]);
}

// Version 1 stored the wound as health/resource, the encounter index as place, and the
// defeated foes as one comma-separated string. Version 2 uses the names the game reads now.
void MigrateRunV1(Emerald::SaveData& data)
{
    const bool hadHealth = data.Has("health");
    if (hadHealth)
        data.Rename("health", "hp");
    if (data.Has("resource"))
        data.Rename("resource", "focus");
    if (data.Has("place"))
        data.Rename("place", "position");
    if (hadHealth && !data.Has("resources"))
        data.SetBool("resources", true);
    SplitFoes(data);
}

// Version 2 remembered the hero's wound, not the foe or whose turn it was. Leave the fight
// unknown so that ongoing file starts the encounter over.
void MigrateRunV2(Emerald::SaveData& data)
{
    if (!data.Has("fight.known"))
        data.SetBool("fight.known", false);
}

// Version 4 has a class and no personal name. Version 5 stores `name`. An empty name
// is the default, so an old Fighter still shows as Fighter. A file that somehow has
// no class becomes fighter; a class the file already names is left alone.
void MigrateRunV4(Emerald::SaveData& data)
{
    if (!data.Has("name"))
        data.SetString("name", "");
    if (!data.Has("class") || data.GetString("class").empty())
        data.SetString("class", "fighter");
}

// Version 3's position is an encounter-list index. Version 4 stores a map, a tile, and
// spawn ids. The index waits under legacy.index until the maps are loaded. A finished
// fight did not carry HP in version 3; drop a leftover health field so it still does not.
void MigrateRunV3(Emerald::SaveData& data)
{
    if (data.Has("position") && !data.Has("legacy.index")) {
        data.SetInt("legacy.index", data.GetInt("position", 0));
        data.Remove("position");
    }
    const std::string standing = data.GetString("standing");
    if (standing == "victory" || standing == "defeat") {
        data.Remove("resources");
        data.Remove("hp");
        data.Remove("focus");
    }
}

Emerald::SaveSystem Open(const std::filesystem::path& folder)
{
    Emerald::SaveSystem system(folder, RunSave::kVersion);
    system.AddMigration(1, MigrateRunV1);
    system.AddMigration(2, MigrateRunV2);
    system.AddMigration(3, MigrateRunV3);
    system.AddMigration(4, MigrateRunV4);
    return system;
}

bool CountOk(i32 count, std::string& error);

const char* SideName(CombatantSide side)
{
    if (side == CombatantSide::Enemy)
        return "enemy";
    return "player";
}

bool SideFrom(std::string_view text, CombatantSide& side)
{
    if (text == "enemy") {
        side = CombatantSide::Enemy;
        return true;
    }
    if (text == "player") {
        side = CombatantSide::Player;
        return true;
    }
    return false;
}

void WriteUnit(Emerald::SaveData& data, const std::string& prefix, const CombatantSnapshot& unit)
{
    data.SetString(prefix + ".id", unit.Id);
    data.SetInt(prefix + ".hp", unit.Hp);
    data.SetInt(prefix + ".focus", unit.Focus);
    data.SetInt(prefix + ".guard", unit.GuardPercent);
    data.SetInt(prefix + ".empower", unit.EmpowerPercent);
    data.SetInt(prefix + ".reflect", unit.ReflectPercent);
    data.SetInt(prefix + ".reflectFlat", unit.ReflectFlat);
    data.SetInt(prefix + ".defense", unit.BonusDefense);
    data.SetInt(prefix + ".defenseTurns", unit.BonusDefenseTurns);
    data.SetString(prefix + ".windup", unit.WindupSkill);
    data.SetInt(prefix + ".windupLeft", unit.WindupLeft);
    data.SetInt(prefix + ".windupTotal", unit.WindupTotal);
    data.SetInt(prefix + ".step", unit.PatternStep);
    data.SetInt(prefix + ".phase", unit.Phase);
    data.SetInt(prefix + ".phaseTurns", unit.PhaseTurns);
    data.SetBool(prefix + ".shard", unit.ShardDue);

    data.SetInt(prefix + ".status.count", static_cast<i32>(unit.Statuses.size()));
    for (i32 index = 0; index < static_cast<i32>(unit.Statuses.size()); ++index) {
        const StatusInstance& status = unit.Statuses[static_cast<usize>(index)];
        const std::string key = Key(prefix + ".status", index, {});
        data.SetString(key + ".id", status.Id);
        data.SetInt(key + ".stacks", status.Stacks);
        data.SetInt(key + ".turns", status.TurnsLeft);
    }

    data.SetInt(prefix + ".use.count", static_cast<i32>(unit.SkillUseIds.size()));
    for (i32 index = 0; index < static_cast<i32>(unit.SkillUseIds.size()); ++index) {
        const std::string key = Key(prefix + ".use", index, {});
        data.SetString(key + ".id", unit.SkillUseIds[static_cast<usize>(index)]);
        const i32 uses = index < static_cast<i32>(unit.SkillUseCounts.size())
                             ? unit.SkillUseCounts[static_cast<usize>(index)]
                             : 0;
        data.SetInt(key + ".count", uses);
    }
}

bool ReadUnit(const Emerald::SaveData& data, const std::string& prefix, CombatantSnapshot& unit,
              std::string& error)
{
    unit = {};
    unit.Id = data.GetString(prefix + ".id");
    unit.Hp = data.GetInt(prefix + ".hp", 0);
    unit.Focus = data.GetInt(prefix + ".focus", 0);
    unit.GuardPercent = data.GetInt(prefix + ".guard", 0);
    unit.EmpowerPercent = data.GetInt(prefix + ".empower", 0);
    unit.ReflectPercent = data.GetInt(prefix + ".reflect", 0);
    unit.ReflectFlat = data.GetInt(prefix + ".reflectFlat", 0);
    unit.BonusDefense = data.GetInt(prefix + ".defense", 0);
    unit.BonusDefenseTurns = data.GetInt(prefix + ".defenseTurns", 0);
    unit.WindupSkill = data.GetString(prefix + ".windup");
    unit.WindupLeft = data.GetInt(prefix + ".windupLeft", 0);
    unit.WindupTotal = data.GetInt(prefix + ".windupTotal", 0);
    unit.PatternStep = data.GetInt(prefix + ".step", 0);
    unit.Phase = data.GetInt(prefix + ".phase", 1);
    unit.PhaseTurns = data.GetInt(prefix + ".phaseTurns", 0);
    unit.ShardDue = data.GetBool(prefix + ".shard", false);

    const i32 statuses = data.GetInt(prefix + ".status.count", 0);
    const i32 uses = data.GetInt(prefix + ".use.count", 0);
    if (!CountOk(statuses, error) || !CountOk(uses, error))
        return false;
    unit.Statuses.resize(static_cast<usize>(statuses));
    for (i32 index = 0; index < statuses; ++index) {
        const std::string key = Key(prefix + ".status", index, {});
        StatusInstance& status = unit.Statuses[static_cast<usize>(index)];
        status.Id = data.GetString(key + ".id");
        status.Stacks = data.GetInt(key + ".stacks", 1);
        status.TurnsLeft = data.GetInt(key + ".turns", 0);
        if (status.Id.empty() || status.Stacks < 1) {
            error = "the save's statuses are damaged";
            return false;
        }
    }
    unit.SkillUseIds.resize(static_cast<usize>(uses));
    unit.SkillUseCounts.resize(static_cast<usize>(uses));
    for (i32 index = 0; index < uses; ++index) {
        const std::string key = Key(prefix + ".use", index, {});
        const std::string id = data.GetString(key + ".id");
        if (id.empty()) {
            error = "the save's skill counts are damaged";
            return false;
        }
        unit.SkillUseIds[static_cast<usize>(index)] = id;
        const i32 count = data.GetInt(key + ".count", 0);
        unit.SkillUseCounts[static_cast<usize>(index)] = count < 0 ? 0 : count;
    }
    error.clear();
    return true;
}

void WriteFight(const FightSnapshot& fight, Emerald::SaveData& data)
{
    data.SetBool("fight.known", fight.Known);
    if (!fight.Known)
        return;
    data.SetInt("fight.turn", fight.TurnCount);
    data.SetString("fight.actor", SideName(fight.Actor));
    data.SetInt("fight.actorIndex", fight.ActorIndex);
    data.SetInt("fight.target", fight.Target);
    data.SetInt("fight.random", fight.RandomCursor);
    data.SetBool("fight.upkeep", fight.UpkeepDone);
    data.SetInt("fight.order.count", static_cast<i32>(fight.Order.size()));
    data.SetInt("fight.orderAt", fight.OrderIndex);
    for (i32 index = 0; index < static_cast<i32>(fight.Order.size()); ++index) {
        const TurnSeat& seat = fight.Order[static_cast<usize>(index)];
        data.SetString(Key("fight.order", index, "side"), SideName(seat.Side));
        data.SetInt(Key("fight.order", index, "index"), seat.Index);
        data.SetInt(Key("fight.order", index, "agility"), seat.Agility);
    }
    WriteUnit(data, "fight.player", fight.Player);
    data.SetInt("fight.foe.count", static_cast<i32>(fight.Foes.size()));
    for (i32 index = 0; index < static_cast<i32>(fight.Foes.size()); ++index)
        WriteUnit(data, Key("fight.foe", index, {}), fight.Foes[static_cast<usize>(index)]);
}

bool ReadFight(const Emerald::SaveData& data, FightSnapshot& fight, std::string& error)
{
    fight = {};
    if (!data.GetBool("fight.known", false)) {
        error.clear();
        return true;
    }
    fight.Known = true;
    fight.TurnCount = data.GetInt("fight.turn", 0);
    if (!SideFrom(data.GetString("fight.actor"), fight.Actor)) {
        error = "the save does not say whose turn it is";
        return false;
    }
    fight.ActorIndex = data.GetInt("fight.actorIndex", -1);
    fight.Target = data.GetInt("fight.target", 0);
    fight.RandomCursor = data.GetInt("fight.random", 0);
    fight.UpkeepDone = data.GetBool("fight.upkeep", false);
    const i32 seats = data.GetInt("fight.order.count", 0);
    const i32 foes = data.GetInt("fight.foe.count", 0);
    if (!CountOk(seats, error) || !CountOk(foes, error))
        return false;
    if (seats < 1 || foes < 1) {
        error = "the save's fight is damaged";
        return false;
    }
    fight.OrderIndex = data.GetInt("fight.orderAt", 0);
    fight.Order.resize(static_cast<usize>(seats));
    for (i32 index = 0; index < seats; ++index) {
        TurnSeat& seat = fight.Order[static_cast<usize>(index)];
        if (!SideFrom(data.GetString(Key("fight.order", index, "side")), seat.Side)) {
            error = "the save's turn order is damaged";
            return false;
        }
        seat.Index = data.GetInt(Key("fight.order", index, "index"), 0);
        seat.Agility = data.GetInt(Key("fight.order", index, "agility"), 0);
    }
    if (!ReadUnit(data, "fight.player", fight.Player, error))
        return false;
    fight.Foes.resize(static_cast<usize>(foes));
    for (i32 index = 0; index < foes; ++index) {
        if (!ReadUnit(data, Key("fight.foe", index, {}), fight.Foes[static_cast<usize>(index)],
                      error))
            return false;
        if (fight.Foes[static_cast<usize>(index)].Id.empty()) {
            error = "the save's foes are damaged";
            return false;
        }
    }
    error.clear();
    return true;
}

void Store(const RunState& run, Emerald::SaveData& data)
{
    data.SetString("name", run.Career.Name);
    data.SetString("class", run.Career.ClassId);
    data.SetInt("xp", run.Career.Xp);
    data.SetBool("custom", run.Career.Custom);
    data.SetBool("choosing", run.Career.ChoosingStarter);
    data.SetInt("pool", run.Career.ClassPool);
    data.SetString("spend", run.Career.SpendId);

    data.SetInt("skill.count", static_cast<i32>(run.Career.Practiced.size()));
    for (i32 index = 0; index < static_cast<i32>(run.Career.Practiced.size()); ++index) {
        data.SetString(Key("skill", index, "id"), run.Career.Practiced[static_cast<usize>(index)]);
        const i32 uses = index < static_cast<i32>(run.Career.Uses.size())
                             ? run.Career.Uses[static_cast<usize>(index)]
                             : 0;
        data.SetInt(Key("skill", index, "uses"), uses);
    }

    data.SetInt("pick.count", static_cast<i32>(run.Career.Picked.size()));
    for (i32 index = 0; index < static_cast<i32>(run.Career.Picked.size()); ++index)
        data.SetString(Key("pick", index, {}), run.Career.Picked[static_cast<usize>(index)]);

    data.SetInt("step.count", static_cast<i32>(run.Career.LevelSteps.size()));
    for (i32 index = 0; index < static_cast<i32>(run.Career.LevelSteps.size()); ++index)
        WriteStats(data, Key("step", index, {}), run.Career.LevelSteps[static_cast<usize>(index)]);

    data.SetInt("source.count", static_cast<i32>(run.Career.Sources.size()));
    for (i32 index = 0; index < static_cast<i32>(run.Career.Sources.size()); ++index) {
        const AttributeSource& source = run.Career.Sources[static_cast<usize>(index)];
        data.SetString(Key("source", index, "id"), source.Id);
        WriteStats(data, Key("source", index, {}), source.Points);
    }

    data.SetInt("gold", run.Bag.Gold);
    data.SetString("worn.weapon", run.Bag.Weapon);
    data.SetString("worn.offhand", run.Bag.Offhand);
    data.SetString("worn.armor", run.Bag.Armor);
    data.SetString("worn.accessory", run.Bag.Accessory);
    data.SetInt("bag.count", static_cast<i32>(run.Bag.Bag.size()));
    for (i32 index = 0; index < static_cast<i32>(run.Bag.Bag.size()); ++index) {
        const ItemStack& stack = run.Bag.Bag[static_cast<usize>(index)];
        data.SetString(Key("bag", index, "id"), stack.Id);
        data.SetInt(Key("bag", index, "count"), stack.Count);
    }

    data.SetInt("defeated.count", static_cast<i32>(run.Defeated.size()));
    for (i32 index = 0; index < static_cast<i32>(run.Defeated.size()); ++index)
        data.SetString(Key("defeated", index, {}), run.Defeated[static_cast<usize>(index)]);

    data.SetInt("found.count", static_cast<i32>(run.Found.size()));
    for (i32 index = 0; index < static_cast<i32>(run.Found.size()); ++index)
        data.SetString(Key("found", index, {}), run.Found[static_cast<usize>(index)]);

    data.SetInt("flag.count", static_cast<i32>(run.Flags.size()));
    for (i32 index = 0; index < static_cast<i32>(run.Flags.size()); ++index)
        data.SetString(Key("flag", index, {}), run.Flags[static_cast<usize>(index)]);

    data.SetString("map", run.MapId);
    data.SetInt("tile.x", run.Tile.X);
    data.SetInt("tile.y", run.Tile.Y);
    data.SetInt("cleared.count", static_cast<i32>(run.Cleared.size()));
    for (i32 index = 0; index < static_cast<i32>(run.Cleared.size()); ++index)
        data.SetString(Key("cleared", index, {}), run.Cleared[static_cast<usize>(index)]);

    data.SetInt("wound.count", static_cast<i32>(run.Wounds.size()));
    for (i32 index = 0; index < static_cast<i32>(run.Wounds.size()); ++index) {
        const FledWounds& wounds = run.Wounds[static_cast<usize>(index)];
        const std::string prefix = Key("wound", index, {});
        data.SetString(prefix + ".spawn", wounds.SpawnId);
        data.SetInt(prefix + ".foe.count", static_cast<i32>(wounds.Foes.size()));
        for (i32 foe = 0; foe < static_cast<i32>(wounds.Foes.size()); ++foe) {
            const std::string foeKey = Key(prefix + ".foe", foe, {});
            data.SetString(foeKey + ".id", wounds.Foes[static_cast<usize>(foe)].Id);
            data.SetInt(foeKey + ".hp", wounds.Foes[static_cast<usize>(foe)].Hp);
        }
    }

    data.SetString("spawn", run.SpawnId);
    data.SetString("encounter", run.EncounterId);
    data.SetString("direct", run.DirectEnemy);
    data.SetString("standing", StandingName(run.Standing));
    data.SetBool("resources", run.ResourcesKnown);
    data.SetInt("hp", run.Hp);
    data.SetInt("focus", run.Focus);
    WriteFight(run.Fight, data);
}

bool CountOk(i32 count, std::string& error)
{
    if (count < 0 || count > kMaxEntries) {
        error = "the save's lists are damaged";
        return false;
    }
    return true;
}

bool Take(const Emerald::SaveData& data, RunState& run, std::string& error)
{
    if (!data.Has("class")) {
        error = "the save has no class";
        return false;
    }
    run.Career.Name = data.GetString("name");
    run.Career.ClassId = data.GetString("class");
    run.Career.Xp = data.GetInt("xp", 0);
    run.Career.Custom = data.GetBool("custom", false);
    run.Career.ChoosingStarter = data.GetBool("choosing", false);
    run.Career.ClassPool = data.GetInt("pool", 0);
    run.Career.SpendId = data.GetString("spend");

    const i32 skills = data.GetInt("skill.count", 0);
    const i32 picks = data.GetInt("pick.count", 0);
    const i32 steps = data.GetInt("step.count", 0);
    const i32 sources = data.GetInt("source.count", 0);
    const i32 bags = data.GetInt("bag.count", 0);
    const i32 foes = data.GetInt("defeated.count", 0);
    const i32 found = data.GetInt("found.count", 0);
    const i32 flags = data.GetInt("flag.count", 0);
    const i32 cleared = data.GetInt("cleared.count", 0);
    const i32 wounds = data.GetInt("wound.count", 0);
    if (!CountOk(skills, error) || !CountOk(picks, error) || !CountOk(steps, error) ||
        !CountOk(sources, error) || !CountOk(bags, error) || !CountOk(foes, error) ||
        !CountOk(found, error) || !CountOk(flags, error) || !CountOk(cleared, error) ||
        !CountOk(wounds, error))
        return false;

    run.Career.Practiced.resize(static_cast<usize>(skills));
    run.Career.Uses.resize(static_cast<usize>(skills));
    for (i32 index = 0; index < skills; ++index) {
        const std::string id = data.GetString(Key("skill", index, "id"));
        if (id.empty()) {
            error = "the save's skill list is damaged";
            return false;
        }
        run.Career.Practiced[static_cast<usize>(index)] = id;
        const i32 uses = data.GetInt(Key("skill", index, "uses"), 0);
        run.Career.Uses[static_cast<usize>(index)] = uses < 0 ? 0 : uses;
    }

    run.Career.Picked.resize(static_cast<usize>(picks));
    for (i32 index = 0; index < picks; ++index) {
        const std::string id = data.GetString(Key("pick", index, {}));
        if (id.empty()) {
            error = "the save's skill picks are damaged";
            return false;
        }
        run.Career.Picked[static_cast<usize>(index)] = id;
    }

    if (run.Career.Custom) {
        run.Career.LevelSteps.resize(static_cast<usize>(steps));
        for (i32 index = 0; index < steps; ++index)
            run.Career.LevelSteps[static_cast<usize>(index)] =
                ReadStats(data, Key("step", index, {}));
        run.Career.Sources.resize(static_cast<usize>(sources));
        for (i32 index = 0; index < sources; ++index) {
            AttributeSource& source = run.Career.Sources[static_cast<usize>(index)];
            source.Id = data.GetString(Key("source", index, "id"));
            if (source.Id.empty()) {
                error = "the save's stats are damaged";
                return false;
            }
            source.Points = ReadStats(data, Key("source", index, {}));
        }
    }

    run.Bag.Gold = data.GetInt("gold", 0);
    run.Bag.Weapon = data.GetString("worn.weapon");
    run.Bag.Offhand = data.GetString("worn.offhand");
    run.Bag.Armor = data.GetString("worn.armor");
    run.Bag.Accessory = data.GetString("worn.accessory");
    run.Bag.Bag.resize(static_cast<usize>(bags));
    for (i32 index = 0; index < bags; ++index) {
        ItemStack& stack = run.Bag.Bag[static_cast<usize>(index)];
        stack.Id = data.GetString(Key("bag", index, "id"));
        stack.Count = data.GetInt(Key("bag", index, "count"), 0);
        if (stack.Count < 0)
            stack.Count = 0;
        if (stack.Count == 0)
            stack.Id.clear();
    }

    run.Defeated.resize(static_cast<usize>(foes));
    for (i32 index = 0; index < foes; ++index) {
        const std::string id = data.GetString(Key("defeated", index, {}));
        if (id.empty()) {
            error = "the save's defeated foes are damaged";
            return false;
        }
        run.Defeated[static_cast<usize>(index)] = id;
    }

    run.Found.resize(static_cast<usize>(found));
    for (i32 index = 0; index < found; ++index) {
        const std::string id = data.GetString(Key("found", index, {}));
        if (id.empty()) {
            error = "the save's found items are damaged";
            return false;
        }
        run.Found[static_cast<usize>(index)] = id;
    }

    run.Flags.resize(static_cast<usize>(flags));
    for (i32 index = 0; index < flags; ++index) {
        const std::string id = data.GetString(Key("flag", index, {}));
        if (id.empty()) {
            error = "the save's dialogue flags are damaged";
            return false;
        }
        run.Flags[static_cast<usize>(index)] = id;
    }

    run.MapId = data.GetString("map");
    run.Tile.X = data.GetInt("tile.x", 0);
    run.Tile.Y = data.GetInt("tile.y", 0);
    run.Cleared.resize(static_cast<usize>(cleared));
    for (i32 index = 0; index < cleared; ++index) {
        const std::string id = data.GetString(Key("cleared", index, {}));
        if (id.empty()) {
            error = "the save's cleared spawns are damaged";
            return false;
        }
        run.Cleared[static_cast<usize>(index)] = id;
    }
    run.Wounds.resize(static_cast<usize>(wounds));
    for (i32 index = 0; index < wounds; ++index) {
        const std::string prefix = Key("wound", index, {});
        FledWounds& wound = run.Wounds[static_cast<usize>(index)];
        wound.SpawnId = data.GetString(prefix + ".spawn");
        if (wound.SpawnId.empty()) {
            error = "the save's flee wounds are damaged";
            return false;
        }
        const i32 foesInWound = data.GetInt(prefix + ".foe.count", 0);
        if (!CountOk(foesInWound, error))
            return false;
        wound.Foes.resize(static_cast<usize>(foesInWound));
        for (i32 foe = 0; foe < foesInWound; ++foe) {
            const std::string foeKey = Key(prefix + ".foe", foe, {});
            FoeWound& body = wound.Foes[static_cast<usize>(foe)];
            body.Id = data.GetString(foeKey + ".id");
            if (body.Id.empty()) {
                error = "the save's flee wounds are damaged";
                return false;
            }
            const i32 hp = data.GetInt(foeKey + ".hp", 0);
            body.Hp = hp < 0 ? 0 : hp;
        }
    }
    run.SpawnId = data.GetString("spawn");
    run.EncounterId = data.GetString("encounter");
    run.LegacyIndex = data.Has("legacy.index") ? data.GetInt("legacy.index", 0) : -1;
    run.DirectEnemy = data.GetString("direct");
    run.Standing = StandingFrom(data.GetString("standing"));
    run.ResourcesKnown = data.GetBool("resources", false);
    run.Hp = data.GetInt("hp", 0);
    run.Focus = data.GetInt("focus", 0);
    std::string fightError;
    if (!ReadFight(data, run.Fight, fightError))
        run.Fight = {};
    error.clear();
    return true;
}

RunSaveError MapError(Emerald::SaveError error)
{
    if (error == Emerald::SaveError::NotFound)
        return RunSaveError::NotFound;
    if (error == Emerald::SaveError::TooNew)
        return RunSaveError::TooNew;
    if (error == Emerald::SaveError::NoMigration)
        return RunSaveError::NoMigration;
    if (error == Emerald::SaveError::WriteFailed)
        return RunSaveError::WriteFailed;
    if (error == Emerald::SaveError::None)
        return RunSaveError::None;
    return RunSaveError::Corrupt;
}

} // namespace

RunSave::RunSave(std::filesystem::path folder) : m_Folder(std::move(folder))
{
}

std::filesystem::path RunSave::DefaultFolder()
{
    return Emerald::SaveSystem::DefaultFolder("Ridejock", "Ascension");
}

bool RunSave::Write(const RunState& run, std::string& error) const
{
    Emerald::SaveData data;
    Store(run, data);
    Emerald::SaveSystem system = Open(m_Folder);
    std::string summary = run.Career.ClassId + " xp " + std::to_string(run.Career.Xp);
    if (!run.Career.Name.empty())
        summary = run.Career.Name + " " + summary;
    if (!system.Save(kSlot, data, summary)) {
        error = "could not write the save";
        return false;
    }
    error.clear();
    return true;
}

RunLoad RunSave::Read() const
{
    const Emerald::SaveSystem system = Open(m_Folder);
    const Emerald::LoadResult loaded = system.Load(kSlot);
    RunLoad out;
    out.FileVersion = loaded.FileVersion;
    out.FromBackup = loaded.FromBackup;
    if (!loaded) {
        out.Error = MapError(loaded.Error);
        out.Message = loaded.Message;
        return out;
    }
    std::string why;
    if (!Take(loaded.Data, out.Run, why)) {
        out.Error = RunSaveError::BadData;
        out.Message = why;
        out.FromBackup = false;
        return out;
    }
    out.Error = RunSaveError::None;
    return out;
}

void AdoptLegacyMap(const Database& data, RunState& run)
{
    if (run.LegacyIndex < 0)
        return;
    const i32 index = run.LegacyIndex;
    run.LegacyIndex = -1;

    const auto encounters = data.GetEncounters();
    if (run.EncounterId.empty() && !encounters.empty()) {
        i32 at = index;
        if (at < 0 || at >= static_cast<i32>(encounters.size()))
            at = 0;
        run.EncounterId = encounters[static_cast<usize>(at)].Id;
    }

    if (run.MapId.empty()) {
        const MapDef* home = nullptr;
        for (const MapDef& map : data.GetMaps()) {
            if (map.Hub)
                home = &map;
        }
        if (home == nullptr && !data.GetMaps().empty())
            home = &data.GetMaps().front();
        if (home != nullptr) {
            run.MapId = home->Id;
            run.Tile = home->Start;
        }
    }

    for (const MapDef& map : data.GetMaps()) {
        for (const MapSpawn& spawn : map.Spawns) {
            if (!spawn.Once || spawn.Id.empty() || spawn.Encounter.empty())
                continue;
            if (!Listed(run.Defeated, spawn.Encounter) || Listed(run.Cleared, spawn.Id))
                continue;
            run.Cleared.push_back(spawn.Id);
        }
    }
}

} // namespace Ascension
