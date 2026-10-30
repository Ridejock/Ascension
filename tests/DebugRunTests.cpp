// Respawn keeps the hero and clears defeated foes. New game matches a fresh start
// and does not touch the save file. No window or GPU.

#include <filesystem>
#include <string>
#include <vector>

#include "Ascension/Progress/DebugRun.h"
#include "Check.h"

#include "Emerald/Core/Log.h"

namespace {

using namespace Ascension;

namespace fs = std::filesystem;

const fs::path kSample = ASCENSION_DATA_DIR;
const fs::path kShipped = ASCENSION_SHIPPED_DATA_DIR;

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

bool Load(Database& data)
{
    DataError error;
    const bool ok = data.Load(&error);
    Check(ok, "data loads");
    if (!ok)
        std::printf("    load: %s\n", error.Message.c_str());
    return ok;
}

bool SameList(const std::vector<std::string>& left, const std::vector<std::string>& right)
{
    if (left.size() != right.size())
        return false;
    for (usize index = 0; index < left.size(); ++index) {
        if (left[index] != right[index])
            return false;
    }
    return true;
}

bool SameStats(const StatBlock& left, const StatBlock& right)
{
    return left.Vitality == right.Vitality && left.Strength == right.Strength &&
           left.Defense == right.Defense && left.Agility == right.Agility &&
           left.Focus == right.Focus;
}

bool SameCareer(const CareerRecord& left, const CareerRecord& right)
{
    if (left.Name != right.Name || left.ClassId != right.ClassId || left.Xp != right.Xp ||
        left.Custom != right.Custom ||
        left.ChoosingStarter != right.ChoosingStarter || left.ClassPool != right.ClassPool ||
        left.SpendId != right.SpendId)
        return false;
    if (!SameList(left.Practiced, right.Practiced) || !SameList(left.Picked, right.Picked))
        return false;
    if (left.Uses.size() != right.Uses.size() ||
        left.LevelSteps.size() != right.LevelSteps.size() ||
        left.Sources.size() != right.Sources.size())
        return false;
    for (usize index = 0; index < left.Uses.size(); ++index) {
        if (left.Uses[index] != right.Uses[index])
            return false;
    }
    for (usize index = 0; index < left.LevelSteps.size(); ++index) {
        if (!SameStats(left.LevelSteps[index], right.LevelSteps[index]))
            return false;
    }
    for (usize index = 0; index < left.Sources.size(); ++index) {
        if (left.Sources[index].Id != right.Sources[index].Id ||
            !SameStats(left.Sources[index].Points, right.Sources[index].Points))
            return false;
    }
    return true;
}

bool SameBag(const BagRecord& left, const BagRecord& right)
{
    if (left.Gold != right.Gold || left.Weapon != right.Weapon || left.Offhand != right.Offhand ||
        left.Armor != right.Armor || left.Accessory != right.Accessory ||
        left.Bag.size() != right.Bag.size())
        return false;
    for (usize index = 0; index < left.Bag.size(); ++index) {
        if (left.Bag[index].Id != right.Bag[index].Id ||
            left.Bag[index].Count != right.Bag[index].Count)
            return false;
    }
    return true;
}

bool SameWounds(const std::vector<FledWounds>& left, const std::vector<FledWounds>& right)
{
    if (left.size() != right.size())
        return false;
    for (usize index = 0; index < left.size(); ++index) {
        if (left[index].SpawnId != right[index].SpawnId ||
            left[index].Foes.size() != right[index].Foes.size())
            return false;
        for (usize foe = 0; foe < left[index].Foes.size(); ++foe) {
            if (left[index].Foes[foe].Id != right[index].Foes[foe].Id ||
                left[index].Foes[foe].Hp != right[index].Foes[foe].Hp)
                return false;
        }
    }
    return true;
}

bool KeptPlayer(const RunState& run, const RunState& before)
{
    return SameCareer(run.Career, before.Career) && SameBag(run.Bag, before.Bag) &&
           run.MapId == before.MapId && run.Tile.X == before.Tile.X &&
           run.Tile.Y == before.Tile.Y && run.ResourcesKnown == before.ResourcesKnown &&
           run.Hp == before.Hp && run.Focus == before.Focus && SameList(run.Found, before.Found) &&
           SameList(run.Flags, before.Flags) && SameWounds(run.Wounds, before.Wounds) &&
           run.Standing == before.Standing && run.SpawnId == before.SpawnId &&
           run.EncounterId == before.EncounterId;
}

i32 StackOf(const Inventory& bag, std::string_view id)
{
    for (i32 index = 0; index < bag.BagSlots(); ++index) {
        if (bag.BagAt(index).Id == id)
            return index;
    }
    return -1;
}

const MapDef* FindHub(const Database& data)
{
    for (const MapDef& map : data.GetMaps()) {
        if (map.Hub)
            return &map;
    }
    return nullptr;
}

RunState PlayedRun()
{
    RunState run;
    run.Career.ClassId = "berserker";
    run.Career.Xp = 80;
    run.Bag.Gold = 33;
    run.Bag.Weapon = "rusty_sword";
    ItemStack potion;
    potion.Id = "minor_potion";
    potion.Count = 2;
    run.Bag.Bag.push_back(potion);
    run.MapId = "fixture_b";
    run.Tile.X = 4;
    run.Tile.Y = 2;
    run.ResourcesKnown = true;
    run.Hp = 7;
    run.Focus = 2;
    run.Defeated.push_back("wolf");
    run.Defeated.push_back("bandit");
    run.Cleared.push_back("fixture_spawn");
    run.Cleared.push_back("retired_spawn");
    run.Found.push_back("alphas_fang");
    run.Flags.push_back("met");
    FledWounds wound;
    wound.SpawnId = "fixture_spawn";
    FoeWound body;
    body.Id = "wolf";
    body.Hp = 4;
    wound.Foes.push_back(body);
    run.Wounds.push_back(wound);
    run.Standing = RunStanding::Victory;
    run.SpawnId = "fixture_hold";
    run.EncounterId = "wolf";
    return run;
}

void TestRespawn()
{
    RunState run = PlayedRun();
    const RunState before = run;
    Check(RespawnFoes(run, true) == FoeRespawn::Busy, "a fight leaves the foes down");
    Check(KeptPlayer(run, before) && SameList(run.Defeated, before.Defeated) &&
              SameList(run.Cleared, before.Cleared),
          "a fight does not clear defeated flags or move the hero");

    Check(RespawnFoes(run, false) == FoeRespawn::Returned, "respawn brings the foes back");
    Check(run.Defeated.empty() && run.Cleared.empty(),
          "defeated foe ids and cleared spawns are gone");
    Check(KeptPlayer(run, before),
          "respawn keeps level, gear, gold, the tile, drops, flags, and flee wounds");
}

void TestNewGame(const Database& sample, const Database& shipped)
{
    Hero hero;
    Inventory bag;
    std::string why;
    Check(hero.Start(sample, why) && bag.Start(sample, why), "a run can start");
    hero.GrantXp(80);
    if (hero.ChoicePending()) {
        const std::vector<std::string> paths = hero.ChoiceIds();
        if (!paths.empty())
            Check(hero.Choose(paths.front(), why), "a path can be chosen before the reset");
    }
    Check(hero.Xp() > 0 && hero.Level() > 1, "the run has left level 1");
    Check(bag.Give("rusty_sword", 1, why) && bag.Give("minor_potion", 2, why) &&
              bag.SetGold(25, why),
          "the bag has gear, a drink, and gold");
    const i32 sword = StackOf(bag, "rusty_sword");
    Check(sword >= 0 && bag.Equip(sword, why), "the sword is worn");
    bag.SetDefeated({"wolf", "bandit"});
    Check(bag.Equipped(GearSlot::Weapon) == "rusty_sword" && bag.Gold() == 25 &&
              bag.Defeated().size() == 2,
          "the played hero is not a fresh start");

    Hero freshHero;
    Inventory freshBag;
    Check(freshHero.Start(sample, why) && freshBag.Start(sample, why), "a twin fresh start");
    CareerRecord freshCareer;
    BagRecord freshRecord;
    freshHero.ExportCareer(freshCareer);
    freshBag.ExportBag(freshRecord);
    const PoolCaps freshPools = HeroPools(sample, freshHero, freshBag);

    FreshStart started;
    Check(BeginNewGame(sample, hero, bag, started, why), "new game resets the sample run");
    CareerRecord afterCareer;
    BagRecord afterBag;
    hero.ExportCareer(afterCareer);
    bag.ExportBag(afterBag);
    Check(SameCareer(afterCareer, freshCareer) && SameCareer(started.Run.Career, freshCareer),
          "new game matches a fresh career");
    Check(SameBag(afterBag, freshRecord) && SameBag(started.Run.Bag, freshRecord),
          "new game matches a fresh bag");
    Check(hero.Level() == freshHero.Level() && hero.Xp() == 0 &&
              hero.ClassId() == freshHero.ClassId(),
          "level, XP, and class are the starter again");
    Check(bag.Gold() == 0 && bag.Equipped(GearSlot::Weapon).empty() && bag.Defeated().empty() &&
              StackOf(bag, "minor_potion") < 0,
          "gold, gear, drinks, and defeated flags are cleared");
    Check(started.Run.Defeated.empty() && started.Run.Cleared.empty() &&
              started.Run.Found.empty() && started.Run.Flags.empty() &&
              started.Run.Wounds.empty() && started.Run.Standing == RunStanding::None &&
              started.Run.MapId.empty(),
          "the sample has no hub, so the fresh place is empty and the lists are clear");
    Check(started.Pools.Known && !started.Pools.ReturnToHub &&
              started.Pools.Hp == freshPools.MaxHp && started.Pools.Focus == freshPools.MaxFocus &&
              started.Run.ResourcesKnown && started.Run.Hp == started.Pools.Hp &&
              started.Run.Focus == started.Pools.Focus,
          "a new game is at full health");

    const MapDef* hub = FindHub(shipped);
    Check(hub != nullptr, "shipped data has a hub");
    if (hub == nullptr)
        return;
    Hero shippedHero;
    Inventory shippedBag;
    FreshStart shippedStart;
    Check(BeginNewGame(shipped, shippedHero, shippedBag, shippedStart, why),
          "new game resets the shipped run");
    Check(shippedStart.Run.MapId == hub->Id && shippedStart.Run.Tile.X == hub->Start.X &&
              shippedStart.Run.Tile.Y == hub->Start.Y && shippedHero.Level() == 1 &&
              shippedHero.Xp() == 0 && shippedBag.Gold() == 0 && shippedHero.Name().empty(),
          "a shipped new game stands on the hub start at level 1");

    Hero named;
    Inventory namedBag;
    FreshStart namedStart;
    Check(!BeginNewGame(shipped, named, namedBag, namedStart, why, "Mara!", "mage"),
          "a name create would refuse does not start");
    Check(BeginNewGame(shipped, named, namedBag, namedStart, why, "  Mara Stone ", "mage"),
          "create starts the named class");
    Check(named.Name() == "Mara Stone" && named.ClassId() == "mage" && named.Level() == 1 &&
              named.Xp() == 0 && namedBag.Gold() == 0 && namedStart.Run.Career.Name == "Mara Stone" &&
              namedStart.Run.Career.ClassId == "mage" && namedStart.Run.MapId == hub->Id &&
              namedStart.Run.Tile.X == hub->Start.X && namedStart.Run.Tile.Y == hub->Start.Y,
          "the named mage stands on the hub start");
}

void TestSaveLeftAlone(const Database& sample)
{
    TempDir dir("ascension-debug-new-game");
    RunState played = PlayedRun();
    played.Career.ClassId = "fighter";
    RunSave saves(dir.Path);
    std::string why;
    Check(saves.Write(played, why), "the played run is on disk");

    Hero hero;
    Inventory bag;
    FreshStart started;
    Check(BeginNewGame(sample, hero, bag, started, why), "new game does not need the save");
    const RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.Run.Career.ClassId == "fighter" &&
              loaded.Run.Career.Xp == 80 && loaded.Run.Bag.Gold == 33 &&
              loaded.Run.Bag.Weapon == "rusty_sword" && loaded.Run.Defeated.size() == 2 &&
              loaded.Run.Cleared.size() == 2 && loaded.Run.MapId == "fixture_b" &&
              loaded.Run.Tile.X == 4 && loaded.Run.Tile.Y == 2,
          "new game leaves the save file until a later save writes it");
    Check(started.Run.Career.Xp == 0 && started.Run.Defeated.empty() && hero.Xp() == 0,
          "the running game is fresh while the file still has the old run");
}

} // namespace

int main()
{
    Emerald::Log::Init({});
    Database sample(kSample);
    Database shipped(kShipped);
    if (!Load(sample) || !Load(shipped)) {
        std::printf("Failed: %d failure(s)\n", g_Failures);
        return 1;
    }
    TestRespawn();
    TestNewGame(sample, shipped);
    TestSaveLeftAlone(sample);
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
