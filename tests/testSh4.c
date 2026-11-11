#include <stdlib.h>
#include <string.h>
#include "core/bytes.h"
#include "corpus.h"
#include "formats/map/mapinfo.h"
#include "formats/script/scn.h"
#include "formats/script/sh4.h"
#include "formats/script/sh4Code.h"
#include "test.h"

// The SH-4 disassembler: the encodings of the manual of the SH-4, and the code of the SH-4 scene programs

typedef struct {
    uint16_t    opcode;
    uint32_t    address;
    uint32_t    mode;
    const char* text;
} Encoding;

static const Encoding s_encodings[] = {
    // The prologue of the programs and of their entry point
    { 0x2F86, 0x30, 0, "mov.l r8,@-r15" },
    { 0x2DE6, 0x30, 0, "mov.l r14,@-r13" },
    { 0x4F22, 0, 0, "sts.l pr,@-r15" },
    { 0x4F26, 0, 0, "lds.l @r15+,pr" },
    { 0x000B, 0, 0, "rts" },
    { 0x0009, 0, 0, "nop" },
    // Immediates
    { 0xE0FF, 0, 0, "mov #-1,r0" },
    { 0x7FFC, 0, 0, "add #-4,r15" },
    { 0xC9FF, 0, 0, "and #0xFF,r0" },
    { 0x8800, 0, 0, "cmp/eq #0,r0" },
    { 0xC320, 0, 0, "trapa #0x20" },
    // Displacements, scaled by the size of the access
    { 0x5123, 0, 0, "mov.l @(0xC,r2),r1" },
    { 0x1123, 0, 0, "mov.l r2,@(0xC,r1)" },
    { 0x8123, 0, 0, "mov.w r0,@(0x6,r2)" },
    { 0x8523, 0, 0, "mov.w @(0x6,r2),r0" },
    { 0x8423, 0, 0, "mov.b @(0x3,r2),r0" },
    { 0xC612, 0, 0, "mov.l @(0x48,gbr),r0" },
    { 0x061C, 0, 0, "mov.b @(r0,r1),r6" },
    { 0x0126, 0, 0, "mov.l r2,@(r0,r1)" },
    // PC-relative
    { 0xD103, 0x100, 0, "mov.l @(0xC,pc),r1" },
    { 0x9105, 0x100, 0, "mov.w @(0xA,pc),r1" },
    { 0xC703, 0x102, 0, "mova @(0xC,pc),r0" },
    // Branches
    { 0xA0FF, 0x200, 0, "bra 0x402" },
    { 0xAFFE, 0x200, 0, "bra 0x200" },
    { 0x8BFE, 0x200, 0, "bf 0x200" },
    { 0x8901, 0, 0, "bt 0x6" },
    { 0x8D02, 0, 0, "bt/s 0x8" },
    { 0xB001, 0, 0, "bsr 0x6" },
    { 0x0123, 0, 0, "braf r1" },
    { 0x0103, 0, 0, "bsrf r1" },
    { 0x412B, 0, 0, "jmp @r1" },
    { 0x410B, 0, 0, "jsr @r1" },
    // System
    { 0x418E, 0, 0, "ldc r1,r0_bank" },
    { 0x41FE, 0, 0, "ldc r1,r7_bank" },
    { 0x40FA, 0, 0, "ldc r0,dbr" },
    { 0x0182, 0, 0, "stc r0_bank,r1" },
    { 0x003A, 0, 0, "stc sgr,r0" },
    { 0x4108, 0, 0, "shll2 r1" },
    { 0x401B, 0, 0, "tas.b @r0" },
    { 0x000F, 0, 0, "mac.l @r0+,@r0+" },
    // FPU, single precision
    { 0xF46D, 0, 0, "fsqrt fr4" },
    { 0xF45C, 0, 0, "fmov fr5,fr4" },
    { 0xF418, 0, 0, "fmov.s @r1,fr4" },
    { 0xF31B, 0, 0, "fmov.s fr1,@-r3" },
    { 0xF56E, 0, 0, "fmac fr0,fr6,fr5" },
    { 0xF012, 0, 0, "fmul fr1,fr0" },
    { 0xF22D, 0, 0, "float fpul,fr2" },
    { 0xF13D, 0, 0, "ftrc fr1,fpul" },
    { 0xF08D, 0, 0, "fldi0 fr0" },
    { 0xF2FD, 0, 0, "fsca fpul,dr2" },
    { 0xF1FD, 0, 0, "ftrv xmtrx,fv0" },
    { 0xF9FD, 0, 0, "ftrv xmtrx,fv8" },
    { 0xF8ED, 0, 0, "fipr fv0,fv8" },
    { 0xF3FD, 0, 0, "fschg" },
    { 0xFBFD, 0, 0, "frchg" },
    { 0x405A, 0, 0, "lds r0,fpul" },
    { 0x4F62, 0, 0, "sts.l fpscr,@-r15" },
    // FPU, double precision and 64-bit fmov
    { 0xF46D, 0, SH4_MODE_PR, "fsqrt dr4" },
    { 0xF460, 0, SH4_MODE_PR, "fadd dr6,dr4" },
    { 0xF45C, 0, SH4_MODE_SZ, "fmov xd4,dr4" },
    { 0xF418, 0, SH4_MODE_SZ, "fmov @r1,dr4" },
    { 0xF31B, 0, SH4_MODE_SZ, "fmov xd0,@-r3" },
    // Invalid
    { 0x0000, 0, 0, ".word 0x0000" },
    { 0xFFFF, 0, 0, ".word 0xFFFF" },
    { 0xFFFD, 0, 0, ".word 0xFFFD" },
    { 0xF56D, 0, SH4_MODE_PR, ".word 0xF56D" },
};

