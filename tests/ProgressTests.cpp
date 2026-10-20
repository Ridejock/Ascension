// Career tests: the XP curve, skill ranks, the level-5 choice, and a hot reload of progress.json.
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
#include "Ascension/Progress/Hero.h"
#include "Ascension/Progress/Inventory.h"
#include "Check.h"

namespace {

using namespace Ascension;

namespace fs = std::filesystem;

const fs::path kSample = ASCENSION_DATA_DIR;

struct TempCopy {
    fs::path Path;

    TempCopy()
    {
        Path = fs::temp_directory_path() / "ascension-progress-tests";
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

bool Contains(const std::vector<std::string>& ids, std::string_view id)
{
    for (const std::string& row : ids) {
        if (row == id)
            return true;
    }
    return false;
}

Hero StartHero(Database& data)
{
    Hero hero;
    std::string error;
    Check(hero.Start(data, error), "the hero starts as a fighter");
    Check(error.empty() && hero.IsReady(), "starting the hero has no error");
    return hero;
}

void Reload(Database& data)
{
    DataError error;
    Check(data.CheckForChanges(&error) == 0, "the first sight of a change waits");
    Check(data.CheckForChanges(&error) == 1, "a settled write reloads");
}

std::vector<SkillRank> RanksOf(const Hero& hero)
{
    std::vector<SkillRank> ranks;
    for (const std::string& id : hero.SkillIds()) {
        SkillRank row;
        row.Id = id;
        row.Rank = hero.RankOf(id);
        ranks.push_back(std::move(row));
    }
    return ranks;
}

void TestCurveAndChoice()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    Hero hero = StartHero(data);
    Check(hero.Level() == 1 && hero.Xp() == 0 && !hero.ChoicePending(),
          "a new fighter is level 1 with no choice");

    const XpGrant wolf = hero.GrantXp(8);
    Check(wolf.LevelAfter == 2 && !wolf.ChoiceOpened && hero.Xp() == 8 && !hero.ChoicePending(),
          "8 XP from the wolf is level 2");
    const XpGrant bandit = hero.GrantXp(18);
    Check(bandit.LevelBefore == 2 && bandit.LevelAfter == 3 && hero.Xp() == 26 &&
              !hero.ChoicePending(),
          "the bandit's 18 XP reaches level 3");
    const XpGrant moth = hero.GrantXp(14);
    Check(moth.LevelBefore == 3 && moth.LevelAfter == 4 && hero.Xp() == 40 && !hero.ChoicePending(),
          "the tomb moth's 14 XP reaches level 4");
    const XpGrant archer = hero.GrantXp(22);
    Check(archer.LevelBefore == 4 && archer.LevelAfter == 5 && archer.ChoiceOpened &&
              hero.Xp() == 62,
          "the bone archer's 22 XP opens the level-5 choice");
    const std::vector<std::string> choices = hero.ChoiceIds();
    Check(choices.size() == 2 && choices[0] == "berserker" && choices[1] == "guardian",
          "the number keys follow classes.json");

    Hero early = StartHero(data);
    Check(early.GrantXp(40).LevelAfter == 4 && !early.ChoicePending(),
          "40 XP is level 4, still short of the evolution");
    Check(early.GrantXp(19).LevelAfter == 4 && early.Xp() == 59 && !early.ChoicePending(),
          "59 XP stays below the level-5 step");
    std::string why;
    Check(!early.Choose("berserker", why), "the choice is closed before the evolve level");
    CheckHas(why, "no evolution is offered", "choosing early says nothing is offered");
    Check(early.GrantXp(1).ChoiceOpened && early.Level() == 5, "60 XP is the level-5 step");

    Hero negative = StartHero(data);
    Check(negative.GrantXp(-4).Amount == 0 && negative.Xp() == 0, "negative XP is ignored");
    Check(negative.GrantXp(2000000).LevelAfter == 10 && negative.Xp() == 999999,
          "XP stops at the cap and the last level");
    Check(negative.AtMaxLevel(), "the last curve step is max level");
}

void TestRanksAndEvolution()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    Hero hero = StartHero(data);
    Check(RankForUses(data.GetProgress(), 2) == 1 && RankForUses(data.GetProgress(), 3) == 2 &&
              RankForUses(data.GetProgress(), 6) == 3 && RankForUses(data.GetProgress(), 10) == 4 &&
              RankForUses(data.GetProgress(), 15) == 5 && RankForUses(data.GetProgress(), 40) == 5,
          "the array is the total uses for rank 2, 3, 4 and 5");
    ProgressRules flat = data.GetProgress();
    flat.UsesPerRank = {2};
    Check(RankForUses(flat, 2) == 2 && RankForUses(flat, 4) == 3,
          "a single usesPerRank integer is still a flat step");
    Check(hero.NoteUse("strike") == 1 && hero.RankOf("strike") == 1,
          "the first use stays at rank 1");
    Check(hero.NoteUse("strike") == 1 && hero.UsesOf("strike") == 2, "two uses are still rank 1");
    Check(hero.NoteUse("strike") == 2 && hero.UsesOf("strike") == 3,
          "the third use reaches rank 2");
    Check(hero.NoteUse("missing") == 1 && hero.UsesOf("missing") == 0,
          "an unknown skill is not practiced");

