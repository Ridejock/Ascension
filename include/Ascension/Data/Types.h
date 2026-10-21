#pragma once

#include <array>
#include <optional>
#include <span>
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

// Who an active skill is aimed at, as a default for its effects. Passives leave this at
// Enemy; it is not read. An effect's own aim wins when the effect sets one.
enum class SkillTarget : u8 { Self, Enemy, AllEnemies };

// Who one effect lands on. `target` is the foe the skill is pointed at: the player's
// selected enemy, or the player when an enemy acts. `allEnemies` is every living enemy
// other than the user. `randomEnemy` is one of those, walking the pack in order.
// `lowestHpEnemy` is the one of those with the lowest health percent; a tie keeps the
// earlier seat. `self` is the unit using the skill.
enum class EffectAim : u8 { Self, Target, AllEnemies, RandomEnemy, LowestHpEnemy };

// When an effect runs. Battle dispatches these. It does not look at a skill id.
// always is folded into the unit (a flat stat, or a missing-health bonus on a blow).
// onUse runs when the skill is spent. onHit runs after that skill's damage lands.
// onBlock runs when a guard reduces a hit against the unit. onTurnStart runs when the
// unit's turn is actually taken. onDamaged runs when a hit lowers the unit's health.
enum class EffectTrigger : u8 { Always, OnUse, OnHit, OnBlock, OnTurnStart, OnDamaged };

// What an effect does. Which fields are set comes from the recipe. A tune cannot add a field
// the recipe left out, and it cannot change the op.
enum class EffectOp : u8 {
    Damage,
    Heal,
    Guard,
    Reflect,
    Empower,
    Harden,
    Stat,
    StatPerMissingHp,
    GrantFocus,
    ApplyStatus,
    StunIfWindingUp,
};

// One effect. An engaged optional is a field the recipe set, which a later tune may replace.
// An empty optional is not on this effect. Power on damage, heal, and reflect is what a rank
// adds to. A guard's Amount is a percent and is not ranked.
struct EffectDef {
    std::string Id;
    EffectTrigger Trigger = EffectTrigger::OnUse;
    EffectOp Op = EffectOp::Damage;
    std::optional<i32> Power;
    std::optional<i32> Amount;
    std::optional<i32> Percent;
    std::optional<i32> Per;
    std::optional<i32> Max;
    std::optional<i32> Turns;
    std::optional<i32> Every;
    std::optional<std::string> Stat;
    std::optional<std::string> Scaling;
    std::optional<std::string> Status;
    // Empty: use the skill's target. A skill target of "enemy" means EffectAim::Target.
    std::optional<EffectAim> Aim;
    // Damage only. When set, the blow lands on an aim only while that aim holds the status.
    // Checked when the effect resolves, before onHit. Consume spends that status at the end of
    // the use: every damage effect in the use still sees it, and the use does not apply it again.
    std::optional<std::string> IfTargetHas;
    bool Consume = false;
};

// A catalog skill, after its own tune and before any grant tune. Effects keep the ids a tune
// targets. Name, target, focus cost, and windup stay here.
struct SkillDef {
    std::string Id;
    std::string Name;
    std::string Description;
    SkillKind Kind = SkillKind::Active;
    SkillTarget Target = SkillTarget::Enemy;
    i32 FocusCost = 0;
    i32 Windup = 0; // active damage: telegraph turns before the hit. 0 hits now
    std::vector<EffectDef> Effects;

    [[nodiscard]] const EffectDef* FindEffect(std::string_view id) const;
};

// One class, enemy, or item grant, resolved to its own copy at load time. Id is the catalog
// skill id, which is also the key skill ranks use. Resolved is the recipe, then the skill tune,
// then this grant's tune. Battle reads the copy. Tuned is set when the grant listed a tune.
struct SkillGrant {
    std::string Id;
    SkillDef Resolved;
    bool Tuned = false;
};

