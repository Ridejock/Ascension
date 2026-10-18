// Loader tests: the sample data, validation errors, and hot reload. No window or GPU.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "Ascension/Data/Database.h"
#include "Check.h"
#include "GearLine.h"

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
    Check(data.GetSkills().size() == 20, "twenty skills");
    Check(data.GetRules().HpPerVitality == 4 && data.GetRules().MinDamage == 1 &&
              data.GetRules().AttackDenominator == 1 && data.GetRules().DefenseDenominator == 1 &&
              data.GetRules().FocusBase == 4 && data.GetRules().FocusPerTurn == 1 &&
              data.GetRules().FocusOnWait == 2,
          "combat.json supplies the damage rules and focus regen");
    Check(data.GetEnemies().size() == 7, "seven enemies, counting the boss and the shard");
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

    Check(!fighter->IsEvolution() && fighter->Starter, "fighter is the base class and a starter");
    Check(berserker != nullptr && !berserker->Starter && guardian != nullptr && !guardian->Starter,
          "evolutions are not starters");
    Check(fighter->Stats.Vitality == 18 && fighter->Stats.Strength == 8 &&
              fighter->Stats.Defense == 6 && fighter->Stats.Agility == 5 &&
              fighter->Stats.Focus == 4,
          "fighter level-1 stats");
    const auto fighterSkills = data.ActiveSkills(*fighter);
    Check(fighterSkills.size() == 4 && fighterSkills[0]->Id == "strike" &&
              fighterSkills[1]->Id == "brace" && fighterSkills[2]->Id == "heavy_blow" &&
              fighterSkills[3]->Id == "second_wind",
          "fighter has strike, brace, heavy blow and second wind");
    const EffectDef* strikeHit = fighterSkills[0]->FindEffect("hit");
    const EffectDef* braceWard = fighterSkills[1]->FindEffect("ward");
    Check(strikeHit != nullptr && strikeHit->Op == EffectOp::Damage && strikeHit->Power == 10,
          "strike is a damage skill");
    Check(braceWard != nullptr && braceWard->Op == EffectOp::Guard && braceWard->Amount == 50,
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
    const EffectDef* fury =
        berserkerPassives.empty() ? nullptr : berserkerPassives[0]->FindEffect("fury");
    Check(berserkerPassives.size() == 1 && berserkerPassives[0]->Id == "bloodlust" &&
              berserkerPassives[0]->Kind == SkillKind::Passive && fury != nullptr &&
              fury->Op == EffectOp::StatPerMissingHp && fury->Stat == "strength" &&
              fury->Amount == 1 && fury->Per == 10 && fury->Max == 6,
          "bloodlust is +1 strength per 10 percent missing, up to 6");
    const auto guardianPassives = data.Passives(*guardian);
    const EffectDef* bulwarkDef =
        guardianPassives.empty() ? nullptr : guardianPassives[0]->FindEffect("def");
    const EffectDef* bulwarkFocus =
        guardianPassives.empty() ? nullptr : guardianPassives[0]->FindEffect("focus");
    const SkillDef* catalogBulwark = data.FindSkill("bulwark");
    Check(guardianPassives.size() == 1 && guardianPassives[0]->Id == "bulwark" &&
              bulwarkDef != nullptr && bulwarkDef->Op == EffectOp::Stat &&
              bulwarkDef->Stat == "defense" && bulwarkDef->Amount == 3 && bulwarkFocus != nullptr &&
              bulwarkFocus->Trigger == EffectTrigger::OnBlock &&
              bulwarkFocus->Op == EffectOp::GrantFocus && bulwarkFocus->Amount == 1,
          "bulwark is +3 defense and 1 focus when a guard stops a hit");
    Check(catalogBulwark != nullptr && guardianPassives.size() == 1 &&
              catalogBulwark != guardianPassives[0] &&
              catalogBulwark->FindEffect("focus") != nullptr &&
              catalogBulwark->FindEffect("focus")->Amount == 1,
          "a string grant is its own copy of the catalog skill");
    const SkillDef* riposte = data.FindSkill("riposte");
    const EffectDef* riposteWard = riposte != nullptr ? riposte->FindEffect("ward") : nullptr;
    const EffectDef* riposteBack = riposte != nullptr ? riposte->FindEffect("back") : nullptr;
    Check(riposte != nullptr && riposte->FocusCost == 2 && riposteWard != nullptr &&
              riposteWard->Op == EffectOp::Guard && riposteWard->Amount == 50 &&
              riposteBack != nullptr && riposteBack->Op == EffectOp::Reflect &&
              riposteBack->Percent == 50 && riposteBack->Power == 10 &&
              riposteBack->Scaling == "defense",
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
              wolf->Skills[0].Id == "bite" && !wolf->Boss,
          "wolf is a level-1 enemy with bite");
    const auto enemies = data.GetEnemies();
    Check(enemies.size() == 7 && enemies[0].Id == "wolf" && enemies[1].Id == "bandit" &&
              enemies[2].Id == "tomb_moth" && enemies[3].Id == "bone_archer" &&
              enemies[4].Id == "barrow_sentry" && enemies[5].Id == "oathkeeper_varn" &&
              enemies[6].Id == "rubble_shard",
          "the roster's boss is Oathkeeper Varn, and the shard follows");
    Check(enemies[2].Level == 4 && enemies[2].Experience == 14 && enemies[2].Gold == 4,
          "the tomb moth pays 14 XP and 4 gold");
    Check(enemies[3].Level == 5 && enemies[3].Experience == 22 && enemies[3].Gold == 8 &&
              enemies[3].Skills.size() == 1 && enemies[3].Skills[0].Id == "aimed_shot",
          "the bone archer pays 22 XP and 8 gold");
    const SkillDef* aimed = data.FindSkill("aimed_shot");
    const EffectDef* aimedHit = aimed != nullptr ? aimed->FindEffect("hit") : nullptr;
    Check(aimed != nullptr && aimedHit != nullptr && aimedHit->Power == 20 && aimed->Windup == 2,
          "aimed shot is power 20 after a 2-turn wind-up");
    Check(enemies[5].Experience == 80 && enemies[5].Boss && enemies[5].Level == 8 &&
              enemies[5].Stats.Vitality == 60 && enemies[5].Stats.Strength == 14 &&
              enemies[5].Stats.Defense == 12 && enemies[5].Skills.size() == 3 &&
              enemies[5].PhaseAtPercent == 50 && enemies[5].SummonId == "rubble_shard" &&
              enemies[5].SummonEvery == 3,
          "Oathkeeper Varn is the only boss");
    Check(enemies.size() == 7 && enemies[6].Id == "rubble_shard" && enemies[6].Experience == 0 &&
              enemies[6].Skills.size() == 1 && enemies[6].Skills[0].Id == "rubble_strike",
          "the rubble shard is an enemy Varn can call");
    const auto encounters = data.GetEncounters();
    Check(encounters.size() == 7 && encounters[0].Id == "wolf" &&
              encounters[0].Enemies.size() == 1 && encounters[4].Id == "wolf_pack" &&
              encounters[4].Enemies.size() == 3 && encounters[4].Enemies[0] == "wolf" &&
              encounters[4].Enemies[2] == "wolf",
          "the wolf pack is three wolves");
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
    const EffectDef* cleaveHit = cleave != nullptr ? cleave->FindEffect("hit") : nullptr;
    Check(cleave != nullptr && cleave->Target == SkillTarget::AllEnemies && cleaveHit != nullptr &&
              cleaveHit->Power == 8 && cleave->FocusCost == 4 && cleaveHit->Scaling == "strength" &&
              cleaveHit->Aim == EffectAim::AllEnemies &&
              data.DescribeSkill(*cleave).find("Hits every enemy.") != std::string::npos,
          "cleave is parsed as an active skill that hits every enemy");
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
        ReplaceInFile(copy.Path / "skills.json", "\"op\": \"damage\"", "\"op\": \"explode\"");
        TestRejected(copy.Path, "skills.json", "explode", "an unknown op is rejected");
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
        ReplaceInFile(copy.Path / "skills.json", "\"amount\": 50", "\"amount\": 101");
        TestRejected(copy.Path, "skills.json", "guard", "a guard percent cannot exceed 100");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "progress.json", "[3, 6, 10, 15]", "[3, 6, 6, 15]");
        TestRejected(copy.Path, "progress.json", "strictly increasing",
                     "rank thresholds have to climb");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json", "\"status\": \"stunned\"",
                      "\"status\": \"missing\"");
        TestRejected(copy.Path, "skills.json", "does not exist", "a skill status must exist");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json", "\"trigger\": \"onUse\"",
                      "\"trigger\": \"whenever\"");
        TestRejected(copy.Path, "skills.json", "whenever", "an unknown trigger is rejected");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json",
                      "\"id\": \"focus\", \"trigger\": \"onBlock\", \"op\": \"grantFocus\", "
                      "\"amount\": 1 }\n      ]\n    },",
                      "\"id\": \"focus\", \"trigger\": \"onBlock\", \"op\": \"grantFocus\", "
                      "\"amount\": 1 }\n      ],\n      \"tune\": { \"focus\": { \"ammount\": 9 } "
                      "}\n    },");
        TestRejected(copy.Path, "skills.json", "ammount",
                     "a misspelled tune field names the field");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "classes.json", "\"passives\": [\"bulwark\"]",
                      "\"passives\": [{ \"skill\": \"bulwark\", \"tune\": { \"focus\": { "
                      "\"per\": 2 } } }]");
        TestRejected(copy.Path, "classes.json", "per",
                     "a tune cannot add a field the effect does not have");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json",
                      "{ \"id\": \"hit\", \"trigger\": \"onUse\", \"op\": \"damage\", \"power\": "
                      "10, \"scaling\": \"strength\", \"target\": \"target\" }",
                      "{ \"id\": \"hit\", \"recipe\": \"missing_blow\" }");
        TestRejected(copy.Path, "skills.json", "missing_blow", "a missing effect recipe is named");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "classes.json", "\"starter\": true", "\"starter\": false");
        TestRejected(copy.Path, "classes.json", "starter", "a roster needs a starter class");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "classes.json",
                      "\"id\": \"berserker\",\n      \"name\": \"Berserker\",\n      "
                      "\"description\": \"Trades caution for blood. Offered when a {from} "
                      "reaches level {evolveLevel}.\",\n      \"evolvesFrom\": \"fighter\"",
                      "\"id\": \"berserker\",\n      \"name\": \"Berserker\",\n      "
                      "\"description\": \"Trades caution for blood. Offered when a {from} "
                      "reaches level {evolveLevel}.\",\n      \"starter\": true,\n      "
                      "\"evolvesFrom\": \"fighter\"");
        TestRejected(copy.Path, "classes.json", "starter", "an evolution cannot be a starter");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json", "\"target\": \"target\"",
                      "\"target\": \"sideways\"");
        TestRejected(copy.Path, "skills.json", "randomEnemy",
                     "an effect target must be a known aim");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "encounters.json",
                      "\"id\": \"wolf_pack\",\n      \"name\": \"Wolf Pack\",\n      \"enemies\": "
                      "[\"wolf\", \"wolf\", \"wolf\"]",
                      "\"id\": \"wolf_pack\",\n      \"name\": \"Wolf Pack\",\n      \"note\": "
                      "\"pack\",\n      \"enemies\": [\"wolf\", \"wolf\", \"wolf\"]");
        TestRejected(copy.Path, "encounters.json", "note", "an encounter rejects an unknown field");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "encounters.json", "\"enemies\": [\"wolf\", \"wolf\", \"wolf\"]",
                      "\"enemies\": [\"wolf\", \"wolf\", \"wolf\", \"wolf\"]");
        TestRejected(copy.Path, "encounters.json", "more than 3",
                     "an encounter cannot list more than 3 enemies");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "encounters.json", "\"enemies\": [\"wolf\", \"wolf\", \"wolf\"]",
                      "\"enemies\": [\"wolf\", \"dragon\"]");
        TestRejected(copy.Path, "encounters.json", "dragon", "an encounter enemy must exist");
    }
}

