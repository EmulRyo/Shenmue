#ifndef __CHRT_H__
#define __CHRT_H__

#include <stdbool.h>

// Decodes the commands of a CHRT file (gzipped or not) and prints them as a script, e.g.:
//   Character('AKMI', 31)
//   Position(-300, 0, 0)
// Returns false if the file is not a valid CHRT file
bool CHRT_Print(const char* filepath);

#endif
