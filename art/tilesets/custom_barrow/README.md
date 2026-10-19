# Outer Barrow tiles (original art)

Original pixel art drawn from scratch for Ascension by the Game Art Director. No third-party or paid pixels.

- `outer_barrow.png` + `outer_barrow.json`: 100 tiles on a 16px grid (drawn at 2x).
- JSON uses the same format as the other sheets: `{tile_size:16, tiles:{name:{x,y,w,h}}}`.
- Colours come from `art/palette.gpl` (50 colours).
- `preview_outer_barrow.png`: 2x mock scene with the Fighter.

Rebuild the sheet from the text maps:

    python tools/make_barrow.py   # reads barrow_maps.txt
