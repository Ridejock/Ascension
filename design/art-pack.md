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

The pack root holds `manifest.json`. Every picture is a PNG inside the pack. Paths use `/`, stay relative, and cannot climb out with `..`. An unknown key is an error. A content id has to already exist in the loaded data. A picture is either a path string or an object with `file`.

| List | Id comes from | Pictures |
|---|---|---|
| `themes` | The pack. `maps` are ids from `data/maps` | No picture. Groups maps that share tile art |
| `tiles` | `tiles.json` | `file` or `variants` fills the overland cell |
| `foes` | `enemies.json` | `overland`, `battle`, or both |
| `heroes` | `classes.json` | `battle` is the fight picture for that class |
| `npcs` | `data/dialogue/<id>.json` | `sprite`, `portrait`, or both |
| `markers` | `hero`, `rest`, `shop`, `exit` | `file` replaces that overland marker |
| `props` | a prop id placed on a map | `file` is one PNG for that id |

Each list can be omitted. An empty file replaces nothing. A foe row can name one picture and leave the other out; the one left out stays on the CC0 art. The same is true of an NPC and of a marker.

On the map, a spawn draws the overland picture of the first enemy id in that spawn's encounter. A talk point draws the sprite for its dialogue id. A shop point draws the sprite for the dialogue id in `shop.json` when that sprite is in the pack and no talk point on the same map already uses that id. A shop whose keeper is already standing, or whose dialogue is empty or has no sprite, draws the shop marker. A portrait is drawn beside that conversation. The hero marker, the rest marker, and the exit marker replace those squares. A marker the pack leaves out keeps the square the game already draws.

The map draws each cell from its tile id. It does not know which neighbouring cells are walls, so the manifest has no neighbour piece set for walls. A building that is one picture, rather than a set of wall pieces, is a prop.

### Props

A map may list `props`. Each entry has an `id` and a `tile` (`x` grows east, `y` grows south), the same shape as a point's tile. That tile is the top-left of the footprint. Optional `w` and `h` are the footprint in cells, east and south from that tile. Optional `open` is a list of rectangles inside the footprint that stay walkable. An open entry is `x`, `y`, and optional `w` and `h` (each defaults to 1). Those coordinates are map cells, not offsets. A door, a rest, a shop, and a talk belong in `open`, or outside the footprint. Two footprints must not block the same cell.

A blocked cell stops a step the way a wall does. Walking, a flee, and a route all read the map footprint. Cells in `open` are not blocked, so the tile under them still decides: a floor stays walkable and a wall stays solid. The start, a spawn, a link, and a point have to sit on a cell the footprint does not block, either outside it or in `open`. With no `w` and `h`, the placement does not block on its own.

```json
{
  "id": "example_inn",
  "tile": { "x": 3, "y": 11 },
  "w": 11,
  "h": 6,
  "open": [ { "x": 7, "y": 11, "w": 2 } ]
}
```

The manifest lists the same ids. `file` is the PNG, a path or a picture object, the same as a marker. Optional `w` and `h` are that picture's size in cells. When the map omitted its footprint, those numbers are the footprint too, and the hero's steps use them. A flee and a route still read the map, so a door that has to stay open without a pack belongs on the map. An id the manifest leaves out is not drawn. The built-in art has no prop pictures, and the map footprint still blocks. An id no map places fails the load.

The picture's bottom-left sits on the footprint's bottom-left. A picture taller than the footprint hangs north of it. Those extra cells are not blocked. The map draws the picture after the ground tiles, in one order with the other overland sprites: the south row of the footprint, then a marker, a foe, and the hero on that same row. A smaller row is farther north and is drawn first, so the overhang covers a sprite standing under it, and a sprite on the south row is drawn in front.

A prop is one PNG. The manifest still has no neighbour piece set, and it does not crop a building out of a tileset.

### Themes, maps, and variants

A tile row may name `map` or `theme`, not both. A row that names the current map wins over a row for that map's theme, which wins over the row that names neither. A map can sit in one theme. Two rows with the same tile id and the same scope are an error.

`variants` is a list of pictures for that tile row, used instead of `file`. The cell `(x, y)` picks `(x * 73856093 + y * 19349663)` modulo the number of variants, in 32-bit unsigned arithmetic. The same cell always picks the same picture. `x` grows east and `y` grows south from the map's top-left.

### Strips

A picture object may set `frames`, `frameWidth`, and `frameDuration`. Frames sit in one horizontal row. `frames` defaults to 1, which uses the whole image, so a path string is unchanged. When `frames` is greater than 1 and `frameWidth` is omitted, the image width is split into equal frames. `frameDuration` is milliseconds. When it is omitted, each frame stays up for 200 milliseconds. The fight picture and the portrait use that clock, so a two-frame idle strip is not stretched into one rectangle.

```json
{
  "themes": [
    { "id": "example_theme", "maps": ["example_map"] }
  ],
  "tiles": [
    { "id": "example_floor", "file": "tiles/example_floor.png" },
    { "id": "example_floor", "theme": "example_theme", "file": "tiles/example_theme.png" },
    {
      "id": "example_floor",
      "map": "example_other",
      "variants": ["tiles/example_a.png", "tiles/example_b.png"]
    }
  ],
  "foes": [
    {
      "id": "example_foe",
      "overland": "foes/example_overland.png",
      "battle": {
        "file": "foes/example_battle.png",
        "frames": 2,
        "frameWidth": 32,
        "frameDuration": 200
      }
    }
  ],
  "heroes": [
    { "id": "example_hero", "battle": { "file": "heroes/example_battle.png", "frames": 2 } }
  ],
  "npcs": [
    {
      "id": "example_npc",
      "sprite": "npcs/example_sprite.png",
      "portrait": { "file": "npcs/example_portrait.png", "frames": 2, "frameWidth": 96 }
    }
  ],
  "markers": [
    { "id": "hero", "file": "markers/example_hero.png" },
    { "id": "rest", "file": "markers/example_rest.png" },
    { "id": "shop", "file": "markers/example_shop.png" },
    { "id": "exit", "file": "markers/example_exit.png" }
  ],
  "props": [
    { "id": "example_inn", "file": "props/example_inn.png", "w": 11, "h": 7 }
  ]
}
```

Those ids are placeholders, except the four marker ids, which are the markers the map already draws. A real pack uses ids the data already has, such as a tile id, an enemy id, a class id, a dialogue id (Pell's file is `pell`), a map id, or a prop id placed on a map. One floor id can then use one picture on one map and another picture on the maps in a theme. A prop id such as `example_inn` has to be a placement in a map file. The hub places `inn`, `shop_house`, and `pell_cabin`.

A path that leaves the pack, a file that is not a PNG, and an id the data does not have fail the load:

```text
manifest.json: foe 'example_foe': id does not exist
manifest.json: tile 'example_floor': '../other.png' must be a .png file inside the pack
manifest.json: tile 'example_floor': width is not divisible by 'frames'
```