// A named condition from statuses.json. Zero fields are unused. Stacks up to MaxStacks, and
// TurnsLeft starts at Turns. DamagePerTurn is dealt at the start of the afflicted unit's turn,
// once per stack. DamageTakenPercent and DamageDealtPercent add (25 and 25 is 50). SkipAction
// spends the unit's action.
struct StatusDef {
    std::string Id;
    std::string Name;
    std::string Description;
    i32 DamagePerTurn = 0;
    i32 Turns = 1;
    i32 MaxStacks = 1;
    i32 DamageTakenPercent = 0;
    i32 DamageDealtPercent = 0;
    bool SkipAction = false;
    // Status ids this one blocks while it is up. An enraged boss can list "stunned".
    std::vector<std::string> Resists;
};

// The integers from combat.json. Damage, before a guard, is
//   max(MinDamage, power + scalingStat * AttackNumerator / AttackDenominator
//                      - defense * DefenseNumerator / DefenseDenominator)
// and an empower percent is applied to the attack term first. Heal uses the same attack term
// with no defense. Max health is vitality times HpPerVitality. The hero's focus pool is
// FocusBase plus their focus stat; it rises by FocusPerTurn at the start of their turn and by
// another FocusOnWait when they wait. Denominators are at least 1.
struct CombatRules {
    i32 HpPerVitality = 1;
    i32 MinDamage = 0;
    i32 AttackNumerator = 1;
    i32 AttackDenominator = 1;
    i32 DefenseNumerator = 1;
    i32 DefenseDenominator = 1;
    i32 FocusBase = 0;
    i32 FocusPerTurn = 0;
    i32 FocusOnWait = 0;
};

// The integers from progress.json. XpToReach[n] is the total XP required to be level n+1, and
// the first entry is 0. Rank starts at 1 and stops at MaxSkillRank. UsesPerRank is either one
// integer (a flat step: rank = 1 + uses / step) or a longer strictly increasing list of the
// total uses required to reach rank 2, rank 3, and so on. Each rank above 1 adds PowerPerRank
// to that skill's power (damage, healing, and a reflect's flat power, not a guard's percent).
struct ProgressRules {
    std::vector<i32> XpToReach;
    std::vector<i32> UsesPerRank;
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

// Rank 1 at 0 uses, capped at MaxSkillRank. One UsesPerRank entry (or none) is a flat step.
// A longer list is the cumulative uses required for rank 2, then rank 3, and so on.
[[nodiscard]] inline i32 RankForUses(const ProgressRules& rules, i32 uses)
{
    if (uses < 0)
        uses = 0;
    i32 rank = 1;
    if (rules.UsesPerRank.size() <= 1) {
        i32 step = 1;
        if (!rules.UsesPerRank.empty() && rules.UsesPerRank[0] > 0)
            step = rules.UsesPerRank[0];
        rank = 1 + uses / step;
    } else {
        for (const i32 threshold : rules.UsesPerRank) {
            if (uses < threshold)
                break;
            ++rank;
        }
    }
    const i32 cap = rules.MaxSkillRank < 1 ? 1 : rules.MaxSkillRank;
    if (rank > cap)
        rank = cap;
    return rank;
}

// A class. A base class (EvolvesFrom empty) has level-1 Stats and per-level Growth. Stats is
// that class's attribute grant. An evolution names the base it comes from and the level it is
// offered at; Bonus is the attribute grant it adds, and Growth applies to levels after that.
// Skills and Passives are what this class itself grants. Starter is set on a base class the
// player may return to when a build is reset. The list is read from data, so another starter
// is a JSON flag, not a code change.
//
// One skill choice a class offers once the hero reaches Level. The hero keeps one option for
// the run. Each option is a grant like a class skill: a catalog id, or an id with a tune. An
// option whose id the class already grants is an upgrade; its copy merges into that skill,
// the best value wins per field, and the one rank is shared.
struct SkillPick {
    i32 Level = 0;
    std::vector<SkillGrant> Options;
};

struct ClassDef {
    std::string Id;
    std::string Name;
    std::string Description;
    StatBlock Stats;
    StatBlock Bonus;
    StatBlock Growth;
    std::vector<SkillGrant> Skills;
    std::vector<SkillGrant> Passives;
    std::vector<SkillPick> Picks; // in level order
    std::string EvolvesFrom;
    i32 EvolveLevel = 0;
    bool Starter = false;

