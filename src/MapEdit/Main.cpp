#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "Ascension/MapEdit/MapEdit.h"

namespace {

const char* kUsage =
    "mapedit check --data <dir>\n"
    "mapedit new --data <dir> --id <id> --kind overland|dungeon|town --width <n> --height <n>\n"
    "mapedit paint --data <dir> --map <id> --tile <id> --x <n> --y <n> [--w <n> --h <n>]\n"
    "mapedit start --data <dir> --map <id> --x <n> --y <n>\n"
    "mapedit link --data <dir> --map <id> --x <n> --y <n> --to <id> --ax <n> --ay <n>\n"
    "mapedit unlink --data <dir> --map <id> --x <n> --y <n>\n"
    "mapedit spawn --data <dir> --map <id> --id <id> --x <n> --y <n> --encounter <id>\n"
    "         [--once true|false] [--no-flee] [--mover <id>]\n"
    "         [--aura-status <id> --aura-radius <n>]\n"
    "mapedit unspawn --data <dir> --map <id> --id <id>\n"
    "mapedit point --data <dir> --map <id> --kind rest|shop|talk --x <n> --y <n> [--dialogue <id>]\n"
    "mapedit unpoint --data <dir> --map <id> --x <n> --y <n>\n"
    "mapedit prop --data <dir> --map <id> --id <id> --x <n> --y <n> [--w <n> --h <n>]\n"
    "         [--open x,y | --open x,y,w,h]\n"
    "mapedit unprop --data <dir> --map <id> --id <id>\n"
    "mapedit hub --data <dir> --map <id> [--off]\n";

struct Cli {
    int Argc = 0;
    char** Argv = nullptr;
    std::vector<bool> Used;
    std::string Error;

    explicit Cli(int argc, char** argv) : Argc(argc), Argv(argv), Used(static_cast<usize>(argc), false)
    {
        if (argc > 1)
            Used[1] = true;
    }

    [[nodiscard]] bool Take(const char* name, std::string& out, bool required)
    {
        for (int index = 2; index < Argc; ++index) {
            if (Used[static_cast<usize>(index)] || std::strcmp(Argv[index], name) != 0)
                continue;
            if (index + 1 >= Argc || Argv[index + 1][0] == '-') {
                Error = std::string(name) + " needs a value";
                return false;
            }
            Used[static_cast<usize>(index)] = true;
            Used[static_cast<usize>(index + 1)] = true;
            out = Argv[index + 1];
            return true;
        }
        if (required)
            Error = std::string(name) + " is required";
        return !required;
    }

    [[nodiscard]] bool TakeAll(const char* name, std::vector<std::string>& out)
    {
        for (int index = 2; index < Argc; ++index) {
            if (Used[static_cast<usize>(index)] || std::strcmp(Argv[index], name) != 0)
                continue;
            if (index + 1 >= Argc || Argv[index + 1][0] == '-') {
                Error = std::string(name) + " needs a value";
                return false;
            }
            Used[static_cast<usize>(index)] = true;
            Used[static_cast<usize>(index + 1)] = true;
            out.emplace_back(Argv[index + 1]);
        }
        return true;
    }

    [[nodiscard]] bool TakeInt(const char* name, i32& out, bool required)
    {
        std::string text;
        if (!Take(name, text, required))
            return false;
        if (text.empty())
            return true;
        i32 value = 0;
        for (const char c : text) {
            if (c < '0' || c > '9') {
                Error = std::string(name) + " must be a whole number";
                return false;
            }
            const i32 digit = c - '0';
            if (value > (2147483647 - digit) / 10) {
                Error = std::string(name) + " must be a whole number";
                return false;
            }
            value = value * 10 + digit;
        }
        out = value;
        return true;
    }

