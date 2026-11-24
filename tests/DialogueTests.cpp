// Dialogue: load, flag syntax, the Fang gate, and Pell's approved lines. No window or GPU.

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/ArtPack.h"
#include "Ascension/Data/Database.h"
#include "Ascension/Data/Talk.h"
#include "Ascension/Progress/Hero.h"
#include "Check.h"

namespace {

using namespace Ascension;

namespace fs = std::filesystem;

const fs::path kSample = ASCENSION_DATA_DIR;
const fs::path kShipped = ASCENSION_SHIPPED_DATA_DIR;

struct TempCopy {
    fs::path Path;

    TempCopy()
    {
        Path = fs::temp_directory_path() / "ascension-dialogue-tests";
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

const unsigned char kPortraitPng[] = {
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
    out.write(reinterpret_cast<const char*>(kPortraitPng),
              static_cast<std::streamsize>(sizeof(kPortraitPng)));
}

bool SamePortrait(const fs::path& got, const fs::path& expected)
{
    if (got.empty())
        return false;
    std::error_code error;
    const fs::path canonical = fs::weakly_canonical(expected, error);
    return got == expected || (!error && got == canonical);
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
}

void Reject(const fs::path& dir, const char* file, const char* part, const char* what)
{
    Database data(dir);
    DataError error;
    Check(!data.Load(&error), what);
    CheckHas(error.Message, file, "the error names the file");
    CheckHas(error.Message, part, what);
}

struct World {
    std::vector<std::string> Flags;
    std::vector<std::string> Found;
    std::vector<std::string> Defeated;
    std::string Node;
    TalkList Lists[2];

    [[nodiscard]] TalkFacts Facts()
    {
        Lists[0].Name = "found";
        Lists[0].Ids = Found;
        Lists[1].Name = "defeated";
        Lists[1].Ids = Defeated;
        TalkFacts facts;
        facts.Flags = Flags;
        facts.Lists = std::span<const TalkList>(Lists, 2);
        return facts;
    }
};

const DialogueDef* MustPell(const Database& data)
{
    const DialogueDef* pell = data.FindDialogue("pell");
    Check(pell != nullptr && data.GetShop().Dialogue == "pell", "the shop talks to pell");
    return pell;
}

bool Enter(const DialogueDef& talk, World& world)
{
    const TalkFacts facts = world.Facts();
    const DialogueNode* node = TalkEntry(talk, facts);
    if (node == nullptr)
        return false;
    world.Node = node->Id;
    TalkApplySets(node->Sets, world.Flags);
    return true;
}

bool Pick(const DialogueDef& talk, World& world, i32 index, TalkMove& move)
{
    const TalkFacts facts = world.Facts();
    return TalkChoose(talk, world.Node, world.Flags, facts, index, move);
}

const DialogueChoice* VisibleChoice(const DialogueDef& talk, const World& world, i32 index)
{
    const DialogueNode* node = talk.FindNode(world.Node);
    if (node == nullptr)
        return nullptr;
    World copy = world;
    std::vector<i32> visible;
    TalkVisible(talk, *node, copy.Facts(), visible);
    if (index < 0 || index >= static_cast<i32>(visible.size()))
        return nullptr;
    return &node->Choices[static_cast<usize>(visible[static_cast<usize>(index)])];
}

void CheckPieces(const DialogueNode& node, const char* const* pieces, i32 count, const char* what)
{
    std::vector<std::string> lines;
    TalkBoxLines(node.Line, kTalkLineWidth, lines);
    Check(static_cast<i32>(lines.size()) == count, what);
    for (i32 index = 0; index < count; ++index) {
        const bool fits = pieces[index] != nullptr && std::string(pieces[index]).size() <= 40;
        const bool same = index < static_cast<i32>(lines.size()) && lines[static_cast<usize>(index)] == pieces[index];
        Check(fits && same, what);
    }
}

void TestLines(const Database& data)
{
    const DialogueDef* pell = MustPell(data);
    if (pell == nullptr)
        return;
    struct Row {
        const char* Id;
        const char* Note;
        const char* Pieces[3];
        i32 Count;
    };
    const Row rows[] = {
        {"pell_greet_b", "rare unease; the grumble drops away.",
         {"Back again, what's that you've got?", "No. Don't take it out. I can feel it."}, 2},
        {"pell_greet_a", "dry, grumbling warmth.",
         {"Back again? Good. Knee says rain.", "Buying, or just leaning on my counter?"}, 2},
        {"pell_rumor_1", "he lowers his voice and leans in.",
         {"Rumors. Hm.", "Wolf pack's got a new leader out in", "the Outer Barrow. Big. Dire big."}, 3},
        {"pell_rumor_2", "matter-of-fact, which makes it worse.",
         {"Hunter saw it up close. Lived, barely.", "Says it's got a tooth that isn't",
          "its own. Too long. Wrong color."},
         3},
        {"pell_rumor_2b", "a little offended that you'd brush it off.",
         {"Not this one. Doesn't eat what it", "kills. Hunter swears it's got a tooth",
          "that isn't its own."},
         3},
        {"pell_rumor_3", "quiet; the one moment he isn't joking.",
         {"Like bone that's been in the ground", "too long. I dug these hills forty",
          "years. Some things you leave buried."},
         3},
        {"pell_rumor_end", "back to grumbling, covering the unease.",
         {"Then buy a potion first. Advice is", "free. Potions aren't."}, 2},
        {"pell_fang_1", "a warning from someone who once heard the stone knock back.",
         {"Course it did.", "Things that come out of those hills", "rarely come out alone. Watch yourself."},
         3},
        {"pell_bye", "fond and gruff.",
         {"Mind the barrow. And your knees.", "Trust me on the knees."}, 2},
    };
    Check(pell->Nodes.size() == std::size(rows), "Pell has the approved nodes");
    for (const Row& row : rows) {
        const DialogueNode* node = pell->FindNode(row.Id);
        Check(node != nullptr && node->Note == row.Note, row.Id);
        if (node != nullptr)
            CheckPieces(*node, row.Pieces, row.Count, row.Id);
    }
}

void TestWrap()
{
    std::vector<std::string> lines;
    TalkBoxLines("alpha / beta", kTalkLineWidth, lines);
    Check(lines.size() == 2 && lines[0] == "alpha" && lines[1] == "beta",
          "a slash is a box break and the spaces around it drop");
    const std::string longLine(50, 'x');
    TalkBoxLines(longLine, kTalkLineWidth, lines);
    Check(lines.size() == 2 && lines[0].size() == 40 && lines[1].size() == 10,
          "a line with no space wraps at 40 characters");
}

void TestGreetA(const Database& data)
{
    const DialogueDef* pell = MustPell(data);
    if (pell == nullptr)
        return;
    World world;
    Check(Enter(*pell, world) && world.Node == "pell_greet_a",
          "before the Fang is found the talk starts at greeting a");
    const DialogueChoice* browse = VisibleChoice(*pell, world, 0);
    const DialogueChoice* rumor = VisibleChoice(*pell, world, 1);
    const DialogueChoice* pass = VisibleChoice(*pell, world, 2);
    Check(browse != nullptr && browse->Kind == TalkChoiceKind::Action && browse->Action == "shop_open" &&
              browse->Text == "Browse wares",
          "browsing is the shop_open action");
    Check(rumor != nullptr && rumor->Text == "Heard any rumors?" && rumor->Next == "pell_rumor_1",
          "greeting a offers the rumor");
    Check(pass != nullptr && pass->Text == "Just passing through.", "greeting a can leave");

    TalkMove move;
    Check(Pick(*pell, world, 1, move) && move.Step == TalkStep::Moved && world.Node == "pell_rumor_1",
          "the rumor chain starts");
    const DialogueNode* rumorNode = pell->FindNode(world.Node);
    Check(rumorNode != nullptr && rumorNode->Line.find("Dire big") != std::string::npos,
          "the first rumor names the dire wolf");
    Check(Pick(*pell, world, 0, move) && world.Node == "pell_rumor_2", "asking what is odd continues");
    const DialogueNode* tooth = pell->FindNode("pell_rumor_2");
    Check(tooth != nullptr && tooth->Line.find("tooth that isn't") != std::string::npos,
          "the rumor names a tooth that isn't its own");
    Check(Pick(*pell, world, 0, move) && world.Node == "pell_rumor_3", "asking how it is wrong continues");
    Check(Pick(*pell, world, 0, move) && world.Node == "pell_rumor_end",
          "offering to look reaches the end of the rumor");
    const DialogueChoice* leave = VisibleChoice(*pell, world, 1);
    Check(leave != nullptr && leave->Text == "Leave" && leave->Kind == TalkChoiceKind::End,
          "Leave ends the talk");
    Check(Pick(*pell, world, 1, move) && move.Step == TalkStep::Ended, "END closes the rumor");

    World brushed;
    Check(Enter(*pell, brushed), "greeting a is still the start");
    Check(Pick(*pell, brushed, 1, move) && brushed.Node == "pell_rumor_1", "the rumor opens again");
    Check(Pick(*pell, brushed, 1, move) && brushed.Node == "pell_rumor_2b",
          "brushing the rumor off takes the other branch");
    const DialogueNode* other = pell->FindNode("pell_rumor_2b");
    Check(other != nullptr && other->Line.find("Doesn't eat what it") != std::string::npos,
          "the other branch still reaches the tooth");
    Check(Pick(*pell, brushed, 0, move) && brushed.Node == "pell_rumor_3",
          "that branch rejoins the rumor");

    World shop;
    Check(Enter(*pell, shop), "greeting a opens for the shop");
    Check(Pick(*pell, shop, 0, move) && move.Step == TalkStep::Action && move.Action == "shop_open",
          "Browse wares returns shop_open");

    World bye;
    Check(Enter(*pell, bye) && Pick(*pell, bye, 2, move) && bye.Node == "pell_bye",
          "passing through reaches the goodbye");
    const DialogueNode* goodbye = pell->FindNode("pell_bye");
    Check(goodbye != nullptr && goodbye->Choices.empty(), "the goodbye has no choices");
    Check(Pick(*pell, bye, -1, move) && move.Step == TalkStep::Ended, "confirming the goodbye ends the talk");
}

void TestGreetB(const Database& data)
{
    const DialogueDef* pell = MustPell(data);
    if (pell == nullptr)
        return;
    World world;
    world.Found.push_back("alphas_fang");
    Check(Enter(*pell, world) && world.Node == "pell_greet_b",
          "after the Fang is found the talk starts at greeting b");
    const DialogueNode* greet = pell->FindNode(world.Node);
    Check(greet != nullptr && greet->Line.find("Don't take it out") != std::string::npos,
          "greeting b is the line for a found Fang");
    bool rumor = false;
    const DialogueNode* node = pell->FindNode(world.Node);
    if (node != nullptr) {
        for (const DialogueChoice& choice : node->Choices) {
            if (choice.Text == "Heard any rumors?")
                rumor = true;
        }
    }
    Check(!rumor, "the rumor choice is gated off once the Fang is found");
    const DialogueChoice* wolf = VisibleChoice(*pell, world, 0);
    const DialogueChoice* browse = VisibleChoice(*pell, world, 1);
    Check(wolf != nullptr && wolf->Text == "The wolf had it." && wolf->Next == "pell_fang_1",
          "greeting b can name the wolf");
    Check(browse != nullptr && browse->Action == "shop_open", "greeting b can still open the shop");

    TalkMove move;
    Check(Pick(*pell, world, 0, move) && world.Node == "pell_fang_1", "the Fang line follows");
    Check(Pick(*pell, world, 1, move) && move.Step == TalkStep::Ended, "Leave ends greeting b");

    World again;
    again.Found.push_back("alphas_fang");
    Check(Enter(*pell, again) && Pick(*pell, again, 2, move) && again.Node == "pell_bye",
          "passing through from greeting b still says goodbye");
}

void TestSets()
{
    TempCopy copy;
    WriteFile(copy.Path / "dialogue" / "setter.json",
              "{\n"
              "  \"dialogue\": {\n"
              "    \"id\": \"setter\",\n"
              "    \"name\": \"Setter\",\n"
              "    \"nodes\": [\n"
              "      { \"id\": \"hi\", \"line\": \"Hello.\", \"sets\": [\"met\"], \"choices\": [\n"
              "        { \"text\": \"Yes.\", \"next\": \"bye\", \"sets\": [\"agreed\"] }\n"
              "      ] },\n"
              "      { \"id\": \"bye\", \"line\": \"Good.\", \"requires\": [\"agreed\"], \"choices\": [] }\n"
              "    ]\n"
              "  }\n"
              "}\n");
    Database data(copy.Path);
    DataError error;
    Check(data.Load(&error), "a talk that sets flags loads");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    const DialogueDef* talk = data.FindDialogue("setter");
    Check(talk != nullptr, "the extra talk is kept");
    if (talk == nullptr)
        return;
    World world;
    Check(Enter(*talk, world) && world.Node == "hi", "the unguarded node starts");
    bool met = false;
    for (const std::string& flag : world.Flags) {
        if (flag == "met")
            met = true;
    }
    Check(met, "entering the node sets its flag");
    TalkMove move;
    Check(Pick(*talk, world, 0, move) && world.Node == "bye", "the choice moves and sets agreed");
    bool agreed = false;
    for (const std::string& flag : world.Flags) {
        if (flag == "agreed")
            agreed = true;
    }
    Check(agreed, "the choice sets its flag");
    Check(Pick(*talk, world, -1, move) && move.Step == TalkStep::Ended, "an empty menu ends");
}

void TestValidation()
{
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "dialogue" / "pell.json", "\"next\": \"pell_fang_1\"",
                      "\"next\": \"no_such_node\"");
        Reject(copy.Path, "dialogue/pell.json", "no_such_node", "a next id has to name a node");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "dialogue" / "pell.json", "\"found.alphas_fang\"",
                      "\"fang_found == false\"");
        Reject(copy.Path, "dialogue/pell.json", "unknown flag syntax", "a bad flag expression is rejected");
        Reject(copy.Path, "dialogue/pell.json", "fang_found == false", "the bad flag text is named");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "dialogue" / "pell.json",
                      "\"requires\": [\"found.alphas_fang\"]",
                      "\"requires\": [\"found.alphas_fang\"], \"sets\": [\"found.alphas_fang\"]");
        Reject(copy.Path, "dialogue/pell.json", "unknown flag syntax",
               "sets cannot write a list member");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "dialogue" / "pell.json", "\"name\": \"Pell Harrow\",\n", "");
        Reject(copy.Path, "dialogue/pell.json", "'name' is required", "a talk needs its own name");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "dialogue" / "pell.json", "\"portrait\": \"pell\"",
                      "\"portrait\": \"\"");
        Reject(copy.Path, "dialogue/pell.json", "'portrait' must not be empty",
               "an empty portrait is rejected");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "dialogue" / "pell.json", "\"portrait\": \"pell\"",
                      "\"portrait\": \"Pell Harrow\"");
        Reject(copy.Path, "dialogue/pell.json", "'portrait' must be letters",
               "a portrait id has to be an id");
    }
    {
        TempCopy copy;
        ReplaceInFile(copy.Path / "dialogue" / "pell.json", "\"name\": \"Pell Harrow\"",
                      "\"name\": \"{keeper}\"");
        Reject(copy.Path, "dialogue/pell.json", "must not use a placeholder",
               "a speaker name cannot be a placeholder");
    }
}

