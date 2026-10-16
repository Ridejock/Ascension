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

constexpr f32 kPollSeconds = 0.25f;
constexpr usize kLogLines = 6;
constexpr int kMaxHotkeys = 8;

constexpr Emerald::Vec4 kInk{0.93f, 0.91f, 0.84f, 1.0f};
constexpr Emerald::Vec4 kDim{0.62f, 0.66f, 0.60f, 1.0f};
constexpr Emerald::Vec4 kFrame{0.72f, 0.64f, 0.38f, 1.0f};
constexpr Emerald::Vec4 kPanel{0.10f, 0.12f, 0.11f, 1.0f};
constexpr Emerald::Vec4 kError{0.95f, 0.45f, 0.38f, 1.0f};
constexpr Emerald::Vec4 kFigure{0.78f, 0.62f, 0.32f, 1.0f};
constexpr Emerald::Vec4 kFoe{0.62f, 0.28f, 0.24f, 1.0f};
constexpr Emerald::Vec4 kHp{0.78f, 0.32f, 0.30f, 1.0f};
constexpr Emerald::Vec4 kFocus{0.66f, 0.46f, 0.84f, 1.0f};

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

std::string Upper(std::string_view text)
{
    std::string out(text);
    for (char& code : out) {
        if (code >= 'a' && code <= 'z')
            code = static_cast<char>(code - 'a' + 'A');
    }
    return out;
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

void DrawBar(Emerald::Renderer2D& renderer, const Emerald::Vec2& origin, f32 width, f32 filled,
             const Emerald::Vec4& color)
{
    constexpr f32 kHeight = 14.0f;
    renderer.DrawRect(origin, {width, kHeight}, color);
    const f32 inner = std::clamp(filled, 0.0f, 1.0f) * (width - 2.0f);
    if (inner > 0.0f)
        renderer.FillRect(origin + Emerald::Vec2{1.0f, 1.0f}, {inner, kHeight - 2.0f}, color);
}

std::string PoolText(std::string_view label, i32 current, i32 maximum)
{
    return std::string(label) + " " + std::to_string(current) + "/" + std::to_string(maximum);
}

std::string AddsLine(const Database& data, const ClassDef& evolution)
{
    std::string line = "Adds";
    for (const std::string& id : evolution.Skills) {
        const SkillDef* skill = data.FindSkill(id);
        line += " ";
        line += skill != nullptr ? skill->Name : id;
    }
    if (!evolution.Passives.empty()) {
        line += " (";
        bool first = true;
        for (const std::string& id : evolution.Passives) {
            if (!first)
                line += " ";
            first = false;
            const SkillDef* skill = data.FindSkill(id);
            line += skill != nullptr ? skill->Name : id;
        }
        line += ")";
    }
    return line;
}

std::string PassiveLine(const Database& data, const SkillDef& passive)
{
    std::string stat = passive.Stat;
    if (const StatDef* named = data.FindStat(passive.Stat))
        stat = named->Name;
    std::string line = "Passive " + passive.Name + " " + stat + " ";
    if (passive.Amount > 0)
        line += "+";
    line += std::to_string(passive.Amount);
    return line;
}

} // namespace

AscensionApp::AscensionApp(const Emerald::ApplicationSpec& spec)
    : Emerald::Application(spec), m_Data(DataDirectory())
{
}

void AscensionApp::Note(std::string line)
{
    if (line.empty())
        return;
    m_Log.push_back(std::move(line));
    while (m_Log.size() > kLogLines)
        m_Log.erase(m_Log.begin());
}

void AscensionApp::Note(const ActionResult& result)
{
    Note(result.Text);
}

void AscensionApp::PushRanks()
{
    if (m_Battle == nullptr || !m_Hero.IsReady())
        return;
    std::vector<SkillRank> ranks;
    for (const std::string& id : m_Hero.SkillIds()) {
        SkillRank row;
        row.Id = id;
        row.Rank = m_Hero.RankOf(id);
        ranks.push_back(std::move(row));
    }
    m_Battle->SetPlayerRanks(m_Data.GetProgress().PowerPerRank, std::move(ranks));
}

