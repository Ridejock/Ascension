#include "Ascension/MapEdit/MapEdit.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Ascension/Data/Database.h"

#include <nlohmann/json.hpp>

namespace Ascension {

namespace {

namespace fs = std::filesystem;

using Json = nlohmann::json;

constexpr i32 kMaxSpan = 256;

struct LegendGlyph {
    char Glyph = 0;
    std::string Tile;
};

struct OpenCell {
    i32 X = 0;
    i32 Y = 0;
    i32 Width = 1;
    i32 Height = 1;
};

struct SpawnRow {
    std::string Id;
    i32 X = 0;
    i32 Y = 0;
    std::string Encounter;
    bool Once = true;
    bool NoFlee = false;
    bool HasMover = false;
    std::string Mover;
    bool HasAura = false;
    i32 AuraRadius = 0;
    std::string AuraStatus;
};

struct LinkRow {
    i32 X = 0;
    i32 Y = 0;
    std::string Map;
    i32 ArrivalX = 0;
    i32 ArrivalY = 0;
};

struct PointRow {
    std::string Kind;
    i32 X = 0;
    i32 Y = 0;
    std::string Dialogue;
};

struct PropRow {
    std::string Id;
    i32 X = 0;
    i32 Y = 0;
    bool Sized = false;
    i32 Width = 0;
    i32 Height = 0;
    std::vector<OpenCell> Open;
};

struct MapDoc {
    std::string Id;
    bool Hub = false;
    std::vector<std::string> Rows;
    std::vector<LegendGlyph> Legend;
    i32 StartX = 0;
    i32 StartY = 0;
    std::vector<SpawnRow> Spawns;
    std::vector<LinkRow> Links;
    std::vector<PointRow> Points;
    std::vector<PropRow> Props;
};

EditResult Fail(std::string message)
{
    EditResult result;
    result.Message = std::move(message);
    return result;
}

EditResult Done(std::string message)
{
    EditResult result;
    result.Ok = true;
    result.Message = std::move(message);
    return result;
}

bool IsMapId(std::string_view id)
{
    if (id.empty())
        return false;
    const char first = id.front();
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z')))
        return false;
    for (const char c : id) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_'))
            return false;
    }
    return true;
}

std::string Quote(std::string_view text)
{
    return "'" + std::string(text) + "'";
}

// Every printable ASCII character except space. The front of the list is the
// glyph a fresh map uses for that tile when the character is still free.
const std::string& GlyphOrder()
{
    static const std::string order = [] {
        std::string glyphs = "+#.,~T=B%";
        const auto add = [&glyphs](char c) {
            if (glyphs.find(c) == std::string::npos)
                glyphs.push_back(c);
        };
        for (char c = 'A'; c <= 'Z'; ++c)
            add(c);
        for (char c = 'a'; c <= 'z'; ++c)
            add(c);
        for (char c = '0'; c <= '9'; ++c)
            add(c);
        for (int c = 33; c <= 126; ++c)
            add(static_cast<char>(c));
        return glyphs;
    }();
    return order;
}

void AppendQuoted(std::string& out, std::string_view text)
{
    out.push_back('"');
    for (const char c : text) {
        if (c == '"' || c == '\\')
            out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
}

void AppendCoord(std::string& out, i32 x, i32 y)
{
    out += "{ \"x\": ";
    out += std::to_string(x);
    out += ", \"y\": ";
    out += std::to_string(y);
    out += " }";
}

std::string JoinChunks(const std::vector<std::string>& fields)
{
    std::string out = "{\n  \"map\": {\n";
    for (usize index = 0; index < fields.size(); ++index) {
        out += fields[index];
        if (index + 1 != fields.size())
            out += ",";
        out += "\n";
    }
    out += "  }\n}\n";
    return out;
}

std::string Dump(const MapDoc& doc)
{
    std::vector<std::string> fields;
    std::string id = "    \"id\": ";
    AppendQuoted(id, doc.Id);
    fields.push_back(std::move(id));
    if (doc.Hub)
        fields.push_back("    \"hub\": true");

    std::string rows = "    \"rows\": [\n";
    for (usize index = 0; index < doc.Rows.size(); ++index) {
        rows += "      ";
        AppendQuoted(rows, doc.Rows[index]);
        if (index + 1 != doc.Rows.size())
            rows += ",";
        rows += "\n";
    }
    rows += "    ]";
    fields.push_back(std::move(rows));

    std::string legend = "    \"legend\": {\n";
    for (usize index = 0; index < doc.Legend.size(); ++index) {
        legend += "      ";
        AppendQuoted(legend, std::string_view(&doc.Legend[index].Glyph, 1));
        legend += ": ";
        AppendQuoted(legend, doc.Legend[index].Tile);
        if (index + 1 != doc.Legend.size())
            legend += ",";
        legend += "\n";
    }
    legend += "    }";
    fields.push_back(std::move(legend));

    std::string start = "    \"start\": ";
    AppendCoord(start, doc.StartX, doc.StartY);
    fields.push_back(std::move(start));

    if (!doc.Spawns.empty()) {
        std::string spawns = "    \"spawns\": [\n";
        for (usize index = 0; index < doc.Spawns.size(); ++index) {
            const SpawnRow& spawn = doc.Spawns[index];
            spawns += "      {\n        \"id\": ";
            AppendQuoted(spawns, spawn.Id);
            spawns += ",\n        \"tile\": ";
            AppendCoord(spawns, spawn.X, spawn.Y);
            spawns += ",\n        \"encounter\": ";
            AppendQuoted(spawns, spawn.Encounter);
            if (!spawn.Once)
                spawns += ",\n        \"once\": false";
            if (spawn.NoFlee)
                spawns += ",\n        \"noFlee\": true";
            if (spawn.HasMover) {
                spawns += ",\n        \"mover\": { \"behavior\": ";
                AppendQuoted(spawns, spawn.Mover);
                spawns += " }";
            }
            if (spawn.HasAura) {
                spawns += ",\n        \"aura\": { \"radius\": ";
                spawns += std::to_string(spawn.AuraRadius);
                spawns += ", \"status\": ";
                AppendQuoted(spawns, spawn.AuraStatus);
                spawns += " }";
            }
            spawns += "\n      }";
            if (index + 1 != doc.Spawns.size())
                spawns += ",";
            spawns += "\n";
        }
        spawns += "    ]";
        fields.push_back(std::move(spawns));
    }

    if (!doc.Links.empty()) {
        std::string links = "    \"links\": [\n";
        for (usize index = 0; index < doc.Links.size(); ++index) {
            const LinkRow& link = doc.Links[index];
            links += "      {\n        \"tile\": ";
            AppendCoord(links, link.X, link.Y);
            links += ",\n        \"map\": ";
            AppendQuoted(links, link.Map);
            links += ",\n        \"arrival\": ";
            AppendCoord(links, link.ArrivalX, link.ArrivalY);
            links += "\n      }";
            if (index + 1 != doc.Links.size())
                links += ",";
            links += "\n";
        }
        links += "    ]";
        fields.push_back(std::move(links));
    }

    if (!doc.Points.empty()) {
        std::string points = "    \"points\": [\n";
        for (usize index = 0; index < doc.Points.size(); ++index) {
            const PointRow& point = doc.Points[index];
            points += "      { \"kind\": ";
            AppendQuoted(points, point.Kind);
            points += ", \"tile\": ";
            AppendCoord(points, point.X, point.Y);
            if (!point.Dialogue.empty()) {
                points += ", \"dialogue\": ";
                AppendQuoted(points, point.Dialogue);
            }
            points += " }";
            if (index + 1 != doc.Points.size())
                points += ",";
            points += "\n";
        }
        points += "    ]";
        fields.push_back(std::move(points));
    }

    if (!doc.Props.empty()) {
        std::string props = "    \"props\": [\n";
        for (usize index = 0; index < doc.Props.size(); ++index) {
            const PropRow& prop = doc.Props[index];
            props += "      {\n        \"id\": ";
            AppendQuoted(props, prop.Id);
            props += ",\n        \"tile\": ";
            AppendCoord(props, prop.X, prop.Y);
            if (prop.Sized) {
                props += ",\n        \"w\": ";
                props += std::to_string(prop.Width);
                props += ",\n        \"h\": ";
                props += std::to_string(prop.Height);
            }
            if (!prop.Open.empty()) {
                props += ",\n        \"open\": [\n";
                for (usize hole = 0; hole < prop.Open.size(); ++hole) {
                    const OpenCell& open = prop.Open[hole];
                    props += "          { \"x\": ";
                    props += std::to_string(open.X);
                    props += ", \"y\": ";
                    props += std::to_string(open.Y);
                    if (open.Width != 1) {
                        props += ", \"w\": ";
                        props += std::to_string(open.Width);
                    }
                    if (open.Height != 1) {
                        props += ", \"h\": ";
                        props += std::to_string(open.Height);
                    }
                    props += " }";
                    if (hole + 1 != prop.Open.size())
                        props += ",";
                    props += "\n";
                }
                props += "        ]";
            }
            props += "\n      }";
            if (index + 1 != doc.Props.size())
                props += ",";
            props += "\n";
        }
        props += "    ]";
        fields.push_back(std::move(props));
    }

    return JoinChunks(fields);
}

bool ReadText(const fs::path& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

bool WriteText(const fs::path& path, std::string_view text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(out);
}

bool ReadInt(const Json& object, const char* key, i32& out)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer())
        return false;
    const std::int64_t value = it->get<std::int64_t>();
    if (value < 0 || value > kMaxSpan)
        return false;
    out = static_cast<i32>(value);
    return true;
}

bool ReadTile(const Json& object, i32& x, i32& y)
{
    if (!object.is_object())
        return false;
    return ReadInt(object, "x", x) && ReadInt(object, "y", y);
}

// False when the file is not a map this tool can rewrite. The caller then asks
// Database::Load so the sentence is the loader's.
bool ReadDoc(const fs::path& path, MapDoc& doc)
{
    std::string text;
    if (!ReadText(path, text))
        return false;
    const Json root = Json::parse(text.begin(), text.end(), nullptr, false);
    if (root.is_discarded())
        return false;
    const auto body = root.find("map");
    if (body == root.end() || !body->is_object())
        return false;
    const auto id = body->find("id");
    if (id == body->end() || !id->is_string())
        return false;
    doc.Id = id->get<std::string>();
    doc.Hub = false;
    if (body->contains("hub")) {
        const Json& hub = (*body)["hub"];
        if (!hub.is_boolean())
            return false;
        doc.Hub = hub.get<bool>();
    }
    const auto rows = body->find("rows");
    if (rows == body->end() || !rows->is_array() || rows->empty())
        return false;
    doc.Rows.clear();
    for (const Json& row : *rows) {
        if (!row.is_string())
            return false;
        doc.Rows.push_back(row.get<std::string>());
    }
    const auto legend = body->find("legend");
    if (legend == body->end() || !legend->is_object())
        return false;
    doc.Legend.clear();
    for (auto it = legend->begin(); it != legend->end(); ++it) {
        if (it.key().size() != 1 || !it->is_string())
            return false;
        LegendGlyph glyph;
        glyph.Glyph = it.key()[0];
        glyph.Tile = it->get<std::string>();
        doc.Legend.push_back(std::move(glyph));
    }
    const auto start = body->find("start");
    if (start == body->end() || !ReadTile(*start, doc.StartX, doc.StartY))
        return false;

    doc.Spawns.clear();
    if (body->contains("spawns")) {
        const Json& spawns = (*body)["spawns"];
        if (!spawns.is_array())
            return false;
        for (const Json& entry : spawns) {
            if (!entry.is_object())
                return false;
            SpawnRow spawn;
            const auto spawnId = entry.find("id");
            const auto encounter = entry.find("encounter");
            const auto tile = entry.find("tile");
            if (spawnId == entry.end() || !spawnId->is_string() || encounter == entry.end() ||
                !encounter->is_string() || tile == entry.end() ||
                !ReadTile(*tile, spawn.X, spawn.Y))
                return false;
            spawn.Id = spawnId->get<std::string>();
            spawn.Encounter = encounter->get<std::string>();
            if (entry.contains("once")) {
                if (!entry["once"].is_boolean())
                    return false;
                spawn.Once = entry["once"].get<bool>();
            }
            if (entry.contains("noFlee")) {
                if (!entry["noFlee"].is_boolean())
                    return false;
                spawn.NoFlee = entry["noFlee"].get<bool>();
            }
            if (entry.contains("mover")) {
                const Json& mover = entry["mover"];
                if (!mover.is_object())
                    return false;
                const auto behavior = mover.find("behavior");
                if (behavior == mover.end() || !behavior->is_string())
                    return false;
                spawn.HasMover = true;
                spawn.Mover = behavior->get<std::string>();
            }
            if (entry.contains("aura")) {
                const Json& aura = entry["aura"];
                if (!aura.is_object() || !ReadInt(aura, "radius", spawn.AuraRadius))
                    return false;
                const auto status = aura.find("status");
                if (status == aura.end() || !status->is_string())
                    return false;
                spawn.HasAura = true;
                spawn.AuraStatus = status->get<std::string>();
            }
            doc.Spawns.push_back(std::move(spawn));
        }
    }

    doc.Links.clear();
    if (body->contains("links")) {
        const Json& links = (*body)["links"];
        if (!links.is_array())
            return false;
        for (const Json& entry : links) {
            if (!entry.is_object())
                return false;
            LinkRow link;
            const auto tile = entry.find("tile");
            const auto map = entry.find("map");
            const auto arrival = entry.find("arrival");
            if (tile == entry.end() || !ReadTile(*tile, link.X, link.Y) || map == entry.end() ||
                !map->is_string() || arrival == entry.end() ||
                !ReadTile(*arrival, link.ArrivalX, link.ArrivalY))
                return false;
            link.Map = map->get<std::string>();
            doc.Links.push_back(std::move(link));
        }
    }

    doc.Points.clear();
    if (body->contains("points")) {
        const Json& points = (*body)["points"];
        if (!points.is_array())
            return false;
        for (const Json& entry : points) {
            if (!entry.is_object())
                return false;
            PointRow point;
            const auto kind = entry.find("kind");
            const auto tile = entry.find("tile");
            if (kind == entry.end() || !kind->is_string() || tile == entry.end() ||
                !ReadTile(*tile, point.X, point.Y))
                return false;
            point.Kind = kind->get<std::string>();
            if (entry.contains("dialogue")) {
                if (!entry["dialogue"].is_string())
                    return false;
                point.Dialogue = entry["dialogue"].get<std::string>();
            }
            doc.Points.push_back(std::move(point));
        }
    }

    doc.Props.clear();
    if (body->contains("props")) {
        const Json& props = (*body)["props"];
        if (!props.is_array())
            return false;
        for (const Json& entry : props) {
            if (!entry.is_object())
                return false;
            PropRow prop;
            const auto propId = entry.find("id");
            const auto tile = entry.find("tile");
            if (propId == entry.end() || !propId->is_string() || tile == entry.end() ||
                !ReadTile(*tile, prop.X, prop.Y))
                return false;
            prop.Id = propId->get<std::string>();
            const bool hasWidth = entry.contains("w");
            const bool hasHeight = entry.contains("h");
            if (hasWidth != hasHeight)
                return false;
            if (hasWidth) {
                if (!ReadInt(entry, "w", prop.Width) || !ReadInt(entry, "h", prop.Height))
                    return false;
                prop.Sized = true;
            }
            if (entry.contains("open")) {
                const Json& holes = entry["open"];
                if (!holes.is_array())
                    return false;
                for (const Json& hole : holes) {
                    if (!hole.is_object())
                        return false;
                    OpenCell open;
                    if (!ReadInt(hole, "x", open.X) || !ReadInt(hole, "y", open.Y))
                        return false;
                    if (hole.contains("w") && !ReadInt(hole, "w", open.Width))
                        return false;
                    if (hole.contains("h") && !ReadInt(hole, "h", open.Height))
                        return false;
                    prop.Open.push_back(open);
                }
            }
            doc.Props.push_back(std::move(prop));
        }
    }
    return true;
}

void PruneLegend(MapDoc& doc)
{
    std::string used;
    for (const std::string& row : doc.Rows) {
        for (const char glyph : row) {
            if (used.find(glyph) == std::string::npos)
                used.push_back(glyph);
        }
    }
    std::vector<LegendGlyph> kept;
    for (const LegendGlyph& glyph : doc.Legend) {
        if (used.find(glyph.Glyph) != std::string::npos)
            kept.push_back(glyph);
    }
    doc.Legend = std::move(kept);
}

char PreferredGlyph(const std::string& tile)
{
    if (tile == "floor")
        return '+';
    if (tile == "wall")
        return '#';
    if (tile == "grass")
        return '.';
    if (tile == "path")
        return ',';
    if (tile == "water")
        return '~';
    if (tile == "tree")
        return 'T';
    if (tile == "stone")
        return '=';
    if (tile == "barrow")
        return 'B';
    if (tile == "rubble")
        return '%';
    return 0;
}

bool AssignGlyph(MapDoc& doc, const std::string& tile, char& glyph, std::string& error)
{
    for (const LegendGlyph& entry : doc.Legend) {
        if (entry.Tile == tile) {
            glyph = entry.Glyph;
            return true;
        }
    }
    std::string used;
    for (const LegendGlyph& entry : doc.Legend)
        used.push_back(entry.Glyph);
    const char preferred = PreferredGlyph(tile);
    if (preferred != 0 && used.find(preferred) == std::string::npos) {
        doc.Legend.push_back(LegendGlyph{preferred, tile});
        glyph = preferred;
        return true;
    }
    for (const char candidate : GlyphOrder()) {
        if (used.find(candidate) != std::string::npos)
            continue;
        doc.Legend.push_back(LegendGlyph{candidate, tile});
        glyph = candidate;
        return true;
    }
    error = "map " + Quote(doc.Id) + " has no free legend character";
    return false;
}

fs::path MapPath(const fs::path& dataDir, const std::string& mapId)
{
    return dataDir / "maps" / (mapId + ".json");
}

// Copy the data, write one map, and load the copy. The real file changes only
// after that load succeeds, so a running game never sees a rejected map.
EditResult Commit(const fs::path& dataDir, const std::string& mapId, const MapDoc& doc,
                  const std::string& okLine)
{
    std::error_code error;
    if (!fs::is_directory(dataDir, error))
        return Fail("data directory does not exist");

    static std::atomic<u32> serial{0};
    const u32 stamp = serial.fetch_add(1);
    const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path temp =
        fs::temp_directory_path() / ("ascension-mapedit-" + std::to_string(stamp) + "-" +
                                     std::to_string(tick));
    struct Cleanup {
        fs::path Path;
        ~Cleanup()
        {
            std::error_code ignored;
            fs::remove_all(Path, ignored);
        }
    } cleanup{temp};

    fs::create_directories(temp, error);
    if (error)
        return Fail("could not create a temp directory");
    fs::copy(dataDir, temp, fs::copy_options::recursive, error);
    if (error)
        return Fail("could not copy the data directory");

    const std::string body = Dump(doc);
    const fs::path tempMap = MapPath(temp, mapId);
    fs::create_directories(tempMap.parent_path(), error);
    if (error || !WriteText(tempMap, body))
        return Fail("could not write the temp map");

    DataError loadError;
    Database data(temp);
    if (!data.Load(&loadError)) {
        if (!loadError.Message.empty())
            return Fail(std::move(loadError.Message));
        return Fail(data.GetLastError());
    }

    const fs::path realMap = MapPath(dataDir, mapId);
    fs::create_directories(realMap.parent_path(), error);
    if (error || !WriteText(realMap, body))
        return Fail("could not write maps/" + mapId + ".json");
    return Done(okLine);
}

EditResult Rejected(const fs::path& dataDir)
{
    return CheckData(dataDir);
}

EditResult WithDoc(const fs::path& dataDir, const std::string& mapId, MapDoc& doc)
{
    if (!IsMapId(mapId))
        return Fail("map id must be letters, digits and _, starting with a letter");
    const fs::path path = MapPath(dataDir, mapId);
    std::error_code error;
    if (!fs::is_regular_file(path, error))
        return Fail("map " + Quote(mapId) + " does not exist");
    if (!ReadDoc(path, doc))
        return Rejected(dataDir);
    if (doc.Id != mapId)
        return Rejected(dataDir);
    return Done({});
}

bool Inside(const MapDoc& doc, i32 x, i32 y, i32 width, i32 height)
{
    if (doc.Rows.empty() || x < 0 || y < 0 || width < 1 || height < 1)
        return false;
    const i64 right = static_cast<i64>(x) + width;
    const i64 bottom = static_cast<i64>(y) + height;
    if (right > static_cast<i64>(doc.Rows.front().size()) ||
        bottom > static_cast<i64>(doc.Rows.size()))
        return false;
    for (i32 row = y; row < y + height; ++row) {
        if (static_cast<i64>(doc.Rows[static_cast<usize>(row)].size()) < right)
            return false;
    }
    return true;
}

std::string Outside(const std::string& mapId, i32 x, i32 y)
{
    return "map " + Quote(mapId) + ": " + std::to_string(x) + "," + std::to_string(y) +
           " is outside the map";
}

void FillRect(MapDoc& doc, char glyph, i32 x, i32 y, i32 width, i32 height)
{
    for (i32 row = y; row < y + height; ++row) {
        std::string& line = doc.Rows[static_cast<usize>(row)];
        for (i32 column = x; column < x + width; ++column)
            line[static_cast<usize>(column)] = glyph;
    }
}

const char* PointName(EditPointKind kind)
{
    if (kind == EditPointKind::Talk)
        return "talk";
    if (kind == EditPointKind::Shop)
        return "shop";
    return "rest";
}

} // namespace

