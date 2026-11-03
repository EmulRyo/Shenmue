#ifndef __PROPERTIES_H__
#define __PROPERTIES_H__

#include <stdbool.h>
#include <raylib.h>

// Panel of the properties of the selected item of a view, below its tree: a title, then rows of a name and a value,
// grouped in sections. Read only. Immediate mode, as raygui: the view adds its rows every frame, between
// Properties_Begin and Properties_End. The values that don't fit are cut, and shown whole in a tooltip

// State of a panel, kept by the view
typedef struct {
    Vector2     scroll;         // Back to the top when the title changes: another item is selected
    char        title[128];
    float       contentHeight;  // Of the previous frame
    // Of the frame
    Rectangle   view;           // Of the rows, without the scroll bar
    float       y;              // Of the next row, in the content
} Properties;

// Splits the column at the left of a view in its tree and, below it, its properties panel
void Properties_Split(Rectangle column, Rectangle* tree, Rectangle* panel);

// title: what is selected, e.g. "Camera 0101"; NULL for "Properties"
void Properties_Begin(Properties* properties, Rectangle bounds, const char* title);
void Properties_Section(Properties* properties, const char* name);
void Properties_Text(Properties* properties, const char* name, const char* value);
void Properties_Int(Properties* properties, const char* name, long long value);
void Properties_Hex(Properties* properties, const char* name, unsigned long long value);
void Properties_Float(Properties* properties, const char* name, float value);
void Properties_Vector(Properties* properties, const char* name, const float* values, int count);
void Properties_Bool(Properties* properties, const char* name, bool value);
// A line of text in gray, without name: e.g. what to do when nothing is selected
void Properties_Note(Properties* properties, const char* text);
void Properties_End(Properties* properties);

#endif