void AscensionApp::AwardXp()
{
    if (m_XpAwarded || m_Battle == nullptr || !m_Hero.IsReady())
        return;
    m_XpAwarded = true;
    const EnemyDef* foe = m_Data.FindEnemy(m_Battle->Enemy().Id);
    const i32 amount = foe != nullptr ? foe->Experience : 0;
    const XpGrant grant = m_Hero.GrantXp(amount);
    if (amount > 0)
        Note(std::to_string(amount) + " XP.");
    if (grant.LevelAfter > grant.LevelBefore)
        Note("Level " + std::to_string(grant.LevelAfter) + ".");
    if (!grant.ChoiceOpened)
        return;
    std::string line = "Choose a path.";
    const std::vector<std::string> choices = m_Hero.ChoiceIds();
    for (usize i = 0; i < choices.size() && i < static_cast<usize>(kMaxHotkeys); ++i) {
        const ClassDef* evolution = m_Data.FindClass(choices[i]);
        line += " " + std::to_string(static_cast<int>(i) + 1) + " ";
        line += evolution != nullptr ? evolution->Name : choices[i];
        line += ".";
    }
    Note(line);
}

void AscensionApp::FinishAction(const ActionResult& result, bool playerActed)
{
    if (playerActed && result.Accepted && !result.SkillId.empty()) {
        const i32 before = m_Hero.RankOf(result.SkillId);
        const i32 after = m_Hero.NoteUse(result.SkillId);
        PushRanks();
        if (after > before) {
            const SkillDef* skill = m_Data.FindSkill(result.SkillId);
            const std::string name = skill != nullptr ? skill->Name : result.SkillId;
            Note(name + " reaches rank " + std::to_string(after) + ".");
        }
    }
    if (result.Result == BattleResult::Victory)
        AwardXp();
}

void AscensionApp::ResolveFoe()
{
    if (m_Battle == nullptr)
        return;
    for (i32 step = 0; step < 4 && m_Battle->Result() == BattleResult::Ongoing &&
                       m_Battle->Actor() == CombatantSide::Enemy;
         ++step) {
        const ActionResult act = m_Battle->EnemyAct();
        Note(act);
        FinishAction(act, false);
    }
}

void AscensionApp::BeginFight()
{
    m_Log.clear();
    m_StartError.clear();
    m_XpAwarded = false;
    if (!m_Data.IsLoaded() || !m_Hero.IsReady())
        return;
    const auto enemies = m_Data.GetEnemies();
    if (enemies.empty()) {
        m_StartError = "No enemies in data/.";
        m_Battle.reset();
        return;
    }
    if (m_EnemyIndex < 0 || m_EnemyIndex >= static_cast<i32>(enemies.size()))
        m_EnemyIndex = 0;
    if (m_Battle == nullptr)
        m_Battle = std::make_unique<Battle>(m_Data, std::make_unique<AgilityRoundModel>());

    std::string error;
    if (!m_Battle->Start(m_Hero.ClassId(), m_Hero.Level(),
                         enemies[static_cast<usize>(m_EnemyIndex)].Id, error)) {
        m_StartError = error;
        return;
    }
    PushRanks();
    if (m_Battle->Result() != BattleResult::Ongoing) {
        Note(m_Battle->Result() == BattleResult::Victory ? "Victory." : "Defeat.");
        return;
    }
    if (m_Battle->Actor() == CombatantSide::Enemy)
        Note(m_Battle->Enemy().Name + " moves first.");
    else
        Note("You move first.");
    ResolveFoe();
}

void AscensionApp::OnStart()
{
    if (!m_Data.Load())
        return;
    std::string error;
    if (!m_Hero.Start(m_Data, error)) {
        m_StartError = error;
        return;
    }
    BeginFight();
}

