// Save and load: a round trip of a run, a migration from the previous version, and a
// truncated file that falls back to the previous copy. No window or GPU.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Ascension/Progress/RunSave.h"
#include "Check.h"
#include "Emerald/Core/Log.h"
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
    return data;
}

struct ShippedCopy {
    fs::path Path;

    ShippedCopy()
    {
        Path = fs::temp_directory_path() / "ascension-save-shipped";
        std::error_code error;
        fs::remove_all(Path, error);
        fs::create_directories(Path);
        fs::copy(ASCENSION_SHIPPED_DATA_DIR, Path, fs::copy_options::recursive);
    }

    ~ShippedCopy()
    {
        std::error_code error;
        fs::remove_all(Path, error);
    }
};

struct DataCopy {
    fs::path Path;

    DataCopy()
    {
        Path = fs::temp_directory_path() / "ascension-save-data";
        std::error_code error;
        fs::remove_all(Path, error);
        fs::create_directories(Path);
        fs::copy(kSample, Path, fs::copy_options::recursive);
    }

    ~DataCopy()
    {
        std::error_code error;
        fs::remove_all(Path, error);
    }
};

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
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

// The frozen sample has no picks. This copy offers the level-6 and level-8 choices the
// round trip keeps, using skills that already exist in the sample. The caller keeps `copy`
// alive for as long as the database is used.
Database SampleWithPicks(const DataCopy& copy)
{
    ReplaceInFile(copy.Path / "classes.json",
                  "\"skills\": [\"rage\", \"cleave\"],\n      \"passives\": [\"bloodlust\"]",
                  "\"skills\": [\"rage\", \"cleave\"],\n      \"passives\": [\"bloodlust\"],\n"
                  "      \"picks\": [\n"
                  "        { \"level\": 6, \"options\": [\"riposte\", { \"skill\": \"brace\", "
                  "\"tune\": { \"ward\": { \"amount\": 75 }, \"focusCost\": 1 } }] },\n"
                  "        { \"level\": 8, \"options\": [\"shield_wall\", { \"skill\": "
                  "\"second_wind\", \"tune\": { \"focusCost\": 1 } }] }\n"
                  "      ]");
    Database data(copy.Path);
    DataError error;
    Check(data.Load(&error), "sample data with picks loads");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    return data;
}

Hero StartHero(const Database& data)
{
    Hero hero;
    std::string why;
    Check(hero.Start(data, why), "the hero starts");
    return hero;
}

Inventory StartBag(const Database& data)
{
    Inventory bag;
    std::string why;
    Check(bag.Start(data, why), "the bag starts");
    return bag;
}

i32 StackCount(const Inventory& bag, std::string_view id)
{
    i32 total = 0;
    for (i32 slot = 0; slot < bag.BagSlots(); ++slot) {
        if (bag.BagAt(slot).Id == id)
            total += bag.BagAt(slot).Count;
    }
    return total;
}

i32 BagIndex(const Inventory& bag, std::string_view id)
{
    for (i32 index = 0; index < bag.BagSlots(); ++index) {
        if (bag.BagAt(index).Id == id)
            return index;
    }
    return -1;
}

const HeroGrant* FindGrant(const std::vector<HeroGrant>& grants, std::string_view id)
{
    for (const HeroGrant& grant : grants) {
        if (grant.Skill.Id == id)
            return &grant;
    }
    return nullptr;
}

std::string BossId(const Database& data)
{
    for (const EnemyDef& enemy : data.GetEnemies()) {
        if (enemy.Boss)
            return enemy.Id;
    }
    return {};
}

bool SameStats(const StatBlock& left, const StatBlock& right)
{
    return left.Vitality == right.Vitality && left.Strength == right.Strength &&
           left.Defense == right.Defense && left.Agility == right.Agility &&
           left.Focus == right.Focus;
}

RunState Capture(const Hero& hero, const Inventory& bag)
{
    RunState run;
    hero.ExportCareer(run.Career);
    bag.ExportBag(run.Bag);
    return run;
}

bool Restore(Hero& hero, Inventory& bag, const RunState& run, std::string& error)
{
    return hero.ImportCareer(run.Career, error) && bag.ImportBag(run.Bag, error);
}

