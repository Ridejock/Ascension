#include "Playtest.h"

#include <charconv>
#include <fstream>
#include <utility>

#include "Ascension/Data/ArtPack.h"
#include "Emerald/Core/Paths.h"

namespace Ascension {

namespace {

std::string_view Trim(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r' ||
                             text.front() == '\n'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' ||
                             text.back() == '\n'))
        text.remove_suffix(1);
    return text;
}

bool IsSpace(char code)
{
    return code == ' ' || code == '\t' || code == '\r' || code == '\n';
}

bool ParseInt(std::string_view text, i32& value)
{
    if (text.empty())
        return false;
    const char* first = text.data();
    const char* last = first + text.size();
    i32 parsed = 0;
    const std::from_chars_result result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc() || result.ptr != last)
        return false;
    value = parsed;
    return true;
}

std::string Lower(std::string_view text)
{
    std::string out(text);
    for (char& code : out) {
        if (code >= 'A' && code <= 'Z')
            code = static_cast<char>(code - 'A' + 'a');
    }
    return out;
}

// Emerald reads these from the same argv (DevOptions.h, plus --gpu). Their values are not
// playtest flags. --profile is the one that does not take a value.
bool IsEngineFlag(std::string_view flag)
{
    return flag == "--frames" || flag == "--screenshot" || flag == "--capture" ||
           flag == "--capture-fps" || flag == "--record" || flag == "--replay" ||
           flag == "--profile" || flag == "--profile-trace" || flag == "--gpu";
}

bool EngineFlagTakesValue(std::string_view flag)
{
    return flag != "--profile";
}

bool LookupKey(std::string_view name, SDL_Scancode& scancode, std::string& error)
{
    const std::string key = Lower(name);
    if (key.size() == 1 && key[0] >= '1' && key[0] <= '8') {
        scancode = static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (key[0] - '1'));
        return true;
    }
    if (key == "c")
        scancode = SDL_SCANCODE_C;
    else if (key == "e")
        scancode = SDL_SCANCODE_E;
    else if (key == "u")
        scancode = SDL_SCANCODE_U;
    else if (key == "x")
        scancode = SDL_SCANCODE_X;
    else if (key == "i")
        scancode = SDL_SCANCODE_I;
    else if (key == "r")
        scancode = SDL_SCANCODE_R;
    else if (key == "n")
        scancode = SDL_SCANCODE_N;
    else if (key == "space")
        scancode = SDL_SCANCODE_SPACE;
    else if (key == "up")
        scancode = SDL_SCANCODE_UP;
    else if (key == "down")
        scancode = SDL_SCANCODE_DOWN;
    else if (key == "left")
        scancode = SDL_SCANCODE_LEFT;
    else if (key == "right")
        scancode = SDL_SCANCODE_RIGHT;
    else if (key == "w")
        scancode = SDL_SCANCODE_W;
    else if (key == "a")
        scancode = SDL_SCANCODE_A;
    else if (key == "s")
        scancode = SDL_SCANCODE_S;
    else if (key == "d")
        scancode = SDL_SCANCODE_D;
    else if (key == "escape")
        scancode = SDL_SCANCODE_ESCAPE;
    else if (key == "f")
        scancode = SDL_SCANCODE_F;
    else if (key == "f5")
        scancode = SDL_SCANCODE_F5;
    else {
        error = "unknown key '" + std::string(name) + "'";
        return false;
    }
    return true;
}

bool ParseKeyList(std::string_view text, std::vector<SDL_Scancode>& keys, std::string& error)
{
    usize index = 0;
    while (index < text.size()) {
        while (index < text.size() && IsSpace(text[index]))
            ++index;
        if (index >= text.size())
            break;
        usize end = index;
        while (end < text.size() && !IsSpace(text[end]))
            ++end;
        SDL_Scancode scancode = SDL_SCANCODE_UNKNOWN;
        if (!LookupKey(text.substr(index, end - index), scancode, error))
            return false;
        keys.push_back(scancode);
        index = end;
    }
    return true;
}

