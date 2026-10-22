// Combat tests: the damage formula, one Fighter against the sample encounters, and a swapped
// turn model. No window or GPU.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Check.h"
#include "FoeAlert.h"

namespace {

using namespace Ascension;

namespace fs = std::filesystem;

const fs::path kSample = ASCENSION_DATA_DIR;

struct TempCopy {
    fs::path Path;

    TempCopy()
    {
        Path = fs::temp_directory_path() / "ascension-combat-tests";
        std::error_code error;
        fs::remove_all(Path, error);
        fs::create_directories(Path);
        fs::copy(kSample, Path, fs::copy_options::recursive);
    }

    ~TempCopy()
    {
        std::error_code error;
        fs::remove_all(Path, error);
    }
};

void WriteFile(const fs::path& path, std::string_view text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

void BumpTime(const fs::path& path)
{
    const auto now = fs::file_time_type::clock::now();
    const auto current = fs::last_write_time(path);
    fs::last_write_time(path, current < now ? now : current + std::chrono::seconds(2));
}

void ReplaceInFile(const fs::path& path, std::string_view from, std::string_view to)
{
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const auto at = text.find(from);
    Check(at != std::string::npos, "the sample text to replace is present");
    if (at == std::string::npos)
        return;
    text.replace(at, from.size(), to);
    WriteFile(path, text);
    BumpTime(path);
}

// A turn model with a fixed order, so tests can force the player to act before a faster foe.
// A side on its own means enemy slot 0. Pass TurnSeat to name a later enemy in a pack.
class ScriptedTurns final : public TurnModel {
public:
    explicit ScriptedTurns(std::vector<CombatantSide> order)
    {
        for (const CombatantSide side : order) {
            TurnSeat seat;
            seat.Side = side;
            m_Order.push_back(seat);
        }
    }

    explicit ScriptedTurns(std::vector<TurnSeat> order) : m_Order(std::move(order)) {}

    void Reset() override { m_Index = 0; }

    TurnSeat Next(std::span<const TurnSeat>) override
    {
        if (m_Order.empty())
            return {};
        if (m_Index >= m_Order.size())
            return m_Order.back();
        return m_Order[m_Index];
    }

    void Advance() override
    {
        if (m_Index + 1 < m_Order.size())
            ++m_Index;
    }

    void Capture(std::vector<TurnSeat>& order, i32& index) const override
    {
        order = m_Order;
        index = static_cast<i32>(m_Index);
    }

    void Place(std::span<const TurnSeat> order, i32 index) override
    {
        m_Order.assign(order.begin(), order.end());
        if (index < 0)
            index = 0;
        m_Index = static_cast<usize>(index);
    }

private:
    std::vector<TurnSeat> m_Order;
    usize m_Index = 0;
};

TurnSeat EnemySeat(i32 index)
{
    TurnSeat seat;
    seat.Side = CombatantSide::Enemy;
    seat.Index = index;
    return seat;
}

CombatRules SampleRules()
{
    CombatRules rules;
    rules.HpPerVitality = 4;
    rules.MinDamage = 1;
    rules.AttackNumerator = 1;
    rules.AttackDenominator = 1;
    rules.DefenseNumerator = 1;
    rules.DefenseDenominator = 1;
    return rules;
}

void TestFormula()
{
    const CombatRules rules = SampleRules();
    StrikeQuery hit;
    hit.Power = 10;
    hit.ScalingStat = 8;
    hit.Defense = 2;
    Check(StrikeDamage(hit, rules) == 16, "strike is power plus strength, minus defense");
    hit.EmpowerPercent = 50;
    Check(StrikeDamage(hit, rules) == 25, "empower scales the attack before defense");
    hit.EmpowerPercent = 0;
    hit.Defense = 100;
    Check(StrikeDamage(hit, rules) == 1, "a hit cannot land below min damage");
    Check(GuardedDamage(16, 50) == 8, "a 50 percent guard halves the hit");
    Check(GuardedDamage(16, 100) == 0, "a full guard can reduce a hit to zero");
    Check(HealAmount(8, 0, rules) == 8, "a heal with no scaling is its power");
    Check(MissingHpBonus(1, 10, 6, 72, 72) == 0, "bloodlust is quiet at full health");
    Check(MissingHpBonus(1, 10, 6, 90, 100) == 1, "10 percent missing is +1 strength");
    Check(MissingHpBonus(1, 10, 6, 36, 72) == 5, "half health is five steps");
    Check(MissingHpBonus(1, 10, 6, 0, 100) == 6, "bloodlust stops at +6");
    Check(ReflectedDamage(10, 50, 28) == 33, "riposte returns half the blocked damage plus power");
    Check(ScalePercent(16, 125) == 20, "exposed adds 25 percent to the hit");
    Check(ScalePercent(16, 130) == 20 && ScalePercent(20, 115) == 23,
          "enraged percents stack as dealt, then taken");
}

Battle StartSample(Database& data, std::unique_ptr<TurnModel> turns, const char* enemy)
{
    Battle battle(data, std::move(turns));
    std::string error;
    Check(battle.Start("fighter", 1, enemy, error), "the sample fight starts");
    Check(error.empty(), "starting the sample fight has no error");
    return battle;
}

void TestWolfFight()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    Battle battle = StartSample(data, std::make_unique<AgilityRoundModel>(), "wolf");
    Check(battle.Player().MaxHp == 72 && battle.Player().MaxFocus == 8 &&
              battle.Player().Focus == 8,
          "fighter health and focus");
    Check(battle.Enemy().MaxHp == 40 && battle.Enemy().Name == "Wolf", "wolf health");
    Check(battle.PlayerSkillIds().size() == 4, "four fighter skills");
    Check(battle.Actor() == CombatantSide::Enemy, "the faster wolf acts first");

    const ActionResult tooSoon = battle.UseSkill("strike");
    Check(!tooSoon.Accepted && battle.Enemy().Hp == 40, "the player cannot act on the wolf's turn");

    const ActionResult bite = battle.EnemyAct();
    Check(bite.Accepted && bite.Damage == 5 && battle.Player().Hp == 67, "bite deals 5");
    Check(battle.Actor() == CombatantSide::Player, "the fighter acts after the wolf");

    battle.UseSkill("strike");
    Check(battle.Enemy().Hp == 24, "strike deals 16");
    battle.EnemyAct();
    battle.UseSkill("strike");
    Check(battle.Enemy().Hp == 8 && battle.Result() == BattleResult::Ongoing,
          "the wolf survives two strikes");
    battle.EnemyAct();
    const ActionResult last = battle.UseSkill("strike");
    Check(last.Result == BattleResult::Victory && battle.Enemy().Hp == 0, "the third strike wins");
    Check(battle.Player().Hp == 57, "three bites land before the wolf falls");
    Check(!battle.UseSkill("strike").Accepted, "a finished fight rejects another skill");
}

void TestSkillsAndGuard()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    Battle battle = StartSample(
        data, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Player}),
        "wolf");

    const ActionResult heavy = battle.UseSkill("heavy_blow");
    Check(heavy.Damage == 24 && battle.Player().Focus == 6 && battle.Enemy().Hp == 16,
          "heavy blow costs 2 focus and hits harder than strike");

    Battle healer = StartSample(data,
                                std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{
                                    CombatantSide::Enemy, CombatantSide::Player}),
                                "wolf");
    healer.EnemyAct();
    const ActionResult wind = healer.UseSkill("second_wind");
    Check(healer.Player().Hp == 72 && wind.Healing == 5,
          "second wind heals the bite, not past full");

    Battle full = StartSample(
        data, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Player}),
        "wolf");
    const ActionResult wasted = full.UseSkill("second_wind");
    Check(wasted.Accepted && wasted.Healing == 0 && full.Player().Focus == 6,
          "second wind at full health still spends focus");

    Battle guard = StartSample(data,
                               std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{
                                   CombatantSide::Player, CombatantSide::Enemy}),
                               "wolf");
    Check(guard.UseSkill("brace").Accepted && guard.Player().GuardPercent == 50,
          "brace sets a guard");
    const ActionResult bitten = guard.EnemyAct();
    Check(bitten.Damage == 2 && guard.Player().Hp == 70 && guard.Player().GuardPercent == 0,
          "brace halves the next bite and is then gone");

    Battle focus = StartSample(
        data, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Player}),
        "wolf");
    i32 braces = 0;
    while (braces < 12 && focus.UseSkill("brace").Accepted)
        ++braces;
    Check(braces == 7 && focus.Player().Focus == 1 && focus.Actor() == CombatantSide::Player,
          "focus regenerates by 1, and a brace that cannot be paid does not spend the turn");
}

void TestDefeatAndSkip()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    Battle wolf = StartSample(
        data, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Enemy}),
        "wolf");
    ActionResult last;
    for (i32 i = 0; i < 30 && wolf.Result() == BattleResult::Ongoing; ++i)
        last = wolf.EnemyAct();
    Check(wolf.Result() == BattleResult::Defeat && wolf.Player().Hp == 0 && last.Damage == 5,
          "standing still against the wolf loses");

    TempCopy copy;
    ReplaceInFile(copy.Path / "skills.json",
                  "\"id\": \"bite\",\n      \"name\": \"Bite\",\n      \"description\": \"A quick "
                  "snap.\",\n      \"kind\": \"active\",\n      \"target\": \"enemy\",\n      "
                  "\"focusCost\": 0,",
                  "\"id\": \"bite\",\n      \"name\": \"Bite\",\n      \"description\": \"A quick "
                  "snap.\",\n      \"kind\": \"active\",\n      \"target\": \"enemy\",\n      "
                  "\"focusCost\": 4,");
    Database priced(copy.Path);
    Check(priced.Load(), "the priced bite loads");
    Battle broke = StartSample(
        priced, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Enemy}),
        "wolf");
    const ActionResult skip = broke.EnemyAct();
    Check(skip.Accepted && skip.Damage == 0 && broke.Player().Hp == 72,
          "a foe who cannot pay for a skill skips the turn");
}

void TestHotReloadAndPassive()
{
    TempCopy copy;
    Database data(copy.Path);
    Check(data.Load(), "copied sample loads");
    Battle battle = StartSample(
        data, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Player}),
        "wolf");
    battle.UseSkill("strike");
    Check(battle.Enemy().Hp == 24, "the first strike leaves the wolf at 24");

    ReplaceInFile(copy.Path / "skills.json", "\"power\": 10", "\"power\": 30");
    DataError error;
    Check(data.CheckForChanges(&error) == 0, "the power edit waits a check");
    Check(data.CheckForChanges(&error) == 1, "the settled power edit reloads");
    std::string why;
    Check(battle.ApplyData(why), "the fight picks up the new rules");
    Check(battle.Enemy().Hp == 24 && battle.Player().Hp == 72, "a reload keeps the wound");
    const ActionResult harder = battle.UseSkill("strike");
    Check(harder.Damage == 36 && harder.Result == BattleResult::Victory,
          "the reloaded strike power is what the next blow uses");
}

void TestPassiveAndSwap()
{
    TempCopy copy;
    Database data(copy.Path);
    Check(data.Load(), "copied sample loads");
    // Berserker already grants bloodlust, so move that passive onto the fighter and give
    // Berserker a different one. An evolution cannot list a passive its parent already has.
    ReplaceInFile(copy.Path / "classes.json", "\"passives\": [\"bloodlust\"]",
                  "\"passives\": [\"bulwark\"]");
    ReplaceInFile(copy.Path / "classes.json", "\"passives\": []", "\"passives\": [\"bloodlust\"]");
    DataError error;
    Check(data.CheckForChanges(&error) == 0, "the passive edit waits");
    Check(data.CheckForChanges(&error) == 1, "the passive edit reloads");

    Battle battle = StartSample(
        data, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Player}),
        "wolf");
    Check(battle.Player().Stats.Strength == 8, "bloodlust does not add strength at full health");
    Check(battle.UseSkill("strike").Damage == 16, "bloodlust is quiet at full health");
    battle.SetPlayerHp(36);
    Check(battle.UseSkill("strike").Damage == 21, "half health adds 5 strength");

    Database sample(kSample);
    Check(sample.Load(), "sample data loads");
    Battle swapped = StartSample(
        sample, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Player}),
        "wolf");
    Check(swapped.Actor() == CombatantSide::Player,
          "the turn model can put the fighter first even though the wolf is faster");
}

std::unique_ptr<TurnModel> Sides(std::vector<CombatantSide> order)
{
    return std::make_unique<ScriptedTurns>(std::move(order));
}

void TestFocusRegen()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    Battle battle = StartSample(data, Sides({CombatantSide::Player}), "bandit");
    for (i32 i = 0; i < 5; ++i)
        Check(battle.Actor() == CombatantSide::Player, "actor stays the player");
    Check(battle.Player().Focus == 8, "reading whose turn it is does not grant focus");
    Check(battle.UseSkill("heavy_blow").Accepted && battle.Player().Focus == 6,
          "the first heavy blow spends 2 from a full pool");
    Check(battle.UseSkill("heavy_blow").Accepted && battle.Player().Focus == 5,
          "the next turn gives 1 focus back before the cost");
    const ActionResult waited = battle.Wait();
    Check(waited.Accepted && battle.Player().Focus == 8,
          "wait adds 2 focus on top of the turn's 1");
}

void TestRiposteAndBulwark()
{
    // Riposte is a pick on the shipped guardian, not a class grant. This copy takes it off the
    // class and puts it on the charm, so the fight does not depend on which option ships.
    TempCopy copy;
    ReplaceInFile(copy.Path / "classes.json", "\"skills\": [\"shield_wall\", \"riposte\"]",
                  "\"skills\": [\"shield_wall\"]");
    ReplaceInFile(copy.Path / "items.json", "\"bonus\": { \"agility\": 2 },",
                  "\"bonus\": { \"agility\": 2 }, \"grants\": [\"riposte\"],");
    Database data(copy.Path);
    Check(data.Load(), "sample data loads");
    Battle battle(data, Sides({CombatantSide::Player, CombatantSide::Enemy}));
    battle.SetWornItems({"wolfhide_charm"});
    std::string error;
    Check(battle.Start("guardian", 5, "wolf", error), "the guardian meets the wolf");
    Check(battle.Player().Stats.Defense == 18 && battle.Player().MaxFocus == 12,
          "bulwark adds 3 defense and focus is 4 plus the focus stat");
    Check(battle.UseSkill("riposte").Accepted && battle.Player().GuardPercent == 50 &&
              battle.Player().ReflectPercent == 50 && battle.Player().ReflectFlat == 28 &&
              battle.Player().Focus == 10,
          "riposte guards 50 percent and its flat reflect is power plus defense");
    const ActionResult bite = battle.EnemyAct();
    // 28 back, less the wolf's defense (the fixture counts defense in full): 26, so 14 left.
    Check(bite.Damage == 0 && battle.Enemy().Hp == 14 && battle.Player().Focus == 11 &&
              battle.Player().GuardPercent == 0,
          "half of the blocked bite returns with the defense scaling, less the wolf's defense, "
          "and bulwark pays focus");

    Battle plain(data, Sides({CombatantSide::Player, CombatantSide::Enemy, CombatantSide::Enemy}));
    Check(plain.Start("guardian", 5, "wolf", error), "the second guardian fight starts");
    Check(plain.UseSkill("brace").Accepted, "brace is a guard");
    Check(plain.EnemyAct().Damage == 0 && plain.Player().Focus == 11,
          "a guard that stops damage grants bulwark focus");
    const i32 focus = plain.Player().Focus;
    Check(plain.EnemyAct().Damage == 1 && plain.Player().Focus == focus,
          "a hit that no guard reduced does not grant focus");
}

