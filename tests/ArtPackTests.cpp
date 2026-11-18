// Art pack manifest: parsing, id checks, and fallback to the built-in art. No window or GPU.

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/ArtPack.h"
#include "ArtGallery.h"
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

void TestProps(const Database& data, const PackDir& dir)
{
    WritePng(dir.Path / "props" / "post.png");
    WriteText(dir.Path / "manifest.json",
              "{\n"
              "  \"props\": [ { \"id\": \"fixture_post\", \"file\": \"props/post.png\", "
              "\"w\": 2, \"h\": 3 } ]\n"
              "}\n");
    ArtPack pack;
    std::string error;
    Check(LoadPack(dir.Path, data, pack, error), "a prop manifest loads");
    if (!error.empty())
        std::printf("    message was: %s\n", error.c_str());
    CheckPacked(pack, ArtSlot::Prop, "fixture_post", dir.Path / "props" / "post.png",
                "a listed prop uses the pack file");
    const ResolvedArt resolved = pack.Find(ArtSlot::Prop, "fixture_post");
    Check(resolved.Origin == ArtOrigin::Pack && resolved.CellsWide == 2 &&
              resolved.CellsHigh == 3,
          "Find returns the prop picture size in cells");
    CheckBuiltin(pack, ArtSlot::Prop, "no_such_prop", "an unlisted prop id is not drawn");
    CheckBuiltin(pack, ArtSlot::Tile, "floor", "a prop list leaves tiles on the built-in art");

    MapProp placement;
    placement.Id = "fixture_post";
    placement.Tile.X = 1;
    placement.Tile.Y = 2;
    MapDef map;
    map.Props.push_back(placement);
    Check(!MapBlocks(map, 1, 2), "an unsized placement does not block by itself");
    Check(!FootprintBlocks(map, 1, 2, nullptr), "no pack leaves an unsized placement open");
    Check(FootprintBlocks(map, 1, 2, &pack) && FootprintBlocks(map, 2, 4, &pack) &&
              !FootprintBlocks(map, 3, 2, &pack) && !FootprintBlocks(map, 1, 5, &pack),
          "the pack size is the footprint when the map omitted it");

    MapProp sized;
    sized.Id = "fixture_post";
    sized.Tile.X = 0;
    sized.Tile.Y = 0;
    sized.Width = 1;
    sized.Height = 1;
    MapSpan door;
    door.X = 0;
    door.Y = 0;
    sized.Open.push_back(door);
    MapDef holed;
    holed.Props.push_back(sized);
    Check(!FootprintBlocks(holed, 0, 0, &pack) && !MapBlocks(holed, 0, 0),
          "an open cell stays walkable even when the pack names a size");
    sized.Open.clear();
    MapDef walled;
    walled.Props.push_back(sized);
    Check(FootprintBlocks(walled, 0, 0, &pack) && !FootprintBlocks(walled, 1, 0, &pack),
          "a map footprint wins over a wider picture");

    WriteText(dir.Path / "manifest.json", "{ \"props\": [] }\n");
    ArtPack empty;
    Check(LoadPack(dir.Path, data, empty, error), "an empty prop list loads");
    CheckBuiltin(empty, ArtSlot::Prop, "fixture_post", "an empty prop list draws no prop");

    ExpectFail(data, dir,
               "{ \"props\": [ { \"id\": \"no_such_prop\", \"file\": \"props/post.png\" } ] }\n",
               "no_such_prop", "an unknown prop id is named");
    ExpectFail(data, dir,
               "{ \"props\": [ { \"id\": \"fixture_post\", \"file\": \"props/post.png\" }, "
               "{ \"id\": \"fixture_post\", \"file\": \"props/post.png\" } ] }\n",
               "another prop", "a repeated prop id is named");
    ExpectFail(data, dir,
               "{ \"props\": [ { \"id\": \"fixture_post\", \"file\": \"props/post.png\", "
               "\"block\": true } ] }\n",
               "block", "a prop rejects an unknown field");
    ExpectFail(data, dir,
               "{ \"props\": [ { \"id\": \"fixture_post\", \"file\": \"props/post.png\", "
               "\"w\": 2 } ] }\n",
               "w", "a prop names w and h together");
    ExpectFail(data, dir, "{ \"props\": { \"id\": \"fixture_post\" } }\n", "must be a list",
               "a props object is rejected");
    ExpectFail(data, dir, "{ \"props\": [ { \"id\": \"fixture_post\" } ] }\n", "file",
               "a prop needs a file");
}

