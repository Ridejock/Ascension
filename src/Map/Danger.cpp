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

namespace {

bool SignMatches(const DangerSign& sign, std::string_view mapId, std::string_view tileId)
{
    if (!sign.Map.empty() && sign.Map != mapId)
        return false;
    if (!sign.Tile.empty() && sign.Tile != tileId)
        return false;
    return true;
}

const DangerSign* FindSign(const DangerRules& rules, std::string_view id)
{
    for (const DangerSign& sign : rules.Signs) {
        if (sign.Id == id)
            return &sign;
    }
    return nullptr;
}

bool SpentHowl(const DangerSignsState& state, std::string_view id)
{
    for (const std::string& spent : state.Spent) {
        if (spent == id)
            return true;
    }
    return false;
}

// A mapped sign of this kind that fits the meter hides the tile-only ones.
bool MappedKind(const DangerRules& rules, DangerSignKind kind, i32 value, std::string_view mapId,
                std::string_view tileId)
{
    for (const DangerSign& sign : rules.Signs) {
        if (sign.Kind != kind || sign.Map.empty() || value < sign.At)
            continue;
        if (SignMatches(sign, mapId, tileId))
            return true;
    }
    return false;
}

const DangerSign* BestSign(const DangerRules& rules, DangerSignKind kind, i32 value,
                           std::string_view mapId, std::string_view tileId)
{
    const bool mapped = MappedKind(rules, kind, value, mapId, tileId);
    const DangerSign* best = nullptr;
    for (const DangerSign& sign : rules.Signs) {
        if (sign.Kind != kind || value < sign.At)
            continue;
        if (!SignMatches(sign, mapId, tileId))
            continue;
        if (mapped && sign.Map.empty())
            continue;
        if (best == nullptr || sign.At >= best->At)
            best = &sign;
    }
    return best;
}

void AppendCue(std::string& line, std::string_view join, std::string_view part)
{
    if (part.empty())
        return;
    if (!line.empty())
        line.append(join);
    line.append(part);
}

const DangerSign* LiveHowl(const DangerRules& rules, const DangerSignsState& state, i32 value,
                           std::string_view mapId, std::string_view tileId)
{
    bool mapped = false;
    for (const DangerHowlHold& hold : state.Howls) {
        if (hold.Left < 1)
            continue;
        const DangerSign* sign = FindSign(rules, hold.SignId);
        if (sign == nullptr || sign->Kind != DangerSignKind::Howl || value < sign->At)
            continue;
        if (!sign->Map.empty() && SignMatches(*sign, mapId, tileId))
            mapped = true;
    }
    const DangerSign* best = nullptr;
    for (const DangerHowlHold& hold : state.Howls) {
        if (hold.Left < 1)
            continue;
        const DangerSign* sign = FindSign(rules, hold.SignId);
        if (sign == nullptr || sign->Kind != DangerSignKind::Howl || value < sign->At)
            continue;
        if (!SignMatches(*sign, mapId, tileId))
            continue;
        if (mapped && sign->Map.empty())
            continue;
        if (best == nullptr || sign->At >= best->At)
            best = sign;
    }
    return best;
}

void DropSpent(DangerSignsState& state, const DangerRules& rules, i32 value)
{
    std::vector<std::string> kept;
    kept.reserve(state.Spent.size());
    for (const std::string& id : state.Spent) {
        const DangerSign* sign = FindSign(rules, id);
        if (sign == nullptr || sign->Kind != DangerSignKind::Howl)
            continue;
        if (value < sign->At)
            continue;
        kept.push_back(id);
    }
    state.Spent = std::move(kept);
}

void DecayHowls(DangerSignsState& state)
{
    std::vector<DangerHowlHold> kept;
    kept.reserve(state.Howls.size());
    for (DangerHowlHold& hold : state.Howls) {
        if (hold.Left > 0)
            --hold.Left;
        if (hold.Left > 0)
            kept.push_back(std::move(hold));
    }
    state.Howls = std::move(kept);
}

} // namespace

