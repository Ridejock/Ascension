#include "AscensionApp.h"
#include "FightSim.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_gpu.h>

#include "Ascension/Data/ArtPack.h"
#include "Emerald/Core/DevOptions.h"
#include "Emerald/Core/Paths.h"

int main(int argc, char** argv)
{
    std::vector<std::string_view> args;
    if (argc > 0)
        args.reserve(static_cast<usize>(argc));
    for (int index = 0; index < argc; ++index) {
        if (argv[index] != nullptr)
            args.emplace_back(argv[index]);
    }

    Ascension::PlaytestOptions play;
    std::string why;
    if (!Ascension::ParsePlaytestOptions(args, play, why) ||
        !Ascension::DiscoverArtPack(play.ArtPackDir, Emerald::Paths::GetBasePath(), why) ||
        !Ascension::CheckPlaytestStartup(play, why)) {
        std::fprintf(stderr, "%s\n", why.c_str());
        return 1;
    }

    if (play.Sim)
        return Ascension::RunPlaytestSim(play);

    Emerald::ApplicationSpec spec;
    spec.Window.Title = "Ascension";
    spec.Window.Width = 1280;
    spec.Window.Height = 720;
    spec.Args = {argv, static_cast<usize>(argc)};
    // The formats emerald_add_shaders actually compiled (shadercross, not fxc).
    spec.ShaderFormats = EMERALD_SHADER_FORMATS;
    spec.LogFile = "logs/Ascension.log";
    spec.ClearColor = {0.06f, 0.07f, 0.08f, 1.0f};
    // A key script ends on its last frame, which is also the --screenshot frame. --frames on
    // the command line still wins: Emerald copies it over MaxFrames while reading argv.
    if (!play.Keys.empty()) {
        const Emerald::DevOptions dev = Emerald::ParseDevOptions(spec.Args);
        if (dev.Frames == 0)
            spec.MaxFrames = static_cast<decltype(spec.MaxFrames)>(play.Keys.size());
    }

    Ascension::AscensionApp app(spec, std::move(play));
    return app.Run();
}
