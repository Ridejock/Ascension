#pragma once

#include <string>
#include <string_view>

#include "Ascension/Input/Controls.h"

namespace Ascension {

// The title sheet inside camp. Column 0 is the equipped slots. Column 1 is owned
// titles that are not worn. Confirm arms a second confirm, the same way the shop
// arms equip and buy. Cancel backs out of that ask, and from the sheet back to camp.
// Menu and Interact leave camp.
enum class TitleAsk : u8 { None, Equip, Unequip };

struct TitleSheet {
    bool Open = false;
    i32 Column = 0;
    i32 Row = 0;
    TitleAsk Ask = TitleAsk::None;
};

// None is a move, an armed ask, or a key the sheet ignores. Back leaves the sheet
// and stays in camp. Close leaves camp. Equip and Unequip are the second confirm:
// the row is the slot, or the owned title, the caller should change.
enum class TitleSheetStep : u8 { None, Back, Close, Equip, Unequip };

// Replaces every {name} in `pattern`. A line with no token is returned as written.
[[nodiscard]] inline std::string TitleLine(std::string_view pattern, std::string_view name)
{
    std::string out(pattern);
    const std::string_view token = "{name}";
    for (;;) {
        const usize at = out.find(token);
        if (at == std::string::npos)
            return out;
        out.replace(at, token.size(), name);
    }
}

inline void ClampTitleSheet(TitleSheet& sheet, i32 slots, i32 owned)
{
    if (sheet.Column < 0)
        sheet.Column = 0;
    if (sheet.Column > 1)
        sheet.Column = 1;
    if (slots < 0)
        slots = 0;
    if (owned < 0)
        owned = 0;
    const i32 rows = sheet.Column == 0 ? slots : owned;
    if (rows < 1)
        sheet.Row = 0;
    else if (sheet.Row >= rows)
        sheet.Row = rows - 1;
    else if (sheet.Row < 0)
        sheet.Row = 0;
}

// `slots` is the class rarity's title slot count. `equipped` is how many of those
// slots are filled from the front. `owned` is owned titles that are not worn.
// An empty slot does not arm a take-off. An empty owned list does not arm an equip.
[[nodiscard]] inline TitleSheetStep ApplyTitleSheet(TitleSheet& sheet, std::string_view action,
                                                    i32 slots, i32 equipped, i32 owned)
{
    if (slots < 0)
        slots = 0;
    if (equipped < 0)
        equipped = 0;
    if (equipped > slots)
        equipped = slots;
    if (owned < 0)
        owned = 0;
    ClampTitleSheet(sheet, slots, owned);

    if (action == kMenu || action == kInteract) {
        sheet = {};
        return TitleSheetStep::Close;
    }

    const bool move = action == kMoveNorth || action == kMoveSouth || action == kMoveWest ||
                      action == kMoveEast || action == kCyclePrev || action == kCycleNext;
    if (sheet.Ask != TitleAsk::None) {
        if (action == kCancel || move) {
            sheet.Ask = TitleAsk::None;
            if (!move)
                return TitleSheetStep::None;
        } else if (action == kConfirm) {
            const TitleSheetStep step =
                sheet.Ask == TitleAsk::Equip ? TitleSheetStep::Equip : TitleSheetStep::Unequip;
            sheet.Ask = TitleAsk::None;
            return step;
        } else {
            return TitleSheetStep::None;
        }
    }

    if (action == kCancel) {
        sheet = {};
        return TitleSheetStep::Back;
    }
    if (action == kMoveNorth) {
        if (sheet.Row > 0)
            --sheet.Row;
        return TitleSheetStep::None;
    }
    if (action == kMoveSouth) {
        const i32 rows = sheet.Column == 0 ? slots : owned;
        if (rows > 0 && sheet.Row + 1 < rows)
            ++sheet.Row;
        return TitleSheetStep::None;
    }
    if (action == kMoveWest || action == kMoveEast || action == kCyclePrev ||
        action == kCycleNext) {
        sheet.Column = sheet.Column == 0 ? 1 : 0;
        ClampTitleSheet(sheet, slots, owned);
        return TitleSheetStep::None;
    }
    if (action == kConfirm) {
        if (sheet.Column == 0) {
            if (sheet.Row >= 0 && sheet.Row < equipped)
                sheet.Ask = TitleAsk::Unequip;
        } else if (sheet.Row >= 0 && sheet.Row < owned && equipped < slots) {
            sheet.Ask = TitleAsk::Equip;
        }
        return TitleSheetStep::None;
    }
    if ((action == kUnequip || action == kContext) && sheet.Column == 0 && sheet.Row >= 0 &&
        sheet.Row < equipped) {
        sheet.Ask = TitleAsk::Unequip;
        return TitleSheetStep::None;
    }
    return TitleSheetStep::None;
}

} // namespace Ascension
