#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <nfd.h>
#include "core/log.h"
#include "app/dialog.h"

#define FILTER_MAX  16
#define ANSWER_MAX  16

static struct {
    bool    headless;
    char*   answers[ANSWER_MAX];    // Queued, the first is the next one
    int     count;
} g_dialog;

void Dialog_SetHeadless(void) {
    g_dialog.headless = true;
}

void Dialog_Answer(const char* answer) {
    if (g_dialog.count == ANSWER_MAX) {
        Log_Error("[Dialog_Answer]: Too many answers queued\n");
        return;
    }
    size_t size = strlen(answer) + 1;
    char* copy = malloc(size);
    if (copy == NULL)
        return;
    memcpy(copy, answer, size);
    g_dialog.answers[g_dialog.count++] = copy;
}

int Dialog_PendingAnswers(void) {
    return g_dialog.count;
}

// The next answer, to free by the caller, or NULL to cancel the dialog
static char* TakeAnswer(const char* dialog) {
    if (g_dialog.count == 0) {
        Log_Warning("[Dialog] %s dialog cancelled: no answer queued\n", dialog);
        return NULL;
    }
    char* answer = g_dialog.answers[0];
    memmove(g_dialog.answers, g_dialog.answers + 1, (size_t)(g_dialog.count - 1) * sizeof(char*));
    g_dialog.count--;
    if (strcmp(answer, "cancel") == 0) {
        free(answer);
        return NULL;
    }
    Log_Info("[Dialog] %s dialog answered %s\n", dialog, answer);
    return answer;
}

// The answer of a dialog that gives one path
static DialogResult HeadlessPath(char path[DIALOG_PATH_SIZE], const char* dialog) {
    char* answer = TakeAnswer(dialog);
    if (answer == NULL)
        return DIALOG_CANCEL;
    snprintf(path, DIALOG_PATH_SIZE, "%s", answer);
    free(answer);
    return DIALOG_OK;
}

static unsigned int ToNFD(nfdu8filteritem_t nfd[FILTER_MAX], const DialogFilter* filters, unsigned int count) {
    if (count > FILTER_MAX)
        count = FILTER_MAX;
    for (unsigned int i = 0; i < count; i++)
        nfd[i] = (nfdu8filteritem_t) { filters[i].name, filters[i].extensions };
    return count;
}

// Copies the path given by NFD and frees it
static DialogResult FromNFD(nfdresult_t result, nfdu8char_t* nfdPath, char path[DIALOG_PATH_SIZE]) {
    if (result == NFD_OKAY) {
        snprintf(path, DIALOG_PATH_SIZE, "%s", nfdPath);
        NFD_FreePathU8(nfdPath);
        return DIALOG_OK;
    }
    if (result == NFD_ERROR) {
        Log_Error("Error: %s\n", NFD_GetError());
        return DIALOG_ERROR;
    }
    return DIALOG_CANCEL;
}

DialogResult Dialog_Open(char path[DIALOG_PATH_SIZE], const DialogFilter* filters, unsigned int filterCount, const char* folder) {
    if (g_dialog.headless)
        return HeadlessPath(path, "Open");
    nfdu8filteritem_t nfd[FILTER_MAX];
    nfdu8char_t* nfdPath = NULL;
    nfdopendialogu8args_t args = { 0 };
    args.filterList = nfd;
    args.filterCount = ToNFD(nfd, filters, filterCount);
    args.defaultPath = folder;
    return FromNFD(NFD_OpenDialogU8_With(&nfdPath, &args), nfdPath, path);
}

DialogResult Dialog_Save(char path[DIALOG_PATH_SIZE], const DialogFilter* filters, unsigned int filterCount, const char* folder,
    const char* name) {
    if (g_dialog.headless)
        return HeadlessPath(path, "Save");
    nfdu8filteritem_t nfd[FILTER_MAX];
    nfdu8char_t* nfdPath = NULL;
    unsigned int count = ToNFD(nfd, filters, filterCount);
    return FromNFD(NFD_SaveDialogU8(&nfdPath, nfd, count, folder, name), nfdPath, path);
}

DialogResult Dialog_PickFolder(char path[DIALOG_PATH_SIZE], const char* folder) {
    if (g_dialog.headless)
        return HeadlessPath(path, "Folder");
    nfdu8char_t* nfdPath = NULL;
    return FromNFD(NFD_PickFolderU8(&nfdPath, folder), nfdPath, path);
}

// Copy of the n characters of text, ended by a null
static char* CopyText(const char* text, size_t n) {
    char* copy = malloc(n + 1);
    if (copy != NULL) {
        memcpy(copy, text, n);
        copy[n] = '\0';
    }
    return copy;
}

DialogResult Dialog_OpenMultiple(char*** paths, unsigned int* count, const DialogFilter* filters, unsigned int filterCount,
    const char* folder) {
    *paths = NULL;
    *count = 0;
    if (g_dialog.headless) {
        char* answer = TakeAnswer("Open");
        if (answer == NULL)
            return DIALOG_CANCEL;
        unsigned int total = 1;
        for (const char* c = answer; *c != '\0'; c++)
            total += *c == '|';
        *paths = calloc(total, sizeof(char*));
        for (const char* start = answer; *paths != NULL && *count < total;) {
            const char* end = strchr(start, '|');
            size_t length = end != NULL ? (size_t)(end - start) : strlen(start);
            char* path = CopyText(start, length);
            if (path != NULL)
                (*paths)[(*count)++] = path;
            if (end == NULL)
                break;
            start = end + 1;
        }
        free(answer);
        return *count > 0 ? DIALOG_OK : DIALOG_CANCEL;
    }

    nfdu8filteritem_t nfd[FILTER_MAX];
    unsigned int nfdCount = ToNFD(nfd, filters, filterCount);
    const nfdpathset_t* pathSet;
    nfdresult_t result = NFD_OpenDialogMultipleU8(&pathSet, nfd, nfdCount, folder);
    if (result == NFD_ERROR) {
        Log_Error("Error: %s\n", NFD_GetError());
        return DIALOG_ERROR;
    }
    if (result != NFD_OKAY)
        return DIALOG_CANCEL;
    nfdpathsetsize_t total = 0;
    NFD_PathSet_GetCount(pathSet, &total);
    *paths = calloc(total > 0 ? total : 1, sizeof(char*));
    for (nfdpathsetsize_t i = 0; *paths != NULL && i < total; i++) {
        nfdu8char_t* nfdPath;
        if (NFD_PathSet_GetPathU8(pathSet, i, &nfdPath) == NFD_OKAY) {
            char* path = CopyText(nfdPath, strlen(nfdPath));
            if (path != NULL)
                (*paths)[(*count)++] = path;
            NFD_PathSet_FreePathU8(nfdPath);
        }
    }
    NFD_PathSet_Free(pathSet);
    return *count > 0 ? DIALOG_OK : DIALOG_CANCEL;
}

void Dialog_FreePaths(char** paths, unsigned int count) {
    if (paths == NULL)
        return;
    for (unsigned int i = 0; i < count; i++)
        free(paths[i]);
    free(paths);
}
