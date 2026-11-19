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
    if (kind == SystemEventKind::Mark)
        return "mark";
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

std::string FillNotice(std::string_view pattern, std::string_view name)
{
    std::string out(pattern);
    const std::string_view token = "{name}";
    for (;;) {
        const usize at = out.find(token);
        if (at == std::string::npos)
            return out;
        out.replace(at, token.size(), name);
    }
}

void PushNotice(SystemState& state, i32 keep, std::string text)
{
    if (text.empty())
        return;
    state.Pending.push_back(text);
    state.Log.push_back(std::move(text));
    if (keep < 1)
        keep = 1;
    while (static_cast<i32>(state.Log.size()) > keep)
        state.Log.erase(state.Log.begin());
}

bool TagListed(const std::vector<std::string>& tags, std::string_view tag)
{
    for (const std::string& have : tags) {
        if (have == tag)
            return true;
    }
    return false;
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

const ClassDef* RarityOwner(const Database& data, const ClassDef& owner)
{
    const ClassDef* klass = &owner;
    if (klass->Rarity.empty() && klass->IsEvolution()) {
        if (const ClassDef* parent = ParentOf(data, *klass))
            klass = parent;
    }
    return klass;
}

i32 RarityOf(const Database& data, const ClassDef& owner)
{
    return RarityFreePoints(data, RarityOwner(data, owner)->Rarity);
}

void FillTitleFact(const SystemState& state, TitleFact& fact)
{
    fact.Owned = Listed(state.Titles, fact.Id);
    fact.Equipped = false;
    fact.Slot = -1;
    for (i32 index = 0; index < static_cast<i32>(state.Equipped.size()); ++index) {
        if (state.Equipped[static_cast<usize>(index)] != fact.Id)
            continue;
        fact.Equipped = true;
        fact.Slot = index;
        return;
    }
}

void Announce(SystemState& state, const Database& data, std::string_view classId, i32 level,
             const std::vector<std::string>& titlesBefore, const std::vector<std::string>& skillsBefore)
{
    const SystemRules& rules = data.GetSystem();
    for (const std::string& id : state.TitleOffers) {
        if (Listed(titlesBefore, id))
            continue;
        const TitleDef* title = data.FindTitle(id);
        const std::string name = title != nullptr ? title->Name : id;
        PushNotice(state, rules.LogKeep, FillNotice(rules.TitleNotice, name));
    }
    for (const std::string& offerId : state.SkillOffers) {
        if (Listed(skillsBefore, offerId))
            continue;
        const SystemSkillRule* rule = RuleOnChain(data, classId, offerId);
        std::string name = offerId;
        if (rule != nullptr) {
            if (const SkillDef* skill = data.FindSkill(rule->Skill))
                name = skill->Name;
            else
                name = rule->Skill;
        }
        PushNotice(state, rules.LogKeep, FillNotice(rules.SkillNotice, name));
    }

    const ClassDef* klass = data.FindClass(classId);
    if (klass == nullptr || klass->IsEvolution()) {
        state.NoticesSettled = false;
        return;
    }
    std::vector<std::string> open;
    for (const ClassDef* evolution : data.EvolutionsOf(classId)) {
        if (evolution == nullptr || evolution->Offer != EvolutionOffer::Meld)
            continue;
        if (!EvolutionOffered(state, *evolution, level))
            continue;
        open.push_back(evolution->Id);
    }
    if (state.NoticesSettled) {
        state.NotedMelds = std::move(open);
        state.NoticesSettled = false;
        return;
    }
    for (const std::string& id : open) {
        if (Listed(state.NotedMelds, id))
            continue;
        state.NotedMelds.push_back(id);
        const ClassDef* evolution = data.FindClass(id);
        const std::string name = evolution != nullptr ? evolution->Name : id;
        PushNotice(state, rules.LogKeep, FillNotice(rules.MeldNotice, name));
    }
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
    const std::vector<std::string> titlesBefore = state.TitleOffers;
    const std::vector<std::string> skillsBefore = state.SkillOffers;
    std::vector<std::string> titles;
    for (const std::string& id : state.TitleOffers) {
        if (data.FindTitle(id) != nullptr)
            titles.push_back(id);
    }
    state.TitleOffers = std::move(titles);
    for (const TitleDef& title : data.GetTitles()) {
        if (Listed(state.Titles, title.Id) || Listed(state.TitleOffers, title.Id))
            continue;
        if (DeedHolds(state, classId, level, title.Deed))
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
        if (DeedHolds(state, classId, level, rule->Deed))
            state.SkillOffers.push_back(rule->Id);
    }
    Announce(state, data, classId, level, titlesBefore, skillsBefore);
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
    return DeedHolds(state, evolution.EvolvesFrom, level, evolution.Meld);
}

i32 TitleSlotCount(const Database& data, std::string_view classId)
{
    const ClassDef* klass = data.FindClass(classId);
    if (klass == nullptr)
        return 0;
    const ClassDef* owner = RarityOwner(data, *klass);
    const RarityDef* rarity = data.FindRarity(owner->Rarity);
    if (rarity == nullptr || rarity->TitleSlots < 0)
        return 0;
    return rarity->TitleSlots;
}

void ClampEquippedTitles(SystemState& state, const Database& data, std::string_view classId)
{
    std::vector<std::string> kept;
    for (const std::string& id : state.Equipped) {
        if (!Listed(state.Titles, id) || Listed(kept, id))
            continue;
        kept.push_back(id);
    }
    const i32 slots = TitleSlotCount(data, classId);
    if (static_cast<i32>(kept.size()) > slots)
        kept.resize(static_cast<usize>(slots < 0 ? 0 : slots));
    state.Equipped = std::move(kept);
}

ModifierStack StackModifiers(std::span<const Modifier> mods)
{
    ModifierStack stack;
    for (const Modifier& mod : mods) {
        if (mod.Kind == ModKind::StatFlat) {
            if (mod.Stat.empty() || mod.Amount == 0)
                continue;
            stack.Flat.Set(mod.Stat, stack.Flat.Get(mod.Stat) + mod.Amount);
        } else if (mod.Kind == ModKind::StatPercent) {
            if (mod.Stat.empty() || mod.Amount == 0)
                continue;
            stack.Percent.Set(mod.Stat, stack.Percent.Get(mod.Stat) + mod.Amount);
        } else if (mod.Kind == ModKind::DamageTag) {
            if (mod.Stat.empty() || mod.Amount == 0)
                continue;
            bool found = false;
            for (TagDamage& row : stack.Damage) {
                if (row.Tag != mod.Stat)
                    continue;
                row.Percent += mod.Amount;
                found = true;
                break;
            }
            if (!found) {
                TagDamage row;
                row.Tag = mod.Stat;
                row.Percent = mod.Amount;
                stack.Damage.push_back(std::move(row));
            }
        } else if (mod.Kind == ModKind::XpPercent) {
            stack.XpPercent += mod.Amount;
        } else if (mod.Kind == ModKind::GoldPercent) {
            stack.GoldPercent += mod.Amount;
        } else if (mod.Kind == ModKind::DropPercent) {
            stack.DropPercent += mod.Amount;
        }
    }
    return stack;
}

void ApplyTitleModifiers(StatBlock& stats, const ModifierStack& stack)
{
    for (const std::string_view id : kStatIds) {
        i32 value = stats.Get(id);
        const i32 percent = stack.Percent.Get(id);
        if (percent != 0 && value > 0) {
            const i64 scaled = static_cast<i64>(value) * (100 + percent);
            value = static_cast<i32>(scaled / 100);
            if (value < 0)
                value = 0;
        }
        value += stack.Flat.Get(id);
        if (value < 0)
            value = 0;
        if (value > 999999)
            value = 999999;
        stats.Set(id, value);
    }
}

i32 ScaleByPercent(i32 amount, i32 percent)
{
    if (amount <= 0 || percent == 0)
        return amount < 0 ? 0 : amount;
    i64 scaled = static_cast<i64>(amount) * (100 + percent) / 100;
    if (scaled > 999999)
        scaled = 999999;
    if (scaled < 0)
        scaled = 0;
    return static_cast<i32>(scaled);
}

ModifierStack TitleModifiers(const Database& data, const SystemState& state)
{
    std::vector<Modifier> mods;
    for (const std::string& id : state.Equipped) {
        const TitleDef* title = data.FindTitle(id);
        if (title == nullptr)
            continue;
        for (const Modifier& mod : title->Bonus)
            mods.push_back(mod);
    }
    return StackModifiers(mods);
}

StatBlock TitleStatBonus(const Database& data, const SystemState& state)
{
    return TitleModifiers(data, state).Flat;
}

std::vector<TagDamage> TitleTagDamage(const Database& data, const SystemState& state)
{
    return TitleModifiers(data, state).Damage;
}

namespace {

void AddTag(std::vector<std::string>& tags, std::string_view tag)
{
    if (tag.empty() || TagListed(tags, tag))
        return;
    tags.emplace_back(tag);
}

} // namespace

void NoteHabit(SystemState& state, const Database& data, std::string_view classId, i32 level,
               const HabitNote& note)
{
    if (note.Kind.empty() || note.Amount <= 0)
        return;
    const SystemRules& rules = data.GetSystem();
    SystemEvent event;
    event.Kind = SystemEventKind::Mark;
    event.Amount = 1;
    if (note.Kind == kHabitDealt) {
        if (note.Amount < rules.HeavyAt)
            return;
        event.Tags.push_back(rules.TagFor(kHabitHeavy));
        DispatchSystem(state, data, classId, level, event);
        return;
    }
    if (note.Kind == kHabitRegion) {
        if (note.Id.empty() || SystemCount(state, SystemEventKind::Mark, note.Id, "") > 0)
            return;
        event.Id = note.Id;
        event.Tags.push_back(rules.TagFor(kHabitRegion));
        DispatchSystem(state, data, classId, level, event);
        return;
    }
    if (note.Kind == kHabitGold) {
        event.Amount = note.Amount;
        event.Tags.push_back(rules.TagFor(kHabitGold));
        DispatchSystem(state, data, classId, level, event);
        return;
    }
    event.Id = note.Id;
    event.Tags = note.Tags;
    AddTag(event.Tags, rules.TagFor(note.Kind));
    DispatchSystem(state, data, classId, level, event);
}

std::vector<std::string> DrainNotices(SystemState& state)
{
    std::vector<std::string> lines = std::move(state.Pending);
    state.Pending.clear();
    return lines;
}

std::vector<TitleFact> QueryTitles(const Database& data, const SystemState& state)
{
    std::vector<TitleFact> facts;
    for (const TitleDef& title : data.GetTitles()) {
        TitleFact fact;
        fact.Id = title.Id;
        fact.Name = title.Name;
        fact.Description = title.Description;
        fact.Bonus = title.Bonus;
        FillTitleFact(state, fact);
        facts.push_back(std::move(fact));
    }
    auto known = [&facts](std::string_view id) {
        for (const TitleFact& fact : facts) {
            if (fact.Id == id)
                return true;
        }
        return false;
    };
    for (const std::string& id : state.Titles) {
        if (known(id))
            continue;
        TitleFact fact;
        fact.Id = id;
        FillTitleFact(state, fact);
        facts.push_back(std::move(fact));
    }
    for (const std::string& id : state.Equipped) {
        if (known(id))
            continue;
        TitleFact fact;
        fact.Id = id;
        FillTitleFact(state, fact);
        facts.push_back(std::move(fact));
    }
    return facts;
}

bool FindTitleFact(const Database& data, const SystemState& state, std::string_view id,
                   TitleFact& out)
{
    if (id.empty())
        return false;
    if (const TitleDef* title = data.FindTitle(id)) {
        out = {};
        out.Id = title->Id;
        out.Name = title->Name;
        out.Description = title->Description;
        out.Bonus = title->Bonus;
        FillTitleFact(state, out);
        return true;
    }
    if (!Listed(state.Titles, id) && !Listed(state.Equipped, id))
        return false;
    out = {};
    out.Id = std::string(id);
    FillTitleFact(state, out);
    return true;
}

bool TitleOwned(const SystemState& state, std::string_view id)
{
    return Listed(state.Titles, id);
}

bool TitleIsEquipped(const SystemState& state, std::string_view id)
{
    return Listed(state.Equipped, id);
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

bool EquipTitle(SystemState& state, const Database& data, std::string_view classId,
                std::string_view id, std::string& error)
{
    if (!Listed(state.Titles, id)) {
        error = "title '" + std::string(id) + "' is not owned";
        return false;
    }
    if (data.FindTitle(id) == nullptr) {
        error = "title '" + std::string(id) + "' does not exist";
        return false;
    }
    if (Listed(state.Equipped, id)) {
        error = "title '" + std::string(id) + "' is already equipped";
        return false;
    }
    ClampEquippedTitles(state, data, classId);
    const i32 slots = TitleSlotCount(data, classId);
    if (static_cast<i32>(state.Equipped.size()) >= slots) {
        error = slots <= 0 ? "no title slot is open" : "every title slot is filled";
        return false;
    }
    state.Equipped.emplace_back(id);
    error.clear();
    return true;
}

bool UnequipTitle(SystemState& state, std::string_view id, std::string& error)
{
    if (!Listed(state.Equipped, id)) {
        error = "title '" + std::string(id) + "' is not equipped";
        return false;
    }
    std::vector<std::string> left;
    for (const std::string& worn : state.Equipped) {
        if (worn != id)
            left.push_back(worn);
    }
    state.Equipped = std::move(left);
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