    hero.GrantXp(66);
    std::string why;
    Check(!hero.Choose("knight", why), "an unknown evolution is refused");
    CheckHas(why, "not offered", "the refusal names that it is not offered");
    Check(hero.Choose("berserker", why) && why.empty(), "berserker is the first path");
    Check(!hero.ChoicePending() && hero.ClassId() == "berserker", "the choice closes after a pick");
    Check(!hero.Choose("guardian", why), "a second pick is refused");
    Check(hero.UsesOf("strike") == 3 && hero.RankOf("strike") == 2 && hero.RankOf("rage") == 1,
          "practiced uses stay, and the new skills start at rank 1");
    const std::vector<std::string> skills = hero.SkillIds();
    Check(skills.size() == 6 && Contains(skills, "strike") && Contains(skills, "rage") &&
              Contains(skills, "cleave"),
          "berserker keeps strike and adds rage and cleave");

    Battle rage(data, std::make_unique<AgilityRoundModel>());
    Check(rage.Start(hero.ClassId(), hero.Level(), "wolf", why), "a berserker fight starts");
    Check(rage.Player().Stats.Strength == 20 && rage.Player().MaxHp == 144,
          "berserker health is vitality times 4, and bloodlust is not flat strength");
    const ClassDef* berserkerClass = data.FindClass(hero.ClassId());
    const StatBlock berserkerLine =
        berserkerClass != nullptr ? data.StatsAt(*berserkerClass, hero.Level()) : StatBlock{};
    Check(hero.ClassSources().size() == 2 && hero.ClassSources()[0].Id == "fighter" &&
              hero.ClassSources()[1].Id == "berserker" &&
              hero.Attributes().Strength == berserkerLine.Strength &&
              hero.AttributePoints() == StatTotal(berserkerLine),
          "fighter and berserker grants plus level growth match the class line");
    rage.SetPlayerRanks(data.GetProgress().PowerPerRank, RanksOf(hero));
    Check(rage.UseSkill("strike").Damage == 30,
          "a kept rank-2 strike still adds its power after evolving");

    Hero guard = StartHero(data);
    guard.GrantXp(60);
    Check(guard.Choose("guardian", why), "guardian is the second path");
    Battle wall(data, std::make_unique<AgilityRoundModel>());
    Check(wall.Start(guard.ClassId(), guard.Level(), "wolf", why), "a guardian fight starts");
    Check(wall.Player().Stats.Defense == 18 && wall.Player().MaxHp == 148,
          "bulwark adds 3 defense, and health follows the guardian's vitality");
}

void TestRankPower()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    Hero hero = StartHero(data);
    hero.NoteUse("strike");
    hero.NoteUse("strike");
    hero.NoteUse("strike");

    Battle battle(data, std::make_unique<AgilityRoundModel>());
    std::string error;
    Check(battle.Start("fighter", 1, "wolf", error), "the rank fight starts");
    battle.SetPlayerRanks(data.GetProgress().PowerPerRank, RanksOf(hero));
    Check(battle.EnemyAct().Accepted, "the wolf still acts first");
    const ActionResult ranked = battle.UseSkill("strike");
    Check(ranked.Damage == 18 && ranked.SkillId == "strike", "rank 2 adds two power to strike");

