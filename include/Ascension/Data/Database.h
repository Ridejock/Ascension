#pragma once

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/Data/Map.h"
#include "Ascension/Data/Types.h"
#include "Ascension/Input/Controls.h"
#include "Ascension/Input/Prompts.h"

namespace Ascension {

// One problem from a load or a reload. Message names the file and the field, for example
// "classes.json: class 'berserker': skill 'smash' does not exist".
struct DataError {
    std::string Message;
};

// Where one of the hero's skills comes from. Id is the class id or the item id.
enum class GrantSource : u8 { Class, Item };

struct GrantOrigin {
    GrantSource Source = GrantSource::Class;
    std::string Id;
};

// One skill the hero has in a fight. One entry per skill id, in first-source order: class
// actives, kept pick options, class passives, then each worn item. Skill is the merged copy.
// Each tunable numeric field keeps the best value across every source; which way is better
// is the "better" table in effects.json. A non-numeric tune (target, stat, scaling, status)
// keeps the first source. Origins lists every source that granted the id, first source first.
// This is a snapshot: ask again after a reload.
struct HeroGrant {
    SkillDef Skill;
    std::vector<GrantOrigin> Origins;
};

// The JSON files in a data directory (stats, effects, skills, statuses, classes, enemies,
// encounters, items, rarities, shop, combat, progress, tiles, walk, movers, input, prompts), every
// data/dialogue/*.json file, every data/maps/*.json file, plus the icon sheet under
// assets/icons, checked
// against each other. Load once, then call CheckForChanges from the frame loop: a file
// that changed, and then stayed unchanged for one more check, is read again. A new or
// removed file under dialogue/ or maps/ is the same kind of change. A failed read keeps
// the previous definitions and sets GetLastError().
//
// Pointers returned by Find* and by ActiveSkills / Passives point into this database. Look them
// up again after a reload; do not keep them across a frame.
class Database {
public:
    // `directory` is where the JSON files live (relative paths are made absolute).
    explicit Database(std::filesystem::path directory);

    // Reads every file. On failure the previous good data stays, GetLastError() is set, and
    // `error` (when not null) receives the same message.
    bool Load(DataError* error = nullptr);

    // One hot-reload check, right here. A change is applied once the new modification time has
    // been seen twice in a row, so a file that is still being written is left alone. Returns 1
    // when the data was replaced, otherwise 0 (including a failed read).
    u32 CheckForChanges(DataError* error = nullptr);

    [[nodiscard]] bool IsLoaded() const { return m_Revision > 0; }
    // Starts at 0. Goes up by one on every successful load, so a screen can see that the file
    // changed even when the numbers it draws happen to match.
    [[nodiscard]] u32 GetRevision() const { return m_Revision; }
    [[nodiscard]] const std::filesystem::path& GetDirectory() const { return m_Directory; }
    // Empty after a successful load. Set when the last attempt failed.
    [[nodiscard]] const std::string& GetLastError() const { return m_LastError; }

    [[nodiscard]] std::span<const StatDef> GetStats() const { return m_Stats; }
    [[nodiscard]] std::span<const SkillDef> GetSkills() const { return m_Skills; }
    [[nodiscard]] std::span<const StatusDef> GetStatuses() const { return m_Statuses; }
    [[nodiscard]] std::span<const ClassDef> GetClasses() const { return m_Classes; }
    [[nodiscard]] std::span<const EnemyDef> GetEnemies() const { return m_Enemies; }
    [[nodiscard]] std::span<const EncounterDef> GetEncounters() const { return m_Encounters; }
    [[nodiscard]] std::span<const ItemDef> GetItems() const { return m_Items; }
    [[nodiscard]] std::span<const RarityDef> GetRarities() const { return m_Rarities; }
    [[nodiscard]] std::span<const IconDef> GetIcons() const { return m_Icons; }
    [[nodiscard]] const ShopDef& GetShop() const { return m_Shop; }
    [[nodiscard]] std::span<const DialogueDef> GetDialogues() const { return m_Dialogues; }
    // Tile kinds in file order, and maps in file-name order.
    [[nodiscard]] std::span<const TileDef> GetTiles() const { return m_Tiles; }
    [[nodiscard]] std::span<const MapDef> GetMaps() const { return m_Maps; }
    // Named map steps, in file order. A spawn's mover names one of these.
    [[nodiscard]] std::span<const MoverDef> GetMovers() const { return m_Movers; }
    // icons_sheet.json. The accent sheet is icons_accent_sheet.png beside it. Dev builds use the
    // source assets/ when that file is present, and a packaged build uses assets/ next to data/.
    [[nodiscard]] const std::filesystem::path& GetIconSheet() const { return m_IconSheet; }
    [[nodiscard]] const CombatRules& GetRules() const { return m_Rules; }
    [[nodiscard]] const ProgressRules& GetProgress() const { return m_Progress; }
    [[nodiscard]] const WalkRules& GetWalk() const { return m_Walk; }
    // Keyboard and gamepad bindings. The game applies these onto Emerald's actions.
    [[nodiscard]] const ControlRules& GetControls() const { return m_Controls; }
    // Glyph sheets and the scales hint text uses beside them.
    [[nodiscard]] const PromptRules& GetPrompts() const { return m_Prompts; }