void TestRoundTrip()
{
    DataCopy picks;
    Database data = SampleWithPicks(picks);
    const SkillDef* catalogWind = data.FindSkill("second_wind");
    const SkillDef* catalogBrace = data.FindSkill("brace");
    const EffectDef* catalogWard =
        catalogBrace != nullptr ? catalogBrace->FindEffect("ward") : nullptr;
    Check(catalogWard != nullptr && catalogWard->Amount == 50 && catalogWind != nullptr &&
              catalogWind->FocusCost == 2,
          "brace guards 50 and second wind costs 2 before a tune");
    Hero hero = StartHero(data);
    Inventory bag = StartBag(data);
    std::string why;
    hero.GrantXp(60);
    Check(hero.Level() == 5 && hero.Choose("berserker", why), "level 5 takes berserker");
    hero.GrantXp(30);
    Check(hero.Level() == 6 && hero.Pick("brace", why), "level 6 keeps the tuned brace");
    Check(hero.NoteUse("strike") == 1 && hero.NoteUse("strike") == 1 && hero.NoteUse("strike") == 2,
          "three strikes reach rank 2");
    hero.GrantXp(90);
    Check(hero.Level() == 8 && hero.Pick("second_wind", why),
          "level 8 keeps the tuned second wind");
    bag.GrantGold(40);
    Check(bag.Give("minor_potion", 6, why) && bag.Give("rusty_sword", 1, why),
          "the bag is stocked");
    i32 swordSlot = -1;
    for (i32 slot = 0; slot < bag.BagSlots(); ++slot) {
        if (bag.BagAt(slot).Id == "rusty_sword")
            swordSlot = slot;
    }
    Check(bag.Equip(swordSlot, why), "the sword is worn");

    RunState run = Capture(hero, bag);
    const std::string boss = BossId(data);
    run.Defeated.push_back("wolf");
    run.Defeated.push_back(boss);
    run.Flags.push_back("met");
    run.Found.push_back("alphas_fang");
    run.EncounterId = "wolf_pack";
    run.MapId = "fixture_a";
    run.Tile.X = 3;
    run.Tile.Y = 1;
    run.Cleared.push_back("fixture_spawn");
    run.Cleared.push_back("retired_spawn");
    FledWounds wounds;
    wounds.SpawnId = "fixture_hold";
    FoeWound foe;
    foe.Id = "wolf";
    foe.Hp = 11;
    wounds.Foes.push_back(foe);
    run.Wounds.push_back(std::move(wounds));
    run.SpawnId = "fixture_hold";
    run.DirectEnemy = "rubble_shard";
    run.Standing = RunStanding::Ongoing;
    run.ResourcesKnown = true;
    run.Hp = 17;
    run.Focus = 1;
    const StatBlock stats = hero.Attributes();

    TempDir dir("ascension-save-round");
    RunSave saves(dir.Path);
    Check(saves.Write(run, why), "the run is written");
    Check(!fs::exists(dir.Path / "run.sav.tmp"), "the temp file is renamed away");
    Check(fs::exists(dir.Path / "run.sav"), "the slot file is in place");

    std::ifstream in(dir.Path / "run.sav");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Check(text.find("EMERALD-SAVE 1") == 0, "the file is an Emerald save, not JSON");
    Check(text.find("version = 4") != std::string::npos, "the header carries the data version");
    Check(text.find("position =") == std::string::npos, "the encounter index is not stored");
    Check(text.find('{') == std::string::npos, "the save text has no JSON object");

    const RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.FileVersion == RunSave::kVersion &&
              !loaded.FromBackup,
          "the same version loads");
    Hero again = StartHero(data);
    Inventory bagAgain = StartBag(data);
    Check(Restore(again, bagAgain, loaded.Run, why), "the run is applied");
    Check(again.ClassId() == "berserker" && again.Xp() == hero.Xp() && again.Level() == 8,
          "class, XP, and level come back");
    Check(SameStats(again.Attributes(), stats), "the stats come back");
    Check(again.UsesOf("strike") == 3 && again.RankOf("strike") == 2, "skill practice comes back");
    Check(again.PickedIds().size() == 2 && again.PickedIds()[0] == "brace" &&
              again.PickedIds()[1] == "second_wind",
          "both skill picks come back in order");
    const ClassDef* berserker = data.FindClass("berserker");
    const std::vector<HeroGrant> grants =
        berserker != nullptr ? data.HeroGrants(*berserker, bagAgain.WornIds(), again.PickedIds())
                             : std::vector<HeroGrant>{};
    const HeroGrant* brace = FindGrant(grants, "brace");
    const HeroGrant* wind = FindGrant(grants, "second_wind");
    const EffectDef* ward = brace != nullptr ? brace->Skill.FindEffect("ward") : nullptr;
    Check(ward != nullptr && ward->Amount == 75, "the brace upgrade tune comes back");
    Check(wind != nullptr && wind->Skill.FocusCost == 1, "the second wind tune comes back");
    Check(bagAgain.Gold() == 40 && StackCount(bagAgain, "minor_potion") == 6 &&
              bagAgain.Equipped(GearSlot::Weapon) == "rusty_sword",
          "gold, stack counts, and worn gear come back");
    Check(loaded.Run.Defeated.size() == 2 && loaded.Run.Defeated[0] == "wolf" &&
              loaded.Run.Defeated[1] == boss && loaded.Run.EncounterId == "wolf_pack" &&
              loaded.Run.DirectEnemy == "rubble_shard" &&
              loaded.Run.Standing == RunStanding::Ongoing && loaded.Run.ResourcesKnown &&
              loaded.Run.Hp == 17 && loaded.Run.Focus == 1 && loaded.Run.Flags.size() == 1 &&
              loaded.Run.Flags[0] == "met" && loaded.Run.Found.size() == 1 &&
              loaded.Run.Found[0] == "alphas_fang",
          "defeated foes, the encounter, the wound, a flag, and the fang come back");
    Check(loaded.Run.MapId == "fixture_a" && loaded.Run.Tile.X == 3 && loaded.Run.Tile.Y == 1 &&
              loaded.Run.SpawnId == "fixture_hold" && loaded.Run.Cleared.size() == 2 &&
              loaded.Run.Cleared[0] == "fixture_spawn" && loaded.Run.Cleared[1] == "retired_spawn" &&
              loaded.Run.Wounds.size() == 1 && loaded.Run.Wounds[0].SpawnId == "fixture_hold" &&
              loaded.Run.Wounds[0].Foes.size() == 1 && loaded.Run.Wounds[0].Foes[0].Id == "wolf" &&
              loaded.Run.Wounds[0].Foes[0].Hp == 11 && loaded.Run.LegacyIndex < 0,
          "the map, the tile, cleared spawn ids, and the flee wound come back");

    Battle battle(data, std::make_unique<AgilityRoundModel>());
    battle.SetPickedSkills(again.PickedIds());
    battle.SetBaseStats(again.Attributes());
    battle.SetGear(bagAgain.GearBonus());
    Check(battle.Start(again.ClassId(), again.Level(), "wolf", why), "the resumed fight starts");
    battle.SetPlayerHp(loaded.Run.Hp);
    battle.SetPlayerFocus(loaded.Run.Focus);
    Check(battle.Player().Hp == 17 && battle.Player().Focus == 1, "HP and focus are restored");

    const fs::path folder = RunSave::DefaultFolder();
    Check(folder.filename() == "saves" && folder.parent_path().filename() == "Ascension",
          "the default folder is the per-user Ascension saves directory");
}

