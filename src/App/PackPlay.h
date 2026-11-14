#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/ArtPack.h"
#include "Emerald/Audio/Audio.h"
#include "Emerald/Math/Vec2.h"
#include "Emerald/Renderer/Renderer2D.h"

namespace Ascension {

class ArtGallery;
class Database;

// Music, ambience, one-shots, battle backdrops, and short VFX from a loaded pak.
// With no pak every call stays silent and draws nothing.
class PackPlay {
public:
    void Sync(Emerald::Audio& audio, const ArtGallery* gallery, std::string_view screen,
              std::string_view mapId, bool fighting, bool victory, f32 dt);
    void Cue(Emerald::Audio& audio, const ArtGallery* gallery, std::string_view key);
    void OnAction(Emerald::Audio& audio, const ArtGallery* gallery, const Database& data,
                  std::string_view skillId, i32 damage, bool levelUp, bool gold, bool chest,
                  const Emerald::Vec2& at);
    void DrawBackdrop(Emerald::Renderer2D& renderer, const ArtGallery* gallery,
                      std::string_view mapId, const Emerald::Vec2& view);
    void DrawFx(Emerald::Renderer2D& renderer, const ArtGallery* gallery, f32 dt);

private:
    struct Live {
        std::string Id;
        Emerald::Vec2 At{};
        f32 Age = 0.0f;
    };
    struct FloatText {
        std::string Text;
        Emerald::Vec2 At{};
        f32 Age = 0.0f;
    };
    struct Cached {
        std::string Path;
        Emerald::Sound Sound;
    };

    void PlayFile(Emerald::Audio& audio, const ArtPack& pack, const std::filesystem::path& file,
                  Emerald::AudioGroup group, f32 volume, bool loop, Emerald::VoiceHandle* held);
    [[nodiscard]] const std::filesystem::path* FindCue(const ArtPack& pack,
                                                       std::string_view key) const;

    std::string m_Music;
    std::string m_Ambience;
    Emerald::VoiceHandle m_AmbienceVoice;
    std::vector<Cached> m_Sounds;
    std::vector<Live> m_Fx;
    std::vector<FloatText> m_Numbers;
    f32 m_Victory = 0.0f;
};

} // namespace Ascension
