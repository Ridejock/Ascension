#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/ArtPack.h"
#include "Emerald/Assets/Assets.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec4.h"
#include "Emerald/Renderer/Renderer2D.h"

namespace Ascension {

// Where a tile picture is drawn. Map is the map id. X and Y are the cell.
// Empty for a picture that is not a map cell.
struct ArtPlace {
    std::string_view Map;
    i32 X = 0;
    i32 Y = 0;
};

// GPU copies of one loaded pack, plus the clock that advances a strip. Empty when
// this launch has no pack. DrawArt then returns false and the caller draws the
// built-in CC0 art.
class ArtGallery {
public:
    bool Load(Emerald::Assets& assets, const ArtPack& pack, std::string& error);
    void Advance(f32 seconds);

    [[nodiscard]] bool IsEmpty() const { return m_Pack.GetImages().empty(); }
    [[nodiscard]] f32 Seconds() const { return m_Seconds; }
    [[nodiscard]] const ArtPack& GetPack() const { return m_Pack; }
    [[nodiscard]] const Emerald::Texture* FindTexture(const std::filesystem::path& file) const;

private:
    struct Stored {
        std::filesystem::path File;
        Emerald::AssetHandle<Emerald::Texture> Image;
    };

    ArtPack m_Pack;
    std::vector<Stored> m_Textures;
    f32 m_Seconds = 0.0f;
};

// Draws the pack image in `size` with its top-left at `topLeft`. A strip shows the
// frame for the gallery clock. False when the gallery has no image for this id:
// the caller draws the built-in CC0 art. `place` picks a tile's map, theme, and variant.
[[nodiscard]] bool DrawArt(Emerald::Renderer2D& renderer, const ArtGallery* gallery, ArtSlot slot,
                           std::string_view id, const Emerald::Vec2& topLeft,
                           const Emerald::Vec2& size, const Emerald::Vec4& tint,
                           const ArtPlace& place = {});

} // namespace Ascension