const EffectDef* EffectOf(const SkillDef* skill, const char* effectId)
{
    return skill != nullptr ? skill->FindEffect(effectId) : nullptr;
}

void TestLayers()
{
    TempCopy copy;
    ReplaceInFile(
        copy.Path / "skills.json",
        "        { \"id\": \"shell\", \"trigger\": \"onUse\", \"op\": \"harden\", "
        "\"amount\": 6, \"turns\": 2 }\n      ]\n    }\n  ]\n}",
        "        { \"id\": \"shell\", \"trigger\": \"onUse\", \"op\": \"harden\", "
        "\"amount\": 6, \"turns\": 2 }\n      ]\n    },\n    {\n      \"id\": \"oath\",\n "
        "     \"name\": \"Oath\",\n      \"description\": \"A recipe tuned twice.\",\n   "
        "   \"kind\": \"passive\",\n      \"effects\": [\n        { \"id\": \"def\", "
        "\"recipe\": \"stat\" },\n        { \"id\": \"focus\", \"recipe\": "
        "\"grant_focus\" }\n      ],\n      \"tune\": {\n        \"def\": { \"stat\": "
        "\"defense\", \"amount\": 4 },\n        \"focus\": { \"amount\": 1 }\n      "
        "}\n    }\n  ]\n}");
    ReplaceInFile(
        copy.Path / "classes.json", "      \"passives\": [\"bulwark\"]\n    }\n  ]\n}",
        "      \"passives\": [\"bulwark\"]\n    },\n    {\n      \"id\": \"paladin\",\n   "
        "   \"name\": \"Paladin\",\n      \"description\": \"Keeps Bulwark, tuned.\",\n   "
        "   \"evolvesFrom\": \"fighter\",\n      \"evolveLevel\": 5,\n      \"bonus\": "
        "{},\n      \"growth\": {},\n      \"skills\": [\"shield_wall\"],\n      "
        "\"passives\": [\n        { \"skill\": \"bulwark\", \"tune\": { \"focus\": { "
        "\"amount\": 2 } } },\n        { \"skill\": \"oath\", \"tune\": { \"focus\": { "
        "\"amount\": 5 } } }\n      ]\n    }\n  ]\n}");
    ReplaceInFile(copy.Path / "items.json",
                  "\"id\": \"wolfhide_charm\",\n      \"name\": \"Wolfhide Charm\",\n      "
                  "\"description\": \"A twist of wolfhide. The step comes easier.\",\n      "
                  "\"slot\": \"accessory\",\n      \"bonus\": { \"agility\": 2 },\n      "
                  "\"value\": 35",
                  "\"id\": \"wolfhide_charm\",\n      \"name\": \"Wolfhide Charm\",\n      "
                  "\"description\": \"A twist of wolfhide. The step comes easier.\",\n      "
                  "\"slot\": \"accessory\",\n      \"bonus\": { \"agility\": 2 },\n      "
                  "\"grants\": [{ \"skill\": \"bulwark\", \"tune\": { \"focus\": { \"amount\": 4 } "
                  "} }],\n      \"value\": 35");
    ReplaceInFile(copy.Path / "enemies.json", "\"skills\": [\"bite\"]",
                  "\"skills\": [{ \"skill\": \"bite\", \"tune\": { \"hit\": { \"power\": 9 } } }]");

    Database data(copy.Path);
    DataError error;
    Check(data.Load(&error), "layered grants load");
    if (!error.Message.empty())
        std::printf("    message was: %s\n", error.Message.c_str());

    const SkillDef* catalogBulwark = data.FindSkill("bulwark");
    const SkillDef* catalogOath = data.FindSkill("oath");
    const ClassDef* guardian = data.FindClass("guardian");
    const ClassDef* paladin = data.FindClass("paladin");
    const EffectDef* catalogFocus = EffectOf(catalogBulwark, "focus");
    const EffectDef* oathDef = EffectOf(catalogOath, "def");
    const EffectDef* oathFocus = EffectOf(catalogOath, "focus");
    Check(catalogFocus != nullptr && catalogFocus->Amount == 1,
          "the catalog bulwark still grants 1 focus");
    Check(oathDef != nullptr && oathDef->Stat == "defense" && oathDef->Amount == 4 &&
              oathFocus != nullptr && oathFocus->Amount == 1,
          "the skill tune replaces the recipe's stat and amount");

    const SkillDef* guardianBulwark = nullptr;
    const SkillDef* paladinBulwark = nullptr;
    const SkillDef* paladinOath = nullptr;
    if (guardian != nullptr) {
        for (const SkillDef* passive : data.Passives(*guardian)) {
            if (passive->Id == "bulwark")
                guardianBulwark = passive;
        }
    }
    if (paladin != nullptr) {
        for (const SkillDef* passive : data.Passives(*paladin)) {
            if (passive->Id == "bulwark")
                paladinBulwark = passive;
            if (passive->Id == "oath")
                paladinOath = passive;
        }
    }
    const EffectDef* guardianFocus = EffectOf(guardianBulwark, "focus");
    const EffectDef* paladinFocus = EffectOf(paladinBulwark, "focus");
    const EffectDef* paladinDef = EffectOf(paladinBulwark, "def");
    const EffectDef* grantedOathFocus = EffectOf(paladinOath, "focus");
    const EffectDef* grantedOathDef = EffectOf(paladinOath, "def");
    Check(guardianBulwark != nullptr && paladinBulwark != nullptr &&
              guardianBulwark != paladinBulwark && guardianBulwark != catalogBulwark &&
              paladinBulwark != catalogBulwark && guardianFocus != nullptr &&
              guardianFocus->Amount == 1 && paladinFocus != nullptr && paladinFocus->Amount == 2 &&
              paladinDef != nullptr && paladinDef->Amount == 3,
          "each class grant is its own copy, and the tune changes only focus");
    Check(paladinOath != nullptr && paladinOath != catalogOath && grantedOathFocus != nullptr &&
              grantedOathFocus->Amount == 5 && grantedOathDef != nullptr &&
              grantedOathDef->Amount == 4 && grantedOathDef->Stat == "defense",
          "a grant tune overrides the skill tune and leaves the other effect alone");

    const ItemDef* charm = data.FindItem("wolfhide_charm");
    const EffectDef* charmFocus = charm != nullptr && charm->Grants.size() == 1
                                      ? EffectOf(&charm->Grants[0].Resolved, "focus")
                                      : nullptr;
    Check(charm != nullptr && charm->Grants.size() == 1 &&
              &charm->Grants[0].Resolved != catalogBulwark && charmFocus != nullptr &&
              charmFocus->Amount == 4 && catalogFocus != nullptr && catalogFocus->Amount == 1,
          "an item grant resolves its own tuned copy and leaves the catalog alone");

    const EnemyDef* wolf = data.FindEnemy("wolf");
    const SkillDef* catalogBite = data.FindSkill("bite");
    const EffectDef* wolfHit = wolf != nullptr && wolf->Skills.size() == 1
                                   ? EffectOf(&wolf->Skills[0].Resolved, "hit")
                                   : nullptr;
    const EffectDef* catalogHit = EffectOf(catalogBite, "hit");
    Check(wolfHit != nullptr && wolfHit->Power == 9 && catalogHit != nullptr &&
              catalogHit->Power == 6 && &wolf->Skills[0].Resolved != catalogBite,
          "an enemy grant tunes power on its own copy");

    ReplaceInFile(copy.Path / "skills.json", "\"op\": \"damage\"", "\"op\": \"explode\"");
    Check(data.CheckForChanges(&error) == 0, "a bad op waits a check");
    Check(data.CheckForChanges(&error) == 0, "a bad op does not replace the data");
    CheckHas(data.GetLastError(), "skills.json", "the reload error names the file");
    CheckHas(data.GetLastError(), "explode", "the reload error names the op");
    const EffectDef* still = EffectOf(data.FindSkill("strike"), "hit");
    Check(still != nullptr && still->Power == 10, "the previous strike power stays");
}