void AppendU16(std::vector<unsigned char>& out, u16 value)
{
    out.push_back(static_cast<unsigned char>(value));
    out.push_back(static_cast<unsigned char>(value >> 8));
}

void AppendU32(std::vector<unsigned char>& out, u32 value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<unsigned char>(value >> shift));
}

void AppendU64(std::vector<unsigned char>& out, u64 value)
{
    AppendU32(out, static_cast<u32>(value));
    AppendU32(out, static_cast<u32>(value >> 32));
}

u32 BlobCrc(const std::vector<unsigned char>& bytes)
{
    u32 crc = 0xFFFFFFFFu;
    for (unsigned char byte : bytes) {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit) {
            const u32 mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

struct PakItem {
    std::string Path;
    std::vector<unsigned char> Bytes;
};

std::vector<unsigned char> BuildPak(const std::vector<PakItem>& items)
{
    u64 tocSize = 0;
    for (const PakItem& item : items)
        tocSize += 2 + item.Path.size() + 24;
    u64 blobAt = (32 + tocSize + 7ull) & ~7ull;
    std::vector<unsigned char> toc;
    std::vector<unsigned char> blobs;
    u64 cursor = blobAt;
    for (const PakItem& item : items) {
        while (blobs.size() + blobAt < cursor)
            blobs.push_back(0);
        AppendU16(toc, static_cast<u16>(item.Path.size()));
        toc.insert(toc.end(), item.Path.begin(), item.Path.end());
        AppendU64(toc, cursor);
        AppendU64(toc, item.Bytes.size());
        AppendU32(toc, BlobCrc(item.Bytes));
        AppendU32(toc, 0);
        blobs.insert(blobs.end(), item.Bytes.begin(), item.Bytes.end());
        cursor = (cursor + item.Bytes.size() + 7ull) & ~7ull;
    }
    std::vector<unsigned char> file = {'A', 'P', 'A', 'K'};
    AppendU16(file, 1);
    AppendU16(file, 0);
    AppendU32(file, static_cast<u32>(items.size()));
    AppendU64(file, 32);
    AppendU64(file, toc.size());
    AppendU32(file, 0);
    file.insert(file.end(), toc.begin(), toc.end());
    while (file.size() < blobAt)
        file.push_back(0);
    file.insert(file.end(), blobs.begin(), blobs.end());
    return file;
}

std::vector<unsigned char> BytesOf(std::string_view text)
{
    return {text.begin(), text.end()};
}

void TestPak(const Database& data, const PackDir& dir)
{
    const std::vector<unsigned char> png(kPng, kPng + sizeof(kPng));
    const std::string manifest =
        "{\n"
        "  \"credit\": \"Willosuna\",\n"
        "  \"tiles\": [ { \"id\": \"floor\", \"file\": \"tiles/floor.png\" } ],\n"
        "  \"ui\": { \"nineslice\": { \"panel\": { \"file\": \"ui/panel.png\", \"border_px\": 6 } },\n"
        "            \"files\": [ \"ui/panel.png\" ] },\n"
        "  \"fonts\": { \"roles\": { \"ui\": { \"file\": \"fonts/ui.ttf\", \"size\": 8 } } },\n"
        "  \"audio\": { \"roles\": { \"title\": \"audio/title.ogg\" },\n"
        "              \"files\": [ \"audio/title.ogg\" ] },\n"
        "  \"icons\": [ { \"id\": \"armor/boots\", \"file\": \"icons/boots.png\" } ],\n"
        "  \"vfx\": [ { \"id\": \"combat/hits/spark\", \"file\": \"vfx/spark.png\" } ]\n"
        "}\n";
    std::vector<unsigned char> font = {'t', 't', 'f'};
    std::vector<unsigned char> ogg = {'O', 'g', 'g', 'S'};
    const fs::path pak = dir.Path / "ascension.pak";
    {
        const std::vector<unsigned char> bytes = BuildPak({
            {"audio/title.ogg", ogg},
            {"fonts/ui.ttf", font},
            {"icons/boots.png", png},
            {"manifest.json", BytesOf(manifest)},
            {"tiles/floor.png", png},
            {"ui/panel.png", png},
            {"vfx/spark.png", png},
        });
        std::ofstream out(pak, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }

    fs::path flagged = pak;
    std::string error;
    Check(DiscoverArtPack(flagged, dir.Path, error), "a pak file is an art pack");
    Check(flagged == pak, "a pak path is kept");

    fs::path folder = dir.Path;
    Check(DiscoverArtPack(folder, dir.Path / "bin", error), "a directory containing the pak is accepted");
    Check(folder == pak, "a directory resolves to ascension.pak");

    fs::path beside;
    const fs::path exe = dir.Path / "exe";
    std::error_code code;
    fs::create_directories(exe / "artpack", code);
    fs::copy_file(pak, exe / "artpack" / "ascension.pak", fs::copy_options::overwrite_existing, code);
    Check(DiscoverArtPack(beside, exe, error), "the default pak beside the executable is accepted");
    Check(beside == exe / "artpack" / "ascension.pak", "the default file is artpack/ascension.pak");

    ArtPack pack;
    Check(LoadPack(pak, data, pack, error), "a pak manifest loads");
    if (!error.empty())
        std::printf("    message was: %s\n", error.c_str());
    Check(pack.HasArchive(), "a pak load keeps the archive");
    Check(pack.Credit() == "Willosuna", "the credit is the manifest string");
    Check(ArtCreditLine(&pack) == "Art and audio: Willosuna", "the credit line names Willosuna");
    const ResolvedArt tile = pack.Find(ArtSlot::Tile, "floor");
    Check(tile.Origin == ArtOrigin::Pack && tile.Picture.File == "tiles/floor.png",
          "a pak tile keeps the relative path");
    CheckBuiltin(pack, ArtSlot::Tile, "wall", "a pak id that is not listed stays built-in");
    const ArtNineSlice* slice = pack.FindSlice("panel");
    Check(slice != nullptr && slice->Border == 6, "a 9-slice margin is read");
    const ArtFontRole* fontRole = pack.FindFont("ui");
    Check(fontRole != nullptr && fontRole->Size == 8 && fontRole->File == "fonts/ui.ttf",
          "a font role resolves its path and size");
    const ArtAudioRole* title = pack.FindAudio("title");
    Check(title != nullptr && title->File == "audio/title.ogg", "a cue role resolves its path");
    Check(pack.FindIcon("armor/boots") != nullptr, "an icon id with a slash loads");
    Check(pack.FindIcon("missing") == nullptr, "an icon the pak does not list is absent");
    const ArtNamedFile* spark = pack.FindVfx("combat/hits/spark");
    Check(spark != nullptr, "a vfx sheet id loads");

    std::vector<unsigned char> broken = BuildPak({{"manifest.json", BytesOf("{}\n")}});
    if (!broken.empty())
        broken.back() ^= 0xFF;
    const fs::path bad = dir.Path / "bad.pak";
    {
        std::ofstream out(bad, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(broken.data()),
                  static_cast<std::streamsize>(broken.size()));
    }
    ArtPack failed;
    Check(!LoadPack(bad, data, failed, error), "a bad crc fails the load");
    CheckHas(error, "crc", "a crc failure names the field");
    CheckHas(error, "bad.pak", "a crc failure names the file");

    const fs::path field = dir.Path / "field.pak";
    const std::string badManifest = "{ \"ui\": { \"nineslice\": { \"panel\": { \"file\": \"ui/panel.png\" } } } }\n";
    const std::vector<unsigned char> fieldBytes = BuildPak({
        {"manifest.json", BytesOf(badManifest)},
        {"ui/panel.png", png},
    });
    {
        std::ofstream out(field, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(fieldBytes.data()),
                  static_cast<std::streamsize>(fieldBytes.size()));
    }
    Check(!LoadPack(field, data, failed, error), "a slice without a margin fails");
    CheckHas(error, "border_px", "a bad slice names the field");
    CheckHas(error, "manifest.json", "a bad slice names the manifest");
}

void TestGalleryBudget(const PackDir& dir)
{
    GalleryBudget missing;
    std::string error;
    const fs::path empty = dir.Path / "empty-data";
    std::error_code code;
    fs::create_directories(empty, code);
    Check(LoadGalleryBudget(empty, missing, error), "a missing gallery.json keeps the defaults");
    Check(missing.UploadsPerFrame == 8, "the default background count is 8");
    Check(missing.BudgetMilliseconds == 4, "the default background budget is 4 ms");
    Check(!missing.Order.empty() && missing.Order.front() == "fonts", "fonts stream first by default");

    WriteText(dir.Path / "tuned" / "gallery.json",
              "{\n"
              "  \"gallery\": {\n"
              "    \"uploadsPerFrame\": 0,\n"
              "    \"budgetMilliseconds\": 12,\n"
              "    \"order\": [\"tiles\", \"ui\"]\n"
              "  }\n"
              "}\n");
    GalleryBudget tuned;
    Check(LoadGalleryBudget(dir.Path / "tuned", tuned, error), "a gallery budget loads");
    Check(tuned.UploadsPerFrame == 0, "zero uploads turns the background stream off");
    Check(tuned.BudgetMilliseconds == 12, "the millisecond budget is kept");
    Check(tuned.Order.size() >= 2 && tuned.Order[0] == "tiles" && tuned.Order[1] == "ui",
          "the listed sections stay in front");
    Check(!tuned.Order.empty() && tuned.Order.back() == "vfx", "a section left out is appended");

    WriteText(dir.Path / "bad-gallery" / "gallery.json", "{ \"gallery\": { \"uploads\": 1 } }\n");
    GalleryBudget bad;
    Check(!LoadGalleryBudget(dir.Path / "bad-gallery", bad, error), "an unknown gallery field fails");
    CheckHas(error, "uploads", "the unknown field is named");
    CheckHas(error, "gallery.json", "the file is named");

    WriteText(dir.Path / "repeat-gallery" / "gallery.json",
              "{ \"gallery\": { \"order\": [\"ui\", \"ui\"] } }\n");
    Check(!LoadGalleryBudget(dir.Path / "repeat-gallery", bad, error), "a repeated section fails");
    CheckHas(error, "repeated", "the repeat is named");

    GalleryBudget shipped;
    Check(LoadGalleryBudget(ASCENSION_SHIPPED_DATA_DIR, shipped, error), "data/gallery.json loads");
    if (!error.empty())
        std::printf("    message was: %s\n", error.c_str());
    Check(shipped.UploadsPerFrame > 0 && shipped.BudgetMilliseconds > 0,
          "the shipped budget streams");
}

void TestGalleryDefers(const Database& data, const PackDir& dir)
{
    const fs::path root = dir.Path / "defer";
    WritePng(root / "tiles" / "floor.png");
    WritePng(root / "ui" / "panel.png");
    WriteText(root / "manifest.json",
              "{\n"
              "  \"tiles\": [ { \"id\": \"floor\", \"file\": \"tiles/floor.png\" } ],\n"
              "  \"ui\": { \"nineslice\": { \"panel\": { \"file\": \"ui/panel.png\", \"border_px\": 4 } } }\n"
              "}\n");
    ArtPack pack;
    std::string error;
    Check(LoadPack(root, data, pack, error), "a two-picture pack loads");
    if (!error.empty())
        std::printf("    message was: %s\n", error.c_str());
    GalleryBudget budget;
    budget.UploadsPerFrame = 0;
    budget.BudgetMilliseconds = 0;
    budget.Order = {"ui", "tiles"};
    ArtGallery gallery;
    Check(gallery.Load(nullptr, nullptr, pack, budget, error), "the gallery catalogues without a device");
    Check(error.empty(), "cataloguing clears the error");
    Check(gallery.Pending() == 2, "both pictures wait");
    Check(gallery.Resident() == 0, "nothing is uploaded at startup");
    Check(!gallery.IsEmpty(), "a catalogued pack is not the built-in art");
    gallery.Pump();
    Check(gallery.Resident() == 0, "a zero budget does not upload");
    const std::vector<fs::path> order = gallery.UploadOrder();
    Check(order.size() == 2, "the queue lists both files");
    if (order.size() == 2) {
        Check(order[0].filename() == "panel.png", "ui is ahead of tiles");
        Check(order[1].filename() == "floor.png", "the tile follows the panel");
    }
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
    TestProps(data, dir);
    TestPak(data, dir);
    TestGalleryBudget(dir);
    TestGalleryDefers(data, dir);
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
