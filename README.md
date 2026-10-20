# Ascension

A LitRPG vertical slice on the [Emerald](https://github.com/Ridejock/Emerald) engine (C++20, SDL3, SDL GPU): turn-based combat and a class change at level 5. This tree is the project skeleton, the JSON data layer, and a combat sandbox. A Fighter earns XP, ranks skills by using them, and at level 5 chooses Berserker or Guardian. The window draws that fight with rectangles and Emerald's built-in debug font.

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

Release is the same preset name used for the zip (`cmake --preset release`, then `cmake --build --preset release`). Escape closes the window. `--frames N` quits after N frames (an Emerald option). The playtest flags are in [Playtest](#playtest).

## Tests

```sh
ctest --test-dir build/debug --output-on-failure
```

`AscensionDataTests` loads `tests/data/`, a frozen copy of the data the checks were written against, checks the Fighter tree, and rejects broken files. `AscensionCombatTests` plays a Fighter against those sample enemies and checks the damage formula. `AscensionProgressTests` checks the XP curve, skill ranks, and the level-5 choice. `AscensionInventoryTests` checks the bag, worn gear, and the shop. `AscensionSaveTests` checks a save round trip, a migration from the previous data version, and a truncated file falling back to the previous copy. `AscensionPlaytestTests` checks the playtest flags, unknown ids, and a `--data` folder that hot-reloads. None of them open a window. One data check loads the shipped `data/` and asserts that every description expands. It does not lock the wording, so a retune of a number does not move it. The other checks stay on `tests/data/`.

## Data

`data/` is copied next to the executable on every build. A dev build watches the source `data/` (so saving a file in the tree updates the running game) and falls back to the copy beside the executable when that directory is not there, which is what a packaged build does. The game does not care about the working directory. Unit tests load `tests/data/` instead, so the numbers in this folder can change without rewriting the checks.

| File | What it defines |
|---|---|
| `stats.json` | The five combat stats: vitality, strength, defense, agility, focus |
| `effects.json` | Reusable effect recipes. A skill can copy one by id and then tune it |
| `skills.json` | Active skills and passives. Each skill holds a list of effects |
| `statuses.json` | Bleed, Exposed, Enraged, Stunned, and Steadfast |
| `classes.json` | Fighter, and the level-5 evolutions Berserker and Guardian |
| `enemies.json` | The roster (level, stats, skills, experience, gold) and which one is the boss |
| `encounters.json` | Each fight: a name, an optional zone, and a list of one to three enemy ids. The same id may repeat |
| `items.json` | Gear (weapon, offhand, armor, accessory) and consumables. Each names an `icon` and a `rarity`. An item may set `requires.defeated` to foe ids the shop waits for |
| `rarities.json` | Rarity id, display name, and `#RRGGBB` color |
| `shop.json` | Old Pell's Provisions: the bag, stack size, sell rate, and Pell's stock |
| `combat.json` | Health per vitality, minimum damage, the attack and defense divisors, and focus regen |
| `progress.json` | Total XP to reach each level, uses per skill rank, and power added per rank |

Fighter is the base class, and `"starter": true` marks it as a class a build reset can return to. Another starter is that flag on another base class. The list is read from `classes.json`. Berserker and Guardian each set `evolvesFrom` to `fighter` and `evolveLevel` to 5. Berserker **adds two active skills and one passive**. Guardian **adds one active skill and one passive**. A class may also list `picks`: each entry is a level and two or three options, written in the same grant form as `skills`. The choice opens when the hero reaches that level, after the evolve level, and the next fight waits until one option is kept. An option the class already grants has to carry a `tune`; it merges into that skill (best value per field, one shared rank) instead of adding a second copy. Kept picks are part of the build, so Reset build clears them. A base class's `stats` are its attribute grant. An evolution's `bonus` is the attribute grant it adds. `growth` is the attribute points earned by leveling. An evolution's stats are the parent's stats at that level, plus `bonus`, and then its own `growth` for later levels. The choice opens when the hero's level reaches an evolution's `evolveLevel` (5 in the sample), and it stays open until a path is picked, including if the Fighter is already past that level. The next fight will not start until then. The number keys follow the order of those evolutions in `classes.json`, and the next number is Reset build. That is the only place skill ranks are cleared. Ranks are the practiced uses stored on the hero by skill id. A reload does not clear them.

`progress.json` lists the total XP required to be at each level. The first entry is 0. In the sample, 8 XP is level 2, 20 is level 3, 40 is level 4, and 60 is level 5. A win grants the sum of every enemy's `experience` and `gold`, including an add that joined during the fight. Gold is spent at Old Pell's Provisions between fights. The hub itself is a later slice: `I` opens Pell's screen after a victory or a defeat. In encounter order that is Wolf 8, Bandit 18, Tomb Moth 14 (level 4, 40 XP), and Bone Archer 22 (level 5, 62 XP), which opens the choice. Leveling does not heal the fight that just ended. The next fight uses the new level and, after a choice, the new class.

A skill starts at rank 1. `usesPerRank` is either one integer (a flat step, `rank = 1 + uses / step`) or a list of the total uses required to reach rank 2, rank 3, and so on. The sample is `[3, 6, 10, 15]`, so the third use is rank 2 and the fifteenth is rank 5, capped by `maxSkillRank`. Each rank above 1 adds `powerPerRank` (2 in the sample) to that skill's damage power, heal power, and a reflect's flat power, including the blow that crosses the rank. A guard's percent is unchanged. The number in parentheses on the skill line is the rank. Ranks live on the hero, keyed by skill id. Evolving keeps the skills the hero already practiced, and those keep their ranks. Reset build, offered only on the evolution choice, is the only thing that clears them. It also returns every class attribute grant to one unspent pool and asks for a starter class. Level, XP, and the points earned by leveling stay, so the total number of attribute points does not change. The player spends the pool, then picks a path. Enemy skills stay at rank 1.

An effect is a `trigger` plus an `op`. Triggers are `always`, `onUse`, `onHit`, `onBlock`, `onTurnStart`, and `onDamaged`. Ops are generic: `stat`, `statPerMissingHp`, `grantFocus`, `damage`, `heal`, `guard`, `reflect`, `empower`, `harden`, `applyStatus`, `stunIfWindingUp`. A skill lists its effects, and each effect has an id. Three layers build the numbers a fight uses. The effect recipe comes first (inline on the skill, or a copy of an entry in `effects.json`). The skill's `tune` replaces fields on those effects. A class, enemy, or item grant can tune the copy again. A later layer replaces only the fields it sets. A tune that names a missing effect, or a field that effect does not have, fails the load and names the file and the field. A field the trigger and op do not read fails the same way, even when another use of that op reads it. Each grant is resolved to its own copy, so two classes can grant Bulwark with different numbers. Battle runs the triggers. It does not look up a skill id.

An effect may set `target` to `self`, `target`, `allEnemies`, or `randomEnemy`. That is who the effect hits, and it can be narrower or wider than the skill's own `target`. `target` is the marked enemy when the player acts, and the player when an enemy acts. `allEnemies` is every living enemy other than the user. `randomEnemy` is one living enemy, walking the pack in order. A field the op does not read, including `target` on a guard or a heal, fails the load. Leave `target` off and the effect uses the skill's target.

A damage effect may set `ifTargetHas` to a status id. The blow lands on an aim only while that aim holds the status, and the check happens when that effect resolves, before `onHit`. `consume` is allowed only together with `ifTargetHas`. The status stays up for the rest of that use, so every damage effect in the use still sees it, that use does not apply the same status to that aim again, and the status is removed after the last `onHit` effect. The log names the foe and the status (`Oathkeeper Varn loses Exposed.`). A miss is not a hit: `onHit` and `consume` reach only aims a damage effect actually struck. Two damage effects that hit one foe in one use are one blow line. Rank bonus is added once per damage effect, and a damage-dealt percent such as Fury multiplies every damage effect that lands.

Bulwark is that shape in the sample:

```json
{ "id": "bulwark", "name": "Bulwark", "kind": "passive", "effects": [
  { "id": "def", "trigger": "always", "op": "stat", "stat": "defense", "amount": 3 },
  { "id": "focus", "trigger": "onBlock", "op": "grantFocus", "amount": 1 } ] }
```

Bloodlust is `always` / `statPerMissingHp`: 1 strength for every 10 percent of health missing, up to 6. It does not change the strength shown as a stat; it is added when a blow scales with strength. Bulwark's `stat` adds 3 defense, and its `grantFocus` pays 1 focus when a guard actually reduces a hit. Defense by itself does not. Riposte costs 2 focus. Its `guard` stops 70 percent of the next hit, and its `reflect` returns 50 percent of the damage that guard stopped plus `power + defense`. What returns meets the attacker's defense the way a hit does. Shield Bash costs 3 focus. It strikes with defense, leaves the foe Exposed, and a foe who is already Exposed takes a second blow and loses Exposed. Rend costs 2 focus: a cut, then a deeper cut that needs Bleed and leaves it, and Bleed is applied twice on the hit. Rally costs 3 focus and sets a guard while knitting a small wound. A class that wants a different Bulwark grants it with a tune, for example `{ "skill": "bulwark", "tune": { "focus": { "amount": 2 } } }`. The string `"bulwark"` uses the recipe as written. Items may list the same `grants` array; the loader resolves those copies, and a fight does not apply them yet.

A hit is `power + scaling stat`, minus defense, and then at least `minDamage`. The attack divisors are 1 in the sample, so the scaling stat is added whole. Defense is `defense` × `defenseNumerator` / `defenseDenominator` (1/2), so it counts half, rounded down. Status percents apply after that formula: damage dealt, then the guard, then damage taken. Brace's guard `amount` is the percent of the next hit that is ignored. Max health is vitality times `hpPerVitality`. The hero's focus pool is `focusBase` (4) plus their focus stat. It rises by `focusPerTurn` (1) at the start of their turn, and waiting adds `focusOnWait` (2) on top of that. Enemies do not regenerate focus. Higher agility acts first each round; a tie lets the Fighter go first. The order comes from a turn model (`AgilityRoundModel`), so a later active-time model can replace it without rewriting damage.

`statuses.json` defines Bleed (3 health at the start of each of the afflicted unit's turns, for 3 turns, stacking to 2), Exposed (+25 percent damage taken, 2 turns), Enraged (+30 percent dealt and +15 percent taken; the sample keeps it up for a fight), Stunned (skips the next action), and Steadfast (cannot be stunned or Exposed for a short while). The Bandit's Slash bleeds on every third use. Heavy Blow stuns a foe who is winding up and cancels that wind-up, unless a status they hold resists stunned. The foe's panel shows the telegraph (`AIMED SHOT 1/2`, then `INCOMING`), a raised guard (`GUARD 75%`), a defense buff with the turns still left (`DEFENSE +150 (2)`, the word taken from Defense in `stats.json`), and any statuses.

`items.json` is the gear and the drinks. Each item has an `icon` id from `assets/icons/icons_sheet.json` and a `rarity` id from `rarities.json`. The sheet is the untinted steel-grey picture. `icons_accent_sheet.png` holds the same rectangles, accent pixels only, and the shop multiplies those by the rarity color and draws them over the base. The item name uses that color. Source maps and the generator are in `art/icons/`, and the master palette is `art/palette.gpl`. From any directory, `python3 art/icons/make_icons.py` reads that palette and `icons.txt` (both relative to the script) and rewrites `assets/icons/`. Adding an icon is a new map in `icons.txt`, then that command. A later mythic rarity can be unique (at most one of that item, its own icon and fixed stats) without a new drawing path: a rarity row can gain a flag, and the item already names its icon. A weapon may set `twoHanded`, which fills the offhand as well. Oak Shield is an offhand. Consumables are not worn: `heal` is flat health and `focus` is flat focus, spent as a turn in a fight. The sample is Rusty Sword (strength +1, 12 gold), Notched Greataxe (strength +6, defense -2, two-handed, 60 gold), Oak Shield (defense +2, 15 gold), Oathkeeper's Plate (defense +6, agility -1, 55 gold), Wolfhide Charm (agility +2, 35 gold), Minor Potion (heal 30, 10 gold), and Focus Tonic (focus +4, 15 gold). The sword, the axe, and the plate prices are the ones chosen for this slice. Notched Greataxe and Oathkeeper's Plate set `requires` to `{ "defeated": ["oathkeeper_varn"] }`, so Old Pell keeps those rows until that foe has fallen. The row still shows the name and the price. Selling pays half the buy price, rounded down (`sellNumerator` 1, `sellDenominator` 2).

`shop.json` is Pell Harrow's shop, Old Pell's Provisions. `bagSlots` is 12 and `stackSize` is 5, so a sixth potion of the same kind opens another slot and sixty potions fill the bag. Worn gear is added to the class stats before passives. A two-handed weapon sends the offhand piece back to the bag, and a shield cannot be worn until that weapon comes off. If a reload makes the worn weapon two-handed while a shield is still in the offhand, the shield's defense stops immediately and the shield is moved into the bag. A full bag sets the last stack down to make that slot. The offhand can always be taken off. The fight starts with an empty bag and no gold. Number keys stop at 8, so `C` opens every consumable stack during the player's turn. Drinking still spends the turn. Keys are fixed in the app; the data has no binding table.

A description may contain `{placeholders}` filled from that entity's own fields. There is no arithmetic and no conditional. A brace that is not a real field fails the load and names the file, the entity, and the placeholder, for example `skills.json: skill 'bulwark' description: unknown placeholder {def.amout}`. The same expander runs when the screen draws the text, so a rank or an item tune shows the numbers on that copy. Damage and reflect `{power}` add `(rank - 1) * powerPerRank`. A new hero is the first class with `"starter": true`.

| Entity | Placeholders |
|---|---|
| Skill | `{name}` `{cost}` `{windup}` (only when windup is set). Per effect: `{id.power}` `{id.amount}` `{id.percent}` `{id.per}` `{id.max}` `{id.turns}` `{id.every}`. `{id.stat}` and `{id.scaling}` are the stat's name. `{id.status}` is the status's name, and `{id.status.dealt}` (also `taken`, `perTurn`, `turns`, `stacks`, `name`) reads that status. `{id.ifTargetHas}` is the name of the status that effect requires. |
| Status | `{name}` `{turns}` `{stacks}` `{perTurn}` `{dealt}` `{taken}`, and only when that number is set |
| Class | `{name}` `{evolveLevel}` `{from}` (the parent class's name). A base class uses `{paths}` (the children's names joined with " or a ") and `{pathLevel}` (their evolve level, which has to be the same) |
| Enemy | `{name}` `{phaseAt}` `{summon}` (the summoned enemy's name) `{summonEvery}` |
| Item | `{name}` `{value}` `{heal}` `{focus}` |

Shop, bag, and worn rows share one facts suffix: heal, focus, each bonus by its name in `stats.json`, "two handed", and `grants` plus the skill name. A worn row keeps the slot and the item name and skips the price, the heal, and "two handed". The camp title is `shop.name`. The buy refusal uses the keeper's full name. The help key and the between-fights note use the keeper's first word.

The public headers (`include/Ascension/Data/`, `include/Ascension/Combat/`, and `include/Ascension/Progress/`) are plain structs, the battle, the hero's career, and the bag. JSON parsing and the error text live in `src/Data/Database.cpp`. A bad file names itself and the field, for example `classes.json: class 'berserker': skill 'smash' does not exist`. Unknown keys are errors, so a typo does not silently do nothing.

While the game is running it checks the data files a few times a second. A save is applied once the new modification time has been seen twice, so a half-written file is left alone. If the new text does not load, the previous definitions stay and the error is shown, naming the file and the field. The revision in the corner goes up on every successful reload. The fight keeps its wounds and whose turn it is. XP and skill uses are kept too: editing the curve or `usesPerRank` updates the level and the ranks on the next successful reload, and class growth applies on the next fight. Edit Strike's `power` in `data/skills.json`, Bulwark's defense `amount`, or `hpPerVitality` in `data/combat.json`, save, and the next blow uses the new numbers.

## Save

`F5` writes the run. Escape writes it and quits. The next launch loads that file, unless the command builds a hero of its own (`--level` and the other setup flags). The file is Emerald's text save, data version 2, one slot named `run` under the per-user Ascension folder (`~/.local/share/Ridejock/Ascension/saves/run.sav` on Linux). A write goes to a temporary file and is renamed into place. A truncated file falls back to the previous copy. The save holds the class, XP, stats, skill uses, kept picks (a tuned upgrade included), gold, bag stacks, worn gear, current HP and focus, the defeated foe ids, and the place in the encounter list. A victory resumes between fights, so the reward is not granted again. An ongoing fight resumes on that encounter with the saved HP and focus. `playtest/outer-barrow/save-commands.txt` is a save, quit, and load run.

## Combat

Run the game. The Wolf is faster, so it bites first. Then:

| Key | Action |
|---|---|
| `1` `2` `3` `4` | Strike, Brace, Heavy Blow, Second Wind |
| `5` `6` | After evolving: Berserker Rage and Cleave, or Guardian Shield Bash and the next free drink. Before that, the first consumables in the bag |
| The next free number, through `8` | A kept pick, then a drink. A drink spends the turn. A stun keeps the bottle. Eight keys is the cap: a bar that is already full leaves drinks on `C` |
| `C` | During your turn, open or close the drink list. Up and Down move. `E` drinks the marked row. `1`–`8` drink that row. Any drink spends the turn |
| Space | Wait. Also gains 2 focus on top of the 1 gained at the start of the turn |
| `I` | Between fights, open or close Old Pell's Provisions. During a fight, Pell is not there |
| Up Down | In the shop, move through the bag or the stock |
| Left Right | During your turn, when the fight has more than one enemy, change the marked target. Dead enemies are skipped. In the shop, switch between the bag and the stock |
| `E` | In the shop, equip the highlighted bag item, take off the highlighted worn piece, or buy the highlighted stock |
| `X` | In the shop, sell one of the highlighted stack |
| `U` | In the shop, take off the highlighted worn piece |
| `R` | Fight this encounter again. Level, XP, gold, gear, and skill ranks stay. Refused while a path is still open |
| `N` | The next encounter in `encounters.json`. While a path is open the fight does not start and the encounter stays put |
| `F5` | Save the run |
| Escape | Save the run, then quit |

The encounters, in order, are Wolf, Bandit, Tomb Moth, Bone Archer, Wolf Pack, Barrow Sentry, the boss Oathkeeper Varn, then the Cairn Warden and the Barrow Brute. The corner of the name shows the place in that list. When an encounter sets `zone`, that text is drawn above the foe name. The Cairn Warden and the Barrow Brute set it to Outer Barrow. A fight with no zone leaves the line off, and the log does not include it. A single enemy fills the right panel. A pack draws one column per enemy (a rectangle, the name, health, a wind-up, a guard or defense buff, and a status). The marked enemy is framed and labeled TARGET. The enemy whose turn it is is labeled ACTS, and the banner uses that name. Varn is marked BOSS. The Wolf Pack is three Dire Wolves. The fight ends when every enemy is dead, and the reward is 8 XP and 3 gold from each (24 XP and 9 gold).

After a win that reaches level 5 the skill list is replaced by the choice, and the next fight waits. `1` is Berserker. `2` is Guardian. `3` is Reset build. A later win that reaches a skill pick replaces the list the same way: `1` and `2` (and `3`, when a pick offers three) are the options, in file order. An upgrade reads `UPGRADES` and the skill's name. That screen has no reset. Guardian's level-6 pick is Riposte or Shield Wall, and level 8 is Rally or Rage. Berserker's level-6 pick is Rend or Brace, and level 8 is Rally or Second Wind. Reset clears skill ranks, puts the class attribute grants back in a pool, and lists every starter class. `1` is Fighter in the sample. After that, `1` through `5` spend the pool on vitality, strength, defense, agility, and focus, and `6` resets again. The path keys return once the pool is empty. Reset is not offered anywhere else. The new skills show once a path is picked. `R` then fights the same encounter again, and `N` is what moves on. Pressing `N` before the choice does not skip ahead. Wolf, Bandit, Tomb Moth, and Bone Archer are 8 + 18 + 14 + 22 XP, which is level 5. The next fight is the Wolf Pack (24 XP, still level 5). The Barrow Sentry after that (40 XP) reaches level 6. Varn is the level-6 boss. The Cairn Warden (30 XP and 14 gold) and the Barrow Brute (40 XP and 20 gold) are the next two fights, both met at level 8.

Varn's pattern is Maul Swing, then Crushing Slam (telegraphed, then it lands), then Stone Skin. Phase 2 starts at 60 percent health: Varn becomes Enraged. Every second action in that phase the panel names the Rubble Shard, and Varn's next action calls it into the fight. A second shard does not join while one is still up: that turn Varn takes his ordinary action instead. The fight holds at most three enemies. The shard has to die too. A corpse does not keep its seat: once it falls, Varn can call another, and a second copy is numbered the same way a wolf pack is (Rubble Shard, then Rubble Shard 2). Cleave hits every living enemy. The line under the skill says who each damage, status, and stun effect hits, using that effect's `target`. Change Cleave's effect `target` to `target` and the line says the marked enemy, which is also who the swing hits. Heavy Blow (`3`) stuns a wind-up and cancels it, which is the Berserker's answer. Riposte or Shield Wall, once that pick is kept, is the Guardian's answer to a telegraph. Shield Bash is the Guardian's class attack. The Bone Archer's Aimed Shot is the same kind of telegraph, two turns long, so it can be practiced before the boss. Its pattern is Aimed Shot, then Bone Bolt: cancelling the draw still lets the bolt fly on the next action. Edit `data/encounters.json`, or an effect's `target`, save, and the next action uses the new fight.

Waiting against the Wolf loses. Strike three times and it dies (8 XP, level 2). `N` restarts on the next foe.

## Playtest

A scripted run sets the hero and the fight from the command line, so a playtest does not have to grind or edit `data/`. Emerald still owns `--frames`, `--screenshot`, `--capture`, `--capture-fps`, `--record`, `--replay`, `--profile`, and `--profile-trace` (and `--gpu`). Ascension reads the flags below and leaves those alone. A value can be the next argument or `--flag=value`. Fights are deterministic, so there is no seed.

| Flag | Effect |
|---|---|
| `--transcript file` | Append every on-screen log line: blows, gold, XP, shop notes, path choices, build resets, a target change (`Target: Wolf 2.`), each enemy Cleave hits, and a summon (`Rubble Shard 2 joins the fight`) |
| `--data dir` | Load that folder instead of `data/`. A save there hot-reloads, the same check the game already runs |
| `--level N` | Start at level N. N has to be on the curve in `progress.json` |
| `--class id` | Start as that class. An evolution is taken only when that choice is open, so `--level` has to be at least the path's evolve level |
| `--enemy id` | Start on that fight. An id from `encounters.json` (such as `wolf_pack`) is that encounter. An id from `enemies.json` that is not an encounter is that one enemy. When an id is both, the encounter wins |
| `--gold N` | Start with N gold |
| `--give id[:count],...` | Put items in the bag. A missing count is 1 |
| `--equip id,...` | Wear those pieces. An id that was also given comes out of the bag; anything else is added and then worn. A two-handed weapon still fills the offhand |
| `--keys "1 1 c e r"` | Press those keys in order, one per frame after the window is up, then quit. The names are the combat table: `1`–`8`, `c`, `e`, `u`, `x`, `i`, `r`, `n`, `space`, `up`, `down`, `left`, `right`, `escape`, `f5`. The window does not need focus |
| `--keys-file path` | The same list, read from a file. Pass this or `--keys`, not both. `f5` saves |
| `--save-dir path` | Read and write the run save in that folder. Empty uses the per-user Ascension saves directory. A command that builds a hero (`--level`, `--class`, `--enemy`, `--gold`, `--give`, `--equip`, `--choose`, `--pick`) does not load a save |
| `--choose id` | When the evolution choice opens, pick that class |
| `--pick id` | When a skill pick opens, keep that option. Repeat the flag for each pick, in the order the picks open |

An unknown id stops the process before the window opens, and the message names the id. The same is true of an unknown key, a missing folder, and a transcript that cannot be opened. A relative `--data` path is relative to the working directory. A key script ends on its last frame, which is the frame `--screenshot` saves, unless `--frames` asks for a different length.

```
Ascension.exe --data playtest-data --level 6 --class berserker --enemy oathkeeper_varn --keys "1 1 3 1" --transcript varn.txt --screenshot varn.png
```

That is a level-6 Berserker against Oathkeeper Varn. The four keys are Strike, Strike, Heavy Blow, and Strike. `varn.txt` collects the log, and `varn.png` is the last of those frames. `oathkeeper_varn` is an encounter id, so the fight is the one in `encounters.json`.

```
Ascension.exe --level 6 --class berserker --enemy wolf_pack --keys "6 right 6" --transcript pack.txt
```

That is the same Berserker against the Wolf Pack. `6` is Cleave, which logs a hit on each living Dire Wolf. `right` marks the next one and logs `Target: Dire Wolf 2.` A Varn fight that calls a second shard logs `Rubble Shard 2 joins the fight.` Those lines are in the transcript because they are the on-screen log.

## Package

```sh
cmake --build --preset release --target package
```

writes `build/release/Ascension-<version>-<platform>.zip` (the executable, shaders, `data/`, licenses, and `.itch.toml`). CI calls Emerald's reusable workflow `Ridejock/Emerald/.github/workflows/game.yml@main` for Windows (MSVC) and Linux (GCC). The itch.io user and game slug are left empty, so a tag does not publish.
