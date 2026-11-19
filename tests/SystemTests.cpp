// The System bus: deed counters, title thresholds, class-rarity free points, the guaranteed
// track beside the heal and crush melds, and a ledger that recovers a new deed on load.
// No window or GPU.

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Ascension/Progress/Hero.h"
#include "Ascension/Progress/RunSave.h"
#include "Ascension/Progress/TitleSheet.h"
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

Database LoadDir(const fs::path& dir, const char* what)
{
    Database data(dir);
    DataError error;
    Check(data.Load(&error), what);
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    return data;
}

Database Sample()
{
    return LoadDir(kSample, "sample data loads");
}

Database Shipped()
{
    return LoadDir(ASCENSION_SHIPPED_DATA_DIR, "shipped data loads");
}

bool SkillTagged(const Database& data, std::string_view skillId, std::string_view tag)
{
    const SkillDef* skill = data.FindSkill(skillId);
    if (skill == nullptr)
        return false;
    for (const std::string& have : skill->Tags) {
        if (have == tag)
            return true;
    }
    return false;
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
              state.TitleOffers.empty() && state.Equipped.empty(),
          "accepting the title takes the offer and does not wear it");
    Check(TitleTagDamage(data, state).empty(), "an owned title does not change damage");
    state.Equipped.push_back("goblin_slayer");
    const std::vector<TagDamage> damage = TitleTagDamage(data, state);
    Check(damage.size() == 1 && damage[0].Tag == "humanoid" && damage[0].Percent == 10,
          "an equipped Goblin Slayer is 10 percent against humanoids");
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
    Check(hero.Attributes().Strength == before && !hero.TitleEquipped("wolf_cull"),
          "owning Wolf Cull does not add strength");
    Check(!hero.EquipTitle("wolf_cull", why), "a class with no rarity has no title slot");
    Check(hero.QueryTitles().size() == data.GetTitles().size(),
          "the query lists every catalog title");
    TitleFact wolf;
    Check(hero.TitleFactFor("wolf_cull", wolf) && wolf.Owned && !wolf.Equipped && wolf.Slot < 0 &&
              wolf.Name == "Wolf Cull" && !wolf.Bonus.empty() &&
              wolf.Bonus[0].Kind == ModKind::StatFlat && wolf.Bonus[0].Stat == "strength" &&
              wolf.Bonus[0].Amount == 1 &&
              !wolf.Description.empty(),
          "the query names an owned title and its bonus");
    Check(!hero.TitleFactFor("no_such_title", wolf), "an unknown title is not a fact");
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
    const RarityDef* commonRarity = data.FindRarity("common");
    const RarityDef* uncommonRarity = data.FindRarity("uncommon");
    const RarityDef* rareRarity = data.FindRarity("rare");
    const RarityDef* epicRarity = data.FindRarity("epic");
    const RarityDef* legendaryRarity = data.FindRarity("legendary");
    const RarityDef* mythicRarity = data.FindRarity("mythic");
    Check(commonRarity != nullptr && uncommonRarity != nullptr && rareRarity != nullptr &&
              epicRarity != nullptr && legendaryRarity != nullptr && mythicRarity != nullptr &&
              commonRarity->TitleSlots == 1 && uncommonRarity->TitleSlots == 2 &&
              rareRarity->TitleSlots == 3 && epicRarity->TitleSlots == 3 &&
              legendaryRarity->TitleSlots == 4 && mythicRarity->TitleSlots == 4,
          "title slots are the rarity row");
    Check(TitleSlotCount(data, "fighter") == 0 && TitleSlotCount(data, "missing") == 0,
          "a class with no rarity, and a missing class, wear no titles");

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
              again.System().Equipped.empty() && !loaded.Run.Career.EquippedLegacy &&
              !again.TitlePending() &&
              SystemCount(again.System(), SystemEventKind::Kill, "wolf", "") == 1,
          "the owned title, the empty worn list, and the wolf count survive");

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

void TestCrushSkill()
{
    Database data = Sample();
    if (!data.IsLoaded())
        return;
    Check(SkillTagged(data, "heavy_blow", "crush") && SkillTagged(data, "cleave", "crush") &&
              !SkillTagged(data, "strike", "crush") && !SkillTagged(data, "crushing_slam", "crush"),
          "heavy blow and cleave are crush skills, and strike and the foe's slam are not");

    Hero hero = StartHero(data);
    if (!hero.IsReady())
        return;
    for (i32 n = 0; n < 8; ++n)
        hero.NoteUse("strike");
    Check(SystemCount(hero.System(), SystemEventKind::SkillUse, "", "crush") == 0 &&
              hero.SystemSkillOffers().empty(),
          "strike does not feed the crush skill deed");
    for (i32 n = 0; n < 3; ++n)
        hero.NoteUse("heavy_blow");
    Check(hero.SystemSkillOffers().empty(), "three heavy blows are short of the skill offer");
    hero.NoteUse("heavy_blow");
    Check(hero.SystemSkillOffers().size() == 1 && hero.SystemSkillOffers()[0] == "wide_swing",
          "the fourth heavy blow offers Cleave");
    Check(SystemCount(hero.System(), SystemEventKind::SkillUse, "", "crush") == 4 &&
              SystemTagCount(hero.System(), "crush") == 4,
          "the skill tag and the shared tag both recorded the swings");

    SystemState items;
    SystemEvent drink;
    drink.Kind = SystemEventKind::ItemUse;
    drink.Id = "minor_potion";
    drink.Tags = {"crush"};
    drink.Amount = 8;
    DispatchSystem(items, data, "fighter", 1, drink);
    Check(SystemCount(items, SystemEventKind::SkillUse, "", "crush") == 0 &&
              SystemTagCount(items, "crush") == 8 && items.SkillOffers.empty(),
          "an item with the tag does not open the skill deed");

    std::string why;
    Check(hero.AcceptSystemSkill("wide_swing", why), "wide swing can be taken");
    Check(hero.GrantedSkillIds().size() == 1 && hero.GrantedSkillIds()[0] == "cleave",
          "the offer grants cleave");
    hero.NoteUse("cleave");
    Check(SystemCount(hero.System(), SystemEventKind::SkillUse, "", "crush") == 5,
          "cleave keeps feeding the crush count");

    Hero beasts = StartHero(data);
    beasts.NoteKill("wolf");
    Check(beasts.TitleOffers().size() == 1 && beasts.TitleOffers()[0] == "wolf_cull",
          "one wolf is Wolf Cull and not Beast Hunter");
    for (i32 n = 0; n < 3; ++n)
        beasts.NoteKill("wolf");
    Check(beasts.TitleOffers().size() == 2 && beasts.TitleOffers()[0] == "wolf_cull" &&
              beasts.TitleOffers()[1] == "beast_hunter",
          "the fourth beast opens Beast Hunter on a later kill");
}