void TestBulwarkReloads()
{
    TempCopy copy;
    Database data(copy.Path);
    Check(data.Load(), "copied sample loads");
    Battle guard(data, std::make_unique<AgilityRoundModel>());
    std::string error;
    Check(guard.Start("guardian", 5, "oathkeeper_varn", error), "the guardian meets Varn");
    Check(guard.Player().Stats.Defense == 18, "bulwark starts at +3 defense");
    Check(guard.Wait().Accepted, "the guardian waits for the maul");
    Check(guard.EnemyAct().Damage == 8, "maul into 18 defense deals 8");

    ReplaceInFile(copy.Path / "skills.json",
                  "\"op\": \"stat\", \"stat\": \"defense\", \"amount\": 3",
                  "\"op\": \"stat\", \"stat\": \"defense\", \"amount\": 0");
    DataError reported;
    Check(data.CheckForChanges(&reported) == 0, "the bulwark edit waits");
    Check(data.CheckForChanges(&reported) == 1, "the bulwark edit reloads");
    Check(guard.ApplyData(error) && guard.Player().Stats.Defense == 15,
          "the fight picks up the new defense");
}

void TestStatuses()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    Battle bleed(data, Sides({CombatantSide::Enemy, CombatantSide::Enemy, CombatantSide::Enemy,
                              CombatantSide::Player}));
    std::string error;
    Check(bleed.Start("fighter", 1, "bandit", error), "the bandit fight starts");
    Check(bleed.EnemyAct().Damage == 9 && bleed.Player().Statuses.empty(),
          "the first slash does not bleed");
    Check(bleed.EnemyAct().Damage == 9, "the second slash does not bleed");
    Check(bleed.EnemyAct().Accepted && bleed.Player().Statuses.size() == 1 &&
              bleed.Player().Statuses[0].Stacks == 1,
          "every third slash bleeds");
    Check(bleed.UseSkill("strike").Accepted && bleed.Player().Hp == 42,
          "bleed deals 3 at the start of the fighter's turn");

    Battle stacked(data, Sides(std::vector<CombatantSide>(6, CombatantSide::Enemy)));
    stacked.Start("fighter", 1, "bandit", error);
    for (i32 i = 0; i < 6; ++i)
        stacked.EnemyAct();
    Check(stacked.Player().Statuses.size() == 1 && stacked.Player().Statuses[0].Stacks == 2 &&
              stacked.Player().Hp == 18,
          "a second bleed stacks, and six slashes are the only damage so far");
    Battle stackedTurn(data, Sides({CombatantSide::Player}));
    // The stack check above never gave the player a turn, so tick it on a fresh inflict.
    Check(stackedTurn.Start("fighter", 1, "wolf", error), "the tick fight starts");
    Check(stackedTurn.Inflict(CombatantSide::Player, "bleed", error), "bleed can be applied");
    Check(stackedTurn.Inflict(CombatantSide::Player, "bleed", error), "bleed stacks to 2");
    Check(stackedTurn.UseSkill("strike").Accepted && stackedTurn.Player().Hp == 66,
          "two bleed stacks deal 6");

    Battle exposed(data, Sides({CombatantSide::Player}));
    Check(exposed.Start("fighter", 1, "wolf", error), "the exposed fight starts");
    Check(exposed.Inflict(CombatantSide::Enemy, "exposed", error), "the wolf is exposed");
    Check(exposed.UseSkill("strike").Damage == 20, "exposed adds 25 percent taken");

    Battle enraged(data, Sides({CombatantSide::Player}));
    Check(enraged.Start("fighter", 1, "wolf", error), "the enraged fight starts");
    Check(enraged.Inflict(CombatantSide::Player, "enraged", error), "the fighter is enraged");
    Check(enraged.UseSkill("strike").Damage == 20, "enraged adds 30 percent dealt");

    Battle both(data, Sides({CombatantSide::Player}));
    Check(both.Start("fighter", 1, "wolf", error), "the stacked-percent fight starts");
    Check(both.Inflict(CombatantSide::Player, "enraged", error) &&
              both.Inflict(CombatantSide::Enemy, "enraged", error),
          "dealt and taken percents can both be up");
    Check(both.UseSkill("strike").Damage == 23, "30 percent dealt then 15 percent taken");

    Battle stunned(data, Sides({CombatantSide::Enemy, CombatantSide::Enemy}));
    Check(stunned.Start("fighter", 1, "wolf", error), "the stun fight starts");
    Check(stunned.Inflict(CombatantSide::Enemy, "stunned", error), "the wolf is stunned");
    Check(stunned.EnemyAct().Damage == 0 && stunned.Enemy().Statuses.empty(),
          "stunned skips the next action and is then gone");
    Check(stunned.EnemyAct().Damage == 5, "the wolf bites on the following turn");
}

void TestStatusReloads()
{
    TempCopy copy;
    Database data(copy.Path);
    Check(data.Load(), "copied sample loads");
    ReplaceInFile(copy.Path / "statuses.json", "\"damagePerTurn\": 3", "\"damagePerTurn\": 5");
    DataError reported;
    Check(data.CheckForChanges(&reported) == 0, "the bleed edit waits");
    Check(data.CheckForChanges(&reported) == 1, "the bleed edit reloads");
    Battle battle(data, Sides({CombatantSide::Player}));
    std::string error;
    Check(battle.Start("fighter", 1, "wolf", error), "the reloaded bleed fight starts");
    Check(battle.Inflict(CombatantSide::Player, "bleed", error),
          "bleed is applied from the new file");
    Check(battle.UseSkill("strike").Accepted && battle.Player().Hp == 67,
          "a hot reload of bleed damage is what the next turn uses");
}

void TestWindup()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    Battle aim(data, Sides(std::vector<CombatantSide>(3, CombatantSide::Enemy)));
    std::string error;
    Check(aim.Start("fighter", 1, "bone_archer", error), "the archer draws");
    const ActionResult first = aim.EnemyAct();
    const ActionResult second = aim.EnemyAct();
    Check(first.Damage == 0 && second.Damage == 0 && aim.Enemy().WindupLeft == 0 &&
              !aim.Enemy().WindupSkill.empty(),
          "aimed shot is only telegraphed for two turns");
    Check(aim.EnemyAct().Damage == 22, "the shot lands for power 20 plus strength, minus defense");

    Battle cancel(data, Sides({CombatantSide::Enemy, CombatantSide::Player, CombatantSide::Enemy,
                               CombatantSide::Enemy}));
    Check(cancel.Start("fighter", 1, "bone_archer", error), "the fighter interrupts the draw");
    Check(cancel.EnemyAct().Damage == 0 && !cancel.Enemy().WindupSkill.empty(),
          "the archer starts the draw");
    const ActionResult heavy = cancel.UseSkill("heavy_blow");
    Check(heavy.Damage == 23 && cancel.Enemy().WindupSkill.empty() &&
              cancel.Enemy().Statuses.size() == 1,
          "heavy blow hits and stuns a foe who is winding up");
    Check(cancel.EnemyAct().Damage == 0, "the stunned archer skips");
    Check(cancel.EnemyAct().Damage == 0 && !cancel.Enemy().WindupSkill.empty(),
          "the shot was cancelled, so the next action starts a new draw");
}

void TestPhaseShard()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    std::string error;
    Battle early(data, std::make_unique<AgilityRoundModel>());
    Check(early.Start("fighter", 1, "oathkeeper_varn", error), "Varn starts in phase 1");
    early.SetEnemyHp(121);
    Check(early.Enemy().Phase == 1 && early.Enemy().Statuses.empty(),
          "phase 2 waits until half health");

    Battle boss(data, Sides(std::vector<CombatantSide>(4, CombatantSide::Enemy)));
    Check(boss.Start("fighter", 1, "oathkeeper_varn", error), "Varn can be dropped to half");
    boss.SetEnemyHp(120);
    Check(boss.Enemy().Phase == 2 && boss.Enemy().Statuses.size() == 1 &&
              boss.Enemy().Statuses[0].Id == "enraged",
          "half health starts phase 2 and enrages Varn");
    boss.EnemyAct();
    boss.EnemyAct();
    boss.EnemyAct();
    Check(boss.Enemy().ShardDue && boss.Enemies().size() == 1,
          "rubble is telegraphed after three phase-2 actions");
    const ActionResult shard = boss.EnemyAct();
    Check(shard.Damage == 0 && shard.Text.find("Rubble Shard joins") != std::string::npos,
          "Varn's next action calls the rubble shard into the fight");
    Check(boss.Enemies().size() == 2 && boss.Enemies()[1].Id == "rubble_shard" &&
              boss.Enemies()[1].Alive() && boss.Enemies()[1].Hp == 24 &&
              boss.Result() == BattleResult::Ongoing,
          "the shard is a second enemy, and the fight is not over");
    Check(boss.Experience() == 80 && boss.Gold() == 40,
          "a shard with no experience and no gold does not change the reward");
}

i32 LivingEnemies(const Battle& battle)
{
    i32 living = 0;
    for (const CombatantState& foe : battle.Enemies()) {
        if (foe.Alive())
            ++living;
    }
    return living;
}

bool PackThreat(const Battle& battle)
{
    for (const CombatantState& foe : battle.Enemies()) {
        if (foe.Alive() && (!foe.WindupSkill.empty() || foe.ShardDue))
            return true;
    }
    return false;
}

i32 LivingAdd(const Battle& battle)
{
    const std::span<const CombatantState> foes = battle.Enemies();
    for (i32 foeIndex = 1; foeIndex < static_cast<i32>(foes.size()); ++foeIndex) {
        if (foes[static_cast<usize>(foeIndex)].Alive())
            return foeIndex;
    }
    return -1;
}

bool SensiblePlan(Battle& battle, bool guardian, bool& sawPhase, bool& sawShard)
{
    for (i32 step = 0; step < 200 && battle.Result() == BattleResult::Ongoing; ++step) {
        for (const CombatantState& foe : battle.Enemies()) {
            if (foe.Phase >= 2)
                sawPhase = true;
        }
        if (battle.Actor() == CombatantSide::Enemy) {
            const ActionResult act = battle.EnemyAct();
            if (act.Text.find("Rubble") != std::string::npos)
                sawShard = true;
            continue;
        }
        const bool threat = PackThreat(battle);
        const i32 focus = battle.Player().Focus;
        const bool hurt = battle.Player().Hp * 100 < battle.Player().MaxHp * 40;
        const i32 add = LivingAdd(battle);
        if (!guardian && threat && focus >= 2)
            battle.UseSkill("heavy_blow");
        else if (guardian && threat && battle.Player().GuardPercent == 0 && focus >= 2)
            battle.UseSkill("riposte");
        else if (hurt && focus >= 2 && battle.Player().Hp < battle.Player().MaxHp)
            battle.UseSkill("second_wind");
        else if (!guardian && LivingEnemies(battle) >= 2 && focus >= 4)
            battle.UseSkill("cleave");
        else {
            if (add >= 0)
                battle.SetTarget(add);
            battle.UseSkill("strike");
        }
    }
    return battle.Result() == BattleResult::Victory && battle.Player().Hp > 0;
}

void TestBossPlans()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    std::string error;
    bool berserkPhase = false;
    bool berserkShard = false;
    Battle berserk(data, std::make_unique<AgilityRoundModel>());
    Check(berserk.Start("berserker", 6, "oathkeeper_varn", error),
          "the berserker meets Oathkeeper Varn");
    Check(SensiblePlan(berserk, false, berserkPhase, berserkShard) && berserkPhase && berserkShard,
          "a berserker who stuns wind-ups and cleaves the shard wins");

    bool guardPhase = false;
    bool guardShard = false;
    Battle guard(data, std::make_unique<AgilityRoundModel>());
    Check(guard.Start("guardian", 6, "oathkeeper_varn", error),
          "the guardian meets Oathkeeper Varn");
    Check(SensiblePlan(guard, true, guardPhase, guardShard) && guardPhase && guardShard,
          "a guardian who ripostes wind-ups and strikes the shard wins");
}

void TestWolfPack()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    std::string error;
    Battle pack(data, std::make_unique<AgilityRoundModel>());
    Check(pack.Start("fighter", 1, "wolf_pack", error), "the wolf pack draws");
    Check(pack.EncounterName() == "Wolf Pack" && pack.Enemies().size() == 3 &&
              pack.Enemies()[0].Name == "Wolf" && pack.Enemies()[1].Name == "Wolf 2" &&
              pack.Enemies()[2].Name == "Wolf 3" && pack.Enemies()[0].Hp == 40,
          "three wolves, and only the later copies are numbered");
    Check(pack.Actor() == CombatantSide::Enemy && pack.ActorIndex() == 0, "the first wolf acts");
    Check(pack.EnemyAct().Damage == 5 && pack.Player().Hp == 67, "the first bite is 5");
    Check(pack.ActorIndex() == 1 && pack.Enemies()[1].Name == "Wolf 2", "wolf 2 is next");
    Check(pack.EnemyAct().Damage == 5 && pack.Player().Hp == 62, "the second bite is 5");
    Check(pack.ActorIndex() == 2, "wolf 3 is next");
    Check(pack.EnemyAct().Damage == 5 && pack.Player().Hp == 57 &&
              pack.Actor() == CombatantSide::Player,
          "the fighter acts after every wolf");

    pack.SetEnemyHp(0);
    Check(pack.Enemies()[0].Hp == 0, "the first wolf can be dropped");
    Check(pack.Result() == BattleResult::Ongoing && pack.TargetIndex() == 1 && !pack.SetTarget(0),
          "a dead wolf is not a target, and the marker steps to the next");
    pack.CycleTarget(1);
    Check(pack.TargetIndex() == 2, "right moves to wolf 3");
    pack.CycleTarget(1);
    Check(pack.TargetIndex() == 1, "right skips the corpse and wraps to wolf 2");
    const ActionResult strike = pack.UseSkill("strike");
    Check(strike.Accepted && strike.Damage == 16 && pack.Enemies()[1].Hp == 24 &&
              pack.Enemies()[2].Hp == 40 && pack.Enemies()[0].Hp == 0,
          "strike hits only the marked wolf");

    Battle cleave(data, Sides({CombatantSide::Player, CombatantSide::Player}));
    Check(cleave.Start("berserker", 5, "wolf_pack", error), "the berserker meets the pack");
    const ActionResult wide = cleave.UseSkill("cleave");
    Check(wide.Accepted && wide.Damage == 78 && cleave.Enemies()[0].Hp == 14 &&
              cleave.Enemies()[1].Hp == 14 && cleave.Enemies()[2].Hp == 14 &&
              wide.Text.find("Wolf 2 26") != std::string::npos &&
              wide.Text.find("Wolf 3 26") != std::string::npos,
          "cleave hits every living wolf for 26");
    cleave.SetTarget(0);
    cleave.SetEnemyHp(0);
    const ActionResult rest = cleave.UseSkill("cleave");
    Check(rest.Damage == 52 && cleave.Enemies()[0].Hp == 0 && cleave.Enemies()[1].Hp == 0 &&
              cleave.Enemies()[2].Hp == 0 && rest.Result == BattleResult::Victory,
          "a second cleave skips the corpse and ends the pack");
    Check(cleave.Experience() == 24 && cleave.Gold() == 9,
          "the pack pays the sum of each wolf's experience and gold");

    Battle reflect(data, std::make_unique<ScriptedTurns>(std::vector<TurnSeat>{
                             EnemySeat(0), TurnSeat{}, EnemySeat(0), TurnSeat{}, EnemySeat(1)}));
    Check(reflect.Start("guardian", 5, "wolf_pack", error), "the guardian meets the pack");
    Check(reflect.EnemyAct().Accepted, "the first wolf bites before the guardian");
    Check(reflect.UseSkill("riposte").Accepted, "riposte is up");
    const ActionResult back = reflect.EnemyAct();
    Check(back.Damage == 0 && back.Text.find("returns") != std::string::npos &&
              reflect.Enemies()[0].Hp == 14 && reflect.Enemies()[1].Hp == 40 &&
              reflect.Enemies()[2].Hp == 40,
          "the riposte lands on the wolf that bit, not the rest of the pack");
    Check(reflect.UseSkill("riposte").Accepted && reflect.EnemyAct().Accepted &&
              reflect.Enemies()[1].Hp == 14 && reflect.Enemies()[0].Hp == 14,
          "a second riposte returns the next bite to wolf 2");

    Battle bleeding(
        data, std::make_unique<ScriptedTurns>(std::vector<TurnSeat>{TurnSeat{}, EnemySeat(1)}));
    Check(bleeding.Start("fighter", 1, "wolf_pack", error), "another pack, for bleed");
    Check(bleeding.SetTarget(1) && bleeding.Inflict(CombatantSide::Enemy, "bleed", error),
          "bleed is applied to wolf 2");
    Check(bleeding.UseSkill("strike").Accepted &&
              bleeding.EnemyAct().Text.find("bleeds") != std::string::npos,
          "wolf 2's turn opens with its own bleed");
    Check(bleeding.Enemies()[1].Hp == 21 && bleeding.Enemies()[0].Hp == 40 &&
              bleeding.Enemies()[2].Hp == 40,
          "the other wolves do not bleed");
}

