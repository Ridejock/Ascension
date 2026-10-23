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
struct ControlRules {
    i32 NavInitialMilliseconds = 280;
    i32 NavRepeatMilliseconds = 120;
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

inline constexpr std::array<std::string_view, 28> kControlActions = {
    kMoveNorth, kMoveSouth, kMoveEast, kMoveWest, kConfirm, kCancel, kMenu,    kContext,
    kPause,     kHelp,      kCyclePrev, kCycleNext, kWait,   kFlee,   kInteract, kUnequip,
    kSave,      kQuit,      kRetry,     kNext,      kSlot1,  kSlot2,  kSlot3,    kSlot4,
    kSlot5,     kSlot6,     kSlot7,     kSlot8,
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
// devices. Pause is not a reason to skip: WasActionPressed is only true on the
// frame the button goes down, and Start / East are how pause ends.
[[nodiscard]] inline bool ReadActionEdges(bool scripting, bool paused)
{
    if (scripting)
        return false;
    if (paused)
        return true;
    return true;
}

// Pause toggles. While paused, Cancel resumes. Any other action leaves the flag.
[[nodiscard]] inline bool PauseAfter(bool paused, std::string_view action)
{
    if (action == kPause)
        return !paused;
    if (paused && action == kCancel)
        return false;
    return paused;
}

// One frame of pause edges. Pause wins over Cancel when both are down, so a
// resume button on the same frame as Start does not also run as a close.
[[nodiscard]] inline bool StepPause(const Emerald::Input& input, bool paused, bool scripting)
{
    if (!ReadActionEdges(scripting, paused))
        return paused;
    if (input.WasActionPressed(kPause))
        return PauseAfter(paused, kPause);
    if (input.WasActionPressed(kCancel))
        return PauseAfter(paused, kCancel);
    return paused;
}

// True when a key's bound actions aim or spend the player's turn. A key with no action does not.
[[nodiscard]] bool BoundKeyNeedsPlayerTurn(const Emerald::Input& input, Emerald::Key key);

// The bind named `name`, or null.
[[nodiscard]] const ControlBind* FindControl(const ControlRules& rules, std::string_view name);

// Replaces each action's keys and buttons on `input`. A name Emerald does not know fails
// the whole call and leaves `input` unchanged. The load already required every action.
bool ApplyControls(Emerald::Input& input, const ControlRules& rules, std::string& error);

} // namespace Ascension