bool HasFlag(const World& world, const char* id)
{
    for (const std::string& flag : world.Flags) {
        if (flag == id)
            return true;
    }
    return false;
}

void TestCalder()
{
    Database data(kShipped);
    DataError error;
    Check(data.Load(&error), "shipped data loads Calder");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    const DialogueDef* calder = data.FindDialogue("calder");
    Check(calder != nullptr, "shipped data has Calder");
    if (calder == nullptr)
        return;

    World world;
    TalkMove move;
    Check(Enter(*calder, world) && world.Node == "calder_greet", "Calder starts with the sluice");
    const DialogueNode* greet = calder->FindNode("calder_greet");
    Check(greet != nullptr && greet->Line.find("under the pond") != std::string::npos,
          "the greeting names the pond");
    Check(Pick(*calder, world, 0, move) && world.Node == "calder_take" && HasFlag(world, "sluice_asked"),
          "taking the errand sets sluice_asked");
    const DialogueNode* take = calder->FindNode("calder_take");
    Check(take != nullptr && take->Line.find("West") != std::string::npos, "the errand points west");
    Check(Pick(*calder, world, -1, move) && move.Step == TalkStep::Ended, "the errand line ends");

    World waiting = world;
    Check(Enter(*calder, waiting) && waiting.Node == "calder_waiting",
          "an open errand waits on the wolf");

    World where;
    Check(Enter(*calder, where) && Pick(*calder, where, 1, move) && where.Node == "calder_where",
          "asking where opens the other branch");
    const DialogueNode* place = calder->FindNode("calder_where");
    Check(place != nullptr && place->Line.find("Not the barrow") != std::string::npos &&
              HasFlag(where, "sluice_asked"),
          "Calder separates the sluice from the barrow");

    World done;
    done.Defeated.push_back("sluice_wolf");
    Check(Enter(*calder, done) && done.Node == "calder_done" && HasFlag(done, "sluice_done"),
          "a felled sluice wolf opens the thanks and closes the errand");
    const DialogueNode* thanks = calder->FindNode("calder_done");
    Check(thanks != nullptr && thanks->Line.find("my thanks") != std::string::npos,
          "the thanks is the closing line");

    World after = done;
    Check(Enter(*calder, after) && after.Node == "calder_after", "the next talk is the quiet ditch");
}