std::string DangerCueLine(const DangerRules& rules, const DangerSignsState& state, i32 value,
                          std::string_view mapId, std::string_view tileId)
{
    if (value < 1 || DangerRatingOf(rules, tileId) < 1)
        return {};
    std::string hint;
    if (const DangerSign* terrain = BestSign(rules, DangerSignKind::Hint, value, mapId, tileId))
        hint = terrain->Text;
    else if (const DangerHint* word = DangerHintFor(rules, value))
        hint = word->Text;
    std::string tracks;
    if (const DangerSign* trail = BestSign(rules, DangerSignKind::Tracks, value, mapId, tileId))
        tracks = trail->Text;
    std::string howl;
    if (const DangerSign* cry = LiveHowl(rules, state, value, mapId, tileId))
        howl = cry->Text;
    const std::string_view join =
        rules.CueJoin.empty() ? kDefaultCueJoin : std::string_view(rules.CueJoin);
    std::string line;
    AppendCue(line, join, hint);
    AppendCue(line, join, tracks);
    AppendCue(line, join, howl);
    return line;
}

void CollectDangerMarks(const DangerRules& rules, i32 value, std::string_view mapId,
                        std::string_view tileId, i32 tileX, i32 tileY,
                        std::vector<DangerMarkSpot>& out)
{
    out.clear();
    if (value < 1 || DangerRatingOf(rules, tileId) < 1)
        return;
    const DangerSign* trail = BestSign(rules, DangerSignKind::Tracks, value, mapId, tileId);
    if (trail == nullptr || !trail->HasColor)
        return;
    for (const DangerMark& mark : trail->Marks) {
        if (mark.Size < 1)
            continue;
        DangerMarkSpot spot;
        spot.MapId = std::string(mapId);
        spot.TileX = tileX;
        spot.TileY = tileY;
        spot.Red = trail->Red;
        spot.Green = trail->Green;
        spot.Blue = trail->Blue;
        spot.X = mark.X;
        spot.Y = mark.Y;
        spot.Size = mark.Size;
        out.push_back(std::move(spot));
    }
}

void AdvanceDangerSigns(DangerSignsState& state, const DangerRules& rules, i32 value,
                        std::string_view mapId, std::string_view tileId)
{
    if (value < 0)
        value = 0;
    DecayHowls(state);
    DropSpent(state, rules, value);
    if (DangerRatingOf(rules, tileId) < 1)
        return;
    const bool mapped = MappedKind(rules, DangerSignKind::Howl, value, mapId, tileId);
    for (const DangerSign& sign : rules.Signs) {
        if (sign.Kind != DangerSignKind::Howl || value < sign.At)
            continue;
        if (!SignMatches(sign, mapId, tileId))
            continue;
        if (mapped && sign.Map.empty())
            continue;
        if (SpentHowl(state, sign.Id))
            continue;
        DangerHowlHold hold;
        hold.SignId = sign.Id;
        hold.Left = sign.Steps < 1 ? 1 : sign.Steps;
        state.Howls.push_back(std::move(hold));
        state.Spent.push_back(sign.Id);
    }
}

void SettleDangerSigns(DangerSignsState& state, const DangerRules& rules, i32 value,
                       std::string_view mapId, std::string_view tileId)
{
    state.Howls.clear();
    if (value < 0)
        value = 0;
    DropSpent(state, rules, value);
    if (DangerRatingOf(rules, tileId) < 1)
        return;
    const bool mapped = MappedKind(rules, DangerSignKind::Howl, value, mapId, tileId);
    for (const DangerSign& sign : rules.Signs) {
        if (sign.Kind != DangerSignKind::Howl || value < sign.At)
            continue;
        if (!SignMatches(sign, mapId, tileId))
            continue;
        if (mapped && sign.Map.empty())
            continue;
        if (SpentHowl(state, sign.Id))
            continue;
        state.Spent.push_back(sign.Id);
    }
}

void ClearDangerSigns(DangerSignsState& state)
{
    state.Howls.clear();
    state.Spent.clear();
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
