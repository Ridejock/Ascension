#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/Types.h"

namespace Ascension {

// A box line is wrapped to this many characters. A '/' in the data is a hard break
// before that wrap, and the slash is not part of the line.
inline constexpr i32 kTalkLineWidth = 40;

// One named list of ids a flag can read. The talk matches the list by name. It does
// not know that "found" is claimed drops.
struct TalkList {
    std::string_view Name;
    std::span<const std::string> Ids;
};

// Flags already set by the talk, and the named lists the run is offering.
struct TalkFacts {
    std::span<const std::string> Flags;
    std::span<const TalkList> Lists;
};

enum class TalkStep : u8 { Moved, Ended, Action };

struct TalkMove {
    TalkStep Step = TalkStep::Moved;
    std::string NodeId;
    std::string Action;
};

// Verbs a choice may name. shop_open opens the shop screen that already exists.
[[nodiscard]] bool KnownTalkAction(std::string_view action);

// True when the expression holds against the flags and lists in `facts`.
[[nodiscard]] bool FlagHolds(const FlagExpr& expr, const TalkFacts& facts);

// The first node whose requirements hold. Null when every node is gated off.
[[nodiscard]] const DialogueNode* TalkEntry(const DialogueDef& talk, const TalkFacts& facts);

// Choice indexes the player can pick, in file order. A choice's own requirements gate
// it. The node it names is reached when the choice is taken; that node's requirements
// are what pick the start, not what hides the choice.
void TalkVisible(const DialogueDef& talk, const DialogueNode& node, const TalkFacts& facts,
                 std::vector<i32>& indexes);

// Adds each id in `sets` that `flags` does not already hold.
void TalkApplySets(std::span<const std::string> sets, std::vector<std::string>& flags);

// `index` picks from the visible list. A negative index acknowledges a node that has
// no visible choices and ends the talk. False when the index does not fit.
[[nodiscard]] bool TalkChoose(const DialogueDef& talk, std::string& nodeId,
                              std::vector<std::string>& flags, const TalkFacts& facts, i32 index,
                              TalkMove& move);

// Splits `line` on '/' and wraps each piece to `width` characters.
void TalkBoxLines(std::string_view line, i32 width, std::vector<std::string>& out);

} // namespace Ascension