void TestCustomStats()
{
    Database data = Sample();
    Hero hero = StartHero(data);
    std::string why;
    hero.GrantXp(60);
    Check(hero.ResetBuild(why) && hero.ChooseStarter("fighter", why) &&
              hero.SpendClassPoint("strength", why),
          "a reset spends one strength");
    const StatBlock stats = hero.Attributes();
    const i32 pool = hero.ClassPool();
    RunState run = Capture(hero, StartBag(data));
    TempDir dir("ascension-save-custom");
    RunSave saves(dir.Path);
    Check(saves.Write(run, why), "the custom build is written");
    const RunLoad loaded = saves.Read();
    Hero again = StartHero(data);
    Inventory bag = StartBag(data);
    Check(static_cast<bool>(loaded) && Restore(again, bag, loaded.Run, why),
          "the custom build loads");
    Check(again.ClassId() == "fighter" && again.ChoosingStarter() == false &&
              again.ClassPool() == pool && SameStats(again.Attributes(), stats),
          "the spent stats and the remaining pool come back");
    Check(again.Attributes().Strength !=
              data.StatsAt(*data.FindClass("fighter"), again.Level()).Strength,
          "a reset build is not rewritten by the class formula");
}

void TestMigration()
{
    Database data = Sample();
    TempDir dir("ascension-save-migrate");
    Emerald::SaveData older;
    older.SetString("class", "fighter");
    older.SetInt("xp", 8);
    older.SetInt("gold", 11);
    older.SetInt("health", 15);
    older.SetInt("resource", 3);
    older.SetInt("place", 2);
    older.SetString("foes", "wolf, bandit");
    older.SetString("standing", "victory");
    older.SetInt("bag.count", 1);
    older.SetString("bag.0.id", "minor_potion");
    older.SetInt("bag.0.count", 2);
    older.SetString("worn.weapon", "rusty_sword");
    Emerald::SaveSystem version1(dir.Path, 1);
    Check(version1.Save("run", older, "old"), "a version 1 file is written");

    RunSave saves(dir.Path);
    RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.FileVersion == 1 && !loaded.FromBackup,
          "version 1 migrates forward");
    Hero hero = StartHero(data);
    Inventory bag = StartBag(data);
    std::string why;
    Check(Restore(hero, bag, loaded.Run, why), "the migrated run is applied");
    Check(hero.ClassId() == "fighter" && hero.Xp() == 8 && hero.Level() == 2,
          "the old career loads");
    Check(bag.Gold() == 11 && StackCount(bag, "minor_potion") == 2 &&
              bag.Equipped(GearSlot::Weapon) == "rusty_sword",
          "the old bag loads");
    AdoptLegacyMap(data, loaded.Run);
    Check(!loaded.Run.ResourcesKnown && loaded.Run.Hp == 0 && loaded.Run.Focus == 0 &&
              loaded.Run.Standing == RunStanding::Victory && loaded.Run.LegacyIndex < 0 &&
              loaded.Run.EncounterId == "tomb_moth" && loaded.Run.MapId == "fixture_a" &&
              loaded.Run.Tile.X == 1 && loaded.Run.Tile.Y == 1 && loaded.Run.Defeated.size() == 2 &&
              loaded.Run.Defeated[0] == "wolf" && loaded.Run.Defeated[1] == "bandit",
          "a finished fight drops the old health, and the index becomes an encounter and a tile");
    Check(loaded.Run.Cleared.size() == 1 && loaded.Run.Cleared[0] == "fixture_spawn",
          "the wolf spawn is cleared because its encounter id was defeated");
    bool holdCleared = false;
    for (const std::string& id : loaded.Run.Cleared) {
        if (id == "fixture_hold")
            holdCleared = true;
    }
    Check(!holdCleared, "a repeatable spawn is not cleared by the old list");
    Check(!loaded.Run.Fight.Known, "a version 1 file has no mid-fight snapshot");
}

const StatusInstance* FindStatus(const CombatantState& who, std::string_view id)
{
    for (const StatusInstance& status : who.Statuses) {
        if (status.Id == id)
            return &status;
    }
    return nullptr;
}

bool SameStatus(const CombatantState& who, std::string_view id, i32 stacks, i32 turns)
{
    const StatusInstance* status = FindStatus(who, id);
    return status != nullptr && status->Stacks == stacks && status->TurnsLeft == turns;
}

