#include "FightSim.h"

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Ascension/Progress/Hero.h"
#include "Ascension/Progress/Inventory.h"
#include "Playtest.h"

namespace Ascension {

namespace {

struct BuildSpec {
    const char* Label;
    const char* ClassId;
    const char* Pick;
    const char* Practiced;
};

const BuildSpec kBuilds[] = {
    {"Fighter", "fighter", nullptr, nullptr},
    {"Berserker rend", "berserker", "rend", "cleave"},
    {"Berserker brace", "berserker", "brace", "cleave"},
    {"Guardian riposte", "guardian", "riposte", "shield_bash"},
    {"Guardian shield wall", "guardian", "shield_wall", "shield_bash"},
};

constexpr i32 kGoblinLevel = 6;
constexpr i32 kStrikeUses = 10;
constexpr i32 kPracticedUses = 3;
constexpr i32 kActionCap = 400;

bool Holds(const CombatantState& who, std::string_view id)
{
    for (const StatusInstance& status : who.Statuses) {
        if (status.Id == id)
            return true;
    }
    return false;
}

i32 HpPercent(i32 hp, i32 maxHp)
{
    if (maxHp < 1)
        maxHp = 1;
    if (hp < 0)
        hp = 0;
    const i64 scaled = static_cast<i64>(hp) * 100 / maxHp;
    return static_cast<i32>(scaled);
}

i32 SpendableFocus(const CombatantState& player, i32 perTurn)
{
    i32 focus = player.Focus;
    if (perTurn > 0 && focus < player.MaxFocus) {
        focus += perTurn;
        if (focus > player.MaxFocus)
            focus = player.MaxFocus;
    }
    return focus;
}

const SkillDef* FindActive(const std::vector<HeroGrant>& grants, std::string_view id)
{
    for (const HeroGrant& grant : grants) {
        if (grant.Skill.Id == id && grant.Skill.Kind == SkillKind::Active)
            return &grant.Skill;
    }
    return nullptr;
}

bool CanCast(const std::vector<HeroGrant>& grants, std::string_view id, i32 focus)
{
    const SkillDef* skill = FindActive(grants, id);
    return skill != nullptr && skill->FocusCost <= focus;
}

// A foe this low dies to one strike from the kit this sim uses. Healing instead leaves
// the pack another turn.
bool FoeIsFinishing(const Battle& battle)
{
    for (const CombatantState& foe : battle.Enemies()) {
        if (foe.Alive() && foe.Hp <= 36)
            return true;
    }
    return false;
}

// Commit to the marked foe. Buff once, heal or guard only when health is low, and
// otherwise spend the best single-target attack that focus can pay. Cleave is left
// unused so the kill order is the thing being measured.
std::string_view ChoosePlayerSkill(const Battle& battle, const std::vector<HeroGrant>& grants,
                                   i32 perTurn)
{
    const CombatantState& player = battle.Player();
    const i32 focus = SpendableFocus(player, perTurn);
    const i32 percent = HpPercent(player.Hp, player.MaxHp);
    if (CanCast(grants, "fang_bloodlust", focus) && !Holds(player, "bloodlust"))
        return "fang_bloodlust";
    if (CanCast(grants, "rage", focus) && !Holds(player, "fury") && percent > 50)
        return "rage";
    const bool finishing = FoeIsFinishing(battle);
    if (!finishing && percent <= 32 && CanCast(grants, "second_wind", focus))
        return "second_wind";
    if (!finishing && percent <= 32 && player.GuardPercent <= 0 &&
        CanCast(grants, "shield_wall", focus))
        return "shield_wall";
    if (!finishing && percent <= 32 && player.GuardPercent <= 0 && CanCast(grants, "brace", focus))
        return "brace";
    if (!finishing && percent <= 32 && player.GuardPercent <= 0 &&
        CanCast(grants, "riposte", focus))
        return "riposte";
    bool winding = false;
    for (const CombatantState& foe : battle.Enemies()) {
        if (foe.Alive() && !foe.WindupSkill.empty())
            winding = true;
    }
    if (winding && CanCast(grants, "heavy_blow", focus))
        return "heavy_blow";
    if (CanCast(grants, "rend", focus))
        return "rend";
    if (CanCast(grants, "shield_bash", focus))
        return "shield_bash";
    if (CanCast(grants, "strike", focus))
        return "strike";
    return {};
}

i32 FocusSeat(const Battle& battle, std::string_view first, std::string_view second)
{
    const auto foes = battle.Enemies();
    i32 fallback = -1;
    i32 secondSeat = -1;
    for (i32 index = 0; index < static_cast<i32>(foes.size()); ++index) {
        if (!foes[static_cast<usize>(index)].Alive())
            continue;
        if (fallback < 0)
            fallback = index;
        if (foes[static_cast<usize>(index)].Id == first)
            return index;
        if (foes[static_cast<usize>(index)].Id == second)
            secondSeat = index;
    }
    if (secondSeat >= 0)
        return secondSeat;
    return fallback;
}

i32 WindingSeat(const Battle& battle)
{
    const auto foes = battle.Enemies();
    for (i32 index = 0; index < static_cast<i32>(foes.size()); ++index) {
        if (foes[static_cast<usize>(index)].Alive() &&
            !foes[static_cast<usize>(index)].WindupSkill.empty())
            return index;
    }
    return -1;
}

void PushRanks(Battle& battle, const Hero& hero, const Database& data, std::string_view pending)
{
    std::vector<SkillRank> ranks;
    const auto ids = battle.PlayerSkillIds();
    for (const std::string& id : ids) {
        SkillRank row;
        row.Id = id;
        if (!pending.empty() && id == pending)
            row.Rank = hero.RankAfterUse(id);
        else
            row.Rank = hero.RankOf(id);
        ranks.push_back(std::move(row));
    }
    battle.SetPlayerRanks(data.GetProgress().PowerPerRank, std::move(ranks));
}

void NoteFirstDown(const Battle& battle, std::vector<char>& wasAlive, std::string& firstDown)
{
    const auto foes = battle.Enemies();
    if (wasAlive.size() != foes.size())
        wasAlive.assign(foes.size(), 0);
    for (i32 index = 0; index < static_cast<i32>(foes.size()); ++index) {
        const bool alive = foes[static_cast<usize>(index)].Alive();
        if (wasAlive[static_cast<usize>(index)] != 0 && !alive && firstDown.empty())
            firstDown = foes[static_cast<usize>(index)].Id;
        wasAlive[static_cast<usize>(index)] = alive ? 1 : 0;
    }
}

bool PrepareHero(const Database& data, const BuildSpec& build, bool fang, Hero& hero,
                 Inventory& inventory, std::string& error)
{
    PlaytestOptions options;
    options.Level = kGoblinLevel;
    options.ClassId = build.ClassId;
    options.Fight.Id = "goblin_pack";
    options.Equip.emplace_back("rusty_sword");
    options.Equip.emplace_back("oak_shield");
    if (fang)
        options.Equip.emplace_back("alphas_fang");
    if (build.Pick != nullptr)
        options.PickIds.emplace_back(build.Pick);
    PlaytestSetup setup;
    if (!ApplyPlaytest(hero, inventory, data, options, setup, error))
        return false;
    if (hero.ClassId() != build.ClassId) {
        error = "class '" + std::string(build.ClassId) + "' was not applied";
        return false;
    }
    if (build.Pick != nullptr &&
        PickSkillOption(hero, build.Pick, error) == SkillOptionPick::Failed)
        return false;
    for (i32 use = 0; use < kStrikeUses; ++use)
        hero.NoteUse("strike");
    if (build.Practiced != nullptr) {
        for (i32 use = 0; use < kPracticedUses; ++use)
            hero.NoteUse(build.Practiced);
    }
    error.clear();
    return true;
}

GoblinSimRow SimulateBuild(const Database& data, const BuildSpec& build, bool fang,
                           std::string_view focusId, std::string_view thenId)
{
    GoblinSimRow row;
    row.Build = build.Label;
    row.FocusId = std::string(focusId);
    row.Fang = fang;

    Hero hero;
    Inventory inventory;
    if (!PrepareHero(data, build, fang, hero, inventory, row.Error))
        return row;

    const ClassDef* klass = data.FindClass(hero.ClassId());
    if (klass == nullptr) {
        row.Error = "class '" + hero.ClassId() + "' does not exist";
        return row;
    }
    const std::vector<HeroGrant> grants =
        data.HeroGrants(*klass, inventory.WornIds(), hero.PickedIds());
    const i32 perTurn = data.GetRules().FocusPerTurn;

    Battle battle(data, std::make_unique<AgilityRoundModel>());
    battle.SetBaseStats(hero.Attributes());
    battle.SetGear(inventory.GearBonus());
    battle.SetWornItems(inventory.WornIds());
    battle.SetPickedSkills(hero.PickedIds());
    PushRanks(battle, hero, data, {});
    std::string why;
    if (!battle.Start(hero.ClassId(), hero.Level(), "goblin_pack", why)) {
        row.Error = why;
        return row;
    }

    std::vector<char> wasAlive;
    NoteFirstDown(battle, wasAlive, row.FirstDown);
    i32 steps = 0;
    while (battle.Result() == BattleResult::Ongoing && steps < kActionCap) {
        ++steps;
        if (battle.Actor() == CombatantSide::Enemy) {
            battle.EnemyAct();
            NoteFirstDown(battle, wasAlive, row.FirstDown);
            continue;
        }
        ++row.PlayerTurns;
        std::string_view skill = ChoosePlayerSkill(battle, grants, perTurn);
        i32 seat = FocusSeat(battle, focusId, thenId);
        if (skill == "heavy_blow") {
            const i32 winding = WindingSeat(battle);
            if (winding >= 0)
                seat = winding;
        }
        if (seat >= 0)
            battle.SetTarget(seat);
        PushRanks(battle, hero, data, skill);
        ActionResult used;
        if (skill.empty())
            used = battle.Wait();
        else
            used = battle.UseSkill(skill);
        if (!used.Accepted)
            used = battle.UseSkill("strike");
        if (used.Accepted && !used.SkillId.empty()) {
            hero.NoteUse(used.SkillId);
            PushRanks(battle, hero, data, {});
        }
        NoteFirstDown(battle, wasAlive, row.FirstDown);
    }

    row.Ok = true;
    row.Victory = battle.Result() == BattleResult::Victory;
    row.TimedOut = battle.Result() == BattleResult::Ongoing;
    row.Hp = battle.Player().Hp;
    row.MaxHp = battle.Player().MaxHp;
    return row;
}

std::string Pad(std::string_view text, i32 width)
{
    std::string out(text);
    while (static_cast<i32>(out.size()) < width)
        out.push_back(' ');
    return out;
}

bool EquipHas(const PlaytestOptions& options, std::string_view id)
{
    for (const std::string& equipped : options.Equip) {
        if (equipped == id)
            return true;
    }
    return false;
}

std::string ThenFocus(std::string_view focus)
{
    if (focus == "goblin_shaman")
        return "goblin";
    if (focus == "goblin")
        return "goblin_shaman";
    return "goblin_shaman";
}

} // namespace

std::vector<GoblinSimRow> SimulateGoblinPack(const Database& data)
{
    std::vector<GoblinSimRow> rows;
    for (const BuildSpec& build : kBuilds) {
        rows.push_back(SimulateBuild(data, build, false, "goblin_shaman", "goblin"));
        rows.push_back(SimulateBuild(data, build, true, "goblin_shaman", "goblin"));
        rows.push_back(SimulateBuild(data, build, false, "goblin_chief", "goblin_shaman"));
        rows.push_back(SimulateBuild(data, build, true, "goblin_chief", "goblin_shaman"));
    }
    return rows;
}

std::string FormatGoblinSim(const std::vector<GoblinSimRow>& rows)
{
    std::string text;
    text += Pad("Build", 22);
    text += Pad("Focus", 16);
    text += Pad("Fang", 6);
    text += Pad("Result", 10);
    text += Pad("HP", 14);
    text += Pad("Turns", 8);
    text += "First down\n";
    for (const GoblinSimRow& row : rows) {
        text += Pad(row.Build, 22);
        text += Pad(row.FocusId, 16);
        text += Pad(row.Fang ? "yes" : "no", 6);
        if (!row.Ok) {
            text += "setup failed: ";
            text += row.Error;
            text += "\n";
            continue;
        }
        const char* result = "loss";
        if (row.TimedOut)
            result = "timeout";
        else if (row.Victory)
            result = "win";
        text += Pad(result, 10);
        text += Pad(std::to_string(row.Hp) + "/" + std::to_string(row.MaxHp), 14);
        text += Pad(std::to_string(row.PlayerTurns), 8);
        text += row.FirstDown;
        text += "\n";
    }
    return text;
}

int RunPlaytestSim(const PlaytestOptions& options)
{
    const std::filesystem::path directory = PlaytestDataDirectory(options);
    Database data(directory);
    DataError loadError;
    if (!data.Load(&loadError)) {
        const std::string message =
            loadError.Message.empty() ? "could not load data" : loadError.Message;
        std::fprintf(stderr, "%s\n", message.c_str());
        return 1;
    }
    if (options.Fight.Id != "goblin_pack") {
        std::fprintf(stderr, "--sim plays the goblin pack\n");
        return 1;
    }
    if (options.ClassId.empty() && options.PickIds.empty() && options.FocusId.empty()) {
        const std::vector<GoblinSimRow> rows = SimulateGoblinPack(data);
        std::fputs(FormatGoblinSim(rows).c_str(), stdout);
        for (const GoblinSimRow& row : rows) {
            if (!row.Ok) {
                std::fprintf(stderr, "%s\n", row.Error.c_str());
                return 1;
            }
        }
        return rows.empty() ? 1 : 0;
    }

    BuildSpec build;
    build.Label = options.ClassId.empty() ? "Fighter" : options.ClassId.c_str();
    build.ClassId = options.ClassId.empty() ? "fighter" : options.ClassId.c_str();
    build.Pick = options.PickIds.empty() ? nullptr : options.PickIds[0].c_str();
    build.Practiced = nullptr;
    if (options.ClassId == "berserker")
        build.Practiced = "cleave";
    else if (options.ClassId == "guardian")
        build.Practiced = "shield_bash";
    const std::string focus =
        options.FocusId.empty() ? std::string("goblin_chief") : options.FocusId;
    const std::string thenId = ThenFocus(focus);
    const GoblinSimRow row =
        SimulateBuild(data, build, EquipHas(options, "alphas_fang"), focus, thenId);
    const std::vector<GoblinSimRow> one{row};
    std::fputs(FormatGoblinSim(one).c_str(), stdout);
    if (!row.Ok) {
        std::fprintf(stderr, "%s\n", row.Error.c_str());
        return 1;
    }
    return 0;
}

} // namespace Ascension
