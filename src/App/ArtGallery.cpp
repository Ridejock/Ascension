#include "ArtGallery.h"

#include <optional>

#include "Emerald/Renderer/Sprite.h"
#include "Emerald/Renderer/Texture.h"

namespace Ascension {

namespace {

bool AssetIsLive(const Emerald::Assets& assets, Emerald::AssetId id)
{
    const std::optional<Emerald::AssetInfo> info = assets.GetInfo(id);
    return info.has_value() && !info->Placeholder;
}

} // namespace

bool ArtGallery::Load(Emerald::Assets& assets, const ArtPack& pack, std::string& error)
{
    m_Entries.clear();
    Emerald::TextureOptions options;
    options.Filter = Emerald::TextureFilter::Nearest;
    for (const ArtImage& image : pack.GetImages()) {
        Emerald::AssetHandle<Emerald::Texture> handle =
            assets.Load<Emerald::Texture>(image.File, options);
        if (!handle || !AssetIsLive(assets, handle.GetId())) {
            error = "could not load '" + image.File.string() + "'";
            m_Entries.clear();
            return false;
        }
        Entry entry;
        entry.Slot = image.Slot;
        entry.Id = image.Id;
        entry.Image = std::move(handle);
        m_Entries.push_back(std::move(entry));
    }
    error.clear();
    return true;
}

const Emerald::Texture* ArtGallery::Find(ArtSlot slot, std::string_view id) const
{
    for (const Entry& entry : m_Entries) {
        if (entry.Slot != slot || entry.Id != id)
            continue;
        if (!entry.Image)
            return nullptr;
        const Emerald::Texture& texture = entry.Image.Get();
        if (!texture.HasGpuTexture())
            return nullptr;
        return &texture;
    }
    return nullptr;
}

bool DrawArt(Emerald::Renderer2D& renderer, const ArtGallery* gallery, ArtSlot slot,
             std::string_view id, const Emerald::Vec2& topLeft, const Emerald::Vec2& size,
             const Emerald::Vec4& tint)
{
    if (gallery == nullptr || gallery->IsEmpty() || id.empty() || size.x <= 0.0f || size.y <= 0.0f)
        return false;
    const Emerald::Texture* texture = gallery->Find(slot, id);
    if (texture == nullptr)
        return false;
    Emerald::Sprite sprite = Emerald::Sprite::FromTexture(*texture);
    Emerald::SpriteOptions options;
    options.Size = size;
    options.Origin = {0.0f, 0.0f};
    options.PixelSnap = true;
    options.Tint = tint;
    renderer.DrawSprite(sprite, topLeft, options);
    return true;
}

} // namespace Ascension