    Battle harder(data, std::make_unique<AgilityRoundModel>());
    Check(harder.Start("fighter", 1, "wolf", error), "the rank-3 fight starts");
    SkillRank row;
    row.Id = "strike";
    row.Rank = 3;
    harder.SetPlayerRanks(2, {row});
    harder.EnemyAct();
    Check(harder.UseSkill("strike").Damage == 20, "rank 3 adds four power to strike");

    Battle plain(data, std::make_unique<AgilityRoundModel>());
    Check(plain.Start("fighter", 1, "wolf", error), "a fight with no rank bonus starts");
    plain.SetPlayerRanks(0, {row});
    plain.EnemyAct();
    Check(plain.UseSkill("strike").Damage == 16, "a zero power-per-rank leaves strike at 16");

    Battle brace(data, std::make_unique<AgilityRoundModel>());
    Check(brace.Start("fighter", 1, "wolf", error), "the guard fight starts");
    SkillRank guard;
    guard.Id = "brace";
    guard.Rank = 5;
    brace.SetPlayerRanks(1, {guard});
    brace.EnemyAct();
    Check(brace.UseSkill("brace").Accepted && brace.Player().GuardPercent == 50,
          "rank does not change a guard's percent");
}

void TestRankLandsOnTheBlow()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    Hero fresh = StartHero(data);
    Check(fresh.RankAfterUse("strike") == 1, "the first use does not reach rank 2");

    Hero hero = StartHero(data);
    Check(hero.NoteUse("strike") == 1 && hero.NoteUse("strike") == 1 && hero.RankOf("strike") == 1,
          "two uses are still rank 1");
    Check(hero.RankAfterUse("strike") == 2, "the third use is the one that reaches rank 2");

    Battle battle(data, std::make_unique<AgilityRoundModel>());
    std::string error;
    Check(battle.Start("fighter", 1, "wolf", error), "the earning blow starts");
    std::vector<SkillRank> ranks = RanksOf(hero);
    for (SkillRank& row : ranks) {
        if (row.Id == "strike")
            row.Rank = hero.RankAfterUse(row.Id);
    }
    battle.SetPlayerRanks(data.GetProgress().PowerPerRank, std::move(ranks));
    Check(battle.EnemyAct().Accepted, "the wolf acts first");
    Check(battle.UseSkill("strike").Damage == 18,
          "the use that reaches rank 2 deals rank-2 damage");
    Check(hero.RankOf("strike") == 1, "the stored rank waits until the use is counted");
    Check(hero.NoteUse("strike") == 2, "counting the blow stores rank 2");
}

void TestHotReload()
{
    TempCopy copy;
    Database data(copy.Path);
    Check(data.Load(), "copied sample loads");
    Hero hero = StartHero(data);
    hero.GrantXp(8);
    hero.NoteUse("strike");
    hero.NoteUse("strike");
    Check(hero.Level() == 2 && hero.RankOf("strike") == 1,
          "8 XP is level 2 and two uses stay rank 1");

    ReplaceInFile(copy.Path / "progress.json", "0, 8, 20", "0, 12, 20");
    Reload(data);
    std::string why;
    Check(hero.ApplyData(why) && hero.Xp() == 8 && hero.Level() == 1,
          "a steeper curve drops the level and keeps the XP");

    ReplaceInFile(copy.Path / "progress.json", "[3, 6, 10, 15]", "1");
    Reload(data);
    Check(hero.ApplyData(why) && hero.UsesOf("strike") == 2 && hero.RankOf("strike") == 3,
          "one use per rank turns two uses into rank 3");

    hero.GrantXp(100);
    Check(hero.Choose("berserker", why), "berserker can still be chosen after the reload");
    const i32 xp = hero.Xp();
    ReplaceInFile(copy.Path / "classes.json", "\"id\": \"berserker\"", "\"id\": \"barbarian\"");
    Reload(data);
    Check(!hero.ApplyData(why) && hero.ClassId() == "berserker" && hero.Xp() == xp,
          "a missing class keeps the career");
    CheckHas(why, "berserker", "the failure names the missing class");
}

