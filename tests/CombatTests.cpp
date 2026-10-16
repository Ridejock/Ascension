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
    Check(battle.Player().MaxHp == 72 && battle.Player().Focus == 4, "fighter health and focus");
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
    Check(heavy.Damage == 24 && battle.Player().Focus == 2 && battle.Enemy().Hp == 16,
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
    Check(wasted.Accepted && wasted.Healing == 0 && full.Player().Focus == 2,
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
    Check(focus.UseSkill("brace").Accepted && focus.UseSkill("brace").Accepted,
          "two braces spend the focus");
    const ActionResult broke = focus.UseSkill("brace");
    Check(!broke.Accepted && focus.Player().Focus == 0 && focus.Actor() == CombatantSide::Player,
          "a third brace is refused and does not spend the turn");
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

    Battle guard = StartSample(
        data, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Enemy}),
        "cairn_guard");
    guard.EnemyAct();
    guard.EnemyAct();
    Check(guard.Enemy().Focus == 0 && guard.Player().Hp == 32, "two slams spend the guard's focus");
    const ActionResult skip = guard.EnemyAct();
    Check(skip.Accepted && skip.Damage == 0 && guard.Player().Hp == 32,
          "a foe with no focus left skips the turn");
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
    Check(battle.Player().Stats.Strength == 10, "bloodlust adds its strength before the fight");
    Check(battle.UseSkill("strike").Damage == 18, "the extra strength is in the blow");

    Database sample(kSample);
    Check(sample.Load(), "sample data loads");
    Battle swapped = StartSample(
        sample, std::make_unique<ScriptedTurns>(std::vector<CombatantSide>{CombatantSide::Player}),
        "wolf");
    Check(swapped.Actor() == CombatantSide::Player,
          "the turn model can put the fighter first even though the wolf is faster");
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
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