EditResult CheckData(const fs::path& dataDir)
{
    DataError error;
    Database data(dataDir);
    if (!data.Load(&error)) {
        if (!error.Message.empty())
            return Fail(std::move(error.Message));
        return Fail(data.GetLastError());
    }
    return Done("ok");
}

EditResult CreateMap(const fs::path& dataDir, const NewMap& spec)
{
    if (!IsMapId(spec.Id))
        return Fail("map id must be letters, digits and _, starting with a letter");
    if (spec.Width < 1 || spec.Width > kMaxSpan || spec.Height < 1 || spec.Height > kMaxSpan)
        return Fail("width and height must be from 1 to 256");
    if (spec.Kind == MapKind::Dungeon && (spec.Width < 5 || spec.Height < 5))
        return Fail("a dungeon needs a width and height of at least 5");
    std::error_code error;
    if (fs::is_regular_file(MapPath(dataDir, spec.Id), error))
        return Fail("map " + Quote(spec.Id) + " already exists");

    const bool dungeon = spec.Kind == MapKind::Dungeon;
    const char* fillTile = dungeon ? "stone" : "grass";
    const char* borderTile = dungeon ? "barrow" : "tree";
    MapDoc doc;
    doc.Id = spec.Id;
    std::string why;
    char fill = 0;
    char border = 0;
    if (!AssignGlyph(doc, fillTile, fill, why) || !AssignGlyph(doc, borderTile, border, why))
        return Fail(std::move(why));

    doc.Rows.assign(static_cast<usize>(spec.Height), std::string(static_cast<usize>(spec.Width), border));
    if (spec.Width >= 3 && spec.Height >= 3) {
        FillRect(doc, fill, 1, 1, spec.Width - 2, spec.Height - 2);
    }
    // The west edge of a thin overland is the border. The start stays on the fill.
    if (spec.Kind == MapKind::Town) {
        doc.StartX = spec.Width / 2;
        doc.StartY = spec.Height / 2;
    } else {
        doc.StartX = spec.Width > 1 ? 1 : 0;
        doc.StartY = spec.Height / 2;
    }
    doc.Rows[static_cast<usize>(doc.StartY)][static_cast<usize>(doc.StartX)] = fill;
    PruneLegend(doc);
    return Commit(dataDir, spec.Id, doc,
                  spec.Id + " " + std::to_string(spec.Width) + "x" + std::to_string(spec.Height));
}

