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
    m_Custom = false;
    m_ChoosingStarter = false;
    m_ClassPool = 0;
    m_SpendId.clear();
    m_LevelSteps.clear();
    m_ClassSources.clear();
    error.clear();
    return true;
}

bool Hero::ApplyData(std::string& error)
{
    if (m_Data == nullptr || m_Data->FindClass(m_ClassId) == nullptr) {
        error = "class '" + m_ClassId + "' does not exist";
        return false;
    }
    NoteLevelSteps();
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
    if (m_ChoosingStarter) {
        error = "pick a class before a path";
        return false;
    }
    if (!offered) {
        error = "evolution '" + std::string(evolutionId) + "' is not offered";
        return false;
    }
    m_ClassId = std::string(evolutionId);
    if (m_Custom && m_Data != nullptr) {
        const ClassDef* evolved = m_Data->FindClass(m_ClassId);
        if (evolved != nullptr) {
            AttributeSource grant;
            grant.Id = evolved->Id;
            grant.Points = evolved->Bonus;
            m_ClassSources.push_back(std::move(grant));
        }
    }
    error.clear();
    return true;
}

bool Hero::ResetBuild(std::string& error)
{
    if (!ChoicePending()) {
        error = "a build resets only when a path is offered";
        return false;
    }
    if (!m_Custom)
        m_LevelSteps = LevelSteps();
    else
        NoteLevelSteps();
    i32 pool = m_ClassPool;
    for (const AttributeSource& source : ClassSources())
        pool += StatTotal(source.Points);
    m_ClassSources.clear();
    m_ClassPool = pool;
    m_SpendId.clear();
    m_Custom = true;
    m_ChoosingStarter = true;
    m_Practiced.clear();
    m_Uses.clear();
    error.clear();
    return true;
}

std::vector<std::string> Hero::StarterIds() const
{
    std::vector<std::string> ids;
    if (m_Data == nullptr)
        return ids;
    for (const ClassDef* starter : m_Data->Starters()) {
        if (starter != nullptr)
            ids.push_back(starter->Id);
    }
    return ids;
}

bool Hero::ChooseStarter(std::string_view classId, std::string& error)
{
    if (!m_ChoosingStarter) {
        error = "a class is picked after a build reset";
        return false;
    }
    const std::string picked(classId);
    bool offered = false;
    for (const std::string& id : StarterIds()) {
        if (id == picked)
            offered = true;
    }
    if (!offered) {
        error = "class '" + picked + "' is not a starter";
        return false;
    }
    m_ClassId = picked;
    m_SpendId = picked;
    m_ChoosingStarter = false;
    bool listed = false;
    for (const AttributeSource& source : m_ClassSources) {
        if (source.Id == picked)
            listed = true;
    }
    if (!listed) {
        AttributeSource grant;
        grant.Id = picked;
        m_ClassSources.push_back(std::move(grant));
    }
    error.clear();
    return true;
}

bool Hero::SpendClassPoint(std::string_view statId, std::string& error)
{
    if (m_ChoosingStarter) {
        error = "pick a class before spending class points";
        return false;
    }
    if (!m_Custom || m_SpendId.empty()) {
        error = "class points are still on the class";
        return false;
    }
    if (m_ClassPool <= 0) {
        error = "no class points left";
        return false;
    }
    StatBlock probe;
    if (FindStatValue(probe, statId) == nullptr) {
        error = "stat '" + std::string(statId) + "' does not exist";
        return false;
    }
    AttributeSource* slot = nullptr;
    for (AttributeSource& source : m_ClassSources) {
        if (source.Id == m_SpendId)
            slot = &source;
    }
    if (slot == nullptr) {
        AttributeSource grant;
        grant.Id = m_SpendId;
        m_ClassSources.push_back(std::move(grant));
        slot = &m_ClassSources.back();
    }
    i32* value = FindStatValue(slot->Points, statId);
    if (value == nullptr || *value >= 9999) {
        error = "stat '" + std::string(statId) + "' is full";
        return false;
    }
    *value += 1;
    --m_ClassPool;
    error.clear();
    return true;
}

