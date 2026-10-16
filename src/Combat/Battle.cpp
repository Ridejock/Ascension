#include "Ascension/Combat/Battle.h"

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

void CarryWounds(const CombatantState& from, CombatantState& to)
{
    to.GuardPercent = from.GuardPercent;
    to.EmpowerPercent = from.EmpowerPercent;
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

} // namespace

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

void AgilityRoundModel::Reset()
{
    m_Round.clear();
    m_Index = 0;
    m_Open = false;
}

CombatantSide AgilityRoundModel::Next(i32 playerAgility, i32 enemyAgility)
{
    if (!m_Open || m_Index >= m_Round.size()) {
        m_Round.clear();
        if (playerAgility >= enemyAgility) {
            m_Round.push_back(CombatantSide::Player);
            m_Round.push_back(CombatantSide::Enemy);
        } else {
            m_Round.push_back(CombatantSide::Enemy);
            m_Round.push_back(CombatantSide::Player);
        }
        m_Index = 0;
        m_Open = true;
    }
    return m_Round[m_Index];
}

void AgilityRoundModel::Advance()
{
    if (m_Open)
        ++m_Index;
}

Battle::Battle(const Database& data, std::unique_ptr<TurnModel> turns)
    : m_Data(&data), m_Turns(std::move(turns))
{
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
    const EnemyDef* enemy = m_Data->FindEnemy(m_EnemyId);
    if (enemy == nullptr) {
        error = "enemy '" + m_EnemyId + "' does not exist";
        return false;
    }

    const CombatRules rules = m_Data->GetRules();
    CombatantState player;
    player.Id = klass->Id;
    player.Name = klass->Name;
    player.Stats = m_Data->StatsAt(*klass, m_Level);
    for (const SkillDef* passive : m_Data->Passives(*klass)) {
        i32 value = player.Stats.Get(passive->Stat) + passive->Amount;
        if (value < 0)
            value = 0;
        player.Stats.Set(passive->Stat, value);
    }
    player.MaxHp = ClampPool(static_cast<i64>(player.Stats.Vitality) * rules.HpPerVitality);
    player.MaxFocus = ClampPool(player.Stats.Focus);

    std::vector<std::string> playerSkills;
    for (const SkillDef* skill : m_Data->ActiveSkills(*klass))
        playerSkills.push_back(skill->Id);

    CombatantState foe;
    foe.Id = enemy->Id;
    foe.Name = enemy->Name;
    foe.Stats = enemy->Stats;
    foe.MaxHp = ClampPool(static_cast<i64>(foe.Stats.Vitality) * rules.HpPerVitality);
    foe.MaxFocus = ClampPool(foe.Stats.Focus);
    const std::vector<std::string> foeSkills = enemy->Skills;

    if (keepWounds) {
        CarryWounds(m_Player, player);
        CarryWounds(m_Enemy, foe);
    } else {
        player.Hp = player.MaxHp;
        player.Focus = player.MaxFocus;
        foe.Hp = foe.MaxHp;
        foe.Focus = foe.MaxFocus;
    }

    m_Player = std::move(player);
    m_Enemy = std::move(foe);
    m_PlayerSkills = std::move(playerSkills);
    m_EnemySkills = foeSkills;
    m_Rules = rules;
    return true;
}

bool Battle::Start(std::string_view classId, i32 level, std::string_view enemyId,
                   std::string& error)
{
    m_ClassId = std::string(classId);
    m_EnemyId = std::string(enemyId);
    m_Level = level;
    m_Started = false;
    m_Result = BattleResult::Ongoing;
    if (!Build(error, false))
        return false;
    m_Turns->Reset();
    if (m_Player.Hp <= 0)
        m_Result = BattleResult::Defeat;
    else if (m_Enemy.Hp <= 0)
        m_Result = BattleResult::Victory;
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
    const CombatantState enemy = m_Enemy;
    const std::vector<std::string> playerSkills = m_PlayerSkills;
    const std::vector<std::string> enemySkills = m_EnemySkills;
    const CombatRules rules = m_Rules;
    if (!Build(error, true)) {
        m_Player = player;
        m_Enemy = enemy;
        m_PlayerSkills = playerSkills;
        m_EnemySkills = enemySkills;
        m_Rules = rules;
        return false;
    }
    error.clear();
    return true;
}

CombatantSide Battle::Actor()
{
    if (!m_Started || m_Turns == nullptr || m_Result != BattleResult::Ongoing)
        return CombatantSide::Player;
    return m_Turns->Next(m_Player.Stats.Agility, m_Enemy.Stats.Agility);
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
    if (m_Player.Hp <= 0)
        m_Result = BattleResult::Defeat;
    else if (m_Enemy.Hp <= 0)
        m_Result = BattleResult::Victory;
}