void TestMidFight()
{
    Database data = Sample();
    std::string why;
    Battle battle(data, std::make_unique<AgilityRoundModel>());
    Check(battle.Start("fighter", 1, "wolf", why), "the wolf fight starts");
    const ActionResult bite = battle.EnemyAct();
    Check(bite.Accepted && battle.Actor() == CombatantSide::Player, "the wolf has already bitten");
    const i32 heroHp = battle.Player().Hp;
    battle.SetEnemyHp(22);
    Check(heroHp < battle.Player().MaxHp && battle.Enemy().Hp == 22, "the wolf is wounded to 22");
    Check(battle.Inflict(CombatantSide::Enemy, "bleed", why) &&
              battle.Inflict(CombatantSide::Enemy, "bleed", why) &&
              battle.Inflict(CombatantSide::Enemy, "fury", why),
          "the wolf is bleeding and furious");
    const i32 step = battle.Enemy().PatternStep;
    const i32 turns = battle.ExportFight().TurnCount;
    Check(turns >= 1 && battle.Enemy().PatternStep == step, "the pattern step is still the wolf's");

    Hero hero = StartHero(data);
    Inventory bag = StartBag(data);
    RunState run = Capture(hero, bag);
    run.Standing = RunStanding::Ongoing;
    run.ResourcesKnown = true;
    run.Hp = heroHp;
    run.Focus = battle.Player().Focus;
    run.EncounterId = "wolf";
    run.SpawnId = "fixture_spawn";
    run.MapId = "fixture_a";
    run.Tile.X = 4;
    run.Tile.Y = 1;
    run.Fight = battle.ExportFight();
    Check(run.Fight.Known && run.Fight.Actor == CombatantSide::Player && run.Fight.Foes.size() == 1,
          "the snapshot is the player's turn against one wolf");

    TempDir dir("ascension-save-fight");
    RunSave saves(dir.Path);
    Check(saves.Write(run, why), "the mid-fight run is written");
    const RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.FileVersion == RunSave::kVersion &&
              loaded.Run.Fight.Known && loaded.Run.SpawnId == "fixture_spawn" &&
              loaded.Run.MapId == "fixture_a" && loaded.Run.Tile.X == 4 && loaded.Run.Tile.Y == 1,
          "the mid-fight snapshot loads with the spawn it came from");

    Battle again(data, std::make_unique<AgilityRoundModel>());
    Check(again.Start("fighter", 1, "wolf", why), "a fresh wolf fight starts");
    Check(again.Actor() == CombatantSide::Enemy && again.Enemy().Hp == again.Enemy().MaxHp,
          "a fresh wolf would bite first at full health");
    const i32 before = again.Enemy().Hp;
    Check(again.ImportFight(loaded.Run.Fight, why), "the snapshot is restored");
    Check(again.Actor() == CombatantSide::Player, "resume stays on the player's turn");
    Check(again.Player().Hp == heroHp && again.Enemy().Hp == 22, "hero and wolf HP match");
    Check(again.Enemy().Hp != before, "the wolf was not left at full health");
    Check(SameStatus(again.Enemy(), "bleed", 2, 3) && SameStatus(again.Enemy(), "fury", 1, 4),
          "bleed stacks and fury turns match");
    Check(again.Enemy().PatternStep == step && again.ExportFight().TurnCount == turns,
          "pattern step and turn count match");
    const ActionResult extra = again.EnemyAct();
    Check(!extra.Accepted && again.Enemy().Hp == 22 && again.Player().Hp == heroHp,
          "restoring does not give the foe an action");

    Battle sentry(data, std::make_unique<AgilityRoundModel>());
    Check(sentry.Start("fighter", 1, "barrow_sentry", why) && sentry.Wait().Accepted &&
              sentry.EnemyAct().Accepted,
          "the sentry hardens on its turn");
    Check(sentry.Enemy().PatternStep == 1 && sentry.Enemy().BonusDefense == 4 &&
              sentry.Enemy().BonusDefenseTurns == 3 && sentry.Actor() == CombatantSide::Player,
          "the sentry's pattern and harden are live");
    sentry.SetEnemyHp(40);
    Check(sentry.Enemy().Hp == 40 && sentry.Inflict(CombatantSide::Enemy, "exposed", why),
          "the sentry is wounded and exposed");
    RunState sentryRun = Capture(hero, bag);
    sentryRun.Standing = RunStanding::Ongoing;
    sentryRun.Fight = sentry.ExportFight();
    TempDir sentryDir("ascension-save-sentry");
    RunSave sentrySave(sentryDir.Path);
    Check(sentrySave.Write(sentryRun, why), "the sentry fight is written");
    const RunLoad sentryLoaded = sentrySave.Read();
    Battle sentryAgain(data, std::make_unique<AgilityRoundModel>());
    Check(static_cast<bool>(sentryLoaded) &&
              sentryAgain.Start("fighter", 1, "barrow_sentry", why) &&
              sentryAgain.ImportFight(sentryLoaded.Run.Fight, why),
          "the sentry snapshot is restored");
    Check(sentryAgain.Enemy().Hp == 40 && sentryAgain.Enemy().PatternStep == 1 &&
              sentryAgain.Enemy().BonusDefense == 4 && sentryAgain.Enemy().BonusDefenseTurns == 3 &&
              SameStatus(sentryAgain.Enemy(), "exposed", 1, 2) &&
              sentryAgain.Actor() == CombatantSide::Player,
          "sentry HP, pattern, harden, and exposed match");
    const ActionResult sentryExtra = sentryAgain.EnemyAct();
    Check(!sentryExtra.Accepted && sentryAgain.Enemy().Hp == 40 &&
              sentryAgain.Enemy().PatternStep == 1,
          "restoring the sentry does not advance it");
}

void TestVersion2Restarts()
{
    TempDir dir("ascension-save-v2");
    Emerald::SaveData older;
    older.SetString("class", "fighter");
    older.SetInt("xp", 0);
    older.SetInt("hp", 45);
    older.SetInt("focus", 4);
    older.SetBool("resources", true);
    older.SetString("standing", "ongoing");
    older.SetInt("position", 0);
    Emerald::SaveSystem version2(dir.Path, 2);
    Check(version2.Save("run", older, "ongoing"), "a version 2 ongoing file is written");

    RunSave saves(dir.Path);
    const RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.FileVersion == 2 && !loaded.Run.Fight.Known &&
              loaded.Run.Standing == RunStanding::Ongoing && loaded.Run.Hp == 45,
          "version 2 migrates and keeps no foe snapshot");
}

void TestCorruptFallsBack()
{
    Database data = Sample();
    Hero hero = StartHero(data);
    Inventory bag = StartBag(data);
    std::string why;
    hero.GrantXp(8);
    bag.GrantGold(4);
    RunState first = Capture(hero, bag);
    first.Standing = RunStanding::Victory;
    first.EncounterId = "bandit";

    Hero richer = StartHero(data);
    Inventory richerBag = StartBag(data);
    richer.GrantXp(20);
    richerBag.GrantGold(9);
    RunState second = Capture(richer, richerBag);
    second.Standing = RunStanding::Victory;

    TempDir dir("ascension-save-corrupt");
    RunSave saves(dir.Path);
    Check(saves.Write(first, why) && saves.Write(second, why), "two saves rotate a backup");
    Check(fs::exists(dir.Path / "run.sav.bak"), "the previous save is kept beside the slot");

    {
        std::ofstream out(dir.Path / "run.sav", std::ios::binary | std::ios::trunc);
        out << "EMERALD-SAVE 1\nversion = 2\n";
    }
    const RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.FromBackup, "a truncated file loads the backup");
    Hero again = StartHero(data);
    Inventory bagAgain = StartBag(data);
    Check(Restore(again, bagAgain, loaded.Run, why), "the backup is applied");
    Check(again.Xp() == 8 && bagAgain.Gold() == 4 && loaded.Run.EncounterId == "bandit",
          "the backup is the earlier run, not the truncated one");

    std::error_code removeError;
    fs::remove(dir.Path / "run.sav.bak", removeError);
    const RunLoad broken = saves.Read();
    Check(!broken && broken.Error == RunSaveError::Corrupt,
          "a truncated file with no backup fails");
    Hero fresh = StartHero(data);
    Check(fresh.Level() == 1 && fresh.Xp() == 0, "a failed load leaves a new run to start");
}

