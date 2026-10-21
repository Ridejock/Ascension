#pragma once

#include "Ascension/Data/Database.h"

namespace Ascension {

// Facts appended to a shop or bag row (`handling` true) or a worn row (`handling` false).
// The caller keeps the row's own name, slot, and price. Heal, focus, and "two handed" are
// the handling facts. Each nonzero bonus uses the stat's name from stats.json, in that
// file's order. Each grant names the resolved skill. The rarity's display name is last.
// Item descriptions are not part of the row.
[[nodiscard]] inline std::string GearLine(const Database& data, const ItemDef& item, bool handling)
{
    std::string line;
    if (handling) {
        if (item.Heal > 0)
            line += "   heal " + std::to_string(item.Heal);
        if (item.Focus > 0)
            line += "   focus +" + std::to_string(item.Focus);
    }
    for (const StatDef& stat : data.GetStats()) {
        const i32 amount = item.Bonus.Get(stat.Id);
        if (amount == 0)
            continue;
        line += "   ";
        line += stat.Name;
        if (amount > 0)
            line += " +";
        else
            line += " ";
        line += std::to_string(amount);
    }
    if (handling && item.TwoHanded)
        line += "   two handed";
    for (const SkillGrant& grant : item.Grants)
        line += "   grants " + grant.Resolved.Name;
    const RarityDef* rarity = data.FindRarity(item.Rarity);
    if (rarity != nullptr && !rarity->Name.empty())
        line += "   " + rarity->Name;
    return line;
}

} // namespace Ascension
