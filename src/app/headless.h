#ifndef __HEADLESS_H__
#define __HEADLESS_H__

#include <stdbool.h>

// Headless mode: the window is hidden and commands drive the application, so it can be tested in the background. The
// commands come from a script, a command per line run in order between the frames (blank lines and lines starting with
// # are ignored), then from a named pipe if there is one: there each command is answered when it ends (see
// ControlPipe), and no frame is drawn while no command runs.
// The coordinates are pixels of the screenshots, from the top left corner of the window contents, and the animations
// advance 1/60 s per frame, whatever the time taken to draw it:
//
//   open <file>                    Opens the file, as if it was dropped on the window
//   frames <n>                     Draws n frames before the next command
//   shot <png>                     Draws a frame and saves it to the image
//   status                         Prints the fields of the status bar
//   move <x> <y>                   Moves the mouse (draw some frames after it to see a tooltip)
//   click <x> <y> [right|middle]   Clicks a button of the mouse (left by default)
//   drag <x1> <y1> <x2> <y2> [right|middle]
//                                  Drags with a button of the mouse pressed, in 8 frames
//   wheel <x> <y> <ticks>          Turns the mouse wheel over the point: down if ticks > 0, up if < 0
//   key <keys>                     Presses and releases a key with its modifiers, e.g. "ctrl+s", "left", "f"
//   hold <keys> <n>                Holds a key down during n frames (e.g. "w" to fly forwards)
//   type <text>                    Types the text, a character per frame, in the text box being edited
//   dialog <answer>                Queues the answer of the next file dialog: a path, several separated by | to open
//                                  several files, or "cancel". A dialog without answer is cancelled
//   quit                           Ends the application (also after the last command of a script without pipe)

// Loads the script and opens the pipe \\.\pipe\<pipeName> (either can be NULL), takes the input of the window, which
// must exist, and mutes the sound. Returns false if the script can't be read or the pipe can't be created
bool Headless_Start(const char* scriptPath, const char* pipeName);
// Saves the frame being drawn if a shot asked for it. Called after the frame is drawn, before EndDrawing
void Headless_Capture(void);
// Runs the commands due before the next frame. Called after EndDrawing, which read the input of the previous frame.
// Returns false when the application must end: at the end of the script without pipe, or with quit
bool Headless_Update(void);
// The application runs in the headless mode
bool Headless_IsActive(void);
// Closes the pipe
void Headless_End(void);
// Number of commands that failed
int Headless_Errors(void);

#endif
