#include "ArtGallery.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <utility>

#include "Emerald/Assets/Image.h"
#include "Emerald/Core/Log.h"
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

bool IsPng(const std::filesystem::path& file)
{
    const std::string extension = file.extension().string();
    return extension == ".png" || extension == ".PNG";
}

double MillisecondsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

const char* SectionOf(ArtSlot slot)
{
    if (slot == ArtSlot::Tile)
        return "tiles";
    if (IsArtMarker(slot))
        return "markers";
    if (slot == ArtSlot::Prop)
        return "props";
    if (slot == ArtSlot::HeroBattle)
        return "heroes";
    if (slot == ArtSlot::NpcSprite || slot == ArtSlot::NpcPortrait)
        return "npcs";
    if (slot == ArtSlot::FoeOverland || slot == ArtSlot::FoeBattle)
        return "foes";
    return "tiles";
}

bool SkinIdHas(std::string_view id, std::string_view needle)
{
    return id.find(needle) != std::string_view::npos;
}

const ArtNineSlice* SliceNamed(const ArtPack& pack, std::string_view id)
{
    return pack.FindSlice(id);
}

} // namespace

void ArtGallery::Reset()
{
    m_Pack = {};
    m_Budget = {};
    m_Assets = nullptr;
    m_Device = nullptr;
    m_Queued.clear();
    m_Textures.clear();
    m_Fonts.clear();
    m_Skins.clear();
    m_Fault.clear();
    m_UploadMs = 0.0;
    m_DecodeMs = 0.0;
    m_Streamed = 0;
    m_OnDemand = 0;
    m_NotedResident = false;
    m_Pictures = 0;
    m_Sequence = 0;
    m_CatalogMs = 0.0;
    m_Seconds = 0.0f;
}

i32 ArtGallery::RankOf(std::string_view section) const
{
    for (i32 index = 0; index < static_cast<i32>(m_Budget.Order.size()); ++index) {
        if (m_Budget.Order[static_cast<usize>(index)] == section)
            return index;
    }
    return static_cast<i32>(m_Budget.Order.size());
}

bool ArtGallery::EnqueueImage(const std::filesystem::path& file, i32 frames, i32 frameWidth,
                              i32 frameHeight, std::string_view section, std::string& error)
{
    if (file.empty())
        return true;
    if (frames < 1 || frameWidth < 1 || frameHeight < 1) {
        error = "could not load '" + file.generic_string() + "'";
        return false;
    }
    const i32 rank = RankOf(section);
    for (Queued& job : m_Queued) {
        if (job.What != Queued::Kind::Image || job.File != file)
            continue;
        const i64 span = static_cast<i64>(frames) * static_cast<i64>(frameWidth);
        const i64 have = static_cast<i64>(job.Frames) * static_cast<i64>(job.FrameWidth);
        if (span > have) {
            job.Frames = frames;
            job.FrameWidth = frameWidth;
        }
        if (frameHeight > job.FrameHeight)
            job.FrameHeight = frameHeight;
        if (rank < job.Rank)
            job.Rank = rank;
        return true;
    }
    Queued job;
    job.What = Queued::Kind::Image;
    job.File = file;
    job.Frames = frames;
    job.FrameWidth = frameWidth;
    job.FrameHeight = frameHeight;
    job.Rank = rank;
    job.Sequence = m_Sequence++;
    m_Queued.push_back(std::move(job));
    ++m_Pictures;
    return true;
}

void ArtGallery::EnqueueFont(const ArtFontRole& role)
{
    if (role.File.empty() || role.Role.empty())
        return;
    for (const Queued& job : m_Queued) {
        if (job.What == Queued::Kind::Font && job.Role == role.Role)
            return;
    }
    Queued job;
    job.What = Queued::Kind::Font;
    job.File = role.File;
    job.Role = role.Role;
    job.FontSize = role.Size;
    job.Rank = RankOf("fonts");
    job.Sequence = m_Sequence++;
    m_Queued.push_back(std::move(job));
}

