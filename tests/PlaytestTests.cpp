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
#include "FightSim.h"
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

    PlaytestOptions wasd;
    Check(Parse({"--keys", "w a s d"}, wasd, why), "map step keys parse");
    Check(wasd.Keys.size() == 4 && wasd.Keys[0] == SDL_SCANCODE_W &&
              wasd.Keys[1] == SDL_SCANCODE_A && wasd.Keys[2] == SDL_SCANCODE_S &&
              wasd.Keys[3] == SDL_SCANCODE_D,
          "w a s d are the map step keys");

    PlaytestOptions map;
    Check(Parse({"--map", "fixture_a", "--keys", "right right right down"}, map, why),
          "--map parses with a key script");
    Check(map.MapId == "fixture_a" && map.Keys.size() == 4 && map.DefinesRun() && map.UsesData() &&
              !map.LoadsSave(),
          "--map names the map and does not open the per-user save");
    PlaytestOptions mapped;
    Check(Parse({"--map", "fixture_a", "--save-dir", "playtest-saves"}, mapped, why),
          "--map with a save folder parses");
    Check(mapped.DefinesRun() && mapped.LoadsSave() && mapped.SaveDir == "playtest-saves",
          "--map with a save folder resumes that folder");
    Check(!Parse({"--map"}, map, why), "--map needs an id");
    Check(!Parse({"--map", "fixture_a", "--map", "fixture_b"}, map, why), "a repeated --map fails");
    Check(!Parse({"--map", "fixture_a", "--enemy", "wolf"}, map, why),
          "--map and --enemy together fail");
    CheckHas(why, "--map", "the message names --map");
    Check(!Parse({"--map", "fixture_a", "--sim", "--enemy", "wolf"}, map, why),
          "--map and --sim together fail");

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
    PlaytestOptions traced;
    Check(Parse({"--trace", "--keys", "1"}, traced, why), "--trace parses");
    Check(traced.Trace && traced.Keys.size() == 1, "--trace is a switch and keeps the keys");
    Check(!Parse({"--trace", "1"}, traced, why), "--trace does not take a value");
    CheckHas(why, "unexpected argument", "a value after --trace is a leftover argument");

    PlaytestOptions saved;
    Check(Parse({"--save-dir", "playtest-saves", "--keys", "f5 escape"}, saved, why),
          "--save-dir and f5 parse");
    Check(saved.SaveDir == "playtest-saves" && saved.Keys.size() == 2 &&
              saved.Keys[0] == SDL_SCANCODE_F5 && saved.Keys[1] == SDL_SCANCODE_ESCAPE,
          "the save folder and the save key are kept");
    Check(!saved.DefinesRun(), "a save-dir command does not build a hero");
    PlaytestOptions built;
    built.Level = 1;
    Check(built.DefinesRun(), "a level command builds a hero and skips the save");
    Check(!Parse({"--save-dir"}, saved, why), "a missing save directory fails");

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

    PlaytestOptions missingMap;
    missingMap.MapId = "no_such_map";
    Check(!ApplyPlaytest(hero, bag, data, missingMap, setup, why), "an unknown map fails");
    CheckHas(why, "no_such_map", "the unknown map id is named");

    PlaytestOptions knownMap;
    knownMap.MapId = "fixture_a";
    Check(ApplyPlaytest(hero, bag, data, knownMap, setup, why), "fixture_a is a map id");

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
    Check(ResolveFight(data, foe.Fight, resolved, why),
          "a summoned foe with no encounter resolves");
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

