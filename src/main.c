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
#include "asm.h"
#include "link.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "pp.h"
#include <unistd.h>

char *current_filename = NULL;

// The runtime's objects for each target, embedded in cobalt when it is built
// (Makefile: the runtime is assembled by the first-stage cobalt, then linked
// into the final one). The first stage has none: it can still produce
// assembly (-A) and objects (-c).
typedef struct {
    const uint8_t *data;
    uint64_t size;
} EmbeddedObject;
extern const EmbeddedObject cobalt_runtime_x86_64_linux_elf[] __attribute__((weak));

static const EmbeddedObject *embedded_runtime(const TargetInfo *target) {
    if (target->triple == TARGET_LINUX_X86_64_ELF) return cobalt_runtime_x86_64_linux_elf;
    return NULL;
}

typedef enum { EMIT_EXECUTABLE, EMIT_OBJECT, EMIT_ASSEMBLY } EmitKind;

static uint8_t *read_binary(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(n > 0 ? n : 1);
    if (data && fread(data, 1, n, f) != (size_t)n) { free(data); data = NULL; }
    fclose(f);
    *size = (size_t)n;
    return data;
}

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
    printf("  -i, --input FILE       Input COBOL source file (.cbl); several sources, or\n");
    printf("                         source and object arguments, make one run unit\n");
    printf("  --module               No main program: every program is a called one\n");
    printf("  --flag-obsolete        Warn about the obsolete elements of COBOL-85 (FIPS flagging)\n\n");

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
    printf("  -D, --dialect NAME     Vendor dialect on top of the standard (default: standard)\n");
    printf("      Supported:\n");
    printf("        standard       (default) the standard's rules\n");
    printf("        ibm            IBM Enterprise COBOL\n");
    printf("        gnucobol       GnuCOBOL\n");
    printf("        microfocus     Micro Focus COBOL\n");
    printf("  -f KEY=VALUE           Override one dialect setting (after --dialect):\n");
    printf("        display=stored|pretty        numeric DISPLAY format\n");
    printf("        sign=ascii|ebcdic            overpunched sign encoding\n");
    printf("        binary-truncate=yes|no       COMP items limited to PICTURE digits\n");
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
    printf("  --fixed                Fixed reference format: columns 8-72 (default)\n");
    printf("  -F, --free             Free format source\n");
    printf("  -I DIR                 Copy library directory (also: COBCPY=dir:dir...)\n");
    printf("  -a                     Always dump AST (debug parsing)\n\n");

    printf("Output (the default is an executable, assembled and linked by cobalt):\n");
    printf("  -o FILE                Output file (default: the input name without .cbl,\n");
    printf("                         .o with -c, .asm with -A)\n");
    printf("  -c                     Compile to an object file only\n");
    printf("  -A, --asm              Compile to assembly only (NASM syntax)\n");
    printf("  --strip                Executable without symbols\n\n");

    printf("Assembler:\n");
    printf("  %s as [-f elf64] [-D NAME[=value]] [-I dir/] [-MD file] [-MP] [-o out.o] in.asm\n\n", progname);

    printf("Target control:\n");
    printf("  -t, --target TRIPLE    Target triple (default: x86_64-linux-elf)\n");
    printf("      Known:\n");
    for (int t = 0; t < TARGET_COUNT; t++) {
        if (backend_supports(&target_infos[t]))
            printf("        %s\n", target_infos[t].name);
        else
            printf("        %-24s (not yet supported)\n", target_infos[t].name);
    }
    printf("\n");

    printf("Debug:\n");
    printf("  --help                 Show this help\n\n");

    printf("Examples:\n");
    printf("  %s -i hello.cbl                    # executable ./hello\n", progname);
    printf("  %s -i hello.cbl -o bin/hello -A    # assembly bin/hello (NASM syntax)\n", progname);
    printf("  %s -i hello.cbl -c                 # object hello.o\n", progname);
}

