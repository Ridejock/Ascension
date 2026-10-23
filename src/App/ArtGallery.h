#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/ArtPack.h"
#include "Emerald/Assets/Assets.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Math/Vec4.h"
#include "Emerald/Renderer/Renderer2D.h"

namespace Ascension {

// GPU copies of one loaded pack. Empty when this launch has no pack, and Find
// then returns null so the caller draws the built-in CC0 art.
class ArtGallery {
public:
    bool Load(Emerald::Assets& assets, const ArtPack& pack, std::string& error);

    [[nodiscard]] bool IsEmpty() const { return m_Entries.empty(); }
    [[nodiscard]] const Emerald::Texture* Find(ArtSlot slot, std::string_view id) const;

private:
    struct Entry {
        ArtSlot Slot = ArtSlot::Tile;
        std::string Id;
        Emerald::AssetHandle<Emerald::Texture> Image;
    };

    std::vector<Entry> m_Entries;
};

// Draws the pack image in `size` with its top-left at `topLeft`. False when the
// gallery has no image for this id: the caller draws the built-in CC0 art.
[[nodiscard]] bool DrawArt(Emerald::Renderer2D& renderer, const ArtGallery* gallery, ArtSlot slot,
                           std::string_view id, const Emerald::Vec2& topLeft,
                           const Emerald::Vec2& size, const Emerald::Vec4& tint);

} // namespace Ascension