void ExpectUnused(const fs::path& dir, const char* file, const char* skill, const char* effect,
                  const char* field, const char* what)
{
    Database data(dir);
    DataError error;
    Check(!data.Load(&error), what);
    Check(!data.IsLoaded(), "a rejected directory is not loaded");
    CheckHas(error.Message, file, "the error names the file");
    CheckHas(error.Message, skill, "the error names the skill");
    CheckHas(error.Message, effect, "the error names the effect");
    CheckHas(error.Message, field, "the error names the unused field");
}

void TestUnusedFields()
{
    {
        TempCopy copy;
        ReplaceInFile(
            copy.Path / "skills.json",
            "{ \"id\": \"mend\", \"trigger\": \"onUse\", \"op\": \"heal\", \"power\": 8 }",
            "{ \"id\": \"mend\", \"trigger\": \"onUse\", \"op\": \"heal\", \"power\": 8, "
            "\"scaling\": \"vitality\" },\n        { \"id\": \"mark\", \"trigger\": "
            "\"onUse\", \"op\": \"applyStatus\", \"status\": \"bleed\", \"every\": 2 }");
        ReplaceInFile(copy.Path / "skills.json",
                      "{ \"id\": \"focus\", \"trigger\": \"onBlock\", \"op\": \"grantFocus\", "
                      "\"amount\": 1 }",
                      "{ \"id\": \"focus\", \"trigger\": \"onBlock\", \"op\": \"grantFocus\", "
                      "\"amount\": 1 },\n        { \"id\": \"cut\", \"trigger\": \"onDamaged\", "
                      "\"op\": \"applyStatus\", \"status\": \"bleed\" },\n        { \"id\": "
                      "\"rest\", \"trigger\": \"onTurnStart\", \"op\": \"heal\", \"power\": 4 },\n "
                      "       { \"id\": \"pulse\", \"trigger\": \"onTurnStart\", \"op\": "
                      "\"grantFocus\", \"amount\": 1 },\n        { \"id\": \"sting\", \"trigger\": "
                      "\"onDamaged\", \"op\": \"grantFocus\", \"amount\": 2 }");
        Database data(copy.Path);
        DataError error;
        Check(data.Load(&error), "fields a trigger and op actually read still load");
        const EffectDef* mend = EffectOf(data.FindSkill("second_wind"), "mend");
        const EffectDef* mark = EffectOf(data.FindSkill("second_wind"), "mark");
        const EffectDef* cut = EffectOf(data.FindSkill("bulwark"), "cut");
        const EffectDef* rest = EffectOf(data.FindSkill("bulwark"), "rest");
        const EffectDef* pulse = EffectOf(data.FindSkill("bulwark"), "pulse");
        const EffectDef* sting = EffectOf(data.FindSkill("bulwark"), "sting");
        Check(mend != nullptr && mend->Scaling == "vitality" && mark != nullptr &&
                  mark->Every == 2 && cut != nullptr && cut->Status == "bleed" &&
                  !cut->Every.has_value() && rest != nullptr && rest->Power == 4 &&
                  !rest->Scaling.has_value() && pulse != nullptr && pulse->Amount == 1 &&
                  sting != nullptr && sting->Amount == 2,
              "onUse keeps scaling and every; onTurnStart heal and onDamaged status do not");
    }
    {
        TempCopy copy;
        ReplaceInFile(
            copy.Path / "skills.json",
            "{ \"id\": \"mend\", \"trigger\": \"onUse\", \"op\": \"heal\", \"power\": 8 }",
            "{ \"id\": \"mend\", \"trigger\": \"onTurnStart\", \"op\": \"heal\", "
            "\"power\": 8, \"scaling\": \"vitality\" }");
        ExpectUnused(copy.Path, "skills.json", "second_wind", "mend", "scaling",
                     "onTurnStart heal does not use scaling");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json",
                      "{ \"id\": \"bleed\", \"trigger\": \"onHit\", \"op\": \"applyStatus\", "
                      "\"status\": \"bleed\", \"every\": 3 }",
                      "{ \"id\": \"bleed\", \"trigger\": \"onDamaged\", \"op\": \"applyStatus\", "
                      "\"status\": \"bleed\", \"every\": 3 }");
        ExpectUnused(copy.Path, "skills.json", "slash", "bleed", "every",
                     "onDamaged status does not use every");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json",
                      "{ \"id\": \"stun\", \"trigger\": \"onHit\", \"op\": \"stunIfWindingUp\", "
                      "\"status\": \"stunned\" }",
                      "{ \"id\": \"stun\", \"trigger\": \"onHit\", \"op\": \"stunIfWindingUp\", "
                      "\"status\": \"stunned\", \"every\": 2 }");
        ExpectUnused(copy.Path, "skills.json", "heavy_blow", "stun", "every",
                     "stunIfWindingUp does not use every");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json",
                      "{ \"id\": \"focus\", \"trigger\": \"onBlock\", \"op\": \"grantFocus\", "
                      "\"amount\": 1 }",
                      "{ \"id\": \"focus\", \"trigger\": \"onBlock\", \"op\": \"grantFocus\", "
                      "\"amount\": 1, \"turns\": 2 }");
        ExpectUnused(copy.Path, "skills.json", "bulwark", "focus", "turns",
                     "grantFocus does not use turns");
    }
    {
        TempCopy copy;
        ReplaceInFile(
            copy.Path / "skills.json",
            "{ \"id\": \"ward\", \"trigger\": \"onUse\", \"op\": \"guard\", \"amount\": 50 }",
            "{ \"id\": \"ward\", \"trigger\": \"onUse\", \"op\": \"guard\", \"amount\": 50, "
            "\"target\": \"self\" }");
        ExpectUnused(copy.Path, "skills.json", "brace", "ward", "target",
                     "a guard does not read target");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "effects.json",
                      "\"id\": \"heal\",\n      \"trigger\": \"onUse\",\n      \"op\": \"heal\",\n "
                      "     \"power\": 0",
                      "\"id\": \"heal\",\n      \"trigger\": \"onTurnStart\",\n      \"op\": "
                      "\"heal\",\n      \"power\": 0,\n      \"scaling\": \"strength\"");
        Database data(copy.Path);
        DataError error;
        Check(!data.Load(&error), "a recipe rejects a field its trigger does not read");
        CheckHas(error.Message, "effects.json", "the recipe error names the file");
        CheckHas(error.Message, "heal", "the recipe error names the effect");
        CheckHas(error.Message, "scaling", "the recipe error names the field");
    }
    {
        TempCopy copy;
        Database data(copy.Path);
        DataError error;
        Check(data.Load(&error), "the sample loads before a bad field edit");
        ReplaceInFile(copy.Path / "skills.json",
                      "{ \"id\": \"bleed\", \"trigger\": \"onHit\", \"op\": \"applyStatus\", "
                      "\"status\": \"bleed\", \"every\": 3 }",
                      "{ \"id\": \"bleed\", \"trigger\": \"onDamaged\", \"op\": \"applyStatus\", "
                      "\"status\": \"bleed\", \"every\": 3 }");
        Check(data.CheckForChanges(&error) == 0, "an unused field waits a check");
        Check(data.CheckForChanges(&error) == 0, "an unused field does not replace the data");
        CheckHas(data.GetLastError(), "skills.json", "the reload error names the file");
        CheckHas(data.GetLastError(), "slash", "the reload error names the skill");
        CheckHas(data.GetLastError(), "bleed", "the reload error names the effect");
        CheckHas(data.GetLastError(), "every", "the reload error names the field");
        const EffectDef* strike = EffectOf(data.FindSkill("strike"), "hit");
        const EffectDef* bleed = EffectOf(data.FindSkill("slash"), "bleed");
        Check(data.GetRevision() == 1 && strike != nullptr && strike->Power == 10 &&
                  bleed != nullptr && bleed->Trigger == EffectTrigger::OnHit && bleed->Every == 3,
              "the previous skills stay after the rejected reload");
    }
}

