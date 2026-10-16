#include "AscensionApp.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

#include <SDL3/SDL.h>

#include "Emerald/Core/Paths.h"
#include "Emerald/Math/Mat4.h"
#include "Emerald/Renderer/DebugText.h"

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

// The debug font draws letters as uppercase. Data names stay mixed case on disk.
std::string Upper(std::string_view text)
{
    std::string out(text);
    for (char& code : out) {
        if (code >= 'a' && code <= 'z')
            code = static_cast<char>(code - 'a' + 'A');
    }
    return out;
}

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
    std::string label = Upper(name);
    if (label.size() < 10)
        label.append(10 - label.size(), ' ');
    label += std::to_string(value);
    Emerald::DrawDebugText(renderer, origin, label, kInk, 2);
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
        Emerald::DrawDebugText(renderer, {left.x + 24.0f, left.y + 28.0f}, "ASCENSION", kFrame, 3);
        const std::string message =
            Upper(m_Data.GetLastError().empty() ? "Could not load data/." : m_Data.GetLastError());
        Emerald::DrawDebugText(renderer, {left.x + 24.0f, left.y + 80.0f}, message, kError, 2,
                               leftSize.x - 48.0f);
        renderer.End();
        return;
    }

    DrawFigure(renderer, {left.x + 36.0f, left.y + 28.0f}, 18.0f, kFigure);
    f32 y = left.y + 28.0f;
    Emerald::DrawDebugText(renderer, {left.x + 150.0f, y}, "ASCENSION", kFrame, 3);
    y += Emerald::DebugTextLineHeight(3) + 4.0f;
    Emerald::DrawDebugText(renderer, {left.x + 150.0f, y}, "PLACEHOLDER", kDim, 2);
    y = left.y + 168.0f;
    Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(fighter->Name), kInk, 3);
    y += Emerald::DebugTextLineHeight(3) + 6.0f;
    y = Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(fighter->Description), kDim, 2,
                               leftSize.x - 56.0f);
    y += 8.0f;
    Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, "LEVEL 1", kInk, 2);
    y += Emerald::DebugTextLineHeight(2) + 10.0f;

    const StatBlock stats = m_Data.StatsAt(*fighter, 1);
    for (const StatDef& stat : m_Data.GetStats()) {
        DrawStatRow(renderer, {left.x + 28.0f, y}, stat.Name, stats.Get(stat.Id),
                    StatColor(stat.Id));
        y += Emerald::DebugTextLineHeight(2) + 6.0f;
    }

    y += 8.0f;
    Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, "SKILLS", kFrame, 2);
    y += Emerald::DebugTextLineHeight(2) + 4.0f;
    for (const SkillDef* skill : m_Data.ActiveSkills(*fighter)) {
        Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(skill->Name), kInk, 2);
        y += Emerald::DebugTextLineHeight(2) + 2.0f;
    }

    y = right.y + 24.0f;
    Emerald::DrawDebugText(renderer, {right.x + 20.0f, y}, "EVOLUTIONS AT LEVEL 5", kFrame, 2);
    y += Emerald::DebugTextLineHeight(2) + 12.0f;
    for (const ClassDef* evolution : m_Data.EvolutionsOf(fighter->Id)) {
        Emerald::DrawDebugText(renderer, {right.x + 20.0f, y}, Upper(evolution->Name), kInk, 2);
        y += Emerald::DebugTextLineHeight(2) + 4.0f;
        const StatBlock atLevel = m_Data.StatsAt(*evolution, evolution->EvolveLevel);
        std::string line = "LEVEL " + std::to_string(evolution->EvolveLevel);
        line += "   VIT " + std::to_string(atLevel.Vitality);
        line += "  STR " + std::to_string(atLevel.Strength);
        line += "  DEF " + std::to_string(atLevel.Defense);
        y = Emerald::DrawDebugText(renderer, {right.x + 20.0f, y}, line, kDim, 1,
                                   rightSize.x - 40.0f);
        std::string added = "ADDS";
        for (const SkillDef* skill : m_Data.ActiveSkills(*evolution)) {
            bool onParent = false;
            for (const SkillDef* parentSkill : m_Data.ActiveSkills(*fighter)) {
                if (parentSkill->Id == skill->Id)
                    onParent = true;
            }
            if (!onParent)
                added += "  " + Upper(skill->Name);
        }
        for (const SkillDef* skill : m_Data.Passives(*evolution))
            added += "  (" + Upper(skill->Name) + ")";
        y = Emerald::DrawDebugText(renderer, {right.x + 20.0f, y}, added, kInk, 1,
                                   rightSize.x - 40.0f);
        y += 10.0f;
    }

    y += 6.0f;
    std::string loaded = std::to_string(m_Data.GetEnemies().size()) + " ENEMIES, " +
                         std::to_string(m_Data.GetItems().size()) + " ITEMS";
    Emerald::DrawDebugText(renderer, {right.x + 20.0f, y}, loaded, kDim, 2);
    y += Emerald::DebugTextLineHeight(2) + 8.0f;
    for (const EnemyDef& enemy : m_Data.GetEnemies()) {
        Emerald::DrawDebugText(renderer, {right.x + 20.0f, y}, Upper(enemy.Name), kInk, 2);
        y += Emerald::DebugTextLineHeight(2) + 2.0f;
    }
    y += 6.0f;
    for (const ItemDef& item : m_Data.GetItems()) {
        const std::string row = Upper(item.Name) + "  (" + Upper(ToString(item.Slot)) + ")";
        Emerald::DrawDebugText(renderer, {right.x + 20.0f, y}, row, kDim, 1);
        y += Emerald::DebugTextLineHeight(1) + 2.0f;
    }

    const std::string revision =
        "DATA REV " + std::to_string(m_Data.GetRevision()) + "    EDIT A DATA JSON FILE TO UPDATE";
    Emerald::DrawDebugText(renderer, {left.x + 28.0f, height - 72.0f}, revision, kDim, 1,
                           width - 80.0f);

    if (!m_Data.GetLastError().empty()) {
        renderer.FillRect({16.0f, height - 40.0f}, {width - 32.0f, 28.0f},
                          {0.35f, 0.10f, 0.10f, 1.0f});
        Emerald::DrawDebugText(renderer, {24.0f, height - 34.0f}, Upper(m_Data.GetLastError()),
                               kError, 1, width - 48.0f);
    }

    renderer.End();
}

} // namespace Ascension
