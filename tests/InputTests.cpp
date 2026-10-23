// Bindings in input.json and the prompt glyph map. No window or GPU.

#include <filesystem>
#include <fstream>
#include <string>

#include <SDL3/SDL.h>

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
    Check(rules.Actions.size() == kControlActions.size(), "every control action is listed once");
    for (const std::string_view action : kControlActions)
        Check(FindControl(rules, action) != nullptr, "a required action is present");

    const PromptRules& prompts = data.GetPrompts();
    Check(prompts.IconScale == 1 && prompts.TextScale == 2, "prompt scales come from prompts.json");
    Check(prompts.TextFamily == "keyboard", "keyboard is the text family");
    Check(prompts.DefaultFamily == "switch", "an unnamed pad uses the switch sheet");
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
    Check(PromptFamilyForType(prompts, "Standard") == "switch", "a generic pad uses the switch sheet");
    Check(PromptFamilyForType(prompts, "Not A Pad") == "switch", "an unknown type uses the default sheet");
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
    pads.AddPad(1, nullptr, "Test Pad", SDL_GAMEPAD_TYPE_XBOXONE);
    Emerald::Input input(keyboard, pads);
    std::string why;
    Check(ApplyControls(input, data.GetControls(), why), "bindings apply");
    if (!why.empty())
        std::printf("    apply: %s\n", why.c_str());

    keyboard.BeginFrame();
    pads.BeginFrame();
    keyboard.OnKeyDown(SDL_SCANCODE_E);
    Check(input.WasActionPressed(kConfirm), "E confirms");
    Check(!input.WasActionPressed(kMoveWest), "E is not a step");

    keyboard.BeginFrame();
    pads.BeginFrame();
    keyboard.OnKeyDown(SDL_SCANCODE_A);
    Check(input.WasActionPressed(kMoveWest), "A steps west");
    Check(!input.WasActionPressed(kConfirm), "A does not confirm");

    keyboard.OnKeyUp(SDL_SCANCODE_E);
    keyboard.OnKeyUp(SDL_SCANCODE_A);
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
    Check(input.WasActionPressed(kPause), "start pauses");
    pads.OnButton(1, Emerald::GamepadButton::Start, false);
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

    Check(BoundKeyNeedsPlayerTurn(input, static_cast<Emerald::Key>(SDL_SCANCODE_2)),
          "a skill key needs the player's turn");
    Check(BoundKeyNeedsPlayerTurn(input, static_cast<Emerald::Key>(SDL_SCANCODE_RIGHT)),
          "aiming needs the player's turn");
    Check(BoundKeyNeedsPlayerTurn(input, static_cast<Emerald::Key>(SDL_SCANCODE_SPACE)),
          "waiting needs the player's turn");
    Check(!BoundKeyNeedsPlayerTurn(input, static_cast<Emerald::Key>(SDL_SCANCODE_N)),
          "N is not a combat key");
    Check(!BoundKeyNeedsPlayerTurn(input, static_cast<Emerald::Key>(SDL_SCANCODE_R)),
          "restart is not a combat key");
    Check(!BoundKeyNeedsPlayerTurn(input, static_cast<Emerald::Key>(SDL_SCANCODE_E)),
          "confirm is not a combat key");

    const std::vector<std::string_view> up =
        input.FindActionsUsing(static_cast<Emerald::Key>(SDL_SCANCODE_UP));
    const std::vector<std::string_view> enter =
        input.FindActionsUsing(static_cast<Emerald::Key>(SDL_SCANCODE_RETURN));
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
    freshPads.AddPad(1, nullptr, "Test Pad", SDL_GAMEPAD_TYPE_XBOXONE);
    freshPads.BeginFrame();
    freshPads.OnButton(1, Emerald::GamepadButton::South, true);
    Check(!fresh.WasActionPressed(kConfirm), "a failed apply leaves the actions unbound");
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
}

} // namespace

int main()
{
    SDL_Init(SDL_INIT_GAMEPAD);
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
    TestRejectedData();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    SDL_Quit();
    return g_Failures == 0 ? 0 : 1;
}