void TestResists()
{
    const char* const kEnraged = "\"damageTakenPercent\": 15,";
    {
        TempCopy unknown;
        ReplaceInFile(unknown.Path / "statuses.json", kEnraged,
                      "\"damageTakenPercent\": 15, \"resists\": [\"dazed\"],");
        Database data(unknown.Path);
        DataError error;
        Check(!data.Load(&error), "a status cannot resist an id that does not exist");
        CheckHas(error.Message, "statuses.json", "the error names the file");
        CheckHas(error.Message, "enraged", "the error names the status");
        CheckHas(error.Message, "resists", "the error names the field");
        CheckHas(error.Message, "dazed", "the error names the missing status");
    }
    {
        TempCopy self;
        ReplaceInFile(self.Path / "statuses.json", kEnraged,
                      "\"damageTakenPercent\": 15, \"resists\": [\"enraged\"],");
        Database data(self.Path);
        DataError error;
        Check(!data.Load(&error), "a status cannot resist itself");
        CheckHas(error.Message, "statuses.json", "the error names the file");
        CheckHas(error.Message, "enraged", "the error names the status");
        CheckHas(error.Message, "resists", "the error names the field");
        CheckHas(error.Message, "itself", "the error says why");
    }
    {
        TempCopy good;
        ReplaceInFile(good.Path / "statuses.json", kEnraged,
                      "\"damageTakenPercent\": 15, \"resists\": [\"stunned\"],");
        Database data(good.Path);
        DataError error;
        Check(data.Load(&error), "enraged may resist stunned");
        const StatusDef* enraged = data.FindStatus("enraged");
        Check(enraged != nullptr && enraged->Resists.size() == 1 &&
                  enraged->Resists[0] == "stunned",
              "the resist list is read");
    }
}

