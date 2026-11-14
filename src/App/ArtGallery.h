#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Ascension/Data/ArtPack.h"
#include "Emerald/Assets/Assets.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec4.h"
#include "Emerald/Renderer/Font.h"
#include "Emerald/Renderer/Gpu.h"
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
    // `device` uploads pack bytes. A loose directory still goes through `assets`.
    bool Load(Emerald::Assets& assets, Emerald::GpuDevice* device, const ArtPack& pack,
              std::string& error);
    void Advance(f32 seconds);

    [[nodiscard]] bool IsEmpty() const
    {
        return m_Pack.GetImages().empty() && m_Skins.empty() && m_Fonts.empty();
    }
    [[nodiscard]] f32 Seconds() const { return m_Seconds; }
    [[nodiscard]] const ArtPack& GetPack() const { return m_Pack; }
    [[nodiscard]] const Emerald::Texture* FindTexture(const std::filesystem::path& file) const;
    [[nodiscard]] const Emerald::Font* FindFont(std::string_view role) const;

    // panel, cursor, confirm, button_gold, button_ruby, button_sapphire,
    // bar_emerald, bar_gold, bar_ruby, bar_sapphire. Null when the pack has none.
    struct Skin {
        const Emerald::Texture* Image = nullptr;
        i32 Border = 0;
    };
    [[nodiscard]] const Skin* FindSkin(std::string_view role) const;

private:
    struct Stored {
        std::filesystem::path File;
        Emerald::AssetHandle<Emerald::Texture> Image;
        std::unique_ptr<Emerald::Texture> Owned;
    };
    struct HeldFont {
        std::string Role;
        Emerald::Font Face;
    };

    [[nodiscard]] const Emerald::Texture* Hold(std::filesystem::path file,
                                               Emerald::AssetHandle<Emerald::Texture> handle,
                                               std::unique_ptr<Emerald::Texture> owned);
    [[nodiscard]] bool HoldPng(Emerald::Assets& assets, Emerald::GpuDevice* device,
                               const std::filesystem::path& file, std::string& error);
    void RememberSkins();

    ArtPack m_Pack;
    std::vector<Stored> m_Textures;
    std::vector<HeldFont> m_Fonts;
    std::vector<std::pair<std::string, Skin>> m_Skins;
    f32 m_Seconds = 0.0f;
};

// Draws a 9-slice. False when `skin` is missing. The center and edges stretch;
// the corners stay `border` pixels.
[[nodiscard]] bool DrawNineSlice(Emerald::Renderer2D& renderer, const ArtGallery::Skin& skin,
                                 const Emerald::Vec2& origin, const Emerald::Vec2& size);

// The pack font for `role` (ui, body, damage), or the built-in debug font.
f32 DrawPackText(Emerald::Renderer2D& renderer, const ArtGallery* gallery, std::string_view role,
                 const Emerald::Vec2& position, std::string_view text, const Emerald::Vec4& color,
                 i32 scale, f32 maxWidth = 0.0f);

// Draws the pack image in `size` with its top-left at `topLeft`. A strip shows the
// frame for the gallery clock. False when the gallery has no image for this id:
// the caller draws the built-in CC0 art. `place` picks a tile's map, theme, and variant.
[[nodiscard]] bool DrawArt(Emerald::Renderer2D& renderer, const ArtGallery* gallery, ArtSlot slot,
                           std::string_view id, const Emerald::Vec2& topLeft,
                           const Emerald::Vec2& size, const Emerald::Vec4& tint,
                           const ArtPlace& place = {});

} // namespace Ascension
