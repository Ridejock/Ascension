#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/Database.h"

namespace Ascension {

// What GrantXp did. ChoiceOpened is true only on the grant that first makes an evolution
// available. Level is derived from XP and the curve, so a hot reload of progress.json moves it.
struct XpGrant {
    i32 Amount = 0;
    i32 LevelBefore = 1;
    i32 LevelAfter = 1;
    bool ChoiceOpened = false;
    bool PickOpened = false; // this grant reached a skill pick that was not open before
};

// One class's attribute grant, as it sits on the character. Id is the class that gave it.
// A reset returns every one of these to the unspent class-point pool.
struct AttributeSource {
    std::string Id;
    StatBlock Points;
};

// The career fields a save writes. Level is not stored: it is derived from XP and the curve.
// Custom is the build-reset flag. When it is set, LevelSteps and Sources are the stats; when
// it is clear, those lists are empty and the class formula is used instead.
struct CareerRecord {
    std::string ClassId;
    i32 Xp = 0;
    std::vector<std::string> Practiced;
    std::vector<i32> Uses;
    std::vector<std::string> Picked;
    bool Custom = false;
    bool ChoosingStarter = false;
    i32 ClassPool = 0;
    std::string SpendId;
    std::vector<StatBlock> LevelSteps;
    std::vector<AttributeSource> Sources;
};

// The Fighter's career: XP, level, skill uses, and the evolution they picked. Skill ids and the
// class id are stored, not pointers, so a reload of the database does not dangle. Rank and level
// are computed from the current progress.json whenever they are read.
//
// Attribute points come from two places. Level points are the growth earned on the way up, and
// a reset leaves them where they are. Class points are the grants in the class JSON (a base
// class's stats, an evolution's bonus), remembered per class. A reset, offered only while an
// evolution choice is open, returns those grants to one pool. The player then picks any starter
// class and spends the pool. The total does not change.
class Hero {
public:
    // Starts as the first starter class, at level 1 with no XP. Fails if `data` has none.
    bool Start(const Database& data, std::string& error);

    // The class id still has to exist. XP and uses are kept, so a new curve or uses-per-rank
    // changes the level and the ranks on the next read.
    bool ApplyData(std::string& error);

    [[nodiscard]] bool IsReady() const { return m_Data != nullptr && !m_ClassId.empty(); }
    [[nodiscard]] const std::string& ClassId() const { return m_ClassId; }
    [[nodiscard]] i32 Xp() const { return m_Xp; }
    [[nodiscard]] i32 Level() const;
    [[nodiscard]] i32 NextLevelXp() const;
    [[nodiscard]] bool AtMaxLevel() const;

    [[nodiscard]] i32 UsesOf(std::string_view skillId) const;
    [[nodiscard]] i32 RankOf(std::string_view skillId) const;
    // Counts one use. Returns the rank after this use (unchanged when `skillId` is unknown).
    i32 NoteUse(std::string_view skillId);

    // The rank this skill would have if the next accepted use counted. Unknown ids stay put.
    // The sandbox asks for this before the blow, so the use that crosses a rank deals the new
    // power.
    [[nodiscard]] i32 RankAfterUse(std::string_view skillId) const;

    // Active skills of the current class, parent first, then the kept pick options in pick
    // order. An upgrade of a class skill is not listed twice. Empty if the class is gone.
    [[nodiscard]] std::vector<std::string> SkillIds() const;

    // True when the class (or its parent) has a pick at or below the current level and none of
    // its options is kept yet. Picks resolve one at a time, lowest level first. Nothing is
    // offered while a starter class is still being picked.
    [[nodiscard]] bool PickPending() const;
    // The open pick's option skill ids, in file order (the order of the number keys).
    [[nodiscard]] std::vector<std::string> PickOptions() const;
    // Keep that option of the open pick. The other options are gone for this run.
    bool Pick(std::string_view skillId, std::string& error);
    // Kept option ids the current class still offers, in pick order. A reload that drops an
    // option drops it here too, and that pick opens again. Pass this to Battle::SetPickedSkills.
    [[nodiscard]] std::vector<std::string> PickedIds() const;

    // True while this is still a base class and at least one of its evolutions is offered
    // at the current level. File order, which is the order of the number keys.
    [[nodiscard]] bool ChoicePending() const;
    [[nodiscard]] std::vector<std::string> ChoiceIds() const;

