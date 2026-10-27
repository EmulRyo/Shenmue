#ifndef __IMAGEVIEW_H__
#define __IMAGEVIEW_H__

#include <stdbool.h>
#include <raylib.h>

// Viewer of textures: shows the image over a checkered pattern, scaled to fit

// Loads a texture to show it, replacing the current one. Returns false if the file can't be decoded
bool ImageView_OpenPVR(const char* filepath);
bool ImageView_OpenSPR(const char* filepath);
void ImageView_Close(void);
bool ImageView_IsOpen(void);
void ImageView_Draw(Rectangle area);
// Saves the image in a format given by the extension (png, jpg, bmp). Returns false if there is no image or on error
bool ImageView_Export(const char* filepath);

#endif
