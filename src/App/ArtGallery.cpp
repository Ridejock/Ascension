#include "ArtGallery.h"

#include <optional>

#include "Emerald/Assets/Image.h"
#include "Emerald/Renderer/DebugText.h"
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

const Emerald::Texture* ArtGallery::Hold(std::filesystem::path file,
                                           Emerald::AssetHandle<Emerald::Texture> handle,
                                           std::unique_ptr<Emerald::Texture> owned)
{
    for (const Stored& stored : m_Textures) {
        if (stored.File == file) {
            if (stored.Image)
                return &stored.Image.Get();
            return stored.Owned.get();
        }
    }
    Stored stored;
    stored.File = std::move(file);
    const Emerald::Texture* texture = nullptr;
    if (owned) {
        texture = owned.get();
        stored.Owned = std::move(owned);
    } else if (handle) {
        texture = &handle.Get();
        stored.Image = std::move(handle);
    }
    m_Textures.push_back(std::move(stored));
    return texture;
}

bool ArtGallery::HoldPng(Emerald::Assets& assets, Emerald::GpuDevice* device,
                         const std::filesystem::path& file, std::string& error)
{
    for (const Stored& stored : m_Textures) {
        if (stored.File == file)
            return true;
    }
    if (m_Pack.HasArchive()) {
        std::vector<unsigned char> bytes;
        if (!m_Pack.ReadFile(file, bytes, error))
            return false;
        const std::optional<Emerald::Image> image =
            Emerald::Image::LoadFromMemory(bytes.data(), bytes.size());
        if (!image || device == nullptr) {
            error = "could not load '" + file.generic_string() + "'";
            return false;
        }
        Emerald::TextureOptions options;
        options.Filter = Emerald::TextureFilter::Nearest;
        std::optional<Emerald::Texture> texture = Emerald::Texture::Create(device, *image, options);
        if (!texture || !texture->HasGpuTexture()) {
            error = "could not load '" + file.generic_string() + "'";
            return false;
        }
        (void)Hold(file, {}, std::make_unique<Emerald::Texture>(std::move(*texture)));
        return true;
    }
    Emerald::TextureOptions options;
    options.Filter = Emerald::TextureFilter::Nearest;
    Emerald::AssetHandle<Emerald::Texture> handle = assets.Load<Emerald::Texture>(file, options);
    if (!handle || !AssetIsLive(assets, handle.GetId()) || !handle->HasGpuTexture()) {
        error = "could not load '" + file.string() + "'";
        return false;
    }
    (void)Hold(file, std::move(handle), {});
    return true;
}

namespace {

bool SkinIdHas(std::string_view id, std::string_view needle)
{
    return id.find(needle) != std::string_view::npos;
}

const ArtNineSlice* SliceNamed(const ArtPack& pack, std::string_view id)
{
    return pack.FindSlice(id);
}

} // namespace

void ArtGallery::RememberSkins()
{
    m_Skins.clear();
    auto addSlice = [this](const char* role, const ArtNineSlice* slice) {
        if (slice == nullptr)
            return;
        const Emerald::Texture* texture = FindTexture(slice->File);
        if (texture == nullptr)
            return;
        Skin skin;
        skin.Image = texture;
        skin.Border = slice->Border;
        m_Skins.push_back({role, skin});
    };
    const ArtNineSlice* panel = SliceNamed(m_Pack, "windowskin_fantasy_emerald_dark_panel");
    const ArtNineSlice* cursor = SliceNamed(m_Pack, "windowskin_fantasy_emerald_dark_cursor");
    const ArtNineSlice* confirm = nullptr;
    const ArtNineSlice* gold = nullptr;
    const ArtNineSlice* ruby = nullptr;
    const ArtNineSlice* sapphire = nullptr;
    for (const ArtNineSlice& slice : m_Pack.Slices()) {
        const std::string& id = slice.Id;
        const bool button = SkinIdHas(id, "button");
        if (panel == nullptr && SkinIdHas(id, "panel") && !SkinIdHas(id, "cursor") && !button)
            panel = &slice;
        if (cursor == nullptr && SkinIdHas(id, "cursor"))
            cursor = &slice;
        if (button && (SkinIdHas(id, "emerald") || SkinIdHas(id, "green") || SkinIdHas(id, "confirm")))
            confirm = &slice;
        if (button && SkinIdHas(id, "gold"))
            gold = &slice;
        if (button && (SkinIdHas(id, "ruby") || SkinIdHas(id, "red")))
            ruby = &slice;
        if (button && (SkinIdHas(id, "sapphire") || SkinIdHas(id, "blue")))
            sapphire = &slice;
    }
    addSlice("panel", panel);
    addSlice("cursor", cursor);
    addSlice("confirm", confirm != nullptr ? confirm : cursor);
    addSlice("button_gold", gold);
    addSlice("button_ruby", ruby);
    addSlice("button_sapphire", sapphire);

    auto addFile = [this](const char* role, std::string_view needle, i32 border) {
        for (const std::filesystem::path& file : m_Pack.UiFiles()) {
            const std::string text = file.generic_string();
            if (text.find(needle) == std::string::npos)
                continue;
            const Emerald::Texture* texture = FindTexture(file);
            if (texture == nullptr)
                continue;
            if (FindSkin(role) != nullptr)
                return;
            Skin skin;
            skin.Image = texture;
            skin.Border = border;
            m_Skins.push_back({role, skin});
            return;
        }
    };
    addFile("confirm", "button", 4);
    addFile("bar_emerald", "bar_fill_emerald", 0);
    addFile("bar_gold", "bar_fill_gold", 0);
    addFile("bar_ruby", "bar_fill_ruby", 0);
    addFile("bar_sapphire", "bar_fill_sapphire", 0);
}

