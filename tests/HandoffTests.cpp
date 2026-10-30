// A spawn fight's return: a win clears it, a loss stands at full HP with the gold
// untouched, and a flee steps back and keeps the foe's wound. No window.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Ascension/Data/ArtPack.h"
#include "Ascension/Map/Handoff.h"
#include "Ascension/Map/Movers.h"
#include "Ascension/Map/Walk.h"
#include "Ascension/Progress/Inventory.h"
#include "Ascension/Progress/RunSave.h"
#include "Ascension/Progress/Vitals.h"
#include "Check.h"

namespace {

using namespace Ascension;

namespace fs = std::filesystem;

const fs::path kSample = ASCENSION_DATA_DIR;

struct TempCopy {
    fs::path Path;

    TempCopy()
    {
        Path = fs::temp_directory_path() / "ascension-handoff-tests";
        std::error_code error;
        fs::remove_all(Path, error);
        fs::create_directories(Path);
        fs::copy(kSample, Path, fs::copy_options::recursive);
    }

    ~TempCopy()
    {
        std::error_code error;
        fs::remove_all(Path, error);
    }
};

void WriteFile(const fs::path& path, std::string_view text)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

void ReplaceInFile(const fs::path& path, std::string_view from, std::string_view to)
{
    std::ifstream in(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const auto at = text.find(from);
    Check(at != std::string::npos, "the sample text to replace is present");
    if (at == std::string::npos)
        return;
    text.replace(at, from.size(), to);
    WriteFile(path, text);
}

// A fixed order, so a flee can be pressed before a faster foe acts.
class ScriptedTurns final : public TurnModel {
public:
    explicit ScriptedTurns(std::vector<CombatantSide> order)
    {
        for (const CombatantSide side : order)
            m_Order.push_back(TurnSeat{side, 0, 0});
    }

    void Reset() override { m_Index = 0; }

    TurnSeat Next(std::span<const TurnSeat>) override
    {
        if (m_Order.empty())
            return {};
        if (m_Index >= m_Order.size())
            return m_Order.back();
        return m_Order[m_Index];
    }

    void Advance() override
    {
        if (m_Index + 1 < m_Order.size())
            ++m_Index;
    }

    void Capture(std::vector<TurnSeat>& order, i32& index) const override
    {
        order = m_Order;
        index = static_cast<i32>(m_Index);
    }

    void Place(std::span<const TurnSeat> order, i32 index) override
    {
        m_Order.assign(order.begin(), order.end());
        if (index < 0)
            index = 0;
        m_Index = static_cast<usize>(index);
    }

private:
    std::vector<TurnSeat> m_Order;
    usize m_Index = 0;
};

Database Loaded(const fs::path& directory, const char* what)
{
    Database data(directory);
    DataError error;
    Check(data.Load(&error), what);
    if (!error.Message.empty())
        std::printf("    %s\n", error.Message.c_str());
    return data;
}

bool TileIs(MapTile tile, i32 x, i32 y)
{
    return tile.X == x && tile.Y == y;
}

ContactQuery StepOnto(i32 x, i32 y, i32 fromX, i32 fromY, bool hasFrom)
{
    ContactQuery query;
    query.Tile.X = x;
    query.Tile.Y = y;
    query.From.X = fromX;
    query.From.Y = fromY;
    query.HasFrom = hasFrom;
    return query;
}

ReturnQuery QueryFor(MapOutcome outcome, const MapFightRequest& request)
{
    ReturnQuery query;
    query.Outcome = outcome;
    query.SpawnId = request.SpawnId;
    query.FoughtMapId = request.MapId;
    query.SpawnTile = request.SpawnTile;
    query.ReturnTile = request.ReturnTile;
    query.Once = request.Once;
    return query;
}

const MapDef* NeedMap(const Database& data, const char* id)
{
    const MapDef* map = data.FindMap(id);
    Check(map != nullptr, id);
    return map;
}

void TestOfferAndReturns()
{
    Database data = Loaded(kSample, "fixture maps load for a spawn fight");
    if (!data.IsLoaded())
        return;
    const MapDef* home = NeedMap(data, "fixture_a");
    const MapDef* hold = NeedMap(data, "fixture_b");
    if (home == nullptr || hold == nullptr)
        return;

    MapMemory memory;
    const ContactQuery approach = StepOnto(4, 1, 3, 1, true);
    const MapFightRequest wolf = OfferSpawn(data, *home, approach, memory);
    Check(wolf.Active && wolf.SpawnId == "fixture_spawn" && wolf.EncounterId == "wolf" &&
              wolf.Once && wolf.AllowFlee && wolf.Foes.size() == 1 && wolf.Foes[0] == "wolf" &&
              wolf.StartStatuses.empty() && wolf.Queue.empty() && wolf.Seats.size() == 1 &&
              wolf.Seats[0].Statuses.empty() && TileIs(wolf.SpawnTile, 4, 1) &&
              TileIs(wolf.ReturnTile, 3, 1),
          "stepping onto the wolf offers that fight and a step back");

    const ContactQuery empty = StepOnto(1, 1, 1, 1, false);
    Check(!OfferSpawn(data, *home, empty, memory).Active, "a rest tile is not a fight");

    const MapReturn won = ResolveReturn(data, QueryFor(MapOutcome::Won, wolf));
    Check(won.Ok && !won.Hub && won.MapId == "fixture_a" && TileIs(won.Tile, 4, 1) &&
              won.ClearSpawn && !won.KeepWounds && !won.FillPools,
          "a win stands on the spawn and clears it");
    FoeWound scratch;
    scratch.Id = "wolf";
    scratch.Hp = 11;
    const FoeWound held[] = {scratch};
    const MapSettle cleared = ApplyOutcome(data, QueryFor(MapOutcome::Won, wolf), memory, held);
    Check(cleared.Cleared && IsCleared(memory, "fixture_spawn") &&
              WoundsOf(memory, "fixture_spawn") == nullptr,
          "a win records the clear and drops any wound");
    Check(!OfferSpawn(data, *home, approach, memory).Active,
          "a cleared spawn does not respawn");
    const MapSettle again =
        ApplyOutcome(data, QueryFor(MapOutcome::Won, wolf), memory, held);
    Check(!again.Cleared && memory.Cleared.size() == 1, "a second win does not record it twice");

    MapMemory fresh;
    const ContactQuery arrival = StepOnto(4, 1, 0, 0, false);
    const MapFightRequest dropped = OfferSpawn(data, *home, arrival, fresh);
    Check(dropped.Active && TileIs(dropped.ReturnTile, 4, 2),
          "a link arrival flees to the first open neighbor");
    ReturnQuery blocked = QueryFor(MapOutcome::Fled, wolf);
    blocked.ReturnTile.X = 0;
    blocked.ReturnTile.Y = 0;
    const MapReturn fledWall = ResolveReturn(data, blocked);
    Check(fledWall.Ok && TileIs(fledWall.Tile, 4, 2) && fledWall.KeepWounds && !fledWall.ClearSpawn,
          "a flee whose step is a wall uses the first open neighbor");

    const MapSettle fled =
        ApplyOutcome(data, QueryFor(MapOutcome::Fled, wolf), fresh, held);
    const FledWounds* wounds = WoundsOf(fresh, "fixture_spawn");
    Check(fled.Return.Ok && TileIs(fled.Return.Tile, 3, 1) && fled.Return.KeepWounds &&
              !IsCleared(fresh, "fixture_spawn") && wounds != nullptr && wounds->Foes.size() == 1 &&
              wounds->Foes[0].Hp == 11 && StoredWound(*wounds, 0, "wolf") == 11 &&
              StoredWound(*wounds, 0, "bandit") == -1 && StoredWound(*wounds, 1, "wolf") == -1,
          "a flee steps back, leaves the spawn, and keeps that foe's wound");
    Check(OfferSpawn(data, *home, approach, fresh).Active, "a fled spawn is still there");

    Inventory bag;
    std::string error;
    Check(bag.Start(data, error), "the bag starts for a loss");
    if (!error.empty())
        std::printf("    %s\n", error.c_str());
    bag.GrantGold(40);
    Vitals vitals;
    vitals.Known = true;
    vitals.Hp = 11;
    vitals.Focus = 1;
    PoolCaps caps;
    caps.MaxHp = 80;
    caps.MaxFocus = 6;
    LoseToHub(vitals, caps);
    const MapSettle lost =
        ApplyOutcome(data, QueryFor(MapOutcome::Lost, wolf), fresh, held);
    Check(bag.Gold() == 40, "a loss does not spend gold");
    Check(vitals.Hp == 80 && vitals.Focus == 6 && vitals.ReturnToHub,
          "a loss fills both pools");
    Check(lost.Return.Ok && !lost.Return.Hub && lost.Return.MapId == "fixture_a" &&
              TileIs(lost.Return.Tile, 1, 1) && lost.Return.FillPools && !lost.Return.ClearSpawn &&
              !IsCleared(fresh, "fixture_spawn") && WoundsOf(fresh, "fixture_spawn") == nullptr,
          "with no hub, a loss returns to the fought map's rest and forgets the wound");
    const LossSpot nowhere = FindLossReturn(data, "no_such_map");
    Check(!nowhere.Ok, "a loss on a missing map has nowhere to stand");

    const ContactQuery ontoHold = StepOnto(1, 1, 1, 2, true);
    const MapFightRequest staying = OfferSpawn(data, *hold, ontoHold, memory);
    Check(staying.Active && staying.SpawnId == "fixture_hold" && !staying.Once &&
              !staying.AllowFlee && TileIs(staying.ReturnTile, 1, 2),
          "noFlee refuses a flee, and once false keeps the spawn");
    Check(!FleeAllowed(data, hold->Spawns[0]), "the spawn's noFlee flag is what refuses it");
    MapMemory holdMemory;
    RememberWounds(holdMemory, "fixture_hold", held);
    const MapSettle stayed =
        ApplyOutcome(data, QueryFor(MapOutcome::Won, staying), holdMemory, held);
    Check(stayed.Return.Ok && TileIs(stayed.Return.Tile, 1, 1) && !stayed.Return.ClearSpawn &&
              !stayed.Cleared && !IsCleared(holdMemory, "fixture_hold") &&
              WoundsOf(holdMemory, "fixture_hold") == nullptr &&
              OfferSpawn(data, *hold, ontoHold, holdMemory).Active,
          "a spawn that is not once stays after a win");

    const LossSpot homeLoss = FindLossReturn(data, "fixture_b");
    Check(homeLoss.Ok && !homeLoss.Hub && homeLoss.MapId == "fixture_b" &&
              TileIs(homeLoss.Tile, 1, 2),
          "a map with no rest loses back to its start");
}

void TestHubLoss()
{
    TempCopy copy;
    ReplaceInFile(copy.Path / "maps" / "fixture_b.json", "\"hub\": false", "\"hub\": true");
    Database data = Loaded(copy.Path, "a hub map loads");
    if (!data.IsLoaded())
        return;
    const LossSpot spot = FindLossReturn(data, "fixture_a");
    Check(spot.Ok && spot.Hub && spot.MapId == "fixture_b" && TileIs(spot.Tile, 1, 2),
          "a loss returns to the hub start when that map has no rest");
    ReturnQuery query;
    query.Outcome = MapOutcome::Lost;
    query.SpawnId = "fixture_spawn";
    query.FoughtMapId = "fixture_a";
    query.SpawnTile.X = 4;
    query.SpawnTile.Y = 1;
    query.Once = true;
    const MapReturn back = ResolveReturn(data, query);
    Check(back.Ok && back.Hub && back.FillPools && !back.ClearSpawn && back.MapId == "fixture_b" &&
              TileIs(back.Tile, 1, 2),
          "the loss return is the hub, not the map that was fought");
}

void TestBossCannotFlee()
{
    Database data = Loaded(kSample, "boss data loads");
    if (!data.IsLoaded())
        return;
    MapSpawn boss;
    boss.Id = "boss_seat";
    boss.Encounter = "oathkeeper_varn";
    boss.NoFlee = false;
    Check(!FleeAllowed(data, boss), "a foe with boss set cannot be fled");

    std::string error;
    Battle battle(data, std::make_unique<ScriptedTurns>(
                            std::vector<CombatantSide>{CombatantSide::Player}));
    battle.SetFleeAllowed(true);
    Check(battle.Start("fighter", 1, "oathkeeper_varn", error), "the boss fight starts");
    if (!error.empty())
        std::printf("    %s\n", error.c_str());
    Check(!battle.FleeAllowed(), "start clears flee, so a list fight cannot flee");
    const ActionResult refused = battle.Flee();
    Check(!refused.Accepted && battle.Result() == BattleResult::Ongoing,
          "a boss press does not end the fight and does not spend the turn");
    CheckHas(refused.Text, "cannot flee", "the refusal says flee is closed");
    battle.SetFleeAllowed(true);
    const ActionResult left = battle.Flee();
    Check(left.Accepted && left.Result == BattleResult::Fled &&
              battle.Result() == BattleResult::Fled,
          "an allowed flee ends the fight as fled");
    CheckHas(left.Text, "flees", "the flee line names the leaving");
}

void TestFoeWoundRoundTrip()
{
    Database data = Loaded(kSample, "wolf data loads for a wound");
    if (!data.IsLoaded())
        return;
    std::string error;
    Battle first(data, std::make_unique<ScriptedTurns>(
                           std::vector<CombatantSide>{CombatantSide::Player}));
    Check(first.Start("fighter", 1, "wolf", error), "the wolf fight starts");
    if (!error.empty())
        std::printf("    %s\n", error.c_str());
    if (first.Result() != BattleResult::Ongoing || first.Enemies().empty())
        return;
    const i32 full = first.Enemies()[0].MaxHp;
    Check(full > 11 && first.SetTarget(0), "the wolf has room for a wound");
    first.SetEnemyHp(11);
    first.SetFleeAllowed(true);
    const ActionResult left = first.Flee();
    Check(left.Accepted && first.Result() == BattleResult::Fled && first.Enemies()[0].Hp == 11,
          "fleeing keeps the wolf at the wounded total");
    std::vector<FoeWound> captured;
    CaptureFoeWounds(first, captured);
    MapMemory memory;
    RememberWounds(memory, "fixture_spawn", captured);

    Battle second(data, std::make_unique<ScriptedTurns>(
                            std::vector<CombatantSide>{CombatantSide::Player}));
    Check(second.Start("fighter", 1, "wolf", error), "the wolf is met again");
    const FledWounds* wounds = WoundsOf(memory, "fixture_spawn");
    Check(wounds != nullptr, "the flee stored the wolf");
    if (wounds == nullptr || second.Enemies().empty())
        return;
    ApplyFoeWounds(second, *wounds);
    Check(second.Result() == BattleResult::Ongoing && second.Enemies()[0].Hp == 11,
          "the next contact writes the wound back");

    FledWounds changed = *wounds;
    changed.Foes[0].Id = "bandit";
    Battle third(data, std::make_unique<ScriptedTurns>(
                           std::vector<CombatantSide>{CombatantSide::Player}));
    Check(third.Start("fighter", 1, "wolf", error), "a changed seat starts full");
    if (third.Enemies().empty())
        return;
    ApplyFoeWounds(third, changed);
    Check(third.Enemies()[0].Hp == third.Enemies()[0].MaxHp,
          "a seat whose id changed keeps the health the new fight rolled");
}

void TestFangOnce()
{
    Database data = Loaded(ASCENSION_SHIPPED_DATA_DIR, "shipped data loads for the fang");
    if (!data.IsLoaded())
        return;
    Inventory bag;
    std::string error;
    Check(bag.Start(data, error), "the bag starts for the fang");
    if (!error.empty())
        std::printf("    %s\n", error.c_str());
    std::vector<std::string> found;
    const std::vector<std::string> killed{"dire_wolf"};
    const std::vector<std::string> first = bag.TakeDrops(killed, found);
    Check(first.size() == 1 && found.size() == 1 && found[0] == "alphas_fang",
          "the dire wolf drops Alpha's Fang once");
    const std::vector<std::string> second = bag.TakeDrops(killed, found);
    Check(second.empty() && found.size() == 1, "a later dire wolf does not drop the fang again");
}

bool HasStatus(const PartyFoe& foe, std::string_view id)
{
    for (const std::string& status : foe.Statuses) {
        if (status == id)
            return true;
    }
    return false;
}

const char* kPartyMap =
    "{\n"
    "  \"map\": {\n"
    "    \"id\": \"fixture_party\",\n"
    "    \"rows\": [\".......\", \".......\", \".......\"],\n"
    "    \"legend\": { \".\": \"floor\" },\n"
    "    \"start\": { \"x\": 0, \"y\": 0 },\n"
    "    \"spawns\": [\n"
    "      { \"id\": \"seat_a\", \"tile\": { \"x\": 1, \"y\": 1 }, \"encounter\": \"wolf\" },\n"
    "      { \"id\": \"seat_b\", \"tile\": { \"x\": 2, \"y\": 1 }, \"encounter\": \"wolf\",\n"
    "        \"aura\": { \"radius\": 1, \"status\": \"exposed\" } },\n"
    "      { \"id\": \"seat_c\", \"tile\": { \"x\": 3, \"y\": 1 }, \"encounter\": \"wolf\" },\n"
    "      { \"id\": \"seat_d\", \"tile\": { \"x\": 4, \"y\": 1 }, \"encounter\": \"wolf\" },\n"
    "      { \"id\": \"seat_e\", \"tile\": { \"x\": 6, \"y\": 1 }, \"encounter\": \"bandit\" }\n"
    "    ]\n"
    "  }\n"
    "}\n";

void TestPartyAndAuras()
{
    TempCopy copy;
    WriteFile(copy.Path / "maps" / "fixture_party.json", kPartyMap);
    Database data = Loaded(copy.Path, "the party fixture loads");
    if (!data.IsLoaded())
        return;
    const MapDef* map = data.FindMap("fixture_party");
    Check(map != nullptr && data.GetRules().MaxFoes == 3, "the fixture map and the cap of 3 load");
    if (map == nullptr)
        return;

    MapMemory memory;
    ContactQuery query;
    query.Tile.X = 1;
    query.Tile.Y = 1;
    query.HasFrom = true;
    query.From.X = 1;
    query.From.Y = 0;
    query.Joining.push_back("seat_c");
    query.Joining.push_back("seat_d");
    query.Joining.push_back("seat_e");
    query.Joining.push_back("seat_b");
    query.Joining.push_back("seat_a");
    const MapFightRequest fight = OfferSpawn(data, *map, query, memory);
    Check(fight.Active && fight.SpawnId == "seat_a" && fight.Foes.size() == 3 &&
              fight.Seats.size() == 3 && fight.Queue.size() == 2,
          "five foes seat three and queue two");
    Check(fight.Seats[0].SpawnId == "seat_a" && fight.Seats[0].Id == "wolf" &&
              fight.Seats[1].SpawnId == "seat_c" && fight.Seats[2].SpawnId == "seat_d" &&
              fight.Queue[0].SpawnId == "seat_e" && fight.Queue[0].Id == "bandit" &&
              fight.Queue[1].SpawnId == "seat_b",
          "the tile's spawn is seated first, then the join order");
    Check(HasStatus(fight.Seats[0], "exposed") && HasStatus(fight.Seats[1], "exposed") &&
              fight.Seats[2].Statuses.empty() && fight.Queue[0].Statuses.empty() &&
              fight.StartStatuses.size() == 1 && fight.StartStatuses[0] == "exposed",
          "a foe inside another spawn's radius carries that status");

    const PartySplit follow = SplitParty(fight.Queue, data.GetRules().MaxFoes);
    Check(follow.Seated.size() == 2 && follow.Queued.empty() &&
              follow.Seated[0].SpawnId == "seat_e" && follow.Seated[1].SpawnId == "seat_b",
          "the queue is the next fight, in the same order");
    std::vector<PartyFoe> second = follow.Seated;
    ApplyMapAuras(data, *map, memory, {}, second);
    Check(second[0].Statuses.empty() && second[1].Statuses.empty(),
          "the follow-up samples auras again, and a spawn's own aura does not count");

    ContactQuery holder;
    holder.Tile.X = 2;
    holder.Tile.Y = 1;
    const MapFightRequest own = OfferSpawn(data, *map, holder, memory);
    Check(own.Active && own.Foes.size() == 1 && own.StartStatuses.empty(),
          "the aura holder does not buff itself");

    ContactQuery far;
    far.Tile.X = 6;
    far.Tile.Y = 1;
    const MapFightRequest bandit = OfferSpawn(data, *map, far, memory);
    Check(bandit.Active && bandit.Foes.size() == 1 && bandit.Foes[0] == "bandit" &&
              bandit.StartStatuses.empty() && bandit.Queue.empty(),
          "a foe outside the radius carries no status");

    std::vector<SpawnPlace> places;
    PlaceSpawns(*map, memory, places);
    for (SpawnPlace& place : places) {
        if (place.Id != "seat_a")
            continue;
        place.Tile.X = 6;
        place.Tile.Y = 2;
    }
    std::vector<PartyFoe> walked;
    PartyFoe away;
    away.SpawnId = "seat_a";
    away.Id = "wolf";
    walked.push_back(away);
    ApplyMapAuras(data, *map, memory, places, walked);
    Check(walked[0].Statuses.empty(), "walking out of the radius drops the status");
    for (SpawnPlace& place : places) {
        if (place.Id != "seat_a")
            continue;
        place.Tile.X = 1;
        place.Tile.Y = 1;
    }
    ApplyMapAuras(data, *map, memory, places, walked);
    Check(HasStatus(walked[0], "exposed"), "standing back inside the radius grants it again");

    RememberClear(memory, "seat_b", true);
    ApplyMapAuras(data, *map, memory, places, walked);
    Check(walked[0].Statuses.empty(), "a cleared aura no longer grants");

    ReplaceInFile(copy.Path / "combat.json", "\"maxFoes\": 3", "\"maxFoes\": 4");
    Database wider = Loaded(copy.Path, "maxFoes 4 reloads the party fixture");
    const MapDef* again = wider.FindMap("fixture_party");
    Check(wider.IsLoaded() && again != nullptr && wider.GetRules().MaxFoes == 4,
          "the cap can be raised from the file");
    if (again == nullptr)
        return;
    MapMemory fresh;
    const MapFightRequest widerFight = OfferSpawn(wider, *again, query, fresh);
    Check(widerFight.Foes.size() == 4 && widerFight.Queue.size() == 1 &&
              widerFight.Queue[0].SpawnId == "seat_b",
          "a cap of 4 seats four and queues the last");
}

void TestRosterStart()
{
    Database data = Loaded(kSample, "roster data loads");
    if (!data.IsLoaded())
        return;
    std::string error;
    Battle pack(data, std::make_unique<AgilityRoundModel>());
    Check(pack.Start("fighter", 1, "wolf_pack", error), "the pack still starts whole");
    Check(pack.Enemies().size() == 3 && pack.EncounterName() == "Wolf Pack",
          "a list fight still seats the encounter");

    std::vector<std::string> one;
    one.push_back("wolf");
    OpeningPools pools;
    pools.Hp = 40;
    pools.Focus = 2;
    Battle seated(data, std::make_unique<AgilityRoundModel>());
    Check(seated.Start("fighter", 1, "wolf_pack", one, pools, error), "a seated list starts");
    Check(seated.Enemies().size() == 1 && seated.Enemies()[0].Id == "wolf" &&
              seated.EncounterName() == "Wolf Pack" && seated.Player().Hp == 40 &&
              seated.Player().Focus == 2,
          "the roster seats the ids it was given and keeps the carried pools");
    Check(seated.GrantStatus(0, "exposed", error) && seated.Enemies()[0].Statuses.size() == 1 &&
              seated.Enemies()[0].Statuses[0].Id == "exposed" && seated.Enemies()[0].Hp == seated.Enemies()[0].MaxHp,
          "an opening status lands on that seat and does not change its health");
    Check(!seated.GrantStatus(2, "exposed", error), "a seat outside the pack is refused");
    Check(!seated.GrantStatus(0, "no_such", error), "an unknown status is refused");
    Check(seated.Enemies()[0].Statuses.size() == 1, "a refused status does not add another");

    std::vector<std::string> none;
    Check(!seated.Start("fighter", 1, "wolf_pack", none, pools, error),
          "an empty roster does not start");
    Check(seated.Enemies().size() == 1 && seated.Enemies()[0].Id == "wolf",
          "the refused roster leaves the fight that was already up");
}

const unsigned char kPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44,
    0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F,
    0x15, 0xC4, 0x89, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xF8,
    0xCF, 0xC0, 0xF0, 0x1F, 0x00, 0x05, 0x00, 0x01, 0xFF, 0x89, 0x99, 0x3D, 0x1D, 0x00, 0x00,
    0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};

void WritePng(const fs::path& path)
{
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(kPng), static_cast<std::streamsize>(sizeof(kPng)));
}

