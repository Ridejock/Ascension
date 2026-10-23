#pragma once

#include <span>
#include <string>

#include "Ascension/Map/Walk.h"
#include "Emerald/Renderer/Renderer2D.h"

namespace Ascension {

class ArtGallery;

// Palette rectangles for the grid, the exit, and the hero. `log` is the recent
// notes, drawn as debug text over the corner. The four pool numbers are the hero's
// carried HP and focus, drawn under the map name. No fight panels.
// `cleared` is the spawn ids a win already removed. Those tiles are not drawn.
// `art` is the optional pack. A null or empty gallery keeps the built-in CC0 squares.
// A tile uses the picture for this map, else its theme, else the plain tile id.
// `motion` is where spawns are drawn. Null stands each spawn on its tile.
void DrawMapScreen(Emerald::Renderer2D& renderer, const Database& data, const Walker& walker,
                   std::span<const std::string> log, f32 width, f32 height, i32 hp, i32 maxHp,
                   i32 focus, i32 maxFocus, std::span<const std::string> cleared,
                   std::span<const SpawnPlace> places = {}, const ArtGallery* art = nullptr,
                   const SpawnGlides* motion = nullptr);

} // namespace Ascension