bool ArtGallery::Load(Emerald::Assets& assets, Emerald::GpuDevice* device, const ArtPack& pack,
                      std::string& error)
{
    m_Pack = {};
    m_Textures.clear();
    m_Fonts.clear();
    m_Skins.clear();
    m_Seconds = 0.0f;
    m_Pack = pack;
    auto need = [this, &assets, device, &error](const std::filesystem::path& file, i32 frames,
                                                i32 frameWidth, i32 frameHeight) {
        if (file.empty())
            return true;
        if (!HoldPng(assets, device, file, error)) {
            m_Textures.clear();
            m_Pack = {};
            return false;
        }
        const Emerald::Texture* texture = FindTexture(file);
        if (texture == nullptr || !texture->HasGpuTexture() || frameWidth < 1 || frames < 1 ||
            frameHeight < 1) {
            error = "could not load '" + file.generic_string() + "'";
            m_Textures.clear();
            m_Pack = {};
            return false;
        }
        const u32 span = static_cast<u32>(frameWidth) * static_cast<u32>(frames);
        if (texture->GetWidth() < span || texture->GetHeight() < static_cast<u32>(frameHeight)) {
            error = "'" + file.generic_string() + "' does not match the frame size";
            m_Textures.clear();
            m_Pack = {};
            return false;
        }
        return true;
    };
    for (const ArtImage& image : pack.GetImages()) {
        for (const ArtPicture& picture : image.Pictures) {
            if (!need(picture.File, picture.Frames, picture.FrameWidth, picture.FrameHeight))
                return false;
        }
    }
    for (const ArtNineSlice& slice : pack.Slices()) {
        if (!need(slice.File, 1, 1, 1))
            return false;
    }
    for (const std::filesystem::path& file : pack.UiFiles()) {
        if (file.extension() == ".png" || file.extension() == ".PNG") {
            if (!need(file, 1, 1, 1))
                return false;
        }
    }
    auto named = [&](const ArtNamedFile& file) {
        return need(file.File, file.Frames, file.FrameWidth, file.FrameHeight);
    };
    for (const ArtNamedFile& file : pack.Icons()) {
        if (!named(file))
            return false;
    }
    for (const ArtNamedFile& file : pack.Items()) {
        if (!named(file))
            return false;
    }
    for (const ArtNamedFile& file : pack.Vfx()) {
        if (!named(file))
            return false;
    }
    for (const ArtBackdrop& backdrop : pack.Backdrops()) {
        if (!backdrop.Flat.empty() && !need(backdrop.Flat, 1, 1, 1))
            return false;
        for (const std::filesystem::path& layer : backdrop.Layers) {
            if (!need(layer, 1, 1, 1))
                return false;
        }
    }
    if (device != nullptr) {
        for (const ArtFontRole& role : pack.Fonts()) {
            std::vector<unsigned char> bytes;
            std::string why;
            bool loaded = false;
            if (pack.HasArchive())
                loaded = pack.ReadFile(role.File, bytes, why);
            else {
                std::optional<Emerald::Font> face = Emerald::Font::Load(
                    device, role.File, {.Size = static_cast<f32>(role.Size), .Oversample = 1,
                                        .Filter = Emerald::TextureFilter::Nearest});
                if (face) {
                    HeldFont held;
                    held.Role = role.Role;
                    held.Face = std::move(*face);
                    m_Fonts.push_back(std::move(held));
                }
                continue;
            }
            if (!loaded)
                continue;
            std::optional<Emerald::Font> face = Emerald::Font::LoadFromMemory(
                device, bytes,
                {.Size = static_cast<f32>(role.Size), .Oversample = 1,
                 .Filter = Emerald::TextureFilter::Nearest});
            if (!face)
                continue;
            HeldFont held;
            held.Role = role.Role;
            held.Face = std::move(*face);
            m_Fonts.push_back(std::move(held));
        }
    }
    RememberSkins();
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
        if (stored.File != file)
            continue;
        const Emerald::Texture* texture = stored.Owned ? stored.Owned.get()
                                                       : (stored.Image ? &stored.Image.Get() : nullptr);
        if (texture == nullptr || !texture->HasGpuTexture())
            return nullptr;
        return texture;
    }
    return nullptr;
}

