#pragma once

#include "Ascension/Combat/Battle.h"

namespace Ascension {

// The foe panel's guard line, before that panel uppercases it. Empty when no guard is up.
// The percent is GuardPercent. "Guard" is the buff, not a stat name.
[[nodiscard]] inline std::string FoeGuardAlert(const CombatantState& who)
{
    if (who.GuardPercent <= 0)
        return {};
    return "Guard " + std::to_string(who.GuardPercent) + "%";
}

// A harden that is still counting down, before the panel uppercases it. The word is
// defense's name in stats.json, so 150 defense with 2 turns left is drawn
// "DEFENSE +150 (2)". Empty once the turns have run out.
[[nodiscard]] inline std::string FoeDefenseAlert(const Database& data, const CombatantState& who)
{
    if (who.BonusDefenseTurns <= 0)
        return {};
    std::string statName = "defense";
    if (const StatDef* stat = data.FindStat("defense"))
        statName = stat->Name;
    std::string line = std::move(statName);
    line += " ";
    if (who.BonusDefense >= 0)
        line += "+";
    line += std::to_string(who.BonusDefense);
    line += " (";
    line += std::to_string(who.BonusDefenseTurns);
    line += ")";
    return line;
}

} // namespace Ascension