i32 ShardSlot(const Battle& battle, bool livingOnly)
{
    const std::span<const CombatantState> foes = battle.Enemies();
    for (i32 foeIndex = 0; foeIndex < static_cast<i32>(foes.size()); ++foeIndex) {
        const CombatantState& foe = foes[static_cast<usize>(foeIndex)];
        if (foe.Id != "rubble_shard")
            continue;
        if (livingOnly && !foe.Alive())
            continue;
        return foeIndex;
    }
    return -1;
}

bool NamedShard(const Battle& battle, const char* name, bool livingOnly)
{
    for (const CombatantState& foe : battle.Enemies()) {
        if (foe.Id == "rubble_shard" && foe.Name == name && (!livingOnly || foe.Alive()))
            return true;
    }
    return false;
}

ActionResult DriveVarn(Battle& battle)
{
    battle.SetPlayerHp(battle.Player().MaxHp);
    return battle.EnemyAct();
}

void TestSummonSeats()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    std::string error;
    std::vector<CombatantSide> turns(20, CombatantSide::Enemy);
    Battle boss(data, Sides(std::move(turns)));
    Check(boss.Start("fighter", 1, "oathkeeper_varn", error), "Varn can call more than one shard");
    boss.SetEnemyHp(120);
    Check(boss.Enemy().Phase == 2, "phase 2 is open");

    ActionResult joined;
    for (i32 step = 0; step < 8 && ShardSlot(boss, true) < 0; ++step)
        joined = DriveVarn(boss);
    const i32 firstShard = ShardSlot(boss, true);
    Check(firstShard >= 0 && joined.Text.find("Rubble Shard joins") != std::string::npos &&
              boss.Enemies().size() == 2,
          "the first shard joins under its own name");
    Check(boss.SetTarget(firstShard), "the shard can be marked");
    boss.SetEnemyHp(0);
    Check(boss.Result() == BattleResult::Ongoing && boss.TargetIndex() != firstShard &&
              !boss.Enemies()[static_cast<usize>(firstShard)].Alive(),
          "a dead shard leaves the marker on someone still up");

    for (i32 step = 0; step < 8 && ShardSlot(boss, true) < 0; ++step)
        joined = DriveVarn(boss);
    Check(joined.Text.find("Rubble Shard 2 joins") != std::string::npos &&
              NamedShard(boss, "Rubble Shard", false) && NamedShard(boss, "Rubble Shard 2", true) &&
              boss.Enemies().size() == 3,
          "a second shard is numbered, and the corpse keeps the first name");

    const i32 secondShard = ShardSlot(boss, true);
    Check(boss.SetTarget(secondShard), "the second shard can be marked");
    boss.SetEnemyHp(0);
    Check(ShardSlot(boss, true) < 0 && boss.Enemies().size() == 3 &&
              boss.Result() == BattleResult::Ongoing,
          "both shards are down and still listed");

    for (i32 step = 0;
         step < 8 && ShardSlot(boss, true) < 0 && boss.Result() == BattleResult::Ongoing; ++step)
        joined = DriveVarn(boss);
    Check(joined.Text.find("joins") != std::string::npos &&
              joined.Text.find("already") == std::string::npos && ShardSlot(boss, true) >= 0 &&
              boss.Enemies().size() == 3,
          "a corpse does not fill the fight, so the next shard takes its seat");
    Check(boss.TargetIndex() >= 0 && boss.Enemies()[static_cast<usize>(boss.TargetIndex())].Alive(),
          "the marker is on a living enemy");
    const i32 marked = boss.TargetIndex();
    boss.CycleTarget(1);
    Check(boss.Enemy().Alive() && boss.TargetIndex() != marked, "cycling skips a corpse");
    Check(boss.ActorIndex() == 0, "Varn's seat in the turn order is unchanged");
}

void TestJoinedShardMark()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    std::string error;
    std::vector<CombatantSide> turns(20, CombatantSide::Enemy);
    Battle boss(data, Sides(std::move(turns)));
    Check(boss.Start("fighter", 1, "oathkeeper_varn", error),
          "Varn can call a shard under the marker");
    boss.SetEnemyHp(120);
    Check(boss.TargetIndex() == 0 && boss.Enemy().Id == "oathkeeper_varn", "Varn starts marked");

    ActionResult joined;
    for (i32 step = 0; step < 8 && ShardSlot(boss, true) < 0; ++step)
        joined = DriveVarn(boss);
    Check(joined.Text.find("Rubble Shard joins") != std::string::npos &&
              boss.Enemies().size() == 2 && boss.TargetIndex() == 0 &&
              boss.Enemy().Id == "oathkeeper_varn",
          "a shard joining does not move a living mark");

    const i32 firstShard = ShardSlot(boss, true);
    Check(firstShard == 1 && boss.SetTarget(firstShard), "the first shard is the second seat");
    boss.SetEnemyHp(0);
    Check(!boss.Enemies()[1].Alive() && boss.TargetIndex() == 0 &&
              boss.Result() == BattleResult::Ongoing,
          "the dead shard leaves the mark on Varn");

    for (i32 step = 0; step < 8 && ShardSlot(boss, true) < 0; ++step)
        joined = DriveVarn(boss);
    Check(joined.Text.find("Rubble Shard 2 joins") != std::string::npos &&
              boss.Enemies().size() == 3 && !boss.Enemies()[1].Alive() &&
              boss.Enemies()[2].Alive() && boss.Enemies()[2].Id == "rubble_shard" &&
              boss.TargetIndex() == 0,
          "the second shard takes a new seat and the mark stays on Varn");
    boss.CycleTarget(1);
    Check(boss.TargetIndex() == 2 && boss.Enemy().Name == "Rubble Shard 2" && boss.Enemy().Alive(),
          "one step right skips the corpse and lands on Rubble Shard 2");
}

void TestPackAim()
{
    TempCopy copy;
    Database data(copy.Path);
    Check(data.Load(), "copied sample loads");
    std::string error;
    DataError reload;
    ReplaceInFile(copy.Path / "skills.json",
                  "{ \"id\": \"hit\", \"trigger\": \"onUse\", \"op\": \"damage\", \"power\": 10, "
                  "\"scaling\": \"strength\", \"target\": \"target\" }",
                  "{ \"id\": \"hit\", \"trigger\": \"onUse\", \"op\": \"damage\", \"power\": 10, "
                  "\"scaling\": \"strength\", \"target\": \"randomEnemy\" }");
    Check(data.CheckForChanges(&reload) == 0, "the aim edit waits a check");
    Check(data.CheckForChanges(&reload) == 1, "the aim edit reloads");
    Battle random(data,
                  Sides({CombatantSide::Player, CombatantSide::Player, CombatantSide::Player}));
    Check(random.Start("fighter", 1, "wolf_pack", error), "the pack waits on the fighter");
    const ActionResult first = random.UseSkill("strike");
    const ActionResult second = random.UseSkill("strike");
    const ActionResult third = random.UseSkill("strike");
    Check(first.Damage == 16 && second.Damage == 16 && third.Damage == 16 &&
              random.Enemies()[0].Hp == 24 && random.Enemies()[1].Hp == 24 &&
              random.Enemies()[2].Hp == 24,
          "randomEnemy hits one wolf, then the next, in encounter order");

    ReplaceInFile(copy.Path / "skills.json",
                  "{ \"id\": \"hit\", \"trigger\": \"onUse\", \"op\": \"damage\", \"power\": 8, "
                  "\"scaling\": \"strength\", \"target\": \"allEnemies\" }",
                  "{ \"id\": \"hit\", \"trigger\": \"onUse\", \"op\": \"damage\", \"power\": 8, "
                  "\"scaling\": \"strength\", \"target\": \"target\" }");
    Check(data.CheckForChanges(&reload) == 0, "cleave's aim edit waits a check");
    Check(data.CheckForChanges(&reload) == 1, "cleave's aim edit reloads");
    Battle marked(data, Sides({CombatantSide::Player}));
    Check(marked.Start("berserker", 5, "wolf_pack", error) && marked.SetTarget(2),
          "the berserker marks wolf 3");
    const ActionResult narrowed = marked.UseSkill("cleave");
    Check(narrowed.Damage == 26 && marked.Enemies()[0].Hp == 40 && marked.Enemies()[1].Hp == 40 &&
              marked.Enemies()[2].Hp == 14,
          "an effect target hits only the marked wolf, even when the skill says all enemies");
    const SkillDef* tuned = data.FindSkill("cleave");
    const ClassDef* berserkClass = data.FindClass("berserker");
    const SkillDef* grantedCleave = nullptr;
    if (berserkClass != nullptr) {
        for (const SkillDef* granted : data.ActiveSkills(*berserkClass)) {
            if (granted->Id == "cleave")
                grantedCleave = granted;
        }
    }
    Check(tuned != nullptr && grantedCleave != nullptr &&
              data.DescribeSkill(*tuned).find("Hits the marked enemy.") != std::string::npos &&
              data.DescribeSkill(*tuned).find("every enemy") == std::string::npos &&
              data.DescribeSkill(*grantedCleave).find("Hits the marked enemy.") !=
                  std::string::npos,
          "cleave's description follows the effect target");

    Battle wounds(data, Sides({CombatantSide::Player}));
    Check(wounds.Start("fighter", 1, "wolf_pack", error), "a pack can shrink on reload");
    Check(wounds.SetTarget(0), "wolf 0 is marked");
    wounds.SetEnemyHp(24);
    ReplaceInFile(copy.Path / "encounters.json", "\"enemies\": [\"wolf\", \"wolf\", \"wolf\"]",
                  "\"enemies\": [\"wolf\", \"wolf\"]");
    Check(data.CheckForChanges(&reload) == 0, "the shorter pack waits a check");
    Check(data.CheckForChanges(&reload) == 1, "the shorter pack reloads");
    Check(wounds.ApplyData(error) && wounds.Enemies().size() == 2 && wounds.Enemies()[0].Hp == 24 &&
              wounds.Enemies()[1].Hp == 40,
          "a hot reload drops the third wolf and keeps the wounds");
}

void TestGearAndConsumables()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    const ItemDef* potion = data.FindItem("minor_potion");
    const ItemDef* tonic = data.FindItem("focus_tonic");
    Check(potion != nullptr && tonic != nullptr, "the sample drinks are loaded");
    if (potion == nullptr || tonic == nullptr)
        return;

    StatBlock sword;
    sword.Strength = 1;
    Battle armed = StartSample(
        data, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Player}),
        "wolf");
    armed.SetGear(sword);
    std::string error;
    Check(armed.Start("fighter", 1, "wolf", error), "gear is on before the fight begins");
    Check(armed.Player().Stats.Strength == 9, "the rusty sword adds 1 strength to the fighter");
    const ActionResult hit = armed.UseSkill("strike");
    Check(hit.Accepted && hit.Damage == 17, "strike with the sword is 17 against the wolf");

    StatBlock axe;
    axe.Strength = 6;
    axe.Defense = -2;
    Battle heavy = StartSample(
        data, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Player}),
        "wolf");
    heavy.SetGear(axe);
    Check(heavy.Start("fighter", 1, "wolf", error), "the greataxe is worn");
    Check(heavy.Player().Stats.Strength == 14 && heavy.Player().Stats.Defense == 4,
          "the greataxe is +6 strength and -2 defense");
    Check(heavy.UseSkill("strike").Damage == 22, "the greataxe strike is 22");

    StatBlock plate;
    plate.Agility = -1;
    plate.Defense = 6;
    Battle slow = StartSample(data, std::make_unique<AgilityRoundModel>(), "wolf");
    slow.SetGear(plate);
    Check(slow.Start("fighter", 1, "wolf", error), "the plate is worn");
    Check(slow.Player().Stats.Defense == 12 && slow.Player().Stats.Agility == 4,
          "Oathkeeper's Plate is +6 defense and -1 agility");

    Battle drinker = StartSample(data,
                                 std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{
                                     CombatantSide::Player, CombatantSide::Enemy}),
                                 "bandit");
    drinker.SetPlayerHp(40);
    const ActionResult healed = drinker.UseItem(*potion);
    Check(healed.Accepted && healed.Healing == 30 && healed.SkillId == "minor_potion" &&
              drinker.Player().Hp == 70,
          "a minor potion heals 30 and spends the turn");
    Check(drinker.Actor() == CombatantSide::Enemy, "drinking hands the turn to the foe");

    Battle focused = StartSample(
        data, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Player}),
        "bandit");
    Check(focused.UseSkill("brace").Accepted && focused.UseSkill("brace").Accepted &&
              focused.UseSkill("brace").Accepted && focused.UseSkill("brace").Accepted,
          "four braces spend focus");
    Check(focused.Player().Focus == 3, "four braces leave 3 focus");
    const ActionResult sipped = focused.UseItem(*tonic);
    Check(sipped.Accepted && sipped.SkillId == "focus_tonic" && focused.Player().Focus == 8 &&
              sipped.Text.find("Focus +4") != std::string::npos,
          "focus tonic restores 4 focus");

    Battle full = StartSample(
        data, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Player}),
        "bandit");
    const ActionResult wasted = full.UseItem(*potion);
    Check(wasted.Accepted && wasted.Healing == 0 && wasted.SkillId == "minor_potion" &&
              wasted.Text.find("nothing changes") != std::string::npos,
          "a potion at full health is still drunk");

    Battle early = StartSample(
        data, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Enemy}),
        "wolf");
    const ActionResult refused = early.UseItem(*potion);
    Check(!refused.Accepted && refused.SkillId.empty(), "a drink on the foe's turn is refused");

    Battle stunned = StartSample(
        data, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Player}),
        "bandit");
    stunned.SetPlayerHp(40);
    Check(stunned.Inflict(CombatantSide::Player, "stunned", error), "the fighter is stunned");
    const ActionResult skipped = stunned.UseItem(*potion);
    Check(skipped.Accepted && skipped.SkillId.empty() && stunned.Player().Hp == 40 &&
              skipped.Text.find("stunned") != std::string::npos,
          "a stun spends the turn and keeps the potion");

    TempCopy copy;
    Database reloaded(copy.Path);
    Check(reloaded.Load(), "a copied roster loads");
    Battle fresh = StartSample(
        reloaded,
        std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Player}),
        "bandit");
    fresh.SetPlayerHp(20);
    ReplaceInFile(copy.Path / "items.json", "\"heal\": 30", "\"heal\": 50");
    DataError reloadError;
    Check(reloaded.CheckForChanges(&reloadError) == 0, "the potion edit waits");
    Check(reloaded.CheckForChanges(&reloadError) == 1, "the potion edit reloads");
    Check(fresh.ApplyData(error), "the fight keeps its wound across the reload");
    const ItemDef* stronger = reloaded.FindItem("minor_potion");
    Check(stronger != nullptr && fresh.UseItem(*stronger).Healing == 50 && fresh.Player().Hp == 70,
          "a reloaded potion heals the new amount");
}

