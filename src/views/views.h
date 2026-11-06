#ifndef __VIEWS_H__
#define __VIEWS_H__

#include <stdbool.h>
#include <raylib.h>

// Views that show the opened file in the content area of the window. Only one of them is open at a time

typedef enum {
    VIEW_NONE = -1,
    VIEW_IMAGE,
    VIEW_MT5,
    VIEW_XWM,
    VIEW_SND,
    VIEW_AUTH,
    VIEW_CHRT,
    VIEW_MAPINFO,
    VIEW_MOTN,
    VIEW_FON,
    VIEW_COUNT,
} ViewID;

// What the status bar shows of the open view
typedef struct {
    char    file[1024]; // Path of the open file
    char    info[512];  // What it contains, or the selected item
    char    hint[256];  // What the mouse does in the view
} ViewStatus;

// Closes the views other than view, after view has opened a file. VIEW_NONE closes all of them
void Views_CloseOthers(ViewID view);
void Views_CloseAll(void);
// Draws the open view inside area
void Views_Draw(Rectangle area);
// Adds the actions of the open view to the toolbar, after a separator. Before Views_Draw, which uses their changes
void Views_Toolbar(void);
// Fills the status of the open view. Returns false if no view is open
bool Views_Status(ViewStatus* status);

#endif
