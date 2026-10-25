# Shenmue

Tool to explore and extract the files of Shenmue I & II (PC version). It is written in C with [raylib](https://www.raylib.com/) and [raygui](https://github.com/raysan5/raygui).

## Features

- Extract the `.tad`/`.tac` archives of the game, restoring the original filenames from `resources/FilenameDatabase.json`.
- Extract the files contained in `AFS`/`IDX`, `PKF` and `PKS` containers, detecting the extension of every file from its magic number.
- Decompress gzip compressed files automatically.
- View `PVR` and `SPR` textures and save them as PNG, JPG or BMP.
- Decode the commands of `CHRT` character scripts.
- Print the dialogue lines of `SCNF` files: voice, speaker, duration and subtitle (converted from EUC-JP), and disassemble the script that chooses them.
- View `MT5`/`CHRM` models in 3D, with their textures, and print their node tree, meshes and textures.
- Play `XWM` (xWMA) audio clips, the voices and music of the game, with their waveform (Windows only: they are decoded with the WMA decoder of Media Foundation).
- Play the samples of `SND` (DTPK) sound banks, the instruments and sound effects of the music and the areas, with their waveform and loop.

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
  - Dialogues (`.scnf`, or `.bin` files that start with `SCNF`, e.g. `scene/01/stream/humans/AKMI/AKMI.bin`) are printed to the console.
  - Models (`.mt5`, `.chrm`) are shown in a 3D viewer and described in the console. Drag with the left button to rotate, with the right or middle button to pan, and use the wheel to zoom; `R` resets the camera, `W` toggles the wireframe and `C` toggles drawing the back faces. Models made of animation frames (e.g. the birds of `model/object`) are played; `Space` pauses them and the left and right arrows show the previous and next frame. The textures of a `.chrm` file are read from its folder, so extract the `.pkf` of the scene first (e.g. `atrk.pkf` for `atrk/ATRS500G.chrm`).
  - Audio clips (`.xwm`, extracted from the audio `.afs` files, e.g. `scene/01/stream/pc/eng/01beda.afs`) are played with their waveform. `Space` plays and pauses, the left and right arrows go back and forward 1 second, `Home` goes to the start, and clicking or dragging on the waveform seeks.
  - Sound banks (`.snd`, e.g. `scene/01/sound/a1_neko1.snd`) list their samples, and the selected one is played with its waveform, repeating its loop. The banks don't store the sample rate, so it is chosen below the waveform (11025, 22050 or 44100 Hz; 22050 by default). `Space` plays and pauses, the up and down arrows select the previous and next sample, the left and right arrows go back and forward 1 second, `Home` goes to the start, `L` turns the loop on and off, and clicking or dragging on the waveform seeks. The sequences (the songs) are not played.
  - Files can also be opened from the command line: `Shenmue <file>`.
- **Save file**: save the texture being shown as an image.

## Acknowledgements

- [Raymonf](https://github.com/Raymonf) of [Wulinshu](https://wulinshu.com/) for the TAD hash database (`resources/FilenameDatabase.json`) used to restore the original filenames.
- [ShenmueDKSharp](https://github.com/philyeahz/ShenmueDKSharp) for the documentation of some of the file formats.
