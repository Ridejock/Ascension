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
    const XpGrant cairn = hero.GrantXp(40);
    Check(cairn.LevelBefore == 3 && cairn.LevelAfter == 5 && cairn.ChoiceOpened && hero.Xp() == 66,
          "the cairn guard skips level 4 and opens the choice");
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
    Check(hero.NoteUse("strike") == 1 && hero.RankOf("strike") == 1,
          "the first use stays at rank 1");
    Check(hero.NoteUse("strike") == 2 && hero.UsesOf("strike") == 2,
          "the second use reaches rank 2");
    Check(hero.NoteUse("missing") == 1 && hero.UsesOf("missing") == 0,
          "an unknown skill is not practiced");

    hero.GrantXp(66);
    std::string why;
    Check(!hero.Choose("knight", why), "an unknown evolution is refused");
    CheckHas(why, "not offered", "the refusal names that it is not offered");
    Check(hero.Choose("berserker", why) && why.empty(), "berserker is the first path");
    Check(!hero.ChoicePending() && hero.ClassId() == "berserker", "the choice closes after a pick");
    Check(!hero.Choose("guardian", why), "a second pick is refused");
    Check(hero.UsesOf("strike") == 2 && hero.RankOf("strike") == 2 && hero.RankOf("rage") == 1,
          "practiced uses stay, and the new skills start at rank 1");
    const std::vector<std::string> skills = hero.SkillIds();
    Check(skills.size() == 6 && Contains(skills, "strike") && Contains(skills, "rage") &&
              Contains(skills, "cleave"),
          "berserker keeps strike and adds rage and cleave");

    Battle rage(data, std::make_unique<AgilityRoundModel>());
    Check(rage.Start(hero.ClassId(), hero.Level(), "wolf", why), "a berserker fight starts");
    Check(rage.Player().Stats.Strength == 22 && rage.Player().MaxHp == 144,
          "bloodlust adds 2 strength and health is vitality times 4");

    Hero guard = StartHero(data);
    guard.GrantXp(60);
    Check(guard.Choose("guardian", why), "guardian is the second path");
    Battle wall(data, std::make_unique<AgilityRoundModel>());
    Check(wall.Start(guard.ClassId(), guard.Level(), "wolf", why), "a guardian fight starts");
    Check(wall.Player().Stats.Defense == 18 && wall.Player().MaxHp == 148,
          "bulwark adds 3 defense and health follows the guardian's vitality");
}

void TestRankPower()
{
    Database data(kSample);
    Check(data.Load(), "sample data loads");
    Hero hero = StartHero(data);
    hero.NoteUse("strike");
    hero.NoteUse("strike");

    Battle battle(data, std::make_unique<AgilityRoundModel>());
    std::string error;
    Check(battle.Start("fighter", 1, "wolf", error), "the rank fight starts");
    battle.SetPlayerRanks(data.GetProgress().PowerPerRank, RanksOf(hero));
    Check(battle.EnemyAct().Accepted, "the wolf still acts first");
    const ActionResult ranked = battle.UseSkill("strike");
    Check(ranked.Damage == 17 && ranked.SkillId == "strike", "rank 2 adds one power to strike");

    Battle harder(data, std::make_unique<AgilityRoundModel>());
    Check(harder.Start("fighter", 1, "wolf", error), "the rank-3 fight starts");
    SkillRank row;
    row.Id = "strike";
    row.Rank = 3;
    harder.SetPlayerRanks(1, {row});
    harder.EnemyAct();
    Check(harder.UseSkill("strike").Damage == 18, "rank 3 adds two power to strike");

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

void TestHotReload()
{
    TempCopy copy;
    Database data(copy.Path);
    Check(data.Load(), "copied sample loads");
    Hero hero = StartHero(data);
    hero.GrantXp(8);
    hero.NoteUse("strike");
    hero.NoteUse("strike");
    Check(hero.Level() == 2 && hero.RankOf("strike") == 2, "8 XP is level 2 at rank 2");

    ReplaceInFile(copy.Path / "progress.json", "0, 8, 20", "0, 12, 20");
    Reload(data);
    std::string why;
    Check(hero.ApplyData(why) && hero.Xp() == 8 && hero.Level() == 1,
          "a steeper curve drops the level and keeps the XP");

    ReplaceInFile(copy.Path / "progress.json", "\"usesPerRank\": 2", "\"usesPerRank\": 1");
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

} // namespace

int main()
{
    TestCurveAndChoice();
    TestRanksAndEvolution();
    TestRankPower();
    TestHotReload();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
