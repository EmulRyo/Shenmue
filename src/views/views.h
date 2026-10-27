#ifndef __VIEWS_H__
#define __VIEWS_H__

#include <raylib.h>

// Views that show the opened file in the content area of the window. Only one of them is open at a time

typedef enum {
    VIEW_NONE = -1,
    VIEW_IMAGE,
    VIEW_MT5,
    VIEW_XWM,
    VIEW_SND,
    VIEW_COUNT,
} ViewID;

// Closes the views other than view, after view has opened a file. VIEW_NONE closes all of them
void Views_CloseOthers(ViewID view);
void Views_CloseAll(void);
// Draws the open view inside area
void Views_Draw(Rectangle area);

#endif
