#include "AscensionApp.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include "FoeAlert.h"
#include "GearLine.h"
#include "MapView.h"
#include "SourceTag.h"

#include <SDL3/SDL.h>

#include "Emerald/Input/InputBindings.h"
#include "Emerald/Math/Mat4.h"
#include "Emerald/Renderer/DebugText.h"
#include "Emerald/Renderer/Sprite.h"
#include "Emerald/Renderer/TextureAtlas.h"

namespace Ascension {

namespace {

constexpr f32 kPollSeconds = 0.25f;
constexpr usize kLogLines = 6;
constexpr int kMaxHotkeys = 8;
// Enemy actions finished inside one scripted key before the press is applied. A round is
// at most a handful of foes; the cap is several rounds so a leftover foe turn cannot eat
// the next key, and it still happens on that key's frame.
constexpr i32 kScriptPumpPasses = 4;

bool SameIds(const std::vector<std::string>& left, const std::vector<std::string>& right)
{
    if (left.size() != right.size())
        return false;
    for (usize index = 0; index < left.size(); ++index) {
        if (left[index] != right[index])
            return false;
    }
    return true;
}

// Directions the map and the menus share. The keys and buttons are input.json.
struct MoveAction {
    std::string_view Action;
    WalkDir Direction;
};

const MoveAction kMoves[] = {
    {kMoveNorth, WalkDir::North},
    {kMoveEast, WalkDir::East},
    {kMoveSouth, WalkDir::South},
    {kMoveWest, WalkDir::West},
};

FightStanding FightStandingOf(const Battle* battle)
{
    if (battle == nullptr)
        return FightStanding::None;
    switch (battle->Result()) {
    case BattleResult::Victory:
        return FightStanding::Victory;
    case BattleResult::Defeat:
        return FightStanding::Defeat;
    case BattleResult::Ongoing:
        return FightStanding::Ongoing;
    case BattleResult::Fled:
        return FightStanding::None;
    }
    return FightStanding::Ongoing;
}

constexpr Emerald::Vec4 kInk{0.93f, 0.91f, 0.84f, 1.0f};
constexpr Emerald::Vec4 kDim{0.62f, 0.66f, 0.60f, 1.0f};
constexpr Emerald::Vec4 kFrame{0.72f, 0.64f, 0.38f, 1.0f};
constexpr Emerald::Vec4 kPanel{0.10f, 0.12f, 0.11f, 1.0f};
constexpr Emerald::Vec4 kError{0.95f, 0.45f, 0.38f, 1.0f};
constexpr Emerald::Vec4 kFigure{0.78f, 0.62f, 0.32f, 1.0f};
constexpr Emerald::Vec4 kFoe{0.62f, 0.28f, 0.24f, 1.0f};
constexpr Emerald::Vec4 kHp{0.78f, 0.32f, 0.30f, 1.0f};
constexpr Emerald::Vec4 kFocus{0.66f, 0.46f, 0.84f, 1.0f};

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

// A pack battle image is drawn when the manifest lists this foe. Otherwise a foe
// with a data color is a square of that color, and anyone else keeps the figure.
void DrawFoeBody(Emerald::Renderer2D& renderer, const Database& data, const ArtGallery* art,
                 const CombatantState& foe, const Emerald::Vec2& origin, f32 size, bool alive,
                 const Emerald::Vec4& fallback)
{
    const Emerald::Vec4 white{1.0f, 1.0f, 1.0f, 1.0f};
    if (art != nullptr && !art->IsEmpty() &&
        DrawArt(renderer, art, ArtSlot::FoeBattle, foe.Id, origin, {size * 4.0f, size * 6.5f},
                alive ? white : kDim))
        return;
    const EnemyDef* def = data.FindEnemy(foe.Id);
    if (def != nullptr && def->HasColor) {
        const Emerald::Vec4 tint = alive ? Emerald::Vec4{def->Red / 255.0f, def->Green / 255.0f,
                                                         def->Blue / 255.0f, 1.0f}
                                         : kDim;
        const f32 side = size * 4.0f;
        renderer.FillRect(origin + Emerald::Vec2{0.0f, size * 1.25f}, {side, side}, tint);
        return;
    }
    DrawFigure(renderer, origin, size, alive ? fallback : kDim);
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

// The stretch named on this encounter. Blank for a fight with no zone, and for a foe
// started on its own rather than from the encounter list.
std::string FightZone(const Database& data, i32 index, bool loneEnemy)
{
    if (loneEnemy)
        return {};
    const std::span<const EncounterDef> encounters = data.GetEncounters();
    if (index < 0 || index >= static_cast<i32>(encounters.size()))
        return {};
    return encounters[static_cast<usize>(index)].Zone;
}

// Draws `zone` and returns where the next line starts. An empty zone leaves `at` alone,
// so a fight that names none keeps its title where it was.
f32 DrawZone(Emerald::Renderer2D& renderer, const std::string& zone, const Emerald::Vec2& at,
             int scale)
{
    if (zone.empty())
        return at.y;
    Emerald::DrawDebugText(renderer, at, Upper(zone), kDim, scale);
    return at.y + Emerald::DebugTextLineHeight(scale) + 2.0f;
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

// The keeper's first word, for the help key and the "waits between battles" note.
std::string KeeperWord(const ShopDef& shop)
{
    const usize space = shop.Keeper.find(' ');
    if (space == std::string::npos)
        return shop.Keeper;
    return shop.Keeper.substr(0, space);
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

// The hero's skills right now: class grants, kept picks, then worn gear. Empty when the
// class is gone.
std::vector<HeroGrant> CurrentGrants(const Database& data, std::string_view classId,
                                     const Inventory& inventory,
                                     std::span<const std::string> pickedIds)
{
    const ClassDef* heroClass = data.FindClass(classId);
    if (heroClass == nullptr)
        return {};
    return data.HeroGrants(*heroClass, inventory.WornIds(), pickedIds);
}

bool ClassHasActive(const Database& data, std::string_view classId, std::string_view skillId)
{
    const ClassDef* klass = data.FindClass(classId);
    if (klass == nullptr)
        return false;
    for (const SkillDef* skill : data.ActiveSkills(*klass)) {
        if (skill->Id == skillId)
            return true;
    }
    return false;
}

std::string SkillName(const Database& data, std::string_view id)
{
    const SkillDef* skill = data.FindSkill(id);
    if (skill == nullptr)
        return std::string(id);
    return skill->Name;
}

// The option as the class offers it, tune included. Null when that id is not a pick.
const SkillGrant* OfferedGrant(const Database& data, std::string_view classId,
                               std::string_view skillId)
{
    const ClassDef* klass = data.FindClass(classId);
    if (klass == nullptr)
        return nullptr;
    for (const SkillPick* pick : data.PicksOf(*klass)) {
        for (const SkillGrant& option : pick->Options) {
            if (option.Id == skillId)
                return &option;
        }
    }
    return nullptr;
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

// Wind-up, a raised guard, a defense buff that is still counting down, a summoned add
// that is about to join, and statuses. The y that comes back is just under the last line.
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
    const std::string guardAlert = FoeGuardAlert(who);
    if (!guardAlert.empty()) {
        alertY = Emerald::DrawDebugText(renderer, {origin.x, alertY}, Upper(guardAlert), kError,
                                        scale, wrap);
        alertY += 4.0f;
    }
    const std::string defenseAlert = FoeDefenseAlert(data, who);
    if (!defenseAlert.empty()) {
        alertY = Emerald::DrawDebugText(renderer, {origin.x, alertY}, Upper(defenseAlert), kError,
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

AscensionApp::AscensionApp(const Emerald::ApplicationSpec& spec, PlaytestOptions options)
    : Emerald::Application(spec), m_Play(std::move(options)), m_Data(PlaytestDataDirectory(m_Play))
{
}

void AscensionApp::Note(std::string line)
{
    if (line.empty())
        return;
    if (m_Transcript.is_open()) {
        m_Transcript << line << '\n';
        m_Transcript.flush();
    }
    m_Log.push_back(std::move(line));
    while (m_Log.size() > kLogLines)
        m_Log.erase(m_Log.begin());
}

void AscensionApp::Note(const ActionResult& result)
{
    // Battle text is the multi-enemy log: Cleave names each hit, and a summon says
    // "Rubble Shard 2 joins the fight." The transcript is this same line.
    Note(result.Text);
    // --trace stays on stderr so a transcript diff still compares the combat log alone.
    if (!m_Play.Trace || m_Battle == nullptr)
        return;
    const std::string state = m_Battle->TraceState();
    if (state.empty())
        return;
    std::fprintf(stderr, "%s | %s\n", state.c_str(), result.Text.c_str());
    std::fflush(stderr);
}

void AscensionApp::NoteMarked(i32 previous)
{
    if (m_Battle == nullptr)
        return;
    const auto foes = m_Battle->Enemies();
    if (foes.size() < 2)
        return;
    const i32 marked = m_Battle->TargetIndex();
    if (marked == previous || marked < 0 || marked >= static_cast<i32>(foes.size()))
        return;
    Note(MarkedTargetLine(foes[static_cast<usize>(marked)].Name));
}

void AscensionApp::PushRanks(std::string_view pendingSkill)
{
    if (m_Battle == nullptr || !m_Hero.IsReady())
        return;
    std::vector<SkillRank> ranks;
    for (const std::string& id : m_Battle->PlayerSkillIds()) {
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
    std::vector<std::string> killed;
    for (const CombatantState& foe : m_Battle->Enemies())
        killed.push_back(foe.Id);
    const std::vector<std::string> found = m_Inventory.TakeDrops(killed, m_Found);
    for (const std::string& line : found)
        Note(line);
    if (grant.LevelAfter > grant.LevelBefore)
        Note("Level " + std::to_string(grant.LevelAfter) + ".");
    const PoolCaps fought{m_Battle->Player().MaxHp, m_Battle->Player().MaxFocus};
    KeepPools(m_Vitals, m_Battle->Player().Hp, m_Battle->Player().Focus, fought);
    if (grant.LevelAfter > grant.LevelBefore) {
        const i32 beforeHp = m_Vitals.Hp;
        const PoolCaps now = HeroPools(m_Data, m_Hero, m_Inventory);
        ApplyLevelHp(m_Vitals, fought.MaxHp, now.MaxHp, m_Data.GetProgress().LevelHp);
        ClampFocus(m_Vitals, now.MaxFocus);
        if (m_Vitals.Hp > beforeHp)
            Note("HP +" + std::to_string(m_Vitals.Hp - beforeHp) + ".");
    }
    m_VitalsSettled = true;
    if (grant.ChoiceOpened) {
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
    if (grant.PickOpened) {
        std::string line = "Choose a skill.";
        const std::vector<std::string> options = m_Hero.PickOptions();
        for (usize i = 0; i < options.size() && i < static_cast<usize>(kMaxHotkeys); ++i) {
            line += " " + std::to_string(static_cast<int>(i) + 1) + " ";
            line += SkillName(m_Data, options[i]);
            line += ".";
        }
        Note(line);
    }
    if (grant.ChoiceOpened || grant.PickOpened)
        TakeAutoPath();
    RememberVictory();
    m_Standing = RunStanding::Victory;
    WriteSave();
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
    else if (result.Result == BattleResult::Defeat)
        RememberDefeat();
    MaybeSettle();
}

void AscensionApp::ResolveFoe()
{
    if (m_Battle == nullptr)
        return;
    for (i32 step = 0; step < 8 && m_Battle != nullptr &&
                       m_Battle->Result() == BattleResult::Ongoing &&
                       m_Battle->Actor() == CombatantSide::Enemy;
         ++step) {
        const i32 markedBefore = m_Battle->TargetIndex();
        const ActionResult act = m_Battle->EnemyAct();
        Note(act);
        NoteMarked(markedBefore);
        FinishAction(act, false);
    }
}

bool AscensionApp::OpenBattle(std::string_view fightId, std::string& error)
{
    const PoolPair pools = PoolsFrom(m_Vitals, HeroPools(m_Data, m_Hero, m_Inventory));
    OpeningPools opening;
    opening.Hp = pools.Hp;
    opening.Focus = pools.Focus;
    return m_Battle->Start(m_Hero.ClassId(), m_Hero.Level(), fightId, opening, error);
}

bool AscensionApp::OpenRoster(std::span<const std::string> foeIds, std::string& error)
{
    const PoolPair pools = PoolsFrom(m_Vitals, HeroPools(m_Data, m_Hero, m_Inventory));
    OpeningPools opening;
    opening.Hp = pools.Hp;
    opening.Focus = pools.Focus;
    return m_Battle->Start(m_Hero.ClassId(), m_Hero.Level(), m_MapFight.EncounterId, foeIds,
                           opening, error);
}

void AscensionApp::ApplyOpeningStatuses()
{
    if (m_Battle == nullptr)
        return;
    const i32 seats = static_cast<i32>(m_MapFight.Seats.size());
    for (i32 index = 0; index < seats; ++index) {
        const PartyFoe& seat = m_MapFight.Seats[static_cast<usize>(index)];
        for (const std::string& status : seat.Statuses) {
            std::string error;
            if (m_Battle->GrantStatus(index, status, error))
                continue;
            if (!error.empty())
                Note(error);
        }
    }
}

void AscensionApp::SyncPlaces()
{
    m_Places.clear();
    m_PlacesMap.clear();
    if (!m_Walker.Placed()) {
        m_Glides.Snap(m_Places);
        return;
    }
    const MapDef* map = m_Data.FindMap(m_Walker.Place().MapId);
    if (map == nullptr) {
        m_Glides.Snap(m_Places);
        return;
    }
    PlaceSpawns(*map, m_MapMemory, m_Places);
    m_PlacesMap = map->Id;
    m_Glides.Snap(m_Places);
}

void AscensionApp::StepMovers()
{
    if (!m_Walker.Placed())
        return;
    if (m_PlacesMap != m_Walker.Place().MapId)
        SyncPlaces();
    const MapDef* map = m_Data.FindMap(m_Walker.Place().MapId);
    if (map == nullptr)
        return;
    const std::vector<SpawnPlace> before = m_Places;
    MoverContext context;
    context.Hero = m_Walker.Place().Tile;
    if (m_Hero.IsReady() && m_Inventory.IsReady()) {
        const PoolCaps caps = HeroPools(m_Data, m_Hero, m_Inventory);
        const PoolPair pools = PoolsFrom(m_Vitals, caps);
        context.HeroHp = pools.Hp;
        context.HeroMaxHp = caps.MaxHp;
    }
    if (!StepSpawnMovers(m_Data, *map, m_MapMemory, context, m_Places) || SpawnFightOpen())
        return;
    for (const SpawnPlace& place : m_Places) {
        const SpawnPlace* older = nullptr;
        for (const SpawnPlace& prior : before) {
            if (prior.Id == place.Id)
                older = &prior;
        }
        if (older == nullptr)
            continue;
        if (older->Tile.X == place.Tile.X && older->Tile.Y == place.Tile.Y)
            continue;
        if (place.Tile.X != context.Hero.X || place.Tile.Y != context.Hero.Y)
            continue;
        ContactQuery query;
        query.Tile = context.Hero;
        TouchSpawn(query);
        return;
    }
}

void AscensionApp::RememberDefeat()
{
    if (m_VitalsSettled || m_Battle == nullptr)
        return;
    LoseToHub(m_Vitals, HeroPools(m_Data, m_Hero, m_Inventory));
    m_VitalsSettled = true;
    m_Standing = RunStanding::Defeat;
}

void AscensionApp::NoteRest()
{
    if (!m_Walker.Placed() || !m_Hero.IsReady() || !m_Inventory.IsReady())
        return;
    const MapDef* map = m_Data.FindMap(m_Walker.Place().MapId);
    if (map == nullptr)
        return;
    const MapTile tile = m_Walker.Place().Tile;
    if (m_RestSeen && m_RestMap == map->Id && m_RestTile.X == tile.X && m_RestTile.Y == tile.Y)
        return;
    m_RestSeen = true;
    m_RestMap = map->Id;
    m_RestTile = tile;
    if (!RestOn(*map, tile, m_Vitals, HeroPools(m_Data, m_Hero, m_Inventory)))
        return;
    Note("Rested.");
}

bool AscensionApp::SpawnFightOpen() const
{
    return m_OnMap && m_MapFight.Active && m_Battle != nullptr;
}

void AscensionApp::TouchSpawn(const ContactQuery& query)
{
    if (!m_Walker.Placed() || SpawnFightOpen())
        return;
    const MapDef* map = m_Data.FindMap(m_Walker.Place().MapId);
    if (map == nullptr)
        return;
    const MapFightRequest request = OfferSpawn(m_Data, *map, query, m_MapMemory, m_Places);
    if (!request.Active)
        return;
    m_MapFight.Active = true;
    m_MapFight.SpawnId = request.SpawnId;
    m_MapFight.EncounterId = request.EncounterId;
    m_MapFight.MapId = request.MapId;
    m_MapFight.SpawnTile = request.SpawnTile;
    m_MapFight.ReturnTile = request.ReturnTile;
    m_MapFight.Once = request.Once;
    m_MapFight.AllowFlee = request.AllowFlee;
    m_MapFight.Foes = request.Foes;
    m_MapFight.Seats = request.Seats;
    BeginSpawnFight();
}

void AscensionApp::BeginSpawnFight()
{
    if (!m_MapFight.Active)
        return;
    if (const char* block = m_Hero.NextFightBlock()) {
        if (m_Log.empty() || m_Log.back() != block)
            Note(block);
        m_MapFight.Active = false;
        return;
    }
    m_Log.clear();
    m_StartError.clear();
    m_XpAwarded = false;
    m_VitalsSettled = false;
    m_Camp = false;
    m_Talk = false;
    m_Picking = false;
    m_CommandRow = 0;
    if (!m_Data.IsLoaded() || !m_Hero.IsReady()) {
        m_MapFight.Active = false;
        return;
    }
    if (m_Battle == nullptr)
        m_Battle = std::make_unique<Battle>(m_Data, std::make_unique<AgilityRoundModel>());
    m_Battle->SetGear(m_Inventory.GearBonus());
    m_Battle->SetWornItems(m_Inventory.WornIds());
    m_Battle->SetPickedSkills(m_Hero.PickedIds());
    m_Battle->SetBaseStats(m_Hero.Attributes());
    std::string error;
    const EncounterDef* encounter = m_Data.FindEncounter(m_MapFight.EncounterId);
    const bool listed = encounter != nullptr && !m_MapFight.Foes.empty() &&
                        SameIds(m_MapFight.Foes, encounter->Enemies);
    const bool opened = listed || m_MapFight.Foes.empty()
                            ? OpenBattle(m_MapFight.EncounterId, error)
                            : OpenRoster(m_MapFight.Foes, error);
    if (!opened) {
        m_StartError = error;
        Note(error);
        m_Battle.reset();
        m_MapFight.Active = false;
        return;
    }
    m_Battle->SetFleeAllowed(m_MapFight.AllowFlee);
    if (m_ResumeFight.Known) {
        const FightSnapshot shot = std::move(m_ResumeFight);
        m_ResumeFight = {};
        m_ResumeSpawn.clear();
        std::string restoreError;
        if (m_Battle->ImportFight(shot, restoreError)) {
            m_ResumeResources = false;
            m_AnnounceResume = false;
            m_Standing = RunStanding::Ongoing;
            PushRanks();
            Note("Resumed.");
            return;
        }
    }
    ApplyOpeningStatuses();
    if (const FledWounds* wounds = WoundsOf(m_MapMemory, m_MapFight.SpawnId))
        ApplyFoeWounds(*m_Battle, *wounds);
    m_Standing = RunStanding::Ongoing;
    PushRanks();
    if (m_Battle->Result() != BattleResult::Ongoing) {
        if (m_Battle->Result() == BattleResult::Defeat)
            RememberDefeat();
        else if (m_Battle->Result() == BattleResult::Victory) {
            const PoolCaps fought{m_Battle->Player().MaxHp, m_Battle->Player().MaxFocus};
            KeepPools(m_Vitals, m_Battle->Player().Hp, m_Battle->Player().Focus, fought);
            m_VitalsSettled = true;
        }
        Note(m_Battle->Result() == BattleResult::Victory ? "Victory." : "Defeat.");
        MaybeSettle();
        return;
    }
    if (m_Battle->Actor() == CombatantSide::Enemy) {
        const i32 acting = m_Battle->ActorIndex();
        const auto foes = m_Battle->Enemies();
        const std::string& name = acting >= 0 && acting < static_cast<i32>(foes.size())
                                      ? foes[static_cast<usize>(acting)].Name
                                      : m_Battle->Enemy().Name;
        Note(name + " moves first.");
    } else {
        Note("You move first.");
    }
    ResolveFoe();
}

void AscensionApp::MaybeSettle()
{
    if (!m_MapFight.Active || m_Battle == nullptr)
        return;
    const BattleResult result = m_Battle->Result();
    if (result == BattleResult::Ongoing)
        return;
    // A win that opened a path stays on that screen. The spawn is settled once
    // the choice is made, so the next step does not start a fight underneath it.
    if (result == BattleResult::Victory && m_Hero.NextFightBlock() != nullptr)
        return;
    if (result == BattleResult::Fled && !m_VitalsSettled) {
        const PoolCaps fought{m_Battle->Player().MaxHp, m_Battle->Player().MaxFocus};
        KeepPools(m_Vitals, m_Battle->Player().Hp, m_Battle->Player().Focus, fought);
        m_VitalsSettled = true;
    }

    ReturnQuery query;
    query.Outcome = MapOutcome::Won;
    if (result == BattleResult::Defeat)
        query.Outcome = MapOutcome::Lost;
    else if (result == BattleResult::Fled)
        query.Outcome = MapOutcome::Fled;
    query.SpawnId = m_MapFight.SpawnId;
    query.FoughtMapId = m_MapFight.MapId;
    query.SpawnTile = m_MapFight.SpawnTile;
    query.ReturnTile = m_MapFight.ReturnTile;
    query.Once = m_MapFight.Once;

    std::vector<FoeWound> wounds;
    if (result == BattleResult::Fled)
        CaptureFoeWounds(*m_Battle, wounds);
    // Gold is not touched. A win already granted it in AwardVictory. A loss and
    // a flee do not reach GrantGold, so the purse stays where it was.
    const MapSettle settled = ApplyOutcome(m_Data, query, m_MapMemory, wounds);
    const bool placed =
        settled.Return.Ok && m_Walker.Place(m_Data, settled.Return.MapId, settled.Return.Tile);
    if (!placed) {
        Note("The way back is gone.");
    } else if (result == BattleResult::Victory && settled.Return.ClearSpawn) {
        Note("The foe is gone.");
    } else if (result == BattleResult::Victory) {
        Note("The foe remains.");
    } else if (result == BattleResult::Fled) {
        Note("The foe holds its ground.");
    } else if (settled.Return.Hub) {
        Note("Back at the hub.");
    } else {
        Note("Back at the start.");
    }
    // The loss flag only means the walk home has not happened yet. Standing on
    // the return tile is that walk, including when the tile is not a rest.
    if (placed && result == BattleResult::Defeat)
        m_Vitals.ReturnToHub = false;

    m_Battle.reset();
    m_MapFight = {};
    SyncPlaces();
    if (result == BattleResult::Victory)
        m_Standing = RunStanding::Victory;
    else if (result == BattleResult::Defeat)
        m_Standing = RunStanding::Defeat;
    else
        m_Standing = RunStanding::None;
    m_Camp = false;
    m_Talk = false;
    m_Picking = false;
    NoteRest();
    WriteSave();
}

void AscensionApp::OpenResumedSpawn()
{
    const MapSpawnRef ref = m_Data.FindSpawn(m_ResumeSpawn);
    if (ref.Map == nullptr || ref.Spawn == nullptr) {
        m_ResumeFight = {};
        m_ResumeSpawn.clear();
        m_Standing = RunStanding::None;
        Note("The spawn is gone.");
        return;
    }
    m_MapFight.Active = true;
    m_MapFight.SpawnId = ref.Spawn->Id;
    m_MapFight.EncounterId = ref.Spawn->Encounter;
    m_MapFight.MapId = ref.Map->Id;
    m_MapFight.SpawnTile = ref.Spawn->Tile;
    m_MapFight.Once = ref.Spawn->Once;
    m_MapFight.AllowFlee = FleeAllowed(m_Data, *ref.Spawn);
    ContactQuery beside;
    beside.Tile = ref.Spawn->Tile;
    const MapFightRequest offer = OfferSpawn(m_Data, *ref.Map, beside, m_MapMemory);
    if (offer.Active)
        m_MapFight.ReturnTile = offer.ReturnTile;
    else
        m_MapFight.ReturnTile = ref.Map->Start;
    BeginSpawnFight();
}

void AscensionApp::BeginFight()
{
    if (const char* block = m_Hero.NextFightBlock()) {
        m_Camp = false;
        m_Talk = false;
        m_Picking = false;
        if (m_Log.empty() || m_Log.back() != block)
            Note(block);
        return;
    }
    m_Log.clear();
    m_StartError.clear();
    // R and N start the fight over. A unique drop already in m_Found is not granted again.
    m_XpAwarded = false;
    m_VitalsSettled = false;
    m_Camp = false;
    m_Talk = false;
    m_Picking = false;
    m_CommandRow = 0;
    if (!m_Data.IsLoaded() || !m_Hero.IsReady())
        return;
    std::string fightId = m_DirectEnemy;
    if (fightId.empty()) {
        const auto encounters = m_Data.GetEncounters();
        if (encounters.empty()) {
            m_StartError = "No encounters in data/.";
            m_Battle.reset();
            return;
        }
        if (m_EnemyIndex < 0 || m_EnemyIndex >= static_cast<i32>(encounters.size()))
            m_EnemyIndex = 0;
        fightId = encounters[static_cast<usize>(m_EnemyIndex)].Id;
    }
    if (m_Battle == nullptr)
        m_Battle = std::make_unique<Battle>(m_Data, std::make_unique<AgilityRoundModel>());

    m_Battle->SetGear(m_Inventory.GearBonus());
    m_Battle->SetWornItems(m_Inventory.WornIds());
    m_Battle->SetPickedSkills(m_Hero.PickedIds());
    m_Battle->SetBaseStats(m_Hero.Attributes());
    std::string error;
    if (!OpenBattle(fightId, error)) {
        m_StartError = error;
        m_ResumeResources = false;
        m_ResumeFight = {};
        return;
    }
    m_Standing = RunStanding::Ongoing;
    PushRanks();
    if (m_ResumeFight.Known) {
        const FightSnapshot shot = std::move(m_ResumeFight);
        m_ResumeFight = {};
        std::string restoreError;
        if (m_Battle->ImportFight(shot, restoreError)) {
            m_ResumeResources = false;
            m_AnnounceResume = false;
            Note("Resumed.");
            return;
        }
        if (!OpenBattle(fightId, restoreError)) {
            m_StartError = restoreError;
            m_ResumeResources = false;
            return;
        }
        PushRanks();
    }
    if (m_ResumeResources) {
        m_Battle->SetPlayerHp(m_ResumeHp);
        m_Battle->SetPlayerFocus(m_ResumeFocus);
        m_ResumeResources = false;
    }
    if (m_Battle->Result() != BattleResult::Ongoing) {
        if (m_Battle->Result() == BattleResult::Defeat)
            RememberDefeat();
        else {
            const PoolCaps fought{m_Battle->Player().MaxHp, m_Battle->Player().MaxFocus};
            KeepPools(m_Vitals, m_Battle->Player().Hp, m_Battle->Player().Focus, fought);
            m_VitalsSettled = true;
        }
        Note(m_Battle->Result() == BattleResult::Victory ? "Victory." : "Defeat.");
        return;
    }
    if (m_Battle->Actor() == CombatantSide::Enemy) {
        const i32 acting = m_Battle->ActorIndex();
        const auto foes = m_Battle->Enemies();
        const std::string& name = acting >= 0 && acting < static_cast<i32>(foes.size())
                                      ? foes[static_cast<usize>(acting)].Name
                                      : m_Battle->Enemy().Name;
        Note(name + " moves first.");
    } else {
        Note("You move first.");
    }
    ResolveFoe();
    if (m_AnnounceResume) {
        Note("Resumed.");
        m_AnnounceResume = false;
    }
}

void AscensionApp::FillTalkFacts(TalkList lists[2], TalkFacts& facts) const
{
    lists[0].Name = "found";
    lists[0].Ids = m_Found;
    lists[1].Name = "defeated";
    lists[1].Ids = m_Defeated;
    facts.Flags = m_TalkFlags;
    facts.Lists = std::span<const TalkList>(lists, 2);
}

bool AscensionApp::OpenTalk(std::string_view dialogueId)
{
    if (dialogueId.empty())
        return false;
    const std::string id(dialogueId);
    const DialogueDef* talk = m_Data.FindDialogue(id);
    if (talk == nullptr)
        return false;
    TalkList lists[2];
    TalkFacts facts;
    FillTalkFacts(lists, facts);
    const DialogueNode* node = TalkEntry(*talk, facts);
    if (node == nullptr)
        return false;
    TalkApplySets(node->Sets, m_TalkFlags);
    m_Talk = true;
    m_TalkId = id;
    m_TalkNode = node->Id;
    m_TalkRow = 0;
    m_Camp = false;
    m_Picking = false;
    NoteTalk(*node);
    return true;
}

void AscensionApp::NoteTalk(const DialogueNode& node)
{
    std::vector<std::string> lines;
    TalkBoxLines(node.Line, kTalkLineWidth, lines);
    for (const std::string& line : lines)
        Note(line);
}

void AscensionApp::RunTalkAction(std::string_view action)
{
    m_Talk = false;
    m_TalkRow = 0;
    if (action == "shop_open") {
        m_Camp = true;
        m_CampColumn = 0;
        m_CampRow = 0;
        m_Picking = false;
    }
}

void AscensionApp::HandleTalk(std::string_view action)
{
    if (action == kInteract || action == kCancel) {
        m_Talk = false;
        return;
    }
    const DialogueDef* talk = m_Data.FindDialogue(m_TalkId);
    const DialogueNode* node = talk != nullptr ? talk->FindNode(m_TalkNode) : nullptr;
    if (talk == nullptr || node == nullptr) {
        m_Talk = false;
        return;
    }
    TalkList lists[2];
    TalkFacts facts;
    FillTalkFacts(lists, facts);
    std::vector<i32> visible;
    TalkVisible(*talk, *node, facts, visible);
    if (m_TalkRow >= static_cast<i32>(visible.size()))
        m_TalkRow = visible.empty() ? 0 : static_cast<i32>(visible.size()) - 1;
    if (m_TalkRow < 0)
        m_TalkRow = 0;

    if (action == kMoveNorth || action == kCyclePrev) {
        if (m_TalkRow > 0)
            --m_TalkRow;
        return;
    }
    if (action == kMoveSouth || action == kCycleNext) {
        if (m_TalkRow + 1 < static_cast<i32>(visible.size()))
            ++m_TalkRow;
        return;
    }

    i32 pick = -1;
    const int digit = SlotIndex(action);
    if (digit >= 0 && digit < kMaxHotkeys && digit < static_cast<int>(visible.size()))
        pick = digit;
    else if (action == kConfirm)
        pick = visible.empty() ? -1 : m_TalkRow;
    else
        return;

    TalkMove move;
    if (!TalkChoose(*talk, m_TalkNode, m_TalkFlags, facts, pick, move))
        return;
    if (move.Step == TalkStep::Ended) {
        m_Talk = false;
        m_TalkRow = 0;
        return;
    }
    if (move.Step == TalkStep::Action) {
        RunTalkAction(move.Action);
        return;
    }
    m_TalkRow = 0;
    const DialogueNode* next = talk->FindNode(m_TalkNode);
    if (next == nullptr) {
        m_Talk = false;
        return;
    }
    NoteTalk(*next);
}

bool AscensionApp::UseMapPoint()
{
    if (!m_Walker.Placed())
        return false;
    const MapDef* map = m_Data.FindMap(m_Walker.Place().MapId);
    if (map == nullptr)
        return false;
    const MapTile tile = m_Walker.Place().Tile;
    const MapPoint* point = nullptr;
    for (const MapPoint& candidate : map->Points) {
        if (candidate.Tile.X == tile.X && candidate.Tile.Y == tile.Y)
            point = &candidate;
    }
    if (point == nullptr || point->Kind == MapPointKind::Rest)
        return false;
    if (point->Kind == MapPointKind::Talk)
        return OpenTalk(point->Dialogue);
    m_Camp = true;
    m_CampColumn = 0;
    m_CampRow = 0;
    m_Picking = false;
    m_Talk = false;
    return true;
}

bool AscensionApp::EnterOverland(std::string_view preferred)
{
    bool stood = false;
    if (m_HasPlace)
        stood = m_Walker.Place(m_Data, m_PlaceMap, m_PlaceTile);
    if (!stood && m_HasPlace)
        stood = m_Walker.Enter(m_Data, m_PlaceMap);
    if (!stood && !preferred.empty())
        stood = m_Walker.Enter(m_Data, preferred);
    // --map names the room on its own. The hub is only the default launch, and
    // the stand-in when a saved map id is no longer in the data.
    if (!stood && (preferred.empty() || m_HasPlace)) {
        for (const MapDef& map : m_Data.GetMaps()) {
            if (!map.Hub)
                continue;
            stood = m_Walker.Enter(m_Data, map.Id);
            break;
        }
    }
    if (!stood)
        return false;
    m_OnMap = true;
    m_HasPlace = true;
    m_PlaceMap = m_Walker.Place().MapId;
    m_PlaceTile = m_Walker.Place().Tile;
    Note("Map " + m_Walker.Place().MapId + ".");
    if (!m_ResumeSpawn.empty() && (m_ResumeFight.Known || m_Standing == RunStanding::Ongoing))
        OpenResumedSpawn();
    else if (m_Standing == RunStanding::Ongoing) {
        // A version 3 fight was the encounter list. It names no spawn, so the
        // map does not open it.
        m_ResumeFight = {};
        m_Standing = RunStanding::None;
    }
    if (!SpawnFightOpen())
        NoteRest();
    SyncPlaces();
    return true;
}

void AscensionApp::OnStart()
{
    if (!m_Play.Transcript.empty()) {
        m_Transcript.open(m_Play.Transcript, std::ios::app);
        if (!m_Transcript) {
            m_StartError = "could not open transcript '" + m_Play.Transcript.string() + "'";
            std::fprintf(stderr, "%s\n", m_StartError.c_str());
            Quit();
            return;
        }
    }
    if (!m_Data.Load()) {
        if (m_Play.UsesData() || !m_Play.Keys.empty() || !m_Play.Transcript.empty() ||
            !m_Play.ArtPackDir.empty()) {
            const std::string& message = m_Data.GetLastError();
            if (!message.empty())
                std::fprintf(stderr, "%s\n", message.c_str());
            Quit();
        }
        return;
    }
    if (!m_Play.ArtPackDir.empty()) {
        ArtPack pack;
        DataError packError;
        if (!pack.Load(m_Play.ArtPackDir, m_Data, &packError)) {
            m_StartError =
                packError.Message.empty() ? "could not load art pack" : packError.Message;
            std::fprintf(stderr, "%s\n", m_StartError.c_str());
            Note(m_StartError);
            Quit();
            return;
        }
        std::string packWhy;
        if (!m_Gallery.Load(GetAssets(), pack, packWhy)) {
            m_StartError = std::move(packWhy);
            std::fprintf(stderr, "%s\n", m_StartError.c_str());
            Note(m_StartError);
            Quit();
            return;
        }
    }
    PlaytestSetup setup;
    std::string why;
    bool resumed = false;
    if (m_Play.LoadsSave())
        resumed = TryResume(why);
    if (!resumed) {
        if (!ApplyPlaytest(m_Hero, m_Inventory, m_Data, m_Play, setup, why)) {
            m_StartError = std::move(why);
            std::fprintf(stderr, "%s\n", m_StartError.c_str());
            Note(m_StartError);
            Quit();
            return;
        }
        // An encounter id is a place in encounters.json. A lone enemy id is started by name.
        m_EnemyIndex = setup.EnemyIndex;
        m_DirectEnemy = std::move(setup.DirectEnemy);
        m_Inventory.SetDefeated(m_Defeated);
    }
    if (!TakeAutoPath())
        return;
    if (m_Hero.IsReady() && m_Inventory.IsReady() && !m_Vitals.Known)
        FillVitals(m_Vitals, HeroPools(m_Data, m_Hero, m_Inventory));
    EnsureIcons();
    if (!BindControls()) {
        Quit();
        return;
    }
    // --enemy is the fight screen, including N to the next list row. Anything else
    // walks. No --map means the hub, and the per-user save when this command did
    // not build a hero.
    if (m_Play.Fight.Id.empty()) {
        if (!EnterOverland(m_Play.MapId)) {
            m_StartError = m_Play.MapId.empty() ? "no hub map"
                                                : "map '" + m_Play.MapId + "' does not exist";
            std::fprintf(stderr, "%s\n", m_StartError.c_str());
            Note(m_StartError);
            Quit();
            return;
        }
        return;
    }
    if (resumed && (m_Standing == RunStanding::Victory || m_Standing == RunStanding::Defeat)) {
        Note(m_Standing == RunStanding::Victory ? "Victory." : "Defeat.");
        Note("Resumed.");
        return;
    }
    if (resumed)
        m_AnnounceResume = true;
    BeginFight();
}

bool AscensionApp::BindControls()
{
    std::string error;
    if (ApplyControls(GetInput(), m_Data.GetControls(), error))
        return true;
    m_StartError = error;
    std::fprintf(stderr, "%s\n", error.c_str());
    Note(error);
    return false;
}

bool AscensionApp::Pressed(std::string_view action)
{
    return GetInput().WasActionPressed(action);
}

bool AscensionApp::NavStep(NavHold& hold, std::string_view action, f32 dt)
{
    Emerald::Input& input = GetInput();
    const ControlRules& rules = m_Data.GetControls();
    if (!input.IsActionDown(action)) {
        hold.Elapsed = 0.0f;
        hold.Repeating = false;
        return false;
    }
    if (input.WasActionPressed(action)) {
        hold.Elapsed = 0.0f;
        hold.Repeating = false;
        return true;
    }
    hold.Elapsed += dt;
    const f32 initial = static_cast<f32>(rules.NavInitialMilliseconds) / 1000.0f;
    const f32 every = static_cast<f32>(rules.NavRepeatMilliseconds) / 1000.0f;
    if (!hold.Repeating) {
        if (hold.Elapsed < initial)
            return false;
        hold.Repeating = true;
        hold.Elapsed = 0.0f;
        return true;
    }
    if (hold.Elapsed < every)
        return false;
    hold.Elapsed = 0.0f;
    return true;
}

bool AscensionApp::Choosing() const
{
    const bool pickingClass = m_Hero.ChoosingStarter();
    const bool spending = !pickingClass && m_Hero.ClassPool() > 0;
    const bool choosingPath = m_Hero.ChoicePending() && !pickingClass && !spending;
    return pickingClass || spending || choosingPath ||
           (m_Hero.PickPending() && !pickingClass && !spending);
}

bool AscensionApp::CursorActive() const
{
    if (m_Paused || !m_Data.IsLoaded())
        return false;
    if (m_Talk || m_Camp || m_Picking)
        return true;
    if (m_Battle != nullptr && m_Battle->Result() == BattleResult::Ongoing)
        return true;
    return Choosing();
}

int AscensionApp::ChoiceCount() const
{
    const bool pickingClass = m_Hero.ChoosingStarter();
    const bool spending = !pickingClass && m_Hero.ClassPool() > 0;
    const bool choosingPath = m_Hero.ChoicePending() && !pickingClass && !spending;
    const bool pickingSkill =
        m_Hero.PickPending() && !pickingClass && !spending && !choosingPath;
    if (pickingClass) {
        const int starters = static_cast<int>(m_Hero.StarterIds().size());
        return starters < kMaxHotkeys ? starters : kMaxHotkeys;
    }
    if (spending) {
        int count = static_cast<int>(m_Data.GetStats().size());
        if (count > kMaxHotkeys)
            count = kMaxHotkeys;
        if (m_Hero.ChoicePending() && count < kMaxHotkeys)
            ++count;
        return count;
    }
    if (pickingSkill) {
        const int options = static_cast<int>(m_Hero.PickOptions().size());
        return options < kMaxHotkeys ? options : kMaxHotkeys;
    }
    if (choosingPath) {
        const int choices = static_cast<int>(m_Hero.ChoiceIds().size());
        int count = choices < kMaxHotkeys ? choices : kMaxHotkeys;
        if (m_Hero.ChoiceIds().size() < static_cast<usize>(kMaxHotkeys))
            ++count;
        return count;
    }
    return 0;
}

int AscensionApp::CommandCount() const
{
    if (m_Battle == nullptr || m_Battle->Result() != BattleResult::Ongoing)
        return 0;
    int skills = static_cast<int>(m_Battle->PlayerSkillIds().size());
    if (skills > kMaxHotkeys)
        skills = kMaxHotkeys;
    int count = skills + 1;
    if (!m_Inventory.ConsumableSlots().empty())
        ++count;
    if (m_MapFight.Active && m_MapFight.AllowFlee)
        ++count;
    return count;
}

std::string AscensionApp::ActionLabel(std::string_view action)
{
    Emerald::Input& input = GetInput();
    const Emerald::Gamepads& pads = input.GetGamepads();
    const std::span<const Emerald::GamepadButton> buttons = input.GetActionButtons(action);
    if (pads.GetCount() > 0 && !buttons.empty()) {
        const char* label = pads.GetButtonLabel(buttons.front());
        if (label != nullptr && label[0] != '\0')
            return label;
    }
    const std::span<const Emerald::Key> keys = input.GetActionKeys(action);
    if (!keys.empty())
        return Emerald::GetKeyLabel(keys.front());
    return {};
}

std::string AscensionApp::CloseLabel()
{
    if (GetInput().GetGamepads().GetCount() > 0)
        return ActionLabel(kCancel);
    return ActionLabel(kInteract);
}

std::string_view AscensionApp::CloseAction() const
{
    const PromptRules& rules = m_Data.GetPrompts();
    if (m_PromptFamily.empty() || m_PromptFamily == rules.TextFamily)
        return kInteract;
    return kCancel;
}

void AscensionApp::NotePromptDevice()
{
    const PromptRules& rules = m_Data.GetPrompts();
    if (m_PromptFamily.empty())
        m_PromptFamily = rules.TextFamily;
    Emerald::Input& input = GetInput();
    if (input.GetKeyboard().GetPressedKey().has_value())
        m_PromptFamily = rules.TextFamily;
    const Emerald::Gamepads& pads = input.GetGamepads();
    if (pads.GetPressedButton().has_value())
        m_PromptFamily = PromptFamilyForType(rules, Emerald::GetGamepadTypeName(pads.GetLastUsedType()));
}

void AscensionApp::EnsurePromptSheets()
{
    const PromptRules& rules = m_Data.GetPrompts();
    if (m_PromptSheets.size() == rules.Sheets.size() && !m_PromptSheets.empty())
        return;
    m_PromptSheets.clear();
    Emerald::TextureOptions options;
    options.Filter = Emerald::TextureFilter::Nearest;
    for (const PromptSheet& sheet : rules.Sheets) {
        PromptPicture picture;
        picture.Family = sheet.Family;
        picture.Texture = GetAssets().Load<Emerald::Texture>(sheet.Image, options);
        m_PromptSheets.push_back(std::move(picture));
    }
}

void AscensionApp::DrawHints(Emerald::Renderer2D& renderer, f32 x, f32 y, f32 maxX,
                             std::span<const HintChip> hints, bool helpLine)
{
    if ((helpLine && !m_ShowHelp) || hints.empty() || !m_Data.IsLoaded())
        return;
    EnsurePromptSheets();
    const PromptRules& rules = m_Data.GetPrompts();
    const PromptMetrics metrics = MeasurePrompts(rules);
    const std::string family = m_PromptFamily.empty() ? rules.TextFamily : m_PromptFamily;
    const PromptSheet* sheet = FindPromptSheet(rules, family);
    const Emerald::Texture* texture = nullptr;
    for (const PromptPicture& picture : m_PromptSheets) {
        if (picture.Family != family || !picture.Texture)
            continue;
        const std::optional<Emerald::AssetInfo> info = GetAssets().GetInfo(picture.Texture.GetId());
        if (info.has_value() && !info->Placeholder)
            texture = &picture.Texture.Get();
    }
    f32 pen = x;
    bool first = true;
    for (const HintChip& hint : hints) {
        const PromptMark mark =
            hint.Action.empty() ? PromptMark{} : ResolvePrompt(GetInput(), rules, hint.Action, family);
        const PromptTile* tile = nullptr;
        if (mark.Icon && sheet != nullptr && texture != nullptr)
            tile = FindPromptTile(*sheet, mark.Tile);
        const bool icon = tile != nullptr;
        const std::string label = icon ? std::string{} : Upper(mark.Label);
        const std::string word = Upper(hint.Word);
        f32 chip = 0.0f;
        if (icon)
            chip = metrics.Icon;
        else if (!label.empty())
            chip = Emerald::MeasureDebugText(label, metrics.TextScale).x;
        f32 wordWidth = 0.0f;
        if (!word.empty())
            wordWidth = Emerald::MeasureDebugText(word, metrics.TextScale).x;
        if (chip == 0.0f && wordWidth == 0.0f)
            continue;
        const f32 lead = first ? 0.0f : metrics.GroupGap;
        const f32 mid = (chip > 0.0f && wordWidth > 0.0f) ? metrics.Gap : 0.0f;
        if (!first && pen + lead > maxX)
            return;
        pen += lead;
        first = false;
        const f32 textY = y + metrics.TextOffset;
        if (icon) {
            Emerald::Sprite sprite;
            sprite.Source = texture;
            sprite.Region.Position = {static_cast<f32>(tile->X), static_cast<f32>(tile->Y)};
            sprite.Region.Size = {static_cast<f32>(tile->Width), static_cast<f32>(tile->Height)};
            Emerald::SpriteOptions options;
            options.Size = {metrics.Icon, metrics.Icon};
            options.Origin = {0.0f, 0.0f};
            options.PixelSnap = true;
            renderer.DrawSprite(sprite, {pen, y}, options);
            pen += metrics.Icon;
        } else if (!label.empty()) {
            Emerald::DrawDebugText(renderer, {pen, textY}, label, kDim, metrics.TextScale);
            pen += Emerald::MeasureDebugText(label, metrics.TextScale).x;
        }
        if (wordWidth > 0.0f) {
            pen += mid;
            Emerald::DrawDebugText(renderer, {pen, textY}, word, kDim, metrics.TextScale);
            pen += wordWidth;
        }
    }
}

void AscensionApp::FinishFrame(Emerald::Renderer2D& renderer, f32 width)
{
    if (m_Paused) {
        Emerald::DrawDebugText(renderer, {width * 0.5f - 42.0f, 16.0f}, "PAUSED", kFrame, 2);
    }
    renderer.End();
}

void AscensionApp::InspectTile()
{
    if (!m_Walker.Placed())
        return;
    const MapDef* map = m_Data.FindMap(m_Walker.Place().MapId);
    if (map == nullptr)
        return;
    const MapTile tile = m_Walker.Place().Tile;
    for (const MapPoint& point : map->Points) {
        if (point.Tile.X != tile.X || point.Tile.Y != tile.Y)
            continue;
        if (point.Kind == MapPointKind::Talk) {
            Note("Someone here will talk.");
            return;
        }
        if (point.Kind == MapPointKind::Shop) {
            Note(m_Data.GetShop().Name + ".");
            return;
        }
        if (point.Kind == MapPointKind::Rest) {
            Note("A place to rest.");
            return;
        }
    }
    for (const SpawnPlace& place : m_Places) {
        if (place.Tile.X == tile.X && place.Tile.Y == tile.Y) {
            Note("A foe holds this tile.");
            return;
        }
    }
    Note("Nothing here.");
}

void AscensionApp::InspectFoe()
{
    if (m_Battle == nullptr)
        return;
    const std::span<const CombatantState> foes = m_Battle->Enemies();
    const i32 index = m_Battle->TargetIndex();
    if (index < 0 || index >= static_cast<i32>(foes.size()))
        return;
    const CombatantState& foe = foes[static_cast<usize>(index)];
    std::string line = foe.Name + "  " + std::to_string(foe.Hp) + "/" + std::to_string(foe.MaxHp);
    for (const StatusInstance& status : foe.Statuses) {
        const StatusDef* def = m_Data.FindStatus(status.Id);
        line += "  ";
        line += def != nullptr ? def->Name : status.Id;
    }
    Note(line);
}

void AscensionApp::PollActions(f32 dt)
{
    if (!m_Data.IsLoaded())
        return;
    NotePromptDevice();
    if (m_Paused) {
        if (Pressed(kQuit))
            HandleAction(kQuit);
        if (Pressed(kSave))
            HandleAction(kSave);
        if (Pressed(kHelp))
            HandleAction(kHelp);
        if (Pressed(kPause))
            HandleAction(kPause);
        if (Pressed(kCancel))
            HandleAction(kCancel);
        return;
    }
    if (Pressed(kQuit)) {
        HandleAction(kQuit);
        return;
    }
    if (Pressed(kPause)) {
        HandleAction(kPause);
        if (m_Paused)
            return;
    }
    if (Pressed(kSave))
        HandleAction(kSave);
    if (Pressed(kHelp))
        HandleAction(kHelp);

    if (!CursorActive()) {
        for (NavHold& hold : m_Nav) {
            hold.Elapsed = 0.0f;
            hold.Repeating = false;
        }
    } else {
        const std::string_view nav[] = {kMoveNorth, kMoveSouth, kMoveWest, kMoveEast, kCyclePrev,
                                        kCycleNext};
        for (int index = 0; index < 6; ++index) {
            if (NavStep(m_Nav[index], nav[index], dt))
                HandleAction(nav[index]);
        }
    }

    const std::string_view shots[] = {kConfirm, kCancel,  kMenu,   kContext, kInteract, kWait,
                                      kFlee,    kUnequip, kRetry,  kNext,    kSlot1,    kSlot2,
                                      kSlot3,   kSlot4,   kSlot5,  kSlot6,   kSlot7,    kSlot8};
    for (const std::string_view action : shots) {
        if (Pressed(action))
            HandleAction(action);
    }
}

void AscensionApp::HandleOverland(std::string_view action)
{
    if (m_Talk) {
        HandleTalk(action);
        return;
    }
    if (m_Camp) {
        if (action == kInteract || action == kCancel || action == kMenu) {
            m_Camp = false;
            return;
        }
        HandleCamp(action);
        return;
    }
    if (action == kInteract || action == kConfirm) {
        if (!UseMapPoint() && action == kInteract)
            Note("Nothing here.");
        return;
    }
    if (action == kMenu) {
        m_Camp = true;
        m_CampColumn = 0;
        m_CampRow = 0;
        m_Picking = false;
        return;
    }
    if (action == kContext) {
        InspectTile();
        return;
    }
    if (m_ApplyingScript)
        m_ScriptDir = DirectionOfAction(action);
}

void AscensionApp::ChooseHotkey(int digit)
{
    const bool pickingClass = m_Hero.ChoosingStarter();
    const bool spending = !pickingClass && m_Hero.ClassPool() > 0;
    const bool choosingPath = m_Hero.ChoicePending() && !pickingClass && !spending;
    const bool pickingSkill =
        m_Hero.PickPending() && !pickingClass && !spending && !choosingPath;
    if (!pickingClass && !spending && !choosingPath && !pickingSkill)
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
        m_ChoiceRow = 0;
        return;
    }
    if (spending) {
        const std::span<const StatDef> stats = m_Data.GetStats();
        if (digit >= 0 && static_cast<usize>(digit) < stats.size()) {
            const StatDef& stat = stats[static_cast<usize>(digit)];
            if (!m_Hero.SpendClassPoint(stat.Id, why)) {
                Note(why);
                return;
            }
            Note(stat.Name + " +1. " + std::to_string(m_Hero.ClassPool()) + " class points left.");
            return;
        }
        if (digit == static_cast<int>(stats.size()) && m_Hero.ChoicePending()) {
            if (!m_Hero.ResetBuild(why)) {
                Note(why);
                return;
            }
            Note("Build reset. Skill ranks cleared. Pick a starter class.");
            PushRanks();
            m_ChoiceRow = 0;
        }
        return;
    }
    if (pickingSkill) {
        const std::vector<std::string> options = m_Hero.PickOptions();
        if (digit >= static_cast<int>(options.size()))
            return;
        const std::string& optionId = options[static_cast<usize>(digit)];
        if (!m_Hero.Pick(optionId, why)) {
            Note(why);
            return;
        }
        Note(SkillName(m_Data, optionId) + " chosen.");
        PushRanks();
        m_ChoiceRow = 0;
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
        m_ChoiceRow = 0;
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
    m_ChoiceRow = 0;
}

void AscensionApp::HandleChoice(std::string_view action)
{
    const int count = ChoiceCount();
    if (count <= 0)
        return;
    if (m_ChoiceRow >= count)
        m_ChoiceRow = count - 1;
    if (m_ChoiceRow < 0)
        m_ChoiceRow = 0;
    if (action == kMoveNorth || action == kCyclePrev) {
        if (m_ChoiceRow > 0)
            --m_ChoiceRow;
        return;
    }
    if (action == kMoveSouth || action == kCycleNext) {
        if (m_ChoiceRow + 1 < count)
            ++m_ChoiceRow;
        return;
    }
    int digit = SlotIndex(action);
    if (action == kConfirm)
        digit = m_ChoiceRow;
    if (digit < 0)
        return;
    ChooseHotkey(digit);
}

void AscensionApp::UseHotkey(int digit)
{
    if (m_Battle == nullptr || m_Battle->Result() != BattleResult::Ongoing)
        return;
    if (digit < 0 || digit >= kMaxHotkeys)
        return;
    const auto skills = m_Battle->PlayerSkillIds();
    if (digit < static_cast<int>(skills.size())) {
        const std::string& skillId = skills[static_cast<usize>(digit)];
        PushRanks(skillId);
        const i32 markedBefore = m_Battle->TargetIndex();
        const ActionResult used = m_Battle->UseSkill(skillId);
        if (!used.Accepted)
            PushRanks();
        Note(used);
        NoteMarked(markedBefore);
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

bool AscensionApp::TakeAutoPath()
{
    if (m_Battle != nullptr && m_Battle->Result() == BattleResult::Ongoing)
        return true;
    std::string why;
    const EvolutionPick pick = PickEvolution(m_Hero, m_Play.ChooseId, why);
    if (pick == EvolutionPick::Failed) {
        Note(why);
        std::fprintf(stderr, "%s\n", why.c_str());
        if (m_Battle == nullptr)
            m_StartError = std::move(why);
        Quit();
        return false;
    }
    if (pick == EvolutionPick::Chosen) {
        const ClassDef* chosen = m_Data.FindClass(m_Hero.ClassId());
        Note(std::string(chosen != nullptr ? chosen->Name : m_Hero.ClassId()) + " chosen.");
        PushRanks();
    }
    while (m_PickCursor < m_Play.PickIds.size() && m_Hero.PickPending()) {
        const std::string& id = m_Play.PickIds[m_PickCursor];
        const SkillOptionPick kept = PickSkillOption(m_Hero, id, why);
        if (kept == SkillOptionPick::Failed) {
            Note(why);
            std::fprintf(stderr, "%s\n", why.c_str());
            if (m_Battle == nullptr)
                m_StartError = std::move(why);
            Quit();
            return false;
        }
        if (kept != SkillOptionPick::Chosen)
            break;
        ++m_PickCursor;
        Note(SkillName(m_Data, id) + " chosen.");
        PushRanks();
    }
    return true;
}

void AscensionApp::UseCommand(int row)
{
    if (m_Battle == nullptr || m_Battle->Result() != BattleResult::Ongoing)
        return;
    if (m_Battle->Actor() != CombatantSide::Player) {
        Note("Wait for your turn.");
        return;
    }
    int skills = static_cast<int>(m_Battle->PlayerSkillIds().size());
    if (skills > kMaxHotkeys)
        skills = kMaxHotkeys;
    if (row < 0)
        return;
    if (row < skills) {
        UseHotkey(row);
        return;
    }
    int cursor = skills;
    if (row == cursor) {
        const i32 markedBefore = m_Battle->TargetIndex();
        const ActionResult waited = m_Battle->Wait();
        Note(waited);
        NoteMarked(markedBefore);
        FinishAction(waited, true);
        ResolveFoe();
        return;
    }
    ++cursor;
    if (!m_Inventory.ConsumableSlots().empty()) {
        if (row == cursor) {
            m_Picking = true;
            m_PickRow = 0;
            return;
        }
        ++cursor;
    }
    if (m_MapFight.Active && m_MapFight.AllowFlee && row == cursor) {
        const ActionResult fled = m_Battle->Flee();
        Note(fled);
        FinishAction(fled, false);
        ResolveFoe();
    }
}

void AscensionApp::HandleBattle(std::string_view action)
{
    if (m_Battle == nullptr || m_Battle->Result() != BattleResult::Ongoing)
        return;
    const bool vertical = action == kMoveNorth || action == kMoveSouth;
    const bool horizontal = action == kMoveWest || action == kMoveEast || action == kCyclePrev ||
                            action == kCycleNext;
    if (vertical) {
        const int count = CommandCount();
        if (count <= 0)
            return;
        if (m_CommandRow >= count)
            m_CommandRow = count - 1;
        if (m_CommandRow < 0)
            m_CommandRow = 0;
        if (action == kMoveNorth) {
            if (m_CommandRow > 0)
                --m_CommandRow;
        } else if (m_CommandRow + 1 < count) {
            ++m_CommandRow;
        }
        return;
    }
    if (horizontal) {
        if (m_Battle->Actor() != CombatantSide::Player) {
            Note("Wait for your turn.");
            return;
        }
        const int step = (action == kMoveEast || action == kCycleNext) ? 1 : -1;
        const i32 markedBefore = m_Battle->TargetIndex();
        m_Battle->CycleTarget(step);
        NoteMarked(markedBefore);
        return;
    }
    if (action == kContext) {
        InspectFoe();
        return;
    }
    if (action == kWait) {
        const i32 markedBefore = m_Battle->TargetIndex();
        const ActionResult waited = m_Battle->Wait();
        Note(waited);
        NoteMarked(markedBefore);
        FinishAction(waited, true);
        ResolveFoe();
        return;
    }
    if (action == kFlee) {
        if (!m_MapFight.Active)
            return;
        if (m_Battle->Actor() != CombatantSide::Player) {
            Note("Wait for your turn.");
            return;
        }
        const ActionResult fled = m_Battle->Flee();
        Note(fled);
        FinishAction(fled, false);
        ResolveFoe();
        return;
    }
    if (action == kConfirm) {
        UseCommand(m_CommandRow);
        return;
    }
    const int digit = SlotIndex(action);
    if (digit >= 0)
        UseHotkey(digit);
}

void AscensionApp::HandleAction(std::string_view action)
{
    if (action == kQuit) {
        WriteSave();
        Quit();
        return;
    }
    if (!m_Data.IsLoaded())
        return;
    if (action == kSave) {
        if (WriteSave())
            Note("Saved.");
        return;
    }
    if (action == kHelp) {
        m_ShowHelp = !m_ShowHelp;
        return;
    }
    if (action == kPause) {
        m_Paused = !m_Paused;
        Note(m_Paused ? "Paused." : "Resumed.");
        return;
    }
    if (m_Paused) {
        if (action == kCancel) {
            m_Paused = false;
            Note("Resumed.");
        }
        return;
    }
    ScriptedPressQuery window;
    window.ScriptRunning = m_KeyCursor < m_Play.Keys.size();
    window.FromScript = m_ApplyingScript;
    if (DecideScriptedPress(window) == ScriptedPress::IgnoreWindow)
        return;

    if (m_OnMap && !SpawnFightOpen()) {
        HandleOverland(action);
        return;
    }
    if (m_OnMap && m_MapFight.Active) {
        if (action == kRetry) {
            if (m_Battle != nullptr && m_Battle->Result() == BattleResult::Ongoing)
                BeginSpawnFight();
            else if (const char* block = m_Hero.NextFightBlock())
                Note(block);
            return;
        }
        if (action == kNext) {
            Note("The next fight is on the map.");
            return;
        }
        if (action == kInteract) {
            if (m_Battle != nullptr && m_Battle->Result() == BattleResult::Ongoing)
                Note("Finish the fight.");
            return;
        }
    }
    if (m_Talk) {
        HandleTalk(action);
        return;
    }
    if (action == kRetry) {
        BeginFight();
        return;
    }
    if (action == kNext) {
        if (!NextFightAllowed(FightStandingOf(m_Battle.get()))) {
            Note("Finish the fight.");
            return;
        }
        m_DirectEnemy.clear();
        const i32 count = static_cast<i32>(m_Data.GetEncounters().size());
        m_EnemyIndex = m_Hero.IndexAfterNextFoe(m_EnemyIndex, count);
        BeginFight();
        return;
    }
    if (action == kInteract) {
        if (m_Battle != nullptr && m_Battle->Result() == BattleResult::Ongoing) {
            Note("Finish the fight. " + KeeperWord(m_Data.GetShop()) + " waits between battles.");
            return;
        }
        if (m_Camp) {
            m_Camp = false;
            return;
        }
        if (OpenTalk(m_Data.GetShop().Dialogue))
            return;
        m_Camp = true;
        m_CampColumn = 0;
        m_CampRow = 0;
        m_Picking = false;
        return;
    }
    if (action == kMenu) {
        const bool ongoing = m_Battle != nullptr && m_Battle->Result() == BattleResult::Ongoing;
        if (ongoing) {
            if (m_Battle->Actor() != CombatantSide::Player) {
                Note("Wait for your turn.");
                return;
            }
            if (m_Picking) {
                m_Picking = false;
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
        if (Choosing() && !m_Camp)
            return;
        m_Camp = !m_Camp;
        if (m_Camp) {
            m_CampColumn = 0;
            m_CampRow = 0;
        }
        m_Picking = false;
        m_Talk = false;
        return;
    }
    if (action == kCancel && m_Camp) {
        m_Camp = false;
        return;
    }
    if (m_Camp) {
        HandleCamp(action);
        return;
    }
    if (!m_StartError.empty())
        return;

    if (m_Battle != nullptr && m_Picking) {
        if (m_Battle->Result() != BattleResult::Ongoing ||
            m_Battle->Actor() != CombatantSide::Player)
            m_Picking = false;
        else {
            HandlePicker(action);
            return;
        }
    }

    if (m_Battle == nullptr || m_Battle->Result() != BattleResult::Ongoing) {
        HandleChoice(action);
        return;
    }
    HandleBattle(action);
}

WalkDir AscensionApp::DirectionOfAction(std::string_view action) const
{
    for (const MoveAction& move : kMoves) {
        if (action == move.Action)
            return move.Direction;
    }
    return WalkDir::None;
}

WalkInput AscensionApp::ReadMapInput()
{
    // Actions only. A parallel change can rebind MoveNorth and the rest without
    // this function looking at a scancode or a button.
    Emerald::Input& input = GetInput();
    WalkInput held;
    for (const MoveAction& move : kMoves) {
        if (input.WasActionPressed(move.Action)) {
            WalkInput fresh;
            fresh.Direction = move.Direction;
            fresh.Fresh = true;
            return fresh;
        }
        if (held.Direction == WalkDir::None && input.IsActionDown(move.Action))
            held.Direction = move.Direction;
    }
    return held;
}

void AscensionApp::StepMap(f32 dt)
{
    if (!m_Walker.Placed())
        return;
    WalkInput input;
    const bool scriptedStep = m_ScriptDir != WalkDir::None;
    const bool scripting = m_KeyCursor < m_Play.Keys.size() || scriptedStep;
    if (scriptedStep) {
        input.Direction = m_ScriptDir;
        input.Fresh = true;
        m_ScriptDir = WalkDir::None;
    } else if (!scripting) {
        input = ReadMapInput();
    }
    // A scripted direction is one tile, so the key harness still lands on that
    // tile before the next key. Live play uses the frame dt.
    f32 stepDt = dt;
    const WalkRules& rules = m_Data.GetWalk();
    i32 moveMs = rules.MoveMilliseconds;
    if (moveMs < 1)
        moveMs = 1;
    const f32 moveSeconds = static_cast<f32>(moveMs) / 1000.0f;
    if (scriptedStep && stepDt < moveSeconds)
        stepDt = moveSeconds;
    const WalkPlace before = m_Walker.Place();
    const WalkStep step = m_Walker.Advance(m_Data, rules, input, stepDt);
    if (step.Kind == WalkStepKind::Exited)
        Note("Exit to " + m_Walker.Place().MapId + ".");
    else if (step.Kind == WalkStepKind::Blocked && input.Fresh)
        Note("Blocked.");
    if (step.Kind == WalkStepKind::Exited || m_PlacesMap != m_Walker.Place().MapId)
        SyncPlaces();
    if (step.Kind == WalkStepKind::Moved || step.Kind == WalkStepKind::Exited) {
        ContactQuery contact;
        contact.Tile = m_Walker.Place().Tile;
        if (step.Kind == WalkStepKind::Moved) {
            contact.HasFrom = true;
            contact.From = before.Tile;
        }
        TouchSpawn(contact);
    }
    if (SpawnFightOpen())
        m_Walker.Halt();
    else if (step.Kind == WalkStepKind::Moved)
        StepMovers();
    if (!SpawnFightOpen())
        NoteRest();
    m_Glides.Advance(m_Places, stepDt, moveSeconds);
}

void AscensionApp::AdoptMap()
{
    const WalkPlace before = m_Walker.Place();
    if (!m_Walker.Adopt(m_Data, m_Play.MapId)) {
        Note("No map to stand on.");
        return;
    }
    const WalkPlace& after = m_Walker.Place();
    if (before.MapId != after.MapId || before.Tile.X != after.Tile.X ||
        before.Tile.Y != after.Tile.Y)
        Note("Back to the start.");
    else
        Note("Data reloaded.");
    SyncPlaces();
    NoteRest();
}

void AscensionApp::PlayScriptedKey()
{
    if (m_KeyCursor >= m_Play.Keys.size() || !m_StartError.empty() || !m_Data.IsLoaded())
        return;
    // Finish a foe turn left over from the previous frame before this press is spent.
    if (m_Battle != nullptr) {
        for (i32 pass = 0;
             pass < kScriptPumpPasses && m_Battle != nullptr &&
             m_Battle->Result() == BattleResult::Ongoing &&
             m_Battle->Actor() == CombatantSide::Enemy;
             ++pass)
            ResolveFoe();
    }
    const SDL_Scancode scancode = m_Play.Keys[m_KeyCursor];
    const bool ongoing = m_Battle != nullptr && m_Battle->Result() == BattleResult::Ongoing;
    ScriptedPressQuery script;
    script.ScriptRunning = true;
    script.FromScript = true;
    script.FightOngoing = ongoing;
    script.PlayerTurn = !ongoing || m_Battle->Actor() == CombatantSide::Player;
    const Emerald::Key key = static_cast<Emerald::Key>(scancode);
    script.NeedsPlayerTurn = BoundKeyNeedsPlayerTurn(GetInput(), key);
    if (DecideScriptedPress(script) == ScriptedPress::WaitForTurn) {
        if (m_Play.Trace)
            std::fprintf(stderr, "trace wait foe-turn\n");
        return;
    }
    ++m_KeyCursor;
    // The script names scancodes. Emerald's keyboard never sees that event, so the
    // press is the actions input.json bound to the key.
    m_ApplyingScript = true;
    const std::vector<std::string_view> actions = GetInput().FindActionsUsing(key);
    for (const std::string_view action : actions)
        HandleAction(action);
    m_ApplyingScript = false;
    if (m_KeyCursor < m_Play.Keys.size())
        return;
    const u64 frameLimit = GetDevOptions().Frames;
    if (frameLimit == 0 || frameLimit <= static_cast<u64>(m_Play.Keys.size()))
        Quit();
}

void AscensionApp::OnUpdate(f32 dt)
{
    m_Gallery.Advance(dt);
    if (!TakeAutoPath())
        return;
    MaybeSettle();
    PlayScriptedKey();
    MaybeSettle();
    const bool scripting = m_KeyCursor < m_Play.Keys.size();
    if (!scripting && !m_Paused)
        PollActions(dt);
    if (!m_Paused && m_OnMap && !SpawnFightOpen() && !m_Talk && !m_Camp)
        StepMap(dt);
    // Frame time is a float, and Windows file stamps are not stable the way this poll
    // expects. A reload between scripted keys rebuilds the fight on a different frame
    // than a Linux replay. The script is the input; data is whatever it started with.
    if (m_KeyCursor < m_Play.Keys.size())
        return;
    m_ReloadWait += dt;
    if (m_ReloadWait < kPollSeconds)
        return;
    m_ReloadWait = 0.0f;
    const u32 revision = m_Data.GetRevision();
    DataError error;
    m_Data.CheckForChanges(&error);
    if (m_Data.GetRevision() == revision)
        return;
    m_PromptSheets.clear();
    if (!BindControls())
        return;
    if (m_OnMap && !SpawnFightOpen()) {
        AdoptMap();
        return;
    }
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
        if (!TakeAutoPath())
            return;
        BeginFight();
        return;
    }
    m_Battle->SetGear(m_Inventory.GearBonus());
    m_Battle->SetWornItems(m_Inventory.WornIds());
    m_Battle->SetPickedSkills(m_Hero.PickedIds());
    m_Battle->SetBaseStats(m_Hero.Attributes());
    if (!m_Battle->ApplyData(why)) {
        Note(why);
        return;
    }
    PushRanks();
    if (m_Hero.Level() != levelBefore)
        Note("Level " + std::to_string(m_Hero.Level()) + ".");
    if (!TakeAutoPath())
        return;
    Note("Data reloaded.");
}

bool AscensionApp::BetweenFights() const
{
    return m_Data.IsLoaded() && m_StartError.empty() && m_Hero.IsReady() && m_Battle == nullptr;
}

bool AscensionApp::SavesProgress() const
{
    return !m_Play.SaveDir.empty() || !m_Play.DefinesRun();
}

std::filesystem::path AscensionApp::SaveFolder() const
{
    if (!m_Play.SaveDir.empty())
        return m_Play.SaveDir;
    return RunSave::DefaultFolder();
}

bool AscensionApp::WriteSave()
{
    if (!SavesProgress() || !m_Hero.IsReady() || !m_Inventory.IsReady())
        return false;
    RunState run;
    m_Hero.ExportCareer(run.Career);
    m_Inventory.ExportBag(run.Bag);
    run.Defeated = m_Defeated;
    run.Found = m_Found;
    run.Flags = m_TalkFlags;
    run.DirectEnemy = m_DirectEnemy;
    run.Cleared = m_MapMemory.Cleared;
    run.Wounds = m_MapMemory.Wounds;
    if (m_Walker.Placed()) {
        run.MapId = m_Walker.Place().MapId;
        run.Tile = m_Walker.Place().Tile;
    } else if (m_HasPlace) {
        run.MapId = m_PlaceMap;
        run.Tile = m_PlaceTile;
    }
    const auto encounters = m_Data.GetEncounters();
    const bool spawnFight = m_Battle != nullptr && m_Battle->Result() == BattleResult::Ongoing &&
                            m_MapFight.Active && !m_MapFight.EncounterId.empty();
    if (spawnFight)
        run.EncounterId = m_MapFight.EncounterId;
    else if (m_EnemyIndex >= 0 && m_EnemyIndex < static_cast<i32>(encounters.size()))
        run.EncounterId = encounters[static_cast<usize>(m_EnemyIndex)].Id;

    if (m_Battle != nullptr && m_Battle->Result() == BattleResult::Ongoing) {
        RememberOutcome(run, BattleResult::Ongoing, m_Battle->Player().Hp, m_Battle->Player().Focus);
        run.Fight = m_Battle->ExportFight();
        if (m_MapFight.Active)
            run.SpawnId = m_MapFight.SpawnId;
    } else if (m_Vitals.Known) {
        BattleResult result = BattleResult::Fled;
        if (m_Battle != nullptr)
            result = m_Battle->Result();
        else if (m_Standing == RunStanding::Victory)
            result = BattleResult::Victory;
        else if (m_Standing == RunStanding::Defeat)
            result = BattleResult::Defeat;
        else if (m_Standing == RunStanding::Ongoing)
            result = BattleResult::Ongoing;
        RememberOutcome(run, result, m_Vitals.Hp, m_Vitals.Focus);
        if (result == BattleResult::Ongoing && m_ResumeFight.Known) {
            run.Fight = m_ResumeFight;
            run.SpawnId = m_ResumeSpawn;
        }
    } else {
        run.Standing = m_Standing;
    }
    RunSave saves(SaveFolder());
    std::string error;
    if (!saves.Write(run, error)) {
        Note(error.empty() ? "could not write the save" : error);
        return false;
    }
    return true;
}

bool AscensionApp::TryResume(std::string& error)
{
    const RunSave saves(SaveFolder());
    const RunLoad loaded = saves.Read();
    if (!loaded) {
        if (loaded.Error != RunSaveError::NotFound)
            Note("The save could not be read. A new run starts.");
        return false;
    }
    if (!m_Hero.Start(m_Data, error) || !m_Inventory.Start(m_Data, error))
        return false;
    RunState run = loaded.Run;
    AdoptLegacyMap(m_Data, run);
    if (!m_Hero.ImportCareer(run.Career, error) || !m_Inventory.ImportBag(run.Bag, error))
        return false;
    m_Defeated = run.Defeated;
    m_Found = run.Found;
    m_TalkFlags = run.Flags;
    m_Inventory.SetDefeated(m_Defeated);
    m_EnemyIndex = 0;
    const auto encounters = m_Data.GetEncounters();
    for (i32 index = 0; index < static_cast<i32>(encounters.size()); ++index) {
        if (encounters[static_cast<usize>(index)].Id == run.EncounterId) {
            m_EnemyIndex = index;
            break;
        }
    }
    m_DirectEnemy = run.DirectEnemy;
    m_Standing = run.Standing;
    m_MapMemory.Cleared = run.Cleared;
    m_MapMemory.Wounds = run.Wounds;
    m_HasPlace = !run.MapId.empty();
    m_PlaceMap = run.MapId;
    m_PlaceTile = run.Tile;
    m_ResumeSpawn = run.SpawnId;
    m_ResumeResources = false;
    m_ResumeFight = {};
    if (ResumeWound(run)) {
        m_Vitals.Known = true;
        m_Vitals.Hp = run.Hp;
        m_Vitals.Focus = run.Focus;
        m_Vitals.ReturnToHub = false;
        m_ResumeResources = true;
        m_ResumeHp = run.Hp;
        m_ResumeFocus = run.Focus;
    }
    if (run.Standing == RunStanding::Ongoing && run.Fight.Known)
        m_ResumeFight = run.Fight;
    if (loaded.FromBackup)
        Note("The save was damaged. The previous one was loaded.");
    error.clear();
    return true;
}

void AscensionApp::RememberFoe(std::string_view id)
{
    if (id.empty())
        return;
    for (const std::string& held : m_Defeated) {
        if (held == id)
            return;
    }
    m_Defeated.emplace_back(id);
    m_Inventory.SetDefeated(m_Defeated);
}

void AscensionApp::RememberVictory()
{
    if (m_MapFight.Active && !m_MapFight.EncounterId.empty()) {
        const EncounterDef* encounter = m_Data.FindEncounter(m_MapFight.EncounterId);
        if (encounter == nullptr)
            return;
        for (const std::string& id : encounter->Enemies)
            RememberFoe(id);
        return;
    }
    if (!m_DirectEnemy.empty()) {
        RememberFoe(m_DirectEnemy);
        return;
    }
    const auto encounters = m_Data.GetEncounters();
    if (m_EnemyIndex < 0 || m_EnemyIndex >= static_cast<i32>(encounters.size()))
        return;
    for (const std::string& id : encounters[static_cast<usize>(m_EnemyIndex)].Enemies)
        RememberFoe(id);
}

void AscensionApp::DrawBetween(Emerald::Renderer2D& renderer, f32 width, f32 height)
{
    const Emerald::Vec2 left{28.0f, 24.0f};
    const Emerald::Vec2 leftSize{600.0f, height - 188.0f};
    const Emerald::Vec2 right{648.0f, 24.0f};
    const Emerald::Vec2 rightSize{width - 676.0f, height - 188.0f};
    DrawPanel(renderer, left, leftSize);
    DrawPanel(renderer, right, rightSize);
    DrawPanel(renderer, {28.0f, height - 148.0f}, {width - 56.0f, 124.0f});

    std::string title = m_Hero.ClassId();
    if (const ClassDef* heroClass = m_Data.FindClass(m_Hero.ClassId()))
        title = heroClass->Name;
    f32 y = left.y + 24.0f;
    Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(title), kInk, 3);
    y += Emerald::DebugTextLineHeight(3) + 8.0f;
    const std::string levelLine = "Level " + std::to_string(m_Hero.Level()) + "   XP " +
                                  std::to_string(m_Hero.Xp()) + "   GOLD " +
                                  std::to_string(m_Inventory.Gold());
    Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(levelLine), kFrame, 2);
    y += Emerald::DebugTextLineHeight(2) + 8.0f;
    Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(WornLine(m_Data, m_Inventory)),
                           kInk, 1, leftSize.x - 56.0f);
    y += Emerald::DebugTextLineHeight(1) + 12.0f;
    Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, "THE NEXT FIGHT WAITS.", kInk, 2);
    if (const char* block = m_Hero.NextFightBlock()) {
        y += Emerald::DebugTextLineHeight(2) + 8.0f;
        Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(block), kError, 2,
                               leftSize.x - 56.0f);
    }

    y = right.y + 24.0f;
    Emerald::DrawDebugText(renderer, {right.x + 24.0f, y}, "FELLED", kFrame, 2);
    y += Emerald::DebugTextLineHeight(2) + 8.0f;
    if (m_Defeated.empty()) {
        Emerald::DrawDebugText(renderer, {right.x + 24.0f, y}, "NONE YET", kDim, 2);
    } else {
        for (const std::string& id : m_Defeated) {
            if (y > right.y + rightSize.y - 28.0f)
                break;
            const EnemyDef* foe = m_Data.FindEnemy(id);
            const std::string name = foe != nullptr ? foe->Name : id;
            Emerald::DrawDebugText(renderer, {right.x + 24.0f, y}, Upper(name), kInk, 2);
            y += Emerald::DebugTextLineHeight(2) + 2.0f;
        }
    }

    const char* banner = "BETWEEN FIGHTS";
    Emerald::Vec4 bannerColor = kFrame;
    if (m_Standing == RunStanding::Victory)
        banner = "VICTORY";
    else if (m_Standing == RunStanding::Defeat) {
        banner = "DEFEAT";
        bannerColor = kError;
    }
    Emerald::DrawDebugText(renderer, {48.0f, height - 132.0f}, banner, bannerColor, 2);
    f32 logY = height - 96.0f;
    for (const std::string& line : m_Log) {
        Emerald::DrawDebugText(renderer, {48.0f, logY}, Upper(line), kDim, 1, width - 96.0f);
        logY += Emerald::DebugTextLineHeight(1) + 1.0f;
    }
    std::vector<HintChip> hints;
    AddHint(hints, kRetry, "FIGHT");
    AddHint(hints, kNext, "NEXT");
    AddHint(hints, CloseAction(), KeeperWord(m_Data.GetShop()));
    AddHint(hints, kSave, "SAVE");
    AddHint(hints, kQuit, "QUIT");
    DrawHints(renderer, 48.0f, height - 36.0f, width - 48.0f, hints, true);
}

void AscensionApp::OnRender2D(Emerald::Renderer2D& renderer)
{
    const Emerald::Vec2i pixels = GetWindowSizeInPixels();
    if (pixels.x <= 0 || pixels.y <= 0)
        return;
    const f32 width = static_cast<f32>(pixels.x);
    const f32 height = static_cast<f32>(pixels.y);
    renderer.Begin(Emerald::Mat4::OrthoPixelSpace(width, height));

    if (m_OnMap && !SpawnFightOpen() && m_Data.IsLoaded() && m_StartError.empty()) {
        if (m_Talk && m_Hero.IsReady()) {
            DrawTalk(renderer, width, height);
            FinishFrame(renderer, width);
            return;
        }
        if (m_Camp && m_Hero.IsReady()) {
            DrawCamp(renderer, width, height);
            FinishFrame(renderer, width);
            return;
        }
        const PoolCaps caps = m_Hero.IsReady() && m_Inventory.IsReady()
                                  ? HeroPools(m_Data, m_Hero, m_Inventory)
                                  : PoolCaps{};
        const PoolPair shown = PoolsFrom(m_Vitals, caps);
        DrawMapScreen(renderer, m_Data, m_Walker, m_Log, width, height, shown.Hp, caps.MaxHp,
                      shown.Focus, caps.MaxFocus, m_MapMemory.Cleared, m_Places, &m_Gallery,
                      &m_Glides);
        if (const char* word = MapInteractWord(m_Data, m_Walker)) {
            const PromptRules& prompts = m_Data.GetPrompts();
            const bool text = m_PromptFamily.empty() || m_PromptFamily == prompts.TextFamily;
            std::vector<HintChip> hints;
            AddHint(hints, text ? kInteract : kConfirm, word);
            const f32 poolsY = 16.0f + Emerald::DebugTextLineHeight(2) + 4.0f;
            const f32 promptY = poolsY + Emerald::DebugTextLineHeight(2) + 4.0f;
            DrawHints(renderer, 16.0f, promptY, width - 16.0f, hints, false);
        }
        FinishFrame(renderer, width);
        return;
    }

    const Emerald::Vec2 left{28.0f, 24.0f};
    const Emerald::Vec2 leftSize{600.0f, height - 188.0f};
    const Emerald::Vec2 right{648.0f, 24.0f};
    const Emerald::Vec2 rightSize{width - 676.0f, height - 188.0f};
    DrawPanel(renderer, left, leftSize);
    DrawPanel(renderer, right, rightSize);
    DrawPanel(renderer, {28.0f, height - 148.0f}, {width - 56.0f, 124.0f});

    if (m_Talk && m_Data.IsLoaded() && m_StartError.empty() && m_Hero.IsReady()) {
        DrawTalk(renderer, width, height);
        FinishFrame(renderer, width);
        return;
    }
    if (m_Camp && m_Data.IsLoaded() && m_StartError.empty() && m_Hero.IsReady()) {
        DrawCamp(renderer, width, height);
        FinishFrame(renderer, width);
        return;
    }
    if (!m_Data.IsLoaded() || !m_StartError.empty() || (m_Battle == nullptr && !BetweenFights())) {
        Emerald::DrawDebugText(renderer, {left.x + 24.0f, left.y + 28.0f}, "ASCENSION", kFrame, 3);
        // No fight yet. A load error keeps its file and field. A fight block (a path still
        // open, a class to pick, points to spend) keeps the sentence NextFightBlock produced.
        const std::string message =
            StartupFailureText(m_StartError, m_Data.GetLastError(), m_Hero.NextFightBlock());
        Emerald::DrawDebugText(renderer, {left.x + 24.0f, left.y + 80.0f}, Upper(message), kError,
                               2, leftSize.x - 48.0f);
        FinishFrame(renderer, width);
        return;
    }

    if (BetweenFights()) {
        DrawBetween(renderer, width, height);
        FinishFrame(renderer, width);
        return;
    }

    const CombatantState& player = m_Battle->Player();
    const CombatantState& enemy = m_Battle->Enemy();
    const std::span<const CombatantState> pack = m_Battle->Enemies();
    const i32 packCount = static_cast<i32>(pack.size());
    const i32 actorSlot = m_Battle->Result() == BattleResult::Ongoing ? m_Battle->ActorIndex() : -1;
    const i32 encounterTotal = static_cast<i32>(m_Data.GetEncounters().size());
    const std::string zone = FightZone(m_Data, m_EnemyIndex, !m_DirectEnemy.empty());
    const bool playerTurn =
        m_Battle->Result() == BattleResult::Ongoing && m_Battle->Actor() == CombatantSide::Player;

    const Emerald::Vec2 figureAt{left.x + 28.0f, left.y + 28.0f};
    const Emerald::Vec4 white{1.0f, 1.0f, 1.0f, 1.0f};
    if (m_Gallery.IsEmpty() ||
        !DrawArt(renderer, &m_Gallery, ArtSlot::HeroBattle, m_Hero.ClassId(), figureAt,
                 {16.0f * 4.0f, 16.0f * 6.5f}, white))
        DrawFigure(renderer, figureAt, 16.0f, kFigure);
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
    const bool choosingPath = fightOver && m_Hero.ChoicePending() && !pickingClass && !spending;
    const bool pickingSkill =
        fightOver && m_Hero.PickPending() && !pickingClass && !spending && !choosingPath;
    const bool choosing = choosingPath || pickingClass || spending || pickingSkill;
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
        else if (pickingSkill)
            heading = "CHOOSE A SKILL";
        Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, heading, kFrame, 2);
        y += Emerald::DebugTextLineHeight(2) + 2.0f;
        Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, "THE NEXT FIGHT WAITS.", kInk, 1);
        y += Emerald::DebugTextLineHeight(1) + 8.0f;
        if (pickingClass) {
            const std::vector<std::string> starters = m_Hero.StarterIds();
            for (usize i = 0; i < starters.size() && i < static_cast<usize>(kMaxHotkeys); ++i) {
                const ClassDef* starter = m_Data.FindClass(starters[i]);
                const bool marked = static_cast<int>(i) == m_ChoiceRow;
                const std::string name = std::string(marked ? "> " : "  ") +
                                         std::to_string(static_cast<int>(i) + 1) + "  " +
                                         (starter != nullptr ? starter->Name : starters[i]);
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(name),
                                       marked ? kFrame : kInk, 2);
                y += Emerald::DebugTextLineHeight(2) + 2.0f;
                if (starter != nullptr) {
                    y = Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                                               Upper(m_Data.Describe(*starter)), kDim, 1,
                                               leftSize.x - 56.0f);
                    y += 8.0f;
                }
            }
            Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                                   "ANY STARTER. CLASS POINTS STAY IN THE POOL.", kDim, 1,
                                   leftSize.x - 56.0f);
        } else if (spending) {
            const std::span<const StatDef> stats = m_Data.GetStats();
            const StatBlock attributes = m_Hero.Attributes();
            Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                                   "POOL " + std::to_string(m_Hero.ClassPool()), kInk, 2);
            y += Emerald::DebugTextLineHeight(2) + 4.0f;
            for (usize index = 0; index < stats.size(); ++index) {
                const StatDef& stat = stats[index];
                const bool marked = static_cast<int>(index) == m_ChoiceRow;
                const std::string line = std::string(marked ? "> " : "  ") +
                                         std::to_string(static_cast<int>(index) + 1) + "  " +
                                         Upper(stat.Name) + "  " +
                                         std::to_string(attributes.Get(stat.Id));
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, line, marked ? kFrame : kInk,
                                       2);
                y += Emerald::DebugTextLineHeight(2) + 2.0f;
            }
            if (m_Hero.ChoicePending() && stats.size() < static_cast<usize>(kMaxHotkeys)) {
                const int resetRow = static_cast<int>(stats.size());
                const bool marked = m_ChoiceRow == resetRow;
                const std::string line = std::string(marked ? "> " : "  ") +
                                         std::to_string(resetRow + 1) + "  RESET BUILD";
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, line, marked ? kFrame : kDim,
                                       1);
            }
        } else if (pickingSkill) {
            const std::vector<std::string> options = m_Hero.PickOptions();
            for (usize i = 0; i < options.size() && i < static_cast<usize>(kMaxHotkeys); ++i) {
                const SkillGrant* offered = OfferedGrant(m_Data, m_Hero.ClassId(), options[i]);
                const SkillDef* skill =
                    offered != nullptr ? &offered->Resolved : m_Data.FindSkill(options[i]);
                const std::string name = SkillName(m_Data, options[i]);
                const bool marked = static_cast<int>(i) == m_ChoiceRow;
                const std::string line = std::string(marked ? "> " : "  ") +
                                         std::to_string(static_cast<int>(i) + 1) + "  " + name;
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(line),
                                       marked ? kFrame : kInk, 2);
                y += Emerald::DebugTextLineHeight(2) + 2.0f;
                if (skill != nullptr) {
                    y = Emerald::DrawDebugText(
                        renderer, {left.x + 28.0f, y},
                        Upper(m_Data.DescribeSkill(*skill, m_Hero.RankOf(skill->Id))), kDim, 1,
                        leftSize.x - 56.0f);
                    y += 2.0f;
                }
                if (ClassHasActive(m_Data, m_Hero.ClassId(), options[i])) {
                    Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper("Upgrades " + name),
                                           kInk, 1);
                    y += Emerald::DebugTextLineHeight(1) + 8.0f;
                } else {
                    y += 6.0f;
                }
            }
        } else {
            const std::vector<std::string> choices = m_Hero.ChoiceIds();
            for (usize i = 0; i < choices.size() && i < static_cast<usize>(kMaxHotkeys); ++i) {
                const ClassDef* evolution = m_Data.FindClass(choices[i]);
                if (evolution == nullptr)
                    continue;
                const bool marked = static_cast<int>(i) == m_ChoiceRow;
                const std::string name = std::string(marked ? "> " : "  ") +
                                         std::to_string(static_cast<int>(i) + 1) + "  " +
                                         evolution->Name;
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(name),
                                       marked ? kFrame : kInk, 2);
                y += Emerald::DebugTextLineHeight(2) + 2.0f;
                y = Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                                           Upper(m_Data.Describe(*evolution)), kDim, 1,
                                           leftSize.x - 56.0f);
                y += 2.0f;
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                                       Upper(AddsLine(m_Data, *evolution)), kInk, 1);
                y += Emerald::DebugTextLineHeight(1) + 8.0f;
            }
            if (choices.size() < static_cast<usize>(kMaxHotkeys)) {
                const bool marked = m_ChoiceRow == static_cast<int>(choices.size());
                const std::string resetName = std::string(marked ? "> " : "  ") +
                                              std::to_string(static_cast<int>(choices.size()) + 1) +
                                              "  RESET BUILD";
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, resetName,
                                       marked ? kFrame : kInk, 2);
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
                               "DRINK THE MARKED ROW, OR ITS NUMBER.", kDim, 1,
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
        // The same list, in the same order, that Battle builds its hotkeys from.
        const std::vector<HeroGrant> grants =
            CurrentGrants(m_Data, m_Hero.ClassId(), m_Inventory, m_Hero.PickedIds());
        usize hotkey = 0;
        for (const HeroGrant& grant : grants) {
            if (grant.Skill.Kind != SkillKind::Active)
                continue;
            if (hotkey >= static_cast<usize>(kMaxHotkeys))
                break;
            const usize shown = hotkey++;
            const SkillDef& skill = grant.Skill;
            ++shownSkills;
            const bool ready = playerTurn && player.Focus >= skill.FocusCost;
            const bool cursor = !fightOver && !m_Picking;
            const bool marked = cursor && static_cast<i32>(shown) == m_CommandRow;
            const Emerald::Vec4 color = marked ? kFrame : (fightOver || ready ? kInk : kDim);
            std::string line = cursor ? (marked ? "> " : "  ") : "";
            line += std::to_string(static_cast<int>(shown) + 1) + "  " + skill.Name;
            line += " (" + std::to_string(m_Hero.RankOf(skill.Id)) + ")   ";
            line += std::to_string(skill.FocusCost) + " focus";
            line += SourceTag(m_Data, grant);
            Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, Upper(line), color, 2);
            y += Emerald::DebugTextLineHeight(2) + 2.0f;
            y = Emerald::DrawDebugText(renderer, {left.x + 28.0f, y},
                                       Upper(m_Data.DescribeSkill(skill, m_Hero.RankOf(skill.Id))),
                                       kDim, 1, leftSize.x - 56.0f);
            y += 4.0f;
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
            Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, "EVERY DRINK IS ON THE LIST",
                                   kFrame, 1);
            y += Emerald::DebugTextLineHeight(1) + 2.0f;
        }
        if (!fightOver && !m_Picking) {
            int row = shownSkills;
            const char* commands[3];
            int commandCount = 0;
            commands[commandCount++] = "WAIT";
            if (!drinks.empty())
                commands[commandCount++] = "ITEMS";
            if (m_MapFight.Active && m_MapFight.AllowFlee)
                commands[commandCount++] = "FLEE";
            for (int index = 0; index < commandCount; ++index) {
                const bool marked = m_CommandRow == row + index;
                const std::string line = std::string(marked ? "> " : "  ") + commands[index];
                Emerald::DrawDebugText(renderer, {left.x + 28.0f, y}, line, marked ? kFrame : kInk,
                                       2);
                y += Emerald::DebugTextLineHeight(2) + 2.0f;
            }
        }
        for (const HeroGrant& passiveGrant : grants) {
            if (passiveGrant.Skill.Kind != SkillKind::Passive)
                continue;
            Emerald::DrawDebugText(
                renderer, {left.x + 28.0f, y},
                Upper(PassiveLine(m_Data, passiveGrant.Skill) + SourceTag(m_Data, passiveGrant)),
                kFrame, 1);
            y += Emerald::DebugTextLineHeight(1) + 2.0f;
        }
    }

    if (packCount <= 1) {
        DrawFoeBody(renderer, m_Data, &m_Gallery, enemy, {right.x + 28.0f, right.y + 28.0f}, 16.0f,
                    enemy.Alive(), kFoe);
        y = right.y + 24.0f;
        y = DrawZone(renderer, zone, {right.x + 140.0f, y}, 1);
        const EnemyDef* foe = m_Data.FindEnemy(enemy.Id);
        std::string foeTitle = enemy.Name;
        if (m_DirectEnemy.empty() && encounterTotal > 0) {
            foeTitle +=
                "  " + std::to_string(m_EnemyIndex + 1) + "/" + std::to_string(encounterTotal);
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
            y = Emerald::DrawDebugText(renderer, {right.x + 28.0f, y}, Upper(m_Data.Describe(*foe)),
                                       kDim, 2, rightSize.x - 56.0f);
            y += 10.0f;
        }
    } else {
        y = right.y + 16.0f;
        y = DrawZone(renderer, zone, {right.x + 16.0f, y}, 1);
        std::string packTitle = m_Battle->EncounterName();
        if (m_DirectEnemy.empty() && encounterTotal > 0) {
            packTitle +=
                "  " + std::to_string(m_EnemyIndex + 1) + "/" + std::to_string(encounterTotal);
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
            const Emerald::Vec4 ink = member.Alive() ? kInk : kDim;
            if (selected)
                renderer.DrawRect({columnX, cardTop}, {columnWidth, cardHeight}, kFrame);
            DrawFoeBody(renderer, m_Data, &m_Gallery, member, {columnX + 8.0f, cardTop + 8.0f},
                        figureSize, member.Alive(), kFoe);
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
            y = Emerald::DrawDebugText(renderer, {right.x + 16.0f, y},
                                       Upper(m_Data.Describe(*marked)), kDim, 1,
                                       rightSize.x - 32.0f);
            y += 8.0f;
        }
    }
    std::string order = "Acts with you";
    if (enemy.Stats.Agility > player.Stats.Agility)
        order = "Acts first";
    else if (enemy.Stats.Agility < player.Stats.Agility)
        order = "Acts second";
    const std::string orderLine = "AGILITY " + std::to_string(enemy.Stats.Agility) + "  " + order;
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
    } else if (pickingSkill) {
        banner = "CHOOSE A SKILL";
    } else if (choosing) {
        banner = "CHOOSE A PATH";
    } else if (m_Picking) {
        banner = "CHOOSE AN ITEM";
    } else if (m_Battle->Result() == BattleResult::Victory) {
        banner = "VICTORY";
    } else if (m_Battle->Result() == BattleResult::Defeat) {
        banner = "DEFEAT";
        bannerColor = kError;
    } else if (m_Battle->Result() == BattleResult::Fled) {
        banner = "FLED";
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
        const std::string_view slots[] = {kSlot1, kSlot2, kSlot3, kSlot4,
                                          kSlot5, kSlot6, kSlot7, kSlot8};
        std::vector<HintChip> hints;
        if (m_Picking && !choosing) {
            AddHint(hints, kMoveNorth, "");
            AddHint(hints, kMoveSouth, "MOVE");
            AddHint(hints, kSlot1, "DRINK");
            AddHint(hints, kConfirm, "DRINK");
            AddHint(hints, kMenu, "CLOSE");
        } else if (pickingClass || spending || pickingSkill || choosing) {
            std::vector<std::string> names;
            if (pickingClass) {
                const std::vector<std::string> starters = m_Hero.StarterIds();
                for (usize i = 0; i < starters.size() && i < static_cast<usize>(kMaxHotkeys); ++i) {
                    const ClassDef* starter = m_Data.FindClass(starters[i]);
                    names.push_back(starter != nullptr ? starter->Name : starters[i]);
                }
            } else if (spending) {
                for (const StatDef& stat : m_Data.GetStats()) {
                    if (names.size() >= static_cast<usize>(kMaxHotkeys))
                        break;
                    names.push_back(stat.Name);
                }
                if (m_Hero.ChoicePending() && names.size() < static_cast<usize>(kMaxHotkeys))
                    names.push_back("RESET BUILD");
            } else if (pickingSkill) {
                const std::vector<std::string> options = m_Hero.PickOptions();
                for (usize i = 0; i < options.size() && i < static_cast<usize>(kMaxHotkeys); ++i)
                    names.push_back(SkillName(m_Data, options[i]));
            } else {
                const std::vector<std::string> choices = m_Hero.ChoiceIds();
                for (usize i = 0; i < choices.size() && i < static_cast<usize>(kMaxHotkeys); ++i) {
                    const ClassDef* evolution = m_Data.FindClass(choices[i]);
                    names.push_back(evolution != nullptr ? evolution->Name : choices[i]);
                }
                if (choices.size() < static_cast<usize>(kMaxHotkeys))
                    names.push_back("RESET BUILD");
            }
            for (usize i = 0; i < names.size() && i < static_cast<usize>(kMaxHotkeys); ++i)
                AddHint(hints, slots[i], names[i]);
            AddHint(hints, CloseAction(), KeeperWord(m_Data.GetShop()));
            AddHint(hints, kQuit, "QUIT");
        } else {
            if (shownSkills > 0)
                AddHint(hints, kSlot1, shownSkills == 1 ? "SKILL" : "SKILLS");
            const int drinkKeys = static_cast<int>(m_Inventory.ConsumableSlots().size());
            const int drinkFirst = shownSkills;
            if (drinkKeys > 0 && drinkFirst < kMaxHotkeys)
                AddHint(hints, slots[drinkFirst], "ITEM");
            if (!fightOver && packCount > 1) {
                AddHint(hints, kMoveWest, "");
                AddHint(hints, kMoveEast, "TARGET");
            }
            if (!fightOver)
                AddHint(hints, kMenu, "ITEMS");
            if (!fightOver && m_MapFight.Active && m_MapFight.AllowFlee)
                AddHint(hints, kFlee, "FLEE");
            AddHint(hints, kWait, "WAIT");
            if (fightOver && !m_OnMap)
                AddHint(hints, CloseAction(), KeeperWord(m_Data.GetShop()));
            AddHint(hints, kRetry, "FIGHT");
            if (!m_OnMap)
                AddHint(hints, kNext, "NEXT FOE");
            AddHint(hints, kSave, "SAVE");
            AddHint(hints, kQuit, "QUIT");
            AddHint(hints, "", "REV " + std::to_string(m_Data.GetRevision()));
        }
        DrawHints(renderer, 48.0f, height - 36.0f, width - 48.0f, hints, true);
    }

    FinishFrame(renderer, width);
}

