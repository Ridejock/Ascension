// The off-road meter, the encounter roll, the encounter signs, and danger.json. No window.

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
    Check(!step.Fight && step.Paused && step.Value == rules.Cap && meter.OnRoad &&
              meter.Value == rules.Cap,
          "a road step freezes a full meter and does not roll");
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
    Check(!road.Fight && !road.Paused && walking.OnRoad && walking.Value == 0,
          "an empty road is not paused");
    const DangerStep left = AdvanceDanger(walking, rules, 8, 0);
    Check(!left.Fight && !left.Paused && !walking.OnRoad && walking.Value == 8 &&
              walking.Grace == rules.GraceSteps - 1,
          "leaving an empty road starts grace and adds only that step");
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

DangerSign MakeSign(DangerSignKind kind, const char* id, i32 at, const char* tile, const char* text)
{
    DangerSign sign;
    sign.Kind = kind;
    sign.Id = id;
    sign.At = at;
    sign.Tile = tile;
    sign.Text = text;
    return sign;
}

void TestSigns()
{
    DangerRules rules = SampleRules();
    rules.Ratings.push_back({"path", 0});
    rules.Ratings.push_back({"grass", 4});
    rules.Ratings.push_back({"forest", 8});
    rules.Hints.push_back({1, "uneasy"});
    rules.Hints.push_back({25, "wary"});
    rules.PauseText = "paused";

    DangerSign prints = MakeSign(DangerSignKind::Tracks, "prints", 8, "forest", "prints");
    prints.HasColor = true;
    prints.Red = 1;
    prints.Marks.push_back({6, 80, 12});
    DangerSign tracks = MakeSign(DangerSignKind::Tracks, "tracks", 32, "forest", "tracks");
    tracks.HasColor = true;
    tracks.Red = 2;
    tracks.Marks.push_back({6, 80, 12});
    tracks.Marks.push_back({82, 4, 12});
    DangerSign howl = MakeSign(DangerSignKind::Howl, "howl", 40, "forest", "a howl");
    howl.Steps = 2;
    DangerSign hunted = MakeSign(DangerSignKind::Hint, "hunted", 48, "forest", "hunted");
    DangerSign meadow = MakeSign(DangerSignKind::Howl, "meadow_howl", 20, "grass", "a distant howl");
    meadow.Steps = 1;
    DangerSign spoor = MakeSign(DangerSignKind::Tracks, "spoor", 8, "forest", "spoor");
    spoor.Map = "overland";
    spoor.HasColor = true;
    spoor.Red = 3;
    spoor.Marks.push_back({4, 4, 8});
    rules.Signs.push_back(prints);
    rules.Signs.push_back(tracks);
    rules.Signs.push_back(howl);
    rules.Signs.push_back(hunted);
    rules.Signs.push_back(meadow);
    rules.Signs.push_back(spoor);

    DangerSignsState state;
    std::vector<DangerMarkSpot> marks;
    Check(DangerCueLine(rules, state, 0, "overland", "forest").empty(), "zero has no cue line");
    CollectDangerMarks(rules, 0, "overland", "forest", 3, 4, marks);
    Check(marks.empty(), "zero draws no tracks");
    Check(DangerCueLine(rules, state, 50, "overland", "path") == "wary - paused",
          "a road shows the held meter as paused");
    Check(DangerCueLine(rules, state, 0, "overland", "path").empty(),
          "an empty meter on the road stays blank");
    CollectDangerMarks(rules, 50, "overland", "path", 1, 1, marks);
    Check(marks.empty(), "a road draws no tracks");

    Check(DangerCueLine(rules, state, 8, "hub", "forest") == "uneasy - prints",
          "the first forest tier is the generic hint plus prints");
    CollectDangerMarks(rules, 8, "hub", "forest", 2, 5, marks);
    Check(marks.size() == 1 && marks[0].TileX == 2 && marks[0].TileY == 5 && marks[0].Red == 1 &&
              marks[0].X == 6 && marks[0].Size == 12,
          "prints are one square on the hero's tile");
    Check(DangerCueLine(rules, state, 32, "hub", "forest") == "wary - tracks",
          "a higher tracks sign replaces the word");
    CollectDangerMarks(rules, 32, "hub", "forest", 2, 5, marks);
    Check(marks.size() == 2 && marks[1].Red == 2, "the higher tracks sign is the one drawn");
    Check(DangerCueLine(rules, state, 8, "overland", "forest") == "uneasy - spoor",
          "a map sign wins over the tile-only tracks");
    CollectDangerMarks(rules, 8, "overland", "forest", 2, 5, marks);
    Check(marks.size() == 1 && marks[0].Red == 3, "the map sign's mark is the one drawn");

    Check(DangerCueLine(rules, state, 30, "hub", "forest") == "wary - prints",
          "below the terrain hint, the generic word stays");
    Check(DangerCueLine(rules, state, 48, "hub", "forest") == "hunted - tracks",
          "the terrain hint replaces the generic word");
    Check(DangerCueLine(rules, state, 48, "hub", "grass") == "wary",
          "another tile keeps the generic hint");

    AdvanceDangerSigns(state, rules, 40, "hub", "forest");
    Check(DangerCueLine(rules, state, 40, "hub", "forest") == "wary - tracks - a howl",
          "crossing the howl shows it once");
    AdvanceDangerSigns(state, rules, 40, "hub", "forest");
    Check(DangerCueLine(rules, state, 40, "hub", "forest") == "wary - tracks - a howl",
          "the howl stays for its second step");
    AdvanceDangerSigns(state, rules, 40, "hub", "forest");
    Check(DangerCueLine(rules, state, 40, "hub", "forest") == "wary - tracks",
          "the howl does not repeat while the meter stays up");

    DangerSignsState again;
    AdvanceDangerSigns(again, rules, 40, "hub", "forest");
    Check(DangerCueLine(rules, again, 40, "hub", "path") == "wary - paused",
          "a road shows the held meter, not the live howl");
    AdvanceDangerSigns(again, rules, 40, "hub", "path");
    AdvanceDangerSigns(again, rules, 40, "hub", "forest");
    Check(DangerCueLine(rules, again, 40, "hub", "forest") == "wary - tracks",
          "a road step counts the howl down, and it does not start again");

    DangerSignsState fresh;
    AdvanceDangerSigns(fresh, rules, 40, "hub", "grass");
    Check(DangerCueLine(rules, fresh, 40, "hub", "grass") == "wary - a distant howl",
          "grass has its own howl");
    AdvanceDangerSigns(fresh, rules, 40, "hub", "grass");
    Check(DangerCueLine(rules, fresh, 40, "hub", "grass") == "wary",
          "a one-step howl is gone on the next step");

    DangerSignsState loaded;
    SettleDangerSigns(loaded, rules, 40, "hub", "forest");
    AdvanceDangerSigns(loaded, rules, 40, "hub", "forest");
    Check(DangerCueLine(rules, loaded, 40, "hub", "forest") == "wary - tracks",
          "a loaded meter does not howl for a threshold it has already passed");
    ClearDangerSigns(loaded);
    AdvanceDangerSigns(loaded, rules, 40, "hub", "forest");
    Check(DangerCueLine(rules, loaded, 40, "hub", "forest") == "wary - tracks - a howl",
          "clearing the cues lets the next climb howl again");
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
    Check(rules.CueJoin == " - " && rules.PauseText == "paused",
          "the cue line joins with a spaced hyphen and names the pause");

    const DangerSign* meadowPrints = nullptr;
    const DangerSign* meadowTracks = nullptr;
    const DangerSign* meadowHowl = nullptr;
    const DangerSign* meadowStalked = nullptr;
    const DangerSign* meadowHunted = nullptr;
    const DangerSign* forestPrints = nullptr;
    const DangerSign* forestTracks = nullptr;
    const DangerSign* forestHowl = nullptr;
    const DangerSign* forestHunted = nullptr;
    const DangerSign* forestClose = nullptr;
    const DangerSign* barrowScuffs = nullptr;
    const DangerSign* barrowHowl = nullptr;
    const DangerSign* barrowHunted = nullptr;
    const DangerSign* barrowCornered = nullptr;
    for (const DangerSign& sign : rules.Signs) {
        if (sign.Id == "meadow_prints")
            meadowPrints = &sign;
        else if (sign.Id == "meadow_tracks")
            meadowTracks = &sign;
        else if (sign.Id == "meadow_howl")
            meadowHowl = &sign;
        else if (sign.Id == "meadow_stalked")
            meadowStalked = &sign;
        else if (sign.Id == "meadow_hunted")
            meadowHunted = &sign;
        else if (sign.Id == "forest_prints")
            forestPrints = &sign;
        else if (sign.Id == "forest_tracks")
            forestTracks = &sign;
        else if (sign.Id == "forest_howl")
            forestHowl = &sign;
        else if (sign.Id == "forest_hunted")
            forestHunted = &sign;
        else if (sign.Id == "forest_close")
            forestClose = &sign;
        else if (sign.Id == "barrow_scuffs")
            barrowScuffs = &sign;
        else if (sign.Id == "barrow_howl")
            barrowHowl = &sign;
        else if (sign.Id == "barrow_hunted")
            barrowHunted = &sign;
        else if (sign.Id == "barrow_cornered")
            barrowCornered = &sign;
    }
    const bool meadowOk = meadowPrints != nullptr && meadowTracks != nullptr &&
                          meadowHowl != nullptr && meadowStalked != nullptr &&
                          meadowHunted != nullptr && meadowPrints->Kind == DangerSignKind::Tracks &&
                          meadowPrints->At == 16 && meadowPrints->Tile == "grass" &&
                          meadowPrints->Text == "prints" && meadowPrints->Marks.size() == 1 &&
                          meadowPrints->Red == 0x99 && meadowPrints->Green == 0x68 &&
                          meadowPrints->Blue == 0x38 && meadowTracks->At == 36 &&
                          meadowTracks->Text == "tracks" && meadowTracks->Marks.size() == 2 &&
                          meadowHowl->Kind == DangerSignKind::Howl && meadowHowl->At == 48 &&
                          meadowHowl->Text == "a distant howl" && meadowHowl->Steps == 2 &&
                          meadowStalked->Kind == DangerSignKind::Hint && meadowStalked->At == 56 &&
                          meadowStalked->Text == "stalked" && meadowHunted->At == 84 &&
                          meadowHunted->Text == "hunted";
    Check(meadowOk, "meadow signs are prints, tracks, a distant howl, stalked, then hunted");
    const bool forestOk =
        forestPrints != nullptr && forestTracks != nullptr && forestHowl != nullptr &&
        forestHunted != nullptr && forestClose != nullptr && forestPrints->At == 8 &&
        forestPrints->Text == "prints" && forestPrints->Marks.size() == 1 &&
        forestPrints->Marks[0].X == 6 && forestPrints->Marks[0].Y == 80 &&
        forestPrints->Marks[0].Size == 12 && forestPrints->Red == 0xB0 &&
        forestPrints->Green == 0x7A && forestPrints->Blue == 0x44 && forestTracks->At == 32 &&
        forestTracks->Text == "tracks" && forestTracks->Marks.size() == 2 &&
        forestHowl->At == 40 && forestHowl->Text == "a howl" && forestHowl->Steps == 2 &&
        forestHunted->Text == "hunted" && forestHunted->At == 48 && forestClose->Text == "close" &&
        forestClose->At == 72;
    Check(forestOk, "forest signs are prints, tracks, a howl, hunted, then close");
    const bool barrowOk = barrowScuffs != nullptr && barrowHowl != nullptr &&
                          barrowHunted != nullptr && barrowCornered != nullptr &&
                          barrowScuffs->At == 18 && barrowScuffs->Tile == "barrow_ground" &&
                          barrowScuffs->Text == "scuffs" && barrowScuffs->Marks.size() == 2 &&
                          barrowScuffs->Red == 0x1A && barrowScuffs->Green == 0x14 &&
                          barrowScuffs->Blue == 0x23 && barrowHowl->At == 36 &&
                          barrowHowl->Text == "a low howl" && barrowHowl->Steps == 2 &&
                          barrowHunted->At == 36 && barrowHunted->Text == "hunted" &&
                          barrowCornered->At == 70 && barrowCornered->Text == "cornered";
    Check(barrowOk, "barrow ground signs are scuffs, a low howl, hunted, then cornered");
    DangerSignsState quiet;
    Check(DangerCueLine(rules, quiet, 8, "overland", "forest") == "uneasy - prints",
          "the shipped forest opens with prints");
    Check(DangerCueLine(rules, quiet, 18, "overland", "barrow_ground") == "uneasy - scuffs",
          "the shipped barrow ground opens with scuffs");
    Check(DangerCueLine(rules, quiet, 80, "overland", "path") == "threatened - paused",
          "a held meter on the road reads as paused");
    Check(DangerCueLine(rules, quiet, 24, "overland", "path") == "uneasy - paused",
          "the pause line keeps the band the meter had reached");
    Check(DangerCueLine(rules, quiet, 80, "sluice", "floor") == "threatened - paused",
          "a safe floor holds the same paused line");
    Check(DangerCueLine(rules, quiet, 0, "overland", "path").empty(),
          "a clear meter on the road stays blank");
    std::vector<DangerMarkSpot> roadMarks;
    CollectDangerMarks(rules, 80, "overland", "path", 1, 1, roadMarks);
    Check(roadMarks.empty(), "the shipped road draws no tracks while the meter is held");
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
        Check(data.GetDanger().Signs.empty() && data.GetDanger().CueJoin == kDefaultCueJoin &&
                  data.GetDanger().PauseText.empty(),
              "a file with no signs keeps the default join and no pause word");
        DangerSignsState quiet;
        Check(data.IsLoaded() &&
                  DangerCueLine(data.GetDanger(), quiet, 10, "hub", "floor").empty(),
              "a file with no pause word leaves the safe line blank");
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
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"cueJoin\": \" / \", \"ratings\": [ { \"tile\": \"floor\", \"rating\": 1 }, "
                  "{ \"tile\": \"wall\", \"rating\": 0 } ], \"tables\": [], \"signs\": [ "
                  "{ \"id\": \"scuff\", \"kind\": \"tracks\", \"at\": 10, \"tile\": \"floor\", "
                  "\"text\": \"scuffs\", \"color\": \"#112233\", \"marks\": [ "
                  "{ \"x\": 4, \"y\": 80, \"size\": 10 } ] }, "
                  "{ \"id\": \"cry\", \"kind\": \"howl\", \"at\": 20, \"tile\": \"floor\", "
                  "\"text\": \"a howl\", \"steps\": 2 }, "
                  "{ \"id\": \"mean\", \"kind\": \"hint\", \"at\": 30, \"tile\": \"floor\", "
                  "\"text\": \"close\" } ] } }\n");
        Database data(copy.Path);
        DataError loadError;
        Check(data.Load(&loadError), "signs load");
        if (!loadError.Message.empty())
            std::printf("    %s\n", loadError.Message.c_str());
        bool loaded = data.IsLoaded() && data.GetDanger().CueJoin == " / " &&
                      data.GetDanger().Signs.size() == 3;
        const DangerSign* scuff = nullptr;
        const DangerSign* cry = nullptr;
        const DangerSign* mean = nullptr;
        if (data.IsLoaded()) {
            for (const DangerSign& sign : data.GetDanger().Signs) {
                if (sign.Id == "scuff")
                    scuff = &sign;
                else if (sign.Id == "cry")
                    cry = &sign;
                else if (sign.Id == "mean")
                    mean = &sign;
            }
        }
        loaded = loaded && scuff != nullptr && scuff->Kind == DangerSignKind::Tracks &&
                 scuff->HasColor && scuff->Red == 0x11 && scuff->Green == 0x22 &&
                 scuff->Blue == 0x33 && scuff->Marks.size() == 1 && scuff->Marks[0].X == 4 &&
                 cry != nullptr && cry->Steps == 2 && cry->Text == "a howl" && mean != nullptr &&
                 mean->Kind == DangerSignKind::Hint && mean->Text == "close";
        Check(loaded, "a tracks sign, a howl, and a hint keep their fields");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"ratings\": [ { \"tile\": \"floor\", \"rating\": 0 }, "
                  "{ \"tile\": \"wall\", \"rating\": 0 } ], \"tables\": [], \"signs\": [ "
                  "{ \"id\": \"bad\", \"kind\": \"sound\", \"at\": 1, \"text\": \"no\" } ] } }\n");
        DataError loadError;
        Check(!load(copy.Path, loadError), "a bad sign kind fails");
        CheckHas(loadError.Message, "sound", "the kind is named");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"ratings\": [ { \"tile\": \"floor\", \"rating\": 0 }, "
                  "{ \"tile\": \"wall\", \"rating\": 0 } ], \"tables\": [], \"signs\": [ "
                  "{ \"id\": \"bad\", \"kind\": \"howl\", \"at\": 1, \"text\": \"a howl\", "
                  "\"steps\": 1, \"volume\": 2 } ] } }\n");
        DataError loadError;
        Check(!load(copy.Path, loadError), "an unknown sign field fails");
        CheckHas(loadError.Message, "volume", "the unknown sign field is named");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"ratings\": [ { \"tile\": \"floor\", \"rating\": 0 }, "
                  "{ \"tile\": \"wall\", \"rating\": 0 } ], \"tables\": [], \"signs\": [ "
                  "{ \"id\": \"bad\", \"kind\": \"howl\", \"at\": 1, \"steps\": 1 } ] } }\n");
        DataError loadError;
        Check(!load(copy.Path, loadError), "a howl with no text fails");
        CheckHas(loadError.Message, "text", "the missing howl text is named");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"ratings\": [ { \"tile\": \"floor\", \"rating\": 0 }, "
                  "{ \"tile\": \"wall\", \"rating\": 0 } ], \"tables\": [], \"signs\": [ "
                  "{ \"id\": \"bad\", \"kind\": \"tracks\", \"at\": 1, \"tile\": \"floor\", "
                  "\"marks\": [ { \"x\": 4, \"y\": 4, \"size\": 8 } ] } ] } }\n");
        DataError loadError;
        Check(!load(copy.Path, loadError), "tracks marks need a color");
        CheckHas(loadError.Message, "color", "the missing mark color is named");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"ratings\": [ { \"tile\": \"floor\", \"rating\": 0 }, "
                  "{ \"tile\": \"wall\", \"rating\": 0 } ], \"tables\": [], \"signs\": [ "
                  "{ \"id\": \"bad\", \"kind\": \"hint\", \"at\": 1, \"text\": \"close\", "
                  "\"steps\": 2 } ] } }\n");
        DataError loadError;
        Check(!load(copy.Path, loadError), "a hint does not take steps");
        CheckHas(loadError.Message, "steps", "the hint steps field is named");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"ratings\": [ { \"tile\": \"floor\", \"rating\": 0 }, "
                  "{ \"tile\": \"wall\", \"rating\": 0 } ], \"tables\": [], \"signs\": [ "
                  "{ \"id\": \"bad\", \"kind\": \"hint\", \"at\": 1, \"tile\": \"missing\", "
                  "\"text\": \"close\" } ] } }\n");
        DataError loadError;
        Check(!load(copy.Path, loadError), "a sign with an unknown tile fails");
        CheckHas(loadError.Message, "missing", "the missing sign tile is named");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"ratings\": [ { \"tile\": \"floor\", \"rating\": 0 }, "
                  "{ \"tile\": \"wall\", \"rating\": 0 } ], \"tables\": [], \"signs\": [ "
                  "{ \"id\": \"bad\", \"kind\": \"hint\", \"at\": 1, \"map\": \"no_such\", "
                  "\"text\": \"close\" } ] } }\n");
        DataError loadError;
        Check(!load(copy.Path, loadError), "a sign with an unknown map fails");
        CheckHas(loadError.Message, "no_such", "the missing sign map is named");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"paused\": \"paused\", \"ratings\": [ { \"tile\": \"floor\", \"rating\": 0 }, "
                  "{ \"tile\": \"wall\", \"rating\": 0 } ], \"tables\": [] } }\n");
        Database data(copy.Path);
        DataError loadError;
        Check(data.Load(&loadError), "a pause word loads");
        Check(data.IsLoaded() && data.GetDanger().PauseText == "paused",
              "the pause word is kept");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"paused\": \"\", \"ratings\": [ { \"tile\": \"floor\", \"rating\": 0 }, "
                  "{ \"tile\": \"wall\", \"rating\": 0 } ], \"tables\": [] } }\n");
        DataError loadError;
        Check(!load(copy.Path, loadError), "an empty pause word fails");
        CheckHas(loadError.Message, "paused", "the pause word is named");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "danger.json",
                  "{ \"danger\": { \"cap\": 100, \"graceSteps\": 0, \"curve\": \"linear\", "
                  "\"paused\": \"this pause word is too long\", \"ratings\": [ "
                  "{ \"tile\": \"floor\", \"rating\": 0 }, "
                  "{ \"tile\": \"wall\", \"rating\": 0 } ], "
                  "\"tables\": [] } }\n");
        DataError loadError;
        Check(!load(copy.Path, loadError), "a long pause word fails");
        CheckHas(loadError.Message, "paused", "the long pause word is named");
    }
}