i32 Hero::LevelPoints() const
{
    i32 total = 0;
    for (const StatBlock& step : LevelSteps())
        total += StatTotal(step);
    return total;
}

i32 Hero::ClassPoints() const
{
    i32 total = m_ClassPool;
    for (const AttributeSource& source : ClassSources())
        total += StatTotal(source.Points);
    return total;
}

i32 Hero::AttributePoints() const
{
    return LevelPoints() + ClassPoints();
}

std::vector<AttributeSource> Hero::ClassSources() const
{
    if (m_Custom)
        return m_ClassSources;
    return DerivedSources();
}

StatBlock Hero::Attributes() const
{
    StatBlock stats;
    for (const StatBlock& step : LevelSteps())
        stats += step;
    for (const AttributeSource& source : ClassSources())
        stats += source.Points;
    return stats;
}

const char* Hero::NextFightBlock() const
{
    if (m_ChoosingStarter)
        return "Choose a class before the next fight.";
    if (m_ClassPool > 0)
        return "Spend class points before the next fight.";
    if (!ChoicePending())
        return nullptr;
    return "Choose a path before the next fight.";
}

i32 Hero::IndexAfterNextFoe(i32 index, i32 count) const
{
    if (NextFightBlock() != nullptr || count <= 0)
        return index;
    return (index + 1) % count;
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
    NoteLevelSteps();
    return grant;
}

void Hero::NoteLevelSteps()
{
    if (!m_Custom || m_Data == nullptr)
        return;
    const i32 count = Level() > 1 ? Level() - 1 : 0;
    if (static_cast<i32>(m_LevelSteps.size()) > count)
        m_LevelSteps.resize(static_cast<usize>(count));
    const ClassDef* klass = m_Data->FindClass(m_ClassId);
    while (static_cast<i32>(m_LevelSteps.size()) < count && klass != nullptr) {
        const i32 step = static_cast<i32>(m_LevelSteps.size()) + 1;
        m_LevelSteps.push_back(GrowthForStep(*klass, step));
    }
}

StatBlock Hero::GrowthForStep(const ClassDef& klass, i32 step) const
{
    if (klass.IsEvolution() && step < klass.EvolveLevel && m_Data != nullptr) {
        if (const ClassDef* parent = m_Data->FindClass(klass.EvolvesFrom))
            return parent->Growth;
    }
    return klass.Growth;
}

std::vector<StatBlock> Hero::LevelSteps() const
{
    if (m_Custom)
        return m_LevelSteps;
    std::vector<StatBlock> steps;
    if (m_Data == nullptr)
        return steps;
    const ClassDef* klass = m_Data->FindClass(m_ClassId);
    if (klass == nullptr)
        return steps;
    const i32 count = Level() > 1 ? Level() - 1 : 0;
    for (i32 step = 1; step <= count; ++step)
        steps.push_back(GrowthForStep(*klass, step));
    return steps;
}

std::vector<AttributeSource> Hero::DerivedSources() const
{
    std::vector<AttributeSource> sources;
    if (m_Data == nullptr)
        return sources;
    const ClassDef* klass = m_Data->FindClass(m_ClassId);
    if (klass == nullptr)
        return sources;
    if (klass->IsEvolution()) {
        if (const ClassDef* parent = m_Data->FindClass(klass->EvolvesFrom)) {
            AttributeSource base;
            base.Id = parent->Id;
            base.Points = parent->Stats;
            sources.push_back(std::move(base));
        }
        AttributeSource bonus;
        bonus.Id = klass->Id;
        bonus.Points = klass->Bonus;
        sources.push_back(std::move(bonus));
        return sources;
    }
    AttributeSource base;
    base.Id = klass->Id;
    base.Points = klass->Stats;
    sources.push_back(std::move(base));
    return sources;
}

} // namespace Ascension
