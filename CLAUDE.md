# Shenmue

## File format wiki

`wiki/` holds a GitHub-wiki-compatible description of every file format the tool handles (one page per format, plus `Home.md` and `_Sidebar.md`).

Whenever a format is added, or what is known about one changes (new fields, sizes, variants, compression, magic detection in `File_ExtensionFromMagic`), update the wiki in the same commit:

- Add or update the format page `wiki/<FORMAT>.md`: description, extension, magic, layout diagram, and a table per structure with offset, size, type, name and description of every field. Unknown fields are documented as unknown, not omitted.
- Add the format to the table and the nesting tree in `wiki/Home.md`, to the magic numbers table if it is detected by magic, and to `wiki/_Sidebar.md`.
- Use wiki-style links between pages (`[PKF](PKF)`, no `.md`) and absolute GitHub URLs (`https://github.com/EmulRyo/Shenmue/blob/main/...`) for source files, so the pages work when copied to the GitHub wiki repository.
- Values are little-endian; write offsets in hexadecimal.
