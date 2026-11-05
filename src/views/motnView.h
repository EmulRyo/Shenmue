#ifndef __MOTNVIEW_H__
#define __MOTNVIEW_H__

#include <stdbool.h>
#include <raylib.h>
#include "views/views.h"

// Player of the motions of a MOTN file on the body of a character. The files don't say which character a motion is
// for: the body is one of the models (MT5/CHRM) of the folder of the file, or Ryo (model/chara/ykb_m.mt5 of the scene)
// for his motions (AKI_*) and when the folder has none

bool MOTNView_Open(const char* filepath);
void MOTNView_Close(void);
bool MOTNView_IsOpen(void);
// Handles the camera, the tree of the motions and models and the timeline, and draws them inside area
void MOTNView_Draw(Rectangle area);
void MOTNView_Status(ViewStatus* status);
void MOTNView_Toolbar(void);

#endif
