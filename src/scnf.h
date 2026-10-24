#ifndef __SCNF_H__
#define __SCNF_H__

#include <stdbool.h>

// Prints the dialogue lines of a SCNF file (gzipped or not): the characters of each block and,
// for each line, its voice, speaker, duration and subtitle, e.g.:
//   F1015B001  B AKMI  2.00 s  明美「Oh, look who's here!」
// Returns false if the file is not a valid SCNF file
bool SCNF_Print(const char* filepath);

#endif
