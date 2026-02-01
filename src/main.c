/* src/main.c */
#include "lexer.h"
#include "parser.h"
#include "diag.h"
#include "ast.h"
#include "config.h"
#include "sema.h"
#include "ir.h"
#include "target.h"
#include "backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>

char *current_filename = NULL;

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        cobalt_error("Could not open input file: %s", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    if (fread(buf, 1, len, f) != len) {
        cobalt_error("Failed to read file: %s", path);
        exit(1);
    }
    buf[len] = '\0';
    fclose(f);
    return buf;
}

static void print_help(const char *progname) {
    printf("COBALT - tiny COBOL compiler\n\n");
    printf("Usage: %s [OPTIONS] -i <input.cbl>\n\n", progname);

    printf("Required:\n");
    printf("  -i, --input FILE       Input COBOL source file (.cbl)\n\n");

    printf("Output control:\n");
    printf("  -o, --output FILE      Output file name (default: <input>.asm)\n");
    printf("                         If -c is used: assembly file only\n");
    printf("                         Without -c: assembly file + build instructions\n\n");

    printf("Mode control:\n");
    printf("  -c                     Compile only (emit .asm file, no linking)\n");
    printf("  -a                     Always dump AST (debug parsing)\n\n");

    printf("Target control:\n");
    printf("  -t, --target TRIPLE    Target triple (default: x86_64-linux-elf)\n");
    printf("      Supported:\n");
    printf("        x86_64-linux-elf         Linux x86_64 (default)\n");
    printf("        x86_64-pc-windows-msvc   Windows x86_64 (future)\n\n");

    printf("Debug:\n");
    printf("  --help                 Show this help\n\n");

    printf("Examples:\n");
    printf("  %s -i hello.cbl                          # → hello.asm + build instructions\n", progname);
    printf("  %s -i test.cbl -c                        # → test.asm only\n", progname);
    printf("  %s -i test.cbl -o prog.asm -a           # → prog.asm + AST dump\n", progname);
    printf("  %s -i test.cbl -o hello.exe -t x86_64-pc-windows-msvc  # → hello.exe.asm (future)\n", progname);
}

int main(int argc, char **argv) {
    if (argc == 1 || strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return 0;
    }

    char *input_file = NULL;
    char *output_file = NULL;
    TargetTriple target = TARGET_LINUX_X86_64_ELF;
    bool compile_only = false;
    bool dump_ast = false;

    CompilerMode mode = {
        .std = STD_COBOL85,
        .strictness = STRICT_NORMAL,
        .extensions = EXT_NONE
    };

    int opt;
    while ((opt = getopt(argc, argv, "i:o:t:ca")) != -1) {
        switch (opt) {
            case 'i': input_file = optarg; break;
            case 'o': output_file = optarg; break;
            case 't':
                if (!strcmp(optarg, "x86_64-linux-elf")) target = TARGET_LINUX_X86_64_ELF;
                else if (!strcmp(optarg, "x86_64-pc-windows-msvc")) target = TARGET_WINDOWS_X86_64_PE;
                else cobalt_error("Unknown target: %s", optarg);
                break;
            case 'c': compile_only = true; break;
            case 'a': dump_ast = true; break;
            default:
                cobalt_error("Unknown option -%c", optopt);
                return 1;
        }
    }

    if (!input_file) {
        cobalt_error("Missing required -i <input_file>");
        return 1;
    }

    if (access(input_file, R_OK) != 0) {
        cobalt_error("Input file not readable: %s", input_file);
        return 1;
    }

    current_filename = input_file;

    char *source = read_file(input_file);
    lexer_init(source);
    AstNode *ast = parse_program(&mode, source);

    if (!ast) {
        free(source);
        return 1;
    }

    SemaContext ctx = { .mode = &mode };
    sema_basic_check(ast, &ctx);

    if (dump_ast) {
        printf("AST dump:\n");
        ast_dump(ast, stdout, 0);
        printf("\n");

        ast_free(ast);
        free(source);
        return 0;
    }

    IrProgram *ir = lower_to_ir(ast);
    if (!ir) {
        fprintf(stderr, "IR lowering failed\n");
        ast_free(ast);
        free(source);
        return 1;
    }

    // Determine output filename
    char asm_filename[512];
    if (output_file) {
        strncpy(asm_filename, output_file, sizeof(asm_filename) - 1);
        asm_filename[sizeof(asm_filename) - 1] = '\0';
    } else {
        const char *base = strrchr(input_file, '/');
        base = base ? base + 1 : input_file;
        char temp[256];
        strncpy(temp, base, sizeof(temp) - 1);
        temp[sizeof(temp) - 1] = '\0';
        char *dot = strrchr(temp, '.');
        if (dot) *dot = '\0';
        snprintf(asm_filename, sizeof(asm_filename), "%s.asm", temp);
    }

    backend_emit_object(ir, asm_filename, target);

    ir_free(ir);
    ast_free(ast);
    free(source);

    printf("Assembly written to: %s\n", asm_filename);

    // Build instructions only if not compile-only mode
    if (!compile_only && target == TARGET_LINUX_X86_64_ELF) {
        const char *exe_name = output_file ? output_file : "a.out";
        printf("\nTo build and run (Linux):\n");
        printf("  nasm -f elf64 \"%s\" -o \"%s.o\"\n", asm_filename, exe_name);
        printf("  ld \"%s.o\" -o \"%s\"\n", exe_name, exe_name);
        printf("  ./%s\n", exe_name);
    }

    return 0;
}
