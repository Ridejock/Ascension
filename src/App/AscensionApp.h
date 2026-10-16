#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Ascension/Progress/Hero.h"
#include "Emerald/Core/Application.h"

namespace Ascension {

// One Fighter against one enemy. Wins grant XP from the enemy. At the evolution level the
// player picks Berserker or Guardian. Skill ranks are counted here and shown on the skills.
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
    void PushRanks(std::string_view pendingSkill = {});
    void AwardXp();
    void Note(std::string line);
    void Note(const ActionResult& result);
    void FinishAction(const ActionResult& result, bool playerActed);

    Database m_Data;
    Hero m_Hero;
    std::unique_ptr<Battle> m_Battle;
    std::vector<std::string> m_Log;
    std::string m_StartError;
    i32 m_EnemyIndex = 0;
    f32 m_ReloadWait = 0.0f;
    bool m_XpAwarded = false;
};

} // namespace Ascension
