#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Emerald/Core/Defines.h"

namespace Ascension {

// Ok is false when the data directory was left unchanged. Message is one line:
// the loader's sentence, or the result of a write.
struct EditResult {
    bool Ok = false;
    std::string Message;
};

// Loads the data directory. Message is "ok" when the load succeeds.
[[nodiscard]] EditResult CheckData(const std::filesystem::path& dataDir);

// A recipe for a new maps/<id>.json. The kind is not stored in the file.
enum class MapKind : u8 { Overland, Dungeon, Town };

struct NewMap {
    std::string Id;
    MapKind Kind = MapKind::Overland;
    i32 Width = 0;
    i32 Height = 0;
};

[[nodiscard]] EditResult CreateMap(const std::filesystem::path& dataDir, const NewMap& spec);

struct PaintRect {
    std::string MapId;
    std::string TileId;
    i32 X = 0;
    i32 Y = 0;
    i32 Width = 1;
    i32 Height = 1;
};

[[nodiscard]] EditResult Paint(const std::filesystem::path& dataDir, const PaintRect& rect);

struct MapCell {
    std::string MapId;
    i32 X = 0;
    i32 Y = 0;
};

[[nodiscard]] EditResult SetStart(const std::filesystem::path& dataDir, const MapCell& cell);

struct MapLinkEdit {
    std::string MapId;
    i32 X = 0;
    i32 Y = 0;
    std::string To;
    i32 ArrivalX = 0;
    i32 ArrivalY = 0;
};

[[nodiscard]] EditResult AddLink(const std::filesystem::path& dataDir, const MapLinkEdit& link);
[[nodiscard]] EditResult RemoveLink(const std::filesystem::path& dataDir, const MapCell& cell);

struct MapSpawnEdit {
    std::string MapId;
    std::string Id;
    i32 X = 0;
    i32 Y = 0;
    std::string Encounter;
    bool Once = true;
    bool NoFlee = false;
    // Empty omits the field. The behavior must already be in movers.json.
    std::string Mover;
    // HasAura writes both fields. Radius is ignored when HasAura is false.
    bool HasAura = false;
    i32 AuraRadius = 0;
    std::string AuraStatus;
};

[[nodiscard]] EditResult AddSpawn(const std::filesystem::path& dataDir, const MapSpawnEdit& spawn);
[[nodiscard]] EditResult RemoveSpawn(const std::filesystem::path& dataDir, const std::string& mapId,
                                     const std::string& spawnId);

enum class EditPointKind : u8 { Rest, Shop, Talk };

struct MapPointEdit {
    std::string MapId;
    EditPointKind Kind = EditPointKind::Rest;
    i32 X = 0;
    i32 Y = 0;
    std::string Dialogue;
};

[[nodiscard]] EditResult AddPoint(const std::filesystem::path& dataDir, const MapPointEdit& point);
[[nodiscard]] EditResult RemovePoint(const std::filesystem::path& dataDir, const MapCell& cell);

struct MapOpenEdit {
    i32 X = 0;
    i32 Y = 0;
    i32 Width = 1;
    i32 Height = 1;
};

struct MapPropEdit {
    std::string MapId;
    std::string Id;
    i32 X = 0;
    i32 Y = 0;
    // Sized false omits w and h. Open cells require a size; the loader rejects the rest.
    bool Sized = false;
    i32 Width = 0;
    i32 Height = 0;
    std::vector<MapOpenEdit> Open;
};

[[nodiscard]] EditResult AddProp(const std::filesystem::path& dataDir, const MapPropEdit& prop);
// Removes every placement of that prop id on the map.
[[nodiscard]] EditResult RemoveProp(const std::filesystem::path& dataDir, const std::string& mapId,
                                    const std::string& propId);

[[nodiscard]] EditResult SetHub(const std::filesystem::path& dataDir, const std::string& mapId,
                                bool on);

} // namespace Ascension
