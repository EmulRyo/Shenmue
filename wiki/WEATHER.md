# WEATHER — Weather and calendar

`WEATHER.BIN` holds the weather of the game day by day, from 29 November 1986 (the first day of the game): curves of the clouds, the rain and the falling snow, and an entry per day with the events of the day (snow on the ground, Christmas, New Year). The [scene programs](MAPINFO#date-weather-and-variants-of-the-map) read the events to choose the variants of their map, e.g. the models with snow.

There are four files, of two versions of the data:

| File                        | Size  | Days                       | Days with snow on the ground |
|-----------------------------|------:|----------------------------|-----------------------------:|
| `misc/coldinit/WEATHER.bin` | 4,520 | 29 Nov 1986 to 30 Apr 1987 | 41 |
| `misc/cold_s/WEATHER.bin`   | 4,076 | 29 Nov 1986 to 20 Apr 1987 | 41 |
| `sprite/weather.bin`        | 4,076 | the same file as `misc/cold_s/WEATHER.bin` | 41 |
| `sprite/weat_org.bin`       | 3,924 | 29 Nov 1986 to 20 Apr 1987 | 21 |

`1ST_READ.BIN` names `WEATHER.BIN` (`"Weather Soft2_Data Read"`) and `WEAT_ORG.BIN` (`"Weather Org_Data Read"`).

- **Extension:** `.bin`
- **Magic:** `WDAT` (`57 44 41 54`)
- **Compression:** none in the game files

## Layout

```
+---------------------------+ 0x00
| WDAT header (8 bytes)     |
+---------------------------+ 0x08
| CLUD chunk                |  clouds
+---------------------------+
| RAIN chunk                |  rain
+---------------------------+
| SNOW chunk                |  falling snow
+---------------------------+
| FCAS chunk                |  one entry per day
+---------------------------+ size
```

### Header (8 bytes)

| Offset | Size | Type    | Name    | Description |
|-------:|-----:|---------|---------|-------------|
| 0x00   | 4    | char[4] | `magic` | `WDAT` |
| 0x04   | 4    | uint32  | `size`  | Always 8: the size of the header |

### Chunk header (8 bytes)

| Offset | Size | Type    | Name   | Description |
|-------:|-----:|---------|--------|-------------|
| 0x00   | 4    | char[4] | `id`   | `CLUD`, `RAIN`, `SNOW` or `FCAS` |
| 0x04   | 4    | uint32  | `size` | Size of the chunk, header included. The data is `(size - 8) / 4` entries of 4 bytes |

## CLUD, RAIN and SNOW — curves

Each entry is a key of the curve: a date, an hour and an intensity. Between two keys, the game interpolates the intensity linearly over the packed values of the keys (bits 0 to 23).

| Bits  | Name        | Description |
|------:|-------------|-------------|
| 0–5   | `minute`    | 0 to 59 |
| 6–10  | `hour`      | 0 to 23 |
| 11–15 | `day`       | 1 to 31 |
| 16–19 | `month`     | 1 to 12 |
| 20–23 | `year`      | Year minus 1986 |
| 24–31 | `intensity` | 0 to 255: 0, 95, 127, 159, 191 or 255 (also 111 in `SNOW`) |

The first key of every curve is 1 January 1986 at 0:00 and the last one 31 December 2001 (`year` 15), both with intensity 0, around the keys of the days of the game. A key at intensity 0 usually follows each period, e.g. `SNOW` of `misc/coldinit/WEATHER.bin` has snow at intensity 255 from 1:00 to 23:00 on 4 November 1986 (with keys at 0 at 0:30 and 23:30), then from 0:30 on 29 November (the snow of the first day of the game).

The keys are in order, but for 2 keys of `CLUD` (30 December 1986 and 18 January 1987) and one of `RAIN` (6 January 1987) of `misc/coldinit/WEATHER.bin`, and one of `RAIN` of `sprite/weat_org.bin`, which are a few hours before the previous key.

| Chunk  | coldinit | cold_s | weat_org | Days with an intensity above 0 (coldinit) |
|--------|---------:|-------:|---------:|------:|
| `CLUD` | 460 keys | 414    | 419      | 111   |
| `RAIN` | 344      | 298    | 323      | 64    |
| `SNOW` | 162      | 153    | 85       | 37    |

## FCAS — events of the days

One entry per day, in order, without gaps (153 in `misc/coldinit/WEATHER.bin`, 143 in the others):

| Bits  | Name      | Description |
|------:|-----------|-------------|
| 0–7   | `unknown` | Unknown: 1 to 67, 38 different values (the most frequent are 1, 8 and 2). *Probably* the forecast of the day: the operation 11 of the function `0x176` of the game returns the low byte of the entry of a date |
| 8–15  | `events`  | Bits of the events of the day (below) |
| 16–20 | `day`     | 1 to 31 |
| 21–24 | `month`   | 1 to 12 |
| 25    | —         | Always 0 |
| 26–31 | `year`    | Year minus 1986 |

The game finds the entry of the current date by comparing the bits 16 to 29 and 31 (mask `0xBFFF0000`) with `(year - 86) << 26 | month << 21 | day << 16`.

| Bit of `events` | Meaning | Days |
|----:|---------|------|
| 0   | Never set | — |
| 1   | Snow on the ground | 41 days from 29 November 1986 to 12 March 1987 (21 in `weat_org.bin`, to 9 March): the day of a heavy snowfall of `SNOW` and usually a few days after it, e.g. 11 to 12 December, 24 to 28 December, 15 to 21 January |
| 2   | Christmas | 15 to 25 December |
| 3   | New Year  | 1 to 5 January |

E.g. 24 and 25 December have the bits 1 and 2 (snow at Christmas), and 1 and 2 January the bits 1 and 3.

## Use by the game

The function `0x176` of the game (Weather) reads the file; its operation 12 returns, for the current date, whether a bit of `events` is set, and the scene programs choose with it the models and the packages of their map: see [Date, weather and variants of the map](MAPINFO#date-weather-and-variants-of-the-map).
