# FON — Bitmap font

A FON file is a bitmap font: glyphs of the same size stored one after another, without a header, an index or a magic. The size of the glyphs and the character set are given only by the size of the file.

- **Extension:** `.fon`
- **Magic:** none

The files of Shenmue are in `misc/`:

| File           | Size   | Glyph | Bytes per glyph | Glyphs | Empty glyphs | Character set |
|----------------|-------:|-------|----------------:|-------:|-------------:|---------------|
| `dc_kana.fon`  | 7680   | 16×15 | 30              | 256    | 74           | Single-byte codes |
| `dc_kanji.fon` | 351270 | 24×15 | 45              | 7806   | 996          | JIS X 0208 |
| `ryou.fon`     | 351270 | 24×15 | 45              | 7806   | 1250         | JIS X 0208 |
| `waza.fon`     | 351270 | 24×15 | 45              | 7806   | 2238         | JIS X 0208 |

## Layout

```
+---------------------+ 0x00
| Glyph 0             |  width × height / 8 bytes
+---------------------+
| Glyph 1             |
+---------------------+
| ...                 |
+---------------------+
| Glyph N−1           |
+---------------------+ N × glyph size
```

## Glyph

| Offset          | Size       | Type    | Name   | Description                                   |
|----------------:|-----------:|---------|--------|-----------------------------------------------|
| 0x00            | width / 8  | uint8[] | `row0` | First row of pixels, from left to right        |
| width / 8       | width / 8  | uint8[] | `row1` | Second row                                     |
| ...             | ...        | ...     | ...    | One row per line of the glyph, `height` rows   |

- 1 bit per pixel: 1 is ink, 0 is background. There is no anti-aliasing.
- The first pixel of each byte is its **least significant bit** (bit 0), the eighth is bit 7. Read from the most significant bit, the halves of every byte are mirrored.
- The rows have no padding: a row is exactly `width / 8` bytes (2 for 16 pixels, 3 for 24), and the glyphs follow each other without alignment (30 or 45 bytes).
- The glyphs use the whole 15 rows, without blank lines around them.

## Character sets

### Single-byte codes (16×15)

The index of a glyph is its code, from 0x00 to 0xFF:

| Codes       | Contents |
|-------------|----------|
| 0x00–0x20   | Empty (0x20 is the space) |
| 0x21–0x7E   | ASCII, with some symbols replaced: `☆` at 0x2A, `‥` at 0x3D, `¥` at 0x5C, `↓` at 0x5E, `→` at 0x5F, a raised mark at 0x60, quotation marks `‘` `“` `’` at 0x7B–0x7D and `…` at 0x7E |
| 0x7F–0x80   | Empty |
| 0x81–0x99   | Katakana with dakuten and handakuten, precomposed: ガ ギ グ ゲ ゴ ザ ジ ズ ゼ ゾ ダ ヂ ヅ デ ド バ パ ビ ピ ブ プ ベ ペ ボ ポ |
| 0x9A–0xA0   | Empty |
| 0xA1–0xDF   | Half-width katakana and punctuation of JIS X 0201 (。「」、・ヲァィゥェォャュョッーアイウ...ン゛゜) |
| 0xE0–0xFF   | Empty |

### JIS X 0208 (24×15)

The glyphs follow the order of the JIS X 0208 table, 94 glyphs per row (区), starting at 0x2121 (row 1, cell 1):

```
index = (row − 1) × 94 + (cell − 1)
JIS   = ((0x21 + index / 94) << 8) | (0x21 + index % 94)
```

- Row 1 (、。，．・：；？！゛゜...) and row 2 (◆□■△▲▽▼※〒→←↑↓〓...) hold the symbols, row 3 the digits and the Latin letters, rows 4 and 5 hiragana and katakana, rows 6 and 7 Greek and Cyrillic, rows 16–47 the level 1 kanji and rows 48–84 the level 2 kanji.
- The cells that JIS X 0208 leaves unassigned are empty glyphs (all bytes zero). Row 8 (box drawing) is empty too.
- Rows 9, 12, 13 and 14, unassigned in JIS X 0208, have 3, 1, 1 and 1 glyphs.
- The glyphs look like a 16-pixel-wide design stretched to 24 pixels: in each group of 3 columns (0–2, 3–5, ..., 21–23) the first two columns are almost always equal (in `dc_kanji.fon`, in 90% of the 443192 groups with ink), as if each pair of pixels had become 3 by repeating the first one. Because of this, strokes that should be equal have different widths (e.g. □ has a left side 1 pixel wide and a right side 2 pixels wide), and the diagonals have irregular steps. With square pixels the glyphs are too wide (□ is 21 × 14 pixels); drawn 2/3 as wide, with pixels 2 wide and 3 high, they get the proportions of the 16 × 15 glyphs of `dc_kana.fon` and □ becomes square (14 × 14).
- 7806 = 83 × 94 + 4: the files end at row 84, cell 4 (0x7424), 2 cells before the end of JIS X 0208 (row 84 has 6 kanji).
- Glyphs with ink per row in `dc_kanji.fon`: 93 in row 1, 16 in row 2, 62 in row 3, 83 in row 4, 86 in row 5, 48 in row 6, 66 in row 7, 94 in rows 16–46 and 48–82, 51 in row 47, 91 in row 83 and 4 in row 84.

### Differences between the three 24×15 fonts

The three files share the same order and most of their glyphs:

- `dc_kanji.fon` is the complete font (6810 glyphs with ink).
- `ryou.fon` differs in 1864 glyphs: punctuation, kana and many kanji are drawn in an irregular, handwritten style. 6556 glyphs have ink: 254 glyphs of `dc_kanji.fon` are empty.
- `waza.fon` differs in 1578 glyphs and is a subset (5568 glyphs with ink): 1242 glyphs of `dc_kanji.fon` are empty, and some glyphs are changed (e.g. the long vowel mark ー is vertical).
- No glyph that is empty in `dc_kanji.fon` has ink in the other two.