void TestBram()
{
    Database data(kShipped);
    DataError error;
    Check(data.Load(&error), "shipped data loads Bram");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    const DialogueDef* bram = data.FindDialogue("bram");
    Check(bram != nullptr, "shipped data has Bram");
    if (bram == nullptr)
        return;

    World world;
    TalkMove move;
    Check(Enter(*bram, world) && world.Node == "bram_greet", "Bram starts with the hollow");
    const DialogueNode* greet = bram->FindNode("bram_greet");
    Check(greet != nullptr && greet->Line.find("charcoal") != std::string::npos,
          "the greeting names the charcoal");
    Check(Pick(*bram, world, 0, move) && world.Node == "bram_take" && HasFlag(world, "hollow_asked"),
          "taking the errand sets hollow_asked");
    const DialogueNode* take = bram->FindNode("bram_take");
    Check(take != nullptr && take->Line.find("South") != std::string::npos, "the errand points south");
    Check(Pick(*bram, world, -1, move) && move.Step == TalkStep::Ended, "the errand line ends");

    World waiting = world;
    Check(Enter(*bram, waiting) && waiting.Node == "bram_waiting",
          "an open errand waits on the boar");

    World where;
    Check(Enter(*bram, where) && Pick(*bram, where, 1, move) && where.Node == "bram_where",
          "asking where opens the other branch");
    const DialogueNode* place = bram->FindNode("bram_where");
    Check(place != nullptr && place->Line.find("Not the sluice") != std::string::npos &&
              HasFlag(where, "hollow_asked"),
          "Bram separates the hollow from the sluice");

    World done;
    done.Defeated.push_back("pit_boar");
    Check(Enter(*bram, done) && done.Node == "bram_done" && HasFlag(done, "hollow_done"),
          "a felled pit boar opens the thanks and closes the errand");
    const DialogueNode* thanks = bram->FindNode("bram_done");
    Check(thanks != nullptr && thanks->Line.find("band is yours") != std::string::npos,
          "the thanks names the found band");

    World after = done;
    Check(Enter(*bram, after) && after.Node == "bram_after", "the next talk is the quiet pit");
}