void TestPickFlag()
{
    PlaytestOptions parsed;
    std::string why;
    Check(Parse({"--pick", "riposte", "--pick=rally"}, parsed, why), "--pick repeats");
    Check(parsed.PickIds.size() == 2 && parsed.PickIds[0] == "riposte" &&
              parsed.PickIds[1] == "rally",
          "the ids stay in the order they were passed");
    Check(parsed.UsesData(), "a pick alone asks for data");
    Check(!Parse({"--pick"}, parsed, why), "a bare --pick fails");
    CheckHas(why, "--pick needs a value", "a missing value is named");
    Check(!Parse({"--pick="}, parsed, why), "an empty --pick fails");
    CheckHas(why, "--pick needs an id", "an empty id is named");

    PlaytestOptions unknown;
    unknown.PickIds.push_back("no_such_skill");
    Check(!CheckPlaytestStartup(unknown, why), "an unknown --pick fails before the window");
    CheckHas(why, "no_such_skill", "the unknown skill is named");
    CheckHas(why, "does not exist", "the error says the skill does not exist");

    PlaytestOptions idle;
    idle.Level = 6;
    idle.ClassId = "guardian";
    idle.PickIds.push_back("riposte");
    Check(CheckPlaytestStartup(idle, why) && why.empty(),
          "a skill id is accepted when no pick is open yet");

    TempCopy copy;
    ReplaceInFile(copy.Path / "classes.json",
                  "\"skills\": [\"shield_wall\", \"riposte\"],\n      \"passives\": [\"bulwark\"]",
                  "\"skills\": [\"shield_wall\"],\n      \"passives\": [\"bulwark\"],\n"
                  "      \"picks\": [\n"
                  "        { \"level\": 6, \"options\": [\"riposte\", \"cleave\"] }\n"
                  "      ]");
    PlaytestOptions refused;
    refused.DataDir = copy.Path;
    refused.Level = 6;
    refused.ClassId = "guardian";
    refused.PickIds.push_back("bite");
    Check(!CheckPlaytestStartup(refused, why), "a skill the open pick does not offer fails");
    CheckHas(why, "bite", "that failure names the skill");

    PlaytestOptions accepted = refused;
    accepted.PickIds = {"riposte"};
    Check(CheckPlaytestStartup(accepted, why) && why.empty(),
          "an offered skill passes the startup check");

    Database data(copy.Path);
    DataError error;
    Check(data.Load(&error), "the pick folder loads");
    Hero hero;
    Inventory bag;
    PlaytestSetup setup;
    Check(ApplyPlaytest(hero, bag, data, accepted, setup, why), "level 6 guardian is ready");
    Check(hero.Level() == 6 && hero.ClassId() == "guardian" && hero.PickPending() &&
              hero.PickedIds().empty(),
          "startup validates --pick without keeping it");
    Check(PickSkillOption(hero, "riposte", why) == SkillOptionPick::Chosen, "--pick keeps riposte");
    Check(!hero.PickPending() && hero.PickedIds().size() == 1 && hero.PickedIds()[0] == "riposte",
          "the kept id is riposte");
    Check(PickSkillOption(hero, "cleave", why) == SkillOptionPick::Waiting,
          "a later --pick waits until another pick is open");

    Hero early;
    Check(early.Start(data, why), "a fresh fighter starts");
    Check(PickSkillOption(early, "riposte", why) == SkillOptionPick::Waiting,
          "--pick does nothing before the pick level");
    early.GrantXp(62);
    Check(early.Choose("guardian", why), "guardian is chosen at level 5");
    Check(PickSkillOption(early, "riposte", why) == SkillOptionPick::Waiting,
          "level 5 is still before the pick");
    early.GrantXp(90 - 62);
    Check(early.Level() == 6 && early.PickPending(), "level 6 opens the pick");
    Check(PickSkillOption(early, "bite", why) == SkillOptionPick::Failed,
          "an id outside the open pick fails");
    CheckHas(why, "bite", "the failure names the id");
    Check(early.PickPending() && early.ClassId() == "guardian", "a failed pick leaves the choice");
    Check(PickSkillOption(early, "cleave", why) == SkillOptionPick::Chosen, "cleave is kept");
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

void TestNextFightAndScriptedKeys()
{
    Check(NextFightAllowed(FightStanding::None), "N advances before the first fight");
    Check(NextFightAllowed(FightStanding::Victory), "N advances after a victory");
    Check(NextFightAllowed(FightStanding::Defeat), "N advances after a defeat");
    Check(!NextFightAllowed(FightStanding::Ongoing), "N does not advance while a foe is alive");

    ScriptedPressQuery window;
    window.ScriptRunning = true;
    Check(DecideScriptedPress(window) == ScriptedPress::IgnoreWindow,
          "a focused window cannot insert a key into a script");
    window.FromScript = true;
    Check(DecideScriptedPress(window) == ScriptedPress::Play, "the script's own press is played");

    ScriptedPressQuery combat;
    combat.ScriptRunning = true;
    combat.FromScript = true;
    combat.FightOngoing = true;
    combat.PlayerTurn = false;
    combat.NeedsPlayerTurn = true;
    Check(DecideScriptedPress(combat) == ScriptedPress::WaitForTurn,
          "a combat key waits while it is still the foe's turn");
    combat.PlayerTurn = true;
    Check(DecideScriptedPress(combat) == ScriptedPress::Play,
          "a combat key plays on the player's turn");

    ScriptedPressQuery nextFight = combat;
    nextFight.PlayerTurn = false;
    nextFight.NeedsPlayerTurn = false;
    Check(DecideScriptedPress(nextFight) == ScriptedPress::Play,
          "N is delivered even when it is the foe's turn");
    nextFight.FightOngoing = false;
    Check(DecideScriptedPress(nextFight) == ScriptedPress::Play, "N plays once the fight is over");

    ScriptedPressQuery idle;
    Check(DecideScriptedPress(idle) == ScriptedPress::Play, "with no script, a window key plays");
}

// The eight Outer Barrow replays. commands.txt is what the PR body pastes. Each
// --keys-file is the list Linux ran. Brace + Second Wind is 107 words; a longer
// paste is a different list.
void TestOuterBarrowReplayKeys()
{
    struct ReplayKeys {
        const char* File;
        const char* Transcript;
        i32 Words;
    };
    const ReplayKeys rows[] = {
        {"guardian-riposte-rally.keys", "guardian-riposte-rally.txt", 64},
        {"guardian-riposte-rage.keys", "guardian-riposte-rage.txt", 65},
        {"guardian-shield-wall-rally.keys", "guardian-shield-wall-rally.txt", 71},
        {"guardian-shield-wall-rage.keys", "guardian-shield-wall-rage.txt", 72},
        {"berserker-rend-rally.keys", "berserker-rend-rally.txt", 53},
        {"berserker-rend-second-wind.keys", "berserker-rend-second-wind.txt", 53},
        {"berserker-brace-rally.keys", "berserker-brace-rally.txt", 65},
        {"berserker-brace-second-wind.keys", "berserker-brace-second-wind.txt", 107},
    };

    const fs::path root = ASCENSION_SOURCE_DIR;
    const fs::path commandsPath = root / "playtest/outer-barrow/commands.txt";
    std::ifstream commandsIn(commandsPath);
    const std::string commands((std::istreambuf_iterator<char>(commandsIn)),
                               std::istreambuf_iterator<char>());
    Check(commandsIn && !commands.empty(), "the Outer Barrow command list is present");
    Check(commands.find("--keys ") == std::string::npos,
          "the commands read key files and do not inline a list");

    for (const ReplayKeys& row : rows) {
        const fs::path keysPath = root / "playtest/outer-barrow" / row.File;
        const std::string flag = std::string("--keys-file playtest/outer-barrow/") + row.File;
        Check(commands.find(flag) != std::string::npos, "the command names this key file");
        Check(commands.find(row.Transcript) != std::string::npos,
              "the command names this transcript");

        std::string why;
        PlaytestOptions options;
        const bool parsed = Parse({"--keys-file", keysPath.string()}, options, why);
        Check(parsed, "the replay key file parses");
        if (!parsed)
            std::printf("    %s\n", why.c_str());
        if (static_cast<i32>(options.Keys.size()) != row.Words)
            std::printf("    %s has %zu keys, expected %d\n", row.File, options.Keys.size(),
                        row.Words);
        Check(static_cast<i32>(options.Keys.size()) == row.Words,
              "the replay key count is the Linux replay");
    }

    std::ifstream braceIn(root / "playtest/outer-barrow/berserker-brace-second-wind.keys");
    const std::string brace((std::istreambuf_iterator<char>(braceIn)),
                            std::istreambuf_iterator<char>());
    Check(brace.find("5 space 3 3 3 4 5 4") != std::string::npos,
          "Brace + Second Wind opens the Warden with Second Wind, then Rage");
    Check(brace.find("5 space 3 3 3 5 4 4") == std::string::npos,
          "Brace + Second Wind does not swap that Second Wind and Rage");
    i32 braceWords = 0;
    for (usize index = 0; index < brace.size();) {
        while (index < brace.size() && (brace[index] == ' ' || brace[index] == '\t' ||
                                        brace[index] == '\n' || brace[index] == '\r'))
            ++index;
        if (index >= brace.size())
            break;
        ++braceWords;
        while (index < brace.size() && brace[index] != ' ' && brace[index] != '\t' &&
               brace[index] != '\n' && brace[index] != '\r')
            ++index;
    }
    if (braceWords != 107)
        std::printf("    Brace + Second Wind has %d words\n", braceWords);
    Check(braceWords == 107, "Brace + Second Wind is 107 words");
}

// Save, quit, and load. Separate from the eight fight replays so their word counts stay put.
void TestSaveQuitLoadKeys()
{
    struct KeysFile {
        const char* File;
        i32 Words;
    };
    const KeysFile rows[] = {
        {"save-quit.keys", 5},
        {"save-load.keys", 2},
    };
    const fs::path root = ASCENSION_SOURCE_DIR;
    const fs::path commandsPath = root / "playtest/outer-barrow/save-commands.txt";
    std::ifstream commandsIn(commandsPath);
    const std::string commands((std::istreambuf_iterator<char>(commandsIn)),
                               std::istreambuf_iterator<char>());
    Check(commandsIn && !commands.empty(), "the save command list is present");
    Check(commands.find("--keys ") == std::string::npos,
          "the save commands read key files and do not inline a list");
    Check(commands.find("--save-dir") != std::string::npos, "the save commands name a folder");

    for (const KeysFile& row : rows) {
        const fs::path keysPath = root / "playtest/outer-barrow" / row.File;
        const std::string flag = std::string("--keys-file playtest/outer-barrow/") + row.File;
        Check(commands.find(flag) != std::string::npos, "the command names this key file");
        std::string why;
        PlaytestOptions options;
        Check(Parse({"--keys-file", keysPath.string()}, options, why), "the save key file parses");
        Check(static_cast<i32>(options.Keys.size()) == row.Words, "the save key count is stable");
        if (static_cast<i32>(options.Keys.size()) != row.Words)
            std::printf("    %s has %zu keys, expected %d\n", row.File, options.Keys.size(),
                        row.Words);
    }
    Check(commands.find("save-quit") != std::string::npos &&
              commands.find("save-load") != std::string::npos,
          "the commands cover save, quit, and load");
}

// Three Strikes, then the rumor: Down onto "Heard any rumors?", E through that chain, Down onto
// Leave.
void TestPellRumorKeys()
{
    const fs::path root = ASCENSION_SOURCE_DIR;
    const fs::path keysPath = root / "playtest/dialogue/pell-rumor.keys";
    const fs::path commandsPath = root / "playtest/dialogue/commands.txt";
    std::ifstream commandsIn(commandsPath);
    const std::string commands((std::istreambuf_iterator<char>(commandsIn)),
                               std::istreambuf_iterator<char>());
    Check(commandsIn &&
              commands.find("--keys-file playtest/dialogue/pell-rumor.keys") != std::string::npos,
          "the dialogue command names the rumor key file");

    std::string why;
    PlaytestOptions options;
    Check(Parse({"--keys-file", keysPath.string()}, options, why), "the rumor key file parses");
    const SDL_Scancode expect[] = {
        SDL_SCANCODE_1,    SDL_SCANCODE_1,    SDL_SCANCODE_1, SDL_SCANCODE_I,
        SDL_SCANCODE_DOWN, SDL_SCANCODE_E,    SDL_SCANCODE_E, SDL_SCANCODE_E,
        SDL_SCANCODE_E,    SDL_SCANCODE_DOWN, SDL_SCANCODE_E,
    };
    Check(options.Keys.size() == std::size(expect), "the rumor walk is eleven keys");
    if (options.Keys.size() == std::size(expect)) {
        bool same = true;
        for (usize index = 0; index < std::size(expect); ++index) {
            if (options.Keys[index] != expect[index])
                same = false;
        }
        Check(same, "the keys strike, talk, take the rumor, and leave");
    }
}

const SimRateRow* FindRate(const std::vector<SimRateRow>& rows, std::string_view build,
                           std::string_view focus, bool fang)
{
    for (const SimRateRow& row : rows) {
        if (row.Build == build && row.Focus == focus && row.Fang == fang)
            return &row;
    }
    return nullptr;
}

// Alpha's Fang adds strength on top of the charm's agility, and its Bloodlust button
// is spent only when that status shortens the kill. A fang row must still clear, and
// it must not finish with less health or in more turns than the same build without it.
void CheckFangNotWorse(const std::vector<SimRateRow>& rows, const char* fight)
{
    for (const SimRateRow& fang : rows) {
        if (!fang.Fang)
            continue;
        const SimRateRow* plain = FindRate(rows, fang.Build, fang.Focus, false);
        const std::string label = std::string(fight) + " " + fang.Build + " " + fang.Focus;
        Check(plain != nullptr && plain->Ok && fang.Ok && fang.Runs == plain->Runs,
              (label + " has a matching row").c_str());
        if (plain == nullptr || !plain->Ok || !fang.Ok)
            continue;
        Check(plain->Wins == plain->Runs, (label + " still clears without the fang").c_str());
        Check(fang.Wins == fang.Runs && fang.Wins >= plain->Wins,
              (label + " still clears with the fang").c_str());
        Check(fang.SumHp >= plain->SumHp, (label + " fang health is not lower").c_str());
        Check(fang.SumTurns <= plain->SumTurns, (label + " fang does not take more turns").c_str());
    }
}

void TestSimRates()
{
    std::string error;
    PlaytestOptions sim;
    Check(Parse({"--sim", "--enemy", "bear"}, sim, error), "--sim with the bear parses");
    Check(sim.Sim && sim.Fight.Id == "bear" && sim.FocusId.empty() && sim.Runs == 1000 &&
              sim.Seed == 1 && sim.Slip == 0,
          "--sim defaults to 1000 runs, seed 1, and no slip");

    PlaytestOptions focus;
    Check(!Parse({"--focus", "bear"}, focus, error), "--focus without --sim fails");
    Check(error.find("--focus") != std::string::npos, "the message names --focus");

    PlaytestOptions bare;
    Check(!Parse({"--sim"}, bare, error), "--sim without --enemy fails");
    Check(error.find("--enemy") != std::string::npos, "the message says --sim needs --enemy");

    PlaytestOptions both;
    Check(!Parse({"--sim", "--enemy", "bear", "--keys", "1"}, both, error),
          "--sim and --keys together fail");
    Check(error.find("--sim") != std::string::npos && error.find("--keys") != std::string::npos,
          "the message names both flags");

    PlaytestOptions rates;
    Check(Parse({"--sim", "--enemy", "wolf_pack", "--runs", "4", "--seed", "7", "--slip", "15"},
                rates, error),
          "--runs, --seed, and --slip parse with --sim");
    Check(rates.Runs == 4 && rates.Seed == 7 && rates.Slip == 15,
          "the rate flags keep their values");

    PlaytestOptions bareRuns;
    Check(!Parse({"--runs", "4"}, bareRuns, error), "--runs without --sim fails");
    Check(error.find("--runs") != std::string::npos, "the message names --runs");

    PlaytestOptions badSlip;
    Check(!Parse({"--sim", "--enemy", "bear", "--slip", "101"}, badSlip, error),
          "--slip above 100 fails");
    Check(error.find("--slip") != std::string::npos, "the message names --slip");

    PlaytestOptions zeroRuns;
    Check(!Parse({"--sim", "--enemy", "bear", "--runs", "0"}, zeroRuns, error),
          "--runs of 0 fails");

    PlaytestOptions seedZero;
    Check(Parse({"--sim", "--enemy", "bear", "--seed", "0"}, seedZero, error), "--seed 0 parses");
    Check(seedZero.Seed == 1, "--seed 0 is stored as 1");

    const fs::path commandsPath = fs::path(ASCENSION_SOURCE_DIR) / "playtest/bear/commands.txt";
    std::ifstream commandsIn(commandsPath);
    const std::string commands((std::istreambuf_iterator<char>(commandsIn)),
                               std::istreambuf_iterator<char>());
    Check(commandsIn && commands.find("--sim --enemy bear") != std::string::npos &&
              commands.find("--sim --enemy barrow_sentry") != std::string::npos,
          "the bear command file names both headless tables");

    Database data(fs::path(ASCENSION_SOURCE_DIR) / "data");
    DataError loadError;
    Check(data.Load(&loadError), "shipped data loads for the rate sim");
    if (!loadError.Message.empty())
        std::printf("    %s\n", loadError.Message.c_str());
    if (!data.IsLoaded())
        return;

    SimRateQuery query;
    query.EncounterId = "wolf_pack";
    query.Runs = 2;
    query.Seed = 1;
    query.Slip = 0;
    const std::vector<SimRateRow> wolves = SimulateEncounterRates(data, query, error);
    Check(error.empty(), "the wolf rate table builds");
    Check(static_cast<i32>(wolves.size()) == 30, "five builds, three wolf focuses, fang on or off");
    const SimRateRow* dire = FindRate(wolves, "Fighter", "dire wolf", false);
    const SimRateRow* wolfOpen = FindRate(wolves, "Fighter", "default", false);
    Check(dire != nullptr && dire->Ok && dire->Runs == 1, "dire-wolf focus is one fight");
    Check(wolfOpen != nullptr && wolfOpen->Ok && wolfOpen->Runs == 2,
          "wolf default targeting uses the seeds");
    CheckFangNotWorse(wolves, "wolf pack");

    query.EncounterId = "barrow_sentry";
    query.Runs = 5;
    query.Slip = 100;
    const std::vector<SimRateRow> sentry = SimulateEncounterRates(data, query, error);
    Check(error.empty(), "the sentry rate table builds");
    Check(static_cast<i32>(sentry.size()) == 10, "five builds and the fang, one sentry row each");
    for (const SimRateRow& row : sentry)
        Check(row.Ok && row.Runs == 1, "one foe does not change with the seed");
    CheckFangNotWorse(sentry, "sentry");

    query.EncounterId = "bear";
    const std::vector<SimRateRow> bears = SimulateEncounterRates(data, query, error);
    Check(error.empty(), "the bear rate table builds");
    Check(static_cast<i32>(bears.size()) == 10, "five builds and the fang, one bear row each");
    for (const SimRateRow& row : bears)
        Check(row.Ok && row.Runs == 1 && row.Error.empty(), "the bear is one fight");
    CheckFangNotWorse(bears, "bear");

    query.EncounterId = "not_a_fight";
    const std::vector<SimRateRow> missing = SimulateEncounterRates(data, query, error);
    Check(missing.empty() && error.find("not_a_fight") != std::string::npos,
          "an unknown encounter is named in the error");

    const std::string table = FormatSimRates(bears, query);
    Check(table.find("Win%") != std::string::npos && table.find("Bear") != std::string::npos,
          "the rate table names the win rate and the bear");
}

const ChainRow* FindChain(const std::vector<ChainRow>& rows, std::string_view build,
                          std::string_view encounter)
{
    for (const ChainRow& row : rows) {
        if (row.Build == build && row.Encounter == encounter)
            return &row;
    }
    return nullptr;
}

void TestChainCarry()
{
    std::string error;
    PlaytestOptions chain;
    Check(Parse({"--sim", "--chain"}, chain, error), "--sim --chain parses");
    Check(chain.Sim && chain.Chain && chain.Fight.Id.empty(),
          "chain walks the encounter list and does not need --enemy");

    PlaytestOptions bare;
    Check(!Parse({"--chain"}, bare, error), "--chain without --sim fails");
    Check(error.find("--chain") != std::string::npos, "the message names --chain");

    PlaytestOptions both;
    Check(!Parse({"--sim", "--chain", "--enemy", "wolf"}, both, error),
          "--chain and --enemy together fail");
    Check(error.find("--chain") != std::string::npos && error.find("--enemy") != std::string::npos,
          "the message names both flags");

    PlaytestOptions still;
    Check(!Parse({"--sim"}, still, error), "--sim without --enemy still fails");

    Database data(fs::path(ASCENSION_SOURCE_DIR) / "data");
    DataError loadError;
    Check(data.Load(&loadError), "shipped data loads for the chain");
    if (!data.IsLoaded())
        return;

    ChainQuery query;
    const std::vector<ChainRow> rows = SimulateChain(data, query, error);
    Check(error.empty() && !rows.empty(), "the chain builds");
    const ChainRow* wolf = FindChain(rows, "Fighter", "Wolf");
    const ChainRow* bandit = FindChain(rows, "Fighter", "Bandit");
    Check(wolf != nullptr && wolf->Ok && wolf->Won && wolf->StartHp == wolf->MaxHp,
          "the fighter's first fight starts full and wins");
    Check(bandit != nullptr && wolf != nullptr && bandit->Ok && bandit->StartHp == wolf->HpLeft &&
              bandit->StartFocus == wolf->FocusLeft && bandit->StartHp < bandit->MaxHp,
          "the next fight starts at the HP and focus the win left");

    SimRateQuery rate;
    rate.EncounterId = "wolf";
    rate.Runs = 1;
    rate.Seed = 1;
    rate.Slip = 0;
    const std::vector<SimRateRow> singles = SimulateEncounterRates(data, rate, error);
    const SimRateRow* single = FindRate(singles, "Fighter", "wolf", false);
    Check(single != nullptr && single->Ok && single->Wins == 1 && wolf != nullptr &&
              wolf->HpLeft == single->SumHp,
          "a full single fight still leaves the same HP");

    const std::string text = FormatChain(rows);
    Check(text.find("Win%") != std::string::npos && text.find("Fighter") != std::string::npos,
          "the chain table names the win rate and the build");
}

struct RouteExpect {
    RouteStepKind Kind;
    const char* Encounter;
    bool Side;
    const char* Map;
};

const RouteRow* FindRoute(const std::vector<RouteRow>& rows, bool fangRoute, std::string_view encounter)
{
    for (const RouteRow& row : rows) {
        if (row.FangRoute == fangRoute && row.Encounter == encounter)
            return &row;
    }
    return nullptr;
}

void CheckCarry(const RouteRow* previous, const RouteRow* next, const char* what)
{
    if (previous == nullptr || next == nullptr || !previous->Ok || !next->Ok)
        return;
    if (previous->Won)
        Check(next->StartHp == previous->HpLeft, what);
    else
        Check(next->StartHp == next->MaxHp, what);
}

void TestRoute()
{
    std::string error;
    PlaytestOptions route;
    Check(Parse({"--sim", "--route"}, route, error), "--sim --route parses");
    Check(route.Sim && route.Route && !route.Chain && route.Fight.Id.empty() && !route.OpensOverland(),
          "route walks the maps and does not open a window");

    PlaytestOptions launch;
    Check(Parse({}, launch, error), "a launch with no flags parses");
    Check(launch.OpensOverland() && launch.LoadsSave() && !launch.DefinesRun() &&
              launch.MapId.empty() && launch.Fight.Id.empty(),
          "no flags walks the hub and reads the per-user save");

    PlaytestOptions enemy;
    Check(Parse({"--enemy", "wolf"}, enemy, error), "--enemy parses");
    Check(!enemy.OpensOverland() && !enemy.LoadsSave() && enemy.DefinesRun(),
          "--enemy stays on the fight screen and does not load a save");

    PlaytestOptions alone;
    Check(!Parse({"--route"}, alone, error), "--route without --sim fails");
    Check(error.find("--route") != std::string::npos, "the message names --route");
    Check(!Parse({"--sim", "--route", "--enemy", "wolf"}, alone, error),
          "--route and --enemy together fail");
    Check(!Parse({"--sim", "--route", "--chain"}, alone, error), "--route and --chain together fail");
    Check(!Parse({"--sim", "--route", "--map", "hub"}, alone, error), "--route and --map together fail");
    Check(!Parse({"--sim", "--route", "--focus", "wolf"}, alone, error),
          "--route and --focus together fail");
    Check(!Parse({"--sim"}, alone, error), "--sim without --enemy still fails");

    const fs::path commandsPath = fs::path(ASCENSION_SOURCE_DIR) / "playtest/route/commands.txt";
    std::ifstream commandsIn(commandsPath);
    const std::string commands((std::istreambuf_iterator<char>(commandsIn)),
                               std::istreambuf_iterator<char>());
    Check(commandsIn && commands.find("--sim --route") != std::string::npos,
          "the route command file names the harness");

    Database data(fs::path(ASCENSION_SOURCE_DIR) / "data");
    DataError loadError;
    Check(data.Load(&loadError), "shipped data loads for the route");
    if (!loadError.Message.empty())
        std::printf("    %s\n", loadError.Message.c_str());
    if (!data.IsLoaded())
        return;

    const MapRoute walked = CollectMapRoute(data);
    if (!walked.Error.empty())
        std::printf("    %s\n", walked.Error.c_str());
    const RouteExpect expect[] = {
        {RouteStepKind::Rest, "", false, "hub"},
        {RouteStepKind::Fight, "wolf", false, "barrow_1"},
        {RouteStepKind::Fight, "bandit", false, "barrow_1"},
        {RouteStepKind::Fight, "tomb_moth", false, "barrow_2"},
        {RouteStepKind::Fight, "bone_archer", false, "barrow_2"},
        {RouteStepKind::Rest, "", false, "barrow_2"},
        {RouteStepKind::Fight, "barrow_sentry", false, "barrow_3"},
        {RouteStepKind::Fight, "bear", false, "barrow_4"},
        {RouteStepKind::Fight, "wolf_pack", true, "barrow_side"},
        {RouteStepKind::Rest, "", false, "barrow_4"},
        {RouteStepKind::Fight, "oathkeeper_varn", false, "barrow_5"},
        {RouteStepKind::Fight, "cairn_warden", false, "barrow_6"},
        {RouteStepKind::Rest, "", false, "barrow_7"},
        {RouteStepKind::Fight, "barrow_brute", false, "barrow_7"},
    };
    Check(walked.Error.empty() && walked.Steps.size() == std::size(expect),
          "the route is the hub, the rooms, the rests, and the optional pack");
    const i32 steps = static_cast<i32>(walked.Steps.size());
    const i32 expectedCount = static_cast<i32>(std::size(expect));
    const i32 compared = steps < expectedCount ? steps : expectedCount;
    for (i32 index = 0; index < compared; ++index) {
        const RouteStep& step = walked.Steps[static_cast<usize>(index)];
        const RouteExpect& want = expect[index];
        const bool match = step.Kind == want.Kind && step.Side == want.Side &&
                           step.EncounterId == want.Encounter && step.MapId == want.Map;
        if (!match) {
            std::printf("    step %d was %s on %s (side %d), wanted %s on %s\n", index,
                        step.EncounterId.c_str(), step.MapId.c_str(), step.Side ? 1 : 0,
                        want.Encounter, want.Map);
        }
        Check(match, "the route step is the next room in map order");
    }

    ChainQuery query;
    query.ClassId = "fighter";
    const std::vector<RouteRow> rows = SimulateRoute(data, query, error);
    Check(error.empty() && !rows.empty(), "the fighter route builds");
    const char* mainOrder[] = {"Wolf", "Bandit", "Tomb Moth", "Bone Archer", "Barrow Sentry",
                               "Bear", "Oathkeeper Varn", "Cairn Warden", "Barrow Brute"};
    const char* fangOrder[] = {"Wolf", "Bandit", "Tomb Moth", "Bone Archer", "Barrow Sentry",
                               "Bear", "Wolf Pack", "Oathkeeper Varn", "Cairn Warden",
                               "Barrow Brute"};
    i32 mainCount = 0;
    i32 fangCount = 0;
    for (const RouteRow& row : rows) {
        Check(row.Ok && row.Build == "Fighter", "each fighter row was played");
        if (row.FangRoute)
            ++fangCount;
        else
            ++mainCount;
    }
    Check(mainCount == static_cast<i32>(std::size(mainOrder)) &&
              fangCount == static_cast<i32>(std::size(fangOrder)),
          "main skips the pack room and fang takes it");
    for (i32 index = 0; index < mainCount && index < static_cast<i32>(std::size(mainOrder)); ++index) {
        i32 seen = 0;
        const RouteRow* row = nullptr;
        for (const RouteRow& candidate : rows) {
            if (candidate.FangRoute)
                continue;
            if (seen == index)
                row = &candidate;
            ++seen;
        }
        Check(row != nullptr && row->Encounter == mainOrder[index], "main fights follow the rooms");
    }
    for (i32 index = 0; index < fangCount && index < static_cast<i32>(std::size(fangOrder)); ++index) {
        i32 seen = 0;
        const RouteRow* row = nullptr;
        for (const RouteRow& candidate : rows) {
            if (!candidate.FangRoute)
                continue;
            if (seen == index)
                row = &candidate;
            ++seen;
        }
        Check(row != nullptr && row->Encounter == fangOrder[index], "fang fights take the side room");
    }

    const RouteRow* wolf = FindRoute(rows, false, "Wolf");
    const RouteRow* bandit = FindRoute(rows, false, "Bandit");
    const RouteRow* sentry = FindRoute(rows, false, "Barrow Sentry");
    const RouteRow* bear = FindRoute(rows, true, "Bear");
    const RouteRow* pack = FindRoute(rows, true, "Wolf Pack");
    const RouteRow* varn = FindRoute(rows, false, "Oathkeeper Varn");
    const RouteRow* fangVarn = FindRoute(rows, true, "Oathkeeper Varn");
    const RouteRow* brute = FindRoute(rows, false, "Barrow Brute");
    Check(wolf != nullptr && wolf->StartHp == wolf->MaxHp, "the first fight starts full");
    CheckCarry(wolf, bandit, "a win's HP is the next fight's start");
    Check(sentry != nullptr && sentry->StartHp == sentry->MaxHp, "the rest before the sentry refills");
    Check(varn != nullptr && varn->StartHp == varn->MaxHp && fangVarn != nullptr &&
              fangVarn->StartHp == fangVarn->MaxHp,
          "both routes rest before Varn");
    Check(brute != nullptr && brute->StartHp == brute->MaxHp, "the rest before the brute refills");
    CheckCarry(bear, pack, "the pack is fought on the HP the bear left");
    Check(pack != nullptr && !pack->Fang, "the pack is fought before the fang is worn");
    if (pack != nullptr && pack->Won && fangVarn != nullptr)
        Check(fangVarn->Fang, "a pack win wears the fang for the later fights");
    if (pack != nullptr && !pack->Won && fangVarn != nullptr)
        Check(!fangVarn->Fang, "a pack loss does not wear the fang");
    Check(varn != nullptr && !varn->Fang, "the main route never wears the fang");

    const std::string table = FormatRoute(rows);
    Check(table.find("Win%") != std::string::npos && table.find("fang") != std::string::npos &&
              table.find("main") != std::string::npos,
          "the route table names both walks and the win rate");
}

const unsigned char kPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
    0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
    0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

void WritePng(const fs::path& path)
{
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(kPng), static_cast<std::streamsize>(sizeof(kPng)));
}

