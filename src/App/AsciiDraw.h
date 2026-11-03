#pragma once

#include "Emerald/Renderer/DebugText.h"
#include "Emerald/Renderer/Renderer2D.h"

namespace Ascension {

// The largest debug-font scale that still fits in the box, at most `requested`.
// A non-positive request, or a box too small for one pixel of the font, is 1.
[[nodiscard]] inline i32 FitAsciiScale(i32 requested, f32 width, f32 height)
{
    i32 scale = requested > 0 ? requested : 1;
    if (width <= 0.0f || height <= 0.0f)
        return 1;
    while (scale > 1) {
        const f32 wide = static_cast<f32>(Emerald::kDebugTextAdvance) * static_cast<f32>(scale);
        const f32 high = static_cast<f32>(Emerald::kDebugTextRows) * static_cast<f32>(scale);
        if (wide <= width && high <= height)
            break;
        --scale;
    }
    return scale;
}

// One character, centered in `size`. The font uppercases letters. A zero glyph
// draws nothing.
inline void DrawAsciiGlyph(Emerald::Renderer2D& renderer, const Emerald::Vec2& origin,
                           const Emerald::Vec2& size, char glyph, const Emerald::Vec4& color,
                           i32 scale)
{
    if (glyph == 0 || size.x <= 0.0f || size.y <= 0.0f)
        return;
    const i32 fitted = FitAsciiScale(scale, size.x, size.y);
    if (fitted < 1)
        return;
    const f32 wide = static_cast<f32>(Emerald::kDebugTextAdvance) * static_cast<f32>(fitted);
    const f32 high = static_cast<f32>(Emerald::kDebugTextRows) * static_cast<f32>(fitted);
    const f32 x = origin.x + (size.x - wide) * 0.5f;
    const f32 y = origin.y + (size.y - high) * 0.5f;
    const char text[2] = {glyph, '\0'};
    Emerald::DrawDebugText(renderer, {x, y}, text, color, fitted);
}

} // namespace Ascension