TitleFact MakeFact(const char* id, const char* name, const char* description, bool owned,
                   bool equipped, i32 slot)
{
    TitleFact fact;
    fact.Id = id;
    fact.Name = name;
    fact.Description = description;
    fact.Owned = owned;
    fact.Equipped = equipped;
    fact.Slot = slot;
    return fact;
}

TitleSenseLine SenseLine(TitleSenseWhen when, const char* id, const char* text)
{
    TitleSenseLine line;
    line.When = when;
    if (id != nullptr)
        line.TitleId = id;
    line.Line = text;
    return line;
}

TitleSenseMatch Hear(const TitleSense& sense, const std::vector<TitleFact>& facts)
{
    return ResolveTitleSense(sense, facts);
}

void TestTitleConditions()
{
    const TitleFact wolf = MakeFact("wolf_cull", "Wolf Cull", "The System counted a wolf.", true,
                                    true, 0);
    const TitleFact wolfHeld =
        MakeFact("wolf_cull", "Wolf Cull", "The System counted a wolf.", true, false, -1);
    const TitleFact beastHeld =
        MakeFact("beast_hunter", "Beast Hunter", "The System counted four beasts.", true, false,
                 -1);
    const TitleFact tomb = MakeFact("tombwalker", "Tombwalker", "The System counted three humanoids.",
                                    true, true, 0);
    const TitleFact bear = MakeFact("bear_cull", "Bear Cull", "The System counted the bear.", true,
                                    true, 1);
    const TitleFact unnamed = MakeFact("gone_title", "", "", true, true, 0);

    TitleSense specific;
    specific.Lines.push_back(SenseLine(TitleSenseWhen::Equipped, "wolf_cull", "the wolf line"));
    specific.Lines.push_back(SenseLine(TitleSenseWhen::Owned, "beast_hunter", "kept {title.name}"));
    specific.Lines.push_back(
        SenseLine(TitleSenseWhen::Fallback, nullptr, "{title.name} / {title.description}"));

    {
        const std::vector<TitleFact> facts{wolf, beastHeld};
        const TitleSenseMatch match = Hear(specific, facts);
        Check(match.Spoken && match.Line == "the wolf line" && match.TitleId == "wolf_cull",
              "a worn title uses its own line");
    }
    {
        const std::vector<TitleFact> facts{wolfHeld, beastHeld};
        const TitleSenseMatch match = Hear(specific, facts);
        Check(match.Spoken && match.Line == "kept Beast Hunter" && match.TitleId == "beast_hunter",
              "an owned title that is not worn uses the owned line");
    }
    {
        const TitleFact beastWorn = MakeFact("beast_hunter", "Beast Hunter",
                                             "The System counted four beasts.", true, true, 0);
        TitleSense ownedOnly;
        ownedOnly.Lines.push_back(
            SenseLine(TitleSenseWhen::Owned, "beast_hunter", "kept {title.name}"));
        const std::vector<TitleFact> facts{beastWorn};
        const TitleSenseMatch match = Hear(ownedOnly, facts);
        Check(match.Spoken && match.Line == "kept Beast Hunter",
              "a worn title still satisfies an owned check");
    }
    {
        const std::vector<TitleFact> facts{wolfHeld};
        const TitleSenseMatch match = Hear(specific, facts);
        Check(!match.Spoken && match.Line.empty() && match.TitleId.empty(),
              "an owned title with no owned line and nothing worn stays silent");
    }
    {
        const std::vector<TitleFact> facts{
            MakeFact("wolf_cull", "Wolf Cull", "The System counted a wolf.", false, false, -1)};
        const TitleSenseMatch match = Hear(specific, facts);
        Check(!match.Spoken && match.Line.empty(), "a title that is neither owned nor worn is silence");
    }
    {
        const std::vector<TitleFact> facts{tomb, bear};
        const TitleSenseMatch match = Hear(specific, facts);
        Check(match.Spoken && match.TitleId == "tombwalker" &&
                  match.Line == "Tombwalker / The System counted three humanoids.",
              "a worn title with no bespoke line fills the fallback from the earliest slot");
        Check(match.Line.find("tombwalker") == std::string::npos &&
                  match.Line.find("bear_cull") == std::string::npos,
              "the fallback does not print a raw id");
    }
    {
        TitleSense anyFirst;
        anyFirst.Lines.push_back(SenseLine(TitleSenseWhen::AnyEquipped, nullptr, "any {title.name}"));
        anyFirst.Lines.push_back(SenseLine(TitleSenseWhen::Equipped, "wolf_cull", "the wolf line"));
        const TitleFact wolfLate =
            MakeFact("wolf_cull", "Wolf Cull", "The System counted a wolf.", true, true, 1);
        const std::vector<TitleFact> facts{tomb, wolfLate};
        const TitleSenseMatch match = Hear(anyFirst, facts);
        Check(match.Spoken && match.Line == "any Tombwalker" && match.TitleId == "tombwalker",
              "any worn title matches the earliest slot, ahead of a later line");
    }
    {
        const TitleFact unslotted =
            MakeFact("wolf_cull", "Wolf Cull", "The System counted a wolf.", true, true, -1);
        const std::vector<TitleFact> facts{unslotted};
        const TitleSenseMatch match = Hear(specific, facts);
        Check(!match.Spoken, "a fact with no slot is not a worn title");
    }
    {
        TitleSense unknown;
        unknown.Lines.push_back(SenseLine(TitleSenseWhen::Equipped, "no_such_title", "{title.name}"));
        const std::vector<TitleFact> facts{wolf};
        const TitleSenseMatch match = Hear(unknown, facts);
        Check(!match.Spoken && match.Line.empty(), "an unknown id is not a fact");
        Check(match.Line.find("no_such_title") == std::string::npos, "an unknown id is not spoken");
    }
    {
        TitleSense lost;
        lost.Lines.push_back(SenseLine(TitleSenseWhen::Fallback, nullptr, "{title.name}"));
        const std::vector<TitleFact> facts{unnamed};
        const TitleSenseMatch match = Hear(lost, facts);
        Check(!match.Spoken && match.Line.find("gone_title") == std::string::npos,
              "an empty name is not replaced with the id");
    }
    {
        TitleSense marked;
        marked.Lines.push_back(SenseLine(TitleSenseWhen::Fallback, nullptr, "A mark remains."));
        const std::vector<TitleFact> facts{unnamed};
        const TitleSenseMatch match = Hear(marked, facts);
        Check(match.Spoken && match.Line == "A mark remains." &&
                  match.Line.find("gone_title") == std::string::npos && match.TitleId == "gone_title",
              "a bespoke fallback can speak without inventing a name");
    }
    {
        TitleSense broken;
        broken.Lines.push_back(SenseLine(TitleSenseWhen::Equipped, "wolf_cull", "{title.rank}"));
        broken.Lines.push_back(SenseLine(TitleSenseWhen::Fallback, nullptr, "{title.name}."));
        const std::vector<TitleFact> facts{wolf};
        const TitleSenseMatch match = Hear(broken, facts);
        Check(match.Spoken && match.Line == "Wolf Cull.",
              "a placeholder that is not a fact field falls through");
        Check(match.Line.find("{") == std::string::npos, "an unknown placeholder is not shown");
    }
    {
        TitleSense twice;
        twice.Lines.push_back(
            SenseLine(TitleSenseWhen::Equipped, "wolf_cull", "{title.name} and {title.name}"));
        const std::vector<TitleFact> facts{wolf};
        const TitleSenseMatch match = Hear(twice, facts);
        Check(match.Spoken && match.Line == "Wolf Cull and Wolf Cull",
              "each title token is replaced from the fact");
    }
    {
        TitleSense bare;
        bare.Lines.push_back(SenseLine(TitleSenseWhen::Fallback, nullptr, "Nothing is written."));
        const TitleSenseMatch match = Hear(bare, {});
        Check(match.Spoken && match.Line == "Nothing is written." && match.TitleId.empty(),
              "a fallback with no title token is the else");
    }
}

