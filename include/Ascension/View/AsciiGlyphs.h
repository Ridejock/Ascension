#pragma once

#include <string_view>

#include "Ascension/Data/Ascii.h"
#include "Ascension/Data/Map.h"
#include "Emerald/Assets/Image.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec4.h"
#include "Emerald/Renderer/Renderer2D.h"

namespace Emerald {
class Texture;
}

namespace Ascension {

// Hardcoded glyph cell. The atlas is this many pixels on a side, nearest-filtered.
inline constexpr i32 kAsciiGlyphPixels = 8;
inline constexpr i32 kAsciiAtlasColumns = 16;

// The map hero. Not the class letter.
inline constexpr char kAsciiHero = '@';
inline constexpr char kAsciiFallback = '?';

// Bright green ink on black paper. The atlas stores grey plus alpha; the tint supplies the green.
inline constexpr Emerald::Vec4 kAsciiInk{0.0f, 1.0f, 0.0f, 1.0f};
inline constexpr Emerald::Vec4 kAsciiPaper{0.0f, 0.0f, 0.0f, 1.0f};
// A fallen foe stays green, only darker, so the slot does not pick up a tile color.
inline constexpr Emerald::Vec4 kAsciiDim{0.0f, 0.45f, 0.0f, 1.0f};

// CPU image of every baked glyph, row-major, grey (white) plus alpha. Empty pixels are transparent
// so a black FillRect shows through. Upload once with Texture::Create and draw with DrawSprite.
struct AsciiAtlas {
    Emerald::Image Image;
    i32 Columns = kAsciiAtlasColumns;
    i32 Count = 0;
};

[[nodiscard]] AsciiAtlas RasterizeAsciiAtlas();
[[nodiscard]] bool AsciiGlyphKnown(char code);
// Index of `code` in the atlas. An unknown code is the '?' cell.
[[nodiscard]] i32 AsciiGlyphIndex(char code);
// Every baked row is 8 ink-or-empty pixels, and the codes do not repeat.
[[nodiscard]] bool AsciiGlyphsWellFormed();

// One glyph for a map cell. A hero replaces whoever else stands there. A foe replaces a mark,
// a mark replaces an exit, an exit replaces a blocked prop cell, and that replaces the ground.
// An open prop cell keeps the ground. `foe` with an empty id is still a body, drawn as '?'.
[[nodiscard]] char AsciiCellGlyph(const AsciiRules& rules, const MapDef& map, i32 x, i32 y,
                                  bool hero, bool foe, std::string_view foeId);

// One tinted quad on the 2D renderer. `atlas` is the texture from RasterizeAsciiAtlas.
void DrawAsciiGlyph(Emerald::Renderer2D& renderer, const Emerald::Texture& atlas, char code,
                    const Emerald::Vec2& topLeft, const Emerald::Vec2& size,
                    const Emerald::Vec4& tint);

} // namespace Ascension
