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
// Keys are fixed in this file. There is no binding table in the data.
constexpr SDL_Scancode kItemKey = SDL_SCANCODE_C;

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
    for (const SkillGrant& grant : evolution.Skills) {
        const SkillDef* skill = data.FindSkill(grant.Id);
        line += " ";
        line += skill != nullptr ? skill->Name : grant.Id;
    }
    if (!evolution.Passives.empty()) {
        line += " (";
        bool first = true;
        for (const SkillGrant& grant : evolution.Passives) {
            if (!first)
                line += " ";
            first = false;
            const SkillDef* skill = data.FindSkill(grant.Id);
            line += skill != nullptr ? skill->Name : grant.Id;
        }
        line += ")";
    }
    return line;
}

std::string StatLabel(const Database& data, const std::string& id)
{
    if (const StatDef* named = data.FindStat(id))
        return named->Name;
    return id;
}

std::string PassiveLine(const Database& data, const SkillDef& passive)
{
    const EffectDef* missing = nullptr;
    const EffectDef* flat = nullptr;
    const EffectDef* focus = nullptr;
    for (const EffectDef& effect : passive.Effects) {
        if (effect.Trigger == EffectTrigger::Always && effect.Op == EffectOp::StatPerMissingHp)
            missing = &effect;
        else if (effect.Trigger == EffectTrigger::Always && effect.Op == EffectOp::Stat)
            flat = &effect;
        else if (effect.Op == EffectOp::GrantFocus)
            focus = &effect;
    }
    if (missing != nullptr) {
        const std::string stat = StatLabel(data, missing->Stat.value_or(""));
        return "Passive " + passive.Name + " +" + std::to_string(missing->Amount.value_or(0)) +
               " " + stat + " per " + std::to_string(missing->Per.value_or(0)) + " up to +" +
               std::to_string(missing->Max.value_or(0));
    }
    if (flat != nullptr && focus != nullptr) {
        const std::string stat = StatLabel(data, flat->Stat.value_or(""));
        return "Passive " + passive.Name + " " + stat + " +" +
               std::to_string(flat->Amount.value_or(0)) + "  focus +" +
               std::to_string(focus->Amount.value_or(0)) + " on block";
    }
    if (flat != nullptr) {
        const std::string stat = StatLabel(data, flat->Stat.value_or(""));
        std::string line = "Passive " + passive.Name + " " + stat + " ";
        const i32 amount = flat->Amount.value_or(0);
        if (amount > 0)
            line += "+";
        line += std::to_string(amount);
        return line;
    }
    return "Passive " + passive.Name;
}

void AppendSigned(std::string& line, const char* label, i32 value)
{
    if (value == 0)
        return;
    if (!line.empty())
        line += "  ";
    line += label;
    if (value > 0)
        line += " +";
    else
        line += " ";
    line += std::to_string(value);
}

std::string BonusText(const StatBlock& bonus)
{
    std::string line;
    AppendSigned(line, "STR", bonus.Strength);
    AppendSigned(line, "DEF", bonus.Defense);
    AppendSigned(line, "AGI", bonus.Agility);
    AppendSigned(line, "FOC", bonus.Focus);
    AppendSigned(line, "VIT", bonus.Vitality);
    return line;
}

std::string WornLine(const Database& data, const Inventory& bag)
{
    const GearSlot slots[] = {GearSlot::Weapon, GearSlot::Offhand, GearSlot::Armor,
                              GearSlot::Accessory};
    const char* labels[] = {"WEAPON", "OFFHAND", "ARMOR", "ACCESSORY"};
    std::string line;
    for (i32 index = 0; index < 4; ++index) {
        if (!line.empty())
            line += "   ";
        line += labels[index];
        line += " ";
        if (slots[index] == GearSlot::Offhand && bag.OffhandBlocked()) {
            line += "BOTH HANDS";
            continue;
        }
        const std::string& worn = bag.Equipped(slots[index]);
        if (worn.empty()) {
            line += "-";
            continue;
        }
        const ItemDef* item = data.FindItem(worn);
        line += item != nullptr ? item->Name : worn;
    }
    return line;
}

std::string StatusLine(const Database& data, const CombatantState& who)
{
    std::string line;
    for (const StatusInstance& status : who.Statuses) {
        if (!line.empty())
            line += "  ";
        const StatusDef* def = data.FindStatus(status.Id);
        line += def != nullptr ? def->Name : status.Id;
        if (status.Stacks > 1)
            line += " " + std::to_string(status.Stacks);
    }
    return line;
}

