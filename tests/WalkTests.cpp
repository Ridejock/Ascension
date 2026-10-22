// Tile steps, walls, exits, the camera clamp, and a reload that keeps or moves
// the hero. No window and no drawing.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "Ascension/Map/Walk.h"
#include "Check.h"

namespace {

using namespace Ascension;

namespace fs = std::filesystem;

const fs::path kSample = ASCENSION_DATA_DIR;

struct TempCopy {
    fs::path Path;

    TempCopy()
    {
        Path = fs::temp_directory_path() / "ascension-walk-tests";
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

bool Near(f32 value, f32 expected)
{
    const f32 delta = value - expected;
    return delta < 0.001f && delta > -0.001f;
}

Database Loaded(const fs::path& directory, const char* what)
{
    Database data(directory);
    DataError error;
    Check(data.Load(&error), what);
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    return data;
}

void Reload(Database& data, const char* what)
{
    DataError error;
    Check(data.CheckForChanges(&error) == 0, what);
    Check(data.CheckForChanges(&error) == 1, what);
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
}

WalkStep Press(Walker& walker, const Database& data, WalkDir direction)
{
    WalkInput input;
    input.Direction = direction;
    input.Fresh = true;
    return walker.Advance(data, data.GetWalk(), input, 0.0f);
}

bool At(const Walker& walker, std::string_view map, i32 x, i32 y)
{
    return walker.Placed() && walker.Place().MapId == map && walker.Place().Tile.X == x &&
           walker.Place().Tile.Y == y;
}

void CheckRules(const Database& data, const char* what)
{
    const WalkRules& rules = data.GetWalk();
    Check(rules.StepMilliseconds == 180 && rules.TilePixels == 64 && rules.HeroInsetPercent == 22,
          what);
    Check(rules.HeroRed == 223 && rules.HeroGreen == 213 && rules.HeroBlue == 188,
          "the hero square uses palette wstone2");
    Check(rules.LinkRed == 208 && rules.LinkGreen == 154 && rules.LinkBlue == 42,
          "the exit square uses palette gold1");
}

void TestRules()
{
    CheckRules(Loaded(ASCENSION_DATA_DIR, "sample walk rules load"),
               "sample walk.json sets the step, the tile, and the inset");
    CheckRules(Loaded(ASCENSION_SHIPPED_DATA_DIR, "shipped walk rules load"),
               "shipped walk.json sets the step, the tile, and the inset");
}

void TestSteps()
{
    Database data = Loaded(kSample, "fixture maps load for walking");
    if (!data.IsLoaded())
        return;
    const WalkRules& rules = data.GetWalk();
    const f32 step = static_cast<f32>(rules.StepMilliseconds) / 1000.0f;

    Walker walker;
    Check(!walker.Enter(data, "no_such_map") && !walker.Placed(), "an unknown map is not entered");
    WalkInput idle;
    Check(walker.Advance(data, rules, idle, step).Kind == WalkStepKind::None,
          "a walker with no map does not step");

    Check(walker.Enter(data, "fixture_a") && At(walker, "fixture_a", 1, 1),
          "fixture_a starts at 1,1");

    const WalkStep north = Press(walker, data, WalkDir::North);
    Check(north.Kind == WalkStepKind::Blocked && At(walker, "fixture_a", 1, 1),
          "a wall blocks a step and the hero stays");
    const WalkStep west = Press(walker, data, WalkDir::West);
    Check(west.Kind == WalkStepKind::Blocked && At(walker, "fixture_a", 1, 1),
          "the west wall blocks");

    WalkInput east;
    east.Direction = WalkDir::East;
    east.Fresh = true;
    const WalkStep first = walker.Advance(data, rules, east, step * 5.0f);
    Check(first.Kind == WalkStepKind::Moved && At(walker, "fixture_a", 2, 1),
          "a long frame still takes one step");
    east.Fresh = false;
    const WalkStep waiting = walker.Advance(data, rules, east, step * 0.5f);
    Check(waiting.Kind == WalkStepKind::None && At(walker, "fixture_a", 2, 1),
          "a held direction waits out the step time");
    const WalkStep repeated = walker.Advance(data, rules, east, step * 0.5f);
    Check(repeated.Kind == WalkStepKind::Moved && At(walker, "fixture_a", 3, 1),
          "the rest of the step time takes the next tile");
    const WalkStep burst = walker.Advance(data, rules, east, step * 5.0f);
    Check(burst.Kind == WalkStepKind::Moved && At(walker, "fixture_a", 4, 1),
          "a held direction still moves one tile when the frame is long");

    WalkInput south;
    south.Direction = WalkDir::South;
    south.Fresh = false;
    const WalkStep turned = walker.Advance(data, rules, south, 0.0f);
    Check(turned.Kind == WalkStepKind::Exited && At(walker, "fixture_b", 1, 2),
          "turning steps at once, and the exit lands on fixture_b");
    Check(turned.Arrival.MapId == "fixture_b" && turned.Arrival.Tile.X == 1 &&
              turned.Arrival.Tile.Y == 2,
          "the exit reports the arrival tile");

    const WalkStep stay = walker.Advance(data, rules, idle, step);
    Check(stay.Kind == WalkStepKind::None && At(walker, "fixture_b", 1, 2),
          "arriving on a tile does not take a link");
    const WalkStep shut = Press(walker, data, WalkDir::East);
    Check(shut.Kind == WalkStepKind::Blocked && At(walker, "fixture_b", 1, 2),
          "fixture_b's east wall blocks");

    Check(Press(walker, data, WalkDir::North).Kind == WalkStepKind::Moved &&
              At(walker, "fixture_b", 1, 1),
          "a spawn tile does not block");
    const WalkStep back = Press(walker, data, WalkDir::East);
    Check(back.Kind == WalkStepKind::Exited && At(walker, "fixture_a", 4, 2),
          "the return link lands on fixture_a's exit tile");
    Check(walker.Advance(data, rules, idle, step).Kind == WalkStepKind::None &&
              At(walker, "fixture_a", 4, 2),
          "that arrival is itself a link and does not bounce");
    Check(Press(walker, data, WalkDir::West).Kind == WalkStepKind::Moved &&
              At(walker, "fixture_a", 3, 2),
          "stepping off the link stays on fixture_a");
    const WalkStep again = Press(walker, data, WalkDir::East);
    Check(again.Kind == WalkStepKind::Exited && At(walker, "fixture_b", 1, 2),
          "stepping back onto the link takes it");
}

void TestScriptPath()
{
    Database data = Loaded(kSample, "the key-script path loads");
    if (!data.IsLoaded())
        return;
    Walker walker;
    Check(walker.Enter(data, "fixture_a"), "the script starts on fixture_a");
    const WalkDir keys[] = {WalkDir::East, WalkDir::East, WalkDir::East, WalkDir::South};
    WalkStep last;
    for (const WalkDir direction : keys)
        last = Press(walker, data, direction);
    Check(last.Kind == WalkStepKind::Exited && At(walker, "fixture_b", 1, 2),
          "right right right down walks from the start through the exit");
}

void TestCamera()
{
    Database data = Loaded(kSample, "camera fixture loads");
    if (!data.IsLoaded())
        return;
    const MapDef* map = data.FindMap("fixture_a");
    Check(map != nullptr, "fixture_a is loaded");
    if (map == nullptr)
        return;
    MapTile near;
    near.X = 1;
    near.Y = 1;
    const WalkFrame tight = FrameMap(*map, near, 4.0f, 3.0f);
    Check(Near(tight.OriginX, 0.0f) && Near(tight.OriginY, 0.0f),
          "a hero in the corner keeps the view on the map");
    MapTile far;
    far.X = 4;
    far.Y = 3;
    const WalkFrame edge = FrameMap(*map, far, 4.0f, 3.0f);
    Check(Near(edge.OriginX, 2.0f) && Near(edge.OriginY, 2.0f),
          "a hero at the far edge stops the view on the map edge");
    Check(Near(edge.OriginX + edge.TilesWide, static_cast<f32>(map->Width)) &&
              Near(edge.OriginY + edge.TilesHigh, static_cast<f32>(map->Height)),
          "the clamped view ends on the map's far edge");
    const WalkFrame wide = FrameMap(*map, near, 20.0f, 12.0f);
    Check(Near(wide.OriginX, -7.0f) && Near(wide.OriginY, -3.5f),
          "a view larger than the map centers the map");
    const WalkFrame empty = FrameMap(*map, near, 0.0f, 3.0f);
    Check(Near(empty.OriginX, 0.0f) && Near(empty.OriginY, 0.0f),
          "a zero-wide view does not move the origin");
}

void TestAdopt()
{
    {
        TempCopy copy;
        Database data = Loaded(copy.Path, "adopt starts from the fixture");
        if (!data.IsLoaded())
            return;
        Walker walker;
        Check(walker.Enter(data, "fixture_a"), "adopt enters fixture_a");
        Check(Press(walker, data, WalkDir::South).Kind == WalkStepKind::Moved, "south is open");
        Check(Press(walker, data, WalkDir::South).Kind == WalkStepKind::Moved &&
                  At(walker, "fixture_a", 1, 3),
              "the hero stands on an open tile");
        ReplaceInFile(copy.Path / "tiles.json", "#996838", "#996839");
        Reload(data, "a color edit reloads under the hero");
        Check(walker.Adopt(data, "fixture_a") && At(walker, "fixture_a", 1, 3),
              "a walkable tile is kept across a reload");
    }
    {
        TempCopy copy;
        Database data = Loaded(copy.Path, "a solid reload starts from the fixture");
        if (!data.IsLoaded())
            return;
        Walker walker;
        Check(walker.Enter(data, "fixture_a") &&
                  Press(walker, data, WalkDir::South).Kind == WalkStepKind::Moved &&
                  At(walker, "fixture_a", 1, 2),
              "the hero stands on the tile that will become a wall");
        ReplaceInFile(copy.Path / "maps" / "fixture_a.json", "\"#.#..#\"", "\"###..#\"");
        Reload(data, "a wall edit reloads");
        Check(walker.Adopt(data, "fixture_a") && At(walker, "fixture_a", 1, 1),
              "a tile that became solid sends the hero to the start");
    }
    {
        TempCopy copy;
        Database data = Loaded(copy.Path, "a removed map starts from the fixture");
        if (!data.IsLoaded())
            return;
        Walker walker;
        Check(walker.Enter(data, "fixture_b"), "the hero is on the map that will go");
        // fixture_a still names fixture_b. A dangling exit is a bad file, and a bad
        // file keeps the last good maps, so the exit has to go before the file does.
        ReplaceInFile(copy.Path / "maps" / "fixture_a.json", "\"map\": \"fixture_b\"",
                      "\"map\": \"fixture_a\"");
        std::error_code error;
        fs::remove(copy.Path / "maps" / "fixture_b.json", error);
        Check(!error, "fixture_b can be removed");
        Reload(data, "removing the current map reloads");
        Check(walker.Adopt(data, "fixture_a") && At(walker, "fixture_a", 1, 1),
              "a missing map uses the fallback start");
    }
    {
        TempCopy copy;
        Database data = Loaded(copy.Path, "a removed fallback starts from the fixture");
        if (!data.IsLoaded())
            return;
        Walker walker;
        Check(walker.Enter(data, "fixture_a"), "the hero is on fixture_a");
        // Arrival 1,1 is open on fixture_b. Pointing the exit there lets fixture_a go.
        ReplaceInFile(copy.Path / "maps" / "fixture_b.json",
                      "\"map\": \"fixture_a\",\n        \"arrival\": { \"x\": 4, \"y\": 2 }",
                      "\"map\": \"fixture_b\",\n        \"arrival\": { \"x\": 1, \"y\": 1 }");
        std::error_code error;
        fs::remove(copy.Path / "maps" / "fixture_a.json", error);
        Reload(data, "removing fixture_a reloads");
        Check(walker.Adopt(data, "fixture_a") && At(walker, "fixture_b", 1, 2),
              "a missing fallback uses the first map's start");
    }
    {
        TempCopy copy;
        Database data = Loaded(copy.Path, "an empty maps folder starts loaded");
        if (!data.IsLoaded())
            return;
        Walker walker;
        Check(walker.Enter(data, "fixture_a"), "the hero starts before the maps go");
        std::error_code error;
        fs::remove(copy.Path / "maps" / "fixture_a.json", error);
        fs::remove(copy.Path / "maps" / "fixture_b.json", error);
        Reload(data, "removing every map reloads");
        Check(!walker.Adopt(data, "fixture_a") && !walker.Placed(),
              "no maps leaves the hero unplaced");
    }
}

void TestEdge()
{
    TempCopy copy;
    fs::create_directories(copy.Path / "maps");
    WriteFile(copy.Path / "maps" / "fixture_edge.json",
              "{\n"
              "  \"map\": {\n"
              "    \"id\": \"fixture_edge\",\n"
              "    \"rows\": [\"...\"],\n"
              "    \"legend\": { \".\": \"floor\" },\n"
              "    \"start\": { \"x\": 0, \"y\": 0 }\n"
              "  }\n"
              "}\n");
    Database data = Loaded(copy.Path, "an open edge map loads");
    if (!data.IsLoaded())
        return;
    Walker walker;
    Check(walker.Enter(data, "fixture_edge") && At(walker, "fixture_edge", 0, 0),
          "the edge map starts in the corner");
    Check(Press(walker, data, WalkDir::West).Kind == WalkStepKind::Blocked &&
              At(walker, "fixture_edge", 0, 0),
          "a step off the grid is blocked");
    Check(Press(walker, data, WalkDir::North).Kind == WalkStepKind::Blocked &&
              At(walker, "fixture_edge", 0, 0),
          "a step off the top of the grid is blocked");
    Check(Press(walker, data, WalkDir::East).Kind == WalkStepKind::Moved &&
              At(walker, "fixture_edge", 1, 0),
          "an open tile beside the edge can be entered");
    Check(Press(walker, data, WalkDir::South).Kind == WalkStepKind::Blocked &&
              At(walker, "fixture_edge", 1, 0),
          "a step off the bottom of a one-row map is blocked");
}

void TestBadWalkFile()
{
    {
        TempCopy copy;
        std::error_code error;
        fs::remove(copy.Path / "walk.json", error);
        Database data(copy.Path);
        DataError loadError;
        Check(!data.Load(&loadError), "a missing walk file fails");
        CheckHas(loadError.Message, "walk.json", "the missing file is named");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "walk.json", "\"stepMilliseconds\": 180",
                      "\"stepMilliseconds\": 0");
        Database data(copy.Path);
        DataError loadError;
        Check(!data.Load(&loadError), "a zero step time fails");
        CheckHas(loadError.Message, "walk.json", "the step error names the file");
        CheckHas(loadError.Message, "stepMilliseconds", "the step error names the field");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "walk.json", "\"heroColor\": \"#DFD5BC\"",
                      "\"heroColor\": \"#DFD5BC\", \"speed\": 1");
        Database data(copy.Path);
        DataError loadError;
        Check(!data.Load(&loadError), "an unknown walk field fails");
        CheckHas(loadError.Message, "speed", "the unknown walk field is named");
    }
}

} // namespace

int main()
{
    TestRules();
    TestSteps();
    TestScriptPath();
    TestCamera();
    TestAdopt();
    TestEdge();
    TestBadWalkFile();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
