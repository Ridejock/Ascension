# OpenRTP starter tilesets

Source: [OpenRTP](https://finalbossblues.itch.io/openrtp-tiles) by finalbossblues. License: CC0 1.0. No credit is required. The notice is `LICENSE_OpenRTP.txt`.

`hub_camp` (59 tiles) and `barrow` (60 tiles) are the approved starter sheets, remapped onto `art/palette.gpl`. Each sheet is an RGBA PNG with real alpha on a 16px grid. The JSON matches the icon atlas: `tile_size` is 16, and `tiles` maps each name to a pixel rect `{x, y, w, h}`. Names are unique within a sheet. Multi-cell props use a multiple of 16 for `w` and `h`.

Draw the tiles at 2x (32px on screen). `preview_hub.png` and `preview_barrow.png` are those scenes at 2x (640x384).

The full remapped source chipsets are not in this tree.

## hub_camp

Grass, a dirt path autotile (edges, inner corners, center, and a single tile), log house walls, a red tile roof and gable, doors and a window, a tent, fences, and camp props: campfire, well, signpost, notice board, torch, logs, pots, flowers, bushes, stump, barrel, bucket, bench, table, and trees.

## barrow

Stone floors, a wall-top autotile, wall faces, ledges, stone and earth stairs, rubble and boulders, gravestones, a tomb door, a ruin portal, pillars, torches, vines, a pot, and a bucket.

## Stand-ins

Cairns, barrow mounds, and standing stones are stand-ins for now. OpenRTP has no stacked-stone cairn, grassy mound, or weathered upright, so the sheets use the closest cave pieces:

- `rock_pile_2x2_cairn_placeholder` is a brown rock pile.
- `peak_grey_2x1_mound_placeholder` is a cliff peak.
- `stone_spire_1x2_standing_stone_placeholder` is a stalagmite.

Outdoor moor ground and a grass-bank tomb entrance are not in these sheets.

## Excluded

Skulls, summoning circles, and the statue from the full source sheets are intentionally excluded. Bones and the gargoyle are left out with them.