bool HoldsStatus(const CombatantState& who, std::string_view id)
{
    for (const StatusInstance& status : who.Statuses) {
        if (status.Id == id)
            return true;
    }
    return false;
}

void TestAiSnapshot()
{
    DataCopy copy;
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
                  "\"do\": \"skill\", \"skill\": \"thrust\" }\n      ],\n      \"experience\": 0,\n"
                  "      \"gold\": 0\n    }");
    Database data(copy.Path);
    DataError loadError;
    Check(data.Load(&loadError), "the rule post loads");
    if (!data.IsLoaded())
        return;
    std::string why;
    Battle post(data, std::make_unique<AgilityRoundModel>());
    Check(post.Start("fighter", 1, "rule_post", why) &&
              post.Inflict(CombatantSide::Player, "exposed", why),
          "the fighter starts Exposed");
    Check(post.UseSkill("strike").Accepted && post.Actor() == CombatantSide::Enemy,
          "the save is taken on the post's turn");
    const i32 step = post.Enemy().PatternStep;
    Check(step == 0 && HoldsStatus(post.Player(), "exposed"),
          "Harden is queued and the fighter is Exposed");

    Hero hero = StartHero(data);
    Inventory bag = StartBag(data);
    RunState run = Capture(hero, bag);
    run.Standing = RunStanding::Ongoing;
    run.Fight = post.ExportFight();
    Check(run.Fight.Known && run.Fight.Actor == CombatantSide::Enemy &&
              run.Fight.Foes.size() == 1 && run.Fight.Foes[0].PatternStep == step,
          "the snapshot is the post's turn at the queued Harden");

    TempDir dir("ascension-save-ai");
    RunSave saves(dir.Path);
    Check(saves.Write(run, why), "the exposed post fight is written");
    const RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.FileVersion == RunSave::kVersion &&
              loaded.Run.Fight.Known,
          "the snapshot loads at the current version");

    Battle again(data, std::make_unique<AgilityRoundModel>());
    Check(again.Start("fighter", 1, "rule_post", why) && again.ImportFight(loaded.Run.Fight, why),
          "the exposed post snapshot is restored");
    Check(again.Actor() == CombatantSide::Enemy && again.Enemy().PatternStep == step &&
              HoldsStatus(again.Player(), "exposed"),
          "step and Exposed survive the round trip");
    const ActionResult next = again.EnemyAct();
    Check(next.Accepted && next.SkillId == "thrust" && again.Enemy().PatternStep == step,
          "the restored post still inserts Thrust and leaves the step");
    if (next.SkillId != "thrust" || again.Enemy().PatternStep != step)
        std::printf("    restored: %s step %d\n", next.SkillId.c_str(), again.Enemy().PatternStep);
}

bool ListedSkill(const Battle& battle, std::string_view id)
{
    for (const std::string& skill : battle.PlayerSkillIds()) {
        if (skill == id)
            return true;
    }
    return false;
}

bool ClassWearsFang(const Database& data, std::string_view classId)
{
    Inventory bag;
    std::string why;
    if (!bag.Start(data, why) || !bag.Give("alphas_fang", 1, why))
        return false;
    const i32 slot = BagIndex(bag, "alphas_fang");
    if (slot < 0 || !bag.Equip(slot, why))
        return false;
    if (bag.Equipped(GearSlot::Accessory) != "alphas_fang")
        return false;

    Battle battle(data, std::make_unique<AgilityRoundModel>());
    battle.SetWornItems({"alphas_fang"});
    if (!battle.Start(classId, 5, "wolf", why))
        return false;
    for (i32 step = 0; step < 8 && battle.Result() == BattleResult::Ongoing &&
                       battle.Actor() != CombatantSide::Player;
         ++step)
        battle.EnemyAct();
    if (battle.Result() != BattleResult::Ongoing || battle.Actor() != CombatantSide::Player)
        return false;
    if (!ListedSkill(battle, "fang_bloodlust"))
        return false;
    const ActionResult used = battle.UseSkill("fang_bloodlust");
    return used.Accepted && HoldsStatus(battle.Player(), "bloodlust");
}

void AddWanderer(const fs::path& classes)
{
    // The shipped roster no longer ends on Guardian. Insert before the classes list closes.
    ReplaceInFile(classes, "    }\n  ]\n}",
                  "    },\n"
                  "    {\n"
                  "      \"id\": \"wanderer\",\n"
                  "      \"name\": \"Wanderer\",\n"
                  "      \"description\": \"A traveler with the same drills.\",\n"
                  "      \"starter\": true,\n"
                  "      \"stats\": {\n"
                  "        \"vitality\": 18,\n"
                  "        \"strength\": 8,\n"
                  "        \"defense\": 6,\n"
                  "        \"agility\": 5,\n"
                  "        \"focus\": 4\n"
                  "      },\n"
                  "      \"growth\": {\n"
                  "        \"vitality\": 4,\n"
                  "        \"strength\": 2,\n"
                  "        \"defense\": 1,\n"
                  "        \"agility\": 1,\n"
                  "        \"focus\": 1\n"
                  "      },\n"
                  "      \"skills\": [\"strike\", \"brace\", \"heavy_blow\", \"second_wind\"],\n"
                  "      \"passives\": []\n"
                  "    }\n"
                  "  ]\n}");
}