void TestDeedRecovery()
{
    Database data = Sample();
    if (!data.IsLoaded())
        return;
    Check(RunSave::kVersion == 8, "the system log is save version 8");
    Hero host = StartHero(data);
    if (!host.IsReady())
        return;
    host.GrantXp(60);
    std::string why;
    CareerRecord career;
    host.ExportCareer(career);
    career.System = {};
    SystemCounter crush;
    crush.Key = "skill.tag:crush";
    crush.Count = 4;
    career.System.Counters.push_back(crush);
    SystemCounter beasts;
    beasts.Key = "kill.tag:beast";
    beasts.Count = 4;
    career.System.Counters.push_back(beasts);
    career.System.FreeFloor = 1;

    TempDir dir("ascension-system-deeds");
    RunSave saves(dir.Path);
    RunState run;
    run.Career = career;
    Check(saves.Write(run, why), "a ledger of counts writes");
    const RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.FileVersion == RunSave::kVersion &&
              loaded.Run.Career.System.SkillOffers.empty() &&
              loaded.Run.Career.System.TitleOffers.empty(),
          "the file keeps the counts and does not invent offers");
    Hero again = StartHero(data);
    Check(again.ImportCareer(loaded.Run.Career, why), "the counted ledger imports");
    Check(again.Level() == 5 && again.SystemSkillOffers().size() == 1 &&
              again.SystemSkillOffers()[0] == "wide_swing",
          "a stored crush count offers the new skill on load");
    Check(again.TitleOffers().size() == 1 && again.TitleOffers()[0] == "beast_hunter",
          "a stored beast count offers Beast Hunter and not Wolf Cull");
}