// Wind-up, a summoned add that is about to join, and statuses. The y that comes back is
// just under the last line.
f32 DrawFoeAlerts(Emerald::Renderer2D& renderer, const Database& data, const CombatantState& who,
                  const Emerald::Vec2& origin, f32 wrap, int scale)
{
    f32 alertY = origin.y;
    if (!who.WindupSkill.empty()) {
        const SkillDef* winding = data.FindSkill(who.WindupSkill);
        const std::string windName = winding != nullptr ? winding->Name : who.WindupSkill;
        i32 shown = who.WindupTotal - who.WindupLeft;
        if (shown < 1)
            shown = 1;
        if (who.WindupTotal > 0 && shown > who.WindupTotal)
            shown = who.WindupTotal;
        std::string windLine =
            windName + " " + std::to_string(shown) + "/" + std::to_string(who.WindupTotal);
        if (who.WindupLeft <= 0)
            windLine += " incoming";
        alertY = Emerald::DrawDebugText(renderer, {origin.x, alertY}, Upper(windLine), kError,
                                        scale, wrap);
        alertY += 4.0f;
    }
    if (who.ShardDue) {
        std::string incoming = "Incoming";
        const EnemyDef* host = data.FindEnemy(who.Id);
        if (host != nullptr && !host->SummonId.empty()) {
            const EnemyDef* add = data.FindEnemy(host->SummonId);
            incoming = add != nullptr ? add->Name : host->SummonId;
        }
        alertY = Emerald::DrawDebugText(renderer, {origin.x, alertY}, Upper(incoming), kError,
                                        scale, wrap);
        alertY += 4.0f;
    }
    const std::string foeStatus = StatusLine(data, who);
    if (!foeStatus.empty()) {
        alertY = Emerald::DrawDebugText(renderer, {origin.x, alertY}, Upper(foeStatus), kError,
                                        scale, wrap);
        alertY += 4.0f;
    }
    return alertY;
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

void AscensionApp::PushRanks(std::string_view pendingSkill)
{
    if (m_Battle == nullptr || !m_Hero.IsReady())
        return;
    std::vector<SkillRank> ranks;
    for (const std::string& id : m_Hero.SkillIds()) {
        SkillRank row;
        row.Id = id;
        if (!pendingSkill.empty() && id == pendingSkill)
            row.Rank = m_Hero.RankAfterUse(id);
        else
            row.Rank = m_Hero.RankOf(id);
        ranks.push_back(std::move(row));
    }
    m_Battle->SetPlayerRanks(m_Data.GetProgress().PowerPerRank, std::move(ranks));
}

void AscensionApp::AwardVictory()
{
    if (m_XpAwarded || m_Battle == nullptr || !m_Hero.IsReady())
        return;
    m_XpAwarded = true;
    const i32 amount = m_Battle->Experience();
    const i32 gold = m_Battle->Gold();
    const XpGrant grant = m_Hero.GrantXp(amount);
    m_Inventory.GrantGold(gold);
    if (amount > 0)
        Note(std::to_string(amount) + " XP.");
    if (gold > 0)
        Note(std::to_string(gold) + " gold.");
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
        AwardVictory();
}

void AscensionApp::ResolveFoe()
{
    if (m_Battle == nullptr)
        return;
    for (i32 step = 0; step < 8 && m_Battle->Result() == BattleResult::Ongoing &&
                       m_Battle->Actor() == CombatantSide::Enemy;
         ++step) {
        const ActionResult act = m_Battle->EnemyAct();
        Note(act);
        FinishAction(act, false);
    }
}

void AscensionApp::BeginFight()
{
    if (const char* block = m_Hero.NextFightBlock()) {
        m_Camp = false;
        m_Picking = false;
        if (m_Log.empty() || m_Log.back() != block)
            Note(block);
        return;
    }
    m_Log.clear();
    m_StartError.clear();
    m_XpAwarded = false;
    m_Camp = false;
    m_Picking = false;
    if (!m_Data.IsLoaded() || !m_Hero.IsReady())
        return;
    const auto encounters = m_Data.GetEncounters();
    if (encounters.empty()) {
        m_StartError = "No encounters in data/.";
        m_Battle.reset();
        return;
    }
    if (m_EnemyIndex < 0 || m_EnemyIndex >= static_cast<i32>(encounters.size()))
        m_EnemyIndex = 0;
    if (m_Battle == nullptr)
        m_Battle = std::make_unique<Battle>(m_Data, std::make_unique<AgilityRoundModel>());

    m_Battle->SetGear(m_Inventory.GearBonus());
    m_Battle->SetBaseStats(m_Hero.Attributes());
    std::string error;
    if (!m_Battle->Start(m_Hero.ClassId(), m_Hero.Level(),
                         encounters[static_cast<usize>(m_EnemyIndex)].Id, error)) {
        m_StartError = error;
        return;
    }
    PushRanks();
    if (m_Battle->Result() != BattleResult::Ongoing) {
        Note(m_Battle->Result() == BattleResult::Victory ? "Victory." : "Defeat.");
        return;
    }
    if (m_Battle->Actor() == CombatantSide::Enemy) {
        const i32 acting = m_Battle->ActorIndex();
        const auto foes = m_Battle->Enemies();
        const std::string& name =
            acting >= 0 && acting < static_cast<i32>(foes.size()) ? foes[static_cast<usize>(acting)].Name
                                                                  : m_Battle->Enemy().Name;
        Note(name + " moves first.");
    } else {
        Note("You move first.");
    }
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
    if (!m_Inventory.Start(m_Data, error)) {
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
        const i32 count = static_cast<i32>(m_Data.GetEncounters().size());
        m_EnemyIndex = m_Hero.IndexAfterNextFoe(m_EnemyIndex, count);
        BeginFight();
        return;
    }
    if (event.key.scancode == SDL_SCANCODE_I) {
        if (m_Battle != nullptr && m_Battle->Result() == BattleResult::Ongoing) {
            Note("Finish the fight. Pell waits between battles.");
            return;
        }
        m_Camp = !m_Camp;
        m_CampColumn = 0;
        m_CampRow = 0;
        m_Picking = false;
        return;
    }
    if (m_Camp) {
        HandleCamp(event.key.scancode);
        return;
    }
    if (m_Battle == nullptr || !m_StartError.empty())
        return;

    if (m_Picking) {
        if (m_Battle->Result() != BattleResult::Ongoing ||
            m_Battle->Actor() != CombatantSide::Player)
            m_Picking = false;
        else {
            HandlePicker(event.key.scancode);
            return;
        }
    }

    const int digit = static_cast<int>(event.key.scancode) - static_cast<int>(SDL_SCANCODE_1);
    if (m_Battle->Result() != BattleResult::Ongoing) {
        const bool pickingClass = m_Hero.ChoosingStarter();
        const bool spending = !pickingClass && m_Hero.ClassPool() > 0;
        const bool choosingPath = m_Hero.ChoicePending() && !pickingClass && !spending;
        if (!pickingClass && !spending && !choosingPath)
            return;
        if (digit < 0 || digit >= kMaxHotkeys)
            return;
        std::string why;
        if (pickingClass) {
            const std::vector<std::string> starters = m_Hero.StarterIds();
            if (digit >= static_cast<int>(starters.size()))
                return;
            if (!m_Hero.ChooseStarter(starters[static_cast<usize>(digit)], why)) {
                Note(why);
                return;
            }
            const ClassDef* starter = m_Data.FindClass(m_Hero.ClassId());
            Note(std::string(starter != nullptr ? starter->Name : m_Hero.ClassId()) +
                 ". Spend the class points.");
            return;
        }
        if (spending) {
            const char* stats[] = {"vitality", "strength", "defense", "agility", "focus"};
            const char* labels[] = {"Vitality", "Strength", "Defense", "Agility", "Focus"};
            if (digit < 5) {
                if (!m_Hero.SpendClassPoint(stats[digit], why)) {
                    Note(why);
                    return;
                }
                Note(std::string(labels[digit]) + " +1. " + std::to_string(m_Hero.ClassPool()) +
                     " class points left.");
                return;
            }
            if (digit == 5 && m_Hero.ChoicePending()) {
                if (!m_Hero.ResetBuild(why)) {
                    Note(why);
                    return;
                }
                Note("Build reset. Skill ranks cleared. Pick a starter class.");
                PushRanks();
            }
            return;
        }
        const std::vector<std::string> choices = m_Hero.ChoiceIds();
        if (digit == static_cast<int>(choices.size())) {
            if (!m_Hero.ResetBuild(why)) {
                Note(why);
                return;
            }
            Note("Build reset. Skill ranks cleared. Pick a starter class.");
            PushRanks();
            return;
        }
        if (digit >= static_cast<int>(choices.size()))
            return;
        if (!m_Hero.Choose(choices[static_cast<usize>(digit)], why)) {
            Note(why);
            return;
        }
        const ClassDef* chosen = m_Data.FindClass(m_Hero.ClassId());
        Note(std::string(chosen != nullptr ? chosen->Name : m_Hero.ClassId()) + " chosen.");
        PushRanks();
        return;
    }

    if (event.key.scancode == SDL_SCANCODE_LEFT || event.key.scancode == SDL_SCANCODE_RIGHT) {
        if (m_Battle->Actor() != CombatantSide::Player) {
            Note("Wait for your turn.");
            return;
        }
        m_Battle->CycleTarget(event.key.scancode == SDL_SCANCODE_RIGHT ? 1 : -1);
        return;
    }

    if (event.key.scancode == kItemKey) {
        if (m_Battle->Actor() != CombatantSide::Player) {
            Note("Wait for your turn.");
            return;
        }
        if (m_Inventory.ConsumableSlots().empty()) {
            Note("The bag has no drinks.");
            return;
        }
        m_Picking = true;
        m_PickRow = 0;
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
    if (digit < 0 || digit >= kMaxHotkeys)
        return;
    if (digit < static_cast<int>(skills.size())) {
        const std::string& skillId = skills[static_cast<usize>(digit)];
        PushRanks(skillId);
        const ActionResult used = m_Battle->UseSkill(skillId);
        if (!used.Accepted)
            PushRanks();
        Note(used);
        FinishAction(used, true);
        ResolveFoe();
        return;
    }
    const std::vector<i32> drinks = m_Inventory.ConsumableSlots();
    const int drink = digit - static_cast<int>(skills.size());
    if (drink < 0 || drink >= static_cast<int>(drinks.size()))
        return;
    DrinkAt(drinks[static_cast<usize>(drink)]);
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
    if (!m_Hero.IsReady() || !m_Inventory.IsReady()) {
        std::string why;
        if (!m_Hero.IsReady() && !m_Hero.Start(m_Data, why)) {
            m_StartError = why;
            return;
        }
        if (!m_Inventory.IsReady() && !m_Inventory.Start(m_Data, why)) {
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
    if (!m_Inventory.ApplyData(why)) {
        Note(why);
        return;
    }
    if (!why.empty())
        Note(why);
    if (m_Battle == nullptr || !m_StartError.empty()) {
        BeginFight();
        return;
    }
    m_Battle->SetGear(m_Inventory.GearBonus());
    m_Battle->SetBaseStats(m_Hero.Attributes());
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

    if (m_Camp) {
        DrawCamp(renderer, width, height);
        renderer.End();
        return;
    }

    const CombatantState& player = m_Battle->Player();
    const CombatantState& enemy = m_Battle->Enemy();
    const std::span<const CombatantState> pack = m_Battle->Enemies();
    const i32 packCount = static_cast<i32>(pack.size());
    const i32 actorSlot =
        m_Battle->Result() == BattleResult::Ongoing ? m_Battle->ActorIndex() : -1;
    const i32 encounterTotal = static_cast<i32>(m_Data.GetEncounters().size());
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
    levelLine += "   GOLD " + std::to_string(m_Inventory.Gold());
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
    const std::string playerStatus = StatusLine(m_Data, player);
    if (!playerStatus.empty()) {
        y += 18.0f;
        Emerald::DrawDebugText(renderer, {left.x + 140.0f, y}, Upper(playerStatus), kError, 1);
    }

    const bool fightOver = m_Battle->Result() != BattleResult::Ongoing;
    const bool pickingClass = fightOver && m_Hero.ChoosingStarter();
    const bool spending = fightOver && !pickingClass && m_Hero.ClassPool() > 0;
    const bool choosing = fightOver && (m_Hero.ChoicePending() || pickingClass || spending);
    y = left.y + 188.0f;
    if (!choosing) {
        Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(WornLine(m_Data, m_Inventory)),
                               kDim, 1, leftSize.x - 56.0f);
    }
    y = left.y + 210.0f;
    int shownSkills = 0;
    if (choosing) {
        const char* heading = "CHOOSE A PATH";
        if (pickingClass)
            heading = "CHOOSE A CLASS";
        else if (spending)
            heading = "SPEND CLASS POINTS";
        Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, heading, kFrame, 2);
        y += Emerald::DebugTextLineHeight(2) + 2.0f;
        Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, "THE NEXT FIGHT WAITS.", kInk, 1);
        y += Emerald::DebugTextLineHeight(1) + 8.0f;
        if (pickingClass) {
            const std::vector<std::string> starters = m_Hero.StarterIds();
            for (usize i = 0; i < starters.size() && i < static_cast<usize>(kMaxHotkeys); ++i) {
                const ClassDef* starter = m_Data.FindClass(starters[i]);
                const std::string name = std::to_string(static_cast<int>(i) + 1) + "  " +
                                         (starter != nullptr ? starter->Name : starters[i]);
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(name), kInk, 2);
                y += Emerald::DebugTextLineHeight(2) + 2.0f;
                if (starter != nullptr) {
                    y = Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                                               Upper(starter->Description), kDim, 1,
                                               leftSize.x - 56.0f);
                    y += 8.0f;
                }
            }
            Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                                   "ANY STARTER. CLASS POINTS STAY IN THE POOL.", kDim, 1,
                                   leftSize.x - 56.0f);
        } else if (spending) {
            const char* labels[] = {"VITALITY", "STRENGTH", "DEFENSE", "AGILITY", "FOCUS"};
            const StatBlock attributes = m_Hero.Attributes();
            const i32 amounts[] = {attributes.Vitality, attributes.Strength, attributes.Defense,
                                   attributes.Agility, attributes.Focus};
            Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                                   "POOL " + std::to_string(m_Hero.ClassPool()), kInk, 2);
            y += Emerald::DebugTextLineHeight(2) + 4.0f;
            for (int stat = 0; stat < 5; ++stat) {
                const std::string line = std::to_string(stat + 1) + "  " + labels[stat] + "  " +
                                         std::to_string(amounts[stat]);
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, line, kInk, 2);
                y += Emerald::DebugTextLineHeight(2) + 2.0f;
            }
            if (m_Hero.ChoicePending()) {
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, "6  RESET BUILD", kDim, 1);
            }
        } else {
            const std::vector<std::string> choices = m_Hero.ChoiceIds();
            for (usize i = 0; i < choices.size() && i < static_cast<usize>(kMaxHotkeys); ++i) {
                const ClassDef* evolution = m_Data.FindClass(choices[i]);
                if (evolution == nullptr)
                    continue;
                const std::string name =
                    std::to_string(static_cast<int>(i) + 1) + "  " + evolution->Name;
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(name), kInk, 2);
                y += Emerald::DebugTextLineHeight(2) + 2.0f;
                y = Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                                           Upper(evolution->Description), kDim, 1,
                                           leftSize.x - 56.0f);
                y += 2.0f;
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                                       Upper(AddsLine(m_Data, *evolution)), kInk, 1);
                y += Emerald::DebugTextLineHeight(1) + 8.0f;
            }
            if (choices.size() < static_cast<usize>(kMaxHotkeys)) {
                const std::string resetName =
                    std::to_string(static_cast<int>(choices.size()) + 1) + "  RESET BUILD";
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, resetName, kInk, 2);
                y += Emerald::DebugTextLineHeight(2) + 2.0f;
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                                       "CLEARS SKILL RANKS. CLASS POINTS RETURN TO THE POOL.", kDim,
                                       1, leftSize.x - 56.0f);
            }
        }
    } else if (m_Picking) {
        Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, "DRINKS", kFrame, 2);
        y += Emerald::DebugTextLineHeight(2) + 4.0f;
        Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                               "E DRINKS THE MARKED ROW. 1-8 DRINK THAT ROW.", kDim, 1,
                               leftSize.x - 56.0f);
        y += Emerald::DebugTextLineHeight(1) + 6.0f;
        const std::vector<i32> picked = m_Inventory.ConsumableSlots();
        for (usize drink = 0; drink < picked.size(); ++drink) {
            const bool marked = static_cast<i32>(drink) == m_PickRow;
            const ItemStack& stack = m_Inventory.BagAt(picked[drink]);
            const ItemDef* item = m_Data.FindItem(stack.Id);
            std::string rowText = marked ? "> " : "  ";
            if (drink < static_cast<usize>(kMaxHotkeys))
                rowText += std::to_string(static_cast<int>(drink) + 1) + "  ";
            if (item == nullptr) {
                rowText += stack.Id;
            } else {
                rowText += item->Name;
                rowText += " x" + std::to_string(stack.Count);
                if (item->Heal > 0)
                    rowText += "   heal " + std::to_string(item->Heal);
                if (item->Focus > 0)
                    rowText += "   focus +" + std::to_string(item->Focus);
            }
            Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(rowText),
                                   marked ? kFrame : kInk, 1, leftSize.x - 56.0f);
            y += Emerald::DebugTextLineHeight(1) + 2.0f;
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
        const std::vector<i32> drinks = m_Inventory.ConsumableSlots();
        int shownDrinks = 0;
        for (usize drink = 0; drink < drinks.size(); ++drink) {
            const int key = shownSkills + static_cast<int>(drink) + 1;
            if (key > kMaxHotkeys)
                break;
            const ItemStack& stack = m_Inventory.BagAt(drinks[drink]);
            const ItemDef* item = m_Data.FindItem(stack.Id);
            if (item == nullptr)
                continue;
            ++shownDrinks;
            const Emerald::Vec4 drinkColor = playerTurn && !fightOver ? kInk : kDim;
            std::string drinkLine =
                std::to_string(key) + "  " + item->Name + " x" + std::to_string(stack.Count);
            Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(drinkLine), drinkColor, 2);
            y += Emerald::DebugTextLineHeight(2) + 4.0f;
        }
        if (shownDrinks < static_cast<int>(drinks.size())) {
            Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, "C  EVERY DRINK", kFrame, 1);
            y += Emerald::DebugTextLineHeight(1) + 2.0f;
        }
        if (const ClassDef* heroClass = m_Data.FindClass(m_Hero.ClassId())) {
            for (const SkillDef* passive : m_Data.Passives(*heroClass)) {
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                                       Upper(PassiveLine(m_Data, *passive)), kFrame, 1);
                y += Emerald::DebugTextLineHeight(1) + 2.0f;
            }
        }
    }

    if (packCount <= 1) {
        DrawFigure(renderer, {right.x + 28.0f, right.y + 28.0f}, 16.0f, kFoe);
        y = right.y + 24.0f;
        const EnemyDef* foe = m_Data.FindEnemy(enemy.Id);
        std::string foeTitle = enemy.Name;
        if (encounterTotal > 0) {
            foeTitle += "  " + std::to_string(m_EnemyIndex + 1) + "/" +
                        std::to_string(encounterTotal);
        }
        Emerald::DrawDebugText(renderer, {right.x + 140.0f, y}, Upper(foeTitle), kInk, 3);
        y += Emerald::DebugTextLineHeight(3) + 4.0f;
        if (foe != nullptr && foe->Boss) {
            std::string bossLine = "BOSS";
            if (enemy.Phase >= 2)
                bossLine += "  PHASE " + std::to_string(enemy.Phase);
            Emerald::DrawDebugText(renderer, {right.x + 140.0f, y}, bossLine, kError, 2);
            y += Emerald::DebugTextLineHeight(2) + 4.0f;
        } else {
            y += 4.0f;
        }
        Emerald::DrawDebugText(renderer, {right.x + 140.0f, y},
                               Upper(PoolText("HP", enemy.Hp, enemy.MaxHp)), kInk, 2);
        y += Emerald::DebugTextLineHeight(2) + 4.0f;
        const f32 enemyHp =
            enemy.MaxHp > 0 ? static_cast<f32>(enemy.Hp) / static_cast<f32>(enemy.MaxHp) : 0.0f;
        DrawBar(renderer, {right.x + 140.0f, y}, 280.0f, enemyHp, kHp);
        y += 22.0f;
        y = DrawFoeAlerts(renderer, m_Data, enemy, {right.x + 28.0f, y}, rightSize.x - 56.0f, 2);
        y += 8.0f;
        if (foe != nullptr) {
            y = Emerald::DrawDebugText(renderer, {right.x + 28.0f, y}, Upper(foe->Description),
                                       kDim, 2, rightSize.x - 56.0f);
            y += 10.0f;
        }
    } else {
        y = right.y + 16.0f;
        std::string packTitle = m_Battle->EncounterName();
        if (encounterTotal > 0) {
            packTitle += "  " + std::to_string(m_EnemyIndex + 1) + "/" +
                         std::to_string(encounterTotal);
        }
        Emerald::DrawDebugText(renderer, {right.x + 16.0f, y}, Upper(packTitle), kInk, 2);
        y += Emerald::DebugTextLineHeight(2) + 8.0f;

        const f32 columnGap = 8.0f;
        const f32 columnsWidth = rightSize.x - 24.0f;
        const f32 columnSpan = columnsWidth - columnGap * static_cast<f32>(packCount - 1);
        const f32 columnWidth = columnSpan / static_cast<f32>(packCount);
        const int columnScale = packCount >= 3 ? 1 : 2;
        const f32 figureSize = packCount >= 3 ? 7.0f : 11.0f;
        const f32 cardTop = y;
        const f32 cardHeight = packCount >= 3 ? 200.0f : 252.0f;
        f32 columnX = right.x + 12.0f;
        for (i32 column = 0; column < packCount; ++column) {
            const CombatantState& member = pack[static_cast<usize>(column)];
            const bool selected = column == m_Battle->TargetIndex();
            const bool acting = column == actorSlot;
            const Emerald::Vec4 body = member.Alive() ? kFoe : kDim;
            const Emerald::Vec4 ink = member.Alive() ? kInk : kDim;
            if (selected)
                renderer.DrawRect({columnX, cardTop}, {columnWidth, cardHeight}, kFrame);
            DrawFigure(renderer, {columnX + 8.0f, cardTop + 8.0f}, figureSize, body);
            f32 textY = cardTop + 12.0f + figureSize * 6.6f;
            const f32 textX = columnX + 8.0f;
            const f32 textWrap = columnWidth - 16.0f;
            Emerald::DrawDebugText(renderer, {textX, textY}, Upper(member.Name),
                                   selected ? kFrame : ink, columnScale, textWrap);
            textY += Emerald::DebugTextLineHeight(columnScale) + 2.0f;
            const EnemyDef* memberDef = m_Data.FindEnemy(member.Id);
            if (memberDef != nullptr && memberDef->Boss) {
                std::string bossLine = "BOSS";
                if (member.Phase >= 2)
                    bossLine += "  P" + std::to_string(member.Phase);
                Emerald::DrawDebugText(renderer, {textX, textY}, bossLine, kError, 1);
                textY += Emerald::DebugTextLineHeight(1) + 2.0f;
            }
            Emerald::DrawDebugText(renderer, {textX, textY},
                                   Upper(PoolText("HP", member.Hp, member.MaxHp)), ink, 1);
            textY += Emerald::DebugTextLineHeight(1) + 2.0f;
            const f32 memberHp = member.MaxHp > 0
                                     ? static_cast<f32>(member.Hp) / static_cast<f32>(member.MaxHp)
                                     : 0.0f;
            DrawBar(renderer, {textX, textY}, textWrap, memberHp, member.Alive() ? kHp : kDim);
            textY += 18.0f;
            textY = DrawFoeAlerts(renderer, m_Data, member, {textX, textY}, textWrap, 1);
            std::string mark;
            if (acting)
                mark = "ACTS";
            if (selected) {
                if (!mark.empty())
                    mark += "  ";
                mark += "TARGET";
            }
            if (!member.Alive()) {
                if (!mark.empty())
                    mark += "  ";
                mark += "DOWN";
            }
            if (!mark.empty()) {
                Emerald::DrawDebugText(renderer, {textX, textY}, mark, selected ? kFrame : kDim, 1,
                                       textWrap);
            }
            columnX += columnWidth + columnGap;
        }
        y = cardTop + cardHeight + 10.0f;
        const EnemyDef* marked = m_Data.FindEnemy(enemy.Id);
        if (marked != nullptr) {
            y = Emerald::DrawDebugText(renderer, {right.x + 16.0f, y}, Upper(marked->Description),
                                       kDim, 1, rightSize.x - 32.0f);
            y += 8.0f;
        }
    }
    std::string order = "Acts with you";
    if (enemy.Stats.Agility > player.Stats.Agility)
        order = "Acts first";
    else if (enemy.Stats.Agility < player.Stats.Agility)
        order = "Acts second";
    const std::string orderLine =
        "AGILITY " + std::to_string(enemy.Stats.Agility) + "  " + order;
    if (packCount <= 1)
        Emerald::DrawDebugText(renderer, {right.x + 28.0f, y}, Upper(orderLine), kDim, 2);
    else
        Emerald::DrawDebugText(renderer, {right.x + 16.0f, y}, Upper(orderLine), kDim, 1);

    std::string banner = "YOUR TURN";
    Emerald::Vec4 bannerColor = kFrame;
    if (pickingClass) {
        banner = "CHOOSE A CLASS";
    } else if (spending) {
        banner = "SPEND CLASS POINTS";
    } else if (choosing) {
        banner = "CHOOSE A PATH";
    } else if (m_Picking) {
        banner = "CHOOSE AN ITEM";
    } else if (m_Battle->Result() == BattleResult::Victory) {
        banner = "VICTORY";
    } else if (m_Battle->Result() == BattleResult::Defeat) {
        banner = "DEFEAT";
        bannerColor = kError;
    } else if (!playerTurn) {
        std::string actingName = enemy.Name;
        if (actorSlot >= 0 && actorSlot < packCount)
            actingName = pack[static_cast<usize>(actorSlot)].Name;
        banner = Upper(actingName) + " ACTS";
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
        if (m_Picking && !choosing) {
            help = "UP DOWN MOVE    1-8 DRINK    E DRINK    C CLOSE";
        } else if (pickingClass) {
            const std::vector<std::string> starters = m_Hero.StarterIds();
            for (usize i = 0; i < starters.size() && i < static_cast<usize>(kMaxHotkeys); ++i) {
                const ClassDef* starter = m_Data.FindClass(starters[i]);
                if (!help.empty())
                    help += "    ";
                help += std::to_string(static_cast<int>(i) + 1) + " ";
                help += starter != nullptr ? starter->Name : starters[i];
            }
            help += "    I PELL    ESC QUIT";
        } else if (spending) {
            help = "1 VITALITY    2 STRENGTH    3 DEFENSE    4 AGILITY    5 FOCUS";
            if (m_Hero.ChoicePending())
                help += "    6 RESET BUILD";
            help += "    I PELL    ESC QUIT";
        } else if (choosing) {
            const std::vector<std::string> choices = m_Hero.ChoiceIds();
            for (usize i = 0; i < choices.size() && i < static_cast<usize>(kMaxHotkeys); ++i) {
                const ClassDef* evolution = m_Data.FindClass(choices[i]);
                if (!help.empty())
                    help += "    ";
                help += std::to_string(static_cast<int>(i) + 1) + " ";
                help += evolution != nullptr ? evolution->Name : choices[i];
            }
            if (choices.size() < static_cast<usize>(kMaxHotkeys)) {
                help += "    ";
                help += std::to_string(static_cast<int>(choices.size()) + 1);
                help += " RESET BUILD";
            }
            help += "    I PELL    ESC QUIT";
        } else {
            if (shownSkills == 1)
                help = "1 SKILL    ";
            else if (shownSkills > 1)
                help = "1-" + std::to_string(shownSkills) + " SKILL    ";
            const int drinkKeys = static_cast<int>(m_Inventory.ConsumableSlots().size());
            const int drinkFirst = shownSkills + 1;
            const int drinkLast = shownSkills + drinkKeys;
            if (drinkKeys > 0 && drinkFirst <= kMaxHotkeys) {
                const int shownLast = drinkLast < kMaxHotkeys ? drinkLast : kMaxHotkeys;
                if (drinkFirst == shownLast)
                    help += std::to_string(drinkFirst) + " ITEM    ";
                else
                    help +=
                        std::to_string(drinkFirst) + "-" + std::to_string(shownLast) + " ITEM    ";
            }
            if (!fightOver && packCount > 1)
                help += "LEFT RIGHT TARGET    ";
            if (!fightOver)
                help += "C ITEMS    ";
            help += "SPACE WAIT    ";
            if (fightOver)
                help += "I PELL    ";
            help +=
                "R FIGHT    N NEXT FOE    ESC QUIT    REV " + std::to_string(m_Data.GetRevision());
        }
        Emerald::DrawDebugText(renderer, {48.0f, height - 36.0f}, Upper(help), kDim, 1,
                               width - 96.0f);
    }

    renderer.End();
}

