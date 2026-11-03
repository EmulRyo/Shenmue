#ifndef __INPUT_H__
#define __INPUT_H__

#include <stdbool.h>

// Who has the keyboard: while a text box is edited, the keys of the actions of the toolbar and of the flight of the
// camera are ignored

// Called every frame by a text box while it is edited
void Input_SetTyping(void);
// A text box is edited in this frame or was in the previous one: the controls drawn before it don't know it yet
bool Input_IsTyping(void);
// The last thing of the frame
void Input_EndFrame(void);

#endif