const SkillDef* Battle::ChooseEnemySkill() const
{
    const SkillDef* best = nullptr;
    for (const std::string& id : m_EnemySkills) {
        const SkillDef* skill = m_Data->FindSkill(id);
        if (skill == nullptr || skill->Kind != SkillKind::Active)
            continue;
        if (skill->FocusCost > m_Enemy.Focus)
            continue;
        if (best == nullptr) {
            best = skill;
            continue;
        }
        const bool bestHits = best->Effect == SkillEffect::Damage;
        const bool hits = skill->Effect == SkillEffect::Damage;
        if (hits && !bestHits)
            best = skill;
        else if (hits == bestHits && skill->Power > best->Power)
            best = skill;
    }
    return best;
}

ActionResult Battle::Resolve(CombatantState& user, CombatantState& other, const SkillDef& skill)
{
    if (user.Focus < skill.FocusCost)
        return Reject("Not enough focus.");

    user.Focus -= skill.FocusCost;
    ActionResult result;
    result.Accepted = true;

    if (skill.Effect == SkillEffect::Damage) {
        StrikeQuery hit;
        hit.Power = skill.Power;
        if (!skill.Scaling.empty())
            hit.ScalingStat = user.Stats.Get(skill.Scaling);
        hit.Defense = other.Stats.Defense;
        hit.EmpowerPercent = user.EmpowerPercent;
        user.EmpowerPercent = 0;
        i32 damage = StrikeDamage(hit, m_Rules);
        damage = GuardedDamage(damage, other.GuardPercent);
        other.GuardPercent = 0;
        other.Hp -= damage;
        if (other.Hp < 0)
            other.Hp = 0;
        result.Damage = damage;
        result.Text = user.Name + " uses " + skill.Name + " for " + std::to_string(damage) + ".";
    } else if (skill.Effect == SkillEffect::Guard) {
        user.GuardPercent = skill.Power;
        result.Text = user.Name + " uses " + skill.Name + ". The next hit is reduced by " +
                      std::to_string(skill.Power) + " percent.";
    } else if (skill.Effect == SkillEffect::Heal) {
        const i32 scaling = skill.Scaling.empty() ? 0 : user.Stats.Get(skill.Scaling);
        const i32 amount = HealAmount(skill.Power, scaling, m_Rules);
        const i32 before = user.Hp;
        const i32 room = user.MaxHp - user.Hp;
        user.Hp = before + (amount < room ? amount : room);
        result.Healing = user.Hp - before;
        if (result.Healing == 0)
            result.Text = user.Name + " uses " + skill.Name + " but is already at full health.";
        else
            result.Text = user.Name + " uses " + skill.Name + " and heals " +
                          std::to_string(result.Healing) + ".";
    } else if (skill.Effect == SkillEffect::Empower) {
        user.EmpowerPercent = skill.Power;
        result.Text = user.Name + " uses " + skill.Name + ". The next blow hits harder.";
    } else {
        result.Text = user.Name + " uses " + skill.Name + ".";
    }

    FinishIfDead();
    result.Result = m_Result;
    if (m_Result == BattleResult::Victory)
        result.Text += " Victory.";
    else if (m_Result == BattleResult::Defeat)
        result.Text += " Defeat.";
    if (m_Result == BattleResult::Ongoing)
        m_Turns->Advance();
    return result;
}

ActionResult Battle::UseSkill(std::string_view skillId)
{
    if (!m_Started)
        return Reject("There is no fight.");
    if (m_Result != BattleResult::Ongoing)
        return Reject("The fight is over.");
    if (Actor() != CombatantSide::Player)
        return Reject("It is not your turn.");

    bool known = false;
    for (const std::string& id : m_PlayerSkills) {
        if (id == skillId)
            known = true;
    }
    const SkillDef* skill = known ? m_Data->FindSkill(skillId) : nullptr;
    if (skill == nullptr || skill->Kind != SkillKind::Active)
        return Reject("You do not know that skill.");
    return Resolve(m_Player, m_Enemy, *skill);
}

ActionResult Battle::Wait()
{
    if (!m_Started)
        return Reject("There is no fight.");
    if (m_Result != BattleResult::Ongoing)
        return Reject("The fight is over.");
    if (Actor() != CombatantSide::Player)
        return Reject("It is not your turn.");
    ActionResult result;
    result.Accepted = true;
    result.Text = m_Player.Name + " waits.";
    m_Turns->Advance();
    result.Result = m_Result;
    return result;
}

ActionResult Battle::EnemyAct()
{
    if (!m_Started)
        return Reject("There is no fight.");
    if (m_Result != BattleResult::Ongoing)
        return Reject("The fight is over.");
    if (Actor() != CombatantSide::Enemy)
        return Reject("It is not the foe's turn.");
    const SkillDef* skill = ChooseEnemySkill();
    if (skill == nullptr) {
        ActionResult result;
        result.Accepted = true;
        result.Text = m_Enemy.Name + " has nothing to spend.";
        m_Turns->Advance();
        result.Result = m_Result;
        return result;
    }
    return Resolve(m_Enemy, m_Player, *skill);
}

} // namespace Ascension