void TestPackFootprintReturn()
{
    TempCopy copy;
    ReplaceInFile(copy.Path / "maps" / "fixture_a.json",
                  "{ \"id\": \"fixture_post\", \"tile\": { \"x\": 2, \"y\": 2 }, \"w\": 1, \"h\": 1 }",
                  "{ \"id\": \"fixture_post\", \"tile\": { \"x\": 2, \"y\": 2 }, \"w\": 1, \"h\": 1 },\n"
                  "      { \"id\": \"fixture_post\", \"tile\": { \"x\": 4, \"y\": 2 } },\n"
                  "      { \"id\": \"fixture_post\", \"tile\": { \"x\": 1, \"y\": 1 } }");
    Database data = Loaded(copy.Path, "a map with unsized props loads");
    if (!data.IsLoaded())
        return;
    const MapDef* home = NeedMap(data, "fixture_a");
    if (home == nullptr)
        return;
    WritePng(copy.Path / "pack" / "props" / "post.png");
    WriteFile(copy.Path / "pack" / "manifest.json",
              "{ \"props\": [ { \"id\": \"fixture_post\", \"file\": \"props/post.png\", "
              "\"w\": 1, \"h\": 1 } ] }\n");
    ArtPack pack;
    DataError packError;
    Check(pack.Load(copy.Path / "pack", data, &packError), "the prop pack loads");
    if (!packError.Message.empty())
        std::printf("    %s\n", packError.Message.c_str());

    MapMemory memory;
    const ContactQuery arrival = StepOnto(4, 1, 0, 0, false);
    const MapFightRequest plain = OfferSpawn(data, *home, arrival, memory);
    Check(plain.Active && TileIs(plain.ReturnTile, 4, 2),
          "without a pack the south floor is the flee tile");
    const MapFightRequest packed = OfferSpawn(data, *home, arrival, memory, {}, &pack);
    Check(packed.Active && TileIs(packed.ReturnTile, 3, 1),
          "a pack footprint is not a flee tile");
    Walker stood;
    Check(stood.Place(data, "fixture_a", packed.ReturnTile, &pack) && TileIs(stood.Place().Tile, 3, 1),
          "the flee tile accepts the hero");
    MapTile south;
    south.X = 4;
    south.Y = 2;
    Check(!stood.Place(data, "fixture_a", south, &pack) && TileIs(stood.Place().Tile, 3, 1),
          "place refuses the footprint cell the map left unsized");

    Walker stuck;
    MapTile start;
    start.X = 1;
    start.Y = 1;
    Check(stuck.Place(data, "fixture_a", start, nullptr), "the rest can be stood on without a pack");
    MapTile spawn;
    spawn.X = 4;
    spawn.Y = 1;
    Check(PlaceNear(stuck, data, "fixture_a", south, spawn, spawn, true, &pack) &&
              TileIs(stuck.Place().Tile, 3, 1),
          "a footprint flee tile falls through to the next open neighbor");

    const LossSpot openRest = FindLossReturn(data, "fixture_a");
    Check(openRest.Ok && TileIs(openRest.Tile, 1, 1),
          "without a pack a loss still stands on the rest");
    const LossSpot loss = FindLossReturn(data, "fixture_a", &pack);
    Check(loss.Ok && loss.MapId == "fixture_a" && TileIs(loss.Tile, 2, 1),
          "a loss beside a footprint rest uses the open neighbor");
    Walker returned;
    Check(returned.Place(data, loss.MapId, loss.Tile, &pack) &&
              TileIs(returned.Place().Tile, 2, 1),
          "the loss tile accepts the hero");
    Walker lost;
    Check(lost.Place(data, "fixture_a", start, nullptr), "the loss fallback starts on the rest");
    Check(PlaceNear(lost, data, "fixture_a", start, start, spawn, false, &pack) &&
              TileIs(lost.Place().Tile, 2, 1),
          "a footprint loss tile falls through to the next open neighbor");
}

void TestFleeKeepsTheWound()
{
    Check(RunSave::kVersion == 6, "the save version records the six-primary sheet");
    RunState run;
    RememberOutcome(run, BattleResult::Fled, 3, 1, 0);
    Check(run.Standing == RunStanding::None && run.ResourcesKnown && run.Hp == 3 &&
              run.Focus == 1 && ResumeWound(run),
          "a flee is between fights and the wound is kept");
}

} // namespace

int main()
{
    TestOfferAndReturns();
    TestHubLoss();
    TestBossCannotFlee();
    TestFoeWoundRoundTrip();
    TestFangOnce();
    TestPartyAndAuras();
    TestRosterStart();
    TestFleeKeepsTheWound();
    TestPackFootprintReturn();
    std::printf("%s: %d failure(s)\n", g_Failures == 0 ? "Passed" : "Failed", g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
