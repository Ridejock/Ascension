#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Emerald/Input/Input.h"

namespace Ascension {

// One tile on a prompt sheet, in the starter atlas (x, y, w, h in pixels).
struct PromptTile {
    std::string Name;
    i32 X = 0;
    i32 Y = 0;
    i32 Width = 0;
    i32 Height = 0;
};

// One device family's sheet. Image is the PNG. Tiles come from the atlas beside it.
struct PromptSheet {
    std::string Family;
    std::filesystem::path Image;
    i32 TilePixels = 0;
    std::vector<PromptTile> Tiles;
};

// Emerald pad-type names (GetGamepadTypeName) that use one sheet.
struct PromptDevice {
    std::string Family;
    std::vector<std::string> Types;
};

// The tile for one Emerald button on one family. An action prompt looks up the button
// that action is bound to now, then this row. Keyboard is not in the list.
struct PromptGlyph {
    std::string Family;
    std::string Button;
    std::string Tile;
};

// iconScale and textScale are the integers prompts.json names. The icon is
// TilePixels * IconScale. Hint text is the debug font at TextScale, whose cap is
// Emerald's 7 rows times TextScale, centered in the icon box.
struct PromptRules {
    i32 IconScale = 1;
    i32 TextScale = 1;
    i32 Gap = 0;
    i32 GroupGap = 0;
    std::string TextFamily;
    std::string DefaultFamily;
    std::vector<PromptDevice> Devices;
    std::vector<PromptSheet> Sheets;
    std::vector<PromptGlyph> Glyphs;
};

// One piece of a hint row. Action empty is words only. Word empty is a glyph with no caption.
struct HintChip {
    std::string Action;
    std::string Word;
};

void AddHint(std::vector<HintChip>& out, std::string_view action, std::string word);

// What one action shows on one device family. Icon is false for the text family and
// for a button the map does not name; Label is the short word either way.
struct PromptMark {
    bool Icon = false;
    std::string Family;
    std::string Tile;
    std::string Label;
};

// Drawn size of one hint row. TextOffset is how far below the icon's top the cap starts
// so the cap sits in the middle of the icon. Cap is the debug-font cap at TextScale.
struct PromptMetrics {
    f32 Icon = 0.0f;
    f32 Cap = 0.0f;
    f32 TextOffset = 0.0f;
    i32 TextScale = 1;
    i32 IconScale = 1;
    f32 Gap = 0.0f;
    f32 GroupGap = 0.0f;
};

[[nodiscard]] const PromptSheet* FindPromptSheet(const PromptRules& rules, std::string_view family);
[[nodiscard]] const PromptTile* FindPromptTile(const PromptSheet& sheet, std::string_view name);
[[nodiscard]] const PromptGlyph* FindPromptGlyph(const PromptRules& rules, std::string_view family,
                                                  std::string_view button);

// Family name for a pad type, or DefaultFamily when the type is not listed.
[[nodiscard]] std::string PromptFamilyForType(const PromptRules& rules, std::string_view typeName);

// The mark for `action` on `family`, from that action's current binding.
[[nodiscard]] PromptMark ResolvePrompt(const Emerald::Input& input, const PromptRules& rules,
                                       std::string_view action, std::string_view family);

[[nodiscard]] PromptMetrics MeasurePrompts(const PromptRules& rules);

} // namespace Ascension
