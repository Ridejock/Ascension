#include "FightSim.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
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

// The kit used to compare encounters. The Bear is met at level 7; this stays 6 so a
// row can be set next to the Barrow Sentry on the same build.
constexpr i32 kHarnessLevel = 6;
constexpr i32 kStrikeUses = 10;
constexpr i32 kPracticedUses = 3;
constexpr i32 kActionCap = 400;

// One finished fight. Ok is false when the hero could not be built.
struct SimRun {
    std::string Build;
    bool Fang = false;
    bool Ok = false;
    bool Victory = false;
    bool TimedOut = false;
    i32 Hp = 0;
    i32 PlayerTurns = 0;
    std::string Error;
};

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
// it another turn.
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

// Kill order for one rate row. Random marks a uniform living foe every player turn.
// First is the enemy id killed first. Then is the id marked once First is dead.
struct FocusOrder {
    std::string Label;
    std::string First;
    std::string Then;
    bool Random = false;
};

struct RunSpec {
    const BuildSpec* Build = nullptr;
    bool Fang = false;
    std::string_view EncounterId;
    std::string_view FocusId;
    std::string_view ThenId;
    bool RandomFocus = false;
    i32 Slip = 0;
    u32 RngState = 1;
};

// xorshift32. State 0 is stuck, so a zero result is replaced.
u32 NextRandom(u32& state)
{
    u32 value = state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    if (value == 0)
        value = 1;
    state = value;
    return value;
}

u32 RunState(i32 seed, i32 index)
{
    u32 state = seed > 0 ? static_cast<u32>(seed) : 1u;
    state += static_cast<u32>(index) * 0x9E3779B9u;
    if (state == 0)
        state = 1;
    return state;
}

i32 LivingCount(const Battle& battle)
{
    i32 count = 0;
    for (const CombatantState& foe : battle.Enemies()) {
        if (foe.Alive())
            ++count;
    }
    return count;
}

i32 RandomLivingSeat(const Battle& battle, u32& state)
{
    const i32 living = LivingCount(battle);
    if (living < 1)
        return -1;
    i32 pick = static_cast<i32>(NextRandom(state) % static_cast<u32>(living));
    const auto foes = battle.Enemies();
    for (i32 index = 0; index < static_cast<i32>(foes.size()); ++index) {
        if (!foes[static_cast<usize>(index)].Alive())
            continue;
        if (pick == 0)
            return index;
        --pick;
    }
    return -1;
}

// Slip is a percent. A roll under it marks at random even when a focus was named.
// Default targeting does not roll slip: every turn is already a fresh mark.
i32 ChooseSeat(const Battle& battle, const RunSpec& spec, u32& state)
{
    if (spec.RandomFocus)
        return RandomLivingSeat(battle, state);
    if (spec.Slip > 0 && static_cast<i32>(NextRandom(state) % 100u) < spec.Slip)
        return RandomLivingSeat(battle, state);
    return FocusSeat(battle, spec.FocusId, spec.ThenId);
}