void TestReaver()
{
    Database data = Shipped();
    if (!data.IsLoaded())
        return;
    const ClassDef* fighter = data.FindClass("fighter");
    const ClassDef* berserker = data.FindClass("berserker");
    const ClassDef* oathbound = data.FindClass("oathbound");
    const ClassDef* reaver = data.FindClass("reaver");
    Check(fighter != nullptr && berserker != nullptr && oathbound != nullptr && reaver != nullptr,
          "the fighter line has berserker, oathbound, and reaver");
    if (fighter == nullptr || berserker == nullptr || oathbound == nullptr || reaver == nullptr)
        return;
    Check(fighter->SystemSkills.size() == 2 && fighter->SystemSkills[0].Id == "field_mend" &&
              fighter->SystemSkills[1].Id == "wide_swing" &&
              fighter->SystemSkills[1].Skill == "cleave" &&
              fighter->SystemSkills[1].Deed.On == DeedOn::Skill &&
              fighter->SystemSkills[1].Deed.Tag == "crush" &&
              fighter->SystemSkills[1].Deed.AtLeast == 4,
          "wide swing is a crush skill deed beside the heal deed");
    Check(reaver->Offer == EvolutionOffer::Meld && reaver->Meld.On == DeedOn::Skill &&
              reaver->Meld.Tag == "crush" && reaver->Meld.AtLeast == 8 && reaver->Rarity == "rare" &&
              oathbound->Meld.On == DeedOn::Tag && oathbound->Meld.Tag == "heal" &&
              oathbound->Meld.AtLeast == 8 && berserker->Offer == EvolutionOffer::Guaranteed,
          "reaver counts crush skill uses, and oathbound still counts the shared heal tag");
    Check(reaver->Skills.size() == 1 && reaver->Skills[0].Id == "cleave" &&
              reaver->Passives.size() == 1 && reaver->Passives[0].Id == "heft" &&
              berserker->Passives.size() == 1 && berserker->Passives[0].Id == "bloodlust" &&
              oathbound->Passives.size() == 1 && oathbound->Passives[0].Id == "bulwark",
          "reaver grants cleave and heft, not bloodlust or bulwark");
    const SkillDef* heft = data.FindSkill("heft");
    const EffectDef* weight = heft != nullptr ? heft->FindEffect("weight") : nullptr;
    Check(heft != nullptr && heft->Kind == SkillKind::Passive && weight != nullptr &&
              weight->Op == EffectOp::Stat && weight->Stat == "strength" && weight->Amount == 2,
          "heft is a flat +2 strength");
    Check(SkillTagged(data, "heavy_blow", "crush") && SkillTagged(data, "cleave", "crush") &&
              !SkillTagged(data, "strike", "crush") && !SkillTagged(data, "rend", "crush") &&
              !SkillTagged(data, "crushing_slam", "crush") &&
              !SkillTagged(data, "heavy_swipe", "crush"),
          "rend and the foes' heavy swings stay untagged");
    const TitleDef* goblin = data.FindTitle("goblin_slayer");
    Check(goblin != nullptr && goblin->Deed.On == DeedOn::Kill && goblin->Deed.Tag == "goblin" &&
              goblin->Deed.AtLeast == 100,
          "goblin slayer stays at one hundred");

    Hero plain = StartHero(data);
    if (!plain.IsReady())
        return;
    plain.GrantXp(60);
    Check(plain.Level() == 5, "sixty experience is level 5");
    const std::vector<std::string> closed = plain.ChoiceIds();
    Check(closed.size() == 2 && closed[0] == "berserker" && closed[1] == "guardian",
          "with no crush swings, reaver stays closed and the guaranteed paths remain");
    for (i32 n = 0; n < 8; ++n)
        plain.NoteUse("strike");
    Check(plain.ChoiceIds().size() == 2 && plain.SystemSkillOffers().empty(),
          "eight strikes do not open the crush path");

    Hero crushed = StartHero(data);
    crushed.GrantXp(60);
    for (i32 n = 0; n < 4; ++n)
        crushed.NoteUse("heavy_blow");
    Check(crushed.SystemSkillOffers().size() == 1 && crushed.SystemSkillOffers()[0] == "wide_swing" &&
              crushed.ChoiceIds().size() == 2,
          "four heavy blows offer cleave and are short of the class meld");
    for (i32 n = 0; n < 4; ++n)
        crushed.NoteUse("heavy_blow");
    const std::vector<std::string> opened = crushed.ChoiceIds();
    Check(opened.size() == 3 && opened[0] == "berserker" && opened[1] == "guardian" &&
              opened[2] == "reaver",
          "eight crush swings add reaver and leave oathbound closed");
    std::string why;
    Check(crushed.AcceptSystemSkill("wide_swing", why) && crushed.GrantedSkillIds().size() == 1 &&
              crushed.GrantedSkillIds()[0] == "cleave",
          "wide swing grants cleave");
    Check(crushed.Choose("reaver", why) && crushed.ClassId() == "reaver" &&
              crushed.ChoiceIds().empty() && crushed.SystemSkillOffers().empty(),
          "taking reaver closes the path list, and cleave is already on the class");

    Hero escape = StartHero(data);
    escape.GrantXp(60);
    for (i32 n = 0; n < 8; ++n)
        escape.NoteUse("heavy_blow");
    Check(escape.Choose("berserker", why) && escape.ClassId() == "berserker",
          "berserker can still be taken after reaver opened");

    Hero both = StartHero(data);
    both.GrantXp(60);
    for (i32 n = 0; n < 8; ++n) {
        both.NoteUse("second_wind");
        both.NoteUse("heavy_blow");
    }
    const std::vector<std::string> paths = both.ChoiceIds();
    Check(paths.size() == 4 && paths[0] == "berserker" && paths[1] == "guardian" &&
              paths[2] == "oathbound" && paths[3] == "reaver",
          "both melds append after the guaranteed paths, and oathbound stays ahead of reaver");

    SystemState drinks;
    SystemEvent potion;
    potion.Kind = SystemEventKind::ItemUse;
    potion.Id = "minor_potion";
    potion.Tags = {"crush"};
    potion.Amount = 8;
    DispatchSystem(drinks, data, "fighter", 5, potion);
    Check(!EvolutionOffered(drinks, *reaver, 5) &&
              SystemCount(drinks, SystemEventKind::SkillUse, "", "crush") == 0,
          "eight crush potions do not open reaver");

    Battle fight(data, std::make_unique<AgilityRoundModel>());
    Check(fight.Start("reaver", 5, "wolf", why), "a reaver fight starts");
    const StatBlock sheet = data.StatsAt(*reaver, 5);
    Check(fight.Player().Stats.Strength == sheet.Strength + 2 &&
              fight.Player().Stats.Defense == sheet.Defense,
          "heft adds two strength, and bulwark does not");
    bool cleave = false;
    bool rage = false;
    for (const std::string& id : fight.PlayerSkillIds()) {
        if (id == "cleave")
            cleave = true;
        if (id == "rage")
            rage = true;
    }
    Check(cleave && !rage, "reaver swings cleave and does not gain rage");

    Hero wolf = StartHero(data);
    wolf.NoteKill("wolf");
    Check(wolf.TitleOffers().size() == 1 && wolf.TitleOffers()[0] == "wolf_cull",
          "the first wolf is only Wolf Cull");
    wolf.NoteKill("wolf");
    wolf.NoteKill("wolf");
    wolf.NoteKill("dire_wolf");
    Check(wolf.TitleOffers().size() == 2 && wolf.TitleOffers()[0] == "wolf_cull" &&
              wolf.TitleOffers()[1] == "beast_hunter",
          "four beasts open Beast Hunter on a later kill than Wolf Cull");

    Hero bear = StartHero(data);
    const i32 stamina = bear.Attributes().Stamina;
    bear.NoteKill("bear");
    Check(bear.TitleOffers().size() == 1 && bear.TitleOffers()[0] == "bear_cull",
          "the bear opens Bear Cull and not Beast Hunter");
    Check(bear.AcceptTitle("bear_cull", why) && bear.Attributes().Stamina == stamina,
          "Bear Cull is owned and not yet worn");
    Check(bear.TitleSlots() >= 1 && bear.EquipTitle("bear_cull", why) &&
              bear.Attributes().Stamina == stamina + 1,
          "equipping Bear Cull adds one stamina");

    Hero barrow = StartHero(data);
    barrow.NoteKill("tomb_moth");
    Check(barrow.TitleOffers().empty(), "the tomb moth is not a humanoid");
    barrow.NoteKill("bandit");
    barrow.NoteKill("bone_archer");
    Check(barrow.TitleOffers().empty(), "two humanoids are short of Tombwalker");
    barrow.NoteKill("barrow_sentry");
    Check(barrow.TitleOffers().size() == 1 && barrow.TitleOffers()[0] == "tombwalker",
          "the sentry is the third humanoid and not Varn");
    const i32 defense = barrow.Attributes().Defense;
    Check(barrow.AcceptTitle("tombwalker", why) && barrow.Attributes().Defense == defense,
          "Tombwalker is owned and not yet worn");
    Check(barrow.EquipTitle("tombwalker", why) && barrow.Attributes().Defense == defense + 1,
          "equipping Tombwalker adds one defense");
    barrow.NoteKill("oathkeeper_varn");
    Check(barrow.TitleOffers().size() == 1 && barrow.TitleOffers()[0] == "oaths_end",
          "Varn opens Oath's End on its own kill");
    Check(barrow.AcceptTitle("oaths_end", why), "Oath's End can be accepted");
    if (static_cast<i32>(barrow.EquippedTitles().size()) >= barrow.TitleSlots())
        Check(barrow.UnequipTitle("tombwalker", why), "a full bar makes a slot");
    const i32 will = barrow.Attributes().Will;
    Check(barrow.EquipTitle("oaths_end", why) && barrow.Attributes().Will == will + 1,
          "equipping Oath's End adds one will");

    Hero old = StartHero(data);
    old.GrantXp(60);
    CareerRecord career;
    old.ExportCareer(career);
    career.System = {};
    SystemCounter crush;
    crush.Key = "skill.tag:crush";
    crush.Count = 8;
    career.System.Counters.push_back(crush);
    career.System.FreeFloor = old.Level();
    TempDir dir("ascension-system-reaver");
    RunSave saves(dir.Path);
    RunState run;
    run.Career = career;
    Check(saves.Write(run, why), "a crush ledger writes");
    const RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.FileVersion == RunSave::kVersion,
          "a new ledger writes the current save version");
    Hero recovered = StartHero(data);
    Check(recovered.ImportCareer(loaded.Run.Career, why), "the crush ledger imports");
    const std::vector<std::string> recoveredPaths = recovered.ChoiceIds();
    Check(recovered.Level() == 5 && recovered.SystemSkillOffers().size() == 1 &&
              recovered.SystemSkillOffers()[0] == "wide_swing" && recoveredPaths.size() == 3 &&
              recoveredPaths[2] == "reaver",
          "stored crush uses offer cleave and reaver on load");
}