void TestChoiceBlocksTheNextFight()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    Hero hero = StartHero(data);
    hero.NoteUse("strike");
    hero.NoteUse("strike");
    hero.NoteUse("strike");
    Check(hero.UsesOf("strike") == 3 && hero.RankOf("strike") == 2, "three uses are rank 2");

    std::string why;
    Check(!hero.ResetBuild(why), "a reset is refused before a path is offered");
    CheckHas(why, "only when a path is offered", "the refusal says a path has to be offered");
    Check(hero.UsesOf("strike") == 3 && hero.NextFightBlock() == nullptr,
          "the refused reset keeps the uses, and level 1 can still fight");
    Check(hero.ApplyData(why) && hero.UsesOf("strike") == 3, "a data reload does not clear ranks");

    Check(hero.GrantXp(60).LevelAfter == 5 && hero.ChoicePending() && hero.ClassId() == "fighter",
          "level 5 opens the choice and stays a fighter");
    const char* block = hero.NextFightBlock();
    Check(block != nullptr && std::string(block).find("next fight") != std::string::npos,
          "the next fight waits until a path is chosen");
    Check(hero.ResetBuild(why) && why.empty(), "the open choice can reset the build");
    Check(hero.UsesOf("strike") == 0 && hero.RankOf("strike") == 1 && hero.Xp() == 60 &&
              hero.Level() == 5 && hero.ChoosingStarter() && hero.ChoicePending(),
          "the reset clears ranks, keeps the level, and asks for a starter class");

    Check(hero.GrantXp(30).LevelAfter == 6 && hero.ClassId() == "fighter" && hero.ChoicePending() &&
              hero.NextFightBlock() != nullptr,
          "level 6 without a path is still blocked");
    hero.NoteUse("strike");
    Check(hero.ResetBuild(why) && hero.UsesOf("strike") == 0 && hero.ChoicePending() &&
              hero.ChoosingStarter(),
          "the choice still open at level 6 can reset again");
    Check(hero.ChooseStarter("fighter", why), "the fighter is a starter");
    bool spent = true;
    while (hero.ClassPool() > 0 && spent)
        spent = hero.SpendClassPoint("strength", why);
    Check(spent && hero.ClassPool() == 0, "the pooled class points can be spent");

    Check(hero.Choose("guardian", why), "guardian is chosen from the open choice");
    hero.NoteUse("strike");
    Check(!hero.ResetBuild(why), "after the path, a reset is refused");
    CheckHas(why, "only when a path is offered",
             "the refusal is the same once the choice is closed");
    Check(hero.UsesOf("strike") == 1 && hero.NextFightBlock() == nullptr &&
              hero.ClassId() == "guardian",
          "the use after the choice stays, and the next fight is allowed");
}

bool SpendPool(Hero& hero, std::string_view statId, std::string& error)
{
    bool spent = true;
    while (hero.ClassPool() > 0 && spent)
        spent = hero.SpendClassPoint(statId, error);
    return spent && hero.ClassPool() == 0;
}

