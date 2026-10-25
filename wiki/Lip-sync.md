# Lip-sync table

A lip-sync table holds, for every voice clip of a group of an audio [AFS](AFS), the character who speaks, the subtitle and the movements of the mouth. Each group of [xWMA](XWM) clips (e.g. the 13 clips `01BEDAA001`…`01BEDAC003` of `scene/01/stream/pc/eng/01beda.afs`) has one table, stored as another entry of the same AFS. The [IDX](IDX) entry of every clip gives the index of its table and the offset of its record.

There are 1,930 tables in the audio AFS files of the PC version (`audio_eng` and `audio_jap` archives).

- **Extension:** none (the tables have no name in the [IDX](IDX))
- **Magic:** none, but every table starts with `08 00 00 00` (the size of the first speaker block)
- **Compression:** none

## Layout

The table is a list of records, one per clip. A record is three blocks, each one starting with a `uint32` with the size of the block (including that field):

```
+------------------------+ 0x00
| Record 0               |
|  +------------------+  |
|  | Speaker block    |  |  8 bytes
|  +------------------+  |
|  | Text block       |  |  textSize bytes
|  +------------------+  |
|  | Keys block       |  |  keysSize bytes
|  +------------------+  |
+------------------------+
| Record 1               |
| ...                    |
+------------------------+
| Zero padding           |  only up to the next 0x800 boundary
+------------------------+ 0x800
| Record N               |
| ...                    |
+------------------------+ size
```

A record never crosses a 0x800 byte boundary: when the next record doesn't fit, the rest of the 0x800 bytes is filled with zeros and the record starts at the boundary. The table ends with the last record, without padding.

## Speaker block (8 bytes)

| Offset | Size | Type    | Name          | Description                                                   |
|-------:|-----:|---------|---------------|---------------------------------------------------------------|
| 0x00   | 4    | uint32  | `speakerSize` | Always 8                                                      |
| 0x04   | 4    | char[4] | `speaker`     | ID of the character who speaks (e.g. `AKIR`, `FUKU`), or 4 zero bytes (2,089 clips) |

## Text block

| Offset | Size         | Type    | Name       | Description                                          |
|-------:|-------------:|---------|------------|------------------------------------------------------|
| 0x00   | 4            | uint32  | `textSize` | Size of the block: 4 + the size of `text`            |
| 0x04   | textSize − 4 | char[]  | `text`     | Subtitle, null terminated and padded with zeros to 4 bytes |

The text is in **EUC-JP**, with the same special characters as the subtitles of the [SCNF](SCNF) files (`A1 F5` `＆` line break, `3D 40` `=@` ellipsis), but without the name of the speaker and the brackets. It is in English in the `eng` folders and in Japanese in the `jap` folders. 1,134 clips (shouts, grunts...) have no text: `textSize` is 4.

## Keys block

| Offset   | Size | Type      | Name       | Description                                           |
|---------:|-----:|-----------|------------|-------------------------------------------------------|
| 0x00     | 4    | uint32    | `keysSize` | Size of the block: 8 + 8 × the number of keys         |
| 0x04     | 8×N  | Key[N]    | `keys`     | Mouth keys, in playback order                         |
| 0x04+8×N | 4    | uint32    | `end`      | `0xFFFFFFFF`                                          |

In 77 records (e.g. the lines of `ASDA` in `scene/01/stream/pc/eng/a0142.afs`) `keysSize` is 4: the block has no keys and no `end` marker.

### Key (8 bytes)

| Offset | Size | Type   | Name       | Description                                                     |
|-------:|-----:|--------|------------|-----------------------------------------------------------------|
| 0x00   | 2    | uint16 | `mouth`    | Shape of the mouth, 0 to 5                                      |
| 0x02   | 2    | uint16 | `unknown`  | Unknown, always 2                                               |
| 0x04   | 4    | uint32 | `duration` | Time the shape is held, in 1/60 s                               |

- The sum of the durations is the length of the line: it matches the length of the [xWMA](XWM) clip, which is a bit longer as it is padded to whole packets (median 55 keys per second of clip, 99 % of the clips under 59.6).
- The last key has `mouth` 0 (except in 3 records), and its `duration` is 1 in 60 % of the records. The first key can have any shape.
- Most durations are under 60 (1 second); 16 keys have a duration of 0.
- In 117 clips of `F1274` (`jap/free01.afs`, `jap/free02.afs`) the durations add up to more than the length of the clip (up to 112 per second): the record doesn't match the clip.

## Notes

- The records are not always in the order of the clips (e.g. in `scene/01/stream/pc/jap/01beda.afs` the lines of `FUKU` come before those of `AKIR`): the record of a clip is found with the offset of its [IDX](IDX) entry.
- The table is not always the entry after the clips of its group.
- Clips without a table (the music of `bgm01.afs`...) have the index `0xFFFF` and the offset `0xFFFFFFFF` in the [IDX](IDX).
