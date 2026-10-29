#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "Emerald/Input/Input.h"

namespace Ascension {

// One action's bindings, as input.json names them. Emerald's own names: key names from
// SDL ("E", "Return", "Space") and button names from GamepadButton ("South", "DPadUp",
// "LeftStickUp"). The game asks about the action; it does not name a key or a button.
struct ControlBind {
    std::string Name;
    std::vector<std::string> Keys;
    std::vector<std::string> Buttons;
};

// navInitialMilliseconds is the pause after the first press before a held direction
// repeats. navRepeatMilliseconds is the pause between repeats after that. The walk
// step uses walk.json; this pair is menus, the battle command cursor, and targeting.
// DebugConfirmMilliseconds is how long the second DebugNewGame press still counts.
// The four debug lines are the notes those actions write.
struct ControlRules {
    i32 NavInitialMilliseconds = 280;
    i32 NavRepeatMilliseconds = 120;
    i32 DebugConfirmMilliseconds = 1500;
    std::string DebugRespawned;
    std::string DebugRespawnBusy;
    std::string DebugNewGameArmed;
    std::string DebugNewGame;
    // Pause menu copy. Continue is the first row. Quit game is the second.
    std::string PauseTitle;
    std::string PauseContinue;
    std::string PauseQuit;
    std::vector<ControlBind> Actions;
};

// Action ids. input.json has to define each of these, and no others. The words are
// the names passed to Emerald::Input.
inline constexpr std::string_view kMoveNorth = "MoveNorth";
inline constexpr std::string_view kMoveSouth = "MoveSouth";
inline constexpr std::string_view kMoveEast = "MoveEast";
inline constexpr std::string_view kMoveWest = "MoveWest";
inline constexpr std::string_view kConfirm = "Confirm";
inline constexpr std::string_view kCancel = "Cancel";
inline constexpr std::string_view kMenu = "Menu";
inline constexpr std::string_view kContext = "Context";
inline constexpr std::string_view kPause = "Pause";
inline constexpr std::string_view kHelp = "Help";
inline constexpr std::string_view kCyclePrev = "CyclePrev";
inline constexpr std::string_view kCycleNext = "CycleNext";
inline constexpr std::string_view kWait = "Wait";
inline constexpr std::string_view kFlee = "Flee";
inline constexpr std::string_view kInteract = "Interact";
inline constexpr std::string_view kUnequip = "Unequip";
inline constexpr std::string_view kSave = "Save";
inline constexpr std::string_view kQuit = "Quit";
inline constexpr std::string_view kRetry = "Retry";
inline constexpr std::string_view kNext = "Next";
inline constexpr std::string_view kSlot1 = "Slot1";
inline constexpr std::string_view kSlot2 = "Slot2";
inline constexpr std::string_view kSlot3 = "Slot3";
inline constexpr std::string_view kSlot4 = "Slot4";
inline constexpr std::string_view kSlot5 = "Slot5";
inline constexpr std::string_view kSlot6 = "Slot6";
inline constexpr std::string_view kSlot7 = "Slot7";
inline constexpr std::string_view kSlot8 = "Slot8";
inline constexpr std::string_view kDebugRespawnFoes = "DebugRespawnFoes";
inline constexpr std::string_view kDebugNewGame = "DebugNewGame";

inline constexpr std::array<std::string_view, 30> kControlActions = {
    kMoveNorth, kMoveSouth, kMoveEast, kMoveWest, kConfirm, kCancel, kMenu,     kContext,
    kPause,     kHelp,      kCyclePrev, kCycleNext, kWait,   kFlee,   kInteract, kUnequip,
    kSave,      kQuit,      kRetry,     kNext,      kSlot1,  kSlot2,  kSlot3,    kSlot4,
    kSlot5,     kSlot6,     kSlot7,     kSlot8,     kDebugRespawnFoes, kDebugNewGame,
};

[[nodiscard]] inline bool IsControlAction(std::string_view name)
{
    for (const std::string_view action : kControlActions) {
        if (action == name)
            return true;
    }
    return false;
}

// 0..7 for Slot1..Slot8. -1 for any other action.
[[nodiscard]] inline int SlotIndex(std::string_view action)
{
    if (action.size() != 5 || action.substr(0, 4) != "Slot")
        return -1;
    const char digit = action[4];
    if (digit < '1' || digit > '8')
        return -1;
    return digit - '1';
}

// True for an action that aims or spends the player's turn. Next, Retry, and Interact
// are not in that set. MoveEast and MoveWest are: in a fight they change the target.
[[nodiscard]] inline bool ActionNeedsPlayerTurn(std::string_view action)
{
    if (action == kWait || action == kFlee || action == kMenu || action == kCyclePrev ||
        action == kCycleNext || action == kMoveEast || action == kMoveWest)
        return true;
    return SlotIndex(action) >= 0;
}

// Scripted keys are applied by the script, so that frame does not also read the
// devices. The pause menu is not a reason to skip: WasActionPressed is only true
// on the frame the button goes down, and that edge opens the menu and moves it.
[[nodiscard]] inline bool ReadActionEdges(bool scripting, bool paused)
{
    if (scripting)
        return false;
    if (paused)
        return true;
    return true;
}

// The second DebugNewGame press. Armed is the first press. Accepted is a second
// press that landed while the window was still open. AdvanceConfirmWindow drops
// an armed press once DebugConfirmMilliseconds has passed.
enum class ConfirmTap : u8 { Armed, Accepted };

struct ConfirmWindow {
    bool Armed = false;
    f32 Elapsed = 0.0f;
};

// dt is seconds, the same clock as the frame. A non-positive dt does not move
// the window. A window below 1ms never expires on its own.
inline void AdvanceConfirmWindow(ConfirmWindow& window, f32 dt, i32 windowMilliseconds)
{
    if (!window.Armed || windowMilliseconds < 1)
        return;
    if (dt > 0.0f)
        window.Elapsed += dt;
    const f32 limit = static_cast<f32>(windowMilliseconds) / 1000.0f;
    if (window.Elapsed < limit)
        return;
    window.Armed = false;
    window.Elapsed = 0.0f;
}

// The first press arms the window. A press while it is armed accepts and disarms.
inline ConfirmTap TapConfirmWindow(ConfirmWindow& window)
{
    if (window.Armed) {
        window.Armed = false;
        window.Elapsed = 0.0f;
        return ConfirmTap::Accepted;
    }
    window.Armed = true;
    window.Elapsed = 0.0f;
    return ConfirmTap::Armed;
}

// Continue is row 0. Quit game is row 1. The words are pauseContinue and pauseQuit.
inline constexpr i32 kPauseRowCount = 2;

struct PauseMenu {
    bool Open = false;
    i32 Row = 0;
};

// Opened and Resumed do not write a save. Quit asks the caller to write the run
// and exit. None means the menu stayed as it was, including a row change.
enum class PauseStep : u8 { None, Opened, Resumed, Quit };

// Actions the open menu reads besides Pause. East and west are not in this set.
[[nodiscard]] inline bool PauseMenuUses(std::string_view action)
{
    return action == kCancel || action == kConfirm || action == kMoveNorth ||
           action == kMoveSouth || action == kCyclePrev || action == kCycleNext;
}

// Closed: Pause opens on Continue. Open: Pause or Cancel resumes. Confirm on
// Continue resumes. Confirm on Quit game returns Quit. North and south move the
// row and leave the menu open.
[[nodiscard]] inline PauseStep ApplyPauseAction(PauseMenu& menu, std::string_view action)
{
    if (!menu.Open) {
        if (action != kPause)
            return PauseStep::None;
        menu.Open = true;
        menu.Row = 0;
        return PauseStep::Opened;
    }
    if (action == kPause || action == kCancel) {
        menu.Open = false;
        menu.Row = 0;
        return PauseStep::Resumed;
    }
    if (action == kMoveNorth || action == kCyclePrev) {
        if (menu.Row > 0)
            --menu.Row;
        return PauseStep::None;
    }
    if (action == kMoveSouth || action == kCycleNext) {
        if (menu.Row + 1 < kPauseRowCount)
            ++menu.Row;
        return PauseStep::None;
    }
    if (action == kConfirm) {
        if (menu.Row <= 0) {
            menu.Open = false;
            menu.Row = 0;
            return PauseStep::Resumed;
        }
        return PauseStep::Quit;
    }
    return PauseStep::None;
}

// One frame of pause-menu edges. Pause wins over Cancel and Confirm, so the
// frame that opens the menu does not also activate Continue.
[[nodiscard]] inline PauseStep StepPauseMenu(const Emerald::Input& input, PauseMenu& menu,
                                             bool scripting)
{
    if (!ReadActionEdges(scripting, menu.Open))
        return PauseStep::None;
    if (input.WasActionPressed(kPause))
        return ApplyPauseAction(menu, kPause);
    if (!menu.Open)
        return PauseStep::None;
    if (input.WasActionPressed(kCancel))
        return ApplyPauseAction(menu, kCancel);
    if (input.WasActionPressed(kConfirm))
        return ApplyPauseAction(menu, kConfirm);
    if (input.WasActionPressed(kMoveNorth))
        return ApplyPauseAction(menu, kMoveNorth);
    if (input.WasActionPressed(kCyclePrev))
        return ApplyPauseAction(menu, kCyclePrev);
    if (input.WasActionPressed(kMoveSouth))
        return ApplyPauseAction(menu, kMoveSouth);
    if (input.WasActionPressed(kCycleNext))
        return ApplyPauseAction(menu, kCycleNext);
    return PauseStep::None;
}

// True when a key's bound actions aim or spend the player's turn. A key with no action does not.
[[nodiscard]] bool BoundKeyNeedsPlayerTurn(const Emerald::Input& input, Emerald::Key key);

// The bind named `name`, or null.
[[nodiscard]] const ControlBind* FindControl(const ControlRules& rules, std::string_view name);

// Replaces each action's keys and buttons on `input`. A name Emerald does not know fails
// the whole call and leaves `input` unchanged. The load already required every action.
bool ApplyControls(Emerald::Input& input, const ControlRules& rules, std::string& error);

} // namespace Ascension
