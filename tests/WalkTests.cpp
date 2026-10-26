// Tile steps, the glide between them, walls, exits, the camera clamp, and a
// reload that keeps or moves the hero. No window and no drawing.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "Ascension/Data/ArtPack.h"
#include "Ascension/Map/Movers.h"
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
    const i32 milliseconds = data.GetWalk().MoveMilliseconds;
    const f32 step = static_cast<f32>(milliseconds < 1 ? 1 : milliseconds) / 1000.0f;
    return walker.Advance(data, data.GetWalk(), input, step);
}

bool At(const Walker& walker, std::string_view map, i32 x, i32 y)
{
    return walker.Placed() && walker.Place().MapId == map && walker.Place().Tile.X == x &&
           walker.Place().Tile.Y == y;
}

void CheckRules(const Database& data, const char* what)
{
    const WalkRules& rules = data.GetWalk();
    Check(rules.MoveMilliseconds == 180 && rules.SettleMilliseconds == 180 &&
              rules.Easing == WalkEase::Linear && rules.TilePixels == 64 &&
              rules.HeroInsetPercent == 22,
          what);
    Check(rules.HeroRed == 223 && rules.HeroGreen == 213 && rules.HeroBlue == 188,
          "the hero square uses palette wstone2");
    Check(rules.LinkRed == 208 && rules.LinkGreen == 154 && rules.LinkBlue == 42,
          "the exit square uses palette gold1");
    Check(rules.SpawnRed == 138 && rules.SpawnGreen == 166 && rules.SpawnBlue == 190,
          "the spawn square uses palette bg2");
    Check(rules.RestRed == 58 && rules.RestGreen == 138 && rules.RestBlue == 62 &&
              rules.TalkRed == 208 && rules.TalkGreen == 138 && rules.TalkBlue == 104 &&
              rules.ShopRed == 176 && rules.ShopGreen == 122 && rules.ShopBlue == 68,
          "rest, talk, and shop squares use fol1, skinA1, and leath2");
}

void TestRules()
{
    CheckRules(Loaded(ASCENSION_DATA_DIR, "sample walk rules load"),
               "sample walk.json sets the move, the settle, and the tile");
    CheckRules(Loaded(ASCENSION_SHIPPED_DATA_DIR, "shipped walk rules load"),
               "shipped walk.json sets the move, the settle, and the tile");
}

void TestPlace()
{
    Database data = Loaded(kSample, "fixture maps load for placing");
    if (!data.IsLoaded())
        return;
    Walker walker;
    Check(walker.Enter(data, "fixture_a") && At(walker, "fixture_a", 1, 1),
          "place starts from the fixture");
    MapTile tile;
    tile.X = 3;
    tile.Y = 1;
    Check(walker.Place(data, "fixture_a", tile) && At(walker, "fixture_a", 3, 1),
          "place stands on an open tile and does not take a link");
    MapTile wall;
    wall.X = 0;
    wall.Y = 0;
    Check(!walker.Place(data, "fixture_a", wall) && At(walker, "fixture_a", 3, 1),
          "place refuses a wall and stays put");
    Check(!walker.Place(data, "no_such_map", tile) && At(walker, "fixture_a", 3, 1),
          "place refuses a missing map and stays put");
    MapTile door;
    door.X = 4;
    door.Y = 2;
    Check(walker.Place(data, "fixture_a", door) && At(walker, "fixture_a", 4, 2),
          "place can stand on an exit without taking it");
}