    [[nodiscard]] bool Switch(const char* name)
    {
        for (int index = 2; index < Argc; ++index) {
            if (Used[static_cast<usize>(index)] || std::strcmp(Argv[index], name) != 0)
                continue;
            Used[static_cast<usize>(index)] = true;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool Leftovers()
    {
        for (int index = 2; index < Argc; ++index) {
            if (!Used[static_cast<usize>(index)]) {
                Error = std::string("unknown argument ") + Argv[index];
                return true;
            }
        }
        return false;
    }
};

bool ParseOpen(const std::string& text, Ascension::MapOpenEdit& open, std::string& error)
{
    std::vector<i32> numbers;
    std::string token;
    const auto push = [&](const std::string& part) {
        if (part.empty())
            return false;
        i32 value = 0;
        for (const char c : part) {
            if (c < '0' || c > '9')
                return false;
            value = value * 10 + (c - '0');
        }
        numbers.push_back(value);
        return true;
    };
    for (const char c : text) {
        if (c == ',') {
            if (!push(token)) {
                error = "--open must be x,y or x,y,w,h";
                return false;
            }
            token.clear();
            continue;
        }
        token.push_back(c);
    }
    if (!push(token)) {
        error = "--open must be x,y or x,y,w,h";
        return false;
    }
    if (numbers.size() != 2 && numbers.size() != 4) {
        error = "--open must be x,y or x,y,w,h";
        return false;
    }
    open.X = numbers[0];
    open.Y = numbers[1];
    if (numbers.size() == 4) {
        open.Width = numbers[2];
        open.Height = numbers[3];
    }
    return true;
}

int Finish(const Ascension::EditResult& result)
{
    if (result.Ok) {
        std::cout << result.Message << '\n';
        return 0;
    }
    std::cerr << result.Message << '\n';
    return 1;
}

int Usage()
{
    std::cerr << kUsage;
    return 2;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
        return Usage();
    const std::string_view command = argv[1];
    if (command == "--help" || command == "-h" || command == "help")
        return Usage();

    Cli cli(argc, argv);
    std::string data;
    if (!cli.Take("--data", data, true)) {
        std::cerr << cli.Error << '\n';
        return 2;
    }

    Ascension::EditResult result;
    if (command == "check") {
        if (cli.Leftovers()) {
            std::cerr << cli.Error << '\n';
            return 2;
        }
        result = Ascension::CheckData(data);
    } else if (command == "new") {
        Ascension::NewMap spec;
        std::string kind;
        if (!cli.Take("--id", spec.Id, true) || !cli.Take("--kind", kind, true) ||
            !cli.TakeInt("--width", spec.Width, true) || !cli.TakeInt("--height", spec.Height, true) ||
            cli.Leftovers()) {
            std::cerr << (cli.Error.empty() ? "missing argument" : cli.Error) << '\n';
            return 2;
        }
        if (kind == "overland")
            spec.Kind = Ascension::MapKind::Overland;
        else if (kind == "dungeon")
            spec.Kind = Ascension::MapKind::Dungeon;
        else if (kind == "town")
            spec.Kind = Ascension::MapKind::Town;
        else {
            std::cerr << "--kind must be overland, dungeon, or town\n";
            return 2;
        }
        result = Ascension::CreateMap(data, spec);
    } else if (command == "paint") {
        Ascension::PaintRect rect;
        rect.Width = 1;
        rect.Height = 1;
        if (!cli.Take("--map", rect.MapId, true) || !cli.Take("--tile", rect.TileId, true) ||
            !cli.TakeInt("--x", rect.X, true) || !cli.TakeInt("--y", rect.Y, true) ||
            !cli.TakeInt("--w", rect.Width, false) || !cli.TakeInt("--h", rect.Height, false) ||
            cli.Leftovers()) {
            std::cerr << (cli.Error.empty() ? "missing argument" : cli.Error) << '\n';
            return 2;
        }
        result = Ascension::Paint(data, rect);
    } else if (command == "start") {
        Ascension::MapCell cell;
        if (!cli.Take("--map", cell.MapId, true) || !cli.TakeInt("--x", cell.X, true) ||
            !cli.TakeInt("--y", cell.Y, true) || cli.Leftovers()) {
            std::cerr << (cli.Error.empty() ? "missing argument" : cli.Error) << '\n';
            return 2;
        }
        result = Ascension::SetStart(data, cell);
    } else if (command == "link") {
        Ascension::MapLinkEdit link;
        if (!cli.Take("--map", link.MapId, true) || !cli.TakeInt("--x", link.X, true) ||
            !cli.TakeInt("--y", link.Y, true) || !cli.Take("--to", link.To, true) ||
            !cli.TakeInt("--ax", link.ArrivalX, true) || !cli.TakeInt("--ay", link.ArrivalY, true) ||
            cli.Leftovers()) {
            std::cerr << (cli.Error.empty() ? "missing argument" : cli.Error) << '\n';
            return 2;
        }
        result = Ascension::AddLink(data, link);
    } else if (command == "unlink") {
        Ascension::MapCell cell;
        if (!cli.Take("--map", cell.MapId, true) || !cli.TakeInt("--x", cell.X, true) ||
            !cli.TakeInt("--y", cell.Y, true) || cli.Leftovers()) {
            std::cerr << (cli.Error.empty() ? "missing argument" : cli.Error) << '\n';
            return 2;
        }
        result = Ascension::RemoveLink(data, cell);
    } else if (command == "spawn") {
        Ascension::MapSpawnEdit spawn;
        std::string once;
        bool sawRadius = false;
        for (int index = 2; index < argc; ++index) {
            if (std::strcmp(argv[index], "--aura-radius") == 0)
                sawRadius = true;
        }
        spawn.NoFlee = cli.Switch("--no-flee");
        if (!cli.Take("--map", spawn.MapId, true) || !cli.Take("--id", spawn.Id, true) ||
            !cli.TakeInt("--x", spawn.X, true) || !cli.TakeInt("--y", spawn.Y, true) ||
            !cli.Take("--encounter", spawn.Encounter, true) || !cli.Take("--once", once, false) ||
            !cli.Take("--mover", spawn.Mover, false) ||
            !cli.Take("--aura-status", spawn.AuraStatus, false) ||
            !cli.TakeInt("--aura-radius", spawn.AuraRadius, false) || cli.Leftovers()) {
            std::cerr << (cli.Error.empty() ? "missing argument" : cli.Error) << '\n';
            return 2;
        }
        if (!once.empty()) {
            if (once == "true")
                spawn.Once = true;
            else if (once == "false")
                spawn.Once = false;
            else {
                std::cerr << "--once must be true or false\n";
                return 2;
            }
        }
        const bool status = !spawn.AuraStatus.empty();
        if (status != sawRadius) {
            std::cerr << "--aura-status and --aura-radius are set together\n";
            return 2;
        }
        spawn.HasAura = status;
        result = Ascension::AddSpawn(data, spawn);
    } else if (command == "unspawn") {
        std::string mapId;
        std::string spawnId;
        if (!cli.Take("--map", mapId, true) || !cli.Take("--id", spawnId, true) || cli.Leftovers()) {
            std::cerr << (cli.Error.empty() ? "missing argument" : cli.Error) << '\n';
            return 2;
        }
        result = Ascension::RemoveSpawn(data, mapId, spawnId);
    } else if (command == "point") {
        Ascension::MapPointEdit point;
        std::string kind;
        if (!cli.Take("--map", point.MapId, true) || !cli.Take("--kind", kind, true) ||
            !cli.TakeInt("--x", point.X, true) || !cli.TakeInt("--y", point.Y, true) ||
            !cli.Take("--dialogue", point.Dialogue, false) || cli.Leftovers()) {
            std::cerr << (cli.Error.empty() ? "missing argument" : cli.Error) << '\n';
            return 2;
        }
        if (kind == "rest")
            point.Kind = Ascension::EditPointKind::Rest;
        else if (kind == "shop")
            point.Kind = Ascension::EditPointKind::Shop;
        else if (kind == "talk")
            point.Kind = Ascension::EditPointKind::Talk;
        else {
            std::cerr << "--kind must be rest, shop, or talk\n";
            return 2;
        }
        if (point.Kind == Ascension::EditPointKind::Talk && point.Dialogue.empty()) {
            std::cerr << "--dialogue is required for a talk\n";
            return 2;
        }
        result = Ascension::AddPoint(data, point);
    } else if (command == "unpoint") {
        Ascension::MapCell cell;
        if (!cli.Take("--map", cell.MapId, true) || !cli.TakeInt("--x", cell.X, true) ||
            !cli.TakeInt("--y", cell.Y, true) || cli.Leftovers()) {
            std::cerr << (cli.Error.empty() ? "missing argument" : cli.Error) << '\n';
            return 2;
        }
        result = Ascension::RemovePoint(data, cell);
    } else if (command == "prop") {
        Ascension::MapPropEdit prop;
        std::vector<std::string> holes;
        bool sawWidth = false;
        bool sawHeight = false;
        for (int index = 2; index < argc; ++index) {
            if (std::strcmp(argv[index], "--w") == 0)
                sawWidth = true;
            if (std::strcmp(argv[index], "--h") == 0)
                sawHeight = true;
        }
        if (!cli.Take("--map", prop.MapId, true) || !cli.Take("--id", prop.Id, true) ||
            !cli.TakeInt("--x", prop.X, true) || !cli.TakeInt("--y", prop.Y, true) ||
            !cli.TakeInt("--w", prop.Width, false) || !cli.TakeInt("--h", prop.Height, false) ||
            !cli.TakeAll("--open", holes) || cli.Leftovers()) {
            std::cerr << (cli.Error.empty() ? "missing argument" : cli.Error) << '\n';
            return 2;
        }
        if (sawWidth != sawHeight) {
            std::cerr << "--w and --h are set together\n";
            return 2;
        }
        prop.Sized = sawWidth;
        for (const std::string& hole : holes) {
            Ascension::MapOpenEdit open;
            if (!ParseOpen(hole, open, cli.Error)) {
                std::cerr << cli.Error << '\n';
                return 2;
            }
            prop.Open.push_back(open);
        }
        result = Ascension::AddProp(data, prop);
    } else if (command == "unprop") {
        std::string mapId;
        std::string propId;
        if (!cli.Take("--map", mapId, true) || !cli.Take("--id", propId, true) || cli.Leftovers()) {
            std::cerr << (cli.Error.empty() ? "missing argument" : cli.Error) << '\n';
            return 2;
        }
        result = Ascension::RemoveProp(data, mapId, propId);
    } else if (command == "hub") {
        std::string mapId;
        if (!cli.Take("--map", mapId, true)) {
            std::cerr << cli.Error << '\n';
            return 2;
        }
        const bool off = cli.Switch("--off");
        if (cli.Leftovers()) {
            std::cerr << cli.Error << '\n';
            return 2;
        }
        result = Ascension::SetHub(data, mapId, !off);
    } else {
        return Usage();
    }
    return Finish(result);
}