void TestTitleSenseFiles()
{
    const char* good =
        "{\n"
        "  \"dialogue\": {\n"
        "    \"id\": \"sense\",\n"
        "    \"titleSense\": {\n"
        "      \"lines\": [\n"
        "        { \"titleEquipped\": \"wolf_cull\", \"line\": \"Wolf.\" },\n"
        "        { \"titleOwned\": \"beast_hunter\", \"line\": \"Kept {title.name}.\" },\n"
        "        { \"anyTitleEquipped\": true, \"line\": \"Any {title.name}.\" },\n"
        "        { \"fallback\": true, \"line\": \"{title.name} / {title.description}\" }\n"
        "      ]\n"
        "    },\n"
        "    \"nodes\": [\n"
        "      { \"id\": \"hi\", \"line\": \"Hello.\", \"choices\": [] }\n"
        "    ]\n"
        "  }\n"
        "}\n";
    {
        TempCopy copy;
        WriteFile(copy.Path / "dialogue" / "sense.json", good);
        Database data(copy.Path);
        DataError error;
        Check(data.Load(&error), "a titleSense block loads");
        if (!error.Message.empty())
            std::printf("    %s\n", error.Message.c_str());
        const DialogueDef* talk = data.FindDialogue("sense");
        Check(talk != nullptr && talk->Sense.Lines.size() == 4, "the four sense lines are kept");
        if (talk != nullptr && talk->Sense.Lines.size() == 4) {
            Check(talk->Sense.Lines[0].When == TitleSenseWhen::Equipped &&
                      talk->Sense.Lines[0].TitleId == "wolf_cull",
                  "titleEquipped keeps the title id");
            Check(talk->Sense.Lines[1].When == TitleSenseWhen::Owned &&
                      talk->Sense.Lines[1].Line == "Kept {title.name}.",
                  "titleOwned stays in the file until a talk resolves it");
            Check(talk->Sense.Lines[2].When == TitleSenseWhen::AnyEquipped &&
                      talk->Sense.Lines[2].TitleId.empty(),
                  "anyTitleEquipped does not name one title");
            Check(talk->Sense.Lines[3].When == TitleSenseWhen::Fallback &&
                      talk->Sense.Lines[3].Line.find("{title.description}") != std::string::npos,
                  "the fallback template is stored as written");
            World world;
            Check(Enter(*talk, world) && world.Node == "hi",
                  "title sense does not change the opening node");
        }
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "dialogue" / "sense.json", good);
        ReplaceInFile(copy.Path / "dialogue" / "sense.json", "wolf_cull", "no_such_title");
        Reject(copy.Path, "dialogue/sense.json", "no_such_title", "an unknown title id is rejected");
        Reject(copy.Path, "dialogue/sense.json", "does not exist", "the error says the title is missing");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "dialogue" / "sense.json", good);
        ReplaceInFile(copy.Path / "dialogue" / "sense.json", "{title.name}", "{title.rank}");
        Reject(copy.Path, "dialogue/sense.json", "{title.rank}", "an unknown title placeholder is rejected");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "dialogue" / "sense.json", good);
        ReplaceInFile(copy.Path / "dialogue" / "sense.json", "\"line\": \"Wolf.\"",
                      "\"line\": \"Wolf.\", \"mood\": \"dry\"");
        Reject(copy.Path, "dialogue/sense.json", "mood", "a sense line rejects an unknown field");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "dialogue" / "sense.json", good);
        ReplaceInFile(copy.Path / "dialogue" / "sense.json",
                      "{ \"titleEquipped\": \"wolf_cull\", \"line\": \"Wolf.\" }",
                      "{ \"titleEquipped\": \"wolf_cull\", \"fallback\": true, \"line\": \"Wolf.\" }");
        Reject(copy.Path, "dialogue/sense.json", "needs one of", "a sense line has one condition");
    }
    {
        TempCopy copy;
        WriteFile(copy.Path / "dialogue" / "sense.json", good);
        ReplaceInFile(copy.Path / "dialogue" / "sense.json",
                      "{ \"titleEquipped\": \"wolf_cull\", \"line\": \"Wolf.\" }",
                      "{ \"fallback\": true, \"line\": \"Else.\" },\n        { \"titleEquipped\": \"wolf_cull\", \"line\": \"Wolf.\" }");
        Reject(copy.Path, "dialogue/sense.json", "fallback must be the last line",
               "a line after the fallback is rejected");
    }
}

