# CHRT — Character script

A CHRT file is a script made of commands (e.g. which character to place and where). The commands are self-describing: each one stores the type of its arguments, so they can be decoded without knowing them in advance. CHRT files are usually found inside [IPAC](IPAC) blocks.

- **Extension:** `.chrt`
- **Magic:** `CHRS` (`43 48 52 53`)
- **Compression:** the whole file can be [gzipped](Home#compression)
- **Source:** [`src/chrt.c`](https://github.com/EmulRyo/Shenmue/blob/master/src/chrt.c)

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

### Argument types (3 bits)

| Value | Type    | Meaning of the 4-byte value                                    |
|------:|---------|----------------------------------------------------------------|
| 0     | End     | No more arguments                                              |
| 1     | Float   | 32-bit float                                                   |
| 2     | Literal | 4 characters stored in place (e.g. a character ID like `AKMI`) |
| 3     | String  | Offset of a null terminated string, **relative to the argument** |
| 4     | Int     | 32-bit unsigned integer                                        |

Other values are unknown and are printed raw.

## STRG block

Holds the null terminated strings referenced by the command names and the string arguments.

## Example

Opening a CHRT file prints its commands as a script:

```
Character('AKMI', 31)
Position(-300, 0, 0)
```
