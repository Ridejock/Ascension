#include "Ascension/Progress/RunSave.h"

#include <string>
#include <vector>

#include "Emerald/Save/Save.h"

namespace Ascension {

namespace {

constexpr i32 kMaxEntries = 256;
constexpr std::string_view kSlot = "run";

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

Emerald::SaveSystem Open(const std::filesystem::path& folder)
{
    Emerald::SaveSystem system(folder, RunSave::kVersion);
    system.AddMigration(1, MigrateRunV1);
    return system;
}

void Store(const RunState& run, Emerald::SaveData& data)
{
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

    data.SetInt("position", run.Position);
    data.SetString("direct", run.DirectEnemy);
    data.SetString("standing", StandingName(run.Standing));
    data.SetBool("resources", run.ResourcesKnown);
    data.SetInt("hp", run.Hp);
    data.SetInt("focus", run.Focus);
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
    if (!CountOk(skills, error) || !CountOk(picks, error) || !CountOk(steps, error) ||
        !CountOk(sources, error) || !CountOk(bags, error) || !CountOk(foes, error))
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

    run.Position = data.GetInt("position", 0);
    run.DirectEnemy = data.GetString("direct");
    run.Standing = StandingFrom(data.GetString("standing"));
    run.ResourcesKnown = data.GetBool("resources", false);
    run.Hp = data.GetInt("hp", 0);
    run.Focus = data.GetInt("focus", 0);
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
    const std::string summary = run.Career.ClassId + " xp " + std::to_string(run.Career.Xp);
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

} // namespace Ascension
