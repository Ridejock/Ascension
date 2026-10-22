#include "MapView.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Emerald/Renderer/DebugText.h"

namespace Ascension {

namespace {

// Palette white and steel2, for the name and the log. The grid itself uses the
// colors in the data.
constexpr u8 kLabelRed = 244;
constexpr u8 kLabelGreen = 241;
constexpr u8 kLabelBlue = 232;
constexpr u8 kLogRed = 149;
constexpr u8 kLogGreen = 155;
constexpr u8 kLogBlue = 171;

Emerald::Vec4 ByteColor(u8 red, u8 green, u8 blue)
{
    return {static_cast<f32>(red) / 255.0f, static_cast<f32>(green) / 255.0f,
            static_cast<f32>(blue) / 255.0f, 1.0f};
}

std::string Upper(std::string_view text)
{
    std::string out(text);
    for (char& code : out) {
        if (code >= 'a' && code <= 'z')
            code = static_cast<char>(code - 'a' + 'A');
        else if (code == '_')
            code = ' ';
    }
    return out;
}

std::string PlaceLabel(const WalkPlace& place)
{
    return place.MapId + "  " + std::to_string(place.Tile.X) + " " + std::to_string(place.Tile.Y);
}

void DrawCell(Emerald::Renderer2D& renderer, f32 left, f32 top, f32 size,
              const Emerald::Vec4& color)
{
    renderer.FillRect({left, top}, {size, size}, color);
}

} // namespace

void DrawMapScreen(Emerald::Renderer2D& renderer, const Database& data, const Walker& walker,
                   std::span<const std::string> log, f32 width, f32 height)
{
    const Emerald::Vec4 label = ByteColor(kLabelRed, kLabelGreen, kLabelBlue);
    const Emerald::Vec4 dim = ByteColor(kLogRed, kLogGreen, kLogBlue);
    const WalkRules& rules = data.GetWalk();
    const MapDef* map = walker.Placed() ? data.FindMap(walker.Place().MapId) : nullptr;
    if (map == nullptr || rules.TilePixels < 1) {
        Emerald::DrawDebugText(renderer, {24.0f, 24.0f}, "NO MAP", label, 3);
        return;
    }

    const f32 tile = static_cast<f32>(rules.TilePixels);
    const f32 tilesWide = width / tile;
    const f32 tilesHigh = height / tile;
    const WalkFrame frame = FrameMap(*map, walker.Place().Tile, tilesWide, tilesHigh);
    const i32 firstX = static_cast<i32>(std::floor(static_cast<double>(frame.OriginX)));
    const i32 firstY = static_cast<i32>(std::floor(static_cast<double>(frame.OriginY)));
    const i32 lastX =
        static_cast<i32>(std::ceil(static_cast<double>(frame.OriginX + frame.TilesWide)));
    const i32 lastY =
        static_cast<i32>(std::ceil(static_cast<double>(frame.OriginY + frame.TilesHigh)));
    const Emerald::Vec4 link = ByteColor(rules.LinkRed, rules.LinkGreen, rules.LinkBlue);
    const f32 inset = tile * static_cast<f32>(rules.HeroInsetPercent) / 100.0f;
    const f32 inner = tile - inset * 2.0f;

    for (i32 y = firstY; y < lastY; ++y) {
        if (y < 0 || y >= map->Height)
            continue;
        for (i32 x = firstX; x < lastX; ++x) {
            if (x < 0 || x >= map->Width)
                continue;
            const std::string* cell = map->Cell(x, y);
            if (cell == nullptr)
                continue;
            const TileDef* def = data.FindTile(*cell);
            if (def == nullptr)
                continue;
            const f32 left = (static_cast<f32>(x) - frame.OriginX) * tile;
            const f32 top = (static_cast<f32>(y) - frame.OriginY) * tile;
            DrawCell(renderer, left, top, tile, ByteColor(def->Red, def->Green, def->Blue));
            bool linked = false;
            for (const MapLink& door : map->Links) {
                if (door.Tile.X == x && door.Tile.Y == y)
                    linked = true;
            }
            if (linked && inner > 0.0f)
                DrawCell(renderer, left + inset, top + inset, inner, link);
        }
    }

    if (inner > 0.0f) {
        const MapTile hero = walker.Place().Tile;
        const f32 left = (static_cast<f32>(hero.X) - frame.OriginX) * tile + inset;
        const f32 top = (static_cast<f32>(hero.Y) - frame.OriginY) * tile + inset;
        const Emerald::Vec4 heroColor =
            ByteColor(rules.HeroRed, rules.HeroGreen, rules.HeroBlue);
        DrawCell(renderer, left, top, inner, heroColor);
    }

    Emerald::DrawDebugText(renderer, {16.0f, 16.0f}, Upper(PlaceLabel(walker.Place())), label, 2);
    const usize shown = std::min(log.size(), static_cast<usize>(4));
    const f32 lineStep = Emerald::DebugTextLineHeight(1) + 2.0f;
    f32 logY = height - 16.0f - static_cast<f32>(shown) * lineStep;
    for (usize index = 0; index < shown; ++index) {
        const std::string& line = log[log.size() - shown + index];
        Emerald::DrawDebugText(renderer, {16.0f, logY}, Upper(line), dim, 1);
        logY += lineStep;
    }
}

} // namespace Ascension
