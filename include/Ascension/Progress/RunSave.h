#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Ascension/Map/Handoff.h"
#include "Ascension/Progress/Hero.h"
#include "Ascension/Progress/Inventory.h"

namespace Ascension {

// Where a saved run was. None is a run that had not started a fight. Ongoing is mid-fight.
// A snapshot in Fight restores that blow, including the foe and whose turn it is. An ongoing
// file with no snapshot starts the encounter over. Victory and defeat stay between fights
// so the same win is not awarded twice.
enum class RunStanding : u8 { None, Ongoing, Victory, Defeat };

// One run: the career, the bag, where the hero is standing, and the spawns already
// cleared. This is the current data version. Older files are migrated before they fill this.
struct RunState {
    CareerRecord Career;
    BagRecord Bag;
    std::vector<std::string> Defeated;
    // Drop item ids already claimed. A version 3 file with no list has claimed nothing.
    std::vector<std::string> Found;
    // Flags set by dialogue. A version 3 file with no list has set none.
    std::vector<std::string> Flags;
    // Empty when the run has no map yet. The tile is the hero's cell on that map.
    std::string MapId;
    MapTile Tile;
    // Spawn ids, stable across inserts and reorders. An id the data no longer has
    // stays here so a later file that brings the spawn back is still cleared.
    std::vector<std::string> Cleared;
    // Wounds a flee left, keyed by spawn id. The same shape the map keeps in memory.
    std::vector<FledWounds> Wounds;
    // The spawn an ongoing fight came from. Empty for a list fight, and between fights.
    std::string SpawnId;
    // Encounter id for the fight screen. Not an index: inserting a row does not shift it.
    std::string EncounterId;
    // Set by the version 3 migration until AdoptLegacyMap has read the maps. -1 after that,
    // and on a file that was written as version 4.
    i32 LegacyIndex = -1;
    std::string DirectEnemy;
    RunStanding Standing = RunStanding::None;
    bool ResourcesKnown = false;
    i32 Hp = 0;
    i32 Focus = 0;
    FightSnapshot Fight;
};

// Records the fight's result and the pools the caller wants kept. A win, a flee, and an
// ongoing blow store HP and focus. A loss stores whatever the caller passes, which is the
// refilled pools when the map has already sent the hero home. A flee is between fights
// (standing None) and still keeps the wound.
inline void RememberOutcome(RunState& run, BattleResult result, i32 hp, i32 focus)
{
    run.ResourcesKnown = true;
    run.Hp = hp;
    run.Focus = focus;
    if (result == BattleResult::Victory) {
        run.Standing = RunStanding::Victory;
        return;
    }
    if (result == BattleResult::Defeat) {
        run.Standing = RunStanding::Defeat;
        return;
    }
    if (result == BattleResult::Fled) {
        run.Standing = RunStanding::None;
        return;
    }
    run.Standing = RunStanding::Ongoing;
}

// True when a loaded run should open on the saved pools instead of full health.
[[nodiscard]] inline bool ResumeWound(const RunState& run)
{
    return run.ResourcesKnown;
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
    // Version 5. `name` is the hero's own name. Empty is the default, and a missing
    // name on an older file stays empty. A missing class becomes `fighter`. The class
    // a version 4 file already stored is kept. Version 4 is a map id and a tile,
    // cleared spawns are string ids, and HP and focus are kept between fights,
    // including after a flee. Found ids and dialogue flags were already optional in
    // version 3: a missing list claimed nothing and set no talk flags.
    static constexpr u32 kVersion = 5;

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

// Version 3 stored `position` as an index into encounters.json, and defeated foe ids.
// The registered step renames that index to `legacy.index` and drops `position`, so a
// list index is never read as a tile. It also drops HP on a victory or a defeat: version
// 3 did not carry pools after a finished fight, and a version 1 file can still hold the
// old health field. An ongoing file keeps its HP and its fight snapshot. The snapshot
// has no spawn id, because those fights were the list.
//
// This applies `legacy.index` once the maps are loaded, then clears it. A version 4 run
// has no legacy index, and this does nothing.
//
// * The encounter id is the encounter at that index, or the first encounter when the
//   index is past the end. The fight screen resumes that id. The index is not written
//   again.
// * The hero stands on the start tile of the map with `hub` set. When no map sets it,
//   the first map in file order is the stand-in. No maps leaves the place empty.
// * A spawn is cleared when its encounter id is one of the defeated ids. The list stores
//   foe ids, and a one-foe encounter uses that same id, so those spawns match. An
//   encounter id that is not a foe id (the wolf pack) is not cleared. A spawn with
//   `once` left false stays up: list progress does not remove a repeatable seat.
//   The ids are copied from the data loaded now. A spawn added after this run is saved
//   as version 4 is not cleared by the old index.
// * `found` is left alone, so a claimed drop such as Alpha's Fang stays claimed.
void AdoptLegacyMap(const Database& data, RunState& run);

} // namespace Ascension
