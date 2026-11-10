// Build tool: writes a C file that defines the contents of a file as an array of bytes, so the executable carries it.
// Usage: embed <input> <output.c> <symbol>
// Defines `const unsigned char symbol[]` (the bytes of the file followed by a '\0') and `const size_t symbolSize` (the
// size of the file, without the '\0')
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char** argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: embed <input> <output.c> <symbol>\n");
        return 1;
    }
    FILE* in = fopen(argv[1], "rb");
    if (in == NULL) {
        fprintf(stderr, "embed: can't open %s\n", argv[1]);
        return 1;
    }
    FILE* out = fopen(argv[2], "w");
    if (out == NULL) {
        fprintf(stderr, "embed: can't create %s\n", argv[2]);
        fclose(in);
        return 1;
    }

    // An array of numbers, not a string literal: MSVC limits string literals to 64 KiB
    fprintf(out, "// Generated from %s by tools/embed.c: don't edit\n", argv[1]);
    fprintf(out, "#include <stddef.h>\n\nconst unsigned char %s[] = {\n", argv[3]);
    unsigned char buffer[4096];
    size_t size = 0;
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        for (size_t i = 0; i < read; i++, size++)
            fprintf(out, "%u,%s", buffer[i], size % 32 == 31 ? "\n" : "");
    }
    fprintf(out, "0\n};\n\nconst size_t %sSize = %zu;\n", argv[3], size);

    fclose(in);
    if (fclose(out) != 0) {
        fprintf(stderr, "embed: can't write %s\n", argv[2]);
        return 1;
    }
    return 0;
}
