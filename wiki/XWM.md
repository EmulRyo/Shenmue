# XWM — xWMA audio

xWMA is the Microsoft audio format used by XAudio2: a RIFF file holding Windows Media Audio packets and a table to seek in them. In the PC version all the voices and music are xWMA clips stored in the audio [AFS](AFS) files (`scene/0x/stream/pc/eng/*.afs` and `scene/0x/stream/pc/jap/*.afs`, in the `audio_eng` and `audio_jap` archives). Each group of voice clips of an AFS has a [lip-sync table](Lip-sync) with the mouth movements and the subtitle of every clip.

- **Extension:** `.xwm`
- **Magic:** `RIFF` (`52 49 46 46`) at 0x00 and `XWMA` (`58 57 4D 41`) at 0x08
- **Compression:** WMA v2 (the PCM data is encoded)

## Layout

All the clips of the game have the same three chunks, in this order, and nothing after them:

```
+------------------------+ 0x00
| RIFF header (12 bytes) |
+------------------------+ 0x0C
| fmt  chunk             |  8 + 18 bytes
+------------------------+ 0x26
| dpds chunk             |  8 + 4 × packetCount
+------------------------+ 0x2E + 4 × packetCount
| data chunk             |  8 + blockAlign × packetCount
+------------------------+ size
```

Every chunk starts with a 4 character ID and a `uint32` size that doesn't include those 8 bytes. As the size of `dpds` depends on the number of packets, the audio data starts between 0x3E (3 packets) and 0x76 in the voice clips.

## RIFF header

| Offset | Size | Type    | Name     | Description                                     |
|-------:|-----:|---------|----------|-------------------------------------------------|
| 0x00   | 4    | char[4] | `id`     | Magic `RIFF`                                    |
| 0x04   | 4    | uint32  | `size`   | Size of the file minus 8 (always exact)         |
| 0x08   | 4    | char[4] | `format` | `XWMA`                                          |

## `fmt ` chunk

A `WAVEFORMATEX` structure. Its size is always 18 bytes, with no extra data.

| Offset | Size | Type   | Name              | Description                                           |
|-------:|-----:|--------|-------------------|-------------------------------------------------------|
| 0x00   | 2    | uint16 | `formatTag`       | `0x0161` (WMA v2) in every clip                        |
| 0x02   | 2    | uint16 | `channels`        | 1 (all the clips are mono)                             |
| 0x04   | 4    | uint32 | `samplesPerSec`   | Sample rate in Hz                                      |
| 0x08   | 4    | uint32 | `avgBytesPerSec`  | Bitrate of the encoded data, in bytes per second       |
| 0x0C   | 2    | uint16 | `blockAlign`      | Size of a WMA packet in bytes                          |
| 0x0E   | 2    | uint16 | `bitsPerSample`   | 16, the bits per sample of the decoded PCM             |
| 0x10   | 2    | uint16 | `cbSize`          | 0                                                      |

WMA v2 normally has 10 bytes of extra data (`samplesPerBlock` uint32, `encodeOptions` uint16, `superBlockAlign` uint32), but the clips have none, so a decoder has to assume the encode options. With `encodeOptions` 0x1F every clip decodes to the size given by the `dpds` table, except the clip at 32000 Hz (`jap/e1030.afs`), which needs 0x17.

The decoded audio of some clips has a DC offset: the silences between the words are not at 0 (e.g. around −5773 in `01BEDAC003`).

Variants found in the game files (all the xWMA entries of the audio AFS files, 111,330 clips, 59,352 different ones):

| `samplesPerSec` | `avgBytesPerSec` | `blockAlign` | Clips   | Files                                                    |
|----------------:|-----------------:|-------------:|--------:|----------------------------------------------------------|
| 22050           | 2500 (20 kbps)   | 929          | 111,072 | Voices                                                   |
| 44100           | 6000 (48 kbps)   | 2230         | 225     | Some voices of `free01.afs`, `free02.afs`, `a0114.afs`, `e1032.afs`, `e1041.afs` (English) |
| 44100           | 4000 (32 kbps)   | 1487         | 32      | Music (`bgm01.afs`, `bgm02.afs`, `bgm03.afs`)            |
| 32000           | 2500 (20 kbps)   | 1280         | 1       | `jap/e1030.afs`                                          |

## `dpds` chunk

The decoded packet cumulative data size table: one `uint32` per packet of the `data` chunk, with the number of bytes of decoded PCM from the start of the clip to the end of that packet.

| Offset | Size | Type        | Name   | Description                                       |
|-------:|-----:|-------------|--------|---------------------------------------------------|
| 0x00   | 4×N  | uint32[N]   | `dpds` | Decoded bytes at the end of each packet           |

- The number of entries (`size / 4`) is always the number of packets (`data size / blockAlign`).
- The values are multiples of 128 bytes (64 samples).
- The length of a clip is `dpds[N-1] / (2 × channels) / samplesPerSec` seconds: from 0.29 s to 249.1 s (the second music of `bgm01.afs`), 37.8 hours in total.
- In 6,553 clips the last value is equal to the previous one: the last packet adds no samples.

## `data` chunk

| Offset | Size          | Type     | Name      | Description                              |
|-------:|--------------:|----------|-----------|------------------------------------------|
| 0x00   | blockAlign×N  | uint8[]  | `packets` | WMA v2 packets of `blockAlign` bytes     |

The size is always a whole number of packets. A voice clip has usually between 3 and 12 packets (about 0.23 s of audio each at 22050 Hz).

## Notes

- The clips are padded to whole packets, so they are slightly longer than the duration of the line in the [SCNF](SCNF) files and in the [lip-sync table](Lip-sync).
- The name of each clip comes from the [IDX](IDX) of its AFS (e.g. `01BEDAA001` in `scene/01/stream/pc/eng/01beda.afs`).