    [[nodiscard]] bool IsEvolution() const { return !EvolvesFrom.empty(); }
};

// What an enemy does when its summon cannot join (a copy is already standing, or the
// fight has no seat). Act spends the turn on the ordinary pattern. Fizzle spends it on
// the refusal.
enum class BlockedSummon : u8 { Act, Fizzle };

// What a matching ai rule does. Pattern is the repeating list. Skill is one of this
// enemy's own grants.
enum class AiAction : u8 { Pattern, Skill };

// One condition. An empty string, or an optional that is not set, is not tested. Every
// field that is set has to hold. A percent uses the phase comparison: hp * 100 against
// maxHp * percent. AllyHpAtMost uses that comparison on the other living foes together:
// their current health over their combined maximum. Dead foes are left out of both
// totals. The acting foe, with no other living ally, does not satisfy it.
// AllyCountAtMost and AllyCountAtLeast count those same other living foes. The acting
// foe is not one of them, and a dead foe is not either. 0 is a foe whose allies have
// all fallen.
struct AiCondition {
    std::optional<i32> SelfHpAtMost;
    std::optional<i32> SelfHpAtLeast;
    std::optional<i32> TargetHpAtMost;
    std::optional<i32> TargetHpAtLeast;
    std::optional<i32> AllyHpAtMost;
    std::optional<i32> AllyCountAtMost;
    std::optional<i32> AllyCountAtLeast;
    std::string SelfStatus;
    std::string SelfStatusAbsent;
    std::string TargetStatus;
    std::string TargetStatusAbsent;
    std::optional<i32> TargetGuardAtLeast;
    std::optional<i32> SelfHardenAtLeast;
    std::optional<i32> Phase;
    std::string Next;
};

// The first matching rule wins. A skill that is the pattern head consumes the step.
// Any other skill is an insert and leaves the step where it is.
struct AiRule {
    AiCondition If;
    AiAction Action = AiAction::Pattern;
    std::string Skill;
};

// One entry of an enemy's drops list. Item is an item id. Message, when set, is the
// victory log line. An empty Message uses "Found <item name>."
struct DropDef {
    std::string Item;
    std::string Message;
};

struct EnemyDef {
    std::string Id;
    std::string Name;
    std::string Description;
    i32 Level = 1;
    StatBlock Stats;
    std::vector<SkillGrant> Skills; // active grants; each is its own copy
    i32 Experience = 0;
    i32 Gold = 0;
    bool Boss = false;     // the roster has one boss, last in the sample
    bool Unique = false;   // at most one of this id in an encounter; other ids may set it too
    bool HasColor = false; // when set, the fight draws a square in Red/Green/Blue
    u8 Red = 0;
    u8 Green = 0;
    u8 Blue = 0;
    std::vector<std::string> Pattern; // repeating skill ids; empty picks the strongest hit
    std::vector<AiRule> Ai;           // ordered interrupts; empty leaves the pattern in charge
    i32 PhaseAtPercent = 0;           // 0 means one phase. Otherwise phase 2 begins at this health
    std::string PhaseStatus;          // applied once when phase 2 begins
    // In phase 2, every SummonEvery actions, this enemy calls SummonId into the fight.
    // Empty SummonId means no add. A second copy is not called while one is still standing.
    // A corpse does not count as standing and does not hold a seat. IfBlocked is what
    // happens on the turn that cannot place the add. Fizzle is the refusal; act is the
    // ordinary pattern action for that turn.
    std::string SummonId;
    i32 SummonEvery = 0;
    BlockedSummon IfBlocked = BlockedSummon::Fizzle;
    // Granted when this foe is in a victorious fight. Empty means no drop.
    // A rarity with UniquePerType is claimed once; other drops can land again.
    // Message on a drop, when set, replaces the generic "Found <item name>." line.
    std::vector<DropDef> Drops;
};

// A fight. Enemies lists enemy ids, and the same id may appear more than once (a pack),
// except an enemy with Unique set, which can be listed once. Different unique enemies
// may share a fight.
// The sandbox walks this list. One id is still a normal single-enemy fight. Zone is the
// campaign stretch drawn above the foe name. Empty means that line is absent. The log
// never includes it.
struct EncounterDef {
    std::string Id;
    std::string Name;
    std::string Zone;
    std::vector<std::string> Enemies;
};

enum class GearSlot : u8 { Weapon, Offhand, Armor, Accessory, Consumable };

// One named rectangle in the icon sheet. x, y, width, and height are pixels of the sheet,
// origin at the top-left. The same rectangle is used for the accent sheet.
struct IconDef {
    std::string Id;
    i32 X = 0;
    i32 Y = 0;
    i32 Width = 0;
    i32 Height = 0;
};

// One row of rarities.json. Red, green, and blue are the #RRGGBB color: the accent layer is
// multiplied by it, and the item name is drawn in it. The display name is also appended on
// shop, bag, and worn rows. UniquePerType comes from the optional uniquePerType flag. A rarity
// with that flag may be used by at most one item of each slot. Slot is the item type: weapon,
// offhand, armor, accessory, or consumable. The item already names its own icon.
struct RarityDef {
    std::string Id;
    std::string Name;
    u8 Red = 0;
    u8 Green = 0;
    u8 Blue = 0;
    bool UniquePerType = false;
};

struct ItemDef {
    std::string Id;
    std::string Name;
    std::string Description;
    std::string Icon;                 // id in the icon sheet
    std::string Rarity;               // id in rarities.json
    GearSlot Slot = GearSlot::Weapon; // item type for UniquePerType
    StatBlock Bonus;
    i32 Value = 0;          // gold to buy one
    bool TwoHanded = false; // weapons only; fills the offhand as well
    i32 Heal = 0;           // consumable: flat health, not the skill formula
    i32 Focus = 0;          // consumable: flat focus, capped at the fighter's pool
    // Resolved grants from the optional "grants" list. Empty on the shipped gear. A worn
    // piece's grants join the class skills. A consumable cannot grant, and an item grants
    // at most one active skill (passives are not capped).
    std::vector<SkillGrant> Grants;
    // Foe ids from enemies.json. The shop withholds the item until every one of them has
    // been defeated. Empty means the item is always offered. The ids live in the item file.
    std::vector<std::string> RequiresDefeated;
};

// The shop, and the bag it sells into. BagSlots is how many stacks the hero can
// carry. StackSize is the most of one item a single stack holds. Selling pays
// value * SellNumerator / SellDenominator, rounded down.
struct ShopDef {
    std::string Id;
    std::string Name;
    std::string Keeper;
    // Dialogue id, a file under data/dialogue/. Empty means the shop opens straight away.
    std::string Dialogue;
    i32 BagSlots = 0;
    i32 StackSize = 1;
    i32 SellNumerator = 1;
    i32 SellDenominator = 1;
    std::vector<std::string> Stock;
};

// A flag expression. Negate flips the result. Tail empty means Head is a flag set by
// a node or a choice. Tail set means Head is a named id list and Tail is a member:
// found.alphas_fang is true while that id is in the list named found. The lists are
// supplied by the run. This struct does not know which list is drops or foes.
struct FlagExpr {
    bool Negate = false;
    std::string Head;
    std::string Tail;
};

// Where a choice goes. Next is another node in the same file. End closes the talk.
// Action is a verb id the game already knows how to perform, such as shop_open.
enum class TalkChoiceKind : u8 { Next, End, Action };

struct DialogueChoice {
    std::string Text;
    TalkChoiceKind Kind = TalkChoiceKind::Next;
    std::string Next;
    std::string Action;
    std::vector<FlagExpr> Requires;
    std::vector<std::string> Sets;
};

// One line of a talk. Note is tone for the writer and is not drawn. Line may contain
// '/' to break the box; the slash is not drawn. Requires gates this node. The first
// node in the file whose requirements hold is where the talk starts. Sets is applied
// when the node is entered.
struct DialogueNode {
    std::string Id;
    std::string Line;
    std::string Note;
    std::vector<FlagExpr> Requires;
    std::vector<std::string> Sets;
    std::vector<DialogueChoice> Choices;
};

// One NPC file under data/dialogue/. Id matches the file name.
struct DialogueDef {
    std::string Id;
    std::vector<DialogueNode> Nodes;

