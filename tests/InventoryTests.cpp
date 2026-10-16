// Bag, gear, and Pell's shop. No window or GPU.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

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
        Path = fs::temp_directory_path() / "ascension-inventory-tests";
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
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text;
    }
    BumpTime(path);
}

void Reload(Database& data)
{
    DataError error;
    Check(data.CheckForChanges(&error) == 0, "the first sight of a change waits");
    Check(data.CheckForChanges(&error) == 1, "a settled write reloads");
}

i32 FindStack(const Inventory& bag, std::string_view id)
{
    for (i32 index = 0; index < bag.BagSlots(); ++index) {
        if (bag.BagAt(index).Id == id)
            return index;
    }
    return -1;
}

i32 CountOf(const Inventory& bag, std::string_view id)
{
    i32 total = 0;
    for (i32 index = 0; index < bag.BagSlots(); ++index) {
        if (bag.BagAt(index).Id == id)
            total += bag.BagAt(index).Count;
    }
    return total;
}

void TestBuySellAndStacks()
{
    Database data(kSample);
    DataError error;
    Check(data.Load(&error), "sample data loads");
    Inventory bag;
    std::string why;
    Check(bag.Start(data, why), "the bag starts empty");
    Check(bag.Gold() == 0 && bag.BagSlots() == 12 && bag.Equipped(GearSlot::Weapon).empty(),
          "a new hero has no gold and twelve empty slots");
    Check(!bag.Buy("minor_potion", why), "a purchase without gold fails");
    CheckHas(why, "not enough gold", "the failure says the purse is short");

    bag.GrantGold(3);
    Check(bag.Gold() == 3 && !bag.Buy("minor_potion", why), "3 gold cannot buy a 10 gold potion");
    bag.GrantGold(70);
    Check(bag.Gold() == 73, "gold adds up");
    for (i32 bought = 0; bought < 5; ++bought)
        Check(bag.Buy("minor_potion", why), "the first five potions share a stack");
    const i32 potionSlot = FindStack(bag, "minor_potion");
    Check(potionSlot == 0 && bag.BagAt(potionSlot).Count == 5 && bag.Gold() == 23,
          "five potions fill one stack and cost 50");
    Check(bag.Buy("minor_potion", why), "the sixth potion opens a second stack");
    Check(bag.BagAt(0).Count == 5 && bag.BagAt(1).Id == "minor_potion" && bag.BagAt(1).Count == 1,
          "stacks stop at 5");
    Check(bag.Sell(0, why) && bag.BagAt(0).Count == 4 && bag.Gold() == 18,
          "selling one potion pays half its price");

    Check(bag.Buy("focus_tonic", why), "Pell sells the tonic");
    Check(!bag.Equip(FindStack(bag, "focus_tonic"), why), "a tonic is not worn");
    CheckHas(why, "drink it in a fight", "the failure points at combat");
}

