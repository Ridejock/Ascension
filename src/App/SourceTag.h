#pragma once

#include "Ascension/Data/Database.h"

namespace Ascension {

// "  (Rusty Sword)" for a skill a worn piece gives. Parentheses, because the debug font
// has no brackets. A class skill with no other source has no tag. A merge lists every
// source, class first.
[[nodiscard]] inline std::string SourceTag(const Database& data, const HeroGrant& grant)
{
    if (grant.Origins.size() == 1 && grant.Origins[0].Source == GrantSource::Class)
        return {};
    std::string tag;
    for (const GrantOrigin& origin : grant.Origins) {
        std::string name = origin.Id;
        if (origin.Source == GrantSource::Item) {
            const ItemDef* piece = data.FindItem(origin.Id);
            if (piece != nullptr)
                name = piece->Name;
        } else {
            const ClassDef* heroClass = data.FindClass(origin.Id);
            if (heroClass != nullptr)
                name = heroClass->Name;
        }
        if (!tag.empty())
            tag += " ";
        tag += "(";
        tag += name;
        tag += ")";
    }
    if (tag.empty())
        return {};
    return "  " + tag;
}

} // namespace Ascension
