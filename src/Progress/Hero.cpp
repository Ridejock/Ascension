#include "Ascension/Progress/Hero.h"

namespace Ascension {

namespace {

constexpr i32 kXpCap = 999999;

} // namespace

bool Hero::Start(const Database& data, std::string& error)
{
    if (data.FindClass("fighter") == nullptr) {
        error = "class 'fighter' does not exist";
        m_Data = nullptr;
        m_ClassId.clear();
        return false;
    }
    m_Data = &data;
    m_ClassId = "fighter";
    m_Xp = 0;
    m_Practiced.clear();
    m_Uses.clear();
    error.clear();
    return true;
}

bool Hero::ApplyData(std::string& error)
{
    if (m_Data == nullptr || m_Data->FindClass(m_ClassId) == nullptr) {
        error = "class '" + m_ClassId + "' does not exist";
        return false;
    }
    error.clear();
    return true;
}

i32 Hero::Level() const
{
    if (m_Data == nullptr)
        return 1;
    return LevelForXp(m_Data->GetProgress(), m_Xp);
}

i32 Hero::NextLevelXp() const
{
    if (m_Data == nullptr)
        return 0;
    return Ascension::NextLevelXp(m_Data->GetProgress(), Level());
}

bool Hero::AtMaxLevel() const
{
    if (m_Data == nullptr)
        return false;
    return Ascension::AtMaxLevel(m_Data->GetProgress(), Level());
}

i32 Hero::UsesOf(std::string_view skillId) const
{
    for (usize i = 0; i < m_Practiced.size(); ++i) {
        if (m_Practiced[i] == skillId)
            return m_Uses[i];
    }
    return 0;
}

i32 Hero::RankOf(std::string_view skillId) const
{
    if (m_Data == nullptr)
        return 1;
    return RankForUses(m_Data->GetProgress(), UsesOf(skillId));
}

i32 Hero::RankAfterUse(std::string_view skillId) const
{
    if (m_Data == nullptr || m_Data->FindSkill(skillId) == nullptr)
        return RankOf(skillId);
    const i32 uses = UsesOf(skillId);
    const i32 next = uses >= 999999 ? uses : uses + 1;
    return RankForUses(m_Data->GetProgress(), next);
}

i32 Hero::NoteUse(std::string_view skillId)
{
    if (m_Data == nullptr || m_Data->FindSkill(skillId) == nullptr)
        return RankOf(skillId);
    for (usize i = 0; i < m_Practiced.size(); ++i) {
        if (m_Practiced[i] == skillId) {
            if (m_Uses[i] < kXpCap)
                ++m_Uses[i];
            return RankOf(skillId);
        }
    }
    m_Practiced.emplace_back(skillId);
    m_Uses.push_back(1);
    return RankOf(skillId);
}

std::vector<std::string> Hero::SkillIds() const
{
    std::vector<std::string> ids;
    if (m_Data == nullptr)
        return ids;
    const ClassDef* klass = m_Data->FindClass(m_ClassId);
    if (klass == nullptr)
        return ids;
    for (const SkillDef* skill : m_Data->ActiveSkills(*klass))
        ids.push_back(skill->Id);
    return ids;
}

bool Hero::ChoicePending() const
{
    if (m_Data == nullptr)
        return false;
    const ClassDef* klass = m_Data->FindClass(m_ClassId);
    if (klass == nullptr || klass->IsEvolution())
        return false;
    const i32 level = Level();
    for (const ClassDef* evolution : m_Data->EvolutionsOf(m_ClassId)) {
        if (level >= evolution->EvolveLevel)
            return true;
    }
    return false;
}

std::vector<std::string> Hero::ChoiceIds() const
{
    std::vector<std::string> ids;
    if (!ChoicePending())
        return ids;
    const i32 level = Level();
    for (const ClassDef* evolution : m_Data->EvolutionsOf(m_ClassId)) {
        if (level >= evolution->EvolveLevel)
            ids.push_back(evolution->Id);
    }
    return ids;
}

bool Hero::Choose(std::string_view evolutionId, std::string& error)
{
    if (!ChoicePending()) {
        error = "no evolution is offered";
        return false;
    }
    bool offered = false;
    for (const std::string& id : ChoiceIds()) {
        if (id == evolutionId)
            offered = true;
    }
    if (!offered) {
        error = "evolution '" + std::string(evolutionId) + "' is not offered";
        return false;
    }
    m_ClassId = std::string(evolutionId);
    error.clear();
    return true;
}

bool Hero::ResetBuild(std::string& error)
{
    if (!ChoicePending()) {
        error = "a build resets only when a path is offered";
        return false;
    }
    m_Practiced.clear();
    m_Uses.clear();
    error.clear();
    return true;
}

const char* Hero::NextFightBlock() const
{
    if (!ChoicePending())
        return nullptr;
    return "Choose a path before the next fight.";
}

XpGrant Hero::GrantXp(i32 amount)
{
    XpGrant grant;
    grant.LevelBefore = Level();
    if (amount < 0 || m_Data == nullptr) {
        grant.LevelAfter = grant.LevelBefore;
        return grant;
    }
    const bool pending = ChoicePending();
    const i64 sum = static_cast<i64>(m_Xp) + amount;
    m_Xp = sum > kXpCap ? kXpCap : static_cast<i32>(sum);
    grant.Amount = amount;
    grant.LevelAfter = Level();
    grant.ChoiceOpened = !pending && ChoicePending();
    return grant;
}

} // namespace Ascension
