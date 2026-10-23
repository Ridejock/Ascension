# Button prompts

Hints the game already draws (talk, camp, battle commands, the map tile prompt) use this map. There is no separate prompt screen.

## Folder

`assets/prompts/`

| File | What it is |
|---|---|
| `xbox.png` | Xbox sheet, 128×48, nearest-neighbor |
| `xbox.json` | Atlas in the starter format |
| `playstation.png` | PlayStation sheet |
| `playstation.json` | Atlas |
| `switch.png` | Switch sheet, also used for a generic pad |
| `switch.json` | Atlas |
| `CREDITS.md` | CC0, original art |

Do not add a keyboard sheet. Keys stay a short text label (`E`, `Enter`).

## Atlas

Starter format, one object:

```json
{ "tile_size": 16, "tiles": { "a": { "x": 0, "y": 0, "w": 16, "h": 16 } } }
```

`tile_size` is 16. Every tile is 16×16. A glyph names a tile on that family's sheet. A name the atlas does not have fails the load.

## Glyph map

`data/prompts.json` (and the frozen copy in `tests/data/`). Top-level key `prompts`.

A prompt is asked by **engine action** and the **last-used device**. The file does not name actions. It names the Emerald button the action is bound to right now (`South`, `DPadUp`, `LeftTrigger`, …) and the device family (`xbox`, `playstation`, `switch`). Rebinding Confirm from South to North picks up the North tile without a code change. Keyboard, and any button with no tile, draws the short label from Emerald (`GetKeyLabel` / `GetButtonLabel`).

Families, from Emerald's pad type name:

| Family | Type names | Sheet |
|---|---|---|
| `xbox` | Xbox 360, Xbox One/Series | `xbox.png` |
| `playstation` | PS3, PS4, PS5 | `playstation.png` |
| `switch` | Switch Pro, Joy-Con (L), Joy-Con (R), Joy-Con pair, Standard, Unknown, GameCube | `switch.png` |

`defaultFamily` is `switch`, so a type the list does not name still uses that sheet. `textFamily` is `keyboard`: no sheet.

Switch face tiles follow the printed letters, not Xbox positions: South is `b`, East is `a`, West is `y`, North is `x`. A generic or Standard pad shares that sheet.

## Size

`iconScale` and `textScale` sit next to the glyph map. Both are integers.

The icon is drawn at `tile_size * iconScale` with nearest filtering and pixel snap, so a 16×16 tile stays on whole pixels.

Hint words and the text-label fallback use Emerald's debug font at `textScale`. That font's cap is 7 rows. The shipped pair is `iconScale` 1 and `textScale` 2: a 16px icon and a 14px cap. The cap is centered in the icon box, `(icon - cap) / 2`, so a glyph and the word beside it (and a letter used in place of a missing glyph) share one line.

The pixel-matched pair for this font is `iconScale` 7 and `textScale` 16 (112px cap on a 112px icon). Change the two numbers in `prompts.json`; the draw code reads them and does not pick a scale of its own.

`gap` is the space between a glyph and its word. `groupGap` is the space after a word, before the next glyph. Both are pixels.
