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
    m_Pack = {};
    m_Textures.clear();
    m_Seconds = 0.0f;
    Emerald::TextureOptions options;
    options.Filter = Emerald::TextureFilter::Nearest;
    std::vector<Stored> textures;
    for (const ArtImage& image : pack.GetImages()) {
        for (const ArtPicture& picture : image.Pictures) {
            bool held = false;
            for (const Stored& stored : textures) {
                if (stored.File == picture.File)
                    held = true;
            }
            Emerald::AssetHandle<Emerald::Texture> handle;
            const Emerald::Texture* texture = nullptr;
            if (held) {
                for (const Stored& stored : textures) {
                    if (stored.File == picture.File && stored.Image)
                        texture = &stored.Image.Get();
                }
            } else {
                handle = assets.Load<Emerald::Texture>(picture.File, options);
                if (!handle || !AssetIsLive(assets, handle.GetId())) {
                    error = "could not load '" + picture.File.string() + "'";
                    m_Textures.clear();
                    return false;
                }
                texture = &handle.Get();
            }
            if (texture == nullptr || !texture->HasGpuTexture() || picture.FrameWidth < 1 ||
                picture.Frames < 1 || picture.FrameHeight < 1) {
                error = "could not load '" + picture.File.string() + "'";
                m_Textures.clear();
                return false;
            }
            const u32 span = static_cast<u32>(picture.FrameWidth) * static_cast<u32>(picture.Frames);
            if (texture->GetWidth() < span ||
                texture->GetHeight() < static_cast<u32>(picture.FrameHeight)) {
                error = "'" + picture.File.string() + "' does not match the frame size";
                m_Textures.clear();
                return false;
            }
            if (!held) {
                Stored stored;
                stored.File = picture.File;
                stored.Image = std::move(handle);
                textures.push_back(std::move(stored));
            }
        }
    }
    m_Pack = pack;
    m_Textures = std::move(textures);
    error.clear();
    return true;
}

void ArtGallery::Advance(f32 seconds)
{
    if (seconds <= 0.0f)
        return;
    m_Seconds += seconds;
    while (m_Seconds > 86400.0f)
        m_Seconds -= 86400.0f;
}

const Emerald::Texture* ArtGallery::FindTexture(const std::filesystem::path& file) const
{
    for (const Stored& stored : m_Textures) {
        if (stored.File != file || !stored.Image)
            continue;
        const Emerald::Texture& texture = stored.Image.Get();
        if (!texture.HasGpuTexture())
            return nullptr;
        return &texture;
    }
    return nullptr;
}

bool DrawArt(Emerald::Renderer2D& renderer, const ArtGallery* gallery, ArtSlot slot,
             std::string_view id, const Emerald::Vec2& topLeft, const Emerald::Vec2& size,
             const Emerald::Vec4& tint, const ArtPlace& place)
{
    if (gallery == nullptr || gallery->IsEmpty() || size.x <= 0.0f || size.y <= 0.0f)
        return false;
    if (id.empty() && !IsArtMarker(slot))
        return false;
    const ResolvedArt art = gallery->GetPack().FindPlaced(slot, id, place.Map, place.X, place.Y);
    if (art.Origin != ArtOrigin::Pack || art.Picture.File.empty())
        return false;
    const Emerald::Texture* texture = gallery->FindTexture(art.Picture.File);
    if (texture == nullptr)
        return false;
    const i32 frames = art.Picture.Frames > 0 ? art.Picture.Frames : 1;
    const i32 index = ArtFrameIndex(frames, art.Picture.FrameDurationMs, gallery->Seconds());
    i32 frameWidth = art.Picture.FrameWidth;
    i32 frameHeight = art.Picture.FrameHeight;
    if (frameWidth < 1)
        frameWidth = static_cast<i32>(texture->GetWidth()) / frames;
    if (frameHeight < 1)
        frameHeight = static_cast<i32>(texture->GetHeight());
    if (frameWidth < 1 || frameHeight < 1)
        return false;
    const i64 right = static_cast<i64>(index) * static_cast<i64>(frameWidth) + frameWidth;
    if (right > static_cast<i64>(texture->GetWidth()))
        return false;
    Emerald::Sprite sprite = Emerald::Sprite::FromTexture(*texture);
    sprite = sprite.Crop({static_cast<f32>(index * frameWidth), 0.0f},
                         {static_cast<f32>(frameWidth), static_cast<f32>(frameHeight)});
    Emerald::SpriteOptions options;
    options.Size = size;
    options.Origin = {0.0f, 0.0f};
    options.PixelSnap = true;
    options.Tint = tint;
    renderer.DrawSprite(sprite, topLeft, options);
    return true;
}

} // namespace Ascension
