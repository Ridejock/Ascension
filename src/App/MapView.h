#pragma once

#include <span>
#include <string>

#include "Ascension/Map/Walk.h"
#include "Emerald/Renderer/Renderer2D.h"

namespace Ascension {

// Palette rectangles for the grid, the exit, and the hero. `log` is the recent
// notes, drawn as debug text over the corner. No sheets and no fight panels.
void DrawMapScreen(Emerald::Renderer2D& renderer, const Database& data, const Walker& walker,
                   std::span<const std::string> log, f32 width, f32 height);

} // namespace Ascension