EditResult Paint(const fs::path& dataDir, const PaintRect& rect)
{
    MapDoc doc;
    if (EditResult opened = WithDoc(dataDir, rect.MapId, doc); !opened.Ok)
        return opened;
    if (!Inside(doc, rect.X, rect.Y, rect.Width, rect.Height))
        return Fail(Outside(rect.MapId, rect.X, rect.Y));
    char glyph = 0;
    std::string why;
    if (!AssignGlyph(doc, rect.TileId, glyph, why))
        return Fail(std::move(why));
    FillRect(doc, glyph, rect.X, rect.Y, rect.Width, rect.Height);
    PruneLegend(doc);
    const i64 count = static_cast<i64>(rect.Width) * rect.Height;
    return Commit(dataDir, rect.MapId, doc,
                  rect.MapId + " painted " + std::to_string(count));
}

EditResult SetStart(const fs::path& dataDir, const MapCell& cell)
{
    MapDoc doc;
    if (EditResult opened = WithDoc(dataDir, cell.MapId, doc); !opened.Ok)
        return opened;
    doc.StartX = cell.X;
    doc.StartY = cell.Y;
    return Commit(dataDir, cell.MapId, doc,
                  cell.MapId + " start " + std::to_string(cell.X) + " " + std::to_string(cell.Y));
}

