// Playtest flags: argument parsing, unknown ids, and a --data folder that hot-reloads.
// No window or GPU.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_scancode.h>

#include "Check.h"
#include "Playtest.h"

namespace {

using namespace Ascension;

namespace fs = std::filesystem;

const fs::path kSample = ASCENSION_DATA_DIR;

struct TempCopy {
    fs::path Path;

    TempCopy()
    {
        Path = fs::temp_directory_path() / "ascension-playtest-tests";
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

bool Parse(std::initializer_list<std::string_view> args, PlaytestOptions& options,
           std::string& error)
{
    std::vector<std::string_view> full;
    full.emplace_back("Ascension");
    full.insert(full.end(), args.begin(), args.end());
    return ParsePlaytestOptions(full, options, error);
}

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

void Reload(Database& data)
{
    DataError error;
    Check(data.CheckForChanges(&error) == 0, "the first sight of a change waits");
    Check(data.CheckForChanges(&error) == 1, "a settled write reloads");
}

i32 CountOf(const Inventory& bag, std::string_view id)
{
    i32 total = 0;
    for (i32 slot = 0; slot < bag.BagSlots(); ++slot) {
        if (bag.BagAt(slot).Id == id)
            total += bag.BagAt(slot).Count;
    }
    return total;
}

i32 IndexOf(const Database& data, std::string_view id)
{
    const auto roster = data.GetEnemies();
    for (i32 rosterIndex = 0; rosterIndex < static_cast<i32>(roster.size()); ++rosterIndex) {
        if (roster[static_cast<usize>(rosterIndex)].Id == id)
            return rosterIndex;
    }
    return -1;
}

i32 EncounterOf(const Database& data, std::string_view id)
{
    const auto encounters = data.GetEncounters();
    for (i32 encounterIndex = 0; encounterIndex < static_cast<i32>(encounters.size());
         ++encounterIndex) {
        if (encounters[static_cast<usize>(encounterIndex)].Id == id)
            return encounterIndex;
    }
    return -1;
}

Database Sample()
{
    Database data(kSample);
    DataError error;
    Check(data.Load(&error), "sample data loads");
    return data;
}

void TestParseExampleAndEngineFlags()
{
    PlaytestOptions options;
    std::string why;
    Check(Parse({"--data", "playtest-data", "--level", "6", "--class", "berserker", "--enemy",
                 "oathkeeper_varn", "--keys", "1 1 3 1", "--transcript", "varn.txt", "--screenshot",
                 "varn.png"},
                options, why),
          "the Varn command parses");
    Check(why.empty(), "a good command has no error");
    Check(options.DataDir == "playtest-data", "--data keeps the folder");
    Check(options.Level == 6, "--level 6");
    Check(options.ClassId == "berserker", "--class berserker");
    Check(options.Fight.Id == "oathkeeper_varn", "--enemy keeps the id for ResolveFight");
    Check(options.Transcript == "varn.txt", "--transcript keeps the file");
    Check(options.Keys.size() == 4 && options.Keys[0] == SDL_SCANCODE_1 &&
              options.Keys[1] == SDL_SCANCODE_1 && options.Keys[2] == SDL_SCANCODE_3 &&
              options.Keys[3] == SDL_SCANCODE_1,
          "keys are one scancode per name");

    PlaytestOptions mixed;
    Check(Parse({"--frames", "9", "--screenshot=shot.png", "--profile", "--level=4", "--capture",
                 "frames", "--capture-fps", "30", "--record", "rec.bin", "--replay", "rec.bin",
                 "--profile-trace", "trace.json", "--gpu", "vulkan", "--gold", "40"},
                mixed, why),
          "Emerald flags are left for the engine");
    Check(mixed.Level == 4 && mixed.Gold == 40 && mixed.ClassId.empty() && mixed.Fight.Id.empty(),
          "an engine value is not read as a playtest flag");

    PlaytestOptions keys;
    Check(Parse({"--keys", "8 c e u x i r n space up down left right escape"}, keys, why),
          "the README key names parse");
    Check(keys.Keys.size() == 14 && keys.Keys[0] == SDL_SCANCODE_8 &&
              keys.Keys[1] == SDL_SCANCODE_C && keys.Keys[8] == SDL_SCANCODE_SPACE &&
              keys.Keys[9] == SDL_SCANCODE_UP && keys.Keys[12] == SDL_SCANCODE_RIGHT &&
              keys.Keys[13] == SDL_SCANCODE_ESCAPE,
          "letters, space, arrows, and escape map to scancodes");

    PlaytestOptions badKey;
    Check(!Parse({"--keys", "1 jump"}, badKey, why), "an unknown key fails");
    CheckHas(why, "jump", "the unknown key is named");

    const fs::path keysPath = fs::temp_directory_path() / "ascension-playtest-keys.txt";
    WriteFile(keysPath, "1\n1 c\n");
    PlaytestOptions fromFile;
    Check(Parse({"--keys-file", keysPath.string()}, fromFile, why), "a keys file parses");
    Check(fromFile.Keys.size() == 3 && fromFile.Keys[2] == SDL_SCANCODE_C,
          "the file is whitespace");
    Check(!Parse({"--keys", "1", "--keys-file", keysPath.string()}, fromFile, why),
          "keys and a keys file together fail");
    CheckHas(why, "--keys", "the message says to pass one of the two");
    std::error_code removeError;
    fs::remove(keysPath, removeError);

    PlaytestOptions items;
    Check(Parse({"--give=minor_potion:2, focus_tonic", "--equip", "rusty_sword,oak_shield"}, items,
                why),
          "give and equip parse");
    Check(items.Give.size() == 2 && items.Give[0].Id == "minor_potion" &&
              items.Give[0].Count == 2 && items.Give[1].Id == "focus_tonic" &&
              items.Give[1].Count == 1,
          "a missing count is 1");
    Check(items.Equip.size() == 2 && items.Equip[0] == "rusty_sword" &&
              items.Equip[1] == "oak_shield",
          "equip is a list of ids");

    Check(!Parse({"--give", "minor_potion:0"}, items, why), "a zero count fails");
    Check(!Parse({"--level"}, items, why), "a missing level fails");
    Check(!Parse({"--level", "6", "--level", "7"}, items, why), "a repeated level fails");
    Check(!Parse({"--bogus"}, items, why), "an unknown flag fails");
    CheckHas(why, "--bogus", "the unknown flag is named");
    Check(!Parse({"leftover"}, items, why), "a positional argument fails");
}

void TestBadIds()
{
    Database data = Sample();
    Hero hero;
    Inventory bag;
    PlaytestSetup setup;
    std::string why;

    PlaytestOptions foe;
    foe.Fight.Id = "no_such_foe";
    Check(!ApplyPlaytest(hero, bag, data, foe, setup, why), "an unknown enemy fails");
    CheckHas(why, "no_such_foe", "the unknown enemy id is named");

    ResolvedFight resolved;
    Check(!ResolveFight(data, foe.Fight, resolved, why), "ResolveFight rejects the same id");
    CheckHas(why, "no_such_foe", "ResolveFight names the id");

    foe.Fight.Id = "wolf_pack";
    Check(ResolveFight(data, foe.Fight, resolved, why), "the wolf pack resolves");
    Check(resolved.Kind == FightKind::Encounter && resolved.Id == "wolf_pack" &&
              resolved.EnemyIndex == EncounterOf(data, "wolf_pack") && resolved.EnemyIndex >= 0,
          "--enemy wolf_pack is the encounter, not a missing enemy");

    foe.Fight.Id = "oathkeeper_varn";
    Check(ResolveFight(data, foe.Fight, resolved, why), "Varn resolves");
    Check(resolved.Kind == FightKind::Encounter &&
              resolved.EnemyIndex == EncounterOf(data, "oathkeeper_varn") &&
              resolved.EnemyIndex != IndexOf(data, "oathkeeper_varn"),
          "an id that is both an encounter and an enemy is the encounter");

    foe.Fight.Id = "wolf";
    Check(ResolveFight(data, foe.Fight, resolved, why) && resolved.Kind == FightKind::Encounter &&
              resolved.EnemyIndex == EncounterOf(data, "wolf"),
          "wolf is the encounter, which is the single wolf");

    foe.Fight.Id = "rubble_shard";
    Check(ResolveFight(data, foe.Fight, resolved, why), "a summoned foe with no encounter resolves");
    Check(resolved.Kind == FightKind::Enemy && resolved.Id == "rubble_shard" &&
              resolved.EnemyIndex == IndexOf(data, "rubble_shard"),
          "a roster id that is not an encounter stays a single enemy");

    Check(MarkedTargetLine("Wolf 2") == "Target: Wolf 2.", "a target change names the enemy");
    Check(MarkedTargetLine("Rubble Shard 2") == "Target: Rubble Shard 2.",
          "a numbered summon keeps that name in the target line");

    PlaytestOptions klass;
    klass.ClassId = "no_such_class";
    Check(!ApplyPlaytest(hero, bag, data, klass, setup, why), "an unknown class fails");
    CheckHas(why, "no_such_class", "the unknown class id is named");

    PlaytestOptions early;
    early.ClassId = "berserker";
    Check(!ApplyPlaytest(hero, bag, data, early, setup, why),
          "berserker at level 1 is not offered");
    CheckHas(why, "berserker", "the class that is not offered is named");

    PlaytestOptions gift;
    PlaytestItem missing;
    missing.Id = "no_such_item";
    missing.Count = 2;
    gift.Give.push_back(missing);
    Check(!ApplyPlaytest(hero, bag, data, gift, setup, why), "an unknown give id fails");
    CheckHas(why, "no_such_item", "the unknown item id is named");

    PlaytestOptions worn;
    worn.Equip.push_back("no_such_gear");
    Check(!ApplyPlaytest(hero, bag, data, worn, setup, why), "an unknown equip id fails");
    CheckHas(why, "no_such_gear", "the unknown gear id is named");

    PlaytestOptions drink;
    drink.Equip.push_back("minor_potion");
    Check(!ApplyPlaytest(hero, bag, data, drink, setup, why), "a consumable is not gear");
    CheckHas(why, "minor_potion", "the consumable id is named");

    PlaytestOptions path;
    path.ChooseId = "no_such_path";
    Check(!ApplyPlaytest(hero, bag, data, path, setup, why), "an unknown choice fails");
    CheckHas(why, "no_such_path", "the unknown choice id is named");

    PlaytestOptions base;
    base.ChooseId = "fighter";
    Check(!ApplyPlaytest(hero, bag, data, base, setup, why), "a base class is not an evolution");
    CheckHas(why, "fighter", "the class that is not an evolution is named");

    PlaytestOptions past;
    past.Level = 11;
    Check(!ApplyPlaytest(hero, bag, data, past, setup, why), "a level past the curve fails");
    CheckHas(why, "11", "the level is named");

    PlaytestOptions purse;
    purse.Gold = 1000000;
    Check(!ApplyPlaytest(hero, bag, data, purse, setup, why), "gold above the cap fails");
    CheckHas(why, "1000000", "the gold amount is named");

    PlaytestOptions hands;
    hands.Equip.push_back("notched_greataxe");
    hands.Equip.push_back("oak_shield");
    Check(!ApplyPlaytest(hero, bag, data, hands, setup, why), "a shield on a greataxe fails");
    CheckHas(why, "oak_shield", "the shield that will not fit is named");

    const fs::path missingDir = fs::temp_directory_path() / "ascension-playtest-missing";
    const std::string missingName = missingDir.string();
    PlaytestOptions folder;
    folder.DataDir = missingDir;
    Check(!CheckPlaytestStartup(folder, why), "a missing --data folder fails");
    CheckHas(why, missingName.c_str(), "the missing folder is named");
}

void TestVarnSetupAndChoose()
{
    Database data = Sample();
    Hero hero;
    Inventory bag;
    PlaytestSetup setup;
    std::string why;

    PlaytestOptions options;
    options.Level = 6;
    options.ClassId = "berserker";
    options.Fight.Id = "oathkeeper_varn";
    options.Gold = 40;
    PlaytestItem potion;
    potion.Id = "minor_potion";
    potion.Count = 2;
    options.Give.push_back(potion);
    PlaytestItem tonic;
    tonic.Id = "focus_tonic";
    tonic.Count = 1;
    options.Give.push_back(tonic);
    options.Equip.push_back("rusty_sword");
    options.Equip.push_back("oak_shield");
    Check(ApplyPlaytest(hero, bag, data, options, setup, why), "a level-6 berserker is ready");
    Check(why.empty(), "setup leaves no error");
    Check(hero.Level() == 6 && hero.ClassId() == "berserker" && !hero.ChoicePending(),
          "the path is already chosen, so the fight may start");
    Check(setup.EnemyIndex == EncounterOf(data, "oathkeeper_varn") && setup.DirectEnemy.empty(),
          "Varn is that encounter, not the roster slot");
    Check(bag.Gold() == 40, "gold is the amount from --gold");
    Check(CountOf(bag, "minor_potion") == 2 && CountOf(bag, "focus_tonic") == 1,
          "give puts stacks in the bag");
    Check(bag.Equipped(GearSlot::Weapon) == "rusty_sword" &&
              bag.Equipped(GearSlot::Offhand) == "oak_shield",
          "equip wears the sword and the shield");

    Hero choosing;
    Inventory emptyBag;
    PlaytestSetup choice;
    PlaytestOptions open;
    open.Level = 6;
    open.ChooseId = "berserker";
    Check(ApplyPlaytest(choosing, emptyBag, data, open, choice, why),
          "level 6 with --choose waits for the pick");
    Check(choosing.ClassId() == "fighter" && choosing.ChoicePending(),
          "the choice is open and the class is still fighter");
    Check(PickEvolution(choosing, open.ChooseId, why) == EvolutionPick::Chosen,
          "--choose takes berserker when the choice opens");
    Check(choosing.ClassId() == "berserker" && !choosing.ChoicePending(),
          "the auto-pick closes the choice");
    Check(PickEvolution(choosing, open.ChooseId, why) == EvolutionPick::Waiting,
          "a second pick waits until a choice is open again");

    Hero later;
    Check(later.Start(data, why), "a fresh fighter starts");
    Check(PickEvolution(later, "berserker", why) == EvolutionPick::Waiting,
          "--choose does nothing before level 5");
    later.GrantXp(60);
    Check(PickEvolution(later, "no_such_path", why) == EvolutionPick::Failed,
          "a bad pick fails once the choice is open");
    CheckHas(why, "no_such_path", "that failure names the id");
    Check(later.ClassId() == "fighter", "a failed pick leaves the fighter");
}

void TestWolfPackAndLoneEnemy()
{
    Database data = Sample();
    Hero hero;
    Inventory bag;
    PlaytestSetup setup;
    std::string why;

    PlaytestOptions pack;
    pack.Fight.Id = "wolf_pack";
    Check(ApplyPlaytest(hero, bag, data, pack, setup, why), "the wolf pack is a legal start");
    Check(setup.EnemyIndex == EncounterOf(data, "wolf_pack") && setup.DirectEnemy.empty(),
          "startup keeps the encounter index and does not treat the pack as one enemy");

    Hero shardHero;
    Inventory shardBag;
    PlaytestSetup shard;
    PlaytestOptions lone;
    lone.Fight.Id = "rubble_shard";
    Check(ApplyPlaytest(shardHero, shardBag, data, lone, shard, why),
          "a roster foe with no encounter is a legal start");
    Check(shard.DirectEnemy == "rubble_shard" && shard.EnemyIndex == 0,
          "that foe is started by id, and the encounter cursor stays on the first fight");

    PlaytestOptions missing;
    missing.Fight.Id = "no_such_pack";
    Check(!CheckPlaytestStartup(missing, why), "an unknown --enemy fails before the window");
    CheckHas(why, "no_such_pack", "startup names the unknown id");
}

void TestDataFolderHotReload()
{
    TempCopy copy;
    PlaytestOptions options;
    options.DataDir = copy.Path;
    Check(PlaytestDataDirectory(options) == copy.Path, "--data selects that folder");
    PlaytestOptions none;
    Check(PlaytestDataDirectory(none) == kSample, "no flag keeps the repo data folder");

    Database data(PlaytestDataDirectory(options));
    DataError error;
    Check(data.Load(&error), "--data loads");
    Check(data.GetDirectory() == fs::absolute(copy.Path) || data.GetDirectory() == copy.Path,
          "the database is pointed at the --data folder");
    const EnemyDef* wolf = data.FindEnemy("wolf");
    Check(wolf != nullptr && wolf->Experience == 8, "the copied wolf still grants 8 XP");

    ReplaceInFile(copy.Path / "enemies.json", "\"experience\": 8", "\"experience\": 11");
    Reload(data);
    const EnemyDef* reloaded = data.FindEnemy("wolf");
    Check(reloaded != nullptr && reloaded->Experience == 11,
          "saving a file in the --data folder hot-reloads");

    std::string why;
    Check(CheckPlaytestStartup(options, why), "startup accepts the copied folder");
}

void TestStartupFailureText()
{
    Database data = Sample();
    Hero hero;
    Inventory bag;
    PlaytestSetup setup;
    std::string why;

    PlaytestOptions pending;
    pending.Level = 5;
    pending.ClassId = "fighter";
    Check(ApplyPlaytest(hero, bag, data, pending, setup, why), "level 5 fighter sets up");
    Check(why.empty() && hero.Level() == 5 && hero.ClassId() == "fighter" && hero.ChoicePending(),
          "the path is still open after setup");
    const char* block = hero.NextFightBlock();
    Check(block != nullptr, "the next fight is blocked");
    const std::string shown = StartupFailureText("", "", block);
    Check(shown == block, "the window uses the fight-block sentence");
    Check(shown != "Could not load data/.", "a pending path is not a load failure");

    const char* load = "classes.json: class 'berserker': skill 'smash' does not exist";
    Check(StartupFailureText("", load, block) == load, "a load error keeps the file and the field");
    const char* start = "enemy 'no_such_foe' does not exist";
    Check(StartupFailureText(start, load, block) == start,
          "a start error stays ahead of a load error");
    Check(StartupFailureText("", "", nullptr) == "Could not load data/.",
          "an empty failure still says the data could not be loaded");
    Check(StartupFailureText("", "", "") == "Could not load data/.",
          "an empty fight block is not a sentence");

    PlaytestOptions command;
    command.Level = 5;
    command.ClassId = "fighter";
    Check(CheckPlaytestStartup(command, why) && why.empty(),
          "level 5 fighter still passes the check that runs before the window");

    Check(hero.ResetBuild(why) && hero.ChoosingStarter(), "a reset asks for a class");
    const char* classBlock = hero.NextFightBlock();
    Check(classBlock != nullptr && StartupFailureText("", "", classBlock) == classBlock,
          "a class choice keeps its own sentence");
    Check(hero.ChooseStarter("fighter", why) && hero.ClassPool() > 0, "the pool is waiting");
    const char* poolBlock = hero.NextFightBlock();
    Check(poolBlock != nullptr && std::string(poolBlock) != classBlock &&
              StartupFailureText("", "", poolBlock) == poolBlock,
          "unspent class points keep their own sentence");

    TempCopy copy;
    WriteFile(copy.Path / "classes.json", "{ not json");
    PlaytestOptions broken;
    broken.DataDir = copy.Path;
    Check(!CheckPlaytestStartup(broken, why), "broken data fails before the window");
    CheckHas(why, "classes.json", "that failure names the file");
    CheckHas(why, "not valid JSON", "that failure says the JSON is broken");
    Check(StartupFailureText("", why, block) == why, "the window would keep that load error");

    const fs::path missingDir = fs::temp_directory_path() / "ascension-playtest-missing-panel";
    const std::string missingName = missingDir.string();
    PlaytestOptions folder;
    folder.DataDir = missingDir;
    Check(!CheckPlaytestStartup(folder, why), "a missing folder fails before the window");
    CheckHas(why, missingName.c_str(), "the missing folder is named");
    Check(StartupFailureText(why, "", block) == why, "the window would keep the folder sentence");
}

} // namespace

int main()
{
    TestParseExampleAndEngineFlags();
    TestBadIds();
    TestVarnSetupAndChoose();
    TestWolfPackAndLoneEnemy();
    TestDataFolderHotReload();
    TestStartupFailureText();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