void AscensionApp::OnEvent(const SDL_Event& event)
{
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat)
        return;
    if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
        Quit();
        return;
    }
    if (!m_Data.IsLoaded())
        return;
    if (event.key.scancode == SDL_SCANCODE_R) {
        BeginFight();
        return;
    }
    if (event.key.scancode == SDL_SCANCODE_N) {
        const i32 count = static_cast<i32>(m_Data.GetEnemies().size());
        if (count > 0)
            m_EnemyIndex = (m_EnemyIndex + 1) % count;
        BeginFight();
        return;
    }
    if (m_Battle == nullptr || !m_StartError.empty())
        return;

    const int digit = static_cast<int>(event.key.scancode) - static_cast<int>(SDL_SCANCODE_1);
    if (m_Battle->Result() != BattleResult::Ongoing) {
        if (!m_Hero.ChoicePending() || digit < 0 || digit >= kMaxHotkeys)
            return;
        const std::vector<std::string> choices = m_Hero.ChoiceIds();
        if (digit >= static_cast<int>(choices.size()))
            return;
        std::string why;
        if (!m_Hero.Choose(choices[static_cast<usize>(digit)], why)) {
            Note(why);
            return;
        }
        const ClassDef* chosen = m_Data.FindClass(m_Hero.ClassId());
        Note(std::string(chosen != nullptr ? chosen->Name : m_Hero.ClassId()) + " chosen.");
        PushRanks();
        return;
    }

    if (event.key.scancode == SDL_SCANCODE_SPACE) {
        const ActionResult waited = m_Battle->Wait();
        Note(waited);
        FinishAction(waited, true);
        ResolveFoe();
        return;
    }

    const auto skills = m_Battle->PlayerSkillIds();
    if (digit < 0 || digit >= kMaxHotkeys || digit >= static_cast<int>(skills.size()))
        return;
    const ActionResult used = m_Battle->UseSkill(skills[static_cast<usize>(digit)]);
    Note(used);
    FinishAction(used, true);
    ResolveFoe();
}