typedef struct {
    uint16_t    opcode;
    uint32_t    flags;      // Expected flags among SH4_BRANCH...SH4_ADDRESS
    uint32_t    target;
} FlagCase;

static const FlagCase s_flagCases[] = {
    { 0x000B, SH4_BRANCH | SH4_DELAYED | SH4_RETURN, 0 },
    { 0x410B, SH4_BRANCH | SH4_DELAYED | SH4_CALL | SH4_INDIRECT, 0 },
    { 0xB001, SH4_BRANCH | SH4_DELAYED | SH4_CALL, 0x6 },
    { 0x8B01, SH4_BRANCH | SH4_CONDITIONAL, 0x6 },
    { 0x8F01, SH4_BRANCH | SH4_CONDITIONAL | SH4_DELAYED, 0x6 },
    { 0xD101, SH4_LITERAL, 0x8 },
    { 0xC701, SH4_ADDRESS, 0x8 },
    { 0x6003, 0, 0 },
};

#define SH4_DISASSEMBLY_HASH 0x8668EDB1u // See Test_Sh4Encodings

#define FLOW_FLAGS (SH4_BRANCH | SH4_DELAYED | SH4_CONDITIONAL | SH4_CALL | SH4_RETURN | SH4_INDIRECT | SH4_LITERAL | \
    SH4_ADDRESS)