int main(int argc, char **argv) {
    reset_diagnostics();
    if (argc > 1 && strcmp(argv[1], "as") == 0) return asm_command(argc - 1, argv + 1);
    if (argc == 1 || strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return 0;
    }

    char *input_file = NULL;
    const char *sources[64];            // COBOL sources (-i, or arguments)
    int nsources = 0;
    const char *objects[64];            // objects to link (arguments ending in .o)
    int nobjects = 0;
    bool module = false;                // every program is a called program
    char *output_file = NULL;
    const TargetInfo *target = target_get_info(TARGET_LINUX_X86_64_ELF);
    EmitKind emit = EMIT_EXECUTABLE;
    bool strip = false;
    bool dump_ast = false;

    CompilerMode mode = {
        .std = STD_COBOL85,
        .strictness = STRICT_NORMAL,
        .extensions = EXT_NONE
    };
    dialect_select(&mode.dialect, "standard");

    const char *copy_dirs[32];
    int ncopy_dirs = 0;
    int opt;
    int opt_index = 0;
    static struct option long_options[] = {
        {"input",       required_argument, 0, 'i'},
        {"output",      required_argument, 0, 'o'},
        {"target",      required_argument, 0, 't'},
        {"std",         required_argument, 0, 'S'},
        {"strictness",  required_argument, 0, 'X'},
        {"extensions",  required_argument, 0, 'E'},
        {"free",        no_argument,       0, 'F'},
        {"fixed",       no_argument,       0, 'R'},
        {"dialect",     required_argument, 0, 'D'},
        {"asm",         no_argument,       0, 'A'},
        {"strip",       no_argument,       0, 0},
        {"module",      no_argument,       0, 'M'},
        {"flag-obsolete", no_argument,     0, 0},
        {"help",        no_argument,       0, 0}, // Handled separately
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "i:o:t:S:X:E:D:f:I:caAFM", long_options, &opt_index)) != -1) {
        switch (opt) {
            case 'i':
                if (nsources < 64) sources[nsources++] = optarg;
                break;
            case 'M': module = true; break;
            case 'o': output_file = optarg; break;
            case 't':
                target = target_from_name(optarg);
                if (!target) {
                    cobalt_error("Unknown target: %s (see --help for known targets)", optarg);
                    return 1;
                }
                break;
            case 'S': mode.std = parse_standard(optarg); break;
            case 'X': mode.strictness = parse_strictness(optarg); break;
            case 'E': mode.extensions = parse_extensions(optarg); break;
            case 'D':
                if (!dialect_select(&mode.dialect, optarg)) {
                    cobalt_error("Unknown dialect: %s (see --help)", optarg);
                    return 1;
                }
                break;
            case 'f':
                if (!dialect_set_option(&mode.dialect, optarg)) return 1;
                break;
            case 'F': mode.free_format = true; break;
            case 'I':
                if (ncopy_dirs < 32) copy_dirs[ncopy_dirs++] = optarg;
                break;
            case 'R': mode.free_format = false; break;
            case 'c': emit = EMIT_OBJECT; break;
            case 'A': emit = EMIT_ASSEMBLY; break;
            case 'a': dump_ast = true; break;
            case 0: // long option without a short option
                if (strcmp(long_options[opt_index].name, "help") == 0) {
                    print_help(argv[0]);
                    return 0;
                }
                if (strcmp(long_options[opt_index].name, "strip") == 0) strip = true;
                if (strcmp(long_options[opt_index].name, "flag-obsolete") == 0) mode.flag_obsolete = true;
                break;
            default:
                cobalt_error("Unknown option -%c", optopt);
                return 1;
        }
    }

    if (!backend_supports(target)) {
        cobalt_error("Target %s is not supported yet", target->name);
        return 1;
    }

    for (int k = optind; k < argc; k++) {
        size_t n = strlen(argv[k]);
        if (n > 2 && strcmp(argv[k] + n - 2, ".o") == 0) {
            if (nobjects < 64) objects[nobjects++] = argv[k];
        } else if (nsources < 64) {
            sources[nsources++] = argv[k];
        }
    }
    if (nsources == 0) {
        cobalt_error("Missing required -i <input_file>");
        return 1;
    }
    input_file = (char *)sources[0];
    for (int k = 0; k < nsources; k++) {
        if (access(sources[k], R_OK) != 0) {
            cobalt_error("Input file not readable: %s", sources[k]);
            return 1;
        }
    }

    // Every program of every source, nested programs after their container:
    // the first one is the run unit's main program (unless --module)
    AstNode *programs[256];
    int program_parent[256];            // the program containing it, or -1
    char *texts[64], *sources_text[64];
    int nprograms = 0;
    for (int k = 0; k < nsources; k++) {
        current_filename = (char *)sources[k];
        char *source = read_file(sources[k]);
        lexer_set_debugging_lines(!mode.free_format && lexer_debugging_mode(source));
        if (!mode.free_format) {
            char *converted = lexer_reference_format(source);
            free(source);
            source = converted;
        }
        // COPY and REPLACE; the lexer reads the result, diagnostics show the
        // lines of the source
        PpOptions pp = { copy_dirs, ncopy_dirs, mode.free_format };
        char *text = pp_process(source, sources[k], &pp);
        if (!text) { free(source); return 1; }
        Lexer lexer;
        lexer_init(&lexer, source, &mode, sources[k]);
        lexer.src = text;
        int errors_before = error_count;
        AstNode *ast = parse_program(&lexer);
        error_count += errors_before;   // parse_program starts counting anew
        texts[k] = text;
        sources_text[k] = source;
        if (!ast) return 1;
        SemaContext ctx = { .mode = &mode, .original_source = source };
        // Programs in order, each followed by the programs nested in it
        AstNode *stack[64];
        int stack_parent[64];
        int depth = 0;
        for (AstNode *p = ast; p; p = p->next) { stack_parent[depth] = -1; stack[depth++] = p; }
        // (reverse so the first program comes first)
        for (int lo = 0, hi = depth - 1; lo < hi; lo++, hi--) {
            AstNode *t = stack[lo]; stack[lo] = stack[hi]; stack[hi] = t;
        }
        while (depth > 0) {
            depth--;
            AstNode *p = stack[depth];
            int parent = stack_parent[depth];
            int me = nprograms;
            if (nprograms < 256) {
                program_parent[nprograms] = parent;
                programs[nprograms++] = p;
            }
            sema_basic_check(p, &ctx);
            AstNode *nested[64];
            int nn = 0;
            for (AstNode *c = p->children; c; c = c->next)
                if (c->kind == AST_PROGRAM && nn < 64) nested[nn++] = c;
            for (int j = nn - 1; j >= 0 && depth < 64; j--) {
                stack_parent[depth] = me;
                stack[depth++] = nested[j];
            }
        }
        if (dump_ast) {
            printf("AST dump:\n");
            ast_dump(ast, stdout, 0);
            printf("\n");
        }
    }
    current_filename = input_file;
    if (error_count > 0) return 1;
    if (dump_ast) return 0;

    // Output file name: as given, or the input's name without its extension
    char out_name[512];
    if (output_file) {
        snprintf(out_name, sizeof(out_name), "%s", output_file);
    } else {
        const char *base = strrchr(input_file, '/');
        base = base ? base + 1 : input_file;
        snprintf(out_name, sizeof(out_name), "%s", base);
        char *dot = strrchr(out_name, '.');
        if (dot) *dot = '\0';
        if (emit == EMIT_OBJECT) strncat(out_name, ".o", sizeof(out_name) - strlen(out_name) - 1);
        else if (emit == EMIT_ASSEMBLY) strncat(out_name, ".asm", sizeof(out_name) - strlen(out_name) - 1);
        else strncat(out_name, target->exe_suffix, sizeof(out_name) - strlen(out_name) - 1);
    }
    const EmbeddedObject *runtime = embedded_runtime(target);
    if (emit == EMIT_EXECUTABLE && !runtime) {
        cobalt_error("This cobalt has no runtime for %s built in: use -A or -c", target->name);
        return 1;
    }

    // Each program: IR, assembly, and an object of its own
    uint8_t *images[300];
    size_t image_sizes[300];
    LinkObject link_objects[300];
    int nlink = 0;
    char table[16384];
    int tn = snprintf(table, sizeof(table), "section .data\nalign 8\nglobal cobrt_program_table\ncobrt_program_table:\n");
    char externs[16384] = "";
    int en = 0;
    for (int k = 0; k < nprograms; k++) {
        IrUnit unit = { .main = k == 0 && !module };
        for (int a = program_parent[k]; a >= 0 && unit.nancestors < 16; a = program_parent[a])
            unit.ancestors[unit.nancestors++] = programs[a];
        IrProgram *ir = lower_to_ir(programs[k], &mode.dialect, &unit);
        if (!ir || error_count > 0) {
            if (!ir) fprintf(stderr, "IR lowering failed\n");
            ir_free(ir);
            return 1;
        }
        if (ir->subprogram && tn < (int)sizeof(table) - 200) {
            tn += snprintf(table + tn, sizeof(table) - tn, "    dq %s\n", ir->info_symbol);
            en += snprintf(externs + en, sizeof(externs) - en, "extern %s\n", ir->info_symbol);
        }
        // The output of program k > 0 with -A or -c: <output>.<program>
        char name_k[600];
        if (k == 0) snprintf(name_k, sizeof(name_k), "%s", out_name);
        else snprintf(name_k, sizeof(name_k), "%s.%s", out_name, ir->program_name);
        char *asm_text = NULL;
        size_t asm_len = 0;
        FILE *out = emit == EMIT_ASSEMBLY ? fopen(name_k, "w") : open_memstream(&asm_text, &asm_len);
        if (!out) {
            cobalt_error("Cannot open output file: %s", name_k);
            return 1;
        }
        int emit_status = backend_emit_assembly(ir, out, target);
        ir_free(ir);
        if (fclose(out) != 0 || emit_status != 0) {
            cobalt_error("Cannot write %s", emit == EMIT_ASSEMBLY ? name_k : "the assembly");
            free(asm_text);
            return 1;
        }
        if (emit == EMIT_ASSEMBLY) continue;
        char asm_name[700];
        snprintf(asm_name, sizeof(asm_name), "%s (generated assembly)", input_file);
        AsmObject *obj = asm_assemble(asm_text, asm_name, NULL);
        free(asm_text);
        if (!obj) {
            cobalt_error("Internal error: the generated assembly did not assemble");
            return 1;
        }
        if (emit == EMIT_OBJECT) {
            bool ok = asm_write_elf64(obj, name_k);
            asm_free(obj);
            if (!ok) { cobalt_error("Cannot write %s", name_k); return 1; }
            continue;
        }
        asm_elf64_image(obj, &images[nlink], &image_sizes[nlink]);
        asm_free(obj);
        link_objects[nlink] = (LinkObject){ images[nlink], image_sizes[nlink], input_file };
        nlink++;
    }
    for (int k = 0; k < nsources; k++) { free(texts[k]); free(sources_text[k]); }
    if (emit != EMIT_EXECUTABLE) return 0;

    // Objects given as arguments, and the program table: every called
    // program's descriptor (runtime/ABI.md section 12)
    for (int k = 0; k < nobjects && nlink < 299; k++) {
        size_t size;
        uint8_t *data = read_binary(objects[k], &size);
        if (!data) { cobalt_error("Cannot read %s", objects[k]); return 1; }
        link_objects[nlink++] = (LinkObject){ data, size, objects[k] };
        char **names = NULL;
        int n = link_object_globals(&link_objects[nlink - 1], "cobinfo_", &names);
        for (int j = 0; j < n; j++) {
            if (tn < (int)sizeof(table) - 200) {
                tn += snprintf(table + tn, sizeof(table) - tn, "    dq %s\n", names[j]);
                en += snprintf(externs + en, sizeof(externs) - en, "extern %s\n", names[j]);
            }
            free(names[j]);
        }
        free(names);
    }
    snprintf(table + tn, sizeof(table) - tn, "    dq 0\n%s", externs);
    AsmObject *tobj = asm_assemble(table, "(program table)", NULL);
    if (!tobj) { cobalt_error("Internal error: the program table did not assemble"); return 1; }
    asm_elf64_image(tobj, &images[nlink], &image_sizes[nlink]);
    asm_free(tobj);
    link_objects[nlink] = (LinkObject){ images[nlink], image_sizes[nlink], "(program table)" };
    nlink++;

    int nruntime = 0;
    while (runtime[nruntime].data) nruntime++;
    LinkObject *library = calloc((size_t)nruntime + 1, sizeof(LinkObject));
    for (int k = 0; k < nruntime; k++)
        library[k] = (LinkObject){ runtime[k].data, runtime[k].size, "runtime" };
    LinkOptions lopts = { target->entry_symbol, strip };
    bool linked = link_executable(link_objects, nlink, library, nruntime, &lopts, out_name);
    free(library);
    return linked ? 0 : 1;
}
