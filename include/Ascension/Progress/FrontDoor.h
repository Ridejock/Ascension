#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Input/Controls.h"
#include "Ascension/Progress/HeroName.h"

namespace Ascension {

// Title, the slot list, the overwrite ask, and character create. Playing is the map
// and the fight. The words on each row live in input.json. This only tracks which row
// is marked and which step comes next. Escape is Pause: on the title it quits the
// process, and on every later step it steps back. It does not open the pause menu here.
enum class FrontPhase : u8 { Title, Slots, Overwrite, Name, Class, Begin, Playing };

// Quit leaves without writing. Continue loads the chosen slot. Began means the name and
// the marked starter are ready for the caller to build the run.
enum class FrontStep : u8 { None, Quit, Continue, Began };

// Why the name step refused Confirm. The screen copies the sentence from input.json.
enum class FrontNotice : u8 { None, Empty, TooLong, BadChar };

// Continue is always the first title row and New game is always the second. A save
// does not remove New game. Quit is the last row. Slot rows sit between New game and
// Quit, so their index follows the catalog. Confirm on a dimmed row does nothing.
inline constexpr i32 kTitleRowContinue = 0;
inline constexpr i32 kTitleRowNew = 1;

// Shown when New game would replace a full slot. The first row goes on to the name.
inline constexpr i32 kOverwriteRowYes = 0;
inline constexpr i32 kOverwriteRowBack = 1;
inline constexpr i32 kOverwriteRowCount = 2;

// One catalog entry the title can mark. The app fills it from the save folder.
// Legacy is the old single file. An empty numbered slot can take a new game.
struct FrontSlot {
    std::string Id;
    bool Legacy = false;
    bool Occupied = false;
    bool Readable = false;
};

enum class TitleKind : u8 { Continue, NewGame, Slot, Quit };

struct TitleRow {
    TitleKind Kind = TitleKind::Quit;
    // Index into FrontDoor::Slots when Kind is Slot. Otherwise -1.
    i32 Slot = -1;
    bool Enabled = true;
};

struct FrontDoor {
    FrontPhase Phase = FrontPhase::Title;
    // Where leaving the name step returns. The title's New game returns to the
    // title. F9 from a running game returns to that game.
    FrontPhase Return = FrontPhase::Title;
    // Catalog, numbered slots first, then the legacy file when it is present.
    std::vector<FrontSlot> Slots;
    // Index of the newest readable slot, or -1. The app sets this. Continue uses it.
    i32 Newest = -1;
    // Slot chosen by Continue, New game, or a slot row. -1 when create has no slot.
    i32 Slot = -1;
    // Row on the slot list to return to if the overwrite ask is refused.
    i32 PickRow = 0;
    // Set when F9 opened create from a running game. Begin then replaces the run
    // in memory and leaves the save file for the next Save.
    bool Debug = false;
    i32 Row = 0;
    i32 ClassRow = 0;
    std::string Name;
    FrontNotice Notice = FrontNotice::None;
};

// Continue, New game, one row per catalog slot, then Quit. Continue is dimmed when
// Newest is -1. A slot row is dimmed when that file cannot be loaded and is not an
// empty numbered slot.
[[nodiscard]] inline std::vector<TitleRow> TitleRows(const FrontDoor& door)
{
    std::vector<TitleRow> rows;
    TitleRow cont;
    cont.Kind = TitleKind::Continue;
    cont.Enabled = door.Newest >= 0;
    rows.push_back(cont);

    TitleRow fresh;
    fresh.Kind = TitleKind::NewGame;
    rows.push_back(fresh);

    for (i32 index = 0; index < static_cast<i32>(door.Slots.size()); ++index) {
        const FrontSlot& slot = door.Slots[static_cast<usize>(index)];
        TitleRow row;
        row.Kind = TitleKind::Slot;
        row.Slot = index;
        row.Enabled = slot.Readable || (!slot.Occupied && !slot.Legacy);
        rows.push_back(row);
    }

    TitleRow quit;
    quit.Kind = TitleKind::Quit;
    rows.push_back(quit);
    return rows;
}

// Numbered slots only. New game uses this list when every numbered slot is full,
// so the legacy file is never the one a new game replaces.
[[nodiscard]] inline std::vector<i32> SlotPickRows(const FrontDoor& door)
{
    std::vector<i32> rows;
    for (i32 index = 0; index < static_cast<i32>(door.Slots.size()); ++index) {
        if (!door.Slots[static_cast<usize>(index)].Legacy)
            rows.push_back(index);
    }
    return rows;
}

[[nodiscard]] inline i32 FirstEmptyNumbered(const std::vector<FrontSlot>& slots)
{
    for (i32 index = 0; index < static_cast<i32>(slots.size()); ++index) {
        const FrontSlot& slot = slots[static_cast<usize>(index)];
        if (!slot.Legacy && !slot.Occupied)
            return index;
    }
    return -1;
}

[[nodiscard]] inline i32 FrontRowCount(const FrontDoor& door, i32 starterCount)
{
    if (door.Phase == FrontPhase::Title)
        return static_cast<i32>(TitleRows(door).size());
    if (door.Phase == FrontPhase::Slots)
        return static_cast<i32>(SlotPickRows(door).size());
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
// same starter. Slot is kept: it is the file Begin will write.
inline void OpenFrontName(FrontDoor& door)
{
    door.Phase = FrontPhase::Name;
    door.Name.clear();
    door.Notice = FrontNotice::None;
    door.Row = 0;
}

// New game from the title. An empty numbered slot opens the name on that slot.
// When every numbered slot is full, the slot list asks which one to replace.
// No numbered slots at all still opens the name, so New game is never missing.
inline void BeginTitleNewGame(FrontDoor& door)
{
    door.Debug = false;
    door.Return = FrontPhase::Title;
    door.Notice = FrontNotice::None;
    const i32 empty = FirstEmptyNumbered(door.Slots);
    i32 numbered = 0;
    for (const FrontSlot& slot : door.Slots) {
        if (!slot.Legacy)
            ++numbered;
    }
    if (empty >= 0 || numbered == 0) {
        door.Slot = empty;
        OpenFrontName(door);
        return;
    }
    door.Phase = FrontPhase::Slots;
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
        const std::vector<TitleRow> rows = TitleRows(door);
        const i32 count = static_cast<i32>(rows.size());
        ClampFrontRow(door, count);
        if (action == kPause)
            return FrontStep::Quit;
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
        if (action != kConfirm || count < 1)
            return FrontStep::None;
        const TitleRow& row = rows[static_cast<usize>(door.Row)];
        if (!row.Enabled)
            return FrontStep::None;
        if (row.Kind == TitleKind::Continue) {
            if (door.Newest < 0 || door.Newest >= static_cast<i32>(door.Slots.size()))
                return FrontStep::None;
            door.Slot = door.Newest;
            return FrontStep::Continue;
        }
        if (row.Kind == TitleKind::Quit)
            return FrontStep::Quit;
        if (row.Kind == TitleKind::NewGame) {
            BeginTitleNewGame(door);
            return FrontStep::None;
        }
        if (row.Slot < 0 || row.Slot >= static_cast<i32>(door.Slots.size()))
            return FrontStep::None;
        door.Slot = row.Slot;
        door.Debug = false;
        door.Return = FrontPhase::Title;
        if (door.Slots[static_cast<usize>(door.Slot)].Readable)
            return FrontStep::Continue;
        OpenFrontName(door);
        return FrontStep::None;
    }

    if (door.Phase == FrontPhase::Slots) {
        const std::vector<i32> picks = SlotPickRows(door);
        const i32 count = static_cast<i32>(picks.size());
        ClampFrontRow(door, count);
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
            if (door.Row + 1 < count)
                ++door.Row;
            return FrontStep::None;
        }
        if (action != kConfirm || count < 1)
            return FrontStep::None;
        door.PickRow = door.Row;
        door.Slot = picks[static_cast<usize>(door.Row)];
        const FrontSlot& slot = door.Slots[static_cast<usize>(door.Slot)];
        if (!slot.Occupied) {
            door.Debug = false;
            door.Return = FrontPhase::Title;
            OpenFrontName(door);
            return FrontStep::None;
        }
        door.Phase = FrontPhase::Overwrite;
        door.Row = kOverwriteRowYes;
        door.Notice = FrontNotice::None;
        return FrontStep::None;
    }

    if (door.Phase == FrontPhase::Overwrite) {
        ClampFrontRow(door, kOverwriteRowCount);
        if (action == kPause || action == kCancel) {
            door.Phase = FrontPhase::Slots;
            door.Row = door.PickRow;
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
            door.Phase = FrontPhase::Slots;
            door.Row = door.PickRow;
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