    // Become that evolution. The skills already practiced keep their uses. Fails if no choice
    // is open, a starter is still being picked, or `evolutionId` is not one of the choices.
    // The evolution's bonus is recorded as its own attribute source.
    bool Choose(std::string_view evolutionId, std::string& error);

    // Clears every practiced skill use, including uses of a skill that only gear grants, every
    // kept skill pick, and returns every class attribute grant to the unspent pool. Level, XP,
    // and the points earned by leveling stay. Gear is not on the hero, so a reset does not
    // touch it. A pick the class still offers opens again after the new path is chosen.
    // Succeeds only while an evolution choice is open. The player then picks a starter with
    // ChooseStarter and spends the pool with SpendClassPoint. Nothing else clears skill uses.
    bool ResetBuild(std::string& error);

    // True after a reset, until a starter class is picked.
    [[nodiscard]] bool ChoosingStarter() const { return m_ChoosingStarter; }

    // Starter classes from the data, in file order.
    [[nodiscard]] std::vector<std::string> StarterIds() const;

    // Become that starter. The pooled class points are not replaced by its grant.
    bool ChooseStarter(std::string_view classId, std::string& error);

    // Become that starter, keeping XP and skill uses. Kept picks belong to the previous
    // class, so they are cleared. A custom build stays on ChooseStarter. Fails if the id
    // is not a starter. A playtest uses this so --class can name any starter.
    bool AdoptStarter(std::string_view classId, std::string& error);

    // Moves one pooled class point onto `statId` (vitality, strength, defense, agility, focus).
    bool SpendClassPoint(std::string_view statId, std::string& error);

    // Points earned by leveling. Kept across a reset.
    [[nodiscard]] i32 LevelPoints() const;

    // Class grants still allocated, plus the unspent pool.
    [[nodiscard]] i32 ClassPoints() const;

    // Level points plus class points. A reset does not change this.
    [[nodiscard]] i32 AttributePoints() const;

    // Class points waiting to be spent. Zero until a reset.
    [[nodiscard]] i32 ClassPool() const { return m_ClassPool; }

    // The class grants currently allocated. Empty once a reset has pooled them.
    [[nodiscard]] std::vector<AttributeSource> ClassSources() const;

    // The stats those points buy: level growth plus spent class grants. The pool is not
    // included until it is spent. Gear and passives are applied later, by the battle.
    [[nodiscard]] StatBlock Attributes() const;

    // The line to show when the next fight must wait for an evolution choice. Null when a
    // fight may start.
    [[nodiscard]] const char* NextFightBlock() const;

    // Roster slot after the next-foe key. `count` is the roster length. While an evolution
    // choice blocks the next fight, `index` is returned unchanged, including across a wrap.
    [[nodiscard]] i32 IndexAfterNextFoe(i32 index, i32 count) const;

    // Adds XP (negative amounts are ignored) and may cross several levels at once.
    XpGrant GrantXp(i32 amount);

    // Copies the career into `out`. The hero must already be started.
    void ExportCareer(CareerRecord& out) const;
    // Replaces the career. The hero must already be started on `data`, and ClassId must exist.
    // A damaged skill list fails and leaves the previous career in place only when the class
    // id is missing or the practiced list does not match its uses; otherwise the record is kept.
    bool ImportCareer(const CareerRecord& in, std::string& error);

private:
    void NoteLevelSteps();
    [[nodiscard]] StatBlock GrowthForStep(const ClassDef& klass, i32 step) const;
    [[nodiscard]] std::vector<StatBlock> LevelSteps() const;
    [[nodiscard]] std::vector<AttributeSource> DerivedSources() const;

    const Database* m_Data = nullptr;
    std::string m_ClassId;
    i32 m_Xp = 0;
    std::vector<std::string> m_Practiced;
    std::vector<i32> m_Uses;
    std::vector<std::string> m_Picked; // kept option ids, in the order they were picked
    // Set by a reset. Level steps and class sources are then remembered here instead of being
    // derived from the class formula, so a new starter does not rewrite the points already earned.
    bool m_Custom = false;
    bool m_ChoosingStarter = false;
    i32 m_ClassPool = 0;
    std::string m_SpendId;
    std::vector<StatBlock> m_LevelSteps;
    std::vector<AttributeSource> m_ClassSources;
};

} // namespace Ascension
