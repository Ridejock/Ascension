#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/Database.h"

namespace Ascension {

enum class CombatantSide : u8 { Player, Enemy };

// Fled is a map fight the hero left. The foe is not dead and the hero is not.
// A list fight never produces it: flee stays off unless the map hands it on.
enum class BattleResult : u8 { Ongoing, Victory, Defeat, Fled };

// One applied status. TurnsLeft counts down at the start of this unit's turn, after the
// per-turn damage and after a skip is noticed.
struct StatusInstance {
    std::string Id;
    i32 Stacks = 1;
    i32 TurnsLeft = 0;
};

// One side of a fight. Flat stat effects are already in Stats. Guard, reflect, and empower each
// last for one incoming or outgoing blow. BonusDefense is a harden that ticks down on this
// unit's turn. WindupSkill is set while an attack is being telegraphed; WindupLeft is the
// telegraph actions still owed after the one on screen. A block runs the defender's onBlock
// effects; nothing on this struct is named after a skill.
struct CombatantState {
    std::string Id;
    std::string Name;
    StatBlock Stats;
    i32 Hp = 0;
    i32 MaxHp = 0;
    i32 Focus = 0;
    i32 MaxFocus = 0;
    i32 Mana = 0;
    i32 MaxMana = 0;
    i32 GuardPercent = 0;
    i32 EmpowerPercent = 0;
    i32 ReflectPercent = 0;
    i32 ReflectFlat = 0;
    i32 BonusDefense = 0;
    i32 BonusDefenseTurns = 0;
    std::vector<StatusInstance> Statuses;
    std::string WindupSkill;
    i32 WindupLeft = 0;
    i32 WindupTotal = 0;
    i32 PatternStep = 0;
    i32 Phase = 1;
    i32 PhaseTurns = 0;
    bool ShardDue = false;
    std::vector<std::string> SkillUseIds;
    std::vector<i32> SkillUseCounts;

    [[nodiscard]] bool Alive() const { return Hp > 0; }
};

struct ActionResult {
    bool Accepted = false;
    std::string Text;
    std::string SkillId;
    i32 Damage = 0;
    i32 Healing = 0;
    BattleResult Result = BattleResult::Ongoing;
};

// One practiced skill. Rank 1 is untrained. Battle adds (Rank - 1) * powerPerRank to damage,
// healing, and a reflect's flat power for the player only. A guard's percent is left alone.
struct SkillRank {
    std::string Id;
    i32 Rank = 1;
};

// The attack term after empower, minus defense, then raised to MinDamage. Guard, dealt and
// taken percents, and a riposte are applied afterwards.
struct StrikeQuery {
    i32 Power = 0;
    i32 ScalingStat = 0;
    i32 Defense = 0;
    i32 EmpowerPercent = 0;
};

// +amount per `stepPercent` of missing health, capped at maxBonus. 0 at full health. This is
// the statPerMissingHp op.
[[nodiscard]] i32 MissingHpBonus(i32 amount, i32 stepPercent, i32 maxBonus, i32 hp, i32 maxHp);

// value * percent / 100. A percent of 100 leaves value alone. Negative value becomes 0.
[[nodiscard]] i32 ScalePercent(i32 value, i32 percent);

[[nodiscard]] i32 StrikeDamage(const StrikeQuery& hit, const CombatRules& rules);
[[nodiscard]] i32 HealAmount(i32 power, i32 scalingStat, const CombatRules& rules);
[[nodiscard]] i32 GuardedDamage(i32 damage, i32 guardPercent);

// blocked * reflectPercent / 100 + flat. The flat is the reflect effect's power plus its
// scaling stat.
[[nodiscard]] i32 ReflectedDamage(i32 blocked, i32 reflectPercent, i32 flat);

// Reapply refreshes the duration and adds a stack up to MaxStacks.
void ApplyStatus(CombatantState& who, const StatusDef& status);

// True when a status `who` has up lists `statusId` in its resists.
[[nodiscard]] bool ResistsStatus(const CombatantState& who, std::string_view statusId,
                                 std::span<const StatusDef> catalog);

struct StatusTick {
    i32 Bleed = 0;
    bool Skip = false;
};

// Deals bleed from the stacks that are up, notes a skip, then counts every status down.
[[nodiscard]] StatusTick TickStatuses(CombatantState& who, std::span<const StatusDef> catalog);
[[nodiscard]] i32 StatusDealtPercent(const CombatantState& who, std::span<const StatusDef> catalog);
[[nodiscard]] i32 StatusTakenPercent(const CombatantState& who, std::span<const StatusDef> catalog);

// One combatant in the turn order. Index is the enemy slot; the player's index is 0 and is
// not read. Agility is what the round model sorts on.
struct TurnSeat {
    CombatantSide Side = CombatantSide::Player;
    i32 Index = 0;
    i32 Agility = 0;
};

// Who acts next. Battle never sorts agility itself: a later active-time or cooldown model
// replaces this and the damage math stays put.
class TurnModel {
public:
    TurnModel() = default;
    TurnModel(const TurnModel&) = delete;
    TurnModel& operator=(const TurnModel&) = delete;
    virtual ~TurnModel() = default;

