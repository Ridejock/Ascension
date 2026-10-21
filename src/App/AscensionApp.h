#pragma once

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Ascension/Progress/Hero.h"
#include "Ascension/Progress/Inventory.h"
#include "Ascension/Progress/RunSave.h"
#include "Emerald/Core/Application.h"
#include "Playtest.h"

namespace Ascension {

// One hero against an encounter. Wins grant the sum of each enemy's XP and gold. Between
// fights, the shop from shop.json
// buys and sells. At an evolution level the next fight waits until the player picks a path,
// and that screen is the only place a build reset clears skill ranks. A class may also offer
// a skill pick at a later level; that screen uses the same number keys and has no reset.
// Consumables in the bag are extra turn actions. C opens every stack; number keys still
// cover the ones that fit. Eight number keys is the cap, so a full skill bar leaves drinks
// on C only.
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
    // A resumed victory or defeat, with no fight on screen. N and R still start one.
    void DrawBetween(Emerald::Renderer2D& renderer, f32 width, f32 height);
    [[nodiscard]] bool BetweenFights() const;
    // A scripted hero (--level, --class, and the rest) is not written unless --save-dir is set.
    [[nodiscard]] bool SavesProgress() const;
    [[nodiscard]] std::filesystem::path SaveFolder() const;
    bool WriteSave();
    // Loads the slot when this command does not build its own hero. A missing file is a new
    // run. A damaged file with no backup is a new run as well.
    bool TryResume(std::string& error);
    void RememberFoe(std::string_view id);
    void RememberVictory();
    // The icon sheet and its accent layer. Nearest filtering, so a scaled pixel stays a square.
    void EnsureIcons();
    // --choose, once the evolution choice is open, then --pick for each open skill pick.
    // Both wait until a fight is not underway. False when a pick failed and the run is stopping.
    bool TakeAutoPath();
    // One scripted key per frame. The press goes through OnEvent, so the window
    // does not need focus. The run ends after the last key unless --frames is longer.
    // Window key-downs are ignored until the script ends.
    void PlayScriptedKey();

    // Declared before m_Data: the database is opened on this command's folder.
    PlaytestOptions m_Play;
    std::ofstream m_Transcript;
    usize m_KeyCursor = 0;
    usize m_PickCursor = 0;
    // True while PlayScriptedKey is inside its own OnEvent. Other key-downs in that
    // moment belong to the window, not the script.
    bool m_ApplyingScript = false;

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
    std::vector<std::string> m_Defeated;
    // Unique drops already claimed. R starts the fight over and does not clear this.
    std::vector<std::string> m_Found;
    RunStanding m_Standing = RunStanding::None;
    bool m_ResumeResources = false;
    bool m_AnnounceResume = false;
    i32 m_ResumeHp = 0;
    i32 m_ResumeFocus = 0;
    FightSnapshot m_ResumeFight;
    bool m_Picking = false;
    i32 m_PickRow = 0;
    Emerald::AssetHandle<Emerald::TextureAtlas> m_Icons;
    Emerald::AssetHandle<Emerald::Texture> m_Accents;
};

} // namespace Ascension
