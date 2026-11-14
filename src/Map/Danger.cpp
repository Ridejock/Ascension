#include "Ascension/Map/Danger.h"

#include "Ascension/Data/Database.h"

namespace Ascension {

namespace {

i32 ClampedCap(const DangerRules& rules)
{
    return rules.Cap < 1 ? 1 : rules.Cap;
}

u32 ThreatOf(const DangerRules& rules, i32 value)
{
    const i32 cap = ClampedCap(rules);
    if (value < 0)
        value = 0;
    if (value > cap)
        value = cap;
    if (rules.Curve == DangerCurve::Square) {
        if (value >= cap)
            return static_cast<u32>(cap);
        const u32 v = static_cast<u32>(value);
        return (v * v) / static_cast<u32>(cap);
    }
    return static_cast<u32>(value);
}

u32 StepRng(DangerRng& rng)
{
    u32 x = rng.State & 0x7fffffffu;
    if (x == 0)
        x = 1;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    x &= 0x7fffffffu;
    if (x == 0)
        x = 1;
    rng.State = x;
    return x;
}

} // namespace

const char* ToString(DangerCurve curve)
{
    return curve == DangerCurve::Square ? "square" : "linear";
}

i32 DangerRatingOf(const DangerRules& rules, std::string_view tileId)
{
    for (const DangerRating& rating : rules.Ratings) {
        if (rating.Tile == tileId)
            return rating.Rating;
    }
    return 0;
}

const DangerTable* FindDangerTable(const DangerRules& rules, std::string_view mapId,
                                   std::string_view tileId)
{
    const DangerTable* tileOnly = nullptr;
    for (const DangerTable& table : rules.Tables) {
        if (table.Tile != tileId)
            continue;
        if (!table.Map.empty()) {
            if (table.Map == mapId)
                return &table;
            continue;
        }
        if (tileOnly == nullptr)
            tileOnly = &table;
    }
    return tileOnly;
}

std::string PickDangerEncounter(const DangerTable& table, u32 roll)
{
    i32 total = 0;
    for (const DangerRow& row : table.Rows) {
        if (row.Weight > 0)
            total += row.Weight;
    }
    if (total < 1 || table.Rows.empty())
        return {};
    i32 at = static_cast<i32>(roll % static_cast<u32>(total));
    for (const DangerRow& row : table.Rows) {
        if (row.Weight < 1)
            continue;
        if (at < row.Weight)
            return row.Encounter;
        at -= row.Weight;
    }
    return table.Rows.back().Encounter;
}

const DangerHint* DangerHintFor(const DangerRules& rules, i32 value)
{
    const DangerHint* best = nullptr;
    for (const DangerHint& hint : rules.Hints) {
        if (value < hint.At)
            continue;
        if (best == nullptr || hint.At >= best->At)
            best = &hint;
    }
    return best;
}

DangerStep AdvanceDanger(DangerMeter& meter, const DangerRules& rules, i32 rating, u32 roll)
{
    DangerStep step;
    const i32 cap = ClampedCap(rules);
    if (rating < 0)
        rating = 0;
    step.Rating = rating;
    if (meter.Value < 0)
        meter.Value = 0;
    if (meter.Value > cap)
        meter.Value = cap;
    if (meter.Grace < 0)
        meter.Grace = 0;

    // A road, a floor, a dungeon stone: the meter does not grow and the
    // roll is not taken, even when the meter is already full.
    if (rating == 0) {
        meter.OnRoad = true;
        step.Value = meter.Value;
        step.Grace = meter.Grace;
        return step;
    }

    if (meter.OnRoad) {
        const i32 grace = rules.GraceSteps < 0 ? 0 : rules.GraceSteps;
        if (meter.Grace < grace)
            meter.Grace = grace;
        meter.OnRoad = false;
    }

    i32 add = rating;
    if (add > cap - meter.Value)
        add = cap - meter.Value;
    meter.Value += add;

    if (meter.Grace > 0) {
        --meter.Grace;
        step.Value = meter.Value;
        step.Grace = meter.Grace;
        return step;
    }

    u32 rolled = roll;
    const u32 span = static_cast<u32>(cap);
    if (rolled >= span)
        rolled %= span;
    step.Fight = rolled < ThreatOf(rules, meter.Value);
    step.Value = meter.Value;
    step.Grace = meter.Grace;
    return step;
}

void ResetDanger(DangerMeter& meter, const DangerRules& rules)
{
    meter.Value = 0;
    meter.Grace = rules.GraceSteps < 0 ? 0 : rules.GraceSteps;
    meter.OnRoad = false;
}

u32 NextDangerRoll(DangerRng& rng, u32 cap)
{
    const u32 rolled = StepRng(rng);
    if (cap == 0)
        return 0;
    return rolled % cap;
}

bool DangerEncounterFlees(const Database& data, std::string_view encounterId)
{
    const EncounterDef* encounter = data.FindEncounter(encounterId);
    if (encounter == nullptr)
        return true;
    for (const std::string& id : encounter->Enemies) {
        const EnemyDef* enemy = data.FindEnemy(id);
        if (enemy != nullptr && enemy->Boss)
            return false;
    }
    return true;
}

} // namespace Ascension
