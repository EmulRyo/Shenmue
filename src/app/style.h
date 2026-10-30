#ifndef __STYLE_H__
#define __STYLE_H__

// Loads the font of the user interface, scaled by the DPI of the window, and sets the dark style of raygui
void Style_Init(void);
// Reloads the font if the DPI of the window has changed (moved to another monitor)
void Style_Update(void);

#endif
