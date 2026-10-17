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
};

// The Fighter's career: XP, level, skill uses, and the evolution they picked. Skill ids and the
// class id are stored, not pointers, so a reload of the database does not dangle. Rank and level
// are computed from the current progress.json whenever they are read.
class Hero {
public:
    // Starts as the level-1 fighter with no XP. Fails if that class is not in `data`.
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

    // Active skills of the current class, parent first. Empty if the class is gone.
    [[nodiscard]] std::vector<std::string> SkillIds() const;

    // True while this is still a base class and at least one of its evolutions is offered
    // at the current level. File order, which is the order of the number keys.
    [[nodiscard]] bool ChoicePending() const;
    [[nodiscard]] std::vector<std::string> ChoiceIds() const;

    // Become that evolution. The skills already practiced keep their uses. Fails if no choice
    // is open or `evolutionId` is not one of them.
    bool Choose(std::string_view evolutionId, std::string& error);

    // Clears practiced skill uses, which is what ranks are computed from. Succeeds only while
    // an evolution choice is open, and that screen is the only place it is offered. The class,
    // the XP, and the level stay. Nothing else clears these uses.
    bool ResetBuild(std::string& error);

    // The line to show when the next fight must wait for an evolution choice. Null when a
    // fight may start.
    [[nodiscard]] const char* NextFightBlock() const;

    // Roster slot after the next-foe key. `count` is the roster length. While an evolution
    // choice blocks the next fight, `index` is returned unchanged, including across a wrap.
    [[nodiscard]] i32 IndexAfterNextFoe(i32 index, i32 count) const;

    // Adds XP (negative amounts are ignored) and may cross several levels at once.
    XpGrant GrantXp(i32 amount);

private:
    const Database* m_Data = nullptr;
    std::string m_ClassId;
    i32 m_Xp = 0;
    std::vector<std::string> m_Practiced;
    std::vector<i32> m_Uses;
};

} // namespace Ascension
