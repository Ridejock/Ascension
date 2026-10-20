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
    i32 Position = 0;
    std::string DirectEnemy;
    RunStanding Standing = RunStanding::None;
    bool ResourcesKnown = false;
    i32 Hp = 0;
    i32 Focus = 0;
    FightSnapshot Fight;
};

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
