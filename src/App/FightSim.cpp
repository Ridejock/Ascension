#include "FightSim.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Ascension/Progress/Hero.h"
#include "Ascension/Progress/Inventory.h"
#include "Playtest.h"

namespace Ascension {

namespace {

struct BuildSpec {
    const char* Label;
    const char* ClassId;
    const char* Pick;
    const char* Practiced;
};

const BuildSpec kBuilds[] = {
    {"Fighter", "fighter", nullptr, nullptr},
    {"Berserker rend", "berserker", "rend", "cleave"},
    {"Berserker brace", "berserker", "brace", "cleave"},
    {"Guardian riposte", "guardian", "riposte", "shield_bash"},
    {"Guardian shield wall", "guardian", "shield_wall", "shield_bash"},
};

// The kit used to compare encounters. The Bear is met at level 7; this stays 6 so a
// row can be set next to the Barrow Sentry on the same build.
constexpr i32 kHarnessLevel = 6;
constexpr i32 kStrikeUses = 10;
constexpr i32 kPracticedUses = 3;
constexpr i32 kActionCap = 400;

// One finished fight. Ok is false when the hero could not be built.
struct SimRun {
    std::string Build;
    bool Fang = false;
    bool Ok = false;
    bool Victory = false;
    bool TimedOut = false;
    i32 Hp = 0;
    i32 Focus = 0;
    i32 StartHp = 0;
    i32 StartFocus = 0;
    i32 MaxHp = 0;
    i32 PlayerTurns = 0;
    std::string Error;
};

bool Holds(const CombatantState& who, std::string_view id)
{
    for (const StatusInstance& status : who.Statuses) {
        if (status.Id == id)
            return true;
    }
    return false;
}

i32 HpPercent(i32 hp, i32 maxHp)
{
    if (maxHp < 1)
        maxHp = 1;
    if (hp < 0)
        hp = 0;
    const i64 scaled = static_cast<i64>(hp) * 100 / maxHp;
    return static_cast<i32>(scaled);
}

i32 SpendableFocus(const CombatantState& player, i32 perTurn)
{
    i32 focus = player.Focus;
    if (perTurn > 0 && focus < player.MaxFocus) {
        focus += perTurn;
        if (focus > player.MaxFocus)
            focus = player.MaxFocus;
    }
    return focus;
}

const SkillDef* FindActive(const std::vector<HeroGrant>& grants, std::string_view id)
{
    for (const HeroGrant& grant : grants) {
        if (grant.Skill.Id == id && grant.Skill.Kind == SkillKind::Active)
            return &grant.Skill;
    }
    return nullptr;
}

bool CanCast(const std::vector<HeroGrant>& grants, std::string_view id, i32 focus)
{
    const SkillDef* skill = FindActive(grants, id);
    return skill != nullptr && skill->FocusCost <= focus;
}

// A foe this low dies to one strike from the kit this sim uses. Healing instead leaves
// it another turn.
bool FoeIsFinishing(const Battle& battle)
{
    for (const CombatantState& foe : battle.Enemies()) {
        if (foe.Alive() && foe.Hp <= 36)
            return true;
    }
    return false;
}

i32 ArmorOf(const CombatantState& who)
{
    const i64 defense = static_cast<i64>(who.Stats.Defense) + who.BonusDefense;
    if (defense < 0)
        return 0;
    if (defense > 999999)
        return 999999;
    return static_cast<i32>(defense);
}

// The same missing-health bonus Battle adds when a blow scales. 0 at full health.
i32 MissingStat(const std::vector<HeroGrant>& grants, const CombatantState& who,
                std::string_view statId)
{
    i32 bonus = 0;
    for (const HeroGrant& grant : grants) {
        for (const EffectDef& effect : grant.Skill.Effects) {
            if (effect.Trigger != EffectTrigger::Always || effect.Op != EffectOp::StatPerMissingHp)
                continue;
            if (!effect.Stat.has_value() || *effect.Stat != statId)
                continue;
            bonus += MissingHpBonus(effect.Amount.value_or(0), effect.Per.value_or(0),
                                    effect.Max.value_or(0), who.Hp, who.MaxHp);
        }
    }
    return bonus;
}

i32 ScalingOf(const std::vector<HeroGrant>& grants, const CombatantState& who,
              const EffectDef& effect)
{
    if (!effect.Scaling.has_value())
        return 0;
    i32 stat = *effect.Scaling == "defense" ? ArmorOf(who) : who.Stats.Get(*effect.Scaling);
    stat += MissingStat(grants, who, *effect.Scaling);
    return stat;
}

i32 RankedBase(const Hero& hero, const Database& data, const SkillDef& skill, i32 base)
{
    i32 rank = hero.RankOf(skill.Id);
    if (rank < 1)
        rank = 1;
    i64 power = static_cast<i64>(base);
    power += static_cast<i64>(rank - 1) * data.GetProgress().PowerPerRank;
    if (power < 0)
        power = 0;
    if (power > 9999)
        power = 9999;
    return static_cast<i32>(power);
}

bool SkillDealsDamage(const SkillDef& skill)
{
    for (const EffectDef& effect : skill.Effects) {
        if (effect.Trigger == EffectTrigger::OnUse && effect.Op == EffectOp::Damage)
            return true;
    }
    return false;
}

bool AppliesStatus(const SkillDef& skill, std::string_view id)
{
    for (const EffectDef& effect : skill.Effects) {
        if (effect.Op == EffectOp::ApplyStatus && effect.Status.has_value() && *effect.Status == id)
            return true;
    }
    return false;
}

// Dealt percent from statuses this skill applies that are not already up. A second
// effect that names the same status does not add the percent again.
i32 UnheldDealtPercent(const Database& data, const CombatantState& who, const SkillDef& skill)
{
    i32 percent = 0;
    std::vector<std::string> seen;
    for (const EffectDef& effect : skill.Effects) {
        if (effect.Trigger != EffectTrigger::OnUse || effect.Op != EffectOp::ApplyStatus)
            continue;
        if (!effect.Status.has_value() || Holds(who, *effect.Status))
            continue;
        bool repeat = false;
        for (const std::string& id : seen) {
            if (id == *effect.Status)
                repeat = true;
        }
        if (repeat)
            continue;
        seen.emplace_back(*effect.Status);
        const StatusDef* status = data.FindStatus(*effect.Status);
        if (status != nullptr && status->DamageDealtPercent > 0)
            percent += status->DamageDealtPercent;
    }
    return percent;
}

i32 AppliedTakenPercent(const Database& data, const SkillDef& skill, const CombatantState& foe)
{
    i32 percent = 0;
    std::vector<std::string> seen;
    for (const EffectDef& effect : skill.Effects) {
        if (effect.Op != EffectOp::ApplyStatus || !effect.Status.has_value())
            continue;
        if (Holds(foe, *effect.Status))
            continue;
        bool repeat = false;
        for (const std::string& id : seen) {
            if (id == *effect.Status)
                repeat = true;
        }
        if (repeat)
            continue;
        seen.emplace_back(*effect.Status);
        const StatusDef* status = data.FindStatus(*effect.Status);
        if (status != nullptr)
            percent += status->DamageTakenPercent;
    }
    return percent;
}

// One use of `attack` against `foe`. Steady counts a conditional blow once this skill
// has applied the status it asks for, which is the second rend or the second bash.
i32 BlowDamage(const Database& data, const Hero& hero, const std::vector<HeroGrant>& grants,
               const CombatantState& player, const SkillDef& attack, const CombatantState& foe,
               i32 dealtPercent, bool steady, i32 extraTaken = 0, std::string_view asIf = {})
{
    i32 taken = StatusTakenPercent(foe, data.GetStatuses()) + extraTaken;
    if (steady)
        taken += AppliedTakenPercent(data, attack, foe);
    const CombatRules& rules = data.GetRules();
    i32 total = 0;
    for (const EffectDef& effect : attack.Effects) {
        if (effect.Trigger != EffectTrigger::OnUse || effect.Op != EffectOp::Damage)
            continue;
        if (effect.IfTargetHas.has_value()) {
            const bool held =
                Holds(foe, *effect.IfTargetHas) || (!asIf.empty() && *effect.IfTargetHas == asIf);
            const bool coming = steady && AppliesStatus(attack, *effect.IfTargetHas);
            if (!held && !coming)
                continue;
        }
        StrikeQuery hit;
        hit.Power = RankedBase(hero, data, attack, effect.Power.value_or(0));
        hit.ScalingStat = ScalingOf(grants, player, effect);
        hit.Defense = ArmorOf(foe);
        i32 raw = StrikeDamage(hit, rules);
        if (dealtPercent != 0)
            raw = ScalePercent(raw, 100 + dealtPercent);
        raw = GuardedDamage(raw, foe.GuardPercent);
        if (taken != 0)
            raw = ScalePercent(raw, 100 + taken);
        total += raw;
    }
    return total;
}

// Attacks to finish one foe: the first blow, then the steady blow after its setup lands.
i32 HitsToFell(i32 hp, i32 opening, i32 steady)
{
    if (hp <= 0)
        return 0;
    if (opening <= 0)
        opening = steady;
    if (steady <= 0)
        steady = opening;
    if (opening <= 0)
        return 1000000;
    if (opening >= hp)
        return 1;
    return 1 + (hp - opening + steady - 1) / steady;
}

const SkillDef* SustainedAttack(const std::vector<HeroGrant>& grants, i32 focus)
{
    if (CanCast(grants, "rend", focus))
        return FindActive(grants, "rend");
    if (CanCast(grants, "shield_bash", focus))
        return FindActive(grants, "shield_bash");
    if (CanCast(grants, "strike", focus))
        return FindActive(grants, "strike");
    const SkillDef* best = nullptr;
    i32 bestPower = -1;
    for (const HeroGrant& grant : grants) {
        const SkillDef& skill = grant.Skill;
        if (skill.Kind != SkillKind::Active || skill.Target != SkillTarget::Enemy ||
            skill.FocusCost > focus || !SkillDealsDamage(skill))
            continue;
        i32 power = 0;
        for (const EffectDef& effect : skill.Effects) {
            if (effect.Trigger == EffectTrigger::OnUse && effect.Op == EffectOp::Damage)
                power += effect.Power.value_or(0);
        }
        if (power > bestPower) {
            best = &skill;
            bestPower = power;
        }
    }
    return best;
}

i32 AttacksToClear(const Battle& battle, const Database& data, const Hero& hero,
                   const std::vector<HeroGrant>& grants, const SkillDef& attack, i32 dealtPercent,
                   i32 extraTaken = 0)
{
    const CombatantState& player = battle.Player();
    i32 hits = 0;
    for (const CombatantState& foe : battle.Enemies()) {
        if (!foe.Alive())
            continue;
        const i32 opening =
            BlowDamage(data, hero, grants, player, attack, foe, dealtPercent, false, extraTaken);
        const i32 steady =
            BlowDamage(data, hero, grants, player, attack, foe, dealtPercent, true, extraTaken);
        hits += HitsToFell(foe.Hp, opening, steady);
    }
    return hits;
}

// Rage is spent while health is high, on its own rule. That status percent is already
// on the attacks either way, so a gear buff has to pay on top of it.
i32 CommittedDealt(const Battle& battle, const Database& data, const std::vector<HeroGrant>& grants,
                   i32 focus)
{
    const CombatantState& player = battle.Player();
    if (HpPercent(player.Hp, player.MaxHp) <= 50)
        return 0;
    if (!CanCast(grants, "rage", focus))
        return 0;
    const SkillDef* rage = FindActive(grants, "rage");
    if (rage == nullptr)
        return 0;
    return UnheldDealtPercent(data, player, *rage);
}

// True when spending this turn on the buff, then attacking, is strictly fewer attacks
// than attacking now. The percent is the status's damageDealtPercent, not a fixed 25.
bool DealtBuffPays(const Battle& battle, const Database& data, const Hero& hero,
                   const std::vector<HeroGrant>& grants, i32 focus, i32 extraDealt)
{
    const SkillDef* attack = SustainedAttack(grants, focus);
    if (attack == nullptr || extraDealt <= 0)
        return false;
    const i32 dealt = StatusDealtPercent(battle.Player(), data.GetStatuses()) +
                      CommittedDealt(battle, data, grants, focus);
    const i32 plain = AttacksToClear(battle, data, hero, grants, *attack, dealt);
    const i32 buffed = AttacksToClear(battle, data, hero, grants, *attack, dealt + extraDealt);
    return buffed + 1 < plain;
}

// A gear self-buff that only raises damage dealt. Rage is left to the rule below so a
// class button still goes out while health is high. Anything else is spent only when
// its status percent pays the turn back.
std::string_view OpeningBuff(const Battle& battle, const std::vector<HeroGrant>& grants,
                             const Database& data, const Hero& hero, i32 focus)
{
    const CombatantState& player = battle.Player();
    for (const HeroGrant& grant : grants) {
        const SkillDef& skill = grant.Skill;
        if (skill.Id == "rage" || skill.Kind != SkillKind::Active ||
            skill.Target != SkillTarget::Self)
            continue;
        if (skill.FocusCost > focus || SkillDealsDamage(skill))
            continue;
        const i32 extra = UnheldDealtPercent(data, player, skill);
        if (extra <= 0)
            continue;
        if (DealtBuffPays(battle, data, hero, grants, focus, extra))
            return skill.Id;
    }
    return {};
}

bool HitsOneFoe(const SkillDef& skill)
{
    if (skill.Kind != SkillKind::Active || skill.Target != SkillTarget::Enemy ||
        !SkillDealsDamage(skill))
        return false;
    for (const EffectDef& effect : skill.Effects) {
        if (effect.Op != EffectOp::Damage || !effect.Aim.has_value())
            continue;
        if (*effect.Aim == EffectAim::AllEnemies || *effect.Aim == EffectAim::RandomEnemy)
            return false;
    }
    return true;
}

bool StunsAWindup(const SkillDef& skill)
{
    for (const EffectDef& effect : skill.Effects) {
        if (effect.Op == EffectOp::StunIfWindingUp)
            return true;
    }
    return false;
}

bool HasHeal(const SkillDef& skill)
{
    for (const EffectDef& effect : skill.Effects) {
        if (effect.Trigger == EffectTrigger::OnUse && effect.Op == EffectOp::Heal)
            return true;
    }
    return false;
}

bool HasGuard(const SkillDef& skill)
{
    for (const EffectDef& effect : skill.Effects) {
        if (effect.Op == EffectOp::Guard)
            return true;
    }
    return false;
}

const CombatantState* MarkedLiving(const Battle& battle)
{
    const auto foes = battle.Enemies();
    const i32 index = battle.TargetIndex();
    if (index >= 0 && index < static_cast<i32>(foes.size()) &&
        foes[static_cast<usize>(index)].Alive())
        return &foes[static_cast<usize>(index)];
    for (const CombatantState& foe : foes) {
        if (foe.Alive())
            return &foe;
    }
    return nullptr;
}

const SkillDef* BestHit(const Battle& battle, const Database& data, const Hero& hero,
                        const std::vector<HeroGrant>& grants, i32 focus, i32 dealtPercent)
{
    const CombatantState* foe = MarkedLiving(battle);
    if (foe == nullptr)
        return nullptr;
    const CombatantState& player = battle.Player();
    const SkillDef* best = nullptr;
    i32 bestDamage = -1;
    for (const HeroGrant& grant : grants) {
        const SkillDef& skill = grant.Skill;
        if (!HitsOneFoe(skill) || skill.FocusCost > focus)
            continue;
        const i32 damage = BlowDamage(data, hero, grants, player, skill, *foe, dealtPercent, false);
        if (damage > bestDamage) {
            best = &skill;
            bestDamage = damage;
        }
    }
    return best;
}

// Used when the build has none of the fighter buttons. A spell and a blade share this
// path: the highest single-target hit, a status that makes that hit land harder, and a
// heal or a guard when health is low. All-enemy swings stay unused, same as Cleave.
std::string_view FallbackSkill(const Battle& battle, const std::vector<HeroGrant>& grants,
                               const Database& data, const Hero& hero, i32 focus)
{
    const CombatantState& player = battle.Player();
    const i32 percent = HpPercent(player.Hp, player.MaxHp);
    const bool finishing = FoeIsFinishing(battle);
    bool winding = false;
    for (const CombatantState& foe : battle.Enemies()) {
        if (foe.Alive() && !foe.WindupSkill.empty())
            winding = true;
    }
    if (winding) {
        for (const HeroGrant& grant : grants) {
            const SkillDef& skill = grant.Skill;
            if (skill.FocusCost <= focus && skill.Target == SkillTarget::Enemy &&
                StunsAWindup(skill))
                return skill.Id;
        }
    }
    if (!finishing && percent <= 32) {
        for (const HeroGrant& grant : grants) {
            const SkillDef& skill = grant.Skill;
            if (skill.FocusCost <= focus && skill.Target == SkillTarget::Self && HasHeal(skill))
                return skill.Id;
        }
    }
    if (!finishing && percent <= 32 && player.GuardPercent <= 0) {
        for (const HeroGrant& grant : grants) {
            const SkillDef& skill = grant.Skill;
            if (skill.FocusCost <= focus && skill.Target == SkillTarget::Self && HasGuard(skill))
                return skill.Id;
        }
    }
    const i32 dealt = StatusDealtPercent(player, data.GetStatuses()) +
                      CommittedDealt(battle, data, grants, focus);
    const CombatantState* foe = MarkedLiving(battle);
    const SkillDef* attack = BestHit(battle, data, hero, grants, focus, dealt);
    if (attack == nullptr || foe == nullptr)
        return {};
    const CombatantState& playerNow = battle.Player();
    for (const EffectDef& effect : attack->Effects) {
        if (!effect.IfTargetHas.has_value() || Holds(*foe, *effect.IfTargetHas))
            continue;
        const SkillDef* setup = nullptr;
        for (const HeroGrant& grant : grants) {
            const SkillDef& skill = grant.Skill;
            if (skill.Id == attack->Id || skill.Kind != SkillKind::Active ||
                skill.FocusCost > focus || skill.Target == SkillTarget::AllEnemies)
                continue;
            if (AppliesStatus(skill, *effect.IfTargetHas))
                setup = &skill;
        }
        if (setup == nullptr)
            continue;
        const i32 plain = BlowDamage(data, hero, grants, playerNow, *attack, *foe, dealt, false);
        const i32 bonus = BlowDamage(data, hero, grants, playerNow, *attack, *foe, dealt, false, 0,
                                     *effect.IfTargetHas);
        if (bonus > plain)
            return setup->Id;
    }
    const i32 plainHits = AttacksToClear(battle, data, hero, grants, *attack, dealt);
    for (const HeroGrant& grant : grants) {
        const SkillDef& skill = grant.Skill;
        if (skill.Id == attack->Id || skill.FocusCost > focus ||
            skill.Target == SkillTarget::AllEnemies)
            continue;
        const i32 extra = AppliedTakenPercent(data, skill, *foe);
        if (extra <= 0)
            continue;
        const i32 buffed = AttacksToClear(battle, data, hero, grants, *attack, dealt, extra);
        if (buffed + 1 < plainHits)
            return skill.Id;
    }
    return attack->Id;
}

// Commit to the marked foe. A damage buff is spent only when it shortens the kill.
// Heal or guard only when health is low, and otherwise spend the best single-target
// attack that focus can pay. Cleave is left unused so the kill order is the thing
// being measured. A build with none of those buttons uses FallbackSkill.
std::string_view ChoosePlayerSkill(const Battle& battle, const std::vector<HeroGrant>& grants,
                                   const Database& data, const Hero& hero, i32 perTurn)
{
    const CombatantState& player = battle.Player();
    const i32 focus = SpendableFocus(player, perTurn);
    const i32 percent = HpPercent(player.Hp, player.MaxHp);
    const std::string_view buff = OpeningBuff(battle, grants, data, hero, focus);
    if (!buff.empty())
        return buff;
    if (CanCast(grants, "rage", focus) && !Holds(player, "fury") && percent > 50)
        return "rage";
    const bool finishing = FoeIsFinishing(battle);
    if (!finishing && percent <= 32 && CanCast(grants, "second_wind", focus))
        return "second_wind";
    if (!finishing && percent <= 32 && player.GuardPercent <= 0 &&
        CanCast(grants, "shield_wall", focus))
        return "shield_wall";
    if (!finishing && percent <= 32 && player.GuardPercent <= 0 && CanCast(grants, "brace", focus))
        return "brace";
    if (!finishing && percent <= 32 && player.GuardPercent <= 0 &&
        CanCast(grants, "riposte", focus))
        return "riposte";
    bool winding = false;
    for (const CombatantState& foe : battle.Enemies()) {
        if (foe.Alive() && !foe.WindupSkill.empty())
            winding = true;
    }
    if (winding && CanCast(grants, "heavy_blow", focus))
        return "heavy_blow";
    if (CanCast(grants, "rend", focus))
        return "rend";
    if (CanCast(grants, "shield_bash", focus))
        return "shield_bash";
    if (CanCast(grants, "strike", focus))
        return "strike";
    return FallbackSkill(battle, grants, data, hero, focus);
}

i32 FocusSeat(const Battle& battle, std::string_view first, std::string_view second)
{
    const auto foes = battle.Enemies();
    i32 fallback = -1;
    i32 secondSeat = -1;
    for (i32 index = 0; index < static_cast<i32>(foes.size()); ++index) {
        if (!foes[static_cast<usize>(index)].Alive())
            continue;
        if (fallback < 0)
            fallback = index;
        if (foes[static_cast<usize>(index)].Id == first)
            return index;
        if (foes[static_cast<usize>(index)].Id == second)
            secondSeat = index;
    }
    if (secondSeat >= 0)
        return secondSeat;
    return fallback;
}

bool StunsNamed(const std::vector<HeroGrant>& grants, std::string_view id)
{
    const SkillDef* skill = FindActive(grants, id);
    return skill != nullptr && StunsAWindup(*skill);
}

i32 WindingSeat(const Battle& battle)
{
    const auto foes = battle.Enemies();
    for (i32 index = 0; index < static_cast<i32>(foes.size()); ++index) {
        if (foes[static_cast<usize>(index)].Alive() &&
            !foes[static_cast<usize>(index)].WindupSkill.empty())
            return index;
    }
    return -1;
}

void PushRanks(Battle& battle, const Hero& hero, const Database& data, std::string_view pending)
{
    std::vector<SkillRank> ranks;
    const auto ids = battle.PlayerSkillIds();
    for (const std::string& id : ids) {
        SkillRank row;
        row.Id = id;
        if (!pending.empty() && id == pending)
            row.Rank = hero.RankAfterUse(id);
        else
            row.Rank = hero.RankOf(id);
        ranks.push_back(std::move(row));
    }
    battle.SetPlayerRanks(data.GetProgress().PowerPerRank, std::move(ranks));
}

// Kill order for one rate row. Random marks a uniform living foe every player turn.
// First is the enemy id killed first. Then is the id marked once First is dead.
struct FocusOrder {
    std::string Label;
    std::string First;
    std::string Then;
    bool Random = false;
};

struct RunSpec {
    const BuildSpec* Build = nullptr;
    bool Fang = false;
    std::string_view EncounterId;
    std::string_view FocusId;
    std::string_view ThenId;
    bool RandomFocus = false;
    i32 Slip = 0;
    u32 RngState = 1;
    // False starts full, which is every single-fight row. True uses StartHp and
    // StartFocus, clamped by the fight.
    bool UsePools = false;
    i32 StartHp = 0;
    i32 StartFocus = 0;
    // Worn on top of the sword, the shield, and the fang flag. Null wears nothing
    // extra. The route passes drops claimed on earlier wins of that same run.
    const std::vector<std::string>* Gear = nullptr;
};

// xorshift32. State 0 is stuck, so a zero result is replaced.
u32 NextRandom(u32& state)
{
    u32 value = state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    if (value == 0)
        value = 1;
    state = value;
    return value;
}

u32 RunState(i32 seed, i32 index)
{
    u32 state = seed > 0 ? static_cast<u32>(seed) : 1u;
    state += static_cast<u32>(index) * 0x9E3779B9u;
    if (state == 0)
        state = 1;
    return state;
}

i32 LivingCount(const Battle& battle)
{
    i32 count = 0;
    for (const CombatantState& foe : battle.Enemies()) {
        if (foe.Alive())
            ++count;
    }
    return count;
}

i32 RandomLivingSeat(const Battle& battle, u32& state)
{
    const i32 living = LivingCount(battle);
    if (living < 1)
        return -1;
    i32 pick = static_cast<i32>(NextRandom(state) % static_cast<u32>(living));
    const auto foes = battle.Enemies();
    for (i32 index = 0; index < static_cast<i32>(foes.size()); ++index) {
        if (!foes[static_cast<usize>(index)].Alive())
            continue;
        if (pick == 0)
            return index;
        --pick;
    }
    return -1;
}

// Slip is a percent. A roll under it marks at random even when a focus was named.
// Default targeting does not roll slip: every turn is already a fresh mark.
i32 ChooseSeat(const Battle& battle, const RunSpec& spec, u32& state)
{
    if (spec.RandomFocus)
        return RandomLivingSeat(battle, state);
    if (spec.Slip > 0 && static_cast<i32>(NextRandom(state) % 100u) < spec.Slip)
        return RandomLivingSeat(battle, state);
    return FocusSeat(battle, spec.FocusId, spec.ThenId);
}

bool PrepareHero(const Database& data, const BuildSpec& build, bool fang,
                 std::string_view encounterId, const std::vector<std::string>* specGear, Hero& hero,
                 Inventory& inventory, std::string& error)
{
    PlaytestOptions options;
    options.Level = kHarnessLevel;
    options.ClassId = build.ClassId;
    options.Fight.Id = std::string(encounterId);
    options.Equip.emplace_back("rusty_sword");
    options.Equip.emplace_back("oak_shield");
    if (fang)
        options.Equip.emplace_back("alphas_fang");
    if (specGear != nullptr) {
        for (const std::string& id : *specGear)
            options.Equip.emplace_back(id);
    }
    if (build.Pick != nullptr)
        options.PickIds.emplace_back(build.Pick);
    PlaytestSetup setup;
    if (!ApplyPlaytest(hero, inventory, data, options, setup, error))
        return false;
    if (hero.ClassId() != build.ClassId) {
        error = "class '" + std::string(build.ClassId) + "' was not applied";
        return false;
    }
    if (build.Pick != nullptr &&
        PickSkillOption(hero, build.Pick, error) == SkillOptionPick::Failed)
        return false;
    for (i32 use = 0; use < kStrikeUses; ++use)
        hero.NoteUse("strike");
    if (build.Practiced != nullptr) {
        for (i32 use = 0; use < kPracticedUses; ++use)
            hero.NoteUse(build.Practiced);
    }
    error.clear();
    return true;
}

SimRun SimulateBuild(const Database& data, const RunSpec& spec)
{
    SimRun row;
    row.Build = spec.Build->Label;
    row.Fang = spec.Fang;

    Hero hero;
    Inventory inventory;
    if (!PrepareHero(data, *spec.Build, spec.Fang, spec.EncounterId, spec.Gear, hero, inventory,
                     row.Error))
        return row;

    const ClassDef* klass = data.FindClass(hero.ClassId());
    if (klass == nullptr) {
        row.Error = "class '" + hero.ClassId() + "' does not exist";
        return row;
    }
    const std::vector<HeroGrant> grants =
        data.HeroGrants(*klass, inventory.WornIds(), hero.PickedIds());
    const i32 perTurn = data.GetRules().FocusPerTurn;

    Battle battle(data, std::make_unique<AgilityRoundModel>());
    battle.SetBaseStats(hero.Attributes());
    battle.SetGear(inventory.GearBonus());
    battle.SetWornItems(inventory.WornIds());
    battle.SetPickedSkills(hero.PickedIds());
    PushRanks(battle, hero, data, {});
    std::string why;
    if (spec.UsePools) {
        OpeningPools opening;
        opening.Hp = spec.StartHp;
        opening.Focus = spec.StartFocus;
        if (!battle.Start(hero.ClassId(), hero.Level(), spec.EncounterId, opening, why)) {
            row.Error = why;
            return row;
        }
    } else if (!battle.Start(hero.ClassId(), hero.Level(), spec.EncounterId, why)) {
        row.Error = why;
        return row;
    }
    row.StartHp = battle.Player().Hp;
    row.StartFocus = battle.Player().Focus;
    row.MaxHp = battle.Player().MaxHp;

    u32 state = spec.RngState == 0 ? 1u : spec.RngState;
    i32 steps = 0;
    while (battle.Result() == BattleResult::Ongoing && steps < kActionCap) {
        ++steps;
        if (battle.Actor() == CombatantSide::Enemy) {
            battle.EnemyAct();
            continue;
        }
        ++row.PlayerTurns;
        std::string_view skill = ChoosePlayerSkill(battle, grants, data, hero, perTurn);
        i32 seat = ChooseSeat(battle, spec, state);
        if (skill == "heavy_blow" || StunsNamed(grants, skill)) {
            const i32 winding = WindingSeat(battle);
            if (winding >= 0)
                seat = winding;
        }
        if (seat >= 0)
            battle.SetTarget(seat);
        PushRanks(battle, hero, data, skill);
        ActionResult used;
        if (skill.empty())
            used = battle.Wait();
        else
            used = battle.UseSkill(skill);
        if (!used.Accepted)
            used = battle.UseSkill("strike");
        if (used.Accepted && !used.SkillId.empty()) {
            hero.NoteUse(used.SkillId);
            PushRanks(battle, hero, data, {});
        }
    }

    row.Ok = true;
    row.Victory = battle.Result() == BattleResult::Victory;
    row.TimedOut = battle.Result() == BattleResult::Ongoing;
    row.Hp = battle.Player().Hp;
    row.Focus = battle.Player().Focus;
    return row;
}

bool SeenId(const std::vector<std::string>& ids, std::string_view id)
{
    for (const std::string& have : ids) {
        if (have == id)
            return true;
    }
    return false;
}

void AddOrder(std::vector<FocusOrder>& orders, std::string label, std::string first,
              std::string thenId, bool random)
{
    FocusOrder order;
    order.Label = std::move(label);
    order.First = std::move(first);
    order.Then = std::move(thenId);
    order.Random = random;
    orders.push_back(std::move(order));
}

// Known packs use the kill orders the rate table reports. Anything else walks the
// member list so a new encounter still has a row per foe. One foe is one row.
std::vector<FocusOrder> OrdersFor(const EncounterDef& encounter)
{
    std::vector<FocusOrder> orders;
    if (encounter.Id == "wolf_pack") {
        AddOrder(orders, "dire wolf", "dire_wolf", "wolf", false);
        AddOrder(orders, "wolf", "wolf", "dire_wolf", false);
        AddOrder(orders, "default", "", "", true);
        return orders;
    }
    if (encounter.Enemies.size() <= 1) {
        const std::string id = encounter.Enemies.empty() ? std::string() : encounter.Enemies[0];
        const std::string label = encounter.Id == "barrow_sentry" ? "sentry" : id;
        AddOrder(orders, label, id, "", false);
        return orders;
    }

    std::vector<std::string> ids;
    for (const std::string& enemy : encounter.Enemies) {
        if (!SeenId(ids, enemy))
            ids.push_back(enemy);
    }
    for (usize index = 0; index < ids.size(); ++index) {
        const usize next = (index + 1) % ids.size();
        AddOrder(orders, ids[index], ids[index], ids[next], false);
    }
    AddOrder(orders, "default", "", "", true);
    return orders;
}

// One living foe cannot be marked differently, and a named focus with no slip never
// reads the seed. Repeating those fights would copy one outcome.
bool OrderVaries(const FocusOrder& order, i32 slip, i32 enemyCount)
{
    if (enemyCount < 2)
        return false;
    if (order.Random)
        return true;
    return slip > 0;
}

i32 Percentile10(std::vector<i32>& samples)
{
    if (samples.empty())
        return 0;
    std::sort(samples.begin(), samples.end());
    const usize last = samples.size() - 1;
    return samples[last * 10 / 100];
}

SimRateRow SimulateRate(const Database& data, const EncounterDef& encounter, const BuildSpec& build,
                        const FocusOrder& order, bool fang, const SimRateQuery& query)
{
    SimRateRow rate;
    rate.Encounter = encounter.Name;
    rate.Build = build.Label;
    rate.Focus = order.Label;
    rate.Fang = fang;

    const i32 enemyCount = static_cast<i32>(encounter.Enemies.size());
    const i32 runs = OrderVaries(order, query.Slip, enemyCount) ? query.Runs : 1;
    std::vector<i32> samples;
    samples.reserve(static_cast<usize>(runs));
    for (i32 index = 0; index < runs; ++index) {
        RunSpec spec;
        spec.Build = &build;
        spec.Fang = fang;
        spec.EncounterId = encounter.Id;
        spec.FocusId = order.First;
        spec.ThenId = order.Then;
        spec.RandomFocus = order.Random;
        spec.Slip = query.Slip;
        spec.RngState = RunState(query.Seed, index);
        const SimRun row = SimulateBuild(data, spec);
        if (!row.Ok) {
            rate.Error = row.Error;
            return rate;
        }
        rate.Runs += 1;
        if (row.TimedOut)
            rate.Timeouts += 1;
        const bool won = row.Victory && !row.TimedOut;
        if (won)
            rate.Wins += 1;
        const i32 left = won ? row.Hp : 0;
        rate.SumHp += left;
        rate.SumTurns += row.PlayerTurns;
        samples.push_back(left);
    }
    rate.P10Hp = Percentile10(samples);
    rate.Ok = true;
    return rate;
}

std::string Pad(std::string_view text, i32 width)
{
    std::string out(text);
    while (static_cast<i32>(out.size()) < width)
        out.push_back(' ');
    return out;
}

bool EquipHas(const PlaytestOptions& options, std::string_view id)
{
    for (const std::string& equipped : options.Equip) {
        if (equipped == id)
            return true;
    }
    return false;
}

std::string OneDecimal(i64 tenths)
{
    if (tenths < 0)
        tenths = 0;
    std::string text = std::to_string(tenths / 10);
    text.push_back('.');
    text.push_back(static_cast<char>('0' + static_cast<int>(tenths % 10)));
    return text;
}

struct Reach {
    std::string Id;
    std::string From;
    i32 Dist = 0;
};

const Reach* FindReach(const std::vector<Reach>& reached, std::string_view id)
{
    for (const Reach& row : reached) {
        if (row.Id == id)
            return &row;
    }
    return nullptr;
}

const MapDef* RouteHub(const Database& data)
{
    for (const MapDef& map : data.GetMaps()) {
        if (map.Hub)
            return &map;
    }
    return nullptr;
}

bool RouteTileOpen(const Database& data, const MapDef& map, i32 x, i32 y)
{
    const std::string* cell = map.Cell(x, y);
    if (cell == nullptr)
        return false;
    const TileDef* tile = data.FindTile(*cell);
    return tile != nullptr && !tile->Solid && !MapBlocks(map, x, y);
}

void ReachFromHub(const Database& data, const std::string& hubId, std::vector<Reach>& out)
{
    Reach start;
    start.Id = hubId;
    out.push_back(start);
    std::vector<std::string> queue;
    queue.push_back(hubId);
    usize cursor = 0;
    while (cursor < queue.size()) {
        const std::string id = queue[cursor];
        ++cursor;
        const Reach* here = FindReach(out, id);
        const MapDef* map = data.FindMap(id);
        if (here == nullptr || map == nullptr)
            continue;
        const i32 dist = here->Dist;
        for (const MapLink& link : map->Links) {
            if (FindReach(out, link.Map) != nullptr || data.FindMap(link.Map) == nullptr)
                continue;
            Reach next;
            next.Id = link.Map;
            next.From = id;
            next.Dist = dist + 1;
            out.push_back(std::move(next));
            queue.push_back(link.Map);
        }
    }
}

struct BossSeat {
    const MapDef* Map = nullptr;
    const MapSpawn* Spawn = nullptr;
    i32 Dist = -1;
};

// A noFlee spawn is the boss room. With none, the spawn that lists the roster
// boss is the stand-in. The further room wins, so a nearer boss flag does not
// cut the walk short.
BossSeat FindBossRoom(const Database& data, const std::vector<Reach>& reached)
{
    BossSeat marked;
    BossSeat roster;
    for (const MapDef& map : data.GetMaps()) {
        const Reach* hop = FindReach(reached, map.Id);
        if (hop == nullptr)
            continue;
        for (const MapSpawn& spawn : map.Spawns) {
            if (spawn.NoFlee && hop->Dist >= marked.Dist) {
                marked.Map = &map;
                marked.Spawn = &spawn;
                marked.Dist = hop->Dist;
            }
            const EncounterDef* encounter = data.FindEncounter(spawn.Encounter);
            if (encounter == nullptr)
                continue;
            for (const std::string& enemyId : encounter->Enemies) {
                const EnemyDef* enemy = data.FindEnemy(enemyId);
                if (enemy != nullptr && enemy->Boss && hop->Dist >= roster.Dist) {
                    roster.Map = &map;
                    roster.Spawn = &spawn;
                    roster.Dist = hop->Dist;
                }
            }
        }
    }
    if (marked.Spawn != nullptr)
        return marked;
    return roster;
}

bool ChainToBoss(const std::vector<Reach>& reached, const std::string& hubId,
                 const std::string& bossId, std::vector<std::string>& chain, std::string& error)
{
    std::string at = bossId;
    for (i32 guard = 0; guard < 64 && !at.empty(); ++guard) {
        chain.push_back(at);
        if (at == hubId)
            break;
        const Reach* hop = FindReach(reached, at);
        if (hop == nullptr || hop->From.empty()) {
            error = "the boss room is not linked from the hub";
            return false;
        }
        at = hop->From;
    }
    if (chain.empty() || chain.back() != hubId) {
        error = "the boss room is not linked from the hub";
        return false;
    }
    std::reverse(chain.begin(), chain.end());
    return true;
}

std::string NextOnRoute(const std::vector<std::string>& chain, std::string_view id)
{
    for (usize index = 0; index + 1 < chain.size(); ++index) {
        if (chain[index] == id)
            return chain[index + 1];
    }
    return {};
}

struct RouteWalk {
    const Database* Data = nullptr;
    const std::vector<std::string>* Main = nullptr;
    std::vector<std::string> Visited;
    std::vector<RouteStep> Steps;
    std::string Error;

