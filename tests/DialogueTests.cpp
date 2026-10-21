// Dialogue: load, flag syntax, the Fang gate, and Pell's approved lines. No window or GPU.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/Database.h"
#include "Ascension/Data/Talk.h"
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
    std::string Node;
    TalkList Lists[1];

    [[nodiscard]] TalkFacts Facts()
    {
        Lists[0].Name = "found";
        Lists[0].Ids = Found;
        TalkFacts facts;
        facts.Flags = Flags;
        facts.Lists = std::span<const TalkList>(Lists, 1);
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
    TestShipped();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