void TestEquipAndHands()
{
    Database data(kSample);
    DataError error;
    Check(data.Load(&error), "sample data loads");
    Inventory bag;
    std::string why;
    Check(bag.Start(data, why), "the bag starts");
    bag.GrantGold(200);
    Check(bag.Buy("rusty_sword", why) && bag.Buy("oak_shield", why) &&
              bag.Buy("notched_greataxe", why) && bag.Buy("oathkeepers_plate", why) &&
              bag.Buy("wolfhide_charm", why),
          "Pell sells the design gear");

    Check(bag.Equip(FindStack(bag, "rusty_sword"), why), "the rusty sword equips");
    Check(bag.Equipped(GearSlot::Weapon) == "rusty_sword" && bag.GearBonus().Strength == 1,
          "the rusty sword adds 1 strength");
    Check(bag.Equip(FindStack(bag, "oak_shield"), why), "the shield equips in the offhand");
    Check(bag.Equipped(GearSlot::Offhand) == "oak_shield" && bag.GearBonus().Defense == 2,
          "the shield adds 2 defense");

    const i32 axeSlot = FindStack(bag, "notched_greataxe");
    Check(bag.Equip(axeSlot, why), "the greataxe equips and sends the shield home");
    Check(bag.Equipped(GearSlot::Weapon) == "notched_greataxe" &&
              bag.Equipped(GearSlot::Offhand).empty() && bag.OffhandBlocked(),
          "a two-handed weapon blocks the offhand");
    Check(bag.GearBonus().Strength == 6 && bag.GearBonus().Defense == -2,
          "the greataxe is +6 strength and -2 defense");
    Check(FindStack(bag, "oak_shield") >= 0 && FindStack(bag, "rusty_sword") >= 0,
          "the sword and the shield are back in the bag");
    Check(!bag.Equip(FindStack(bag, "oak_shield"), why), "the shield cannot be worn with the axe");
    CheckHas(why, "two-handed", "the failure names the two-handed weapon");
    Check(!bag.Unequip(GearSlot::Offhand, why), "an empty offhand has nothing to take off");
    CheckHas(why, "nothing is worn", "taking the offhand off is not blocked by the axe");

    Check(bag.Equip(FindStack(bag, "oathkeepers_plate"), why), "the plate equips");
    Check(bag.Equip(FindStack(bag, "wolfhide_charm"), why), "the charm equips");
    const StatBlock worn = bag.GearBonus();
    Check(worn.Strength == 6 && worn.Defense == 4 && worn.Agility == 1,
          "plate and charm stack with the axe: defense  -2+6, agility +2-1");
    Check(bag.Unequip(GearSlot::Weapon, why) && !bag.OffhandBlocked() &&
              bag.Equipped(GearSlot::Weapon).empty(),
          "unequipping the axe frees the offhand");
    Check(bag.Equip(FindStack(bag, "oak_shield"), why) &&
              bag.Equipped(GearSlot::Offhand) == "oak_shield",
          "the shield equips once both hands are free");
}

void TestFullBag()
{
    Database data(kSample);
    DataError error;
    Check(data.Load(&error), "sample data loads");
    Inventory bag;
    std::string why;
    Check(bag.Start(data, why), "the bag starts");
    bag.GrantGold(1000);
    bool filled = true;
    for (i32 bought = 0; bought < 60; ++bought) {
        if (!bag.Buy("minor_potion", why))
            filled = false;
    }
    Check(filled && CountOf(bag, "minor_potion") == 60, "sixty potions fill twelve stacks of five");
    Check(static_cast<i32>(bag.ConsumableSlots().size()) == 12,
          "every consumable stack is listed, past the eight number keys");
    Check(!bag.Buy("rusty_sword", why), "a full bag refuses another item");
    CheckHas(why, "the bag is full", "the failure says the bag is full");
    Check(bag.Gold() == 1000 - 600, "the refused sword did not spend gold");

    Check(bag.Sell(0, why), "selling one opens room in a stack");
    Check(bag.Buy("minor_potion", why) && CountOf(bag, "minor_potion") == 60,
          "the freed stack room takes another potion");
    Check(!bag.Buy("rusty_sword", why), "a different item still needs an empty slot");
}

void TestHotReload()
{
    TempCopy copy;
    Database data(copy.Path);
    DataError error;
    Check(data.Load(&error), "copied sample loads");
    Inventory bag;
    std::string why;
    Check(bag.Start(data, why), "the bag starts from the copy");
    bag.GrantGold(50);
    Check(bag.Buy("minor_potion", why) && bag.Buy("minor_potion", why) &&
              bag.Buy("minor_potion", why),
          "three potions are one stack");
    Check(bag.Buy("rusty_sword", why) && bag.Equip(FindStack(bag, "rusty_sword"), why),
          "the sword is worn");
    Check(bag.GearBonus().Strength == 1 && bag.Gold() == 8, "three potions and the sword cost 42");

    ReplaceInFile(copy.Path / "shop.json", "\"stackSize\": 5", "\"stackSize\": 2");
    Reload(data);
    Check(bag.ApplyData(why) && bag.BagAt(0).Count == 2 && bag.Gold() == 8,
          "a smaller stack size clamps the count and keeps the gold");

    ReplaceInFile(copy.Path / "items.json", "\"strength\": 1", "\"strength\": 4");
    Reload(data);
    Check(bag.ApplyData(why) && bag.GearBonus().Strength == 4,
          "a reload of the worn sword updates its bonus");
}

