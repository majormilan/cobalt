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

static Standard parse_standard(const char *s) {
    if (!strcmp(s, "COBOL-85")) return STD_COBOL85;
    if (!strcmp(s, "COBOL-2002")) return STD_COBOL2002;
    if (!strcmp(s, "COBOL-2014")) return STD_COBOL2014;
    if (!strcmp(s, "GnuCOBOL")) return STD_GNUCOBOL;
    if (!strcmp(s, "IBM_Enterprise")) return STD_IBM_ENTERPRISE;
    cobalt_error("Unknown COBOL standard: %s", s);
    exit(1);
}

static Strictness parse_strictness(const char *s) {
    if (!strcmp(s, "relaxed")) return STRICT_RELAXED;
    if (!strcmp(s, "normal")) return STRICT_NORMAL;
    if (!strcmp(s, "pedantic")) return STRICT_PEDANTIC;
    if (!strcmp(s, "ibm-strict")) return STRICT_IBM;
    cobalt_error("Unknown strictness level: %s", s);
    exit(1);
}

static Extensions parse_extensions(const char *s) {
    if (!strcmp(s, "none")) return EXT_NONE;
    if (!strcmp(s, "cobalt-basic")) return EXT_COBALT_BASIC;
    cobalt_error("Unknown extension: %s", s);
    exit(1);
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
    printf("  -S, --std STANDARD     COBOL standard (default: COBOL-85)\n");
    printf("      Supported:\n");
    printf("        COBOL-85       (default)\n");
    printf("        COBOL-2002\n");
    printf("        COBOL-2014\n");
    printf("        GnuCOBOL\n");
    printf("        IBM_Enterprise\n");
    printf("  -X, --strictness LEVEL Strictness level (default: normal)\n");
    printf("      Supported:\n");
    printf("        relaxed\n");
    printf("        normal         (default)\n");
    printf("        pedantic\n");
    printf("        ibm-strict\n");
    printf("  -E, --extensions EXT   Enable compiler extensions (default: none)\n");
    printf("      Supported:\n");
    printf("        none           (default)\n");
    printf("        cobalt-basic\n");
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
    reset_diagnostics();
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
    int opt_index = 0;
    static struct option long_options[] = {
        {"input",       required_argument, 0, 'i'},
        {"output",      required_argument, 0, 'o'},
        {"target",      required_argument, 0, 't'},
        {"std",         required_argument, 0, 'S'},
        {"strictness",  required_argument, 0, 'X'},
        {"extensions",  required_argument, 0, 'E'},
        {"help",        no_argument,       0, 0}, // Handled separately
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "i:o:t:S:X:E:ca", long_options, &opt_index)) != -1) {
        switch (opt) {
            case 'i': input_file = optarg; break;
            case 'o': output_file = optarg; break;
            case 't':
                if (!strcmp(optarg, "x86_64-linux-elf")) target = TARGET_LINUX_X86_64_ELF;
                else if (!strcmp(optarg, "x86_64-pc-windows-msvc")) target = TARGET_WINDOWS_X86_64_PE;
                else {
                    cobalt_error("Unknown target: %s", optarg);
                    return 1;
                }
                break;
            case 'S': mode.std = parse_standard(optarg); break;
            case 'X': mode.strictness = parse_strictness(optarg); break;
            case 'E': mode.extensions = parse_extensions(optarg); break;
            case 'c': compile_only = true; break;
            case 'a': dump_ast = true; break;
            case 0: // long option without a short option
                if (strcmp(long_options[opt_index].name, "help") == 0) {
                    print_help(argv[0]);
                    return 0;
                }
                break;
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
    Lexer lexer;
    lexer_init(&lexer, source, &mode, input_file);
    AstNode *ast = parse_program(&lexer);

    if (!ast) {
        free(source);
        return 1;
    }

    SemaContext ctx = { .mode = &mode, .original_source = source };
    sema_basic_check(ast, &ctx);

    // If there were any errors during parsing or semantic analysis, exit with a non-zero status
    if (error_count > 0) {
        ast_free(ast);
        free(source);
        return 1;
    }

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
        printf("  make runtime TARGET=x86_64-linux-elf\n");
        printf("  ld \"%s.o\" \"obj/runtime/x86_64-linux-elf/libcobrt.a\" -o \"%s\"\n", exe_name, exe_name);
        printf("  ./%s\n", exe_name);
    }

    return 0;
}
