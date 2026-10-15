#include "AscensionApp.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

#include <SDL3/SDL.h>

#include "Emerald/Core/Paths.h"
#include "Emerald/Math/Mat4.h"
#include "PlaceholderText.h"

namespace Ascension {

namespace {

// The source tree's data/ while it is still on disk (a dev build). A packaged
// build's recorded path does not exist, so it uses the copy beside the executable.
std::filesystem::path DataDirectory()
{
#ifdef ASCENSION_DATA_DIR
    const std::filesystem::path source = ASCENSION_DATA_DIR;
    std::error_code error;
    if (std::filesystem::is_directory(source, error))
        return source;
#endif
    return Emerald::Paths::GetBasePath() / "data";
}

constexpr Emerald::Vec4 kInk{0.93f, 0.91f, 0.84f, 1.0f};
constexpr Emerald::Vec4 kDim{0.62f, 0.66f, 0.60f, 1.0f};
constexpr Emerald::Vec4 kFrame{0.72f, 0.64f, 0.38f, 1.0f};
constexpr Emerald::Vec4 kPanel{0.10f, 0.12f, 0.11f, 1.0f};
constexpr Emerald::Vec4 kError{0.95f, 0.45f, 0.38f, 1.0f};
constexpr Emerald::Vec4 kFigure{0.78f, 0.62f, 0.32f, 1.0f};

constexpr f32 kPollSeconds = 0.25f;

Emerald::Vec4 StatColor(std::string_view id)
{
    if (id == "vitality")
        return {0.78f, 0.32f, 0.30f, 1.0f};
    if (id == "strength")
        return {0.86f, 0.55f, 0.26f, 1.0f};
    if (id == "defense")
        return {0.36f, 0.55f, 0.82f, 1.0f};
    if (id == "agility")
        return {0.38f, 0.74f, 0.46f, 1.0f};
    return {0.66f, 0.46f, 0.84f, 1.0f};
}

void DrawFigure(Emerald::Renderer2D& renderer, const Emerald::Vec2& origin, f32 size,
                const Emerald::Vec4& color)
{
    const f32 s = size;
    renderer.FillRect(origin + Emerald::Vec2{s * 1.15f, 0.0f}, {s * 1.7f, s * 1.5f}, color);
    renderer.FillRect(origin + Emerald::Vec2{s * 0.7f, s * 1.7f}, {s * 2.6f, s * 2.5f}, color);
    renderer.FillRect(origin + Emerald::Vec2{0.0f, s * 1.9f}, {s * 0.6f, s * 1.9f}, color);
    renderer.FillRect(origin + Emerald::Vec2{s * 3.4f, s * 1.9f}, {s * 0.6f, s * 1.9f}, color);
    renderer.FillRect(origin + Emerald::Vec2{s * 0.9f, s * 4.4f}, {s * 0.8f, s * 2.1f}, color);
    renderer.FillRect(origin + Emerald::Vec2{s * 2.3f, s * 4.4f}, {s * 0.8f, s * 2.1f}, color);
}

void DrawPanel(Emerald::Renderer2D& renderer, const Emerald::Vec2& origin,
               const Emerald::Vec2& size)
{
    renderer.FillRect(origin, size, kPanel);
    renderer.DrawRect(origin, size, kFrame);
}

void DrawStatRow(Emerald::Renderer2D& renderer, const Emerald::Vec2& origin, std::string_view name,
                 i32 value, const Emerald::Vec4& color)
{
    std::string label(name);
    if (label.size() < 10)
        label.append(10 - label.size(), ' ');
    label += std::to_string(value);
    DrawText(renderer, origin, label, kInk, 2.0f);
    const Emerald::Vec2 bar{origin.x + 200.0f, origin.y + 2.0f};
    constexpr f32 kBarWidth = 220.0f;
    constexpr f32 kBarHeight = 12.0f;
    renderer.DrawRect(bar, {kBarWidth, kBarHeight}, color);
    const f32 filled = std::clamp(static_cast<f32>(value) / 40.0f, 0.0f, 1.0f) * (kBarWidth - 2.0f);
    if (filled > 0.0f)
        renderer.FillRect(bar + Emerald::Vec2{1.0f, 1.0f}, {filled, kBarHeight - 2.0f}, color);
}

} // namespace

AscensionApp::AscensionApp(const Emerald::ApplicationSpec& spec)
    : Emerald::Application(spec), m_Data(DataDirectory())
{
}

void AscensionApp::OnStart()
{
    m_Data.Load();
}

void AscensionApp::OnEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
        event.key.scancode == SDL_SCANCODE_ESCAPE)
        Quit();
}

void AscensionApp::OnUpdate(f32 dt)
{
    m_ReloadWait += dt;
    if (m_ReloadWait < kPollSeconds)
        return;
    m_ReloadWait = 0.0f;
    DataError error;
    m_Data.CheckForChanges(&error);
}