void AscensionApp::DrinkAt(i32 bagIndex)
{
    if (m_Battle == nullptr)
        return;
    const ItemStack& stack = m_Inventory.BagAt(bagIndex);
    const ItemDef* item = m_Data.FindItem(stack.Id);
    if (item == nullptr)
        return;
    const i32 markedBefore = m_Battle->TargetIndex();
    const ActionResult drunk = m_Battle->UseItem(*item);
    if (drunk.Accepted && drunk.SkillId == item->Id) {
        std::string why;
        if (!m_Inventory.Take(bagIndex, why))
            Note(why);
    }
    Note(drunk);
    NoteMarked(markedBefore);
    FinishAction(drunk, false);
    ResolveFoe();
    if (drunk.Accepted)
        m_Picking = false;
}

void AscensionApp::HandlePicker(std::string_view action)
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

    if (action == kMenu || action == kCancel) {
        m_Picking = false;
        return;
    }
    if (action == kMoveNorth || action == kCyclePrev) {
        if (m_PickRow > 0)
            --m_PickRow;
        return;
    }
    if (action == kMoveSouth || action == kCycleNext) {
        if (m_PickRow + 1 < static_cast<i32>(drinks.size()))
            ++m_PickRow;
        return;
    }

    int pick = SlotIndex(action);
    if (pick >= static_cast<int>(drinks.size()))
        pick = -1;
    if (action == kConfirm)
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

