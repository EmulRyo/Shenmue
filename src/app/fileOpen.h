#ifndef __FILEOPEN_H__
#define __FILEOPEN_H__

// Opens a file by its extension or, for the generic extensions (e.g. ".bin"), by its magic: shows it in its view, prints
// it or extracts it
void FileOpen_Open(const char* filepath);
// Shows the open file dialog and opens the selected file
void FileOpen_Dialog(void);

#endif
