# Resources

Databases used by the application. They are plain text so they can be edited and reviewed in a diff.

## FilenameDatabase.txt

Paths of the files of the TAD archives of the PC version, one per line (see its header comment).

## Shenmue1Maps.tsv

Maps of Shenmue I, from the [Shenmue-Mods Knowledgebase](https://github.com/Shenmue-Mods/Knowledgebase) (`Shenmue_I/Map_Names_IDs.md`). Tab-separated, with a header row.

| Column   | Description |
|----------|-------------|
| `id`     | Map ID, the name of its folder (`scene/0N/<id>`) in uppercase |
| `ipac`   | For the maps stored inside another one: the name of its IPAC block (e.g. `ALPHA` in `MFSY`). Empty otherwise |
| `scenes` | Scenes (Dreamcast discs) whose folder has the map, from the game files, separated by commas |
| `name`   | Name of the map |
| `area`   | Area of the map (Dobuita, Harbor...) |
| `notes`  | Entry points and other notes of the Knowledgebase |

## Shenmue1Characters.tsv

Characters of Shenmue I. Tab-separated, with a header row.

The rows up to Yutaka Koizumi come from the [Shenmue-Mods Knowledgebase](https://github.com/Shenmue-Mods/Knowledgebase) (`Shenmue_I/Characters_Names_IDs_Models.md`), with three errors fixed from the game files (`SYM` → `ISYM`, `JME_` → `JME_L`, `OGM_` → `OGM_L`). The rows after it are the characters of `scene/0N/stream/humans.idx` missing from that page; its vehicles and objects (cars, doors...) are left out. Those whose files contain the ID or the own voices of a named character are versions of it and have its name; the passers-by have the English translation of their role in `jpname` (`Worker`, `Young woman`...); the others have no name.

| Column   | Description |
|----------|-------------|
| `id`     | Character ID (4 characters). Empty for some characters of the Knowledgebase |
| `model`  | Model of the character (e.g. `HOS_L`). Two rows have two models, separated by a comma |
| `name`   | Name of the character |
| `jpname` | Name of the speaker in the Japanese subtitles of its lines (the SCNF of its PKS in `humans.afs`): a name, or a role for the passers-by (`作業員`, worker; `若い女`, young woman...). Empty when it has no lines |
| `area`   | Area where it is found, from the Knowledgebase |
| `place`  | Place or occupation, from the Knowledgebase |
| `maps`   | Maps (`scene/0N/<map>`) whose character scripts, cutscenes, `cycleman.bin` or scene program use the ID, from the game files, separated by commas. Empty when no map uses it |
| `scenes` | Scenes of those maps, separated by commas |
| `wiki`   | Page of the character in the Neoseeker wiki |

The files are UTF-8 (`jpname` has Japanese text).