EditResult AddLink(const fs::path& dataDir, const MapLinkEdit& link)
{
    MapDoc doc;
    if (EditResult opened = WithDoc(dataDir, link.MapId, doc); !opened.Ok)
        return opened;
    LinkRow row;
    row.X = link.X;
    row.Y = link.Y;
    row.Map = link.To;
    row.ArrivalX = link.ArrivalX;
    row.ArrivalY = link.ArrivalY;
    doc.Links.push_back(std::move(row));
    return Commit(dataDir, link.MapId, doc,
                  link.MapId + " link " + std::to_string(link.X) + " " + std::to_string(link.Y) +
                      " " + link.To);
}

EditResult RemoveLink(const fs::path& dataDir, const MapCell& cell)
{
    MapDoc doc;
    if (EditResult opened = WithDoc(dataDir, cell.MapId, doc); !opened.Ok)
        return opened;
    bool found = false;
    std::vector<LinkRow> kept;
    for (const LinkRow& link : doc.Links) {
        if (!found && link.X == cell.X && link.Y == cell.Y) {
            found = true;
            continue;
        }
        kept.push_back(link);
    }
    if (!found)
        return Fail("link at " + std::to_string(cell.X) + "," + std::to_string(cell.Y) +
                    " is not on " + Quote(cell.MapId));
    doc.Links = std::move(kept);
    return Commit(dataDir, cell.MapId, doc,
                  cell.MapId + " unlinked " + std::to_string(cell.X) + " " + std::to_string(cell.Y));
}

