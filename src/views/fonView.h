#ifndef __FONVIEW_H__
#define __FONVIEW_H__

#include <stdbool.h>
#include <raylib.h>
#include "views/views.h"

// Viewer of bitmap fonts: the glyphs in a grid that fills the width of the view, scrolled vertically, with the code of
// the glyph under the mouse in the status bar. The zoom is in pixels of the screen, whatever the scale of the display, so
// that every pixel of a glyph is drawn with the same size

// Loads a font, replacing the current one, with the same options (zoom, aspect, empty glyphs). Returns false if the file isn't a
// font
bool FONView_Open(const char* filepath);
void FONView_Close(void);
bool FONView_IsOpen(void);
// Handles the input (wheel: scroll, Ctrl+wheel: zoom) and draws the glyphs inside area
void FONView_Draw(Rectangle area);
// Fills the texts of the status bar
void FONView_Status(ViewStatus* status);
// Adds its actions to the toolbar (Z: next zoom, X: aspect of the 24-pixel glyphs, H: empty glyphs)
void FONView_Toolbar(void);

#endif
