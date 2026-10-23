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
    Check(art.Origin == ArtOrigin::Builtin && art.File.empty(), what);
}

void CheckPacked(const ArtPack& pack, ArtSlot slot, const char* id, const fs::path& file,
                 const char* what)
{
    const ResolvedArt art = pack.Find(slot, id);
    std::error_code error;
    const fs::path expected = fs::weakly_canonical(file, error);
    const bool same = art.Origin == ArtOrigin::Pack && (art.File == file || art.File == expected);
    Check(same, what);
    if (!same)
        std::printf("    file was: %s\n", art.File.string().c_str());
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
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
