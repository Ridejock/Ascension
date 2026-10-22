#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_scancode.h>

#include "Ascension/Progress/Hero.h"
#include "Ascension/Progress/Inventory.h"

namespace Ascension {

// One stack from --give. Count defaults to 1 when the flag omits ":count".
struct PlaytestItem {
    std::string Id;
    i32 Count = 1;
};

// The id from --enemy. ResolveFight is the only lookup. An encounter id and a single
// enemy id share this field. Encounter ids are checked first.
struct FightRequest {
    std::string Id;
};

enum class FightKind {
    Enemy,
    Encounter,
};

struct ResolvedFight {
    FightKind Kind = FightKind::Enemy;
    std::string Id;
    // Encounter-list index for FightKind::Encounter. Roster index for FightKind::Enemy.
    i32 EnemyIndex = 0;
};

// Command-line setup for a scripted run. Empty fields mean "leave the normal start".
// Emerald's own flags (--frames, --screenshot, and the rest) are skipped, not stored.
struct PlaytestOptions {
    std::filesystem::path DataDir;
    std::filesystem::path Transcript;
    // Empty uses the per-user save folder. A set path is where this run reads and writes.
    std::filesystem::path SaveDir;
    std::optional<i32> Level;
    std::string ClassId;
    FightRequest Fight;
    // --map. Empty stays on the fight screen. A set id opens that map. A spawn
    // starts its fight; until then no fight is up.
    std::string MapId;
    std::optional<i32> Gold;
    std::vector<PlaytestItem> Give;
    std::vector<std::string> Equip;
    std::vector<SDL_Scancode> Keys;
    std::string ChooseId;
    // --pick, in the order they were passed. Applied one open pick at a time.
    std::vector<std::string> PickIds;
    // --focus, with --sim. The enemy id the policy kills first. Empty on a one-off marks
    // a random living foe each turn. Empty on the matrix prints every kill order.
    std::string FocusId;
    // --runs, with --sim. Requested seeded fights for a row whose mark can change.
    // A fixed kill order is still one fight. Default 1000. Range 1 to 100000.
    i32 Runs = 1000;
    // --seed, with --sim. First xorshift32 state for default targeting and for --slip.
    // 0 is stored as 1. Damage, turn order, and foe AI do not read it.
    i32 Seed = 1;
    // --slip, with --sim. Percent of player turns that mark a random living foe instead
    // of the named focus. 0 keeps that order exact. Range 0 to 100.
    i32 Slip = 0;
    // --trace writes one state line per action to stderr. The transcript stays the combat log.
    bool Trace = false;
    // --sim plays the fight without a window. With no class and no focus, --enemy prints
    // a rate table for every class build.
    bool Sim = false;
    // --chain, with --sim. Walks encounters.json in order and carries HP and focus.
    // No --enemy. The single-fight table is unchanged.
    bool Chain = false;

    // True when startup has to load data to check an id, a level, or a folder.
    [[nodiscard]] bool UsesData() const;

