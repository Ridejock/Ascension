#include "PlaceholderText.h"

#include "Emerald/Renderer/Renderer2D.h"

namespace Ascension {

namespace {

// Five columns, seven rows. '#' is ink. Bit patterns would be shorter and harder to read.
const char* GlyphRows(char code)
{
    switch (code) {
    case 'A':
        return ".###."
               "#...#"
               "#...#"
               "#####"
               "#...#"
               "#...#"
               "#...#";
    case 'B':
        return "####."
               "#...#"
               "#...#"
               "####."
               "#...#"
               "#...#"
               "####.";
    case 'C':
        return ".###."
               "#...#"
               "#...."
               "#...."
               "#...."
               "#...#"
               ".###.";
    case 'D':
        return "####."
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               "####.";
    case 'E':
        return "#####"
               "#...."
               "#...."
               "####."
               "#...."
               "#...."
               "#####";
    case 'F':
        return "#####"
               "#...."
               "#...."
               "####."
               "#...."
               "#...."
               "#....";
    case 'G':
        return ".###."
               "#...#"
               "#...."
               "#.###"
               "#...#"
               "#...#"
               ".####";
    case 'H':
        return "#...#"
               "#...#"
               "#...#"
               "#####"
               "#...#"
               "#...#"
               "#...#";
    case 'I':
        return ".###."
               "..#.."
               "..#.."
               "..#.."
               "..#.."
               "..#.."
               ".###.";
    case 'J':
        return "..###"
               "...#."
               "...#."
               "...#."
               "#..#."
               "#..#."
               ".##..";
    case 'K':
        return "#...#"
               "#..#."
               "#.#.."
               "##..."
               "#.#.."
               "#..#."
               "#...#";
    case 'L':
        return "#...."
               "#...."
               "#...."
               "#...."
               "#...."
               "#...."
               "#####";
    case 'M':
        return "#...#"
               "##.##"
               "#.#.#"
               "#...#"
               "#...#"
               "#...#"
               "#...#";
    case 'N':
        return "#...#"
               "##..#"
               "#.#.#"
               "#..##"
               "#...#"
               "#...#"
               "#...#";
    case 'O':
        return ".###."
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               ".###.";
    case 'P':
        return "####."
               "#...#"
               "#...#"
               "####."
               "#...."
               "#...."
               "#....";
    case 'Q':
        return ".###."
               "#...#"
               "#...#"
               "#...#"
               "#.#.#"
               "#..#."
               ".##.#";
    case 'R':
        return "####."
               "#...#"
               "#...#"
               "####."
               "#.#.."
               "#..#."
               "#...#";
    case 'S':
        return ".####"
               "#...."
               "#...."
               ".###."
               "....#"
               "....#"
               "####.";
    case 'T':
        return "#####"
               "..#.."
               "..#.."
               "..#.."
               "..#.."
               "..#.."
               "..#..";
    case 'U':
        return "#...#"
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               ".###.";
    case 'V':
        return "#...#"
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               ".#.#."
               "..#..";
    case 'W':
        return "#...#"
               "#...#"
               "#...#"
               "#.#.#"
               "#.#.#"
               "#.#.#"
               ".#.#.";
    case 'X':
        return "#...#"
               "#...#"
               ".#.#."
               "..#.."
               ".#.#."
               "#...#"
               "#...#";
    case 'Y':
        return "#...#"
               "#...#"
               ".#.#."
               "..#.."
               "..#.."
               "..#.."
               "..#..";
    case 'Z':
        return "#####"
               "....#"
               "...#."
               "..#.."
               ".#..."
               "#...."
               "#####";
    case '0':
        return ".###."
               "#...#"
               "#..##"
               "#.#.#"
               "##..#"
               "#...#"
               ".###.";
    case '1':
        return "..#.."
               ".##.."
               "..#.."
               "..#.."
               "..#.."
               "..#.."
               ".###.";
    case '2':
        return ".###."
               "#...#"
               "....#"
               "...#."
               "..#.."
               ".#..."
               "#####";
    case '3':
        return ".###."
               "#...#"
               "....#"
               "..##."
               "....#"
               "#...#"
               ".###.";
    case '4':
        return "...#."
               "..##."
               ".#.#."
               "#..#."
               "#####"
               "...#."
               "...#.";
    case '5':
        return "#####"
               "#...."
               "#...."
               "####."
               "....#"
               "....#"
               "####.";
    case '6':
        return ".###."
               "#...."
               "#...."
               "####."
               "#...#"
               "#...#"
               ".###.";
    case '7':
        return "#####"
               "....#"
               "...#."
               "..#.."
               ".#..."
               ".#..."
               ".#...";
    case '8':
        return ".###."
               "#...#"
               "#...#"
               ".###."
               "#...#"
               "#...#"
               ".###.";
    case '9':
        return ".###."
               "#...#"
               "#...#"
               ".####"
               "....#"
               "....#"
               ".###.";
    case '.':
        return "....."
               "....."
               "....."
               "....."
               "....."
               "..#.."
               "..#..";
    case ',':
        return "....."
               "....."
               "....."
               "....."
               "..#.."
               "..#.."
               ".#...";
    case ':':
        return "....."
               "..#.."
               "..#.."
               "....."
               "..#.."
               "..#.."
               ".....";
    case '-':
        return "....."
               "....."
               "....."
               "#####"
               "....."
               "....."
               ".....";
    case '+':
        return "....."
               "..#.."
               "..#.."
               "#####"
               "..#.."
               "..#.."
               ".....";
    case '/':
        return "....#"
               "...#."
               "...#."
               "..#.."
               ".#..."
               ".#..."
               "#....";
    case '!':
        return "..#.."
               "..#.."
               "..#.."
               "..#.."
               "....."
               "..#.."
               ".....";
    case '?':
        return ".###."
               "#...#"
               "....#"
               "...#."
               "..#.."
               "....."
               "..#..";
    case '\'':
        return "..#.."
               "..#.."
               "....."
               "....."
               "....."
               "....."
               ".....";
    case '(':
        return "..#.."
               ".#..."
               ".#..."
               ".#..."
               ".#..."
               ".#..."
               "..#..";
    case ')':
        return ".#..."
               "..#.."
               "..#.."
               "..#.."
               "..#.."
               "..#.."
               ".#...";
    default:
        return "#####"
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               "#...#"
               "#####";
    }
}

char Upper(char code)
{
    if (code >= 'a' && code <= 'z')
        return static_cast<char>(code - 'a' + 'A');
    return code;
}

void DrawGlyph(Emerald::Renderer2D& renderer, f32 x, f32 y, char code, const Emerald::Vec4& color,
               f32 scale)
{
    if (code == ' ')
        return;
    const char* rows = GlyphRows(Upper(code));
    for (int row = 0; row < 7; ++row) {
        int col = 0;
        while (col < 5) {
            if (rows[row * 5 + col] != '#') {
                ++col;
                continue;
            }
            const int start = col;
            while (col < 5 && rows[row * 5 + col] == '#')
                ++col;
            renderer.FillRect(
                {x + static_cast<f32>(start) * scale, y + static_cast<f32>(row) * scale},
                {static_cast<f32>(col - start) * scale, scale}, color);
        }
    }
}

} // namespace

f32 TextLineHeight(f32 scale)
{
    return 9.0f * scale;
}

f32 DrawText(Emerald::Renderer2D& renderer, const Emerald::Vec2& position, std::string_view text,
             const Emerald::Vec4& color, f32 scale, f32 maxWidth)
{
    if (scale <= 0.0f)
        return position.y;
    const f32 advance = 6.0f * scale;
    const f32 line = TextLineHeight(scale);
    const f32 limit = maxWidth > 0.0f ? position.x + maxWidth : 0.0f;
    f32 x = position.x;
    f32 y = position.y;

    const auto newline = [&]() {
        x = position.x;
        y += line;
    };

    usize i = 0;
    while (i < text.size()) {
        const char code = text[i];
        if (code == '\n') {
            newline();
            ++i;
            continue;
        }
        if (code == '\r') {
            ++i;
            continue;
        }
        usize end = i;
        while (end < text.size() && text[end] != ' ' && text[end] != '\n' && text[end] != '\r')
            ++end;
        const f32 word = static_cast<f32>(end - i) * advance;
        if (limit > 0.0f && x > position.x && x + word > limit)
            newline();
        for (; i < end; ++i) {
            DrawGlyph(renderer, x, y, text[i], color, scale);
            x += advance;
        }
        if (i < text.size() && text[i] == ' ') {
            x += advance;
            ++i;
        }
    }
    return y + line;
}

} // namespace Ascension