void AscensionApp::OnRender2D(Emerald::Renderer2D& renderer)
{
    const Emerald::Vec2i pixels = GetWindowSizeInPixels();
    if (pixels.x <= 0 || pixels.y <= 0)
        return;
    const f32 width = static_cast<f32>(pixels.x);
    const f32 height = static_cast<f32>(pixels.y);

    renderer.Begin(Emerald::Mat4::OrthoPixelSpace(width, height));

    const Emerald::Vec2 left{28.0f, 24.0f};
    const Emerald::Vec2 leftSize{760.0f, height - 92.0f};
    const Emerald::Vec2 right{808.0f, 24.0f};
    const Emerald::Vec2 rightSize{width - 836.0f, height - 92.0f};
    DrawPanel(renderer, left, leftSize);
    DrawPanel(renderer, right, rightSize);

    const ClassDef* fighter = m_Data.FindClass("fighter");
    if (!m_Data.IsLoaded() || fighter == nullptr) {
        DrawText(renderer, {left.x + 24.0f, left.y + 28.0f}, "Ascension", kFrame, 3.0f);
        const std::string message =
            m_Data.GetLastError().empty() ? "Could not load data/." : m_Data.GetLastError();
        DrawText(renderer, {left.x + 24.0f, left.y + 80.0f}, message, kError, 2.0f,
                 leftSize.x - 48.0f);
        renderer.End();
        return;
    }

    DrawFigure(renderer, {left.x + 36.0f, left.y + 28.0f}, 18.0f, kFigure);
    f32 y = left.y + 28.0f;
    DrawText(renderer, {left.x + 150.0f, y}, "Ascension", kFrame, 3.0f);
    y += TextLineHeight(3.0f) + 4.0f;
    DrawText(renderer, {left.x + 150.0f, y}, "Placeholder", kDim, 2.0f);
    y = left.y + 168.0f;
    DrawText(renderer, {left.x + 28.0f, y}, fighter->Name, kInk, 3.0f);
    y += TextLineHeight(3.0f) + 6.0f;
    y = DrawText(renderer, {left.x + 28.0f, y}, fighter->Description, kDim, 2.0f,
                 leftSize.x - 56.0f);
    y += 8.0f;
    DrawText(renderer, {left.x + 28.0f, y}, "Level 1", kInk, 2.0f);
    y += TextLineHeight(2.0f) + 10.0f;

    const StatBlock stats = m_Data.StatsAt(*fighter, 1);
    for (const StatDef& stat : m_Data.GetStats()) {
        DrawStatRow(renderer, {left.x + 28.0f, y}, stat.Name, stats.Get(stat.Id),
                    StatColor(stat.Id));
        y += TextLineHeight(2.0f) + 6.0f;
    }

    y += 8.0f;
    DrawText(renderer, {left.x + 28.0f, y}, "Skills", kFrame, 2.0f);
    y += TextLineHeight(2.0f) + 4.0f;
    for (const SkillDef* skill : m_Data.ActiveSkills(*fighter)) {
        DrawText(renderer, {left.x + 28.0f, y}, skill->Name, kInk, 2.0f);
        y += TextLineHeight(2.0f) + 2.0f;
    }

    y = right.y + 24.0f;
    DrawText(renderer, {right.x + 20.0f, y}, "Evolutions at level 5", kFrame, 2.0f);
    y += TextLineHeight(2.0f) + 12.0f;
    for (const ClassDef* evolution : m_Data.EvolutionsOf(fighter->Id)) {
        DrawText(renderer, {right.x + 20.0f, y}, evolution->Name, kInk, 2.0f);
        y += TextLineHeight(2.0f) + 4.0f;
        const StatBlock atLevel = m_Data.StatsAt(*evolution, evolution->EvolveLevel);
        std::string line = "Level " + std::to_string(evolution->EvolveLevel);
        line += "   Vit " + std::to_string(atLevel.Vitality);
        line += "  Str " + std::to_string(atLevel.Strength);
        line += "  Def " + std::to_string(atLevel.Defense);
        y = DrawText(renderer, {right.x + 20.0f, y}, line, kDim, 1.0f, rightSize.x - 40.0f);
        std::string added = "Adds";
        for (const SkillDef* skill : m_Data.ActiveSkills(*evolution)) {
            bool onParent = false;
            for (const SkillDef* parentSkill : m_Data.ActiveSkills(*fighter)) {
                if (parentSkill->Id == skill->Id)
                    onParent = true;
            }
            if (!onParent)
                added += "  " + skill->Name;
        }
        for (const SkillDef* skill : m_Data.Passives(*evolution))
            added += "  [" + skill->Name + "]";
        y = DrawText(renderer, {right.x + 20.0f, y}, added, kInk, 1.0f, rightSize.x - 40.0f);
        y += 10.0f;
    }

    y += 6.0f;
    std::string loaded = std::to_string(m_Data.GetEnemies().size()) + " enemies, " +
                         std::to_string(m_Data.GetItems().size()) + " items";
    DrawText(renderer, {right.x + 20.0f, y}, loaded, kDim, 2.0f);
    y += TextLineHeight(2.0f) + 8.0f;
    for (const EnemyDef& enemy : m_Data.GetEnemies()) {
        DrawText(renderer, {right.x + 20.0f, y}, enemy.Name, kInk, 2.0f);
        y += TextLineHeight(2.0f) + 2.0f;
    }
    y += 6.0f;
    for (const ItemDef& item : m_Data.GetItems()) {
        const std::string row = item.Name + "  (" + ToString(item.Slot) + ")";
        DrawText(renderer, {right.x + 20.0f, y}, row, kDim, 1.0f);
        y += TextLineHeight(1.0f) + 2.0f;
    }

    const std::string revision =
        "Data rev " + std::to_string(m_Data.GetRevision()) + "    edit data/*.json to update";
    DrawText(renderer, {left.x + 28.0f, height - 72.0f}, revision, kDim, 1.0f, width - 80.0f);

    if (!m_Data.GetLastError().empty()) {
        renderer.FillRect({16.0f, height - 40.0f}, {width - 32.0f, 28.0f},
                          {0.35f, 0.10f, 0.10f, 1.0f});
        DrawText(renderer, {24.0f, height - 34.0f}, m_Data.GetLastError(), kError, 1.0f,
                 width - 48.0f);
    }

    renderer.End();
}

} // namespace Ascension
