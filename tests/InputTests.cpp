// Bindings in input.json and the prompt glyph map. No window or GPU.

#include <filesystem>
#include <fstream>
#include <string>

#include "Ascension/Data/Database.h"
#include "Ascension/Input/Controls.h"
#include "Ascension/Input/Prompts.h"
#include "Check.h"

#include "Emerald/Input/Gamepads.h"
#include "Emerald/Input/Input.h"
#include "Emerald/Input/Keyboard.h"
#include "Emerald/Renderer/DebugText.h"

namespace {

using namespace Ascension;
namespace fs = std::filesystem;

const fs::path kSample = ASCENSION_DATA_DIR;
const fs::path kShipped = ASCENSION_SHIPPED_DATA_DIR;

struct TempCopy {
    fs::path Path;

    TempCopy()
    {
        Path = fs::temp_directory_path() / "ascension-input-tests";
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

void ReplaceIn(const fs::path& path, const std::string& from, const std::string& to)
{
    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const usize at = text.find(from);
    Check(at != std::string::npos, "the sample contains the text being replaced");
    if (at == std::string::npos)
        return;
    text.replace(at, from.size(), to);
    std::ofstream out(path);
    out << text;
}

bool HasAction(const std::vector<std::string_view>& actions, std::string_view name)
{
    for (const std::string_view action : actions) {
        if (action == name)
            return true;
    }
    return false;
}

void TestShippedControls(const Database& data)
{
    const ControlRules& rules = data.GetControls();
    Check(rules.NavInitialMilliseconds == 280, "nav initial pause is 280ms");
    Check(rules.NavRepeatMilliseconds == 120, "nav repeat is 120ms");
    Check(rules.DebugConfirmMilliseconds == 1500, "new game confirm window is 1500ms");
    Check(rules.DebugRespawned == "Foes returned." && rules.DebugRespawnBusy == "Finish the fight." &&
              rules.DebugNewGameArmed == "Press again to start a new game." &&
              rules.DebugNewGame == "New game.",
          "debug notes come from input.json");
    Check(rules.PauseTitle == "Paused" && rules.PauseContinue == "Continue" &&
              rules.PauseQuit == "Quit game",
          "pause menu lines come from input.json");
    Check(rules.SaveSlotCount == 4, "the title offers four numbered save slots");
    Check(rules.TitleWordmark == "Ascension" && rules.TitleTagline == "A name, a class, and the road." &&
              rules.TitleContinue == "Continue" && rules.TitleNewGame == "New game" &&
              rules.TitleQuit == "Quit" && rules.TitleOverwrite == "Overwrite this save?" &&
              rules.TitleOverwriteYes == "Overwrite" && rules.TitleBack == "Back" &&
              rules.TitleSlot == "Slot" && rules.TitleSlotEmpty == "Empty" &&
              rules.TitleSlotDamaged == "Damaged" && rules.TitleLegacySlot == "Saved run" &&
              rules.TitleChooseSlot == "Choose a slot",
          "title lines come from input.json");
    Check(rules.CreateName == "Name" && rules.CreateClass == "Class" && rules.CreateBegin == "Begin" &&
              rules.CreateNameEmpty == "Enter a name." && rules.CreateNameLong == "That name is too long." &&
              rules.CreateNameBad == "That name cannot be used.",
          "create lines come from input.json");
    Check(rules.Actions.size() == kControlActions.size(), "every control action is listed once");
    for (const std::string_view action : kControlActions)
        Check(FindControl(rules, action) != nullptr, "a required action is present");
    const ControlBind* respawn = FindControl(rules, kDebugRespawnFoes);
    const ControlBind* restart = FindControl(rules, kDebugNewGame);
    Check(respawn != nullptr && respawn->Keys.size() == 1 && respawn->Keys[0] == "F6" &&
              respawn->Buttons.empty(),
          "respawn is F6 and has no pad button");
    Check(restart != nullptr && restart->Keys.size() == 1 && restart->Keys[0] == "F9" &&
              restart->Buttons.empty(),
          "new game is F9 and has no pad button");
    const ControlBind* ascii = FindControl(rules, kAscii);
    Check(ascii != nullptr && ascii->Keys.size() == 1 && ascii->Keys[0] == "F8" &&
              ascii->Buttons.empty(),
          "ascii is F8 and has no pad button");
    const ControlBind* pause = FindControl(rules, kPause);
    const ControlBind* quit = FindControl(rules, kQuit);
    Check(pause != nullptr && pause->Keys.size() == 2 && pause->Keys[0] == "P" &&
              pause->Keys[1] == "Escape" && pause->Buttons.size() == 1 &&
              pause->Buttons[0] == "Start",
          "pause is P, Escape, and Start");
    Check(quit != nullptr && quit->Keys.empty() && quit->Buttons.empty(),
          "quit has no key or button");

    const PromptRules& prompts = data.GetPrompts();
    Check(prompts.IconScale == 1 && prompts.TextScale == 2, "prompt scales come from prompts.json");
    Check(prompts.TextFamily == "keyboard", "keyboard is the text family");
    Check(prompts.DefaultFamily == "xbox", "an unnamed pad uses the xbox sheet");
    Check(prompts.Sheets.size() == 3, "xbox, playstation, and switch sheets");
    for (const PromptSheet& sheet : prompts.Sheets) {
        Check(sheet.TilePixels == 16, "prompt tiles are 16 pixels");
        Check(sheet.Tiles.size() == 18, "each sheet has 18 tiles");
    }
    const PromptMetrics metrics = MeasurePrompts(prompts);
    const f32 cap = static_cast<f32>(Emerald::kDebugTextRows * prompts.TextScale);
    Check(metrics.Icon == 16.0f && metrics.Cap == cap, "the icon box and the cap use the data scales");
    Check(metrics.TextOffset == (metrics.Icon - metrics.Cap) * 0.5f,
          "hint text is centered on the icon");
    Check(PromptFamilyForType(prompts, "Xbox One/Series") == "xbox", "xbox one uses the xbox sheet");
    Check(PromptFamilyForType(prompts, "PS5") == "playstation", "ps5 uses the playstation sheet");
    Check(PromptFamilyForType(prompts, "Switch Pro") == "switch", "switch pro uses the switch sheet");
    Check(PromptFamilyForType(prompts, "Joy-Con pair") == "switch", "joy-cons use the switch sheet");
    Check(PromptFamilyForType(prompts, "GameCube") == "switch", "gamecube uses the switch sheet");
    Check(PromptFamilyForType(prompts, "Standard") == "xbox", "a standard pad uses the xbox sheet");
    Check(PromptFamilyForType(prompts, "Unknown") == "xbox", "an unknown pad uses the xbox sheet");
    Check(PromptFamilyForType(prompts, "Not A Pad") == "xbox", "an unlisted type uses the xbox sheet");
}

void TestMapping()
{
    Database data(kSample);
    DataError error;
    Check(data.Load(&error), "the sample input loads");
    if (!data.IsLoaded())
        return;

    Emerald::Keyboard keyboard;
    Emerald::Gamepads pads;
    pads.AddSyntheticPad(1, "Test Pad", Emerald::GamepadType::XboxOne);
    Emerald::Input input(keyboard, pads);
    std::string why;
    Check(ApplyControls(input, data.GetControls(), why), "bindings apply");
    if (!why.empty())
        std::printf("    apply: %s\n", why.c_str());

    keyboard.BeginFrame();
    pads.BeginFrame();
    keyboard.OnKeyDown(Emerald::Key::E);
    Check(input.WasActionPressed(kConfirm), "E confirms");
    Check(!input.WasActionPressed(kMoveWest), "E is not a step");

    keyboard.BeginFrame();
    pads.BeginFrame();
    keyboard.OnKeyDown(Emerald::Key::A);
    Check(input.WasActionPressed(kMoveWest), "A steps west");
    Check(!input.WasActionPressed(kConfirm), "A does not confirm");

    keyboard.OnKeyUp(Emerald::Key::E);
    keyboard.OnKeyUp(Emerald::Key::A);
    keyboard.BeginFrame();
    pads.BeginFrame();
    pads.OnButton(1, Emerald::GamepadButton::South, true);
    Check(input.WasActionPressed(kConfirm), "south confirms");
    pads.OnButton(1, Emerald::GamepadButton::South, false);
    pads.BeginFrame();
    pads.OnButton(1, Emerald::GamepadButton::East, true);
    Check(input.WasActionPressed(kCancel) && !input.WasActionPressed(kConfirm), "east cancels");
    pads.OnButton(1, Emerald::GamepadButton::East, false);
    pads.BeginFrame();
    pads.OnButton(1, Emerald::GamepadButton::West, true);
    Check(input.WasActionPressed(kMenu), "west opens the menu");
    pads.OnButton(1, Emerald::GamepadButton::West, false);
    pads.BeginFrame();
    pads.OnButton(1, Emerald::GamepadButton::North, true);
    Check(input.WasActionPressed(kContext), "north is context");
    pads.OnButton(1, Emerald::GamepadButton::North, false);
    pads.BeginFrame();
    pads.OnButton(1, Emerald::GamepadButton::Start, true);
    Check(input.WasActionPressed(kPause) && !input.WasActionPressed(kQuit), "start pauses");
    pads.OnButton(1, Emerald::GamepadButton::Start, false);
    pads.BeginFrame();
    keyboard.BeginFrame();
    keyboard.OnKeyDown(Emerald::Key::Escape);
    Check(input.WasActionPressed(kPause) && !input.WasActionPressed(kQuit),
          "escape is pause, not quit");
    keyboard.OnKeyUp(Emerald::Key::Escape);
    keyboard.BeginFrame();
    pads.BeginFrame();
    pads.OnButton(1, Emerald::GamepadButton::Back, true);
    Check(input.WasActionPressed(kHelp), "back toggles help");
    pads.OnButton(1, Emerald::GamepadButton::Back, false);
    pads.BeginFrame();
    pads.OnButton(1, Emerald::GamepadButton::LeftShoulder, true);
    Check(input.WasActionPressed(kCyclePrev), "lb cycles back");
    pads.OnButton(1, Emerald::GamepadButton::LeftShoulder, false);
    pads.BeginFrame();
    pads.OnButton(1, Emerald::GamepadButton::RightShoulder, true);
    Check(input.WasActionPressed(kCycleNext), "rb cycles forward");
    pads.OnButton(1, Emerald::GamepadButton::RightShoulder, false);
    pads.BeginFrame();
    pads.OnButton(1, Emerald::GamepadButton::DPadUp, true);
    Check(input.WasActionPressed(kMoveNorth), "d-pad up steps north");
    pads.OnButton(1, Emerald::GamepadButton::DPadUp, false);

    pads.BeginFrame();
    pads.OnAxis(1, Emerald::GamepadAxis::LeftY, -1.0f);
    Check(input.WasActionPressed(kMoveNorth), "the left stick steps north");

    const PromptRules& prompts = data.GetPrompts();
    const PromptMark xbox = ResolvePrompt(input, prompts, kConfirm, "xbox");
    Check(xbox.Icon && xbox.Tile == "a", "confirm on xbox is the A glyph");
    const PromptMark ps = ResolvePrompt(input, prompts, kConfirm, "playstation");
    Check(ps.Icon && ps.Tile == "cross", "confirm on playstation is cross");
    const PromptMark sw = ResolvePrompt(input, prompts, kConfirm, "switch");
    Check(sw.Icon && sw.Tile == "b", "confirm on switch is the B glyph");
    const PromptMark keys = ResolvePrompt(input, prompts, kConfirm, prompts.TextFamily);
    Check(!keys.Icon && !keys.Label.empty(), "keyboard confirm is a text label");
    const PromptMark guide = ResolvePrompt(input, prompts, kPause, "xbox");
    Check(guide.Icon && guide.Tile == "menu", "start is the menu glyph");

    Check(BoundKeyNeedsPlayerTurn(input, Emerald::Key::Num2),
          "a skill key needs the player's turn");
    Check(BoundKeyNeedsPlayerTurn(input, Emerald::Key::Right),
          "aiming needs the player's turn");
    Check(BoundKeyNeedsPlayerTurn(input, Emerald::Key::Space),
          "waiting needs the player's turn");
    Check(!BoundKeyNeedsPlayerTurn(input, Emerald::Key::N),
          "N is not a combat key");
    Check(!BoundKeyNeedsPlayerTurn(input, Emerald::Key::R),
          "restart is not a combat key");
    Check(!BoundKeyNeedsPlayerTurn(input, Emerald::Key::E),
          "confirm is not a combat key");
    Check(!BoundKeyNeedsPlayerTurn(input, Emerald::Key::F6),
          "respawn is not a combat key");
    Check(!BoundKeyNeedsPlayerTurn(input, Emerald::Key::F9),
          "new game is not a combat key");
    Check(!BoundKeyNeedsPlayerTurn(input, Emerald::Key::F8),
          "ascii is not a combat key");

    keyboard.BeginFrame();
    pads.BeginFrame();
    keyboard.OnKeyDown(Emerald::Key::F6);
    Check(input.WasActionPressed(kDebugRespawnFoes), "F6 is respawn");
    Check(!input.WasActionPressed(kSave), "F6 is not save");
    keyboard.OnKeyUp(Emerald::Key::F6);
    keyboard.BeginFrame();
    pads.BeginFrame();
    keyboard.OnKeyDown(Emerald::Key::F9);
    Check(input.WasActionPressed(kDebugNewGame) && !input.WasActionPressed(kDebugRespawnFoes),
          "F9 is new game");
    keyboard.OnKeyUp(Emerald::Key::F9);
    keyboard.BeginFrame();
    pads.BeginFrame();
    keyboard.OnKeyDown(Emerald::Key::F8);
    Check(input.WasActionPressed(kAscii) && !input.WasActionPressed(kDebugNewGame),
          "F8 is the ascii toggle");
    keyboard.OnKeyUp(Emerald::Key::F8);
    pads.BeginFrame();
    keyboard.BeginFrame();
    pads.OnButton(1, Emerald::GamepadButton::South, true);
    Check(!input.WasActionPressed(kDebugRespawnFoes) && !input.WasActionPressed(kDebugNewGame),
          "a face button does not respawn or start over");
    pads.OnButton(1, Emerald::GamepadButton::South, false);

    const std::vector<std::string_view> up =
        input.FindActionsUsing(Emerald::Key::Up);
    const std::vector<std::string_view> enter =
        input.FindActionsUsing(Emerald::Key::Enter);
    const std::vector<std::string_view> south =
        input.FindActionsUsing(Emerald::GamepadButton::South);
    const std::vector<std::string_view> east =
        input.FindActionsUsing(Emerald::GamepadButton::East);
    const std::vector<std::string_view> dpad =
        input.FindActionsUsing(Emerald::GamepadButton::DPadUp);
    Check(HasAction(up, kMoveNorth), "up moves the talk menu");
    Check(HasAction(enter, kConfirm), "enter speaks");
    Check(HasAction(south, kConfirm), "south speaks");
    Check(HasAction(dpad, kMoveNorth), "the d-pad moves the menu");
    Check(HasAction(east, kCancel) && !HasAction(east, kConfirm), "east is cancel, not confirm");

    Emerald::Keyboard freshKeys;
    Emerald::Gamepads freshPads;
    Emerald::Input fresh(freshKeys, freshPads);
    ControlRules broken = data.GetControls();
    for (ControlBind& bind : broken.Actions) {
        if (bind.Name == "Confirm")
            bind.Buttons = {"Soth"};
    }
    std::string failed;
    Check(!ApplyControls(fresh, broken, failed), "an unknown button is refused");
    Check(failed.find("Soth") != std::string::npos, "the error names the button");
    freshPads.AddSyntheticPad(1, "Test Pad", Emerald::GamepadType::XboxOne);
    freshPads.BeginFrame();
    freshPads.OnButton(1, Emerald::GamepadButton::South, true);
    Check(!fresh.WasActionPressed(kConfirm), "a failed apply leaves the actions unbound");
}

void TestPauseResume()
{
    Database data(kSample);
    DataError error;
    Check(data.Load(&error), "pause test loads input");
    if (!data.IsLoaded())
        return;

    Emerald::Keyboard keyboard;
    Emerald::Gamepads pads;
    pads.AddSyntheticPad(1, "Test Pad", Emerald::GamepadType::XboxOne);
    Emerald::Input input(keyboard, pads);
    std::string why;
    Check(ApplyControls(input, data.GetControls(), why), "pause bindings apply");

    Check(ReadActionEdges(false, true), "a paused frame still samples action edges");
    Check(ReadActionEdges(false, false), "play samples action edges");
    Check(!ReadActionEdges(true, true), "a script does not also sample the window");

    PauseMenu menu;
    keyboard.BeginFrame();
    pads.BeginFrame();
    keyboard.OnKeyDown(Emerald::Key::P);
    Check(input.WasActionPressed(kPause), "P is the pause action");
    Check(StepPauseMenu(input, menu, false) == PauseStep::Opened && menu.Open && menu.Row == 0,
          "P opens the menu on Continue");

    keyboard.BeginFrame();
    pads.BeginFrame();
    Check(!input.WasActionPressed(kPause), "a held P is not a second edge");
    Check(StepPauseMenu(input, menu, false) == PauseStep::None && menu.Open,
          "holding P stays on the menu");

    keyboard.OnKeyUp(Emerald::Key::P);
    keyboard.BeginFrame();
    pads.BeginFrame();
    keyboard.OnKeyDown(Emerald::Key::P);
    Check(StepPauseMenu(input, menu, false) == PauseStep::Resumed && !menu.Open,
          "P resumes and does not quit");

    keyboard.OnKeyUp(Emerald::Key::P);
    keyboard.BeginFrame();
    pads.BeginFrame();
    pads.OnButton(1, Emerald::GamepadButton::Start, true);
    Check(input.WasActionPressed(kPause), "Start is the pause action");
    Check(StepPauseMenu(input, menu, false) == PauseStep::Opened && menu.Row == 0,
          "Start opens the menu on Continue");

    pads.OnButton(1, Emerald::GamepadButton::Start, false);
    pads.BeginFrame();
    keyboard.BeginFrame();
    Check(!input.WasActionPressed(kPause), "a released Start does not resume by itself");
    Check(StepPauseMenu(input, menu, false) == PauseStep::None && menu.Open,
          "the menu waits for an edge");

    pads.OnButton(1, Emerald::GamepadButton::East, true);
    Check(input.WasActionPressed(kCancel) && !input.WasActionPressed(kPause),
          "East is cancel, the resume action");
    Check(StepPauseMenu(input, menu, false) == PauseStep::Resumed && !menu.Open,
          "East resumes and does not quit");

    pads.OnButton(1, Emerald::GamepadButton::East, false);
    pads.BeginFrame();
    keyboard.BeginFrame();
    keyboard.OnKeyDown(Emerald::Key::Escape);
    Check(input.WasActionPressed(kPause) && !input.WasActionPressed(kQuit),
          "Escape is pause, not quit");
    Check(StepPauseMenu(input, menu, false) == PauseStep::Opened && menu.Row == 0,
          "Escape opens the menu on Continue");
    keyboard.OnKeyUp(Emerald::Key::Escape);

    pads.BeginFrame();
    keyboard.BeginFrame();
    pads.OnButton(1, Emerald::GamepadButton::DPadDown, true);
    Check(StepPauseMenu(input, menu, false) == PauseStep::None && menu.Open && menu.Row == 1,
          "south moves to Quit game and leaves the menu open");
    pads.OnButton(1, Emerald::GamepadButton::DPadDown, false);

    pads.BeginFrame();
    keyboard.BeginFrame();
    pads.OnButton(1, Emerald::GamepadButton::DPadUp, true);
    Check(input.WasActionPressed(kMoveNorth), "a direction is still an edge while paused");
    Check(StepPauseMenu(input, menu, true) == PauseStep::None && menu.Open && menu.Row == 1,
          "a scripted frame does not leave the menu");
    Check(StepPauseMenu(input, menu, false) == PauseStep::None && menu.Open && menu.Row == 0,
          "north returns to Continue and does not resume");
    pads.OnButton(1, Emerald::GamepadButton::DPadUp, false);

    pads.BeginFrame();
    keyboard.BeginFrame();
    pads.OnButton(1, Emerald::GamepadButton::South, true);
    Check(StepPauseMenu(input, menu, false) == PauseStep::Resumed && !menu.Open,
          "confirm on Continue resumes and does not quit");
    pads.OnButton(1, Emerald::GamepadButton::South, false);

    menu = {};
    Check(ApplyPauseAction(menu, kPause) == PauseStep::Opened && menu.Row == 0,
          "the first row is Continue");
    Check(ApplyPauseAction(menu, kMoveSouth) == PauseStep::None && menu.Row == 1,
          "south selects Quit game");
    Check(ApplyPauseAction(menu, kConfirm) == PauseStep::Quit && menu.Open,
          "confirm on Quit game asks to save and exit");
    menu.Open = true;
    menu.Row = 1;
    Check(ApplyPauseAction(menu, kPause) == PauseStep::Resumed && !menu.Open,
          "Pause again resumes from Quit game without saving");
    Check(ApplyPauseAction(menu, kConfirm) == PauseStep::None && !menu.Open,
          "confirm while closed does not quit");
}

void TestNewGameConfirm()
{
    ConfirmWindow window;
    Check(TapConfirmWindow(window) == ConfirmTap::Armed && window.Armed, "the first press arms");
    AdvanceConfirmWindow(window, 1.4f, 1500);
    Check(window.Armed, "1400ms is still inside a 1500ms window");
    Check(TapConfirmWindow(window) == ConfirmTap::Accepted && !window.Armed,
          "the second press inside the window accepts");

    Check(TapConfirmWindow(window) == ConfirmTap::Armed, "a later press arms again");
    AdvanceConfirmWindow(window, 1.5f, 1500);
    Check(!window.Armed, "the window closes at its data limit");
    Check(TapConfirmWindow(window) == ConfirmTap::Armed && window.Armed,
          "a press after the window arms instead of starting over");

    AdvanceConfirmWindow(window, 0.0f, 1500);
    Check(window.Armed, "a zero step does not spend the window");
    AdvanceConfirmWindow(window, -1.0f, 1500);
    Check(window.Armed, "a backward step does not close the window");
}

void TestRejectedData()
{
    {
        TempCopy copy;
        ReplaceIn(copy.Path / "input.json",
                  "{ \"name\": \"Confirm\", \"keys\": [\"E\", \"Return\", \"Keypad Enter\"], \"buttons\": [\"South\"] }",
                  "{ \"name\": \"Confirm\", \"keys\": [\"E\"], \"buttons\": [\"South\"] },\n"
                  "      { \"name\": \"Confirm\", \"keys\": [\"Return\"], \"buttons\": [] }");
        Database data(copy.Path);
        DataError error;
        Check(!data.Load(&error), "a duplicate action fails the load");
        Check(error.Message.find("twice") != std::string::npos, "the duplicate error says twice");
    }
    {
        TempCopy copy;
        const std::string confirm =
            "{ \"name\": \"Confirm\", \"keys\": [\"E\", \"Return\", \"Keypad Enter\"], \"buttons\": [\"South\"] },\n";
        ReplaceIn(copy.Path / "input.json", confirm, "");
        Database data(copy.Path);
        DataError error;
        Check(!data.Load(&error), "a missing action fails the load");
        Check(error.Message.find("Confirm") != std::string::npos, "the error names Confirm");
    }
    {
        TempCopy copy;
        ReplaceIn(copy.Path / "prompts.json", "\"tile\": \"a\"", "\"tile\": \"nope\"");
        Database data(copy.Path);
        DataError error;
        Check(!data.Load(&error), "a missing glyph tile fails the load");
        Check(error.Message.find("nope") != std::string::npos, "the error names the tile");
    }
    {
        TempCopy copy;
        ReplaceIn(copy.Path / "input.json", "\"debugConfirmMilliseconds\": 1500,\n", "");
        Database data(copy.Path);
        DataError error;
        Check(!data.Load(&error), "a missing confirm window fails the load");
        Check(error.Message.find("debugConfirmMilliseconds") != std::string::npos,
              "the error names the confirm window");
    }
    {
        TempCopy copy;
        ReplaceIn(copy.Path / "input.json", "\"pauseContinue\": \"Continue\",\n", "");
        Database data(copy.Path);
        DataError error;
        Check(!data.Load(&error), "a missing pause line fails the load");
        Check(error.Message.find("pauseContinue") != std::string::npos,
              "the error names the pause line");
    }
}

} // namespace

void TestFrontDoor();

int main()
{
    Database sample(kSample);
    DataError sampleError;
    Check(sample.Load(&sampleError), "tests/data loads input and prompts");
    Database shipped(kShipped);
    DataError shippedError;
    Check(shipped.Load(&shippedError), "shipped data loads input and prompts");
    if (sample.IsLoaded())
        TestShippedControls(sample);
    if (shipped.IsLoaded())
        TestShippedControls(shipped);
    TestMapping();
    TestPauseResume();
    TestNewGameConfirm();
    TestRejectedData();
    TestFrontDoor();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
