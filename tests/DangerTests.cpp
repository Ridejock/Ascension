// The off-road meter, the encounter roll, and danger.json. No window.

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include "Ascension/Map/Danger.h"
#include "Ascension/Progress/RunSave.h"
#include "Check.h"
#include "Emerald/Save/Save.h"

namespace {

using namespace Ascension;

namespace fs = std::filesystem;

const fs::path kSample = ASCENSION_DATA_DIR;

struct TempCopy {
    fs::path Path;

    TempCopy()
    {
        Path = fs::temp_directory_path() / "ascension-danger-tests";
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

void WriteFile(const fs::path& path, std::string_view text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

DangerRules SampleRules()
{
    DangerRules rules;
    rules.Cap = 100;
    rules.GraceSteps = 3;
    rules.Curve = DangerCurve::Square;
    return rules;
}

void TestRoadNeverRolls()
{
    DangerRules rules = SampleRules();
    DangerMeter meter;
    meter.Value = rules.Cap;
    meter.Grace = 0;
    meter.OnRoad = false;
    const DangerStep step = AdvanceDanger(meter, rules, 0, 0);
    Check(!step.Fight && step.Value == rules.Cap && meter.OnRoad,
          "a road step never fights, even with a full meter and a zero roll");
}

void TestDangerAddsByTerrain()
{
    DangerRules rules = SampleRules();
    DangerMeter meter;
    meter.OnRoad = false;
    meter.Grace = 0;
    const DangerStep meadow = AdvanceDanger(meter, rules, 4, 99);
    const DangerStep forest = AdvanceDanger(meter, rules, 8, 99);
    const DangerStep barrow = AdvanceDanger(meter, rules, 18, 99);
    Check(!meadow.Fight && meadow.Value == 4, "meadow adds its rating");
    Check(!forest.Fight && forest.Value == 12, "forest adds more than meadow");
    Check(!barrow.Fight && barrow.Value == 30, "barrow ground adds the most");

    meter.Value = 95;
    const DangerStep full = AdvanceDanger(meter, rules, 18, 0);
    Check(full.Value == rules.Cap && full.Fight, "the meter stops at the cap, and a full meter fights");
}

void TestResetAndGrace()
{
    DangerRules rules = SampleRules();
    DangerMeter meter;
    meter.Value = 80;
    meter.Grace = 0;
    meter.OnRoad = false;
    ResetDanger(meter, rules);
    Check(meter.Value == 0 && meter.Grace == rules.GraceSteps && !meter.OnRoad,
          "a fight clears the meter and starts grace");
    bool fought = false;
    for (i32 step = 0; step < rules.GraceSteps; ++step) {
        const DangerStep quiet = AdvanceDanger(meter, rules, 8, 0);
        if (quiet.Fight)
            fought = true;
    }
    Check(!fought && meter.Value == 8 * rules.GraceSteps, "grace adds danger and does not roll");
    const DangerStep after = AdvanceDanger(meter, rules, 8, 0);
    Check(after.Fight, "the step after grace can fight");

    DangerMeter walking;
    const DangerStep road = AdvanceDanger(walking, rules, 0, 0);
    Check(!road.Fight && walking.OnRoad, "a zero rating stays on the road");
    const DangerStep left = AdvanceDanger(walking, rules, 8, 0);
    Check(!left.Fight && !walking.OnRoad && walking.Grace == rules.GraceSteps - 1,
          "leaving the road starts the same grace");
}

void TestCurves()
{
    DangerRules square = SampleRules();
    DangerMeter meter;
    meter.OnRoad = false;
    meter.Grace = 0;
    meter.Value = 42;
    const DangerStep low = AdvanceDanger(meter, square, 8, 25);
    Check(meter.Value == 50 && !low.Fight, "square at half a meter ignores a roll of 25");
    meter.Value = 42;
    const DangerStep hit = AdvanceDanger(meter, square, 8, 24);
    Check(hit.Fight, "square at half a meter takes a roll of 24");

    DangerRules linear = SampleRules();
    linear.Curve = DangerCurve::Linear;
    meter.Value = 42;
    meter.Grace = 0;
    const DangerStep miss = AdvanceDanger(meter, linear, 8, 50);
    Check(meter.Value == 50 && !miss.Fight, "linear at 50 ignores a roll of 50");
    meter.Value = 42;
    const DangerStep land = AdvanceDanger(meter, linear, 8, 49);
    Check(land.Fight, "linear at 50 takes a roll of 49");
}

void TestTables()
{
    DangerRules rules;
    DangerTable meadow;
    meadow.Id = "meadow";
    meadow.Tile = "grass";
    meadow.Rows.push_back({"wolf", 1});
    DangerTable forest;
    forest.Id = "forest";
    forest.Map = "overland";
    forest.Tile = "grass";
    forest.Rows.push_back({"wolf", 3});
    forest.Rows.push_back({"wolf_pack", 1});
    rules.Tables.push_back(meadow);
    rules.Tables.push_back(forest);
    const DangerTable* hub = FindDangerTable(rules, "hub", "grass");
    const DangerTable* woods = FindDangerTable(rules, "overland", "grass");
    Check(hub != nullptr && hub->Id == "meadow", "a tile-only table covers the other maps");
    Check(woods != nullptr && woods->Id == "forest", "a map table wins on that map");
    Check(FindDangerTable(rules, "overland", "path") == nullptr, "a tile with no table has no foes");
    Check(PickDangerEncounter(*woods, 0) == "wolf" && PickDangerEncounter(*woods, 2) == "wolf" &&
              PickDangerEncounter(*woods, 3) == "wolf_pack" &&
              PickDangerEncounter(*woods, 4) == "wolf",
          "weights walk in order and then wrap");

    rules.Hints.push_back({1, "uneasy"});
    rules.Hints.push_back({25, "wary"});
    Check(DangerHintFor(rules, 0) == nullptr, "zero has no hint");
    const DangerHint* uneasy = DangerHintFor(rules, 1);
    const DangerHint* wary = DangerHintFor(rules, 40);
    Check(uneasy != nullptr && uneasy->Text == "uneasy" && wary != nullptr && wary->Text == "wary",
          "the highest hint at or below the meter is the one shown");
}

void TestRollSequence()
{
    DangerRng left;
    DangerRng right;
    const u32 first = NextDangerRoll(left, 100);
    Check(first == NextDangerRoll(right, 100) && left.State == right.State && left.State != 1u,
          "the same seed walks the same rolls");
    Check(left.State <= 0x7fffffffu, "the saved roll stays in a positive int");
}

void TestShippedDanger()
{
    Database data(ASCENSION_SHIPPED_DATA_DIR);
    DataError error;
    Check(data.Load(&error), "shipped danger data loads");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    if (!data.IsLoaded())
        return;
    const DangerRules& rules = data.GetDanger();
    Check(rules.Cap == 100 && rules.GraceSteps == 3 && rules.Curve == DangerCurve::Square,
          "the shipped meter is square, cap 100, grace 3");
    Check(DangerRatingOf(rules, "path") == 0, "the road rates 0");
    Check(DangerRatingOf(rules, "floor") == 0 && DangerRatingOf(rules, "stone") == 0,
          "a floor and dungeon stone rate 0");
    Check(DangerRatingOf(rules, "grass") == 4 && DangerRatingOf(rules, "forest") == 8 &&
              DangerRatingOf(rules, "barrow_ground") == 18,
          "meadow is 4, forest is 8, barrow ground is 18");
    i32 unrated = 0;
    for (const TileDef& tile : data.GetTiles()) {
        bool listed = false;
        for (const DangerRating& rating : rules.Ratings) {
            if (rating.Tile == tile.Id)
                listed = true;
        }
        if (!listed)
            ++unrated;
    }
    Check(unrated == 0, "every shipped tile has a danger rating");
    const MapDef* road = data.FindMap("overland");
    Check(road != nullptr, "the overland loads");
    if (road == nullptr)
        return;
    const std::string* start = road->Cell(road->Start.X, road->Start.Y);
    Check(start != nullptr && *start == "path", "the overland starts on the path");
    bool sawForest = false;
    bool sawBarrow = false;
    bool sawGrass = false;
    for (i32 y = 0; y < road->Height; ++y) {
        for (i32 x = 0; x < road->Width; ++x) {
            const std::string* cell = road->Cell(x, y);
            if (cell == nullptr)
                continue;
            if (*cell == "forest")
                sawForest = true;
            else if (*cell == "barrow_ground")
                sawBarrow = true;
            else if (*cell == "grass")
                sawGrass = true;
        }
    }
    Check(sawForest && sawBarrow && !sawGrass, "overland off-road is forest and the approach is barrow ground");
    const MapSpawn* wolf = road->FindSpawn("barrow_wolf");
    const std::string* wolfTile = wolf != nullptr ? road->Cell(wolf->Tile.X, wolf->Tile.Y) : nullptr;
    Check(wolfTile != nullptr && *wolfTile == "path", "the road wolf still stands on the path");

    const DangerTable* meadow = FindDangerTable(rules, "hub", "grass");
    const DangerTable* woods = FindDangerTable(rules, "overland", "forest");
    const DangerTable* verge = FindDangerTable(rules, "overland", "barrow_ground");
    Check(meadow != nullptr && meadow->Id == "meadow" && meadow->Rows.size() == 1 &&
              meadow->Rows[0].Encounter == "wolf" && meadow->Rows[0].Weight == 1,
          "meadow grass is one wolf");
    Check(woods != nullptr && woods->Id == "forest", "the forest has its own table");
    bool pack = false;
    bool forestRows = woods != nullptr;
    i32 forestWeight = 0;
    if (woods != nullptr) {
        for (const DangerRow& row : woods->Rows) {
            if (data.FindEncounter(row.Encounter) == nullptr || row.Weight < 1)
                forestRows = false;
            forestWeight += row.Weight;
            if (row.Encounter == "wolf_pack")
                pack = true;
        }
    }
    Check(forestRows && pack && forestWeight == 4, "the forest is a wolf or a wolf pack, weights 3 and 1");
    bool barrowRows = verge != nullptr && verge->Rows.size() == 3;
    bool bear = false;
    if (verge != nullptr) {
        for (const DangerRow& row : verge->Rows) {
            if (data.FindEncounter(row.Encounter) == nullptr)
                barrowRows = false;
            if (row.Encounter == "bear")
                bear = true;
        }
    }
    Check(barrowRows && bear, "barrow ground fields a wolf, a bandit, and the bear");
    Check(DangerEncounterFlees(data, "wolf") && !DangerEncounterFlees(data, "oathkeeper_varn"),
          "a wolf flees and the boss does not");
    const DangerHint* uneasy = DangerHintFor(rules, 1);
    const DangerHint* wary = DangerHintFor(rules, 25);
    const DangerHint* threatened = DangerHintFor(rules, 70);
    Check(DangerHintFor(rules, 0) == nullptr && uneasy != nullptr && uneasy->Text == "uneasy" &&
              wary != nullptr && wary->Text == "wary" && threatened != nullptr &&
              threatened->Text == "threatened",
          "the hints are uneasy, wary, then threatened");
}

void TestRejectedDanger()
{
    auto load = [](const fs::path& dir, DataError& error) {
        Database data(dir);
        return data.Load(&error);
    };
    {
        TempCopy copy;
        std::error_code error;
        fs::remove(copy.Path / "danger.json", error);
        DataError loadError;
        Check(!load(copy.Path, loadError), "a missing danger file fails");
        CheckHas(loadError.Message, "danger.json", "the missing file is named");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"ratings\": [ { \"tile\": \"floor\", \"rating\": 0 }, "
                  "{ \"tile\": \"wall\", \"rating\": 0 } ], \"tables\": [], \"speed\": 1 } }\n");
        DataError loadError;
        Check(!load(copy.Path, loadError), "an unknown danger field fails");
        CheckHas(loadError.Message, "speed", "the unknown field is named");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"bounce\", "
                  "\"ratings\": [ { \"tile\": \"floor\", \"rating\": 0 }, "
                  "{ \"tile\": \"wall\", \"rating\": 0 } ], \"tables\": [] } }\n");
        DataError loadError;
        Check(!load(copy.Path, loadError), "a bad curve fails");
        CheckHas(loadError.Message, "curve", "the curve is named");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"ratings\": [ { \"tile\": \"floor\", \"rating\": 0 }, "
                  "{ \"tile\": \"missing\", \"rating\": 1 } ], \"tables\": [] } }\n");
        DataError loadError;
        Check(!load(copy.Path, loadError), "an unknown tile fails");
        CheckHas(loadError.Message, "missing", "the missing tile is named");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"ratings\": [ { \"tile\": \"floor\", \"rating\": 0 } ], \"tables\": [] } }\n");
        Database data(copy.Path);
        DataError loadError;
        Check(data.Load(&loadError), "a tile with no rating still loads");
        Check(data.IsLoaded() && DangerRatingOf(data.GetDanger(), "wall") == 0,
              "an unlisted tile rates 0 and never rolls");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"ratings\": [ { \"tile\": \"floor\", \"rating\": 0 }, "
                  "{ \"tile\": \"wall\", \"rating\": 0 } ], \"tables\": [ "
                  "{ \"id\": \"wild\", \"tile\": \"floor\", \"rows\": [ "
                  "{ \"encounter\": \"no_such\", \"weight\": 1 } ] } ] } }\n");
        DataError loadError;
        Check(!load(copy.Path, loadError), "an unknown encounter fails");
        CheckHas(loadError.Message, "no_such", "the missing encounter is named");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"ratings\": [ { \"tile\": \"floor\", \"rating\": 0 }, "
                  "{ \"tile\": \"wall\", \"rating\": 0 } ], \"tables\": [ "
                  "{ \"id\": \"wild\", \"map\": \"no_such\", \"tile\": \"floor\", \"rows\": [ "
                  "{ \"encounter\": \"wolf\", \"weight\": 1 } ] } ] } }\n");
        DataError loadError;
        Check(!load(copy.Path, loadError), "an unknown map fails");
        CheckHas(loadError.Message, "no_such", "the missing map is named");
    }
}