void TestFangAnyClass()
{
    Database data(ASCENSION_SHIPPED_DATA_DIR);
    DataError error;
    Check(data.Load(&error), "shipped data loads for the fang");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    if (!data.IsLoaded())
        return;
    for (const ClassDef& klass : data.GetClasses()) {
        const bool wore = ClassWearsFang(data, klass.Id);
        if (!wore)
            std::printf("    %s could not equip the fang\n", klass.Id.c_str());
        Check(wore, "every shipped class equips Alpha's Fang and uses its skill");
    }

    ShippedCopy copy;
    AddWanderer(copy.Path / "classes.json");
    Database future(copy.Path);
    DataError again;
    Check(future.Load(&again), "a future starter loads beside the shipped classes");
    if (!again.Message.empty())
        std::printf("    %s\n", again.Message.c_str());
    if (!future.IsLoaded())
        return;
    const ClassDef* wanderer = future.FindClass("wanderer");
    Check(wanderer != nullptr && wanderer->Starter, "wanderer is a starter");
    i32 checked = 0;
    for (const ClassDef& klass : future.GetClasses()) {
        const bool wore = ClassWearsFang(future, klass.Id);
        if (!wore)
            std::printf("    %s could not equip the fang\n", klass.Id.c_str());
        Check(wore, "every class on the roster, including a future starter, equips the fang");
        ++checked;
    }
    Check(checked == static_cast<i32>(data.GetClasses().size()) + 1,
          "the future roster is the shipped classes plus wanderer");
}

void TestFangDrop()
{
    Database data(ASCENSION_SHIPPED_DATA_DIR);
    DataError error;
    Check(data.Load(&error), "shipped data loads for the drop");
    if (!data.IsLoaded())
        return;
    Inventory bag = StartBag(data);
    std::vector<std::string> found;
    const std::vector<std::string> first =
        bag.TakeDrops(std::vector<std::string>{"dire_wolf"}, found);
    Check(first.size() == 1 && first[0] == "The Alpha falls. The fang slides free, still warm." &&
              StackCount(bag, "alphas_fang") == 1 && found.size() == 1 && found[0] == "alphas_fang",
          "the first dire wolf kill prints the fang line");
    const std::vector<std::string> second =
        bag.TakeDrops(std::vector<std::string>{"dire_wolf"}, found);
    Check(second.empty() && StackCount(bag, "alphas_fang") == 1 && found.size() == 1,
          "a second kill does not drop the fang again");
    std::string why;
    const i32 slot = BagIndex(bag, "alphas_fang");
    Check(slot >= 0 && bag.Sell(slot, why) && StackCount(bag, "alphas_fang") == 0,
          "the fang can be sold");
    const std::vector<std::string> sold =
        bag.TakeDrops(std::vector<std::string>{"dire_wolf"}, found);
    Check(sold.empty() && StackCount(bag, "alphas_fang") == 0 && found.size() == 1,
          "a kill after the sale still does not drop the fang");

    Hero hero = StartHero(data);
    RunState run = Capture(hero, bag);
    run.Found = found;
    TempDir dir("ascension-save-fang");
    RunSave saves(dir.Path);
    Check(saves.Write(run, why), "the claimed fang is written");
    const RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.Run.Found.size() == 1 &&
              loaded.Run.Found[0] == "alphas_fang",
          "the save keeps the claimed fang");
    Inventory fresh = StartBag(data);
    std::vector<std::string> claimed = loaded.Run.Found;
    const std::vector<std::string> again =
        fresh.TakeDrops(std::vector<std::string>{"dire_wolf"}, claimed);
    Check(again.empty() && StackCount(fresh, "alphas_fang") == 0,
          "a loaded claim blocks the drop on an empty bag");
}

void SetWolfDrops(const fs::path& dir, std::string_view drops)
{
    const std::string to =
        std::string("\"skills\": [\"bite\"],\n      \"experience\": 8,\n      \"gold\": 3,\n"
                    "      \"drops\": ") +
        std::string(drops);
    ReplaceInFile(dir / "enemies.json",
                  "\"skills\": [\"bite\"],\n      \"experience\": 8,\n      \"gold\": 3", to);
}

void TestGenericDrop()
{
    {
        DataCopy copy;
        SetWolfDrops(copy.Path, "[{ \"item\": \"minor_potion\" }]");
        Database data(copy.Path);
        DataError error;
        Check(data.Load(&error), "a drop without a message loads");
        if (!error.Message.empty())
            std::printf("    %s\n", error.Message.c_str());
        if (!data.IsLoaded())
            return;
        Inventory bag = StartBag(data);
        std::vector<std::string> found;
        const std::vector<std::string> first =
            bag.TakeDrops(std::vector<std::string>{"wolf"}, found);
        const std::vector<std::string> second =
            bag.TakeDrops(std::vector<std::string>{"wolf"}, found);
        Check(first.size() == 1 && first[0] == "Found Minor Potion." && second.size() == 1 &&
                  second[0] == "Found Minor Potion." && StackCount(bag, "minor_potion") == 2 &&
                  found.empty(),
              "a common drop repeats and uses the generic line");
    }
    {
        DataCopy copy;
        SetWolfDrops(copy.Path, "[{ \"item\": \"minor_potion\", \"message\": \"A cold vial.\" }]");
        Database data(copy.Path);
        DataError error;
        Check(data.Load(&error), "a drop message loads");
        if (!data.IsLoaded())
            return;
        Inventory bag = StartBag(data);
        std::vector<std::string> found;
        const std::vector<std::string> line =
            bag.TakeDrops(std::vector<std::string>{"wolf"}, found);
        Check(line.size() == 1 && line[0] == "A cold vial." && StackCount(bag, "minor_potion") == 1,
              "a drop message is the log line");
    }
}

