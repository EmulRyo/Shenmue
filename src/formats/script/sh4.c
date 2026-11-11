#include <stdio.h>
#include <string.h>
#include "formats/script/sh4.h"

// Table of the SH-4 instructions: an opcode matches an entry if (opcode & mask) == match. The template gives the mnemonic
// and the operands, with these fields of the opcode:
//   %N  rn  (bits 8-11)          %M  rm  (bits 4-7)
//   %F  frn (bits 8-11)          %G  frm (bits 4-7)    drn/xdn in double mode (SH4_PRECISION, SH4_SIZE)
//   %I  #imm, signed (bits 0-7)  %U  #imm, unsigned (bits 0-7)
//   %D  displacement in bytes    %T  target address (branches)
//   %K  number of a bank register, r0_bank to r7_bank (bits 4-6)
//   %V  fvn (bits 10-11)         %W  fvm (bits 8-9)     %E  drn (bits 9-11)

typedef enum {
    DISP_NONE,
    DISP_4,     // Bits 0-3, unsigned, scaled
    DISP_8,     // Bits 0-7, unsigned, scaled (GBR)
    DISP_PCW,   // mov.w @(disp,PC): target = PC + 4 + disp × 2
    DISP_PCL,   // mov.l @(disp,PC), mova: target = (PC & ~3) + 4 + disp × 4
    DISP_BR8,   // bf, bt: target = PC + 4 + signed disp × 2
    DISP_BR12,  // bra, bsr: target = PC + 4 + signed disp × 2
} DispKind;

typedef struct {
    const char* text;
    uint16_t    match;
    uint16_t    mask;
    uint8_t     disp;   // DispKind
    uint8_t     scale;  // Size of the access, for DISP_4 and DISP_8
    uint32_t    flags;
} Entry;

#define BR      (SH4_BRANCH | SH4_SLOT_ILLEGAL)
#define FP      SH4_FPU
#define FPR     (SH4_FPU | SH4_PRECISION)
#define FSZ     (SH4_FPU | SH4_SIZE)
#define PRIV    SH4_PRIVILEGED

