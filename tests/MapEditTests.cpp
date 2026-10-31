#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#include "Ascension/Data/Database.h"
#include "Ascension/MapEdit/MapEdit.h"
#include "Check.h"

namespace {

using namespace Ascension;

namespace fs = std::filesystem;

const fs::path kSample = ASCENSION_DATA_DIR;

const char* kTinyWild =
    "{\n"
    "  \"map\": {\n"
    "    \"id\": \"tiny_wild\",\n"
    "    \"rows\": [\n"
    "      \"TTTTT\",\n"
    "      \"T...T\",\n"
    "      \"TTTTT\"\n"
    "    ],\n"
    "    \"legend\": {\n"
    "      \".\": \"grass\",\n"
    "      \"T\": \"tree\"\n"
    "    },\n"
    "    \"start\": { \"x\": 1, \"y\": 1 }\n"
    "  }\n"
    "}\n";

struct TempCopy {
    fs::path Path;

    TempCopy()
    {
        static i32 serial = 0;
        Path = fs::temp_directory_path() / ("ascension-mapedit-test-" + std::to_string(++serial));
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

std::string ReadFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteFile(const fs::path& path, std::string_view text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

i32 Count(std::string_view text, std::string_view part)
{
    i32 found = 0;
    for (usize at = 0; at + part.size() <= text.size(); ++at) {
        if (text.compare(at, part.size(), part) == 0)
            ++found;
    }
    return found;
}

void AddTiles(const fs::path& data)
{
    std::string text = ReadFile(data / "tiles.json");
    std::string flat;
    flat.reserve(text.size());
    for (const char c : text) {
        if (c != '\r')
            flat.push_back(c);
    }
    const std::string from = "      \"color\": \"#2A2438\"\n    }\n  ]";
    const std::string extra =
        "      \"color\": \"#2A2438\"\n    },\n"
        "    { \"id\": \"grass\", \"solid\": false, \"color\": \"#64AC45\" },\n"
        "    { \"id\": \"tree\", \"solid\": true, \"color\": \"#1E4A3A\" },\n"
        "    { \"id\": \"barrow\", \"solid\": true, \"color\": \"#212D1C\" },\n"
        "    { \"id\": \"stone\", \"solid\": false, \"color\": \"#717B6C\" },\n"
        "    { \"id\": \"path\", \"solid\": false, \"color\": \"#996838\" }\n"
        "  ]";
    const usize at = flat.find(from);
    Check(at != std::string::npos, "tiles.json still ends with the wall row");
    if (at == std::string::npos)
        return;
    flat.replace(at, from.size(), extra);
    WriteFile(data / "tiles.json", flat);
}

struct Loaded {
    Database Data;
    explicit Loaded(const fs::path& path) : Data(path)
    {
        DataError error;
        Check(Data.Load(&error), "the edited data loads");
        if (!error.Message.empty())
            std::printf("    load: %s\n", error.Message.c_str());
    }

    [[nodiscard]] const MapDef* Map(const char* id) const
    {
        const MapDef* map = Data.FindMap(id);
        Check(map != nullptr, id);
        return map;
    }
};

void TestCheckAndArrival()
{
    TempCopy copy;
    const EditResult fresh = CheckData(copy.Path);
    Check(fresh.Ok, "check accepts the sample");
    Check(fresh.Message == "ok", "check says ok");

    AddTiles(copy.Path);
    NewMap wild;
    wild.Id = "camp";
    wild.Kind = MapKind::Overland;
    wild.Width = 5;
    wild.Height = 3;
    Check(CreateMap(copy.Path, wild).Ok, "camp is a new overland");

    MapLinkEdit link;
    link.MapId = "fixture_a";
    link.X = 1;
    link.Y = 3;
    link.To = "camp";
    link.ArrivalX = 2;
    link.ArrivalY = 1;
    Check(AddLink(copy.Path, link).Ok, "fixture_a links to a grass cell on camp");

    const fs::path camp = copy.Path / "maps" / "camp.json";
    std::string text = ReadFile(camp);
    const usize row = text.find("T...T");
    Check(row != std::string::npos, "camp's middle row is grass");
    if (row != std::string::npos)
        text.replace(row, 5, "T.T.T");
    WriteFile(camp, text);

    const EditResult broken = CheckData(copy.Path);
    Check(!broken.Ok, "a walled arrival fails check");
    CheckHas(broken.Message, "arrival", "the failure names the arrival");
    CheckHas(broken.Message, "solid tile", "the failure says the arrival is solid");
}

void TestNewMaps()
{
    TempCopy copy;
    AddTiles(copy.Path);

    NewMap wild;
    wild.Id = "tiny_wild";
    wild.Kind = MapKind::Overland;
    wild.Width = 5;
    wild.Height = 3;
    const EditResult created = CreateMap(copy.Path, wild);
    Check(created.Ok && created.Message == "tiny_wild 5x3", "overland reports its size");
    const std::string bytes = ReadFile(copy.Path / "maps" / "tiny_wild.json");
    if (bytes != kTinyWild)
        std::printf("---- tiny_wild ----\n%s---- end ----\n", bytes.c_str());
    Check(bytes == kTinyWild, "the new overland bytes stay put");

    NewMap hole;
    hole.Id = "tiny_barrow";
    hole.Kind = MapKind::Dungeon;
    hole.Width = 5;
    hole.Height = 5;
    Check(CreateMap(copy.Path, hole).Ok, "dungeon is created");
    const Loaded barrowData(copy.Path);
    const MapDef* barrow = barrowData.Map("tiny_barrow");
    if (barrow != nullptr) {
        Check(barrow->Width == 5 && barrow->Height == 5, "the dungeon is 5 by 5");
        Check(barrow->Start.X == 1 && barrow->Start.Y == 2, "the dungeon starts on the west interior");
        Check(barrow->Cell(0, 0) != nullptr && *barrow->Cell(0, 0) == "barrow", "the dungeon border is barrow");
        Check(barrow->Cell(1, 1) != nullptr && *barrow->Cell(1, 1) == "stone", "the dungeon room is stone");
    }
    CheckHas(ReadFile(copy.Path / "maps" / "tiny_barrow.json"), "\"B\": \"barrow\"",
             "barrow keeps the B glyph");
    CheckHas(ReadFile(copy.Path / "maps" / "tiny_barrow.json"), "\"=\": \"stone\"",
             "stone keeps the = glyph");

    NewMap town;
    town.Id = "tiny_town";
    town.Kind = MapKind::Town;
    town.Width = 6;
    town.Height = 5;
    Check(CreateMap(copy.Path, town).Ok, "town is created");
    const Loaded townData(copy.Path);
    const MapDef* built = townData.Map("tiny_town");
    if (built != nullptr) {
        Check(built->Start.X == 3 && built->Start.Y == 2, "the town starts at the center");
        Check(built->Cell(0, 0) != nullptr && *built->Cell(0, 0) == "tree", "the town border is tree");
        Check(built->Cell(3, 2) != nullptr && *built->Cell(3, 2) == "grass", "the town center is grass");
        Check(!built->Hub, "a new town is not the hub");
    }

    NewMap cramped;
    cramped.Id = "cramped";
    cramped.Kind = MapKind::Dungeon;
    cramped.Width = 4;
    cramped.Height = 5;
    const EditResult refused = CreateMap(copy.Path, cramped);
    Check(!refused.Ok, "a dungeon smaller than 5 is refused");
    CheckHas(refused.Message, "5", "the refusal names the minimum");
    std::error_code missing;
    Check(!fs::is_regular_file(copy.Path / "maps" / "cramped.json", missing),
          "the refused dungeon writes no file");
}

void TestPaint()
{
    TempCopy copy;
    const fs::path file = copy.Path / "maps" / "fixture_a.json";
    const std::string before = ReadFile(file);

    PaintRect cover;
    cover.MapId = "fixture_a";
    cover.TileId = "wall";
    cover.X = 4;
    cover.Y = 1;
    const EditResult blocked = Paint(copy.Path, cover);
    Check(!blocked.Ok, "painting the spawn solid is refused");
    CheckHas(blocked.Message, "solid tile", "the refusal says the tile is solid");
    CheckHas(blocked.Message, "fixture_spawn", "the refusal names the spawn");
    Check(ReadFile(file) == before, "a refused paint leaves the file bytes alone");

    PaintRect rect;
    rect.MapId = "fixture_a";
    rect.TileId = "wall";
    rect.X = 1;
    rect.Y = 3;
    rect.Width = 2;
    rect.Height = 1;
    const EditResult painted = Paint(copy.Path, rect);
    Check(painted.Ok && painted.Message == "fixture_a painted 2", "a rectangle reports its cell count");
    const Loaded paintedData(copy.Path);
    const MapDef* map = paintedData.Map("fixture_a");
    if (map != nullptr) {
        Check(map->Cell(1, 3) != nullptr && *map->Cell(1, 3) == "wall", "the first painted cell is wall");
        Check(map->Cell(2, 3) != nullptr && *map->Cell(2, 3) == "wall", "the second painted cell is wall");
    }

    PaintRect again;
    again.MapId = "fixture_a";
    again.TileId = "wall";
    again.X = 4;
    again.Y = 3;
    Check(Paint(copy.Path, again).Ok, "a second wall paint is accepted");
    Check(Count(ReadFile(file), "\"#\": \"wall\"") == 1, "wall keeps the glyph it already had");
}

void TestFixtureSpawnSurvivesRewrite()
{
    TempCopy copy;
    PaintRect floor;
    floor.MapId = "fixture_b";
    floor.TileId = "floor";
    floor.X = 1;
    floor.Y = 2;
    Check(Paint(copy.Path, floor).Ok, "repainting the start floor rewrites fixture_b");
    const Loaded loaded(copy.Path);
    const MapDef* map = loaded.Map("fixture_b");
    if (map == nullptr || map->Spawns.empty()) {
        Check(false, "fixture_b still has its spawn");
        return;
    }
    Check(!map->Spawns.front().Once, "once false survives the rewrite");
    Check(map->Spawns.front().NoFlee, "noFlee survives the rewrite");
    Check(map->Spawns.front().Mover.Listed && map->Spawns.front().Mover.Behavior == "stand",
          "the mover survives the rewrite");
}

void TestLinkAndHub()
{
    TempCopy copy;
    MapLinkEdit missing;
    missing.MapId = "fixture_a";
    missing.X = 1;
    missing.Y = 3;
    missing.To = "missing";
    missing.ArrivalX = 1;
    missing.ArrivalY = 1;
    const EditResult absent = AddLink(copy.Path, missing);
    Check(!absent.Ok, "a link to a missing map is refused");
    CheckHas(absent.Message, "does not exist", "the refusal says the map is missing");

    MapLinkEdit link;
    link.MapId = "fixture_a";
    link.X = 1;
    link.Y = 3;
    link.To = "fixture_b";
    link.ArrivalX = 1;
    link.ArrivalY = 2;
    Check(AddLink(copy.Path, link).Ok, "a link onto a walkable arrival is kept");
    const Loaded linked(copy.Path);
    const MapDef* map = linked.Map("fixture_a");
    bool found = false;
    if (map != nullptr) {
        for (const MapLink& door : map->Links) {
            if (door.Tile.X == 1 && door.Tile.Y == 3 && door.Map == "fixture_b")
                found = true;
        }
    }
    Check(found, "the new link is on fixture_a");

    Check(SetHub(copy.Path, "fixture_a", true).Ok, "fixture_a can be the hub");
    const EditResult second = SetHub(copy.Path, "fixture_b", true);
    Check(!second.Ok, "a second hub is refused");
    CheckHas(second.Message, "fixture_a", "the refusal names the hub that is already set");
}

void TestProps()
{
    TempCopy copy;
    AddTiles(copy.Path);
    NewMap town;
    town.Id = "hamlet";
    town.Kind = MapKind::Town;
    town.Width = 8;
    town.Height = 6;
    Check(CreateMap(copy.Path, town).Ok, "hamlet exists");

    MapPropEdit door;
    door.MapId = "hamlet";
    door.Id = "inn";
    door.X = 1;
    door.Y = 1;
    door.Sized = true;
    door.Width = 1;
    door.Height = 1;
    MapOpenEdit outside;
    outside.X = 0;
    outside.Y = 0;
    door.Open.push_back(outside);
    const EditResult hole = AddProp(copy.Path, door);
    Check(!hole.Ok, "an open cell outside the footprint is refused");
    CheckHas(hole.Message, "outside the footprint", "the refusal says the open cell is outside");

    MapPropEdit cover;
    cover.MapId = "hamlet";
    cover.Id = "inn";
    cover.X = 4;
    cover.Y = 3;
    cover.Sized = true;
    cover.Width = 1;
    cover.Height = 1;
    const EditResult blocked = AddProp(copy.Path, cover);
    Check(!blocked.Ok, "a prop on the start is refused");
    CheckHas(blocked.Message, "start", "the refusal names the start");

    MapPropEdit inn;
    inn.MapId = "hamlet";
    inn.Id = "inn";
    inn.X = 1;
    inn.Y = 1;
    inn.Sized = true;
    inn.Width = 2;
    inn.Height = 2;
    MapOpenEdit gate;
    gate.X = 1;
    gate.Y = 1;
    inn.Open.push_back(gate);
    Check(AddProp(copy.Path, inn).Ok, "the inn is placed");

    MapPointEdit rest;
    rest.MapId = "hamlet";
    rest.Kind = EditPointKind::Rest;
    rest.X = 1;
    rest.Y = 1;
    Check(AddPoint(copy.Path, rest).Ok, "a rest sits in the open cell");

    const Loaded loaded(copy.Path);
    const MapDef* map = loaded.Map("hamlet");
    if (map == nullptr)
        return;
    Check(map->Props.size() == 1 && map->Props.front().Id == "inn", "hamlet has the inn");
    bool rested = false;
    for (const MapPoint& point : map->Points) {
        if (point.Kind == MapPointKind::Rest && point.Tile.X == 1 && point.Tile.Y == 1)
            rested = true;
    }
    Check(rested, "the rest point is inside the inn");
}

void TestRemoveAndLegend()
{
    TempCopy copy;
    AddTiles(copy.Path);
    NewMap wild;
    wild.Id = "meadow";
    wild.Kind = MapKind::Overland;
    wild.Width = 5;
    wild.Height = 3;
    Check(CreateMap(copy.Path, wild).Ok, "meadow exists");

    MapSpawnEdit spawn;
    spawn.MapId = "meadow";
    spawn.Id = "meadow_wolf";
    spawn.X = 2;
    spawn.Y = 1;
    spawn.Encounter = "wolf";
    Check(AddSpawn(copy.Path, spawn).Ok, "the wolf is stamped");
    Check(RemoveSpawn(copy.Path, "meadow", "meadow_wolf").Ok, "the wolf is removed");

    MapLinkEdit link;
    link.MapId = "meadow";
    link.X = 3;
    link.Y = 1;
    link.To = "fixture_a";
    link.ArrivalX = 1;
    link.ArrivalY = 1;
    Check(AddLink(copy.Path, link).Ok, "the meadow link is stamped");
    MapCell at;
    at.MapId = "meadow";
    at.X = 3;
    at.Y = 1;
    Check(RemoveLink(copy.Path, at).Ok, "the meadow link is removed");

    MapPointEdit shop;
    shop.MapId = "meadow";
    shop.Kind = EditPointKind::Shop;
    shop.X = 2;
    shop.Y = 1;
    Check(AddPoint(copy.Path, shop).Ok, "the shop point is stamped");
    at.X = 2;
    Check(RemovePoint(copy.Path, at).Ok, "the shop point is removed");

    MapPropEdit prop;
    prop.MapId = "meadow";
    prop.Id = "sign";
    prop.X = 2;
    prop.Y = 1;
    prop.Sized = true;
    prop.Width = 1;
    prop.Height = 1;
    Check(AddProp(copy.Path, prop).Ok, "the sign is stamped");
    Check(RemoveProp(copy.Path, "meadow", "sign").Ok, "the sign is removed");

    const Loaded loaded(copy.Path);
    const MapDef* cleared = loaded.Map("meadow");
    if (cleared != nullptr) {
        Check(cleared->Spawns.empty() && cleared->Links.empty() && cleared->Points.empty() &&
                  cleared->Props.empty(),
              "the stamps are gone");
    }

    PaintRect all;
    all.MapId = "meadow";
    all.TileId = "path";
    all.X = 0;
    all.Y = 0;
    all.Width = 5;
    all.Height = 3;
    Check(Paint(copy.Path, all).Ok, "the meadow is painted to path");
    const std::string text = ReadFile(copy.Path / "maps" / "meadow.json");
    Check(text.find("grass") == std::string::npos, "an unused grass glyph is dropped");
    Check(text.find("tree") == std::string::npos, "an unused tree glyph is dropped");
    CheckHas(text, R"(",": "path")", "path keeps the comma glyph");
}

void TestLegendFull()
{
    TempCopy copy;
    std::string legend = "    \"legend\": {\n";
    bool first = true;
    for (int code = 33; code <= 126; ++code) {
        if (!first)
            legend += ",\n";
        first = false;
        legend += "      ";
        const char glyph = static_cast<char>(code);
        if (glyph == '"' || glyph == '\\')
            legend += std::string("\"\\") + glyph + "\"";
        else
            legend += std::string("\"") + glyph + "\"";
        legend += ": \"floor\"";
    }
    legend += "\n    }";
    const std::string body = "{\n  \"map\": {\n    \"id\": \"full_legend\",\n    \"rows\": [\n"
                             "      \"!!\"\n    ],\n" +
                             legend + ",\n    \"start\": { \"x\": 0, \"y\": 0 }\n  }\n}\n";
    WriteFile(copy.Path / "maps" / "full_legend.json", body);
    const std::string before = ReadFile(copy.Path / "maps" / "full_legend.json");

    PaintRect wall;
    wall.MapId = "full_legend";
    wall.TileId = "wall";
    wall.X = 1;
    wall.Y = 0;
    const EditResult full = Paint(copy.Path, wall);
    Check(!full.Ok, "a full legend cannot take a new tile");
    CheckHas(full.Message, "full_legend", "the refusal names the map");
    CheckHas(full.Message, "legend", "the refusal says the legend is full");
    Check(ReadFile(copy.Path / "maps" / "full_legend.json") == before,
          "a full legend leaves the file bytes alone");
}

} // namespace

int main()
{
    TestCheckAndArrival();
    TestNewMaps();
    TestPaint();
    TestFixtureSpawnSurvivesRewrite();
    TestLinkAndHub();
    TestProps();
    TestRemoveAndLegend();
    TestLegendFull();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