    virtual void Reset() = 0;
    // `living` is every combatant still up. Repeated calls return the same seat until Advance.
    // A seat that is no longer in `living` is skipped.
    virtual TurnSeat Next(std::span<const TurnSeat> living) = 0;
    virtual void Advance() = 0;
    // The seats of this round and the index of the seat that acts next. Place puts a saved
    // round back, so the next Next() is that seat and nobody has acted.
    virtual void Capture(std::vector<TurnSeat>& order, i32& index) const = 0;
    virtual void Place(std::span<const TurnSeat> order, i32 index) = 0;
    // Drops this seat from the open round. A summon that replaces a corpse keeps that
    // index, and the round may still list it. The new body does not act on the dead foe's
    // remaining turn. The next round, built from whoever is standing, includes it.
    // A model that does not keep a round leaves this empty.
    virtual void ReleaseSeat(CombatantSide, i32) {}
};

// One action each per round. Higher agility goes first. An equal agility gives the player
// the first action, then enemies in encounter order.
class AgilityRoundModel final : public TurnModel {
public:
    void Reset() override;
    TurnSeat Next(std::span<const TurnSeat> living) override;
    void Advance() override;
    void Capture(std::vector<TurnSeat>& order, i32& index) const override;
    void Place(std::span<const TurnSeat> order, i32 index) override;
    void ReleaseSeat(CombatantSide side, i32 index) override;

private:
    void Open(std::span<const TurnSeat> living);

    std::vector<TurnSeat> m_Round;
    usize m_Index = 0;
    bool m_Open = false;
};

// The part of one combatant that a save has to put back. Stats and max pools are rebuilt
// from data. Statuses keep the stacks and the turns still left.
struct CombatantSnapshot {
    std::string Id;
    i32 Hp = 0;
    i32 Focus = 0;
    // Negative means the save did not store mana (a file from before the pool). Import
    // fills that unit to its max. Zero is an empty pool.
    i32 Mana = 0;
    i32 GuardPercent = 0;
    i32 EmpowerPercent = 0;
    i32 ReflectPercent = 0;
    i32 ReflectFlat = 0;
    i32 BonusDefense = 0;
    i32 BonusDefenseTurns = 0;
    std::vector<StatusInstance> Statuses;
    std::string WindupSkill;
    i32 WindupLeft = 0;
    i32 WindupTotal = 0;
    i32 PatternStep = 0;
    i32 Phase = 1;
    i32 PhaseTurns = 0;
    bool ShardDue = false;
    std::vector<std::string> SkillUseIds;
    std::vector<i32> SkillUseCounts;
};

// One ongoing fight. Known is false when the save only knows that a fight was in progress,
// and that fight starts again from the encounter. Order is whose turn it is: index OrderIndex
// acts next, and TurnCount is how many actions have already been taken.
struct FightSnapshot {
    bool Known = false;
    i32 TurnCount = 0;
    CombatantSide Actor = CombatantSide::Player;
    i32 ActorIndex = -1;
    i32 Target = 0;
    i32 RandomCursor = 0;
    bool UpkeepDone = false;
    std::vector<TurnSeat> Order;
    i32 OrderIndex = 0;
    CombatantSnapshot Player;
    std::vector<CombatantSnapshot> Foes;
};

// Pools a fight starts from. Hp is clamped to 0..MaxHp. 0 is a defeat before anyone
// acts. Focus is clamped to 0..MaxFocus and mana to 0..MaxMana. The Start that does not
// take this fills all three, which is a lone fight and the single-encounter harness.
struct OpeningPools {
    i32 Hp = 0;
    i32 Focus = 0;
    i32 Mana = 0;
};

// One player class against an encounter (one enemy, or a pack). Skill ids are kept, not
// pointers, so a hot reload can replace the database underneath. Pass TurnModel in;
// AgilityRoundModel is the sandbox default.
class Battle {
public:
    Battle(const Database& data, std::unique_ptr<TurnModel> turns);