void TestGrantCopies()
{
    TempCopy copy;
    ReplaceInFile(
        copy.Path / "classes.json", "      \"passives\": [\"bulwark\"]\n    }\n  ]\n}",
        "      \"passives\": [\"bulwark\"]\n    },\n    {\n      \"id\": \"paladin\",\n   "
        "   \"name\": \"Paladin\",\n      \"description\": \"Keeps Bulwark, tuned.\",\n   "
        "   \"evolvesFrom\": \"fighter\",\n      \"evolveLevel\": 5,\n      \"bonus\": "
        "{},\n      \"growth\": {},\n      \"skills\": [\"shield_wall\"],\n      "
        "\"passives\": [\n        { \"skill\": \"bulwark\", \"tune\": { \"focus\": { "
        "\"amount\": 2 } } }\n      ]\n    }\n  ]\n}");
    ReplaceInFile(copy.Path / "enemies.json", "\"skills\": [\"bite\"]",
                  "\"skills\": [{ \"skill\": \"bite\", \"tune\": { \"hit\": { \"power\": 9 } } }]");
    ReplaceInFile(copy.Path / "items.json", "\"bonus\": { \"agility\": 2 },\n      \"value\": 35",
                  "\"bonus\": { \"agility\": 2 },\n      \"grants\": [{ \"skill\": \"bulwark\", "
                  "\"tune\": { \"focus\": { \"amount\": 4 } } }],\n      \"value\": 35");
    Database data(copy.Path);
    Check(data.Load(), "the tuned roster loads");

    Battle paladin(data, Sides({CombatantSide::Player, CombatantSide::Enemy}));
    std::string error;
    Check(paladin.Start("paladin", 5, "wolf", error), "the paladin meets the wolf");
    Check(paladin.Player().Stats.Defense == 13, "the shared bulwark still adds 3 defense");
    Check(paladin.UseSkill("brace").Accepted, "the paladin braces");
    Check(paladin.EnemyAct().Damage == 0 && paladin.Player().Focus == 12,
          "the paladin's grant returns 2 focus when a guard stops a hit");

    Battle guardian(data, Sides({CombatantSide::Player, CombatantSide::Enemy}));
    Check(guardian.Start("guardian", 5, "wolf", error), "the guardian meets the same wolf");
    Check(guardian.UseSkill("brace").Accepted && guardian.EnemyAct().Damage == 0 &&
              guardian.Player().Focus == 11,
          "the guardian's own copy of bulwark still returns 1 focus");

    Battle fighter = StartSample(data, Sides({CombatantSide::Enemy}), "wolf");
    Check(fighter.Player().Stats.Defense == 6 && fighter.EnemyAct().Damage == 8,
          "the wolf's tuned bite deals 8, and an item grant does not change the fighter");
}

void TestStunResist()
{
    TempCopy copy;
    ReplaceInFile(copy.Path / "statuses.json", "\"damageTakenPercent\": 15,",
                  "\"damageTakenPercent\": 15, \"resists\": [\"stunned\"],");
    Database data(copy.Path);
    Check(data.Load(), "the copy with a stun resist loads");
    Battle resist(data, Sides({CombatantSide::Enemy, CombatantSide::Player, CombatantSide::Enemy,
                               CombatantSide::Enemy}));
    std::string error;
    Check(resist.Start("fighter", 1, "bone_archer", error), "the archer draws");
    Check(resist.Inflict(CombatantSide::Enemy, "enraged", error), "the archer is enraged");
    Check(ResistsStatus(resist.Enemy(), "stunned", data.GetStatuses()), "enraged resists stunned");
    Check(resist.EnemyAct().Damage == 0 && !resist.Enemy().WindupSkill.empty(),
          "the archer starts the draw");
    const ActionResult heavy = resist.UseSkill("heavy_blow");
    Check(heavy.Accepted && heavy.Damage > 0 && !resist.Enemy().WindupSkill.empty() &&
              heavy.Text.find("shrugs off the stun") != std::string::npos,
          "a resisted stun still hits but leaves the wind-up running");
    Check(resist.Enemy().Statuses.size() == 1, "only enraged is up");
}

void TestSummonFallback()
{
    const char* const kSummon = "\"summon\": { \"enemy\": \"rubble_shard\", \"every\": 3 },";
    {
        TempCopy bad;
        ReplaceInFile(
            bad.Path / "enemies.json", kSummon,
            "\"summon\": { \"enemy\": \"rubble_shard\", \"every\": 3, \"ifBlocked\": \"nap\" },");
        Database data(bad.Path);
        DataError error;
        Check(!data.Load(&error), "ifBlocked rejects an unknown mode");
        CheckHas(error.Message, "enemies.json", "the ifBlocked error names the file");
        CheckHas(error.Message, "oathkeeper_varn", "the ifBlocked error names the enemy");
        CheckHas(error.Message, "ifBlocked", "the ifBlocked error names the field");
    }

    auto driveBlocked = [&](std::string_view mode, ActionResult& blocked, bool& joined) {
        TempCopy copy;
        const std::string summon =
            std::string(
                "\"summon\": { \"enemy\": \"rubble_shard\", \"every\": 3, \"ifBlocked\": \"") +
            std::string(mode) + "\" },";
        ReplaceInFile(copy.Path / "enemies.json", kSummon, summon);
        Database data(copy.Path);
        Check(data.Load(), "the summon mode loads");
        const EnemyDef* varn = data.FindEnemy("oathkeeper_varn");
        const bool act = mode == "act";
        Check(varn != nullptr &&
                  varn->IfBlocked == (act ? BlockedSummon::Act : BlockedSummon::Fizzle),
              "ifBlocked is read");
        std::string error;
        std::vector<CombatantSide> turns(16, CombatantSide::Enemy);
        Battle boss(data, Sides(std::move(turns)));
        Check(boss.Start("fighter", 1, "oathkeeper_varn", error), "Varn can be driven to a summon");
        boss.SetEnemyHp(120);
        std::vector<ActionResult> acts;
        for (i32 step = 0; step < 7 && boss.Result() == BattleResult::Ongoing; ++step)
            acts.push_back(DriveVarn(boss));
        joined = acts.size() == 7 && acts[3].Text.find("joins") != std::string::npos &&
                 ShardSlot(boss, true) >= 0;
        if (acts.size() == 7)
            blocked = acts[6];
    };

    ActionResult fizzled;
    bool fizzleJoined = false;
    driveBlocked("fizzle", fizzled, fizzleJoined);
    Check(fizzleJoined, "the first shard still joins when a later summon would be blocked");
    Check(fizzled.Text.find("already in the fight") != std::string::npos,
          "ifBlocked fizzle spends the turn on the refusal");

    ActionResult acted;
    bool actJoined = false;
    driveBlocked("act", acted, actJoined);
    Check(actJoined, "the first shard joins before the fallback");
    Check(acted.Text.find("already") == std::string::npos &&
              acted.Text.find("joins") == std::string::npos && acted.SkillId != "rubble_shard" &&
              !acted.SkillId.empty(),
          "a blocked summon falls through to the ordinary action");

    TempCopy follow;
    ReplaceInFile(
        follow.Path / "enemies.json", kSummon,
        "\"summon\": { \"enemy\": \"rubble_shard\", \"every\": 3, \"ifBlocked\": \"act\" },");
    Database data(follow.Path);
    Check(data.Load(), "the fallback copy loads");
    std::string error;
    std::vector<CombatantSide> turns(20, CombatantSide::Enemy);
    Battle boss(data, Sides(std::move(turns)));
    Check(boss.Start("fighter", 1, "oathkeeper_varn", error), "Varn can summon after a corpse");
    boss.SetEnemyHp(120);
    for (i32 step = 0; step < 7 && boss.Result() == BattleResult::Ongoing; ++step)
        DriveVarn(boss);
    const i32 living = ShardSlot(boss, true);
    Check(living >= 0 && boss.SetTarget(living), "the living shard can be marked");
    boss.SetEnemyHp(0);
    ActionResult next;
    for (i32 step = 0; step < 6 && boss.Result() == BattleResult::Ongoing; ++step) {
        next = DriveVarn(boss);
        if (next.Text.find("joins") != std::string::npos)
            break;
    }
    Check(next.Text.find("joins") != std::string::npos && ShardSlot(boss, true) >= 0,
          "a free seat still summons on the next call");
}

void GiveGearGrants(const fs::path& root)
{
    ReplaceInFile(root / "items.json", "\"bonus\": { \"strength\": 1 },",
                  "\"bonus\": { \"strength\": 1 }, \"grants\": [{ \"skill\": \"cleave\", "
                  "\"tune\": { \"focusCost\": 1, \"hit\": { \"power\": 5, \"target\": "
                  "\"target\" } } }],");
    ReplaceInFile(root / "items.json", "\"bonus\": { \"defense\": 6, \"agility\": -1 },",
                  "\"bonus\": { \"defense\": 6, \"agility\": -1 }, \"grants\": [{ \"skill\": "
                  "\"cleave\", \"tune\": { \"focusCost\": 6, \"hit\": { \"power\": 12 } } }],");
    ReplaceInFile(root / "items.json", "\"bonus\": { \"agility\": 2 },",
                  "\"bonus\": { \"agility\": 2 }, \"grants\": [{ \"skill\": \"bulwark\", "
                  "\"tune\": { \"def\": { \"amount\": 1 }, \"focus\": { \"amount\": 4 } } }, "
                  "{ \"skill\": \"bloodlust\", \"tune\": { \"fury\": { \"amount\": 3, \"per\": 5, "
                  "\"max\": 2 } } }],");
}

usize CountId(std::span<const std::string> ids, std::string_view id)
{
    usize count = 0;
    for (const std::string& row : ids) {
        if (row == id)
            ++count;
    }
    return count;
}

void CheckLine(const ActionResult& result, i32 damage, const char* text, const char* what)
{
    const bool ok = result.Accepted && result.Damage == damage && result.Text == text;
    Check(ok, what);
    if (!ok)
        std::printf("    damage %d text: %s\n", result.Damage, result.Text.c_str());
}

bool Holds(const CombatantState& who, std::string_view id)
{
    for (const StatusInstance& held : who.Statuses) {
        if (held.Id == id)
            return true;
    }
    return false;
}

void TestConditionalDamage()
{
    TempCopy copy;
    ReplaceInFile(copy.Path / "classes.json",
                  "\"skills\": [\"strike\", \"brace\", \"heavy_blow\", \"second_wind\"]",
                  "\"skills\": [\"strike\", \"brace\", \"heavy_blow\", \"second_wind\", "
                  "\"shell_bash\", \"cash_in\"]");
    Database data(copy.Path);
    Check(data.Load(), "the conditional roster loads");
    std::string error;

    Battle open(data, Sides({CombatantSide::Player}));
    Check(open.Start("fighter", 1, "bandit", error), "the bash fight starts");
    const ActionResult first = open.UseSkill("shell_bash");
    CheckLine(first, 6, "Fighter uses Shell Bash for 6. Exposed.",
              "the opener hits once and the follow-up does not land");
    Check(open.Enemy().Hp == 58 && Holds(open.Enemy(), "exposed") &&
              open.Enemy().Statuses.size() == 1 && open.Enemy().Statuses[0].TurnsLeft == 2,
          "the opener applies Exposed");
    const ActionResult cash = open.UseSkill("shell_bash");
    CheckLine(cash, 27, "Fighter uses Shell Bash for 27. Bandit loses Exposed.",
              "both hits land while Exposed is up, on one blow line");
    Check(open.Enemy().Hp == 31 && open.Enemy().Statuses.empty(),
          "the cash-in removes Exposed and does not apply it again");
    const ActionResult again = open.UseSkill("shell_bash");
    CheckLine(again, 6, "Fighter uses Shell Bash for 6. Exposed.", "the next use opens again");
    Check(Holds(open.Enemy(), "exposed"), "the third use applies Exposed");

    Battle miss(data, Sides({CombatantSide::Player}));
    Check(miss.Start("fighter", 1, "bandit", error), "the miss fight starts");
    const ActionResult whiff = miss.UseSkill("cash_in");
    CheckLine(whiff, 0, "Fighter uses Cash In.",
              "a conditional miss deals nothing and does not apply onHit");
    Check(miss.Enemy().Hp == 64 && miss.Enemy().Statuses.empty(),
          "a miss does not consume or leave a status");
    Check(miss.Inflict(CombatantSide::Enemy, "exposed", error), "exposed can be placed");
    const ActionResult spent = miss.UseSkill("cash_in");
    CheckLine(spent, 20, "Fighter uses Cash In for 20. Bandit loses Exposed.",
              "the condition lands, consumes Exposed, and does not reapply it");
    Check(miss.Enemy().Hp == 44 && miss.Enemy().Statuses.empty(),
          "the spent status is gone after the use");

    Battle ranked(data, Sides({CombatantSide::Player}));
    Check(ranked.Start("fighter", 1, "bandit", error), "the ranked fight starts");
    std::vector<SkillRank> rows;
    SkillRank row;
    row.Id = "shell_bash";
    row.Rank = 4;
    rows.push_back(std::move(row));
    ranked.SetPlayerRanks(data.GetProgress().PowerPerRank, std::move(rows));
    Check(ranked.Inflict(CombatantSide::Enemy, "exposed", error), "the ranked target is exposed");
    const ActionResult boosted = ranked.UseSkill("shell_bash");
    CheckLine(boosted, 42, "Fighter uses Shell Bash for 42. Bandit loses Exposed.",
              "each damage effect gets the rank bonus");

    Battle furious(data, Sides({CombatantSide::Player}));
    Check(furious.Start("fighter", 1, "bandit", error), "the fury fight starts");
    Check(furious.Inflict(CombatantSide::Player, "fury", error) &&
              furious.Inflict(CombatantSide::Enemy, "exposed", error),
          "fury and exposed are both up");
    const ActionResult both = furious.UseSkill("shell_bash");
    CheckLine(both, 41, "Fighter uses Shell Bash for 41. Bandit loses Exposed.",
              "fury multiplies both damage effects");
}

