#pragma once

#include <string>
#include <vector>

#include "Ascension/Data/Database.h"

namespace Ascension {

struct PlaytestOptions;

// One row of a seeded rate table. Focus "default" marks a uniform living foe every
// turn. Any other focus is the kill order named in the plan. Runs is how many distinct
// fights were played: a named focus with slip 0, and any fight with one enemy, is one
// outcome, so Runs stays 1 however large the request is. HP left on a loss or a timeout
// is 0. P10Hp is the sorted sample at index (runs - 1) * 10 / 100.
struct SimRateRow {
    std::string Encounter;
    std::string Build;
    std::string Focus;
    bool Fang = false;
    i32 Runs = 0;
    i32 Wins = 0;
    i32 Timeouts = 0;
    i64 SumHp = 0;
    i64 SumTurns = 0;
    i32 P10Hp = 0;
    bool Ok = false;
    std::string Error;
};

// Runs is the request. The harness plays that many only when the seed can change the
// mark. Seed 0 is treated as 1. Slip is the percent of player turns that ignore a named
// focus and mark a random living foe instead. 0 keeps the kill order exact.
struct SimRateQuery {
    std::string EncounterId;
    i32 Runs = 1000;
    i32 Seed = 1;
    i32 Slip = 0;
};

// Wolf pack: dire wolf, wolf, then default. A one-enemy encounter, such as the Barrow
// Sentry or the Bear, is a single row. Other encounters walk their member ids and add
// default when more than one foe can be marked.
[[nodiscard]] std::vector<SimRateRow>
SimulateEncounterRates(const Database& data, const SimRateQuery& query, std::string& error);

[[nodiscard]] std::string FormatSimRates(const std::vector<SimRateRow>& rows,
                                         const SimRateQuery& query);

// One fight in a chain. The harness kit is the single-fight one: level 6, a rusty
// sword, and an oak shield, with no fang. The first fight of a build starts full, so
// its HP left matches that build's single-fight row. A win carries HP and focus into
// the next encounter. A loss or a timeout leaves HP left at 0 and the next fight
// starts full, which is the loss rule (back at full health, gold untouched).
struct ChainRow {
    std::string Encounter;
    std::string Build;
    i32 StartHp = 0;
    i32 StartFocus = 0;
    i32 MaxHp = 0;
    bool Won = false;
    i32 HpLeft = 0;
    i32 FocusLeft = 0;
    bool Ok = false;
    std::string Error;
};

// Empty ClassId is every harness build. PickId, when set, keeps only the build whose
// level-6 option is that skill.
struct ChainQuery {
    std::string ClassId;
    std::string PickId;
};

[[nodiscard]] std::vector<ChainRow> SimulateChain(const Database& data, const ChainQuery& query,
                                                  std::string& error);

[[nodiscard]] std::string FormatChain(const std::vector<ChainRow>& rows);

// One step of the walk from the hub to the boss. Fight names an encounter. Rest
// refills both pools and has an empty encounter id. Side is the optional room:
// the main route skips it, and the fang route takes it. The order is the order
// a hero meets them, derived from the maps. Nothing here names a room.
enum class RouteStepKind : u8 { Fight, Rest };

struct RouteStep {
    RouteStepKind Kind = RouteStepKind::Fight;
    bool Side = false;
    std::string EncounterId;
    std::string MapId;
};

// Empty when the maps have a hub, a linked boss (a noFlee spawn, or else the
// roster boss), and a walk that reaches that boss. Error names what is missing.
struct MapRoute {
    std::vector<RouteStep> Steps;
    std::string Error;
};

[[nodiscard]] MapRoute CollectMapRoute(const Database& data);

// One fight on that route. FangRoute is the run that takes the side room. Fang
// is true when a drop from an earlier win on this run is worn. The kit is the
// chain kit. A rest, and a loss, start the next fight full.
struct RouteRow {
    std::string Encounter;
    std::string Build;
    bool FangRoute = false;
    bool Fang = false;
    i32 StartHp = 0;
    i32 MaxHp = 0;
    bool Won = false;
    i32 HpLeft = 0;
    i32 FocusLeft = 0;
    bool Ok = false;
    std::string Error;
};

// `query` filters builds the same way the chain does. Both routes are played
// for each build: the main path, then the path that takes every side step.
[[nodiscard]] std::vector<RouteRow> SimulateRoute(const Database& data, const ChainQuery& query,
                                                  std::string& error);

[[nodiscard]] std::string FormatRoute(const std::vector<RouteRow>& rows);

// --sim. No --class prints the rate table for --enemy. A class or a focus plays that
// one row. 0 when every row was built. Losses are still 0: the table is the result.
[[nodiscard]] int RunPlaytestSim(const PlaytestOptions& options);

} // namespace Ascension
