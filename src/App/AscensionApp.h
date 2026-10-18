#pragma once

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Ascension/Progress/Hero.h"
#include "Ascension/Progress/Inventory.h"
#include "Emerald/Core/Application.h"
#include "Playtest.h"

namespace Ascension {

// One hero against an encounter. Wins grant the sum of each enemy's XP and gold. Between
// fights, the shop from shop.json
// buys and sells. At an evolution level the next fight waits until the player picks a path,
// and that screen is the only place a build reset clears skill ranks. Consumables in the bag
// are extra turn actions. C opens every stack; number keys still cover the ones that fit.
class AscensionApp : public Emerald::Application {
public:
    AscensionApp(const Emerald::ApplicationSpec& spec, PlaytestOptions options);

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
    // Writes a target-change line when the marked enemy moved. previous is the index
    // before the action. A single enemy, or a marker that stayed put, writes nothing.
    void NoteMarked(i32 previous);
    void FinishAction(const ActionResult& result, bool playerActed);
    void DrinkAt(i32 bagIndex);
    void HandlePicker(SDL_Scancode scancode);
    void ClampCamp();
    void HandleCamp(SDL_Scancode scancode);
    void DrawCamp(Emerald::Renderer2D& renderer, f32 width, f32 height);
    // --choose, once the evolution choice is open and a fight is not underway.
    // False when that pick failed and the run is stopping.
    bool TakeAutoPath();
    // One scripted key per frame. The press goes through OnEvent, so the window
    // does not need focus. The run ends after the last key unless --frames is longer.
    void PlayScriptedKey();

    // Declared before m_Data: the database is opened on this command's folder.
    PlaytestOptions m_Play;
    std::ofstream m_Transcript;
    usize m_KeyCursor = 0;

    Database m_Data;
    Hero m_Hero;
    Inventory m_Inventory;
    std::unique_ptr<Battle> m_Battle;
    std::vector<std::string> m_Log;
    std::string m_StartError;
    i32 m_EnemyIndex = 0;
    // A roster foe from --enemy that is not an encounter. Empty fights use the encounter list.
    std::string m_DirectEnemy;
    f32 m_ReloadWait = 0.0f;
    bool m_XpAwarded = false;
    bool m_Camp = false;
    i32 m_CampColumn = 0;
    i32 m_CampRow = 0;
    bool m_Picking = false;
    i32 m_PickRow = 0;
};

} // namespace Ascension