bool HasSense(const DialogueDef& talk, const char* titleId)
{
    for (const TitleSenseLine& line : talk.Sense.Lines) {
        if (line.When == TitleSenseWhen::Equipped && line.TitleId == titleId && !line.Line.empty())
            return true;
    }
    return false;
}

void ExpectVillageSense(const DialogueDef* talk, const char* who)
{
    Check(talk != nullptr && talk->Sense.Lines.size() >= 8, who);
    if (talk == nullptr || talk->Sense.Lines.size() < 8)
        return;
    Check(talk->Sense.Lines[0].When == TitleSenseWhen::Equipped &&
              talk->Sense.Lines[0].TitleId == "wolf_cull",
          who);
    Check(talk->Sense.Lines[1].When == TitleSenseWhen::Equipped &&
              talk->Sense.Lines[1].TitleId == "beast_hunter",
          who);
    Check(HasSense(*talk, "crusher") && HasSense(*talk, "warden") && HasSense(*talk, "mender") &&
              HasSense(*talk, "unscathed") && HasSense(*talk, "runner"),
          who);
    Check(talk->Sense.Lines.back().When == TitleSenseWhen::AnyEquipped &&
              talk->Sense.Lines.back().Line.find("{title.") == std::string::npos,
          who);
}

std::string Folded(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const unsigned char c : text)
        out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

void TestSenseDoesNotNameTitles()
{
    Database data(kShipped);
    DataError error;
    Check(data.Load(&error), "shipped data loads for the name check");
    if (!data.IsLoaded())
        return;
    for (const DialogueDef& talk : data.GetDialogues()) {
        for (const TitleSenseLine& line : talk.Sense.Lines) {
            const std::string folded = Folded(line.Line);
            std::string hit;
            if (folded.find("{title.name}") != std::string::npos ||
                folded.find("{title.description}") != std::string::npos)
                hit = "a title token";
            for (const TitleDef& title : data.GetTitles()) {
                if (hit.empty() && !title.Name.empty() &&
                    folded.find(Folded(title.Name)) != std::string::npos)
                    hit = title.Name;
                if (hit.empty() && !title.Description.empty() &&
                    folded.find(Folded(title.Description)) != std::string::npos)
                    hit = title.Description;
            }
            Check(hit.empty(), "a shipped remark does not say a title's name");
            if (!hit.empty())
                std::printf("    %s: %s\n", talk.Id.c_str(), hit.c_str());
        }
    }
}

