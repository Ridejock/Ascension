#pragma once

#include <string>
#include <string_view>

#include "Ascension/Input/Controls.h"
#include "Ascension/Progress/HeroName.h"

namespace Ascension {

// Title, the overwrite ask, and character create. Playing is the map and the fight.
// The words on each row live in input.json. This only tracks which row is marked
// and which step comes next. Escape is Pause: on the title it quits the process,
// and on every later step it steps back. It does not open the pause menu here.
enum class FrontPhase : u8 { Title, Overwrite, Name, Class, Begin, Playing };

// Quit leaves without writing. Continue loads the slot. Began means the name and
// the marked starter are ready for the caller to build the run.
enum class FrontStep : u8 { None, Quit, Continue, Began };

// Why the name step refused Confirm. The screen copies the sentence from input.json.
enum class FrontNotice : u8 { None, Empty, TooLong, BadChar };

// Continue, New game, Quit. Continue is the first row and is dimmed when no save
// can be loaded. Confirm on a dimmed row does nothing.
inline constexpr i32 kTitleRowContinue = 0;
inline constexpr i32 kTitleRowNew = 1;
inline constexpr i32 kTitleRowQuit = 2;
inline constexpr i32 kTitleRowCount = 3;

// Shown only when New game would replace a save. The first row goes on to the name.
inline constexpr i32 kOverwriteRowYes = 0;
inline constexpr i32 kOverwriteRowBack = 1;
inline constexpr i32 kOverwriteRowCount = 2;

struct FrontDoor {
    FrontPhase Phase = FrontPhase::Title;
    // Where leaving the name step returns. The title's New game returns to the
    // title. F9 from a running game returns to that game.
    FrontPhase Return = FrontPhase::Title;
    bool SaveExists = false;
    // Set when F9 opened create from a running game. Begin then replaces the run
    // in memory and leaves the save file for the next Save.
    bool Debug = false;
    i32 Row = 0;
    i32 ClassRow = 0;
    std::string Name;
    FrontNotice Notice = FrontNotice::None;
};

[[nodiscard]] inline i32 FrontRowCount(const FrontDoor& door, i32 starterCount)
{
    if (door.Phase == FrontPhase::Title)
        return kTitleRowCount;
    if (door.Phase == FrontPhase::Overwrite)
        return kOverwriteRowCount;
    if (door.Phase == FrontPhase::Class)
        return starterCount > 0 ? starterCount : 0;
    if (door.Phase == FrontPhase::Begin)
        return 1;
    return 0;
}

inline void ClampFrontRow(FrontDoor& door, i32 count)
{
    if (count < 1) {
        door.Row = 0;
        return;
    }
    if (door.Row < 0)
        door.Row = 0;
    if (door.Row >= count)
        door.Row = count - 1;
}

// The name step, with an empty draft. ClassRow is kept so Back returns to the
// same starter.
inline void OpenFrontName(FrontDoor& door)
{
    door.Phase = FrontPhase::Name;
    door.Name.clear();
    door.Notice = FrontNotice::None;
    door.Row = 0;
}

// F9 from a running game. The save file is not the question: Begin does not write
// it. Cancel from an empty name returns to the game.
inline void OpenDebugCreate(FrontDoor& door)
{
    door.Debug = true;
    door.Return = FrontPhase::Playing;
    door.ClassRow = 0;
    OpenFrontName(door);
}

inline void LeaveFront(FrontDoor& door)
{
    door.Phase = door.Return;
    door.Notice = FrontNotice::None;
    door.Name.clear();
    door.Debug = false;
    door.Row = door.Phase == FrontPhase::Title ? kTitleRowNew : 0;
}

// MoveNorth and MoveSouth, and the shoulder cycle, move a row. Confirm activates
// it. Cancel steps back. On the name step Cancel deletes one character, and leaves
// when the draft is already empty. Pause quits only from the title.
[[nodiscard]] inline FrontStep ApplyFrontAction(FrontDoor& door, std::string_view action,
                                                i32 starterCount)
{
    if (door.Phase == FrontPhase::Playing)
        return FrontStep::None;

    if (door.Phase == FrontPhase::Title) {
        ClampFrontRow(door, kTitleRowCount);
        if (action == kPause)
            return FrontStep::Quit;
        if (action == kMoveNorth || action == kCyclePrev) {
            if (door.Row > 0)
                --door.Row;
            return FrontStep::None;
        }
        if (action == kMoveSouth || action == kCycleNext) {
            if (door.Row + 1 < kTitleRowCount)
                ++door.Row;
            return FrontStep::None;
        }
        if (action != kConfirm)
            return FrontStep::None;
        if (door.Row == kTitleRowContinue) {
            if (!door.SaveExists)
                return FrontStep::None;
            return FrontStep::Continue;
        }
        if (door.Row == kTitleRowQuit)
            return FrontStep::Quit;
        door.Debug = false;
        door.Return = FrontPhase::Title;
        if (door.SaveExists) {
            door.Phase = FrontPhase::Overwrite;
            door.Row = kOverwriteRowYes;
            door.Notice = FrontNotice::None;
            return FrontStep::None;
        }
        OpenFrontName(door);
        return FrontStep::None;
    }

    if (door.Phase == FrontPhase::Overwrite) {
        ClampFrontRow(door, kOverwriteRowCount);
        if (action == kPause || action == kCancel) {
            door.Phase = FrontPhase::Title;
            door.Row = kTitleRowNew;
            door.Notice = FrontNotice::None;
            return FrontStep::None;
        }
        if (action == kMoveNorth || action == kCyclePrev) {
            if (door.Row > 0)
                --door.Row;
            return FrontStep::None;
        }
        if (action == kMoveSouth || action == kCycleNext) {
            if (door.Row + 1 < kOverwriteRowCount)
                ++door.Row;
            return FrontStep::None;
        }
        if (action != kConfirm)
            return FrontStep::None;
        if (door.Row != kOverwriteRowYes) {
            door.Phase = FrontPhase::Title;
            door.Row = kTitleRowNew;
            return FrontStep::None;
        }
        OpenFrontName(door);
        return FrontStep::None;
    }

    if (door.Phase == FrontPhase::Name) {
        if (action == kPause) {
            LeaveFront(door);
            return FrontStep::None;
        }
        if (action == kCancel) {
            if (!door.Name.empty()) {
                PopHeroName(door.Name);
                door.Notice = FrontNotice::None;
                return FrontStep::None;
            }
            LeaveFront(door);
            return FrontStep::None;
        }
        if (action != kConfirm)
            return FrontStep::None;
        const HeroNameCheck check = CheckHeroName(door.Name);
        if (check.Status == HeroNameStatus::TooLong) {
            door.Notice = FrontNotice::TooLong;
            return FrontStep::None;
        }
        if (check.Status == HeroNameStatus::BadChar) {
            door.Notice = FrontNotice::BadChar;
            return FrontStep::None;
        }
        if (check.Status != HeroNameStatus::Ok) {
            door.Notice = FrontNotice::Empty;
            return FrontStep::None;
        }
        door.Name = check.Name;
        door.Notice = FrontNotice::None;
        door.Phase = FrontPhase::Class;
        door.Row = door.ClassRow;
        ClampFrontRow(door, starterCount);
        return FrontStep::None;
    }

    if (door.Phase == FrontPhase::Class) {
        const i32 count = FrontRowCount(door, starterCount);
        ClampFrontRow(door, count);
        if (action == kPause || action == kCancel) {
            door.ClassRow = door.Row;
            door.Phase = FrontPhase::Name;
            door.Notice = FrontNotice::None;
            return FrontStep::None;
        }
        if (count < 1)
            return FrontStep::None;
        if (action == kMoveNorth || action == kCyclePrev) {
            if (door.Row > 0)
                --door.Row;
            return FrontStep::None;
        }
        if (action == kMoveSouth || action == kCycleNext) {
            if (door.Row + 1 < count)
                ++door.Row;
            return FrontStep::None;
        }
        if (action != kConfirm)
            return FrontStep::None;
        door.ClassRow = door.Row;
        door.Phase = FrontPhase::Begin;
        door.Row = 0;
        door.Notice = FrontNotice::None;
        return FrontStep::None;
    }

    if (door.Phase == FrontPhase::Begin) {
        if (action == kPause || action == kCancel) {
            door.Phase = FrontPhase::Class;
            door.Row = door.ClassRow;
            ClampFrontRow(door, starterCount);
            door.Notice = FrontNotice::None;
            return FrontStep::None;
        }
        if (action == kConfirm)
            return FrontStep::Began;
    }
    return FrontStep::None;
}

} // namespace Ascension