static const Entry s_entries[] = {
    // Data transfer
    { "mov %I,%N",              0xE000, 0xF000 },
    { "mov.w @(%D,pc),%N",      0x9000, 0xF000, DISP_PCW, 2, SH4_LITERAL },
    { "mov.l @(%D,pc),%N",      0xD000, 0xF000, DISP_PCL, 4, SH4_LITERAL },
    { "mov %M,%N",              0x6003, 0xF00F },
    { "mov.b %M,@%N",           0x2000, 0xF00F },
    { "mov.w %M,@%N",           0x2001, 0xF00F },
    { "mov.l %M,@%N",           0x2002, 0xF00F },
    { "mov.b @%M,%N",           0x6000, 0xF00F },
    { "mov.w @%M,%N",           0x6001, 0xF00F },
    { "mov.l @%M,%N",           0x6002, 0xF00F },
    { "mov.b %M,@-%N",          0x2004, 0xF00F },
    { "mov.w %M,@-%N",          0x2005, 0xF00F },
    { "mov.l %M,@-%N",          0x2006, 0xF00F },
    { "mov.b @%M+,%N",          0x6004, 0xF00F },
    { "mov.w @%M+,%N",          0x6005, 0xF00F },
    { "mov.l @%M+,%N",          0x6006, 0xF00F },
    { "mov.b r0,@(%D,%M)",      0x8000, 0xFF00, DISP_4, 1 },
    { "mov.w r0,@(%D,%M)",      0x8100, 0xFF00, DISP_4, 2 },
    { "mov.l %M,@(%D,%N)",      0x1000, 0xF000, DISP_4, 4 },
    { "mov.b @(%D,%M),r0",      0x8400, 0xFF00, DISP_4, 1 },
    { "mov.w @(%D,%M),r0",      0x8500, 0xFF00, DISP_4, 2 },
    { "mov.l @(%D,%M),%N",      0x5000, 0xF000, DISP_4, 4 },
    { "mov.b %M,@(r0,%N)",      0x0004, 0xF00F },
    { "mov.w %M,@(r0,%N)",      0x0005, 0xF00F },
    { "mov.l %M,@(r0,%N)",      0x0006, 0xF00F },
    { "mov.b @(r0,%M),%N",      0x000C, 0xF00F },
    { "mov.w @(r0,%M),%N",      0x000D, 0xF00F },
    { "mov.l @(r0,%M),%N",      0x000E, 0xF00F },
    { "mov.b r0,@(%D,gbr)",     0xC000, 0xFF00, DISP_8, 1 },
    { "mov.w r0,@(%D,gbr)",     0xC100, 0xFF00, DISP_8, 2 },
    { "mov.l r0,@(%D,gbr)",     0xC200, 0xFF00, DISP_8, 4 },
    { "mov.b @(%D,gbr),r0",     0xC400, 0xFF00, DISP_8, 1 },
    { "mov.w @(%D,gbr),r0",     0xC500, 0xFF00, DISP_8, 2 },
    { "mov.l @(%D,gbr),r0",     0xC600, 0xFF00, DISP_8, 4 },
    { "mova @(%D,pc),r0",       0xC700, 0xFF00, DISP_PCL, 4, SH4_ADDRESS },
    { "movt %N",                0x0029, 0xF0FF },
    { "movca.l r0,@%N",         0x00C3, 0xF0FF },
    { "swap.b %M,%N",           0x6008, 0xF00F },
    { "swap.w %M,%N",           0x6009, 0xF00F },
    { "xtrct %M,%N",            0x200D, 0xF00F },

    // Arithmetic
    { "add %M,%N",              0x300C, 0xF00F },
    { "add %I,%N",              0x7000, 0xF000 },
    { "addc %M,%N",             0x300E, 0xF00F },
    { "addv %M,%N",             0x300F, 0xF00F },
    { "cmp/eq %I,r0",           0x8800, 0xFF00 },
    { "cmp/eq %M,%N",           0x3000, 0xF00F },
    { "cmp/hs %M,%N",           0x3002, 0xF00F },
    { "cmp/ge %M,%N",           0x3003, 0xF00F },
    { "cmp/hi %M,%N",           0x3006, 0xF00F },
    { "cmp/gt %M,%N",           0x3007, 0xF00F },
    { "cmp/pz %N",              0x4011, 0xF0FF },
    { "cmp/pl %N",              0x4015, 0xF0FF },
    { "cmp/str %M,%N",          0x200C, 0xF00F },
    { "div1 %M,%N",             0x3004, 0xF00F },
    { "div0s %M,%N",            0x2007, 0xF00F },
    { "div0u",                  0x0019, 0xFFFF },
    { "dmuls.l %M,%N",          0x300D, 0xF00F },
    { "dmulu.l %M,%N",          0x3005, 0xF00F },
    { "dt %N",                  0x4010, 0xF0FF },
    { "exts.b %M,%N",           0x600E, 0xF00F },
    { "exts.w %M,%N",           0x600F, 0xF00F },
    { "extu.b %M,%N",           0x600C, 0xF00F },
    { "extu.w %M,%N",           0x600D, 0xF00F },
    { "mac.l @%M+,@%N+",        0x000F, 0xF00F },
    { "mac.w @%M+,@%N+",        0x400F, 0xF00F },
    { "mul.l %M,%N",            0x0007, 0xF00F },
    { "muls.w %M,%N",           0x200F, 0xF00F },
    { "mulu.w %M,%N",           0x200E, 0xF00F },
    { "neg %M,%N",              0x600B, 0xF00F },
    { "negc %M,%N",             0x600A, 0xF00F },
    { "sub %M,%N",              0x3008, 0xF00F },
    { "subc %M,%N",             0x300A, 0xF00F },
    { "subv %M,%N",             0x300B, 0xF00F },

    // Logic
    { "and %M,%N",              0x2009, 0xF00F },
    { "and %U,r0",              0xC900, 0xFF00 },
    { "and.b %U,@(r0,gbr)",     0xCD00, 0xFF00 },
    { "not %M,%N",              0x6007, 0xF00F },
    { "or %M,%N",               0x200B, 0xF00F },
    { "or %U,r0",               0xCB00, 0xFF00 },
    { "or.b %U,@(r0,gbr)",      0xCF00, 0xFF00 },
    { "tas.b @%N",              0x401B, 0xF0FF },
    { "tst %M,%N",              0x2008, 0xF00F },
    { "tst %U,r0",              0xC800, 0xFF00 },
    { "tst.b %U,@(r0,gbr)",     0xCC00, 0xFF00 },
    { "xor %M,%N",              0x200A, 0xF00F },
    { "xor %U,r0",              0xCA00, 0xFF00 },
    { "xor.b %U,@(r0,gbr)",     0xCE00, 0xFF00 },

    // Shift
    { "rotl %N",                0x4004, 0xF0FF },
    { "rotr %N",                0x4005, 0xF0FF },
    { "rotcl %N",               0x4024, 0xF0FF },
    { "rotcr %N",               0x4025, 0xF0FF },
    { "shad %M,%N",             0x400C, 0xF00F },
    { "shal %N",                0x4020, 0xF0FF },
    { "shar %N",                0x4021, 0xF0FF },
    { "shld %M,%N",             0x400D, 0xF00F },
    { "shll %N",                0x4000, 0xF0FF },
    { "shlr %N",                0x4001, 0xF0FF },
    { "shll2 %N",               0x4008, 0xF0FF },
    { "shlr2 %N",               0x4009, 0xF0FF },
    { "shll8 %N",               0x4018, 0xF0FF },
    { "shlr8 %N",               0x4019, 0xF0FF },
    { "shll16 %N",              0x4028, 0xF0FF },
    { "shlr16 %N",              0x4029, 0xF0FF },

    // Branch
    { "bf %T",                  0x8B00, 0xFF00, DISP_BR8, 0, BR | SH4_CONDITIONAL },
    { "bf/s %T",                0x8F00, 0xFF00, DISP_BR8, 0, BR | SH4_CONDITIONAL | SH4_DELAYED },
    { "bt %T",                  0x8900, 0xFF00, DISP_BR8, 0, BR | SH4_CONDITIONAL },
    { "bt/s %T",                0x8D00, 0xFF00, DISP_BR8, 0, BR | SH4_CONDITIONAL | SH4_DELAYED },
    { "bra %T",                 0xA000, 0xF000, DISP_BR12, 0, BR | SH4_DELAYED },
    { "braf %N",                0x0023, 0xF0FF, DISP_NONE, 0, BR | SH4_DELAYED | SH4_INDIRECT },
    { "bsr %T",                 0xB000, 0xF000, DISP_BR12, 0, BR | SH4_DELAYED | SH4_CALL },
    { "bsrf %N",                0x0003, 0xF0FF, DISP_NONE, 0, BR | SH4_DELAYED | SH4_CALL | SH4_INDIRECT },
    { "jmp @%N",                0x402B, 0xF0FF, DISP_NONE, 0, BR | SH4_DELAYED | SH4_INDIRECT },
    { "jsr @%N",                0x400B, 0xF0FF, DISP_NONE, 0, BR | SH4_DELAYED | SH4_CALL | SH4_INDIRECT },
    { "rts",                    0x000B, 0xFFFF, DISP_NONE, 0, BR | SH4_DELAYED | SH4_RETURN },

    // System control
    { "clrmac",                 0x0028, 0xFFFF },
    { "clrs",                   0x0048, 0xFFFF },
    { "clrt",                   0x0008, 0xFFFF },
    { "sets",                   0x0058, 0xFFFF },
    { "sett",                   0x0018, 0xFFFF },
    { "ldtlb",                  0x0038, 0xFFFF, DISP_NONE, 0, PRIV },
    { "nop",                    0x0009, 0xFFFF },
    { "rte",                    0x002B, 0xFFFF, DISP_NONE, 0, BR | SH4_DELAYED | SH4_RETURN | PRIV },
    { "sleep",                  0x001B, 0xFFFF, DISP_NONE, 0, PRIV },
    { "ldc %N,sr",              0x400E, 0xF0FF, DISP_NONE, 0, PRIV | SH4_SLOT_ILLEGAL },
    { "ldc %N,gbr",             0x401E, 0xF0FF },
    { "ldc %N,vbr",             0x402E, 0xF0FF, DISP_NONE, 0, PRIV },
    { "ldc %N,ssr",             0x403E, 0xF0FF, DISP_NONE, 0, PRIV },
    { "ldc %N,spc",             0x404E, 0xF0FF, DISP_NONE, 0, PRIV },
    { "ldc %N,dbr",             0x40FA, 0xF0FF, DISP_NONE, 0, PRIV },
    { "ldc %N,r%K_bank",        0x408E, 0xF08F, DISP_NONE, 0, PRIV },
    { "ldc.l @%N+,sr",          0x4007, 0xF0FF, DISP_NONE, 0, PRIV | SH4_SLOT_ILLEGAL },
    { "ldc.l @%N+,gbr",         0x4017, 0xF0FF },
    { "ldc.l @%N+,vbr",         0x4027, 0xF0FF, DISP_NONE, 0, PRIV },
    { "ldc.l @%N+,ssr",         0x4037, 0xF0FF, DISP_NONE, 0, PRIV },
    { "ldc.l @%N+,spc",         0x4047, 0xF0FF, DISP_NONE, 0, PRIV },
    { "ldc.l @%N+,dbr",         0x40F6, 0xF0FF, DISP_NONE, 0, PRIV },
    { "ldc.l @%N+,r%K_bank",    0x4087, 0xF08F, DISP_NONE, 0, PRIV },
    { "lds %N,mach",            0x400A, 0xF0FF },
    { "lds %N,macl",            0x401A, 0xF0FF },
    { "lds %N,pr",              0x402A, 0xF0FF },
    { "lds.l @%N+,mach",        0x4006, 0xF0FF },
    { "lds.l @%N+,macl",        0x4016, 0xF0FF },
    { "lds.l @%N+,pr",          0x4026, 0xF0FF },
    { "stc sr,%N",              0x0002, 0xF0FF, DISP_NONE, 0, PRIV },
    { "stc gbr,%N",             0x0012, 0xF0FF },
    { "stc vbr,%N",             0x0022, 0xF0FF, DISP_NONE, 0, PRIV },
    { "stc ssr,%N",             0x0032, 0xF0FF, DISP_NONE, 0, PRIV },
    { "stc spc,%N",             0x0042, 0xF0FF, DISP_NONE, 0, PRIV },
    { "stc sgr,%N",             0x003A, 0xF0FF, DISP_NONE, 0, PRIV },
    { "stc dbr,%N",             0x00FA, 0xF0FF, DISP_NONE, 0, PRIV },
    { "stc r%K_bank,%N",        0x0082, 0xF08F, DISP_NONE, 0, PRIV },
    { "stc.l sr,@-%N",          0x4003, 0xF0FF, DISP_NONE, 0, PRIV },
    { "stc.l gbr,@-%N",         0x4013, 0xF0FF },
    { "stc.l vbr,@-%N",         0x4023, 0xF0FF, DISP_NONE, 0, PRIV },
    { "stc.l ssr,@-%N",         0x4033, 0xF0FF, DISP_NONE, 0, PRIV },
    { "stc.l spc,@-%N",         0x4043, 0xF0FF, DISP_NONE, 0, PRIV },
    { "stc.l sgr,@-%N",         0x4032, 0xF0FF, DISP_NONE, 0, PRIV },
    { "stc.l dbr,@-%N",         0x40F2, 0xF0FF, DISP_NONE, 0, PRIV },
    { "stc.l r%K_bank,@-%N",    0x4083, 0xF08F, DISP_NONE, 0, PRIV },
    { "sts mach,%N",            0x000A, 0xF0FF },
    { "sts macl,%N",            0x001A, 0xF0FF },
    { "sts pr,%N",              0x002A, 0xF0FF },
    { "sts.l mach,@-%N",        0x4002, 0xF0FF },
    { "sts.l macl,@-%N",        0x4012, 0xF0FF },
    { "sts.l pr,@-%N",          0x4022, 0xF0FF },
    { "pref @%N",               0x0083, 0xF0FF },
    { "ocbi @%N",               0x0093, 0xF0FF },
    { "ocbp @%N",               0x00A3, 0xF0FF },
    { "ocbwb @%N",              0x00B3, 0xF0FF },
    { "trapa %U",               0xC300, 0xFF00, DISP_NONE, 0, SH4_SLOT_ILLEGAL },

    // Floating point
    { "fldi0 %F",               0xF08D, 0xF0FF, DISP_NONE, 0, FP },
    { "fldi1 %F",               0xF09D, 0xF0FF, DISP_NONE, 0, FP },
    { "fmov %G,%F",             0xF00C, 0xF00F, DISP_NONE, 0, FSZ },
    { "fmov.s @%M,%F",          0xF008, 0xF00F, DISP_NONE, 0, FSZ },
    { "fmov.s @(r0,%M),%F",     0xF006, 0xF00F, DISP_NONE, 0, FSZ },
    { "fmov.s @%M+,%F",         0xF009, 0xF00F, DISP_NONE, 0, FSZ },
    { "fmov.s %G,@%N",          0xF00A, 0xF00F, DISP_NONE, 0, FSZ },
    { "fmov.s %G,@-%N",         0xF00B, 0xF00F, DISP_NONE, 0, FSZ },
    { "fmov.s %G,@(r0,%N)",     0xF007, 0xF00F, DISP_NONE, 0, FSZ },
    { "flds %F,fpul",           0xF01D, 0xF0FF, DISP_NONE, 0, FP },
    { "fsts fpul,%F",           0xF00D, 0xF0FF, DISP_NONE, 0, FP },
    { "fabs %F",                0xF05D, 0xF0FF, DISP_NONE, 0, FPR },
    { "fadd %G,%F",             0xF000, 0xF00F, DISP_NONE, 0, FPR },
    { "fcmp/eq %G,%F",          0xF004, 0xF00F, DISP_NONE, 0, FPR },
    { "fcmp/gt %G,%F",          0xF005, 0xF00F, DISP_NONE, 0, FPR },
    { "fdiv %G,%F",             0xF003, 0xF00F, DISP_NONE, 0, FPR },
    { "float fpul,%F",          0xF02D, 0xF0FF, DISP_NONE, 0, FPR },
    { "fmac fr0,%G,%F",         0xF00E, 0xF00F, DISP_NONE, 0, FP },
    { "fmul %G,%F",             0xF002, 0xF00F, DISP_NONE, 0, FPR },
    { "fneg %F",                0xF04D, 0xF0FF, DISP_NONE, 0, FPR },
    { "fsqrt %F",               0xF06D, 0xF0FF, DISP_NONE, 0, FPR },
    { "fsub %G,%F",             0xF001, 0xF00F, DISP_NONE, 0, FPR },
    { "ftrc %F,fpul",           0xF03D, 0xF0FF, DISP_NONE, 0, FPR },
    { "fsrra %F",               0xF07D, 0xF0FF, DISP_NONE, 0, FP },
    { "fsca fpul,%E",           0xF0FD, 0xF1FF, DISP_NONE, 0, FP },
    { "fcnvds %E,fpul",         0xF0BD, 0xF1FF, DISP_NONE, 0, FP },
    { "fcnvsd fpul,%E",         0xF0AD, 0xF1FF, DISP_NONE, 0, FP },
    { "fipr %W,%V",             0xF0ED, 0xF0FF, DISP_NONE, 0, FP },
    { "ftrv xmtrx,%V",          0xF1FD, 0xF3FF, DISP_NONE, 0, FP },
    { "frchg",                  0xFBFD, 0xFFFF, DISP_NONE, 0, FP },
    { "fschg",                  0xF3FD, 0xFFFF, DISP_NONE, 0, FP },
    { "lds %N,fpscr",           0x406A, 0xF0FF, DISP_NONE, 0, FP },
    { "lds %N,fpul",            0x405A, 0xF0FF, DISP_NONE, 0, FP },
    { "lds.l @%N+,fpscr",       0x4066, 0xF0FF, DISP_NONE, 0, FP },
    { "lds.l @%N+,fpul",        0x4056, 0xF0FF, DISP_NONE, 0, FP },
    { "sts fpscr,%N",           0x006A, 0xF0FF, DISP_NONE, 0, FP },
    { "sts fpul,%N",            0x005A, 0xF0FF, DISP_NONE, 0, FP },
    { "sts.l fpscr,@-%N",       0x4062, 0xF0FF, DISP_NONE, 0, FP },
    { "sts.l fpul,@-%N",        0x4052, 0xF0FF, DISP_NONE, 0, FP },
};

