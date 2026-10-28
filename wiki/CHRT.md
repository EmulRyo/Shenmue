# CHRT — Character script

A CHRT file is a script made of commands that places the characters and objects of a scene: the models to load, and for each character its model, position, angle, flags, collision, shadow, etc. The commands are self-describing: each one stores the type of its arguments, so they can be decoded without knowing them in advance. Some commands open a block that contains other commands.

CHRT files are found inside [IPAC](IPAC) blocks, always with the name `CHARA`, next to the models they reference. The game has 646 different CHRT files, with 0 to 148 characters each (one character in 456 of them).

- **Extension:** `.chrt`
- **Magic:** `CHRS` (`43 48 52 53`)
- **Compression:** the whole file can be [gzipped](Home#compression)

## Layout

```
+---------------------+ 0x00
| CHRS header         |  8 bytes
+---------------------+ 0x08
| Command             |
| Command             |
| ...                 |
+---------------------+ size
| STRG block          |  null terminated strings
+---------------------+
```

## CHRS header (8 bytes)

| Offset | Size | Type    | Name   | Description                                          |
|-------:|-----:|---------|--------|------------------------------------------------------|
| 0x00   | 4    | char[4] | `id`   | Magic `CHRS`                                         |
| 0x04   | 4    | uint32  | `size` | Size of the CHRS block; the STRG block starts here   |

## Command (8 + 4×N bytes)

| Offset | Size | Type      | Name       | Description                                                  |
|-------:|-----:|-----------|------------|--------------------------------------------------------------|
| 0x00   | 4    | uint32    | `name`     | Offset of the command name, **relative to this field**       |
| 0x04   | 4    | uint32    | `argTypes` | Type of each argument, 3 bits per argument starting at the lowest bits; a 0 ends the list |
| 0x08   | 4×N  | uint32[N] | `args`     | One 4-byte value per argument                                |

Command names are case-insensitive: the same command is written `Image`, `IMAGE` or `image` in different files. The same happens with some string arguments (`DISP`, `Disp`).

### Argument types (3 bits)

| Value | Type    | Meaning of the 4-byte value                                    |
|------:|---------|----------------------------------------------------------------|
| 0     | End     | No more arguments                                              |
| 1     | Float   | 32-bit float                                                   |
| 2     | Literal | 4 characters stored in place (e.g. a character ID like `AKMI`) |
| 3     | String  | Offset of a null terminated string, **relative to the argument** |
| 4     | Int     | 32-bit unsigned integer                                        |

Other values are not used by the game files.

### Blocks

The commands `DefImage`, `Character`, `Entry` and the long form of `Image` end with an int argument: the **size of a block**, in 4-byte words, made of the commands that follow it. The size of a command is 2 + N words (name, argument types and N arguments), so the commands of a block are the following commands whose sizes add up to that value. This holds for every block of the game files.

The file is a list of `DefImage` and `Character` blocks (nothing else is found at the top level):

```
DefImage("HOS_L", 4)            ┐ model definition
    Image(1, "HOS_L.MT5")       ┘ 4 words
Character('AKMI', 31)           ┐ character
    Position(-300, 0, 0)        │ 31 words
    Angle(0, 0, 0)              │
    Human()                     │
    Flags("DISP", "NOMAPEV")    │
    MOTCLIP("HYPER")            │
    COLIOFF()                   │
    Position(-1005.3, 0, 93.56) │
    Image("HOS_L")              │
    CLOTH()                     ┘
```

A `DefImage` is usually right before the `Character` blocks that use it.

## STRG block

Holds the null terminated strings referenced by the command names and the string arguments.

## Commands

Arguments are written as `name:type`. Counts are over all the commands of the 646 CHRT files of the game. Meanings marked as *probably* are deduced from the values and the context, and the rest of the commands and arguments are unknown.

### Top level

| Command | Arguments | Count | Description |
|---------|-----------|------:|-------------|
| `DefImage` | `name:string, size:int` | 2120 | Defines a model named `name`, to be used by the `Image` commands of the characters. Its block always contains one `Image(1, "<file>.MT5")`, so `size` is always 4. `name` is usually the file name without extension, but not always (`GEOS5HG` → `GEOS5SHG.MT5`) |
| `Character` | `id:literal, size:int` | 2774 | A character or object of the scene, with a 4-character ID (`AKMI`, `DOOR`, `CAN1`, `BIKE`...). The block holds its properties. The same ID can appear in several files |

### Inside `DefImage`

| Command | Arguments | Count | Description |
|---------|-----------|------:|-------------|
| `Image` | `unknown:float, file:string` | 2120 | Model file of the definition, e.g. `HOS_L.MT5`. The model is a [MT5 / CHRM](MT5) file of the same [IPAC](IPAC) block (99.6% of the references found in the game files; the extension of the stored file can be `.chrm`). `unknown` is always 1 |

### Inside `Character`

The most common commands, from most to least used:

| Command | Arguments | Count | Description |
|---------|-----------|------:|-------------|
| `Image` | `name:string` | 2680 | Model of the character, the `name` of a `DefImage`. 98% of the names are defined in the same file; the rest (e.g. `KGSH501G`) are defined in other files |
| `Image` | `name:string, size:int` | 47 | Animated model: block with `Num` and optionally `Loop`, `Wait` and `Kind` (see below) |
| `Position` | `x:float, y:float, z:float` | 2708 | Position of the character in the scene. `y` is the height, usually 0. Characters can have 2 `Position` commands (401 characters): the first one is usually off the scene (`(0, 0, -1000)`, `(-300, 0, 0)`, `(0, -1000, 0)`...) |
| `Flags` | `flag:string` or `flag1:string, flag2:string` | 2608 | State of the character: `DISP` (displayed), `SLEEP` (*probably* inactive until something wakes it), `NOMAPEV` (*probably* without map events). The usual combinations are `DISP`, `SLEEP`, `DISP NOMAPEV` and `SLEEP NOMAPEV` |
| `Angle` | `y:float` or `x:float, y:float, z:float` | 2307 | Rotation in degrees (`0`, `90`, `-90`, `180`, `270`...). The short form has only the rotation around the vertical axis |
| `Object` | — | 1917 | The character is an object (doors, cans, boxes, bikes...) |
| `ColiOff` | — | 1898 | *Probably* disables the collision of the character |
| `ShadowOff` | — | 1553 | *Probably* disables the shadow of the character |
| `Human` | — | 557 | The character is a person. Never in the same block as `Object`; 310 characters have neither |
| `Range` | `range:float` | 543 | Unknown distance. Values from 0.6 to 1000 (0.6, 5, 30, 9, 1.5, 100...) |
| `Trans` | — or `unknown:float` | 364 | Unknown. One `Trans(0.5)`, the rest without arguments |
| `MOTCLIP` | `mode:string` | 324 | Only in `Human` characters: `HYPER` (321) or `HIGH` (8), once `NONE` |
| `RefBoard` | — | 301 | Unknown |
| `Shadow` | `type:int` | 267 | Shadow of the character: 3, 11, 13, 6 or 8 |
| `Coli` | `type:int` | 237 | Collision of the character: 8, 5, 4, 3, 6, 12, 15 or 11 |
| `Mode` | `mode:float` | 222 | Unknown: 2, 4, 0, 1 or 3 |
| `Size` | `x:float` or `x:float, y:float` or `x:float, y:float, z:float` | 191 | *Probably* size of the collision of objects (e.g. `(0.9, 0.9, 0.9)`, `(0.8, 0.8, 0.8)`) |
| `Scale` | `x:float, y:float, z:float` | 145 | Unknown, almost only in objects (e.g. `(1.5, 1.5, 1.5)`) |
| `Hand` | `left:string, right:string` or `left:string, right:string, unknown:float` | 118 | Models of the hands, e.g. `KAS_TL.MT5` and `KAS_TR.MT5`. Both can be the same model. `unknown` is 1 or 2 |
| `TinyMot` | — | 116 | Unknown, almost only in objects |
| `ProjectionTrans` | — | 113 | Unknown |
| `Height` | `height:float` | 113 | Unknown height: 0.02, 1.5, 1, -0.65 or 2.59 |
| `Face` | `file:string` | 93 | Model of the face, e.g. `KAS_F.MT5` |
| `Cloth` | — | 80 | Unknown, only in `Human` characters |
| `Level` | `level:float` | 68 | Unknown: 3, 0, 1 or 2 |
| `Bone` | `name:string` | 62 | Only in `Human` characters, always `AKI` |
| `Adjust` | 1 to 4 strings | 62 | Unknown, with the values `CHAR`, `MAP`, `FLOAT` and `NONE` (e.g. `Adjust("MAP", "CHAR")`) |
| `LineWalk` | — | 57 | Unknown |
| `Step` | `type:string` | 35 | Sound of the steps (*probably*), always `SOFT` |
| `Walk3D` | — | 34 | Unknown |
| `Osage` | — | 31 | Only in `Human` characters. *Probably* the character has hair or clothes with physics (*osage* is a pigtail) |
| `TinyGame` | — | 26 | Unknown, only in objects |
| `DummyChara` | — | 19 | Unknown, only in `Human` characters |
| `Contact` | `unknown:int` | 18 | Unknown, always 9 |
| `Radius` | `radius:float` | 18 | Unknown radius: 2 or 3 |
| `TailLamp` | — | 17 | *Probably* the tail lights of a vehicle |
| `Blur` | `file:string` | 12 | A model, e.g. `CHA_B.MT5` |
| `Fight` | `unknown:int` | 12 | Unknown, always 3 |
| `Player` | — | 11 | The character is the player (e.g. `AKIR`) |
| `CutMvShadow` | — | 11 | Unknown |

Other commands without arguments, used less than 11 times: `ForkLift`, `Clock`, `Patrol`, `Knife`, `Bicycle`, `Vehicle`, `Calendar`. Others with arguments: `Tel(unknown:float)` (or without arguments), `Vender(unknown:float)`, `Entry` (see below).

### Inside an animated `Image`

The animated `Image` holds an animation of the model, e.g. `Image("BFA0101G", 12)` (fire) or `Image("GRW1", 3)`.

| Command | Arguments | Count | Description |
|---------|-----------|------:|-------------|
| `Num` | `count:float` | 47 | *Probably* number of frames: 6, 4 or 20 |
| `Loop` | `unknown:float` | 19 | Always 0 |
| `Wait` | `unknown:float` | 19 | Always 1 |
| `Kind` | `kind:string` | 19 | Always `REPEAT` |

### Inside `Entry`

`Entry(unknown:float, size:int)` is a block with a `Position` and an `Angle`, found in the player character (`AKIR`). A character can have several, with `unknown` 0, 10 and 20; they are *probably* alternative places where the character appears.

## Example

A person, with the model, face and hands defined by the `DefImage` blocks of the same file:

```
Character('ASDA', 29)
    Position(-1300, 0, 1000)
    Angle(0, 0, 0)
    Human()
    Flags("DISP", "NOMAPEV")
    MOTCLIP("HYPER")
    Image("KAS_L")
    Face("KAS_F.MT5")
    Hand("KAS_TL.MT5", "KAS_TR.MT5")
```

An object:

```
Character('CAN1', 24)
    Position(0, 0, 0)
    Angle(0)
    Image("GEOS5HG")
    Flags("DISP")
    Object()
    RefBoard()
    ShadowOff()
    CutMvShadow()
    ColiOff()
```
