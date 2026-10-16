#pragma once

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/Database.h"

namespace Ascension {

enum class CombatantSide : u8 { Player, Enemy };

enum class BattleResult : u8 { Ongoing, Victory, Defeat };

// One side of a fight. Stat passives are already in Stats. WardPercent stays for the whole
// fight. Guard and empower each last for one incoming or outgoing blow. WeakenPercent is the
// reduction on this side's next damaging blow. RefundOnHit is focus gained when a blow actually
// deals damage to this side.
struct CombatantState {
    std::string Id;
    std::string Name;
    StatBlock Stats;
    i32 Hp = 0;
    i32 MaxHp = 0;
    i32 Focus = 0;
    i32 MaxFocus = 0;
    i32 GuardPercent = 0;
    i32 EmpowerPercent = 0;
    i32 WardPercent = 0;
    i32 WeakenPercent = 0;
    i32 RefundOnHit = 0;

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

// One practiced skill. Rank 1 is untrained. Battle adds (Rank - 1) * powerPerRank to damage
// and healing for the player only; a guard's percent is left alone.
struct SkillRank {
    std::string Id;
    i32 Rank = 1;
};

// The attack term after empower, minus defense, then raised to MinDamage. Guard is applied
// afterwards by GuardedDamage, which may reduce the hit to zero.
struct StrikeQuery {
    i32 Power = 0;
    i32 ScalingStat = 0;
    i32 Defense = 0;
    i32 EmpowerPercent = 0;
    i32 BloodiedPercent = 0;
};

// Power percent at empty health, and 0 at full health. Linear in the health that is gone.
[[nodiscard]] i32 BloodiedPercent(i32 power, i32 hp, i32 maxHp);

[[nodiscard]] i32 StrikeDamage(const StrikeQuery& hit, const CombatRules& rules);
[[nodiscard]] i32 HealAmount(i32 power, i32 scalingStat, const CombatRules& rules);
[[nodiscard]] i32 GuardedDamage(i32 damage, i32 guardPercent);

// Who acts next. Battle never sorts agility itself: a later active-time or cooldown model
// replaces this and the damage math stays put.
class TurnModel {
public:
    TurnModel() = default;
    TurnModel(const TurnModel&) = delete;
    TurnModel& operator=(const TurnModel&) = delete;
    virtual ~TurnModel() = default;

    virtual void Reset() = 0;
    // Both sides are alive. Repeated calls return the same side until Advance.
    virtual CombatantSide Next(i32 playerAgility, i32 enemyAgility) = 0;
    virtual void Advance() = 0;
};

// One action each per round. Higher agility goes first. An equal agility gives the player
// the first action.
class AgilityRoundModel final : public TurnModel {
public:
    void Reset() override;
    CombatantSide Next(i32 playerAgility, i32 enemyAgility) override;
    void Advance() override;

private:
    std::vector<CombatantSide> m_Round;
    usize m_Index = 0;
    bool m_Open = false;
};

// One player class against one enemy. Skill ids are kept, not pointers, so a hot reload can
// replace the database underneath. Pass TurnModel in; AgilityRoundModel is the sandbox default.
class Battle {
public:
    Battle(const Database& data, std::unique_ptr<TurnModel> turns);

    bool Start(std::string_view classId, i32 level, std::string_view enemyId, std::string& error);

    // A successful reload of the JSON. Keeps the wound, the spent focus and whose action it
    // is. If the class or the enemy is gone, the fight is left unchanged.
    bool ApplyData(std::string& error);

    [[nodiscard]] const CombatantState& Player() const { return m_Player; }
    [[nodiscard]] const CombatantState& Enemy() const { return m_Enemy; }
    [[nodiscard]] CombatantSide Actor();
    [[nodiscard]] BattleResult Result() const { return m_Result; }
    [[nodiscard]] i32 Level() const { return m_Level; }
    [[nodiscard]] std::span<const std::string> PlayerSkillIds() const { return m_PlayerSkills; }

    // Empty ranks and a zero bonus leave every skill at the power in skills.json, which is
    // what the combat tests use. The sandbox passes the hero's ranks before each blow.
    void SetPlayerRanks(i32 powerPerRank, std::vector<SkillRank> ranks);

    // Clamps to 1..MaxHp. The sandbox never calls this; tests place a wound so a passive that
    // reads health can be checked at a known total.
    void SetPlayerHp(i32 hp);

    ActionResult UseSkill(std::string_view skillId);
    ActionResult Wait();
    ActionResult EnemyAct();

private:
    bool Build(std::string& error, bool keepWounds);
    ActionResult Reject(std::string text) const;
    ActionResult Resolve(CombatantState& user, CombatantState& other, const SkillDef& skill);
    const SkillDef* ChooseEnemySkill() const;
    void FinishIfDead();
    void ApplyPassives(CombatantState& player) const;
    [[nodiscard]] i32 PlayerPower(const SkillDef& skill) const;
    [[nodiscard]] i32 BloodiedFor(const CombatantState& user) const;

    const Database* m_Data = nullptr;
    std::unique_ptr<TurnModel> m_Turns;
    std::string m_ClassId;
    std::string m_EnemyId;
    i32 m_Level = 1;
    CombatantState m_Player;
    CombatantState m_Enemy;
    std::vector<std::string> m_PlayerSkills;
    std::vector<std::string> m_EnemySkills;
    CombatRules m_Rules;
    std::vector<SkillRank> m_PlayerRanks;
    i32 m_PowerPerRank = 0;
    BattleResult m_Result = BattleResult::Ongoing;
    bool m_Started = false;
};

} // namespace Ascension
