# Shenmue

Tool to explore and extract the files of Shenmue I & II (PC version). It is written in C with [raylib](https://www.raylib.com/) and [raygui](https://github.com/raysan5/raygui).

## Features

- Extract the `.tad`/`.tac` archives of the game, restoring the original filenames from `resources/FilenameDatabase.json`.
- Extract the files contained in `AFS`/`IDX`, `PKF` and `PKS` containers, detecting the extension of every file from its magic number.
- Decompress gzip compressed files automatically.
- View `PVR` and `SPR` textures and save them as PNG, JPG or BMP.
- Decode the commands of `CHRT` character scripts.

The file formats are described in the [wiki](https://github.com/EmulRyo/Shenmue/wiki) (source pages in [`wiki/`](wiki)).

## How to build

The repository uses git submodules ([cJSON](https://github.com/DaveGamble/cJSON) and [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended)), so clone it with `--recursive`. raylib 5.5 is downloaded by CMake if it is not installed.

```
git clone --recursive https://github.com/EmulRyo/Shenmue.git
cd Shenmue
cmake -B build && cmake --build build
```

If the repository was cloned without `--recursive`, run `git submodule update --init`.

## Usage

Run the program from the repository folder (it reads `resources/FilenameDatabase.json`).

- **Extract TAD files**: select the folder that contains the `.tad`/`.tac` files of the game. All of them are extracted to the `content` folder.
- **Open files**: open one of the supported files. Files can also be dragged and dropped onto the window.
  - Textures (`.pvr`, `.spr`) are shown in the window.
  - Containers (`.afs`, `.idx`, `.pkf`, `.pks`) are extracted to a folder with the name of the file, next to it.
  - Scripts (`.chrt`) are decoded to the console.
- **Save file**: save the texture being shown as an image.
