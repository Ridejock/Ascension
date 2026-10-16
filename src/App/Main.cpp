#include "AscensionApp.h"

#include <SDL3/SDL_gpu.h>

int main(int argc, char** argv)
{
    Emerald::ApplicationSpec spec;
    spec.Window.Title = "Ascension";
    spec.Window.Width = 1280;
    spec.Window.Height = 720;
    spec.Args = {argv, static_cast<usize>(argc)};
    // The formats emerald_add_shaders actually compiled (shadercross, not fxc).
    spec.ShaderFormats = EMERALD_SHADER_FORMATS;
    spec.LogFile = "logs/Ascension.log";
    spec.ClearColor = {0.06f, 0.07f, 0.08f, 1.0f};

    Ascension::AscensionApp app(spec);
    return app.Run();
}
