# Shenmue

## Build and tests

The build folder is `build/`, already configured with the Visual Studio 17 2022 generator (multi-config, so `--config` is needed). `SHENMUE_CONTENT` points to `content/Shenmue1`, the extracted files of Shenmue I. Don't reconfigure or create other build folders.

- Build the application: `cmake --build build --config Release --target Shenmue` → `build/Release/Shenmue.exe`. Use Release: the emulation of the programs is several times slower in Debug.
- MSBuild is verbose: filter the output, e.g. `cmake --build build --config Release --target Shenmue 2>&1 | grep -E " error | warning C"`.
- If the link fails because the executable is in use, stop the running instance: `Get-Process Shenmue -ErrorAction SilentlyContinue | Stop-Process -Force`.
- Build the tests: `cmake --build build --config Release --target ShenmueTests`.
- While iterating, run only the affected test: `ctest --test-dir build -C Release -R cutscene --output-on-failure`. The whole set (`ctest --test-dir build -C Release -j 16 --output-on-failure`) takes about 70 s: run it once, at the end.
- Tests (`tests/CMakeLists.txt`): `mapinfo_chunks scn3_chunks scn_files scn_pairs sh4_encodings sh4_code sh4_program sh4_listing game_functions bc_encodings bc_files scn_pair decompile scn_data vm display cutscene`. The longest are `display`, `cutscene`, `scn_data` and `vm`. A test can also be run directly, to see all its output: `build/tests/Release/ShenmueTests.exe content/Shenmue1 <test>`.
- A test that only checks that the output doesn't change: run it before and after the change and compare the outputs.

## Running the application

- `build/Release/Shenmue.exe <file>` opens the file at start (e.g. a `mapinfo.bin` or an `.auth` of `content/Shenmue1`); the log goes to stdout/stderr.
- There is no computer-use: to see the window, drive it with `tools/ui.ps1` (from PowerShell): `start <file> [log]`, `shot <png>`, `click <x> <y>`, `wheel <x> <y> [ticks]`, `type <keys>` (SendKeys syntax) and `stop`. The coordinates are relative to the window, in the pixels of the screenshot. Save the screenshots in the scratchpad, read them to see the window, and stop the application when done. Extend the script if an action is missing instead of writing another one.

## Environment

- Windows 11, Windows PowerShell 5.1 with Spanish messages (no `&&`, `?:` or `??`); Git Bash is also available. Python 3.13 (`python`) for one-off analysis scripts, kept in the scratchpad, not in the repository.
- The working tree has CRLF line ends (`.gitattributes`, `.editorconfig`); the repository stores LF. Write new files with CRLF.

## Commits

Only when asked. The messages follow the style of the commits up to 2026-11-07 (`git log --until=2026-11-07`), not the long single-paragraph ones after it:

- A title line in English, imperative, without prefix, that says what changes for the user (e.g. "Open the gzipped SPR files (.gz) and list the textures of the SPR files with several"), up to about 100 characters.
- A small change has only the title. Otherwise a blank line and a few short paragraphs, wrapped at about 80 columns: what was found in the files and why the change is needed, then what the application does now. No list of every function, file or field touched.
- A last line for the wiki when it changes (e.g. "Document the PKF families in the wiki.").

## File format wiki

`wiki/` holds a GitHub-wiki-compatible description of every file format the tool handles (one page per format, plus `Home.md` and `_Sidebar.md`).

Whenever a format is added, or what is known about one changes (new fields, sizes, variants, compression, magic detection in `File_ExtensionFromMagic`), update the wiki in the same commit:

- Add or update the format page `wiki/<FORMAT>.md`: description, extension, magic, layout diagram, and a table per structure with offset, size, type, name and description of every field. Unknown fields are documented as unknown, not omitted.
- Add the format to the table and the nesting tree in `wiki/Home.md`, to the magic numbers table if it is detected by magic, and to `wiki/_Sidebar.md`.
- The wiki documents only the formats and the particularities found in the files (variants, unknown or unreliable fields, wrong extensions, statistics over the game files...). It never mentions this application, its source code, its resources or how it handles the files (extraction, naming, viewer, workarounds, what it supports).
- Use wiki-style links between pages (`[PKF](PKF)`, no `.md`), so the pages work when copied to the GitHub wiki repository.
- Values are little-endian; write offsets in hexadecimal.
- Don't edit the GitHub wiki directly: `.github/workflows/wiki.yml` copies `wiki/` to it on every push to master, overwriting it.