    bool Start(std::string_view classId, i32 level, std::string_view enemyId, std::string& error);

    // Same fight, starting at `pools` instead of full. A win's leftover HP and focus are
    // what the caller passes the next time.
    bool Start(std::string_view classId, i32 level, std::string_view enemyId, OpeningPools pools,
               std::string& error);

    // The same encounter name and rules, seating `foeIds` instead of the encounter's
    // whole list. The map passes the ids a contact seated. When that list is the
    // encounter's own list, the other Start is the same fight. Empty fails and leaves
    // the current fight alone.
    bool Start(std::string_view classId, i32 level, std::string_view encounterId,
               std::span<const std::string> foeIds, OpeningPools pools, std::string& error);

    // A successful reload of the JSON. Keeps the wound, the spent focus and whose action it
    // is. If the class or the enemy is gone, the fight is left unchanged.
    bool ApplyData(std::string& error);

    [[nodiscard]] const CombatantState& Player() const { return m_Player; }
    // The selected enemy. After the fight starts this is one of Enemies().
    [[nodiscard]] const CombatantState& Enemy() const;
    [[nodiscard]] std::span<const CombatantState> Enemies() const { return m_Enemies; }
    [[nodiscard]] i32 TargetIndex() const { return m_Target; }
    // False when `index` is outside the pack or that enemy is dead.
    bool SetTarget(i32 index);
    // Steps through the pack. Dead enemies are skipped. `delta` is -1 or 1.
    void CycleTarget(i32 delta);
    [[nodiscard]] CombatantSide Actor();
    // Enemy slot of the combatant whose turn it is, or -1 when it is the player's turn.
    [[nodiscard]] i32 ActorIndex();
    // The next `count` turns, the actor who goes now first. Empty when the fight is over.
    // The round model supplies the order. A later model that has no order leaves this empty.
    void FillTurnPreview(std::vector<TurnSeat>& out, i32 count);
    [[nodiscard]] BattleResult Result() const { return m_Result; }
    [[nodiscard]] i32 Level() const { return m_Level; }
    [[nodiscard]] const std::string& EncounterName() const { return m_EncounterName; }
    // Sum of every enemy in the fight, including a shard that has already joined.
    [[nodiscard]] i32 Experience() const;
    [[nodiscard]] i32 Gold() const;
    // One line of the live fight: player health, focus, bloodlust, statuses, and each foe's
    // health and harden. Empty before the fight starts. --trace prints it beside each action.
    [[nodiscard]] std::string TraceState() const;
    [[nodiscard]] std::span<const std::string> PlayerSkillIds() const { return m_PlayerSkills; }

    // Empty ranks and a zero bonus leave every skill at the power in skills.json, which is
    // what the combat tests use. The sandbox passes the hero's ranks before each blow.
    void SetPlayerRanks(i32 powerPerRank, std::vector<SkillRank> ranks);

    // Worn gear, added to the class stats before passives. Call it before Start, and again
    // before ApplyData when a reload changes a worn piece. Zero leaves the class line alone.
    void SetGear(StatBlock gear);
    // Worn item ids, in slot order, whose grants join the class skills. Same timing as
    // SetGear: the next Start, and ApplyData, read them. Ids are kept, so a reload resolves
    // them again; an id that is gone adds nothing.
    void SetWornItems(std::vector<std::string> itemIds);
    // Skill ids the hero kept from the class's picks. Same timing as SetWornItems. An id the
    // class does not offer adds nothing.
    void SetPickedSkills(std::vector<std::string> skillIds);

    // Replaces the class stat line (level growth and class grants) before gear and passives.
    // The sandbox passes the hero's attributes. Leave it unset and Start keeps the class formula.
    void SetBaseStats(StatBlock stats);