void TestOldSaveLoads()
{
    TempDir dir("ascension-danger-save");
    RunState run;
    run.Career.ClassId = "fighter";
    run.DangerValue = 40;
    run.DangerGrace = 1;
    run.DangerRoad = false;
    run.DangerRng = 9;
    run.Wild = true;
    run.WildReturn.X = 3;
    run.WildReturn.Y = 4;
    run.EncounterId = "wolf";
    RunSave saves(dir.Path);
    std::string why;
    Check(saves.Write(run, why), "a meter save writes");
    const RunLoad loaded = saves.Read();
    Check(static_cast<bool>(loaded) && loaded.Run.DangerValue == 40 && loaded.Run.DangerGrace == 1 &&
              !loaded.Run.DangerRoad && loaded.Run.DangerRng == 9 && loaded.Run.Wild &&
              loaded.Run.WildReturn.X == 3 && loaded.Run.WildReturn.Y == 4 &&
              loaded.Run.EncounterId == "wolf",
          "the meter and the wild fight round trip");

    TempDir older("ascension-danger-old");
    Emerald::SaveData file;
    file.SetString("class", "fighter");
    file.SetInt("xp", 12);
    Emerald::SaveSystem version(older.Path, RunSave::kVersion);
    Check(version.Save("run", file, "old"), "a version 7 file without a meter is written");
    RunSave oldSaves(older.Path);
    const RunLoad old = oldSaves.Read();
    Check(static_cast<bool>(old) && old.Run.DangerValue == 0 && old.Run.DangerGrace == 0 &&
              old.Run.DangerRoad && old.Run.DangerRng == 1 && !old.Run.Wild && old.Run.Career.Xp == 12,
          "an older version 7 file loads with an empty meter");
}

} // namespace

int main()
{
    TestRoadNeverRolls();
    TestDangerAddsByTerrain();
    TestResetAndGrace();
    TestCurves();
    TestTables();
    TestRollSequence();
    TestShippedDanger();
    TestRejectedDanger();
    TestOldSaveLoads();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