bool ArtGallery::QueuedImage(const std::filesystem::path& file) const
{
    for (const Queued& job : m_Queued) {
        if (job.What == Queued::Kind::Image && job.File == file)
            return true;
    }
    return false;
}

const Emerald::Texture* ArtGallery::Hold(std::filesystem::path file,
                                         Emerald::AssetHandle<Emerald::Texture> handle,
                                         std::unique_ptr<Emerald::Texture> owned) const
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

const Emerald::Texture* ArtGallery::ResidentTexture(const std::filesystem::path& file) const
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

bool ArtGallery::UploadImage(const Queued& job) const
{
    const std::string shown =
        m_Pack.HasArchive() ? job.File.generic_string() : job.File.string();
    if (m_Device == nullptr) {
        m_Fault = "could not load '" + shown + "'";
        return false;
    }
    if (m_Pack.HasArchive()) {
        std::vector<unsigned char> bytes;
        std::string why;
        if (!m_Pack.ReadFile(job.File, bytes, why)) {
            m_Fault = why.empty() ? "could not load '" + shown + "'" : why;
            return false;
        }
        const auto decodeStart = std::chrono::steady_clock::now();
        const std::optional<Emerald::Image> image =
            Emerald::Image::LoadFromMemory(bytes.data(), bytes.size());
        m_DecodeMs += MillisecondsSince(decodeStart);
        if (!image) {
            m_Fault = "could not load '" + shown + "'";
            return false;
        }
        Emerald::TextureOptions options;
        options.Filter = Emerald::TextureFilter::Nearest;
        const auto uploadStart = std::chrono::steady_clock::now();
        std::optional<Emerald::Texture> texture = Emerald::Texture::Create(m_Device, *image, options);
        m_UploadMs += MillisecondsSince(uploadStart);
        if (!texture || !texture->HasGpuTexture()) {
            m_Fault = "could not load '" + shown + "'";
            return false;
        }
        const u32 span = static_cast<u32>(job.FrameWidth) * static_cast<u32>(job.Frames);
        if (texture->GetWidth() < span || texture->GetHeight() < static_cast<u32>(job.FrameHeight)) {
            m_Fault = "'" + shown + "' does not match the frame size";
            return false;
        }
        (void)Hold(job.File, {}, std::make_unique<Emerald::Texture>(std::move(*texture)));
        return true;
    }
    if (m_Assets == nullptr) {
        m_Fault = "could not load '" + shown + "'";
        return false;
    }
    Emerald::TextureOptions options;
    options.Filter = Emerald::TextureFilter::Nearest;
    const auto uploadStart = std::chrono::steady_clock::now();
    Emerald::AssetHandle<Emerald::Texture> handle = m_Assets->Load<Emerald::Texture>(job.File, options);
    m_UploadMs += MillisecondsSince(uploadStart);
    if (!handle || !AssetIsLive(*m_Assets, handle.GetId()) || !handle->HasGpuTexture()) {
        m_Fault = "could not load '" + shown + "'";
        return false;
    }
    const u32 span = static_cast<u32>(job.FrameWidth) * static_cast<u32>(job.Frames);
    if (handle->GetWidth() < span || handle->GetHeight() < static_cast<u32>(job.FrameHeight)) {
        m_Fault = "'" + shown + "' does not match the frame size";
        return false;
    }
    (void)Hold(job.File, std::move(handle), {});
    return true;
}

bool ArtGallery::UploadFont(const Queued& job) const
{
    if (m_Device == nullptr)
        return false;
    const auto uploadStart = std::chrono::steady_clock::now();
    std::optional<Emerald::Font> face;
    if (m_Pack.HasArchive()) {
        std::vector<unsigned char> bytes;
        std::string why;
        if (!m_Pack.ReadFile(job.File, bytes, why))
            return false;
        face = Emerald::Font::LoadFromMemory(
            m_Device, std::span<const unsigned char>(bytes.data(), bytes.size()),
            {.Size = static_cast<f32>(job.FontSize), .Oversample = 1,
             .Filter = Emerald::TextureFilter::Nearest});
    } else {
        face = Emerald::Font::Load(m_Device, job.File,
                                   {.Size = static_cast<f32>(job.FontSize), .Oversample = 1,
                                    .Filter = Emerald::TextureFilter::Nearest});
    }
    m_UploadMs += MillisecondsSince(uploadStart);
    if (!face)
        return false;
    HeldFont held;
    held.Role = job.Role;
    held.Face = std::move(*face);
    m_Fonts.push_back(std::move(held));
    return true;
}

