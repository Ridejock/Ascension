#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Emerald/Core/Application.h"

namespace Ascension {

// One Fighter against one enemy from data/. Skills, health and the damage formula come from
// the JSON, and a settled save updates the fight that is already on screen.
class AscensionApp : public Emerald::Application {
public:
    explicit AscensionApp(const Emerald::ApplicationSpec& spec);

protected:
    void OnStart() override;
    void OnEvent(const SDL_Event& event) override;
    void OnUpdate(f32 dt) override;
    void OnRender2D(Emerald::Renderer2D& renderer) override;

private:
    void BeginFight();
    void ResolveFoe();
    void Note(std::string line);
    void Note(const ActionResult& result);

    Database m_Data;
    std::unique_ptr<Battle> m_Battle;
    std::vector<std::string> m_Log;
    std::string m_StartError;
    i32 m_EnemyIndex = 0;
    f32 m_ReloadWait = 0.0f;
};

} // namespace Ascension
