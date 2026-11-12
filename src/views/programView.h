#ifndef __PROGRAMVIEW_H__
#define __PROGRAMVIEW_H__

#include <stdbool.h>
#include <raylib.h>
#include "views/views.h"

// View of a scene program: the SCN3 chunk of a mapinfo.bin (opened from the view of the map, shown over it until it is
// closed) or a .scn file of scenario/. The programs, of SH-4 code or bytecode, are listed by function, with the calls to
// the game and the strings of the data they use; a click on a branch or a call goes to its target. The header and the
// relocations are shown too

// Opens a .scn file. Returns false if it isn't a valid scene program
bool ProgramView_Open(const char* filepath);
// Opens the program of a mapinfo.bin, over the view of the map, which stays open to go back to it
bool ProgramView_OpenMap(const char* filepath);
void ProgramView_Close(void);
bool ProgramView_IsOpen(void);
// The view is open over the view of the map
bool ProgramView_UsesMap(void);
void ProgramView_Draw(Rectangle area);
void ProgramView_Status(ViewStatus* status);
// Backspace: back to the map, Left/Right: back and forward in the visited places, Tab/Shift+Tab: next/previous reference,
// C: pseudo-C of the function (scnDecompile.h), from the bytecode of the program or of the SCN file paired with it
void ProgramView_Toolbar(void);
// The listing can be exported: an SH-4 program is open
bool ProgramView_CanExport(void);
// Saves the listing of the whole program to a text file chosen in a dialog
void ProgramView_ExportDialog(void);

#endif