#define ENTRY_COUNT ((int)(sizeof(s_entries) / sizeof(s_entries[0])))
// The lookup table holds the entries in a byte
typedef char EntryCountCheck[sizeof(s_entries) / sizeof(s_entries[0]) < 255 ? 1 : -1];

// Entry of every opcode (index + 1, 0 if invalid), built on the first use: the entry with the most bits in its mask
// wins, so the specific encodings (fschg) come before the general ones (fsca)
static uint8_t s_lookup[0x10000];
static char s_mnemonics[ENTRY_COUNT][16]; // First word of the template of each entry
static bool s_lookupBuilt;

static int CountBits(uint16_t value) {
    int count = 0;
    for (; value != 0; value &= value - 1)
        count++;
    return count;
}

static void BuildLookup(void) {
    for (uint32_t opcode = 0; opcode < 0x10000; opcode++) {
        int best = -1, bestBits = -1;
        for (int e = 0; e < ENTRY_COUNT; e++) {
            if ((opcode & s_entries[e].mask) == s_entries[e].match && CountBits(s_entries[e].mask) > bestBits) {
                best = e;
                bestBits = CountBits(s_entries[e].mask);
            }
        }
        s_lookup[opcode] = (uint8_t)(best + 1);
    }
    for (int e = 0; e < ENTRY_COUNT; e++) {
        size_t length = strcspn(s_entries[e].text, " ");
        memcpy(s_mnemonics[e], s_entries[e].text, length < 15 ? length : 15);
    }
    s_lookupBuilt = true;
}