void Test_Sh4Encodings(const TestContext* context) {
    (void)context;
    for (size_t i = 0; i < sizeof(s_encodings) / sizeof(s_encodings[0]); i++) {
        const Encoding* e = &s_encodings[i];
        SH4_Instruction ins;
        bool valid = SH4_Decode(e->opcode, e->address, e->mode, &ins);
        char text[64];
        SH4_Format(&ins, text, sizeof(text));
        CHECK(strcmp(text, e->text) == 0, "0x%04X: \"%s\", expected \"%s\"", e->opcode, text, e->text);
        CHECK(valid == (strncmp(e->text, ".word", 5) != 0), "0x%04X: valid %d", e->opcode, valid);
    }
    for (size_t i = 0; i < sizeof(s_flagCases) / sizeof(s_flagCases[0]); i++) {
        const FlagCase* c = &s_flagCases[i];
        SH4_Instruction ins;
        SH4_Decode(c->opcode, 0, 0, &ins);
        CHECK((ins.flags & FLOW_FLAGS) == c->flags, "0x%04X: flags 0x%X, expected 0x%X", c->opcode,
            ins.flags & FLOW_FLAGS, c->flags);
        CHECK(ins.target == c->target, "0x%04X: target 0x%X, expected 0x%X", c->opcode, ins.target, c->target);
    }
    SH4_Instruction literal;
    SH4_Decode(0x9101, 0x10, 0, &literal);
    CHECK(literal.literalSize == 2 && literal.target == 0x16, "mov.w literal: size %u, target 0x%X",
        literal.literalSize, literal.target);

    // Every entry is reachable (no entry is hidden by another one), and the disassembly of the 65536 opcodes is the one
    // checked against the SuperH4:LE:32 language of Ghidra 12.1.4: the same instructions, operands and targets for the
    // 59001 valid opcodes (each one at address opcode × 4, followed by a nop). Ghidra also accepts the SH-4A
    // instructions (movua.l, icbi, synco, fpchg), absent from the SH-4 of the Dreamcast, and 0xFFFD as fsca, whose
    // bit 8 must be 0; it names fmov.s FRm,@(R0,Rn) "fmov". The hash (FNV-1a of the lines "opcode<TAB>text\n", text
    // "?" for the invalid opcodes) detects any change of the disassembly
    uint32_t valid = 0, hash = 2166136261u;
    int* uses = calloc((size_t)SH4_EntryCount(), sizeof(int));
    for (uint32_t opcode = 0; opcode < 0x10000 && uses != NULL; opcode++) {
        SH4_Instruction ins;
        char line[96], text[64];
        if (SH4_Decode((uint16_t)opcode, opcode * 4, 0, &ins)) {
            valid++;
            uses[ins.entry]++;
            SH4_Format(&ins, text, sizeof(text));
        } else {
            strcpy(text, "?");
        }
        snprintf(line, sizeof(line), "%04X\t%s\n", opcode, text);
        for (const char* c = line; *c != '\0'; c++)
            hash = (hash ^ (uint8_t)*c) * 16777619u;
    }
    for (int e = 0; e < SH4_EntryCount() && uses != NULL; e++)
        CHECK(uses[e] > 0, "entry \"%s\" never decoded", SH4_EntryTemplate(e));
    free(uses);
    CHECK_EQ(valid, 59001, "valid opcodes");
    CHECK_EQ(hash, SH4_DISASSEMBLY_HASH, "hash of the disassembly of every opcode");
    Test_Info("%d instructions, %u valid opcodes of 65536, hash 0x%08X", SH4_EntryCount(), valid, hash);
}

