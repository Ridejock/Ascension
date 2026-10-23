#pragma once

#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Ascension/Data/Talk.h"
#include "Ascension/Map/Handoff.h"
#include "Ascension/Map/Movers.h"
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
    // Seats the ids a contact kept, instead of the encounter's whole list. Used when
    // those two lists differ. The ordinary spawn fight does not call it.
    bool OpenRoster(std::span<const std::string> foeIds, std::string& error);
    void ApplyOpeningStatuses();
    // True while a spawn's fight is on screen, including a win that is still choosing
    // a path. The map does not walk during that.
    [[nodiscard]] bool SpawnFightOpen() const;
    void TouchSpawn(const ContactQuery& query);
    void BeginSpawnFight();
    // Puts a finished spawn fight back on the map. A list fight never has one open.
    void MaybeSettle();
    // An ongoing save named a spawn. Opens that fight and puts the snapshot back.
    // A spawn id the data no longer has leaves the hero where they were standing.
    void OpenResumedSpawn();
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
    // `dialogueId` is a file under data/dialogue/. The fight screen passes the shop's
    // keeper. A map talk point passes that point's id.
    bool OpenTalk(std::string_view dialogueId);
    // The tile under the hero. A talk point opens its dialogue. A shop point opens
    // the shop. A rest point, and an empty tile, do nothing: rest already ran on
    // the step. False when there is nothing to open.
    bool UseMapPoint();
    // Stands on the saved tile, then `preferred` (--map), then the hub. False when
    // none of those maps can be entered.
    bool EnterOverland(std::string_view preferred);
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
    // Places for the map the hero is standing on. Cleared spawns are left out.
    void SyncPlaces();
    // One mover step. Idle does not change a tile, so a shipped map looks the same.
    void StepMovers();
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
    std::string m_TalkId;
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
    // The hero is walking a map. A launch with no --enemy opens the hub (or --map
    // when that was passed). --enemy stays on the fight screen.
    bool m_OnMap = false;
    Walker m_Walker;
    // A scripted key does not reach Emerald's keyboard, so the map records it here
    // for the step taken later in the same frame.
    WalkDir m_ScriptDir = WalkDir::None;
    // The spawn fight on screen. Active is false on the encounter list, and on the
    // map whenever the hero is walking. Cleared ids and flee wounds live in
    // m_MapMemory, and the save writes them by spawn id.
    struct MapEngagement {
        bool Active = false;
        std::string SpawnId;
        std::string EncounterId;
        std::string MapId;
        MapTile SpawnTile;
        MapTile ReturnTile;
        bool Once = true;
        bool AllowFlee = false;
        // Empty on a resumed fight and on a list fight. A contact copies the request.
        // When Foes matches the encounter, the fight starts the ordinary way.
        std::vector<std::string> Foes;
        std::vector<PartyFoe> Seats;
    };
    MapEngagement m_MapFight;
    MapMemory m_MapMemory;
    // Where this map's spawns are standing. Rebuilt from the file on entry, on an
    // exit, and after a reload. Idle leaves every tile on its home.
    std::vector<SpawnPlace> m_Places;
    std::string m_PlacesMap;
    // The place a save recorded when this launch is not standing on the map. The
    // walker is the place while --map is up. A fight-screen launch writes this back
    // so it does not erase a map the save already had.
    bool m_HasPlace = false;
    std::string m_PlaceMap;
    MapTile m_PlaceTile;
    // Spawn id from a save, until the snapshot is restored or dropped.
    std::string m_ResumeSpawn;
    // HP and focus between fights. A new hero is full. A win or a flee writes the
    // leftover back. A loss fills both and sets ReturnToHub. The save records the
    // pools whenever they are known.
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
