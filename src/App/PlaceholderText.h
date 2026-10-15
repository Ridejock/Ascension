#pragma once

#include <string_view>

#include "Emerald/Core/Defines.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec4.h"

namespace Emerald {
class Renderer2D;
}

namespace Ascension {

// Height of one line at `scale`, including the gap under the glyphs.
[[nodiscard]] f32 TextLineHeight(f32 scale);

// Text as filled rectangles (a 5x7 placeholder font). Letters are drawn in upper case.
// `maxWidth` of 0 draws one line per '\n' and does not wrap. Returns the y just below the
// last line.
f32 DrawText(Emerald::Renderer2D& renderer, const Emerald::Vec2& position, std::string_view text,
             const Emerald::Vec4& color, f32 scale, f32 maxWidth = 0.0f);

} // namespace Ascension
