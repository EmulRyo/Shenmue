# Shenmue

Tool to explore and extract the files of Shenmue I & II (PC version). It is written in C with [raylib](https://www.raylib.com/) and [raygui](https://github.com/raysan5/raygui).

## Features

- Extract the `.tad`/`.tac` archives of the game, restoring the original filenames from `resources/FilenameDatabase.json`.
- Extract the files contained in `AFS`/`IDX`, `PKF` and `PKS` containers, detecting the extension of every file from its magic number.
- Decompress gzip compressed files automatically.
- View `PVR`, `SPR` and `DDS` textures and save them as PNG, JPG or BMP. The DDS textures that the game stores with the extension `.png` or `.tga` are opened too, also those without header (their format and size are guessed from the size of the file), the cube maps are shown as a cross of their 6 faces, and the textures of `textureoverride`, stored upside down, are flipped.
- Decode the commands of `CHRT` character scripts.
- Print the dialogue lines of `SCNF` files: voice, speaker, duration and subtitle (converted from EUC-JP), and disassemble the script that chooses them.
- View `MT5`/`CHRM` models in 3D, with their textures, print their node tree, meshes and textures, and export them to glTF (`.glb`) or OBJ.
- Play `XWM` (xWMA) audio clips, the voices and music of the game, with their waveform (Windows only: they are decoded with the WMA decoder of Media Foundation).
- Play the music and sound effects of `SND` (DTPK) sound banks as the Dreamcast does: the sequences drive the instruments of the bank on an emulation of the voices and the reverb (DSP) of the AICA sound chip. The samples can also be played alone, with their waveform and loop.
- Play `AUTH` cutscenes: the list of their commands (motions, voices and sound effects), the camera and the characters in 3D, animated by their motions, seen from a free camera or from the camera of the cutscene, and a timeline.
- Play the `MOTN` motions of the characters on their models, and export a model with its skeleton and the motions as animations to glTF (`.glb`).
- View the `mapinfo.bin` files of the maps: the models of the map and its doors, with its collisions and areas, lights, cameras and characters, seen from a free camera or from the cameras of the map.

The file formats are described in the [wiki](https://github.com/EmulRyo/Shenmue/wiki) (source pages in [`wiki/`](wiki)).

## How to build

The repository uses git submodules ([raylib](https://github.com/raysan5/raylib), [cJSON](https://github.com/DaveGamble/cJSON) and [nativefiledialog-extended](https://github.com/btzy/nativefiledialog-extended)), so clone it with `--recursive`.

```
git clone --recursive https://github.com/EmulRyo/Shenmue.git
cd Shenmue
cmake -B build && cmake --build build
```

If the repository was cloned without `--recursive`, run `git submodule update --init`.

## Usage

Run the program from the repository folder (it reads `resources/FilenameDatabase.json`).

- **Extract TAD files**: select one or several `.tad` files of the game (each next to its `.tac`), then the output folder. The files of all of them are extracted to the same tree in that folder, at their path in the game (`scene/01/...`). When it ends, the status bar shows how many files were extracted and where: click it to open the folder, right click to close it.
- **Open files**: open one of the supported files. Files can also be dragged and dropped onto the window.
  - Textures (`.pvr`, `.spr`, `.dds`, `.png`, `.tga`) are shown in the window, fitted to it or at their real size (`Z` switches between both). The wheel zooms on the pixel under the mouse and dragging with the left or middle button moves the image; the status bar shows the zoom and the pixel under the mouse. The fit or 1:1 mode is kept when another image is opened. `V` flips the image vertically (on when opening a texture of `textureoverride`). For a DDS texture without header that fits several formats or sizes, the toolbar shows all of them, the most likely selected (`L` selects the next one).
  - Containers (`.afs`, `.idx`, `.pkf`, `.pks`) are extracted to a folder with the name of the file, next to it.
  - Scripts (`.chrt`) are decoded to the console.
  - Dialogues (`.scnf`, or `.bin` files that start with `SCNF`, e.g. `scene/01/stream/humans/AKMI/AKMI.bin`) are printed to the console.
  - Models (`.mt5`, `.chrm`) are shown in a 3D viewer and described in the console. Drag with the left button to rotate, with the right or middle button to pan, and use the wheel to zoom; `R` resets the camera, `W` toggles the wireframe and `C` toggles drawing the back faces. Models made of animation frames (e.g. the birds of `model/object`) are played; `Space` pauses them and the left and right arrows show the previous and next frame. The textures of a `.chrm` file are read from its folder, so extract the `.pkf` of the scene first (e.g. `atrk.pkf` for `atrk/ATRS500G.chrm`). **Export** (`Ctrl+S`) saves the model as it is shown (the current frame of the animated ones) to a binary glTF file (`.glb`, with the textures inside, and the transparency and texture wrapping of each material) or to an OBJ file (with an `.mtl` file and the textures as `.png` files next to it). The glTF file has the skeleton of the model: a joint per node, in the pose of the file, that moves its vertices.
  - Audio clips (`.xwm`, extracted from the audio `.afs` files, e.g. `scene/01/stream/pc/eng/01beda.afs`) are played with their waveform. `Space` plays and pauses, the left and right arrows go back and forward 1 second, `Home` goes to the start, and clicking or dragging on the waveform seeks.
  - Sound banks (`.snd`, e.g. `scene/01/sound/bgm013.snd` or `scene/01/sound/a1_neko1.snd`) list their sequences, music and effects, and the selected one is played with a level meter per channel. `Tab` (or the buttons above the list) switches to the list of samples, played alone with their waveform, repeating their loop, at the rate given by the instruments of the bank (or at 11025, 22050 or 44100 Hz). `Space` plays and pauses, the up and down arrows select the previous and next item, and `Home` plays it again from the start. With the samples, the left and right arrows go back and forward 1 second, `L` turns the loop on and off, and clicking or dragging on the waveform seeks.
  - Cutscenes (`.auth`, extracted from the `.pks` of the scenes, e.g. `scene/01/d000/drauth.pks`) are played: the list of commands, the characters (as cylinders, with the direction they face, their voices and sounds, and their motion) and the camera in 3D, and a timeline with the motions, voices, sounds and camera cuts. The motions are read from the `.motn` files of the same folder (or, if it has none, of the other folders of its family, e.g. `djhn_mot` and `djhn_01` for `djhn_07`), from the `m_*.bin` files of the map with the name of that folder (e.g. `scene/01/yq14` for `scene/01/d000/yq14`) and from the common motions of the game (`motion/motion.bin`), and the characters whose model is a body are animated by them. `Space` plays and pauses, the left and right arrows go to the previous and next command, `Home` goes to the start, and clicking a command or clicking or dragging on the timeline seeks. `V` (or the buttons below the scene) switches between the camera of the cutscene and a free camera, controlled as in the model viewer.
  - Motions (`.motn`, extracted from the `.pks` of the scenes, e.g. `scene/01/d000/auth.pks`, and the common motions `motion/motion.bin`) are played on the body of a character: Ryo (`scene/01/model/chara/ykb_m.mt5`) for his motions (`AKI_*`), otherwise the first body of the models of the same folder; the list on the left shows the motions and the models, and clicking one selects it. `Space` plays and pauses, the left and right arrows go to the previous and next frame, the up and down arrows to the previous and next motion, `Home` goes to the start, and clicking or dragging on the timeline seeks. `G` makes the camera follow the character and `T` shows the positions the motion gives to the head, the hands and the feet. **Export** (`Ctrl+S`) saves the model shown to a binary glTF file (`.glb`) with its skeleton and, if it is the body of a character, every motion of the file that can pose it as an animation with the name of the motion, at 30 frames per second.
  - Map information (`mapinfo.bin`, e.g. `scene/01/dcbn/mapinfo.bin`) is shown in 3D: the models of the map (the `MAP*.mt5` files of its folder and of its subfolders, and those of the `.pkf`/`.pks` files of the map that aren't extracted, e.g. `mpk00.pkf`, which are read directly) and its doors (placed by the door table of the scene program, with their models from the same files), and over them the collisions and areas (`COLI`, `HGHT`, `EVNT`, `UNDU`, `SOND` and `PROP`), the lights, the cameras and the characters. The list on the left shows the information of the file and its cameras, lights, characters and doors: clicking a camera shows the map from it (animated cameras are played in a loop), and clicking a light, a character or a door looks at it. `V` switches between the camera of the map and a free camera, controlled as in the model viewer, and the buttons below the scene show and hide each layer.
  - The options of the 3D viewers (the wireframe, the back faces, the fly mode, the camera of the cutscene, the labels, the sleeping characters and the layers of the map) are kept when another file is opened.
  - In the lists of the viewers, the items that couldn't be loaded (a sample that can't be decoded, a door or a character whose model is missing or invalid) are marked in red with a warning icon and the reason.
  - Files can also be opened from the command line: `Shenmue <file>`.
- **Save file**: save the texture being shown as an image.

## Acknowledgements

- [Raymonf](https://github.com/Raymonf) of [Wulinshu](https://wulinshu.com/) for the TAD hash database (`resources/FilenameDatabase.json`) used to restore the original filenames.
- [ShenmueDKSharp](https://github.com/philyeahz/ShenmueDKSharp) for the documentation of some of the file formats.
