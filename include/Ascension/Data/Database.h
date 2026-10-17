#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/Types.h"

namespace Ascension {

// One problem from a load or a reload. Message names the file and the field, for example
// "classes.json: class 'berserker': skill 'smash' does not exist".
struct DataError {
    std::string Message;
};

// The JSON files in a data directory (stats, effects, skills, statuses, classes, enemies, items,
// shop, combat, progress), checked against each other. Load once, then call CheckForChanges from
// the frame loop: a file that changed, and then stayed unchanged for one more check, is read
// again. A failed read keeps the previous definitions and sets GetLastError().
//
// Pointers returned by Find* and by ActiveSkills / Passives point into this database. Look them
// up again after a reload; do not keep them across a frame.
class Database {
public:
    // `directory` is where the JSON files live (relative paths are made absolute).
    explicit Database(std::filesystem::path directory);

    // Reads every file. On failure the previous good data stays, GetLastError() is set, and
    // `error` (when not null) receives the same message.
    bool Load(DataError* error = nullptr);

    // One hot-reload check, right here. A change is applied once the new modification time has
    // been seen twice in a row, so a file that is still being written is left alone. Returns 1
    // when the data was replaced, otherwise 0 (including a failed read).
    u32 CheckForChanges(DataError* error = nullptr);

    [[nodiscard]] bool IsLoaded() const { return m_Revision > 0; }
    // Starts at 0. Goes up by one on every successful load, so a screen can see that the file
    // changed even when the numbers it draws happen to match.
    [[nodiscard]] u32 GetRevision() const { return m_Revision; }
    [[nodiscard]] const std::filesystem::path& GetDirectory() const { return m_Directory; }
    // Empty after a successful load. Set when the last attempt failed.
    [[nodiscard]] const std::string& GetLastError() const { return m_LastError; }

    [[nodiscard]] std::span<const StatDef> GetStats() const { return m_Stats; }
    [[nodiscard]] std::span<const SkillDef> GetSkills() const { return m_Skills; }
    [[nodiscard]] std::span<const StatusDef> GetStatuses() const { return m_Statuses; }
    [[nodiscard]] std::span<const ClassDef> GetClasses() const { return m_Classes; }
    [[nodiscard]] std::span<const EnemyDef> GetEnemies() const { return m_Enemies; }
    [[nodiscard]] std::span<const ItemDef> GetItems() const { return m_Items; }
    [[nodiscard]] const ShopDef& GetShop() const { return m_Shop; }
    [[nodiscard]] const CombatRules& GetRules() const { return m_Rules; }
    [[nodiscard]] const ProgressRules& GetProgress() const { return m_Progress; }

    [[nodiscard]] const StatDef* FindStat(std::string_view id) const;
    [[nodiscard]] const SkillDef* FindSkill(std::string_view id) const;
    [[nodiscard]] const StatusDef* FindStatus(std::string_view id) const;
    [[nodiscard]] const ClassDef* FindClass(std::string_view id) const;
    [[nodiscard]] const EnemyDef* FindEnemy(std::string_view id) const;
    [[nodiscard]] const ItemDef* FindItem(std::string_view id) const;

    // Evolutions whose EvolvesFrom is `baseId`, in file order.
    [[nodiscard]] std::vector<const ClassDef*> EvolutionsOf(std::string_view baseId) const;

    // Base classes marked starter, in file order. Reset offers this list, not a hard-coded class.
    [[nodiscard]] std::vector<const ClassDef*> Starters() const;

    // Active skills this class has, parent first, then the ones it adds. Each pointer is that
    // grant's own resolved copy (the catalog skill, then the grant's tune). FindSkill returns
    // the catalog entry, which has no grant tune. Empty when the class grants none.
    [[nodiscard]] std::vector<const SkillDef*> ActiveSkills(const ClassDef& klass) const;
    [[nodiscard]] std::vector<const SkillDef*> Passives(const ClassDef& klass) const;

    // Stats at `level` (clamped to 1..99). A base class is Stats + Growth * (level - 1). An
    // evolution below its EvolveLevel is still the parent. At the evolve level and after, it is
    // the parent's stats at EvolveLevel, plus Bonus, plus Growth for each level past that.
    [[nodiscard]] StatBlock StatsAt(const ClassDef& klass, i32 level) const;

private:
    struct FileWatch {
        std::filesystem::path Path;
        std::optional<std::filesystem::file_time_type> Loaded;
        std::optional<std::filesystem::file_time_type> Seen;
        bool Waiting = false;
    };

    void NoteFailure(const DataError& error, DataError* out);
    void NoteSuccess(DataError* out);
    void ApplyTimes(const std::vector<std::optional<std::filesystem::file_time_type>>& times);

    std::filesystem::path m_Directory;
    std::vector<FileWatch> m_Watches;
    std::vector<StatDef> m_Stats;
    std::vector<SkillDef> m_Skills;
    std::vector<StatusDef> m_Statuses;
    std::vector<ClassDef> m_Classes;
    std::vector<EnemyDef> m_Enemies;
    std::vector<ItemDef> m_Items;
    ShopDef m_Shop;
    CombatRules m_Rules;
    ProgressRules m_Progress;
    std::string m_LastError;
    u32 m_Revision = 0;
};

} // namespace Ascension