    // True when the command builds a hero of its own. That run does not load a save.
    [[nodiscard]] bool DefinesRun() const
    {
        return Level.has_value() || !ClassId.empty() || !Fight.Id.empty() || !MapId.empty() ||
               Gold.has_value() || !Give.empty() || !Equip.empty() || !ChooseId.empty() ||
               !PickIds.empty() || Sim;
    }
};

struct PlaytestSetup {
    // Place in encounters.json. A normal start is 0. An encounter id from --enemy
    // is that encounter's place in the list.
    i32 EnemyIndex = 0;
    // Set when --enemy names a roster foe that is not an encounter. Empty for an
    // encounter, and for a start that did not pass --enemy.
    std::string DirectEnemy;
};

// What PickEvolution did. Waiting means the choice is not open (or no id was set).
enum class EvolutionPick { Waiting, Chosen, Failed };

// What PickSkillOption did. Waiting means no skill pick is open (or no id was set).
enum class SkillOptionPick { Waiting, Chosen, Failed };

// `args[0]` is the program name and is skipped. Unknown flags fail and name the flag.
// A bad key name fails here and names the key. Ids are checked later, against the data.
[[nodiscard]] bool ParsePlaytestOptions(std::span<const std::string_view> args,
                                        PlaytestOptions& options, std::string& error);

// The folder Database should load. --data wins. Otherwise the source data/ when that
// directory is present, and the copy beside the executable when it is not.
[[nodiscard]] std::filesystem::path PlaytestDataDirectory(const PlaytestOptions& options);

// Looks `request` up. An id in encounters.json becomes FightKind::Encounter, even when
// an enemy uses the same id. Otherwise an enemy id becomes FightKind::Enemy. Anything
// else fails and the message names the id.
[[nodiscard]] bool ResolveFight(const Database& data, const FightRequest& request,
                                ResolvedFight& resolved, std::string& error);

// The on-screen line when the marked enemy changes. Note() writes it, so --transcript
// keeps it with the other log lines.
[[nodiscard]] std::string MarkedTargetLine(std::string_view name);

// Starts the hero and the bag, then applies level, class, gold, items, gear, and the foe.
// Unknown ids fail and the message names the id. Does not pick --choose or --pick; call
// PickEvolution when a choice is open, and PickSkillOption when a skill pick is open,
// including once at startup before the first fight.
[[nodiscard]] bool ApplyPlaytest(Hero& hero, Inventory& inventory, const Database& data,
                                 const PlaytestOptions& options, PlaytestSetup& setup,
                                 std::string& error);

// Picks `classId` when an evolution choice is open. A miss names the id.
[[nodiscard]] EvolutionPick PickEvolution(Hero& hero, std::string_view classId, std::string& error);

// Keeps `skillId` when a skill pick is open. A miss names the id.
[[nodiscard]] SkillOptionPick PickSkillOption(Hero& hero, std::string_view skillId,
                                              std::string& error);

// Loads the data folder, checks every id, and checks that the transcript can be opened.
// Failure is the process error for a bad playtest command, before the window is created.
[[nodiscard]] bool CheckPlaytestStartup(const PlaytestOptions& options, std::string& error);

// The line drawn when no fight is on screen. A start error, a load error, and a fight
// block (Hero::NextFightBlock) each keep the sentence already produced. The generic
// load line is only used when all three are empty. `fightBlock` may be null.
[[nodiscard]] std::string StartupFailureText(std::string_view startError,
                                             std::string_view loadError, const char* fightBlock);

// Where a fight stands for the N key. None is the gap before the first battle.
enum class FightStanding { None, Ongoing, Victory, Defeat };

// N advances only once the fight is over. An ongoing battle stays on the current foe.
[[nodiscard]] inline bool NextFightAllowed(FightStanding standing)
{
    return standing != FightStanding::Ongoing;
}

// True for a press that aims or spends the player's turn. N, R, and I are not in that set.
[[nodiscard]] inline bool KeyNeedsPlayerTurn(SDL_Scancode scancode)
{
    if (scancode == SDL_SCANCODE_SPACE || scancode == SDL_SCANCODE_LEFT ||
        scancode == SDL_SCANCODE_RIGHT || scancode == SDL_SCANCODE_C ||
        scancode == SDL_SCANCODE_F)
        return true;
    const int digit = static_cast<int>(scancode) - static_cast<int>(SDL_SCANCODE_1);
    return digit >= 0 && digit < 8;
}

// What to do with one key while a --keys script is running.
enum class ScriptedPress { Play, IgnoreWindow, WaitForTurn };

// ScriptRunning is true while scripted presses remain. FromScript is true only for the
// press the script itself is applying. A focused window's own key must not join that
// stream. A combat key waits when the foe's turn has not finished.
struct ScriptedPressQuery {
    bool ScriptRunning = false;
    bool FromScript = false;
    bool FightOngoing = false;
    bool PlayerTurn = true;
    bool NeedsPlayerTurn = false;
};

[[nodiscard]] inline ScriptedPress DecideScriptedPress(const ScriptedPressQuery& query)
{
    if (query.ScriptRunning && !query.FromScript)
        return ScriptedPress::IgnoreWindow;
    if (query.FightOngoing && query.NeedsPlayerTurn && !query.PlayerTurn)
        return ScriptedPress::WaitForTurn;
    return ScriptedPress::Play;
}

} // namespace Ascension
