#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/Database.h"

namespace Ascension {

// What gameplay tells the System. The bus does not decide offers. It records the fact,
// then the ledger is read against the deeds in the data. Mark is a habit: the id and
// tags are whatever the caller observed, and the deed names those same tags.
enum class SystemEventKind : u8 { Kill, SkillUse, ItemUse, LevelUp, Mark };

struct SystemEvent {
    SystemEventKind Kind = SystemEventKind::Kill;
    std::string Id;
    std::vector<std::string> Tags;
    // Kills, skill uses, and item uses add this many. A level-up stores Amount as the
    // level reached, and a later level replaces a lower one instead of adding.
    i32 Amount = 1;
};

// One running total. Key is the deed key: kill:<id>, kill.tag:<tag>, skill:<id>,
// skill.tag:<tag>, item:<id>, item.tag:<tag>, tag:<tag>, mark:<id>, mark.tag:<tag>,
// or level.
struct SystemCounter {
    std::string Key;
    i32 Count = 0;
};

// The System's memory of one run. Counters are the bus. Titles are owned (accepted).
// Equipped is the owned ids currently worn, in slot order, no longer than the class
// rarity's titleSlots. Only Equipped grants a bonus. TitleOffers and SkillOffers are
// still waiting. GrantedSkills are System skill grants already taken. FreeSpent is the
// free points already placed. FreeFloor is the level through which free points were
// already settled: a new game uses 1, so every step counts, and a migrated save uses
// the level it loaded at, so old levels do not dump a spend screen.
struct SystemState {
    std::vector<SystemCounter> Counters;
    std::vector<std::string> Titles;
    std::vector<std::string> Equipped;
    std::vector<std::string> TitleOffers;
    std::vector<std::string> SkillOffers;
    std::vector<std::string> GrantedSkills;
    StatBlock FreeSpent;
    i32 FreeFloor = 1;
    // Lines the System has spoken, oldest first, capped by the data's logKeep.
    // Pending is the same lines not yet shown. It is not saved. NotedMelds are
    // meld class ids already announced, so a reload does not say them again.
    // NoticesSettled is set by a version 7 migration and is not a counter: the
    // first evaluation records the melds already open and then clears it.
    std::vector<std::string> Log;
    std::vector<std::string> Pending;
    std::vector<std::string> NotedMelds;
    bool NoticesSettled = false;
};

// One catalog title as this run wears it. Slot is the equipped index, or -1 when it is
// not worn. Dialogue reads Owned, Equipped, Name, and Description from this fact. It
// does not turn the id into words. An owned id whose row is gone keeps the id and
// leaves the name empty.
struct TitleFact {
    std::string Id;
    std::string Name;
    std::string Description;
    std::vector<Modifier> Bonus;
    bool Owned = false;
    bool Equipped = false;
    i32 Slot = -1;
};

// True when the deed holds. Level leaves read the ledger, not `level`. Class leaves
// compare `classId`. Title leaves read the owned and equipped lists. A count with an
// empty ledger is zero. Inline so dialogue, which lives with the data, can use the
// same check without the career library.
[[nodiscard]] inline bool DeedHolds(const SystemState& state, std::string_view classId, i32 level,
                                    const DeedDef& deed)
{
    (void)level;
    if (deed.Op == DeedOp::All) {
        if (deed.Parts.empty())
            return false;
        for (const DeedDef& part : deed.Parts) {
            if (!DeedHolds(state, classId, level, part))
                return false;
        }
        return true;
    }
    if (deed.Op == DeedOp::Any) {
        for (const DeedDef& part : deed.Parts) {
            if (DeedHolds(state, classId, level, part))
                return true;
        }
        return false;
    }
    if (deed.Op == DeedOp::Not)
        return deed.Parts.size() == 1 && !DeedHolds(state, classId, level, deed.Parts[0]);

    auto listed = [](const std::vector<std::string>& ids, std::string_view id) {
        for (const std::string& held : ids) {
            if (held == id)
                return true;
        }
        return false;
    };
    if (deed.On == DeedOn::Class)
        return !deed.Id.empty() && deed.Id == classId;
    if (deed.On == DeedOn::Title) {
        if (deed.Id.empty())
            return false;
        if (deed.Worn)
            return listed(state.Equipped, deed.Id);
        return listed(state.Titles, deed.Id);
    }

    auto read = [&](std::string_view key) {
        for (const SystemCounter& counter : state.Counters) {
            if (counter.Key == key)
                return counter.Count;
        }
        return 0;
    };
    auto prefix = [](SystemEventKind kind) {
        if (kind == SystemEventKind::SkillUse)
            return "skill";
        if (kind == SystemEventKind::ItemUse)
            return "item";
        if (kind == SystemEventKind::LevelUp)
            return "level";
        if (kind == SystemEventKind::Mark)
            return "mark";
        return "kill";
    };
    i32 have = 0;
    if (deed.On == DeedOn::Level) {
        have = read("level");
    } else if (deed.On == DeedOn::Tag) {
        have = read(std::string("tag:") + std::string(deed.Tag));
    } else {
        SystemEventKind kind = SystemEventKind::Kill;
        if (deed.On == DeedOn::Skill)
            kind = SystemEventKind::SkillUse;
        else if (deed.On == DeedOn::Item)
            kind = SystemEventKind::ItemUse;
        else if (deed.On == DeedOn::Mark)
            kind = SystemEventKind::Mark;
        const std::string stem = prefix(kind);
        if (!deed.Tag.empty())
            have = read(stem + ".tag:" + deed.Tag);
        else
            have = read(stem + ":" + deed.Id);
        if (!deed.Tag.empty() && !deed.Id.empty()) {
            const i32 byId = read(stem + ":" + deed.Id);
            if (byId < have)
                have = byId;
        }
    }
    return have >= deed.AtLeast;
}

// Records the event and opens any offer the new totals earn. classId is the class the
// hero is at that moment, so a skill rule stays on that class's chain. A new title,
// skill, or meld appends a line to Pending and to Log.
void DispatchSystem(SystemState& state, const Database& data, std::string_view classId, i32 level,
                    const SystemEvent& event);

// Turns one habit into mark counters. Gold adds its amount. A dealt blow at least
// heavyAt adds one heavy mark. A region id is counted once. Other habits add one,
// plus the note's own tags and the tag the data maps that kind to.
void NoteHabit(SystemState& state, const Database& data, std::string_view classId, i32 level,
               const HabitNote& note);

// The pending lines, in the order they were earned. Pending is left empty.
[[nodiscard]] std::vector<std::string> DrainNotices(SystemState& state);

// Sums one list. Same tags of damage become one row. Percents add.
[[nodiscard]] ModifierStack StackModifiers(std::span<const Modifier> mods);
// Percent of each primary, then the flat, written back into `stats`. A zero percent
// leaves the primary and adds the flat, which is what a stat amount did before.
void ApplyTitleModifiers(StatBlock& stats, const ModifierStack& stack);
// amount * (100 + percent) / 100, clamped to 0..999999. A zero percent returns amount.
[[nodiscard]] i32 ScaleByPercent(i32 amount, i32 percent);

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

// How many titles `classId` can wear. That is its rarity's titleSlots. An evolution
// with no rarity uses the parent's rarity, the same way free points do. A missing
// class, or a rarity row that omits titleSlots, wears none.
[[nodiscard]] i32 TitleSlotCount(const Database& data, std::string_view classId);

// Drops equipped ids that are not owned, then keeps the earliest ones that fit.
void ClampEquippedTitles(SystemState& state, const Database& data, std::string_view classId);

// Equipped titles, summed. An owned title that is not worn adds nothing. A worn id
// whose row is gone adds nothing and still holds its slot.
[[nodiscard]] ModifierStack TitleModifiers(const Database& data, const SystemState& state);
// The flat half of TitleModifiers. A percent title is not in this block.
[[nodiscard]] StatBlock TitleStatBonus(const Database& data, const SystemState& state);
[[nodiscard]] std::vector<TagDamage> TitleTagDamage(const Database& data, const SystemState& state);

// Every catalog title, in file order, then any owned or equipped id the catalog no
// longer has. Equipped and Slot describe this run. An id that is not returned is
// not a fact.
[[nodiscard]] std::vector<TitleFact> QueryTitles(const Database& data, const SystemState& state);
// False when `id` is not a catalog title and this run does not own or wear it.
bool FindTitleFact(const Database& data, const SystemState& state, std::string_view id,
                   TitleFact& out);
[[nodiscard]] bool TitleOwned(const SystemState& state, std::string_view id);
[[nodiscard]] bool TitleIsEquipped(const SystemState& state, std::string_view id);

// The rule on this class or its parent. Null when the offer is not on that chain.
[[nodiscard]] const SystemSkillRule* FindSystemSkillRule(const Database& data,
                                                         std::string_view classId,
                                                         std::string_view offerId);

bool AcceptTitle(SystemState& state, const Database& data, std::string_view id,
                 std::string& error);
// Wears an owned title in the next open slot. Fails when it is not owned, already
// worn, unknown, or every slot of `classId` is filled.
bool EquipTitle(SystemState& state, const Database& data, std::string_view classId,
                std::string_view id, std::string& error);
// Takes a worn title off. It stays owned.
bool UnequipTitle(SystemState& state, std::string_view id, std::string& error);
bool AcceptSystemSkill(SystemState& state, const Database& data, std::string_view classId,
                       i32 level, std::string_view offerId, std::string& error);

} // namespace Ascension