const HeroGrant* FindGrant(const std::vector<HeroGrant>& grants, std::string_view id)
{
    for (const HeroGrant& grant : grants)
        if (grant.Skill.Id == id)
            return &grant;
    return nullptr;
}

bool HasOrigin(const HeroGrant& grant, GrantSource source, std::string_view id)
{
    for (const GrantOrigin& origin : grant.Origins) {
        if (origin.Source == source && origin.Id == id)
            return true;
    }
    return false;
}

void TestItemGrants()
{
    {
        TempCopy drink;
        ReplaceInFile(drink.Path / "items.json", "\"heal\": 30,",
                      "\"heal\": 30, \"grants\": [\"bulwark\"],");
        Database data(drink.Path);
        DataError error;
        Check(!data.Load(&error), "a consumable cannot grant a skill");
        CheckHas(error.Message, "items.json", "the error names the file");
        CheckHas(error.Message, "minor_potion", "the error names the item");
        CheckHas(error.Message, "grants", "the error names the field");
    }
    {
        TempCopy two;
        ReplaceInFile(two.Path / "items.json", "\"bonus\": { \"strength\": 1 },",
                      "\"bonus\": { \"strength\": 1 }, \"grants\": [\"cleave\", \"strike\"],");
        Database data(two.Path);
        DataError error;
        Check(!data.Load(&error), "an item cannot grant two active skills");
        CheckHas(error.Message, "items.json", "the error names the file");
        CheckHas(error.Message, "rusty_sword", "the error names the item");
        CheckHas(error.Message, "one active", "the error says an item grants one active");
    }
    {
        TempCopy missing;
        ReplaceInFile(missing.Path / "effects.json",
                      "    \"every\": \"lower\",\n    \"focusCost\": \"lower\"",
                      "    \"every\": \"lower\"");
        Database data(missing.Path);
        DataError error;
        Check(!data.Load(&error), "a tunable number with no direction is rejected");
        CheckHas(error.Message, "effects.json", "the error names the file");
        CheckHas(error.Message, "focusCost", "the error names the field");
        CheckHas(error.Message, "better", "the error says the direction is missing");
    }
    {
        TempCopy worn;
        ReplaceInFile(worn.Path / "items.json", "\"bonus\": { \"strength\": 1 },",
                      "\"bonus\": { \"strength\": 1 }, \"grants\": [{ \"skill\": \"cleave\", "
                      "\"tune\": { \"focusCost\": 1, \"hit\": { \"power\": 5, \"target\": "
                      "\"target\" } } }],");
        ReplaceInFile(worn.Path / "items.json", "\"bonus\": { \"defense\": 6, \"agility\": -1 },",
                      "\"bonus\": { \"defense\": 6, \"agility\": -1 }, \"grants\": [{ \"skill\": "
                      "\"cleave\", \"tune\": { \"focusCost\": 6, \"hit\": { \"power\": 12 } } }],");
        ReplaceInFile(
            worn.Path / "items.json", "\"bonus\": { \"agility\": 2 },",
            "\"bonus\": { \"agility\": 2 }, \"grants\": [{ \"skill\": \"bulwark\", \"tune\": { "
            "\"def\": { \"amount\": 1 }, \"focus\": { \"amount\": 4 } } }, { \"skill\": "
            "\"bloodlust\", \"tune\": { \"fury\": { \"amount\": 3, \"per\": 5, \"max\": 2 } } }],");
        Database data(worn.Path);
        DataError error;
        Check(data.Load(&error), "gear may grant one active and any number of passives");
        const ItemDef* sword = data.FindItem("rusty_sword");
        const ItemDef* charm = data.FindItem("wolfhide_charm");
        Check(sword != nullptr && sword->Grants.size() == 1 &&
                  sword->Grants[0].Resolved.Kind == SkillKind::Active,
              "the sword grants one active");
        Check(charm != nullptr && charm->Grants.size() == 2 &&
                  charm->Grants[0].Resolved.Kind == SkillKind::Passive &&
                  charm->Grants[1].Resolved.Kind == SkillKind::Passive,
              "passives on one item are not capped");

        const ClassDef* fighter = data.FindClass("fighter");
        const ClassDef* berserker = data.FindClass("berserker");
        const ClassDef* guardian = data.FindClass("guardian");
        const std::vector<std::string> wornIds{"rusty_sword", "oathkeepers_plate", "wolfhide_charm",
                                               "no_such_item"};
        std::vector<HeroGrant> fighterGrants;
        std::vector<HeroGrant> berserkerGrants;
        std::vector<HeroGrant> guardianGrants;
        if (fighter != nullptr)
            fighterGrants = data.HeroGrants(*fighter, wornIds);
        if (berserker != nullptr)
            berserkerGrants = data.HeroGrants(*berserker, wornIds);
        if (guardian != nullptr)
            guardianGrants = data.HeroGrants(*guardian, wornIds);

        const HeroGrant* fighterCleave = FindGrant(fighterGrants, "cleave");
        const EffectDef* fighterHit =
            fighterCleave != nullptr ? fighterCleave->Skill.FindEffect("hit") : nullptr;
        Check(fighterGrants.size() == 7 && fighterCleave != nullptr &&
                  fighterCleave->Skill.FocusCost == 1 && fighterHit != nullptr &&
                  fighterHit->Power == 12 && fighterHit->Aim == EffectAim::Target,
              "per field: lower focusCost, higher power, and the first item's target");
        Check(fighterCleave != nullptr && fighterCleave->Origins.size() == 2 &&
                  HasOrigin(*fighterCleave, GrantSource::Item, "rusty_sword") &&
                  HasOrigin(*fighterCleave, GrantSource::Item, "oathkeepers_plate") &&
                  &fighterGrants[4] == fighterCleave,
              "a new item skill comes after the class skills, once");

        const HeroGrant* berserkerCleave = FindGrant(berserkerGrants, "cleave");
        const EffectDef* berserkerHit =
            berserkerCleave != nullptr ? berserkerCleave->Skill.FindEffect("hit") : nullptr;
        const HeroGrant* bloodlust = FindGrant(berserkerGrants, "bloodlust");
        const EffectDef* fury =
            bloodlust != nullptr ? bloodlust->Skill.FindEffect("fury") : nullptr;
        usize cleaves = 0;
        for (const HeroGrant& grant : berserkerGrants) {
            if (grant.Skill.Id == "cleave")
                ++cleaves;
        }
        Check(cleaves == 1 && berserkerCleave != nullptr && berserkerCleave->Skill.FocusCost == 1 &&
                  berserkerHit != nullptr && berserkerHit->Power == 12 &&
                  berserkerHit->Aim == EffectAim::AllEnemies,
              "the class copy wins a non-numeric field; numbers still merge");
        Check(berserkerCleave != nullptr &&
                  HasOrigin(*berserkerCleave, GrantSource::Class, "berserker") &&
                  berserkerCleave->Origins.size() == 3 && &berserkerGrants[5] == berserkerCleave,
              "the merged cleave keeps the class hotkey slot");
        Check(fury != nullptr && fury->Amount == 3 && fury->Per == 5 && fury->Max == 6,
              "lower per wins, and higher amount and max win, on one passive");

        const HeroGrant* bulwark = FindGrant(guardianGrants, "bulwark");
        const EffectDef* defense = bulwark != nullptr ? bulwark->Skill.FindEffect("def") : nullptr;
        const EffectDef* focus = bulwark != nullptr ? bulwark->Skill.FindEffect("focus") : nullptr;
        Check(bulwark != nullptr && bulwark->Origins.size() == 2 &&
                  HasOrigin(*bulwark, GrantSource::Class, "guardian") &&
                  HasOrigin(*bulwark, GrantSource::Item, "wolfhide_charm") && defense != nullptr &&
                  defense->Amount == 3 && focus != nullptr && focus->Amount == 4,
              "bulwark keeps the class defense and the charm's focus");
    }
}