    [[nodiscard]] const StatDef* FindStat(std::string_view id) const;
    [[nodiscard]] const SkillDef* FindSkill(std::string_view id) const;
    [[nodiscard]] const StatusDef* FindStatus(std::string_view id) const;
    [[nodiscard]] const ClassDef* FindClass(std::string_view id) const;
    [[nodiscard]] const EnemyDef* FindEnemy(std::string_view id) const;
    [[nodiscard]] const EncounterDef* FindEncounter(std::string_view id) const;
    [[nodiscard]] const ItemDef* FindItem(std::string_view id) const;
    [[nodiscard]] const RarityDef* FindRarity(std::string_view id) const;
    [[nodiscard]] const IconDef* FindIcon(std::string_view id) const;
    [[nodiscard]] const DialogueDef* FindDialogue(std::string_view id) const;
    [[nodiscard]] const TileDef* FindTile(std::string_view id) const;
    [[nodiscard]] const MapDef* FindMap(std::string_view id) const;
    // The map is the one that holds the spawn. Both pointers are null when the id is absent.
    [[nodiscard]] MapSpawnRef FindSpawn(std::string_view id) const;
    [[nodiscard]] const MoverDef* FindMover(std::string_view id) const;

    // Evolutions whose EvolvesFrom is `baseId`, in file order.
    [[nodiscard]] std::vector<const ClassDef*> EvolutionsOf(std::string_view baseId) const;

    // Base classes marked starter, in file order. Reset offers this list, not a hard-coded class.
    [[nodiscard]] std::vector<const ClassDef*> Starters() const;

    // Active skills this class has, parent first, then the ones it adds. Each pointer is that
    // grant's own resolved copy (the catalog skill, then the grant's tune). FindSkill returns
    // the catalog entry, which has no grant tune. Empty when the class grants none.
    [[nodiscard]] std::vector<const SkillDef*> ActiveSkills(const ClassDef& klass) const;
    [[nodiscard]] std::vector<const SkillDef*> Passives(const ClassDef& klass) const;

    // Class actives, then the kept pick options, then class passives, then each worn item's
    // grants. `pickedIds` are option skill ids from this class's picks (parent first); an id
    // no pick offers adds nothing. Unknown item ids add nothing. Ranks are keyed by skill id,
    // so a merged copy keeps the one rank already earned. An option the class already grants
    // merges into that skill: the best value wins per field.
    [[nodiscard]] std::vector<HeroGrant>
    HeroGrants(const ClassDef& klass, std::span<const std::string> wornItemIds,
               std::span<const std::string> pickedIds = {}) const;

    // The picks this class offers, parent first. Pointers last until the next reload.
    [[nodiscard]] std::vector<const SkillPick*> PicksOf(const ClassDef& klass) const;

    // Stats at `level` (clamped to 1..99). A base class is Stats + Growth * (level - 1). An
    // evolution below its EvolveLevel is still the parent. At the evolve level and after, it is
    // the parent's stats at EvolveLevel, plus Bonus, plus Growth for each level past that.
    [[nodiscard]] StatBlock StatsAt(const ClassDef& klass, i32 level) const;

    // Descriptions with {placeholders} resolved from this entity (see README). A placeholder
    // the entity does not have fails Load. DescribeSkill then adds one sentence for each
    // effect that lands on an aim (damage, a status, a stun), using the effect's target, or
    // the skill's target when the effect doesn't set one. `rank` scales damage and reflect
    // power by (rank - 1) * powerPerRank, the same addition a fight uses. Rank 1 is the base.
    [[nodiscard]] std::string DescribeSkill(const SkillDef& skill, i32 rank = 1) const;
    [[nodiscard]] std::string Describe(const ClassDef& def) const;
    [[nodiscard]] std::string Describe(const EnemyDef& def) const;
    [[nodiscard]] std::string Describe(const StatusDef& def) const;
    [[nodiscard]] std::string Describe(const ItemDef& def) const;

private:
    struct FileWatch {
        std::filesystem::path Path;
        std::optional<std::filesystem::file_time_type> Loaded;
        std::optional<std::filesystem::file_time_type> Seen;
        bool Waiting = false;
    };

    void NoteFailure(const DataError& error, DataError* out);
    void NoteSuccess(DataError* out);
    void ApplyTimes(const std::vector<std::optional<std::filesystem::file_time_type>>& times);
    // True when data/dialogue or data/maps gained or lost a file. The next stable check reloads.
    [[nodiscard]] bool RefreshFolderWatches();

    std::filesystem::path m_Directory;
    std::vector<FileWatch> m_Watches;
    bool m_ListingArmed = false;
    std::vector<StatDef> m_Stats;
    std::vector<SkillDef> m_Skills;
    std::vector<StatusDef> m_Statuses;
    std::vector<ClassDef> m_Classes;
    std::vector<EnemyDef> m_Enemies;
    std::vector<EncounterDef> m_Encounters;
    std::vector<ItemDef> m_Items;
    std::vector<RarityDef> m_Rarities;
    std::vector<IconDef> m_Icons;
    std::filesystem::path m_IconSheet;
    ShopDef m_Shop;
    std::vector<DialogueDef> m_Dialogues;
    std::vector<TileDef> m_Tiles;
    std::vector<MapDef> m_Maps;
    std::vector<MoverDef> m_Movers;
    CombatRules m_Rules;
    ProgressRules m_Progress;
    WalkRules m_Walk;
    ControlRules m_Controls;
    PromptRules m_Prompts;
    // Field names from effects.json "better". The merge reads these; it does not decide
    // higher or lower from the field's name.
    std::vector<std::string> m_HigherFields;
    std::vector<std::string> m_LowerFields;
    std::string m_LastError;
    u32 m_Revision = 0;
};

} // namespace Ascension
