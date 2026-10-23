#include "Ascension/Input/Prompts.h"

#include "Emerald/Input/InputBindings.h"
#include "Emerald/Renderer/DebugText.h"

namespace Ascension {

void AddHint(std::vector<HintChip>& out, std::string_view action, std::string word)
{
    HintChip chip;
    chip.Action = std::string(action);
    chip.Word = std::move(word);
    out.push_back(std::move(chip));
}

const PromptSheet* FindPromptSheet(const PromptRules& rules, std::string_view family)
{
    for (const PromptSheet& sheet : rules.Sheets) {
        if (sheet.Family == family)
            return &sheet;
    }
    return nullptr;
}

const PromptTile* FindPromptTile(const PromptSheet& sheet, std::string_view name)
{
    for (const PromptTile& tile : sheet.Tiles) {
        if (tile.Name == name)
            return &tile;
    }
    return nullptr;
}

const PromptGlyph* FindPromptGlyph(const PromptRules& rules, std::string_view family,
                                    std::string_view button)
{
    for (const PromptGlyph& glyph : rules.Glyphs) {
        if (glyph.Family == family && glyph.Button == button)
            return &glyph;
    }
    return nullptr;
}

std::string PromptFamilyForType(const PromptRules& rules, std::string_view typeName)
{
    for (const PromptDevice& device : rules.Devices) {
        for (const std::string& type : device.Types) {
            if (type == typeName)
                return device.Family;
        }
    }
    return rules.DefaultFamily;
}

PromptMark ResolvePrompt(const Emerald::Input& input, const PromptRules& rules,
                         std::string_view action, std::string_view family)
{
    PromptMark mark;
    mark.Family = std::string(family);
    if (family.empty() || family == rules.TextFamily) {
        const std::span<const Emerald::Key> keys = input.GetActionKeys(action);
        if (!keys.empty())
            mark.Label = Emerald::GetKeyLabel(keys.front());
        return mark;
    }

    const std::span<const Emerald::GamepadButton> buttons = input.GetActionButtons(action);
    if (buttons.empty())
        return mark;
    const Emerald::GamepadButton button = buttons.front();
    const std::string_view name = Emerald::GetGamepadButtonName(button);
    const char* label = input.GetGamepads().GetButtonLabel(button);
    if (label != nullptr)
        mark.Label = label;
    if (mark.Label.empty())
        mark.Label = std::string(name);

    const PromptGlyph* glyph = FindPromptGlyph(rules, family, name);
    if (glyph == nullptr)
        return mark;
    const PromptSheet* sheet = FindPromptSheet(rules, family);
    if (sheet == nullptr || FindPromptTile(*sheet, glyph->Tile) == nullptr)
        return mark;
    mark.Icon = true;
    mark.Tile = glyph->Tile;
    return mark;
}

PromptMetrics MeasurePrompts(const PromptRules& rules)
{
    PromptMetrics metrics;
    i32 tile = 0;
    if (!rules.Sheets.empty())
        tile = rules.Sheets.front().TilePixels;
    const i32 iconScale = rules.IconScale > 0 ? rules.IconScale : 0;
    const i32 textScale = rules.TextScale > 0 ? rules.TextScale : 0;
    metrics.IconScale = iconScale;
    metrics.TextScale = textScale;
    metrics.Icon = static_cast<f32>(tile * iconScale);
    metrics.Cap = static_cast<f32>(Emerald::kDebugTextRows * textScale);
    metrics.TextOffset = (metrics.Icon - metrics.Cap) * 0.5f;
    metrics.Gap = static_cast<f32>(rules.Gap);
    metrics.GroupGap = static_cast<f32>(rules.GroupGap);
    return metrics;
}

} // namespace Ascension
