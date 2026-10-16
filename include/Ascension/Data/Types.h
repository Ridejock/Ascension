#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "Emerald/Core/Defines.h"

// Typed definitions for the slice. The JSON files that fill these live under data/ and are
// read in Database.cpp; nothing in this header knows about a JSON library.

namespace Ascension {

// The combat stats every class, enemy and piece of gear is written in. These ids are the JSON
// keys. Adding a stat means a new field here and a matching entry in stats.json; the loader
// rejects either side getting ahead of the other.
inline constexpr std::array<std::string_view, 5> kStatIds = {
    "vitality", "strength", "defense", "agility", "focus",
};

// A bundle of the five stats. Missing keys in a bonus object are zero. Unknown keys are errors
// at load time, not zeros here.
struct StatBlock {
    i32 Vitality = 0;
    i32 Strength = 0;
    i32 Defense = 0;
    i32 Agility = 0;
    i32 Focus = 0;

    // 0 when `id` is not one of kStatIds.
    [[nodiscard]] i32 Get(std::string_view id) const;
    // False if `id` is not one of kStatIds (the block is unchanged).
    bool Set(std::string_view id, i32 value);

    StatBlock& operator+=(const StatBlock& other);
};

[[nodiscard]] StatBlock operator+(StatBlock left, const StatBlock& right);

// One row of stats.json: the display name for a stat id.
struct StatDef {
    std::string Id;
    std::string Name;
    std::string Description;
};

enum class SkillKind : u8 { Active, Passive };

// Who an active skill lands on. Passives leave this at Enemy; it is not used.
enum class SkillTarget : u8 { Self, Enemy, AllEnemies };

// What an active skill does. The numbers (power, scaling, focus) come from the skill; the
// damage formula that combines them with defense comes from CombatRules.
enum class SkillEffect : u8 { Damage, Guard, Heal, Empower, Taunt };

// What a passive does. Stat adds Amount to Stat. Bloodied adds Power percent of the attack
// term at empty health, and nothing at full health. Ward shaves Power percent off every
// incoming blow. Battle reads these; it does not special-case a passive id.
enum class PassiveEffect : u8 { Stat, Bloodied, Ward };

struct SkillDef {
    std::string Id;
    std::string Name;
    std::string Description;
    SkillKind Kind = SkillKind::Active;
    SkillEffect Effect = SkillEffect::Damage;
    SkillTarget Target = SkillTarget::Enemy;
    PassiveEffect Passive = PassiveEffect::Stat;
    i32 Power = 0;       // damage/heal added to the scaling stat; guard and taunt are a percent
                         // (0-100); empower is the percent added to the next blow; bloodied is
                         // the percent added to the attack term at empty health; ward is the
                         // percent shaved off every incoming blow
    i32 FocusCost = 0;   // active
    i32 Refund = 0;      // taunt: focus paid back when the next hit actually lands
    std::string Scaling; // active: a stat id the power scales with, or empty
    std::string Stat;    // stat passive: the stat it changes
    i32 Amount = 0;      // stat passive: added to that stat
};

// The integers from combat.json. Damage, before a guard, is
//   max(MinDamage, power + scalingStat * AttackNumerator / AttackDenominator
//                      - defense * DefenseNumerator / DefenseDenominator)
// and an empower percent and a bloodied percent are applied to the attack term first. Heal
// uses the same attack term with no defense. Max health is vitality times HpPerVitality.
// Denominators are at least 1.
struct CombatRules {
    i32 HpPerVitality = 1;
    i32 MinDamage = 0;
    i32 AttackNumerator = 1;
    i32 AttackDenominator = 1;
    i32 DefenseNumerator = 1;
    i32 DefenseDenominator = 1;
};

// The integers from progress.json. XpToReach[n] is the total XP required to be level n+1, and
// the first entry is 0. Rank starts at 1 and goes up by one every UsesPerRank uses of that
// skill, stopping at MaxSkillRank. Each rank above 1 adds PowerPerRank to that skill's power
// (damage and healing, not a guard's percent).
struct ProgressRules {
    std::vector<i32> XpToReach;
    i32 UsesPerRank = 1;
    i32 MaxSkillRank = 1;
    i32 PowerPerRank = 0;
};

// 1 when the table is empty. XP below the first step stays at 1, and XP past the last step
// stays at the last level.
[[nodiscard]] inline i32 LevelForXp(const ProgressRules& rules, i32 xp)
{
    i32 level = 1;
    if (xp < 0)
        xp = 0;
    for (usize i = 1; i < rules.XpToReach.size(); ++i) {
        if (xp < rules.XpToReach[i])
            break;
        level = static_cast<i32>(i) + 1;
    }
    return level;
}

// Total XP required to reach `level + 1`. At max level this is the last step, which the hero
// has already passed.
[[nodiscard]] inline i32 NextLevelXp(const ProgressRules& rules, i32 level)
{
    if (rules.XpToReach.empty())
        return 0;
    if (level < 1)
        level = 1;
    if (static_cast<usize>(level) >= rules.XpToReach.size())
        return rules.XpToReach.back();
    return rules.XpToReach[static_cast<usize>(level)];
}

[[nodiscard]] inline bool AtMaxLevel(const ProgressRules& rules, i32 level)
{
    return rules.XpToReach.empty() || level >= static_cast<i32>(rules.XpToReach.size());
}

// Rank 1 at 0 uses. UsesPerRank uses move it up by one, capped at MaxSkillRank.
[[nodiscard]] inline i32 RankForUses(const ProgressRules& rules, i32 uses)
{
    if (uses < 0)
        uses = 0;
    const i32 step = rules.UsesPerRank < 1 ? 1 : rules.UsesPerRank;
    i32 rank = 1 + uses / step;
    const i32 cap = rules.MaxSkillRank < 1 ? 1 : rules.MaxSkillRank;
    if (rank > cap)
        rank = cap;
    return rank;
}

// A class. A base class (EvolvesFrom empty) has level-1 Stats and per-level Growth. An evolution
// names the base it comes from and the level it is offered at; Bonus is added on top of the
// parent's stats at that level, and Growth applies to levels after that. Skills and Passives are
// what this class itself grants: an evolution's lists are the skills and passives it adds.
struct ClassDef {
    std::string Id;
    std::string Name;
    std::string Description;
    StatBlock Stats;
    StatBlock Bonus;
    StatBlock Growth;
    std::vector<std::string> Skills;
    std::vector<std::string> Passives;
    std::string EvolvesFrom;
    i32 EvolveLevel = 0;

