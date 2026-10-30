#pragma once

#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Combat/Battle.h"
#include "Ascension/Data/Talk.h"
#include "Ascension/Map/Handoff.h"
#include "Ascension/Map/Movers.h"
#include "Ascension/Map/Walk.h"
#include "Ascension/Progress/Hero.h"
#include "Ascension/Progress/Inventory.h"
#include "Ascension/Progress/DebugRun.h"
#include "Ascension/Progress/FrontDoor.h"
#include "Ascension/Progress/RunSave.h"
#include "Ascension/Progress/Vitals.h"
#include "Emerald/Core/Application.h"
#include "Ascension/Input/Controls.h"
#include "ArtGallery.h"
#include "Playtest.h"

namespace Ascension {

// One hero against an encounter. Wins grant the sum of each enemy's XP and gold. Between
// fights, the shop from shop.json
// buys and sells. At an evolution level the next fight waits until the player picks a path,
// and that screen is the only place a build reset clears skill ranks. A class may also offer
// a skill pick at a later level; that screen uses the same number keys and has no reset.
// Consumables in the bag are extra turn actions. The menu action opens every stack; number
// slots still cover the ones that fit. Eight slots is the cap, so a full skill bar leaves
// drinks on the menu action only. Keys and buttons for every action live in input.json.
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
    void HandlePicker(std::string_view action);
    void ClampCamp();
    void HandleCamp(std::string_view action);
    void DrawCamp(Emerald::Renderer2D& renderer, f32 width, f32 height);
    void DrawFight(Emerald::Renderer2D& renderer, f32 width, f32 height);
    // One line on the battle and shop screens. Further lines wait, and Confirm skips the wait.
    void ClearPresented();
    void PacePresented(f32 dt);
    void ActOneEnemy();
    // True when Confirm was spent showing the next waiting line.
    bool SkipPresented();
    // Scripted keys still resolve every waiting foe turn. A live fight waits for the message hold.
    void SettleFoes();
    void CommitShop();
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
    void HandleTalk(std::string_view action);
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
    // One scripted key per frame. The scancode is turned into the actions bound to it,
    // then HandleAction. The window does not need focus. The run ends after the last
    // key unless --frames is longer. Window key-downs are ignored until the script ends.
    void PlayScriptedKey();
    // Puts input.json onto Emerald's actions. False names the key or button in `error`
    // via the log and m_StartError.
    bool BindControls();
    // One frame of actions. Menus and the battle command cursor repeat a held direction.
    // The map's own step repeat reads the move actions in StepMap.
    void PollActions(f32 dt);
    void HandleAction(std::string_view action);
    // Opened and Resumed log a line. Quit writes the run and exits, the same path as Quit.
    void TakePauseStep(PauseStep step);
    void DrawPauseMenu(Emerald::Renderer2D& renderer, f32 width, f32 height);
    // While the pause menu is open, the bottom hint row shows confirm, cancel, and navigate.
    void RetargetPauseHints(std::vector<HintChip>& hints) const;
    // DebugRespawnFoes brings cleared foes back. An ongoing fight, or a map fight
    // that has not settled, is left alone and notes the busy line from input.json.
    void DebugRespawnFoes();
    // DebugNewGame twice, inside debugConfirmMilliseconds, opens character create.
    // From a running game, Begin replaces the run and leaves the save file alone.
    // From the title it is New game, including the overwrite ask.
    void DebugNewGame();
    // No save, or a save the player has not continued. Create is the other way in.
    void ShowTitle();
    // --class, --keys, and the other setup flags. The title is not shown.
    void BootPreparedRun();
    // After the hero exists: the hub, or the fight screen when --enemy was set.
    void FinishBoot(bool resumed);
    void ContinueSavedRun();
    void CommitCreatedRun();
    void PollFront(f32 dt);
    void TakeFrontStep(FrontStep step);
    void SyncTextInput();
    void DrawFront(Emerald::Renderer2D& renderer, f32 width, f32 height);
    // E is Confirm and a letter. A text event for that key does not also submit.
    [[nodiscard]] bool NameConfirmSuppressed();
    [[nodiscard]] std::string FrontNoticeLine() const;
    // Letters from the keyboard when this frame had no text event.
    void TypeNameFromKeys();
    void ApplyFreshRun(const FreshStart& fresh);
    void HandleOverland(std::string_view action);
    void HandleChoice(std::string_view action);
    void HandleBattle(std::string_view action);
    // Confirm on a skill arms a target. Confirm again uses it. Cancel leaves that step.
    void ConfirmCommand();
    void ChooseHotkey(int digit);
    void UseHotkey(int digit);
    void UseCommand(int row);
    [[nodiscard]] int ChoiceCount() const;
    [[nodiscard]] int CommandCount() const;
    [[nodiscard]] bool Choosing() const;
    [[nodiscard]] bool CursorActive() const;
    [[nodiscard]] bool Pressed(std::string_view action);
    // Held directions repeat after the initial pause, then at the repeat pause.
    struct NavHold {
        f32 Elapsed = 0.0f;
        bool Repeating = false;
    };
    // True on the press, then again after the pauses in input.json while it stays down.
    bool NavStep(NavHold& hold, std::string_view action, f32 dt);
    // The word drawn for an action: the pad label when a pad is connected, otherwise the
    // first key. Empty when that action has neither.
    [[nodiscard]] std::string ActionLabel(std::string_view action);
    [[nodiscard]] std::string CloseLabel();
    void FinishFrame(Emerald::Renderer2D& renderer, f32 width, f32 height);
    // The hint row already on this screen. Glyphs use prompts.json; missing art is the label.
    // helpLine hides the row when the help action has toggled it off. The map tile
    // prompt passes false: that line stays.
    void DrawHints(Emerald::Renderer2D& renderer, f32 x, f32 y, f32 maxX,
                   std::span<const HintChip> hints, bool helpLine);
    void EnsurePromptSheets();
    void NotePromptDevice();
    // Interact on a keyboard, Cancel on a pad. Both close the talk and the camp.
    [[nodiscard]] std::string_view CloseAction() const;
    void InspectTile();
    void InspectFoe();
    void StepMap(f32 dt);
    void AdoptMap();
    // Places for the map the hero is standing on. Cleared spawns are left out.
    void SyncPlaces();
    // One mover step. Idle does not change a tile, so a shipped map looks the same.
    void StepMovers();
    [[nodiscard]] WalkDir DirectionOfAction(std::string_view action) const;
    [[nodiscard]] WalkInput ReadMapInput();