void TestItemGrantsInFight()
{
    TempCopy copy;
    GiveGearGrants(copy.Path);
    Database data(copy.Path);
    Check(data.Load(), "the copy with gear grants loads");
    std::string error;

    Battle bare(data, Sides({CombatantSide::Player, CombatantSide::Enemy}));
    Check(bare.Start("fighter", 1, "wolf", error) && bare.PlayerSkillIds().size() == 4 &&
              !bare.UseSkill("cleave").Accepted,
          "unworn gear grants nothing");
    bare.SetWornItems({"rusty_sword", "oathkeepers_plate"});
    Check(bare.PlayerSkillIds().size() == 4, "worn ids wait for the next build");
    Check(bare.ApplyData(error) && CountId(bare.PlayerSkillIds(), "cleave") == 1 &&
              bare.PlayerSkillIds().size() == 5 && bare.PlayerSkillIds()[4] == "cleave",
          "a reload picks up the sword and the plate as one cleave");

    Battle swordOnly(data, Sides({CombatantSide::Player}));
    swordOnly.SetWornItems({"rusty_sword"});
    Check(swordOnly.Start("fighter", 1, "wolf", error), "the sword alone meets the wolf");
    const i32 swordFocus = swordOnly.Player().Focus;
    const ActionResult swordHit = swordOnly.UseSkill("cleave");
    Check(swordHit.Accepted && swordOnly.Player().Focus == swordFocus - 1,
          "the sword's cleave spends 1 focus");

    Battle merged(data, Sides({CombatantSide::Player}));
    merged.SetWornItems({"rusty_sword", "oathkeepers_plate"});
    Check(merged.Start("fighter", 1, "wolf", error), "the sword and plate meet the wolf");
    const i32 mergedFocus = merged.Player().Focus;
    const ActionResult first = merged.UseSkill("cleave");
    Check(CountId(merged.PlayerSkillIds(), "cleave") == 1 && first.Accepted &&
              merged.Player().Focus == mergedFocus - 1 && first.Damage == swordHit.Damage + 7,
          "one cleave: the lower focus cost, and the plate's higher power");
    merged.SetPlayerRanks(2, {SkillRank{"cleave", 3}});
    const ActionResult ranked = merged.UseSkill("cleave");
    Check(ranked.Accepted && ranked.Damage == first.Damage + 4, "the one cleave ranks by its id");

    Battle berserker(data, Sides({CombatantSide::Player}));
    berserker.SetWornItems({"rusty_sword", "oathkeepers_plate"});
    Check(berserker.Start("berserker", 5, "wolf", error) &&
              berserker.PlayerSkillIds().size() == 6 &&
              CountId(berserker.PlayerSkillIds(), "cleave") == 1,
          "class and gear share one cleave and keep the class hotkey count");
    const i32 classFocus = berserker.Player().Focus;
    const ActionResult classBlow = berserker.UseSkill("cleave");
    const i32 afterOne = berserker.Player().Focus;
    berserker.SetPlayerRanks(2, {SkillRank{"cleave", 3}});
    const ActionResult classRanked = berserker.UseSkill("cleave");
    Check(classBlow.Accepted && afterOne == classFocus - 1 && classRanked.Accepted &&
              classRanked.Damage == classBlow.Damage + 4,
          "the shared cleave spends the lower cost and has one rank");

    Battle charm(data, Sides({CombatantSide::Player, CombatantSide::Enemy}));
    charm.SetWornItems({"wolfhide_charm"});
    Check(charm.Start("fighter", 1, "wolf", error), "the charmed fighter meets the wolf");
    Check(charm.Player().Stats.Defense == bare.Player().Stats.Defense + 1,
          "the charm is the only bulwark, so its defense amount of 1 is the skill");
    Check(charm.UseSkill("brace").Accepted && charm.EnemyAct().Accepted &&
              charm.Player().Focus == charm.Player().MaxFocus,
          "the charm's 4 focus refund fills the pool");

    Battle guardian(data, Sides({CombatantSide::Player, CombatantSide::Enemy}));
    guardian.SetWornItems({"wolfhide_charm"});
    const ClassDef* guardianClass = data.FindClass("guardian");
    const i32 classDefense = guardianClass != nullptr ? data.StatsAt(*guardianClass, 5).Defense : 0;
    Check(guardian.Start("guardian", 5, "wolf", error), "the guardian wears the charm");
    Check(guardian.Player().Stats.Defense == classDefense + 3,
          "best defense is the class 3, not the charm's 1, and it is applied once");
    Check(guardian.UseSkill("brace").Accepted && guardian.EnemyAct().Accepted &&
              guardian.Player().Focus == guardian.Player().MaxFocus,
          "best focus refund is the charm's 4");

    Battle sword(data, Sides({CombatantSide::Player, CombatantSide::Enemy}));
    sword.SetWornItems({"rusty_sword"});
    Check(sword.Start("fighter", 1, "wolf", error) && sword.PlayerSkillIds().size() == 5,
          "the sword alone adds cleave");
    const u32 revision = data.GetRevision();
    ReplaceInFile(copy.Path / "items.json", "\"items\": [", "\"items\": {");
    DataError reloadError;
    Check(data.CheckForChanges(&reloadError) == 0, "a broken items file waits");
    Check(data.CheckForChanges(&reloadError) == 0, "a broken items file does not replace the data");
    Check(data.GetRevision() == revision, "a failed reload keeps the revision");
    CheckHas(data.GetLastError(), "items.json", "the error names the file");
    Check(sword.ApplyData(error) && sword.PlayerSkillIds().size() == 5,
          "the fight keeps the sword's cleave when the reload fails");
    ReplaceInFile(copy.Path / "items.json", "\"items\": {", "\"items\": [");
    Check(data.CheckForChanges(&reloadError) == 0, "the restored file waits");
    Check(data.CheckForChanges(&reloadError) == 1, "the restored file reloads");
    ReplaceInFile(copy.Path / "items.json",
                  "\"grants\": [{ \"skill\": \"cleave\", \"tune\": { \"focusCost\": 1, \"hit\": { "
                  "\"power\": 5, \"target\": \"target\" } } }],",
                  "");
    Check(data.CheckForChanges(&reloadError) == 0, "dropping the grant waits");
    Check(data.CheckForChanges(&reloadError) == 1, "dropping the grant reloads");
    Check(sword.ApplyData(error) && sword.PlayerSkillIds().size() == 4 &&
              !sword.UseSkill("cleave").Accepted,
          "a reload drops the sword's cleave from the fight");
}

// Test-only foes on a copy of tests/data. The frozen roster stays free of these ids.
void AddPostFoes(const fs::path& root)
{
    ReplaceInFile(root / "statuses.json",
                  "      \"damageDealtPercent\": 50,\n      \"turns\": 4,\n      "
                  "\"maxStacks\": 1\n    }\n  ]\n}",
                  "      \"damageDealtPercent\": 50,\n      \"turns\": 4,\n      "
                  "\"maxStacks\": 1\n    },\n    {\n      \"id\": \"steadfast\",\n      "
                  "\"name\": \"Steadfast\",\n      \"description\": \"Cannot be stunned for a "
                  "short while.\",\n      \"resists\": [\"stunned\"],\n      \"turns\": 3,\n      "
                  "\"maxStacks\": 1\n    }\n  ]\n}");
    ReplaceInFile(root / "skills.json",
                  "        { \"id\": \"shell\", \"trigger\": \"onUse\", \"op\": \"harden\", "
                  "\"amount\": 6, \"turns\": 2 }\n      ]\n    }\n  ]\n}",
                  "        { \"id\": \"shell\", \"trigger\": \"onUse\", \"op\": \"harden\", "
                  "\"amount\": 6, \"turns\": 2 }\n      ]\n    },\n    {\n      \"id\": "
                  "\"test_shell\",\n      \"name\": \"Test Shell\",\n      \"description\": \"A "
                  "shell closes. Blows barely land for a short while.\",\n      \"kind\": "
                  "\"active\",\n      \"target\": \"self\",\n      \"focusCost\": 0,\n      "
                  "\"effects\": [\n        { \"id\": \"shell\", \"trigger\": \"onUse\", \"op\": "
                  "\"harden\", \"amount\": 150, \"turns\": 2 }\n      ]\n    },\n    {\n      "
                  "\"id\": \"test_tap\",\n      \"name\": \"Test Tap\",\n      \"description\": "
                  "\"A light tap.\",\n      \"kind\": \"active\",\n      \"target\": \"enemy\",\n "
                  "     \"focusCost\": 0,\n      \"effects\": [\n        { \"id\": \"hit\", "
                  "\"trigger\": \"onUse\", \"op\": \"damage\", \"power\": 1 }\n      ]\n    },\n   "
                  " {\n      \"id\": \"test_bellow\",\n      \"name\": \"Test Bellow\",\n      "
                  "\"description\": \"A roar that sets the feet.\",\n      \"kind\": "
                  "\"active\",\n      \"target\": \"self\",\n      \"focusCost\": 0,\n      "
                  "\"effects\": [\n        { \"id\": \"set\", \"trigger\": \"onUse\", \"op\": "
                  "\"applyStatus\", \"status\": \"steadfast\" }\n      ]\n    },\n    {\n      "
                  "\"id\": \"test_draw\",\n      \"name\": \"Test Draw\",\n      \"description\": "
                  "\"A telegraphed swing.\",\n      \"kind\": \"active\",\n      \"target\": "
                  "\"enemy\",\n      \"focusCost\": 0,\n      \"windup\": 1,\n      \"effects\": "
                  "[\n        { \"id\": \"hit\", \"trigger\": \"onUse\", \"op\": \"damage\", "
                  "\"power\": 1 }\n      ]\n    }\n  ]\n}");
    ReplaceInFile(
        root / "enemies.json",
        "      \"skills\": [\"rubble_strike\"],\n      \"experience\": 0,\n      \"gold\": 0\n    "
        "}\n  ]\n}",
        "      \"skills\": [\"rubble_strike\"],\n      \"experience\": 0,\n      \"gold\": 0\n    "
        "},\n    {\n      \"id\": \"shell_post\",\n      \"name\": \"Shell Post\",\n      "
        "\"description\": \"A post that closes a stone shell, then taps.\",\n      \"level\": "
        "1,\n      \"stats\": {\n        \"vitality\": 30,\n        \"strength\": 0,\n        "
        "\"defense\": 0,\n        \"agility\": 1,\n        \"focus\": 0\n      },\n      "
        "\"skills\": [\"test_shell\", \"test_tap\"],\n      \"pattern\": [\"test_shell\", "
        "\"test_tap\", \"test_tap\"],\n      \"experience\": 0,\n      \"gold\": 0\n    },\n    "
        "{\n "
        "     \"id\": \"steadfast_post\",\n      \"name\": \"Steadfast Post\",\n      "
        "\"description\": \"A post that sets its feet, then draws.\",\n      \"level\": 1,\n      "
        "\"stats\": {\n        \"vitality\": 30,\n        \"strength\": 0,\n        \"defense\": "
        "0,\n        \"agility\": 1,\n        \"focus\": 0\n      },\n      \"skills\": "
        "[\"test_bellow\", \"test_draw\"],\n      \"pattern\": [\"test_bellow\", "
        "\"test_draw\"],\n      \"experience\": 0,\n      \"gold\": 0\n    },\n    {\n      "
        "\"id\": \"open_post\",\n      \"name\": \"Open Post\",\n      \"description\": \"A post "
        "that draws with nothing holding the stun off.\",\n      \"level\": 1,\n      \"stats\": "
        "{\n        \"vitality\": 30,\n        \"strength\": 0,\n        \"defense\": 0,\n        "
        "\"agility\": 1,\n        \"focus\": 0\n      },\n      \"skills\": [\"test_draw\"],\n     "
        " \"pattern\": [\"test_draw\"],\n      \"experience\": 0,\n      \"gold\": 0\n    }\n  "
        "]\n}");
}

void TestSteadfastBlocksStun()
{
    TempCopy copy;
    AddPostFoes(copy.Path);
    Database data(copy.Path);
    Check(data.Load(), "the steadfast post loads");
    const StatusDef* steadfast = data.FindStatus("steadfast");
    Check(steadfast != nullptr && steadfast->Resists.size() == 1 &&
              steadfast->Resists[0] == "stunned" && steadfast->Turns == 3,
          "steadfast resists stunned for a short while");

    Battle held(data, Sides({CombatantSide::Enemy, CombatantSide::Player, CombatantSide::Enemy,
                             CombatantSide::Player}));
    std::string error;
    Check(held.Start("fighter", 1, "steadfast_post", error), "the steadfast post fight starts");
    const ActionResult bellow = held.EnemyAct();
    Check(bellow.Accepted && bellow.Text.find("Steadfast.") != std::string::npos &&
              ResistsStatus(held.Enemy(), "stunned", data.GetStatuses()),
          "the bellow applies steadfast");
    Check(held.UseSkill("strike").Accepted, "a strike spends the turn before the draw");
    const ActionResult draw = held.EnemyAct();
    Check(draw.Accepted && draw.Damage == 0 && held.Enemy().WindupSkill == "test_draw",
          "the draw is telegraphed");
    const ActionResult heavy = held.UseSkill("heavy_blow");
    Check(heavy.Accepted && heavy.Damage > 0 &&
              heavy.Text.find("shrugs off the stun") != std::string::npos &&
              held.Enemy().WindupSkill == "test_draw",
          "steadfast blocks the stun and leaves the wind-up running");
    bool stunned = false;
    for (const StatusInstance& status : held.Enemy().Statuses) {
        if (status.Id == "stunned")
            stunned = true;
    }
    Check(!stunned, "steadfast does not let stunned land");

    Battle open(data, Sides({CombatantSide::Enemy, CombatantSide::Player}));
    Check(open.Start("fighter", 1, "open_post", error), "the open post fight starts");
    Check(open.EnemyAct().Accepted && open.Enemy().WindupSkill == "test_draw",
          "the open post starts the same draw");
    const ActionResult stunnedBlow = open.UseSkill("heavy_blow");
    Check(stunnedBlow.Accepted && stunnedBlow.Text.find("is stunned.") != std::string::npos &&
              open.Enemy().WindupSkill.empty(),
          "the same draw is stunned when steadfast is not up");
}

void TestShellDefenseExpires()
{
    TempCopy copy;
    AddPostFoes(copy.Path);
    Database data(copy.Path);
    Check(data.Load(), "the shell post loads");
    Battle battle(data,
                  Sides({CombatantSide::Enemy, CombatantSide::Player, CombatantSide::Enemy,
                         CombatantSide::Player, CombatantSide::Enemy, CombatantSide::Player}));
    std::string error;
    Check(battle.Start("fighter", 1, "shell_post", error), "the shell post fight starts");
    const ActionResult shell = battle.EnemyAct();
    Check(shell.Accepted && battle.Enemy().BonusDefense == 150 &&
              battle.Enemy().BonusDefenseTurns == 2,
          "the shell raises defense by 150 for two of the holder's turns");
    const ActionResult first = battle.UseSkill("strike");
    Check(first.Accepted && first.Damage == 1 && battle.Enemy().BonusDefense == 150 &&
              battle.Enemy().BonusDefenseTurns == 2,
          "a blow during the shell lands for 1, and the hero's turn does not tick it");
    Check(battle.EnemyAct().Accepted && battle.Enemy().BonusDefense == 150 &&
              battle.Enemy().BonusDefenseTurns == 1,
          "the shell ticks down on the foe's next turn and is still up");
    const ActionResult second = battle.UseSkill("strike");
    Check(second.Accepted && second.Damage == 1 && battle.Enemy().BonusDefenseTurns == 1,
          "the second hero turn still faces the shell");
    Check(battle.EnemyAct().Accepted && battle.Enemy().BonusDefense == 0 &&
              battle.Enemy().BonusDefenseTurns == 0,
          "the shell expires at the start of the foe's following turn");
    const ActionResult third = battle.UseSkill("strike");
    Check(third.Accepted && third.Damage == 18, "once the shell has expired a strike lands for 18");
}