void AscensionApp::HandleCamp(std::string_view action)
{
    ClampCamp();
    const ShopDef& shop = m_Data.GetShop();
    if (action == kMoveNorth) {
        --m_CampRow;
        ClampCamp();
        return;
    }
    if (action == kMoveSouth) {
        ++m_CampRow;
        ClampCamp();
        return;
    }
    if (action == kMoveWest || action == kMoveEast || action == kCyclePrev || action == kCycleNext) {
        m_CampColumn = m_CampColumn == 0 ? 1 : 0;
        ClampCamp();
        return;
    }

    const bool confirm = action == kConfirm;
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
        (confirm || action == kUnequip || (action == kContext && m_CampColumn == 0))) {
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
    if (action == kContext) {
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

namespace {

// Base sheet plus the accent sheet. Column and Row are the drawn icon size (native pixels
// times Scale). Scale is an integer so nearest filtering keeps every texel the same size.
struct IconArt {
    const Emerald::TextureAtlas* Base = nullptr;
    const Emerald::Texture* Accent = nullptr;
    i32 Scale = 1;
    f32 Column = 0.0f;
    f32 Row = 0.0f;
};

bool AssetIsLive(const Emerald::Assets& assets, Emerald::AssetId id)
{
    const std::optional<Emerald::AssetInfo> info = assets.GetInfo(id);
    return info.has_value() && !info->Placeholder;
}

i32 TallestIcon(const Database& data)
{
    i32 tallest = 1;
    for (const IconDef& icon : data.GetIcons()) {
        if (icon.Height > tallest)
            tallest = icon.Height;
    }
    return tallest;
}

// Largest integer scale whose icons fit in `room` pixels across `rows` rows. 1 when the native
// icon is already taller than the slot.
i32 IconScaleFor(f32 room, i32 rows, i32 pixels)
{
    i32 scale = 1;
    if (rows < 1 || pixels < 1 || room <= 0.0f)
        return scale;
    for (i32 next = 2; next <= 4; ++next) {
        const f32 need = static_cast<f32>(next) * static_cast<f32>(pixels) * static_cast<f32>(rows);
        if (need > room + 0.5f)
            break;
        scale = next;
    }
    return scale;
}

void MeasureIconColumn(const Database& data, i32 scale, f32& column, f32& row)
{
    column = 0.0f;
    row = 0.0f;
    const i32 drawScale = scale < 1 ? 1 : scale;
    for (const IconDef& icon : data.GetIcons()) {
        const f32 width = static_cast<f32>(icon.Width * drawScale);
        const f32 height = static_cast<f32>(icon.Height * drawScale);
        if (width > column)
            column = width;
        if (height > row)
            row = height;
    }
}

Emerald::Vec4 RarityTint(const Database& data, const ItemDef& item, const Emerald::Vec4& fallback)
{
    const RarityDef* rarity = data.FindRarity(item.Rarity);
    if (rarity == nullptr)
        return fallback;
    return {static_cast<f32>(rarity->Red) / 255.0f, static_cast<f32>(rarity->Green) / 255.0f,
            static_cast<f32>(rarity->Blue) / 255.0f, 1.0f};
}

void DrawItemIcon(Emerald::Renderer2D& renderer, const IconArt& art, std::string_view iconId,
                  const Emerald::Vec2& topLeft, const Emerald::Vec4& tint)
{
    if (art.Base == nullptr || art.Base->IsPlaceholder() || iconId.empty())
        return;
    const std::optional<Emerald::Sprite> sprite = art.Base->Find(iconId);
    if (!sprite.has_value() || sprite->Source == nullptr)
        return;
    Emerald::SpriteOptions options;
    options.Size = {sprite->Region.Size.x * static_cast<f32>(art.Scale),
                    sprite->Region.Size.y * static_cast<f32>(art.Scale)};
    options.Origin = {0.0f, 0.0f};
    options.PixelSnap = true;
    const f32 shiftX = art.Column > options.Size.x ? (art.Column - options.Size.x) * 0.5f : 0.0f;
    const f32 shiftY = art.Row > options.Size.y ? (art.Row - options.Size.y) * 0.5f : 0.0f;
    const Emerald::Vec2 at = topLeft + Emerald::Vec2{shiftX, shiftY};
    renderer.DrawSprite(*sprite, at, options);
    if (art.Accent == nullptr)
        return;
    Emerald::Sprite accent;
    accent.Source = art.Accent;
    accent.Region = sprite->Region;
    options.Tint = tint;
    renderer.DrawSprite(accent, at, options);
}

// Icon, then `lead`, then the item name in the rarity color, then `tail` in `color`.
// Returns the y of the next row.
f32 DrawGearRow(Emerald::Renderer2D& renderer, const IconArt& art, const Emerald::Vec2& origin,
                f32 width, const ItemDef* item, const Emerald::Vec4& nameColor,
                std::string_view lead, std::string_view name, std::string_view tail,
                const Emerald::Vec4& color)
{
    const f32 textLine = Emerald::DebugTextLineHeight(1);
    const f32 rowH = art.Row > textLine ? art.Row : textLine;
    if (item != nullptr)
        DrawItemIcon(renderer, art, item->Icon, origin, nameColor);
    const i32 textOffset = static_cast<i32>(rowH - textLine) / 2;
    const f32 textY = origin.y + static_cast<f32>(textOffset > 0 ? textOffset : 0);
    f32 x = origin.x;
    if (art.Column > 0.0f)
        x += art.Column + 4.0f;
    const std::string leadText = Upper(lead);
    const std::string nameText = Upper(name);
    const std::string tailText = Upper(tail);
    if (!leadText.empty()) {
        Emerald::DrawDebugText(renderer, {x, textY}, leadText, color, 1);
        x += Emerald::MeasureDebugText(leadText, 1).x;
    }
    if (!nameText.empty()) {
        Emerald::DrawDebugText(renderer, {x, textY}, nameText, nameColor, 1);
        x += Emerald::MeasureDebugText(nameText, 1).x;
    }
    if (!tailText.empty()) {
        const f32 used = x - origin.x;
        const f32 remain = width > used ? width - used : 0.0f;
        Emerald::DrawDebugText(renderer, {x, textY}, tailText, color, 1, remain);
    }
    return origin.y + rowH;
}

} // namespace

void AscensionApp::EnsureIcons()
{
    if (m_Icons || !m_Data.IsLoaded())
        return;
    Emerald::TextureOptions options;
    options.Filter = Emerald::TextureFilter::Nearest;
    const std::filesystem::path sheet = m_Data.GetIconSheet();
    m_Icons = GetAssets().Load<Emerald::TextureAtlas>(sheet, options);
    const std::filesystem::path accent = sheet.parent_path() / "icons_accent_sheet.png";
    m_Accents = GetAssets().Load<Emerald::Texture>(accent, options);
}

void AscensionApp::DrawCamp(Emerald::Renderer2D& renderer, f32 width, f32 height)
{
    EnsureIcons();
    const Emerald::Vec2 left{28.0f, 24.0f};
    // The worn and bag lists are the tall column. The footer under it keeps a few notes and
    // the help line. Icon scale is chosen so that column's rows fit at an integer size.
    const f32 footer = 88.0f;
    const f32 panelH = height - left.y - footer;
    const Emerald::Vec2 leftSize{600.0f, panelH};
    const Emerald::Vec2 right{648.0f, 24.0f};
    const Emerald::Vec2 rightSize{width - 676.0f, panelH};
    DrawPanel(renderer, left, leftSize);
    DrawPanel(renderer, right, rightSize);
    const f32 panelBottom = left.y + panelH;
    const f32 bottomY = panelBottom + 8.0f;
    const f32 bottomH = height - 16.0f - bottomY;
    if (bottomH > 8.0f)
        DrawPanel(renderer, {28.0f, bottomY}, {width - 56.0f, bottomH});

    ClampCamp();
    const ShopDef& shop = m_Data.GetShop();
    const bool bagSide = m_CampColumn == 0;
    const f32 text2 = Emerald::DebugTextLineHeight(2);
    f32 y = left.y + 16.0f;
    Emerald::DrawDebugText(renderer, {left.x + 24.0f, y}, Upper(shop.Name), kFrame, 2);
    y += text2 + 2.0f;
    Emerald::DrawDebugText(renderer, {left.x + 24.0f, y},
                           Upper(shop.Keeper + "    gold " + std::to_string(m_Inventory.Gold())),
                           kInk, 2);
    y += text2 + 2.0f;
    y += 8.0f;
    Emerald::DrawDebugText(renderer, {left.x + 24.0f, y}, "WORN AND BAG", bagSide ? kFrame : kDim,
                           2);
    y += text2 + 4.0f;

    const i32 bagSlots = m_Inventory.BagSlots();
    const i32 leftRows = 4 + bagSlots;
    const f32 sectionGap = 6.0f;
    const f32 room = panelBottom - y - 4.0f - sectionGap;
    const i32 scale = IconScaleFor(room, leftRows, TallestIcon(m_Data));
    IconArt art;
    art.Scale = scale;
    if (m_Icons && AssetIsLive(GetAssets(), m_Icons.GetId()))
        art.Base = &m_Icons.Get();
    if (m_Accents && AssetIsLive(GetAssets(), m_Accents.GetId()))
        art.Accent = &m_Accents.Get();
    MeasureIconColumn(m_Data, art.Scale, art.Column, art.Row);

    const f32 iconX = left.x + 16.0f;
    const f32 rowWidth = leftSize.x - 32.0f;
    const GearSlot wornSlots[] = {GearSlot::Weapon, GearSlot::Offhand, GearSlot::Armor,
                                  GearSlot::Accessory};
    const char* wornLabels[] = {"WEAPON", "OFFHAND", "ARMOR", "ACCESSORY"};
    for (i32 wornIndex = 0; wornIndex < 4; ++wornIndex) {
        const bool marked = bagSide && m_CampRow == wornIndex;
        const Emerald::Vec4 rowColor = marked ? kFrame : kInk;
        std::string lead = marked ? "> " : "  ";
        lead += wornLabels[wornIndex];
        lead += "  ";
        const ItemDef* piece = nullptr;
        std::string name;
        std::string tail;
        if (wornSlots[wornIndex] == GearSlot::Offhand && m_Inventory.OffhandBlocked()) {
            lead += "BOTH HANDS";
        } else {
            const std::string& worn = m_Inventory.Equipped(wornSlots[wornIndex]);
            if (worn.empty()) {
                lead += "EMPTY";
            } else {
                piece = m_Data.FindItem(worn);
                if (piece == nullptr)
                    lead += worn;
                else {
                    name = piece->Name;
                    tail = GearLine(m_Data, *piece, false);
                }
            }
        }
        const Emerald::Vec4 nameColor =
            piece != nullptr ? RarityTint(m_Data, *piece, rowColor) : rowColor;
        y = DrawGearRow(renderer, art, {iconX, y}, rowWidth, piece, nameColor, lead, name, tail,
                        rowColor);
    }
    y += sectionGap;
    for (i32 bagIndex = 0; bagIndex < bagSlots; ++bagIndex) {
        const bool marked = bagSide && m_CampRow == bagIndex + 4;
        const Emerald::Vec4 rowColor = marked ? kFrame : kDim;
        const ItemStack& stack = m_Inventory.BagAt(bagIndex);
        std::string lead = marked ? "> " : "  ";
        const ItemDef* held = nullptr;
        std::string name;
        std::string tail;
        if (stack.Empty()) {
            lead += "EMPTY";
        } else {
            held = m_Data.FindItem(stack.Id);
            if (held == nullptr) {
                lead += stack.Id;
                lead += " x" + std::to_string(stack.Count);
            } else {
                name = held->Name;
                tail = " x" + std::to_string(stack.Count);
                tail += "   sell " + std::to_string(SellPrice(shop, held->Value));
                tail += GearLine(m_Data, *held, true);
            }
        }
        const Emerald::Vec4 nameColor =
            held != nullptr ? RarityTint(m_Data, *held, rowColor) : rowColor;
        y = DrawGearRow(renderer, art, {iconX, y}, rowWidth, held, nameColor, lead, name, tail,
                        rowColor);
    }

    y = right.y + 16.0f;
    Emerald::DrawDebugText(renderer, {right.x + 24.0f, y}, "STOCK", bagSide ? kDim : kFrame, 2);
    y += text2 + 8.0f;
    const f32 stockWidth = rightSize.x - 32.0f;
    for (i32 stockIndex = 0; stockIndex < static_cast<i32>(shop.Stock.size()); ++stockIndex) {
        const bool marked = !bagSide && m_CampRow == stockIndex;
        const Emerald::Vec4 rowColor = marked ? kFrame : kInk;
        const ItemDef* goods = m_Data.FindItem(shop.Stock[static_cast<usize>(stockIndex)]);
        const bool locked = goods != nullptr && !ItemIsOffered(*goods, m_Inventory.Defeated());
        const Emerald::Vec4 ink = locked && !marked ? kDim : rowColor;
        std::string lead = marked ? "> " : "  ";
        std::string name;
        std::string tail;
        if (goods == nullptr) {
            lead += shop.Stock[static_cast<usize>(stockIndex)];
        } else {
            name = goods->Name;
            tail = "   " + std::to_string(goods->Value) + " gold";
            tail += GearLine(m_Data, *goods, true);
        }
        const Emerald::Vec4 nameColor = goods != nullptr ? RarityTint(m_Data, *goods, ink) : ink;
        y = DrawGearRow(renderer, art, {right.x + 16.0f, y}, stockWidth, goods, nameColor, lead,
                        name, tail, ink);
    }

    const f32 helpY = height - 36.0f;
    f32 logY = bottomY + 10.0f;
    for (const std::string& note : m_Log) {
        if (logY + Emerald::DebugTextLineHeight(1) > helpY)
            break;
        Emerald::DrawDebugText(renderer, {48.0f, logY}, Upper(note), kDim, 1, width - 96.0f);
        logY += Emerald::DebugTextLineHeight(1) + 1.0f;
    }
    std::vector<HintChip> hints;
    AddHint(hints, kMoveNorth, "");
    AddHint(hints, kMoveSouth, "MOVE");
    AddHint(hints, kMoveWest, "");
    AddHint(hints, kMoveEast, "BAG OR STOCK");
    AddHint(hints, kConfirm, "EQUIP OR BUY");
    AddHint(hints, kContext, "SELL");
    AddHint(hints, kUnequip, "UNEQUIP");
    AddHint(hints, CloseAction(), "CLOSE");
    if (m_Hero.ChoicePending())
        AddHint(hints, "", "CHOOSE A PATH BEFORE THE NEXT FIGHT");
    else if (m_Hero.PickPending())
        AddHint(hints, "", "PICK A SKILL BEFORE THE NEXT FIGHT");
    else if (!m_OnMap) {
        AddHint(hints, kRetry, "FIGHT");
        AddHint(hints, kNext, "NEXT");
    }
    AddHint(hints, kQuit, "QUIT");
    DrawHints(renderer, 48.0f, height - 36.0f, width - 48.0f, hints, true);
}

void AscensionApp::DrawTalk(Emerald::Renderer2D& renderer, f32 width, f32 height)
{
    const DialogueDef* talk = m_Data.FindDialogue(m_TalkId);
    const DialogueNode* node = talk != nullptr ? talk->FindNode(m_TalkNode) : nullptr;
    if (talk == nullptr || node == nullptr) {
        m_Talk = false;
        return;
    }
    TalkList lists[2];
    TalkFacts facts;
    FillTalkFacts(lists, facts);
    std::vector<i32> visible;
    TalkVisible(*talk, *node, facts, visible);
    if (m_TalkRow < 0)
        m_TalkRow = 0;
    if (m_TalkRow >= static_cast<i32>(visible.size()))
        m_TalkRow = visible.empty() ? 0 : static_cast<i32>(visible.size()) - 1;

    std::vector<std::string> lines;
    TalkBoxLines(node->Line, kTalkLineWidth, lines);

    DrawPanel(renderer, {28.0f, 24.0f}, {width - 56.0f, height - 48.0f});
    const Emerald::Vec4 white{1.0f, 1.0f, 1.0f, 1.0f};
    f32 textX = 52.0f;
    if (!m_Gallery.IsEmpty() &&
        DrawArt(renderer, &m_Gallery, ArtSlot::NpcPortrait, m_TalkId, {52.0f, 44.0f}, {96.0f, 96.0f},
                white))
        textX = 52.0f + 96.0f + 16.0f;
    const f32 textWrap = width - textX - 52.0f;
    const f32 lineH = Emerald::DebugTextLineHeight(2);
    f32 y = 44.0f;
    Emerald::DrawDebugText(renderer, {textX, y}, Upper(m_Data.GetShop().Keeper), kFrame, 2);
    y += lineH + 10.0f;
    for (const std::string& line : lines) {
        if (y > height - 120.0f)
            break;
        Emerald::DrawDebugText(renderer, {textX, y}, Upper(line), kInk, 2, textWrap);
        y += lineH + 2.0f;
    }
    y += 10.0f;
    for (i32 index = 0; index < static_cast<i32>(visible.size()); ++index) {
        if (y > height - 72.0f)
            break;
        const usize choiceIndex = static_cast<usize>(visible[static_cast<usize>(index)]);
        const DialogueChoice& choice = node->Choices[choiceIndex];
        const bool marked = index == m_TalkRow;
        std::string text = marked ? "> " : "  ";
        text += choice.Text;
        const Emerald::Vec4 color = marked ? kFrame : kInk;
        Emerald::DrawDebugText(renderer, {textX, y}, Upper(text), color, 2, textWrap);
        y += lineH + 2.0f;
    }
    std::vector<HintChip> hints;
    if (visible.empty()) {
        AddHint(hints, kConfirm, "CONTINUE");
    } else {
        AddHint(hints, kMoveNorth, "");
        AddHint(hints, kMoveSouth, "MOVE");
        AddHint(hints, kConfirm, "SPEAK");
    }
    AddHint(hints, CloseAction(), "CLOSE");
    DrawHints(renderer, 52.0f, height - 56.0f, width - 52.0f, hints, true);
}

} // namespace Ascension
