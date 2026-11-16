#include <raylib.h>
#include "core/frameTime.h"

static float g_fixed;

float FrameTime_Get(void) {
    return g_fixed > 0.0f ? g_fixed : GetFrameTime();
}

void FrameTime_SetFixed(float seconds) {
    g_fixed = seconds;
}
