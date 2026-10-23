#include "Ascension/Progress/DebugRun.h"

namespace Ascension {

namespace {

const MapDef* HubMap(const Database& data)
{
    for (const MapDef& map : data.GetMaps()) {
        if (map.Hub)
            return &map;
    }
    return nullptr;
}

} // namespace

FoeRespawn RespawnFoes(RunState& run, bool inBattle)
{
    if (inBattle)
        return FoeRespawn::Busy;
    run.Defeated.clear();
    run.Cleared.clear();
    return FoeRespawn::Returned;
}

bool BeginNewGame(const Database& data, Hero& hero, Inventory& inventory, FreshStart& out,
                  std::string& error)
{
    if (!hero.Start(data, error))
        return false;
    if (!inventory.Start(data, error))
        return false;
    inventory.SetDefeated({});

    out = {};
    hero.ExportCareer(out.Run.Career);
    inventory.ExportBag(out.Run.Bag);
    if (const MapDef* hub = HubMap(data)) {
        out.Run.MapId = hub->Id;
        out.Run.Tile = hub->Start;
    }
    FillVitals(out.Pools, HeroPools(data, hero, inventory));
    out.Run.ResourcesKnown = out.Pools.Known;
    out.Run.Hp = out.Pools.Hp;
    out.Run.Focus = out.Pools.Focus;
    out.Run.Standing = RunStanding::None;
    error.clear();
    return true;
}

} // namespace Ascension