void AscensionApp::OnUpdate(f32 dt)
{
    m_ReloadWait += dt;
    if (m_ReloadWait < kPollSeconds)
        return;
    m_ReloadWait = 0.0f;
    const u32 revision = m_Data.GetRevision();
    DataError error;
    m_Data.CheckForChanges(&error);
    if (m_Data.GetRevision() == revision)
        return;
    if (!m_Hero.IsReady()) {
        std::string why;
        if (!m_Hero.Start(m_Data, why)) {
            m_StartError = why;
            return;
        }
        BeginFight();
        return;
    }
    const i32 levelBefore = m_Hero.Level();
    std::string why;
    if (!m_Hero.ApplyData(why)) {
        Note(why);
        return;
    }
    if (m_Battle == nullptr || !m_StartError.empty()) {
        BeginFight();
        return;
    }
    if (!m_Battle->ApplyData(why)) {
        Note(why);
        return;
    }
    PushRanks();
    if (m_Hero.Level() != levelBefore)
        Note("Level " + std::to_string(m_Hero.Level()) + ".");
    Note("Data reloaded.");
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
    const Emerald::Vec2 leftSize{600.0f, height - 188.0f};
    const Emerald::Vec2 right{648.0f, 24.0f};
    const Emerald::Vec2 rightSize{width - 676.0f, height - 188.0f};
    DrawPanel(renderer, left, leftSize);
    DrawPanel(renderer, right, rightSize);
    DrawPanel(renderer, {28.0f, height - 148.0f}, {width - 56.0f, 124.0f});

    if (!m_Data.IsLoaded() || m_Battle == nullptr || !m_StartError.empty()) {
        Emerald::DrawDebugText(renderer, {left.x + 24.0f, left.y + 28.0f}, "ASCENSION", kFrame, 3);
        std::string message = m_StartError;
        if (message.empty())
            message = m_Data.GetLastError();
        if (message.empty())
            message = "Could not load data/.";
        Emerald::DrawDebugText(renderer, {left.x + 24.0f, left.y + 80.0f}, Upper(message), kError,
                               2, leftSize.x - 48.0f);
        renderer.End();
        return;
    }

    const CombatantState& player = m_Battle->Player();
    const CombatantState& enemy = m_Battle->Enemy();
    const bool playerTurn =
        m_Battle->Result() == BattleResult::Ongoing && m_Battle->Actor() == CombatantSide::Player;

    DrawFigure(renderer, {left.x + 28.0f, left.y + 28.0f}, 16.0f, kFigure);
    f32 y = left.y + 24.0f;
    std::string title = player.Name;
    if (const ClassDef* heroClass = m_Data.FindClass(m_Hero.ClassId()))
        title = heroClass->Name;
    Emerald::DrawDebugText(renderer, {left.x + 140.0f, y}, Upper(title), kInk, 3);
    y += Emerald::DebugTextLineHeight(3) + 8.0f;
    std::string levelLine = "Level " + std::to_string(m_Hero.Level()) + "   XP ";
    if (m_Hero.AtMaxLevel())
        levelLine += "max";
    else
        levelLine += std::to_string(m_Hero.Xp()) + "/" + std::to_string(m_Hero.NextLevelXp());
    Emerald::DrawDebugText(renderer, {left.x + 140.0f, y}, Upper(levelLine), kFrame, 2);
    y += Emerald::DebugTextLineHeight(2) + 4.0f;
    Emerald::DrawDebugText(renderer, {left.x + 140.0f, y},
                           Upper(PoolText("HP", player.Hp, player.MaxHp)), kInk, 2);
    y += Emerald::DebugTextLineHeight(2) + 4.0f;
    const f32 playerHp =
        player.MaxHp > 0 ? static_cast<f32>(player.Hp) / static_cast<f32>(player.MaxHp) : 0.0f;
    DrawBar(renderer, {left.x + 140.0f, y}, 280.0f, playerHp, kHp);
    y += 22.0f;
    Emerald::DrawDebugText(renderer, {left.x + 140.0f, y},
                           Upper(PoolText("FOCUS", player.Focus, player.MaxFocus)), kFocus, 2);
    y += Emerald::DebugTextLineHeight(2) + 4.0f;
    const f32 playerFocus = player.MaxFocus > 0
                                ? static_cast<f32>(player.Focus) / static_cast<f32>(player.MaxFocus)
                                : 0.0f;
    DrawBar(renderer, {left.x + 140.0f, y}, 280.0f, playerFocus, kFocus);

    y = left.y + 200.0f;
    const bool fightOver = m_Battle->Result() != BattleResult::Ongoing;
    const bool choosing = fightOver && m_Hero.ChoicePending();
    int shownSkills = 0;
    if (choosing) {
        Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, "CHOOSE A PATH", kFrame, 2);
        y += Emerald::DebugTextLineHeight(2) + 8.0f;
        const std::vector<std::string> choices = m_Hero.ChoiceIds();
        for (usize i = 0; i < choices.size() && i < static_cast<usize>(kMaxHotkeys); ++i) {
            const ClassDef* evolution = m_Data.FindClass(choices[i]);
            if (evolution == nullptr)
                continue;
            const std::string name =
                std::to_string(static_cast<int>(i) + 1) + "  " + evolution->Name;
            Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(name), kInk, 2);
            y += Emerald::DebugTextLineHeight(2) + 2.0f;
            y = Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(evolution->Description),
                                       kDim, 1, leftSize.x - 56.0f);
            y += 2.0f;
            Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                                   Upper(AddsLine(m_Data, *evolution)), kInk, 1);
            y += Emerald::DebugTextLineHeight(1) + 8.0f;
        }
    } else {
        Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, "SKILLS", kFrame, 2);
        y += Emerald::DebugTextLineHeight(2) + 8.0f;
        std::vector<std::string> skillIds;
        if (fightOver)
            skillIds = m_Hero.SkillIds();
        else {
            for (const std::string& id : m_Battle->PlayerSkillIds())
                skillIds.push_back(id);
        }
        for (usize i = 0; i < skillIds.size() && i < static_cast<usize>(kMaxHotkeys); ++i) {
            const SkillDef* skill = m_Data.FindSkill(skillIds[i]);
            if (skill == nullptr)
                continue;
            ++shownSkills;
            const bool ready = playerTurn && player.Focus >= skill->FocusCost;
            const Emerald::Vec4 color = fightOver || ready ? kInk : kDim;
            std::string line = std::to_string(static_cast<int>(i) + 1) + "  " + skill->Name;
            line += " (" + std::to_string(m_Hero.RankOf(skill->Id)) + ")   ";
            line += std::to_string(skill->FocusCost) + " focus";
            Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(line), color, 2);
            y += Emerald::DebugTextLineHeight(2) + 4.0f;
        }
        if (const ClassDef* heroClass = m_Data.FindClass(m_Hero.ClassId())) {
            for (const SkillDef* passive : m_Data.Passives(*heroClass)) {
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                                       Upper(PassiveLine(m_Data, *passive)), kFrame, 1);
                y += Emerald::DebugTextLineHeight(1) + 2.0f;
            }
        }
    }

    DrawFigure(renderer, {right.x + 28.0f, right.y + 28.0f}, 16.0f, kFoe);
    y = right.y + 24.0f;
    Emerald::DrawDebugText(renderer, {right.x + 140.0f, y}, Upper(enemy.Name), kInk, 3);
    y += Emerald::DebugTextLineHeight(3) + 8.0f;
    Emerald::DrawDebugText(renderer, {right.x + 140.0f, y},
                           Upper(PoolText("HP", enemy.Hp, enemy.MaxHp)), kInk, 2);
    y += Emerald::DebugTextLineHeight(2) + 4.0f;
    const f32 enemyHp =
        enemy.MaxHp > 0 ? static_cast<f32>(enemy.Hp) / static_cast<f32>(enemy.MaxHp) : 0.0f;
    DrawBar(renderer, {right.x + 140.0f, y}, 280.0f, enemyHp, kHp);
    y += 28.0f;
    const EnemyDef* foe = m_Data.FindEnemy(enemy.Id);
    if (foe != nullptr) {
        y = Emerald::DrawDebugText(renderer, {right.x + 28.0f, y}, Upper(foe->Description), kDim, 2,
                                   rightSize.x - 56.0f);
        y += 10.0f;
    }
    std::string order = "Acts with you";
    if (enemy.Stats.Agility > player.Stats.Agility)
        order = "Acts first";
    else if (enemy.Stats.Agility < player.Stats.Agility)
        order = "Acts second";
    Emerald::DrawDebugText(renderer, {right.x + 28.0f, y},
                           Upper("AGILITY " + std::to_string(enemy.Stats.Agility) + "  " + order),
                           kDim, 2);

    std::string banner = "YOUR TURN";
    Emerald::Vec4 bannerColor = kFrame;
    if (choosing) {
        banner = "CHOOSE A PATH";
    } else if (m_Battle->Result() == BattleResult::Victory) {
        banner = "VICTORY";
    } else if (m_Battle->Result() == BattleResult::Defeat) {
        banner = "DEFEAT";
        bannerColor = kError;
    } else if (!playerTurn) {
        banner = Upper(enemy.Name) + " ACTS";
    }
    Emerald::DrawDebugText(renderer, {48.0f, height - 132.0f}, banner, bannerColor, 2);

    f32 logY = height - 96.0f;
    for (const std::string& line : m_Log) {
        Emerald::DrawDebugText(renderer, {48.0f, logY}, Upper(line), kDim, 1, width - 96.0f);
        logY += Emerald::DebugTextLineHeight(1) + 1.0f;
    }

    if (!m_Data.GetLastError().empty()) {
        Emerald::DrawDebugText(renderer, {48.0f, height - 36.0f}, Upper(m_Data.GetLastError()),
                               kError, 1, width - 96.0f);
    } else {
        std::string help;
        if (choosing) {
            const std::vector<std::string> choices = m_Hero.ChoiceIds();
            for (usize i = 0; i < choices.size() && i < static_cast<usize>(kMaxHotkeys); ++i) {
                const ClassDef* evolution = m_Data.FindClass(choices[i]);
                if (!help.empty())
                    help += "    ";
                help += std::to_string(static_cast<int>(i) + 1) + " ";
                help += evolution != nullptr ? evolution->Name : choices[i];
            }
            help += "    R FIGHT    N NEXT FOE    ESC QUIT";
        } else {
            if (shownSkills == 1)
                help = "1 SKILL    ";
            else if (shownSkills > 1)
                help = "1-" + std::to_string(shownSkills) + " SKILL    ";
            help += "SPACE WAIT    R FIGHT    N NEXT FOE    ESC QUIT    REV " +
                    std::to_string(m_Data.GetRevision());
        }
        Emerald::DrawDebugText(renderer, {48.0f, height - 36.0f}, Upper(help), kDim, 1,
                               width - 96.0f);
    }

    renderer.End();
}

} // namespace Ascension
