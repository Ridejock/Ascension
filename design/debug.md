# Debug playtest keys

Two keyboard actions for a running game. The names are `DebugRespawnFoes` and `DebugNewGame`. Their bindings, the confirm window, and the on-screen lines live in `data/input.json` (and the frozen copy in `tests/data/`). The game asks Emerald for the action. It does not read a scancode.

`F5` is already Save, so respawn is the next free function key.

| Action | Key | Pad | What it does |
|---|---|---|---|
| DebugRespawnFoes | F6 |  | Bring every defeated foe back, on every map |
| DebugNewGame | F9, twice |  | Open character create. The second press has to land inside the confirm window |

Neither action has a gamepad button. Rebinding is a change to `input.json` only.

## Respawn

`DebugRespawnFoes` clears defeated foe ids and cleared spawn ids. The hero keeps stats, level, XP, worn gear, the bag, gold, and the tile they are standing on. Claimed drops stay claimed. A flee wound stays on that spawn, because that foe was not defeated. Talk flags stay.

The line is `debugRespawned` (`Foes returned.`).

An ongoing fight, and a map fight that has not settled yet, does nothing to those lists. The line is `debugRespawnBusy` (`Finish the fight.`).

## New game

`DebugNewGame` the first time arms a confirm. The line is `debugNewGameArmed` (`Press again to start a new game.`).

The second press opens character create when it lands within `debugConfirmMilliseconds` of the first. That value is 1500. A later press arms the window again instead of opening create. There is no gamepad confirm for this: the second press is the same action.

From a running game, Begin is the new run. The hero is the name and the starter the player picked, at level 1, with no XP. The bag is empty and gold is 0. Defeated foes, cleared spawns, claimed drops, and talk flags are clear. Health and focus are full. The hero stands on the start tile of the map that sets `hub`. Escape before Begin returns to the game and changes nothing.

The line is `debugNewGame` (`New game.`), when create opens.

The save file is not deleted and not rewritten. The next Save, or Quit game on the pause menu, writes this run over it. From the title, F9 is New game, and Begin writes the file the way the title's own New game does. A save still asks before it is replaced.

## Help

While the help line is showing, the map, the fight screen, and the between-fights screen draw F6 as FOES and F9 as NEW. Hiding the help line hides those chips. The keys still work.
