#pragma once

#include <string>
#include <string_view>

#include "Emerald/Core/Defines.h"

namespace Ascension {

// A created hero's name. The create screen is the only writer. A save from before
// names existed loads as empty, and an empty name is shown as the class name.
// Letters are ASCII: the debug font has no other alphabet. Digits, one space, a
// hyphen, and an apostrophe are the rest of the set. The ends are trimmed and a
// double space collapses when the name is accepted.
inline constexpr i32 kHeroNameMin = 1;
inline constexpr i32 kHeroNameMax = 16;

enum class HeroNameStatus : u8 { Ok, Empty, TooLong, BadChar };

struct HeroNameCheck {
    HeroNameStatus Status = HeroNameStatus::Empty;
    std::string Name;
};

[[nodiscard]] inline bool IsHeroNameChar(unsigned char code)
{
    if (code >= 'A' && code <= 'Z')
        return true;
    if (code >= 'a' && code <= 'z')
        return true;
    if (code >= '0' && code <= '9')
        return true;
    return code == ' ' || code == '-' || code == '\'';
}

// Trim both ends and collapse repeated spaces. Other characters are copied through.
// Callers that need to reject them use CheckHeroName.
[[nodiscard]] inline std::string NormalizeHeroName(std::string_view raw)
{
    std::string out;
    out.reserve(raw.size());
    bool gap = false;
    for (const unsigned char code : raw) {
        if (code == ' ') {
            if (!out.empty())
                gap = true;
            continue;
        }
        if (gap) {
            out.push_back(' ');
            gap = false;
        }
        out.push_back(static_cast<char>(code));
    }
    return out;
}

[[nodiscard]] inline HeroNameCheck CheckHeroName(std::string_view raw)
{
    for (const unsigned char code : raw) {
        if (!IsHeroNameChar(code))
            return {HeroNameStatus::BadChar, {}};
    }
    std::string name = NormalizeHeroName(raw);
    if (name.empty() || static_cast<i32>(name.size()) < kHeroNameMin)
        return {HeroNameStatus::Empty, {}};
    if (static_cast<i32>(name.size()) > kHeroNameMax)
        return {HeroNameStatus::TooLong, {}};
    return {HeroNameStatus::Ok, std::move(name)};
}

// What a typed chunk did to the draft. The draft keeps a single trailing space so
// the next word can be typed. Confirm trims that space. A bad character is refused
// whole. A chunk that would pass 16 characters is refused whole.
enum class NameEdit : u8 { Kept, Added, Rejected, Full };

[[nodiscard]] inline NameEdit AppendHeroName(std::string& name, std::string_view text)
{
    if (text.empty())
        return NameEdit::Kept;
    for (const unsigned char code : text) {
        if (!IsHeroNameChar(code))
            return NameEdit::Rejected;
    }
    std::string next = name;
    for (const unsigned char code : text) {
        if (code == ' ') {
            if (next.empty() || next.back() == ' ')
                continue;
        }
        if (static_cast<i32>(next.size()) >= kHeroNameMax)
            return NameEdit::Full;
        next.push_back(static_cast<char>(code));
    }
    if (next == name)
        return NameEdit::Kept;
    name = std::move(next);
    return NameEdit::Added;
}

inline void PopHeroName(std::string& name)
{
    if (!name.empty())
        name.pop_back();
}

} // namespace Ascension
