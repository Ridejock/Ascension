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

} // namespace

i32 BloodlustBonus(i32 amount, i32 stepPercent, i32 maxBonus, i32 hp, i32 maxHp)
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
    player.Stats += m_Gear;
    ApplyPassives(player);
    player.MaxHp = ClampPool(static_cast<i64>(player.Stats.Vitality) * rules.HpPerVitality);
    // The hero's pool is the focus stat plus focusBase. Enemies spend only the focus on
    // their stat line, and it does not regenerate, so a pattern is not gated by a pool.
    player.MaxFocus = ClampPool(static_cast<i64>(player.Stats.Focus) + rules.FocusBase);

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
        CopyFightState(m_Player, player);
        CopyFightState(m_Enemy, foe);
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
    m_UpkeepDone = false;
    m_Opening.clear();
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

void Battle::AdvanceTurn()
{
    m_UpkeepDone = false;
    m_Opening.clear();
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

void Battle::ApplyPassives(CombatantState& player) const
{
    player.BulwarkFocus = 0;
    const ClassDef* klass = m_Data == nullptr ? nullptr : m_Data->FindClass(player.Id);
    if (klass == nullptr)
        return;
    for (const SkillDef* passive : m_Data->Passives(*klass)) {
        if (passive->Passive == PassiveEffect::Stat || passive->Passive == PassiveEffect::Bulwark) {
            i32 value = player.Stats.Get(passive->Stat) + passive->Amount;
            if (value < 0)
                value = 0;
            player.Stats.Set(passive->Stat, value);
        }
        if (passive->Passive == PassiveEffect::Bulwark)
            player.BulwarkFocus += passive->FocusGain;
    }
    if (player.BulwarkFocus < 0)
        player.BulwarkFocus = 0;
}

i32 Battle::BloodlustFor(const CombatantState& user, std::string_view statId) const
{
    if (m_Data == nullptr || user.MaxHp <= 0)
        return 0;
    const ClassDef* klass = m_Data->FindClass(user.Id);
    if (klass == nullptr)
        return 0;
    i32 bonus = 0;
    for (const SkillDef* passive : m_Data->Passives(*klass)) {
        if (passive->Passive != PassiveEffect::Bloodied || passive->Stat != statId)
            continue;
        bonus += BloodlustBonus(passive->Amount, passive->StepPercent, passive->Max, user.Hp,
                                user.MaxHp);
    }
    return bonus;
}

i32 Battle::DefenseOf(const CombatantState& who) const
{
    const i64 defense = static_cast<i64>(who.Stats.Defense) + who.BonusDefense;
    return ClampPool(defense);
}

i32 Battle::PlayerPower(const SkillDef& skill) const
{
    i32 rank = 1;
    for (const SkillRank& row : m_PlayerRanks) {
        if (row.Id == skill.Id && row.Rank > rank)
            rank = row.Rank;
    }
    i64 power = static_cast<i64>(skill.Power) + static_cast<i64>(rank - 1) * m_PowerPerRank;
    if (power < 0)
        power = 0;
    if (power > 9999)
        power = 9999;
    return static_cast<i32>(power);
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

void Battle::EnterPhase(std::string& note)
{
    if (m_Data == nullptr || m_Enemy.Phase >= 2 || m_Enemy.MaxHp <= 0 || m_Enemy.Hp <= 0)
        return;
    const EnemyDef* enemy = m_Data->FindEnemy(m_EnemyId);
    if (enemy == nullptr || enemy->PhaseAtPercent <= 0)
        return;
    if (static_cast<i64>(m_Enemy.Hp) * 100 >
        static_cast<i64>(m_Enemy.MaxHp) * enemy->PhaseAtPercent)
        return;
    m_Enemy.Phase = 2;
    if (!enemy->PhaseStatus.empty()) {
        const StatusDef* status = m_Data->FindStatus(enemy->PhaseStatus);
        if (status != nullptr)
            ApplyStatus(m_Enemy, *status);
    }
    if (!note.empty())
        note += " ";
    note += m_Enemy.Name + " enters phase 2.";
}

void Battle::ArmShard(std::string& note)
{
    if (m_Data == nullptr || m_Result != BattleResult::Ongoing || m_Enemy.Phase < 2)
        return;
    const EnemyDef* enemy = m_Data->FindEnemy(m_EnemyId);
    if (enemy == nullptr || enemy->ShardEvery <= 0 || enemy->ShardDamage <= 0)
        return;
    ++m_Enemy.PhaseTurns;
    if (m_Enemy.PhaseTurns % enemy->ShardEvery != 0)
        return;
    m_Enemy.ShardDue = true;
    if (!note.empty())
        note += " ";
    note += "Rubble shifts in the barrow.";
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

void Battle::SetEnemyHp(i32 hp)
{
    if (!m_Started || m_Enemy.MaxHp <= 0)
        return;
    if (hp < 0)
        hp = 0;
    if (hp > m_Enemy.MaxHp)
        hp = m_Enemy.MaxHp;
    m_Enemy.Hp = hp;
    if (m_Enemy.Hp <= 0) {
        FinishIfDead();
        return;
    }
    std::string note;
    EnterPhase(note);
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
    ApplyStatus(side == CombatantSide::Player ? m_Player : m_Enemy, *status);
    error.clear();
    return true;
}

ActionResult Battle::Resolve(CombatantState& user, CombatantState& other, const SkillDef& skill)
{
    if (user.Focus < skill.FocusCost)
        return Reject("Not enough focus.");

    user.Focus -= skill.FocusCost;
    ActionResult result;
    result.Accepted = true;
    result.SkillId = skill.Id;
    const bool hero = &user == &m_Player;
    const bool ranked = skill.Effect == SkillEffect::Damage || skill.Effect == SkillEffect::Heal ||
                        skill.Effect == SkillEffect::Riposte;
    const i32 power = hero && ranked ? PlayerPower(skill) : skill.Power;
    const std::span<const StatusDef> catalog =
        m_Data != nullptr ? m_Data->GetStatuses() : std::span<const StatusDef>{};

    if (skill.Effect == SkillEffect::Damage) {
        StrikeQuery hit;
        hit.Power = power;
        if (!skill.Scaling.empty()) {
            if (skill.Scaling == "defense")
                hit.ScalingStat = DefenseOf(user);
            else
                hit.ScalingStat = user.Stats.Get(skill.Scaling);
            hit.ScalingStat += BloodlustFor(user, skill.Scaling);
        }
        hit.Defense = DefenseOf(other);
        hit.EmpowerPercent = user.EmpowerPercent;
        user.EmpowerPercent = 0;
        i32 damage = StrikeDamage(hit, m_Rules);
        const i32 dealt = StatusDealtPercent(user, catalog);
        if (dealt != 0)
            damage = ScalePercent(damage, 100 + dealt);
        const i32 raw = damage;
        i32 taken = GuardedDamage(raw, other.GuardPercent);
        const i32 blocked = raw > taken ? raw - taken : 0;
        i32 reflected = 0;
        if (blocked > 0 && other.ReflectPercent > 0) {
            reflected = ReflectedDamage(blocked, other.ReflectPercent, other.ReflectFlat);
            const i32 back = StatusTakenPercent(user, catalog);
            if (back != 0)
                reflected = ScalePercent(reflected, 100 + back);
            user.Hp -= reflected;
            if (user.Hp < 0)
                user.Hp = 0;
        }
        if (blocked > 0 && other.BulwarkFocus > 0)
            GrantFocus(other, other.BulwarkFocus);
        other.GuardPercent = 0;
        other.ReflectPercent = 0;
        other.ReflectFlat = 0;
        const i32 takenPct = StatusTakenPercent(other, catalog);
        if (takenPct != 0)
            taken = ScalePercent(taken, 100 + takenPct);
        other.Hp -= taken;
        if (other.Hp < 0)
            other.Hp = 0;
        result.Damage = taken;
        result.Text = user.Name + " uses " + skill.Name + " for " + std::to_string(taken) + ".";
        if (reflected > 0)
            result.Text += " " + std::to_string(reflected) + " returns.";
        if (!skill.StunWindup.empty() && !other.WindupSkill.empty()) {
            const StatusDef* stun =
                m_Data != nullptr ? m_Data->FindStatus(skill.StunWindup) : nullptr;
            if (stun != nullptr) {
                ApplyStatus(other, *stun);
                result.Text += " " + other.Name + " is stunned.";
            }
            other.WindupSkill.clear();
            other.WindupLeft = 0;
            other.WindupTotal = 0;
        }
        if (!skill.Status.empty()) {
            const i32 uses = NoteSkillUse(user, skill.Id);
            const bool due = skill.StatusEvery <= 1 || uses % skill.StatusEvery == 0;
            if (due && m_Data != nullptr) {
                const StatusDef* inflicted = m_Data->FindStatus(skill.Status);
                if (inflicted != nullptr) {
                    ApplyStatus(other, *inflicted);
                    result.Text += " " + inflicted->Name + ".";
                }
            }
        }
    } else if (skill.Effect == SkillEffect::Guard) {
        user.GuardPercent = skill.Power;
        user.ReflectPercent = 0;
        user.ReflectFlat = 0;
        result.Text = user.Name + " uses " + skill.Name + ". The next hit is reduced by " +
                      std::to_string(skill.Power) + " percent.";
    } else if (skill.Effect == SkillEffect::Heal) {
        i32 scaling = 0;
        if (!skill.Scaling.empty())
            scaling = user.Stats.Get(skill.Scaling);
        const i32 amount = HealAmount(power, scaling, m_Rules);
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
    } else if (skill.Effect == SkillEffect::Riposte) {
        user.GuardPercent = skill.Guard;
        user.ReflectPercent = skill.Reflect;
        i32 scaling = 0;
        if (skill.Scaling == "defense")
            scaling = DefenseOf(user);
        else if (!skill.Scaling.empty())
            scaling = user.Stats.Get(skill.Scaling);
        user.ReflectFlat =
            power + Scaled(scaling, m_Rules.AttackNumerator, m_Rules.AttackDenominator);
        result.Text = user.Name + " uses " + skill.Name + ". The next hit loses " +
                      std::to_string(skill.Guard) + " percent, and " +
                      std::to_string(skill.Reflect) + " percent of what it stops returns.";
    } else if (skill.Effect == SkillEffect::Harden) {
        user.BonusDefense = skill.Power;
        user.BonusDefenseTurns = skill.Duration;
        result.Text =
            user.Name + " uses " + skill.Name + ". Defense +" + std::to_string(skill.Power) + ".";
    } else {
        result.Text = user.Name + " uses " + skill.Name + ".";
    }

    EnterPhase(result.Text);
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

ActionResult Battle::ResolveShard()
{
    const EnemyDef* enemy = m_Data != nullptr ? m_Data->FindEnemy(m_EnemyId) : nullptr;
    const i32 raw = enemy != nullptr && enemy->ShardDamage > 0 ? enemy->ShardDamage : 0;
    const std::span<const StatusDef> catalog =
        m_Data != nullptr ? m_Data->GetStatuses() : std::span<const StatusDef>{};
    ActionResult result;
    result.Accepted = true;
    result.SkillId = "rubble";
    i32 taken = GuardedDamage(raw, m_Player.GuardPercent);
    const i32 blocked = raw > taken ? raw - taken : 0;
    i32 reflected = 0;
    if (blocked > 0 && m_Player.ReflectPercent > 0) {
        reflected = ReflectedDamage(blocked, m_Player.ReflectPercent, m_Player.ReflectFlat);
        const i32 back = StatusTakenPercent(m_Enemy, catalog);
        if (back != 0)
            reflected = ScalePercent(reflected, 100 + back);
        m_Enemy.Hp -= reflected;
        if (m_Enemy.Hp < 0)
            m_Enemy.Hp = 0;
    }
    if (blocked > 0 && m_Player.BulwarkFocus > 0)
        GrantFocus(m_Player, m_Player.BulwarkFocus);
    m_Player.GuardPercent = 0;
    m_Player.ReflectPercent = 0;
    m_Player.ReflectFlat = 0;
    const i32 takenPct = StatusTakenPercent(m_Player, catalog);
    if (takenPct != 0)
        taken = ScalePercent(taken, 100 + takenPct);
    m_Player.Hp -= taken;
    if (m_Player.Hp < 0)
        m_Player.Hp = 0;
    result.Damage = taken;
    result.Text = "Rubble hits " + m_Player.Name + " for " + std::to_string(taken) + ".";
    if (reflected > 0)
        result.Text += " " + std::to_string(reflected) + " returns.";
    EnterPhase(result.Text);
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

const SkillDef* Battle::TakeEnemySkill()
{
    if (m_Data == nullptr)
        return nullptr;
    const EnemyDef* enemy = m_Data->FindEnemy(m_EnemyId);
    if (enemy != nullptr && !enemy->Pattern.empty()) {
        const i32 count = static_cast<i32>(enemy->Pattern.size());
        for (i32 step = 0; step < count; ++step) {
            const i32 index = (m_Enemy.PatternStep + step) % count;
            const SkillDef* skill = m_Data->FindSkill(enemy->Pattern[static_cast<usize>(index)]);
            if (skill == nullptr || skill->Kind != SkillKind::Active)
                continue;
            if (skill->FocusCost > m_Enemy.Focus)
                continue;
            m_Enemy.PatternStep = index + 1;
            return skill;
        }
        return nullptr;
    }

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

    bool known = false;
    for (const std::string& id : m_PlayerSkills) {
        if (id == skillId)
            known = true;
    }
    const SkillDef* skill = known && m_Data != nullptr ? m_Data->FindSkill(skillId) : nullptr;
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
    if (Actor() != CombatantSide::Enemy)
        return Reject("It is not the foe's turn.");
    const TurnOpen open = PrepareTurn(m_Enemy);
    if (!open.Continue)
        return open.Result;

    if (m_Enemy.ShardDue) {
        m_Enemy.ShardDue = false;
        ActionResult shard = ResolveShard();
        ArmShard(shard.Text);
        return shard;
    }

    if (!m_Enemy.WindupSkill.empty() && m_Data != nullptr) {
        const SkillDef* winding = m_Data->FindSkill(m_Enemy.WindupSkill);
        if (winding == nullptr) {
            m_Enemy.WindupSkill.clear();
            m_Enemy.WindupLeft = 0;
            m_Enemy.WindupTotal = 0;
        } else if (m_Enemy.WindupLeft > 0) {
            const i32 shown = m_Enemy.WindupTotal - m_Enemy.WindupLeft + 1;
            --m_Enemy.WindupLeft;
            ActionResult result;
            result.Accepted = true;
            result.SkillId = winding->Id;
            result.Text = m_Enemy.Name + " is winding up " + winding->Name + " " +
                          std::to_string(shown) + "/" + std::to_string(m_Enemy.WindupTotal) + ".";
            PrefixOpening(result);
            ArmShard(result.Text);
            result.Result = m_Result;
            AdvanceTurn();
            return result;
        } else {
            const SkillDef firing = *winding;
            m_Enemy.WindupSkill.clear();
            m_Enemy.WindupLeft = 0;
            m_Enemy.WindupTotal = 0;
            ActionResult fired = Resolve(m_Enemy, m_Player, firing);
            ArmShard(fired.Text);
            return fired;
        }
    }

    const SkillDef* skill = TakeEnemySkill();
    if (skill == nullptr) {
        ActionResult result;
        result.Accepted = true;
        result.Text = m_Enemy.Name + " has nothing to spend.";
        PrefixOpening(result);
        ArmShard(result.Text);
        result.Result = m_Result;
        AdvanceTurn();
        return result;
    }
    if (skill->Windup > 0) {
        m_Enemy.WindupSkill = skill->Id;
        m_Enemy.WindupTotal = skill->Windup;
        m_Enemy.WindupLeft = skill->Windup - 1;
        ActionResult result;
        result.Accepted = true;
        result.SkillId = skill->Id;
        result.Text = m_Enemy.Name + " is winding up " + skill->Name + " 1/" +
                      std::to_string(skill->Windup) + ".";
        PrefixOpening(result);
        ArmShard(result.Text);
        result.Result = m_Result;
        AdvanceTurn();
        return result;
    }
    ActionResult acted = Resolve(m_Enemy, m_Player, *skill);
    ArmShard(acted.Text);
    return acted;
}

} // namespace Ascension