std::string UpperAlert(std::string_view text)
{
    std::string out(text);
    for (char& code : out) {
        if (code >= 'a' && code <= 'z')
            code = static_cast<char>(code - 'a' + 'A');
    }
    return out;
}

void TestFoeBuffAlert()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    CombatantState quiet;
    Check(FoeGuardAlert(quiet).empty() && FoeDefenseAlert(data, quiet).empty(),
          "a foe with no guard and no shell has no buff alert");
    CombatantState raised;
    raised.GuardPercent = 75;
    raised.BonusDefense = 150;
    raised.BonusDefenseTurns = 2;
    const std::string guard = FoeGuardAlert(raised);
    const std::string defense = FoeDefenseAlert(data, raised);
    Check(guard == "Guard 75%" && UpperAlert(guard) == "GUARD 75%",
          "a raised guard reads GUARD 75%");
    Check(defense == "Defense +150 (2)" && UpperAlert(defense) == "DEFENSE +150 (2)",
          "a defense buff reads DEFENSE +150 (2) from the Defense stat");
    raised.BonusDefenseTurns = 0;
    raised.BonusDefense = 150;
    Check(FoeDefenseAlert(data, raised).empty(), "a shell with no turns left is not shown");

    TempCopy copy;
    ReplaceInFile(copy.Path / "stats.json", "\"name\": \"Defense\"", "\"name\": \"Armor\"");
    Database renamed(copy.Path);
    Check(renamed.Load(), "a renamed defense stat loads");
    CombatantState named;
    named.BonusDefense = 12;
    named.BonusDefenseTurns = 1;
    Check(FoeDefenseAlert(renamed, named) == "Armor +12 (1)",
          "the defense alert uses the stat's name from stats.json");
}

// One riposte against the sample wolf. The flat return is 28; defense comes off that.
struct CounterBlow {
    bool Landed = false;
    i32 WolfHp = 0;
    std::string Text;
};

CounterBlow RiposteTheWolf(Database& data)
{
    CounterBlow blow;
    Battle battle(data, Sides({CombatantSide::Player, CombatantSide::Enemy}));
    std::string error;
    if (!battle.Start("guardian", 5, "wolf", error) || !battle.UseSkill("riposte").Accepted)
        return blow;
    const ActionResult bite = battle.EnemyAct();
    blow.Landed = bite.Accepted && bite.Damage == 0;
    blow.WolfHp = battle.Enemy().Hp;
    blow.Text = bite.Text;
    return blow;
}

void TestCounterMeetsDefense()
{
    Database sample(kSample);
    Check(sample.Load(), "sample data loads");
    const CounterBlow ordinary = RiposteTheWolf(sample);
    Check(ordinary.Landed && ordinary.WolfHp == 14 &&
              ordinary.Text == "Wolf uses Bite for 0. 26 returns.",
          "the wolf's defense of 2 comes off the 28 sent back");

    TempCopy naked;
    ReplaceInFile(naked.Path / "enemies.json", "\"strength\": 5,\n        \"defense\": 2,",
                  "\"strength\": 5,\n        \"defense\": 0,");
    Database undefended(naked.Path);
    Check(undefended.Load(), "a wolf with no defense loads");
    const CounterBlow open = RiposteTheWolf(undefended);
    Check(open.Landed && open.WolfHp == 12 && open.Text == "Wolf uses Bite for 0. 28 returns.",
          "with no defense the whole counter lands");

    TempCopy plated;
    ReplaceInFile(plated.Path / "enemies.json", "\"strength\": 5,\n        \"defense\": 2,",
                  "\"strength\": 5,\n        \"defense\": 40,");
    Database shelled(plated.Path);
    Check(shelled.Load(), "a heavily armored wolf loads");
    const CounterBlow stopped = RiposteTheWolf(shelled);
    Check(stopped.Landed && stopped.WolfHp == 39 &&
              stopped.Text == "Wolf uses Bite for 0. 1 returns.",
          "defense can reduce a counter only as far as minimum damage");
}

void TestSecondWindExposed()
{
    // The frozen sample still has the flat heal. This copy is the shipped shape: 8 plus
    // Focus, and Exposed on the user. The directory is still ASCENSION_DATA_DIR.
    TempCopy copy;
    ReplaceInFile(copy.Path / "skills.json",
                  "\"focusCost\": 2,\n"
                  "      \"effects\": [\n"
                  "        { \"id\": \"mend\", \"trigger\": \"onUse\", \"op\": \"heal\", "
                  "\"power\": 8 }\n"
                  "      ]",
                  "\"focusCost\": 3,\n"
                  "      \"effects\": [\n"
                  "        { \"id\": \"mend\", \"trigger\": \"onUse\", \"op\": \"heal\", "
                  "\"power\": 8, \"scaling\": \"focus\" },\n"
                  "        { \"id\": \"open\", \"trigger\": \"onUse\", \"op\": \"applyStatus\", "
                  "\"status\": \"exposed\" }\n"
                  "      ]");
    Database data(copy.Path);
    Check(data.Load(), "a focus-scaled second wind loads from the frozen sample");
    std::string error;

    Battle low(data, Sides({CombatantSide::Player}));
    Check(low.Start("fighter", 1, "wolf", error), "the focus-4 fight starts");
    Check(low.Player().Stats.Focus == 4, "the fighter's Focus is 4");
    low.SetPlayerHp(20);
    const ActionResult mend = low.UseSkill("second_wind");
    Check(mend.Accepted && mend.Healing == 12 && low.Player().Hp == 32 &&
              Holds(low.Player(), "exposed") && low.Player().Statuses.size() == 1 &&
              low.Player().Statuses[0].TurnsLeft == 2 && low.Enemy().Statuses.empty(),
          "second wind heals 8 plus Focus and leaves its user Exposed");

    Battle high(data, Sides({CombatantSide::Player}));
    StatBlock focused;
    focused.Vitality = 18;
    focused.Strength = 8;
    focused.Defense = 6;
    focused.Agility = 5;
    focused.Focus = 9;
    high.SetBaseStats(focused);
    Check(high.Start("fighter", 1, "wolf", error), "the focus-9 fight starts");
    Check(high.Player().Stats.Focus == 9 && high.Player().MaxHp == 72,
          "raising Focus leaves max health alone");
    high.SetPlayerHp(20);
    const ActionResult more = high.UseSkill("second_wind");
    Check(more.Accepted && more.Healing == 17 && high.Player().Hp == 37 &&
              Holds(high.Player(), "exposed"),
          "a higher Focus mends more");
}

// Rally sets a guard and knits a wound. The log names the health that fit under max HP.
// Second Wind is only a heal and uses the same sentence.
void TestLoggedHeal()
{
    TempCopy copy;
    ReplaceInFile(copy.Path / "skills.json",
                  "{ \"id\": \"mend\", \"trigger\": \"onUse\", \"op\": \"heal\", \"power\": 8 }\n"
                  "      ]\n"
                  "    },",
                  "{ \"id\": \"mend\", \"trigger\": \"onUse\", \"op\": \"heal\", \"power\": 8 }\n"
                  "      ]\n"
                  "    },\n"
                  "    {\n"
                  "      \"id\": \"rally\",\n"
                  "      \"name\": \"Rally\",\n"
                  "      \"description\": \"Set your guard and catch your breath in one motion.\",\n"
                  "      \"kind\": \"active\",\n"
                  "      \"target\": \"self\",\n"
                  "      \"focusCost\": 3,\n"
                  "      \"effects\": [\n"
                  "        { \"id\": \"ward\", \"trigger\": \"onUse\", \"op\": \"guard\", "
                  "\"amount\": 50 },\n"
                  "        { \"id\": \"mend\", \"trigger\": \"onUse\", \"op\": \"heal\", "
                  "\"power\": 12 }\n"
                  "      ]\n"
                  "    },");
    ReplaceInFile(copy.Path / "classes.json",
                  "\"skills\": [\"strike\", \"brace\", \"heavy_blow\", \"second_wind\"]",
                  "\"skills\": [\"strike\", \"brace\", \"heavy_blow\", \"second_wind\", \"rally\"]");
    Database data(copy.Path);
    Check(data.Load(), "a fighter who knows Rally loads");
    if (!data.IsLoaded())
        return;
    std::string error;

    Battle open(data, Sides({CombatantSide::Player}));
    Check(open.Start("fighter", 1, "wolf", error), "the rally fight starts");
    open.SetPlayerHp(40);
    const ActionResult rally = open.UseSkill("rally");
    Check(rally.Accepted && rally.Healing == 12 && open.Player().Hp == 52 &&
              open.Player().GuardPercent == 50 &&
              rally.Text == "Fighter uses Rally. The next hit is reduced by 50 percent. Heals 12.",
          "rally logs the 12 health it restored");
    if (rally.Text != "Fighter uses Rally. The next hit is reduced by 50 percent. Heals 12.")
        std::printf("    text: %s\n", rally.Text.c_str());

    Battle capped(data, Sides({CombatantSide::Player}));
    Check(capped.Start("fighter", 1, "wolf", error), "the capped rally fight starts");
    Check(capped.Player().MaxHp == 72, "the fighter's max health is 72");
    capped.SetPlayerHp(68);
    const ActionResult fitted = capped.UseSkill("rally");
    Check(fitted.Accepted && fitted.Healing == 4 && capped.Player().Hp == 72 &&
              capped.Player().GuardPercent == 50 &&
              fitted.Text == "Fighter uses Rally. The next hit is reduced by 50 percent. Heals 4.",
          "rally logs the 4 health that fit under the cap");
    if (fitted.Text != "Fighter uses Rally. The next hit is reduced by 50 percent. Heals 4.")
        std::printf("    text: %s\n", fitted.Text.c_str());

    Battle full(data, Sides({CombatantSide::Player}));
    Check(full.Start("fighter", 1, "wolf", error), "the full-health rally fight starts");
    const ActionResult none = full.UseSkill("rally");
    Check(none.Accepted && none.Healing == 0 && full.Player().Hp == 72 &&
              full.Player().GuardPercent == 50 &&
              none.Text == "Fighter uses Rally. The next hit is reduced by 50 percent.",
          "rally at full health names the guard and does not claim a heal");

    Battle wind(data, Sides({CombatantSide::Player}));
    Check(wind.Start("fighter", 1, "wolf", error), "the second wind fight starts");
    wind.SetPlayerHp(50);
    const ActionResult mend = wind.UseSkill("second_wind");
    Check(mend.Accepted && mend.Healing == 8 && wind.Player().Hp == 58 &&
              mend.Text == "Fighter uses Second Wind. Heals 8.",
          "a heal on its own uses the same words");
    wind.SetPlayerHp(70);
    const ActionResult shortWind = wind.UseSkill("second_wind");
    Check(shortWind.Accepted && shortWind.Healing == 2 && wind.Player().Hp == 72 &&
              shortWind.Text == "Fighter uses Second Wind. Heals 2.",
          "second wind logs the health that fit, not its power");
}

void TestFoeRules()
{
    TempCopy copy;
    ReplaceInFile(copy.Path / "enemies.json",
                  "      \"skills\": [\"rubble_strike\"],\n      \"experience\": 0,\n      "
                  "\"gold\": 0\n    }",
                  "      \"skills\": [\"rubble_strike\"],\n      \"experience\": 0,\n      "
                  "\"gold\": 0\n    },\n    {\n      \"id\": \"rule_post\",\n      \"name\": "
                  "\"Rule Post\",\n      \"description\": \"A post with a short pattern and two "
                  "rules.\",\n      \"level\": 1,\n      \"stats\": {\n        \"vitality\": 40,\n"
                  "        \"strength\": 0,\n        \"defense\": 0,\n        \"agility\": 1,\n"
                  "        \"focus\": 0\n      },\n      \"skills\": [\"harden\", \"thrust\"],\n"
                  "      \"pattern\": [\"harden\", \"thrust\"],\n      \"ai\": [\n        "
                  "{ \"if\": { \"targetStatus\": \"exposed\", \"next\": \"harden\" }, "
                  "\"do\": \"skill\", \"skill\": \"thrust\" },\n        "
                  "{ \"if\": { \"targetStatus\": \"exposed\", \"next\": \"thrust\" }, "
                  "\"do\": \"skill\", \"skill\": \"thrust\" }\n      ],\n      \"experience\": 0,\n"
                  "      \"gold\": 0\n    }");
    ReplaceInFile(copy.Path / "enemies.json", "\"pattern\": [\"harden\", \"thrust\", \"thrust\"],",
                  "\"pattern\": [\"harden\", \"thrust\", \"thrust\"],\n"
                  "      \"ai\": [\n"
                  "        { \"if\": { \"targetGuardAtLeast\": 1 }, \"do\": \"skill\", "
                  "\"skill\": \"harden\" }\n"
                  "      ],");
    Database data(copy.Path);
    DataError loadError;
    Check(data.Load(&loadError), "the rule post loads");
    if (!loadError.Message.empty())
        std::printf("    %s\n", loadError.Message.c_str());
    if (!data.IsLoaded())
        return;
    std::string error;

    Battle opening(data, std::make_unique<AgilityRoundModel>());
    Check(opening.Start("fighter", 1, "rule_post", error), "the rule post fight starts");
    Check(opening.UseSkill("strike").Accepted, "the fighter strikes first");
    const ActionResult hardened = opening.EnemyAct();
    Check(hardened.Accepted && hardened.SkillId == "harden" && opening.Enemy().PatternStep == 1 &&
              opening.Enemy().BonusDefense == 4 && opening.Enemy().BonusDefenseTurns == 3,
          "with no Exposed the post hardens and advances the step");
    if (hardened.SkillId != "harden" || opening.Enemy().PatternStep != 1)
        std::printf("    opening: %s step %d\n", hardened.SkillId.c_str(),
                    opening.Enemy().PatternStep);

    Battle inserted(data, std::make_unique<AgilityRoundModel>());
    Check(inserted.Start("fighter", 1, "rule_post", error) &&
              inserted.Inflict(CombatantSide::Player, "exposed", error),
          "the fighter starts Exposed");
    Check(inserted.UseSkill("strike").Accepted && Holds(inserted.Player(), "exposed"),
          "the opening strike leaves Exposed up");
    const ActionResult thrust = inserted.EnemyAct();
    Check(thrust.Accepted && thrust.SkillId == "thrust" && inserted.Enemy().PatternStep == 0 &&
              inserted.Enemy().BonusDefense == 0,
          "Exposed inserts Thrust and leaves Harden queued");
    if (thrust.SkillId != "thrust" || inserted.Enemy().PatternStep != 0)
        std::printf("    insert: %s step %d\n", thrust.SkillId.c_str(),
                    inserted.Enemy().PatternStep);

    Battle consumed(data, std::make_unique<AgilityRoundModel>());
    Check(consumed.Start("fighter", 1, "rule_post", error), "the consume fight starts");
    Check(consumed.UseSkill("strike").Accepted && consumed.EnemyAct().SkillId == "harden" &&
              consumed.Enemy().PatternStep == 1,
          "the post hardens once so Thrust is next");
    Check(consumed.Inflict(CombatantSide::Player, "exposed", error) &&
              consumed.UseSkill("strike").Accepted && Holds(consumed.Player(), "exposed"),
          "Exposed is up while Thrust is queued");
    const ActionResult head = consumed.EnemyAct();
    Check(head.Accepted && head.SkillId == "thrust" && consumed.Enemy().PatternStep == 2,
          "a rule that plays the pattern head consumes the step");
    if (head.SkillId != "thrust" || consumed.Enemy().PatternStep != 2)
        std::printf("    head: %s step %d\n", head.SkillId.c_str(), consumed.Enemy().PatternStep);

    Battle sentry(data, std::make_unique<AgilityRoundModel>());
    Check(sentry.Start("fighter", 1, "barrow_sentry", error), "the guarded sentry fight starts");
    Check(sentry.UseSkill("strike").Accepted && sentry.EnemyAct().SkillId == "harden" &&
              sentry.Enemy().PatternStep == 1,
          "with no guard the sentry hardens and advances");
    Check(sentry.UseSkill("brace").Accepted && sentry.Player().GuardPercent == 50,
          "the fighter braces before the thrust");
    const ActionResult answered = sentry.EnemyAct();
    Check(answered.Accepted && answered.SkillId == "harden" && sentry.Enemy().PatternStep == 1,
          "a guard of at least 1 inserts Harden and leaves the thrust queued");
    if (answered.SkillId != "harden" || sentry.Enemy().PatternStep != 1)
        std::printf("    guard rule: %s step %d\n", answered.SkillId.c_str(),
                    sentry.Enemy().PatternStep);
}

