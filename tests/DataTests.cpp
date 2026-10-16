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
    Check(data.GetSkills().size() == 13, "thirteen skills");
    Check(data.GetRules().HpPerVitality == 4 && data.GetRules().MinDamage == 1 &&
              data.GetRules().AttackDenominator == 1 && data.GetRules().DefenseDenominator == 1,
          "combat.json supplies the damage rules");
    Check(data.GetEnemies().size() == 3, "three enemies");
    Check(data.GetItems().size() == 4, "four items");
    const ProgressRules& progress = data.GetProgress();
    Check(progress.XpToReach.size() == 10 && progress.XpToReach[0] == 0 &&
              progress.XpToReach[1] == 8,
          "progress.json has ten steps and level 2 at 8 XP");
    Check(progress.UsesPerRank == 2 && progress.MaxSkillRank == 5 && progress.PowerPerRank == 1,
          "ranks climb every two uses and add one power");

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
              berserkerPassives[0]->Stat == "strength" && berserkerPassives[0]->Amount == 2,
          "berserker's passive is bloodlust");
    const auto guardianPassives = data.Passives(*guardian);
    Check(guardianPassives.size() == 1 && guardianPassives[0]->Id == "bulwark",
          "guardian's passive is bulwark");

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
              wolf->Skills[0] == "bite",
          "wolf is a level-1 enemy with bite");
    const ItemDef* sword = data.FindItem("iron_sword");
    Check(sword != nullptr && sword->Slot == GearSlot::Weapon && sword->Bonus.Strength == 3,
          "iron sword is a weapon with +3 strength");
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
        ReplaceInFile(copy.Path / "progress.json", "\"usesPerRank\": 2", "\"usesPerRank\": 0");
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