    [[nodiscard]] const DialogueNode* FindNode(std::string_view id) const;
};

// True when every foe in RequiresDefeated is in `defeated`. An item with an empty list is
// offered. The shop and the buy path both use this, so a locked row cannot be purchased.
[[nodiscard]] inline bool ItemIsOffered(const ItemDef& item, std::span<const std::string> defeated)
{
    for (const std::string& foe : item.RequiresDefeated) {
        bool down = false;
        for (const std::string& id : defeated) {
            if (id == foe)
                down = true;
        }
        if (!down)
            return false;
    }
    return true;
}

[[nodiscard]] inline i32 SellPrice(const ShopDef& shop, i32 value)
{
    if (value <= 0 || shop.SellNumerator <= 0 || shop.SellDenominator <= 0)
        return 0;
    const i64 price = static_cast<i64>(value) * shop.SellNumerator / shop.SellDenominator;
    if (price > 999999)
        return 999999;
    return static_cast<i32>(price);
}

[[nodiscard]] const char* ToString(SkillKind kind);
[[nodiscard]] const char* ToString(SkillTarget target);
[[nodiscard]] const char* ToString(EffectAim aim);
[[nodiscard]] const char* ToString(EffectTrigger trigger);
[[nodiscard]] const char* ToString(EffectOp op);
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

[[nodiscard]] inline i32 StatTotal(const StatBlock& block)
{
    return block.Vitality + block.Strength + block.Defense + block.Agility + block.Focus;
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

inline const char* ToString(EffectAim aim)
{
    if (aim == EffectAim::Self)
        return "self";
    if (aim == EffectAim::AllEnemies)
        return "allEnemies";
    if (aim == EffectAim::RandomEnemy)
        return "randomEnemy";
    if (aim == EffectAim::LowestHpEnemy)
        return "lowestHpEnemy";
    return "target";
}

inline const char* ToString(EffectTrigger trigger)
{
    if (trigger == EffectTrigger::Always)
        return "always";
    if (trigger == EffectTrigger::OnUse)
        return "onUse";
    if (trigger == EffectTrigger::OnHit)
        return "onHit";
    if (trigger == EffectTrigger::OnBlock)
        return "onBlock";
    if (trigger == EffectTrigger::OnTurnStart)
        return "onTurnStart";
    return "onDamaged";
}

inline const char* ToString(EffectOp op)
{
    if (op == EffectOp::Heal)
        return "heal";
    if (op == EffectOp::Guard)
        return "guard";
    if (op == EffectOp::Reflect)
        return "reflect";
    if (op == EffectOp::Empower)
        return "empower";
    if (op == EffectOp::Harden)
        return "harden";
    if (op == EffectOp::Stat)
        return "stat";
    if (op == EffectOp::StatPerMissingHp)
        return "statPerMissingHp";
    if (op == EffectOp::GrantFocus)
        return "grantFocus";
    if (op == EffectOp::ApplyStatus)
        return "applyStatus";
    if (op == EffectOp::StunIfWindingUp)
        return "stunIfWindingUp";
    return "damage";
}

inline const EffectDef* SkillDef::FindEffect(std::string_view id) const
{
    for (const EffectDef& effect : Effects) {
        if (effect.Id == id)
            return &effect;
    }
    return nullptr;
}

inline const DialogueNode* DialogueDef::FindNode(std::string_view id) const
{
    for (const DialogueNode& node : Nodes) {
        if (node.Id == id)
            return &node;
    }
    return nullptr;
}

inline const char* ToString(GearSlot slot)
{
    if (slot == GearSlot::Offhand)
        return "offhand";
    if (slot == GearSlot::Armor)
        return "armor";
    if (slot == GearSlot::Accessory)
        return "accessory";
    if (slot == GearSlot::Consumable)
        return "consumable";
    return "weapon";
}

} // namespace Ascension
