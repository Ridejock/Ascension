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
    std::optional<i32> Level;
    std::string ClassId;
    FightRequest Fight;
    std::optional<i32> Gold;
    std::vector<PlaytestItem> Give;
    std::vector<std::string> Equip;
    std::vector<SDL_Scancode> Keys;
    std::string ChooseId;

    // True when startup has to load data to check an id, a level, or a folder.
    [[nodiscard]] bool UsesData() const;
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
// Unknown ids fail and the message names the id. Does not pick --choose; call PickEvolution
// when a choice is open, including once at startup before the first fight.
[[nodiscard]] bool ApplyPlaytest(Hero& hero, Inventory& inventory, const Database& data,
                                 const PlaytestOptions& options, PlaytestSetup& setup,
                                 std::string& error);

// Picks `classId` when an evolution choice is open. A miss names the id.
[[nodiscard]] EvolutionPick PickEvolution(Hero& hero, std::string_view classId, std::string& error);

// Loads the data folder, checks every id, and checks that the transcript can be opened.
// Failure is the process error for a bad playtest command, before the window is created.
[[nodiscard]] bool CheckPlaytestStartup(const PlaytestOptions& options, std::string& error);

} // namespace Ascension