    // Declared before m_Data: the database is opened on this command's folder.
    PlaytestOptions m_Play;
    std::ofstream m_Transcript;
    usize m_KeyCursor = 0;
    usize m_PickCursor = 0;
    // True while PlayScriptedKey is applying the actions bound to the scripted scancode.
    // Other key-downs in that moment belong to the window, not the script.
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
    // Empty when this launch has no art pack. Drawing then stays on the CC0 art.
    ArtGallery m_Gallery;
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
    // Drawn points for m_Places. The tiles themselves stay on m_Places.
    SpawnGlides m_Glides;
    // The place a save recorded when this launch is not standing on the map. The
    // walker is the place while --map is up. A fight-screen launch writes this back
    // so it does not erase a map the save already had.
    bool m_HasPlace = false;
    std::string m_PlaceMap;
    MapTile m_PlaceTile;
    // Spawn id from a save, until the snapshot is restored or dropped.
    std::string m_ResumeSpawn;
    // A loaded victory whose spawn is not cleared yet. BeginSpawnFight seals the
    // fight as a win so the choice can be answered, and does not grant XP again.
    bool m_HoldVictory = false;
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
    // Pause opens this menu on Continue. Confirm on that row, Cancel, and Pause
    // resume without saving. Quit game writes the run and exits. The map and
    // further actions wait. Edges are still read while it is open.
    PauseMenu m_Pause;
    // Back/View and the help key hide the line at the bottom. The map's tile prompt stays.
    bool m_ShowHelp = true;
    // The highlighted battle command (skills, then wait, items, flee) and choice row.
    i32 m_CommandRow = 0;
    // True after Confirm on a skill, until Confirm uses it or Cancel steps back.
    bool m_Aiming = false;
    enum class ShopAsk : u8 { None, Buy, Sell, Equip, Unequip };
    ShopAsk m_ShopAsk = ShopAsk::None;
    // The line on screen, and the lines still waiting. Age is seconds on the current line.
    std::string m_Shown;
    std::vector<std::string> m_Pending;
    f32 m_ShownAge = 0.0f;
    f32 m_TurnHold = 0.0f;
    i32 m_ChoiceRow = 0;
    // Held-direction repeat for the four moves and the two cycle actions, in that order.
    NavHold m_Nav[6]{};
    // Last device that pressed a key or a button. prompts.json's text family until a pad is used.
    std::string m_PromptFamily;
    // First DebugNewGame press. The second, inside the data window, opens create.
    ConfirmWindow m_NewGameConfirm;
    // Title until a save is continued, a hero is created, or a playtest flag skips it.
    FrontDoor m_Front;
    // ShowTitle reads the slot once. A later data reload does not ask again.
    bool m_SaveProbed = false;
    // Set when this frame's text event added to the name. Cleared at the end of the poll.
    bool m_NameTyped = false;
    struct PromptPicture {
        std::string Family;
        Emerald::AssetHandle<Emerald::Texture> Texture;
    };
    std::vector<PromptPicture> m_PromptSheets;
};

} // namespace Ascension