bool Braced(const std::string& text)
{
    return text.find('{') != std::string::npos || text.find('}') != std::string::npos;
}

void NoteBrace(const std::string& label, const std::string& text, i32& bad)
{
    if (!Braced(text))
        return;
    ++bad;
    std::printf("    braced %s: %s\n", label.c_str(), text.c_str());
}

i32 BracedDescriptions(const Database& data)
{
    i32 bad = 0;
    for (const StatDef& stat : data.GetStats())
        NoteBrace("stat " + stat.Id, stat.Description, bad);
    for (const SkillDef& skill : data.GetSkills())
        NoteBrace("skill " + skill.Id, data.DescribeSkill(skill), bad);
    for (const StatusDef& status : data.GetStatuses())
        NoteBrace("status " + status.Id, data.Describe(status), bad);
    for (const ClassDef& klass : data.GetClasses()) {
        NoteBrace("class " + klass.Id, data.Describe(klass), bad);
        for (const SkillGrant& grant : klass.Skills)
            NoteBrace(klass.Id + ":" + grant.Id, data.DescribeSkill(grant.Resolved), bad);
        for (const SkillGrant& grant : klass.Passives)
            NoteBrace(klass.Id + ":" + grant.Id, data.DescribeSkill(grant.Resolved), bad);
    }
    for (const EnemyDef& enemy : data.GetEnemies()) {
        NoteBrace("enemy " + enemy.Id, data.Describe(enemy), bad);
        for (const SkillGrant& grant : enemy.Skills)
            NoteBrace(enemy.Id + ":" + grant.Id, data.DescribeSkill(grant.Resolved), bad);
    }
    for (const ItemDef& item : data.GetItems()) {
        NoteBrace("item " + item.Id, data.Describe(item), bad);
        for (const SkillGrant& grant : item.Grants)
            NoteBrace(item.Id + ":" + grant.Id, data.DescribeSkill(grant.Resolved), bad);
    }
    return bad;
}