void TestRoadHoldsMeter()
{
    DangerRules rules = SampleRules();
    rules.PauseText = "paused";
    rules.Ratings.push_back({"path", 0});
    rules.Ratings.push_back({"floor", 0});
    rules.Ratings.push_back({"forest", 8});
    rules.Hints.push_back({1, "uneasy"});
    rules.Hints.push_back({25, "wary"});
    rules.Hints.push_back({70, "threatened"});
    DangerSign prints = MakeSign(DangerSignKind::Tracks, "prints", 8, "forest", "prints");
    prints.HasColor = true;
    prints.Red = 1;
    prints.Marks.push_back({6, 80, 12});
    DangerSign howl = MakeSign(DangerSignKind::Howl, "howl", 40, "forest", "a howl");
    howl.Steps = 2;
    rules.Signs.push_back(prints);
    rules.Signs.push_back(howl);

    DangerMeter meter;
    meter.OnRoad = false;
    meter.Grace = 0;
    for (i32 step = 0; step < 5; ++step) {
        const DangerStep built = AdvanceDanger(meter, rules, 8, 99);
        Check(!built.Fight && !built.Paused && built.Value == 8 * (step + 1),
              "off-road steps add the tile and do not pause");
    }
    Check(meter.Value == 40, "five forest steps reach 40");

    DangerSignsState signs;
    AdvanceDangerSigns(signs, rules, meter.Value, "overland", "forest");
    Check(DangerCueLine(rules, signs, meter.Value, "overland", "forest") ==
              "wary - prints - a howl",
          "the wild line is the band, the tracks, and the howl");

    const i32 held = meter.Value;
    const DangerStep onto = AdvanceDanger(meter, rules, 0, 0);
    AdvanceDangerSigns(signs, rules, meter.Value, "overland", "path");
    Check(!onto.Fight && onto.Paused && onto.Value == held && meter.Value == held && meter.OnRoad &&
              meter.Grace == 0,
          "stepping onto the road freezes the meter and reports paused");
    Check(DangerCueLine(rules, signs, meter.Value, "overland", "path") == "wary - paused",
          "the road line is the held band plus paused");
    Check(DangerCueLine(rules, signs, meter.Value, "sluice", "floor") == "wary - paused",
          "a safe floor uses the same paused line");
    std::vector<DangerMarkSpot> marks;
    CollectDangerMarks(rules, meter.Value, "overland", "path", 2, 3, marks);
    Check(marks.empty(), "the road draws no tracks");

    for (i32 step = 0; step < 3; ++step) {
        const DangerStep walk = AdvanceDanger(meter, rules, 0, 0);
        AdvanceDangerSigns(signs, rules, meter.Value, "overland", "path");
        Check(!walk.Fight && walk.Paused && walk.Value == held && meter.Value == held &&
                  meter.Grace == 0,
              "walking the road keeps the value and stays paused");
    }
    Check(DangerCueLine(rules, signs, meter.Value, "overland", "path") == "wary - paused",
          "the pause line stays while the hero walks the road");

    const DangerStep back = AdvanceDanger(meter, rules, 8, 0);
    AdvanceDangerSigns(signs, rules, meter.Value, "overland", "forest");
    Check(!back.Fight && !back.Paused && back.Value == held + 8 && meter.Value == held + 8,
          "the first step off the road adds only that tile");
    Check(meter.Grace == rules.GraceSteps - 1,
          "leaving the road arms grace, so that step does not fight");
    Check(DangerCueLine(rules, signs, meter.Value, "overland", "forest") == "wary - prints",
          "tracks return at the resumed band");
    std::vector<DangerMarkSpot> returned;
    CollectDangerMarks(rules, meter.Value, "overland", "forest", 4, 5, returned);
    Check(returned.size() == 1 && returned[0].Size == 12,
          "the same prints are back, not a new mark");

    const i32 resumed = meter.Value;
    const DangerStep next = AdvanceDanger(meter, rules, 8, 0);
    Check(!next.Fight && next.Value == resumed + 8 && next.Value != rules.Cap,
          "the next step adds the tile again, and nothing more");

    ResetDanger(meter, rules);
    ClearDangerSigns(signs);
    Check(meter.Value == 0 && meter.Grace == rules.GraceSteps && !meter.OnRoad,
          "a fight clears the held risk and starts grace");
    Check(DangerCueLine(rules, signs, meter.Value, "overland", "forest").empty(),
          "a fight clears the cue on the ground where it happened");
    DangerMeter side = meter;
    const DangerStep cleared = AdvanceDanger(side, rules, 0, 0);
    Check(!cleared.Fight && !cleared.Paused && cleared.Value == 0 && side.OnRoad,
          "after a fight the road is empty, not a paused meter");
    Check(DangerCueLine(rules, signs, side.Value, "overland", "path").empty(),
          "a cleared meter on the road stays blank");
    const DangerStep rebuilt = AdvanceDanger(meter, rules, 8, 0);
    Check(!rebuilt.Fight && !rebuilt.Paused && rebuilt.Value == 8,
          "the trip after a fight builds from zero");
    AdvanceDangerSigns(signs, rules, meter.Value, "overland", "forest");
    Check(DangerCueLine(rules, signs, meter.Value, "overland", "forest") == "uneasy - prints",
          "signs after a fight follow the new meter, not the old one");
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
    Check(version.Save("run", file, "old"), "a current-version file without a meter is written");
    RunSave oldSaves(older.Path);
    const RunLoad old = oldSaves.Read();
    Check(static_cast<bool>(old) && old.Run.DangerValue == 0 && old.Run.DangerGrace == 0 &&
              old.Run.DangerRoad && old.Run.DangerRng == 1 && !old.Run.Wild && old.Run.Career.Xp == 12,
          "a file that omits the meter loads empty");
    DangerMeter empty;
    empty.Value = old.Run.DangerValue;
    empty.Grace = old.Run.DangerGrace;
    empty.OnRoad = old.Run.DangerRoad;
    const DangerStep blank = AdvanceDanger(empty, SampleRules(), 0, 0);
    Check(static_cast<bool>(old) && !blank.Paused && blank.Value == 0,
          "an older file with no meter is empty, not paused");

    TempDir heldDir("ascension-danger-held");
    RunState heldRun;
    heldRun.Career.ClassId = "fighter";
    heldRun.DangerValue = 36;
    heldRun.DangerGrace = 0;
    heldRun.DangerRoad = true;
    heldRun.DangerRng = 4;
    RunSave heldSaves(heldDir.Path);
    Check(heldSaves.Write(heldRun, why), "a paused meter save writes");
    const RunLoad held = heldSaves.Read();
    Check(static_cast<bool>(held) && held.Run.DangerValue == 36 && held.Run.DangerGrace == 0 &&
              held.Run.DangerRoad && held.Run.DangerRng == 4,
          "the held meter round-trips on the existing keys");
    DangerMeter paused;
    paused.Value = held.Run.DangerValue;
    paused.Grace = held.Run.DangerGrace;
    paused.OnRoad = held.Run.DangerRoad;
    const DangerStep still = AdvanceDanger(paused, SampleRules(), 0, 0);
    Check(static_cast<bool>(held) && still.Paused && still.Value == 36 && !still.Fight,
          "a loaded road meter stays paused without a new save key");
    const DangerStep resumed = AdvanceDanger(paused, SampleRules(), 8, 0);
    Check(static_cast<bool>(held) && !resumed.Fight && !resumed.Paused && resumed.Value == 44,
          "leaving the saved road resumes from the held value");
}

} // namespace

int main()
{
    TestRoadNeverRolls();
    TestDangerAddsByTerrain();
    TestResetAndGrace();
    TestRoadHoldsMeter();
    TestCurves();
    TestTables();
    TestRollSequence();
    TestSigns();
    TestShippedDanger();
    TestRejectedDanger();
    TestOldSaveLoads();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
