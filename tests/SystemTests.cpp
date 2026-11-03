// The System bus: deed counters, a title threshold, class-rarity free points, and the
// guaranteed track beside a meld. No window or GPU.

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Ascension/Progress/Hero.h"
#include "Ascension/Progress/RunSave.h"
#include "Check.h"
#include "Emerald/Save/Save.h"

namespace {

using namespace Ascension;

namespace fs = std::filesystem;

const fs::path kSample = ASCENSION_DATA_DIR;

struct TempDir {
    fs::path Path;

    explicit TempDir(const char* name)
    {
        Path = fs::temp_directory_path() / name;
        std::error_code error;
        fs::remove_all(Path, error);
        fs::create_directories(Path);
    }

    ~TempDir()
    {
        std::error_code error;
        fs::remove_all(Path, error);
    }
};

Database Sample()
{
    Database data(kSample);
    DataError error;
    Check(data.Load(&error), "sample data loads");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    return data;
}

Hero StartHero(const Database& data)
{
    Hero hero;
    std::string why;
    Check(hero.Start(data, why), "a hero starts");
    if (!why.empty())
        std::printf("    %s\n", why.c_str());
    return hero;
}

void TestBusDispatch()
{
    Database data = Sample();
    if (!data.IsLoaded())
        return;
    SystemState state;
    SystemEvent goblins;
    goblins.Kind = SystemEventKind::Kill;
    goblins.Id = "goblin";
    goblins.Tags = {"goblin"};
    goblins.Amount = 99;
    DispatchSystem(state, data, "fighter", 1, goblins);
    Check(SystemCount(state, SystemEventKind::Kill, "goblin", "") == 99, "a kill id counts");
    Check(SystemCount(state, SystemEventKind::Kill, "", "goblin") == 99, "a kill tag counts");
    Check(SystemTagCount(state, "goblin") == 0, "a kill tag does not feed the shared tag");
    Check(state.TitleOffers.empty(), "99 goblins are short of the title");

    SystemEvent heal;
    heal.Kind = SystemEventKind::SkillUse;
    heal.Id = "second_wind";
    heal.Tags = {"heal"};
    heal.Amount = 2;
    DispatchSystem(state, data, "fighter", 1, heal);
    Check(SystemCount(state, SystemEventKind::SkillUse, "second_wind", "") == 2,
          "a skill use counts on its id");
    Check(SystemTagCount(state, "heal") == 2, "a skill tag feeds the shared tag");
    Check(SystemCount(state, SystemEventKind::Kill, "goblin", "") == 99,
          "a skill use does not move the kill count");

    SystemEvent draught;
    draught.Kind = SystemEventKind::ItemUse;
    draught.Id = "minor_potion";
    draught.Tags = {"heal"};
    DispatchSystem(state, data, "fighter", 1, draught);
    Check(SystemTagCount(state, "heal") == 3, "an item with the tag adds to the same total");
    Check(SystemCount(state, SystemEventKind::ItemUse, "minor_potion", "") == 1,
          "the item id has its own count");

    SystemEvent level;
    level.Kind = SystemEventKind::LevelUp;
    level.Amount = 4;
    DispatchSystem(state, data, "fighter", 4, level);
    level.Amount = 2;
    DispatchSystem(state, data, "fighter", 2, level);
    Check(SystemCount(state, SystemEventKind::LevelUp, "", "") == 4,
          "a level event keeps the highest level, it does not add");
}

void TestTitleThreshold()
{
    Database data = Sample();
    if (!data.IsLoaded())
        return;
    SystemState state;
    SystemEvent goblins;
    goblins.Kind = SystemEventKind::Kill;
    goblins.Id = "goblin";
    goblins.Tags = {"goblin"};
    goblins.Amount = 99;
    DispatchSystem(state, data, "fighter", 1, goblins);
    Check(state.TitleOffers.empty(), "the goblin title stays closed at 99");
    goblins.Amount = 1;
    DispatchSystem(state, data, "fighter", 1, goblins);
    Check(state.TitleOffers.size() == 1 && state.TitleOffers[0] == "goblin_slayer",
          "the hundredth goblin offers Goblin Slayer");
    DispatchSystem(state, data, "fighter", 1, goblins);
    Check(state.TitleOffers.size() == 1, "the offer is not listed twice");

    std::string why;
    Check(AcceptTitle(state, data, "goblin_slayer", why) && state.Titles.size() == 1 &&
              state.TitleOffers.empty(),
          "accepting the title takes the offer");
    const std::vector<TagDamage> damage = TitleTagDamage(data, state);
    Check(damage.size() == 1 && damage[0].Tag == "humanoid" && damage[0].Percent == 10,
          "Goblin Slayer is 10 percent against humanoids");
    Check(!AcceptTitle(state, data, "goblin_slayer", why), "the title is not offered again");

    Hero hero = StartHero(data);
    if (!hero.IsReady())
        return;
    const i32 before = hero.Attributes().Strength;
    hero.NoteKill("wolf");
    Check(hero.TitlePending() && hero.TitleOffers().size() == 1 &&
              hero.TitleOffers()[0] == "wolf_cull",
          "one wolf kill offers Wolf Cull");
    Check(hero.AcceptTitle("wolf_cull", why), "Wolf Cull can be accepted");
    Check(hero.Attributes().Strength == before + 1, "Wolf Cull adds one strength");
}

void TestFreePoints()
{
    Database data = Sample();
    if (!data.IsLoaded())
        return;
    Check(RarityFreePoints(data, "common") == 1 && RarityFreePoints(data, "uncommon") == 2 &&
              RarityFreePoints(data, "rare") == 3 && RarityFreePoints(data, "") == 0 &&
              RarityFreePoints(data, "missing") == 0,
          "free points are the rarity row, and a missing rarity is zero");

    ClassDef common;
    common.Rarity = "common";
    Check(FreePointsEarned(data, common, 1) == 0, "level 1 has no free points");
    Check(FreePointsEarned(data, common, 5) == 4, "four steps of a common class are 4");
    Check(FreePointsUnspent(data, common, 5, 1, {}) == 4, "an unsettled floor keeps them unspent");
    Check(FreePointsUnspent(data, common, 5, 5, {}) == 0, "a floor at the current level grants none");
    StatBlock spent;
    spent.Strength = 1;
    Check(FreePointsUnspent(data, common, 6, 5, spent) == 0,
          "the one new point is gone once it is spent");

    ClassDef evolved;
    evolved.EvolvesFrom = "fighter";
    evolved.EvolveLevel = 5;
    evolved.Rarity = "rare";
    Check(FreePointsEarned(data, evolved, 7) == 6,
          "steps before the evolve level use the parent, and later steps use rare");

    Hero hero = StartHero(data);
    if (!hero.IsReady())
        return;
    hero.GrantXp(60);
    Check(hero.Level() == 5 && hero.FreePool() == 0,
          "the fixture classes name no rarity, so the pool stays empty");
}

void TestEvolutionOffers()
{
    Database data = Sample();
    if (!data.IsLoaded())
        return;
    const ClassDef* berserker = data.FindClass("berserker");
    const ClassDef* oathbound = data.FindClass("oathbound");
    Check(berserker != nullptr && berserker->Offer == EvolutionOffer::Guaranteed &&
              oathbound != nullptr && oathbound->Offer == EvolutionOffer::Meld &&
              oathbound->Meld.On == DeedOn::Tag && oathbound->Meld.Tag == "heal" &&
              oathbound->Meld.AtLeast == 8,
          "berserker is the guaranteed track and oathbound is the heal meld");

    Hero hero = StartHero(data);
    if (!hero.IsReady())
        return;
    hero.GrantXp(60);
    const std::vector<std::string> plain = hero.ChoiceIds();
    Check(hero.Level() == 5 && plain.size() == 2 && plain[0] == "berserker" &&
              plain[1] == "guardian",
          "with no healing, only the guaranteed paths are offered");
    for (i32 n = 0; n < 4; ++n)
        hero.NoteUse("second_wind");
    Check(hero.SystemSkillPending() && hero.SystemSkillOffers().size() == 1 &&
              hero.SystemSkillOffers()[0] == "field_mend",
          "four heals offer the fighter's system skill");
    Check(hero.ChoiceIds().size() == 2, "four heals are short of the class meld");
    for (i32 n = 0; n < 4; ++n)
        hero.NoteUse("second_wind");
    const std::vector<std::string> melded = hero.ChoiceIds();
    Check(melded.size() == 3 && melded[0] == "berserker" && melded[2] == "oathbound",
          "eight heals add Oathbound and keep Berserker");
    std::string why;
    Check(hero.AcceptSystemSkill("field_mend", why), "the system skill can be taken");
    const std::vector<std::string> granted = hero.GrantedSkillIds();
    Check(granted.size() == 1 && granted[0] == "shield_wall", "the offer grants shield wall");
    Check(hero.Choose("berserker", why) && hero.ClassId() == "berserker",
          "the guaranteed track can still be taken after the meld opened");
}

void TestTagDamage()
{
    Database data = Sample();
    if (!data.IsLoaded())
        return;
    auto hitWolf = [&](bool bonus, ActionResult& out) {
        Battle battle(data, std::make_unique<AgilityRoundModel>());
        StatBlock stats;
        stats.Strength = 20;
        stats.Stamina = 30;
        stats.Agility = 40;
        battle.SetBaseStats(stats);
        if (bonus) {
            TagDamage row;
            row.Tag = "beast";
            row.Percent = 100;
            battle.SetTagDamage({row});
        }
        std::string why;
        if (!battle.Start("fighter", 1, "wolf", why)) {
            Check(false, "the wolf fight starts");
            std::printf("    %s\n", why.c_str());
            return;
        }
        out = battle.UseSkill("strike");
    };
    ActionResult plain;
    ActionResult marked;
    hitWolf(false, plain);
    hitWolf(true, marked);
    Check(plain.Accepted && marked.Accepted && marked.Damage > plain.Damage,
          "a title percent against the foe's tag raises the blow");
}

void TestSaveLedger()
{
    Database data = Sample();
    if (!data.IsLoaded())
        return;
    Hero hero = StartHero(data);
    if (!hero.IsReady())
        return;
    hero.NoteKill("wolf");
    std::string why;
    Check(hero.AcceptTitle("wolf_cull", why), "the title is accepted before the save");
    hero.GrantXp(8);
    TempDir dir("ascension-system-save");
    RunSave saves(dir.Path);
    RunState run;
    hero.ExportCareer(run.Career);
    Check(saves.Write(run, why), "the ledger writes");
    const RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.FileVersion == RunSave::kVersion, "the ledger reads");
    Hero again = StartHero(data);
    Check(again.ImportCareer(loaded.Run.Career, why), "the ledger imports");
    Check(again.System().Titles.size() == 1 && again.System().Titles[0] == "wolf_cull" &&
              !again.TitlePending() &&
              SystemCount(again.System(), SystemEventKind::Kill, "wolf", "") == 1,
          "the accepted title and the wolf count survive");

