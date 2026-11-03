#ifndef __TOOLBAR_H__
#define __TOOLBAR_H__

#include <stdbool.h>
#include <raylib.h>

// Toolbar at the top of the window. Each control is an action with an icon, a name and an optional key: the name and the
// key are shown in a tooltip, and the key does the same as a click. Immediate mode, as raygui: the application and the
// open view add their actions every frame, between Toolbar_Begin and Toolbar_End. The actions added while raygui is
// disabled (GuiDisable) are drawn disabled and their key does nothing.
// The keys of the flight of the free camera (W/A/S/D/Q/E) are reserved for it: a warning is printed if an action uses one
// of them, or if two actions of the same frame use the same key

// Modifiers and options of the key of an action, added to a raylib key
#define TOOLBAR_SHIFT   0x10000 // With Shift pressed. Without it, the key does nothing while Shift is pressed
#define TOOLBAR_CTRL    0x20000 // The same with Control
#define TOOLBAR_REPEAT  0x40000 // Repeated while the key is held

void Toolbar_Begin(Rectangle area);
// Space between two groups of actions
void Toolbar_Separator(void);
// A button with an icon and a label, NULL for only the icon. Returns true when it is clicked or its key is pressed
bool Toolbar_Button(int icon, const char* label, const char* name, int key);
// A button that is pressed while *value. A click or the key inverts it. Returns true when it changes
bool Toolbar_Toggle(int icon, const char* label, const char* name, int key, bool* value);
// A button per option, separated by ';', with the active one pressed. The key selects the next option. Returns true
// when it changes
bool Toolbar_Choice(const char* options, const char* name, int key, int* active);
void Toolbar_End(void);
// Draws the tooltip of the action under the mouse over everything else: the last thing drawn in the frame
void Toolbar_DrawTooltip(void);

#endif
