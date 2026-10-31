# Maps

`mapedit` writes `data/maps/*.json`. A person and a grokbot use the same commands. The game loads those files and hot-reloads them. There is no map-type field. Overland, dungeon, and town are recipes for a new file.

Build it from the repo:

```
cmake --build --preset debug --target mapedit
.\build\debug\bin\mapedit.exe check --data data
```

`--data` is the data directory. From the repo that path is `data`. A command that changes a map copies the directory, writes the map there, and loads the copy. The real file changes only when that load succeeds. A failure prints the loader's sentence and leaves the real file alone. `check` only loads. Exit 0 is success, exit 1 is a rejected map, and exit 2 is bad usage.

You can also edit a map file by hand and then run `check`. The commands exist so you do not have to invent glyphs or keep the rows the same width.

## New maps

```
mapedit new --data data --id wild_north --kind overland --width 48 --height 16
mapedit new --data data --id barrow_8 --kind dungeon --width 24 --height 14
mapedit new --data data --id east_town --kind town --width 40 --height 20
```

- **overland.** Grass, with a tree border. The start is `(1, height/2)`, on grass.
- **dungeon.** A barrow border and a stone room one cell in from the edge. The start is the west interior cell. Width and height are at least 5.
- **town.** Grass, with a tree border. The start is the center. Buildings and doors are later `prop` and `point` commands.

The loss-return town is the one map `hub` sets. Any other town is an ordinary map. A rest on any map refills the hero who stands on it.

## Stamps

```
mapedit paint --data data --map wild_north --tile path --x 0 --y 8 --w 48 --h 1
mapedit start --data data --map wild_north --x 1 --y 8
mapedit link --data data --map wild_north --x 47 --y 8 --to barrow_1 --ax 0 --ay 7
mapedit spawn --data data --map wild_north --id north_wolf --x 20 --y 8 --encounter wolf
mapedit point --data data --map east_town --kind rest --x 8 --y 14
mapedit point --data data --map east_town --kind talk --x 13 --y 9 --dialogue pell
mapedit point --data data --map east_town --kind shop --x 15 --y 5
mapedit prop --data data --map east_town --id inn --x 3 --y 11 --w 11 --h 6 --open 7,11,2,1
mapedit hub --data data --map east_town
mapedit hub --data data --map east_town --off
```

`spawn` also takes `--once true|false` (the default is true), `--no-flee`, `--mover stand`, and `--aura-status <id> --aura-radius <n>`. `prop` may repeat `--open`. `--w` and `--h` are both set or both left out. `--open` is `x,y` or `x,y,w,h`.

Remove a stamp with `unlink`, `unspawn`, `unpoint`, or `unprop`. `unprop` removes every placement of that id.

```
mapedit unlink --data data --map wild_north --x 47 --y 8
mapedit unspawn --data data --map wild_north --id north_wolf
mapedit unpoint --data data --map east_town --x 15 --y 5
mapedit unprop --data data --map east_town --id inn
```

## Rules the loader already enforces

- One marker on a tile. A rest may share the start. A prop footprint may not cover a start, a spawn, a link, or a point, unless that cell is in the prop's `open` list.
- Spawn ids are unique across every map.
- A link's arrival is a walkable tile on a map that exists.
- An open cell sits inside its footprint.
- Only one map sets `hub`.
- A paint that makes a start, a spawn, a link, or a point solid is rejected, and the file is not written.

A spawn names an encounter that already exists. A talk names a dialogue file that already exists. A shop point opens the shop in `shop.json`. This tool does not add encounters, dialogue, or tiles.

## Glyphs

A tile id that is already on the map keeps its glyph. A new tile id takes its preferred glyph when that character is free: floor `+`, wall `#`, grass `.`, path `,`, water `~`, tree `T`, stone `=`, barrow `B`, rubble `%`. Anything else takes the next free printable character, skipping space. After a write, a glyph that no longer appears in the rows is dropped. A map with no free character is rejected by name.