void ArtGallery::NoteIfResident() const
{
    if (m_NotedResident || !m_Queued.empty() || m_Pictures == 0 || m_Device == nullptr)
        return;
    m_NotedResident = true;
    EM_INFO("gallery resident: {} pictures, upload {:.1f} ms, decode {:.1f} ms, streamed {}, "
            "on demand {}",
            m_Pictures, m_UploadMs, m_DecodeMs, m_Streamed, m_OnDemand);
}

void ArtGallery::RememberSkins()
{
    m_Skins.clear();
    auto hasRole = [this](std::string_view role) {
        for (const SkinSource& source : m_Skins) {
            if (source.Role == role)
                return true;
        }
        return false;
    };
    auto addSlice = [this](const char* role, const ArtNineSlice* slice) {
        if (slice == nullptr || !QueuedImage(slice->File))
            return;
        SkinSource source;
        source.Role = role;
        source.File = slice->File;
        source.Ready.Border = slice->Border;
        m_Skins.push_back(std::move(source));
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

    auto addFile = [this, &hasRole](const char* role, std::string_view needle, i32 border) {
        for (const std::filesystem::path& file : m_Pack.UiFiles()) {
            const std::string text = file.generic_string();
            if (text.find(needle) == std::string::npos || !QueuedImage(file))
                continue;
            if (hasRole(role))
                return;
            SkinSource source;
            source.Role = role;
            source.File = file;
            source.Ready.Border = border;
            m_Skins.push_back(std::move(source));
            return;
        }
    };
    addFile("confirm", "button", 4);
    addFile("bar_emerald", "bar_fill_emerald", 0);
    addFile("bar_gold", "bar_fill_gold", 0);
    addFile("bar_ruby", "bar_fill_ruby", 0);
    addFile("bar_sapphire", "bar_fill_sapphire", 0);
}

bool ArtGallery::Load(Emerald::Assets* assets, Emerald::GpuDevice* device, const ArtPack& pack,
                      const GalleryBudget& budget, std::string& error)
{
    Reset();
    const auto catalogStart = std::chrono::steady_clock::now();
    m_Budget = budget;
    m_Assets = assets;
    m_Device = device;
    m_Pack = pack;
    auto fail = [this, &error](std::string message) {
        Reset();
        error = std::move(message);
        return false;
    };
    auto need = [this, &error](const std::filesystem::path& file, i32 frames, i32 frameWidth,
                               i32 frameHeight, std::string_view section) {
        if (!EnqueueImage(file, frames, frameWidth, frameHeight, section, error))
            return false;
        return true;
    };
    for (const ArtImage& image : pack.GetImages()) {
        const char* section = SectionOf(image.Slot);
        for (const ArtPicture& picture : image.Pictures) {
            if (!need(picture.File, picture.Frames, picture.FrameWidth, picture.FrameHeight, section))
                return fail(error);
        }
    }
    for (const ArtNineSlice& slice : pack.Slices()) {
        if (!need(slice.File, 1, 1, 1, "ui"))
            return fail(error);
    }
    for (const std::filesystem::path& file : pack.UiFiles()) {
        if (IsPng(file) && !need(file, 1, 1, 1, "ui"))
            return fail(error);
    }
    for (const ArtNamedFile& file : pack.Icons()) {
        if (!need(file.File, file.Frames, file.FrameWidth, file.FrameHeight, "icons"))
            return fail(error);
    }
    for (const ArtNamedFile& file : pack.Items()) {
        if (!need(file.File, file.Frames, file.FrameWidth, file.FrameHeight, "items"))
            return fail(error);
    }
    for (const ArtNamedFile& file : pack.Vfx()) {
        if (!need(file.File, file.Frames, file.FrameWidth, file.FrameHeight, "vfx"))
            return fail(error);
    }
    for (const ArtBackdrop& backdrop : pack.Backdrops()) {
        if (!need(backdrop.Flat, 1, 1, 1, "backdrops"))
            return fail(error);
        for (const std::filesystem::path& layer : backdrop.Layers) {
            if (!need(layer, 1, 1, 1, "backdrops"))
                return fail(error);
        }
    }
    for (const ArtFontRole& role : pack.Fonts())
        EnqueueFont(role);
    std::stable_sort(m_Queued.begin(), m_Queued.end(), [](const Queued& left, const Queued& right) {
        if (left.Rank != right.Rank)
            return left.Rank < right.Rank;
        return left.Sequence < right.Sequence;
    });
    RememberSkins();
    m_CatalogMs = MillisecondsSince(catalogStart);
    if (m_Device != nullptr) {
        EM_INFO("gallery deferred: {} pictures, catalog {:.1f} ms, {} waiting", m_Pictures,
                m_CatalogMs, m_Queued.size());
    }
    error.clear();
    return true;
}

void ArtGallery::Pump()
{
    if (!m_Fault.empty() || m_Device == nullptr)
        return;
    if (m_Budget.UploadsPerFrame <= 0 || m_Budget.BudgetMilliseconds <= 0)
        return;
    const auto start = std::chrono::steady_clock::now();
    i32 uploaded = 0;
    while (uploaded < m_Budget.UploadsPerFrame && !m_Queued.empty()) {
        if (uploaded > 0 && MillisecondsSince(start) >= static_cast<double>(m_Budget.BudgetMilliseconds))
            break;
        Queued job = std::move(m_Queued.front());
        m_Queued.erase(m_Queued.begin());
        const bool ok = job.What == Queued::Kind::Image ? UploadImage(job) : UploadFont(job);
        if (job.What == Queued::Kind::Image && !ok)
            return;
        if (ok)
            ++m_Streamed;
        ++uploaded;
    }
    NoteIfResident();
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
    if (const Emerald::Texture* ready = ResidentTexture(file))
        return ready;
    for (usize index = 0; index < m_Queued.size(); ++index) {
        if (m_Queued[index].What != Queued::Kind::Image || m_Queued[index].File != file)
            continue;
        Queued job = std::move(m_Queued[index]);
        m_Queued.erase(m_Queued.begin() + static_cast<std::ptrdiff_t>(index));
        if (!UploadImage(job))
            return nullptr;
        ++m_OnDemand;
        NoteIfResident();
        return ResidentTexture(file);
    }
    return nullptr;
}

const Emerald::Font* ArtGallery::FindFont(std::string_view role) const
{
    for (const HeldFont& font : m_Fonts) {
        if (font.Role == role)
            return &font.Face;
    }
    for (usize index = 0; index < m_Queued.size(); ++index) {
        if (m_Queued[index].What != Queued::Kind::Font || m_Queued[index].Role != role)
            continue;
        Queued job = std::move(m_Queued[index]);
        m_Queued.erase(m_Queued.begin() + static_cast<std::ptrdiff_t>(index));
        if (UploadFont(job)) {
            ++m_OnDemand;
            NoteIfResident();
            for (const HeldFont& font : m_Fonts) {
                if (font.Role == role)
                    return &font.Face;
            }
        }
        break;
    }
    if (role != "fallback")
        return FindFont("fallback");
    return nullptr;
}

const ArtGallery::Skin* ArtGallery::FindSkin(std::string_view role) const
{
    for (SkinSource& source : m_Skins) {
        if (source.Role != role)
            continue;
        if (source.Ready.Image == nullptr)
            source.Ready.Image = FindTexture(source.File);
        if (source.Ready.Image == nullptr)
            return nullptr;
        return &source.Ready;
    }
    return nullptr;
}

std::vector<std::filesystem::path> ArtGallery::UploadOrder() const
{
    std::vector<std::filesystem::path> order;
    order.reserve(m_Queued.size());
    for (const Queued& job : m_Queued)
        order.push_back(job.File);
    return order;
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
