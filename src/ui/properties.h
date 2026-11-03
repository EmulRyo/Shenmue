#ifndef __PROPERTIES_H__
#define __PROPERTIES_H__

#include <stdbool.h>
#include <raylib.h>

// Panel of the properties of the selected item of a view, below its tree: a title, then rows of a name and a value,
// grouped in sections. Immediate mode, as raygui: the view adds its rows every frame, between Properties_Begin and
// Properties_End. The values are shown as controls: the texts and the numbers in boxes, the yes or no values with a
// switch. Those that can't be edited are drawn without a border. The values that don't fit are cut, and shown whole in
// a tooltip.
// The numbers that can be edited are changed by dragging them horizontally (Shift: 10 times finer), or by typing a value
// after a click on them (Enter or a click outside applies it). Only the values in memory change, never the files

// State of a panel, kept by the view
typedef struct {
    Vector2     scroll;         // Back to the top when the title changes: another item is selected
    char        title[128];
    float       contentHeight;  // Of the previous frame
    int         active;         // Number field dragged or typed, -1 if none
    bool        typing;         // Its value is typed in text
    bool        dragged;        // It was dragged since the click
    float       dragStart;      // Mouse x at the click
    float       startValue;     // Value at the click
    char        text[32];       // Typed value
    bool        cursor;         // The cursor is the horizontal resize one
    // Of the frame
    Rectangle   view;           // Of the rows, without the scroll bar
    float       y;              // Of the next row, in the content
    int         field;          // Index of the next number field
    bool        hover;          // The mouse is over a number field, or one is dragged
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
// Editable numbers, changed by step per pixel dragged. If original isn't NULL and the values differ from it, the name is
// highlighted and a button at the right restores them. Return true when the values change
bool Properties_EditFloat(Properties* properties, const char* name, float* value, const float* original, float step);
bool Properties_EditVector(Properties* properties, const char* name, float* values, const float* original, int count, float step);
// A line of text in gray, without name: e.g. what to do when nothing is selected
void Properties_Note(Properties* properties, const char* text);
void Properties_End(Properties* properties);

#endif
