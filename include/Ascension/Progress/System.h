#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/Database.h"

namespace Ascension {

// What gameplay tells the System. The bus does not decide offers. It records the fact,
// then the ledger is read against the deeds in the data.
enum class SystemEventKind : u8 { Kill, SkillUse, ItemUse, LevelUp };

struct SystemEvent {
    SystemEventKind Kind = SystemEventKind::Kill;
    std::string Id;
    std::vector<std::string> Tags;
    // Kills, skill uses, and item uses add this many. A level-up stores Amount as the
    // level reached, and a later level replaces a lower one instead of adding.
    i32 Amount = 1;
};

// One running total. Key is the deed key: kill:<id>, kill.tag:<tag>, skill:<id>,
// skill.tag:<tag>, item:<id>, item.tag:<tag>, tag:<tag>, or level.
struct SystemCounter {
    std::string Key;
    i32 Count = 0;
};

// The System's memory of one run. Counters are the bus. Titles are accepted. TitleOffers
// and SkillOffers are still waiting. GrantedSkills are System skill grants already taken.
// FreeSpent is the free points already placed. FreeFloor is the level through which free
// points were already settled: a new game uses 1, so every step counts, and a migrated
// save uses the level it loaded at, so old levels do not dump a spend screen.
struct SystemState {
    std::vector<SystemCounter> Counters;
    std::vector<std::string> Titles;
    std::vector<std::string> TitleOffers;
    std::vector<std::string> SkillOffers;
    std::vector<std::string> GrantedSkills;
    StatBlock FreeSpent;
    i32 FreeFloor = 1;
};

// Records the event and opens any offer the new totals earn. classId is the class the
// hero is at that moment, so a skill rule stays on that class's chain.
void DispatchSystem(SystemState& state, const Database& data, std::string_view classId, i32 level,
                    const SystemEvent& event);

// Opens offers the ledger already earned. A reload calls this so a new row can meet an
// old count. An offer the System has already made is not taken back, except a skill
// offer whose class chain no longer holds the rule, or an offer whose row is gone.
void EvaluateSystem(SystemState& state, const Database& data, std::string_view classId,
                    i32 level);

// The total for one deed key. An id count passes an empty tag. A tag count for that kind
// passes an empty id. Level passes both empty. The shared tag total is SystemTagCount.
[[nodiscard]] i32 SystemCount(const SystemState& state, SystemEventKind kind, std::string_view id,
                              std::string_view tag);

// Skill and item events share this total, under tag:<tag>. Kill tags do not add to it.
[[nodiscard]] i32 SystemTagCount(const SystemState& state, std::string_view tag);

[[nodiscard]] i32 RarityFreePoints(const Database& data, std::string_view rarityId);

// Free points the class line has earned from step 1 through level, using the parent's
// rarity before EvolveLevel and this class's rarity after. Level 1 earns nothing.
[[nodiscard]] i32 FreePointsEarned(const Database& data, const ClassDef& klass, i32 level);

// Earned between floor and level, minus points already spent. Never negative.
[[nodiscard]] i32 FreePointsUnspent(const Database& data, const ClassDef& klass, i32 level,
                                    i32 floor, const StatBlock& spent);

// True when this evolution is on offer at level: a guaranteed track at its evolve level,
// or a meld whose deed the ledger has met.
[[nodiscard]] bool EvolutionOffered(const SystemState& state, const ClassDef& evolution,
                                    i32 level);

[[nodiscard]] StatBlock TitleStatBonus(const Database& data, const SystemState& state);
[[nodiscard]] std::vector<TagDamage> TitleTagDamage(const Database& data, const SystemState& state);

// The rule on this class or its parent. Null when the offer is not on that chain.
[[nodiscard]] const SystemSkillRule* FindSystemSkillRule(const Database& data,
                                                         std::string_view classId,
                                                         std::string_view offerId);

bool AcceptTitle(SystemState& state, const Database& data, std::string_view id,
                 std::string& error);
bool AcceptSystemSkill(SystemState& state, const Database& data, std::string_view classId,
                       i32 level, std::string_view offerId, std::string& error);

} // namespace Ascension
