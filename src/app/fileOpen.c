#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <nfd.h>
#include <raylib.h>
#include "core/file.h"
#include "formats/archive/afs.h"
#include "formats/archive/idx.h"
#include "formats/archive/pkf.h"
#include "formats/archive/pks.h"
#include "formats/model/mt5.h"
#include "formats/text/chrt.h"
#include "formats/text/scnf.h"
#include "views/authView.h"
#include "views/chrtView.h"
#include "views/imageView.h"
#include "views/mapinfoView.h"
#include "views/mt5View.h"
#include "views/sndView.h"
#include "views/xwmView.h"
#include "views/views.h"
#include "app/fileOpen.h"

typedef struct {
    const char* extensions; // As IsFileExtension takes them, e.g. ".mt5;.chrm"
    const char* magic;      // Extension given by the magic to open the files with a generic extension, NULL if none
    const char* name;
    bool        (*open)(const char* filepath); // Returns false if the file can't be loaded
    ViewID      view;       // View that shows the file, VIEW_NONE if it is printed or extracted
    const char* filename;   // Lowercase name of the files of a format with a generic extension and no magic, NULL if none
} Format;

static bool ExtractAFS(const char* filepath) {
    AFS_ExtractAllFiles(filepath, "");
    return true;
}

static bool ExtractIDX(const char* filepath) {
    IDX_ExtractAllFiles(filepath, "");
    return true;
}

static bool ExtractPKF(const char* filepath) {
    PKF_ExtractAllFiles(filepath, "");
    return true;
}

static bool ExtractPKS(const char* filepath) {
    PKS_ExtractAllFiles(filepath, "");
    return true;
}

static bool OpenMT5(const char* filepath) {
    MT5_Print(filepath);
    return MT5View_Open(filepath);
}

static bool OpenCHRT(const char* filepath) {
    CHRT_Print(filepath);
    return CHRTView_Open(filepath);
}

static const Format g_formats[] = {
    { ".pvr",       NULL,   "PVR",  ImageView_OpenPVR,  VIEW_IMAGE },
    { ".spr",       NULL,   "SPR",  ImageView_OpenSPR,  VIEW_IMAGE },
    { ".afs",       NULL,   "AFS",  ExtractAFS,         VIEW_NONE },
    { ".idx",       NULL,   "IDX",  ExtractIDX,         VIEW_NONE },
    { ".pkf",       NULL,   "PKF",  ExtractPKF,         VIEW_NONE },
    { ".pks",       NULL,   "PKS",  ExtractPKS,         VIEW_NONE },
    { ".mt5;.chrm", NULL,   "MT5",  OpenMT5,            VIEW_MT5 },
    { ".chrt",      NULL,   "CHRT", OpenCHRT,           VIEW_CHRT },
    { ".scnf",      "scnf", "SCNF", SCNF_Print,         VIEW_NONE },
    { ".xwm",       "xwm",  "XWM",  XWMView_Open,       VIEW_XWM },
    { ".snd",       "snd",  "SND",  SNDView_Open,       VIEW_SND },
    { ".auth",      "auth", "AUTH", AUTHView_Open,      VIEW_AUTH },
    { NULL,         NULL,   "MAPINFO", MAPINFOView_Open, VIEW_MAPINFO, "mapinfo.bin" },
};

#define FORMAT_COUNT (sizeof(g_formats) / sizeof(g_formats[0]))

// Returns the extension given by the magic of the file, or NULL if it is unknown
static const char* GetExtensionFromMagic(const char* filepath) {
    size_t size = 0;
    uint8_t* buffer = File_LoadToMemoryDecompressed(filepath, &size);
    if (buffer == NULL)
        return NULL;
    const char* ext = File_ExtensionFromMagic(buffer, size);
    free(buffer);
    return ext;
}

static const Format* FindFormat(const char* filepath) {
    for (size_t i = 0; i < FORMAT_COUNT; i++) {
        if (g_formats[i].extensions != NULL && IsFileExtension(filepath, g_formats[i].extensions))
            return &g_formats[i];
        if (g_formats[i].filename != NULL && strcmp(TextToLower(GetFileName(filepath)), g_formats[i].filename) == 0)
            return &g_formats[i];
    }
    // Files with a generic extension (e.g. ".bin") are opened by their magic
    const char* ext = GetExtensionFromMagic(filepath);
    for (size_t i = 0; i < FORMAT_COUNT && ext != NULL; i++) {
        if (g_formats[i].magic != NULL && strcmp(ext, g_formats[i].magic) == 0)
            return &g_formats[i];
    }
    return NULL;
}

void FileOpen_Open(const char* filepath) {
    const Format* format = FindFormat(filepath);
    if (format == NULL) {
        printf("File not implemented yet: %s\n", filepath);
        return;
    }
    bool opened = format->open(filepath);
    if (format->view == VIEW_NONE)
        return;
    if (opened)
        Views_CloseOthers(format->view);
    else
        printf("Failed to load %s file: %s\n", format->name, filepath);
}

void FileOpen_Dialog(void) {
    nfdu8char_t* outPath = NULL;
    nfdu8filteritem_t filters[8] = { { "Containers", "afs,idx,pkf,pks" }, { "Textures", "pvr,spr,dds" }, { "Models", "mt5,chrm,mt7" }, { "Sounds", "xwm,snd" }, { "Scripts", "chrt" }, { "Dialogues", "scnf,bin" }, { "Cutscenes", "auth" }, { "Maps", "bin" } };
    nfdopendialogu8args_t args = { 0 };
    args.filterList = filters;
    args.filterCount = 8;
    nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);
    if (result == NFD_OKAY)
        FileOpen_Open(outPath);
    else if (result == NFD_ERROR)
        printf("Error: %s\n", NFD_GetError());

    if (outPath != NULL)
        NFD_FreePathU8(outPath);
}