// The code of the SH-4 programs, explored from the start of the code and the entry point: every word reached is a valid
// instruction, no branch is in a delay slot, no word is both code and literal, every branch and literal is inside the
// code and the offsets of all the braf and bsrf are literals
void Test_Sh4Code(const TestContext* context) {
    CorpusList list = Corpus_MapinfoFiles(context->content);
    uint32_t programs = 0, slotErrors = 0;
    SH4_CodeStats total = { 0 };
    uint64_t words = 0, calls = 0, relativeCalls = 0, relativeJumps = 0, jumps = 0, fpu = 0, privileged = 0;
    uint64_t prologues = 0; // Functions that start with mov.l r14,@-r13 and sts.l pr,@-r13
    uint32_t counts[256] = { 0 };
    for (uint32_t i = 0; i < list.count; i++) {
        const char* name = list.files[i].name;
        MAPINFO_File file;
        if (!MAPINFO_Load(list.files[i].path, &file))
            continue;
        const MAPINFO_Chunk* chunk = MAPINFO_FindChunk(&file, "SCN3");
        SCN_Program program;
        if (chunk == NULL || !SCN_Read(file.data + chunk->offset, chunk->size, &program) || !SCN_IsSH4(&program)) {
            MAPINFO_Free(&file);
            continue;
        }
        programs++;
        const SCN_Header* header = &program.header;
        uint32_t roots[2] = { header->codeStart, header->entry };
        SH4_Code code;
        if (!SH4_ExploreCode(&code, program.data, header->codeStart, header->dataStart, roots, 2)) {
            CHECK(false, "%s: memory error", name);
            MAPINFO_Free(&file);
            continue;
        }
        const SH4_CodeStats* stats = &code.stats;
        CHECK(stats->invalid + stats->illegalSlots + stats->overlaps + stats->outside + stats->unresolved == 0,
            "%s: %u invalid, %u illegal slots, %u overlaps, %u outside, %u unresolved (first at 0x%X)", name,
            stats->invalid, stats->illegalSlots, stats->overlaps, stats->outside, stats->unresolved, stats->firstError);
        CHECK(SH4_WordKind(&code, header->entry) & SH4_WORD_FUNCTION, "%s: the entry point isn't a function", name);
        total.instructions += stats->instructions;
        total.literals += stats->literals;
        total.unknown += stats->unknown;
        total.functions += stats->functions;
        total.invalid += stats->invalid;
        total.illegalSlots += stats->illegalSlots;
        total.overlaps += stats->overlaps;
        total.outside += stats->outside;
        total.unresolved += stats->unresolved;
        words += (header->dataStart - header->codeStart) / 2;
        for (uint32_t pc = header->codeStart; pc < header->dataStart; pc += 2) {
            SH4_Instruction ins;
            uint8_t kind = SH4_WordKind(&code, pc);
            if ((kind & SH4_WORD_FUNCTION) && pc != header->codeStart)
                prologues += Bytes_ReadU16(program.data + pc) == 0x2DE6 &&
                    Bytes_ReadU16(program.data + pc + 2) == 0x4D22;
            if (!(kind & SH4_WORD_CODE) || !SH4_Decode(Bytes_ReadU16(program.data + pc), pc, 0, &ins))
                continue;
            counts[ins.entry]++;
            relativeJumps += strcmp(ins.mnemonic, "braf") == 0;
            calls += strcmp(ins.mnemonic, "jsr") == 0;
            relativeCalls += strcmp(ins.mnemonic, "bsrf") == 0 || strcmp(ins.mnemonic, "bsr") == 0;
            jumps += strcmp(ins.mnemonic, "jmp") == 0;
            fpu += (ins.flags & SH4_FPU) != 0;
            privileged += (ins.flags & SH4_PRIVILEGED) != 0;
            slotErrors += (kind & SH4_WORD_SLOT) && (ins.flags & SH4_SLOT_ILLEGAL);
        }
        SH4_FreeCode(&code);
        MAPINFO_Free(&file);
    }
    CHECK_EQ(programs, 119, "SH-4 programs");
    CHECK_EQ(total.invalid, 0, "invalid instructions reached");
    CHECK_EQ(total.illegalSlots + slotErrors, 0, "branches in a delay slot");
    CHECK_EQ(total.overlaps, 0, "words that are code and literal");
    CHECK_EQ(total.outside, 0, "branches and literals outside the code");
    CHECK_EQ(total.unresolved, 0, "braf and bsrf without a literal offset");
    CHECK_EQ(privileged, 0, "privileged instructions");
    CHECK(total.unknown * 50 < words, "%u words not reached, 2%% or more of %llu", total.unknown,
        (unsigned long long)words);
    // Every function but the code at codeStart starts with the prologue
    CHECK_EQ(prologues, total.functions - programs, "functions with the prologue");
    Test_Info("%u programs: %llu words, %u instructions, %u literal words, %u not reached (%.2f%%)", programs,
        (unsigned long long)words, total.instructions, total.literals, total.unknown,
        words ? 100.0 * total.unknown / (double)words : 0.0);
    Test_Info("%u functions, %llu with the prologue mov.l r14,@-r13; sts.l pr,@-r13", total.functions,
        (unsigned long long)prologues);
    Test_Info("%llu jsr, %llu bsr/bsrf, %llu braf, %llu jmp, %llu FPU instructions", (unsigned long long)calls,
        (unsigned long long)relativeCalls, (unsigned long long)relativeJumps, (unsigned long long)jumps,
        (unsigned long long)fpu);
    // The most used instructions
    for (int shown = 0; shown < 12; shown++) {
        int best = -1;
        for (int e = 0; e < SH4_EntryCount(); e++) {
            if (counts[e] > 0 && (best < 0 || counts[e] > counts[best]))
                best = e;
        }
        if (best < 0)
            break;
        Test_Info("  %-24s %u", SH4_EntryTemplate(best), counts[best]);
        counts[best] = 0;
    }
    Corpus_Free(&list);
}
