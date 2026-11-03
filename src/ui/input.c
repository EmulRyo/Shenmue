#include <stdbool.h>
#include "ui/input.h"

static bool g_typing;   // In this frame
static bool g_typed;    // In the previous frame

void Input_SetTyping(void) {
    g_typing = true;
}

bool Input_IsTyping(void) {
    return g_typing || g_typed;
}

void Input_EndFrame(void) {
    g_typed = g_typing;
    g_typing = false;
}
