#include "Ascension/Data/Talk.h"

namespace Ascension {

namespace {

bool RequirementsHold(std::span<const FlagExpr> gates, const TalkFacts& facts)
{
    for (const FlagExpr& expr : gates) {
        if (!FlagHolds(expr, facts))
            return false;
    }
    return true;
}

bool ListHas(const TalkFacts& facts, std::string_view name, std::string_view id)
{
    for (const TalkList& list : facts.Lists) {
        if (list.Name != name)
            continue;
        for (const std::string& held : list.Ids) {
            if (held == id)
                return true;
        }
        return false;
    }
    return false;
}

void WrapPiece(std::string_view piece, i32 width, std::vector<std::string>& out)
{
    std::string_view rest = piece;
    while (!rest.empty()) {
        if (static_cast<i32>(rest.size()) <= width) {
            out.emplace_back(rest);
            return;
        }
        i32 split = width;
        for (i32 index = width; index > 0; --index) {
            if (rest[static_cast<usize>(index - 1)] == ' ') {
                split = index - 1;
                break;
            }
        }
        if (split <= 0)
            split = width;
        out.emplace_back(rest.substr(0, static_cast<usize>(split)));
        rest.remove_prefix(static_cast<usize>(split));
        while (!rest.empty() && rest.front() == ' ')
            rest.remove_prefix(1);
    }
}

} // namespace

bool KnownTalkAction(std::string_view action)
{
    return action == "shop_open";
}

bool FlagHolds(const FlagExpr& expr, const TalkFacts& facts)
{
    bool held = false;
    if (expr.Tail.empty()) {
        for (const std::string& flag : facts.Flags) {
            if (flag == expr.Head)
                held = true;
        }
    } else {
        held = ListHas(facts, expr.Head, expr.Tail);
    }
    if (expr.Negate)
        return !held;
    return held;
}

const DialogueNode* TalkEntry(const DialogueDef& talk, const TalkFacts& facts)
{
    for (const DialogueNode& node : talk.Nodes) {
        if (RequirementsHold(node.Requires, facts))
            return &node;
    }
    return nullptr;
}

void TalkVisible(const DialogueDef& talk, const DialogueNode& node, const TalkFacts& facts,
                 std::vector<i32>& indexes)
{
    indexes.clear();
    for (i32 index = 0; index < static_cast<i32>(node.Choices.size()); ++index) {
        const DialogueChoice& choice = node.Choices[static_cast<usize>(index)];
        if (!RequirementsHold(choice.Requires, facts))
            continue;
        if (choice.Kind == TalkChoiceKind::Next && talk.FindNode(choice.Next) == nullptr)
            continue;
        indexes.push_back(index);
    }
}

void TalkApplySets(std::span<const std::string> sets, std::vector<std::string>& flags)
{
    for (const std::string& id : sets) {
        bool have = false;
        for (const std::string& flag : flags) {
            if (flag == id)
                have = true;
        }
        if (!have)
            flags.push_back(id);
    }
}

bool TalkChoose(const DialogueDef& talk, std::string& nodeId, std::vector<std::string>& flags,
                const TalkFacts& facts, i32 index, TalkMove& move)
{
    const DialogueNode* node = talk.FindNode(nodeId);
    if (node == nullptr)
        return false;
    std::vector<i32> visible;
    TalkVisible(talk, *node, facts, visible);
    if (index < 0) {
        if (!visible.empty())
            return false;
        move = {};
        move.Step = TalkStep::Ended;
        return true;
    }
    if (index >= static_cast<i32>(visible.size()))
        return false;
    const usize choiceIndex = static_cast<usize>(visible[static_cast<usize>(index)]);
    const DialogueChoice& choice = node->Choices[choiceIndex];
    TalkApplySets(choice.Sets, flags);
    move = {};
    if (choice.Kind == TalkChoiceKind::End) {
        move.Step = TalkStep::Ended;
        return true;
    }
    if (choice.Kind == TalkChoiceKind::Action) {
        move.Step = TalkStep::Action;
        move.Action = choice.Action;
        return true;
    }
    const DialogueNode* next = talk.FindNode(choice.Next);
    if (next == nullptr)
        return false;
    TalkApplySets(next->Sets, flags);
    move.Step = TalkStep::Moved;
    move.NodeId = next->Id;
    nodeId = next->Id;
    return true;
}

void TalkBoxLines(std::string_view line, i32 width, std::vector<std::string>& out)
{
    out.clear();
    if (width < 1)
        width = 1;
    std::string_view rest = line;
    while (true) {
        const usize slash = rest.find('/');
        std::string_view piece = slash == std::string_view::npos ? rest : rest.substr(0, slash);
        while (!piece.empty() && piece.front() == ' ')
            piece.remove_prefix(1);
        while (!piece.empty() && piece.back() == ' ')
            piece.remove_suffix(1);
        if (!piece.empty())
            WrapPiece(piece, width, out);
        if (slash == std::string_view::npos)
            return;
        rest.remove_prefix(slash + 1);
    }
}

} // namespace Ascension
