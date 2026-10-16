# Ascension

A LitRPG vertical slice on the [Emerald](https://github.com/Ridejock/Emerald) engine (C++20, SDL3, SDL GPU): turn-based combat and a class change at level 5. This tree is the project skeleton, the JSON data layer, and a combat sandbox: one level-1 Fighter against one enemy. The window draws that fight with rectangles and Emerald's built-in debug font.

Emerald is fetched by CMake and pinned to commit `f45d5a65ab03fe533f2d0f37282602991da67165` (see `CMakeLists.txt`). To use a local checkout instead:

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

`AscensionDataTests` loads `data/`, checks the Fighter tree, and rejects broken files. `AscensionCombatTests` plays a Fighter against the sample enemies and checks the damage formula. Neither test opens a window.

## Data

`data/` is copied next to the executable on every build. A dev build watches the source `data/` (so saving a file in the tree updates the running game) and falls back to the copy beside the executable when that directory is not there, which is what a packaged build does. The game does not care about the working directory.

| File | What it defines |
|---|---|
| `stats.json` | The five combat stats: vitality, strength, defense, agility, focus |
| `skills.json` | Active skills and passives |
| `classes.json` | Fighter, and the level-5 evolutions Berserker and Guardian |
| `enemies.json` | Enemies (level, stats, skills, experience, gold) |
| `items.json` | Gear (weapon, armor, accessory) and the stats it adds |
| `combat.json` | Health per vitality, minimum damage, and the attack and defense divisors |

Fighter is the base class. At level 1 the sandbox gives them four skills: Strike, Brace, Heavy Blow, and Second Wind. Berserker and Guardian each set `evolvesFrom` to `fighter` and `evolveLevel` to 5, and each **adds two active skills and one passive**. An evolution's stats are the parent's stats at that level, plus `bonus`, and then its own `growth` for later levels. Class change is not in the fight yet.

A hit is `power + scaling stat`, minus defense, and then at least `minDamage`. Those divisors are 1 in the sample, so the stat is added and subtracted whole. Brace's `power` is the percent of the next hit that is ignored. Max health is vitality times `hpPerVitality`. Focus is the focus stat, and it does not come back until the fight restarts. Higher agility acts first each round; a tie lets the Fighter go first. The order comes from a turn model (`AgilityRoundModel`), so a later active-time model can replace it without rewriting damage.

The public headers (`include/Ascension/Data/` and `include/Ascension/Combat/`) are plain structs and the battle. JSON parsing and the error text live in `src/Data/Database.cpp`. A bad file names itself and the field, for example `classes.json: class 'berserker': skill 'smash' does not exist`. Unknown keys are errors, so a typo does not silently do nothing.

While the game is running it checks the data files a few times a second. A save is applied once the new modification time has been seen twice, so a half-written file is left alone. If the new text does not load, the previous definitions stay and the error is shown. The revision in the corner goes up on every successful reload. The fight keeps its wounds and whose turn it is. Edit Strike's `power` in `data/skills.json`, or `hpPerVitality` in `data/combat.json`, save, and the next blow uses the new numbers.

## Combat

Run the game. The Wolf is faster, so it bites first. Then:

| Key | Action |
|---|---|
| `1` `2` `3` `4` | Strike, Brace, Heavy Blow, Second Wind |
| Space | Wait, and let the enemy act |
| `R` | Restart this fight |
| `N` | The next enemy in `enemies.json` (Wolf, Bandit, Cairn Guard) |
| Escape | Quit |

Waiting against the Wolf loses. Strike three times and it dies. `N` restarts on the next foe.

## Package

```sh
cmake --build --preset release --target package
```

writes `build/release/Ascension-<version>-<platform>.zip` (the executable, shaders, `data/`, licenses, and `.itch.toml`). CI calls Emerald's reusable workflow `Ridejock/Emerald/.github/workflows/game.yml@main` for Windows (MSVC) and Linux (GCC). The itch.io user and game slug are left empty, so a tag does not publish.
