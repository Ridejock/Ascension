#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Ascension/Progress/Hero.h"
#include "Ascension/Progress/Inventory.h"

namespace Ascension {

// Where a saved run was. None is a run that had not started a fight. Ongoing is mid-fight.
// A snapshot in Fight restores that blow, including the foe and whose turn it is. An ongoing
// file with no snapshot starts the encounter over. Victory and defeat stay between fights
// so the same win is not awarded twice.
enum class RunStanding : u8 { None, Ongoing, Victory, Defeat };

// One run: the career, the bag, the foes already defeated, and the place in the encounter
// list. This is the current data version. Older files are migrated before they fill this.
struct RunState {
    CareerRecord Career;
    BagRecord Bag;
    std::vector<std::string> Defeated;
    // Drop item ids already claimed. A version 3 file with no list has claimed nothing.
    std::vector<std::string> Found;
    // Flags set by dialogue. A version 3 file with no list has set none.
    std::vector<std::string> Flags;
    i32 Position = 0;
    std::string DirectEnemy;
    RunStanding Standing = RunStanding::None;
    bool ResourcesKnown = false;
    i32 Hp = 0;
    i32 Focus = 0;
    FightSnapshot Fight;
};

// An ongoing fight keeps the hero's HP and focus. Victory and defeat do not, so the next
// fight starts full, the same as moving on without quitting. Hp and Focus stay at whatever
// the run already held; a fresh run leaves them at 0.
inline void RememberOutcome(RunState& run, BattleResult result, i32 hp, i32 focus)
{
    if (result == BattleResult::Victory) {
        run.Standing = RunStanding::Victory;
        return;
    }
    if (result == BattleResult::Defeat) {
        run.Standing = RunStanding::Defeat;
        return;
    }
    run.Standing = RunStanding::Ongoing;
    run.ResourcesKnown = true;
    run.Hp = hp;
    run.Focus = focus;
}

// True when a loaded run should open on the saved wound instead of full health.
[[nodiscard]] inline bool ResumeWound(const RunState& run)
{
    return run.Standing == RunStanding::Ongoing && run.ResourcesKnown;
}

enum class RunSaveError : u8 {
    None,
    NotFound,
    Corrupt,
    TooNew,
    NoMigration,
    WriteFailed,
    BadData,
};

struct RunLoad {
    RunState Run;
    u32 FileVersion = 0;
    RunSaveError Error = RunSaveError::None;
    std::string Message;
    bool FromBackup = false;

    explicit operator bool() const { return Error == RunSaveError::None; }
};

// One slot ("run") in a folder, written with Emerald's versioned text save. The current data
// version is kVersion. A file from the previous version is migrated one step at a time. The
// write goes to a temp file and is renamed over the slot, and a damaged file falls back to
// the previous good copy. The folder is the per-user path unless a test passes its own.
class RunSave {
public:
    // Version 3. Found ids and dialogue flags are optional in this version, so an older
    // file of it needs no migration: a missing list is a run that has claimed no drops
    // and set no talk flags.
    static constexpr u32 kVersion = 3;

    explicit RunSave(std::filesystem::path folder);

    // Paths::GetPrefPath("Ridejock", "Ascension") / "saves", or "saves" when that path
    // cannot be created.
    [[nodiscard]] static std::filesystem::path DefaultFolder();

    [[nodiscard]] const std::filesystem::path& Folder() const { return m_Folder; }

    bool Write(const RunState& run, std::string& error) const;
    [[nodiscard]] RunLoad Read() const;

private:
    std::filesystem::path m_Folder;
};

} // namespace Ascension