    bool Walk(const std::string& mapId, MapTile entrance, const std::string& cameFrom, bool side);
};

bool RouteWalk::Walk(const std::string& mapId, MapTile entrance, const std::string& cameFrom,
                     bool side)
{
    if (SeenId(Visited, mapId))
        return true;
    Visited.push_back(mapId);
    const MapDef* map = Data->FindMap(mapId);
    if (map == nullptr) {
        Error = "map '" + mapId + "' does not exist";
        return false;
    }
    if (!RouteTileOpen(*Data, *map, entrance.X, entrance.Y)) {
        Error = "map '" + mapId + "' entrance is blocked";
        return false;
    }

    const i32 width = map->Width;
    const i32 height = map->Height;
    std::vector<u8> seen(static_cast<usize>(width) * static_cast<usize>(height), 0);
    struct Node {
        i32 X = 0;
        i32 Y = 0;
    };
    std::vector<Node> queue;
    Node first;
    first.X = entrance.X;
    first.Y = entrance.Y;
    queue.push_back(first);
    seen[static_cast<usize>(entrance.Y) * static_cast<usize>(width) +
         static_cast<usize>(entrance.X)] = 1;

    std::string follow;
    MapTile followAt;
    const std::string next = side || Main == nullptr ? std::string() : NextOnRoute(*Main, mapId);
    usize cursor = 0;
    while (cursor < queue.size()) {
        const Node node = queue[cursor];
        ++cursor;
        for (const MapPoint& point : map->Points) {
            if (point.Kind != MapPointKind::Rest || point.Tile.X != node.X || point.Tile.Y != node.Y)
                continue;
            RouteStep step;
            step.Kind = RouteStepKind::Rest;
            step.Side = side;
            step.MapId = mapId;
            Steps.push_back(step);
        }
        for (const MapSpawn& spawn : map->Spawns) {
            if (spawn.Tile.X != node.X || spawn.Tile.Y != node.Y)
                continue;
            RouteStep step;
            step.Kind = RouteStepKind::Fight;
            step.Side = side;
            step.EncounterId = spawn.Encounter;
            step.MapId = mapId;
            Steps.push_back(step);
        }
        for (const MapLink& link : map->Links) {
            if (link.Tile.X != node.X || link.Tile.Y != node.Y)
                continue;
            if (link.Map == cameFrom || SeenId(Visited, link.Map))
                continue;
            if (!next.empty() && link.Map == next) {
                follow = link.Map;
                followAt = link.Arrival;
                continue;
            }
            if (!Walk(link.Map, link.Arrival, mapId, true))
                return false;
        }

        const i32 stepX[] = {0, 1, 0, -1};
        const i32 stepY[] = {-1, 0, 1, 0};
        for (i32 dir = 0; dir < 4; ++dir) {
            const i32 x = node.X + stepX[dir];
            const i32 y = node.Y + stepY[dir];
            if (!RouteTileOpen(*Data, *map, x, y))
                continue;
            const usize index =
                static_cast<usize>(y) * static_cast<usize>(width) + static_cast<usize>(x);
            if (seen[index] != 0)
                continue;
            seen[index] = 1;
            Node further;
            further.X = x;
            further.Y = y;
            queue.push_back(further);
        }
    }

    if (!next.empty() && follow.empty()) {
        Error = "map '" + mapId + "' does not reach '" + next + "'";
        return false;
    }
    if (!follow.empty())
        return Walk(follow, followAt, mapId, side);
    return true;
}

void RememberDrops(const Database& data, std::string_view encounterId, std::vector<std::string>& gear)
{
    const EncounterDef* encounter = data.FindEncounter(encounterId);
    if (encounter == nullptr)
        return;
    for (const std::string& enemyId : encounter->Enemies) {
        const EnemyDef* enemy = data.FindEnemy(enemyId);
        if (enemy == nullptr)
            continue;
        for (const DropDef& drop : enemy->Drops) {
            if (drop.Item.empty() || SeenId(gear, drop.Item))
                continue;
            gear.push_back(drop.Item);
        }
    }
}

bool PlayOneRoute(const Database& data, const BuildSpec& build, const MapRoute& route,
                  bool fangRoute, std::vector<RouteRow>& rows, std::string& error)
{
    bool full = true;
    i32 hp = 0;
    i32 focus = 0;
    std::vector<std::string> gear;
    for (const RouteStep& step : route.Steps) {
        if (!fangRoute && step.Side)
            continue;
        if (step.Kind == RouteStepKind::Rest) {
            full = true;
            continue;
        }
        const EncounterDef* encounter = data.FindEncounter(step.EncounterId);
        if (encounter == nullptr) {
            error = "no encounter '" + step.EncounterId + "'";
            return false;
        }
        const std::vector<FocusOrder> orders = OrdersFor(*encounter);
        FocusOrder order;
        if (!orders.empty())
            order = orders.front();
        RunSpec spec;
        spec.Build = &build;
        spec.EncounterId = encounter->Id;
        spec.FocusId = order.First;
        spec.ThenId = order.Then;
        spec.RandomFocus = order.Random;
        spec.RngState = 1;
        spec.UsePools = !full;
        spec.StartHp = hp;
        spec.StartFocus = focus;
        spec.Gear = gear.empty() ? nullptr : &gear;
        const SimRun run = SimulateBuild(data, spec);

        RouteRow row;
        row.Encounter = encounter->Name;
        row.Build = build.Label;
        row.FangRoute = fangRoute;
        row.Fang = SeenId(gear, "alphas_fang");
        row.Ok = run.Ok;
        row.Error = run.Error;
        if (!run.Ok) {
            rows.push_back(std::move(row));
            error = run.Error.empty() ? "the route could not start a fight" : run.Error;
            return false;
        }
        row.StartHp = run.StartHp;
        row.MaxHp = run.MaxHp;
        row.Won = run.Victory && !run.TimedOut;
        row.HpLeft = row.Won ? run.Hp : 0;
        row.FocusLeft = row.Won ? run.Focus : 0;
        if (row.Won) {
            full = false;
            hp = run.Hp;
            focus = run.Focus;
            RememberDrops(data, encounter->Id, gear);
        } else {
            full = true;
        }
        rows.push_back(std::move(row));
    }
    return true;
}

} // namespace

std::vector<SimRateRow> SimulateEncounterRates(const Database& data, const SimRateQuery& query,
                                               std::string& error)
{
    error.clear();
    const EncounterDef* encounter = data.FindEncounter(query.EncounterId);
    if (encounter == nullptr) {
        error = "no encounter '" + query.EncounterId + "'";
        return {};
    }
    if (query.Runs < 1) {
        error = "runs must be at least 1";
        return {};
    }

    const std::vector<FocusOrder> orders = OrdersFor(*encounter);
    std::vector<SimRateRow> rows;
    for (const BuildSpec& build : kBuilds) {
        for (const FocusOrder& order : orders) {
            const SimRateRow plain = SimulateRate(data, *encounter, build, order, false, query);
            rows.push_back(plain);
            if (!plain.Ok) {
                error = plain.Error;
                return rows;
            }
            const SimRateRow fanged = SimulateRate(data, *encounter, build, order, true, query);
            rows.push_back(fanged);
            if (!fanged.Ok) {
                error = fanged.Error;
                return rows;
            }
        }
    }
    return rows;
}

std::string FormatSimRates(const std::vector<SimRateRow>& rows, const SimRateQuery& query)
{
    std::string text;
    text += "Battle damage, turn order, and foe AI do not use a seed.\n";
    text += "A named focus with slip 0 is one outcome, so Runs is 1.\n";
    text += "default marks a uniform living foe each turn, once per seed.\n";
    text += "A one-enemy fight does not change with the seed.\n";
    text += "HP left is 0 on a loss or a timeout.\n";
    text += "P10 is sorted HP at index (runs-1)*10/100.\n";
    text += "Requested runs " + std::to_string(query.Runs);
    text += ", seed " + std::to_string(query.Seed);
    text += ", slip " + std::to_string(query.Slip) + ".\n";
    text += Pad("Encounter", 16);
    text += Pad("Build", 22);
    text += Pad("Focus", 12);
    text += Pad("Fang", 6);
    text += Pad("Runs", 6);
    text += Pad("Win%", 8);
    text += Pad("Mean HP", 10);
    text += Pad("P10 HP", 8);
    text += "Turns\n";
    for (const SimRateRow& row : rows) {
        text += Pad(row.Encounter, 16);
        text += Pad(row.Build, 22);
        text += Pad(row.Focus, 12);
        text += Pad(row.Fang ? "yes" : "no", 6);
        if (!row.Ok) {
            text += "setup failed: ";
            text += row.Error;
            text += "\n";
            continue;
        }
        const i32 runs = row.Runs > 0 ? row.Runs : 1;
        const i64 winTenths = static_cast<i64>(row.Wins) * 1000 / runs;
        const i64 hpTenths = row.SumHp * 10 / runs;
        const i64 turnTenths = row.SumTurns * 10 / runs;
        text += Pad(std::to_string(row.Runs), 6);
        text += Pad(OneDecimal(winTenths), 8);
        text += Pad(OneDecimal(hpTenths), 10);
        text += Pad(std::to_string(row.P10Hp), 8);
        text += OneDecimal(turnTenths);
        text += "\n";
    }
    return text;
}

bool WantedBuild(const BuildSpec& build, const ChainQuery& query)
{
    if (!query.ClassId.empty() && query.ClassId != build.ClassId)
        return false;
    if (!query.PickId.empty()) {
        if (build.Pick == nullptr || query.PickId != build.Pick)
            return false;
    }
    return true;
}

std::vector<ChainRow> SimulateChain(const Database& data, const ChainQuery& query,
                                    std::string& error)
{
    error.clear();
    std::vector<ChainRow> rows;
    bool any = false;
    for (const BuildSpec& build : kBuilds) {
        if (WantedBuild(build, query))
            any = true;
    }
    if (!any) {
        error = "no harness build matches that class";
        return rows;
    }
    const auto encounters = data.GetEncounters();
    if (encounters.empty()) {
        error = "no encounters";
        return rows;
    }

    for (const BuildSpec& build : kBuilds) {
        if (!WantedBuild(build, query))
            continue;
        bool carry = false;
        i32 hp = 0;
        i32 focus = 0;
        for (const EncounterDef& encounter : encounters) {
            const std::vector<FocusOrder> orders = OrdersFor(encounter);
            FocusOrder order;
            if (!orders.empty())
                order = orders.front();
            RunSpec spec;
            spec.Build = &build;
            spec.EncounterId = encounter.Id;
            spec.FocusId = order.First;
            spec.ThenId = order.Then;
            spec.RandomFocus = order.Random;
            spec.RngState = 1;
            spec.UsePools = carry;
            spec.StartHp = hp;
            spec.StartFocus = focus;
            const SimRun run = SimulateBuild(data, spec);

            ChainRow row;
            row.Encounter = encounter.Name;
            row.Build = build.Label;
            row.Ok = run.Ok;
            row.Error = run.Error;
            if (!run.Ok) {
                rows.push_back(std::move(row));
                error = run.Error.empty() ? "the chain could not start a fight" : run.Error;
                return rows;
            }
            row.StartHp = run.StartHp;
            row.StartFocus = run.StartFocus;
            row.MaxHp = run.MaxHp;
            row.Won = run.Victory && !run.TimedOut;
            row.HpLeft = row.Won ? run.Hp : 0;
            row.FocusLeft = row.Won ? run.Focus : 0;
            if (row.Won) {
                carry = true;
                hp = run.Hp;
                focus = run.Focus;
            } else {
                carry = false;
                hp = 0;
                focus = 0;
            }
            rows.push_back(std::move(row));
        }
    }
    return rows;
}

std::string FormatChain(const std::vector<ChainRow>& rows)
{
    std::string text;
    text += "Chain at level 6 with a rusty sword and an oak shield. No fang.\n";
    text += "Fights run in encounter order. The pack kills the dire wolf first.\n";
    text += "The first fight of a build starts full, so it matches that single-fight row.\n";
    text += "A win carries HP and focus. A loss restores both, and the next fight starts full.\n";
    text += "HP left and focus left are 0 on a loss or a timeout.\n";
    text += Pad("Encounter", 18);
    text += Pad("Build", 22);
    text += Pad("Start HP", 10);
    text += Pad("Win%", 8);
    text += Pad("HP left", 10);
    text += "Focus\n";
    for (const ChainRow& row : rows) {
        text += Pad(row.Encounter, 18);
        text += Pad(row.Build, 22);
        if (!row.Ok) {
            text += "setup failed: ";
            text += row.Error;
            text += "\n";
            continue;
        }
        text += Pad(std::to_string(row.StartHp), 10);
        text += Pad(row.Won ? "100.0" : "0.0", 8);
        text += Pad(std::to_string(row.HpLeft), 10);
        text += std::to_string(row.FocusLeft);
        text += "\n";
    }
    return text;
}

MapRoute CollectMapRoute(const Database& data)
{
    MapRoute route;
    const MapDef* hub = RouteHub(data);
    if (hub == nullptr) {
        route.Error = "no hub map";
        return route;
    }
    std::vector<Reach> reached;
    ReachFromHub(data, hub->Id, reached);
    const BossSeat boss = FindBossRoom(data, reached);
    if (boss.Map == nullptr || boss.Spawn == nullptr) {
        route.Error = "no boss room linked from the hub";
        return route;
    }
    std::vector<std::string> chain;
    if (!ChainToBoss(reached, hub->Id, boss.Map->Id, chain, route.Error))
        return route;

    RouteWalk walk;
    walk.Data = &data;
    walk.Main = &chain;
    if (!walk.Walk(hub->Id, hub->Start, "", false)) {
        route.Error = walk.Error.empty() ? "the route could not be walked" : walk.Error;
        return route;
    }
    bool found = false;
    for (const RouteStep& step : walk.Steps) {
        if (step.Kind == RouteStepKind::Fight && step.EncounterId == boss.Spawn->Encounter)
            found = true;
    }
    if (!found) {
        route.Error = "the boss is not on the route";
        return route;
    }
    route.Steps = std::move(walk.Steps);
    return route;
}

std::vector<RouteRow> SimulateRoute(const Database& data, const ChainQuery& query, std::string& error)
{
    error.clear();
    std::vector<RouteRow> rows;
    const MapRoute route = CollectMapRoute(data);
    if (!route.Error.empty()) {
        error = route.Error;
        return rows;
    }
    bool any = false;
    for (const BuildSpec& build : kBuilds) {
        if (WantedBuild(build, query))
            any = true;
    }
    if (!any) {
        error = "no harness build matches that class";
        return rows;
    }
    for (const BuildSpec& build : kBuilds) {
        if (!WantedBuild(build, query))
            continue;
        if (!PlayOneRoute(data, build, route, false, rows, error))
            return rows;
        if (!PlayOneRoute(data, build, route, true, rows, error))
            return rows;
    }
    return rows;
}

std::string FormatRoute(const std::vector<RouteRow>& rows)
{
    std::string text;
    text += "Route at level 6 with a rusty sword and an oak shield.\n";
    text += "Main skips the optional room. Fang takes it, and a win there wears that fight's drops.\n";
    text += "Fights follow the maps from the hub. A rest on the way refills HP and focus.\n";
    text += "A win carries both. A loss restores both, and the next fight starts full.\n";
    text += "The pack kills the dire wolf first. HP left and focus left are 0 on a loss or a timeout.\n";
    text += Pad("Route", 8);
    text += Pad("Encounter", 18);
    text += Pad("Build", 22);
    text += Pad("Fang", 6);
    text += Pad("Start HP", 10);
    text += Pad("Win%", 8);
    text += Pad("HP left", 10);
    text += "Focus\n";
    for (const RouteRow& row : rows) {
        text += Pad(row.FangRoute ? "fang" : "main", 8);
        text += Pad(row.Encounter, 18);
        text += Pad(row.Build, 22);
        if (!row.Ok) {
            text += "setup failed: ";
            text += row.Error;
            text += "\n";
            continue;
        }
        text += Pad(row.Fang ? "yes" : "no", 6);
        text += Pad(std::to_string(row.StartHp), 10);
        text += Pad(row.Won ? "100.0" : "0.0", 8);
        text += Pad(std::to_string(row.HpLeft), 10);
        text += std::to_string(row.FocusLeft);
        text += "\n";
    }
    return text;
}

int RunPlaytestSim(const PlaytestOptions& options)
{
    const std::filesystem::path directory = PlaytestDataDirectory(options);
    Database data(directory);
    DataError loadError;
    if (!data.Load(&loadError)) {
        const std::string message =
            loadError.Message.empty() ? "could not load data" : loadError.Message;
        std::fprintf(stderr, "%s\n", message.c_str());
        return 1;
    }

    if (options.Chain) {
        ChainQuery chain;
        chain.ClassId = options.ClassId;
        if (!options.PickIds.empty())
            chain.PickId = options.PickIds.front();
        std::string error;
        const std::vector<ChainRow> rows = SimulateChain(data, chain, error);
        if (rows.empty()) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        std::fputs(FormatChain(rows).c_str(), stdout);
        if (!error.empty()) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        return 0;
    }

    if (options.Route) {
        ChainQuery routeQuery;
        routeQuery.ClassId = options.ClassId;
        if (!options.PickIds.empty())
            routeQuery.PickId = options.PickIds.front();
        std::string error;
        const std::vector<RouteRow> rows = SimulateRoute(data, routeQuery, error);
        if (rows.empty()) {
            std::fprintf(stderr, "%s\n", error.empty() ? "the route produced no fights" : error.c_str());
            return 1;
        }
        std::fputs(FormatRoute(rows).c_str(), stdout);
        if (!error.empty()) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        return 0;
    }

    SimRateQuery query;
    query.EncounterId = options.Fight.Id;
    query.Runs = options.Runs < 1 ? 1 : options.Runs;
    query.Seed = options.Seed;
    query.Slip = options.Slip;

    if (options.ClassId.empty() && options.PickIds.empty() && options.FocusId.empty()) {
        std::string error;
        const std::vector<SimRateRow> rows = SimulateEncounterRates(data, query, error);
        if (rows.empty()) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        std::fputs(FormatSimRates(rows, query).c_str(), stdout);
        if (!error.empty()) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        return 0;
    }

    const EncounterDef* encounter = data.FindEncounter(options.Fight.Id);
    if (encounter == nullptr) {
        std::fprintf(stderr, "no encounter '%s'\n", options.Fight.Id.c_str());
        return 1;
    }

    BuildSpec build;
    build.Label = options.ClassId.empty() ? "Fighter" : options.ClassId.c_str();
    build.ClassId = options.ClassId.empty() ? "fighter" : options.ClassId.c_str();
    build.Pick = options.PickIds.empty() ? nullptr : options.PickIds[0].c_str();
    build.Practiced = nullptr;
    if (options.ClassId == "berserker")
        build.Practiced = "cleave";
    else if (options.ClassId == "guardian")
        build.Practiced = "shield_bash";

    FocusOrder order;
    if (options.FocusId.empty()) {
        order.Label = "default";
        order.Random = true;
    } else {
        order.Label = options.FocusId;
        order.First = options.FocusId;
        const std::vector<FocusOrder> orders = OrdersFor(*encounter);
        for (const FocusOrder& known : orders) {
            if (!known.Random && known.First == options.FocusId)
                order.Then = known.Then;
        }
        if (order.Then.empty()) {
            for (const std::string& enemy : encounter->Enemies) {
                if (enemy != options.FocusId) {
                    order.Then = enemy;
                    break;
                }
            }
        }
    }

    const bool fang = EquipHas(options, "alphas_fang");
    const SimRateRow row = SimulateRate(data, *encounter, build, order, fang, query);
    const std::vector<SimRateRow> one{row};
    std::fputs(FormatSimRates(one, query).c_str(), stdout);
    if (!row.Ok) {
        std::fprintf(stderr, "%s\n", row.Error.c_str());
        return 1;
    }
    return 0;
}

} // namespace Ascension