    // The name drawn and logged for the hero. Empty keeps the class name, which is what
    // a save from before names, and a fight that never called this, already show.
    // Call it before Start, and again before ApplyData. The next Build reads it.
    void SetHeroLabel(std::string_view name) { m_HeroLabel = std::string(name); }

    // Drinks a consumable on the player's turn. Accepted with SkillId set means the drink
    // landed and the bag should drop one. A stun spends the turn and leaves SkillId empty,
    // so the bottle stays in the bag.
    ActionResult UseItem(const ItemDef& item);

    // Clamps to 1..MaxHp. The sandbox never calls this; tests place a wound so a passive that
    // reads health can be checked at a known total. A resumed fight uses it for the saved wound.
    void SetPlayerHp(i32 hp);

    // Clamps to 0..MaxFocus. A resumed fight restores the saved pool with this.
    void SetPlayerFocus(i32 focus);

    // Clamps to 0..MaxMana. A resumed fight restores the saved pool with this.
    void SetPlayerMana(i32 mana);

    // Clamps the selected enemy to 0..MaxHp. 0 may end the fight. Crossing that enemy's phase
    // threshold starts phase 2.
    void SetEnemyHp(i32 hp);

    // Marks a started fight as already won. Foes drop to 0 HP. XP and gold are not granted.
    // A saved map win that is still choosing a path uses this so the choice screen and the
    // settle see a victory, without paying the fight again.
    void HoldVictory();

    // The live fight, including each foe's wound, statuses, pattern step, and whose action
    // it is. Empty (Known false) before the fight starts or after it ends.
    FightSnapshot ExportFight();
    // After Start. Puts the snapshot back and does not take an action. False leaves the
    // fight as Start built it.
    bool ImportFight(const FightSnapshot& shot, std::string& error);

    // Applies a status from statuses.json. Tests place one before a blow.
    bool Inflict(CombatantSide side, std::string_view statusId, std::string& error);

    // Applies a status to that enemy seat. The map calls it after Start, before anyone
    // acts, with the statuses sampled when the fight was offered. A status the foe
    // resists does not land, and that is still success. An unknown id or an index
    // outside the pack fails and does not change the fight.
    bool GrantStatus(i32 foeIndex, std::string_view statusId, std::string& error);

    ActionResult UseSkill(std::string_view skillId);
    ActionResult Wait();

    // Ends the fight as Fled on the player's turn, when the map allowed it.
    // A boss fight (noFlee, or a foe with boss set) leaves the fight untouched.
    // Call SetFleeAllowed after Start. Start clears it, so a list fight cannot flee.
    void SetFleeAllowed(bool allowed);
    [[nodiscard]] bool FleeAllowed() const { return m_FleeAllowed; }
    ActionResult Flee();

    ActionResult EnemyAct();

private:
    struct TurnOpen {
        bool Continue = false;
        ActionResult Result;
    };

    bool StartFight(std::string_view classId, i32 level, std::string_view enemyId, bool usePools,
                    i32 hp, i32 focus, i32 mana, std::string& error);
    bool Build(std::string& error, bool keepWounds);
    ActionResult Reject(std::string text) const;
    void FinishIfDead();
    void AdvanceTurn();
    void PrefixOpening(ActionResult& result);
    TurnOpen PrepareTurn(CombatantState& user);
    struct AimRef {
        bool Player = false;
        i32 Index = 0;
    };
    // A status a damage effect spent on one aim. It stays up until the last onHit of that use.
    struct StatusSpend {
        AimRef Aim;
        std::string StatusId;
    };
    struct HitLanded {
        i32 Taken = 0;
        i32 Reflected = 0;
    };

