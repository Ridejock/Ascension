#pragma once

#include "Ascension/Data/Database.h"
#include "Emerald/Core/Application.h"

namespace Ascension {

// Loads data/ next to the executable and draws the Fighter with rectangles and placeholder text.
// CheckForChanges runs a few times a second, so saving a JSON file updates the screen.
class AscensionApp : public Emerald::Application {
public:
    explicit AscensionApp(const Emerald::ApplicationSpec& spec);

protected:
    void OnStart() override;
    void OnEvent(const SDL_Event& event) override;
    void OnUpdate(f32 dt) override;
    void OnRender2D(Emerald::Renderer2D& renderer) override;

private:
    Database m_Data;
    f32 m_ReloadWait = 0.0f;
};

} // namespace Ascension