void TestAllyHp()
{
    TempCopy copy;
    ReplaceInFile(copy.Path / "enemies.json",
                  "      \"skills\": [\"rubble_strike\"],\n      \"experience\": 0,\n      "
                  "\"gold\": 0\n    }",
                  "      \"skills\": [\"rubble_strike\"],\n      \"experience\": 0,\n      "
                  "\"gold\": 0\n    },\n    {\n      \"id\": \"caller\",\n      \"name\": "
                  "\"Caller\",\n      \"description\": \"A made-up foe that watches its allies.\",\n"
                  "      \"level\": 1,\n      \"stats\": {\n        \"vitality\": 40,\n"
                  "        \"strength\": 0,\n        \"defense\": 0,\n        \"agility\": 3,\n"
                  "        \"focus\": 0\n      },\n      \"skills\": [\"harden\", \"bite\"],\n"
                  "      \"pattern\": [\"harden\"],\n      \"ai\": [\n"
                  "        { \"if\": { \"allyHpAtMost\": 50 }, \"do\": \"skill\", "
                  "\"skill\": \"bite\" }\n"
                  "      ],\n      \"experience\": 0,\n      \"gold\": 0\n    },\n    {\n"
                  "      \"id\": \"mate\",\n      \"name\": \"Mate\",\n"
                  "      \"description\": \"A made-up ally with a known health pool.\",\n"
                  "      \"level\": 1,\n      \"stats\": {\n        \"vitality\": 10,\n"
                  "        \"strength\": 0,\n        \"defense\": 0,\n        \"agility\": 1,\n"
                  "        \"focus\": 0\n      },\n      \"skills\": [\"harden\"],\n"
                  "      \"pattern\": [\"harden\"],\n      \"experience\": 0,\n      \"gold\": 0\n"
                  "    }");
    ReplaceInFile(copy.Path / "encounters.json",
                  "      \"enemies\": [\"oathkeeper_varn\"]\n    }",
                  "      \"enemies\": [\"oathkeeper_varn\"]\n    },\n    {\n"
                  "      \"id\": \"ally_pack\",\n      \"name\": \"Ally Pack\",\n"
                  "      \"enemies\": [\"caller\", \"mate\", \"mate\"]\n    },\n    {\n"
                  "      \"id\": \"caller_alone\",\n      \"name\": \"Caller Alone\",\n"
                  "      \"enemies\": [\"caller\"]\n    }");
    Database data(copy.Path);
    DataError loadError;
    Check(data.Load(&loadError), "the caller and its mates load");
    if (!loadError.Message.empty())
        std::printf("    %s\n", loadError.Message.c_str());
    if (!data.IsLoaded())
        return;
    std::string error;

    auto ready = [&](Battle& battle, const char* encounter) {
        Check(battle.Start("fighter", 1, encounter, error), "the made-up fight starts");
        Check(battle.TargetIndex() == 0 && battle.UseSkill("strike").Accepted,
              "the fighter strikes the caller, not a mate");
    };

    Battle mixed(data, std::make_unique<AgilityRoundModel>());
    ready(mixed, "ally_pack");
    mixed.SetTarget(1);
    mixed.SetEnemyHp(8);
    mixed.SetTarget(2);
    Check(mixed.Enemies()[1].Hp == 8 && mixed.Enemies()[2].Hp == 40,
          "one mate is at 8 of 40 and the other is full");
    const ActionResult mixedAct = mixed.EnemyAct();
    Check(mixedAct.SkillId == "harden" && mixedAct.Text.find("Caller uses Harden") != std::string::npos,
          "8 plus 40 of 80 is above 50, so one wounded ally does not trip the combined pool");
    if (mixedAct.SkillId != "harden")
        std::printf("    mixed: %s\n", mixedAct.Text.c_str());

    Battle boundary(data, std::make_unique<AgilityRoundModel>());
    ready(boundary, "ally_pack");
    boundary.SetTarget(1);
    boundary.SetEnemyHp(20);
    boundary.SetTarget(2);
    boundary.SetEnemyHp(20);
    Check(boundary.Enemies()[1].Hp == 20 && boundary.Enemies()[2].Hp == 20,
          "each mate is at half");
    const ActionResult half = boundary.EnemyAct();
    Check(half.SkillId == "bite" && half.Text.find("Caller uses Bite") != std::string::npos,
          "20 plus 20 of 80 is 50, and allyHpAtMost includes that combined percent");
    if (half.SkillId != "bite")
        std::printf("    half: %s\n", half.Text.c_str());

    Battle gone(data, std::make_unique<AgilityRoundModel>());
    ready(gone, "ally_pack");
    gone.SetTarget(1);
    gone.SetEnemyHp(0);
    gone.SetTarget(2);
    gone.SetEnemyHp(0);
    Check(gone.Result() == BattleResult::Ongoing && !gone.Enemies()[1].Alive() &&
              !gone.Enemies()[2].Alive() && gone.Enemies()[0].Alive(),
          "both mates can fall while the caller still stands");
    const ActionResult alone = gone.EnemyAct();
    Check(alone.SkillId == "harden" && alone.Text.find("Caller uses Harden") != std::string::npos,
          "no living ally does not satisfy allyHpAtMost");
    if (alone.SkillId != "harden")
        std::printf("    gone: %s\n", alone.Text.c_str());

    Battle solo(data, std::make_unique<AgilityRoundModel>());
    ready(solo, "caller_alone");
    const ActionResult soloAct = solo.EnemyAct();
    Check(soloAct.SkillId == "harden" && solo.Enemies().size() == 1,
          "a lone caller does not treat itself as an ally");
    if (soloAct.SkillId != "harden")
        std::printf("    solo: %s\n", soloAct.Text.c_str());
}

void StrikeIfPlayer(Battle& battle)
{
    if (battle.Actor() == CombatantSide::Player)
        battle.UseSkill("strike");
}

void TestBear()
{
    Database data(ASCENSION_SHIPPED_DATA_DIR);
    DataError loadError;
    Check(data.Load(&loadError), "shipped data loads for the bear");
    if (!loadError.Message.empty())
        std::printf("    %s\n", loadError.Message.c_str());
    if (!data.IsLoaded())
        return;
    std::string error;

    Battle maul(data, std::make_unique<AgilityRoundModel>());
    Check(maul.Start("fighter", 1, "bear", error), "the bear fight starts");
    Check(maul.Enemies().size() == 1 && maul.Enemy().Id == "bear", "the bear is fought alone");
    Check(maul.Actor() == CombatantSide::Player, "a level-1 fighter is quicker than the bear");
    StrikeIfPlayer(maul);
    const ActionResult opened = maul.EnemyAct();
    Check(opened.Accepted && opened.SkillId == "rending_maul" && maul.Enemy().PatternStep == 0 &&
              Holds(maul.Player(), "bleed"),
          "with no Bleed the bear mauls and leaves the swipe queued");
    if (opened.SkillId != "rending_maul" || maul.Enemy().PatternStep != 0)
        std::printf("    maul: %s step %d\n", opened.SkillId.c_str(), maul.Enemy().PatternStep);

    Battle swipe(data, std::make_unique<AgilityRoundModel>());
    Check(swipe.Start("fighter", 1, "bear", error) &&
              swipe.Inflict(CombatantSide::Player, "bleed", error),
          "the fighter starts Bleeding");
    StrikeIfPlayer(swipe);
    const ActionResult heavy = swipe.EnemyAct();
    Check(heavy.Accepted && heavy.SkillId == "heavy_swipe" && swipe.Enemy().PatternStep == 1 &&
              !Holds(swipe.Enemy(), "enraged"),
          "Bleed already up, so the bear swipes and consumes that pattern step");
    if (heavy.SkillId != "heavy_swipe" || swipe.Enemy().PatternStep != 1)
        std::printf("    swipe: %s step %d\n", heavy.SkillId.c_str(), swipe.Enemy().PatternStep);

    Battle rage(data, std::make_unique<AgilityRoundModel>());
    Check(rage.Start("fighter", 1, "bear", error), "the enrage fight starts");
    rage.SetEnemyHp(rage.Enemy().MaxHp * 40 / 100);
    Check(rage.Enemy().Hp * 100 <= rage.Enemy().MaxHp * 40, "the bear is at 40 percent or below");
    StrikeIfPlayer(rage);
    const ActionResult enraged = rage.EnemyAct();
    Check(enraged.Accepted && enraged.SkillId == "enrage" && Holds(rage.Enemy(), "enraged") &&
              rage.Enemy().PatternStep == 0,
          "low health inserts Enrage and leaves the pattern where it was");
    if (enraged.SkillId != "enrage" || rage.Enemy().PatternStep != 0)
        std::printf("    enrage: %s step %d\n", enraged.SkillId.c_str(), rage.Enemy().PatternStep);
}

void TestStatusNameOnce()
{
    Database data(ASCENSION_SHIPPED_DATA_DIR);
    DataError error;
    Check(data.Load(&error), "shipped data loads for the status log");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    if (!data.IsLoaded())
        return;
    std::string why;

    Battle fang(data, Sides({CombatantSide::Player}));
    fang.SetWornItems({"alphas_fang"});
    Check(fang.Start("fighter", 1, "wolf", why), "the fang fight starts");
    const ActionResult cast = fang.UseSkill("fang_bloodlust");
    Check(cast.Accepted && cast.Text == "Fighter uses Bloodlust." &&
              Holds(fang.Player(), "bloodlust"),
          "a status with the skill's name is logged once");
    if (cast.Text != "Fighter uses Bloodlust.")
        std::printf("    fang: %s\n", cast.Text.c_str());

    Battle wolf(data, std::make_unique<AgilityRoundModel>());
    Check(wolf.Start("fighter", 1, "dire_wolf", why), "the dire wolf fight starts");
    wolf.SetPlayerHp(wolf.Player().MaxHp * 45 / 100);
    Check(wolf.Actor() == CombatantSide::Enemy, "the dire wolf acts first");
    const ActionResult rage = wolf.EnemyAct();
    Check(rage.Accepted && rage.SkillId == "dire_bloodlust" &&
              rage.Text == "Dire Wolf uses Bloodlust.",
          "another skill that shares its status name is also logged once");
    if (rage.Text != "Dire Wolf uses Bloodlust.")
        std::printf("    dire: %s\n", rage.Text.c_str());
}

std::vector<TurnSeat> ThreeLooks()
{
    std::vector<TurnSeat> turns;
    turns.push_back(EnemySeat(0));
    turns.push_back(TurnSeat{});
    turns.push_back(EnemySeat(0));
    turns.push_back(TurnSeat{});
    turns.push_back(EnemySeat(0));
    return turns;
}

void DropSeat(Battle& battle, i32 index)
{
    Check(battle.SetTarget(index), "the ally can be marked");
    battle.SetEnemyHp(0);
}