std::string StockLine(const ItemDef& item)
{
    std::string line = item.Name + "   " + std::to_string(item.Value) + " gold";
    if (item.Slot == GearSlot::Consumable) {
        if (item.Heal > 0)
            line += "   heal " + std::to_string(item.Heal);
        if (item.Focus > 0)
            line += "   focus +" + std::to_string(item.Focus);
        return line;
    }
    const std::string bonus = BonusText(item.Bonus);
    if (!bonus.empty())
        line += "   " + bonus;
    if (item.TwoHanded)
        line += "   two handed";
    return line;
}

void AscensionApp::DrinkAt(i32 bagIndex)
{
    if (m_Battle == nullptr)
        return;
    const ItemStack& stack = m_Inventory.BagAt(bagIndex);
    const ItemDef* item = m_Data.FindItem(stack.Id);
    if (item == nullptr)
        return;
    const ActionResult drunk = m_Battle->UseItem(*item);
    if (drunk.Accepted && drunk.SkillId == item->Id) {
        std::string why;
        if (!m_Inventory.Take(bagIndex, why))
            Note(why);
    }
    Note(drunk);
    FinishAction(drunk, false);
    ResolveFoe();
    if (drunk.Accepted)
        m_Picking = false;
}

void AscensionApp::HandlePicker(SDL_Scancode scancode)
{
    const std::vector<i32> drinks = m_Inventory.ConsumableSlots();
    if (drinks.empty()) {
        m_Picking = false;
        Note("The bag has no drinks.");
        return;
    }
    if (m_PickRow < 0)
        m_PickRow = 0;
    if (m_PickRow >= static_cast<i32>(drinks.size()))
        m_PickRow = static_cast<i32>(drinks.size()) - 1;

    if (scancode == kItemKey) {
        m_Picking = false;
        return;
    }
    if (scancode == SDL_SCANCODE_UP) {
        if (m_PickRow > 0)
            --m_PickRow;
        return;
    }
    if (scancode == SDL_SCANCODE_DOWN) {
        if (m_PickRow + 1 < static_cast<i32>(drinks.size()))
            ++m_PickRow;
        return;
    }

    const int digit = static_cast<int>(scancode) - static_cast<int>(SDL_SCANCODE_1);
    i32 pick = -1;
    if (digit >= 0 && digit < kMaxHotkeys && digit < static_cast<int>(drinks.size()))
        pick = digit;
    if (scancode == SDL_SCANCODE_E || scancode == SDL_SCANCODE_RETURN)
        pick = m_PickRow;
    if (pick < 0)
        return;
    DrinkAt(drinks[static_cast<usize>(pick)]);
}