const Emerald::Font* ArtGallery::FindFont(std::string_view role) const
{
    for (const HeldFont& font : m_Fonts) {
        if (font.Role == role)
            return &font.Face;
    }
    if (role != "fallback")
        return FindFont("fallback");
    return nullptr;
}

const ArtGallery::Skin* ArtGallery::FindSkin(std::string_view role) const
{
    for (const std::pair<std::string, Skin>& skin : m_Skins) {
        if (skin.first == role && skin.second.Image != nullptr)
            return &skin.second;
    }
    return nullptr;
}

bool DrawNineSlice(Emerald::Renderer2D& renderer, const ArtGallery::Skin& skin,
                   const Emerald::Vec2& origin, const Emerald::Vec2& size)
{
    if (skin.Image == nullptr || size.x <= 0.0f || size.y <= 0.0f)
        return false;
    const f32 width = static_cast<f32>(skin.Image->GetWidth());
    const f32 height = static_cast<f32>(skin.Image->GetHeight());
    f32 border = static_cast<f32>(skin.Border);
    if (border < 0.0f)
        border = 0.0f;
    if (border * 2.0f > width)
        border = width * 0.5f;
    if (border * 2.0f > height)
        border = height * 0.5f;
    if (border * 2.0f > size.x)
        border = size.x * 0.5f;
    if (border * 2.0f > size.y)
        border = size.y * 0.5f;
    const f32 srcMidW = width - border * 2.0f;
    const f32 srcMidH = height - border * 2.0f;
    const f32 dstMidW = size.x - border * 2.0f;
    const f32 dstMidH = size.y - border * 2.0f;
    const Emerald::Vec2 sources[9] = {
        {0.0f, 0.0f},     {border, 0.0f},     {width - border, 0.0f},
        {0.0f, border},   {border, border},   {width - border, border},
        {0.0f, height - border}, {border, height - border}, {width - border, height - border},
    };
    const Emerald::Vec2 sourceSize[9] = {
        {border, border}, {srcMidW, border}, {border, border},
        {border, srcMidH}, {srcMidW, srcMidH}, {border, srcMidH},
        {border, border}, {srcMidW, border}, {border, border},
    };
    const Emerald::Vec2 dest[9] = {
        origin,
        {origin.x + border, origin.y},
        {origin.x + size.x - border, origin.y},
        {origin.x, origin.y + border},
        {origin.x + border, origin.y + border},
        {origin.x + size.x - border, origin.y + border},
        {origin.x, origin.y + size.y - border},
        {origin.x + border, origin.y + size.y - border},
        {origin.x + size.x - border, origin.y + size.y - border},
    };
    const Emerald::Vec2 destSize[9] = {
        {border, border}, {dstMidW, border}, {border, border},
        {border, dstMidH}, {dstMidW, dstMidH}, {border, dstMidH},
        {border, border}, {dstMidW, border}, {border, border},
    };
    for (int index = 0; index < 9; ++index) {
        if (sourceSize[index].x <= 0.0f || sourceSize[index].y <= 0.0f ||
            destSize[index].x <= 0.0f || destSize[index].y <= 0.0f)
            continue;
        Emerald::Sprite sprite = Emerald::Sprite::FromTexture(*skin.Image);
        sprite = sprite.Crop(sources[index], sourceSize[index]);
        Emerald::SpriteOptions options;
        options.Size = destSize[index];
        options.Origin = {0.0f, 0.0f};
        options.PixelSnap = true;
        renderer.DrawSprite(sprite, dest[index], options);
    }
    return true;
}

f32 DrawPackText(Emerald::Renderer2D& renderer, const ArtGallery* gallery, std::string_view role,
                 const Emerald::Vec2& position, std::string_view text, const Emerald::Vec4& color,
                 i32 scale, f32 maxWidth)
{
    const Emerald::Font* font = gallery != nullptr ? gallery->FindFont(role) : nullptr;
    if (font == nullptr && role != "body" && gallery != nullptr)
        font = gallery->FindFont("body");
    if (font != nullptr) {
        renderer.DrawString(*font, text, position, color, 1.0f);
        return position.y + font->GetLineHeight();
    }
    return Emerald::DrawDebugText(renderer, position, text, color, scale, maxWidth);
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
