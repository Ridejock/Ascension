// Loader tests: the sample data, validation errors, and hot reload. No window or GPU.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "Ascension/Data/Database.h"
#include "Check.h"

namespace {

using namespace Ascension;

namespace fs = std::filesystem;

const fs::path kSample = ASCENSION_DATA_DIR;

struct TempCopy {
    fs::path Path;

    TempCopy()
    {
        Path = fs::temp_directory_path() / "ascension-data-tests";
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

// last_write_time can stay put when two writes land in the same tick. Push it forward so the
// watcher sees a change.
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

const ClassDef* MustClass(const Database& data, const char* id)
{
    const ClassDef* klass = data.FindClass(id);
    Check(klass != nullptr, id);
    return klass;
}

void TestSample()
{
    Database data(kSample);
    DataError error;
    Check(data.Load(&error), "sample data loads");
    Check(error.Message.empty(), "a successful load has no error");
    Check(data.GetRevision() == 1, "the first load is revision 1");
    Check(data.GetStats().size() == 5, "five stats");
    Check(data.GetSkills().size() == 19, "nineteen skills");
    Check(data.GetRules().HpPerVitality == 4 && data.GetRules().MinDamage == 1 &&
              data.GetRules().AttackDenominator == 1 && data.GetRules().DefenseDenominator == 1 &&
              data.GetRules().FocusBase == 4 && data.GetRules().FocusPerTurn == 1 &&
              data.GetRules().FocusOnWait == 2,
          "combat.json supplies the damage rules and focus regen");
    Check(data.GetEnemies().size() == 6, "six enemies, counting the boss");
    Check(data.GetItems().size() == 7, "seven items");
    const ShopDef& shop = data.GetShop();
    Check(shop.Id == "pells_provisions" && shop.Name == "Old Pell's Provisions" &&
              shop.Keeper == "Pell Harrow" && shop.BagSlots == 12 && shop.StackSize == 5 &&
              shop.SellNumerator == 1 && shop.SellDenominator == 2 && shop.Stock.size() == 7,
          "Pell Harrow runs a 12-slot bag with stacks of 5");
    Check(SellPrice(shop, 10) == 5 && SellPrice(shop, 35) == 17, "selling pays half, rounded down");
    const ProgressRules& progress = data.GetProgress();
    Check(progress.XpToReach.size() == 10 && progress.XpToReach[0] == 0 &&
              progress.XpToReach[1] == 8,
          "progress.json has ten steps and level 2 at 8 XP");
    Check(progress.UsesPerRank.size() == 4 && progress.UsesPerRank[0] == 3 &&
              progress.UsesPerRank[1] == 6 && progress.UsesPerRank[2] == 10 &&
              progress.UsesPerRank[3] == 15 && progress.MaxSkillRank == 5 &&
              progress.PowerPerRank == 2,
          "ranks use the escalating thresholds and add two power");

    const ClassDef* fighter = MustClass(data, "fighter");
    const ClassDef* berserker = MustClass(data, "berserker");
    const ClassDef* guardian = MustClass(data, "guardian");
    if (fighter == nullptr || berserker == nullptr || guardian == nullptr)
        return;

    Check(!fighter->IsEvolution(), "fighter is the base class");
    Check(fighter->Stats.Vitality == 18 && fighter->Stats.Strength == 8 &&
              fighter->Stats.Defense == 6 && fighter->Stats.Agility == 5 &&
              fighter->Stats.Focus == 4,
          "fighter level-1 stats");
    const auto fighterSkills = data.ActiveSkills(*fighter);
    Check(fighterSkills.size() == 4 && fighterSkills[0]->Id == "strike" &&
              fighterSkills[1]->Id == "brace" && fighterSkills[2]->Id == "heavy_blow" &&
              fighterSkills[3]->Id == "second_wind",
          "fighter has strike, brace, heavy blow and second wind");
    Check(fighterSkills[0]->Effect == SkillEffect::Damage && fighterSkills[0]->Power == 10,
          "strike is a damage skill");
    Check(fighterSkills[1]->Effect == SkillEffect::Guard && fighterSkills[1]->Power == 50,
          "brace guards half of the next hit");
    Check(data.Passives(*fighter).empty(), "fighter has no passive");

    const auto evolutions = data.EvolutionsOf("fighter");
    Check(evolutions.size() == 2, "fighter has two evolutions");
    Check(berserker->IsEvolution() && berserker->EvolvesFrom == "fighter" &&
              berserker->EvolveLevel == 5,
          "berserker is a level-5 evolution of fighter");
    Check(guardian->IsEvolution() && guardian->EvolvesFrom == "fighter" &&
              guardian->EvolveLevel == 5,
          "guardian is a level-5 evolution of fighter");
    Check(berserker->Skills.size() == 2 && berserker->Passives.size() == 1,
          "berserker adds 2 skills and 1 passive");
    Check(guardian->Skills.size() == 2 && guardian->Passives.size() == 1,
          "guardian adds 2 skills and 1 passive");

    const auto berserkerSkills = data.ActiveSkills(*berserker);
    Check(berserkerSkills.size() == 6 && berserkerSkills[0]->Id == "strike" &&
              berserkerSkills[1]->Id == "brace" && berserkerSkills[4]->Id == "rage" &&
              berserkerSkills[5]->Id == "cleave",
          "berserker keeps fighter's skills and adds rage and cleave");
    const auto berserkerPassives = data.Passives(*berserker);
    Check(berserkerPassives.size() == 1 && berserkerPassives[0]->Id == "bloodlust" &&
              berserkerPassives[0]->Kind == SkillKind::Passive &&
              berserkerPassives[0]->Passive == PassiveEffect::Bloodied &&
              berserkerPassives[0]->Stat == "strength" && berserkerPassives[0]->Amount == 1 &&
              berserkerPassives[0]->StepPercent == 10 && berserkerPassives[0]->Max == 6,
          "bloodlust is +1 strength per 10 percent missing, up to 6");
    const auto guardianPassives = data.Passives(*guardian);
    Check(guardianPassives.size() == 1 && guardianPassives[0]->Id == "bulwark" &&
              guardianPassives[0]->Passive == PassiveEffect::Bulwark &&
              guardianPassives[0]->Stat == "defense" && guardianPassives[0]->Amount == 3 &&
              guardianPassives[0]->FocusGain == 1,
          "bulwark is +3 defense and 1 focus when a guard stops a hit");
    const SkillDef* riposte = data.FindSkill("riposte");
    Check(riposte != nullptr && riposte->Effect == SkillEffect::Riposte && riposte->Power == 10 &&
              riposte->FocusCost == 2 && riposte->Guard == 50 && riposte->Reflect == 50 &&
              riposte->Scaling == "defense",
          "riposte guards half the next hit and reflects half of what it stopped");
    Check(data.FindSkill("taunt") == nullptr, "taunt is gone");

    const StatBlock level1 = data.StatsAt(*fighter, 1);
    Check(level1.Vitality == 18 && level1.Strength == 8, "level 1 is the base stats");
    const StatBlock level5 = data.StatsAt(*fighter, 5);
    // 18 + 4*4 vitality, 8 + 2*4 strength, 6 + 1*4 defense, 5 + 4 agility, 4 + 4 focus.
    Check(level5.Vitality == 34 && level5.Strength == 16 && level5.Defense == 10 &&
              level5.Agility == 9 && level5.Focus == 8,
          "fighter level 5 applies four growth steps");
    const StatBlock rage = data.StatsAt(*berserker, 5);
    Check(rage.Vitality == 36 && rage.Strength == 20 && rage.Defense == 10,
          "berserker at 5 is the fighter plus its bonus");
    const StatBlock guard = data.StatsAt(*guardian, 5);
    Check(guard.Vitality == 37 && guard.Defense == 15 && guard.Strength == 16,
          "guardian at 5 is the fighter plus its bonus");
    const StatBlock before = data.StatsAt(*berserker, 4);
    const StatBlock parent = data.StatsAt(*fighter, 4);
    Check(before.Vitality == parent.Vitality && before.Strength == parent.Strength,
          "before the evolve level, an evolution is still the parent");

    const EnemyDef* wolf = data.FindEnemy("wolf");
    Check(wolf != nullptr && wolf->Level == 1 && wolf->Skills.size() == 1 &&
              wolf->Skills[0] == "bite" && !wolf->Boss,
          "wolf is a level-1 enemy with bite");
    const auto enemies = data.GetEnemies();
    Check(enemies.size() == 6 && enemies[0].Id == "wolf" && enemies[1].Id == "bandit" &&
              enemies[2].Id == "tomb_moth" && enemies[3].Id == "bone_archer" &&
              enemies[4].Id == "barrow_sentry" && enemies[5].Id == "oathkeeper_varn",
          "the roster ends at Oathkeeper Varn");
    Check(enemies[2].Level == 4 && enemies[2].Experience == 14 && enemies[2].Gold == 4,
          "the tomb moth pays 14 XP and 4 gold");
    Check(enemies[3].Level == 5 && enemies[3].Experience == 22 && enemies[3].Gold == 8 &&
              enemies[3].Skills.size() == 1 && enemies[3].Skills[0] == "aimed_shot",
          "the bone archer pays 22 XP and 8 gold");
    const SkillDef* aimed = data.FindSkill("aimed_shot");
    Check(aimed != nullptr && aimed->Power == 20 && aimed->Windup == 2,
          "aimed shot is power 20 after a 2-turn wind-up");
    Check(enemies[5].Experience == 80 && enemies[5].Boss && enemies[5].Level == 8 &&
              enemies[5].Stats.Vitality == 60 && enemies[5].Stats.Strength == 14 &&
              enemies[5].Stats.Defense == 12 && enemies[5].Skills.size() == 3 &&
              enemies[5].PhaseAtPercent == 50 && enemies[5].ShardDamage == 10,
          "Oathkeeper Varn is the only boss");
    Check(data.FindEnemy("mire_lurker") == nullptr && data.FindEnemy("oathbreaker") == nullptr &&
              data.FindEnemy("cairn_guard") == nullptr && data.FindEnemy("cairn_warden") == nullptr,
          "mire lurker, oathbreaker and the cairn names are gone");
    for (const EnemyDef& enemy : enemies) {
        Check(enemy.Description.find("cairn") == std::string::npos &&
                  enemy.Description.find("Cairn") == std::string::npos,
              "descriptions say barrow, not cairn");
    }
    Check(data.GetStatuses().size() == 4, "four statuses");
    const StatusDef* bleed = data.FindStatus("bleed");
    const StatusDef* exposed = data.FindStatus("exposed");
    const StatusDef* enraged = data.FindStatus("enraged");
    const StatusDef* stunned = data.FindStatus("stunned");
    Check(bleed != nullptr && bleed->DamagePerTurn == 3 && bleed->Turns == 3 &&
              bleed->MaxStacks == 2,
          "bleed is 3 a turn for 3 turns and stacks to 2");
    Check(exposed != nullptr && exposed->DamageTakenPercent == 25, "exposed adds 25 percent taken");
    Check(enraged != nullptr && enraged->DamageDealtPercent == 30 &&
              enraged->DamageTakenPercent == 15,
          "enraged adds 30 percent dealt and 15 percent taken");
    Check(stunned != nullptr && stunned->SkipAction && stunned->Turns == 1,
          "stunned skips the next action");
    const ItemDef* sword = data.FindItem("rusty_sword");
    const ItemDef* axe = data.FindItem("notched_greataxe");
    const ItemDef* shield = data.FindItem("oak_shield");
    const ItemDef* plate = data.FindItem("oathkeepers_plate");
    const ItemDef* charm = data.FindItem("wolfhide_charm");
    const ItemDef* potion = data.FindItem("minor_potion");
    const ItemDef* tonic = data.FindItem("focus_tonic");
    Check(sword != nullptr && sword->Slot == GearSlot::Weapon && !sword->TwoHanded &&
              sword->Bonus.Strength == 1 && sword->Value == 12,
          "rusty sword is a one-handed weapon with +1 strength");
    Check(axe != nullptr && axe->TwoHanded && axe->Bonus.Strength == 6 &&
              axe->Bonus.Defense == -2 && axe->Value == 60,
          "the notched greataxe is two-handed, +6 strength and -2 defense");
    Check(shield != nullptr && shield->Slot == GearSlot::Offhand && shield->Bonus.Defense == 2,
          "oak shield sits in the offhand");
    Check(plate != nullptr && plate->Name == "Oathkeeper's Plate" &&
              plate->Slot == GearSlot::Armor && plate->Bonus.Defense == 6 &&
              plate->Bonus.Agility == -1,
          "Oathkeeper's Plate is +6 defense and -1 agility");
    Check(charm != nullptr && charm->Slot == GearSlot::Accessory && charm->Bonus.Agility == 2 &&
              charm->Value == 35,
          "wolfhide charm is +2 agility and costs 35");
    Check(potion != nullptr && potion->Slot == GearSlot::Consumable && potion->Heal == 30 &&
              potion->Value == 10,
          "minor potion heals 30 and costs 10");
    Check(tonic != nullptr && tonic->Slot == GearSlot::Consumable && tonic->Focus == 4 &&
              tonic->Value == 15,
          "focus tonic restores 4 focus and costs 15");
    Check(data.FindItem("iron_sword") == nullptr && data.FindItem("warden_forged_plate") == nullptr,
          "the placeholder sword and the old plate name are gone");
    const SkillDef* cleave = data.FindSkill("cleave");
    Check(cleave != nullptr && cleave->Target == SkillTarget::AllEnemies && cleave->Power == 8 &&
              cleave->FocusCost == 4 && cleave->Scaling == "strength",
          "cleave is parsed as an active skill");
}

void TestHotReload()
{
    TempCopy copy;
    Database data(copy.Path);
    DataError error;
    Check(data.Load(&error), "copied sample loads");

    ReplaceInFile(copy.Path / "classes.json", "\"vitality\": 18", "\"vitality\": 30");
    Check(data.CheckForChanges(&error) == 0, "the first sight of a change waits");
    Check(data.GetRevision() == 1, "waiting does not bump the revision");
    const ClassDef* fighter = data.FindClass("fighter");
    Check(fighter != nullptr && fighter->Stats.Vitality == 18,
          "old stats stay until the write settles");
    Check(data.CheckForChanges(&error) == 1, "a settled write reloads");
    fighter = data.FindClass("fighter");
    Check(data.GetRevision() == 2 && fighter != nullptr && fighter->Stats.Vitality == 30,
          "hot reload updates the fighter's vitality");
    Check(data.GetLastError().empty(), "a good reload clears the error");

    WriteFile(copy.Path / "classes.json", "{ not json");
    BumpTime(copy.Path / "classes.json");
    Check(data.CheckForChanges(&error) == 0, "a broken write also waits a check");
    Check(data.CheckForChanges(&error) == 0, "a broken file does not replace the data");
    CheckHas(data.GetLastError(), "classes.json", "the error names the file");
    CheckHas(data.GetLastError(), "not valid JSON", "the error says the JSON is broken");
    fighter = data.FindClass("fighter");
    Check(data.GetRevision() == 2 && fighter != nullptr && fighter->Stats.Vitality == 30,
          "a failed reload keeps the previous fighter");

    // Same broken bytes, new timestamp: reported once, not as a second distinct failure.
    BumpTime(copy.Path / "classes.json");
    const std::string first = data.GetLastError();
    Check(data.CheckForChanges(&error) == 0, "another look at the same break waits");
    Check(data.CheckForChanges(&error) == 0, "the same break does not reload");
    Check(data.GetLastError() == first, "the error stays until the file is fixed");
}

void TestRejected(const fs::path& dir, const char* file, const char* part, const char* what)
{
    Database data(dir);
    DataError error;
    Check(!data.Load(&error), what);
    Check(!data.IsLoaded(), "a rejected directory is not loaded");
    CheckHas(error.Message, file, "the error names the file");
    CheckHas(error.Message, part, what);
}

void TestValidation()
{
    {
        TempCopy copy;
        fs::remove(copy.Path / "skills.json");
        TestRejected(copy.Path, "skills.json", "could not be read", "a missing file is an error");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "stats.json", "[1, 2, 3]");
        TestRejected(copy.Path, "stats.json", "JSON object", "a JSON array is not a data file");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "classes.json", "\"id\": \"berserker\"", "\"id\": \"fighter\"");
        TestRejected(copy.Path, "classes.json", "another class",
                     "duplicate class ids are rejected");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "classes.json", "\"strike\"", "\"smash\"");
        TestRejected(copy.Path, "classes.json", "does not exist", "a missing skill id is named");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "classes.json", "\"passives\": []", "\"passives\": [\"strike\"]");
        TestRejected(copy.Path, "classes.json", "passive",
                     "an active skill cannot be listed as a passive");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "classes.json", "\"vitality\": 18", "\"vitality\": \"lots\"");
        TestRejected(copy.Path, "classes.json", "integer", "a stat must be an integer");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "classes.json", "\"vitality\": 18", "\"strenght\": 18");
        TestRejected(copy.Path, "classes.json", "unknown stat",
                     "a misspelled stat key is rejected");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "classes.json", "\"evolvesFrom\": \"fighter\"",
                      "\"evolvesFrom\": \"knight\"");
        TestRejected(copy.Path, "classes.json", "does not exist",
                     "an unknown parent class is named");
    }
    {
        TempCopy copy;
        const std::string extra = R"({
          "classes": [
            {
              "id": "fighter",
              "name": "Fighter",
              "description": "Base.",
              "stats": {"vitality": 1, "strength": 1, "defense": 1, "agility": 1, "focus": 1},
              "growth": {"vitality": 1, "strength": 1, "defense": 1, "agility": 1, "focus": 1},
              "skills": ["strike"],
              "passives": []
            },
            {
              "id": "berserker",
              "name": "Berserker",
              "description": "First evolution.",
              "evolvesFrom": "fighter",
              "evolveLevel": 5,
              "growth": {},
              "skills": ["rage"],
              "passives": ["bloodlust"]
            },
            {
              "id": "warlord",
              "name": "Warlord",
              "description": "Evolves from an evolution.",
              "evolvesFrom": "berserker",
              "evolveLevel": 10,
              "growth": {},
              "skills": ["cleave"],
              "passives": ["bulwark"]
            }
          ]
        })";
        WriteFile(copy.Path / "classes.json", extra);
        TestRejected(copy.Path, "classes.json", "base class",
                     "an evolution must come from the base class");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "stats.json", "\"id\": \"focus\"", "\"id\": \"luck\"");
        TestRejected(copy.Path, "stats.json", "unknown stat", "stats.json cannot invent a stat");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "items.json", "\"slot\": \"weapon\"", "\"slot\": \"hat\"");
        TestRejected(copy.Path, "items.json", "slot", "an unknown gear slot is rejected");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "items.json", "\"slot\": \"offhand\"",
                      "\"slot\": \"offhand\", \"twoHanded\": true");
        TestRejected(copy.Path, "items.json", "twoHanded", "only a weapon may be two-handed");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "items.json", "\"heal\": 30", "\"heal\": 0");
        TestRejected(copy.Path, "items.json", "heal", "a consumable needs heal or focus");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "shop.json", "\"minor_potion\"", "\"missing_potion\"");
        TestRejected(copy.Path, "shop.json", "does not exist", "shop stock must name a real item");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "shop.json", "\"bagSlots\": 12", "\"bagSlots\": 0");
        TestRejected(copy.Path, "shop.json", "bagSlots", "the bag needs at least one slot");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json", "\"effect\": \"damage\"",
                      "\"effect\": \"explode\"");
        TestRejected(copy.Path, "skills.json", "effect", "an unknown skill effect is rejected");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "combat.json", "\"attackDenominator\": 1",
                      "\"attackDenominator\": 0");
        TestRejected(copy.Path, "combat.json", "attackDenominator",
                     "a combat divisor of zero is rejected");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "enemies.json", "\"bite\"", "\"bloodlust\"");
        TestRejected(copy.Path, "enemies.json", "passive", "enemies list active skills");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "progress.json", "[3, 6, 10, 15]", "0");
        TestRejected(copy.Path, "progress.json", "usesPerRank", "uses per rank must be at least 1");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "progress.json", "20, 40", "20, 20");
        TestRejected(copy.Path, "progress.json", "greater than", "the XP curve must keep rising");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "progress.json", "[0, 8", "[1, 8");
        TestRejected(copy.Path, "progress.json", "level 1", "level 1 XP must be 0");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "enemies.json", "\"id\": \"wolf\"",
                      "\"id\": \"wolf\", \"boss\": true");
        TestRejected(copy.Path, "enemies.json", "exactly one boss", "a second boss is rejected");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "enemies.json", "\"boss\": true", "\"boss\": false");
        TestRejected(copy.Path, "enemies.json", "exactly one boss", "a roster needs a boss");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json", "\"guard\": 50", "\"guard\": 101");
        TestRejected(copy.Path, "skills.json", "guard", "a riposte guard cannot exceed 100");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "progress.json", "[3, 6, 10, 15]", "[3, 6, 6, 15]");
        TestRejected(copy.Path, "progress.json", "strictly increasing",
                     "rank thresholds have to climb");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json", "\"stunWindup\": \"stunned\"",
                      "\"stunWindup\": \"missing\"");
        TestRejected(copy.Path, "skills.json", "does not exist", "a skill status must exist");
    }
}

} // namespace

int main()
{
    TestSample();
    TestHotReload();
    TestValidation();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
