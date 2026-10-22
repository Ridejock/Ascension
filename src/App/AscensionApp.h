#pragma once

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Ascension/Data/Talk.h"
#include "Ascension/Map/Walk.h"
#include "Ascension/Progress/Hero.h"
#include "Ascension/Progress/Inventory.h"
#include "Ascension/Progress/RunSave.h"
#include "Ascension/Progress/Vitals.h"
#include "Emerald/Core/Application.h"
#include "Playtest.h"

#include <SDL3/SDL_gamepad.h>

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
    ~AscensionApp() override;

protected:
    void OnStart() override;
    void OnEvent(const SDL_Event& event) override;
    void OnUpdate(f32 dt) override;
    void OnRender2D(Emerald::Renderer2D& renderer) override;

private:
    void BeginFight();
    // Starts the battle at the hero's carried HP and focus. A lone Start() fills both;
    // this path is the one the encounter list uses.
    bool OpenBattle(std::string_view fightId, std::string& error);
    // A loss fills both pools and marks a hub return. Gold is not touched.
    void RememberDefeat();
    // Standing on a rest point fills both pools, once per arrival.
    void NoteRest();
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
    void OpenPads();
    void OpenPad(SDL_JoystickID id);
    void ClosePad(SDL_JoystickID id);
    // Lists the run already keeps. "found" is claimed drops. "defeated" is felled foes.
    // The talk matches those names from data. No item id is named here.
    void FillTalkFacts(TalkList lists[2], TalkFacts& facts) const;
    bool OpenTalk();
    void HandleTalk(SDL_Scancode scancode);
    void RunTalkAction(std::string_view action);
    void NoteTalk(const DialogueNode& node);
    void DrawTalk(Emerald::Renderer2D& renderer, f32 width, f32 height);
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
    // MoveNorth/East/South/West on the arrows, WASD, the d-pad, and the left stick.
    // The fight screen does not read these actions.
    void BindMapActions();
    void StepMap(f32 dt);
    void AdoptMap();
    [[nodiscard]] WalkDir DirectionOfKey(SDL_Scancode scancode);
    [[nodiscard]] WalkInput ReadMapInput();

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
    bool m_Talk = false;
    i32 m_TalkRow = 0;
    std::string m_TalkNode;
    std::vector<std::string> m_TalkFlags;
    struct HeldPad {
        SDL_JoystickID Id = 0;
        SDL_Gamepad* Pad = nullptr;
    };
    std::vector<HeldPad> m_Pads;
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
    // Set only for --map. The fight screen stays the path when this is false.
    bool m_OnMap = false;
    Walker m_Walker;
    // A scripted key does not reach Emerald's keyboard, so the map records it here
    // for the step taken later in the same frame.
    WalkDir m_ScriptDir = WalkDir::None;
    // HP and focus between fights. A new hero is full. A win writes the leftover
    // back. A loss fills both and sets ReturnToHub. The save still uses version 3,
    // which does not store this.
    Vitals m_Vitals;
    // True once this fight's end has been written into m_Vitals. R mid-fight leaves
    // it clear, so the retry starts from the pools the fight opened with.
    bool m_VitalsSettled = false;
    // The tile NoteRest last considered, so standing still does not log Rested again.
    bool m_RestSeen = false;
    std::string m_RestMap;
    MapTile m_RestTile;
};

} // namespace Ascension