EditResult AddSpawn(const fs::path& dataDir, const MapSpawnEdit& spawn)
{
    MapDoc doc;
    if (EditResult opened = WithDoc(dataDir, spawn.MapId, doc); !opened.Ok)
        return opened;
    SpawnRow row;
    row.Id = spawn.Id;
    row.X = spawn.X;
    row.Y = spawn.Y;
    row.Encounter = spawn.Encounter;
    row.Once = spawn.Once;
    row.NoFlee = spawn.NoFlee;
    row.HasMover = !spawn.Mover.empty();
    row.Mover = spawn.Mover;
    row.HasAura = spawn.HasAura;
    row.AuraRadius = spawn.AuraRadius;
    row.AuraStatus = spawn.AuraStatus;
    doc.Spawns.push_back(std::move(row));
    return Commit(dataDir, spawn.MapId, doc, spawn.MapId + " spawn " + spawn.Id);
}

EditResult RemoveSpawn(const fs::path& dataDir, const std::string& mapId, const std::string& spawnId)
{
    MapDoc doc;
    if (EditResult opened = WithDoc(dataDir, mapId, doc); !opened.Ok)
        return opened;
    bool found = false;
    std::vector<SpawnRow> kept;
    for (const SpawnRow& spawn : doc.Spawns) {
        if (spawn.Id == spawnId) {
            found = true;
            continue;
        }
        kept.push_back(spawn);
    }
    if (!found)
        return Fail("spawn " + Quote(spawnId) + " is not on " + Quote(mapId));
    doc.Spawns = std::move(kept);
    return Commit(dataDir, mapId, doc, mapId + " unspawned " + spawnId);
}

