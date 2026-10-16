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

struct SkillDef {
    std::string Id;
    std::string Name;
    std::string Description;
    SkillKind Kind = SkillKind::Active;
    SkillEffect Effect = SkillEffect::Damage;
    SkillTarget Target = SkillTarget::Enemy;
    i32 Power = 0;       // damage/heal added to the scaling stat; guard is a percent (0-100);
                         // empower is the percent added to the next blow
    i32 FocusCost = 0;   // active
    std::string Scaling; // active: a stat id the power scales with, or empty
    std::string Stat;    // passive: the stat it changes
    i32 Amount = 0;      // passive: added to that stat
};

// The integers from combat.json. Damage, before a guard, is
//   max(MinDamage, power + scalingStat * AttackNumerator / AttackDenominator
//                      - defense * DefenseNumerator / DefenseDenominator)
// and an empower percent is applied to the attack term first. Heal uses the same attack term
// with no defense. Max health is vitality times HpPerVitality. Denominators are at least 1.
struct CombatRules {
    i32 HpPerVitality = 1;
    i32 MinDamage = 0;
    i32 AttackNumerator = 1;
    i32 AttackDenominator = 1;
    i32 DefenseNumerator = 1;
    i32 DefenseDenominator = 1;
};

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

inline const char* ToString(GearSlot slot)
{
    if (slot == GearSlot::Armor)
        return "armor";
    if (slot == GearSlot::Accessory)
        return "accessory";
    return "weapon";
}

} // namespace Ascension