bool PrepareHero(const Database& data, const BuildSpec& build, bool fang,
                 std::string_view encounterId, Hero& hero, Inventory& inventory, std::string& error)
{
    PlaytestOptions options;
    options.Level = kHarnessLevel;
    options.ClassId = build.ClassId;
    options.Fight.Id = std::string(encounterId);
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

SimRun SimulateBuild(const Database& data, const RunSpec& spec)
{
    SimRun row;
    row.Build = spec.Build->Label;
    row.Fang = spec.Fang;

    Hero hero;
    Inventory inventory;
    if (!PrepareHero(data, *spec.Build, spec.Fang, spec.EncounterId, hero, inventory, row.Error))
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
    if (!battle.Start(hero.ClassId(), hero.Level(), spec.EncounterId, why)) {
        row.Error = why;
        return row;
    }

    u32 state = spec.RngState == 0 ? 1u : spec.RngState;
    i32 steps = 0;
    while (battle.Result() == BattleResult::Ongoing && steps < kActionCap) {
        ++steps;
        if (battle.Actor() == CombatantSide::Enemy) {
            battle.EnemyAct();
            continue;
        }
        ++row.PlayerTurns;
        std::string_view skill = ChoosePlayerSkill(battle, grants, perTurn);
        i32 seat = ChooseSeat(battle, spec, state);
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
    }

    row.Ok = true;
    row.Victory = battle.Result() == BattleResult::Victory;
    row.TimedOut = battle.Result() == BattleResult::Ongoing;
    row.Hp = battle.Player().Hp;
    return row;
}

bool SeenId(const std::vector<std::string>& ids, std::string_view id)
{
    for (const std::string& have : ids) {
        if (have == id)
            return true;
    }
    return false;
}

void AddOrder(std::vector<FocusOrder>& orders, std::string label, std::string first,
              std::string thenId, bool random)
{
    FocusOrder order;
    order.Label = std::move(label);
    order.First = std::move(first);
    order.Then = std::move(thenId);
    order.Random = random;
    orders.push_back(std::move(order));
}

// Known packs use the kill orders the rate table reports. Anything else walks the
// member list so a new encounter still has a row per foe. One foe is one row.
std::vector<FocusOrder> OrdersFor(const EncounterDef& encounter)
{
    std::vector<FocusOrder> orders;
    if (encounter.Id == "wolf_pack") {
        AddOrder(orders, "dire wolf", "dire_wolf", "wolf", false);
        AddOrder(orders, "wolf", "wolf", "dire_wolf", false);
        AddOrder(orders, "default", "", "", true);
        return orders;
    }
    if (encounter.Enemies.size() <= 1) {
        const std::string id = encounter.Enemies.empty() ? std::string() : encounter.Enemies[0];
        const std::string label = encounter.Id == "barrow_sentry" ? "sentry" : id;
        AddOrder(orders, label, id, "", false);
        return orders;
    }

    std::vector<std::string> ids;
    for (const std::string& enemy : encounter.Enemies) {
        if (!SeenId(ids, enemy))
            ids.push_back(enemy);
    }
    for (usize index = 0; index < ids.size(); ++index) {
        const usize next = (index + 1) % ids.size();
        AddOrder(orders, ids[index], ids[index], ids[next], false);
    }
    AddOrder(orders, "default", "", "", true);
    return orders;
}

// One living foe cannot be marked differently, and a named focus with no slip never
// reads the seed. Repeating those fights would copy one outcome.
bool OrderVaries(const FocusOrder& order, i32 slip, i32 enemyCount)
{
    if (enemyCount < 2)
        return false;
    if (order.Random)
        return true;
    return slip > 0;
}

i32 Percentile10(std::vector<i32>& samples)
{
    if (samples.empty())
        return 0;
    std::sort(samples.begin(), samples.end());
    const usize last = samples.size() - 1;
    return samples[last * 10 / 100];
}

SimRateRow SimulateRate(const Database& data, const EncounterDef& encounter, const BuildSpec& build,
                        const FocusOrder& order, bool fang, const SimRateQuery& query)
{
    SimRateRow rate;
    rate.Encounter = encounter.Name;
    rate.Build = build.Label;
    rate.Focus = order.Label;
    rate.Fang = fang;

    const i32 enemyCount = static_cast<i32>(encounter.Enemies.size());
    const i32 runs = OrderVaries(order, query.Slip, enemyCount) ? query.Runs : 1;
    std::vector<i32> samples;
    samples.reserve(static_cast<usize>(runs));
    for (i32 index = 0; index < runs; ++index) {
        RunSpec spec;
        spec.Build = &build;
        spec.Fang = fang;
        spec.EncounterId = encounter.Id;
        spec.FocusId = order.First;
        spec.ThenId = order.Then;
        spec.RandomFocus = order.Random;
        spec.Slip = query.Slip;
        spec.RngState = RunState(query.Seed, index);
        const SimRun row = SimulateBuild(data, spec);
        if (!row.Ok) {
            rate.Error = row.Error;
            return rate;
        }
        rate.Runs += 1;
        if (row.TimedOut)
            rate.Timeouts += 1;
        const bool won = row.Victory && !row.TimedOut;
        if (won)
            rate.Wins += 1;
        const i32 left = won ? row.Hp : 0;
        rate.SumHp += left;
        rate.SumTurns += row.PlayerTurns;
        samples.push_back(left);
    }
    rate.P10Hp = Percentile10(samples);
    rate.Ok = true;
    return rate;
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

std::string OneDecimal(i64 tenths)
{
    if (tenths < 0)
        tenths = 0;
    std::string text = std::to_string(tenths / 10);
    text.push_back('.');
    text.push_back(static_cast<char>('0' + static_cast<int>(tenths % 10)));
    return text;
}

} // namespace

std::vector<SimRateRow> SimulateEncounterRates(const Database& data, const SimRateQuery& query,
                                               std::string& error)
{
    error.clear();
    const EncounterDef* encounter = data.FindEncounter(query.EncounterId);
    if (encounter == nullptr) {
        error = "no encounter '" + query.EncounterId + "'";
        return {};
    }
    if (query.Runs < 1) {
        error = "runs must be at least 1";
        return {};
    }

    const std::vector<FocusOrder> orders = OrdersFor(*encounter);
    std::vector<SimRateRow> rows;
    for (const BuildSpec& build : kBuilds) {
        for (const FocusOrder& order : orders) {
            const SimRateRow plain = SimulateRate(data, *encounter, build, order, false, query);
            rows.push_back(plain);
            if (!plain.Ok) {
                error = plain.Error;
                return rows;
            }
            const SimRateRow fanged = SimulateRate(data, *encounter, build, order, true, query);
            rows.push_back(fanged);
            if (!fanged.Ok) {
                error = fanged.Error;
                return rows;
            }
        }
    }
    return rows;
}

std::string FormatSimRates(const std::vector<SimRateRow>& rows, const SimRateQuery& query)
{
    std::string text;
    text += "Battle damage, turn order, and foe AI do not use a seed.\n";
    text += "A named focus with slip 0 is one outcome, so Runs is 1.\n";
    text += "default marks a uniform living foe each turn, once per seed.\n";
    text += "A one-enemy fight does not change with the seed.\n";
    text += "HP left is 0 on a loss or a timeout.\n";
    text += "P10 is sorted HP at index (runs-1)*10/100.\n";
    text += "Requested runs " + std::to_string(query.Runs);
    text += ", seed " + std::to_string(query.Seed);
    text += ", slip " + std::to_string(query.Slip) + ".\n";
    text += Pad("Encounter", 16);
    text += Pad("Build", 22);
    text += Pad("Focus", 12);
    text += Pad("Fang", 6);
    text += Pad("Runs", 6);
    text += Pad("Win%", 8);
    text += Pad("Mean HP", 10);
    text += Pad("P10 HP", 8);
    text += "Turns\n";
    for (const SimRateRow& row : rows) {
        text += Pad(row.Encounter, 16);
        text += Pad(row.Build, 22);
        text += Pad(row.Focus, 12);
        text += Pad(row.Fang ? "yes" : "no", 6);
        if (!row.Ok) {
            text += "setup failed: ";
            text += row.Error;
            text += "\n";
            continue;
        }
        const i32 runs = row.Runs > 0 ? row.Runs : 1;
        const i64 winTenths = static_cast<i64>(row.Wins) * 1000 / runs;
        const i64 hpTenths = row.SumHp * 10 / runs;
        const i64 turnTenths = row.SumTurns * 10 / runs;
        text += Pad(std::to_string(row.Runs), 6);
        text += Pad(OneDecimal(winTenths), 8);
        text += Pad(OneDecimal(hpTenths), 10);
        text += Pad(std::to_string(row.P10Hp), 8);
        text += OneDecimal(turnTenths);
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

    SimRateQuery query;
    query.EncounterId = options.Fight.Id;
    query.Runs = options.Runs < 1 ? 1 : options.Runs;
    query.Seed = options.Seed;
    query.Slip = options.Slip;

    if (options.ClassId.empty() && options.PickIds.empty() && options.FocusId.empty()) {
        std::string error;
        const std::vector<SimRateRow> rows = SimulateEncounterRates(data, query, error);
        if (rows.empty()) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        std::fputs(FormatSimRates(rows, query).c_str(), stdout);
        if (!error.empty()) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        return 0;
    }

    const EncounterDef* encounter = data.FindEncounter(options.Fight.Id);
    if (encounter == nullptr) {
        std::fprintf(stderr, "no encounter '%s'\n", options.Fight.Id.c_str());
        return 1;
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

    FocusOrder order;
    if (options.FocusId.empty()) {
        order.Label = "default";
        order.Random = true;
    } else {
        order.Label = options.FocusId;
        order.First = options.FocusId;
        const std::vector<FocusOrder> orders = OrdersFor(*encounter);
        for (const FocusOrder& known : orders) {
            if (!known.Random && known.First == options.FocusId)
                order.Then = known.Then;
        }
        if (order.Then.empty()) {
            for (const std::string& enemy : encounter->Enemies) {
                if (enemy != options.FocusId) {
                    order.Then = enemy;
                    break;
                }
            }
        }
    }

    const bool fang = EquipHas(options, "alphas_fang");
    const SimRateRow row = SimulateRate(data, *encounter, build, order, fang, query);
    const std::vector<SimRateRow> one{row};
    std::fputs(FormatSimRates(one, query).c_str(), stdout);
    if (!row.Ok) {
        std::fprintf(stderr, "%s\n", row.Error.c_str());
        return 1;
    }
    return 0;
}

} // namespace Ascension