void TestTitleSheet()
{
    TitleSheet sheet;
    sheet.Open = true;
    Check(ApplyTitleSheet(sheet, kMoveEast, 1, 0, 1) == TitleSheetStep::None && sheet.Column == 1 &&
              sheet.Row == 0,
          "east moves from the worn slots to the owned titles");
    Check(ApplyTitleSheet(sheet, kConfirm, 1, 0, 1) == TitleSheetStep::None &&
              sheet.Ask == TitleAsk::Equip,
          "confirm on an owned title arms equip");
    Check(ApplyTitleSheet(sheet, kCancel, 1, 0, 1) == TitleSheetStep::None &&
              sheet.Ask == TitleAsk::None,
          "cancel leaves the ask and stays on the sheet");
    Check(ApplyTitleSheet(sheet, kConfirm, 1, 0, 1) == TitleSheetStep::None &&
              sheet.Ask == TitleAsk::Equip,
          "confirm arms the equip again");
    Check(ApplyTitleSheet(sheet, kConfirm, 1, 0, 1) == TitleSheetStep::Equip &&
              sheet.Ask == TitleAsk::None && sheet.Open,
          "the second confirm equips the marked owned title");
    Check(ApplyTitleSheet(sheet, kMoveWest, 1, 1, 0) == TitleSheetStep::None && sheet.Column == 0,
          "west returns to the worn slots");
    Check(ApplyTitleSheet(sheet, kUnequip, 1, 1, 0) == TitleSheetStep::None &&
              sheet.Ask == TitleAsk::Unequip,
          "unequip arms take-off on a filled slot");
    Check(ApplyTitleSheet(sheet, kConfirm, 1, 1, 0) == TitleSheetStep::Unequip,
          "the second confirm takes the title off");
    sheet.Open = true;
    sheet.Column = 0;
    sheet.Row = 1;
    Check(ApplyTitleSheet(sheet, kConfirm, 2, 1, 0) == TitleSheetStep::None &&
              sheet.Ask == TitleAsk::None,
          "an empty slot does not arm take-off");
    sheet.Open = true;
    sheet.Column = 1;
    sheet.Row = 0;
    sheet.Ask = TitleAsk::None;
    Check(ApplyTitleSheet(sheet, kConfirm, 1, 1, 1) == TitleSheetStep::None &&
              sheet.Ask == TitleAsk::None,
          "a full bar does not arm equip");
    Check(ApplyTitleSheet(sheet, kCancel, 2, 1, 0) == TitleSheetStep::Back && !sheet.Open,
          "cancel from the sheet returns to camp");
    sheet.Open = true;
    Check(ApplyTitleSheet(sheet, kMenu, 1, 0, 0) == TitleSheetStep::Close && !sheet.Open,
          "menu closes camp from the sheet");
    Check(TitleLine("Equip {name}?", "Wolf Cull") == "Equip Wolf Cull?",
          "the equip question takes the title name");
}