void TestDescriptionsResolve()
{
    Database data(kSample);
    DataError error;
    Check(data.Load(&error), "tests/data loads with templated descriptions");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    if (!data.IsLoaded())
        return;
    Check(BracedDescriptions(data) == 0, "every tests/data description resolves");

    const ClassDef* fighter = data.FindClass("fighter");
    Check(fighter != nullptr &&
              data.Describe(*fighter).find("a Berserker or a Guardian") != std::string::npos,
          "paths join with ' or a '");
    const SkillDef* riposte = data.FindSkill("riposte");
    Check(riposte != nullptr &&
              data.DescribeSkill(*riposte).find("Guard 50 percent") != std::string::npos,
          "riposte prints the guard percent");
    const SkillDef* bulwark = data.FindSkill("bulwark");
    Check(bulwark != nullptr &&
              data.DescribeSkill(*bulwark).find("Defense +3.") != std::string::npos &&
              data.DescribeSkill(*bulwark).find("returns 1 focus.") != std::string::npos,
          "bulwark prints its stat and focus");
    const EnemyDef* varn = data.FindEnemy("oathkeeper_varn");
    Check(varn != nullptr && data.Describe(*varn).find("Rubble Shard") != std::string::npos &&
              data.Describe(*varn).find("50 percent") != std::string::npos,
          "the boss text names the phase and the summon");

    const ItemDef* potion = data.FindItem("minor_potion");
    const ItemDef* tonic = data.FindItem("focus_tonic");
    const ItemDef* axe = data.FindItem("notched_greataxe");
    Check(potion != nullptr && GearLine(data, *potion, true).find("heal 30") != std::string::npos,
          "a shop row keeps the heal");
    Check(tonic != nullptr && GearLine(data, *tonic, true).find("focus +4") != std::string::npos,
          "a shop row keeps the focus");
    Check(axe != nullptr && GearLine(data, *axe, true).find("Strength +6") != std::string::npos &&
              GearLine(data, *axe, true).find("Defense -2") != std::string::npos &&
              GearLine(data, *axe, true).find("two handed") != std::string::npos &&
              GearLine(data, *axe, false).find("two handed") == std::string::npos &&
              GearLine(data, *axe, false).find("Strength +6") != std::string::npos,
          "rows use stat names, and a worn row skips the handling words");
}

