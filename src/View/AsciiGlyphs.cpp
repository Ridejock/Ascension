#include "Ascension/View/AsciiGlyphs.h"

#include <cstring>

#include "Emerald/Renderer/Sprite.h"
#include "Emerald/Renderer/Texture.h"

namespace Ascension {

namespace {

// One baked character. '#' is ink, '.' is empty. Eight rows, eight columns.
// The shapes are the font. data/ascii.json only names which character an id uses.
struct GlyphPattern {
    char Code;
    const char* Rows[8];
};

constexpr GlyphPattern kGlyphs[] = {
    {'?',
     {"..####..", ".##..##.", ".....##.", "....#...", "....#...", "........", "....#...",
      "........"}},
    {'@',
     {"..####..", ".##..##.", ".##.##.#", ".##.#.#.", ".##.###.", ".##.....", "..####..",
      "........"}},
    {'#',
     {"..#..#..", ".######.", "..#..#..", ".######.", "..#..#..", "........", "........",
      "........"}},
    {'~',
     {"........", "........", ".##..##.", "#..##..#", "........", "........", "........",
      "........"}},
    {'.',
     {"........", "........", "........", "........", "........", "...##...", "...##...",
      "........"}},
    {'"',
     {".#...#..", ".#...#..", ".#...#..", "........", "........", "........", "........",
      "........"}},
    {'+',
     {"...#....", "...#....", ".#####..", "...#....", "...#....", "........", "........",
      "........"}},
    {'=',
     {"........", "........", ".#####..", "........", ".#####..", "........", "........",
      "........"}},
    {'%',
     {"##...##.", "##..##..", "...##...", "..##....", ".##..##.", "##...##.", "........",
      "........"}},
    {'>',
     {"........", ".##.....", "..##....", "...##...", "..##....", ".##.....", "........",
      "........"}},
    {'&',
     {"..##....", ".#..#...", ".#.#....", "..#.....", ".#.#.#..", "#..#.#..", ".##.##..",
      "........"}},
    {'$',
     {"...#....", "..####..", ".#.#....", "..###...", "...#.#..", ".#####..", "...#....",
      "........"}},
    {'*',
     {"...#....", ".#.#.#..", "..###...", ".#####..", "..###...", "...#....", "...#....",
      "........"}},
    {'o',
     {"........", "..###...", ".#...#..", ".#...#..", ".#...#..", "..###...", "........",
      "........"}},
    {'^',
     {"...#....", "..#.#...", ".#...#..", "#.....#.", "........", "........", "........",
      "........"}},
    {'A',
     {"..###...", ".#...#..", ".#...#..", ".#####..", ".#...#..", ".#...#..", ".#...#..",
      "........"}},
    {'B',
     {".####...", ".#...#..", ".#...#..", ".####...", ".#...#..", ".#...#..", ".####...",
      "........"}},
    {'C',
     {"..###...", ".#...#..", ".#......", ".#......", ".#......", ".#...#..", "..###...",
      "........"}},
    {'E',
     {".#####..", ".#......", ".#......", ".####...", ".#......", ".#......", ".#####..",
      "........"}},
    {'F',
     {".#####..", ".#......", ".#......", ".####...", ".#......", ".#......", ".#......",
      "........"}},
    {'G',
     {"..###...", ".#...#..", ".#......", ".#.###..", ".#...#..", ".#...#..", "..###...",
      "........"}},
    {'K',
     {".#...#..", ".#..#...", ".#.#....", ".##.....", ".#.#....", ".#..#...", ".#...#..",
      "........"}},
    {'M',
     {".#...#..", ".##.##..", ".#.#.#..", ".#...#..", ".#...#..", ".#...#..", ".#...#..",
      "........"}},
    {'N',
     {".#...#..", ".##..#..", ".#.#.#..", ".#..##..", ".#...#..", ".#...#..", ".#...#..",
      "........"}},
    {'O',
     {"..###...", ".#...#..", ".#...#..", ".#...#..", ".#...#..", ".#...#..", "..###...",
      "........"}},
    {'P',
     {".####...", ".#...#..", ".#...#..", ".####...", ".#......", ".#......", ".#......",
      "........"}},
    {'R',
     {".####...", ".#...#..", ".#...#..", ".####...", ".#.#....", ".#..#...", ".#...#..",
      "........"}},
    {'S',
     {"..####..", ".#......", ".#......", "..###...", ".....#..", ".....#..", ".####...",
      "........"}},
    {'T',
     {".#####..", "...#....", "...#....", "...#....", "...#....", "...#....", "...#....",
      "........"}},
    {'V',
     {".#...#..", ".#...#..", ".#...#..", ".#...#..", ".#...#..", "..#.#...", "...#....",
      "........"}},
    {'W',
     {".#...#..", ".#...#..", ".#...#..", ".#.#.#..", ".#.#.#..", ".##.##..", ".#...#..",
      "........"}},
    {'a',
     {"........", "........", "..###...", ".....#..", "..####..", ".#...#..", "..####..",
      "........"}},
    {'b',
     {".#......", ".#......", ".####...", ".#...#..", ".#...#..", ".#...#..", ".####...",
      "........"}},
    {'c',
     {"........", "........", "..###...", ".#...#..", ".#......", ".#...#..", "..###...",
      "........"}},
    {'d',
     {".....#..", ".....#..", "..####..", ".#...#..", ".#...#..", ".#...#..", "..####..",
      "........"}},
    {'h',
     {".#......", ".#......", ".####...", ".#...#..", ".#...#..", ".#...#..", ".#...#..",
      "........"}},
    {'i',
     {"...#....", "........", "..##....", "...#....", "...#....", "...#....", "..###...",
      "........"}},
    {'m',
     {"........", "........", ".#.#.#..", ".##.##..", ".#.#.#..", ".#...#..", ".#...#..",
      "........"}},
    {'p',
     {"........", "........", ".####...", ".#...#..", ".####...", ".#......", ".#......",
      "........"}},
    {'s',
     {"........", "........", "..####..", ".#......", "..###...", ".....#..", ".####...",
      "........"}},
    {'w',
     {"........", "........", ".#...#..", ".#...#..", ".#.#.#..", ".##.##..", ".#...#..",
      "........"}},
    {'x',
     {"........", "........", ".#...#..", "..#.#...", "...#....", "..#.#...", ".#...#..",
      "........"}},
};

constexpr i32 kGlyphCount = static_cast<i32>(sizeof(kGlyphs) / sizeof(kGlyphs[0]));

i32 FindCode(char code)
{
    for (i32 index = 0; index < kGlyphCount; ++index) {
        if (kGlyphs[index].Code == code)
            return index;
    }
    return -1;
}

void WritePixel(Emerald::Image& image, i32 x, i32 y, bool ink)
{
    const usize offset =
        (static_cast<usize>(y) * static_cast<usize>(image.Width) + static_cast<usize>(x)) * 4;
    const u8 value = ink ? 255 : 0;
    image.Pixels[offset] = value;
    image.Pixels[offset + 1] = value;
    image.Pixels[offset + 2] = value;
    image.Pixels[offset + 3] = value;
}

} // namespace

bool AsciiGlyphsWellFormed()
{
    if (FindCode(kAsciiFallback) < 0 || FindCode(kAsciiHero) < 0 || FindCode('#') < 0 ||
        FindCode('~') < 0)
        return false;
    for (i32 index = 0; index < kGlyphCount; ++index) {
        if (FindCode(kGlyphs[index].Code) != index)
            return false;
        for (const char* row : kGlyphs[index].Rows) {
            if (row == nullptr || std::strlen(row) != 8)
                return false;
            for (i32 column = 0; column < 8; ++column) {
                if (row[column] != '#' && row[column] != '.')
                    return false;
            }
        }
    }
    return true;
}

bool AsciiGlyphKnown(char code)
{
    return FindCode(code) >= 0;
}

i32 AsciiGlyphIndex(char code)
{
    const i32 found = FindCode(code);
    if (found >= 0)
        return found;
    const i32 fallback = FindCode(kAsciiFallback);
    return fallback >= 0 ? fallback : 0;
}

AsciiAtlas RasterizeAsciiAtlas()
{
    AsciiAtlas atlas;
    atlas.Count = kGlyphCount;
    atlas.Columns = kAsciiAtlasColumns;
    const i32 rows = (kGlyphCount + kAsciiAtlasColumns - 1) / kAsciiAtlasColumns;
    atlas.Image.Width = kAsciiAtlasColumns * kAsciiGlyphPixels;
    atlas.Image.Height = rows * kAsciiGlyphPixels;
    atlas.Image.Pixels.assign(
        static_cast<usize>(atlas.Image.Width) * static_cast<usize>(atlas.Image.Height) * 4, 0);
    for (i32 index = 0; index < kGlyphCount; ++index) {
        const i32 cellX = (index % kAsciiAtlasColumns) * kAsciiGlyphPixels;
        const i32 cellY = (index / kAsciiAtlasColumns) * kAsciiGlyphPixels;
        for (i32 y = 0; y < kAsciiGlyphPixels; ++y) {
            const char* row = kGlyphs[index].Rows[y];
            for (i32 x = 0; x < kAsciiGlyphPixels; ++x) {
                const bool ink = row != nullptr && std::strlen(row) == 8 && row[x] == '#';
                WritePixel(atlas.Image, cellX + x, cellY + y, ink);
            }
        }
    }
    return atlas;
}

char AsciiCellGlyph(const AsciiRules& rules, const MapDef& map, i32 x, i32 y, bool hero, bool foe,
                    std::string_view foeId)
{
    char glyph = kAsciiFallback;
    if (const std::string* ground = map.Cell(x, y))
        glyph = FindAsciiGlyph(rules.Tiles, *ground, kAsciiFallback);
    for (const MapProp& prop : map.Props) {
        if (MapPropBlocks(prop, x, y))
            glyph = FindAsciiGlyph(rules.Props, prop.Id, kAsciiFallback);
    }
    for (const MapLink& link : map.Links) {
        if (link.Tile.X == x && link.Tile.Y == y)
            glyph = rules.Exit;
    }
    for (const MapPoint& point : map.Points) {
        if (point.Tile.X != x || point.Tile.Y != y)
            continue;
        if (point.Kind == MapPointKind::Talk)
            glyph = rules.Talk;
        else if (point.Kind == MapPointKind::Shop)
            glyph = rules.Shop;
        else
            glyph = rules.Rest;
    }
    if (foe)
        glyph = foeId.empty() ? kAsciiFallback : FindAsciiGlyph(rules.Foes, foeId, kAsciiFallback);
    if (hero)
        glyph = kAsciiHero;
    return glyph;
}

void DrawAsciiGlyph(Emerald::Renderer2D& renderer, const Emerald::Texture& atlas, char code,
                    const Emerald::Vec2& topLeft, const Emerald::Vec2& size,
                    const Emerald::Vec4& tint)
{
    if (size.x <= 0.0f || size.y <= 0.0f || !atlas.HasGpuTexture())
        return;
    const i32 index = AsciiGlyphIndex(code);
    const f32 cell = static_cast<f32>(kAsciiGlyphPixels);
    const f32 column = static_cast<f32>(index % kAsciiAtlasColumns) * cell;
    const f32 row = static_cast<f32>(index / kAsciiAtlasColumns) * cell;
    Emerald::Sprite sprite;
    sprite.Source = &atlas;
    sprite.Region = {{column, row}, {cell, cell}};
    Emerald::SpriteOptions options;
    options.Size = size;
    options.Origin = {0.0f, 0.0f};
    options.PixelSnap = true;
    options.Tint = tint;
    renderer.DrawSprite(sprite, topLeft, options);
}

} // namespace Ascension