void TestVillageSense()
{
    Database data(kShipped);
    DataError error;
    Check(data.Load(&error), "shipped data loads title sense");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    if (!data.IsLoaded())
        return;
    const DialogueDef* pell = data.FindDialogue("pell");
    const DialogueDef* bram = data.FindDialogue("bram");
    const DialogueDef* calder = data.FindDialogue("calder");
    ExpectVillageSense(pell, "Pell senses a worn title");
    ExpectVillageSense(bram, "Bram senses a worn title");
    ExpectVillageSense(calder, "Calder senses a worn title");
    Check(pell != nullptr && !pell->Sense.Lines.empty() &&
              pell->Sense.Lines.back().When == TitleSenseWhen::AnyEquipped &&
              pell->Sense.Lines.back().Line.find("loud in my shop") != std::string::npos,
          "Pell's closing line hints and does not read the title");

    SystemState none;
    TitleFact missing;
    Check(!FindTitleFact(data, none, "no_such_title", missing),
          "the query does not invent a fact for an unknown id");
    TitleSense unknown;
    unknown.Lines.push_back(SenseLine(TitleSenseWhen::Equipped, "no_such_title", "{title.name}"));
    const std::vector<TitleFact> catalog = QueryTitles(data, none);
    const TitleSenseMatch quiet = Hear(unknown, catalog);
    Check(!quiet.Spoken && quiet.Line.find("no_such_title") == std::string::npos,
          "a queried list does not speak an unknown id");

    SystemState worn;
    worn.Titles.push_back("tombwalker");
    worn.Equipped.push_back("tombwalker");
    TitleFact tomb;
    Check(FindTitleFact(data, worn, "tombwalker", tomb) && tomb.Equipped && tomb.Name == "Tombwalker" &&
              !tomb.Description.empty(),
          "FindTitleFact returns the worn catalog title");
    const std::vector<TitleFact> facts = QueryTitles(data, worn);
    Check(pell != nullptr, "Pell is loaded");
    if (pell == nullptr)
        return;
    const TitleSenseMatch other = Hear(pell->Sense, facts);
    Check(other.Spoken && other.TitleId == tomb.Id && other.Line.find(tomb.Name) == std::string::npos &&
              other.Line.find(tomb.Description) == std::string::npos &&
              other.Line == pell->Sense.Lines.back().Line,
          "another worn title gets Pell's hint, not the title's words");
    Check(other.Line.find("tombwalker") == std::string::npos, "Pell does not speak the raw id");

    Hero hero;
    std::string why;
    Check(hero.Start(data, why), "a hero can be asked about titles");
    if (!why.empty())
        std::printf("    %s\n", why.c_str());
    if (!hero.IsReady())
        return;
    for (i32 n = 0; n < 4; ++n)
        hero.NoteKill("wolf");
    Check(hero.AcceptTitle("wolf_cull", why), "Wolf Cull can be owned");
    const TitleSenseMatch held = Hear(pell->Sense, hero.QueryTitles());
    TitleFact wolf;
    Check(hero.TitleFactFor("wolf_cull", wolf) && wolf.Owned && !wolf.Equipped && wolf.Name == "Wolf Cull",
          "the owned title is a fact and is not worn yet");
    Check(!held.Spoken, "Pell does not remark on a title that is only owned");
    Check(!hero.TitleFactFor("no_such_title", wolf), "TitleFactFor refuses an unknown id");
    Check(hero.TitleSlots() >= 1 && hero.EquipTitle("wolf_cull", why), "Wolf Cull can be worn");
    const TitleSenseMatch cull = Hear(pell->Sense, hero.QueryTitles());
    Check(cull.Spoken && cull.TitleId == "wolf_cull" && cull.Line == pell->Sense.Lines[0].Line,
          "the worn Wolf Cull line is the one in Pell's file");
    Check(hero.AcceptTitle("beast_hunter", why) && hero.UnequipTitle("wolf_cull", why) &&
              hero.EquipTitle("beast_hunter", why),
          "Beast Hunter can take the slot");
    const TitleSenseMatch hunter = Hear(bram->Sense, hero.QueryTitles());
    Check(bram != nullptr && hunter.Spoken && hunter.TitleId == "beast_hunter" &&
              hunter.Line == bram->Sense.Lines[1].Line,
          "Bram uses the Beast Hunter line from his file");
    for (i32 n = 0; n < 6; ++n)
        hero.NoteUse("heavy_blow");
    Check(hero.AcceptTitle("crusher", why) && hero.UnequipTitle("beast_hunter", why) &&
              hero.EquipTitle("crusher", why),
          "Crusher can take the slot");
    const TitleSenseMatch crusher = Hear(pell->Sense, hero.QueryTitles());
    Check(crusher.Spoken && crusher.TitleId == "crusher" &&
              crusher.Line.find("counter owes you gold") != std::string::npos &&
              crusher.Line.find("Crusher") == std::string::npos,
          "Pell hints at Crusher and does not say the name");
    if (calder != nullptr) {
        World world;
        Check(Enter(*calder, world) && world.Node == "calder_greet",
              "a worn title does not replace Calder's opening node");
    }
}

void TestSpeakers()
{
    Database data(kShipped);
    DataError error;
    Check(data.Load(&error), "shipped data loads speaker names");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    const DialogueDef* pell = data.FindDialogue("pell");
    const DialogueDef* bram = data.FindDialogue("bram");
    const DialogueDef* calder = data.FindDialogue("calder");
    Check(pell != nullptr && bram != nullptr && calder != nullptr, "pell, bram, and calder all load");
    if (pell == nullptr || bram == nullptr || calder == nullptr)
        return;

    const TalkSpeaker pellFace = TalkSpeakerOf(*pell);
    const TalkSpeaker bramFace = TalkSpeakerOf(*bram);
    const TalkSpeaker calderFace = TalkSpeakerOf(*calder);
    Check(pellFace.Label == "Pell Harrow" && pellFace.Label == pell->Name,
          "Pell's label is his own name");
    Check(pellFace.Portrait == "pell" && pellFace.Portrait == pell->Portrait,
          "Pell's portrait id is his own");
    Check(bramFace.Label == "Bram" && bramFace.Label != pellFace.Label &&
              bramFace.Label != data.GetShop().Keeper,
          "Bram's label is his own name");
    Check(bramFace.Portrait.empty(), "Bram has no portrait configured");
    Check(calderFace.Label == "Calder" && calderFace.Label != pellFace.Label &&
              calderFace.Label != bramFace.Label && calderFace.Label != data.GetShop().Keeper,
          "Calder's label is his own name");
    Check(calderFace.Portrait.empty(), "Calder has no portrait configured");
    Check(bramFace.Portrait != pellFace.Portrait && calderFace.Portrait != pellFace.Portrait,
          "Bram and Calder do not use Pell's portrait id");

    const fs::path dir = fs::temp_directory_path() / "ascension-dialogue-portraits";
    std::error_code removeError;
    fs::remove_all(dir, removeError);
    fs::create_directories(dir / "npcs");
    WritePng(dir / "npcs" / "pell_portrait.png");
    WritePng(dir / "npcs" / "bram_portrait.png");
    const fs::path pellFile = dir / "npcs" / "pell_portrait.png";
    const fs::path bramFile = dir / "npcs" / "bram_portrait.png";
    WriteFile(dir / "manifest.json",
              "{\n"
              "  \"npcs\": [\n"
              "    { \"id\": \"pell\", \"portrait\": \"npcs/pell_portrait.png\" },\n"
              "    { \"id\": \"bram\", \"portrait\": \"npcs/bram_portrait.png\" }\n"
              "  ]\n"
              "}\n");

    ArtPack both;
    DataError packError;
    Check(both.Load(dir, data, &packError), "a pack with two portraits loads");
    if (!packError.Message.empty())
        std::printf("    %s\n", packError.Message.c_str());
    const fs::path pellArt = ResolveTalkPortrait(pellFace, both);
    const fs::path bramArt = ResolveTalkPortrait(bramFace, both);
    const fs::path calderArt = ResolveTalkPortrait(calderFace, both);
    Check(SamePortrait(pellArt, pellFile), "Pell resolves his own portrait file");
    Check(bramArt.empty() && calderArt.empty(), "a speaker with no portrait resolves no file");
    Check(!SamePortrait(bramArt, pellFile) && !SamePortrait(calderArt, pellFile) &&
              !SamePortrait(bramArt, bramFile),
          "Bram and Calder never resolve Pell's portrait, or a picture they did not name");

    DialogueDef bramOwn = *bram;
    bramOwn.Portrait = "bram";
    const fs::path bramOwnArt = ResolveTalkPortrait(TalkSpeakerOf(bramOwn), both);
    Check(SamePortrait(bramOwnArt, bramFile) && !SamePortrait(bramOwnArt, pellFile),
          "a speaker who names a portrait resolves that file");

    WriteFile(dir / "manifest.json",
              "{\n"
              "  \"npcs\": [ { \"id\": \"pell\", \"portrait\": \"npcs/pell_portrait.png\" } ]\n"
              "}\n");
    ArtPack pellOnly;
    packError = {};
    Check(pellOnly.Load(dir, data, &packError), "a pack with only Pell's portrait loads");
    if (!packError.Message.empty())
        std::printf("    %s\n", packError.Message.c_str());
    const fs::path onlyPell = ResolveTalkPortrait(pellFace, pellOnly);
    const fs::path bramMissing = ResolveTalkPortrait(TalkSpeakerOf(bramOwn), pellOnly);
    const fs::path bramNone = ResolveTalkPortrait(bramFace, pellOnly);
    const fs::path calderNone = ResolveTalkPortrait(calderFace, pellOnly);
    Check(SamePortrait(onlyPell, pellFile), "Pell still resolves when he is the only portrait");
    Check(bramMissing.empty() && bramNone.empty() && calderNone.empty(),
          "a missing portrait is omitted instead of Pell's");
    Check(!SamePortrait(bramMissing, pellFile) && !SamePortrait(bramNone, pellFile) &&
              !SamePortrait(calderNone, pellFile),
          "no other speaker resolves Pell's portrait file");
    fs::remove_all(dir, removeError);
}

