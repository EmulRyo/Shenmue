#ifndef __LISTVIEW_H__
#define __LISTVIEW_H__

#include <stdbool.h>
#include <raylib.h>

// Lists of the views, where the items that couldn't be loaded are marked: their label starts with LISTVIEW_FAILED (a
// warning icon) and ends with the reason, and their row is drawn in red

#define LISTVIEW_FAILED "#220#" // raygui ICON_WARNING

// As GuiListViewEx, with failed[i] true for the items that couldn't be loaded. failed can be NULL
void ListView_Draw(Rectangle bounds, char** labels, const bool* failed, int count, int* scroll, int* active, int* focus);

#endif
