#include "Ascension/Progress/Hero.h"

#include "Ascension/Progress/HeroName.h"

namespace Ascension {

namespace {

constexpr i32 kXpCap = 999999;

bool Lists(const std::vector<std::string>& ids, std::string_view id)
{
    for (const std::string& held : ids) {
        if (held == id)
            return true;
    }
    return false;
}

} // namespace

bool Hero::Start(const Database& data, std::string& error)
{
    const std::vector<const ClassDef*> starters = data.Starters();
    if (starters.empty() || starters.front() == nullptr) {
        error = "no starter class";
        m_Data = nullptr;
        m_Name.clear();
        m_ClassId.clear();
        return false;
    }
    m_Data = &data;
    m_Name.clear();
    m_ClassId = starters.front()->Id;
    m_Xp = 0;
    m_Practiced.clear();
    m_Uses.clear();
    m_Picked.clear();
    m_Custom = false;
    m_ChoosingStarter = false;
    m_ClassPool = 0;
    m_SpendId.clear();
    m_LevelSteps.clear();
    m_ClassSources.clear();
    m_System = {};
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
    EvaluateSystem(m_System, *m_Data, m_ClassId, Level());
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

void Hero::TellSystem(const SystemEvent& event)
{
    if (m_Data == nullptr)
        return;
    DispatchSystem(m_System, *m_Data, m_ClassId, Level(), event);
}

i32 Hero::NoteUse(std::string_view skillId)
{
    if (m_Data == nullptr || m_Data->FindSkill(skillId) == nullptr)
        return RankOf(skillId);
    bool counted = false;
    for (usize i = 0; i < m_Practiced.size(); ++i) {
        if (m_Practiced[i] == skillId) {
            if (m_Uses[i] < kXpCap)
                ++m_Uses[i];
            counted = true;
            break;
        }
    }
    if (!counted) {
        m_Practiced.emplace_back(skillId);
        m_Uses.push_back(1);
    }
    SystemEvent event;
    event.Kind = SystemEventKind::SkillUse;
    event.Id = std::string(skillId);
    if (const SkillDef* skill = m_Data->FindSkill(skillId))
        event.Tags = skill->Tags;
    TellSystem(event);
    return RankOf(skillId);
}

void Hero::NoteKill(std::string_view enemyId)
{
    if (m_Data == nullptr || enemyId.empty())
        return;
    SystemEvent event;
    event.Kind = SystemEventKind::Kill;
    event.Id = std::string(enemyId);
    if (const EnemyDef* enemy = m_Data->FindEnemy(enemyId))
        event.Tags = enemy->Tags;
    TellSystem(event);
}

void Hero::NoteItem(std::string_view itemId)
{
    if (m_Data == nullptr || itemId.empty())
        return;
    SystemEvent event;
    event.Kind = SystemEventKind::ItemUse;
    event.Id = std::string(itemId);
    if (const ItemDef* item = m_Data->FindItem(itemId))
        event.Tags = item->Tags;
    TellSystem(event);
}

bool Hero::AcceptTitle(std::string_view id, std::string& error)
{
    if (m_Data == nullptr) {
        error = "the hero is not ready";
        return false;
    }
    return Ascension::AcceptTitle(m_System, *m_Data, id, error);
}

bool Hero::AcceptSystemSkill(std::string_view offerId, std::string& error)
{
    if (m_Data == nullptr) {
        error = "the hero is not ready";
        return false;
    }
    return Ascension::AcceptSystemSkill(m_System, *m_Data, m_ClassId, Level(), offerId, error);
}

i32 Hero::FreePool() const
{
    if (m_Data == nullptr)
        return 0;
    const ClassDef* klass = m_Data->FindClass(m_ClassId);
    if (klass == nullptr)
        return 0;
    return FreePointsUnspent(*m_Data, *klass, Level(), m_System.FreeFloor, m_System.FreeSpent);
}

bool Hero::SpendFreePoint(std::string_view statId, std::string& error)
{
    if (m_ChoosingStarter) {
        error = "pick a class before spending free points";
        return false;
    }
    if (FreePool() <= 0) {
        error = "no free points left";
        return false;
    }
    StatBlock probe;
    if (FindStatValue(probe, statId) == nullptr) {
        error = "stat '" + std::string(statId) + "' does not exist";
        return false;
    }
    i32* value = FindStatValue(m_System.FreeSpent, statId);
    if (value == nullptr || *value >= 9999) {
        error = "stat '" + std::string(statId) + "' is full";
        return false;
    }
    *value += 1;
    error.clear();
    return true;
}

std::vector<TagDamage> Hero::TitleDamage() const
{
    if (m_Data == nullptr)
        return {};
    return TitleTagDamage(*m_Data, m_System);
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
    for (const std::string& kept : PickedIds()) {
        if (!Lists(ids, kept))
            ids.push_back(kept);
    }
    for (const std::string& extra : m_System.GrantedSkills) {
        if (!Lists(ids, extra))
            ids.push_back(extra);
    }
    return ids;
}

std::vector<std::string> Hero::PickedIds() const
{
    std::vector<std::string> ids;
    if (m_Data == nullptr)
        return ids;
    const ClassDef* klass = m_Data->FindClass(m_ClassId);
    if (klass == nullptr)
        return ids;
    for (const SkillPick* pick : m_Data->PicksOf(*klass)) {
        for (const SkillGrant& option : pick->Options) {
            if (Lists(m_Picked, option.Id) && !Lists(ids, option.Id))
                ids.push_back(option.Id);
        }
    }
    return ids;
}

bool Hero::PickPending() const
{
    return !PickOptions().empty();
}

std::vector<std::string> Hero::PickOptions() const
{
    std::vector<std::string> ids;
    if (m_Data == nullptr || m_ChoosingStarter)
        return ids;
    const ClassDef* klass = m_Data->FindClass(m_ClassId);
    if (klass == nullptr)
        return ids;
    const i32 level = Level();
    const SkillPick* open = nullptr;
    for (const SkillPick* pick : m_Data->PicksOf(*klass)) {
        if (pick->Level > level)
            continue;
        bool kept = false;
        for (const SkillGrant& option : pick->Options)
            kept = kept || Lists(m_Picked, option.Id);
        if (kept)
            continue;
        if (open == nullptr || pick->Level < open->Level)
            open = pick;
    }
    if (open == nullptr)
        return ids;
    for (const SkillGrant& option : open->Options)
        ids.push_back(option.Id);
    return ids;
}

bool Hero::Pick(std::string_view skillId, std::string& error)
{
    const std::vector<std::string> options = PickOptions();
    if (options.empty()) {
        error = "no skill pick is open";
        return false;
    }
    if (!Lists(options, skillId)) {
        error = "skill '" + std::string(skillId) + "' is not offered";
        return false;
    }
    m_Picked.emplace_back(skillId);
    error.clear();
    return true;
}

bool Hero::ChoicePending() const
{
    return !ChoiceIds().empty();
}

std::vector<std::string> Hero::ChoiceIds() const
{
    std::vector<std::string> ids;
    if (m_Data == nullptr)
        return ids;
    const ClassDef* klass = m_Data->FindClass(m_ClassId);
    if (klass == nullptr || klass->IsEvolution())
        return ids;
    const i32 level = Level();
    for (const ClassDef* evolution : m_Data->EvolutionsOf(m_ClassId)) {
        if (evolution != nullptr && EvolutionOffered(m_System, *evolution, level))
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
    if (m_Data != nullptr)
        EvaluateSystem(m_System, *m_Data, m_ClassId, Level());
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
    m_Picked.clear();
    m_System.GrantedSkills.clear();
    m_System.SkillOffers.clear();
    if (m_Data != nullptr)
        EvaluateSystem(m_System, *m_Data, m_ClassId, Level());
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
    if (m_Data != nullptr)
        EvaluateSystem(m_System, *m_Data, m_ClassId, Level());
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

bool Hero::AdoptStarter(std::string_view classId, std::string& error)
{
    if (m_Data == nullptr) {
        error = "the hero is not ready";
        return false;
    }
    if (m_Custom || m_ChoosingStarter) {
        error = "a custom build picks a starter with ChooseStarter";
        return false;
    }
    const std::string picked(classId);
    const ClassDef* starter = m_Data->FindClass(picked);
    if (starter == nullptr || !starter->Starter || starter->IsEvolution()) {
        error = "class '" + picked + "' is not a starter";
        return false;
    }
    if (m_ClassId != picked) {
        m_ClassId = picked;
        m_Picked.clear();
    }
    EvaluateSystem(m_System, *m_Data, m_ClassId, Level());
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
    stats += m_System.FreeSpent;
    if (m_Data != nullptr)
        stats += TitleStatBonus(*m_Data, m_System);
    return stats;
}

const char* Hero::NextFightBlock() const
{
    if (m_ChoosingStarter)
        return "Choose a class before the next fight.";
    if (m_ClassPool > 0)
        return "Spend class points before the next fight.";
    if (FreePool() > 0)
        return "Spend free points before the next fight.";
    if (ChoicePending())
        return "Choose a path before the next fight.";
    if (PickPending())
        return "Pick a skill before the next fight.";
    if (TitlePending())
        return "The System offers a title.";
    if (SystemSkillPending())
        return "The System offers a skill.";
    return nullptr;
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
    const bool picking = PickPending();
    const i64 sum = static_cast<i64>(m_Xp) + amount;
    m_Xp = sum > kXpCap ? kXpCap : static_cast<i32>(sum);
    grant.Amount = amount;
    grant.LevelAfter = Level();
    if (grant.LevelAfter > grant.LevelBefore) {
        SystemEvent event;
        event.Kind = SystemEventKind::LevelUp;
        event.Amount = grant.LevelAfter;
        TellSystem(event);
    }
    grant.ChoiceOpened = !pending && ChoicePending();
    grant.PickOpened = !picking && PickPending();
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

void Hero::ExportCareer(CareerRecord& out) const
{
    out.Name = m_Name;
    out.ClassId = m_ClassId;
    out.Xp = m_Xp;
    out.Practiced = m_Practiced;
    out.Uses = m_Uses;
    out.Picked = m_Picked;
    out.Custom = m_Custom;
    out.ChoosingStarter = m_ChoosingStarter;
    out.ClassPool = m_ClassPool;
    out.SpendId = m_SpendId;
    if (m_Custom) {
        out.LevelSteps = m_LevelSteps;
        out.Sources = m_ClassSources;
    } else {
        out.LevelSteps.clear();
        out.Sources.clear();
    }
    out.System = m_System;
    out.FreeLegacy = false;
}

bool Hero::ImportCareer(const CareerRecord& in, std::string& error)
{
    if (m_Data == nullptr) {
        error = "the hero is not ready";
        return false;
    }
    if (in.ClassId.empty() || m_Data->FindClass(in.ClassId) == nullptr) {
        error = "class '" + in.ClassId + "' does not exist";
        return false;
    }
    if (in.Practiced.size() != in.Uses.size()) {
        error = "the save's skill list is damaged";
        return false;
    }
    m_ClassId = in.ClassId;
    if (in.Name.empty()) {
        m_Name.clear();
    } else {
        const HeroNameCheck check = CheckHeroName(in.Name);
        m_Name = check.Status == HeroNameStatus::Ok ? check.Name : std::string{};
    }
    m_Xp = in.Xp < 0 ? 0 : in.Xp;
    if (m_Xp > kXpCap)
        m_Xp = kXpCap;
    m_Practiced = in.Practiced;
    m_Uses = in.Uses;
    for (i32& uses : m_Uses) {
        if (uses < 0)
            uses = 0;
    }
    m_Picked = in.Picked;
    m_Custom = in.Custom;
    m_ChoosingStarter = in.ChoosingStarter;
    m_ClassPool = in.ClassPool < 0 ? 0 : in.ClassPool;
    m_SpendId = in.SpendId;
    if (m_Custom) {
        m_LevelSteps = in.LevelSteps;
        m_ClassSources = in.Sources;
    } else {
        m_LevelSteps.clear();
        m_ClassSources.clear();
        m_ClassPool = 0;
        m_SpendId.clear();
        m_ChoosingStarter = false;
    }
    m_System = in.System;
    if (m_System.FreeFloor < 1)
        m_System.FreeFloor = 1;
    if (in.FreeLegacy)
        m_System.FreeFloor = Level();
    return ApplyData(error);
}

} // namespace Ascension