EditResult AddPoint(const fs::path& dataDir, const MapPointEdit& point)
{
    MapDoc doc;
    if (EditResult opened = WithDoc(dataDir, point.MapId, doc); !opened.Ok)
        return opened;
    PointRow row;
    row.Kind = PointName(point.Kind);
    row.X = point.X;
    row.Y = point.Y;
    if (point.Kind == EditPointKind::Talk)
        row.Dialogue = point.Dialogue;
    doc.Points.push_back(std::move(row));
    return Commit(dataDir, point.MapId, doc,
                  point.MapId + " " + row.Kind + " " + std::to_string(point.X) + " " +
                      std::to_string(point.Y));
}

EditResult RemovePoint(const fs::path& dataDir, const MapCell& cell)
{
    MapDoc doc;
    if (EditResult opened = WithDoc(dataDir, cell.MapId, doc); !opened.Ok)
        return opened;
    bool found = false;
    std::vector<PointRow> kept;
    for (const PointRow& point : doc.Points) {
        if (!found && point.X == cell.X && point.Y == cell.Y) {
            found = true;
            continue;
        }
        kept.push_back(point);
    }
    if (!found)
        return Fail("point at " + std::to_string(cell.X) + "," + std::to_string(cell.Y) +
                    " is not on " + Quote(cell.MapId));
    doc.Points = std::move(kept);
    return Commit(dataDir, cell.MapId, doc,
                  cell.MapId + " unpointed " + std::to_string(cell.X) + " " +
                      std::to_string(cell.Y));
}