void AscensionApp::ClampCamp()
{
    if (m_CampColumn < 0)
        m_CampColumn = 0;
    if (m_CampColumn > 1)
        m_CampColumn = 1;
    i32 rows = static_cast<i32>(m_Data.GetShop().Stock.size());
    if (m_CampColumn == 0)
        rows = 4 + m_Inventory.BagSlots();
    if (rows < 1)
        m_CampRow = 0;
    else if (m_CampRow >= rows)
        m_CampRow = rows - 1;
    else if (m_CampRow < 0)
        m_CampRow = 0;
}

void AscensionApp::HandleCamp(SDL_Scancode scancode)
{
    ClampCamp();
    const ShopDef& shop = m_Data.GetShop();
    if (scancode == SDL_SCANCODE_UP) {
        --m_CampRow;
        ClampCamp();
        return;
    }
    if (scancode == SDL_SCANCODE_DOWN) {
        ++m_CampRow;
        ClampCamp();
        return;
    }
    if (scancode == SDL_SCANCODE_LEFT || scancode == SDL_SCANCODE_RIGHT) {
        m_CampColumn = m_CampColumn == 0 ? 1 : 0;
        ClampCamp();
        return;
    }

    const bool confirm = scancode == SDL_SCANCODE_E || scancode == SDL_SCANCODE_RETURN;
    std::string why;
    if (m_CampColumn == 1 && confirm) {
        if (m_CampRow < 0 || m_CampRow >= static_cast<i32>(shop.Stock.size()))
            return;
        const std::string& stocked = shop.Stock[static_cast<usize>(m_CampRow)];
        const ItemDef* goods = m_Data.FindItem(stocked);
        if (goods == nullptr) {
            Note("That item is gone.");
            return;
        }
        if (!m_Inventory.Buy(stocked, why)) {
            Note(why);
            return;
        }
        Note("Bought " + goods->Name + " for " + std::to_string(goods->Value) + " gold.");
        return;
    }

    if (m_CampColumn == 0 && m_CampRow >= 0 && m_CampRow < 4 &&
        (confirm || scancode == SDL_SCANCODE_U)) {
        const GearSlot wornSlots[] = {GearSlot::Weapon, GearSlot::Offhand, GearSlot::Armor,
                                      GearSlot::Accessory};
        const GearSlot slot = wornSlots[m_CampRow];
        const std::string worn = m_Inventory.Equipped(slot);
        if (!m_Inventory.Unequip(slot, why)) {
            Note(why);
            return;
        }
        const ItemDef* piece = m_Data.FindItem(worn);
        Note("Put " + std::string(piece != nullptr ? piece->Name : worn) + " in the bag.");
        if (!why.empty())
            Note(why);
        return;
    }

    if (m_CampColumn != 0 || m_CampRow < 4)
        return;
    const i32 bagIndex = m_CampRow - 4;
    const ItemStack stack = m_Inventory.BagAt(bagIndex);
    const ItemDef* held = stack.Empty() ? nullptr : m_Data.FindItem(stack.Id);
    if (scancode == SDL_SCANCODE_X) {
        if (!m_Inventory.Sell(bagIndex, why)) {
            Note(why);
            return;
        }
        const std::string sold = held != nullptr ? held->Name : stack.Id;
        const i32 paid = held != nullptr ? SellPrice(shop, held->Value) : 0;
        Note("Sold " + sold + " for " + std::to_string(paid) + " gold.");
        return;
    }
    if (!confirm)
        return;
    if (!m_Inventory.Equip(bagIndex, why)) {
        Note(why);
        return;
    }
    const std::string wornName = held != nullptr ? held->Name : stack.Id;
    Note("Equipped " + wornName + ".");
}