bool ReadKeysFile(const std::filesystem::path& path, std::vector<SDL_Scancode>& keys,
                  std::string& error)
{
    std::ifstream in(path);
    if (!in) {
        error = "could not read keys file '" + path.string() + "'";
        return false;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return ParseKeyList(text, keys, error);
}

bool ParseGiveList(std::string_view text, std::vector<PlaytestItem>& items, std::string& error)
{
    if (Trim(text).empty()) {
        error = "--give needs an item id";
        return false;
    }
    usize start = 0;
    while (start <= text.size()) {
        const usize comma = text.find(',', start);
        const usize stop = comma == std::string_view::npos ? text.size() : comma;
        const std::string_view token = Trim(text.substr(start, stop - start));
        if (token.empty()) {
            error = "--give has an empty entry";
            return false;
        }
        std::string_view id = token;
        i32 count = 1;
        if (const usize colon = token.find(':'); colon != std::string_view::npos) {
            id = Trim(token.substr(0, colon));
            const std::string_view countText = Trim(token.substr(colon + 1));
            if (id.empty() || countText.empty() || !ParseInt(countText, count) || count < 1) {
                error = "--give needs a count of at least 1 in '" + std::string(token) + "'";
                return false;
            }
        }
        if (id.empty()) {
            error = "--give needs an item id";
            return false;
        }
        PlaytestItem item;
        item.Id = std::string(id);
        item.Count = count;
        items.push_back(std::move(item));
        if (comma == std::string_view::npos)
            break;
        start = comma + 1;
    }
    return true;
}

bool ParseEquipList(std::string_view text, std::vector<std::string>& ids, std::string& error)
{
    if (Trim(text).empty()) {
        error = "--equip needs an item id";
        return false;
    }
    usize start = 0;
    while (start <= text.size()) {
        const usize comma = text.find(',', start);
        const usize stop = comma == std::string_view::npos ? text.size() : comma;
        const std::string_view id = Trim(text.substr(start, stop - start));
        if (id.empty()) {
            error = "--equip has an empty entry";
            return false;
        }
        ids.emplace_back(id);
        if (comma == std::string_view::npos)
            break;
        start = comma + 1;
    }
    return true;
}

bool TakeValue(std::span<const std::string_view> args, usize& index,
               const std::optional<std::string_view>& attached, std::string_view flag,
               std::string_view& value, std::string& error)
{
    if (attached.has_value()) {
        value = *attached;
        return true;
    }
    if (index + 1 >= args.size()) {
        error = std::string(flag) + " needs a value";
        return false;
    }
    value = args[++index];
    return true;
}

bool AlreadySet(bool set, std::string_view flag, std::string& error)
{
    if (!set)
        return false;
    error = std::string(flag) + " was already set";
    return true;
}

bool GrantLevel(Hero& hero, const Database& data, i32 level, std::string& error)
{
    const ProgressRules& rules = data.GetProgress();
    if (level < 1) {
        error = "level " + std::to_string(level) + " is below 1";
        return false;
    }
    if (rules.XpToReach.empty() || level > static_cast<i32>(rules.XpToReach.size())) {
        error = "level " + std::to_string(level) + " is past the curve";
        return false;
    }
    const i32 target = rules.XpToReach[static_cast<usize>(level - 1)];
    const i32 have = hero.Xp();
    if (target > have)
        hero.GrantXp(target - have);
    if (hero.Level() != level) {
        error = "level " + std::to_string(level) + " is past the curve";
        return false;
    }
    error.clear();
    return true;
}

bool ApplyClass(Hero& hero, const Database& data, std::string_view classId, std::string& error)
{
    if (classId.empty()) {
        error.clear();
        return true;
    }
    const ClassDef* picked = data.FindClass(classId);
    if (picked == nullptr) {
        error = "class '" + std::string(classId) + "' does not exist";
        return false;
    }
    if (picked->Id == hero.ClassId()) {
        error.clear();
        return true;
    }
    if (!picked->IsEvolution()) {
        if (!picked->Starter) {
            error = "class '" + std::string(classId) + "' is not a starter";
            return false;
        }
        if (!hero.AdoptStarter(classId, error))
            return false;
        error.clear();
        return true;
    }
    const ClassDef* parent = data.FindClass(picked->EvolvesFrom);
    const ClassDef* current = data.FindClass(hero.ClassId());
    if (parent != nullptr && parent->Starter && current != nullptr && current->Starter &&
        current->Id != parent->Id) {
        if (!hero.AdoptStarter(parent->Id, error))
            return false;
    }
    if (!hero.ChoicePending()) {
        error = "class '" + std::string(classId) + "' is not offered";
        return false;
    }
    std::string why;
    if (!hero.Choose(classId, why)) {
        error = why.find(classId) == std::string::npos
                    ? "class '" + std::string(classId) + "' is not offered"
                    : std::move(why);
        return false;
    }
    error.clear();
    return true;
}

i32 BagSlotOf(const Inventory& bag, std::string_view id)
{
    for (i32 slot = 0; slot < bag.BagSlots(); ++slot) {
        const ItemStack& stack = bag.BagAt(slot);
        if (!stack.Empty() && stack.Id == id)
            return slot;
    }
    return -1;
}

bool ApplyEquip(Inventory& inventory, const Database& data, std::string_view id, std::string& error)
{
    const ItemDef* gear = data.FindItem(id);
    if (gear == nullptr) {
        error = "item '" + std::string(id) + "' does not exist";
        return false;
    }
    if (gear->Slot == GearSlot::Consumable) {
        error = "item '" + std::string(id) + "' is not gear";
        return false;
    }
    i32 slot = BagSlotOf(inventory, gear->Id);
    if (slot < 0) {
        if (!inventory.Give(gear->Id, 1, error))
            return false;
        slot = BagSlotOf(inventory, gear->Id);
    }
    if (slot < 0) {
        error = "item '" + std::string(id) + "' could not be equipped";
        return false;
    }
    std::string why;
    if (!inventory.Equip(slot, why)) {
        error = "item '" + std::string(id) + "': " + why;
        return false;
    }
    error.clear();
    return true;
}

bool CheckChooseId(const Hero& hero, const Database& data, std::string_view classId,
                   std::string& error)
{
    if (classId.empty()) {
        error.clear();
        return true;
    }
    const ClassDef* path = data.FindClass(classId);
    if (path == nullptr) {
        error = "class '" + std::string(classId) + "' does not exist";
        return false;
    }
    if (!path->IsEvolution()) {
        error = "class '" + std::string(classId) + "' is not an evolution";
        return false;
    }
    if (!hero.ChoicePending()) {
        error.clear();
        return true;
    }
    for (const std::string& offered : hero.ChoiceIds()) {
        if (offered == classId) {
            error.clear();
            return true;
        }
    }
    error = "class '" + std::string(classId) + "' is not offered";
    return false;
}

bool CheckPickIds(const Hero& hero, const Database& data, std::span<const std::string> ids,
                  std::string& error)
{
    for (const std::string& id : ids) {
        if (data.FindSkill(id) == nullptr) {
            error = "skill '" + id + "' does not exist";
            return false;
        }
    }
    // Walk the picks this level can already resolve, on a copy, so a bad id fails before
    // the window and the hero passed in is left unpicked.
    Hero probe = hero;
    for (const std::string& id : ids) {
        if (!probe.PickPending())
            break;
        if (!probe.Pick(id, error))
            return false;
    }
    error.clear();
    return true;
}

} // namespace

