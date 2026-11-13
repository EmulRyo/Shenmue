# SCN — Scene program (bytecode)

A SCN file is the program of a map, as a bytecode: the same structure as the [`SCN3` chunk](MAPINFO#scn3--scene-program) of the [MAPINFO](MAPINFO) files, stored alone. The file is that chunk, header included.

The Shenmue I data has 136 SCN files in the `scenario/` folder of the [TAD / TAC](TAD-TAC) archive, one per `mapinfo.bin`: `scenario/actNN_<map>.scn` is the program of `scene/NN/<map>/mapinfo.bin` (44 `act01_*`, 42 `act02_*` and 50 `act03_*`). E.g. `scenario/act01_d000.scn` is the program of `scene/01/d000`. 133 of them are different.

- **Extension:** `.scn`
- **Magic:** `SCN3`
- **Size:** 144 (`act01_mk99`, `act01_mo99`) to 416,616 bytes (`act03_mfsy`)
- **Compression:** none in the game files

## Relation with the SCN3 chunk of the map

All the SCN files are version `0x100` (bytecode). In the `mapinfo.bin` files, the `SCN3` chunk is native SH-4 code (version `0x20000`) in 119 maps and bytecode in 17:

| SCN3 chunk of the map | Maps | SCN file |
|-----------------------|-----:|----------|
| Bytecode (`0x100`)    | 13   | Identical to the chunk, byte for byte (`0000`, `betd` of the 3 folders, `mf99`, `mk99`, `mo99`, `yd8s`, `gmct`, `arar` and `door` of `scene/03`, `mc5q`, `nbik`) |
| Bytecode (`0x100`)    | 4    | Slightly different, with a size close to that of the chunk: `act01_jd99` and `act01_ju99` (16,256 bytes, chunk of 16,224), `act01_yq14` (19,004, chunk of 19,284), `act02_arar` (24,332, chunk of 24,416) |
| SH-4 code (`0x20000`) | 119  | The same program as a bytecode: about half the size of the chunk (`act01_d000`: 332,932 bytes, chunk of 729,128) |

For the 119 maps with SH-4 code, the data that follows the code is almost the same in both versions: 32,028 of the 32,396 strings of the data of the SCN files are also in the data of the chunks (paths, model, sprite and sound names, and debug messages such as `Daily practice %d: Check: in range, setting training flag to true`). On average, the first 48% of the data is identical byte for byte. The [door table](MAPINFO#door-table) and the strings of the [jukebox](MAPINFO#jukebox) are also in the data of the SCN files.

The code of both versions has the same functions, in the same order: the function *i* of the bytecode is the function *i + 1* of the SH-4 code, whose function 0 is the code at `codeStart` (resume and yield, which the bytecode doesn't need). The 119 pairs have 23,864 functions, all paired. In 23,668 of them (99.2%) the calls to the program's own functions, to the [functions of the game](MAPINFO#functions-of-the-game) and to the services of the context are the same, in the same order. The SCN files are a slightly different revision of the same programs: the other 196 functions have a few more or fewer calls, e.g. calls to `Print` (`0x000`) with debug messages, or a fourth `VoiceArchive` (`0x0CA`) in a function that `daza`, `dbhb`, `dbyo`, `dcha` and other maps share. The bytecode makes 255,303 calls to the game, against 255,148 for the SH-4 code.

The arguments of the paired calls are the same when both versions give them as constants or strings of the data (551,861 arguments), except 184: e.g. in `scene/01/d000` some phone numbers have 6 digits in the SH-4 code (`"615647"`) and 5 in the bytecode (`"61564"`), and some floats are 1.0 in one version and -1.0 in the other.

## Layout

```
+---------------------------+ 0x00
| Header (0x30 bytes)       |
+---------------------------+ 0x30 (codeStart)
| Bytecode                  |  entry point somewhere inside
+---------------------------+ dataStart
| Data                      |  floats, colors, tables, strings
+---------------------------+ end
| Relocations (8 × N)       |
+---------------------------+
| End of relocations (8)    |  8 null bytes
+---------------------------+ file size
```

All the offsets are relative to the start of the file.

## Header (0x30 bytes)

| Offset | Size | Type    | Name        | Description |
|-------:|-----:|---------|-------------|-------------|
| 0x00   | 4    | char[4] | `id`        | `SCN3` |
| 0x04   | 4    | uint32  | `size`      | Size of the file. Always equal to the file size |
| 0x08   | 4    | uint32  | `version`   | Always `0x100` (bytecode) |
| 0x0C   | 4    | uint32  | `entry`     | Entry point, inside the bytecode. Odd in 75 files; `0x30` (the start of the bytecode) in 3 |
| 0x10   | 4    | uint32  | `dataStart` | Start of the data, end of the bytecode |
| 0x14   | 4    | uint32  | `end`       | End of the data, start of the [relocations](#relocations). `size − 8` in the 39 files without relocations |
| 0x18   | 4    | uint32  | `variables` | Size of the variables of the program: an area of the game, not part of the file (0 to `0xF50`, 0 in 11 files). The highest variable used by the bytecode ends at most there: within 16 bytes in 116 files |
| 0x1C   | 4    | uint32  | —           | Always 0 |
| 0x20   | 4    | uint32  | `codeStart` | Start of the bytecode. Always `0x30` |
| 0x24   | 4    | uint32  | `end2`      | Always equal to `end` |
| 0x28   | 8    | —       | —           | Always 0 |

## Bytecode

From `codeStart` to `dataStart`: the instructions of a stack machine, 1 to 5 bytes long, followed in 112 files by `0xFF` bytes up to `dataStart`. They are those read by the interpreter of `1ST_READ.BIN` (PAL), at `0x0C08CD04`. The 136 files have 4,349,410 instructions, and no other byte.

The machine has a stack of 32-bit values (`sp`), a frame pointer (`fp`) and a result register, set by the calls and by the operator `0x80`, which the conditional branch tests. A call pushes the return address and `fp`, and sets `fp` to `sp`: the function finds its arguments at `fp+8`, `fp+12`..., the first argument first, and makes room for its local variables, at negative offsets, with `sp -= n`. As in the [SH-4 programs](MAPINFO#context-and-calls), the caller pushes the arguments from the last one to the first one, and drops them after the call (`sp += 4 × count`).

### Encoding

The two highest bits of the first byte give the group of the instruction:

| First byte  | Group | Instruction |
|-------------|-------|-------------|
| `0x00–0x3F` | Control | Bits 4-5: size of the operand that follows, 0, 1, 2 or 4 bytes (signed, little-endian); bits 0-3: the operation |
| `0x40–0x7F` | Push    | The same: bits 4-5, size of the operand; bits 0-3, what is pushed |
| `0x80–0xA6` | Operator | No operand |
| `0xA7–0xFF` | — | Not an instruction (skipped by the interpreter) |

For example `1B 02` is the control operation `0xB` with an operand of 1 byte (`0x02`), and `73 43 00 00 00` the push `3` with an operand of 4 bytes (`0x43`).

### Control (`0x00–0x3F`)

| Op  | Count   | Instruction |
|----:|--------:|-------------|
| 0   | 24,400  | Return. At the first function of a thread, ends the thread |
| 1   | 44,135  | Call the function at the offset `n` of the program |
| 2   | 0       | Call the function at the address popped |
| 3   | 356,612 | `sp += n`: drops `n / 4` values (`n > 0`), or makes room for local variables (`n < 0`) |
| 4   | 0       | Jump to the offset `n` of the program |
| 5   | 123,394 | Jump `n` bytes from the end of the instruction |
| 6   | 151,360 | The same if the result is 0 |
| 7   | 32,209  | Number of arguments of the next operation `0xC`, always just before it |
| 8   | 576     | Calls the function of `ctx[0x2C]` with the ID 1 and the address of the offset `n` of the program (in its data), and yields |
| 9   | 0       | Function `n` of a table of the interpreter, with the stack |
| 10  | 0       | Function `n + 1` of that table, with the first 4 values of the stack |
| 11  | 260,948 | Function of the game with the ID `n`: the same IDs and arguments as `ctx[0x28]` of the [SH-4 programs](MAPINFO#functions-of-the-game) |
| 12  | 32,209  | Function of `ctx[0x2C]` with the ID `n`, and yields |
| 13  | 3,462   | Function of `ctx[0x30]` with the ID `n` |
| 14, 15 | 0    | Nothing |

The operations 11 to 13 put the value returned in the result register.

The interpreter (`0x0C08CD04`) keeps the address of the next instruction, `sp`, `fp` and the result in registers. A call (1) pushes the return address and `fp`, sets `fp` to `sp` and jumps; the return (0) sets `sp` to `fp`, pops `fp` and the return address, and ends the thread when the `fp` popped is 0: the first function of a thread is run as if called with `fp` equal to 0. The conditional branch (6) tests the result register.

### Push (`0x40–0x7F`)

| Op  | Count     | Pushes |
|----:|----------:|--------|
| 0   | 1,082,267 | The constant `n` (e.g. `70 41 4B 49 52`: the ID `AKIR`) |
| 1–3 | 73,278    | The address of the offset `n` of the program: a function (8,182) or a variable or string of the data (65,096). The files only use 3 |
| 4–7 | 0         | The byte, halfword or word at `n` of a variable area of the game, or its address (4, 5, 6, 7) |
| 8–11 | 6,182, 692, 43,287, 84,700 | The byte, halfword or word at the offset `n` of the variables of the program, or its address. The variables are an area of the game whose size is the field `variables` of the header, not the data of the file: the strings and tables of the data are pushed with the operations 1–3 |
| 12–15 | 11,678, 4,635, 364,701, 290,912 | The byte, halfword or word at `fp+n` (a local variable or an argument), or its address. The local variables are below `fp` (`n` negative) and the arguments above: the first one at `fp+8`, the next ones every 4 bytes |

The bytes and halfwords are signed.

### Operators (`0x80–0xA6`)

They pop their operands from the stack (the second one first) and push their result. The comparisons push -1 when they are true and 0 when false.

| Byte | Operator | Count | Byte | Operator | Count |
|------|----------|------:|------|----------|------:|
| `80` | Pop the result register | 395,709 | `94` | `*` | 46,947 |
| `81` | Push the result register | 86,547 | `95` | `/` | 608 |
| `82`–`84` | Load the byte, halfword or word at the address popped | 6,153, 523, 70,797 | `96` | `%` | 316 |
| `85`–`87` | Store a byte, halfword or word: the value popped at the address popped, then push the value | 26,093, 4,010, 230,629 | `97` | `<<` (arithmetic) | 29,618 |
| `88` | `==` | 122,899 | `98` | `>>` (arithmetic) | 1,427 |
| `89` | `!=` | 7,884 | `99`–`9C` | Nothing | 0 |
| `8A` | `<` | 3,398 | `9D` | Integer to float | 7,716 |
| `8B` | `<=` | 1,791 | `9E` | Float to integer | 2,652 |
| `8C` | `>` | 2,752 | `9F`–`A2` | Float `<`, `<=`, `>`, `>=` | 3,863, 2,247, 2,345, 3,662 |
| `8D` | `>=` | 3,779 | `A3` | Float `+` | 10,247 |
| `8E` | `~` (not) | 405 | `A4` | Float `-` | 13,326 |
| `8F`–`91` | `&`, `|`, `^` | 27,490, 36,352, 427 | `A5` | Float `*` | 11,751 |
| `92`, `93` | `+`, `-` | 181,321, 6,032 | `A6` | Float `/` | 6,057 |

### Compiled code

The bytecode is compiled from C-like code, with the same patterns in all the files:

| Construct | Instructions |
|-----------|--------------|
| Function | `sp -= n` (room for its local variables, `13 00` if none) ... `ret`. A function starts after the `ret` of the previous one |
| Statement | The expression, then `80` (pop the result), e.g. an assignment: `push &variable`, the value, `store.l`, `80` |
| Call | The arguments from the last one to the first one, the call, `sp += 4 × count`, then `81` (push the result) if its value is used |
| Return of a value | The value, `80`, then `ret` or a branch to the final `ret` of the function |
| `if` | The condition, `80`, `bz` past the block. With an `else`, the first block ends with a branch past the second one |
| `while` | At the start of the loop, the condition, `80` and `bz` past the loop; the body ends with a branch back to the start. `while (1)` tests `push 1` |
| `do ... while` | The body, then the condition, `80` and `bz` back to the start of the body: the loop goes on while the condition is 0 |
| Array | The address of the array plus the index shifted by the size of the elements (`<< 2` for words), then a load or a store |
| Bit field | `(store) & mask`, then `80` |

In the 24,400 functions of the 136 files, every branch is part of one of these blocks (or a `break` or `continue` of a loop) except 12, in 9 functions. The values left on the stack are always those that the next instructions use: no value is left on it at the target of a branch.

### Example

The program of `act01_mk99.scn` (from `0x30`, its entry point) starts a thread, as the [function of the game](MAPINFO#functions-of-the-game) `0x002` with the arguments `(0x43, 1, 'AKIR')`: the function at `0x43`, its number of arguments, and that argument.

```
0030  13 00           sp += 0
0032  70 41 4B 49 52  push 'AKIR'
0037  50 01           push 1
0039  73 43 00 00 00  push &0x43
003E  1B 02           game 0x002
0040  13 0C           sp += 12
0042  00              ret
0043  13 00           sp += 0
0045  50 01           push 1
0047  5E 08           push.l [fp+8]      ; 'AKIR'
0049  1B 1F           game 0x01F
...
```

The values of the code relocations (`0x2402`) are always the start of a function, and every jump and branch goes to an instruction.

## Data

From `dataStart` to `end`: floats, colors, tables of offsets and null terminated strings, as in the [SCN3 chunk](MAPINFO#scn3--scene-program). The data of 129 files contains the values `20.0, 800.0, 1.0, 0xFFFFFFFF`, the same as the default [fog](MAPINFO#fog--fog).

## Relocations

After the data, 97 files have a list of 8-byte entries (1 to 181), sorted by offset. Each entry is the offset of a 32-bit value of the data that is itself an offset in the file: *probably* the values that must be turned into addresses when the program is loaded. The list ends with 8 null bytes, which are the only bytes after `end` in the other 39 files. There are 4,265 entries in all.

| Offset | Size | Type   | Name     | Description |
|-------:|-----:|--------|----------|-------------|
| 0x00   | 4    | uint32 | `type`   | `0x2402`: the value is an offset in the bytecode (3,797 entries; e.g. tables of functions). `0x1402`: the value is an offset in the data (468 entries; e.g. the `name` of the models of the [door table](MAPINFO#door-model-16-bytes)) |
| 0x04   | 4    | uint32 | `offset` | Offset of the value, always inside the data (`dataStart` to `end`) |

The `SCN3` chunks of version `0x100` of the `mapinfo.bin` files have the same list: in `scene/03/door` the list has 49 entries, and the other 16 chunks end with only the 8 null bytes.
