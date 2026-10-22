#include "Ascension/Progress/Vitals.h"

namespace Ascension {

namespace {

i32 ClampPool(i64 value)
{
    if (value < 0)
        return 0;
    if (value > 999999)
        return 999999;
    return static_cast<i32>(value);
}

i32 ClampTo(i32 value, i32 least, i32 most)
{
    if (value < least)
        return least;
    if (most >= least && value > most)
        return most;
    return value;
}

PoolCaps Normalized(PoolCaps caps)
{
    if (caps.MaxHp < 1)
        caps.MaxHp = 1;
    if (caps.MaxFocus < 0)
        caps.MaxFocus = 0;
    return caps;
}

void ApplyAlwaysStats(StatBlock& stats, const std::vector<HeroGrant>& grants)
{
    for (const HeroGrant& held : grants) {
        for (const EffectDef& effect : held.Skill.Effects) {
            if (effect.Trigger != EffectTrigger::Always || effect.Op != EffectOp::Stat)
                continue;
            if (!effect.Stat.has_value())
                continue;
            i32 value = stats.Get(*effect.Stat) + effect.Amount.value_or(0);
            if (value < 0)
                value = 0;
            stats.Set(*effect.Stat, value);
        }
    }
}

bool SamePoint(MapTile left, MapTile right)
{
    return left.X == right.X && left.Y == right.Y;
}

} // namespace

PoolCaps HeroPools(const Database& data, const Hero& hero, const Inventory& bag)
{
    PoolCaps caps;
    const ClassDef* klass = data.FindClass(hero.ClassId());
    if (klass == nullptr || !hero.IsReady()) {
        caps.MaxHp = 1;
        caps.MaxFocus = 0;
        return caps;
    }
    StatBlock stats = hero.Attributes();
    stats += bag.GearBonus();
    ApplyAlwaysStats(stats, data.HeroGrants(*klass, bag.WornIds(), hero.PickedIds()));
    const CombatRules& rules = data.GetRules();
    caps.MaxHp = ClampPool(static_cast<i64>(stats.Vitality) * rules.HpPerVitality);
    caps.MaxFocus = ClampPool(static_cast<i64>(stats.Focus) + rules.FocusBase);
    if (caps.MaxHp < 1)
        caps.MaxHp = 1;
    return caps;
}

void FillVitals(Vitals& vitals, PoolCaps caps)
{
    caps = Normalized(caps);
    vitals.Hp = caps.MaxHp;
    vitals.Focus = caps.MaxFocus;
    vitals.Known = true;
    vitals.ReturnToHub = false;
}

void KeepPools(Vitals& vitals, i32 hp, i32 focus, PoolCaps caps)
{
    caps = Normalized(caps);
    vitals.Hp = ClampTo(hp, 0, caps.MaxHp);
    vitals.Focus = ClampTo(focus, 0, caps.MaxFocus);
    vitals.Known = true;
    vitals.ReturnToHub = false;
}

void LoseToHub(Vitals& vitals, PoolCaps caps)
{
    FillVitals(vitals, caps);
    vitals.ReturnToHub = true;
}

void ApplyLevelHp(Vitals& vitals, i32 oldMaxHp, i32 newMaxHp, LevelHpRule rule)
{
    if (newMaxHp < 1)
        newMaxHp = 1;
    if (!vitals.Known) {
        vitals.Hp = newMaxHp;
        vitals.Known = true;
        return;
    }
    if (rule == LevelHpRule::Full) {
        vitals.Hp = newMaxHp;
    } else if (rule == LevelHpRule::Gained) {
        const i32 gained = newMaxHp > oldMaxHp ? newMaxHp - oldMaxHp : 0;
        const i64 sum = static_cast<i64>(vitals.Hp) + gained;
        vitals.Hp = sum > newMaxHp ? newMaxHp : static_cast<i32>(sum);
    }
    if (vitals.Hp > newMaxHp)
        vitals.Hp = newMaxHp;
    if (vitals.Hp < 0)
        vitals.Hp = 0;
}

void ClampFocus(Vitals& vitals, i32 maxFocus)
{
    if (maxFocus < 0)
        maxFocus = 0;
    vitals.Focus = ClampTo(vitals.Focus, 0, maxFocus);
}

PoolPair PoolsFrom(const Vitals& vitals, PoolCaps caps)
{
    caps = Normalized(caps);
    PoolPair pools;
    if (!vitals.Known) {
        pools.Hp = caps.MaxHp;
        pools.Focus = caps.MaxFocus;
        return pools;
    }
    pools.Hp = ClampTo(vitals.Hp, 0, caps.MaxHp);
    pools.Focus = ClampTo(vitals.Focus, 0, caps.MaxFocus);
    return pools;
}

bool RestOn(const MapDef& map, MapTile tile, Vitals& vitals, PoolCaps caps)
{
    const MapPoint* rest = nullptr;
    for (const MapPoint& point : map.Points) {
        if (point.Kind == MapPointKind::Rest && SamePoint(point.Tile, tile))
            rest = &point;
    }
    if (rest == nullptr)
        return false;
    FillVitals(vitals, caps);
    return true;
}

} // namespace Ascension
