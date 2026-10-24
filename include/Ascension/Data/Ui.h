#pragma once

#include "Emerald/Core/Defines.h"

namespace Ascension {

// One #RRGGBB color from the screen data. The draw code turns it into a float color.
struct UiRgb {
    u8 Red = 0;
    u8 Green = 0;
    u8 Blue = 0;
};

// Type size, spacing, color, and message timing shared by the battle and the shop.
// battle_ui.json is the file. The shop reads this for type and color, and shop_ui.json
// for how its own list and detail block are arranged. A larger number is a larger gap
// or a larger debug-font scale (each step is 9 pixels of line height).
struct UiStyle {
    i32 TitleScale = 4;
    i32 BodyScale = 3;
    i32 DetailScale = 2;
    i32 BannerScale = 3;
    i32 MessageScale = 3;
    i32 LineGap = 12;
    i32 GroupGap = 22;
    i32 RowGap = 12;
    i32 PanelPad = 22;
    i32 BarHeight = 16;
    i32 BarWidth = 300;
    i32 HintClearance = 20;
    i32 MessageMinMilliseconds = 1100;
    UiRgb Ink{};
    UiRgb Dim{};
    UiRgb Frame{};
    UiRgb Panel{};
    UiRgb Error{};
    UiRgb Hp{};
    UiRgb Focus{};
    UiRgb Highlight{};
    UiRgb Figure{};
    UiRgb Foe{};
};

// Where the battle panels sit, on top of UiStyle. The hint row at the bottom of the
// window is not part of this: that bar stays where the prompt draw puts it.
// BottomInset is the empty pixels under the lower panel. BottomHeight is that panel.
// SplitGap is the gap between the side panels and the lower panel.
struct BattleUi {
    UiStyle Style;
    i32 Margin = 28;
    i32 PanelTop = 24;
    i32 PartyWidth = 600;
    i32 Gutter = 20;
    i32 BottomHeight = 156;
    i32 BottomInset = 24;
    i32 SplitGap = 16;
    i32 FigureSize = 18;
    i32 TurnPreview = 4;
    i32 PackScale = 2;
};

// Shop-only layout. Type, color, padding, and message timing come from BattleUi::Style.
// ListRows is how many rows are on screen at once. A longer bag scrolls with the cursor.
struct ShopUi {
    i32 ListRows = 7;
    i32 IconScale = 2;
    i32 DetailLines = 4;
    i32 ColumnGap = 16;
};

} // namespace Ascension