void TestTitleEquip()
{
    Database data = Shipped();
    if (!data.IsLoaded())
        return;
    const RarityDef* common = data.FindRarity("common");
    const RarityDef* uncommon = data.FindRarity("uncommon");
    Check(common != nullptr && uncommon != nullptr && common->TitleSlots >= 1 &&
              TitleSlotCount(data, "fighter") == common->TitleSlots &&
              TitleSlotCount(data, "berserker") == uncommon->TitleSlots &&
              TitleSlotCount(data, "missing") == 0,
          "a class wears as many titles as its rarity row");
    Check(!data.GetTitleLabels().Open.empty() && !data.GetTitleLabels().EquipAsk.empty() &&
              !data.GetTitleLabels().UnequipAsk.empty(),
          "the title sheet words load from titles.json");

    Hero hero = StartHero(data);
    if (!hero.IsReady())
        return;
    const i32 strength = hero.Attributes().Strength;
    const i32 agility = hero.Attributes().Agility;
    for (i32 n = 0; n < 4; ++n)
        hero.NoteKill("wolf");
    std::string why;
    Check(hero.AcceptTitle("wolf_cull", why) && hero.AcceptTitle("beast_hunter", why),
          "Wolf Cull and Beast Hunter can be owned");
    Check(hero.Attributes().Strength == strength && hero.Attributes().Agility == agility,
          "owned titles do not change the sheet");
    Check(hero.EquipTitle("wolf_cull", why) && hero.TitleEquipped("wolf_cull") &&
              hero.Attributes().Strength == strength + 1,
          "equipping Wolf Cull adds one strength");
    const bool both = hero.EquipTitle("beast_hunter", why);
    if (hero.TitleSlots() >= 2) {
        Check(both && hero.Attributes().Agility == agility + 1,
              "a second slot wears Beast Hunter");
    } else {
        Check(!both && hero.Attributes().Agility == agility && !hero.TitleEquipped("beast_hunter"),
              "a full bar refuses another title");
        Check(hero.UnequipTitle("wolf_cull", why) && hero.Attributes().Strength == strength,
              "taking Wolf Cull off returns the strength");
        Check(hero.EquipTitle("beast_hunter", why) && hero.Attributes().Agility == agility + 1 &&
                  !hero.TitleEquipped("wolf_cull"),
              "the open slot can wear Beast Hunter instead");
        Check(hero.UnequipTitle("beast_hunter", why) && hero.EquipTitle("wolf_cull", why),
              "Wolf Cull goes back on so the later clamp keeps it");
    }
    TitleFact fact;
    Check(hero.TitleFactFor("wolf_cull", fact) && fact.Owned && fact.Equipped && fact.Slot == 0 &&
              fact.Name == "Wolf Cull" && !fact.Bonus.empty() && fact.Bonus[0].Amount == 1,
          "the query reports the worn title, its slot, and its bonus");
    Check(hero.OwnsTitle("beast_hunter"), "Beast Hunter stays owned");

    hero.GrantXp(60);
    Check(hero.Level() == 5 && hero.Choose("berserker", why), "berserker can be chosen");
    const i32 wider = hero.TitleSlots();
    Check(wider == uncommon->TitleSlots, "the new class uses its own rarity's slots");
    if (!hero.TitleEquipped("beast_hunter") &&
        static_cast<i32>(hero.EquippedTitles().size()) < wider)
        Check(hero.EquipTitle("beast_hunter", why), "a new slot can wear Beast Hunter");
    const i32 wornBefore = static_cast<i32>(hero.EquippedTitles().size());
    Check(hero.AdoptStarter("fighter", why), "the build can return to fighter");
    Check(static_cast<i32>(hero.EquippedTitles().size()) <= hero.TitleSlots() &&
              hero.OwnsTitle("wolf_cull") && hero.OwnsTitle("beast_hunter"),
          "a smaller bar takes off the newest titles and keeps them owned");
    if (wider > hero.TitleSlots()) {
        Check(static_cast<i32>(hero.EquippedTitles().size()) < wornBefore &&
                  hero.TitleEquipped("wolf_cull"),
              "the titles worn first stay when the bar shrinks");
    }
    const i32 settled = hero.Attributes().Strength;
    if (hero.TitleEquipped("wolf_cull")) {
        Check(hero.UnequipTitle("wolf_cull", why) && hero.Attributes().Strength == settled - 1,
              "taking Wolf Cull off after a class change removes its strength");
    } else {
        Check(hero.EquipTitle("wolf_cull", why) && hero.Attributes().Strength == settled + 1,
              "equipping Wolf Cull after a class change adds its strength");
    }

    TempDir kept("ascension-title-equip");
    RunSave saves(kept.Path);
    RunState run;
    hero.ExportCareer(run.Career);
    Check(saves.Write(run, why), "the worn titles write");
    const RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && !loaded.Run.Career.EquippedLegacy &&
              loaded.Run.Career.System.Equipped == hero.EquippedTitles(),
          "the worn list round-trips, and a written file is not a legacy equip");
    Hero again = StartHero(data);
    Check(again.ImportCareer(loaded.Run.Career, why) &&
              again.EquippedTitles() == hero.EquippedTitles() &&
              again.Attributes().Strength == hero.Attributes().Strength,
          "a loaded run wears the same titles");

    TempDir legacy("ascension-title-legacy");
    Emerald::SaveData older;
    older.SetString("class", "fighter");
    older.SetInt("title.count", 1);
    older.SetString("title.0", "wolf_cull");
    Emerald::SaveSystem version7(legacy.Path, 7);
    Check(version7.Save("run", older, "v7"), "a version 7 file with no worn list is written");
    RunSave migrated(legacy.Path);
    const RunLoad old = migrated.Read();
    Check(static_cast<bool>(old) && old.FileVersion == 7 && old.Run.Career.EquippedLegacy &&
              old.Run.Career.System.Equipped.empty() && old.Run.Career.System.Titles.size() == 1,
          "a missing worn list is marked and not invented in the file");
    Hero fromOld = StartHero(data);
    const i32 bare = fromOld.Attributes().Strength;
    Check(fromOld.ImportCareer(old.Run.Career, why), "the legacy career imports");
    Check(fromOld.OwnsTitle("wolf_cull") && fromOld.TitleEquipped("wolf_cull") &&
              fromOld.Attributes().Strength == bare + 1,
          "a save from before slots wears the owned title when a slot is open");

    TempDir bareFile("ascension-title-empty");
    Emerald::SaveData emptyWorn;
    emptyWorn.SetString("class", "fighter");
    emptyWorn.SetInt("title.count", 1);
    emptyWorn.SetString("title.0", "wolf_cull");
    emptyWorn.SetInt("titleEquip.count", 0);
    Emerald::SaveSystem explicitNone(bareFile.Path, 7);
    Check(explicitNone.Save("run", emptyWorn, "v7"), "a worn count of zero is written");
    const RunLoad none = RunSave(bareFile.Path).Read();
    Hero unworn = StartHero(data);
    Check(static_cast<bool>(none) && !none.Run.Career.EquippedLegacy &&
              unworn.ImportCareer(none.Run.Career, why) && unworn.OwnsTitle("wolf_cull") &&
              !unworn.TitleEquipped("wolf_cull") && unworn.Attributes().Strength == bare,
          "a stored empty worn list stays empty");
}