void TestResetKeepsPoints()
{
    TempCopy copy;
    ReplaceInFile(
        copy.Path / "classes.json", "      \"passives\": [\"bulwark\"]\n    }\n  ]\n}",
        "      \"passives\": [\"bulwark\"]\n    },\n    {\n      \"id\": \"ranger\",\n   "
        "   \"name\": \"Ranger\",\n      \"description\": \"A second starter.\",\n      "
        "\"starter\": true,\n      \"stats\": {\"vitality\": 1, \"strength\": 1, "
        "\"defense\": 1, \"agility\": 1, \"focus\": 1},\n      \"growth\": {\"vitality\": "
        "1, \"strength\": 1, \"defense\": 1, \"agility\": 1, \"focus\": 1},\n      "
        "\"skills\": [\"strike\"],\n      \"passives\": []\n    },\n    {\n      \"id\": "
        "\"adept\",\n      \"name\": \"Adept\",\n      \"description\": \"Not a "
        "starter.\",\n      \"stats\": {\"vitality\": 1, \"strength\": 1, \"defense\": "
        "1, \"agility\": 1, \"focus\": 1},\n      \"growth\": {\"vitality\": 1, "
        "\"strength\": 1, \"defense\": 1, \"agility\": 1, \"focus\": 1},\n      "
        "\"skills\": [\"strike\"],\n      \"passives\": []\n    }\n  ]\n}");
    Database data(copy.Path);
    Check(data.Load(), "a roster with two starters loads");
    const std::vector<const ClassDef*> starters = data.Starters();
    Check(starters.size() == 2 && starters[0]->Id == "fighter" && starters[1]->Id == "ranger",
          "starters are read from the class data, in file order");
    bool adeptListed = false;
    bool berserkerListed = false;
    for (const ClassDef* starter : starters) {
        if (starter->Id == "adept")
            adeptListed = true;
        if (starter->Id == "berserker")
            berserkerListed = true;
    }
    Check(!adeptListed && !berserkerListed, "a base class without the flag is not a starter");

    Hero hero = StartHero(data);
    hero.GrantXp(60);
    const i32 level = hero.Level();
    const i32 xp = hero.Xp();
    const i32 levelPoints = hero.LevelPoints();
    const i32 classPoints = hero.ClassPoints();
    const i32 total = hero.AttributePoints();
    const ClassDef* fighter = data.FindClass("fighter");
    const StatBlock line = fighter != nullptr ? data.StatsAt(*fighter, level) : StatBlock{};
    Check(level == 5 && fighter != nullptr && hero.ClassSources().size() == 1 &&
              hero.ClassSources()[0].Id == "fighter" && total == StatTotal(line) &&
              classPoints == StatTotal(fighter->Stats) && levelPoints + classPoints == total,
          "level growth and the fighter grant add up to the class line");

    std::string why;
    Check(!hero.ChooseStarter("ranger", why), "a starter is picked only after a reset");
    Check(hero.ResetBuild(why) && why.empty(), "the level-5 choice can reset the build");
    Check(hero.Level() == level && hero.Xp() == xp && hero.LevelPoints() == levelPoints &&
              hero.ClassPool() == classPoints && hero.ClassPoints() == classPoints &&
              hero.AttributePoints() == total && hero.ClassSources().empty(),
          "a reset keeps the level and the level points, and pools the class points");

    Check(!hero.ChooseStarter("adept", why), "adept is not offered");
    CheckHas(why, "not a starter", "the refusal says it is not a starter");
    Check(!hero.ChooseStarter("berserker", why), "an evolution is not a starter");
    Check(hero.ChooseStarter("ranger", why) && hero.ClassId() == "ranger" &&
              !hero.ChoosingStarter() && hero.Level() == level &&
              hero.LevelPoints() == levelPoints && hero.ClassPool() == classPoints &&
              hero.AttributePoints() == total,
          "any starter can be picked, and the pooled points stay the same size");

    const i32 levelDefense = hero.Attributes().Defense;
    Check(SpendPool(hero, "defense", why) && hero.ClassPool() == 0 && hero.Level() == level &&
              hero.LevelPoints() == levelPoints && hero.AttributePoints() == total &&
              hero.Attributes().Defense == levelDefense + classPoints,
          "spending the pool changes where the class points sit, not how many there are");
    Check(hero.ClassSources().size() == 1 && hero.ClassSources()[0].Id == "ranger" &&
              StatTotal(hero.ClassSources()[0].Points) == classPoints,
          "the spent points are remembered on the class that was picked");

    Battle battle(data, std::make_unique<AgilityRoundModel>());
    battle.SetBaseStats(hero.Attributes());
    Check(battle.Start("ranger", hero.Level(), "wolf", why) &&
              battle.Player().Stats.Defense == levelDefense + classPoints &&
              battle.Player().MaxHp == hero.Attributes().Vitality * 4,
          "the next fight uses the redistributed attributes");
}

void TestResetClearsItemPractice()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    Hero hero = StartHero(data);
    Inventory bag;
    std::string why;
    Check(bag.Start(data, why), "the bag starts");
    Check(bag.Give("rusty_sword", 1, why) && bag.Equip(0, why), "the sword is worn");
    hero.NoteUse("cleave");
    hero.NoteUse("cleave");
    hero.NoteUse("cleave");
    hero.NoteUse("strike");
    Check(hero.UsesOf("cleave") == 3 && hero.RankOf("cleave") == 2 && hero.UsesOf("strike") == 1,
          "an item skill ranks by its id, the same as a class skill");
    Check(hero.GrantXp(60).LevelAfter == 5, "level 5 opens a reset");
    Check(hero.ResetBuild(why) && why.empty(), "the reset runs");
    Check(hero.UsesOf("cleave") == 0 && hero.UsesOf("strike") == 0 && hero.RankOf("cleave") == 1,
          "a reset clears item-skill practice along with class skills");
    Check(bag.Equipped(GearSlot::Weapon) == "rusty_sword", "a reset does not touch worn gear");
}