void TestSteps()
{
    Database data = Loaded(kSample, "fixture maps load for walking");
    if (!data.IsLoaded())
        return;
    const WalkRules& rules = data.GetWalk();
    const f32 step = static_cast<f32>(rules.MoveMilliseconds) / 1000.0f;

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

    Check(Press(walker, data, WalkDir::East).Kind == WalkStepKind::Moved &&
              At(walker, "fixture_a", 2, 1),
          "east enters the next tile");
    Check(Press(walker, data, WalkDir::East).Kind == WalkStepKind::Moved &&
              At(walker, "fixture_a", 3, 1),
          "a second east continues");
    Check(Press(walker, data, WalkDir::East).Kind == WalkStepKind::Moved &&
              At(walker, "fixture_a", 4, 1),
          "a third east stands on the exit's column");
    const WalkStep turned = Press(walker, data, WalkDir::South);
    Check(turned.Kind == WalkStepKind::Exited && At(walker, "fixture_b", 1, 2),
          "south takes the exit and lands on fixture_b");
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

struct StepMark {
    i32 X = 0;
    i32 Y = 0;
};

void TraceStand(const Database& data, i32 hp, i32 maxHp, std::vector<StepMark>& out)
{
    out.clear();
    const MapDef* map = data.FindMap("fixture_b");
    if (map == nullptr)
        return;
    Walker walker;
    if (!walker.Enter(data, "fixture_b"))
        return;
    MapMemory memory;
    std::vector<SpawnPlace> places;
    PlaceSpawns(*map, memory, places);
    const WalkDir keys[] = {WalkDir::North, WalkDir::South, WalkDir::North, WalkDir::South,
                            WalkDir::East};
    for (const WalkDir direction : keys) {
        const WalkStep step = Press(walker, data, direction);
        if (step.Kind != WalkStepKind::Moved)
            continue;
        MoverContext context;
        context.Hero = walker.Place().Tile;
        context.HeroHp = hp;
        context.HeroMaxHp = maxHp;
        StepSpawnMovers(data, *map, memory, context, places);
        if (places.empty())
            continue;
        StepMark mark;
        mark.X = places[0].Tile.X;
        mark.Y = places[0].Tile.Y;
        out.push_back(mark);
    }
}

void TestIdleSteps()
{
    Database data = Loaded(kSample, "fixture maps load for a mover step");
    if (!data.IsLoaded())
        return;
    const MapDef* map = data.FindMap("fixture_b");
    Check(map != nullptr && map->Spawns.size() == 1 && map->Spawns[0].Mover.Behavior == "stand",
          "fixture_b names the stand behavior");
    if (map == nullptr)
        return;
    std::vector<StepMark> full;
    std::vector<StepMark> wounded;
    std::vector<StepMark> again;
    TraceStand(data, 100, 100, full);
    TraceStand(data, 10, 100, wounded);
    TraceStand(data, 100, 100, again);
    Check(full.size() == 4 && wounded.size() == 4 && again.size() == 4,
          "four open steps record the mover");
    bool stayed = full.size() == wounded.size() && full.size() == again.size();
    for (usize index = 0; stayed && index < full.size(); ++index) {
        const bool home = full[index].X == 1 && full[index].Y == 1;
        const bool same = full[index].X == wounded[index].X && full[index].Y == wounded[index].Y &&
                          full[index].X == again[index].X && full[index].Y == again[index].Y;
        if (!home || !same)
            stayed = false;
    }
    Check(stayed, "stand stays on its tile at full health and at a wound, both runs");

    MapMemory memory;
    std::vector<SpawnPlace> places;
    PlaceSpawns(*map, memory, places);
    Check(places.size() == 1 && places[0].Home.X == 1 && places[0].Home.Y == 1 &&
              places[0].Tile.X == 1 && places[0].Tile.Y == 1,
          "a fresh place stands on the file tile");
    RememberClear(memory, "fixture_hold", true);
    PlaceSpawns(*map, memory, places);
    Check(places.empty(), "a cleared spawn is not stepped");
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

f32 MoveSeconds(const WalkRules& rules)
{
    return static_cast<f32>(rules.MoveMilliseconds) / 1000.0f;
}

f32 SettleSeconds(const WalkRules& rules)
{
    return static_cast<f32>(rules.SettleMilliseconds) / 1000.0f;
}

WalkStep Hold(Walker& walker, const Database& data, const WalkRules& rules, WalkDir direction,
              f32 dt, bool fresh)
{
    WalkInput input;
    input.Direction = direction;
    input.Fresh = fresh;
    return walker.Advance(data, rules, input, dt);
}

WalkStep Release(Walker& walker, const Database& data, const WalkRules& rules, f32 dt)
{
    WalkInput input;
    return walker.Advance(data, rules, input, dt);
}

void TestGlideTiming()
{
    Database data = Loaded(kSample, "fixture maps load for glide timing");
    if (!data.IsLoaded())
        return;
    WalkRules rules = data.GetWalk();
    rules.MoveMilliseconds = 1000;
    rules.SettleMilliseconds = 1000;
    rules.Easing = WalkEase::Linear;
    const f32 move = MoveSeconds(rules);

    Walker walker;
    Check(walker.Enter(data, "fixture_a") && At(walker, "fixture_a", 1, 1),
          "glide timing starts at 1,1");
    const WalkStep partial = Hold(walker, data, rules, WalkDir::East, move * 0.5f, true);
    Check(partial.Kind == WalkStepKind::None && At(walker, "fixture_a", 1, 1),
          "half a tile does not commit");
    Check(Near(walker.DrawTile().X, 1.5f) && Near(walker.DrawTile().Y, 1.0f) && walker.Moving(),
          "the drawn point is halfway to the next tile");

    const WalkStep arrived = Hold(walker, data, rules, WalkDir::East, move * 0.5f, false);
    Check(arrived.Kind == WalkStepKind::Moved && At(walker, "fixture_a", 2, 1),
          "the rest of the tile commits when the boundary is reached");
    Check(Near(walker.DrawTile().X, 2.0f) && walker.Moving(),
          "holding past the boundary stays in motion on that tile");

    const WalkStep onward = Hold(walker, data, rules, WalkDir::East, move * 0.25f, false);
    Check(onward.Kind == WalkStepKind::None && At(walker, "fixture_a", 2, 1) &&
              walker.DrawTile().X > 2.2f,
          "a held direction leaves the tile immediately, with no pause");

    Walker burst;
    Check(burst.Enter(data, "fixture_a"), "the long frame starts on fixture_a");
    const WalkStep once = Hold(burst, data, rules, WalkDir::East, move * 5.0f, true);
    Check(once.Kind == WalkStepKind::Moved && At(burst, "fixture_a", 2, 1) &&
              Near(burst.DrawTile().X, 2.0f),
          "a long frame still commits one tile");

    Walker splitA;
    Walker splitB;
    Check(splitA.Enter(data, "fixture_a") && splitB.Enter(data, "fixture_a"),
          "both frame splits start together");
    for (i32 i = 0; i < 4; ++i)
        Hold(splitA, data, rules, WalkDir::East, move / 4.0f, i == 0);
    Hold(splitB, data, rules, WalkDir::East, move / 2.0f, true);
    Hold(splitB, data, rules, WalkDir::East, move / 2.0f, false);
    Check(At(splitA, "fixture_a", 2, 1) && At(splitB, "fixture_a", 2, 1) &&
              Near(splitA.DrawTile().X, splitB.DrawTile().X) &&
              Near(splitA.DrawTile().Y, splitB.DrawTile().Y),
          "the same travel time lands on the same point however the frames split");
}

void TestNearBoundary()
{
    Database data = Loaded(kSample, "fixture maps load for a near boundary");
    if (!data.IsLoaded())
        return;
    WalkRules rules = data.GetWalk();
    rules.MoveMilliseconds = 1000;
    rules.SettleMilliseconds = 1000;
    rules.Easing = WalkEase::Linear;
    const f32 move = MoveSeconds(rules);

    Walker east;
    Check(east.Enter(data, "fixture_a"), "the near boundary starts on fixture_a");
    const WalkStep shortEast = Hold(east, data, rules, WalkDir::East, move * 0.999f, true);
    Check(shortEast.Kind == WalkStepKind::None && At(east, "fixture_a", 1, 1) &&
              east.DrawTile().X > 1.99f && east.DrawTile().X < 2.0f,
          "just short of the next tile has not committed it");
    const WalkStep eastTile = Hold(east, data, rules, WalkDir::East, move * 0.5f, false);
    Check(eastTile.Kind == WalkStepKind::Moved && At(east, "fixture_a", 2, 1) &&
              Near(east.DrawTile().X, 2.0f),
          "crossing that boundary commits the tile in front, not the one after");

    Walker west;
    Check(west.Enter(data, "fixture_a"), "the west boundary starts on fixture_a");
    MapTile column;
    column.X = 4;
    column.Y = 1;
    Check(west.Place(data, "fixture_a", column), "the west boundary stands on 4,1");
    const WalkStep shortWest = Hold(west, data, rules, WalkDir::West, move * 0.999f, true);
    Check(shortWest.Kind == WalkStepKind::None && At(west, "fixture_a", 4, 1) &&
              west.DrawTile().X < 3.02f && west.DrawTile().X > 3.0f,
          "just short of the western tile has not committed it");
    const WalkStep westTile = Hold(west, data, rules, WalkDir::West, move * 0.5f, false);
    Check(westTile.Kind == WalkStepKind::Moved && At(west, "fixture_a", 3, 1) &&
              Near(west.DrawTile().X, 3.0f),
          "crossing westward commits the tile in front, not the one after");
}

void TestSettle()
{
    Database data = Loaded(kSample, "fixture maps load for settling");
    if (!data.IsLoaded())
        return;
    WalkRules rules = data.GetWalk();
    rules.MoveMilliseconds = 1000;
    rules.SettleMilliseconds = 1000;
    rules.Easing = WalkEase::Linear;
    const f32 move = MoveSeconds(rules);
    const f32 settle = SettleSeconds(rules);

    Walker walker;
    Check(walker.Enter(data, "fixture_a"), "settle starts on fixture_a");
    Check(Hold(walker, data, rules, WalkDir::East, move * 0.35f, true).Kind == WalkStepKind::None,
          "the release test leaves the hero between tiles");
    const WalkPoint mid = walker.DrawTile();
    Check(Release(walker, data, rules, 0.0f).Kind == WalkStepKind::None &&
              Near(walker.DrawTile().X, mid.X) && Near(walker.DrawTile().Y, mid.Y) &&
              At(walker, "fixture_a", 1, 1),
          "releasing does not pop the drawn point or the tile");
    const WalkStep landed = Release(walker, data, rules, settle);
    Check(landed.Kind == WalkStepKind::Moved && At(walker, "fixture_a", 2, 1) &&
              Near(walker.DrawTile().X, 2.0f) && !walker.Moving(),
          "a release glides forward to the next tile and then stops");

    Walker aligned;
    Check(aligned.Enter(data, "fixture_a"), "an aligned release starts on fixture_a");
    Check(Hold(aligned, data, rules, WalkDir::East, move, true).Kind == WalkStepKind::Moved &&
              At(aligned, "fixture_a", 2, 1),
          "a full tile is the committed place");
    Check(Release(aligned, data, rules, settle).Kind == WalkStepKind::None &&
              At(aligned, "fixture_a", 2, 1),
          "releasing on a tile does not take another");

    Walker stopped;
    Check(stopped.Enter(data, "fixture_a"), "halt starts on fixture_a");
    Hold(stopped, data, rules, WalkDir::East, move * 0.4f, true);
    stopped.Halt();
    Check(!stopped.Moving() && At(stopped, "fixture_a", 1, 1) && Near(stopped.DrawTile().X, 1.0f),
          "halt stands on the committed tile");
}

void TestBuffer()
{
    Database data = Loaded(kSample, "fixture maps load for a buffered turn");
    if (!data.IsLoaded())
        return;
    WalkRules rules = data.GetWalk();
    rules.MoveMilliseconds = 1000;
    rules.SettleMilliseconds = 1000;
    rules.Easing = WalkEase::Linear;
    const f32 move = MoveSeconds(rules);
    const f32 settle = SettleSeconds(rules);

    Walker walker;
    Check(walker.Enter(data, "fixture_a"), "the buffer starts on fixture_a");
    MapTile column;
    column.X = 4;
    column.Y = 1;
    Check(walker.Place(data, "fixture_a", column), "the buffer stands on 4,1");

    Check(Hold(walker, data, rules, WalkDir::West, move * 0.4f, true).Kind == WalkStepKind::None &&
              At(walker, "fixture_a", 4, 1),
          "west has not reached the next tile yet");
    const WalkPoint beforeTurn = walker.DrawTile();
    const WalkStep arrived =
        Hold(walker, data, rules, WalkDir::South, move, true);
    Check(arrived.Kind == WalkStepKind::Moved && At(walker, "fixture_a", 3, 1) &&
              Near(walker.DrawTile().Y, beforeTurn.Y),
          "a press during travel is kept until the tile is reached");

    const WalkStep turned = Release(walker, data, rules, settle);
    Check(turned.Kind == WalkStepKind::Moved && At(walker, "fixture_a", 3, 2) &&
              Near(walker.DrawTile().X, 3.0f) && Near(walker.DrawTile().Y, 2.0f),
          "the buffered direction glides one tile after the release");

    Walker reverse;
    Check(reverse.Enter(data, "fixture_a"), "reverse starts on fixture_a");
    Check(reverse.Place(data, "fixture_a", column), "reverse stands on 4,1");
    Hold(reverse, data, rules, WalkDir::West, move * 0.5f, true);
    const WalkPoint mid = reverse.DrawTile();
    Hold(reverse, data, rules, WalkDir::East, move * 0.2f, true);
    Check(At(reverse, "fixture_a", 4, 1) && reverse.DrawTile().X > mid.X,
          "the opposite direction reverses immediately");
}

void TestEasing()
{
    Database data = Loaded(kSample, "fixture maps load for easing");
    if (!data.IsLoaded())
        return;
    WalkRules linear = data.GetWalk();
    linear.MoveMilliseconds = 1000;
    linear.SettleMilliseconds = 1000;
    linear.Easing = WalkEase::Linear;
    WalkRules smooth = linear;
    smooth.Easing = WalkEase::Smooth;
    const f32 move = MoveSeconds(linear);
    const f32 slice = 0.75f * SettleSeconds(linear) * 0.25f;

    Walker straight;
    Walker curved;
    Check(straight.Enter(data, "fixture_a") && curved.Enter(data, "fixture_a"),
          "easing starts on fixture_a");
    Hold(straight, data, linear, WalkDir::East, move * 0.25f, true);
    Hold(curved, data, smooth, WalkDir::East, move * 0.25f, true);
    Release(straight, data, linear, 0.0f);
    Release(curved, data, smooth, 0.0f);
    Release(straight, data, linear, slice);
    Release(curved, data, smooth, slice);
    Check(Near(straight.DrawTile().X, 1.4375f), "linear settle covers a quarter of the gap");
    Check(Near(curved.DrawTile().X, 1.3671875f), "smooth settle is behind linear at that time");
}

void TestSpawnGlide()
{
    SpawnPlace wolf;
    wolf.Id = "wolf";
    wolf.Tile.X = 1;
    wolf.Tile.Y = 1;
    SpawnPlace stand;
    stand.Id = "stand";
    stand.Tile.X = 4;
    stand.Tile.Y = 1;
    std::vector<SpawnPlace> places;
    places.push_back(wolf);
    places.push_back(stand);

    SpawnGlides glides;
    glides.Snap(places);
    const WalkPoint snapped = glides.Sample("wolf", wolf.Tile);
    Check(Near(snapped.X, 1.0f) && Near(snapped.Y, 1.0f), "a snap stands on the tile");

    places[0].Tile.X = 2;
    const f32 move = 0.18f;
    glides.Advance(places, 0.0f, move);
    Check(Near(glides.Sample("wolf", wolf.Tile).X, 1.0f),
          "retargeting does not pop the drawn point");
    glides.Advance(places, move * 0.5f, move);
    const WalkPoint half = glides.Sample("wolf", wolf.Tile);
    const WalkPoint other = glides.Sample("stand", stand.Tile);
    Check(Near(half.X, 1.5f) && Near(half.Y, 1.0f), "a mover glides across the tile at move speed");
    Check(Near(other.X, 4.0f) && Near(other.Y, 1.0f), "a spawn that stayed put is not dragged");
    glides.Advance(places, move * 0.5f, move);
    Check(Near(glides.Sample("wolf", wolf.Tile).X, 2.0f), "the mover arrives on the new tile");

    places[0].Tile.X = 4;
    glides.Advance(places, 0.0f, move);
    Check(Near(glides.Sample("wolf", wolf.Tile).X, 2.0f), "a second retarget keeps the current point");
    glides.Advance(places, move * 0.5f, move);
    Check(Near(glides.Sample("wolf", wolf.Tile).X, 2.5f),
          "the glide continues from the current point toward the new tile");
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

void TestPropFootprint()
{
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "maps" / "fixture_a.json",
                      "\"tile\": { \"x\": 2, \"y\": 2 }, \"w\": 1, \"h\": 1",
                      "\"tile\": { \"x\": 1, \"y\": 2 }, \"w\": 1, \"h\": 2, "
                      "\"open\": [ { \"x\": 1, \"y\": 3 } ]");
        Database data = Loaded(copy.Path, "a footprint map loads");
        if (!data.IsLoaded())
            return;
        const MapDef* map = data.FindMap("fixture_a");
        Check(map != nullptr && MapBlocks(*map, 1, 2) && !MapBlocks(*map, 1, 3),
              "the footprint blocks its cell and the open cell stays clear");
        Walker walker;
        Check(walker.Enter(data, "fixture_a") && At(walker, "fixture_a", 1, 1),
              "the footprint test starts on the fixture");
        Check(Press(walker, data, WalkDir::South).Kind == WalkStepKind::Blocked &&
                  At(walker, "fixture_a", 1, 1),
              "a footprint blocks a step the way a wall does");
        MapTile hole;
        hole.X = 1;
        hole.Y = 3;
        Check(walker.Place(data, "fixture_a", hole) && At(walker, "fixture_a", 1, 3),
              "an open cell inside the footprint can be stood on");
        MapTile blocked;
        blocked.X = 1;
        blocked.Y = 2;
        Check(!walker.Place(data, "fixture_a", blocked) && At(walker, "fixture_a", 1, 3),
              "place refuses the blocked footprint cell");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "maps" / "fixture_a.json",
                      "\"tile\": { \"x\": 2, \"y\": 2 }, \"w\": 1, \"h\": 1",
                      "\"tile\": { \"x\": 1, \"y\": 2 }");
        Database data = Loaded(copy.Path, "an unsized prop map loads");
        if (!data.IsLoaded())
            return;
        WritePng(copy.Path / "pack" / "props" / "post.png");
        WriteFile(copy.Path / "pack" / "manifest.json",
                  "{ \"props\": [ { \"id\": \"fixture_post\", \"file\": \"props/post.png\", "
                  "\"w\": 1, \"h\": 1 } ] }\n");
        ArtPack pack;
        DataError packError;
        Check(pack.Load(copy.Path / "pack", data, &packError), "the prop pack loads");
        if (!packError.Message.empty())
            std::printf("    %s\n", packError.Message.c_str());
        Walker open;
        Check(open.Enter(data, "fixture_a"), "the unsized prop starts on the fixture");
        Check(Press(open, data, WalkDir::South).Kind == WalkStepKind::Moved &&
                  At(open, "fixture_a", 1, 2),
              "an unsized prop does not block without a pack");
        Walker closed;
        Check(closed.Enter(data, "fixture_a", &pack), "the packed prop starts on the fixture");
        WalkInput south;
        south.Direction = WalkDir::South;
        south.Fresh = true;
        const f32 step = static_cast<f32>(data.GetWalk().MoveMilliseconds) / 1000.0f;
        const WalkStep blocked = closed.Advance(data, data.GetWalk(), south, step, &pack);
        Check(blocked.Kind == WalkStepKind::Blocked && At(closed, "fixture_a", 1, 1),
              "a pack footprint blocks the step the map left unsized");
    }
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
        ReplaceInFile(copy.Path / "walk.json", "\"moveMilliseconds\": 180",
                      "\"moveMilliseconds\": 0");
        Database data(copy.Path);
        DataError loadError;
        Check(!data.Load(&loadError), "a zero move time fails");
        CheckHas(loadError.Message, "walk.json", "the move error names the file");
        CheckHas(loadError.Message, "moveMilliseconds", "the move error names the field");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "walk.json", "\"settleMilliseconds\": 180",
                      "\"settleMilliseconds\": 0");
        Database data(copy.Path);
        DataError loadError;
        Check(!data.Load(&loadError), "a zero settle time fails");
        CheckHas(loadError.Message, "settleMilliseconds", "the settle error names the field");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "walk.json", "\"easing\": \"linear\"", "\"easing\": \"bounce\"");
        Database data(copy.Path);
        DataError loadError;
        Check(!data.Load(&loadError), "an unknown easing fails");
        CheckHas(loadError.Message, "easing", "the easing error names the field");
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
    TestPlace();
    TestSteps();
    TestGlideTiming();
    TestNearBoundary();
    TestSettle();
    TestBuffer();
    TestEasing();
    TestSpawnGlide();
    TestScriptPath();
    TestIdleSteps();
    TestCamera();
    TestAdopt();
    TestEdge();
    TestPropFootprint();
    TestBadWalkFile();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