void TestAllyCount()
{
    TempCopy copy;
    ReplaceInFile(copy.Path / "enemies.json",
                  "      \"skills\": [\"rubble_strike\"],\n      \"experience\": 0,\n      "
                  "\"gold\": 0\n    }",
                  "      \"skills\": [\"rubble_strike\"],\n      \"experience\": 0,\n      "
                  "\"gold\": 0\n    },\n    {\n      \"id\": \"counter\",\n      \"name\": "
                  "\"Counter\",\n      \"description\": \"A made-up foe that counts its "
                  "allies.\",\n      \"level\": 1,\n      \"stats\": {\n        \"vitality\": 40,\n"
                  "        \"strength\": 0,\n        \"defense\": 0,\n        \"agility\": 1,\n"
                  "        \"focus\": 0\n      },\n      \"skills\": [\"harden\", \"bite\", "
                  "\"thrust\"],\n      \"pattern\": [\"harden\"],\n      \"ai\": [\n"
                  "        { \"if\": { \"allyCountAtMost\": 0 }, \"do\": \"skill\", "
                  "\"skill\": \"thrust\" },\n"
                  "        { \"if\": { \"allyCountAtMost\": 1 }, \"do\": \"skill\", "
                  "\"skill\": \"bite\" }\n"
                  "      ],\n      \"experience\": 0,\n      \"gold\": 0\n    },\n    {\n"
                  "      \"id\": \"floor\",\n      \"name\": \"Floor\",\n"
                  "      \"description\": \"A made-up foe that wants a full pack.\",\n"
                  "      \"level\": 1,\n      \"stats\": {\n        \"vitality\": 40,\n"
                  "        \"strength\": 0,\n        \"defense\": 0,\n        \"agility\": 1,\n"
                  "        \"focus\": 0\n      },\n      \"skills\": [\"harden\", \"bite\"],\n"
                  "      \"pattern\": [\"harden\"],\n      \"ai\": [\n"
                  "        { \"if\": { \"allyCountAtLeast\": 2 }, \"do\": \"skill\", "
                  "\"skill\": \"bite\" }\n"
                  "      ],\n      \"experience\": 0,\n      \"gold\": 0\n    },\n    {\n"
                  "      \"id\": \"mate\",\n      \"name\": \"Mate\",\n"
                  "      \"description\": \"A made-up ally.\",\n"
                  "      \"level\": 1,\n      \"stats\": {\n        \"vitality\": 10,\n"
                  "        \"strength\": 0,\n        \"defense\": 0,\n        \"agility\": 1,\n"
                  "        \"focus\": 0\n      },\n      \"skills\": [\"harden\"],\n"
                  "      \"pattern\": [\"harden\"],\n      \"experience\": 0,\n      \"gold\": 0\n"
                  "    }");
    ReplaceInFile(copy.Path / "encounters.json",
                  "      \"enemies\": [\"oathkeeper_varn\"]\n    }",
                  "      \"enemies\": [\"oathkeeper_varn\"]\n    },\n    {\n"
                  "      \"id\": \"count_pack\",\n      \"name\": \"Count Pack\",\n"
                  "      \"enemies\": [\"counter\", \"mate\", \"mate\"]\n    },\n    {\n"
                  "      \"id\": \"floor_pack\",\n      \"name\": \"Floor Pack\",\n"
                  "      \"enemies\": [\"floor\", \"mate\", \"mate\"]\n    }");
    Database data(copy.Path);
    DataError loadError;
    Check(data.Load(&loadError), "the counter and the floor load");
    if (!loadError.Message.empty())
        std::printf("    %s\n", loadError.Message.c_str());
    if (!data.IsLoaded())
        return;
    std::string error;

    Battle counted(data, std::make_unique<ScriptedTurns>(ThreeLooks()));
    Check(counted.Start("fighter", 1, "count_pack", error), "the count fight starts");
    const ActionResult two = counted.EnemyAct();
    Check(two.SkillId == "harden", "two living allies miss both at-most rules");
    if (two.SkillId != "harden")
        std::printf("    two: %s\n", two.Text.c_str());
    DropSeat(counted, 1);
    Check(counted.Wait().Accepted, "the fighter yields after one mate falls");
    const ActionResult one = counted.EnemyAct();
    Check(one.SkillId == "bite", "one living ally matches allyCountAtMost 1");
    if (one.SkillId != "bite")
        std::printf("    one: %s\n", one.Text.c_str());
    DropSeat(counted, 2);
    Check(counted.Result() == BattleResult::Ongoing && counted.Wait().Accepted,
          "the counter still stands after both mates fall");
    const ActionResult none = counted.EnemyAct();
    Check(none.SkillId == "thrust", "no living ally matches allyCountAtMost 0");
    if (none.SkillId != "thrust")
        std::printf("    none: %s\n", none.Text.c_str());

    Battle floored(data, std::make_unique<ScriptedTurns>(ThreeLooks()));
    Check(floored.Start("fighter", 1, "floor_pack", error), "the floor fight starts");
    const ActionResult full = floored.EnemyAct();
    Check(full.SkillId == "bite", "two living allies match allyCountAtLeast 2");
    if (full.SkillId != "bite")
        std::printf("    full: %s\n", full.Text.c_str());
    DropSeat(floored, 1);
    Check(floored.Wait().Accepted, "the fighter yields with one mate left");
    const ActionResult thin = floored.EnemyAct();
    Check(thin.SkillId == "harden", "one living ally misses allyCountAtLeast 2");
    if (thin.SkillId != "harden")
        std::printf("    thin: %s\n", thin.Text.c_str());
    DropSeat(floored, 2);
    Check(floored.Wait().Accepted, "the fighter yields once the floor is alone");
    const ActionResult solo = floored.EnemyAct();
    Check(solo.SkillId == "harden", "a lone foe does not count itself");
    if (solo.SkillId != "harden")
        std::printf("    solo: %s\n", solo.Text.c_str());
}

void TestLowestHpHeal()
{
    TempCopy copy;
    ReplaceInFile(copy.Path / "skills.json",
                  "\"name\": \"Second Wind\",\n      \"description\": \"Catch your breath and "
                  "knit a wound.\",\n      \"kind\": \"active\",\n      \"target\": \"self\",\n"
                  "      \"focusCost\": 2,\n      \"effects\": [\n        { \"id\": \"mend\", "
                  "\"trigger\": \"onUse\", \"op\": \"heal\", \"power\": 8 }\n      ]",
                  "\"name\": \"Second Wind\",\n      \"description\": \"Catch your breath and "
                  "knit a wound.\",\n      \"kind\": \"active\",\n      \"target\": \"self\",\n"
                  "      \"focusCost\": 0,\n      \"effects\": [\n        { \"id\": \"mend\", "
                  "\"trigger\": \"onUse\", \"op\": \"heal\", \"power\": 8, \"target\": "
                  "\"lowestHpAlly\" }\n      ]");
    ReplaceInFile(copy.Path / "enemies.json",
                  "      \"skills\": [\"rubble_strike\"],\n      \"experience\": 0,\n      "
                  "\"gold\": 0\n    }",
                  "      \"skills\": [\"rubble_strike\"],\n      \"experience\": 0,\n      "
                  "\"gold\": 0\n    },\n    {\n      \"id\": \"big\",\n      \"name\": \"Big\",\n"
                  "      \"description\": \"A made-up foe with a large pool.\",\n"
                  "      \"level\": 1,\n      \"stats\": {\n        \"vitality\": 25,\n"
                  "        \"strength\": 0,\n        \"defense\": 0,\n        \"agility\": 1,\n"
                  "        \"focus\": 0\n      },\n      \"skills\": [\"harden\"],\n"
                  "      \"pattern\": [\"harden\"],\n      \"experience\": 0,\n      \"gold\": 0\n"
                  "    },\n    {\n      \"id\": \"small\",\n      \"name\": \"Small\",\n"
                  "      \"description\": \"A made-up foe with a small pool.\",\n"
                  "      \"level\": 1,\n      \"stats\": {\n        \"vitality\": 10,\n"
                  "        \"strength\": 0,\n        \"defense\": 0,\n        \"agility\": 1,\n"
                  "        \"focus\": 0\n      },\n      \"skills\": [\"harden\"],\n"
                  "      \"pattern\": [\"harden\"],\n      \"experience\": 0,\n      \"gold\": 0\n"
                  "    },\n    {\n      \"id\": \"mender\",\n      \"name\": \"Mender\",\n"
                  "      \"description\": \"A made-up foe that mends someone else.\",\n"
                  "      \"level\": 1,\n      \"stats\": {\n        \"vitality\": 20,\n"
                  "        \"strength\": 0,\n        \"defense\": 0,\n        \"agility\": 1,\n"
                  "        \"focus\": 0\n      },\n      \"skills\": [\"second_wind\"],\n"
                  "      \"pattern\": [\"second_wind\"],\n      \"experience\": 0,\n"
                  "      \"gold\": 0\n    },\n    {\n      \"id\": \"mate\",\n      \"name\": "
                  "\"Mate\",\n      \"description\": \"A made-up ally.\",\n      \"level\": 1,\n"
                  "      \"stats\": {\n        \"vitality\": 10,\n        \"strength\": 0,\n"
                  "        \"defense\": 0,\n        \"agility\": 1,\n        \"focus\": 0\n"
                  "      },\n      \"skills\": [\"harden\"],\n      \"pattern\": [\"harden\"],\n"
                  "      \"experience\": 0,\n      \"gold\": 0\n    }");
    ReplaceInFile(copy.Path / "encounters.json",
                  "      \"enemies\": [\"oathkeeper_varn\"]\n    }",
                  "      \"enemies\": [\"oathkeeper_varn\"]\n    },\n    {\n"
                  "      \"id\": \"wound_pack\",\n      \"name\": \"Wound Pack\",\n"
                  "      \"enemies\": [\"big\", \"small\"]\n    },\n    {\n"
                  "      \"id\": \"mend_pack\",\n      \"name\": \"Mend Pack\",\n"
                  "      \"enemies\": [\"mender\", \"mate\"]\n    }");
    Database data(copy.Path);
    DataError loadError;
    Check(data.Load(&loadError), "the aimed heal loads");
    if (!loadError.Message.empty())
        std::printf("    %s\n", loadError.Message.c_str());
    if (!data.IsLoaded())
        return;
    std::string error;

    // The pool is the foe side, the same seats as allEnemies. The player has no allies,
    // so this cast still picks the most wounded foe. A foe's cast, below, picks another
    // foe, which from that foe is an ally.
    Battle wounds(data, Sides({CombatantSide::Player}));
    Check(wounds.Start("fighter", 1, "wound_pack", error), "the wound fight starts");
    Check(wounds.SetTarget(0) && wounds.Enemies()[0].MaxHp == 100, "Big has 100 health");
    wounds.SetEnemyHp(20);
    Check(wounds.SetTarget(1) && wounds.Enemies()[1].MaxHp == 40, "Small has 40 health");
    wounds.SetEnemyHp(16);
    const i32 heroHp = wounds.Player().Hp;
    const ActionResult percent = wounds.UseSkill("second_wind");
    Check(percent.Text == "Fighter uses Second Wind. Heals Big 8." &&
              wounds.Enemies()[0].Hp == 28 && wounds.Enemies()[1].Hp == 16 &&
              wounds.Player().Hp == heroHp,
          "20 of 100 is healed ahead of 16 of 40");
    if (percent.Text != "Fighter uses Second Wind. Heals Big 8.")
        std::printf("    percent: %s\n", percent.Text.c_str());

    Battle tied(data, Sides({CombatantSide::Player}));
    Check(tied.Start("fighter", 1, "wound_pack", error), "the tie fight starts");
    Check(tied.SetTarget(0), "Big can be marked");
    tied.SetEnemyHp(50);
    Check(tied.SetTarget(1), "Small can be marked");
    tied.SetEnemyHp(20);
    const ActionResult same = tied.UseSkill("second_wind");
    Check(same.Text == "Fighter uses Second Wind. Heals Big 8." && tied.Enemies()[0].Hp == 58 &&
              tied.Enemies()[1].Hp == 20,
          "a percent tie keeps the earlier seat");
    if (same.Text != "Fighter uses Second Wind. Heals Big 8.")
        std::printf("    tie: %s\n", same.Text.c_str());

    Battle aimed(data, std::make_unique<ScriptedTurns>(std::vector<TurnSeat>{EnemySeat(0)}));
    Check(aimed.Start("fighter", 1, "mend_pack", error), "the mender fight starts");
    Check(aimed.SetTarget(1), "the mate can be marked");
    aimed.SetEnemyHp(10);
    const i32 menderHp = aimed.Enemies()[0].Hp;
    const ActionResult helped = aimed.EnemyAct();
    Check(helped.Text == "Mender uses Second Wind. Heals Mate 8." && aimed.Enemies()[1].Hp == 18 &&
              aimed.Enemies()[0].Hp == menderHp,
          "an aimed heal restores the wounded ally and not the user");
    if (helped.Text != "Mender uses Second Wind. Heals Mate 8.")
        std::printf("    aimed: %s\n", helped.Text.c_str());

    Battle full(data, std::make_unique<ScriptedTurns>(std::vector<TurnSeat>{EnemySeat(0)}));
    Check(full.Start("fighter", 1, "mend_pack", error), "the full pack starts");
    const ActionResult spare = full.EnemyAct();
    Check(spare.Text == "Mender uses Second Wind. No one needs it." &&
              full.Result() == BattleResult::Ongoing &&
              full.Enemies()[0].Hp == full.Enemies()[0].MaxHp &&
              full.Enemies()[1].Hp == full.Enemies()[1].MaxHp,
          "a full pack is told that no one needs the heal");
    if (spare.Text != "Mender uses Second Wind. No one needs it.")
        std::printf("    spare: %s\n", spare.Text.c_str());

    Battle gone(data, std::make_unique<ScriptedTurns>(std::vector<TurnSeat>{EnemySeat(0)}));
    Check(gone.Start("fighter", 1, "mend_pack", error), "the fallen mate fight starts");
    Check(gone.SetTarget(1), "the mate can be dropped");
    gone.SetEnemyHp(0);
    const ActionResult alone = gone.EnemyAct();
    Check(alone.Text == "Mender uses Second Wind. No one needs it." &&
              gone.Result() == BattleResult::Ongoing && gone.Enemies()[0].Alive(),
          "a dead ally is not a heal target, and the mender still stands");
    if (alone.Text != "Mender uses Second Wind. No one needs it.")
        std::printf("    alone: %s\n", alone.Text.c_str());
}

void TestCarriedHp()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    std::string error;

    OpeningPools low;
    low.Hp = 50;
    low.Focus = 2;
    Battle wounded(data, std::make_unique<AgilityRoundModel>());
    Check(wounded.Start("fighter", 1, "wolf", low, error), "a wounded fight starts");
    Check(wounded.Player().Hp == 50 && wounded.Player().Focus == 2 && wounded.Player().MaxHp == 72,
          "a fight started at 50 HP and 2 focus shows those pools");
    const ActionResult bite = wounded.EnemyAct();
    Check(bite.Accepted && bite.Damage == 5 && wounded.Player().Hp == 45,
          "the bite comes off the carried HP");

    Battle full(data, std::make_unique<AgilityRoundModel>());
    Check(full.Start("fighter", 1, "wolf", error), "a lone fight starts");
    Check(full.Player().Hp == full.Player().MaxHp && full.Player().Focus == full.Player().MaxFocus &&
              full.Player().Hp == 72 && full.Player().Focus == 8,
          "a fight with no opening pools still starts full");

    OpeningPools matched;
    matched.Hp = 72;
    matched.Focus = 8;
    Battle same(data, std::make_unique<AgilityRoundModel>());
    Check(same.Start("fighter", 1, "wolf", matched, error), "a full pool start succeeds");
    const ActionResult sameBite = same.EnemyAct();
    Check(same.Player().Hp == 67 && sameBite.Damage == 5,
          "starting at full through the pool path matches a lone fight");

    Battle first(data, std::make_unique<AgilityRoundModel>());
    Check(first.Start("fighter", 1, "wolf", error), "the carry fight starts full");
    Check(first.EnemyAct().Accepted, "the wolf bites");
    Check(first.UseSkill("strike").Accepted, "the first strike lands");
    Check(first.EnemyAct().Accepted, "the wolf bites again");
    Check(first.UseSkill("strike").Accepted, "the second strike lands");
    Check(first.EnemyAct().Accepted, "the wolf bites a third time");
    const ActionResult last = first.UseSkill("strike");
    Check(last.Result == BattleResult::Victory && first.Player().Hp == 57,
          "the full wolf fight still ends at 57 HP");
    OpeningPools kept;
    kept.Hp = first.Player().Hp;
    kept.Focus = first.Player().Focus;
    Battle second(data, std::make_unique<AgilityRoundModel>());
    Check(second.Start("fighter", 1, "bandit", kept, error), "the next fight takes the leftover");
    Check(second.Player().Hp == 57 && second.Player().Focus == first.Player().Focus &&
              second.Player().Hp < second.Player().MaxHp,
          "a win keeps the HP and focus that were left");
}

} // namespace

int main()
{
    TestFormula();
    TestWolfFight();
    TestSkillsAndGuard();
    TestDefeatAndSkip();
    TestHotReloadAndPassive();
    TestPassiveAndSwap();
    TestFocusRegen();
    TestRiposteAndBulwark();
    TestBulwarkReloads();
    TestStatuses();
    TestStatusReloads();
    TestWindup();
    TestPhaseShard();
    TestWolfPack();
    TestSummonSeats();
    TestJoinedShardMark();
    TestPackAim();
    TestGearAndConsumables();
    TestBossPlans();
    TestGrantCopies();
    TestStunResist();
    TestSummonFallback();
    TestConditionalDamage();
    TestItemGrantsInFight();
    TestSteadfastBlocksStun();
    TestShellDefenseExpires();
    TestFoeBuffAlert();
    TestCounterMeetsDefense();
    TestSecondWindExposed();
    TestLoggedHeal();
    TestFoeRules();
    TestAllyHp();
    TestBear();
    TestStatusNameOnce();
    TestAllyCount();
    TestLowestHpHeal();
    TestCarriedHp();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
