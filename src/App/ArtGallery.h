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
//
// Load records every picture and font and uploads none of them. That keeps the
// GPU copy off the startup path. Pump uploads a few each frame, inside the budget
// from gallery.json. A draw that needs a picture still missing uploads that one
// before the sprite is submitted, so the frame shows the pack art.
class ArtGallery {
public:
    // `device` uploads pack bytes when a picture is needed. A loose directory still
    // goes through `assets`. Both may be null: Load then only catalogues, and a later
    // upload fails into Fault().
    bool Load(Emerald::Assets* assets, Emerald::GpuDevice* device, const ArtPack& pack,
              const GalleryBudget& budget, std::string& error);
    // Uploads the next queued pictures until the frame budget is spent.
    void Pump();
    void Advance(f32 seconds);

    [[nodiscard]] bool IsEmpty() const
    {
        return m_Queued.empty() && m_Textures.empty() && m_Fonts.empty() && m_Skins.empty();
    }
    [[nodiscard]] i32 Pending() const { return static_cast<i32>(m_Queued.size()); }
    [[nodiscard]] i32 Resident() const
    {
        return static_cast<i32>(m_Textures.size() + m_Fonts.size());
    }
    [[nodiscard]] i32 OnDemand() const { return m_OnDemand; }
    [[nodiscard]] i32 Streamed() const { return m_Streamed; }
    [[nodiscard]] double UploadMilliseconds() const { return m_UploadMs; }
    // Set when a picture failed to decode or upload. A font that does not load is skipped.
    [[nodiscard]] const std::string& Fault() const { return m_Fault; }
    // Background queue, front first. Pictures a draw already took are gone.
    [[nodiscard]] std::vector<std::filesystem::path> UploadOrder() const;
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
    struct Queued {
        enum class Kind : u8 { Image, Font };
        Kind What = Kind::Image;
        std::filesystem::path File;
        i32 Frames = 1;
        i32 FrameWidth = 1;
        i32 FrameHeight = 1;
        std::string Role;
        i32 FontSize = 0;
        i32 Rank = 0;
        i32 Sequence = 0;
    };
    struct SkinSource {
        std::string Role;
        std::filesystem::path File;
        Skin Ready;
    };

    void Reset();
    [[nodiscard]] i32 RankOf(std::string_view section) const;
    [[nodiscard]] bool EnqueueImage(const std::filesystem::path& file, i32 frames, i32 frameWidth,
                                    i32 frameHeight, std::string_view section, std::string& error);
    void EnqueueFont(const ArtFontRole& role);
    [[nodiscard]] bool QueuedImage(const std::filesystem::path& file) const;
    [[nodiscard]] const Emerald::Texture* Hold(std::filesystem::path file,
                                               Emerald::AssetHandle<Emerald::Texture> handle,
                                               std::unique_ptr<Emerald::Texture> owned) const;
    [[nodiscard]] const Emerald::Texture* ResidentTexture(const std::filesystem::path& file) const;
    [[nodiscard]] bool UploadImage(const Queued& job) const;
    [[nodiscard]] bool UploadFont(const Queued& job) const;
    void NoteIfResident() const;
    void RememberSkins();

    ArtPack m_Pack;
    GalleryBudget m_Budget;
    Emerald::Assets* m_Assets = nullptr;
    Emerald::GpuDevice* m_Device = nullptr;
    mutable std::vector<Queued> m_Queued;
    mutable std::vector<Stored> m_Textures;
    mutable std::vector<HeldFont> m_Fonts;
    mutable std::vector<SkinSource> m_Skins;
    mutable std::string m_Fault;
    mutable double m_UploadMs = 0.0;
    mutable double m_DecodeMs = 0.0;
    mutable i32 m_Streamed = 0;
    mutable i32 m_OnDemand = 0;
    mutable bool m_NotedResident = false;
    i32 m_Pictures = 0;
    i32 m_Sequence = 0;
    double m_CatalogMs = 0.0;
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
