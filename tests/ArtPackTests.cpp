// Art pack manifest: parsing, id checks, and fallback to the built-in art. No window or GPU.

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/ArtPack.h"
#include "Check.h"

namespace {

using namespace Ascension;

namespace fs = std::filesystem;

const unsigned char kPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
    0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
    0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

struct PackDir {
    fs::path Path;

    PackDir()
    {
        Path = fs::temp_directory_path() / "ascension-artpack-tests";
        std::error_code error;
        fs::remove_all(Path, error);
        fs::create_directories(Path);
    }

    ~PackDir()
    {
        std::error_code error;
        fs::remove_all(Path, error);
    }
};

void WriteText(const fs::path& path, std::string_view text)
{
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

void WritePng(const fs::path& path)
{
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(kPng), static_cast<std::streamsize>(sizeof(kPng)));
}

u32 PngCrc(const unsigned char* data, int length)
{
    u32 crc = 0xFFFFFFFFu;
    for (int index = 0; index < length; ++index) {
        crc ^= data[index];
        for (int bit = 0; bit < 8; ++bit) {
            const u32 mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

void WriteSizedPng(const fs::path& path, i32 width, i32 height)
{
    unsigned char bytes[sizeof(kPng)];
    for (usize index = 0; index < sizeof(kPng); ++index)
        bytes[index] = kPng[index];
    const u32 wide = static_cast<u32>(width);
    const u32 high = static_cast<u32>(height);
    bytes[16] = static_cast<unsigned char>((wide >> 24) & 0xFFu);
    bytes[17] = static_cast<unsigned char>((wide >> 16) & 0xFFu);
    bytes[18] = static_cast<unsigned char>((wide >> 8) & 0xFFu);
    bytes[19] = static_cast<unsigned char>(wide & 0xFFu);
    bytes[20] = static_cast<unsigned char>((high >> 24) & 0xFFu);
    bytes[21] = static_cast<unsigned char>((high >> 16) & 0xFFu);
    bytes[22] = static_cast<unsigned char>((high >> 8) & 0xFFu);
    bytes[23] = static_cast<unsigned char>(high & 0xFFu);
    const u32 crc = PngCrc(bytes + 12, 17);
    bytes[29] = static_cast<unsigned char>((crc >> 24) & 0xFFu);
    bytes[30] = static_cast<unsigned char>((crc >> 16) & 0xFFu);
    bytes[31] = static_cast<unsigned char>((crc >> 8) & 0xFFu);
    bytes[32] = static_cast<unsigned char>(crc & 0xFFu);
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(sizeof(bytes)));
}

bool LoadPack(const fs::path& directory, const Database& data, ArtPack& pack, std::string& error)
{
    DataError failed;
    const bool ok = pack.Load(directory, data, &failed);
    error = failed.Message;
    return ok;
}

void CheckBuiltin(const ArtPack& pack, ArtSlot slot, const char* id, const char* what)
{
    const ResolvedArt art = pack.Find(slot, id);
    Check(art.Origin == ArtOrigin::Builtin && art.Picture.File.empty(), what);
}

void CheckPacked(const ArtPack& pack, ArtSlot slot, const char* id, const fs::path& file,
                 const char* what)
{
    const ResolvedArt art = pack.Find(slot, id);
    std::error_code error;
    const fs::path expected = fs::weakly_canonical(file, error);
    const bool same =
        art.Origin == ArtOrigin::Pack && (art.Picture.File == file || art.Picture.File == expected);
    Check(same, what);
    if (!same)
        std::printf("    file was: %s\n", art.Picture.File.string().c_str());
}

void TestUnloadedIsBuiltin()
{
    ArtPack pack;
    Check(!pack.IsLoaded(), "a pack that was not loaded is not loaded");
    CheckBuiltin(pack, ArtSlot::Tile, "floor", "no pack keeps the built-in tile");
    CheckBuiltin(pack, ArtSlot::FoeBattle, "wolf", "no pack keeps the built-in foe");
    CheckBuiltin(pack, ArtSlot::HeroBattle, "fighter", "no pack keeps the built-in hero");
    CheckBuiltin(pack, ArtSlot::NpcPortrait, "pell", "no pack keeps the built-in portrait");
}

void TestFallback(const Database& data, const PackDir& dir)
{
    WritePng(dir.Path / "tiles" / "floor.png");
    WritePng(dir.Path / "foes" / "wolf_overland.png");
    WritePng(dir.Path / "heroes" / "fighter.PNG");
    WritePng(dir.Path / "npcs" / "pell_portrait.png");
    WriteText(dir.Path / "manifest.json",
              "{\n"
              "  \"tiles\": [ { \"id\": \"floor\", \"file\": \"tiles/floor.png\" } ],\n"
              "  \"foes\": [ { \"id\": \"wolf\", \"overland\": \"foes/wolf_overland.png\" } ],\n"
              "  \"heroes\": [ { \"id\": \"fighter\", \"battle\": \"heroes/fighter.PNG\" } ],\n"
              "  \"npcs\": [ { \"id\": \"pell\", \"portrait\": \"npcs/pell_portrait.png\" } ]\n"
              "}\n");

    ArtPack pack;
    std::string error;
    Check(LoadPack(dir.Path, data, pack, error), "a partial manifest loads");
    if (!error.empty())
        std::printf("    message was: %s\n", error.c_str());
    Check(pack.IsLoaded(), "a partial manifest is loaded");
    CheckPacked(pack, ArtSlot::Tile, "floor", dir.Path / "tiles" / "floor.png",
                "a listed tile uses the pack file");
    CheckBuiltin(pack, ArtSlot::Tile, "wall", "an unlisted tile stays on the built-in art");
    CheckPacked(pack, ArtSlot::FoeOverland, "wolf", dir.Path / "foes" / "wolf_overland.png",
                "a listed overland foe uses the pack file");
    CheckBuiltin(pack, ArtSlot::FoeBattle, "wolf", "a foe battle image left out stays built-in");
    CheckBuiltin(pack, ArtSlot::FoeOverland, "bandit", "an unlisted foe stays built-in");
    CheckPacked(pack, ArtSlot::HeroBattle, "fighter", dir.Path / "heroes" / "fighter.PNG",
                "a listed hero battle image uses the pack file");
    CheckBuiltin(pack, ArtSlot::HeroBattle, "berserker", "an unlisted class stays built-in");
    CheckPacked(pack, ArtSlot::NpcPortrait, "pell", dir.Path / "npcs" / "pell_portrait.png",
                "a listed portrait uses the pack file");
    CheckBuiltin(pack, ArtSlot::NpcSprite, "pell", "an npc sprite left out stays built-in");
}

void TestEmptyManifest(const Database& data, const PackDir& dir)
{
    WriteText(dir.Path / "manifest.json", "{}\n");
    ArtPack pack;
    std::string error;
    Check(LoadPack(dir.Path, data, pack, error), "an empty manifest loads");
    Check(pack.GetImages().empty(), "an empty manifest lists no files");
    CheckBuiltin(pack, ArtSlot::Tile, "floor", "an empty manifest falls back for every id");
}

void TestBom(const Database& data, const PackDir& dir)
{
    const std::string text = "\xEF\xBB\xBF{}\n";
    WriteText(dir.Path / "manifest.json", text);
    ArtPack pack;
    std::string error;
    Check(LoadPack(dir.Path, data, pack, error), "a manifest with a BOM loads");
}

void ExpectFail(const Database& data, const PackDir& dir, std::string_view manifest,
                const char* part, const char* what)
{
    WriteText(dir.Path / "manifest.json", manifest);
    ArtPack pack;
    std::string error;
    Check(!LoadPack(dir.Path, data, pack, error), what);
    Check(!pack.IsLoaded(), "a failed load does not keep the pack");
    CheckHas(error, "manifest.json", "a failure names the manifest");
    CheckHas(error, part, what);
}

void TestRejections(const Database& data, const PackDir& dir)
{
    WritePng(dir.Path / "tiles" / "floor.png");
    WritePng(dir.Path / "outside.png");
    ExpectFail(data, dir, "{ \"tiles\": [ { \"id\": \"no_such_tile\", \"file\": \"tiles/floor.png\" } ] }\n",
               "no_such_tile", "an unknown tile id is named");
    ExpectFail(data, dir,
               "{ \"tiles\": [ { \"id\": \"floor\", \"file\": \"tiles/floor.png\" },"
               " { \"id\": \"floor\", \"file\": \"tiles/floor.png\" } ] }\n",
               "another tile", "a repeated tile id is named");
    ExpectFail(data, dir, "{ \"tiles\": [ { \"id\": \"floor\", \"file\": \"tiles/floor.png\", \"block\": true } ] }\n",
               "block", "an unknown field is named");
    ExpectFail(data, dir, "{ \"sprites\": [] }\n", "sprites", "an unknown list is named");
    ExpectFail(data, dir, "{ \"tiles\": { \"floor\": \"tiles/floor.png\" } }\n", "must be a list",
               "a tiles object is rejected");
    ExpectFail(data, dir, "not json\n", "not valid JSON", "broken JSON is named");
    ExpectFail(data, dir, "{ \"tiles\": [ { \"id\": \"floor\", \"file\": \"../outside.png\" } ] }\n",
               "inside the pack", "a path that climbs out is rejected");
    ExpectFail(data, dir, "{ \"tiles\": [ { \"id\": \"floor\", \"file\": \"tiles/missing.png\" } ] }\n",
               "does not exist", "a missing image is named");
    WriteText(dir.Path / "tiles" / "notes.png", "not a png");
    ExpectFail(data, dir, "{ \"tiles\": [ { \"id\": \"floor\", \"file\": \"tiles/notes.png\" } ] }\n",
               "not a png", "a file that is not a png is named");
    WritePng(dir.Path / "tiles" / "floor.txt");
    ExpectFail(data, dir, "{ \"tiles\": [ { \"id\": \"floor\", \"file\": \"tiles/floor.txt\" } ] }\n",
               ".png", "a file that is not named .png is rejected");
    ExpectFail(data, dir,
               "{ \"tiles\": [ { \"id\": \"floor\", \"file\": \"tiles\\\\floor.png\" } ] }\n",
               "must use '/'", "a backslash path is rejected");
    ExpectFail(data, dir, "{ \"foes\": [ { \"id\": \"wolf\" } ] }\n", "overland",
               "a foe with no image is rejected");
    ExpectFail(data, dir, "{ \"heroes\": [ { \"id\": \"wolf\", \"battle\": \"tiles/floor.png\" } ] }\n",
               "wolf", "a hero id that is not a class is named");
    ExpectFail(data, dir, "{ \"npcs\": [ { \"id\": \"floor\", \"sprite\": \"tiles/floor.png\" } ] }\n",
               "floor", "an npc id that is not a dialogue is named");
    ExpectFail(data, dir, "{ \"heroes\": [ { \"id\": \"fighter\" } ] }\n", "battle",
               "a hero without a battle image is rejected");

    const fs::path absolute = fs::weakly_canonical(dir.Path / "outside.png");
    std::string absoluteText = absolute.generic_string();
    for (char& code : absoluteText) {
        if (code == '\\')
            code = '/';
    }
    ExpectFail(data, dir,
               "{ \"tiles\": [ { \"id\": \"floor\", \"file\": \"" + absoluteText + "\" } ] }\n",
               "inside the pack", "an absolute path is rejected");
}

void TestMissingManifest(const Database& data, const PackDir& dir)
{
    std::error_code error;
    fs::remove(dir.Path / "manifest.json", error);
    ArtPack pack;
    std::string message;
    Check(!LoadPack(dir.Path, data, pack, message), "a pack without a manifest fails");
    CheckHas(message, "could not be read", "a missing manifest says it could not be read");
}

void TestDiscover(const PackDir& dir)
{
    fs::path requested;
    std::string error;
    const fs::path exe = dir.Path / "bin";
    std::error_code code;
    fs::create_directories(exe, code);
    Check(DiscoverArtPack(requested, exe, error), "no folder beside the executable is fine");
    Check(requested.empty() && error.empty(), "a missing default folder leaves the pack unset");

    fs::create_directories(exe / "artpack", code);
    Check(DiscoverArtPack(requested, exe, error), "a folder beside the executable is accepted");
    Check(requested == exe / "artpack", "the default folder is artpack next to the executable");

    fs::path flagged = dir.Path / "private";
    fs::create_directories(flagged, code);
    Check(DiscoverArtPack(flagged, exe, error), "an explicit directory wins");
    Check(flagged == dir.Path / "private", "an explicit directory is not replaced");

    fs::path missing = dir.Path / "gone";
    Check(!DiscoverArtPack(missing, exe, error), "a missing explicit directory fails");
    CheckHas(error, "gone", "the missing directory is named");

    fs::path file = dir.Path / "not-a-dir";
    WriteText(file, "x");
    Check(!DiscoverArtPack(file, exe, error), "a file is not an art pack directory");
    CheckHas(error, "not a directory", "a file path says it is not a directory");

    fs::path emptyExe;
    fs::path untouched;
    Check(DiscoverArtPack(untouched, emptyExe, error), "an unknown executable folder does not search");
    Check(untouched.empty(), "an unknown executable folder does not invent a pack");
}

void TestStrips(const Database& data, const PackDir& dir)
{
    WriteSizedPng(dir.Path / "heroes" / "fighter.png", 4, 1);
    WriteSizedPng(dir.Path / "npcs" / "pell.png", 4, 2);
    WriteText(dir.Path / "manifest.json",
              "{\n"
              "  \"heroes\": [ { \"id\": \"fighter\", \"battle\": { \"file\": "
              "\"heroes/fighter.png\", \"frames\": 2 } } ],\n"
              "  \"npcs\": [ { \"id\": \"pell\", \"portrait\": { \"file\": \"npcs/pell.png\", "
              "\"frames\": 2, \"frameWidth\": 2, \"frameDuration\": 100 } } ]\n"
              "}\n");
    ArtPack pack;
    std::string error;
    Check(LoadPack(dir.Path, data, pack, error), "a strip manifest loads");
    if (!error.empty())
        std::printf("    message was: %s\n", error.c_str());
    const ResolvedArt battle = pack.Find(ArtSlot::HeroBattle, "fighter");
    Check(battle.Origin == ArtOrigin::Pack && battle.Picture.Frames == 2 &&
              battle.Picture.FrameWidth == 2 && battle.Picture.FrameHeight == 1 &&
              battle.Picture.FrameDurationMs == 0,
          "an omitted frame width splits the strip, and an omitted duration stays unset");
    Check(ArtFrameIndex(battle.Picture.Frames, battle.Picture.FrameDurationMs, 0.0f) == 0,
          "a strip starts on frame 0");
    Check(ArtFrameIndex(battle.Picture.Frames, battle.Picture.FrameDurationMs, 0.25f) == 1,
          "the default duration advances a two-frame strip");
    Check(ArtFrameIndex(battle.Picture.Frames, battle.Picture.FrameDurationMs, 0.4f) == 0,
          "the default duration loops a two-frame strip");
    const ResolvedArt portrait = pack.Find(ArtSlot::NpcPortrait, "pell");
    Check(portrait.Picture.Frames == 2 && portrait.Picture.FrameWidth == 2 &&
              portrait.Picture.FrameHeight == 2 && portrait.Picture.FrameDurationMs == 100,
          "a portrait strip keeps its frame width and duration");
    Check(ArtFrameIndex(portrait.Picture.Frames, portrait.Picture.FrameDurationMs, 0.05f) == 0,
          "a short portrait duration is still on frame 0");
    Check(ArtFrameIndex(portrait.Picture.Frames, portrait.Picture.FrameDurationMs, 0.15f) == 1,
          "a portrait duration advances the frame");
    CheckBuiltin(pack, ArtSlot::NpcSprite, "pell", "a portrait strip leaves the sprite built-in");

    ExpectFail(data, dir,
               "{ \"heroes\": [ { \"id\": \"fighter\", \"battle\": { \"file\": "
               "\"heroes/fighter.png\", \"frames\": 3 } } ] }\n",
               "frames", "a strip that does not divide the width is rejected");
    ExpectFail(data, dir,
               "{ \"heroes\": [ { \"id\": \"fighter\", \"battle\": { \"file\": "
               "\"heroes/fighter.png\", \"frames\": 2, \"frameWidth\": 3 } } ] }\n",
               "frameWidth", "a frame wider than the strip is rejected");
    ExpectFail(data, dir,
               "{ \"heroes\": [ { \"id\": \"fighter\", \"battle\": { \"file\": "
               "\"heroes/fighter.png\", \"frames\": 0 } } ] }\n",
               "frames", "a frame count of 0 is rejected");
}

void TestTileScope(const Database& data, const PackDir& dir)
{
    WritePng(dir.Path / "tiles" / "default.png");
    WritePng(dir.Path / "tiles" / "theme.png");
    WritePng(dir.Path / "tiles" / "map.png");
    WriteSizedPng(dir.Path / "tiles" / "variant_a.png", 2, 1);
    WritePng(dir.Path / "tiles" / "variant_b.png");
    WriteText(dir.Path / "manifest.json",
              "{\n"
              "  \"themes\": [ { \"id\": \"example_theme\", \"maps\": [\"fixture_a\"] } ],\n"
              "  \"tiles\": [\n"
              "    { \"id\": \"floor\", \"file\": \"tiles/default.png\" },\n"
              "    { \"id\": \"floor\", \"theme\": \"example_theme\", \"file\": \"tiles/theme.png\" },\n"
              "    { \"id\": \"floor\", \"map\": \"fixture_a\", \"file\": \"tiles/map.png\" },\n"
              "    { \"id\": \"wall\", \"variants\": [ \"tiles/variant_a.png\", { \"file\": "
              "\"tiles/variant_b.png\", \"frames\": 1 } ] }\n"
              "  ]\n"
              "}\n");
    ArtPack pack;
    std::string error;
    Check(LoadPack(dir.Path, data, pack, error), "scoped tiles load");
    if (!error.empty())
        std::printf("    message was: %s\n", error.c_str());

    const ResolvedArt plain = pack.Find(ArtSlot::Tile, "floor");
    Check(plain.Origin == ArtOrigin::Pack && plain.Picture.File.filename() == "default.png",
          "a tile with no cell uses the unscoped picture");
    const ResolvedArt onTheme = pack.FindPlaced(ArtSlot::Tile, "floor", "fixture_b", 1, 1);
    Check(onTheme.Picture.File.filename() == "default.png",
          "a map outside the theme keeps the unscoped picture");
    const ResolvedArt onMap = pack.FindPlaced(ArtSlot::Tile, "floor", "fixture_a", 2, 3);
    Check(onMap.Picture.File.filename() == "map.png", "a map row wins over the theme row");

    WriteText(dir.Path / "manifest.json",
              "{\n"
              "  \"themes\": [ { \"id\": \"example_theme\", \"maps\": [\"fixture_a\"] } ],\n"
              "  \"tiles\": [\n"
              "    { \"id\": \"floor\", \"file\": \"tiles/default.png\" },\n"
              "    { \"id\": \"floor\", \"theme\": \"example_theme\", \"file\": \"tiles/theme.png\" }\n"
              "  ]\n"
              "}\n");
    ArtPack themed;
    Check(LoadPack(dir.Path, data, themed, error), "a theme tile loads");
    const ResolvedArt themeCell = themed.FindPlaced(ArtSlot::Tile, "floor", "fixture_a", 0, 0);
    Check(themeCell.Picture.File.filename() == "theme.png", "a theme row is used on its maps");
    Check(themed.FindPlaced(ArtSlot::Tile, "floor", "fixture_b", 0, 0).Picture.File.filename() ==
              "default.png",
          "another map keeps the unscoped tile");

    ArtPack variants;
    WriteText(dir.Path / "manifest.json",
              "{\n"
              "  \"tiles\": [ { \"id\": \"wall\", \"variants\": [ \"tiles/variant_a.png\", "
              "\"tiles/variant_b.png\" ] } ]\n"
              "}\n");
    Check(LoadPack(dir.Path, data, variants, error), "tile variants load");
    const u32 mixed = 2u * kArtVariantX + 5u * kArtVariantY;
    const i32 index = static_cast<i32>(mixed % 2u);
    Check(ArtVariantIndex(2, 5, 2) == index, "the variant follows the cell formula");
    const ResolvedArt first = variants.FindPlaced(ArtSlot::Tile, "wall", "fixture_a", 2, 5);
    const ResolvedArt again = variants.FindPlaced(ArtSlot::Tile, "wall", "fixture_a", 2, 5);
    Check(first.Picture.File == again.Picture.File, "the same cell keeps the same variant");
    const char* names[] = {"variant_a.png", "variant_b.png"};
    Check(first.Picture.File.filename() == names[index], "the cell uses that variant file");
    CheckBuiltin(variants, ArtSlot::Tile, "floor", "a tile with no row stays built-in");
    i32 otherY = 0;
    while (otherY < 32 && ArtVariantIndex(2, otherY, 2) == index)
        ++otherY;
    Check(otherY < 32, "another cell can land on the other variant");
    const ResolvedArt other = variants.FindPlaced(ArtSlot::Tile, "wall", "fixture_b", 2, otherY);
    Check(other.Picture.File.filename() == names[1 - index], "the other cell uses the other file");

    ExpectFail(data, dir,
               "{ \"tiles\": [ { \"id\": \"floor\", \"map\": \"fixture_a\", \"theme\": "
               "\"example_theme\", \"file\": \"tiles/default.png\" } ] }\n",
               "not both", "a tile cannot name a map and a theme");
    ExpectFail(data, dir,
               "{ \"tiles\": [ { \"id\": \"floor\", \"map\": \"no_such_map\", \"file\": "
               "\"tiles/default.png\" } ] }\n",
               "no_such_map", "an unknown map is named");
    ExpectFail(data, dir,
               "{ \"themes\": [ { \"id\": \"example_theme\", \"maps\": [\"fixture_a\"] } ], "
               "\"tiles\": [ { \"id\": \"floor\", \"theme\": \"missing_theme\", \"file\": "
               "\"tiles/default.png\" } ] }\n",
               "missing_theme", "an unknown theme is named");
    ExpectFail(data, dir,
               "{ \"themes\": [ { \"id\": \"example_theme\", \"maps\": [\"fixture_a\"] }, "
               "{ \"id\": \"other_theme\", \"maps\": [\"fixture_a\"] } ] }\n",
               "already in a theme", "a map cannot sit in two themes");
    ExpectFail(data, dir,
               "{ \"tiles\": [ { \"id\": \"floor\", \"file\": \"tiles/default.png\" }, "
               "{ \"id\": \"floor\", \"file\": \"tiles/theme.png\" } ] }\n",
               "another tile", "two unscoped rows for one tile are rejected");
    ExpectFail(data, dir, "{ \"tiles\": [ { \"id\": \"floor\", \"variants\": [] } ] }\n", "variants",
               "an empty variant list is rejected");
}

void TestMarkers(const Database& data, const PackDir& dir)
{
    WritePng(dir.Path / "markers" / "hero.png");
    WritePng(dir.Path / "markers" / "exit.png");
    WriteText(dir.Path / "manifest.json",
              "{\n"
              "  \"markers\": [\n"
              "    { \"id\": \"hero\", \"file\": \"markers/hero.png\" },\n"
              "    { \"id\": \"exit\", \"file\": \"markers/exit.png\" }\n"
              "  ]\n"
              "}\n");
    ArtPack pack;
    std::string error;
    Check(LoadPack(dir.Path, data, pack, error), "marker rows load");
    if (!error.empty())
        std::printf("    message was: %s\n", error.c_str());
    CheckPacked(pack, ArtSlot::HeroMarker, "hero", dir.Path / "markers" / "hero.png",
                "the hero marker uses the pack file");
    const ResolvedArt bySlot = pack.Find(ArtSlot::HeroMarker, "");
    Check(bySlot.Origin == ArtOrigin::Pack && bySlot.Picture.File.filename() == "hero.png",
          "the hero marker is found from its slot");
    CheckPacked(pack, ArtSlot::ExitMarker, "", dir.Path / "markers" / "exit.png",
                "the exit marker uses the pack file");
    CheckBuiltin(pack, ArtSlot::RestMarker, "", "a rest marker left out stays built-in");
    CheckBuiltin(pack, ArtSlot::ShopMarker, "shop", "a shop marker left out stays built-in");

    ExpectFail(data, dir, "{ \"markers\": [ { \"id\": \"talk\", \"file\": \"markers/hero.png\" } ] }\n",
               "talk", "an unknown marker id is named");
    ExpectFail(data, dir,
               "{ \"markers\": [ { \"id\": \"hero\", \"file\": \"markers/hero.png\" }, "
               "{ \"id\": \"hero\", \"file\": \"markers/exit.png\" } ] }\n",
               "another marker", "a repeated marker id is named");
}

} // namespace

int main()
{
    Database data(ASCENSION_DATA_DIR);
    DataError loadError;
    if (!data.Load(&loadError)) {
        std::printf("[FAIL] fixture data loads: %s\n", loadError.Message.c_str());
        return 1;
    }
    TestUnloadedIsBuiltin();
    PackDir dir;
    TestFallback(data, dir);
    TestEmptyManifest(data, dir);
    TestBom(data, dir);
    TestRejections(data, dir);
    TestMissingManifest(data, dir);
    TestDiscover(dir);
    TestStrips(data, dir);
    TestTileScope(data, dir);
    TestMarkers(data, dir);
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
