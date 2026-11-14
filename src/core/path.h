#ifndef __PATH_H__
#define __PATH_H__

#include <stdbool.h>
#include <stddef.h>

// Replaces the \ separators of a path with /
void        Path_ToSlashes(char* path);
// Removes the last component of a path with / separators. Returns false if there is none
bool        Path_ToParent(char* path);
// The last component of a path with / separators: the whole path if it has none
const char* Path_LastComponent(const char* path);
// The same path, ignoring the case and the kind of separators
bool        Path_Equals(const char* a, const char* b);
// Folder of the scene that contains the file, with / separators (e.g. .../scene/01 for
// .../scene/01/d000/auth/SEQDATA0.auth): the folder whose parent is named "scene". Empty if the file isn't in a scene
// folder
void        Path_FindSceneFolder(const char* filepath, char* folder, size_t size);

#endif