EditResult AddProp(const fs::path& dataDir, const MapPropEdit& prop)
{
    MapDoc doc;
    if (EditResult opened = WithDoc(dataDir, prop.MapId, doc); !opened.Ok)
        return opened;
    PropRow row;
    row.Id = prop.Id;
    row.X = prop.X;
    row.Y = prop.Y;
    row.Sized = prop.Sized;
    row.Width = prop.Width;
    row.Height = prop.Height;
    for (const MapOpenEdit& open : prop.Open) {
        OpenCell hole;
        hole.X = open.X;
        hole.Y = open.Y;
        hole.Width = open.Width;
        hole.Height = open.Height;
        row.Open.push_back(hole);
    }
    doc.Props.push_back(std::move(row));
    return Commit(dataDir, prop.MapId, doc, prop.MapId + " prop " + prop.Id);
}

EditResult RemoveProp(const fs::path& dataDir, const std::string& mapId, const std::string& propId)
{
    MapDoc doc;
    if (EditResult opened = WithDoc(dataDir, mapId, doc); !opened.Ok)
        return opened;
    bool found = false;
    std::vector<PropRow> kept;
    for (const PropRow& prop : doc.Props) {
        if (prop.Id == propId) {
            found = true;
            continue;
        }
        kept.push_back(prop);
    }
    if (!found)
        return Fail("prop " + Quote(propId) + " is not on " + Quote(mapId));
    doc.Props = std::move(kept);
    return Commit(dataDir, mapId, doc, mapId + " unpropped " + propId);
}

EditResult SetHub(const fs::path& dataDir, const std::string& mapId, bool on)
{
    MapDoc doc;
    if (EditResult opened = WithDoc(dataDir, mapId, doc); !opened.Ok)
        return opened;
    doc.Hub = on;
    return Commit(dataDir, mapId, doc, mapId + (on ? " hub on" : " hub off"));
}

} // namespace Ascension