void AscensionApp::DrawCamp(Emerald::Renderer2D& renderer, f32 width, f32 height)
{
    const Emerald::Vec2 left{28.0f, 24.0f};
    const Emerald::Vec2 leftSize{600.0f, height - 188.0f};
    const Emerald::Vec2 right{648.0f, 24.0f};
    const Emerald::Vec2 rightSize{width - 676.0f, height - 188.0f};
    DrawPanel(renderer, left, leftSize);
    DrawPanel(renderer, right, rightSize);
    DrawPanel(renderer, {28.0f, height - 148.0f}, {width - 56.0f, 124.0f});

    ClampCamp();
    const ShopDef& shop = m_Data.GetShop();
    const bool bagSide = m_CampColumn == 0;
    f32 y = left.y + 16.0f;
    Emerald::DrawDebugText(renderer, {left.x + 24.0f, y}, Upper(shop.Name), kFrame, 2);
    y += Emerald::DebugTextLineHeight(2) + 2.0f;
    Emerald::DrawDebugText(renderer, {left.x + 24.0f, y},
                           Upper(shop.Keeper + "    gold " + std::to_string(m_Inventory.Gold())),
                           kInk, 2);
    y += Emerald::DebugTextLineHeight(2) + 8.0f;
    Emerald::DrawDebugText(renderer, {left.x + 24.0f, y}, "WORN AND BAG", bagSide ? kFrame : kDim,
                           2);
    y += Emerald::DebugTextLineHeight(2) + 4.0f;

    const GearSlot wornSlots[] = {GearSlot::Weapon, GearSlot::Offhand, GearSlot::Armor,
                                  GearSlot::Accessory};
    const char* wornLabels[] = {"WEAPON", "OFFHAND", "ARMOR", "ACCESSORY"};
    for (i32 row = 0; row < 4; ++row) {
        const bool marked = bagSide && m_CampRow == row;
        std::string rowText = marked ? "> " : "  ";
        rowText += wornLabels[row];
        rowText += "  ";
        if (wornSlots[row] == GearSlot::Offhand && m_Inventory.OffhandBlocked()) {
            rowText += "BOTH HANDS";
        } else {
            const std::string& worn = m_Inventory.Equipped(wornSlots[row]);
            if (worn.empty())
                rowText += "EMPTY";
            else {
                const ItemDef* piece = m_Data.FindItem(worn);
                rowText += piece != nullptr ? piece->Name : worn;
            }
        }
        Emerald::DrawDebugText(renderer, {left.x + 24.0f, y}, Upper(rowText),
                               marked ? kFrame : kInk, 1, leftSize.x - 48.0f);
        y += Emerald::DebugTextLineHeight(1) + 2.0f;
    }
    y += 6.0f;
    for (i32 slot = 0; slot < m_Inventory.BagSlots(); ++slot) {
        const bool marked = bagSide && m_CampRow == slot + 4;
        const ItemStack& stack = m_Inventory.BagAt(slot);
        std::string rowText = marked ? "> " : "  ";
        if (stack.Empty()) {
            rowText += "EMPTY";
        } else {
            const ItemDef* held = m_Data.FindItem(stack.Id);
            rowText += held != nullptr ? held->Name : stack.Id;
            rowText += " x" + std::to_string(stack.Count);
            if (held != nullptr)
                rowText += "   sell " + std::to_string(SellPrice(shop, held->Value));
        }
        Emerald::DrawDebugText(renderer, {left.x + 24.0f, y}, Upper(rowText),
                               marked ? kFrame : kDim, 1, leftSize.x - 48.0f);
        y += Emerald::DebugTextLineHeight(1) + 1.0f;
    }

    y = right.y + 16.0f;
    Emerald::DrawDebugText(renderer, {right.x + 24.0f, y}, "STOCK", bagSide ? kDim : kFrame, 2);
    y += Emerald::DebugTextLineHeight(2) + 8.0f;
    for (i32 row = 0; row < static_cast<i32>(shop.Stock.size()); ++row) {
        const bool marked = !bagSide && m_CampRow == row;
        const ItemDef* goods = m_Data.FindItem(shop.Stock[static_cast<usize>(row)]);
        std::string rowText = marked ? "> " : "  ";
        if (goods == nullptr)
            rowText += shop.Stock[static_cast<usize>(row)];
        else
            rowText += StockLine(*goods);
        Emerald::DrawDebugText(renderer, {right.x + 24.0f, y}, Upper(rowText),
                               marked ? kFrame : kInk, 1, rightSize.x - 48.0f);
        y += Emerald::DebugTextLineHeight(1) + 6.0f;
    }

    Emerald::DrawDebugText(renderer, {48.0f, height - 132.0f}, "OLD PELL'S PROVISIONS", kFrame, 2);
    f32 logY = height - 96.0f;
    for (const std::string& note : m_Log) {
        Emerald::DrawDebugText(renderer, {48.0f, logY}, Upper(note), kDim, 1, width - 96.0f);
        logY += Emerald::DebugTextLineHeight(1) + 1.0f;
    }
    const char* help = "UP DOWN MOVE    LEFT RIGHT BAG OR STOCK    E EQUIP OR BUY    "
                       "X SELL    U UNEQUIP    I CLOSE    R FIGHT    N NEXT    ESC QUIT";
    if (m_Hero.ChoicePending()) {
        help = "UP DOWN MOVE    LEFT RIGHT BAG OR STOCK    E EQUIP OR BUY    "
               "X SELL    U UNEQUIP    I CLOSE    CHOOSE A PATH BEFORE THE NEXT FIGHT    ESC QUIT";
    }
    Emerald::DrawDebugText(renderer, {48.0f, height - 36.0f}, help, kDim, 1, width - 96.0f);
}

} // namespace Ascension