void TestVictoryHp()
{
    Database data = Sample();
    if (!data.IsLoaded())
        return;
    std::string why;
    Battle battle(data, std::make_unique<AgilityRoundModel>());
    Check(battle.Start("fighter", 1, "wolf", why), "the victory fight starts");
    Check(battle.EnemyAct().Accepted, "the wolf bites before the win");
    const i32 wounded = battle.Player().Hp;
    Check(wounded > 0 && wounded < battle.Player().MaxHp, "the bite leaves a wound");
    for (i32 step = 0; step < 12 && battle.Result() == BattleResult::Ongoing; ++step) {
        if (battle.Actor() == CombatantSide::Player)
            battle.UseSkill("strike");
        else
            battle.EnemyAct();
    }
    Check(battle.Result() == BattleResult::Victory && battle.Player().Hp > 0 &&
              battle.Player().Hp < battle.Player().MaxHp,
          "the win leaves the hero alive and still wounded");

    Hero hero = StartHero(data);
    RunState run = Capture(hero, StartBag(data));
    RememberOutcome(run, battle.Result(), battle.Player().Hp, battle.Player().Focus);
    TempDir dir("ascension-save-victory-hp");
    RunSave saves(dir.Path);
    Check(saves.Write(run, why), "the victory is written");
    const RunLoad loaded = saves.Read();
    const i32 left = battle.Player().Hp;
    Check(static_cast<bool>(loaded) && loaded.Run.Standing == RunStanding::Victory &&
              loaded.Run.ResourcesKnown && loaded.Run.Hp == left && ResumeWound(loaded.Run),
          "a victory save keeps the HP that was left");

    Battle next(data, std::make_unique<AgilityRoundModel>());
    Check(next.Start("fighter", 1, "bandit", why), "the next fight starts from that save");
    if (ResumeWound(loaded.Run))
        next.SetPlayerHp(loaded.Run.Hp);
    Check(next.Player().Hp == left, "the next fight starts at the HP the win left");
}

bool HoldsId(const std::vector<std::string>& ids, std::string_view id)
{
    for (const std::string& held : ids) {
        if (held == id)
            return true;
    }
    return false;
}

void TestVersion3Hub()
{
    DataCopy copy;
    ReplaceInFile(copy.Path / "maps" / "fixture_b.json", "\"hub\": false", "\"hub\": true");
    Database data(copy.Path);
    DataError error;
    Check(data.Load(&error), "a hub map loads");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    if (!data.IsLoaded())
        return;
    const MapDef* hub = data.FindMap("fixture_b");
    Check(hub != nullptr && hub->Hub, "fixture_b is the hub");

    TempDir dir("ascension-save-v3-hub");
    Emerald::SaveData older;
    older.SetString("class", "fighter");
    older.SetInt("xp", 8);
    older.SetInt("gold", 4);
    older.SetInt("position", 0);
    older.SetString("standing", "victory");
    older.SetInt("defeated.count", 1);
    older.SetString("defeated.0", "wolf");
    older.SetInt("found.count", 1);
    older.SetString("found.0", "alphas_fang");
    Emerald::SaveSystem version3(dir.Path, 3);
    Check(version3.Save("run", older, "v3"), "a version 3 file is written");

    RunSave saves(dir.Path);
    RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.FileVersion == 3 && loaded.Run.LegacyIndex == 0 &&
              loaded.Run.Found.size() == 1 && loaded.Run.Found[0] == "alphas_fang",
          "version 3 migrates and keeps the fang");
    AdoptLegacyMap(data, loaded.Run);
    Check(loaded.Run.LegacyIndex < 0 && loaded.Run.EncounterId == "wolf" &&
              loaded.Run.MapId == "fixture_b" && loaded.Run.Tile.X == hub->Start.X &&
              loaded.Run.Tile.Y == hub->Start.Y && HoldsId(loaded.Run.Cleared, "fixture_spawn") &&
              !HoldsId(loaded.Run.Cleared, "fixture_hold") && loaded.Run.Found.size() == 1 &&
              loaded.Run.Found[0] == "alphas_fang",
          "the hero stands on the hub start, and the once-spawn of a defeated encounter is cleared");

    std::string why;
    Check(saves.Write(loaded.Run, why), "the migrated run is written as version 4");
    std::ifstream in(dir.Path / "run.sav");
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Check(text.find("version = 4") != std::string::npos && text.find("position =") == std::string::npos &&
              text.find("legacy.index") == std::string::npos &&
              text.find("alphas_fang") != std::string::npos,
          "the new file is version 4, with spawn ids and the fang, and no list index");
}

void TestFixtureV3()
{
    const fs::path fixture = ASCENSION_SAVE_FIXTURE;
    TempDir dir("ascension-save-fixture-v3");
    std::error_code copyError;
    fs::copy_file(fixture, dir.Path / "run.sav", copyError);
    Check(!copyError, "the version 3 fixture is copied");
    if (copyError)
        return;
    RunSave saves(dir.Path);
    RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.FileVersion == 3 && loaded.Run.LegacyIndex == 1 &&
              loaded.Run.Found.size() == 1 && loaded.Run.Found[0] == "alphas_fang" &&
              loaded.Run.Flags.size() == 1 && loaded.Run.Flags[0] == "met",
          "the fixture version 3 file migrates and keeps the fang");
    if (!loaded)
        std::printf("    %s\n", loaded.Message.c_str());
    Database data = Sample();
    if (!data.IsLoaded())
        return;
    AdoptLegacyMap(data, loaded.Run);
    Hero hero = StartHero(data);
    Inventory bag = StartBag(data);
    std::string why;
    Check(Restore(hero, bag, loaded.Run, why), "the fixture career loads");
    Check(hero.ClassId() == "fighter" && hero.Xp() == 8 && bag.Gold() == 4 &&
              loaded.Run.EncounterId == "bandit" && loaded.Run.MapId == "fixture_a" &&
              loaded.Run.Tile.X == 1 && loaded.Run.Tile.Y == 1 &&
              HoldsId(loaded.Run.Cleared, "fixture_spawn") &&
              !HoldsId(loaded.Run.Cleared, "fixture_hold") && loaded.Run.LegacyIndex < 0,
          "the fixture stands on the first map's start with the wolf spawn cleared");
    Check(saves.Write(loaded.Run, why), "the fixture is saved as version 4");
    const RunLoad again = saves.Read();
    Check(static_cast<bool>(again) && again.FileVersion == RunSave::kVersion &&
              again.Run.Found.size() == 1 && again.Run.Found[0] == "alphas_fang" &&
              HoldsId(again.Run.Cleared, "fixture_spawn") && again.Run.LegacyIndex < 0,
          "saving the fixture does not drop the fang or the cleared id");
}

