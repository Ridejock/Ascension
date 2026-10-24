# Battle and shop screens

The owner playtest could not read the fight or the shop. Text was small and tight, the acting fighter was a short label, and the command and shop rows did not say what the cursor would do.

The hint row along the bottom (the prompt icons from `prompts.json`) is unchanged: same glyphs, same scale, same place (`x = 48`, `y = height - 36`). The new panels stop above it.

## Before

One 1280×720 fight frame. Side panels started at (28, 24). The lower panel was 124 pixels tall and held a scale-2 banner plus six scale-1 log lines, 1 pixel apart. Skills listed a full description under every row at scale 1, with 2 pixel gaps. A pack drew names at scale 1. Whose turn it was was `YOUR TURN` or `NAME ACTS` in that lower panel. The marked foe was a one-pixel frame and the word `TARGET`. Confirm used the row immediately. Cancel had no keyboard key.

The shop used the same frame. Item rows were scale 1 with the price, the sell price, and every stat on that same line. Gold sat in the keeper line. Confirm bought or equipped at once.

## After

Type, color, gaps, panel rectangles, the message hold, and the turn preview count live in `data/battle_ui.json`. The shop’s list and detail live in `data/shop_ui.json`. The shop uses the battle file for type size, color, padding, and the message hold, so the two screens stay one style.

| What | Where | Shipped |
|---|---|---|
| Names | `titleScale` | 4 (36px line) |
| HP, commands, item names | `bodyScale` | 3 (27px) |
| The selected description, pack names | `detailScale` / `packScale` | 2 (18px) |
| Turn banner | `bannerScale` | 3 |
| The one message line | `messageScale` | 3 |
| Space under a line | `lineGap` | 12 |
| Space under a command row | `rowGap` | 12 |
| Space between groups | `groupGap` | 22 |
| Inside a panel | `panelPad` | 22 |
| Message hold | `messageMinMilliseconds` | 1100 |
| Upcoming turns | `turnPreview` | 4 |
| Shop rows on screen | `shopUi.listRows` | 7 |
| Shop icons | `shopUi.iconScale` | 2 |

A command row is the name, the rank, and the focus cost. The description under the cursor is the only skill paragraph. The lower panel shows one message. Confirm skips the hold when another line is waiting. The fight keeps that line up for `messageMinMilliseconds` before the next one.

The acting fighter’s sprite and name use `highlight`, and that side’s panel edge does too. The banner reads `YOUR TURN` or `NAME'S TURN`. When the round model has an order, a strip lists the next `turnPreview` turns in the foe panel, and it is left off when that panel has no room under the health line. The banner and the message keep their size.

Confirm on a skill arms a target instead of swinging. An arrow and a highlight sit on the foe or the hero, with the name and HP. A confirm / cancel prompt uses the same prompt icons, drawn above the hint row, not in it. Cancel (Backspace, or East on a pad) leaves that step. Wait, items, and flee still run from the row. Number keys still cast at once.

The shop list is the worn rows and the bag, with the stock beside them. The cursor row is highlighted. Gold is its own line. The detail under the lists is the item, its price, what it does, and the stat change against the piece in that slot (`+` / `-` from what is worn now). The first confirm arms buy, sell, equip, or take-off. The next confirm does it. Cancel returns to the list, and from the list it closes the shop.