bool PlaytestOptions::UsesData() const
{
    return !DataDir.empty() || Level.has_value() || !ClassId.empty() || !Fight.Id.empty() ||
           !MapId.empty() || Gold.has_value() || !Give.empty() || !Equip.empty() ||
           !ChooseId.empty() || !PickIds.empty() || Sim;
}

bool ParsePlaytestOptions(std::span<const std::string_view> args, PlaytestOptions& options,
                          std::string& error)
{
    bool sawKeys = false;
    bool sawKeysFile = false;
    bool sawRuns = false;
    bool sawSeed = false;
    bool sawSlip = false;
    for (usize index = 1; index < args.size(); ++index) {
        std::string_view token = args[index];
        if (token.empty())
            continue;
        if (!token.starts_with("--")) {
            error = "unexpected argument '" + std::string(token) + "'";
            return false;
        }
        std::optional<std::string_view> attached;
        std::string_view flag = token;
        if (const usize equals = flag.find('='); equals != std::string_view::npos) {
            attached = flag.substr(equals + 1);
            flag = flag.substr(0, equals);
        }
        if (IsEngineFlag(flag)) {
            if (!attached.has_value() && EngineFlagTakesValue(flag)) {
                if (index + 1 >= args.size()) {
                    error = std::string(flag) + " needs a value";
                    return false;
                }
                ++index;
            }
            continue;
        }

        std::string_view value;
        if (flag == "--data") {
            if (AlreadySet(!options.DataDir.empty(), flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            if (value.empty()) {
                error = "--data needs a directory";
                return false;
            }
            options.DataDir = std::string(value);
        } else if (flag == "--art-pack") {
            if (AlreadySet(!options.ArtPackDir.empty(), flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            if (value.empty()) {
                error = "--art-pack needs a directory";
                return false;
            }
            options.ArtPackDir = std::string(value);
        } else if (flag == "--transcript") {
            if (AlreadySet(!options.Transcript.empty(), flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            if (value.empty()) {
                error = "--transcript needs a file";
                return false;
            }
            options.Transcript = std::string(value);
        } else if (flag == "--level") {
            if (AlreadySet(options.Level.has_value(), flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            i32 level = 0;
            if (!ParseInt(value, level)) {
                error = "--level needs a number, got '" + std::string(value) + "'";
                return false;
            }
            options.Level = level;
        } else if (flag == "--class") {
            if (AlreadySet(!options.ClassId.empty(), flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            if (value.empty()) {
                error = "--class needs an id";
                return false;
            }
            options.ClassId = std::string(value);
        } else if (flag == "--enemy") {
            if (AlreadySet(!options.Fight.Id.empty(), flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            if (value.empty()) {
                error = "--enemy needs an id";
                return false;
            }
            options.Fight.Id = std::string(value);
        } else if (flag == "--map") {
            if (AlreadySet(!options.MapId.empty(), flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            if (value.empty()) {
                error = "--map needs an id";
                return false;
            }
            options.MapId = std::string(value);
        } else if (flag == "--gold") {
            if (AlreadySet(options.Gold.has_value(), flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            i32 gold = 0;
            if (!ParseInt(value, gold)) {
                error = "--gold needs a number, got '" + std::string(value) + "'";
                return false;
            }
            options.Gold = gold;
        } else if (flag == "--give") {
            if (AlreadySet(!options.Give.empty(), flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            if (!ParseGiveList(value, options.Give, error))
                return false;
        } else if (flag == "--equip") {
            if (AlreadySet(!options.Equip.empty(), flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            if (!ParseEquipList(value, options.Equip, error))
                return false;
        } else if (flag == "--keys") {
            if (sawKeysFile) {
                error = "pass --keys or --keys-file, not both";
                return false;
            }
            if (AlreadySet(sawKeys, flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            sawKeys = true;
            if (!ParseKeyList(value, options.Keys, error))
                return false;
        } else if (flag == "--keys-file") {
            if (sawKeys) {
                error = "pass --keys or --keys-file, not both";
                return false;
            }
            if (AlreadySet(sawKeysFile, flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            if (value.empty()) {
                error = "--keys-file needs a file";
                return false;
            }
            sawKeysFile = true;
            if (!ReadKeysFile(std::string(value), options.Keys, error))
                return false;
        } else if (flag == "--choose") {
            if (AlreadySet(!options.ChooseId.empty(), flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            if (value.empty()) {
                error = "--choose needs an id";
                return false;
            }
            options.ChooseId = std::string(value);
        } else if (flag == "--pick") {
            if (!TakeValue(args, index, attached, flag, value, error))
                return false;
            if (value.empty()) {
                error = "--pick needs an id";
                return false;
            }
            options.PickIds.emplace_back(value);
        } else if (flag == "--focus") {
            if (AlreadySet(!options.FocusId.empty(), flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            if (value.empty()) {
                error = "--focus needs an enemy id";
                return false;
            }
            options.FocusId = std::string(value);
        } else if (flag == "--save-dir") {
            if (AlreadySet(!options.SaveDir.empty(), flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            if (value.empty()) {
                error = "--save-dir needs a directory";
                return false;
            }
            options.SaveDir = std::string(value);
        } else if (flag == "--trace") {
            if (attached.has_value()) {
                error = "--trace does not take a value";
                return false;
            }
            options.Trace = true;
        } else if (flag == "--sim") {
            if (attached.has_value()) {
                error = "--sim does not take a value";
                return false;
            }
            if (options.Sim) {
                error = "--sim was already set";
                return false;
            }
            options.Sim = true;
        } else if (flag == "--chain") {
            if (attached.has_value()) {
                error = "--chain does not take a value";
                return false;
            }
            if (options.Chain) {
                error = "--chain was already set";
                return false;
            }
            options.Chain = true;
        } else if (flag == "--route") {
            if (attached.has_value()) {
                error = "--route does not take a value";
                return false;
            }
            if (options.Route) {
                error = "--route was already set";
                return false;
            }
            options.Route = true;
        } else if (flag == "--runs") {
            if (AlreadySet(sawRuns, flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            i32 runs = 0;
            if (!ParseInt(value, runs) || runs < 1 || runs > 100000) {
                error = "--runs needs a count from 1 to 100000";
                return false;
            }
            sawRuns = true;
            options.Runs = runs;
        } else if (flag == "--seed") {
            if (AlreadySet(sawSeed, flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            i32 seed = 0;
            if (!ParseInt(value, seed) || seed < 0) {
                error = "--seed needs a non-negative integer";
                return false;
            }
            sawSeed = true;
            options.Seed = seed == 0 ? 1 : seed;
        } else if (flag == "--slip") {
            if (AlreadySet(sawSlip, flag, error) ||
                !TakeValue(args, index, attached, flag, value, error))
                return false;
            i32 slip = 0;
            if (!ParseInt(value, slip) || slip < 0 || slip > 100) {
                error = "--slip needs a percent from 0 to 100";
                return false;
            }
            sawSlip = true;
            options.Slip = slip;
        } else {
            error = "unknown flag '" + std::string(flag) + "'";
            return false;
        }
    }
    if (options.Chain && !options.Sim) {
        error = "--chain is only used with --sim";
        return false;
    }
    if (options.Route && !options.Sim) {
        error = "--route is only used with --sim";
        return false;
    }
    if (options.Chain && options.Route) {
        error = "pass --chain or --route, not both";
        return false;
    }
    if (options.Sim && !options.Chain && !options.Route && options.Fight.Id.empty()) {
        error = "--sim needs --enemy";
        return false;
    }
    if (options.Chain && !options.Fight.Id.empty()) {
        error = "pass --chain or --enemy, not both";
        return false;
    }
    if (options.Route && !options.Fight.Id.empty()) {
        error = "pass --route or --enemy, not both";
        return false;
    }
    if (options.Chain && !options.FocusId.empty()) {
        error = "--chain does not take --focus";
        return false;
    }
    if (options.Route && !options.FocusId.empty()) {
        error = "--route does not take --focus";
        return false;
    }
    if (options.Chain && (sawRuns || sawSeed || sawSlip)) {
        error = "--chain does not take --runs, --seed, or --slip";
        return false;
    }
    if (options.Route && (sawRuns || sawSeed || sawSlip)) {
        error = "--route does not take --runs, --seed, or --slip";
        return false;
    }
    if (options.Route && !options.MapId.empty()) {
        error = "pass --route or --map, not both";
        return false;
    }
    if (!options.MapId.empty() && options.Sim) {
        error = "pass --map or --sim, not both";
        return false;
    }
    if (!options.MapId.empty() && !options.Fight.Id.empty()) {
        error = "pass --map or --enemy, not both";
        return false;
    }
    if (options.Sim && !options.Keys.empty()) {
        error = "pass --sim or --keys, not both";
        return false;
    }
    if (!options.FocusId.empty() && !options.Sim) {
        error = "--focus is only used with --sim";
        return false;
    }
    if (sawRuns && !options.Sim) {
        error = "--runs is only used with --sim";
        return false;
    }
    if (sawSeed && !options.Sim) {
        error = "--seed is only used with --sim";
        return false;
    }
    if (sawSlip && !options.Sim) {
        error = "--slip is only used with --sim";
        return false;
    }
    error.clear();
    return true;
}

std::filesystem::path PlaytestDataDirectory(const PlaytestOptions& options)
{
    if (!options.DataDir.empty())
        return options.DataDir;
#ifdef ASCENSION_DATA_DIR
    const std::filesystem::path source = ASCENSION_DATA_DIR;
    std::error_code missing;
    if (std::filesystem::is_directory(source, missing))
        return source;
#endif
    return Emerald::Paths::GetBasePath() / "data";
}

std::string MarkedTargetLine(std::string_view name)
{
    return "Target: " + std::string(name) + ".";
}

bool ResolveFight(const Database& data, const FightRequest& request, ResolvedFight& resolved,
                  std::string& error)
{
    if (request.Id.empty()) {
        error = "--enemy needs an id";
        return false;
    }
    // Encounter ids take precedence. wolf and oathkeeper_varn are both an encounter and
    // an enemy; --enemy wolf_pack is only an encounter.
    const auto encounters = data.GetEncounters();
    for (i32 encounterIndex = 0; encounterIndex < static_cast<i32>(encounters.size());
         ++encounterIndex) {
        if (encounters[static_cast<usize>(encounterIndex)].Id != request.Id)
            continue;
        resolved.Kind = FightKind::Encounter;
        resolved.Id = request.Id;
        resolved.EnemyIndex = encounterIndex;
        error.clear();
        return true;
    }
    const auto roster = data.GetEnemies();
    for (i32 rosterIndex = 0; rosterIndex < static_cast<i32>(roster.size()); ++rosterIndex) {
        if (roster[static_cast<usize>(rosterIndex)].Id != request.Id)
            continue;
        resolved.Kind = FightKind::Enemy;
        resolved.Id = request.Id;
        resolved.EnemyIndex = rosterIndex;
        error.clear();
        return true;
    }
    error = "enemy '" + request.Id + "' does not exist";
    return false;
}

bool ApplyPlaytest(Hero& hero, Inventory& inventory, const Database& data,
                   const PlaytestOptions& options, PlaytestSetup& setup, std::string& error)
{
    if (!hero.Start(data, error))
        return false;
    if (!inventory.Start(data, error))
        return false;

    if (options.Level.has_value() && !GrantLevel(hero, data, *options.Level, error))
        return false;
    if (!ApplyClass(hero, data, options.ClassId, error))
        return false;
    if (options.Gold.has_value() && !inventory.SetGold(*options.Gold, error))
        return false;

    for (const PlaytestItem& item : options.Give) {
        if (!inventory.Give(item.Id, item.Count, error))
            return false;
    }
    for (const std::string& id : options.Equip) {
        if (!ApplyEquip(inventory, data, id, error))
            return false;
    }

    setup.EnemyIndex = 0;
    setup.DirectEnemy.clear();
    if (!options.Fight.Id.empty()) {
        ResolvedFight resolved;
        if (!ResolveFight(data, options.Fight, resolved, error))
            return false;
        if (resolved.Kind == FightKind::Encounter)
            setup.EnemyIndex = resolved.EnemyIndex;
        else
            setup.DirectEnemy = resolved.Id;
    }
    if (!CheckChooseId(hero, data, options.ChooseId, error))
        return false;
    if (!CheckPickIds(hero, data, options.PickIds, error))
        return false;
    if (!options.MapId.empty() && data.FindMap(options.MapId) == nullptr) {
        error = "map '" + options.MapId + "' does not exist";
        return false;
    }
    error.clear();
    return true;
}

EvolutionPick PickEvolution(Hero& hero, std::string_view classId, std::string& error)
{
    if (classId.empty() || !hero.IsReady() || hero.ChoosingStarter() || !hero.ChoicePending()) {
        error.clear();
        return EvolutionPick::Waiting;
    }
    std::string why;
    if (!hero.Choose(classId, why)) {
        error = why.find(classId) == std::string::npos
                    ? "class '" + std::string(classId) + "' is not offered"
                    : std::move(why);
        return EvolutionPick::Failed;
    }
    error.clear();
    return EvolutionPick::Chosen;
}

SkillOptionPick PickSkillOption(Hero& hero, std::string_view skillId, std::string& error)
{
    if (skillId.empty() || !hero.IsReady() || !hero.PickPending()) {
        error.clear();
        return SkillOptionPick::Waiting;
    }
    std::string why;
    if (!hero.Pick(skillId, why)) {
        error = why.find(skillId) == std::string::npos
                    ? "skill '" + std::string(skillId) + "' is not offered"
                    : std::move(why);
        return SkillOptionPick::Failed;
    }
    error.clear();
    return SkillOptionPick::Chosen;
}

bool CheckPlaytestStartup(const PlaytestOptions& options, std::string& error)
{
    if (!options.Transcript.empty()) {
        std::ofstream transcript(options.Transcript, std::ios::app);
        if (!transcript) {
            error = "could not open transcript '" + options.Transcript.string() + "'";
            return false;
        }
    }
    const bool pack = !options.ArtPackDir.empty();
    if (!options.UsesData() && !pack) {
        error.clear();
        return true;
    }

    const std::filesystem::path directory = PlaytestDataDirectory(options);
    if (!options.DataDir.empty()) {
        std::error_code missing;
        if (!std::filesystem::is_directory(directory, missing)) {
            error = "data folder '" + options.DataDir.string() + "' is not a directory";
            return false;
        }
    }

    Database data(directory);
    DataError loadError;
    if (!data.Load(&loadError)) {
        error = loadError.Message.empty() ? "could not load data" : loadError.Message;
        return false;
    }
    if (pack) {
        ArtPack art;
        DataError packError;
        if (!art.Load(options.ArtPackDir, data, &packError)) {
            error = packError.Message.empty() ? "could not load art pack" : packError.Message;
            return false;
        }
    }
    if (!options.UsesData()) {
        error.clear();
        return true;
    }
    Hero hero;
    Inventory inventory;
    PlaytestSetup setup;
    return ApplyPlaytest(hero, inventory, data, options, setup, error);
}

std::string StartupFailureText(std::string_view startError, std::string_view loadError,
                               const char* fightBlock)
{
    if (!startError.empty())
        return std::string(startError);
    if (!loadError.empty())
        return std::string(loadError);
    if (fightBlock != nullptr && fightBlock[0] != '\0')
        return std::string(fightBlock);
    return "Could not load data/.";
}

} // namespace Ascension
