// The System bus: deed counters, title thresholds, class-rarity free points, the guaranteed
// track beside the heal and crush melds, and a ledger that recovers a new deed on load.
// No window or GPU.

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
    Check(RunSave::kVersion == 7, "new deeds stay on save version 7");
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
    Check(bear.AcceptTitle("bear_cull", why) && bear.Attributes().Stamina == stamina + 1,
          "Bear Cull adds one stamina");

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
    Check(barrow.AcceptTitle("tombwalker", why) && barrow.Attributes().Defense == defense + 1,
          "Tombwalker adds one defense");
    const i32 will = barrow.Attributes().Will;
    barrow.NoteKill("oathkeeper_varn");
    Check(barrow.TitleOffers().size() == 1 && barrow.TitleOffers()[0] == "oaths_end",
          "Varn opens Oath's End on its own kill");
    Check(barrow.AcceptTitle("oaths_end", why) && barrow.Attributes().Will == will + 1,
          "Oath's End adds one will");

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
    Check(static_cast<bool>(loaded) && loaded.FileVersion == 7, "the crush ledger is still version 7");
    Hero recovered = StartHero(data);
    Check(recovered.ImportCareer(loaded.Run.Career, why), "the crush ledger imports");
    const std::vector<std::string> recoveredPaths = recovered.ChoiceIds();
    Check(recovered.Level() == 5 && recovered.SystemSkillOffers().size() == 1 &&
              recovered.SystemSkillOffers()[0] == "wide_swing" && recoveredPaths.size() == 3 &&
              recoveredPaths[2] == "reaver",
          "stored crush uses offer cleave and reaver on load");
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
    if (g_Failures == 0)
        std::printf("All system checks passed.\n");
    else
        std::printf("%d system check(s) failed.\n", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
