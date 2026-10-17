// Combat tests: the damage formula, one Fighter against one enemy, and a swapped turn model.
// No window or GPU.

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
class ScriptedTurns final : public TurnModel {
public:
    explicit ScriptedTurns(std::vector<CombatantSide> order) : m_Order(std::move(order)) {}

    void Reset() override { m_Index = 0; }

    CombatantSide Next(i32, i32) override
    {
        if (m_Order.empty())
            return CombatantSide::Player;
        if (m_Index >= m_Order.size())
            return m_Order.back();
        return m_Order[m_Index];
    }

    void Advance() override
    {
        if (m_Index + 1 < m_Order.size())
            ++m_Index;
    }

private:
    std::vector<CombatantSide> m_Order;
    usize m_Index = 0;
};

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
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    Battle battle(data, Sides({CombatantSide::Player, CombatantSide::Enemy}));
    std::string error;
    Check(battle.Start("guardian", 5, "wolf", error), "the guardian meets the wolf");
    Check(battle.Player().Stats.Defense == 18 && battle.Player().MaxFocus == 12,
          "bulwark adds 3 defense and focus is 4 plus the focus stat");
    Check(battle.UseSkill("riposte").Accepted && battle.Player().GuardPercent == 50 &&
              battle.Player().ReflectPercent == 50 && battle.Player().ReflectFlat == 28 &&
              battle.Player().Focus == 10,
          "riposte guards 50 percent and its flat reflect is power plus defense");
    const ActionResult bite = battle.EnemyAct();
    Check(bite.Damage == 0 && battle.Enemy().Hp == 12 && battle.Player().Focus == 11 &&
              battle.Player().GuardPercent == 0,
          "half of the blocked bite returns with the defense scaling, and bulwark pays focus");

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
    Check(boss.Enemy().ShardDue, "rubble is telegraphed after three phase-2 actions");
    const ActionResult shard = boss.EnemyAct();
    Check(shard.Damage == 10 && shard.Text.find("Rubble") != std::string::npos,
          "the shard stands in for a second enemy and hits for 10");
}

bool SensiblePlan(Battle& battle, bool guardian, bool& sawPhase, bool& sawShard)
{
    for (i32 step = 0; step < 200 && battle.Result() == BattleResult::Ongoing; ++step) {
        if (battle.Enemy().Phase >= 2)
            sawPhase = true;
        if (battle.Actor() == CombatantSide::Enemy) {
            const ActionResult act = battle.EnemyAct();
            if (act.Text.find("Rubble hits") != std::string::npos)
                sawShard = true;
            continue;
        }
        const bool threat = !battle.Enemy().WindupSkill.empty() || battle.Enemy().ShardDue;
        const i32 focus = battle.Player().Focus;
        const bool hurt = battle.Player().Hp * 100 < battle.Player().MaxHp * 40;
        if (!guardian && threat && focus >= 2)
            battle.UseSkill("heavy_blow");
        else if (guardian && threat && battle.Player().GuardPercent == 0 && focus >= 2)
            battle.UseSkill("riposte");
        else if (hurt && focus >= 2 && battle.Player().Hp < battle.Player().MaxHp)
            battle.UseSkill("second_wind");
        else
            battle.UseSkill("strike");
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
          "a berserker who stuns wind-ups wins, including the rubble");

    bool guardPhase = false;
    bool guardShard = false;
    Battle guard(data, std::make_unique<AgilityRoundModel>());
    Check(guard.Start("guardian", 6, "oathkeeper_varn", error),
          "the guardian meets Oathkeeper Varn");
    Check(SensiblePlan(guard, true, guardPhase, guardShard) && guardPhase && guardShard,
          "a guardian who ripostes wind-ups and rubble wins");
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
    TestGearAndConsumables();
    TestBossPlans();
    TestGrantCopies();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
