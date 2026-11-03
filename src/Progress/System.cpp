#include "Ascension/Progress/System.h"

namespace Ascension {

namespace {

constexpr i32 kCountCap = 999999;

bool Listed(const std::vector<std::string>& ids, std::string_view id)
{
    for (const std::string& held : ids) {
        if (held == id)
            return true;
    }
    return false;
}

const char* KindPrefix(SystemEventKind kind)
{
    if (kind == SystemEventKind::SkillUse)
        return "skill";
    if (kind == SystemEventKind::ItemUse)
        return "item";
    if (kind == SystemEventKind::LevelUp)
        return "level";
    return "kill";
}

std::string IdKey(SystemEventKind kind, std::string_view id)
{
    std::string key = KindPrefix(kind);
    key.push_back(':');
    key.append(id);
    return key;
}

std::string KindTagKey(SystemEventKind kind, std::string_view tag)
{
    std::string key = KindPrefix(kind);
    key.append(".tag:");
    key.append(tag);
    return key;
}

std::string SharedTagKey(std::string_view tag)
{
    std::string key = "tag:";
    key.append(tag);
    return key;
}

SystemCounter* FindCounter(SystemState& state, std::string_view key)
{
    for (SystemCounter& counter : state.Counters) {
        if (counter.Key == key)
            return &counter;
    }
    return nullptr;
}

const SystemCounter* FindCounter(const SystemState& state, std::string_view key)
{
    for (const SystemCounter& counter : state.Counters) {
        if (counter.Key == key)
            return &counter;
    }
    return nullptr;
}

i32 ReadCount(const SystemState& state, std::string_view key)
{
    const SystemCounter* counter = FindCounter(state, key);
    return counter != nullptr ? counter->Count : 0;
}

void AddCount(SystemState& state, const std::string& key, i32 amount)
{
    if (key.empty() || amount <= 0)
        return;
    SystemCounter* counter = FindCounter(state, key);
    if (counter == nullptr) {
        SystemCounter added;
        added.Key = key;
        added.Count = amount > kCountCap ? kCountCap : amount;
        state.Counters.push_back(std::move(added));
        return;
    }
    const i64 sum = static_cast<i64>(counter->Count) + amount;
    counter->Count = sum > kCountCap ? kCountCap : static_cast<i32>(sum);
}

void RaiseLevel(SystemState& state, i32 level)
{
    if (level < 1)
        return;
    SystemCounter* counter = FindCounter(state, "level");
    if (counter == nullptr) {
        SystemCounter added;
        added.Key = "level";
        added.Count = level;
        state.Counters.push_back(std::move(added));
        return;
    }
    if (level > counter->Count)
        counter->Count = level;
}

bool DeedMet(const SystemState& state, const DeedDef& deed)
{
    i32 have = 0;
    if (deed.On == DeedOn::Level) {
        have = ReadCount(state, "level");
    } else if (deed.On == DeedOn::Tag) {
        have = ReadCount(state, SharedTagKey(deed.Tag));
    } else {
        SystemEventKind kind = SystemEventKind::Kill;
        if (deed.On == DeedOn::Skill)
            kind = SystemEventKind::SkillUse;
        else if (deed.On == DeedOn::Item)
            kind = SystemEventKind::ItemUse;
        if (!deed.Tag.empty())
            have = ReadCount(state, KindTagKey(kind, deed.Tag));
        else
            have = ReadCount(state, IdKey(kind, deed.Id));
        // A deed that names both an id and a tag needs each count to reach the threshold.
        if (!deed.Tag.empty() && !deed.Id.empty()) {
            const i32 byId = ReadCount(state, IdKey(kind, deed.Id));
            if (byId < have)
                have = byId;
        }
    }
    return have >= deed.AtLeast;
}

const ClassDef* ParentOf(const Database& data, const ClassDef& klass)
{
    if (!klass.IsEvolution())
        return nullptr;
    return data.FindClass(klass.EvolvesFrom);
}

bool ChainGrants(const Database& data, std::string_view classId, std::string_view skillId)
{
    const ClassDef* klass = data.FindClass(classId);
    if (klass == nullptr)
        return false;
    const ClassDef* parent = ParentOf(data, *klass);
    for (const SkillDef* skill : data.ActiveSkills(*klass)) {
        if (skill->Id == skillId)
            return true;
    }
    for (const SkillDef* skill : data.Passives(*klass)) {
        if (skill->Id == skillId)
            return true;
    }
    if (parent == nullptr)
        return false;
    for (const SkillDef* skill : data.ActiveSkills(*parent)) {
        if (skill->Id == skillId)
            return true;
    }
    for (const SkillDef* skill : data.Passives(*parent)) {
        if (skill->Id == skillId)
            return true;
    }
    return false;
}

void CollectRules(const Database& data, std::string_view classId,
                  std::vector<const SystemSkillRule*>& out)
{
    const ClassDef* klass = data.FindClass(classId);
    if (klass == nullptr)
        return;
    for (const SystemSkillRule& rule : klass->SystemSkills)
        out.push_back(&rule);
    if (const ClassDef* parent = ParentOf(data, *klass)) {
        for (const SystemSkillRule& rule : parent->SystemSkills)
            out.push_back(&rule);
    }
}

const SystemSkillRule* RuleOnChain(const Database& data, std::string_view classId,
                                   std::string_view offerId)
{
    std::vector<const SystemSkillRule*> rules;
    CollectRules(data, classId, rules);
    for (const SystemSkillRule* rule : rules) {
        if (rule->Id == offerId)
            return rule;
    }
    return nullptr;
}

i32 RarityOf(const Database& data, const ClassDef& owner)
{
    const ClassDef* klass = &owner;
    if (klass->Rarity.empty() && klass->IsEvolution()) {
        if (const ClassDef* parent = ParentOf(data, *klass))
            klass = parent;
    }
    return RarityFreePoints(data, klass->Rarity);
}

} // namespace

void DispatchSystem(SystemState& state, const Database& data, std::string_view classId, i32 level,
                    const SystemEvent& event)
{
    if (event.Kind == SystemEventKind::LevelUp) {
        RaiseLevel(state, event.Amount);
        EvaluateSystem(state, data, classId, level);
        return;
    }
    const i32 amount = event.Amount > 0 ? event.Amount : 0;
    if (amount == 0) {
        EvaluateSystem(state, data, classId, level);
        return;
    }
    if (!event.Id.empty())
        AddCount(state, IdKey(event.Kind, event.Id), amount);
    for (const std::string& tag : event.Tags) {
        if (tag.empty())
            continue;
        AddCount(state, KindTagKey(event.Kind, tag), amount);
        if (event.Kind == SystemEventKind::SkillUse || event.Kind == SystemEventKind::ItemUse)
            AddCount(state, SharedTagKey(tag), amount);
    }
    EvaluateSystem(state, data, classId, level);
}

void EvaluateSystem(SystemState& state, const Database& data, std::string_view classId, i32 level)
{
    (void)level;
    std::vector<std::string> titles;
    for (const std::string& id : state.TitleOffers) {
        if (data.FindTitle(id) != nullptr)
            titles.push_back(id);
    }
    state.TitleOffers = std::move(titles);
    for (const TitleDef& title : data.GetTitles()) {
        if (Listed(state.Titles, title.Id) || Listed(state.TitleOffers, title.Id))
            continue;
        if (DeedMet(state, title.Deed))
            state.TitleOffers.push_back(title.Id);
    }

    std::vector<const SystemSkillRule*> rules;
    CollectRules(data, classId, rules);
    std::vector<std::string> skills;
    for (const std::string& offerId : state.SkillOffers) {
        const SystemSkillRule* rule = RuleOnChain(data, classId, offerId);
        if (rule == nullptr)
            continue;
        if (Listed(state.GrantedSkills, rule->Skill) || ChainGrants(data, classId, rule->Skill))
            continue;
        skills.push_back(offerId);
    }
    state.SkillOffers = std::move(skills);
    for (const SystemSkillRule* rule : rules) {
        if (rule == nullptr)
            continue;
        if (Listed(state.SkillOffers, rule->Id) || Listed(state.GrantedSkills, rule->Skill))
            continue;
        if (ChainGrants(data, classId, rule->Skill))
            continue;
        if (DeedMet(state, rule->Deed))
            state.SkillOffers.push_back(rule->Id);
    }
}

i32 SystemCount(const SystemState& state, SystemEventKind kind, std::string_view id,
                std::string_view tag)
{
    if (kind == SystemEventKind::LevelUp)
        return ReadCount(state, "level");
    if (!tag.empty())
        return ReadCount(state, KindTagKey(kind, tag));
    if (!id.empty())
        return ReadCount(state, IdKey(kind, id));
    return 0;
}

i32 SystemTagCount(const SystemState& state, std::string_view tag)
{
    return ReadCount(state, SharedTagKey(tag));
}

i32 RarityFreePoints(const Database& data, std::string_view rarityId)
{
    if (rarityId.empty())
        return 0;
    const RarityDef* rarity = data.FindRarity(rarityId);
    if (rarity == nullptr || rarity->FreePoints < 0)
        return 0;
    return rarity->FreePoints;
}

i32 FreePointsEarned(const Database& data, const ClassDef& klass, i32 level)
{
    const i32 count = level > 1 ? level - 1 : 0;
    i32 total = 0;
    for (i32 step = 1; step <= count; ++step) {
        const ClassDef* owner = &klass;
        if (klass.IsEvolution() && step < klass.EvolveLevel) {
            if (const ClassDef* parent = ParentOf(data, klass))
                owner = parent;
        }
        total += RarityOf(data, *owner);
    }
    return total;
}

i32 FreePointsUnspent(const Database& data, const ClassDef& klass, i32 level, i32 floor,
                      const StatBlock& spent)
{
    if (floor < 1)
        floor = 1;
    i32 earned = FreePointsEarned(data, klass, level) - FreePointsEarned(data, klass, floor);
    if (earned < 0)
        earned = 0;
    const i32 used = StatTotal(spent);
    if (used >= earned)
        return 0;
    return earned - used;
}

bool EvolutionOffered(const SystemState& state, const ClassDef& evolution, i32 level)
{
    if (!evolution.IsEvolution() || level < evolution.EvolveLevel)
        return false;
    if (evolution.Offer == EvolutionOffer::Guaranteed)
        return true;
    return DeedMet(state, evolution.Meld);
}

StatBlock TitleStatBonus(const Database& data, const SystemState& state)
{
    StatBlock bonus;
    for (const std::string& id : state.Titles) {
        const TitleDef* title = data.FindTitle(id);
        if (title == nullptr || title->Bonus.Stat.empty() || title->Bonus.Amount == 0)
            continue;
        const i32 have = bonus.Get(title->Bonus.Stat);
        bonus.Set(title->Bonus.Stat, have + title->Bonus.Amount);
    }
    return bonus;
}

std::vector<TagDamage> TitleTagDamage(const Database& data, const SystemState& state)
{
    std::vector<TagDamage> rows;
    for (const std::string& id : state.Titles) {
        const TitleDef* title = data.FindTitle(id);
        if (title == nullptr || title->Bonus.DamagePercent == 0 || title->Bonus.Against.empty())
            continue;
        TagDamage row;
        row.Tag = title->Bonus.Against;
        row.Percent = title->Bonus.DamagePercent;
        rows.push_back(std::move(row));
    }
    return rows;
}

const SystemSkillRule* FindSystemSkillRule(const Database& data, std::string_view classId,
                                           std::string_view offerId)
{
    return RuleOnChain(data, classId, offerId);
}

bool AcceptTitle(SystemState& state, const Database& data, std::string_view id, std::string& error)
{
    if (!Listed(state.TitleOffers, id)) {
        error = "title '" + std::string(id) + "' is not offered";
        return false;
    }
    if (data.FindTitle(id) == nullptr) {
        error = "title '" + std::string(id) + "' does not exist";
        return false;
    }
    std::vector<std::string> left;
    for (const std::string& offer : state.TitleOffers) {
        if (offer != id)
            left.push_back(offer);
    }
    state.TitleOffers = std::move(left);
    if (!Listed(state.Titles, id))
        state.Titles.emplace_back(id);
    error.clear();
    return true;
}

bool AcceptSystemSkill(SystemState& state, const Database& data, std::string_view classId,
                       i32 level, std::string_view offerId, std::string& error)
{
    if (!Listed(state.SkillOffers, offerId)) {
        error = "skill '" + std::string(offerId) + "' is not offered";
        return false;
    }
    const SystemSkillRule* rule = RuleOnChain(data, classId, offerId);
    if (rule == nullptr) {
        error = "skill '" + std::string(offerId) + "' is not offered";
        return false;
    }
    if (!Listed(state.GrantedSkills, rule->Skill))
        state.GrantedSkills.emplace_back(rule->Skill);
    std::vector<std::string> left;
    for (const std::string& offer : state.SkillOffers) {
        if (offer != offerId)
            left.push_back(offer);
    }
    state.SkillOffers = std::move(left);
    EvaluateSystem(state, data, classId, level);
    error.clear();
    return true;
}

} // namespace Ascension
