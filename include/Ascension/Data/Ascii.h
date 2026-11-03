#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Ascension {

// One id from tiles, props, foes, or classes, and the atlas character drawn for it.
// The bitmap itself is not in the JSON. It is a hardcoded glyph selected by this character.
struct AsciiGlyphId {
    std::string Id;
    char Glyph = '?';
};

// data/ascii.json. Marks are the cells an exit, a rest, a talk, or a shop replaces.
// The hero on the map is always '@' and is not a row here.
struct AsciiRules {
    std::vector<AsciiGlyphId> Tiles;
    std::vector<AsciiGlyphId> Props;
    std::vector<AsciiGlyphId> Foes;
    std::vector<AsciiGlyphId> Classes;
    char Exit = '>';
    char Rest = '+';
    char Talk = '&';
    char Shop = '$';
};

// The character listed for `id`, or `fallback` when the table has no row.
[[nodiscard]] inline char FindAsciiGlyph(std::span<const AsciiGlyphId> rows, std::string_view id,
                                         char fallback)
{
    for (const AsciiGlyphId& row : rows) {
        if (row.Id == id)
            return row.Glyph;
    }
    return fallback;
}

} // namespace Ascension