bool HasId(const std::vector<std::string>& ids, std::string_view id)
{
    for (const std::string& have : ids) {
        if (have == id)
            return true;
    }
    return false;
}

bool NoticeNames(const std::vector<std::string>& lines, std::string_view name)
{
    for (const std::string& line : lines) {
        if (line.find(name) != std::string::npos)
            return true;
    }
    return false;
}

void TestDeedCompounds()
{
    SystemState state;
    DeedDef crush;
    crush.On = DeedOn::Skill;
    crush.Tag = "crush";
    crush.AtLeast = 2;
    DeedDef worn;
    worn.On = DeedOn::Title;
    worn.Id = "wolf_cull";
    worn.Worn = true;
    DeedDef both;
    both.Op = DeedOp::All;
    both.Parts.push_back(crush);
    both.Parts.push_back(worn);
    Check(!DeedHolds(state, "fighter", 1, both), "an empty ledger meets neither part");

    SystemCounter swings;
    swings.Key = "skill.tag:crush";
    swings.Count = 2;
    state.Counters.push_back(swings);
    Check(!DeedHolds(state, "fighter", 1, both), "the swings are not enough without the title");
    state.Titles.push_back("wolf_cull");
    Check(!DeedHolds(state, "fighter", 1, both), "an owned title is not a worn one");
    state.Equipped.push_back("wolf_cull");
    Check(DeedHolds(state, "fighter", 1, both), "all holds when every part holds");

    DeedDef mage;
    mage.On = DeedOn::Class;
    mage.Id = "mage";
    DeedDef either;
    either.Op = DeedOp::Any;
    either.Parts.push_back(mage);
    either.Parts.push_back(crush);
    Check(DeedHolds(state, "cleric", 1, either), "any holds on the crush part");
    DeedDef denied;
    denied.Op = DeedOp::Not;
    denied.Parts.push_back(mage);
    Check(DeedHolds(state, "fighter", 1, denied), "not flips a class the hero is not");
    Check(!DeedHolds(state, "mage", 1, denied), "not rejects the class the hero is");

    DeedDef level;
    level.On = DeedOn::Level;
    level.AtLeast = 4;
    Check(!DeedHolds(state, "fighter", 9, level), "a level deed reads the ledger, not the argument");
    SystemCounter reached;
    reached.Key = "level";
    reached.Count = 4;
    state.Counters.push_back(reached);
    Check(DeedHolds(state, "fighter", 1, level), "a stored level still meets a level deed");
}

void TestHabitCounts()
{
    Database data = Sample();
    if (!data.IsLoaded())
        return;
    SystemState state;
    HabitNote blocked;
    blocked.Kind = std::string(kHabitBlocked);
    blocked.Id = "brace";
    blocked.Amount = 9;
    blocked.Tags = {"defense", "guard"};
    NoteHabit(state, data, "fighter", 1, blocked);
    NoteHabit(state, data, "fighter", 1, blocked);
    Check(SystemCount(state, SystemEventKind::Mark, "", "blocked") == 2, "each block counts once");
    Check(SystemCount(state, SystemEventKind::Mark, "", "defense") == 2,
          "the guard's tags ride the mark");
    Check(SystemCount(state, SystemEventKind::Mark, "brace", "") == 2, "the skill id is counted");

    HabitNote light;
    light.Kind = std::string(kHabitDealt);
    light.Amount = 4;
    NoteHabit(state, data, "fighter", 1, light);
    Check(SystemCount(state, SystemEventKind::Mark, "", "heavy") == 0, "a small blow is not heavy");
    HabitNote heavy;
    heavy.Kind = std::string(kHabitDealt);
    heavy.Amount = data.GetSystem().HeavyAt;
    NoteHabit(state, data, "fighter", 1, heavy);
    Check(SystemCount(state, SystemEventKind::Mark, "", "heavy") == 1, "a blow at the line is heavy");

    HabitNote gold;
    gold.Kind = std::string(kHabitGold);
    gold.Amount = 40;
    NoteHabit(state, data, "fighter", 1, gold);
    Check(SystemCount(state, SystemEventKind::Mark, "", "gold") == 40, "gold adds its amount");

    HabitNote region;
    region.Kind = std::string(kHabitRegion);
    region.Id = "hub";
    region.Amount = 1;
    NoteHabit(state, data, "fighter", 1, region);
    NoteHabit(state, data, "fighter", 1, region);
    Check(SystemCount(state, SystemEventKind::Mark, "hub", "") == 1, "a region is counted once");
    Check(SystemCount(state, SystemEventKind::Mark, "", "region") == 1,
          "the region tag counts distinct maps");

    HabitNote step;
    step.Kind = std::string(kHabitStep);
    step.Amount = 1;
    NoteHabit(state, data, "fighter", 1, step);
    NoteHabit(state, data, "fighter", 1, step);
    Check(SystemCount(state, SystemEventKind::Mark, "", "step") == 2, "steps add");
}

