#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Ascension/Progress/Hero.h"
#include "Ascension/Progress/Inventory.h"
#include "Emerald/Core/Application.h"

namespace Ascension {

// One Fighter against one enemy. Wins grant XP and gold. Between fights, Old Pell's Provisions
// buys and sells. At the evolution level the player picks Berserker or Guardian. Skill ranks
// are counted here and shown on the skills. Consumables in the bag are extra turn actions.
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
    void AwardVictory();
    void Note(std::string line);
    void Note(const ActionResult& result);
    void FinishAction(const ActionResult& result, bool playerActed);
    void ClampCamp();
    void HandleCamp(SDL_Scancode scancode);
    void DrawCamp(Emerald::Renderer2D& renderer, f32 width, f32 height);

    Database m_Data;
    Hero m_Hero;
    Inventory m_Inventory;
    std::unique_ptr<Battle> m_Battle;
    std::vector<std::string> m_Log;
    std::string m_StartError;
    i32 m_EnemyIndex = 0;
    f32 m_ReloadWait = 0.0f;
    bool m_XpAwarded = false;
    bool m_Camp = false;
    i32 m_CampColumn = 0;
    i32 m_CampRow = 0;
};

} // namespace Ascension
