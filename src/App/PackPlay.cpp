#include "PackPlay.h"

#include <algorithm>

#include "ArtGallery.h"
#include "Ascension/Data/Database.h"
#include "Emerald/Audio/Sound.h"

namespace Ascension {

namespace {

struct CueRow {
    const char* Key;
    const char* Match;
};

constexpr CueRow kAudioCues[] = {
    {"confirm", "click"}, {"confirm", "confirm"}, {"confirm", "ui"},
    {"cancel", "cancel"}, {"hit", "hit"},         {"slash", "slash"},
    {"fire", "fire"},     {"ice", "ice"},         {"lightning", "lightning"},
    {"holy", "holy"},     {"nature", "nature"},   {"coin", "coin"},
    {"chest", "chest"},   {"level", "level"},     {"spell", "spell"},
};

constexpr CueRow kVfxCues[] = {
    {"hit", "hit"},     {"slash", "slash"}, {"fire", "fire"},
    {"ice", "ice"},     {"lightning", "lightning"}, {"holy", "holy"},
    {"nature", "nature"}, {"coin", "coin"}, {"chest", "chest"},
    {"level", "level"}, {"spell", "spell"},
};

struct PlaceRow {
    const char* Map;
    const char* Prefix;
    const char* Id;
};

constexpr PlaceRow kPlaces[] = {
    {"hub", "", "meadow"},
    {"overland", "", "forest"},
    {"sluice", "", "forest"},
    {"", "barrow", "crystal_cave"},
};

bool Contains(std::string_view text, std::string_view needle)
{
    return !needle.empty() && text.find(needle) != std::string_view::npos;
}

std::string ExtensionOf(const std::filesystem::path& file)
{
    std::string text = file.extension().string();
    for (char& code : text) {
        if (code >= 'A' && code <= 'Z')
            code = static_cast<char>(code - 'A' + 'a');
    }
    if (!text.empty() && text.front() == '.')
        text.erase(0, 1);
    return text;
}

std::string_view BackdropId(std::string_view mapId)
{
    for (const PlaceRow& place : kPlaces) {
        if (place.Map[0] != '\0' && mapId == place.Map)
            return place.Id;
        if (place.Prefix[0] != '\0' && mapId.starts_with(place.Prefix))
            return place.Id;
    }
    return {};
}

std::string ActionKey(const Database& data, std::string_view skillId, i32 damage)
{
    const SkillDef* skill = skillId.empty() ? nullptr : data.FindSkill(skillId);
    if (skill != nullptr) {
        if (skill->School == "holy" || Contains(skill->Id, "holy"))
            return "holy";
        if (Contains(skill->Id, "fire") || Contains(skill->Name, "Fire"))
            return "fire";
        if (Contains(skill->Id, "ice") || Contains(skill->Name, "Ice"))
            return "ice";
        if (Contains(skill->Id, "lightning") || Contains(skill->Name, "Lightning"))
            return "lightning";
        if (Contains(skill->Id, "nature") || Contains(skill->Name, "Nature"))
            return "nature";
        if (skill->School == "arcane")
            return "lightning";
        if (Contains(skill->Id, "slash"))
            return "slash";
        if (!skill->School.empty())
            return "spell";
    }
    if (damage > 0)
        return "hit";
    return {};
}

} // namespace

const std::filesystem::path* PackPlay::FindCue(const ArtPack& pack, std::string_view key) const
{
    for (const CueRow& row : kAudioCues) {
        if (key != row.Key)
            continue;
        for (const std::filesystem::path& file : pack.AudioFiles()) {
            if (Contains(file.generic_string(), row.Match))
                return &file;
        }
        for (const ArtAudioRole& role : pack.AudioRoles()) {
            if (Contains(role.File.generic_string(), row.Match))
                return &role.File;
        }
    }
    return nullptr;
}

void PackPlay::PlayFile(Emerald::Audio& audio, const ArtPack& pack, const std::filesystem::path& file,
                        Emerald::AudioGroup group, f32 volume, bool loop, Emerald::VoiceHandle* held)
{
    if (!pack.HasArchive() || file.empty())
        return;
    const Emerald::Sound* sound = nullptr;
    for (const Cached& cached : m_Sounds) {
        if (cached.Path == file.generic_string())
            sound = &cached.Sound;
    }
    if (sound == nullptr) {
        std::vector<unsigned char> bytes;
        std::string error;
        if (!pack.ReadFile(file, bytes, error))
            return;
        const std::string kind = ExtensionOf(file);
        std::optional<Emerald::Sound> decoded;
        if (kind == "ogg")
            decoded = Emerald::LoadOggFromMemory(bytes.data(), bytes.size());
        else if (kind == "mp3")
            decoded = Emerald::LoadMp3FromMemory(bytes.data(), bytes.size());
        else if (kind == "wav")
            decoded = Emerald::LoadSound(file);
        if (!decoded || decoded->IsEmpty())
            return;
        Cached cached;
        cached.Path = file.generic_string();
        cached.Sound = std::move(*decoded);
        m_Sounds.push_back(std::move(cached));
        sound = &m_Sounds.back().Sound;
    }
    Emerald::PlayOptions options;
    options.Volume = volume;
    options.Group = group;
    options.Loop = loop;
    const Emerald::VoiceHandle voice = audio.Play(*sound, options);
    if (held != nullptr) {
        if (held->IsValid())
            audio.Stop(*held, 200.0f);
        *held = voice;
    }
}

void PackPlay::Cue(Emerald::Audio& audio, const ArtGallery* gallery, std::string_view key)
{
    if (gallery == nullptr || !gallery->GetPack().HasArchive() || key.empty())
        return;
    const std::filesystem::path* file = FindCue(gallery->GetPack(), key);
    if (file == nullptr)
        return;
    const Emerald::AudioGroup group =
        key == "confirm" || key == "cancel" ? Emerald::AudioGroup::Ui : Emerald::AudioGroup::Sfx;
    PlayFile(audio, gallery->GetPack(), *file, group, 0.8f, false, nullptr);
}

void PackPlay::OnAction(Emerald::Audio& audio, const ArtGallery* gallery, const Database& data,
                        std::string_view skillId, i32 damage, bool levelUp, bool gold, bool chest,
                        const Emerald::Vec2& at)
{
    if (levelUp) {
        Cue(audio, gallery, "level");
        m_Fx.push_back({"level", at, 0.0f});
    }
    if (gold) {
        Cue(audio, gallery, "coin");
        m_Fx.push_back({"coin", at, 0.0f});
    }
    if (chest) {
        Cue(audio, gallery, "chest");
        m_Fx.push_back({"chest", at, 0.0f});
    }
    const std::string key = ActionKey(data, skillId, damage);
    if (!key.empty()) {
        Cue(audio, gallery, key);
        m_Fx.push_back({key, at, 0.0f});
    }
    if (damage > 0)
        m_Numbers.push_back({std::to_string(damage), at, 0.0f});
}

void PackPlay::Sync(Emerald::Audio& audio, const ArtGallery* gallery, std::string_view screen,
                    std::string_view mapId, bool fighting, bool victory, f32 dt)
{
    for (Live& fx : m_Fx)
        fx.Age += dt;
    m_Fx.erase(std::remove_if(m_Fx.begin(), m_Fx.end(), [](const Live& fx) { return fx.Age > 0.6f; }),
               m_Fx.end());
    for (FloatText& number : m_Numbers)
        number.Age += dt;
    m_Numbers.erase(std::remove_if(m_Numbers.begin(), m_Numbers.end(),
                                   [](const FloatText& number) { return number.Age > 0.8f; }),
                    m_Numbers.end());
    if (gallery == nullptr || !gallery->GetPack().HasArchive())
        return;
    const ArtPack& pack = gallery->GetPack();
    if (victory)
        m_Victory = 3.5f;
    else if (m_Victory > 0.0f)
        m_Victory = std::max(0.0f, m_Victory - dt);

    std::string music = "overland";
    std::string ambience;
    if (m_Victory > 0.0f)
        music = "victory";
    else if (screen == "title")
        music = "title";
    else if (fighting)
        music = "battle";
    else if (mapId == "hub")
        music = "hub";
    else if (mapId.starts_with("barrow"))
        music = "barrow";
    if (!fighting && m_Victory <= 0.0f && screen != "title") {
        if (mapId == "hub")
            ambience = "ambience_hub";
        else if (mapId.starts_with("barrow"))
            ambience = "ambience_barrow";
        else if (!mapId.empty())
            ambience = "ambience_overland";
    } else if (fighting) {
        if (mapId == "hub")
            ambience = "ambience_hub_intense";
        else if (mapId.starts_with("barrow"))
            ambience = "ambience_barrow_intense";
        else if (!mapId.empty())
            ambience = "ambience_overland_intense";
    }

    if (music != m_Music) {
        const ArtAudioRole* role = pack.FindAudio(music);
        if (role != nullptr) {
            std::vector<unsigned char> bytes;
            std::string error;
            if (pack.ReadFile(role->File, bytes, error)) {
                const std::string kind = ExtensionOf(role->File);
                const bool loop = music != "victory";
                if (audio.CrossfadeMusicMemory(bytes, kind, 800.0f, loop))
                    m_Music = music;
            }
        }
    }
    if (ambience != m_Ambience) {
        m_Ambience = ambience;
        if (ambience.empty()) {
            audio.Stop(m_AmbienceVoice, 200.0f);
            m_AmbienceVoice = {};
        } else {
            const ArtAudioRole* role = pack.FindAudio(ambience);
            if (role != nullptr)
                PlayFile(audio, pack, role->File, Emerald::AudioGroup::Sfx, 0.28f, true,
                         &m_AmbienceVoice);
        }
    }
}

void PackPlay::DrawBackdrop(Emerald::Renderer2D& renderer, const ArtGallery* gallery,
                            std::string_view mapId, const Emerald::Vec2& view)
{
    if (gallery == nullptr || !gallery->GetPack().HasArchive())
        return;
    const std::string_view backdropId = BackdropId(mapId);
    const ArtBackdrop* backdrop =
        backdropId.empty() ? nullptr : gallery->GetPack().FindBackdrop(backdropId);
    if (backdrop != nullptr) {
        const std::vector<std::filesystem::path> layers =
            backdrop->Layers.empty() ? std::vector<std::filesystem::path>{backdrop->Flat}
                                     : backdrop->Layers;
        for (const std::filesystem::path& layer : layers) {
            const Emerald::Texture* texture = gallery->FindTexture(layer);
            if (texture == nullptr)
                continue;
            Emerald::Sprite sprite = Emerald::Sprite::FromTexture(*texture);
            Emerald::SpriteOptions options;
            options.Size = view;
            options.Origin = {0.0f, 0.0f};
            options.PixelSnap = true;
            renderer.DrawSprite(sprite, {0.0f, 0.0f}, options);
        }
    }
}

void PackPlay::DrawFx(Emerald::Renderer2D& renderer, const ArtGallery* gallery, f32 dt)
{
    if (gallery == nullptr || !gallery->GetPack().HasArchive())
        return;
    (void)dt;
    for (const Live& fx : m_Fx) {
        const ArtNamedFile* sheet = nullptr;
        for (const CueRow& row : kVfxCues) {
            if (fx.Id != row.Key)
                continue;
            for (const ArtNamedFile& file : gallery->GetPack().Vfx()) {
                if (Contains(file.Id, row.Match)) {
                    sheet = &file;
                    break;
                }
            }
            if (sheet != nullptr)
                break;
        }
        if (sheet == nullptr)
            continue;
        const Emerald::Texture* texture = gallery->FindTexture(sheet->File);
        if (texture == nullptr)
            continue;
        const i32 frames = sheet->Frames > 0 ? sheet->Frames : 1;
        const i32 index = ArtFrameIndex(frames, 80, fx.Age);
        i32 frameWidth = sheet->FrameWidth;
        i32 frameHeight = sheet->FrameHeight;
        if (frameWidth < 1)
            frameWidth = static_cast<i32>(texture->GetWidth()) / frames;
        if (frameHeight < 1)
            frameHeight = static_cast<i32>(texture->GetHeight());
        Emerald::Sprite sprite = Emerald::Sprite::FromTexture(*texture);
        sprite = sprite.Crop({static_cast<f32>(index * frameWidth), 0.0f},
                             {static_cast<f32>(frameWidth), static_cast<f32>(frameHeight)});
        Emerald::SpriteOptions options;
        options.Size = {96.0f, 96.0f};
        options.Origin = {0.5f, 0.5f};
        options.PixelSnap = true;
        renderer.DrawSprite(sprite, fx.At, options);
    }

    for (const FloatText& number : m_Numbers) {
        const Emerald::Vec2 at{number.At.x, number.At.y - number.Age * 24.0f};
        DrawPackText(renderer, gallery, "damage", at, number.Text, {1.0f, 0.95f, 0.8f, 1.0f}, 2);
    }
}

} // namespace Ascension
