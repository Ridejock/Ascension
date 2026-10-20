#include "Ascension/Combat/Battle.h"

#include <algorithm>
#include <utility>

namespace Ascension {

namespace {

i32 ClampPool(i64 value)
{
    if (value < 0)
        return 0;
    if (value > 999999)
        return 999999;
    return static_cast<i32>(value);
}

i32 Scaled(i32 stat, i32 numerator, i32 denominator)
{
    if (denominator == 0 || stat == 0 || numerator == 0)
        return 0;
    const i64 value = static_cast<i64>(stat) * numerator / denominator;
    return ClampPool(value);
}

const StatusDef* FindStatusDef(std::span<const StatusDef> catalog, std::string_view id)
{
    for (const StatusDef& status : catalog) {
        if (status.Id == id)
            return &status;
    }
    return nullptr;
}

void CopyFightState(const CombatantState& from, CombatantState& to)
{
    to.GuardPercent = from.GuardPercent;
    to.EmpowerPercent = from.EmpowerPercent;
    to.ReflectPercent = from.ReflectPercent;
    to.ReflectFlat = from.ReflectFlat;
    to.BonusDefense = from.BonusDefense;
    to.BonusDefenseTurns = from.BonusDefenseTurns;
    to.Statuses = from.Statuses;
    to.WindupSkill = from.WindupSkill;
    to.WindupLeft = from.WindupLeft;
    to.WindupTotal = from.WindupTotal;
    to.PatternStep = from.PatternStep;
    to.Phase = from.Phase;
    to.PhaseTurns = from.PhaseTurns;
    to.ShardDue = from.ShardDue;
    to.SkillUseIds = from.SkillUseIds;
    to.SkillUseCounts = from.SkillUseCounts;
    if (from.Hp <= 0 || from.MaxHp <= 0) {
        to.Hp = 0;
    } else {
        const i32 missing = from.MaxHp - from.Hp;
        to.Hp = to.MaxHp - missing;
        if (to.Hp < 1)
            to.Hp = 1;
        if (to.Hp > to.MaxHp)
            to.Hp = to.MaxHp;
    }
    const i32 spent = from.MaxFocus - from.Focus;
    to.Focus = to.MaxFocus - spent;
    if (to.Focus < 0)
        to.Focus = 0;
    if (to.Focus > to.MaxFocus)
        to.Focus = to.MaxFocus;
}

CombatantSnapshot SnapOf(const CombatantState& who)
{
    CombatantSnapshot shot;
    shot.Id = who.Id;
    shot.Hp = who.Hp;
    shot.Focus = who.Focus;
    shot.GuardPercent = who.GuardPercent;
    shot.EmpowerPercent = who.EmpowerPercent;
    shot.ReflectPercent = who.ReflectPercent;
    shot.ReflectFlat = who.ReflectFlat;
    shot.BonusDefense = who.BonusDefense;
    shot.BonusDefenseTurns = who.BonusDefenseTurns;
    shot.Statuses = who.Statuses;
    shot.WindupSkill = who.WindupSkill;
    shot.WindupLeft = who.WindupLeft;
    shot.WindupTotal = who.WindupTotal;
    shot.PatternStep = who.PatternStep;
    shot.Phase = who.Phase;
    shot.PhaseTurns = who.PhaseTurns;
    shot.ShardDue = who.ShardDue;
    shot.SkillUseIds = who.SkillUseIds;
    shot.SkillUseCounts = who.SkillUseCounts;
    return shot;
}

i32 ClampPoolTo(i32 value, i32 least, i32 most)
{
    if (value < least)
        return least;
    if (most >= least && value > most)
        return most;
    return value;
}

void ApplySnap(CombatantState& who, const CombatantSnapshot& shot, bool hero)
{
    const i32 leastHp = hero ? 1 : 0;
    who.Hp = ClampPoolTo(shot.Hp, leastHp, who.MaxHp);
    who.Focus = ClampPoolTo(shot.Focus, 0, who.MaxFocus);
    who.GuardPercent = shot.GuardPercent < 0 ? 0 : shot.GuardPercent;
    who.EmpowerPercent = shot.EmpowerPercent;
    who.ReflectPercent = shot.ReflectPercent < 0 ? 0 : shot.ReflectPercent;
    who.ReflectFlat = shot.ReflectFlat < 0 ? 0 : shot.ReflectFlat;
    who.BonusDefense = shot.BonusDefense < 0 ? 0 : shot.BonusDefense;
    who.BonusDefenseTurns = shot.BonusDefenseTurns < 0 ? 0 : shot.BonusDefenseTurns;
    who.Statuses = shot.Statuses;
    who.WindupSkill = shot.WindupSkill;
    who.WindupLeft = shot.WindupLeft < 0 ? 0 : shot.WindupLeft;
    who.WindupTotal = shot.WindupTotal < 0 ? 0 : shot.WindupTotal;
    who.PatternStep = shot.PatternStep < 0 ? 0 : shot.PatternStep;
    who.Phase = shot.Phase < 1 ? 1 : shot.Phase;
    who.PhaseTurns = shot.PhaseTurns < 0 ? 0 : shot.PhaseTurns;
    who.ShardDue = shot.ShardDue;
    who.SkillUseIds = shot.SkillUseIds;
    who.SkillUseCounts = shot.SkillUseCounts;
}

// A fight draws at most this many enemies. A corpse does not hold one of these seats.
constexpr i32 kMaxCombatants = 3;

// The first copy of a repeated name stays as it is. Later copies gain " 2", " 3", and so on.
// Call NameCombatants so every name is the enemy's own name before this runs.
void DisambiguateNames(std::vector<CombatantState>& foes)
{
    const usize count = foes.size();
    std::vector<i32> place(count, 0);
    for (usize nameIndex = 0; nameIndex < count; ++nameIndex) {
        i32 copies = 0;
        i32 seen = 0;
        for (usize other = 0; other < count; ++other) {
            if (foes[other].Name != foes[nameIndex].Name)
                continue;
            ++copies;
            if (other <= nameIndex)
                ++seen;
        }
        if (copies > 1)
            place[nameIndex] = seen;
    }
    for (usize nameIndex = 0; nameIndex < count; ++nameIndex) {
        if (place[nameIndex] > 1)
            foes[nameIndex].Name += " " + std::to_string(place[nameIndex]);
    }
}

void NameCombatants(const Database* data, std::vector<CombatantState>& foes)
{
    if (data != nullptr) {
        for (CombatantState& foe : foes) {
            const EnemyDef* def = data->FindEnemy(foe.Id);
            if (def != nullptr)
                foe.Name = def->Name;
        }
    }
    DisambiguateNames(foes);
}

bool SeatListed(const TurnSeat& seat, std::span<const TurnSeat> living)
{
    for (const TurnSeat& other : living) {
        if (other.Side == seat.Side && other.Index == seat.Index)
            return true;
    }
    return false;
}

} // namespace

i32 MissingHpBonus(i32 amount, i32 stepPercent, i32 maxBonus, i32 hp, i32 maxHp)
{
    if (amount == 0 || stepPercent <= 0 || maxHp <= 0 || hp >= maxHp)
        return 0;
    if (hp < 0)
        hp = 0;
    const i32 missing = static_cast<i32>(static_cast<i64>(maxHp - hp) * 100 / maxHp);
    const i32 steps = missing / stepPercent;
    i64 bonus = static_cast<i64>(steps) * amount;
    if (maxBonus >= 0 && bonus > maxBonus)
        bonus = maxBonus;
    if (bonus < 0)
        bonus = 0;
    return ClampPool(bonus);
}

i32 ScalePercent(i32 value, i32 percent)
{
    if (value <= 0 || percent <= 0)
        return 0;
    return ClampPool(static_cast<i64>(value) * percent / 100);
}

i32 StrikeDamage(const StrikeQuery& hit, const CombatRules& rules)
{
    i64 attack = hit.Power;
    attack += Scaled(hit.ScalingStat, rules.AttackNumerator, rules.AttackDenominator);
    if (hit.EmpowerPercent != 0)
        attack = attack * (100 + hit.EmpowerPercent) / 100;
    attack = ClampPool(attack);
    const i32 defense = hit.Defense > 0 ? hit.Defense : 0;
    attack -= Scaled(defense, rules.DefenseNumerator, rules.DefenseDenominator);
    if (attack < rules.MinDamage)
        attack = rules.MinDamage;
    return ClampPool(attack);
}

i32 HealAmount(i32 power, i32 scalingStat, const CombatRules& rules)
{
    return ClampPool(static_cast<i64>(power) +
                     Scaled(scalingStat, rules.AttackNumerator, rules.AttackDenominator));
}

i32 GuardedDamage(i32 damage, i32 guardPercent)
{
    if (damage <= 0 || guardPercent <= 0)
        return damage > 0 ? damage : 0;
    if (guardPercent >= 100)
        return 0;
    return static_cast<i32>(static_cast<i64>(damage) * (100 - guardPercent) / 100);
}

i32 ReflectedDamage(i32 blocked, i32 reflectPercent, i32 flat)
{
    if (blocked <= 0)
        return 0;
    i64 damage = 0;
    if (reflectPercent > 0)
        damage = static_cast<i64>(blocked) * reflectPercent / 100;
    if (flat > 0)
        damage += flat;
    return ClampPool(damage);
}

void ApplyStatus(CombatantState& who, const StatusDef& status)
{
    const i32 cap = status.MaxStacks < 1 ? 1 : status.MaxStacks;
    const i32 turns = status.Turns < 1 ? 1 : status.Turns;
    for (StatusInstance& have : who.Statuses) {
        if (have.Id != status.Id)
            continue;
        if (have.Stacks < cap)
            ++have.Stacks;
        have.TurnsLeft = turns;
        return;
    }
    StatusInstance added;
    added.Id = status.Id;
    added.Stacks = 1;
    added.TurnsLeft = turns;
    who.Statuses.push_back(std::move(added));
}

bool ResistsStatus(const CombatantState& who, std::string_view statusId,
                   std::span<const StatusDef> catalog)
{
    for (const StatusInstance& have : who.Statuses) {
        const StatusDef* def = FindStatusDef(catalog, have.Id);
        if (def == nullptr)
            continue;
        for (const std::string& blocked : def->Resists) {
            if (blocked == statusId)
                return true;
        }
    }
    return false;
}

StatusTick TickStatuses(CombatantState& who, std::span<const StatusDef> catalog)
{
    StatusTick tick;
    for (const StatusInstance& have : who.Statuses) {
        const StatusDef* def = FindStatusDef(catalog, have.Id);
        if (def == nullptr)
            continue;
        if (def->DamagePerTurn > 0)
            tick.Bleed += def->DamagePerTurn * (have.Stacks > 0 ? have.Stacks : 1);
        if (def->SkipAction && have.TurnsLeft > 0)
            tick.Skip = true;
    }
    std::vector<StatusInstance> kept;
    kept.reserve(who.Statuses.size());
    for (StatusInstance& have : who.Statuses) {
        --have.TurnsLeft;
        if (have.TurnsLeft > 0)
            kept.push_back(std::move(have));
    }
    who.Statuses = std::move(kept);
    return tick;
}

i32 StatusDealtPercent(const CombatantState& who, std::span<const StatusDef> catalog)
{
    i32 percent = 0;
    for (const StatusInstance& have : who.Statuses) {
        const StatusDef* def = FindStatusDef(catalog, have.Id);
        if (def != nullptr)
            percent += def->DamageDealtPercent;
    }
    return percent;
}

i32 StatusTakenPercent(const CombatantState& who, std::span<const StatusDef> catalog)
{
    i32 percent = 0;
    for (const StatusInstance& have : who.Statuses) {
        const StatusDef* def = FindStatusDef(catalog, have.Id);
        if (def != nullptr)
            percent += def->DamageTakenPercent;
    }
    return percent;
}

void AgilityRoundModel::Reset()
{
    m_Round.clear();
    m_Index = 0;
    m_Open = false;
}

void AgilityRoundModel::Open(std::span<const TurnSeat> living)
{
    m_Round.assign(living.begin(), living.end());
    std::sort(m_Round.begin(), m_Round.end(), [](const TurnSeat& left, const TurnSeat& right) {
        if (left.Agility != right.Agility)
            return left.Agility > right.Agility;
        if (left.Side != right.Side)
            return left.Side == CombatantSide::Player;
        return left.Index < right.Index;
    });
    m_Index = 0;
    m_Open = true;
}

TurnSeat AgilityRoundModel::Next(std::span<const TurnSeat> living)
{
    if (living.empty())
        return {};
    if (!m_Open || m_Index >= m_Round.size())
        Open(living);
    while (m_Index < m_Round.size() && !SeatListed(m_Round[m_Index], living))
        ++m_Index;
    if (m_Index >= m_Round.size()) {
        Open(living);
        while (m_Index < m_Round.size() && !SeatListed(m_Round[m_Index], living))
            ++m_Index;
    }
    if (m_Index >= m_Round.size())
        return living.front();
    return m_Round[m_Index];
}

void AgilityRoundModel::Advance()
{
    if (m_Open)
        ++m_Index;
}

void AgilityRoundModel::Capture(std::vector<TurnSeat>& order, i32& index) const
{
    order = m_Round;
    index = static_cast<i32>(m_Index);
}

void AgilityRoundModel::Place(std::span<const TurnSeat> order, i32 index)
{
    m_Round.assign(order.begin(), order.end());
    if (index < 0)
        index = 0;
    if (static_cast<usize>(index) > m_Round.size())
        index = static_cast<i32>(m_Round.size());
    m_Index = static_cast<usize>(index);
    m_Open = !m_Round.empty();
}

Battle::Battle(const Database& data, std::unique_ptr<TurnModel> turns)
    : m_Data(&data), m_Turns(std::move(turns))
{
}

void Battle::MakeFoe(const EnemyDef& def, CombatantState& foe) const
{
    const CombatRules rules = m_Data != nullptr ? m_Data->GetRules() : CombatRules{};
    foe = {};
    foe.Id = def.Id;
    foe.Name = def.Name;
    foe.Stats = def.Stats;
    ApplyAlways(foe, false);
    foe.MaxHp = ClampPool(static_cast<i64>(foe.Stats.Vitality) * rules.HpPerVitality);
    // Enemies spend only the focus on their stat line, and it does not regenerate.
    foe.MaxFocus = ClampPool(foe.Stats.Focus);
    foe.Hp = foe.MaxHp;
    foe.Focus = foe.MaxFocus;
}

bool Battle::Build(std::string& error, bool keepWounds)
{
    if (m_Data == nullptr || m_Turns == nullptr) {
        error = "combat is missing its data or its turn model";
        return false;
    }
    const ClassDef* klass = m_Data->FindClass(m_ClassId);
    if (klass == nullptr) {
        error = "class '" + m_ClassId + "' does not exist";
        return false;
    }

    std::vector<std::string> ids;
    std::string encounterName = m_EncounterName;
    if (!m_EncounterId.empty()) {
        const EncounterDef* encounter = m_Data->FindEncounter(m_EncounterId);
        if (encounter == nullptr) {
            error = "encounter '" + m_EncounterId + "' does not exist";
            return false;
        }
        ids = encounter->Enemies;
        encounterName = encounter->Name;
    } else if (!m_FallbackEnemy.empty()) {
        if (m_Data->FindEnemy(m_FallbackEnemy) == nullptr) {
            error = "enemy '" + m_FallbackEnemy + "' does not exist";
            return false;
        }
        ids.push_back(m_FallbackEnemy);
        const EnemyDef* lone = m_Data->FindEnemy(m_FallbackEnemy);
        if (lone != nullptr)
            encounterName = lone->Name;
    } else {
        error = "the fight has no encounter";
        return false;
    }

    const CombatRules rules = m_Data->GetRules();
    m_HeroGrants = m_Data->HeroGrants(*klass, m_WornItems, m_PickedSkills);
    CombatantState player;
    player.Id = klass->Id;
    player.Name = klass->Name;
    player.Stats = m_HasBaseStats ? m_BaseStats : m_Data->StatsAt(*klass, m_Level);
    player.Stats += m_Gear;
    ApplyAlways(player, true);
    player.MaxHp = ClampPool(static_cast<i64>(player.Stats.Vitality) * rules.HpPerVitality);
    // The hero's pool is the focus stat plus focusBase. Enemies spend only the focus on
    // their stat line, and it does not regenerate, so a pattern is not gated by a pool.
    player.MaxFocus = ClampPool(static_cast<i64>(player.Stats.Focus) + rules.FocusBase);

    std::vector<std::string> playerSkills;
    for (const HeroGrant& held : m_HeroGrants) {
        if (held.Skill.Kind == SkillKind::Active)
            playerSkills.push_back(held.Skill.Id);
    }

    const std::vector<CombatantState> previous = m_Enemies;
    const i32 previousBase = m_BaseCount;
    std::vector<CombatantState> foes;
    foes.reserve(ids.size() + 2);
    for (usize slot = 0; slot < ids.size(); ++slot) {
        const EnemyDef* def = m_Data->FindEnemy(ids[slot]);
        if (def == nullptr) {
            error = "enemy '" + ids[slot] + "' does not exist";
            return false;
        }
        CombatantState foe;
        MakeFoe(*def, foe);
        if (keepWounds && static_cast<i32>(slot) < previousBase && slot < previous.size())
            CopyFightState(previous[slot], foe);
        foes.push_back(std::move(foe));
    }
    if (keepWounds) {
        for (i32 extra = previousBase; extra < static_cast<i32>(previous.size()); ++extra) {
            if (foes.size() >= static_cast<usize>(kMaxCombatants))
                break;
            const CombatantState& older = previous[static_cast<usize>(extra)];
            const EnemyDef* def = m_Data->FindEnemy(older.Id);
            if (def == nullptr)
                continue;
            CombatantState foe;
            MakeFoe(*def, foe);
            CopyFightState(older, foe);
            foes.push_back(std::move(foe));
        }
    }
    NameCombatants(m_Data, foes);

    if (keepWounds)
        CopyFightState(m_Player, player);
    else {
        player.Hp = player.MaxHp;
        player.Focus = player.MaxFocus;
    }

    m_Player = std::move(player);
    m_Enemies = std::move(foes);
    m_PlayerSkills = std::move(playerSkills);
    m_Rules = rules;
    m_EncounterName = std::move(encounterName);
    m_BaseCount = static_cast<i32>(ids.size());
    if (!keepWounds || m_Target < 0 || m_Target >= static_cast<i32>(m_Enemies.size()))
        m_Target = 0;
    Retarget();
    return true;
}

bool Battle::Start(std::string_view classId, i32 level, std::string_view enemyId,
                   std::string& error)
{
    m_ClassId = std::string(classId);
    m_Level = level;
    m_EncounterId.clear();
    m_FallbackEnemy.clear();
    m_EncounterName.clear();
    m_RandomCursor = 0;
    m_Target = 0;
    m_BaseCount = 0;
    m_Started = false;
    m_Result = BattleResult::Ongoing;
    m_UpkeepDone = false;
    m_TurnCount = 0;
    m_Opening.clear();
    if (m_Data == nullptr) {
        error = "combat is missing its data or its turn model";
        return false;
    }
    if (const EncounterDef* encounter = m_Data->FindEncounter(enemyId)) {
        m_EncounterId = encounter->Id;
        m_EncounterName = encounter->Name;
    } else if (m_Data->FindEnemy(enemyId) != nullptr) {
        m_FallbackEnemy = std::string(enemyId);
    } else {
        error = "encounter '" + std::string(enemyId) + "' does not exist";
        return false;
    }
    m_Enemies.clear();
    if (!Build(error, false))
        return false;
    m_Turns->Reset();
    if (m_Player.Hp <= 0)
        m_Result = BattleResult::Defeat;
    else
        FinishIfDead();
    m_Started = true;
    error.clear();
    return true;
}

bool Battle::ApplyData(std::string& error)
{
    if (!m_Started) {
        error = "there is no fight to update";
        return false;
    }
    const CombatantState player = m_Player;
    const std::vector<CombatantState> enemies = m_Enemies;
    const std::vector<std::string> playerSkills = m_PlayerSkills;
    const std::vector<HeroGrant> grants = m_HeroGrants;
    const CombatRules rules = m_Rules;
    const i32 baseCount = m_BaseCount;
    const i32 target = m_Target;
    const std::string encounterName = m_EncounterName;
    if (!Build(error, true)) {
        m_Player = player;
        m_Enemies = enemies;
        m_PlayerSkills = playerSkills;
        m_HeroGrants = grants;
        m_Rules = rules;
        m_BaseCount = baseCount;
        m_Target = target;
        m_EncounterName = encounterName;
        return false;
    }
    error.clear();
    return true;
}

const CombatantState& Battle::Enemy() const
{
    if (m_Target >= 0 && m_Target < static_cast<i32>(m_Enemies.size()))
        return m_Enemies[static_cast<usize>(m_Target)];
    if (!m_Enemies.empty())
        return m_Enemies.front();
    static const CombatantState kEmpty;
    return kEmpty;
}

bool Battle::SetTarget(i32 index)
{
    if (index < 0 || index >= static_cast<i32>(m_Enemies.size()))
        return false;
    if (!m_Enemies[static_cast<usize>(index)].Alive())
        return false;
    m_Target = index;
    return true;
}

void Battle::CycleTarget(i32 delta)
{
    const i32 count = static_cast<i32>(m_Enemies.size());
    if (count <= 0 || delta == 0)
        return;
    const i32 step = delta > 0 ? 1 : -1;
    for (i32 tries = 0; tries < count; ++tries) {
        i32 next = m_Target + step;
        if (next < 0)
            next = count - 1;
        else if (next >= count)
            next = 0;
        m_Target = next;
        if (m_Enemies[static_cast<usize>(m_Target)].Alive())
            return;
    }
}

void Battle::Retarget()
{
    const i32 count = static_cast<i32>(m_Enemies.size());
    if (count <= 0)
        return;
    if (m_Target < 0 || m_Target >= count)
        m_Target = 0;
    if (m_Enemies[static_cast<usize>(m_Target)].Alive())
        return;
    for (i32 step = 1; step <= count; ++step) {
        const i32 candidate = (m_Target + step) % count;
        if (m_Enemies[static_cast<usize>(candidate)].Alive()) {
            m_Target = candidate;
            return;
        }
    }
}

i32 Battle::Experience() const
{
    i32 sum = 0;
    if (m_Data == nullptr)
        return 0;
    for (const CombatantState& foe : m_Enemies) {
        const EnemyDef* def = m_Data->FindEnemy(foe.Id);
        if (def != nullptr)
            sum += def->Experience;
    }
    return sum;
}

i32 Battle::Gold() const
{
    i32 sum = 0;
    if (m_Data == nullptr)
        return 0;
    for (const CombatantState& foe : m_Enemies) {
        const EnemyDef* def = m_Data->FindEnemy(foe.Id);
        if (def != nullptr)
            sum += def->Gold;
    }
    return sum;
}

std::string Battle::TraceState() const
{
    if (!m_Started)
        return {};
    std::string line = "trace P ";
    line += std::to_string(m_Player.Hp);
    line += "/";
    line += std::to_string(m_Player.MaxHp);
    line += " F ";
    line += std::to_string(m_Player.Focus);
    line += "/";
    line += std::to_string(m_Player.MaxFocus);
    line += " BL ";
    line += std::to_string(MissingFor(m_Player, "strength"));
    line += " guard ";
    line += std::to_string(m_Player.GuardPercent);
    line += " def+";
    line += std::to_string(m_Player.BonusDefense);
    line += " t";
    line += std::to_string(m_Player.BonusDefenseTurns);
    for (const StatusInstance& status : m_Player.Statuses) {
        line += " ";
        line += status.Id;
        line += " ";
        line += std::to_string(status.Stacks);
        line += "x";
        line += std::to_string(status.TurnsLeft);
    }
    const i32 count = static_cast<i32>(m_Enemies.size());
    for (i32 index = 0; index < count; ++index) {
        const CombatantState& foe = m_Enemies[static_cast<usize>(index)];
        line += " | ";
        line += foe.Name.empty() ? foe.Id : foe.Name;
        line += " ";
        line += std::to_string(foe.Hp);
        line += "/";
        line += std::to_string(foe.MaxHp);
        line += " def+";
        line += std::to_string(foe.BonusDefense);
        line += " t";
        line += std::to_string(foe.BonusDefenseTurns);
        line += " step ";
        line += std::to_string(foe.PatternStep);
        for (const StatusInstance& status : foe.Statuses) {
            line += " ";
            line += status.Id;
            line += " ";
            line += std::to_string(status.Stacks);
            line += "x";
            line += std::to_string(status.TurnsLeft);
        }
    }
    return line;
}

std::vector<TurnSeat> Battle::Living() const
{
    std::vector<TurnSeat> seats;
    if (m_Player.Alive()) {
        TurnSeat seat;
        seat.Side = CombatantSide::Player;
        seat.Agility = m_Player.Stats.Agility;
        seats.push_back(seat);
    }
    const i32 count = static_cast<i32>(m_Enemies.size());
    for (i32 foeIndex = 0; foeIndex < count; ++foeIndex) {
        if (!m_Enemies[static_cast<usize>(foeIndex)].Alive())
            continue;
        TurnSeat seat;
        seat.Side = CombatantSide::Enemy;
        seat.Index = foeIndex;
        seat.Agility = m_Enemies[static_cast<usize>(foeIndex)].Stats.Agility;
        seats.push_back(seat);
    }
    return seats;
}

TurnSeat Battle::CurrentSeat()
{
    if (!m_Started || m_Turns == nullptr || m_Result != BattleResult::Ongoing)
        return {};
    const std::vector<TurnSeat> living = Living();
    if (living.empty())
        return {};
    return m_Turns->Next(living);
}

CombatantSide Battle::Actor()
{
    return CurrentSeat().Side;
}

i32 Battle::ActorIndex()
{
    const TurnSeat seat = CurrentSeat();
    if (seat.Side != CombatantSide::Enemy)
        return -1;
    return seat.Index;
}

ActionResult Battle::Reject(std::string text) const
{
    ActionResult result;
    result.Text = std::move(text);
    result.Result = m_Result;
    return result;
}

void Battle::FinishIfDead()
{
    if (m_Player.Hp <= 0) {
        m_Result = BattleResult::Defeat;
        return;
    }
    Retarget();
    for (const CombatantState& foe : m_Enemies) {
        if (foe.Alive())
            return;
    }
    if (!m_Enemies.empty())
        m_Result = BattleResult::Victory;
}

void Battle::AdvanceTurn()
{
    m_UpkeepDone = false;
    m_Opening.clear();
    if (m_TurnCount < 1000000)
        ++m_TurnCount;
    if (m_Turns != nullptr)
        m_Turns->Advance();
}

void Battle::PrefixOpening(ActionResult& result)
{
    if (m_Opening.empty())
        return;
    if (result.Text.empty())
        result.Text = m_Opening;
    else
        result.Text = m_Opening + " " + result.Text;
    m_Opening.clear();
}

void Battle::GrantFocus(CombatantState& who, i32 amount) const
{
    if (amount <= 0)
        return;
    i64 focus = static_cast<i64>(who.Focus) + amount;
    if (focus > who.MaxFocus)
        focus = who.MaxFocus;
    if (focus < 0)
        focus = 0;
    who.Focus = static_cast<i32>(focus);
}

bool Battle::Afflict(CombatantState& who, const StatusDef& status) const
{
    if (m_Data != nullptr && ResistsStatus(who, status.Id, m_Data->GetStatuses()))
        return false;
    ApplyStatus(who, status);
    return true;
}

void Battle::CollectGranted(bool hero, std::string_view enemyId,
                            std::vector<const SkillDef*>& out) const
{
    out.clear();
    if (m_Data == nullptr)
        return;
    if (hero) {
        const ClassDef* klass = m_Data->FindClass(m_ClassId);
        if (klass == nullptr)
            return;
        for (const HeroGrant& held : m_HeroGrants)
            out.push_back(&held.Skill);
        return;
    }
    const EnemyDef* enemy = m_Data->FindEnemy(enemyId);
    if (enemy == nullptr)
        return;
    for (const SkillGrant& grant : enemy->Skills)
        out.push_back(&grant.Resolved);
}

void Battle::ApplyAlways(CombatantState& who, bool hero) const
{
    std::vector<const SkillDef*> granted;
    CollectGranted(hero, who.Id, granted);
    for (const SkillDef* skill : granted) {
        for (const EffectDef& effect : skill->Effects) {
            if (effect.Trigger != EffectTrigger::Always || effect.Op != EffectOp::Stat)
                continue;
            if (!effect.Stat.has_value())
                continue;
            i32 value = who.Stats.Get(*effect.Stat) + effect.Amount.value_or(0);
            if (value < 0)
                value = 0;
            who.Stats.Set(*effect.Stat, value);
        }
    }
}

void Battle::RunTrigger(CombatantState& who, EffectTrigger trigger) const
{
    if (m_Data == nullptr)
        return;
    std::vector<const SkillDef*> granted;
    CollectGranted(&who == &m_Player, who.Id, granted);
    for (const SkillDef* skill : granted) {
        for (const EffectDef& effect : skill->Effects) {
            if (effect.Trigger != trigger)
                continue;
            if (effect.Op == EffectOp::GrantFocus) {
                GrantFocus(who, effect.Amount.value_or(0));
            } else if (effect.Op == EffectOp::Heal) {
                const i32 amount = effect.Power.value_or(0);
                if (amount <= 0 || who.Hp >= who.MaxHp)
                    continue;
                const i32 room = who.MaxHp - who.Hp;
                who.Hp += amount < room ? amount : room;
            } else if (effect.Op == EffectOp::ApplyStatus && effect.Status.has_value()) {
                const StatusDef* status = m_Data->FindStatus(*effect.Status);
                if (status != nullptr)
                    Afflict(who, *status);
            }
        }
    }
}

i32 Battle::MissingFor(const CombatantState& user, std::string_view statId) const
{
    if (user.MaxHp <= 0)
        return 0;
    std::vector<const SkillDef*> granted;
    CollectGranted(&user == &m_Player, user.Id, granted);
    i32 bonus = 0;
    for (const SkillDef* skill : granted) {
        for (const EffectDef& effect : skill->Effects) {
            if (effect.Trigger != EffectTrigger::Always || effect.Op != EffectOp::StatPerMissingHp)
                continue;
            if (!effect.Stat.has_value() || *effect.Stat != statId)
                continue;
            bonus += MissingHpBonus(effect.Amount.value_or(0), effect.Per.value_or(0),
                                    effect.Max.value_or(0), user.Hp, user.MaxHp);
        }
    }
    return bonus;
}

i32 Battle::DefenseOf(const CombatantState& who) const
{
    const i64 defense = static_cast<i64>(who.Stats.Defense) + who.BonusDefense;
    return ClampPool(defense);
}

i32 Battle::RankedPower(const SkillDef& skill, i32 base) const
{
    i32 rank = 1;
    for (const SkillRank& row : m_PlayerRanks) {
        if (row.Id == skill.Id && row.Rank > rank)
            rank = row.Rank;
    }
    i64 power = static_cast<i64>(base) + static_cast<i64>(rank - 1) * m_PowerPerRank;
    if (power < 0)
        power = 0;
    if (power > 9999)
        power = 9999;
    return static_cast<i32>(power);
}

bool Battle::DealsDamage(const SkillDef& skill) const
{
    for (const EffectDef& effect : skill.Effects) {
        if (effect.Trigger == EffectTrigger::OnUse && effect.Op == EffectOp::Damage)
            return true;
    }
    return false;
}

i32 Battle::SkillScore(const SkillDef& skill) const
{
    for (const EffectDef& effect : skill.Effects) {
        if (effect.Trigger == EffectTrigger::OnUse && effect.Op == EffectOp::Damage)
            return effect.Power.value_or(0);
    }
    for (const EffectDef& effect : skill.Effects) {
        if (effect.Power.has_value())
            return *effect.Power;
        if (effect.Amount.has_value())
            return *effect.Amount;
    }
    return 0;
}

const SkillDef* Battle::PlayerSkill(std::string_view skillId) const
{
    for (const HeroGrant& held : m_HeroGrants) {
        if (held.Skill.Kind == SkillKind::Active && held.Skill.Id == skillId)
            return &held.Skill;
    }
    return nullptr;
}

const SkillDef* Battle::EnemySkill(const CombatantState& who, std::string_view skillId) const
{
    if (m_Data == nullptr)
        return nullptr;
    const EnemyDef* enemy = m_Data->FindEnemy(who.Id);
    if (enemy == nullptr)
        return nullptr;
    for (const SkillGrant& grant : enemy->Skills) {
        if (grant.Id == skillId)
            return &grant.Resolved;
    }
    return nullptr;
}

Battle::HitLanded Battle::LandHit(CombatantState& attacker, CombatantState& defender, i32 raw)
{
    HitLanded landed;
    const std::span<const StatusDef> catalog =
        m_Data != nullptr ? m_Data->GetStatuses() : std::span<const StatusDef>{};
    i32 taken = GuardedDamage(raw, defender.GuardPercent);
    const i32 blocked = raw > taken ? raw - taken : 0;
    if (&attacker != &defender && blocked > 0 && defender.ReflectPercent > 0) {
        landed.Reflected = ReflectedDamage(blocked, defender.ReflectPercent, defender.ReflectFlat);
        // The blow sent back meets the attacker's defense the way any hit does: the defense
        // term comes off, the result stays at least MinDamage, and a taken-percent status
        // is applied after that.
        const i32 armor =
            Scaled(DefenseOf(attacker), m_Rules.DefenseNumerator, m_Rules.DefenseDenominator);
        i32 returned = landed.Reflected - armor;
        if (returned < m_Rules.MinDamage)
            returned = m_Rules.MinDamage;
        landed.Reflected = returned;
        const i32 back = StatusTakenPercent(attacker, catalog);
        if (back != 0)
            landed.Reflected = ScalePercent(landed.Reflected, 100 + back);
        attacker.Hp -= landed.Reflected;
        if (attacker.Hp < 0)
            attacker.Hp = 0;
    }
    if (blocked > 0)
        RunTrigger(defender, EffectTrigger::OnBlock);
    defender.GuardPercent = 0;
    defender.ReflectPercent = 0;
    defender.ReflectFlat = 0;
    const i32 takenPct = StatusTakenPercent(defender, catalog);
    if (takenPct != 0)
        taken = ScalePercent(taken, 100 + takenPct);
    defender.Hp -= taken;
    if (defender.Hp < 0)
        defender.Hp = 0;
    landed.Taken = taken;
    if (taken > 0)
        RunTrigger(defender, EffectTrigger::OnDamaged);
    if (landed.Reflected > 0)
        RunTrigger(attacker, EffectTrigger::OnDamaged);
    return landed;
}

i32 Battle::NoteSkillUse(CombatantState& who, const std::string& skillId)
{
    for (usize i = 0; i < who.SkillUseIds.size(); ++i) {
        if (who.SkillUseIds[i] == skillId) {
            ++who.SkillUseCounts[i];
            return who.SkillUseCounts[i];
        }
    }
    who.SkillUseIds.push_back(skillId);
    who.SkillUseCounts.push_back(1);
    return 1;
}

void Battle::EnterPhase(i32 index, std::string& note)
{
    if (m_Data == nullptr || index < 0 || index >= static_cast<i32>(m_Enemies.size()))
        return;
    CombatantState& foe = m_Enemies[static_cast<usize>(index)];
    if (foe.Phase >= 2 || foe.MaxHp <= 0 || foe.Hp <= 0)
        return;
    const EnemyDef* enemy = m_Data->FindEnemy(foe.Id);
    if (enemy == nullptr || enemy->PhaseAtPercent <= 0)
        return;
    if (static_cast<i64>(foe.Hp) * 100 > static_cast<i64>(foe.MaxHp) * enemy->PhaseAtPercent)
        return;
    foe.Phase = 2;
    if (!enemy->PhaseStatus.empty()) {
        const StatusDef* status = m_Data->FindStatus(enemy->PhaseStatus);
        if (status != nullptr)
            Afflict(foe, *status);
    }
    if (!note.empty())
        note += " ";
    note += foe.Name + " enters phase " + std::to_string(foe.Phase) + ".";
}

void Battle::ArmSummon(i32 index, std::string& note)
{
    if (m_Data == nullptr || m_Result != BattleResult::Ongoing)
        return;
    if (index < 0 || index >= static_cast<i32>(m_Enemies.size()))
        return;
    CombatantState& foe = m_Enemies[static_cast<usize>(index)];
    if (foe.Phase < 2 || !foe.Alive())
        return;
    const EnemyDef* enemy = m_Data->FindEnemy(foe.Id);
    if (enemy == nullptr || enemy->SummonEvery <= 0 || enemy->SummonId.empty())
        return;
    ++foe.PhaseTurns;
    if (foe.PhaseTurns % enemy->SummonEvery != 0)
        return;
    foe.ShardDue = true;
    const EnemyDef* add = m_Data->FindEnemy(enemy->SummonId);
    const std::string addName = add != nullptr ? add->Name : enemy->SummonId;
    if (!note.empty())
        note += " ";
    note += addName + " stirs.";
}

Battle::TurnOpen Battle::PrepareTurn(CombatantState& user)
{
    TurnOpen open;
    open.Continue = true;
    if (m_UpkeepDone || m_Data == nullptr)
        return open;
    m_UpkeepDone = true;

    if (user.BonusDefenseTurns > 0) {
        --user.BonusDefenseTurns;
        if (user.BonusDefenseTurns <= 0) {
            user.BonusDefense = 0;
            user.BonusDefenseTurns = 0;
        }
    }

    const StatusTick tick = TickStatuses(user, m_Data->GetStatuses());
    if (tick.Bleed > 0) {
        user.Hp -= tick.Bleed;
        if (user.Hp < 0)
            user.Hp = 0;
        m_Opening = user.Name + " bleeds for " + std::to_string(tick.Bleed) + ".";
    }
    FinishIfDead();
    if (m_Result != BattleResult::Ongoing) {
        open.Continue = false;
        open.Result.Accepted = true;
        open.Result.Damage = tick.Bleed;
        open.Result.Text = m_Opening;
        m_Opening.clear();
        if (m_Result == BattleResult::Victory)
            open.Result.Text += " Victory.";
        else if (m_Result == BattleResult::Defeat)
            open.Result.Text += " Defeat.";
        open.Result.Result = m_Result;
        return open;
    }
    if (tick.Skip) {
        open.Continue = false;
        open.Result.Accepted = true;
        open.Result.Text = user.Name + " is stunned.";
        PrefixOpening(open.Result);
        open.Result.Result = m_Result;
        AdvanceTurn();
        return open;
    }
    RunTrigger(user, EffectTrigger::OnTurnStart);
    if (&user == &m_Player)
        GrantFocus(user, m_Rules.FocusPerTurn);
    return open;
}

void Battle::SetPlayerRanks(i32 powerPerRank, std::vector<SkillRank> ranks)
{
    m_PowerPerRank = powerPerRank > 0 ? powerPerRank : 0;
    m_PlayerRanks = std::move(ranks);
}

void Battle::SetGear(StatBlock gear)
{
    m_Gear = gear;
}

void Battle::SetWornItems(std::vector<std::string> itemIds)
{
    m_WornItems = std::move(itemIds);
}

void Battle::SetPickedSkills(std::vector<std::string> skillIds)
{
    m_PickedSkills = std::move(skillIds);
}

void Battle::SetBaseStats(StatBlock stats)
{
    m_BaseStats = stats;
    m_HasBaseStats = true;
}

ActionResult Battle::UseItem(const ItemDef& item)
{
    if (!m_Started)
        return Reject("There is no fight.");
    if (m_Result != BattleResult::Ongoing)
        return Reject("The fight is over.");
    if (Actor() != CombatantSide::Player)
        return Reject("It is not your turn.");
    if (item.Slot != GearSlot::Consumable || (item.Heal <= 0 && item.Focus <= 0))
        return Reject("That cannot be used in a fight.");

    const TurnOpen open = PrepareTurn(m_Player);
    if (!open.Continue)
        return open.Result;

    const i32 healthBefore = m_Player.Hp;
    const i32 focusBefore = m_Player.Focus;
    if (item.Heal > 0) {
        i64 healed = static_cast<i64>(m_Player.Hp) + item.Heal;
        if (healed > m_Player.MaxHp)
            healed = m_Player.MaxHp;
        m_Player.Hp = static_cast<i32>(healed);
    }
    if (item.Focus > 0)
        GrantFocus(m_Player, item.Focus);

    ActionResult result;
    result.Accepted = true;
    result.SkillId = item.Id;
    const i32 healthGained = m_Player.Hp - healthBefore;
    const i32 focusGained = m_Player.Focus - focusBefore;
    result.Healing = healthGained;
    result.Text = m_Player.Name + " uses " + item.Name;
    if (healthGained > 0)
        result.Text += " and heals " + std::to_string(healthGained);
    if (focusGained > 0)
        result.Text += ". Focus +" + std::to_string(focusGained);
    if (healthGained <= 0 && focusGained <= 0)
        result.Text += " but nothing changes";
    result.Text += ".";
    PrefixOpening(result);
    result.Result = m_Result;
    if (m_Result == BattleResult::Ongoing)
        AdvanceTurn();
    return result;
}

void Battle::SetPlayerHp(i32 hp)
{
    if (!m_Started || m_Player.MaxHp <= 0)
        return;
    if (hp < 1)
        hp = 1;
    if (hp > m_Player.MaxHp)
        hp = m_Player.MaxHp;
    m_Player.Hp = hp;
}

void Battle::SetPlayerFocus(i32 focus)
{
    if (!m_Started)
        return;
    if (focus < 0)
        focus = 0;
    if (focus > m_Player.MaxFocus)
        focus = m_Player.MaxFocus;
    m_Player.Focus = focus;
}

void Battle::SetEnemyHp(i32 hp)
{
    if (!m_Started || m_Target < 0 || m_Target >= static_cast<i32>(m_Enemies.size()))
        return;
    CombatantState& foe = m_Enemies[static_cast<usize>(m_Target)];
    if (foe.MaxHp <= 0)
        return;
    if (hp < 0)
        hp = 0;
    if (hp > foe.MaxHp)
        hp = foe.MaxHp;
    foe.Hp = hp;
    if (foe.Hp <= 0) {
        FinishIfDead();
        return;
    }
    std::string note;
    EnterPhase(m_Target, note);
}

FightSnapshot Battle::ExportFight()
{
    FightSnapshot shot;
    if (!m_Started || m_Result != BattleResult::Ongoing || m_Turns == nullptr)
        return shot;
    const TurnSeat seat = CurrentSeat();
    shot.Known = true;
    shot.TurnCount = m_TurnCount;
    shot.Actor = seat.Side;
    shot.ActorIndex = seat.Side == CombatantSide::Enemy ? seat.Index : -1;
    shot.Target = m_Target;
    shot.RandomCursor = m_RandomCursor;
    shot.UpkeepDone = m_UpkeepDone;
    m_Turns->Capture(shot.Order, shot.OrderIndex);
    shot.Player = SnapOf(m_Player);
    shot.Foes.reserve(m_Enemies.size());
    for (const CombatantState& foe : m_Enemies)
        shot.Foes.push_back(SnapOf(foe));
    return shot;
}

bool Battle::ImportFight(const FightSnapshot& shot, std::string& error)
{
    if (!m_Started || m_Data == nullptr || m_Turns == nullptr || !shot.Known ||
        shot.Order.empty()) {
        error = "the fight cannot be restored";
        return false;
    }
    if (shot.Foes.size() < m_Enemies.size()) {
        error = "the save's foes do not match this fight";
        return false;
    }
    const i32 built = static_cast<i32>(m_Enemies.size());
    for (i32 index = 0; index < built; ++index) {
        if (shot.Foes[static_cast<usize>(index)].Id != m_Enemies[static_cast<usize>(index)].Id) {
            error = "the save's foes do not match this fight";
            return false;
        }
    }
    for (i32 index = built; index < static_cast<i32>(shot.Foes.size()); ++index) {
        if (m_Enemies.size() >= static_cast<usize>(kMaxCombatants)) {
            error = "the save has too many foes";
            return false;
        }
        const EnemyDef* def = m_Data->FindEnemy(shot.Foes[static_cast<usize>(index)].Id);
        if (def == nullptr) {
            error = "the save's foe does not exist";
            return false;
        }
        CombatantState foe;
        MakeFoe(*def, foe);
        m_Enemies.push_back(std::move(foe));
    }
    ApplySnap(m_Player, shot.Player, true);
    for (i32 index = 0; index < static_cast<i32>(m_Enemies.size()); ++index)
        ApplySnap(m_Enemies[static_cast<usize>(index)], shot.Foes[static_cast<usize>(index)],
                  false);
    NameCombatants(m_Data, m_Enemies);
    m_Target = shot.Target;
    Retarget();
    m_RandomCursor = shot.RandomCursor < 0 ? 0 : shot.RandomCursor;
    m_UpkeepDone = shot.UpkeepDone;
    m_TurnCount = shot.TurnCount < 0 ? 0 : shot.TurnCount;
    m_Opening.clear();
    m_Turns->Place(shot.Order, shot.OrderIndex);
    FinishIfDead();
    if (m_Result != BattleResult::Ongoing) {
        error = "the saved fight is already over";
        return false;
    }
    error.clear();
    return true;
}

bool Battle::Inflict(CombatantSide side, std::string_view statusId, std::string& error)
{
    if (!m_Started || m_Data == nullptr) {
        error = "there is no fight";
        return false;
    }
    const StatusDef* status = m_Data->FindStatus(statusId);
    if (status == nullptr) {
        error = "status '" + std::string(statusId) + "' does not exist";
        return false;
    }
    if (side == CombatantSide::Player) {
        Afflict(m_Player, *status);
    } else {
        if (m_Target < 0 || m_Target >= static_cast<i32>(m_Enemies.size())) {
            error = "there is no enemy to afflict";
            return false;
        }
        Afflict(m_Enemies[static_cast<usize>(m_Target)], *status);
    }
    error.clear();
    return true;
}

EffectAim Battle::AimOf(const SkillDef& skill, const EffectDef& effect) const
{
    if (effect.Aim.has_value())
        return *effect.Aim;
    if (skill.Target == SkillTarget::Self)
        return EffectAim::Self;
    if (skill.Target == SkillTarget::AllEnemies)
        return EffectAim::AllEnemies;
    return EffectAim::Target;
}

void Battle::CollectAims(const CombatantState& user, EffectAim aim, std::vector<AimRef>& out)
{
    out.clear();
    const i32 count = static_cast<i32>(m_Enemies.size());
    if (aim == EffectAim::Self) {
        AimRef self;
        if (&user == &m_Player) {
            self.Player = true;
            out.push_back(self);
            return;
        }
        for (i32 foeIndex = 0; foeIndex < count; ++foeIndex) {
            if (&m_Enemies[static_cast<usize>(foeIndex)] != &user)
                continue;
            self.Index = foeIndex;
            out.push_back(self);
            return;
        }
        return;
    }

    if (aim == EffectAim::Target) {
        if (&user == &m_Player) {
            if (m_Target >= 0 && m_Target < count &&
                m_Enemies[static_cast<usize>(m_Target)].Alive()) {
                AimRef foe;
                foe.Index = m_Target;
                out.push_back(foe);
            }
            return;
        }
        if (m_Player.Alive()) {
            AimRef player;
            player.Player = true;
            out.push_back(player);
        }
        return;
    }

    std::vector<i32> pool;
    for (i32 foeIndex = 0; foeIndex < count; ++foeIndex) {
        if (!m_Enemies[static_cast<usize>(foeIndex)].Alive())
            continue;
        if (&m_Enemies[static_cast<usize>(foeIndex)] == &user)
            continue;
        pool.push_back(foeIndex);
    }
    if (pool.empty())
        return;
    if (aim == EffectAim::RandomEnemy) {
        const i32 poolCount = static_cast<i32>(pool.size());
        const i32 cursor = m_RandomCursor % poolCount;
        ++m_RandomCursor;
        if (m_RandomCursor > 1000000)
            m_RandomCursor = 0;
        AimRef foe;
        foe.Index = pool[static_cast<usize>(cursor)];
        out.push_back(foe);
        return;
    }
    for (const i32 foeIndex : pool) {
        AimRef foe;
        foe.Index = foeIndex;
        out.push_back(foe);
    }
}

CombatantState& Battle::Aimed(const AimRef& aim)
{
    if (aim.Player)
        return m_Player;
    return m_Enemies[static_cast<usize>(aim.Index)];
}

bool Battle::EffectLands(const EffectDef& effect, const CombatantState& foe)
{
    if (!effect.IfTargetHas.has_value())
        return true;
    for (const StatusInstance& held : foe.Statuses) {
        if (held.Id == *effect.IfTargetHas)
            return true;
    }
    return false;
}

usize Battle::AimSlot(std::span<const AimRef> aims, const AimRef& aim)
{
    for (usize slot = 0; slot < aims.size(); ++slot) {
        if (aims[slot].Player == aim.Player && aims[slot].Index == aim.Index)
            return slot;
    }
    return aims.size();
}

bool Battle::Spends(std::span<const StatusSpend> spends, const AimRef& aim,
                    std::string_view statusId)
{
    for (const StatusSpend& spend : spends) {
        if (spend.StatusId == statusId && spend.Aim.Player == aim.Player &&
            spend.Aim.Index == aim.Index)
            return true;
    }
    return false;
}

ActionResult Battle::Resolve(CombatantState& user, const SkillDef& skill)
{
    if (user.Focus < skill.FocusCost)
        return Reject("Not enough focus.");

    user.Focus -= skill.FocusCost;
    ActionResult result;
    result.Accepted = true;
    result.SkillId = skill.Id;
    const bool hero = &user == &m_Player;
    i32 userIndex = -1;
    if (!hero) {
        const i32 count = static_cast<i32>(m_Enemies.size());
        for (i32 foeIndex = 0; foeIndex < count; ++foeIndex) {
            if (&m_Enemies[static_cast<usize>(foeIndex)] == &user) {
                userIndex = foeIndex;
                break;
            }
        }
    }
    const std::span<const StatusDef> catalog =
        m_Data != nullptr ? m_Data->GetStatuses() : std::span<const StatusDef>{};

    bool damageRan = false;
    // Aims a damage effect landed on. onHit reaches only these, and two hits on one aim share
    // one blow line. A status this use spends stays up until the last onHit, so later damage
    // in the use still sees it, and that use does not apply it again.
    std::vector<AimRef> struckAims;
    std::vector<StatusSpend> spends;
    i32 taken = 0;
    i32 reflected = 0;
    i32 struck = 0;
    bool sawGuard = false;
    i32 guardAmount = 0;
    bool sawReflect = false;
    i32 reflectPercent = 0;
    bool sawHeal = false;
    bool sawEmpower = false;
    bool sawHarden = false;
    i32 hardenAmount = 0;
    bool noted = false;
    i32 notedUses = 0;
    std::string extra;
    std::string phaseNote;
    struct StrikeNote {
        std::string Name;
        i32 Taken = 0;
    };
    std::vector<StrikeNote> blows;
    auto countUse = [&]() {
        if (!noted) {
            notedUses = NoteSkillUse(user, skill.Id);
            noted = true;
        }
        return notedUses;
    };

    for (const EffectDef& effect : skill.Effects) {
        if (effect.Trigger != EffectTrigger::OnUse)
            continue;
        if (effect.Op == EffectOp::Damage) {
            std::vector<AimRef> aims;
            CollectAims(user, AimOf(skill, effect), aims);
            const i32 empower = user.EmpowerPercent;
            user.EmpowerPercent = 0;
            const i32 base = effect.Power.value_or(0);
            const i32 power = hero ? RankedPower(skill, base) : base;
            i32 scalingStat = 0;
            if (effect.Scaling.has_value()) {
                if (*effect.Scaling == "defense")
                    scalingStat = DefenseOf(user);
                else
                    scalingStat = user.Stats.Get(*effect.Scaling);
                scalingStat += MissingFor(user, *effect.Scaling);
            }
            const i32 dealt = StatusDealtPercent(user, catalog);
            for (const AimRef& aim : aims) {
                CombatantState& foe = Aimed(aim);
                if (!EffectLands(effect, foe))
                    continue;
                const usize slot = AimSlot(struckAims, aim);
                if (slot == struckAims.size()) {
                    struckAims.push_back(aim);
                    StrikeNote blow;
                    blow.Name = foe.Name;
                    blows.push_back(std::move(blow));
                }
                if (effect.Consume && effect.IfTargetHas.has_value()) {
                    StatusSpend spend;
                    spend.Aim = aim;
                    spend.StatusId = *effect.IfTargetHas;
                    spends.push_back(std::move(spend));
                }
                StrikeQuery hit;
                hit.Power = power;
                hit.ScalingStat = scalingStat;
                hit.Defense = DefenseOf(foe);
                hit.EmpowerPercent = empower;
                i32 raw = StrikeDamage(hit, m_Rules);
                if (dealt != 0)
                    raw = ScalePercent(raw, 100 + dealt);
                const HitLanded landed = LandHit(user, foe, raw);
                taken += landed.Taken;
                reflected += landed.Reflected;
                blows[slot].Taken += landed.Taken;
                if (!aim.Player)
                    EnterPhase(aim.Index, phaseNote);
                if (userIndex >= 0)
                    EnterPhase(userIndex, phaseNote);
            }
        } else if (effect.Op == EffectOp::Guard) {
            user.GuardPercent = effect.Amount.value_or(0);
            user.ReflectPercent = 0;
            user.ReflectFlat = 0;
            guardAmount = user.GuardPercent;
            sawGuard = true;
            sawReflect = false;
        } else if (effect.Op == EffectOp::Reflect) {
            user.ReflectPercent = effect.Percent.value_or(0);
            i32 scaling = 0;
            if (effect.Scaling.has_value()) {
                if (*effect.Scaling == "defense")
                    scaling = DefenseOf(user);
                else
                    scaling = user.Stats.Get(*effect.Scaling);
            }
            const i32 flatBase = effect.Power.value_or(0);
            const i32 flatPower = hero ? RankedPower(skill, flatBase) : flatBase;
            user.ReflectFlat =
                flatPower + Scaled(scaling, m_Rules.AttackNumerator, m_Rules.AttackDenominator);
            reflectPercent = user.ReflectPercent;
            sawReflect = true;
        } else if (effect.Op == EffectOp::Heal) {
            const i32 base = effect.Power.value_or(0);
            const i32 power = hero ? RankedPower(skill, base) : base;
            i32 scaling = 0;
            if (effect.Scaling.has_value())
                scaling = user.Stats.Get(*effect.Scaling);
            const i32 amount = HealAmount(power, scaling, m_Rules);
            const i32 before = user.Hp;
            const i32 room = user.MaxHp - user.Hp;
            user.Hp = before + (amount < room ? amount : room);
            result.Healing = user.Hp - before;
            sawHeal = true;
        } else if (effect.Op == EffectOp::Empower) {
            user.EmpowerPercent = effect.Amount.value_or(0);
            sawEmpower = true;
        } else if (effect.Op == EffectOp::Harden) {
            user.BonusDefense = effect.Amount.value_or(0);
            user.BonusDefenseTurns = effect.Turns.value_or(1);
            hardenAmount = user.BonusDefense;
            sawHarden = true;
        } else if (effect.Op == EffectOp::GrantFocus) {
            GrantFocus(user, effect.Amount.value_or(0));
        } else if (effect.Op == EffectOp::ApplyStatus && effect.Status.has_value() &&
                   m_Data != nullptr) {
            const i32 every = effect.Every.value_or(1);
            const i32 uses = countUse();
            if (every <= 1 || uses % every == 0) {
                const StatusDef* inflicted = m_Data->FindStatus(*effect.Status);
                if (inflicted != nullptr) {
                    std::vector<AimRef> aims;
                    CollectAims(user, AimOf(skill, effect), aims);
                    for (const AimRef& aim : aims) {
                        if (!Afflict(Aimed(aim), *inflicted))
                            continue;
                        extra += " ";
                        extra += inflicted->Name;
                        extra += ".";
                    }
                }
            }
        }
    }

    struck = static_cast<i32>(blows.size());
    damageRan = !struckAims.empty();

    if (damageRan) {
        for (const EffectDef& follow : skill.Effects) {
            if (follow.Trigger != EffectTrigger::OnHit)
                continue;
            std::vector<AimRef> aims;
            CollectAims(user, AimOf(skill, follow), aims);
            // A miss is not a hit: an aim no damage effect struck gets no onHit. Spending a
            // status also blocks this use from applying that same status to that aim.
            std::vector<AimRef> reached;
            for (const AimRef& aim : aims) {
                if (AimSlot(struckAims, aim) == struckAims.size())
                    continue;
                if (follow.Op == EffectOp::ApplyStatus && follow.Status.has_value() &&
                    Spends(spends, aim, *follow.Status))
                    continue;
                reached.push_back(aim);
            }
            aims = std::move(reached);
            if (follow.Op == EffectOp::StunIfWindingUp) {
                for (const AimRef& aim : aims) {
                    CombatantState& foe = Aimed(aim);
                    if (foe.WindupSkill.empty())
                        continue;
                    if (follow.Status.has_value() && m_Data != nullptr) {
                        const StatusDef* stun = m_Data->FindStatus(*follow.Status);
                        if (stun != nullptr) {
                            if (!Afflict(foe, *stun)) {
                                // A resisted stun leaves the wind-up running.
                                extra += " ";
                                extra += foe.Name;
                                extra += " shrugs off the stun.";
                                continue;
                            }
                            extra += " ";
                            extra += foe.Name;
                            extra += " is stunned.";
                        }
                    }
                    foe.WindupSkill.clear();
                    foe.WindupLeft = 0;
                    foe.WindupTotal = 0;
                }
            } else if (follow.Op == EffectOp::ApplyStatus && follow.Status.has_value() &&
                       m_Data != nullptr) {
                const i32 every = follow.Every.value_or(1);
                const i32 uses = countUse();
                if (every > 1 && uses % every != 0)
                    continue;
                const StatusDef* inflicted = m_Data->FindStatus(*follow.Status);
                if (inflicted == nullptr)
                    continue;
                for (const AimRef& aim : aims) {
                    if (!Afflict(Aimed(aim), *inflicted))
                        continue;
                    extra += " ";
                    extra += inflicted->Name;
                    extra += ".";
                }
            }
        }
    }

    for (const StatusSpend& spend : spends) {
        CombatantState& foe = Aimed(spend.Aim);
        bool removed = false;
        std::vector<StatusInstance>::iterator held = foe.Statuses.begin();
        while (held != foe.Statuses.end()) {
            if (held->Id != spend.StatusId) {
                ++held;
                continue;
            }
            held = foe.Statuses.erase(held);
            removed = true;
        }
        if (!removed || m_Data == nullptr)
            continue;
        const StatusDef* gone = m_Data->FindStatus(spend.StatusId);
        extra += " ";
        extra += foe.Name;
        if (gone != nullptr) {
            extra += " loses ";
            extra += gone->Name;
            extra += ".";
        } else {
            extra += " recovers.";
        }
    }

    if (damageRan) {
        if (struck <= 1) {
            result.Damage = taken;
            result.Text = user.Name + " uses " + skill.Name + " for " + std::to_string(taken) + ".";
        } else {
            result.Damage = taken;
            result.Text = user.Name + " uses " + skill.Name + ".";
            for (const StrikeNote& blow : blows) {
                result.Text += " ";
                result.Text += blow.Name;
                result.Text += " ";
                result.Text += std::to_string(blow.Taken);
                result.Text += ".";
            }
        }
        if (reflected > 0)
            result.Text += " " + std::to_string(reflected) + " returns.";
    } else if (sawGuard && sawReflect) {
        result.Text = user.Name + " uses " + skill.Name + ". The next hit loses " +
                      std::to_string(guardAmount) + " percent, and " +
                      std::to_string(reflectPercent) + " percent of what it stops returns.";
    } else if (sawGuard) {
        result.Text = user.Name + " uses " + skill.Name + ". The next hit is reduced by " +
                      std::to_string(guardAmount) + " percent.";
    } else if (sawHeal) {
        if (result.Healing == 0)
            result.Text = user.Name + " uses " + skill.Name + " but is already at full health.";
        else
            result.Text = user.Name + " uses " + skill.Name + " and heals " +
                          std::to_string(result.Healing) + ".";
    } else if (sawEmpower) {
        result.Text = user.Name + " uses " + skill.Name + ". The next blow hits harder.";
    } else if (sawHarden) {
        result.Text =
            user.Name + " uses " + skill.Name + ". Defense +" + std::to_string(hardenAmount) + ".";
    } else {
        result.Text = user.Name + " uses " + skill.Name + ".";
    }
    result.Text += extra;
    if (!phaseNote.empty()) {
        result.Text += " ";
        result.Text += phaseNote;
    }

    FinishIfDead();
    PrefixOpening(result);
    result.Result = m_Result;
    if (m_Result == BattleResult::Victory)
        result.Text += " Victory.";
    else if (m_Result == BattleResult::Defeat)
        result.Text += " Defeat.";
    if (m_Result == BattleResult::Ongoing)
        AdvanceTurn();
    return result;
}

Battle::SummonSpot Battle::FindSummonSpot(i32 index) const
{
    SummonSpot spot;
    const i32 count = static_cast<i32>(m_Enemies.size());
    if (index >= 0 && index < count)
        spot.HostName = m_Enemies[static_cast<usize>(index)].Name;
    else
        spot.HostName = "The foe";
    const EnemyDef* host = m_Data != nullptr && index >= 0 && index < count
                               ? m_Data->FindEnemy(m_Enemies[static_cast<usize>(index)].Id)
                               : nullptr;
    spot.Add = host != nullptr && m_Data != nullptr ? m_Data->FindEnemy(host->SummonId) : nullptr;
    for (i32 body = 0; body < count; ++body) {
        const CombatantState& present = m_Enemies[static_cast<usize>(body)];
        if (!present.Alive()) {
            if (spot.OpenSlot < 0)
                spot.OpenSlot = body;
            continue;
        }
        ++spot.Living;
        if (spot.Add != nullptr && present.Id == spot.Add->Id)
            spot.Already = true;
    }
    return spot;
}

bool Battle::SummonTakesTurn(i32 index) const
{
    const SummonSpot spot = FindSummonSpot(index);
    if (spot.Add != nullptr && !spot.Already && spot.Living < kMaxCombatants)
        return true;
    const i32 count = static_cast<i32>(m_Enemies.size());
    const EnemyDef* host = m_Data != nullptr && index >= 0 && index < count
                               ? m_Data->FindEnemy(m_Enemies[static_cast<usize>(index)].Id)
                               : nullptr;
    // A missing definition still reports the refusal. Act falls through to the pattern.
    if (host == nullptr)
        return true;
    return host->IfBlocked != BlockedSummon::Act;
}

ActionResult Battle::ResolveSummon(i32 index)
{
    ActionResult result;
    result.Accepted = true;
    const SummonSpot spot = FindSummonSpot(index);
    result.SkillId = spot.Add != nullptr ? spot.Add->Id : "";
    const i32 count = static_cast<i32>(m_Enemies.size());
    if (spot.Add == nullptr) {
        result.Text = spot.HostName + " calls, and nothing answers.";
    } else if (spot.Already || spot.Living >= kMaxCombatants) {
        result.Text = spot.Add->Name + " is already in the fight.";
    } else {
        CombatantState added;
        MakeFoe(*spot.Add, added);
        i32 joined = spot.OpenSlot;
        if (count >= kMaxCombatants && spot.OpenSlot >= 0) {
            m_Enemies[static_cast<usize>(spot.OpenSlot)] = std::move(added);
        } else {
            m_Enemies.push_back(std::move(added));
            joined = static_cast<i32>(m_Enemies.size()) - 1;
        }
        NameCombatants(m_Data, m_Enemies);
        result.Text = m_Enemies[static_cast<usize>(joined)].Name + " joins the fight.";
    }
    FinishIfDead();
    PrefixOpening(result);
    result.Result = m_Result;
    if (m_Result == BattleResult::Victory)
        result.Text += " Victory.";
    else if (m_Result == BattleResult::Defeat)
        result.Text += " Defeat.";
    if (m_Result == BattleResult::Ongoing)
        AdvanceTurn();
    return result;
}

const SkillDef* Battle::TakeEnemySkill(CombatantState& who)
{
    if (m_Data == nullptr)
        return nullptr;
    const EnemyDef* enemy = m_Data->FindEnemy(who.Id);
    if (enemy != nullptr && !enemy->Pattern.empty()) {
        const i32 count = static_cast<i32>(enemy->Pattern.size());
        for (i32 step = 0; step < count; ++step) {
            const i32 patternIndex = (who.PatternStep + step) % count;
            const SkillDef* skill =
                EnemySkill(who, enemy->Pattern[static_cast<usize>(patternIndex)]);
            if (skill == nullptr || skill->Kind != SkillKind::Active)
                continue;
            if (skill->FocusCost > who.Focus)
                continue;
            who.PatternStep = patternIndex + 1;
            return skill;
        }
        return nullptr;
    }

    const SkillDef* best = nullptr;
    if (enemy == nullptr)
        return nullptr;
    for (const SkillGrant& grant : enemy->Skills) {
        const SkillDef* skill = &grant.Resolved;
        if (skill->Kind != SkillKind::Active)
            continue;
        if (skill->FocusCost > who.Focus)
            continue;
        if (best == nullptr) {
            best = skill;
            continue;
        }
        const bool bestHits = DealsDamage(*best);
        const bool hits = DealsDamage(*skill);
        if (hits && !bestHits)
            best = skill;
        else if (hits == bestHits && SkillScore(*skill) > SkillScore(*best))
            best = skill;
    }
    return best;
}

ActionResult Battle::UseSkill(std::string_view skillId)
{
    if (!m_Started)
        return Reject("There is no fight.");
    if (m_Result != BattleResult::Ongoing)
        return Reject("The fight is over.");
    if (Actor() != CombatantSide::Player)
        return Reject("It is not your turn.");

    const TurnOpen open = PrepareTurn(m_Player);
    if (!open.Continue)
        return open.Result;

    const SkillDef* skill = PlayerSkill(skillId);
    if (skill == nullptr || skill->Kind != SkillKind::Active)
        return Reject("You do not know that skill.");
    return Resolve(m_Player, *skill);
}

ActionResult Battle::Wait()
{
    if (!m_Started)
        return Reject("There is no fight.");
    if (m_Result != BattleResult::Ongoing)
        return Reject("The fight is over.");
    if (Actor() != CombatantSide::Player)
        return Reject("It is not your turn.");
    const TurnOpen open = PrepareTurn(m_Player);
    if (!open.Continue)
        return open.Result;
    const i32 before = m_Player.Focus;
    GrantFocus(m_Player, m_Rules.FocusOnWait);
    ActionResult result;
    result.Accepted = true;
    result.Text = m_Player.Name + " waits.";
    if (m_Player.Focus > before)
        result.Text += " Focus +" + std::to_string(m_Player.Focus - before) + ".";
    PrefixOpening(result);
    result.Result = m_Result;
    AdvanceTurn();
    return result;
}

ActionResult Battle::EnemyAct()
{
    if (!m_Started)
        return Reject("There is no fight.");
    if (m_Result != BattleResult::Ongoing)
        return Reject("The fight is over.");
    const TurnSeat seat = CurrentSeat();
    if (seat.Side != CombatantSide::Enemy)
        return Reject("It is not the foe's turn.");
    const i32 foeIndex = seat.Index;
    if (foeIndex < 0 || foeIndex >= static_cast<i32>(m_Enemies.size()) ||
        !m_Enemies[static_cast<usize>(foeIndex)].Alive()) {
        AdvanceTurn();
        return Reject("That foe is already down.");
    }
    const TurnOpen open = PrepareTurn(m_Enemies[static_cast<usize>(foeIndex)]);
    if (!open.Continue)
        return open.Result;

    if (m_Enemies[static_cast<usize>(foeIndex)].ShardDue) {
        m_Enemies[static_cast<usize>(foeIndex)].ShardDue = false;
        if (SummonTakesTurn(foeIndex)) {
            ActionResult summoned = ResolveSummon(foeIndex);
            ArmSummon(foeIndex, summoned.Text);
            return summoned;
        }
    }

    if (!m_Enemies[static_cast<usize>(foeIndex)].WindupSkill.empty() && m_Data != nullptr) {
        const SkillDef* winding = EnemySkill(m_Enemies[static_cast<usize>(foeIndex)],
                                             m_Enemies[static_cast<usize>(foeIndex)].WindupSkill);
        if (winding == nullptr) {
            m_Enemies[static_cast<usize>(foeIndex)].WindupSkill.clear();
            m_Enemies[static_cast<usize>(foeIndex)].WindupLeft = 0;
            m_Enemies[static_cast<usize>(foeIndex)].WindupTotal = 0;
        } else if (m_Enemies[static_cast<usize>(foeIndex)].WindupLeft > 0) {
            const i32 shown = m_Enemies[static_cast<usize>(foeIndex)].WindupTotal -
                              m_Enemies[static_cast<usize>(foeIndex)].WindupLeft + 1;
            --m_Enemies[static_cast<usize>(foeIndex)].WindupLeft;
            ActionResult result;
            result.Accepted = true;
            result.SkillId = winding->Id;
            result.Text = m_Enemies[static_cast<usize>(foeIndex)].Name + " is winding up " +
                          winding->Name + " " + std::to_string(shown) + "/" +
                          std::to_string(m_Enemies[static_cast<usize>(foeIndex)].WindupTotal) + ".";
            PrefixOpening(result);
            ArmSummon(foeIndex, result.Text);
            result.Result = m_Result;
            AdvanceTurn();
            return result;
        } else {
            const SkillDef firing = *winding;
            m_Enemies[static_cast<usize>(foeIndex)].WindupSkill.clear();
            m_Enemies[static_cast<usize>(foeIndex)].WindupLeft = 0;
            m_Enemies[static_cast<usize>(foeIndex)].WindupTotal = 0;
            ActionResult fired = Resolve(m_Enemies[static_cast<usize>(foeIndex)], firing);
            ArmSummon(foeIndex, fired.Text);
            return fired;
        }
    }

    const SkillDef* skill = TakeEnemySkill(m_Enemies[static_cast<usize>(foeIndex)]);
    if (skill == nullptr) {
        ActionResult result;
        result.Accepted = true;
        result.Text = m_Enemies[static_cast<usize>(foeIndex)].Name + " has nothing to spend.";
        PrefixOpening(result);
        ArmSummon(foeIndex, result.Text);
        result.Result = m_Result;
        AdvanceTurn();
        return result;
    }
    if (skill->Windup > 0) {
        m_Enemies[static_cast<usize>(foeIndex)].WindupSkill = skill->Id;
        m_Enemies[static_cast<usize>(foeIndex)].WindupTotal = skill->Windup;
        m_Enemies[static_cast<usize>(foeIndex)].WindupLeft = skill->Windup - 1;
        ActionResult result;
        result.Accepted = true;
        result.SkillId = skill->Id;
        result.Text = m_Enemies[static_cast<usize>(foeIndex)].Name + " is winding up " +
                      skill->Name + " 1/" + std::to_string(skill->Windup) + ".";
        PrefixOpening(result);
        ArmSummon(foeIndex, result.Text);
        result.Result = m_Result;
        AdvanceTurn();
        return result;
    }
    const SkillDef acting = *skill;
    ActionResult acted = Resolve(m_Enemies[static_cast<usize>(foeIndex)], acting);
    ArmSummon(foeIndex, acted.Text);
    return acted;
}

} // namespace Ascension
