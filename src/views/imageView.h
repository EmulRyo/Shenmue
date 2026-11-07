#ifndef __IMAGEVIEW_H__
#define __IMAGEVIEW_H__

#include <stdbool.h>
#include <raylib.h>
#include "views/views.h"

// Viewer of textures: shows the image over a checkered pattern, scaled to fit or at its real size. The wheel zooms on the
// pixel under the mouse and a drag moves the image. The SPR files with several textures show them in a list at the left,
// with the properties of the selected one below it

// Loads a texture to show it, replacing the current one. Returns false if the file can't be decoded
bool ImageView_OpenPVR(const char* filepath);
bool ImageView_OpenSPR(const char* filepath);
void ImageView_Close(void);
bool ImageView_IsOpen(void);
// Adds the actions to choose the texture of an SPR file with several
void ImageView_Toolbar(void);
bool ImageView_HasToolbar(void);
void ImageView_Draw(Rectangle area);
// Fills the texts of the status bar
void ImageView_Status(ViewStatus* status);
// Saves the image in a format given by the extension (png, jpg, bmp). Returns false if there is no image or on error
bool ImageView_Export(const char* filepath);

#endif