void TestSpawnIds()
{
    DataCopy copy;
    ReplaceInFile(copy.Path / "maps" / "fixture_a.json",
                  "\"spawns\": [\n"
                  "      {\n"
                  "        \"id\": \"fixture_spawn\",\n"
                  "        \"tile\": { \"x\": 4, \"y\": 1 },\n"
                  "        \"encounter\": \"wolf\"\n"
                  "      }\n"
                  "    ]",
                  "\"spawns\": [\n"
                  "      {\n"
                  "        \"id\": \"fixture_extra\",\n"
                  "        \"tile\": { \"x\": 4, \"y\": 1 },\n"
                  "        \"encounter\": \"bandit\"\n"
                  "      },\n"
                  "      {\n"
                  "        \"id\": \"fixture_spawn\",\n"
                  "        \"tile\": { \"x\": 1, \"y\": 2 },\n"
                  "        \"encounter\": \"wolf\"\n"
                  "      }\n"
                  "    ]");
    Database data(copy.Path);
    DataError error;
    Check(data.Load(&error), "a reordered spawn list loads");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    if (!data.IsLoaded())
        return;
    const MapDef* map = data.FindMap("fixture_a");
    const MapSpawnRef spawn = data.FindSpawn("fixture_spawn");
    const MapSpawnRef missing = data.FindSpawn("missing_spawn");
    Check(map != nullptr && map->Spawns.size() == 2 && map->Spawns[0].Id == "fixture_extra" &&
              spawn.Spawn != nullptr && spawn.Spawn->Tile.X == 1 && spawn.Spawn->Tile.Y == 2 &&
              missing.Spawn == nullptr,
          "the saved id is not the first spawn, and a removed id is absent");

    Hero hero = StartHero(data);
    RunState run = Capture(hero, StartBag(data));
    run.MapId = "fixture_a";
    run.Tile.X = 2;
    run.Tile.Y = 1;
    run.Cleared.push_back("fixture_spawn");
    run.Cleared.push_back("missing_spawn");
    FledWounds wounds;
    wounds.SpawnId = "missing_spawn";
    FoeWound foe;
    foe.Id = "wolf";
    foe.Hp = 9;
    wounds.Foes.push_back(foe);
    run.Wounds.push_back(std::move(wounds));
    run.Found.push_back("alphas_fang");
    std::string why;
    TempDir dir("ascension-save-spawn-ids");
    RunSave saves(dir.Path);
    Check(saves.Write(run, why), "cleared ids and an unknown wound are written");
    const RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.Run.Cleared.size() == 2 &&
              loaded.Run.Cleared[0] == "fixture_spawn" && loaded.Run.Cleared[1] == "missing_spawn" &&
              loaded.Run.Wounds.size() == 1 && loaded.Run.Wounds[0].SpawnId == "missing_spawn" &&
              loaded.Run.Wounds[0].Foes.size() == 1 && loaded.Run.Wounds[0].Foes[0].Hp == 9 &&
              loaded.Run.Found.size() == 1 && loaded.Run.Found[0] == "alphas_fang" &&
              loaded.Run.MapId == "fixture_a" && loaded.Run.Tile.X == 2 && loaded.Run.Tile.Y == 1,
          "reordering spawns leaves the cleared id, and an unknown id still loads");

    TempDir brokenDir("ascension-save-empty-spawn");
    Emerald::SaveData broken;
    broken.SetString("class", "fighter");
    broken.SetInt("cleared.count", 1);
    broken.SetString("cleared.0", "");
    Emerald::SaveSystem version4(brokenDir.Path, 4);
    Check(version4.Save("run", broken, "empty"), "a version 4 file with an empty spawn id is written");
    RunSave brokenSave(brokenDir.Path);
    const RunLoad bad = brokenSave.Read();
    Check(!bad && bad.Error == RunSaveError::BadData, "an empty spawn id is damaged, not a crash");
}

void TestFoundOptional()
{
    TempDir dir("ascension-save-found-absent");
    Emerald::SaveData older;
    older.SetString("class", "fighter");
    Emerald::SaveSystem version3(dir.Path, 3);
    Check(version3.Save("run", older, "plain"), "a version 3 file without found is written");
    RunSave saves(dir.Path);
    const RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.FileVersion == 3 && loaded.Run.Found.empty(),
          "a version 3 file with no found list has claimed nothing");
}

} // namespace

int main()
{
    // Emerald logs a warning when a save is damaged and the backup is used. Tests do not start
    // the application, so the loggers have to exist before that path runs.
    Emerald::Log::Init({});
    TestRoundTrip();
    TestCustomStats();
    TestMigration();
    TestMidFight();
    TestVersion2Restarts();
    TestCorruptFallsBack();
    TestAiSnapshot();
    TestFangAnyClass();
    TestFangDrop();
    TestGenericDrop();
    TestVictoryHp();
    TestVersion3Hub();
    TestFixtureV3();
    TestSpawnIds();
    TestFoundOptional();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    Emerald::Log::Shutdown();
    return g_Failures == 0 ? 0 : 1;
}
