# Ascension

A LitRPG vertical slice on the [Emerald](https://github.com/Ridejock/Emerald) engine (C++20, SDL3, SDL GPU): turn-based combat and a class change at level 5. This tree is the project skeleton and the JSON data layer. The window draws the loaded Fighter with rectangles and a placeholder font. Combat is not in yet.

Emerald is fetched by CMake and pinned to commit `dc90b7ddae80c3fb6b8630cb47cd883a8a2e3bba` (see `CMakeLists.txt`). To use a local checkout instead:

```sh
cmake --preset debug -DASCENSION_EMERALD_SOURCE_DIR=C:/dev/Emerald
```

## Build

Requirements: CMake 3.24+, Ninja, a C++20 compiler, Git. The presets match [Ridejock/Asteroids](https://github.com/Ridejock/Asteroids): Ninja, `build/<preset>/`, `debug`, `release`, and `debug-full` (ImGui). They work with MSVC on Windows and with GCC or Clang on Linux. The first configure builds [SDL_shadercross](https://github.com/libsdl-org/SDL_shadercross) (the only shader compiler; the build does not call fxc) and caches it for later builds.

### Windows (MSVC)

From a **Developer PowerShell for VS 2022** (x64), so `cl` and `ninja` are on `PATH`:

```powershell
cmake --preset debug
cmake --build --preset debug
.\build\debug\bin\Ascension.exe
```

### Linux (GCC)

SDL's development packages are the same set Emerald documents in `docs/building.md`. On Debian or Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build git pkg-config \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
    libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols \
    libegl1-mesa-dev libgl1-mesa-dev libdrm-dev libgbm-dev \
    libasound2-dev libpulse-dev libudev-dev libdbus-1-dev

cmake --preset debug -DEMERALD_SHADER_FORMATS=SPIRV
cmake --build --preset debug
./build/debug/bin/Ascension
```

`EMERALD_SHADER_FORMATS=SPIRV` matches Linux CI. DXIL (Direct3D 12) is built on Windows. GCC 13's shader compiler crashes on DXIL, so leave that format to the Windows build.

### Linux (Clang)

Same packages, plus `clang`. The preset does not hard-code a compiler; point CMake at Clang for this configure:

```sh
CC=clang CXX=clang++ cmake --preset debug -DEMERALD_SHADER_FORMATS=SPIRV
cmake --build --preset debug
./build/debug/bin/Ascension
```

Release is the same preset name used for the zip (`cmake --preset release`, then `cmake --build --preset release`). Escape closes the window. `--frames N` quits after N frames (an Emerald option).

## Tests

```sh
ctest --test-dir build/debug --output-on-failure
```

`AscensionDataTests` loads `data/`, checks the Fighter tree, and rejects broken files. It does not open a window.

## Data

`data/` is copied next to the executable on every build. A dev build watches the source `data/` (so saving a file in the tree updates the running game) and falls back to the copy beside the executable when that directory is not there, which is what a packaged build does. The game does not care about the working directory.

| File | What it defines |
|---|---|
| `stats.json` | The five combat stats: vitality, strength, defense, agility, focus |
| `skills.json` | Active skills and passives |
| `classes.json` | Fighter, and the level-5 evolutions Berserker and Guardian |
| `enemies.json` | Enemies (level, stats, skills, experience, gold) |
| `items.json` | Gear (weapon, armor, accessory) and the stats it adds |

Fighter is the base class. Berserker and Guardian each set `evolvesFrom` to `fighter` and `evolveLevel` to 5, and each **adds two active skills and one passive**. An evolution's stats are the parent's stats at that level, plus `bonus`, and then its own `growth` for later levels.

The public headers (`include/Ascension/Data/`) are plain structs. JSON parsing and the error text live in `src/Data/Database.cpp`. A bad file names itself and the field, for example `classes.json: class 'berserker': skill 'smash' does not exist`. Unknown keys are errors, so a typo does not silently do nothing.

While the game is running it checks the five files a few times a second. A save is applied once the new modification time has been seen twice, so a half-written file is left alone. If the new text does not load, the previous definitions stay on screen and the error is shown. The revision in the corner goes up on every successful reload. Edit `vitality` under Fighter in `data/classes.json`, save, and the bar updates.

## Package

```sh
cmake --build --preset release --target package
```

writes `build/release/Ascension-<version>-<platform>.zip` (the executable, shaders, `data/`, licenses, and `.itch.toml`). CI calls Emerald's reusable workflow `Ridejock/Emerald/.github/workflows/game.yml@main` for Windows (MSVC) and Linux (GCC). The itch.io user and game slug are left empty, so a tag does not publish.
