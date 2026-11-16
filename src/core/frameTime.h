#ifndef __FRAMETIME_H__
#define __FRAMETIME_H__

// Time of the last frame, for the animations, the camera and the delays of the user interface: the time measured by
// raylib, or a fixed step (headless mode, so the same frames give the same images)

float   FrameTime_Get(void);
// Seconds of every frame from now on. 0 uses the time measured again
void    FrameTime_SetFixed(float seconds);

#endif