    ActionResult Resolve(CombatantState& user, const SkillDef& skill);
    struct SummonSpot {
        const EnemyDef* Add = nullptr;
        std::string HostName;
        bool Already = false;
        i32 Living = 0;
        i32 OpenSlot = -1;
    };
    [[nodiscard]] SummonSpot FindSummonSpot(i32 index) const;
    // False when the add cannot join and this enemy's ifBlocked is act, so the caller
    // spends the turn on the ordinary pattern instead.
    [[nodiscard]] bool SummonTakesTurn(i32 index) const;
    ActionResult ResolveSummon(i32 index);
    void MakeFoe(const EnemyDef& def, CombatantState& foe) const;
    void CollectAims(const CombatantState& user, EffectAim aim, std::vector<AimRef>& out);
    [[nodiscard]] EffectAim AimOf(const SkillDef& skill, const EffectDef& effect) const;
    [[nodiscard]] CombatantState& Aimed(const AimRef& aim);
    // A damage effect with no IfTargetHas always lands. Otherwise the aim must hold it.
    [[nodiscard]] static bool EffectLands(const EffectDef& effect, const CombatantState& foe);
    // Index of `aim` in `aims`, or aims.size() when it is not there.
    [[nodiscard]] static usize AimSlot(std::span<const AimRef> aims, const AimRef& aim);
    [[nodiscard]] static bool Spends(std::span<const StatusSpend> spends, const AimRef& aim,
                                     std::string_view statusId);
    [[nodiscard]] std::vector<TurnSeat> Living() const;
    [[nodiscard]] TurnSeat CurrentSeat();
    void Retarget();

    // The pattern skill the foe would take next, without moving the step. Index is that
    // skill's place in the pattern. Skill is null when the pattern has nothing to spend.
    struct PatternHead {
        const SkillDef* Skill = nullptr;
        i32 Index = -1;
    };
    [[nodiscard]] PatternHead PeekPattern(const CombatantState& who, const EnemyDef& enemy) const;
    const SkillDef* TakeEnemySkill(CombatantState& who);
    const SkillDef* PlayerSkill(std::string_view skillId) const;
    const SkillDef* EnemySkill(const CombatantState& who, std::string_view skillId) const;
    void CollectGranted(bool hero, std::string_view enemyId,
                        std::vector<const SkillDef*>& out) const;
    void ApplyAlways(CombatantState& who, bool hero) const;
    void RunTrigger(CombatantState& who, EffectTrigger trigger) const;
    void ArmSummon(i32 index, std::string& note);
    void EnterPhase(i32 index, std::string& note);
    [[nodiscard]] i32 RankedPower(const SkillDef& skill, i32 base) const;
    [[nodiscard]] i32 MissingFor(const CombatantState& user, std::string_view statId) const;
    [[nodiscard]] i32 DefenseOf(const CombatantState& who) const;
    [[nodiscard]] i32 SkillScore(const SkillDef& skill) const;
    [[nodiscard]] bool DealsDamage(const SkillDef& skill) const;
    HitLanded LandHit(CombatantState& attacker, CombatantState& defender, i32 raw);
    i32 NoteSkillUse(CombatantState& who, const std::string& skillId);
    void GrantFocus(CombatantState& who, i32 amount) const;
    void GrantMana(CombatantState& who, i32 amount) const;
    [[nodiscard]] bool SpendsMana(const SkillDef& skill) const;
    [[nodiscard]] bool CanPay(const CombatantState& who, const SkillDef& skill) const;
    // ApplyStatus unless a status `who` has up resists it. False when it was resisted.
    bool Afflict(CombatantState& who, const StatusDef& status) const;

    const Database* m_Data = nullptr;
    std::unique_ptr<TurnModel> m_Turns;
    std::string m_ClassId;
    std::string m_HeroLabel;
    std::string m_EncounterId;
    // When set, Build seats these ids instead of the encounter's list. Empty means
    // the encounter's own list, which is every fight that did not come from a queue.
    std::vector<std::string> m_Roster;
    std::string m_EncounterName;
    std::string m_FallbackEnemy;
    i32 m_Level = 1;
    i32 m_BaseCount = 0;
    i32 m_Target = 0;
    i32 m_RandomCursor = 0;
    CombatantState m_Player;
    std::vector<CombatantState> m_Enemies;
    std::vector<std::string> m_PlayerSkills;
    CombatRules m_Rules;
    std::vector<SkillRank> m_PlayerRanks;
    i32 m_PowerPerRank = 0;
    StatBlock m_Gear;
    std::vector<std::string> m_WornItems;
    std::vector<std::string> m_PickedSkills;
    // Merged hero skills for this fight. Rebuilt in Build. Pointers into it last until the
    // next Build.
    std::vector<HeroGrant> m_HeroGrants;
    StatBlock m_BaseStats;
    bool m_HasBaseStats = false;
    BattleResult m_Result = BattleResult::Ongoing;
    bool m_FleeAllowed = false;
    bool m_Started = false;
    bool m_UpkeepDone = false;
    i32 m_TurnCount = 0;
    std::string m_Opening;
};

} // namespace Ascension
