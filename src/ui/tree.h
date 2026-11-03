#ifndef __TREE_H__
#define __TREE_H__

#include <stdbool.h>
#include <raylib.h>

// Tree of the contents of a view: groups that can be collapsed, with the count of their items, and items that can be
// selected. The groups and the items that can be hidden have a visibility toggle (an eye) at the right. A filter at the
// top shows only the items whose text contains it (or whose group's name does), in every group, collapsed or not.
// Immediate mode, as raygui: the view adds its groups and items every frame, between Tree_Begin and Tree_End, also the
// items of the collapsed groups, which are counted. The text that doesn't fit is cut, and shown whole in a tooltip

#define TREE_MAX_DEPTH      4
#define TREE_FILTER_SIZE    64
#define TREE_NAME_SIZE      96

// Flags of an item
#define TREE_SELECTED   1
#define TREE_FAILED     2   // Couldn't be loaded: marked in red, with a warning icon
#define TREE_DIMMED     4   // Drawn in gray: not visible for another reason than its toggle

typedef struct {
    float       y;          // Of its row, in the content
    char        name[TREE_NAME_SIZE];
    bool*       expanded;
    bool*       visible;    // NULL without a toggle
    bool        open;       // Its rows are shown: it and its parents are expanded, or there is a filter
    bool        hidden;     // It or a parent is hidden
    bool        matched;    // Its name or the name of a parent contains the filter
    bool        changed;    // Its toggle was clicked
    int         count;      // Items, with those of the subgroups
    int         matches;    // Items that contain the filter
} TreeGroup;

// State of a tree, kept by the view
typedef struct {
    char        filter[TREE_FILTER_SIZE];
    bool        editing;        // The filter has the keyboard
    Vector2     scroll;
    float       contentHeight;  // Of the previous frame
    bool        reveal;         // Scroll to the selected item
    bool        revealExpand;   // Expanding its groups if it is in a collapsed one
    // Of the frame
    Rectangle   view;           // Of the rows, without the scroll bar
    float       y;              // Of the next row, in the content
    bool        revealing;      // The groups of the selected item were expanded to reveal it
    int         depth;
    TreeGroup   groups[TREE_MAX_DEPTH];
} Tree;

void Tree_Begin(Tree* tree, Rectangle bounds);
// A group, closed by Tree_EndGroup, with the items and subgroups added between them. visible can be NULL
void Tree_BeginGroup(Tree* tree, const char* name, bool* expanded, bool* visible);
// Returns true when its visibility toggle was clicked
bool Tree_EndGroup(Tree* tree);
// An item with TREE_* flags, and its visibility toggle if visible isn't NULL. Returns true when it is clicked, not on its
// toggle
bool Tree_Item(Tree* tree, const char* text, int flags, bool* visible);
void Tree_End(Tree* tree);
// Scrolls to the selected item in the next frames: after a selection outside the tree. If it is in a collapsed group,
// expands its groups, or does nothing without expand (e.g. for an item that follows the playback)
void Tree_Reveal(Tree* tree, bool expand);

#endif