int SH4_EntryCount(void) {
    return ENTRY_COUNT;
}

const char* SH4_EntryTemplate(int entry) {
    return entry >= 0 && entry < ENTRY_COUNT ? s_entries[entry].text : NULL;
}

bool SH4_Decode(uint16_t opcode, uint32_t address, uint32_t mode, SH4_Instruction* ins) {
    if (!s_lookupBuilt)
        BuildLookup();
    memset(ins, 0, sizeof(*ins));
    ins->opcode = opcode;
    ins->address = address;
    ins->mode = mode;
    ins->entry = s_lookup[opcode] - 1;
    ins->n = (opcode >> 8) & 0xF;
    ins->m = (opcode >> 4) & 0xF;
    if (ins->entry < 0)
        return false;
    const Entry* entry = &s_entries[ins->entry];
    // In double precision, the operands of the arithmetic instructions are pairs of registers: the odd ones are invalid
    if ((entry->flags & SH4_PRECISION) && (mode & SH4_MODE_PR) &&
        ((ins->n & 1) || (strstr(entry->text, "%G") != NULL && (ins->m & 1)))) {
        ins->entry = -1;
        return false;
    }
    ins->mnemonic = s_mnemonics[ins->entry];
    ins->flags = entry->flags;
    ins->immediate = strstr(entry->text, "%U") != NULL ? (int32_t)(opcode & 0xFF) : (int32_t)(int8_t)(opcode & 0xFF);
    switch (entry->disp) {
    case DISP_4:
        ins->disp = (opcode & 0xF) * entry->scale;
        break;
    case DISP_8:
        ins->disp = (opcode & 0xFF) * entry->scale;
        break;
    case DISP_PCW:
        ins->disp = (opcode & 0xFF) * 2;
        ins->target = address + 4 + ins->disp;
        ins->literalSize = 2;
        break;
    case DISP_PCL:
        ins->disp = (opcode & 0xFF) * 4;
        ins->target = (address & ~3u) + 4 + ins->disp;
        ins->literalSize = (entry->flags & SH4_LITERAL) ? 4 : 0;
        break;
    case DISP_BR8:
        ins->disp = (int8_t)(opcode & 0xFF) * 2;
        ins->target = address + 4 + ins->disp;
        break;
    case DISP_BR12:
        ins->disp = ((int32_t)((opcode & 0xFFF) << 20) >> 20) * 2;
        ins->target = address + 4 + ins->disp;
        break;
    default:
        break;
    }
    return true;
}