void TestModifierStack()
{
    std::vector<Modifier> mods;
    Modifier flat;
    flat.Kind = ModKind::StatFlat;
    flat.Stat = "strength";
    flat.Amount = 1;
    mods.push_back(flat);
    flat.Amount = 2;
    mods.push_back(flat);
    Modifier percent;
    percent.Kind = ModKind::StatPercent;
    percent.Stat = "strength";
    percent.Amount = 10;
    mods.push_back(percent);
    Modifier versus;
    versus.Kind = ModKind::DamageTag;
    versus.Stat = "humanoid";
    versus.Amount = 10;
    mods.push_back(versus);
    versus.Amount = 5;
    mods.push_back(versus);
    Modifier xp;
    xp.Kind = ModKind::XpPercent;
    xp.Amount = 10;
    mods.push_back(xp);
    Modifier gold;
    gold.Kind = ModKind::GoldPercent;
    gold.Amount = 5;
    mods.push_back(gold);
    Modifier drop;
    drop.Kind = ModKind::DropPercent;
    drop.Amount = 5;
    mods.push_back(drop);

    const ModifierStack stack = StackModifiers(mods);
    Check(stack.Flat.Strength == 3 && stack.Percent.Strength == 10, "flats and percents add");
    Check(stack.Damage.size() == 1 && stack.Damage[0].Tag == "humanoid" && stack.Damage[0].Percent == 15,
          "damage against one tag is one row");
    Check(stack.XpPercent == 10 && stack.GoldPercent == 5 && stack.DropPercent == 5,
          "reward percents add");
    StatBlock stats;
    stats.Strength = 10;
    ApplyTitleModifiers(stats, stack);
    Check(stats.Strength == 14, "percent of the base, then the flat");
    Check(ScaleByPercent(60, 0) == 60 && ScaleByPercent(60, 10) == 66, "a reward percent scales");

    Database data = Sample();
    if (!data.IsLoaded())
        return;
    SystemState worn;
    worn.Titles.push_back("goblin_slayer");
    worn.Equipped.push_back("goblin_slayer");
    const std::vector<TagDamage> rows = TitleTagDamage(data, worn);
    Check(rows.size() == 1 && rows[0].Tag == "humanoid" && rows[0].Percent == 10,
          "an old damage bonus still stacks as one tag row");
    worn.Equipped[0] = "wolf_cull";
    StatBlock sheet;
    sheet.Strength = 8;
    ApplyTitleModifiers(sheet, TitleModifiers(data, worn));
    Check(sheet.Strength == 9, "an old flat bonus is still one strength");
}

