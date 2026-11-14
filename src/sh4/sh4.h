#ifndef __SH4_H__
#define __SH4_H__

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Disassembler of the SH-4 (Hitachi SH7750, the CPU of the Dreamcast): the whole instruction set, with the FPU. The
// instructions are 16 bits, little-endian in the files. The branches have a delay slot (the next instruction runs before
// the jump), and the PC-relative loads (mov.w and mov.l @(disp,PC)) read a literal placed after the code

// Flags of an instruction
#define SH4_BRANCH          0x0001  // Can change the flow: bf, bt, bra, braf, bsr, bsrf, jmp, jsr, rts, rte
#define SH4_DELAYED         0x0002  // Has a delay slot: the next instruction runs before the branch
#define SH4_CONDITIONAL     0x0004  // bf, bt, bf/s, bt/s
#define SH4_CALL            0x0008  // bsr, bsrf, jsr: returns to the instruction after the delay slot
#define SH4_RETURN          0x0010  // rts, rte
#define SH4_INDIRECT        0x0020  // The target is in a register: braf, bsrf, jmp, jsr
#define SH4_LITERAL         0x0040  // mov.w or mov.l @(disp,PC),Rn: loads the literal at target
#define SH4_ADDRESS         0x0080  // mova @(disp,PC),R0: loads the address target
#define SH4_FPU             0x0100  // Floating-point instruction
#define SH4_PRECISION       0x0200  // Its operands are double registers if FPSCR.PR is set
#define SH4_SIZE            0x0400  // fmov: moves pairs of registers if FPSCR.SZ is set
#define SH4_PRIVILEGED      0x0800  // Only in privileged mode (rte, ldc to SR, sleep...)
#define SH4_SLOT_ILLEGAL    0x1000  // Can't be in a delay slot

// Mode of the FPU (bits of FPSCR) that changes the meaning of some FPU instructions
#define SH4_MODE_PR         0x00080000  // Double precision
#define SH4_MODE_SZ         0x00100000  // fmov moves 64 bits

typedef struct {
    uint16_t    opcode;
    uint32_t    address;    // Of the instruction
    int         entry;      // Index in the table of instructions, -1 if the opcode isn't a valid instruction
    const char* mnemonic;   // e.g. "mov.l", NULL if invalid
    uint32_t    flags;      // SH4_BRANCH...
    int         n, m;       // Register fields (bits 8-11 and 4-7)
    int32_t     immediate;  // #imm, signed or not as the instruction reads it
    int32_t     disp;       // Displacement in bytes, scaled by the size of the access
    uint32_t    target;     // Branch target (not for the indirect ones), address of the literal or of the mova
    uint32_t    literalSize;// 2 (mov.w) or 4 (mov.l), 0 otherwise
    uint32_t    mode;       // SH4_MODE_* used to decode it
} SH4_Instruction;

// Decodes an opcode at an address, in the given FPU mode (0: single precision, 32-bit fmov, the mode at the start of
// the programs). Returns false if it isn't a valid instruction (ins->mnemonic is NULL)
bool SH4_Decode(uint16_t opcode, uint32_t address, uint32_t mode, SH4_Instruction* ins);
// Writes the instruction in the Hitachi syntax, in lower case, e.g. "mov.l @(0x1C,pc),r1", "bra 0x420", "fmul fr4,fr5".
// The PC-relative operands show the displacement; the branches, the target. Returns the length written (snprintf)
int SH4_Format(const SH4_Instruction* ins, char* text, size_t size);
// Number of entries of the table of instructions
int SH4_EntryCount(void);
// Mnemonic and operands of an entry, e.g. "mov.l @(%D,pc),%N" (see sh4.c)
const char* SH4_EntryTemplate(int entry);

#endif
