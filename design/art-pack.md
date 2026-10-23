# Art pack

A private build can replace pictures without putting those files in this repo. The game still ships the CC0 art. An id the pack does not list keeps that art.

## Where the pack is

Startup looks in this order:

1. `--art-pack <dir>` (the next argument, or `--art-pack=<dir>`). A relative path is relative to the working directory. The directory has to exist.
2. Otherwise, a directory named `artpack` next to the executable, when that directory exists.
3. Otherwise there is no pack. Drawing stays on the built-in CC0 art, the same as a build with no flag and no folder.

`--art-pack` does not change the hero or the save. A bad manifest stops the process before the window and names the file and the field, the same way a bad file under `data/` does.

The folder is gitignored. Packaging copies `data/` and `assets/` by name and does not copy `artpack/`. A zip that contains that folder fails the package step.

## manifest.json

The pack root holds `manifest.json`. Every picture is a PNG inside the pack. Paths use `/`, stay relative, and cannot climb out with `..`. An unknown key is an error. An id has to already exist in the loaded data. A repeated id in the same list is an error.

| List | Id comes from | Pictures |
|---|---|---|
| `tiles` | `tiles.json` | `file` fills the overland cell |
| `foes` | `enemies.json` | `overland`, `battle`, or both |
| `heroes` | `classes.json` | `battle` is the fight portrait for that class |
| `npcs` | `data/dialogue/<id>.json` | `sprite`, `portrait`, or both |

Each list can be omitted. An empty file replaces nothing. A foe row can name one picture and leave the other out; the one left out stays on the CC0 art. The same is true of an NPC.

On the map, a spawn draws the overland picture of the first enemy id in that spawn's encounter. A talk point draws the sprite for its dialogue id. A shop point draws the sprite for the dialogue id in `shop.json`. A portrait is drawn beside that conversation. The hero's square on the map is not in the manifest.

```json
{
  "tiles": [
    { "id": "example_floor", "file": "tiles/example_floor.png" }
  ],
  "foes": [
    {
      "id": "example_foe",
      "overland": "foes/example_overland.png",
      "battle": "foes/example_battle.png"
    }
  ],
  "heroes": [
    { "id": "example_hero", "battle": "heroes/example_battle.png" }
  ],
  "npcs": [
    {
      "id": "example_npc",
      "sprite": "npcs/example_sprite.png",
      "portrait": "npcs/example_portrait.png"
    }
  ]
}
```

Those ids are placeholders. A real pack uses ids the data already has, such as a tile id, an enemy id, a class id, or a dialogue id (Pell's file is `pell`).

A path that leaves the pack, a file that is not a PNG, and an id the data does not have fail the load:

```text
manifest.json: foe 'example_foe': id does not exist
manifest.json: tile 'example_floor': '../other.png' must be a .png file inside the pack
```