void TestNextFoeStaysWhileChoosing()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    const auto enemies = data.GetEnemies();
    const i32 roster = static_cast<i32>(enemies.size());
    Check(roster == 7 && enemies[0].Id == "wolf" && enemies[1].Id == "bandit",
          "the roster opens wolf, then bandit");

    Hero hero = StartHero(data);
    i32 foe = 0;
    Check(hero.IndexAfterNextFoe(foe, roster) == 1, "before the choice, N moves to the bandit");
    foe = hero.IndexAfterNextFoe(foe, roster);
    Check(foe == 1, "the bandit is the foe a free N would store");

    foe = 0;
    hero.GrantXp(62);
    Check(hero.Level() == 5 && hero.ChoicePending() && hero.ClassId() == "fighter",
          "the bone archer's XP opens the choice on the wolf");
    foe = hero.IndexAfterNextFoe(foe, roster);
    foe = hero.IndexAfterNextFoe(foe, roster);
    Check(foe == 0 && enemies[static_cast<usize>(foe)].Id == "wolf",
          "N while the choice is open leaves the wolf selected");
    Check(hero.IndexAfterNextFoe(roster - 1, roster) == roster - 1,
          "N does not move the last slot while the choice is open");

    std::string why;
    Check(hero.Choose("berserker", why), "berserker is chosen with the wolf still selected");
    Check(foe == 0, "choosing a path does not move the roster");
    Check(hero.IndexAfterNextFoe(foe, roster) == 1, "after the path, N reaches the bandit");
    Check(hero.IndexAfterNextFoe(-1, roster) == 0, "a negative slot steps onto the wolf");
    Check(hero.IndexAfterNextFoe(2, 0) == 2, "an empty roster does not move");
}

// The player acts, then the same seat is returned. Guard checks do not need the foe to move.
class PlayerFirst final : public TurnModel {
public:
    void Reset() override {}

    TurnSeat Next(std::span<const TurnSeat>) override { return {}; }

    void Advance() override {}

    void Capture(std::vector<TurnSeat>& order, i32& index) const override
    {
        order.clear();
        index = 0;
    }

    void Place(std::span<const TurnSeat>, i32) override {}
};

i32 CountId(const std::vector<std::string>& ids, std::string_view id)
{
    i32 count = 0;
    for (const std::string& row : ids) {
        if (row == id)
            ++count;
    }
    return count;
}

bool SpendPool(Hero& hero, std::string& error)
{
    while (hero.ClassPool() > 0) {
        if (!hero.SpendClassPoint("vitality", error))
            return false;
    }
    return true;
}

void AddRally(const fs::path& dir)
{
    ReplaceInFile(dir / "skills.json", "      \"id\": \"stone_skin\",",
                  "      \"id\": \"rally\",\n"
                  "      \"name\": \"Rally\",\n"
                  "      \"description\": \"Raise a guard and close a small wound.\",\n"
                  "      \"kind\": \"active\",\n"
                  "      \"target\": \"self\",\n"
                  "      \"focusCost\": 3,\n"
                  "      \"effects\": [\n"
                  "        { \"id\": \"ward\", \"trigger\": \"onUse\", \"op\": \"guard\", "
                  "\"amount\": 50 },\n"
                  "        { \"id\": \"mend\", \"trigger\": \"onUse\", \"op\": \"heal\", "
                  "\"power\": 12 }\n"
                  "      ]\n"
                  "    },\n"
                  "    {\n"
                  "      \"id\": \"stone_skin\",");
}