void ReplaceFile(const fs::path& path, std::string_view from, std::string_view to)
{
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const auto at = text.find(from);
    Check(at != std::string::npos, "the fixture text is present");
    if (at == std::string::npos)
        return;
    text.replace(at, from.size(), to);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

void TestDeedJson()
{
    TempDir dir("ascension-deed-json");
    std::error_code error;
    fs::remove_all(dir.Path, error);
    fs::copy(kSample, dir.Path, fs::copy_options::recursive, error);
    Check(!error, "the sample data copies");
    if (error)
        return;
    const fs::path titles = dir.Path / "titles.json";
    ReplaceFile(titles, "\"deed\": { \"on\": \"kill\", \"id\": \"wolf\", \"atLeast\": 1 }",
                "\"deed\": { \"all\": [ { \"on\": \"kill\", \"id\": \"wolf\", \"atLeast\": 1 }, "
                "{ \"not\": { \"on\": \"class\", \"id\": \"guardian\" } } ] }");
    Database data = LoadDir(dir.Path, "a compound deed loads");
    if (!data.IsLoaded())
        return;
    const TitleDef* wolf = data.FindTitle("wolf_cull");
    Check(wolf != nullptr && wolf->Deed.Op == DeedOp::All && wolf->Deed.Parts.size() == 2,
          "the wolf deed is all of two parts");
    SystemState fighter;
    SystemEvent kill;
    kill.Kind = SystemEventKind::Kill;
    kill.Id = "wolf";
    kill.Amount = 1;
    DispatchSystem(fighter, data, "fighter", 1, kill);
    Check(HasId(fighter.TitleOffers, "wolf_cull"), "a fighter who is not a guardian earns it");
    SystemState guardian;
    DispatchSystem(guardian, data, "guardian", 1, kill);
    Check(!HasId(guardian.TitleOffers, "wolf_cull"), "the not keeps a guardian from the title");

    ReplaceFile(titles, "\"all\": [ { \"on\": \"kill\", \"id\": \"wolf\", \"atLeast\": 1 }, "
                        "{ \"not\": { \"on\": \"class\", \"id\": \"guardian\" } } ]",
                "\"all\": []");
    Database rejected(dir.Path);
    DataError why;
    Check(!rejected.Load(&why), "an empty all is rejected");
}

void TestPlaystyleTitles()
{
    Database data = Shipped();
    if (!data.IsLoaded())
        return;
    const ClassDef* oathbound = data.FindClass("oathbound");
    const ClassDef* templar = data.FindClass("templar");
    Check(oathbound != nullptr && templar != nullptr && oathbound->Offer == EvolutionOffer::Meld &&
              oathbound->EvolvesFrom == "fighter" && oathbound->Meld.On == DeedOn::Tag &&
              oathbound->Meld.Tag == "heal" && templar->Offer == EvolutionOffer::Guaranteed &&
              templar->EvolvesFrom == "cleric",
          "oathbound is the fighter heal meld, and templar is the cleric's own path");
    if (oathbound == nullptr || templar == nullptr)
        return;
    Check(!oathbound->Skills.empty() && !templar->Skills.empty() &&
              oathbound->Skills[0].Id != templar->Skills[0].Id,
          "the two paladin paths do not grant the same skill");
    Check(SkillTagged(data, "brace", "defense") && SkillTagged(data, "slip", "evade") &&
              SkillTagged(data, "aegis", "ward"),
          "guard, evade, and ward are tags on the skills");

    Hero crushed = StartHero(data);
    if (!crushed.IsReady())
        return;
    for (i32 n = 0; n < 5; ++n)
        crushed.NoteUse("heavy_blow");
    Check(!HasId(crushed.TitleOffers(), "crusher"), "five heavy blows are short of Crusher");
    crushed.NoteUse("heavy_blow");
    Check(HasId(crushed.TitleOffers(), "crusher"), "the sixth heavy blow earns Crusher");
    const std::vector<std::string> told = crushed.TakeNotices();
    Check(NoticeNames(told, "Crusher") && NoticeNames(told, "Cleave"),
          "the System names the title and the skill it unlocked");

    Hero guarded = StartHero(data);
    for (i32 n = 0; n < 4; ++n)
        guarded.NoteUse("brace");
    Check(!HasId(guarded.TitleOffers(), "warden"), "four braces are short of Warden");
    guarded.NoteUse("brace");
    Check(HasId(guarded.TitleOffers(), "warden"), "five defense skills earn Warden");

    Hero blocked = StartHero(data);
    HabitNote stop;
    stop.Kind = std::string(kHabitBlocked);
    stop.Amount = 4;
    stop.Tags = {"defense"};
    for (i32 n = 0; n < 3; ++n)
        blocked.NoteHabit(stop);
    Check(HasId(blocked.TitleOffers(), "warden"), "three blocks earn Warden without the casts");

    Hero mended = StartHero(data);
    for (i32 n = 0; n < 3; ++n) {
        mended.NoteUse("second_wind");
        mended.NoteItem("minor_potion");
    }
    Check(HasId(mended.TitleOffers(), "mender"), "heals from a skill and a potion share one count");
    std::string why;
    const i32 will = mended.Attributes().Will;
    Check(mended.AcceptTitle("mender", why) && mended.EquipTitle("mender", why) &&
              mended.Attributes().Will == will + 1,
          "wearing Mender adds one will");

    Hero clean = StartHero(data);
    clean.NoteVictory(0, 20, 40, 3);
    Check(HasId(clean.TitleOffers(), "unscathed"), "a fight that dealt no damage earns Unscathed");
    Check(SystemCount(clean.System(), SystemEventKind::Mark, "", "gold") == 3,
          "the victory's gold is counted");
    Check(!HasId(clean.TitleOffers(), "runner"), "a win is not a flee");
    Hero left = StartHero(data);
    left.NoteFlee();
    left.NoteFlee();
    Check(!HasId(left.TitleOffers(), "runner"), "two retreats are short of Runner");
    left.NoteFlee();
    Check(HasId(left.TitleOffers(), "runner"), "the third retreat earns Runner");

    Hero host = StartHero(data);
    host.GrantXp(60);
    for (i32 n = 0; n < 8; ++n)
        host.NoteUse("second_wind");
    const std::vector<std::string> paths = host.ChoiceIds();
    Check(HasId(paths, "oathbound"), "eight heals still open oathbound");
    Check(NoticeNames(host.TakeNotices(), "Oathbound"), "the System names the meld");
}

void TestLogSave()
{
    Database data = Shipped();
    if (!data.IsLoaded())
        return;
    TempDir older("ascension-system-v7");
    Emerald::SaveData file;
    file.SetString("class", "fighter");
    file.SetInt("xp", 0);
    file.SetInt("counter.count", 1);
    file.SetString("counter.0.key", "skill.tag:crush");
    file.SetInt("counter.0.count", 6);
    Emerald::SaveSystem version(older.Path, 7);
    Check(version.Save("run", file, "v7"), "a version 7 ledger writes");
    RunSave oldSaves(older.Path);
    const RunLoad old = oldSaves.Read();
    Check(static_cast<bool>(old) && old.FileVersion == 7 && old.Run.Career.System.NoticesSettled &&
              old.Run.Career.System.Log.empty(),
          "version 7 migrates with an empty log and a settled flag");
    Hero hero = StartHero(data);
    if (!hero.IsReady())
        return;
    std::string why;
    Check(hero.ImportCareer(old.Run.Career, why), "the version 7 ledger imports");
    Check(HasId(hero.TitleOffers(), "crusher"), "six stored crush swings earn Crusher on load");
    Check(NoticeNames(hero.TakeNotices(), "Crusher"), "the migrated ledger still announces the title");
    Check(!hero.System().NoticesSettled, "the settled flag is spent on the first evaluation");

    TempDir dir("ascension-system-log");
    RunSave saves(dir.Path);
    CareerRecord career;
    hero.ExportCareer(career);
    RunState run;
    run.Career = career;
    Check(saves.Write(run, why), "the log writes");
    const RunLoad again = saves.Read();
    Check(static_cast<bool>(again) && again.FileVersion == 8 &&
              NoticeNames(again.Run.Career.System.Log, "Crusher"),
          "version 8 keeps the System log");
}

void TestGuardBlock()
{
    Database data = Shipped();
    if (!data.IsLoaded())
        return;
    Battle fight(data, std::make_unique<AgilityRoundModel>());
    std::string why;
    Check(fight.Start("fighter", 1, "wolf", why), "a wolf fight starts");
    if (fight.Result() == BattleResult::Ongoing && fight.Actor() == CombatantSide::Enemy) {
        fight.EnemyAct();
        fight.ClearHabits();
    }
    if (fight.Actor() != CombatantSide::Player || fight.Result() != BattleResult::Ongoing)
        return;
    const ActionResult brace = fight.UseSkill("brace");
    Check(brace.Accepted && fight.Habits().empty(), "raising a guard is not yet a block");
    if (fight.Actor() == CombatantSide::Enemy && fight.Result() == BattleResult::Ongoing)
        fight.EnemyAct();
    bool blocked = false;
    for (const HabitNote& note : fight.Habits()) {
        if (note.Kind == std::string(kHabitBlocked) && note.Amount > 0)
            blocked = true;
    }
    Check(blocked, "a guard that stops the next blow is a blocked habit");
}

} // namespace

int main()
{
    TestBusDispatch();
    TestTitleThreshold();
    TestFreePoints();
    TestEvolutionOffers();
    TestCrushSkill();
    TestDeedRecovery();
    TestReaver();
    TestTagDamage();
    TestSaveLedger();
    TestTitleSheet();
    TestTitleEquip();
    TestDeedCompounds();
    TestHabitCounts();
    TestModifierStack();
    TestDeedJson();
    TestPlaystyleTitles();
    TestLogSave();
    TestGuardBlock();
    if (g_Failures == 0)
        std::printf("All system checks passed.\n");
    else
        std::printf("%d system check(s) failed.\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