void TestTalkBox()
{
    const f32 pad = 22.0f;
    const f32 lineGap = 12.0f;
    const f32 rowGap = 12.0f;
    const f32 lineH = 18.0f;
    const f32 portrait = 96.0f;

    const TalkBox bare = PlaceTalkBox(pad, lineGap, rowGap, lineH, 0.0f, false, portrait, lineH);
    Check(bare.Portrait == 0.0f && bare.ContentX == pad && bare.TextX == pad,
          "a speaker with no portrait keeps the line at the panel pad");
    Check(bare.SlotY == pad && bare.LabelY == pad,
          "a flat panel puts the label on the same pad the shop header uses");
    Check(bare.BodyY == pad + lineH + lineGap, "the body follows the label by one line and the gap");

    const TalkBox faced = PlaceTalkBox(pad, lineGap, rowGap, lineH, 0.0f, true, portrait, lineH);
    Check(faced.ContentX == pad && faced.Portrait == portrait &&
              faced.TextX == pad + portrait + rowGap,
          "a portrait shifts the line by the box and the row gap");
    Check(faced.LabelY == pad, "a portrait does not move the label down");

    const f32 frame = 20.0f;
    const TalkBox boxed = PlaceTalkBox(pad, lineGap, rowGap, lineH, frame, false, portrait, lineH);
    Check(boxed.TextX == frame + pad && boxed.LabelY == frame + pad && boxed.SlotY == frame + pad,
          "type is one panel pad inside the frame");
    Check(boxed.TextX - frame == pad && boxed.LabelY - frame == pad,
          "the pad stays open panel, past the border");
    Check(boxed.TextX > bare.TextX && boxed.LabelY > bare.LabelY,
          "a skinned frame pushes the line off the border");
    Check(boxed.BodyY == boxed.LabelY + lineH + lineGap, "the body stays one line under the label");

    const TalkBox boxedFace =
        PlaceTalkBox(pad, lineGap, rowGap, lineH, frame, true, portrait, lineH);
    Check(boxedFace.ContentX == frame + pad &&
              boxedFace.TextX == boxedFace.ContentX + portrait + rowGap,
          "a portrait beside a frame still starts inside the border");
    Check(boxedFace.TextX != boxed.TextX, "the portrait column is not the text column");

    const f32 fontH = 8.0f;
    const TalkBox shortFont =
        PlaceTalkBox(pad, lineGap, rowGap, lineH, frame, false, portrait, fontH);
    const f32 drop = (lineH - fontH) * 0.5f;
    Check(shortFont.LabelY == frame + pad + drop && shortFont.SlotY == frame + pad,
          "a shorter font is centered in the line");
    Check(shortFont.LabelY > frame && shortFont.BodyY == shortFont.SlotY + lineH + lineGap,
          "the label stays off the top border and the body keeps the line grid");

    const TalkBox messy = PlaceTalkBox(-4.0f, -1.0f, -2.0f, 10.0f, -3.0f, true, -8.0f, 40.0f);
    Check(messy.ContentX == 0.0f && messy.TextX == 0.0f && messy.Portrait == 0.0f &&
              messy.LabelY == 0.0f && messy.BodyY == 10.0f,
          "a bad size does not push the box off the panel");
}

void TestShipped()
{
    Database data(kShipped);
    DataError error;
    Check(data.Load(&error), "shipped data loads the talk");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    const DialogueDef* pell = data.FindDialogue("pell");
    const DialogueNode* greet = pell != nullptr ? pell->FindNode("pell_greet_a") : nullptr;
    Check(greet != nullptr && greet->Line.find("Knee says rain") != std::string::npos,
          "the shipped greeting is the approved line");
}

} // namespace

int main()
{
    Database data(kSample);
    DataError error;
    Check(data.Load(&error), "sample data loads with dialogue");
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    TestLines(data);
    TestWrap();
    TestGreetA(data);
    TestGreetB(data);
    TestSets();
    TestValidation();
    TestTitleConditions();
    TestTitleSenseFiles();
    TestVillageSense();
    TestSenseDoesNotNameTitles();
    TestSpeakers();
    TestTalkBox();
    TestShipped();
    TestCalder();
    TestBram();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
