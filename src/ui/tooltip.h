#ifndef __TOOLTIP_H__
#define __TOOLTIP_H__

#include <stdbool.h>
#include <raylib.h>

// Text shown when the mouse rests over a control of the user interface, e.g. the name and the key of an action of the
// toolbar, or the whole text of a field of the status bar that is cut

// Sets the tooltip of the control at bounds, which the mouse is over. Called every frame while it is
void Tooltip_Set(Rectangle bounds, const char* text);
// The text cut with "..." at its end, or at its start (for the paths, to keep the name of the file), to fit in width with
// the font of raygui. If it is cut and the mouse is over bounds, the whole text is the tooltip of bounds. The result is
// valid until the next call
const char* Tooltip_Fit(Rectangle bounds, const char* text, float width, bool start);
// Draws the tooltip over everything else, below the control or above it if there is no room: the last thing drawn in
// the frame
void Tooltip_Draw(void);

#endif
