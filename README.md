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
- Play the music and sound effects of `SND` (DTPK) sound banks as the Dreamcast does: the sequences drive the instruments of the bank on an emulation of the voices and the reverb (DSP) of the AICA sound chip. The samples can also be played alone, with their waveform and loop.
- Play `AUTH` cutscenes: the list of their commands (motions, voices and sound effects), the camera and the characters in 3D, seen from a free camera or from the camera of the cutscene, and a timeline.
- View the `mapinfo.bin` files of the maps: the models of the map with its collisions and areas, lights, cameras and characters, seen from a free camera or from the cameras of the map.

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
  - Sound banks (`.snd`, e.g. `scene/01/sound/bgm013.snd` or `scene/01/sound/a1_neko1.snd`) list their sequences, music and effects, and the selected one is played with a level meter per channel. `Tab` (or the buttons above the list) switches to the list of samples, played alone with their waveform, repeating their loop, at the rate given by the instruments of the bank (or at 11025, 22050 or 44100 Hz). `Space` plays and pauses, the up and down arrows select the previous and next item, and `Home` plays it again from the start. With the samples, the left and right arrows go back and forward 1 second, `L` turns the loop on and off, and clicking or dragging on the waveform seeks.
  - Cutscenes (`.auth`, extracted from the `.pks` of the scenes, e.g. `scene/01/d000/drauth.pks`) are played: the list of commands, the characters (as cylinders, with the direction they face, their voices and sounds, and their motion) and the camera in 3D, and a timeline with the motions, voices, sounds and camera cuts. The names of the motions are read from the `.motn` files of the same folder. `Space` plays and pauses, the left and right arrows go to the previous and next command, `Home` goes to the start, and clicking a command or clicking or dragging on the timeline seeks. `V` (or the buttons below the scene) switches between the camera of the cutscene and a free camera, controlled as in the model viewer.
  - Map information (`mapinfo.bin`, e.g. `scene/01/dcbn/mapinfo.bin`) is shown in 3D: the models of the map (the `MAP*.mt5` files of its folder and of its subfolders, so extract the `.pkf` files of the map first, e.g. `mpk00.pkf`), and over them the collisions and areas (`COLI`, `HGHT`, `EVNT`, `UNDU`, `SOND` and `PROP`), the lights, the cameras and the characters. The list on the left shows the information of the file and its cameras, lights and characters: clicking a camera shows the map from it (animated cameras are played in a loop), and clicking a light or a character looks at it. `V` switches between the camera of the map and a free camera, controlled as in the model viewer, and the buttons below the scene show and hide each layer.
  - Files can also be opened from the command line: `Shenmue <file>`.
- **Save file**: save the texture being shown as an image.

## Acknowledgements

- [Raymonf](https://github.com/Raymonf) of [Wulinshu](https://wulinshu.com/) for the TAD hash database (`resources/FilenameDatabase.json`) used to restore the original filenames.
- [ShenmueDKSharp](https://github.com/philyeahz/ShenmueDKSharp) for the documentation of some of the file formats.
