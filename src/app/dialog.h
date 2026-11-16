#ifndef __DIALOG_H__
#define __DIALOG_H__

#include <stdbool.h>
#include <stddef.h>

// The native dialogs to open and save files and to pick folders. In the headless mode no dialog is shown: each one takes
// the next answer given with Dialog_Answer, or is cancelled if there is none

#define DIALOG_PATH_SIZE 1024

typedef struct {
    const char* name;       // e.g. "PNG image"
    const char* extensions; // Separated by commas, without the dots: e.g. "png" or "mt5,chrm"
} DialogFilter;

typedef enum {
    DIALOG_OK,
    DIALOG_CANCEL,
    DIALOG_ERROR,   // Logged
} DialogResult;

// A file to open, starting at folder (NULL for the default one)
DialogResult Dialog_Open(char path[DIALOG_PATH_SIZE], const DialogFilter* filters, unsigned int filterCount, const char* folder);
// Several files to open. *paths is an array of *count paths, freed with Dialog_FreePaths
DialogResult Dialog_OpenMultiple(char*** paths, unsigned int* count, const DialogFilter* filters, unsigned int filterCount,
    const char* folder);
void Dialog_FreePaths(char** paths, unsigned int count);
// A file to save, starting at folder with name
DialogResult Dialog_Save(char path[DIALOG_PATH_SIZE], const DialogFilter* filters, unsigned int filterCount, const char* folder,
    const char* name);
// A folder, starting at folder
DialogResult Dialog_PickFolder(char path[DIALOG_PATH_SIZE], const char* folder);

// Headless mode: the dialogs take the answers instead of being shown
void Dialog_SetHeadless(void);
// Queues the answer of the next dialog: a path, several separated by | for Dialog_OpenMultiple, or "cancel"
void Dialog_Answer(const char* answer);
// Number of answers not taken yet
int Dialog_PendingAnswers(void);

#endif