    [[nodiscard]] bool IsEvolution() const { return !EvolvesFrom.empty(); }
};

struct EnemyDef {
    std::string Id;
    std::string Name;
    std::string Description;
    i32 Level = 1;
    StatBlock Stats;
    std::vector<std::string> Skills; // active skill ids
    i32 Experience = 0;
    i32 Gold = 0;
    bool Boss = false; // the roster has one boss, last in the sample
};

enum class GearSlot : u8 { Weapon, Armor, Accessory };

struct ItemDef {
    std::string Id;
    std::string Name;
    std::string Description;
    GearSlot Slot = GearSlot::Weapon;
    StatBlock Bonus;
    i32 Value = 0; // gold
};

[[nodiscard]] const char* ToString(SkillKind kind);
[[nodiscard]] const char* ToString(SkillTarget target);
[[nodiscard]] const char* ToString(SkillEffect effect);
[[nodiscard]] const char* ToString(GearSlot slot);

// --- StatBlock (inline: five fields, no reason to hide them in a .cpp) ---

inline const i32* FindStatValue(const StatBlock& block, std::string_view id)
{
    if (id == "vitality")
        return &block.Vitality;
    if (id == "strength")
        return &block.Strength;
    if (id == "defense")
        return &block.Defense;
    if (id == "agility")
        return &block.Agility;
    if (id == "focus")
        return &block.Focus;
    return nullptr;
}

inline i32* FindStatValue(StatBlock& block, std::string_view id)
{
    return const_cast<i32*>(FindStatValue(static_cast<const StatBlock&>(block), id));
}

inline i32 StatBlock::Get(std::string_view id) const
{
    const i32* value = FindStatValue(*this, id);
    return value != nullptr ? *value : 0;
}

inline bool StatBlock::Set(std::string_view id, i32 value)
{
    i32* slot = FindStatValue(*this, id);
    if (slot == nullptr)
        return false;
    *slot = value;
    return true;
}

inline StatBlock& StatBlock::operator+=(const StatBlock& other)
{
    Vitality += other.Vitality;
    Strength += other.Strength;
    Defense += other.Defense;
    Agility += other.Agility;
    Focus += other.Focus;
    return *this;
}

inline StatBlock operator+(StatBlock left, const StatBlock& right)
{
    left += right;
    return left;
}

inline const char* ToString(SkillKind kind)
{
    return kind == SkillKind::Passive ? "passive" : "active";
}

inline const char* ToString(SkillTarget target)
{
    if (target == SkillTarget::Self)
        return "self";
    if (target == SkillTarget::AllEnemies)
        return "allEnemies";
    return "enemy";
}

inline const char* ToString(SkillEffect effect)
{
    if (effect == SkillEffect::Guard)
        return "guard";
    if (effect == SkillEffect::Heal)
        return "heal";
    if (effect == SkillEffect::Empower)
        return "empower";
    if (effect == SkillEffect::Taunt)
        return "taunt";
    return "damage";
}

inline const char* ToString(PassiveEffect effect)
{
    if (effect == PassiveEffect::Bloodied)
        return "bloodied";
    if (effect == PassiveEffect::Ward)
        return "ward";
    return "stat";
}

inline const char* ToString(GearSlot slot)
{
    if (slot == GearSlot::Armor)
        return "armor";
    if (slot == GearSlot::Accessory)
        return "accessory";
    return "weapon";
}

} // namespace Ascension