void TestSkillPicks()
{
    {
        TempCopy copy;
        AddRally(copy.Path);
        ReplaceInFile(copy.Path / "classes.json",
                      "\"skills\": [\"shield_wall\", \"riposte\"],\n      \"passives\": "
                      "[\"bulwark\"]",
                      "\"skills\": [\"shield_wall\"],\n      \"passives\": [\"bulwark\"],\n"
                      "      \"picks\": [\n"
                      "        { \"level\": 6, \"options\": [\"riposte\", \"cleave\"] },\n"
                      "        { \"level\": 8, \"options\": [\"rally\", { \"skill\": \"brace\", "
                      "\"tune\": { \"ward\": { \"amount\": 75 } } }] }\n"
                      "      ]");
        Database data(copy.Path);
        DataError error;
        Check(data.Load(&error), "guardian picks load");
        if (!error.Message.empty())
            std::printf("    %s\n", error.Message.c_str());
        Hero hero = StartHero(data);
        Check(!hero.PickPending() && hero.PickedIds().empty(), "level 1 has no pick");
        hero.GrantXp(62);
        Check(hero.Level() == 5 && hero.ChoicePending() && !hero.PickPending(),
              "level 5 opens the path and no pick");
        std::string why;
        Check(hero.Choose("guardian", why), "guardian is chosen");
        const XpGrant sixth = hero.GrantXp(126 - 62);
        Check(hero.Level() == 6 && sixth.PickOpened && hero.PickPending(), "level 6 opens a pick");
        const std::vector<std::string> first = hero.PickOptions();
        Check(first.size() == 2 && first[0] == "riposte" && first[1] == "cleave",
              "level 6 offers riposte then cleave");
        const char* blocked = hero.NextFightBlock();
        Check(blocked != nullptr && std::string(blocked) == "Pick a skill before the next fight.",
              "the next fight waits on the pick");
        Check(hero.IndexAfterNextFoe(0, 4) == 0, "N does not advance while a pick is open");
        Check(!hero.Pick("rally", why), "a later pick's option is refused");
        CheckHas(why, "rally", "the refusal names the skill");
        Check(hero.Pick(first[0], why), "the first option is kept");
        Check(!hero.PickPending() && hero.NextFightBlock() == nullptr &&
                  Contains(hero.SkillIds(), "riposte") && hero.PickedIds().size() == 1,
              "the kept skill is listed and the fight may start");
        Check(!hero.ResetBuild(why), "a reset is refused once the path is chosen");
        CheckHas(why, "only when a path is offered", "the refusal says when a reset is allowed");
        Check(hero.NoteUse("brace") == 1 && hero.NoteUse("brace") == 1 &&
                  hero.NoteUse("brace") == 2,
              "three uses of brace reach rank 2");
        const XpGrant eighth = hero.GrantXp(206 - 126);
        Check(hero.Level() == 8 && eighth.PickOpened && hero.PickPending(),
              "level 8 opens the second pick");
        const std::vector<std::string> second = hero.PickOptions();
        Check(second.size() == 2 && second[1] == "brace", "the second option upgrades brace");
        Check(hero.Pick(second[1], why) && hero.PickedIds().size() == 2 &&
                  hero.PickedIds()[0] == "riposte" && hero.PickedIds()[1] == "brace",
              "both kept ids stay in pick order");
        Check(hero.ApplyData(why) && hero.PickedIds().size() == 2, "ApplyData keeps the picks");
        const std::vector<std::string> skills = hero.SkillIds();
        Check(CountId(skills, "brace") == 1 && CountId(skills, "riposte") == 1 &&
                  !Contains(skills, "rally") && hero.RankOf("brace") == 2 &&
                  hero.UsesOf("brace") == 3,
              "an upgrade shares the rank and is not listed twice");

        Battle battle(data, std::make_unique<PlayerFirst>());
        battle.SetPickedSkills(hero.PickedIds());
        Check(battle.Start("guardian", 8, "wolf", why), "the upgraded brace fight starts");
        Check(battle.UseSkill("brace").Accepted && battle.Player().GuardPercent == 75,
              "the kept tune raises brace to 75 percent");

        ReplaceInFile(copy.Path / "classes.json", "\"vitality\": 18", "\"vitality\": 19");
        Reload(data);
        Check(hero.ApplyData(why) && hero.PickedIds().size() == 2 &&
                  Contains(hero.PickedIds(), "riposte"),
              "a reload that still offers the options keeps the picks");
        ReplaceInFile(copy.Path / "classes.json", "\"options\": [\"riposte\", \"cleave\"]",
                      "\"options\": [\"bite\", \"cleave\"]");
        Reload(data);
        Check(hero.ApplyData(why) && hero.PickedIds().size() == 1 &&
                  hero.PickedIds()[0] == "brace" && hero.PickPending(),
              "dropping a kept option reopens that pick");
        const std::vector<std::string> reopened = hero.PickOptions();
        Check(reopened.size() == 2 && reopened[0] == "bite" && reopened[1] == "cleave",
              "the reopened pick lists the options still in the file");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "classes.json",
                      "\"skills\": [\"strike\", \"brace\", \"heavy_blow\", \"second_wind\"],\n"
                      "      \"passives\": []",
                      "\"skills\": [\"strike\", \"brace\", \"heavy_blow\", \"second_wind\"],\n"
                      "      \"passives\": [],\n"
                      "      \"picks\": [\n"
                      "        { \"level\": 3, \"options\": [\"cleave\", \"riposte\"] }\n"
                      "      ]");
        Database data(copy.Path);
        DataError error;
        Check(data.Load(&error), "a fighter pick at level 3 loads");
        Hero hero = StartHero(data);
        const XpGrant third = hero.GrantXp(26);
        Check(hero.Level() == 3 && third.PickOpened && hero.PickPending(),
              "level 3 opens the fighter pick");
        std::string why;
        const std::string kept = hero.PickOptions().empty() ? std::string() : hero.PickOptions()[0];
        Check(kept == "cleave" && hero.Pick(kept, why), "cleave is kept");
        const XpGrant fifth = hero.GrantXp(62 - 26);
        Check(hero.Level() == 5 && fifth.ChoiceOpened && !fifth.PickOpened &&
                  hero.ChoicePending() && hero.PickedIds().size() == 1,
              "level 5 opens the path with the pick still kept");
        Check(hero.ResetBuild(why) && why.empty() && hero.PickedIds().empty() &&
                  hero.ChoosingStarter(),
              "a reset clears picks");
        Check(!hero.PickPending(), "no pick is offered while the starter is still open");
        Check(hero.ChooseStarter("fighter", why) && SpendPool(hero, why),
              "the fighter is chosen and the class pool is spent");
        Check(hero.ChoicePending() && hero.PickPending() &&
                  std::string(hero.NextFightBlock()) == "Choose a path before the next fight.",
              "the path is still the block, and the level-3 pick is open again");
        Check(hero.Choose("guardian", why) && hero.PickPending() && hero.PickedIds().empty(),
              "the parent pick reopens after the new path");
        const std::vector<std::string> options = hero.PickOptions();
        Check(options.size() == 2 && options[0] == "cleave" && options[1] == "riposte",
              "the reopened pick is still cleave then riposte");
        Check(hero.Pick("riposte", why) && CountId(hero.SkillIds(), "riposte") == 1,
              "keeping the parent's riposte does not list the class skill twice");
        Hero fresh = StartHero(data);
        Check(fresh.PickedIds().empty() && !fresh.PickPending(), "Start clears picks");
    }
}

