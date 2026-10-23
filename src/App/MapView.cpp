#include "MapView.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>

#include "ArtGallery.h"
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

bool SpawnCleared(std::span<const std::string> cleared, std::string_view id)
{
    for (const std::string& held : cleared) {
        if (held == id)
            return true;
    }
    return false;
}

MapTile SpawnDrawTile(const MapSpawn& spawn, std::span<const SpawnPlace> places)
{
    for (const SpawnPlace& place : places) {
        if (place.Id == spawn.Id)
            return place.Tile;
    }
    return spawn.Tile;
}

// The foe drawn on a spawn tile. The encounter lists that foe first. A pack with
// no enemies, or a missing encounter, keeps the built-in square.
const std::string* SpawnFoeId(const Database& data, const MapSpawn& spawn)
{
    const EncounterDef* encounter = data.FindEncounter(spawn.Encounter);
    if (encounter == nullptr || encounter->Enemies.empty())
        return nullptr;
    return &encounter->Enemies.front();
}

} // namespace

void DrawMapScreen(Emerald::Renderer2D& renderer, const Database& data, const Walker& walker,
                   std::span<const std::string> log, f32 width, f32 height, i32 hp, i32 maxHp,
                   i32 focus, i32 maxFocus, std::span<const std::string> cleared,
                   std::span<const SpawnPlace> places, const ArtGallery* art,
                   const SpawnGlides* motion)
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
    const WalkPoint heroPoint = walker.DrawTile();
    const WalkFrame frame = FrameMap(*map, heroPoint.X, heroPoint.Y, tilesWide, tilesHigh);
    const i32 firstX = static_cast<i32>(std::floor(static_cast<double>(frame.OriginX)));
    const i32 firstY = static_cast<i32>(std::floor(static_cast<double>(frame.OriginY)));
    const i32 lastX =
        static_cast<i32>(std::ceil(static_cast<double>(frame.OriginX + frame.TilesWide)));
    const i32 lastY =
        static_cast<i32>(std::ceil(static_cast<double>(frame.OriginY + frame.TilesHigh)));
    const Emerald::Vec4 link = ByteColor(rules.LinkRed, rules.LinkGreen, rules.LinkBlue);
    const Emerald::Vec4 white{1.0f, 1.0f, 1.0f, 1.0f};
    const bool packed = art != nullptr && !art->IsEmpty();
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
            // The cell is this tile id. Neighbouring walls are not consulted, so a pack
            // cannot pick a wall piece from what the cell touches.
            ArtPlace place;
            place.Map = map->Id;
            place.X = x;
            place.Y = y;
            if (!packed || !DrawArt(renderer, art, ArtSlot::Tile, def->Id, {left, top}, {tile, tile},
                                    white, place))
                DrawCell(renderer, left, top, tile, ByteColor(def->Red, def->Green, def->Blue));
            bool linked = false;
            for (const MapLink& door : map->Links) {
                if (door.Tile.X == x && door.Tile.Y == y)
                    linked = true;
            }
            if (linked && inner > 0.0f) {
                if (!packed || !DrawArt(renderer, art, ArtSlot::ExitMarker, "",
                                        {left + inset, top + inset}, {inner, inner}, white))
                    DrawCell(renderer, left + inset, top + inset, inner, link);
            }
        }
    }

    if (inner > 0.0f) {
        const Emerald::Vec4 restColor = ByteColor(rules.RestRed, rules.RestGreen, rules.RestBlue);
        const Emerald::Vec4 talkColor = ByteColor(rules.TalkRed, rules.TalkGreen, rules.TalkBlue);
        const Emerald::Vec4 shopColor = ByteColor(rules.ShopRed, rules.ShopGreen, rules.ShopBlue);
        for (const MapPoint& point : map->Points) {
            if (point.Tile.X < firstX || point.Tile.X >= lastX || point.Tile.Y < firstY ||
                point.Tile.Y >= lastY)
                continue;
            const f32 left = (static_cast<f32>(point.Tile.X) - frame.OriginX) * tile + inset;
            const f32 top = (static_cast<f32>(point.Tile.Y) - frame.OriginY) * tile + inset;
            bool drewNpc = false;
            if (packed && point.Kind == MapPointKind::Talk)
                drewNpc = DrawArt(renderer, art, ArtSlot::NpcSprite, point.Dialogue, {left, top},
                                  {inner, inner}, white);
            else if (packed && point.Kind == MapPointKind::Shop) {
                drewNpc = DrawArt(renderer, art, ArtSlot::NpcSprite, data.GetShop().Dialogue,
                                  {left, top}, {inner, inner}, white);
                if (!drewNpc)
                    drewNpc = DrawArt(renderer, art, ArtSlot::ShopMarker, "", {left, top},
                                      {inner, inner}, white);
            } else if (packed && point.Kind == MapPointKind::Rest)
                drewNpc = DrawArt(renderer, art, ArtSlot::RestMarker, "", {left, top},
                                  {inner, inner}, white);
            if (!drewNpc) {
                const Emerald::Vec4* color = &restColor;
                if (point.Kind == MapPointKind::Talk)
                    color = &talkColor;
                else if (point.Kind == MapPointKind::Shop)
                    color = &shopColor;
                DrawCell(renderer, left, top, inner, *color);
            }
        }
    }

    if (inner > 0.0f) {
        const Emerald::Vec4 spawnColor =
            ByteColor(rules.SpawnRed, rules.SpawnGreen, rules.SpawnBlue);
        for (const MapSpawn& spawn : map->Spawns) {
            if (SpawnCleared(cleared, spawn.Id))
                continue;
            const MapTile at = SpawnDrawTile(spawn, places);
            const WalkPoint drawn =
                motion != nullptr ? motion->Sample(spawn.Id, at)
                                  : WalkPoint{static_cast<f32>(at.X), static_cast<f32>(at.Y)};
            if (drawn.X + 1.0f < static_cast<f32>(firstX) || drawn.X >= static_cast<f32>(lastX) ||
                drawn.Y + 1.0f < static_cast<f32>(firstY) || drawn.Y >= static_cast<f32>(lastY))
                continue;
            const f32 left = (drawn.X - frame.OriginX) * tile + inset;
            const f32 top = (drawn.Y - frame.OriginY) * tile + inset;
            const std::string* foeId = packed ? SpawnFoeId(data, spawn) : nullptr;
            if (foeId == nullptr ||
                !DrawArt(renderer, art, ArtSlot::FoeOverland, *foeId, {left, top}, {inner, inner},
                         white))
                DrawCell(renderer, left, top, inner, spawnColor);
        }
    }

    if (inner > 0.0f) {
        const f32 left = (heroPoint.X - frame.OriginX) * tile + inset;
        const f32 top = (heroPoint.Y - frame.OriginY) * tile + inset;
        const Emerald::Vec4 heroColor =
            ByteColor(rules.HeroRed, rules.HeroGreen, rules.HeroBlue);
        if (!packed || !DrawArt(renderer, art, ArtSlot::HeroMarker, "", {left, top}, {inner, inner},
                                white))
            DrawCell(renderer, left, top, inner, heroColor);
    }

    Emerald::DrawDebugText(renderer, {16.0f, 16.0f}, Upper(PlaceLabel(walker.Place())), label, 2);
    const std::string pools = "HP " + std::to_string(hp) + "/" + std::to_string(maxHp) +
                              "   FOCUS " + std::to_string(focus) + "/" + std::to_string(maxFocus);
    const f32 poolsY = 16.0f + Emerald::DebugTextLineHeight(2) + 4.0f;
    Emerald::DrawDebugText(renderer, {16.0f, poolsY}, Upper(pools), label, 2);
    const MapTile heroTile = walker.Place().Tile;
    const char* prompt = nullptr;
    for (const MapPoint& point : map->Points) {
        if (point.Tile.X != heroTile.X || point.Tile.Y != heroTile.Y)
            continue;
        if (point.Kind == MapPointKind::Talk)
            prompt = "I TALK";
        else if (point.Kind == MapPointKind::Shop)
            prompt = "I SHOP";
    }
    if (prompt != nullptr) {
        const f32 promptY = poolsY + Emerald::DebugTextLineHeight(2) + 4.0f;
        Emerald::DrawDebugText(renderer, {16.0f, promptY}, prompt, label, 2);
    }
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
