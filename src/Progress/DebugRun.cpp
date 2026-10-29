#include "Ascension/Progress/DebugRun.h"

#include "Ascension/Progress/HeroName.h"

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
                  std::string& error, std::string_view name, std::string_view classId)
{
    std::string accepted;
    if (!name.empty()) {
        const HeroNameCheck check = CheckHeroName(name);
        if (check.Status != HeroNameStatus::Ok) {
            error = "the name cannot be used";
            return false;
        }
        accepted = check.Name;
    }
    if (!classId.empty()) {
        bool starter = false;
        for (const ClassDef* klass : data.Starters()) {
            if (klass != nullptr && klass->Id == classId) {
                starter = true;
                break;
            }
        }
        if (!starter) {
            error = "class '" + std::string(classId) + "' is not a starter";
            return false;
        }
    }

    if (!hero.Start(data, error))
        return false;
    if (!classId.empty() && !hero.AdoptStarter(classId, error))
        return false;
    hero.SetName(std::move(accepted));
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
