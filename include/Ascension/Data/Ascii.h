#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Emerald/Core/Defines.h"

namespace Ascension {

// One character in ascii.json. Id is a tile, a mark, a prop, a foe, or a class.
// Glyph is one character Emerald's debug font can draw.
struct AsciiGlyph {
    std::string Id;
    char Glyph = 0;
};

// The opt-in character presentation. Graphical drawing stays the default.
// Scale is the debug-font size on a map cell. BattleScale is the size in a
// fight figure. Paper fills the cell. Ink is the color of a prop. A tile is
// drawn in that tile's own color, and a foe uses its color when it has one.
struct AsciiRules {
    i32 Scale = 6;
    i32 BattleScale = 8;
    u8 PaperRed = 18;
    u8 PaperGreen = 16;
    u8 PaperBlue = 14;
    u8 InkRed = 232;
    u8 InkGreen = 224;
    u8 InkBlue = 204;
    std::vector<AsciiGlyph> Tiles;
    std::vector<AsciiGlyph> Marks;
    std::vector<AsciiGlyph> Props;
    std::vector<AsciiGlyph> Foes;
    std::vector<AsciiGlyph> Classes;
};

[[nodiscard]] inline const AsciiGlyph* FindAsciiGlyph(std::span<const AsciiGlyph> rows,
                                                      std::string_view id)
{
    for (const AsciiGlyph& row : rows) {
        if (row.Id == id)
            return &row;
    }
    return nullptr;
}

// `fallback` when `id` is not in the table. A new tile, foe, class, or prop
// can load before it has a row. The draw uses `fallback` until the table names it.
[[nodiscard]] inline char AsciiChar(std::span<const AsciiGlyph> rows, std::string_view id,
                                    char fallback)
{
    const AsciiGlyph* found = FindAsciiGlyph(rows, id);
    return found != nullptr ? found->Glyph : fallback;
}

} // namespace Ascension