void TestShippedDescriptions()
{
    Database data(ASCENSION_SHIPPED_DATA_DIR);
    DataError error;
    Check(data.Load(&error), "shipped data loads");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    if (!data.IsLoaded())
        return;
    Check(BracedDescriptions(data) == 0, "shipped descriptions resolve with no braces");
}

void TestDescriptionRejected(const fs::path& dir, const char* skill, const char* token,
                             const char* what)
{
    Database data(dir);
    DataError error;
    Check(!data.Load(&error), what);
    CheckHas(error.Message, "skills.json", what);
    CheckHas(error.Message, skill, what);
    CheckHas(error.Message, token, what);
}

void TestBadDescriptions()
{
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json", "{def.amount}", "{def.amout}");
        TestDescriptionRejected(copy.Path, "bulwark", "{def.amout}",
                                "a misspelled field is named with the skill");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json", "A straightforward blow.", "A {hit.status} blow.");
        TestDescriptionRejected(copy.Path, "strike", "{hit.status}",
                                "a placeholder for a missing field names the skill");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json", "A straightforward blow.", "Wait {windup}.");
        TestDescriptionRejected(copy.Path, "strike", "{windup}",
                                "windup on a skill without one names the skill");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json",
                      "{def.stat} +{def.amount}. A guard that actually stops damage also returns "
                      "{focus.amount} focus.",
                      "Defense +{def.amount");
        TestDescriptionRejected(copy.Path, "bulwark", "{def.amount",
                                "an unclosed brace names the skill");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json",
                      "{def.stat} +{def.amount}. A guard that actually stops damage also returns "
                      "{focus.amount} focus.",
                      "Defense }");
        TestDescriptionRejected(copy.Path, "bulwark", "}", "a stray close brace names the skill");
    }
}

void TestTunedAndRankedText()
{
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "items.json", "\"bonus\": { \"defense\": 2 },",
                      "\"bonus\": { \"defense\": 2 }, \"grants\": [{ \"skill\": \"bulwark\", "
                      "\"tune\": { \"def\": { \"amount\": 1 }, \"focus\": { \"amount\": 2 } } }],");
        Database data(copy.Path);
        DataError error;
        Check(data.Load(&error), "a tuned bulwark grant loads");
        if (!error.Message.empty())
            std::printf("    %s\n", error.Message.c_str());
        const ItemDef* shield = data.FindItem("oak_shield");
        const SkillDef* tuned = nullptr;
        if (shield != nullptr && !shield->Grants.empty())
            tuned = &shield->Grants[0].Resolved;
        const std::string text = tuned != nullptr ? data.DescribeSkill(*tuned) : std::string();
        Check(text.find("Defense +1.") != std::string::npos &&
                  text.find("returns 2 focus.") != std::string::npos,
              "the grant copy prints its own amounts");
        const std::string row = shield != nullptr ? GearLine(data, *shield, true) : std::string();
        Check(row.find("grants Bulwark") != std::string::npos &&
                  row.find("Defense +2") != std::string::npos,
              "the row names the grant and the stat");

        ReplaceInFile(copy.Path / "stats.json", "\"name\": \"Defense\"", "\"name\": \"Guard\"");
        Database renamed(copy.Path);
        DataError again;
        Check(renamed.Load(&again), "a renamed stat still loads");
        const ItemDef* renamedShield = renamed.FindItem("oak_shield");
        const std::string renamedRow =
            renamedShield != nullptr ? GearLine(renamed, *renamedShield, true) : std::string();
        Check(renamedRow.find("Guard +2") != std::string::npos &&
                  renamedRow.find("Defense") == std::string::npos &&
                  renamedRow.find("grants Bulwark") != std::string::npos,
              "the row label follows stats.json");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "skills.json",
                      "A committed swing. It stuns a foe who is winding up.",
                      "power {hit.power}, cost {cost}.");
        Database data(copy.Path);
        DataError error;
        Check(data.Load(&error), "a heavy blow that names its power loads");
        const SkillDef* heavy = data.FindSkill("heavy_blow");
        const std::string ranked = heavy != nullptr ? data.DescribeSkill(*heavy, 3) : std::string();
        Check(ranked.find("power 22, cost 2.") != std::string::npos &&
                  ranked.find("Hits the marked enemy.") != std::string::npos,
              "rank 3 adds two power per rank and keeps the cost and the aim sentence");
        const std::string base = heavy != nullptr ? data.DescribeSkill(*heavy) : std::string();
        Check(base.find("power 18, cost 2.") != std::string::npos, "rank 1 is the base power");
    }
}

} // namespace

int main()
{
    TestSample();
    TestHotReload();
    TestValidation();
    TestLayers();
    TestUnusedFields();
    TestResists();
    TestItemGrants();
    TestDescriptionsResolve();
    TestShippedDescriptions();
    TestBadDescriptions();
    TestTunedAndRankedText();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