void TestArtPackFlag()
{
    std::string why;
    PlaytestOptions parsed;
    Check(Parse({"--art-pack", "private-pack"}, parsed, why), "--art-pack keeps the directory");
    Check(parsed.ArtPackDir == "private-pack", "--art-pack stores the path");
    Check(parsed.LoadsSave() && !parsed.DefinesRun(), "--art-pack does not change the save or the hero");

    PlaytestOptions attached;
    Check(Parse({"--art-pack=private-pack"}, attached, why), "--art-pack=dir parses");
    Check(attached.ArtPackDir == "private-pack", "the attached directory is stored");

    PlaytestOptions bare;
    Check(!Parse({"--art-pack"}, bare, why), "a missing art pack directory fails");
    CheckHas(why, "--art-pack", "the message names --art-pack");
    Check(!Parse({"--art-pack="}, bare, why), "an empty --art-pack fails");
    Check(!Parse({"--art-pack", "a", "--art-pack", "b"}, bare, why), "a repeated --art-pack fails");

    const fs::path root = fs::temp_directory_path() / "ascension-artpack-playtest";
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root / "tiles", error);
    WritePng(root / "tiles" / "floor.png");
    WriteFile(root / "manifest.json",
              "{ \"tiles\": [ { \"id\": \"floor\", \"file\": \"tiles/floor.png\" } ] }\n");

    PlaytestOptions good;
    Check(Parse({"--art-pack", root.string()}, good, why), "a real art pack path parses");
    Check(CheckPlaytestStartup(good, why), "a manifest of a known tile passes startup");
    if (!why.empty())
        std::printf("    message was: %s\n", why.c_str());

    WriteFile(root / "manifest.json",
              "{ \"tiles\": [ { \"id\": \"no_such_tile\", \"file\": \"tiles/floor.png\" } ] }\n");
    PlaytestOptions bad;
    Check(Parse({"--art-pack", root.string()}, bad, why), "a bad art pack path still parses");
    Check(!CheckPlaytestStartup(bad, why), "an unknown art pack id fails startup");
    CheckHas(why, "no_such_tile", "startup names the unknown id");
    CheckHas(why, "manifest.json", "startup names the manifest");

    PlaytestOptions missing;
    Check(Parse({"--art-pack", (root / "gone").string()}, missing, why), "a missing folder parses");
    Check(!CheckPlaytestStartup(missing, why), "a missing art pack folder fails startup");
    CheckHas(why, "not a directory", "startup says the folder is not a directory");

    fs::remove_all(root, error);
}

} // namespace

int main()
{
    TestParseExampleAndEngineFlags();
    TestArtPackFlag();
    TestBadIds();
    TestVarnSetupAndChoose();
    TestWolfPackAndLoneEnemy();
    TestDataFolderHotReload();
    TestPickFlag();
    TestStartupFailureText();
    TestNextFightAndScriptedKeys();
    TestOuterBarrowReplayKeys();
    TestSaveQuitLoadKeys();
    TestPellRumorKeys();
    TestSimRates();
    TestChainCarry();
    TestRoute();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