void TestStartUsesFirstStarter()
{
    TempCopy copy;
    ReplaceInFile(copy.Path / "classes.json", "{\n      \"id\": \"fighter\",",
                  "{\n      \"id\": \"ranger\",\n      \"name\": \"Ranger\",\n      "
                  "\"description\": \"A second starter.\",\n      \"starter\": true,\n      "
                  "\"stats\": {\"vitality\": 1, \"strength\": 1, \"defense\": 1, \"agility\": 1, "
                  "\"focus\": 1},\n      \"growth\": {\"vitality\": 1, \"strength\": 1, "
                  "\"defense\": 1, \"agility\": 1, \"focus\": 1},\n      \"skills\": "
                  "[\"strike\"],\n      \"passives\": []\n    },\n    {\n      \"id\": "
                  "\"fighter\",");
    Database data(copy.Path);
    DataError error;
    Check(data.Load(&error), "a roster whose first class is a new starter loads");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    Hero hero;
    std::string why;
    Check(hero.Start(data, why), "start follows the starter flag");
    Check(why.empty() && hero.ClassId() == "ranger",
          "Hero::Start picks the first starter in the file");
}

} // namespace

int main()
{
    TestCurveAndChoice();
    TestStartUsesFirstStarter();
    TestRanksAndEvolution();
    TestRankPower();
    TestRankLandsOnTheBlow();
    TestHotReload();
    TestChoiceBlocksTheNextFight();
    TestResetKeepsPoints();
    TestResetClearsItemPractice();
    TestNextFoeStaysWhileChoosing();
    TestSkillPicks();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