void WearSwordAndShield(Inventory& bag)
{
    std::string why;
    bag.GrantGold(80);
    Check(bag.Buy("rusty_sword", why) && bag.Buy("oak_shield", why),
          "the sword and the shield are bought");
    Check(bag.Equip(FindStack(bag, "rusty_sword"), why) &&
              bag.Equip(FindStack(bag, "oak_shield"), why),
          "the sword and the shield are worn");
    Check(bag.Equipped(GearSlot::Offhand) == "oak_shield" && bag.GearBonus().Defense == 2,
          "the shield adds 2 defense beside the sword");
}

void TestTwoHandedReload()
{
    TempCopy copy;
    Database data(copy.Path);
    DataError error;
    Check(data.Load(&error), "copied sample loads");

    Inventory open;
    Inventory manual;
    Inventory packed;
    std::string why;
    Check(open.Start(data, why) && manual.Start(data, why) && packed.Start(data, why),
          "three bags start from the copy");
    WearSwordAndShield(open);
    WearSwordAndShield(manual);
    WearSwordAndShield(packed);
    Check(open.Buy("minor_potion", why) && CountOf(open, "minor_potion") == 1,
          "the open bag keeps one potion beside the worn gear");
    packed.GrantGold(600);
    bool filled = true;
    for (i32 bought = 0; bought < 60; ++bought) {
        if (!packed.Buy("minor_potion", why))
            filled = false;
    }
    Check(filled && CountOf(packed, "minor_potion") == 60, "the packed bag has no empty slot");

    ReplaceInFile(copy.Path / "items.json", "\"id\": \"rusty_sword\"",
                  "\"id\": \"rusty_sword\", \"twoHanded\": true");
    Reload(data);
    const ItemDef* sword = data.FindItem("rusty_sword");
    Check(sword != nullptr && sword->TwoHanded, "the reloaded sword is two-handed");

    Check(open.Equipped(GearSlot::Offhand) == "oak_shield" && open.GearBonus().Defense == 0,
          "the shield's defense stops as soon as the sword is two-handed");
    Check(open.ApplyData(why) && why.empty(), "a spare slot takes the shield with no note");
    Check(open.Equipped(GearSlot::Offhand).empty() &&
              open.Equipped(GearSlot::Weapon) == "rusty_sword" &&
              FindStack(open, "oak_shield") >= 0 && CountOf(open, "minor_potion") == 1,
          "the shield moves into the bag and the potion stays");
    Check(open.OffhandBlocked() && open.GearBonus().Defense == 0, "the offhand stays blocked");
    Check(!open.Equip(FindStack(open, "oak_shield"), why), "the shield still cannot be worn");
    CheckHas(why, "two-handed", "wearing the shield is still refused");

    Check(manual.Unequip(GearSlot::Offhand, why),
          "the offhand comes off while the sword is two-handed");
    Check(why.find("two-handed") == std::string::npos, "that unequip is not refused as two-handed");
    Check(manual.Equipped(GearSlot::Offhand).empty() && FindStack(manual, "oak_shield") >= 0,
          "the shield is back in the bag");
    Check(!manual.Unequip(GearSlot::Offhand, why),
          "the empty offhand has nothing left to take off");
    CheckHas(why, "nothing is worn", "the empty offhand says nothing is worn");

    Check(packed.GearBonus().Defense == 0, "a full bag still loses the shield's defense");
    Check(packed.ApplyData(why), "apply stows the shield out of a full bag");
    CheckHas(why, "bag was full", "the note says a stack was set down");
    Check(packed.Equipped(GearSlot::Offhand).empty() && FindStack(packed, "oak_shield") >= 0 &&
              CountOf(packed, "minor_potion") == 55 && packed.GearBonus().Defense == 0,
          "one stack of five is set down and the shield takes that slot");
}

} // namespace

int main()
{
    TestBuySellAndStacks();
    TestEquipAndHands();
    TestFullBag();
    TestHotReload();
    TestTwoHandedReload();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