// Name of an FPU register: frN, or drN/xdN for the pairs
static int FormatFloatRegister(char* text, size_t size, int reg, bool pair) {
    if (!pair)
        return snprintf(text, size, "fr%d", reg);
    return snprintf(text, size, (reg & 1) ? "xd%d" : "dr%d", reg & ~1);
}

int SH4_Format(const SH4_Instruction* ins, char* text, size_t size) {
    if (ins->entry < 0)
        return snprintf(text, size, ".word 0x%04X", ins->opcode);
    const Entry* entry = &s_entries[ins->entry];
    bool sizePairs = (entry->flags & SH4_SIZE) && (ins->mode & SH4_MODE_SZ);
    bool precisionPairs = (entry->flags & SH4_PRECISION) && (ins->mode & SH4_MODE_PR);
    size_t length = 0;
    for (const char* p = entry->text; *p != '\0' && length + 1 < size; p++) {
        char* out = text + length;
        size_t room = size - length;
        int written = 1;
        if (*p != '%') {
            // fmov.s moves 64 bits in SZ mode: fmov
            if (sizePairs && p[0] == '.' && p[1] == 's' && p[2] == ' ') {
                p++;
                continue;
            }
            *out = *p;
        } else {
            switch (*++p) {
            case 'N': written = snprintf(out, room, "r%d", ins->n); break;
            case 'M': written = snprintf(out, room, "r%d", ins->m); break;
            case 'F': written = FormatFloatRegister(out, room, ins->n, sizePairs || precisionPairs); break;
            case 'G': written = FormatFloatRegister(out, room, ins->m, sizePairs || precisionPairs); break;
            case 'I': written = snprintf(out, room, "#%d", ins->immediate); break;
            case 'U': written = snprintf(out, room, "#0x%X", (unsigned)ins->immediate); break;
            case 'D': written = snprintf(out, room, "0x%X", (unsigned)ins->disp); break;
            case 'T': written = snprintf(out, room, "0x%X", ins->target); break;
            case 'K': written = snprintf(out, room, "%d", (ins->opcode >> 4) & 7); break;
            case 'V': written = snprintf(out, room, "fv%d", ((ins->opcode >> 10) & 3) * 4); break;
            case 'W': written = snprintf(out, room, "fv%d", ((ins->opcode >> 8) & 3) * 4); break;
            case 'E': written = snprintf(out, room, "dr%d", ((ins->opcode >> 9) & 7) * 2); break;
            default:  written = 0; break;
            }
        }
        if (written < 0)
            break;
        length += (size_t)written < room ? (size_t)written : room - 1;
    }
    text[length < size ? length : size - 1] = '\0';
    return (int)length;
}