    TempDir legacy("ascension-system-v6");
    Emerald::SaveData older;
    older.SetString("class", "fighter");
    older.SetInt("xp", 60);
    Emerald::SaveSystem version6(legacy.Path, 6);
    Check(version6.Save("run", older, "v6"), "a version 6 file is written");
    RunSave migrated(legacy.Path);
    const RunLoad old = migrated.Read();
    Check(static_cast<bool>(old) && old.FileVersion == 6 && old.Run.Career.FreeLegacy &&
              old.Run.Career.System.Counters.empty() && old.Run.Career.System.Titles.empty(),
          "version 6 migrates with an empty ledger and the legacy mark");
    Hero fromOld = StartHero(data);
    Check(fromOld.ImportCareer(old.Run.Career, why), "the version 6 career imports");
    Check(fromOld.Level() == 5 && fromOld.FreeFloor() == 5 && fromOld.FreePool() == 0 &&
              !fromOld.TitlePending(),
          "a migrated save settles free points at the level it already had");
}

} // namespace

int main()
{
    TestBusDispatch();
    TestTitleThreshold();
    TestFreePoints();
    TestEvolutionOffers();
    TestTagDamage();
    TestSaveLedger();
    if (g_Failures == 0)
        std::printf("All system checks passed.\n");
    else
        std::printf("%d system check(s) failed.\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
