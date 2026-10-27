#ifndef __SPR_H__
#define __SPR_H__

typedef struct SPR_Header {
    char        id[4]; // "TEXN"
    uint32_t    size;
    char        filename[8];
    uint8_t     pvrData[];
} SPR_Header;

bool SPR_Load(const char* filepath, uint16_t* width, uint16_t* height, uint32_t** pixels);

#endif