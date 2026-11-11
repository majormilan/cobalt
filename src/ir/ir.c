/* src/ir/ir.c */
#include "ir.h"
#include "ast.h"
#include "diag.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <stdio.h>
#include <stdbool.h>
#include <limits.h>

// ── Data items ───────────────────────────────────────────────────────────────
//
// Every 01/77 item (and every 01 that redefines it) lives in one record; the
// items below it are at offsets inside that record. Index-names (INDEXED BY)
// get a record of their own: an 8-byte binary occurrence number.

typedef struct {
    char *name;
    int level;
    int parent;             // enclosing group item; for a condition-name its data item; -1
    int first_child;        // subordinate items in order, linked by next_sibling
    int next_sibling;
    int record;             // index of the record holding the item
    int offset;             // offset of the first occurrence in the record
    int size;               // bytes of one occurrence
    int occurs;             // OCCURS count (the maximum with DEPENDING ON), 0 without OCCURS
    int occurs_min;         // OCCURS min TO max DEPENDING ON: the minimum
    AstNode *depending;     // OCCURS DEPENDING ON item (owned by the AST), or NULL
    int redefines;          // item redefined by this one, or -1
    int first_index;        // table: its first index-name, or -1
    bool is_group;
    bool is_filler;
    bool is_index;          // index-name
    bool justified;         // JUSTIFIED RIGHT
    int decimal_places;     // scale: digits after the implied point
    int digits;             // digit positions (numeric items)
    bool is_numeric;
    bool is_signed;
    AstUsageType usage_type;
    AstNode *value;         // VALUE clause literal (owned by the AST), or NULL
    bool is_cond;           // level-88 condition-name
    AstNode *cond_values;   // condition-name: literals (THRU upper bound in extra)
    AstNode *decl;
    int file;               // record description of this file (FD), or -1
    const char *mask;       // edited item: edit mask (owned by the AST), else NULL
    bool num_edited;        // numeric edited (values are edited into it)
    char float_char;        // numeric edited: floating $ + or -
    bool blank_zero;        // BLANK WHEN ZERO
    char mask_label[32];    // data literal holding the mask, once emitted
    int sign_mode;          // SIGN clause (own or inherited from a group): SIGN_* bits
    int switch_value;       // condition-name of a switch: 1 for ON STATUS, 0 for OFF; else -1
    int switch_number;      // its switch (SWITCH-n)
    int qual_file;          // LINAGE-COUNTER: the file that qualifies it, else -1
    bool linkage;           // LINKAGE SECTION item
    const char *owner;      // GLOBAL item of a containing program: its entry symbol
    bool assigned;          // a statement gives it a value (-Wuninitialized)
    SourceLocation first_read;  // where a statement first reads it, if one does
} VarInfo;

static bool is_num_edited(const VarInfo *v) { return v->mask && v->num_edited; }
static bool is_alnum_edited(const VarInfo *v) { return v->mask && !v->num_edited; }

#define MAX_VARS 4096
static VarInfo vars[MAX_VARS];
static int num_vars = 0;
static int record_size[MAX_VARS];
static int num_records = 0;
static int record_ptr_slot[MAX_VARS];       // records reached through an address slot
                                            // (LINKAGE, EXTERNAL), else -1
static char *record_extern[MAX_VARS];       // GLOBAL record of a containing program: its symbol
static char *record_export[MAX_VARS];       // GLOBAL record of this program: a symbol for it
static char *record_external[MAX_VARS];     // EXTERNAL record: its name
static const char *self_symbol = "";        // this program's entry symbol
static int external_status_fixups[64][2];   // EXTERNAL file: FCB record, status record/offset
static int num_status_fixups = 0;

// The label of offset off in record r (see IR_ADDR_SET in ir.h; "@symbol"
// is storage of another program)
static void record_label(char *buf, size_t n, int r, int off) {
    if (record_ptr_slot[r] >= 0) snprintf(buf, n, "*A_%d+%d", record_ptr_slot[r], off);
    else if (record_extern[r]) snprintf(buf, n, "@%s+%d", record_extern[r], off);
    else snprintf(buf, n, "%%r%d+%d", r, off);
}

// The symbol of GLOBAL item or file name of the program with entry symbol
// owner: kind 'g' a data record, 'f' a file's control block, 'r' its record area
static char *global_symbol(const char *owner, char kind, const char *name) {
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "cobg%c_%s_", kind, owner + 4);  // owner: "cob_..."
    for (const char *p = name; *p && n < (int)sizeof(buf) - 1; p++)
        buf[n++] = isalnum((unsigned char)*p) ? (char)toupper((unsigned char)*p) : '_';
    buf[n] = '\0';
    return strdup(buf);
}
static int lit_counter = 0;
static int if_counter = 0;
static int addr_slots = 0;  // address/length slots used so far

// SPECIAL-NAMES
static char currency_char = '$';            // CURRENCY SIGN
static bool decimal_comma = false;          // DECIMAL-POINT IS COMMA
static bool has_collation = false;          // PROGRAM COLLATING SEQUENCE
static unsigned char collate_weight[256];
static int low_value = 0, high_value = 0xFF;   // LOW-VALUE / HIGH-VALUE characters

typedef struct {
    const char *name;
    char members[257];      // the characters of a CLASS (NUL not supported)
    int count;
} ClassInfo;
static ClassInfo classes[32];
static int num_classes = 0;

// Symbolic characters (SYMBOLIC CHARACTERS name IS ordinal)
static struct { const char *name; int ch; } symbolic[256];
static int num_symbolic = 0;

// Mnemonic names of switches (SWITCH-n IS name)
static struct { const char *name; int number; } switch_names[16];
static int num_switch_names = 0;

// Files (SELECT + FD)
typedef struct {
    const char *name;
    AstNode *select;
    int first_record;       // first 01 record description (its record is the area)
    int largest_record;     // the longest 01 record description
    AstNode *fd;            // its FD entry
    int fcb_record;         // the record that is its file control block, or -1
    char fcb_label[48];     // the label of its control block
    int reclen_var;         // FCB_RECLEN, FCB_KEY and FCB_KEYREF as data items
    int key_var;
    int keyref_var;
    int linage_record;      // its LINAGE block, or -1
    int lin_var[4];         // LINAGE values: lines, FOOTING, TOP, BOTTOM
    int min_size, max_size; // record lengths
    bool variable;          // records of different lengths
    int same_group;         // SAME RECORD AREA group, or -1
    bool is_sort;           // SD: a sort-merge file
    const char *owner;      // GLOBAL file of a containing program: its entry symbol
    bool external;          // FD IS EXTERNAL
    int status_var;         // its FILE STATUS item, or -1
    int nkeys;              // indexed: prime and alternate keys
    int key_item[16];       // their data items
    bool referenced;        // named by a statement (OPEN, SORT USING ...)
} FileInfo;

#define FCB_BYTES      192  // runtime/ABI.md section 7
#define FCB_OFF_RECLEN 72
#define FCB_OFF_KEY    80
#define FCB_OFF_KEYREF 168
#define FILE_VARIABLE  8    // runtime FCB flags
#define FILE_LINAGE    16
#define ORG_RELATIVE   0x100
#define ORG_INDEXED    0x200
#define ORG_SORT       0x400
#define ACC_RANDOM     0x1000
#define ACC_DYNAMIC    0x2000

#define FILE_LINE     1     // runtime FCB flags (runtime/ABI.md section 7)
#define FILE_OPTIONAL 2

static FileInfo files[64];
static int num_files = 0;
#define FILE_USE      4     // a USE AFTER EXCEPTION procedure may apply: no run-ending error

// Declarative procedures: USE AFTER STANDARD EXCEPTION/ERROR PROCEDURE ON a
// file (file set) or on the files open in a mode (mode 1..4)
typedef struct {
    AstNode *section;
    const char *file;
    int mode;
    int first, last;        // procedure range of the section
    char *symbol;           // USE GLOBAL of a containing program: its entry
    int global_index;       // USE GLOBAL of this program: its number, else -1
} UseDecl;
#define USE_REPORTING 5     // mode of a USE BEFORE REPORTING procedure (file: the report group)
static UseDecl uses[64];
static int num_uses = 0;
static bool has_declaratives = false;

// USE FOR DEBUGGING sections (the Debug module)
typedef struct {
    AstNode *section;
    AstNode *use;           // its USE statement: the procedures, items and files
    int first, last;        // procedure range of the section
} DebugUse;
static DebugUse debug_uses[32];
static int num_debug_uses = 0;
static bool debugging_mode = false;         // WITH DEBUGGING MODE
static int debug_item = -1;                 // DEBUG-ITEM and its fields (vars)
static int debug_line, debug_name, debug_sub[3], debug_contents;
#define DEBUG_CONTENTS_SIZE 2048   // DEBUG-CONTENTS: records up to this size whole

// The data-name of the RELATIVE / RECORD key clause of file f, or NULL
static AstNode *file_key(int f, const char *what) {
    for (AstNode *k = files[f].select->children; k; k = k->next)
        if (k->kind == AST_FILE_KEY && strcmp(k->name, what) == 0) return k->extra;
    return NULL;
}

static const IrUnit *current_unit;

// The USE AFTER EXCEPTION sections of a program: entries for this program
// (symbol NULL), or for a containing program's USE GLOBAL sections (owner:
// its entry symbol; they are entered through a symbol each)
static void collect_program_uses(AstNode *program, const char *owner, bool own) {
    int global_count = 0;
    for (AstNode *div = program->children; div; div = div->next) {
        if (div->kind != AST_DIVISION || strcasecmp(div->name, "PROCEDURE") != 0) continue;
        for (AstNode *n = div->children; n; n = n->next) {
            if (n->kind != AST_SECTION || !n->is_signed) continue;
            if (own) has_declaratives = true;
            AstNode *u = n->extra;
            if (own && u && u->name && strcmp(u->name, "DEBUGGING") == 0 && num_debug_uses < 32)
                debug_uses[num_debug_uses++] = (DebugUse){ n, u, -1, -1 };
            if (own && u && u->name && strcmp(u->name, "REPORTING") == 0) {
                // USE BEFORE REPORTING report-group (the Report Writer)
                for (AstNode *g = u->children; g; g = g->next)
                    if (num_uses < 64) uses[num_uses++] = (UseDecl){ n, g->name, USE_REPORTING, -1, -1, NULL, -1 };
                continue;
            }
            if (!u || !u->name || strcmp(u->name, "ERROR") != 0) continue;
            int gi = u->is_signed ? global_count++ : -1;
            if (!own && gi < 0) continue;
            char *sym = NULL;
            if (!own) {
                char buf[200];
                snprintf(buf, sizeof(buf), "cobuse_%s_%d", owner + 4, gi);
                sym = strdup(buf);
            }
            if (u->text) {
                int mode = strcasecmp(u->text, "INPUT") == 0 ? 1 : strcasecmp(u->text, "OUTPUT") == 0 ? 2 :
                           strcasecmp(u->text, "I-O") == 0 ? 3 : 4;
                if (num_uses < 64) uses[num_uses++] = (UseDecl){ n, NULL, mode, -1, -1, sym, own ? gi : -1 };
            }
            for (AstNode *f = u->children; f; f = f->next)
                if (num_uses < 64) uses[num_uses++] = (UseDecl){ n, f->name, 0, -1, -1,
                                                                 sym ? strdup(sym) : NULL, own ? gi : -1 };
        }
    }
}

static void collect_uses(AstNode *program) {
    num_uses = 0;
    num_debug_uses = 0;
    has_declaratives = false;
    collect_program_uses(program, NULL, true);
    // Then USE GLOBAL of the containing programs, the innermost first
    for (int a = 0; a < current_unit->nancestors; a++) {
        char entry[160], info[160];
        AstNode *anc = current_unit->ancestors[a];
        ir_program_symbols(anc->name ? anc->name : "MAIN", entry, info, sizeof(entry));
        collect_program_uses(anc, entry, false);
    }
}

// The USE procedure for the file itself, or -1
static int use_for_file(const char *file) {
    for (int k = 0; k < num_uses; k++)
        if (uses[k].file && uses[k].mode != USE_REPORTING && strcasecmp(uses[k].file, file) == 0) return k;
    return -1;
}

// The USE procedure for files open in a mode, or -1
static int use_for_mode(int mode) {
    for (int k = 0; k < num_uses; k++)
        if (!uses[k].file && uses[k].mode == mode) return k;
    return -1;
}

// Procedures (sections and paragraphs) in source order
typedef struct {
    const char *name;
    const char *section;    // enclosing section of a paragraph, or NULL
    bool is_section;
} ProcInfo;

static ProcInfo procs[MAX_PROCS];
static SourceLocation proc_loc_of(int p);
static int num_procs = 0;
static const char *current_section = NULL;   // during lowering
static int current_proc = -1;                // procedure being lowered
static bool altered[MAX_PROCS];                   // paragraphs named in an ALTER statement
static const char *sentence_end = NULL;      // label after the current sentence

static IrInstr *new_instr(IrOpcode op, const char *var_name, const char *lit_name,
                          const char *content, int lit_len, int size, int decimal_places, char pad_char, AstUsageType usage_type) {
    IrInstr *i = calloc(1, sizeof(IrInstr));
    if (!i) return NULL;
    i->op = op;
    i->var_name = var_name ? strdup(var_name) : NULL;
    i->lit_name = lit_name ? strdup(lit_name) : NULL;
    i->content = content ? strdup(content) : NULL;
    i->lit_len = lit_len;
    i->size = size;
    i->decimal_places = decimal_places;
    i->pad_char = pad_char;
    i->usage_type = usage_type;
    return i;
}

static void append_instr(IrProgram *ir, IrInstr *i) {
    if (!ir->head) {
        ir->head = ir->tail = i;
    } else {
        ir->tail->next = i;
        ir->tail = i;
    }
}

static IrInstr *emit(IrProgram *ir, IrOpcode op) {
    IrInstr *i = calloc(1, sizeof(IrInstr));
    if (!i) return NULL;
    i->op = op;
    append_instr(ir, i);
    return i;
}

static void emit_label(IrProgram *ir, const char *label) {
    append_instr(ir, new_instr(IR_LABEL, label, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
}

static void emit_jump(IrProgram *ir, IrOpcode op, const char *target) {
    IrInstr *i = emit(ir, op);
    i->jump_target = strdup(target);
}

static void new_label(char *buf, size_t size, const char *what) {
    snprintf(buf, size, "$%s_%d", what, if_counter++);
}

// Emit a data literal and return its label in buf
static const char *emit_data_lit(IrProgram *ir, const char *prefix, const char *content,
                                 char *buf, size_t buf_size) {
    snprintf(buf, buf_size, "%s_%d", prefix, lit_counter++);
    append_instr(ir, new_instr(IR_DATA_LIT, NULL, buf, content, (int)strlen(content), 0, 0, 0, USAGE_DISPLAY));
    return buf;
}

#define FIG_PATTERN (-2)   // ALL with a literal of several characters

// Character of a figurative constant, FIG_PATTERN for ALL "multi-char", or
// -1 if n is not a figurative constant. HIGH-VALUE and LOW-VALUE are the
// highest and lowest characters of the program collating sequence.
static int figurative_char(const AstNode *n) {
    if (n->kind != AST_FIGURATIVE_CONSTANT || !n->name) return -1;
    if (strcasecmp(n->name, "ALL") == 0)
        return (n->text && strlen(n->text) == 1) ? (unsigned char)n->text[0] : FIG_PATTERN;
    if (strncasecmp(n->name, "ZERO", 4) == 0)       return '0';
    if (strncasecmp(n->name, "SPACE", 5) == 0)      return ' ';
    if (strncasecmp(n->name, "HIGH-VALUE", 10) == 0) return high_value;
    if (strncasecmp(n->name, "LOW-VALUE", 9) == 0)  return low_value;
    if (strncasecmp(n->name, "QUOTE", 5) == 0)      return '"';
    if (strcmp(n->name, "SYMBOLIC") == 0 && n->text)
        for (int k = 0; k < num_symbolic; k++)
            if (strcasecmp(symbolic[k].name, n->text) == 0) return symbolic[k].ch;
    return -1;
}

// ── Data layout ──────────────────────────────────────────────────────────────

static bool has_clause(AstNode *decl, AstKind kind) {
    for (AstNode *c = decl->children; c; c = c->next)
        if (c->kind == kind) return true;
    return false;
}

static AstNode *find_clause(AstNode *decl, AstKind kind) {
    for (AstNode *c = decl->children; c; c = c->next)
        if (c->kind == kind) return c;
    return NULL;
}

static VarInfo *new_var(const char *name) {
    if (num_vars >= MAX_VARS) {
        cobalt_error("Too many data items (%d limit)", MAX_VARS);
        return NULL;
    }
    VarInfo *v = &vars[num_vars++];
    memset(v, 0, sizeof(VarInfo));
    v->name = strdup(name);
    v->parent = v->first_child = v->next_sibling = v->redefines = v->first_index = v->file = -1;
    v->switch_value = -1;
    v->qual_file = -1;
    return v;
}

static void add_child(int parent, int child) {
    if (vars[parent].first_child < 0) {
        vars[parent].first_child = child;
        return;
    }
    int c = vars[parent].first_child;
    while (vars[c].next_sibling >= 0) c = vars[c].next_sibling;
    vars[c].next_sibling = child;
}

// Assign record and offsets; returns the item's extent (bytes from offset)
static int layout_item(int idx, int record, int offset) {
    VarInfo *v = &vars[idx];
    v->record = record;
    v->offset = offset;
    if (!v->is_group) return v->size * (v->occurs ? v->occurs : 1);

    int pos = offset, end = offset;
    for (int c = v->first_child; c >= 0; c = vars[c].next_sibling) {
        int start = vars[c].redefines >= 0 ? vars[vars[c].redefines].offset : pos;
        int ext = layout_item(c, record, start);
        if (vars[c].redefines < 0) pos = start + ext;
        if (start + ext > end) end = start + ext;
    }
    v->size = end - offset;
    return v->size * (v->occurs ? v->occurs : 1);
}

// The REDEFINES target: the previous item at the same level under the same parent
static int find_redefined(const char *name, int parent, int before) {
    for (int i = before - 1; i >= 0; i--) {
        if (vars[i].is_cond || vars[i].is_index) continue;
        if (vars[i].parent == parent && strcasecmp(vars[i].name, name) == 0) return i;
    }
    return -1;
}

static VarInfo *find_item(const AstNode *ref, bool report);

// Character of one ALPHABET / CLASS literal element: a character of a
// nonnumeric literal, the ordinal position of a numeric one, or a figurative
// constant in the native (ASCII) set
static int spec_char(const AstNode *lit, int k) {
    if (lit->kind == AST_LITERAL_NUMBER) return (atoi(lit->name) - 1) & 0xFF;
    if (lit->kind == AST_FIGURATIVE_CONSTANT) {
        if (strncasecmp(lit->name, "HIGH-VALUE", 10) == 0) return 0xFF;
        if (strncasecmp(lit->name, "LOW-VALUE", 9) == 0) return 0;
        if (strncasecmp(lit->name, "SPACE", 5) == 0) return ' ';
        if (strncasecmp(lit->name, "QUOTE", 5) == 0) return '"';
        return '0';
    }
    return (unsigned char)lit->name[k];
}

static int spec_length(const AstNode *lit) {
    return lit->kind == AST_STRING_LITERAL ? (int)strlen(lit->name) : 1;
}

// SPECIAL-NAMES and OBJECT-COMPUTER entries
// Collating weights of an ALPHABET with a character list: listed characters
// in order (ALSO: the same weight as the character before), then the others
// in their native order
static void alphabet_weights(AstNode *e, unsigned char weight[256]) {
    bool listed[256] = {false};
    int w = -1;
    for (AstNode *lit = e->children; lit; lit = lit->next) {
        if (lit->extra) {
            int from = spec_char(lit, 0), to = spec_char(lit->extra, 0);
            int step = from <= to ? 1 : -1;
            for (int ch = from; ; ch += step) {
                if (!listed[ch]) { weight[ch] = (unsigned char)++w; listed[ch] = true; }
                if (ch == to) break;
            }
        } else {
            for (int k = 0; k < spec_length(lit); k++) {
                int ch = spec_char(lit, k);
                if (listed[ch]) continue;
                if (!(lit->is_signed && k == 0) || w < 0) w++;
                weight[ch] = (unsigned char)w;
                listed[ch] = true;
            }
        }
    }
    for (int ch = 0; ch < 256; ch++)
        if (!listed[ch]) weight[ch] = (unsigned char)(w < 255 ? ++w : 255);
}

static void build_special_names(AstNode *program) {
    currency_char = '$';
    decimal_comma = has_collation = false;
    low_value = 0;
    high_value = 0xFF;
    num_classes = 0;
    num_switch_names = 0;
    num_symbolic = 0;
    AstNode *env = NULL;
    for (AstNode *div = program->children; div; div = div->next)
        if (div->kind == AST_DIVISION && strcasecmp(div->name, "ENVIRONMENT") == 0) env = div;
    if (!env) return;

    const char *collating = NULL;
    for (AstNode *e = env->children; e; e = e->next) {
        if (e->kind != AST_SPECIAL) continue;
        if (strcmp(e->name, "CURRENCY") == 0 && e->text[0]) currency_char = e->text[0];
        else if (strcmp(e->name, "DECIMAL-POINT") == 0) decimal_comma = true;
        else if (strcmp(e->name, "COLLATING") == 0) collating = e->text;
        else if (strcmp(e->name, "CLASS") == 0 && num_classes < 32) {
            ClassInfo *c = &classes[num_classes++];
            c->name = e->text;
            c->count = 0;
            for (AstNode *lit = e->children; lit; lit = lit->next) {
                if (lit->extra) {
                    int from = spec_char(lit, 0), to = spec_char(lit->extra, 0);
                    int step = from <= to ? 1 : -1;
                    for (int ch = from; ; ch += step) {
                        if (c->count < 256) c->members[c->count++] = (char)ch;
                        if (ch == to) break;
                    }
                } else {
                    for (int k = 0; k < spec_length(lit); k++)
                        if (c->count < 256) c->members[c->count++] = (char)spec_char(lit, k);
                }
            }
            c->members[c->count] = '\0';
        } else if (strcmp(e->name, "SWITCH") == 0) {
            // SWITCH-n: its state comes from the environment at run time
            const char *digit = e->text + strlen(e->text);
            while (digit > e->text && isdigit((unsigned char)digit[-1])) digit--;
            int number = atoi(digit);
            if ((e->on_error || e->not_on_error) && (number < 1 || number > 9)) {
                diag_error(e->loc, "Switch '%s' is not SWITCH-1 to SWITCH-9", e->text);
                continue;
            }
            if (e->extra && number >= 1 && number <= 9 && num_switch_names < 16) {
                switch_names[num_switch_names].name = e->extra->name;
                switch_names[num_switch_names++].number = number;
            }
            for (int on = 1; on >= 0; on--) {
                AstNode *name = on ? e->on_error : e->not_on_error;
                if (!name) continue;
                VarInfo *cv = new_var(name->name);
                if (cv) { cv->is_cond = true; cv->switch_value = on; cv->switch_number = number; }
            }
        }
    }

    // Program collating sequence from an ALPHABET with a character list
    for (AstNode *e = env->children; collating && e; e = e->next) {
        if (e->kind != AST_SPECIAL || strcmp(e->name, "ALPHABET") != 0 ||
            strcasecmp(e->text, collating) != 0 || !e->children) continue;
        alphabet_weights(e, collate_weight);
        has_collation = true;
        int lo = 0, hi = 0;
        for (int ch = 0; ch < 256; ch++) {
            if (collate_weight[ch] < collate_weight[lo]) lo = ch;
            if (collate_weight[ch] >= collate_weight[hi]) hi = ch;
        }
        low_value = lo;
        high_value = hi;
    }

    // Symbolic characters: the character at an ordinal position (from 1) of
    // the native character set or of an alphabet
    for (AstNode *e = env->children; e; e = e->next) {
        if (e->kind != AST_SPECIAL || strcmp(e->name, "SYMBOLIC") != 0) continue;
        unsigned char weights[256];
        for (int ch = 0; ch < 256; ch++) weights[ch] = (unsigned char)ch;
        if (e->text) {
            AstNode *alpha = NULL;
            for (AstNode *a = env->children; a; a = a->next)
                if (a->kind == AST_SPECIAL && strcmp(a->name, "ALPHABET") == 0 && strcasecmp(a->text, e->text) == 0)
                    alpha = a;
            if (!alpha) {
                diag_error(e->loc, "Undeclared alphabet '%s'", e->text);
                continue;
            }
            if (alpha->children) alphabet_weights(alpha, weights);
        }
        for (AstNode *n = e->children; n; n = n->next) {
            int ch = -1;
            for (int c = 0; c < 256 && ch < 0; c++)
                if (n->digits >= 1 && weights[c] == n->digits - 1) ch = c;
            if (ch < 0) {
                diag_error(n->loc, "Ordinal position %d of symbolic character '%s' is out of range", n->digits, n->name);
                continue;
            }
            if (num_symbolic < 256) {
                symbolic[num_symbolic].name = n->name;
                symbolic[num_symbolic++].ch = ch;
            }
        }
    }
}

// Level 66: an item over the storage from the first renamed item to the end
// of the THRU item. Renaming one elementary item gives its attributes.
static void build_renames(AstNode *decl, int record_item) {
    VarInfo *from = find_item(decl->on_error, true);
    VarInfo *thru = decl->not_on_error ? find_item(decl->not_on_error, true) : from;
    if (!from || !thru) return;
    int top_from = (int)(from - vars), top_thru = (int)(thru - vars);
    while (vars[top_from].parent >= 0) top_from = vars[top_from].parent;
    while (vars[top_thru].parent >= 0) top_thru = vars[top_thru].parent;
    bool in_table = false;
    for (VarInfo *a = from; a; a = a->parent >= 0 ? &vars[a->parent] : NULL) in_table |= a->occurs > 0;
    for (VarInfo *a = thru; a; a = a->parent >= 0 ? &vars[a->parent] : NULL) in_table |= a->occurs > 0;
    int end = thru->offset + thru->size;
    if (top_from != record_item || top_thru != record_item || from->level == 1 ||
        from->is_cond || thru->is_cond || from->level == 66 || in_table || end <= from->offset) {
        diag_error(decl->loc, "Invalid RENAMES in '%s': the items must be in the record before it, "
                         "outside tables, in order", decl->name);
        return;
    }
    VarInfo *v = new_var(decl->name);
    if (!v) return;
    char *name = v->name;
    if (!decl->not_on_error && !from->is_group) {
        *v = *from;                     // one elementary item: the same attributes
        v->value = NULL;
    } else {
        memset(v, 0, sizeof(*v));
        v->is_group = true;
        v->usage_type = USAGE_DISPLAY;
        v->size = end - from->offset;
        v->switch_value = -1;
        v->redefines = -1;
        v->file = -1;
        v->first_index = -1;
        v->record = from->record;
        v->offset = from->offset;
    }
    v->name = name;
    v->level = 66;
    v->decl = decl;
    v->is_filler = false;
    v->parent = record_item;            // for qualification only: not a child
    v->first_child = v->next_sibling = -1;
    v->redefines = -1;
    v->occurs = 0;
}

// A qword of record r at offset off as an unsigned binary data item
// without a name (FCB fields, LINAGE values)
static int hidden_qword(int r, int off, AstNode *decl) {
    VarInfo *v = new_var("$file");
    if (!v) return 0;
    v->is_filler = true;
    v->record = r;
    v->offset = off;
    v->size = 8;
    v->usage_type = USAGE_COMP5;
    v->is_numeric = true;
    v->digits = 18;
    v->decl = decl;
    return (int)(v - vars);
}

static bool is_fcb_record(int r) {
    for (int f = 0; f < num_files; f++)
        if (files[f].fcb_record == r) return true;
    return false;
}

// For each file: its record lengths (RECORD clause or record descriptions),
// its file control block as a record of its own (so that its record length
// and key fields are data items), its LINAGE block and LINAGE-COUNTER
static void build_file_records(void) {
    for (int f = 0; f < num_files; f++) {
        FileInfo *fi = &files[f];
        if (fi->first_record < 0) continue;
        int area = vars[fi->first_record].record;
        int lo = 0, hi = 0;
        for (int i = 0; i < num_vars; i++) {
            if (vars[i].file != f || vars[i].parent >= 0 || vars[i].is_cond) continue;
            if (!lo || vars[i].size < lo) lo = vars[i].size;
            if (vars[i].size > hi) hi = vars[i].size;
        }
        AstNode *fd = fi->fd;
        if (fd && fd->decimal_places > hi) hi = fd->decimal_places;
        if (fd && fd->digits > 0) lo = fd->digits;
        else if (fd && fd->is_signed) lo = 1;     // VARYING without FROM (OCCURS DEPENDING ON)
        if (lo > hi) lo = hi;
        if (hi > record_size[area]) record_size[area] = hi;
        fi->min_size = lo;
        fi->max_size = hi;          // the area may be longer (SAME RECORD AREA)
        fi->variable = (fd && fd->is_signed) || lo != hi;
        int r = num_records++;
        record_size[r] = FCB_BYTES;
        fi->fcb_record = r;
        if (fi->owner) record_extern[r] = global_symbol(fi->owner, 'f', fi->name);
        else if (fi->external) {
            record_ptr_slot[r] = addr_slots++;
            char nm[160];
            snprintf(nm, sizeof(nm), "%s (file)", fi->name);
            record_external[r] = strdup(nm);
        }
        record_label(fi->fcb_label, sizeof(fi->fcb_label), r, 0);
        fi->reclen_var = hidden_qword(r, FCB_OFF_RECLEN, fi->select);
        fi->key_var = hidden_qword(r, FCB_OFF_KEY, fi->select);
        fi->keyref_var = hidden_qword(r, FCB_OFF_KEYREF, fi->select);
        if (fd && fd->loop) {
            int j = num_records++;
            record_size[j] = 72;
            fi->linage_record = j;
            for (int k = 0; k < 4; k++) fi->lin_var[k] = hidden_qword(j, 8 * k, fd);
            VarInfo *c = new_var("LINAGE-COUNTER");
            if (c) {
                int idx = hidden_qword(j, 32, fd);
                *c = vars[idx];
                c->name = strdup("LINAGE-COUNTER");
                c->is_filler = false;
                c->qual_file = f;
                c->digits = 10;     // shown like GnuCOBOL's (an unsigned binary long)
            }
        }
    }
}

static void emit_qword(IrProgram *ir, const char *dest, int mode, long long imm,
                       const char *src, int size);

// DEBUG-ITEM: DEBUG-LINE X(6), FILLER X, DEBUG-NAME X(30), FILLER X,
// DEBUG-SUB-1 .. -3 S9(4) SIGN LEADING SEPARATE each followed by FILLER X,
// DEBUG-CONTENTS X(200)
static int debug_field(const char *name, int parent, int offset, int size, AstNode *decl) {
    VarInfo *v = new_var(name);
    if (!v) return 0;
    int idx = (int)(v - vars);
    v->level = parent < 0 ? 1 : 2;
    v->parent = parent;
    v->record = parent < 0 ? num_records : vars[parent].record;
    v->offset = offset;
    v->size = size;
    v->usage_type = USAGE_DISPLAY;
    v->decl = decl;
    if (parent >= 0) add_child(parent, idx);
    return idx;
}

static void build_debug_item(AstNode *program) {
    int total = 56 + DEBUG_CONTENTS_SIZE;
    debug_item = debug_field("DEBUG-ITEM", -1, 0, total, program);
    vars[debug_item].is_group = true;
    int r = num_records++;
    record_size[r] = total;
    debug_line = debug_field("DEBUG-LINE", debug_item, 0, 6, program);
    debug_name = debug_field("DEBUG-NAME", debug_item, 7, 30, program);
    for (int k = 0; k < 3; k++) {
        static const char *names[] = { "DEBUG-SUB-1", "DEBUG-SUB-2", "DEBUG-SUB-3" };
        int d = debug_field(names[k], debug_item, 38 + 6 * k, 5, program);
        vars[d].is_numeric = true;
        vars[d].is_signed = true;
        vars[d].digits = 4;
        vars[d].sign_mode = SIGN_GIVEN | SIGN_LEADING | SIGN_SEPARATE;
        debug_sub[k] = d;
    }
    debug_contents = debug_field("DEBUG-CONTENTS", debug_item, 56, DEBUG_CONTENTS_SIZE, program);
}

static void build_data(IrProgram *ir, AstNode *program) {
    int stack[64];
    int depth = 0;
    int last_item = -1;     // data item that level-88 entries belong to
    AstNode *renames[256];  // level-66 entries, resolved after the layout
    int renames_record[256];
    int num_renames = 0;

    // Files named in FILE-CONTROL
    num_files = 0;
    for (AstNode *div = program->children; div; div = div->next) {
        if (div->kind != AST_DIVISION || strcasecmp(div->name, "ENVIRONMENT") != 0) continue;
        for (AstNode *sel = div->children; sel; sel = sel->next) {
            if (sel->kind != AST_SELECT) continue;
            if (num_files >= 64) {
                diag_error(sel->loc, "Too many files in one program (64 limit)");
                continue;
            }
            files[num_files++] = (FileInfo){ .name = sel->name, .select = sel, .first_record = -1,
                                             .largest_record = -1, .fcb_record = -1,
                                             .linage_record = -1, .same_group = -1, .status_var = -1 };
        }
        int group = 0;
        for (AstNode *same = div->children; same; same = same->next, group++) {
            if (same->kind == AST_SELECT) continue;
            if (same->kind != AST_SPECIAL || strcmp(same->name, "SAME RECORD") != 0) continue;
            for (AstNode *fn = same->children; fn; fn = fn->next) {
                int f = -1;
                for (int k = 0; k < num_files; k++)
                    if (strcasecmp(files[k].name, fn->name) == 0) f = k;
                if (f < 0) diag_error(fn->loc, "'%s' is not a file", fn->name);
                else files[f].same_group = group;
            }
        }
    }

    // GLOBAL files of the containing programs (a name of the program's own
    // hides them)
    for (int a = 0; a < current_unit->nancestors; a++) {
        AstNode *anc = current_unit->ancestors[a];
        char owner_entry[160], owner_info[160];
        ir_program_symbols(anc->name ? anc->name : "MAIN", owner_entry, owner_info, sizeof(owner_entry));
        for (AstNode *div = anc->children; div; div = div->next) {
            if (div->kind != AST_DIVISION || strcasecmp(div->name, "DATA") != 0) continue;
            for (AstNode *sec = div->children; sec; sec = sec->next)
                for (AstNode *fd = sec->kind == AST_SECTION ? sec->children : NULL; fd; fd = fd->next) {
                    if (fd->kind != AST_FILE_DECL || !fd->is_global) continue;
                    bool hidden = false;
                    for (int k = 0; k < num_files; k++)
                        if (strcasecmp(files[k].name, fd->name) == 0) hidden = true;
                    AstNode *sel = NULL;
                    for (AstNode *ed = anc->children; ed; ed = ed->next)
                        if (ed->kind == AST_DIVISION && strcasecmp(ed->name, "ENVIRONMENT") == 0)
                            for (AstNode *e = ed->children; e; e = e->next)
                                if (e->kind == AST_SELECT && strcasecmp(e->name, fd->name) == 0) sel = e;
                    if (hidden || !sel) continue;
                    if (num_files >= 64) {
                        diag_error(sel->loc, "Too many files in one program (64 limit)");
                        continue;
                    }
                    files[num_files++] = (FileInfo){ .name = sel->name, .select = sel, .first_record = -1,
                                                     .largest_record = -1, .fcb_record = -1,
                                                     .linage_record = -1, .same_group = -1,
                                                     .status_var = -1, .owner = strdup(owner_entry) };
                }
        }
    }

    // Record descriptions: WORKING-STORAGE entries, and the entries of every
    // FD in the FILE SECTION (decl_file tells which file, or -1); first the
    // GLOBAL entries of the containing programs (decl_owner: their entry
    // symbol), outermost first
    AstNode *decls[MAX_VARS];
    int decl_file[MAX_VARS];
    const char *decl_owner[MAX_VARS];
    int ndecls = 0;
    for (int a = current_unit->nancestors - 1; a >= 0; a--) {
        AstNode *anc = current_unit->ancestors[a];
        char owner_entry[160], owner_info[160];
        ir_program_symbols(anc->name ? anc->name : "MAIN", owner_entry, owner_info, sizeof(owner_entry));
        char *owner = strdup(owner_entry);
        for (AstNode *div = anc->children; div; div = div->next) {
            if (div->kind != AST_DIVISION || strcasecmp(div->name, "DATA") != 0) continue;
            for (AstNode *sec = div->children; sec; sec = sec->next) {
                if (sec->kind != AST_SECTION || (sec->name && strcmp(sec->name, "LINKAGE") == 0)) continue;
                bool in_global = false;
                for (AstNode *entry = sec->children; entry; entry = entry->next) {
                    if (entry->kind == AST_FILE_DECL) {
                        in_global = false;
                        int f = -1;
                        for (int k = 0; k < num_files; k++)
                            if (files[k].owner && strcmp(files[k].owner, owner) == 0 &&
                                strcasecmp(files[k].name, entry->name) == 0) f = k;
                        if (f < 0) continue;
                        files[f].fd = entry;
                        files[f].is_sort = entry->is_numeric;
                        for (AstNode *d = entry->children; d && ndecls < MAX_VARS; d = d->next) {
                            decls[ndecls] = d;
                            decl_owner[ndecls] = owner;
                            decl_file[ndecls++] = f;
                        }
                        continue;
                    }
                    if (entry->kind != AST_VAR_DECL) continue;
                    int level = entry->text ? atoi(entry->text) : 1;
                    if (level == 1 || level == 77) in_global = entry->is_global;
                    if (!in_global || ndecls >= MAX_VARS) continue;
                    decls[ndecls] = entry;
                    decl_owner[ndecls] = owner;
                    decl_file[ndecls++] = -1;
                }
            }
        }
    }
    for (AstNode *div = program->children; div; div = div->next) {
        if (div->kind != AST_DIVISION || strcasecmp(div->name, "DATA") != 0) continue;
        for (AstNode *sec = div->children; sec; sec = sec->next) {
            if (sec->kind != AST_SECTION) continue;
            for (AstNode *entry = sec->children; entry; entry = entry->next) {
                if (entry->kind == AST_FILE_DECL) {
                    int f = -1;
                    for (int k = 0; k < num_files; k++)
                        if (strcasecmp(files[k].name, entry->name) == 0) f = k;
                    if (f < 0) {
                        if (num_files < 64)      // past the limit, already reported
                            diag_error(entry->loc, "FD '%s' has no SELECT", entry->name);
                    }
                    else {
                        files[f].fd = entry;
                        files[f].is_sort = entry->is_numeric;
                        files[f].external = entry->is_external;
                    }
                    for (AstNode *d = entry->children; d && ndecls < MAX_VARS; d = d->next) {
                        decls[ndecls] = d;
                        decl_owner[ndecls] = NULL;
                        decl_file[ndecls++] = f;
                    }
                } else if (ndecls < MAX_VARS) {
                    decls[ndecls] = entry;
                    decl_owner[ndecls] = NULL;
                    // -2: LINKAGE SECTION (no storage of its own)
                    decl_file[ndecls++] = sec->name && strcmp(sec->name, "LINKAGE") == 0 ? -2 : -1;
                }
            }
        }
    }

    {
        {
            for (int di = 0; di < ndecls; di++) {
                AstNode *decl = decls[di];
                int file = decl_file[di];
                bool linkage = file == -2;
                if (linkage) file = -1;
                if (decl->kind != AST_VAR_DECL) continue;
                int level = decl->text ? atoi(decl->text) : 1;

                if (level == 88) {
                    if (last_item < 0) {
                        diag_error(decl->loc, "Condition-name '%s' has no data item", decl->name);
                        continue;
                    }
                    VarInfo *cv = new_var(decl->name);
                    if (!cv) return;
                    cv->is_cond = true;
                    cv->parent = last_item;
                    cv->cond_values = decl->children;
                    cv->decl = decl;
                    continue;
                }
                if (level == 66) {
                    // belongs to the record being described
                    int record = depth > 0 ? stack[0] : -1;
                    if (record < 0 || vars[record].level == 77) {
                        diag_error(decl->loc, "RENAMES entry '%s' does not follow a record", decl->name);
                    } else if (num_renames < 256) {
                        renames_record[num_renames] = record;
                        renames[num_renames++] = decl;
                    }
                    continue;
                }
                if (level != 1 && level != 77 && (level < 2 || level > 49)) {
                    if (level != 78 || !diag_later_element(decl->loc, LATER_SECTION, "78", 2))
                        diag_error(decl->loc, "Level %d is not supported", level);
                    continue;
                }

                VarInfo *v = new_var(decl->name);
                if (!v) return;
                int idx = (int)(v - vars);
                last_item = idx;
                v->level = level;
                v->decl = decl;
                v->linkage = linkage;
                v->owner = decl_owner[di];
                v->is_filler = strcasecmp(decl->name, "FILLER") == 0;
                v->is_group = !has_clause(decl, AST_PIC_CLAUSE) && !decl->is_numeric;
                v->justified = has_clause(decl, AST_JUSTIFIED);
                // sema.c has already populated the semantic fields
                v->size = (int)decl->data_size;
                v->decimal_places = decl->decimal_places;
                v->digits = decl->digits;
                v->is_numeric = decl->is_numeric;
                v->is_signed = decl->is_signed;
                v->usage_type = decl->usage_type;
                v->mask = decl->mask;
                v->num_edited = decl->mask && !strchr(decl->mask, 'X');
                v->float_char = decl->float_char;
                v->blank_zero = decl->blank_when_zero;
                v->sign_mode = decl->sign_mode;
                for (AstNode *c = decl->children; c; c = c->next) {
                    if (c->kind == AST_STRING_LITERAL || c->kind == AST_LITERAL_NUMBER ||
                        c->kind == AST_FIGURATIVE_CONSTANT) v->value = c;
                }

                if (level == 1 || level == 77) {
                    depth = 0;
                    if (file >= 0) {
                        // Every record description of a file shares its record area
                        v->file = file;
                        if (level == 77)
                            diag_error(decl->loc, "Level 77 is not allowed in an FD");
                            // SAME RECORD AREA: the record area of the group's first file
                        int area_file = file;
                        for (int g = 0; g < num_files; g++)
                            if (files[g].same_group == files[file].same_group && files[file].same_group >= 0 &&
                                files[g].first_record >= 0) { area_file = g; break; }
                        if (files[area_file].first_record < 0) files[area_file].first_record = idx;
                        else v->redefines = files[area_file].first_record;
                        if (files[file].first_record < 0) files[file].first_record = idx;
                    }
                } else {
                    while (depth > 0 && vars[stack[depth - 1]].level >= level) depth--;
                    if (depth == 0) {
                        diag_error(decl->loc, "Level %02d item '%s' is not inside a group", level, decl->name);
                        continue;
                    }
                    v->parent = stack[depth - 1];
                    if (!vars[v->parent].is_group) {
                        diag_error(decl->loc, "'%s' has a PICTURE, so it cannot contain '%s'",
                                         vars[v->parent].name, decl->name);
                    }
                    add_child(v->parent, idx);
                }
                if (depth < 64) stack[depth++] = idx;

                AstNode *redef = find_clause(decl, AST_REDEFINES);
                if (redef) {
                    v->redefines = find_redefined(redef->name, v->parent, idx);
                    if (v->redefines < 0)
                        diag_error(decl->loc, "REDEFINES target '%s' is not the preceding item at this level", redef->name);
                }

                AstNode *occurs = find_clause(decl, AST_OCCURS);
                if (occurs) {
                    v->occurs = atoi(occurs->name);
                    v->depending = occurs->extra;
                    v->occurs_min = occurs->digits > 0 ? occurs->digits : 0;
                    if (v->depending && (occurs->digits < 0 || occurs->digits >= v->occurs))
                        diag_error(decl->loc, "OCCURS %d TO %d is not a valid range", occurs->digits, v->occurs);
                    if (v->occurs < 1 || level == 1 || level == 77) {
                        diag_error(decl->loc, "Invalid OCCURS on '%s'", decl->name);
                        v->occurs = 0;
                    }
                    int dims = 0;
                    for (const VarInfo *a = v; a; a = a->parent >= 0 ? &vars[a->parent] : NULL)
                        if (a->occurs) dims++;
                    if (dims > 16) {
                        diag_error(decl->loc, "'%s' is a table of %d dimensions (16 limit)", decl->name, dims);
                        v->occurs = 0;
                    }
                    for (AstNode *ix = occurs->children; ix; ix = ix->next) {
                        VarInfo *xv = new_var(ix->name);
                        if (!xv) return;
                        v = &vars[idx];     // vars[] does not move, but keep v explicit
                        xv->is_index = true;
                        xv->is_numeric = true;
                        xv->is_signed = true;
                        xv->usage_type = USAGE_COMP5;
                        xv->size = 8;
                        xv->digits = 18;
                        xv->decl = decl;
                        if (v->first_index < 0) v->first_index = (int)(xv - vars);
                    }
                }
            }
        }
    }

    // SIGN clause: a group's clause applies to its signed DISPLAY numeric
    // items without their own; a separate sign takes one more character
    for (int i = 0; i < num_vars; i++) {
        VarInfo *v = &vars[i];
        if (v->is_cond || v->is_index || v->is_group) continue;
        if (!v->sign_mode)
            for (int a = v->parent; a >= 0 && !v->sign_mode; a = vars[a].parent)
                v->sign_mode = vars[a].sign_mode;
        if (!v->sign_mode) continue;
        if (!v->is_numeric || !v->is_signed || v->usage_type != USAGE_DISPLAY) {
            if (v->decl->sign_mode)
                diag_error(v->decl->loc, "SIGN clause requires a signed numeric DISPLAY item ('%s')", v->name);
            v->sign_mode = 0;
            continue;
        }
        if (v->sign_mode & SIGN_SEPARATE) v->size += 1;
    }

    // Validate the hierarchy, then lay out the records
    for (int i = 0; i < num_vars; i++) {
        VarInfo *v = &vars[i];
        if (v->is_cond || v->is_index) continue;
        if (v->is_group && v->first_child < 0)
            diag_error(v->decl->loc, "'%s' needs a PICTURE clause (it has no subordinate items)", v->name);
    }
    for (int i = 0; i < num_vars; i++) {
        VarInfo *v = &vars[i];
        if (v->is_cond || (v->parent >= 0 && !v->is_index)) continue;
        if (v->redefines >= 0) {
            // 01 REDEFINES 01: shares the record
            int r = vars[v->redefines].record;
            int ext = layout_item(i, r, 0);
            if (ext > record_size[r]) record_size[r] = ext;
            continue;
        }
        int r = num_records++;
        record_size[r] = layout_item(i, r, 0);
        // LINKAGE records: at the addresses the caller passes
        if (v->linkage) record_ptr_slot[r] = addr_slots++;
        // GLOBAL: storage of the containing program, or storage of this
        // program that the programs it contains use (the area of a file)
        const char *gname = v->file >= 0 ? files[v->file].name : v->name;
        if (v->owner) record_extern[r] = global_symbol(v->owner, v->file >= 0 ? 'r' : 'g', gname);
        else if (v->file >= 0 ? (files[v->file].fd && files[v->file].fd->is_global) : v->decl->is_global)
            record_export[r] = global_symbol(self_symbol, v->file >= 0 ? 'r' : 'g', gname);
        // EXTERNAL: shared by name with the other programs of the run unit
        if (v->file < 0 ? v->decl->is_external : files[v->file].external) {
            record_ptr_slot[r] = addr_slots++;
            char nm[160];
            snprintf(nm, sizeof(nm), v->file < 0 ? "%s" : "%s (record area)", gname);
            record_external[r] = strdup(nm);
        }
    }
    for (int k = 0; k < num_renames; k++) build_renames(renames[k], renames_record[k]);
    build_file_records();
    build_debug_item(program);
    for (int r = 0; r < num_records; r++) {
        if (is_fcb_record(r)) continue;     // emitted as IR_DATA_FILE
        if (record_ptr_slot[r] >= 0) continue;  // storage of the caller, or EXTERNAL
        if (record_extern[r]) continue;     // storage of a containing program
        char label[32];
        snprintf(label, sizeof(label), "%%r%d", r);
        IrInstr *d = new_instr(IR_DATA_VAR, label, NULL, NULL, 0,
                               record_size[r] > 0 ? record_size[r] : 1, 0, 0, USAGE_DISPLAY);
        d->symbol = record_export[r];
        append_instr(ir, d);
    }

    // File control blocks
    for (int f = 0; f < num_files; f++) {
        FileInfo *fi = &files[f];
        if (fi->first_record < 0) {
            diag_error(fi->select->loc, "File '%s' has no FD with a record description", fi->name);
            continue;
        }
        fi->largest_record = fi->first_record;
        for (int i = 0; i < num_vars; i++)
            if (vars[i].file == f && vars[i].parent < 0 && vars[i].size > vars[fi->largest_record].size)
                fi->largest_record = i;
        if (fi->owner) continue;            // the containing program's
        char name_lit[32];
        emit_data_lit(ir, "fname", fi->select->text, name_lit, sizeof(name_lit));
        int area = vars[fi->first_record].record;
        // An EXTERNAL file: this is the control block it starts as
        char tpl[32];
        if (fi->external) snprintf(tpl, sizeof(tpl), "$fcbtpl_%d", f);
        else snprintf(tpl, sizeof(tpl), "%%r%d", fi->fcb_record);
        IrInstr *d = new_instr(IR_DATA_FILE, tpl, name_lit, NULL, 0, fi->max_size, 0, 0, USAGE_DISPLAY);
        if (fi->fd && fi->fd->is_global) d->symbol = global_symbol(self_symbol, 'f', fi->name);
        AstNode *sel = fi->select;
        d->flags = (sel->decimal_places == 1 ? FILE_LINE : 0) |
                   (sel->is_signed ? FILE_OPTIONAL : 0) |
                   (fi->variable ? FILE_VARIABLE : 0) |
                   (fi->linage_record >= 0 ? FILE_LINAGE : 0) |
                   (sel->org == FILE_ORG_RELATIVE ? ORG_RELATIVE : 0) |
                   (sel->org == FILE_ORG_INDEXED ? ORG_INDEXED : 0) |
                   (fi->is_sort ? ORG_SORT : 0) |
                   (sel->access == FILE_ACCESS_RANDOM ? ACC_RANDOM : 0) |
                   (sel->access == FILE_ACCESS_DYNAMIC ? ACC_DYNAMIC : 0);
        d->digits = fi->min_size;
        if (fi->linage_record >= 0) {
            char lin[16];
            snprintf(lin, sizeof(lin), "%%r%d", fi->linage_record);
            d->cmp_op = strdup(lin);
        }
        if (sel->org == FILE_ORG_INDEXED) {
            // Key table: the record area, the number of keys, then offset,
            // length and duplicates of the prime key and each alternate key
            fi->nkeys = 0;
            char table[2048];
            record_label(table, sizeof(table), area, 0);
            int n = (int)strlen(table);
            char body[2000] = "";
            int bn = 0;
            for (int pass = 0; pass < 2; pass++) {
                for (AstNode *k = sel->children; k; k = k->next) {
                    if (k->kind != AST_FILE_KEY) continue;
                    bool prime = strcmp(k->name, "RECORD") == 0;
                    if (prime != (pass == 0) || strcmp(k->name, "RELATIVE") == 0) continue;
                    VarInfo *kv = find_item(k->extra, true);
                    if (!kv) continue;
                    int top = (int)(kv - vars);
                    while (vars[top].parent >= 0) top = vars[top].parent;
                    if (vars[top].file != f) {
                        diag_error(k->extra->loc, "Key '%s' is not in a record of file '%s'", kv->name, fi->name);
                        continue;
                    }
                    if (fi->nkeys >= 16) break;
                    fi->key_item[fi->nkeys++] = (int)(kv - vars);
                    bn += snprintf(body + bn, sizeof(body) - bn, " %d %d %d", kv->offset, kv->size,
                                   k->is_signed ? 1 : 0);
                }
            }
            if (!file_key(f, "RECORD"))
                diag_error(sel->loc, "INDEXED file '%s' has no RECORD KEY", fi->name);
            snprintf(table + n, sizeof(table) - n, " %d%s", fi->nkeys, body);
            char label[32];
            snprintf(label, sizeof(label), "$keys_%d", f);
            IrInstr *q = new_instr(IR_DATA_QWORDS, label, NULL, table, 0, 0, 0, 0, USAGE_DISPLAY);
            append_instr(ir, q);
            d->jump_target = strdup(label);
        }
        if (sel->org == FILE_ORG_RELATIVE && file_key(f, "RELATIVE")) {
            // The largest record number the RELATIVE KEY item holds (READ: 14)
            VarInfo *kv = find_item(file_key(f, "RELATIVE"), true);
            if (kv && kv->is_numeric && kv->decimal_places == 0 && kv->digits > 0 && kv->digits < 19) {
                long long lim = 1;
                for (int k = 0; k < kv->digits; k++) lim *= 10;
                char num[32];
                snprintf(num, sizeof(num), "%lld", lim - 1);
                d->jump_target = strdup(num);
            } else if (kv && !kv->is_numeric) {
                diag_error(sel->loc, "RELATIVE KEY '%s' must be an unsigned integer item", kv->name);
            }
        }
        if (sel->org == FILE_ORG_RELATIVE && sel->access != FILE_ACCESS_SEQUENTIAL && !file_key(f, "RELATIVE"))
            diag_error(sel->loc, "RELATIVE file '%s' with RANDOM or DYNAMIC access needs a RELATIVE KEY", fi->name);
        bool any_mode = false;
        for (int k = 1; k <= 4; k++) any_mode |= use_for_mode(k) >= 0;
        if (any_mode || use_for_file(fi->name) >= 0) d->flags |= FILE_USE;
        if (fi->select->extra) {
            VarInfo *st = find_item(fi->select->extra, true);
            if (st && (st->is_numeric || st->size != 2))
                diag_error(fi->select->extra->loc, "FILE STATUS item '%s' must be a two-character alphanumeric item", st->name);
            else if (st && !fi->external && record_ptr_slot[st->record] < 0 && !record_extern[st->record]) {
                char label[48];
                record_label(label, sizeof(label), st->record, st->offset);
                d->content = strdup(label);
            } else if (st && num_status_fixups < 64) {
                // Its address is known at run time only: stored at initialization
                external_status_fixups[num_status_fixups][0] = f;
                external_status_fixups[num_status_fixups++][1] = (int)(st - vars);
            }
            if (st) fi->status_var = (int)(st - vars);
        }
        append_instr(ir, d);
    }
}

// ── Data references ──────────────────────────────────────────────────────────

// An identifier whose name is "#<n>" refers to vars[n] directly (used for
// trees built by the compiler, e.g. the data item of a condition-name).
static AstNode *item_ref(int idx, SourceLocation loc) {
    char name[16];
    snprintf(name, sizeof(name), "#%d", idx);
    return ast_new(AST_IDENTIFIER, name, loc);
}

// ── Intrinsic functions ──────────────────────────────────────────────────────
// A function reference (an identifier "!NAME", arguments in on_error) is a
// temporary data item holding its value; resolve_ref evaluates it there
// (runtime/ABI.md section 13).

typedef struct {
    const char *name;
    int id;                 // runtime function number (function.asm)
    char result;            // 'n' numeric, 'a' alphanumeric
    int scale;              // numeric result scale; -1: the arguments' largest, -2: that + 1
    int min_args, max_args;
    char args;              // 'n' numeric, 'a' alphanumeric, 'x' either (all alike), '?' any
} FuncInfo;

static const FuncInfo functions[] = {
    {"ACOS", 0, 'n', 12, 1, 1, 'n'}, {"ANNUITY", 1, 'n', 12, 2, 2, 'n'},
    {"ASIN", 2, 'n', 12, 1, 1, 'n'}, {"ATAN", 3, 'n', 12, 1, 1, 'n'},
    {"CHAR", 4, 'a', 0, 1, 1, 'n'}, {"COS", 5, 'n', 12, 1, 1, 'n'},
    {"CURRENT-DATE", 6, 'a', 0, 0, 0, 'n'}, {"DATE-OF-INTEGER", 7, 'n', 0, 1, 1, 'n'},
    {"DAY-OF-INTEGER", 8, 'n', 0, 1, 1, 'n'}, {"FACTORIAL", 9, 'n', 0, 1, 1, 'n'},
    {"INTEGER", 10, 'n', 0, 1, 1, 'n'}, {"INTEGER-OF-DATE", 11, 'n', 0, 1, 1, 'n'},
    {"INTEGER-OF-DAY", 12, 'n', 0, 1, 1, 'n'}, {"INTEGER-PART", 13, 'n', 0, 1, 1, 'n'},
    {"LENGTH", 14, 'n', 0, 1, 1, '?'}, {"LOG", 15, 'n', 12, 1, 1, 'n'},
    {"LOG10", 16, 'n', 12, 1, 1, 'n'}, {"LOWER-CASE", 17, 'a', 0, 1, 1, 'a'},
    {"MAX", 18, 'n', -1, 1, 999, 'x'}, {"MEAN", 19, 'n', 9, 1, 999, 'n'},
    {"MEDIAN", 20, 'n', -2, 1, 999, 'n'}, {"MIDRANGE", 21, 'n', -2, 1, 999, 'n'},
    {"MIN", 22, 'n', -1, 1, 999, 'x'}, {"MOD", 23, 'n', 0, 2, 2, 'n'},
    {"NUMVAL", 24, 'n', 9, 1, 1, 'a'}, {"NUMVAL-C", 25, 'n', 9, 1, 2, 'a'},
    {"ORD", 26, 'n', 0, 1, 1, 'a'}, {"ORD-MAX", 27, 'n', 0, 1, 999, 'x'},
    {"ORD-MIN", 28, 'n', 0, 1, 999, 'x'}, {"PRESENT-VALUE", 29, 'n', 9, 2, 999, 'n'},
    {"RANDOM", 30, 'n', 12, 0, 1, 'n'}, {"RANGE", 31, 'n', -1, 1, 999, 'n'},
    {"REM", 32, 'n', -1, 2, 2, 'n'}, {"REVERSE", 33, 'a', 0, 1, 1, 'a'},
    {"SIN", 34, 'n', 12, 1, 1, 'n'}, {"SQRT", 35, 'n', 9, 1, 1, 'n'},
    {"STANDARD-DEVIATION", 36, 'n', 9, 1, 999, 'n'}, {"SUM", 37, 'n', -1, 1, 999, 'n'},
    {"TAN", 38, 'n', 12, 1, 1, 'n'}, {"UPPER-CASE", 39, 'a', 0, 1, 1, 'a'},
    {"VARIANCE", 40, 'n', 9, 1, 999, 'n'}, {"WHEN-COMPILED", 41, 'a', 0, 0, 0, 'n'},
};

static const FuncInfo *function_info(const char *name) {
    for (size_t k = 0; k < sizeof(functions) / sizeof(functions[0]); k++)
        if (strcasecmp(functions[k].name, name) == 0) return &functions[k];
    return NULL;
}

static struct { const AstNode *node; int var; } fn_temps[2048];
static int num_fn_temps = 0;
static bool record_pending[MAX_VARS];       // temporary records still to emit (IR_DATA_VAR)

static VarInfo *find_item(const AstNode *ref, bool report);
static bool arg_is_alnum(const AstNode *a);
static int arg_scale(AstNode *a);
static int arg_length(const AstNode *a);
static void expand_all_args(const AstNode *f, AstNode **out, int *n, int max);

// A temporary binary item of scale s (record of its own)
static int temp_numeric(int s, const AstNode *decl) {
    VarInfo *v = new_var("$tmp");
    if (!v) return 0;
    v->is_filler = true;
    v->record = num_records++;
    record_pending[v->record] = true;
    record_size[v->record] = 8;
    v->size = 8;
    v->usage_type = USAGE_COMP5;
    v->is_numeric = true;
    v->is_signed = true;
    v->digits = 18;
    v->decimal_places = s;
    v->decl = (AstNode *)decl;
    return (int)(v - vars);
}

static int temp_alnum(int size, const AstNode *decl) {
    VarInfo *v = new_var("$tmp");
    if (!v) return 0;
    v->is_filler = true;
    v->record = num_records++;
    record_pending[v->record] = true;
    record_size[v->record] = size > 0 ? size : 1;
    v->size = size > 0 ? size : 1;
    v->usage_type = USAGE_DISPLAY;
    v->decl = (AstNode *)decl;
    return (int)(v - vars);
}

// The temporary item of function reference f (created on first use)
static int function_temp(const AstNode *f) {
    for (int k = 0; k < num_fn_temps; k++)
        if (fn_temps[k].node == f) return fn_temps[k].var;
    const FuncInfo *fi = function_info(f->name + 1);
    int idx;
    if (!fi) {
        if (!diag_later_element(f->loc, LATER_FUNCTION, f->name + 1, (size_t)-1))
            diag_error(f->loc, "Unknown function '%s'", f->name + 1);
        idx = temp_numeric(0, f);
    } else {
        AstNode *args[1024];
        int nargs = 0;
        expand_all_args(f, args, &nargs, 1024);
        bool alnum_args = nargs > 0 && arg_is_alnum(args[0]);
        if (fi->result == 'a' || (fi->args == 'x' && alnum_args && fi->id != 27 && fi->id != 28)) {
            int size = 1;
            if (fi->id == 6 || fi->id == 41) size = 21;          // CURRENT-DATE, WHEN-COMPILED
            else if (fi->id != 4)
                for (int k = 0; k < nargs; k++) {
                    int n = arg_length(args[k]);
                    if (n > size) size = n;
                }
            idx = temp_alnum(size, f);
        } else {
            int s = fi->scale;
            if (s < 0) {
                int m = 0;
                for (int k = 0; k < nargs; k++) {
                    int a = arg_is_alnum(args[k]) ? 0 : arg_scale(args[k]);
                    if (a > m) m = a;
                }
                s = m + (fi->scale == -2 ? 1 : 0);
                if (s > 17) s = 17;
            }
            idx = temp_numeric(s, f);
        }
    }
    if (num_fn_temps < 2048) {
        fn_temps[num_fn_temps].node = f;
        fn_temps[num_fn_temps++].var = idx;
    }
    return idx;
}

// For messages: an item's name with its qualifiers ("G1 OF G")
static const char *qualified_name(const VarInfo *v) {
    static char buf[2][256];
    static int which;
    char *b = buf[which ^= 1];
    snprintf(b, 256, "%s", v->name);
    for (int a = v->parent; a >= 0 && strlen(b) < 200; a = vars[a].parent)
        if (!vars[a].is_filler) snprintf(b + strlen(b), 256 - strlen(b), " OF %s", vars[a].name);
    return b;
}

static const char *pic_of(const VarInfo *v) {
    if (!v->decl) return NULL;
    for (AstNode *c = v->decl->children; c; c = c->next)
        if (c->kind == AST_PIC_CLAUSE) return c->name;
    return NULL;
}

// For messages: what kind of item it is ("an alphanumeric item (PICTURE X(10))")
static const char *item_kind(const VarInfo *v) {
    static char buf[160];
    const char *pic = pic_of(v);
    const char *what = v->is_cond ? "a condition-name" : v->is_index ? "an index-name" :
                       v->is_group ? "a group item" : v->num_edited ? "a numeric edited item" :
                       v->mask ? "an alphanumeric edited item" : v->is_numeric ? "a numeric item" :
                       "an alphanumeric item";
    if (pic && !v->is_group) snprintf(buf, sizeof(buf), "%s (PICTURE %s)", what, pic);
    else snprintf(buf, sizeof(buf), "%s", what);
    return buf;
}

// A note on where an item is declared and what it is
static void note_item(const VarInfo *v) {
    if (v->decl && v->decl->loc.line)
        diag_note(v->decl->loc, "'%s' is declared here", qualified_name(v));
}

static void undefined_item(const AstNode *ref) {
    // The name exists, but not with these qualifiers
    int same = -1, nsame = 0;
    for (int i = 0; i < num_vars; i++)
        if (!vars[i].is_filler && strcasecmp(vars[i].name, ref->name) == 0) { if (same < 0) same = i; nsame++; }
    if (same >= 0 && ref->extra) {
        // a qualifier that names nothing at all
        for (const AstNode *q = ref->extra; q; q = q->next) {
            bool known = false;
            for (int i = 0; i < num_vars && !known; i++) known = strcasecmp(vars[i].name, q->name) == 0;
            for (int f = 0; f < num_files && !known; f++) known = strcasecmp(files[f].name, q->name) == 0;
            if (!known) {
                diag_error(q->loc, "'%s' is not defined", q->name);
                return;
            }
        }
        diag_error(ref->loc, "No '%s' is in '%s'", ref->name, ref->extra->name);
        for (int i = 0, shown = 0; i < num_vars && shown < 5; i++)
            if (!vars[i].is_filler && strcasecmp(vars[i].name, ref->name) == 0) {
                if (vars[i].decl && vars[i].decl->loc.line)
                    diag_note(vars[i].decl->loc, "there is '%s'", qualified_name(&vars[i]));
                shown++;
            }
        return;
    }
    diag_error(ref->loc, "'%s' is not defined", ref->name);
    const char *names[MAX_VARS > 4096 ? 4096 : MAX_VARS];
    int n = 0;
    for (int i = 0; i < num_vars && n < (int)(sizeof(names) / sizeof(names[0])); i++)
        if (!vars[i].is_filler && vars[i].decl) names[n++] = vars[i].name;
    const char *near = diag_closest(ref->name, names, n);
    for (int i = 0; near && i < num_vars; i++)
        if (vars[i].name == near) {
            diag_note(vars[i].decl->loc, "did you mean '%s'?", qualified_name(&vars[i]));
            break;
        }
    for (int p = 0; p < num_procs; p++)
        if (strcasecmp(procs[p].name, ref->name) == 0) {
            diag_note(proc_loc_of(p), "'%s' is a %s, not a data item", ref->name, procs[p].is_section ? "section" : "paragraph");
            break;
        }
}

// -Wuninitialized: the statement being lowered. References in statements
// that only read data mark items read; any other statement may store into
// what it names, so its references count as assignments.
static int stmt_kind_now = -1;

static bool reads_only(int kind) {
    return kind == AST_STATEMENT_DISPLAY || kind == AST_STATEMENT_IF || kind == AST_STATEMENT_EVALUATE ||
           kind == AST_STATEMENT_MOVE || kind == AST_STATEMENT_ADD || kind == AST_STATEMENT_SUBTRACT ||
           kind == AST_STATEMENT_MULTIPLY || kind == AST_STATEMENT_DIVIDE || kind == AST_STATEMENT_COMPUTE ||
           kind == AST_STATEMENT_GOTO;
}

static void note_reference(VarInfo *v, const AstNode *ref) {
    if (stmt_kind_now < 0 || !v) return;
    if (!reads_only(stmt_kind_now)) v->assigned = true;
    else if (!v->first_read.line && ref->loc.line) v->first_read = ref->loc;
}

static VarInfo *find_item_ref(const AstNode *ref, bool report);

// Find the data item an identifier refers to, applying OF/IN qualification.
// Reports unknown and ambiguous names when report is set.
static VarInfo *find_item(const AstNode *ref, bool report) {
    VarInfo *v = find_item_ref(ref, report);
    if (v && ref->name[0] != '#' && ref->name[0] != '!') note_reference(v, ref);
    return v;
}

// The data items by name (case-insensitive), each chain in ascending order,
// so a lookup visits the items of that name in the order a scan would.
// Items are indexed when looked up after they were made; names do not
// change once made.
#define NAME_BUCKETS 8192
static int name_head[NAME_BUCKETS], name_tail[NAME_BUCKETS], name_next[MAX_VARS];
static int names_indexed;

static unsigned name_hash(const char *s) {
    unsigned h = 2166136261u;
    for (; *s; s++) h = (h ^ (unsigned char)toupper((unsigned char)*s)) * 16777619u;
    return h & (NAME_BUCKETS - 1);
}

static int first_named(const char *name) {
    if (names_indexed == 0) {
        memset(name_head, -1, sizeof(name_head));
        memset(name_tail, -1, sizeof(name_tail));
    }
    for (; names_indexed < num_vars; names_indexed++) {
        int i = names_indexed;
        unsigned b = name_hash(vars[i].name);
        name_next[i] = -1;
        if (name_tail[b] < 0) name_head[b] = i;
        else name_next[name_tail[b]] = i;
        name_tail[b] = i;
    }
    return name_head[name_hash(name)];
}

static VarInfo *find_item_ref(const AstNode *ref, bool report) {
    if (!ref->name) {                   // not a data name (an expression where one was expected)
        if (report) diag_error(ref->loc, "Expected a data name here");
        return NULL;
    }
    if (ref->name[0] == '#') return &vars[atoi(ref->name + 1)];
    if (ref->name[0] == '!') return &vars[function_temp(ref)];
    VarInfo *found = NULL;
    int count = 0;
    for (int i = first_named(ref->name); i >= 0; i = name_next[i]) {
        if (vars[i].is_filler || strcasecmp(vars[i].name, ref->name) != 0) continue;
        int a = vars[i].parent;
        bool ok = true, file_used = false;
        for (const AstNode *q = ref->extra; q && ok; q = q->next) {
            while (a >= 0 && strcasecmp(vars[a].name, q->name) != 0) a = vars[a].parent;
            if (a >= 0) { a = vars[a].parent; continue; }
            // The outermost qualifier may be the file of a record
            int top = i;
            while (vars[top].parent >= 0) top = vars[top].parent;
            int f = vars[top].file >= 0 ? vars[top].file : vars[top].qual_file;
            if (!file_used && !q->next && f >= 0 && strcasecmp(files[f].name, q->name) == 0) file_used = true;
            else ok = false;
        }
        if (!ok) continue;
        // A program's own names hide the GLOBAL names of its containers
        if (found && found->owner && !vars[i].owner) { found = &vars[i]; count = 1; continue; }
        if (found && !found->owner && vars[i].owner) continue;
        if (!found) found = &vars[i];
        count++;
    }
    if (report && !found)
        undefined_item(ref);
    else if (report && count > 1) {
        diag_error(ref->loc, "'%s' is ambiguous: %d data items have this name", ref->name, count);
        for (int i = 0, shown = 0; i < num_vars && shown < 5; i++)
            if (!vars[i].is_filler && strcasecmp(vars[i].name, ref->name) == 0 && vars[i].decl) {
                diag_note(vars[i].decl->loc, "it could be '%s'", qualified_name(&vars[i]));
                shown++;
            }
        // Qualification helps only if the candidates' qualified names differ
        bool distinct = true;
        const char *first_q = NULL;
        char firstbuf[256];
        for (int i = 0; i < num_vars && distinct; i++)
            if (!vars[i].is_filler && strcasecmp(vars[i].name, ref->name) == 0) {
                const char *q = qualified_name(&vars[i]);
                if (!first_q) { snprintf(firstbuf, sizeof(firstbuf), "%s", q); first_q = firstbuf; }
                else if (strcasecmp(first_q, q) == 0) distinct = false;
            }
        if (distinct) diag_note_plain("qualify the name with OF or IN and the name of a group that holds it");
        else diag_note_plain("qualification cannot tell them apart: their groups have the same names too; rename one");
    }
    return count == 1 ? found : (report ? NULL : found);
}

// Table dimensions of an item, outermost first: occurrence counts and strides
static int item_dims(const VarInfo *v, int counts[], int strides[]) {
    int n = 0, tmp_c[16], tmp_s[16];
    for (const VarInfo *a = v; a && n < 16; a = a->parent >= 0 ? &vars[a->parent] : NULL) {
        if (a->is_cond) continue;
        if (a->occurs) {
            tmp_c[n] = a->occurs;
            tmp_s[n] = a->size;
            n++;
        }
    }
    for (int i = 0; i < n; i++) {
        counts[i] = tmp_c[n - 1 - i];
        strides[i] = tmp_s[n - 1 - i];
    }
    return n;
}

// A resolved field operand (see IR_ADDR_SET in ir.h for the label forms)
typedef struct {
    VarInfo *var;
    char label[64];
    int size;               // bytes; -(k+1): length in length slot N_<k>
    bool refmod;            // reference-modified: always alphanumeric
} FieldRef;

static bool is_int_literal(const AstNode *n, long long *value);
static bool num_expr_valid(AstNode *e);
static int num_expr(IrProgram *ir, AstNode *e, int W);
static void emit_num(IrProgram *ir, IrOpcode op, long long imm, int flags);
static void emit_rescale(IrProgram *ir, int shift, bool rounded);

// Push the integer value of a subscript or reference-modifier expression
static void push_int(IrProgram *ir, AstNode *e) {
    int s = num_expr(ir, e, 0);
    emit_rescale(ir, -s, false);
}

// Resolve an identifier to a field, emitting the address computation for
// subscripts and reference modifiers that are not constant.
// Set while resolving the receiver of a MOVE (see odo_length)
static bool resolving_receiver = false;

// The table with OCCURS DEPENDING ON inside group v (strictly), or NULL
static VarInfo *odo_table_in(const VarInfo *v) {
    if (!v->is_group) return NULL;
    for (int i = 0; i < num_vars; i++) {
        if (!vars[i].depending) continue;
        for (int a = vars[i].parent; a >= 0; a = vars[a].parent)
            if (&vars[a] == v) return &vars[i];
    }
    return NULL;
}

// Length of group v with ODO table t at run time: the bytes before the table
// plus its current occurrences. Returns the size of the length slot. A
// receiving group that also holds the DEPENDING ON item has its maximum
// length (the count arrives with the data).
// ── Run-time errors and checks (-fcheck) ────────────────────────────────────

bool ir_runtime_checks;                       // -fcheck: range checks
static const char *where_program;             // the program being lowered
static const AstNode *where_stmt;             // the statement being lowered

// The prefix of run-time error messages for a location:
// "FILE:LINE: run-time error in program P: " (a data literal, one per line)
static const char *where_label(IrProgram *ir, SourceLocation loc) {
    static char label[32];
    static const char *last_file;
    static int last_line = -1;
    static IrProgram *last_ir;
    if (last_ir == ir && last_file == loc.filename && last_line == loc.line) return label;
    char text[512];
    if (loc.filename && loc.line)
        snprintf(text, sizeof(text), "%s:%d: run-time error in program %s: ", loc.filename, loc.line,
                 where_program ? where_program : "?");
    else
        snprintf(text, sizeof(text), "run-time error in program %s: ", where_program ? where_program : "?");
    emit_data_lit(ir, "where", text, label, sizeof(label));
    last_ir = ir;
    last_file = loc.filename;
    last_line = loc.line;
    return label;
}

// Check that the value on top of the numeric stack is low..high (high < 0:
// the depending item is the limit), naming it by what
static void emit_check(IrProgram *ir, SourceLocation loc, const char *what, int low, int high, AstNode *limit) {
    char wl[32], tl[32];
    snprintf(wl, sizeof(wl), "%s", where_label(ir, where_stmt ? where_stmt->loc : loc));
    emit_data_lit(ir, "what", what, tl, sizeof(tl));
    if (limit) push_int(ir, limit);
    IrInstr *c = emit(ir, IR_CHECK_RANGE);
    c->var_name = strdup(wl);
    c->lit_name = strdup(tl);
    c->size = low;
    c->imm = high;
    c->flags = limit ? 1 : 0;
}

static int odo_length(IrProgram *ir, const VarInfo *v, const VarInfo *t) {
    VarInfo *dv = find_item(t->depending, true);
    if (resolving_receiver && dv)
        for (int a = dv->parent; a >= 0; a = vars[a].parent)
            if (&vars[a] == v) return v->size;
    if (!dv || !dv->is_numeric || dv->decimal_places > 0) {
        if (dv) diag_error(t->depending->loc, "DEPENDING ON item '%s' must be an integer", dv->name);
        return v->size;
    }
    int slot = addr_slots++;
    push_int(ir, t->depending);
    if (ir_runtime_checks) {
        char what[160];
        snprintf(what, sizeof(what), "the value of '%s' (OCCURS DEPENDING ON of '%s')", dv->name, t->name);
        emit_check(ir, t->depending->loc, what, t->occurs_min, t->occurs, NULL);
    }
    emit_num(ir, IR_NUM_CONST, t->size, 0);
    emit_num(ir, IR_NUM_MUL, 0, 0);
    emit_num(ir, IR_NUM_CONST, t->offset - v->offset, 0);
    emit_num(ir, IR_NUM_ADD, 0, 0);
    emit(ir, IR_LEN_SET)->imm = slot;
    return -(slot + 1);
}

static bool eval_function(IrProgram *ir, AstNode *f);

static bool resolve_ref(IrProgram *ir, AstNode *ref, FieldRef *out) {
    memset(out, 0, sizeof(*out));
    if (ref->name[0] == '!' && !eval_function(ir, ref)) return false;
    VarInfo *v = find_item(ref, true);
    if (!v) return false;
    if (v->is_cond) {
        diag_error(ref->loc, "Condition-name '%s' is not a data item", ref->name);
        return false;
    }
    out->var = v;

    int counts[16], strides[16];
    int ndims = item_dims(v, counts, strides);
    int nsubs = 0;
    for (AstNode *s = ref->children; s; s = s->next) nsubs++;
    if (nsubs != ndims) {
        if (ndims == 0)
            diag_error(ref->loc, "'%s' is not a table, so it cannot be subscripted", ref->name);
        else
            diag_error(ref->loc, "'%s' needs %d subscript%s, %d given", ref->name, ndims, ndims == 1 ? "" : "s", nsubs);
        if (ndims > 0) {
            // the items with OCCURS that make the dimensions
            for (const VarInfo *a = v; a; a = a->parent >= 0 ? &vars[a->parent] : NULL)
                if (a->occurs && a->decl) diag_note(a->decl->loc, "'%s' has OCCURS %d", a->name, a->occurs);
        } else {
            note_item(v);
        }
        return false;
    }

    int offset = v->offset;
    bool dynamic = false;
    int k = 0;
    for (AstNode *s = ref->children; s; s = s->next, k++) {
        long long n;
        if (is_int_literal(s, &n)) {
            if (n < 1 || n > counts[k]) {
                diag_error(s->loc, "Subscript %lld of '%s' is out of range 1 to %d", n, ref->name, counts[k]);
                int dim = 0;
                for (const VarInfo *a = v; a; a = a->parent >= 0 ? &vars[a->parent] : NULL)
                    if (a->occurs && dim++ == ndims - 1 - k && a->decl) {
                        diag_note(a->decl->loc, "'%s' has OCCURS %d", a->name, a->occurs);
                        break;
                    }
                return false;
            }
            offset += (int)(n - 1) * strides[k];
        } else {
            if (!num_expr_valid(s)) return false;
            dynamic = true;
        }
    }

    int size = v->size;
    AstNode *start = NULL, *length = NULL;
    long long start_val = 1, len_val = -1;
    bool start_const = true, len_const = true;
    if (ref->loop) {
        out->refmod = true;
        start = ref->loop->children;
        length = start ? start->next : NULL;
        if (!is_int_literal(start, &start_val)) {
            if (!num_expr_valid(start)) return false;
            start_const = false;
            dynamic = true;
        } else if (start_val < 1 || start_val > v->size) {
            diag_error(start->loc, "Reference modifier start is out of range");
            return false;
        }
        if (length && !is_int_literal(length, &len_val)) {
            if (!num_expr_valid(length)) return false;
            len_const = false;
            dynamic = true;
        } else if (length && (len_val < 1 || (start_const && start_val + len_val - 1 > v->size))) {
            diag_error(length->loc, "Reference modifier length is out of range");
            return false;
        }
        if (start_const) offset += (int)start_val - 1;
        if (start_const && len_const) size = length ? (int)len_val : v->size - (int)start_val + 1;
        else if (len_const && length) size = (int)len_val;
    }

    // -fcheck: the item with OCCURS of each dimension (outermost first), and
    // literal subscripts of tables with OCCURS DEPENDING ON (their limit is
    // known only at run time)
    const VarInfo *dim_var[16] = {0};
    char what[200];
    if (ir_runtime_checks) {
        int d = ndims;
        for (const VarInfo *a = v; a && d > 0; a = a->parent >= 0 ? &vars[a->parent] : NULL)
            if (a->occurs) dim_var[--d] = a;
        k = 0;
        for (AstNode *s = ref->children; s; s = s->next, k++) {
            long long n;
            if (!is_int_literal(s, &n) || !dim_var[k] || !dim_var[k]->depending) continue;
            emit_num(ir, IR_NUM_CONST, n, 0);
            if (ndims == 1) snprintf(what, sizeof(what), "the subscript of '%s'", ref->name);
            else snprintf(what, sizeof(what), "subscript %d of '%s'", k + 1, ref->name);
            emit_check(ir, s->loc, what, 1, counts[k], dim_var[k]->depending);       // the current number
            emit(ir, IR_NUM_DROP);
        }
    }

    // A group holding a table with OCCURS DEPENDING ON: its length follows
    // the current number of occurrences (the table ends the group)
    VarInfo *odo = ref->loop ? NULL : odo_table_in(v);
    if (!dynamic) {
        record_label(out->label, sizeof(out->label), v->record, offset);
        out->size = odo ? odo_length(ir, v, odo) : size;
        return true;
    }

    int slot = addr_slots++;
    char base[32];
    record_label(base, sizeof(base), v->record, offset);
    IrInstr *set = emit(ir, IR_ADDR_SET);
    set->imm = slot;
    set->var_name = strdup(base);
    k = 0;
    for (AstNode *s = ref->children; s; s = s->next, k++) {
        long long n;
        if (is_int_literal(s, &n)) continue;
        push_int(ir, s);
        if (ir_runtime_checks) {
            if (ndims == 1) snprintf(what, sizeof(what), "the subscript of '%s'", ref->name);
            else snprintf(what, sizeof(what), "subscript %d of '%s'", k + 1, ref->name);
            // the OCCURS maximum, then (OCCURS DEPENDING ON) the current number
            emit_check(ir, s->loc, what, 1, counts[k], NULL);
            if (dim_var[k] && dim_var[k]->depending) emit_check(ir, s->loc, what, 1, counts[k], dim_var[k]->depending);
        }
        IrInstr *add = emit(ir, IR_ADDR_ADD);
        add->imm = slot;
        add->size = strides[k];
    }
    if (ref->loop && !start_const) {
        push_int(ir, start);
        if (ir_runtime_checks) {
            snprintf(what, sizeof(what), "the reference modification start of '%s'", ref->name);
            emit_check(ir, start->loc, what, 1, length && len_const ? v->size - (int)len_val + 1 : v->size, NULL);
        }
        IrInstr *add = emit(ir, IR_ADDR_ADD);
        add->imm = slot;
        add->size = 1;
    }
    if (ref->loop && !(start_const && len_const) && !(len_const && length)) {
        // Length known only at run time
        if (length) {
            push_int(ir, length);
            if (ir_runtime_checks) {
                snprintf(what, sizeof(what), "the reference modification length of '%s'", ref->name);
                emit_check(ir, length->loc, what, 1, start_const ? v->size - (int)start_val + 1 : v->size, NULL);
                if (!start_const) {
                    // the last character: start + length - 1
                    emit(ir, IR_NUM_DUP);
                    push_int(ir, start);
                    emit_num(ir, IR_NUM_ADD, 0, 0);
                    emit_num(ir, IR_NUM_CONST, 1, 0);
                    emit_num(ir, IR_NUM_SUB, 0, 0);
                    snprintf(what, sizeof(what), "the end of the reference modification of '%s'", ref->name);
                    emit_check(ir, length->loc, what, 1, v->size, NULL);
                    emit(ir, IR_NUM_DROP);
                }
            }
        } else {
            emit_num(ir, IR_NUM_CONST, v->size + 1, 0);
            push_int(ir, start);
            emit_num(ir, IR_NUM_SUB, 0, 0);
        }
        emit(ir, IR_LEN_SET)->imm = slot;
        size = -(slot + 1);
    }
    snprintf(out->label, sizeof(out->label), "*A_%d", slot);
    out->size = odo ? odo_length(ir, v, odo) : size;
    return true;
}

// resolve_ref for the receiving item of a MOVE
static bool resolve_receiver(IrProgram *ir, AstNode *ref, FieldRef *out) {
    resolving_receiver = true;
    VarInfo *rv = find_item(ref, false);
    if (rv) rv->assigned = true;
    bool ok = resolve_ref(ir, ref, out);
    resolving_receiver = false;
    return ok;
}

// ── Numeric expressions ──────────────────────────────────────────────────────

// Parse a numeric literal ("-12.50") into an unscaled integer and its scale.
static bool parse_num_literal(const char *s, long long *value, int *scale) {
    bool neg = false;
    if (*s == '+' || *s == '-') neg = (*s++ == '-');
    long long v = 0;
    int sc = 0;
    bool frac = false, any = false;
    for (; *s; s++) {
        if (*s == '.') {
            if (frac) return false;
            frac = true;
            continue;
        }
        if (!isdigit((unsigned char)*s)) return false;
        if (v > (LLONG_MAX - 9) / 10) return false;
        v = v * 10 + (*s - '0');
        if (frac) sc++;
        any = true;
    }
    if (!any) return false;
    *value = neg ? -v : v;
    *scale = sc;
    return true;
}

static bool is_int_literal(const AstNode *n, long long *value) {
    int scale;
    return n && n->kind == AST_LITERAL_NUMBER && parse_num_literal(n->name, value, &scale) && scale == 0;
}

static const Dialect *dialect;     // of the program being lowered

static int var_flags(const VarInfo *v) {
    int flags = 0;
    if (v->usage_type == USAGE_COMP && !dialect->binary_truncate) flags |= IR_NUM_NOTRUNC;
    if (!v->is_signed) return flags;
    flags |= IR_NUM_SIGNED;
    if (v->usage_type == USAGE_DISPLAY) {
        if (v->sign_mode & SIGN_LEADING) flags |= IR_NUM_SIGN_LEADING;
        if (v->sign_mode & SIGN_SEPARATE) flags |= IR_NUM_SIGN_SEPARATE;
    }
    return flags;
}

// Numeric data item an identifier refers to (no reference modification)
static VarInfo *numeric_item(AstNode *n, const char *role) {
    VarInfo *v = find_item(n, true);
    if (!v) return NULL;
    if (!v->is_numeric || v->is_cond || n->loop) {
        if (n->loop) diag_error(n->loc, "%s '%s' must be numeric, but it is reference modified", role, n->name);
        else {
            diag_error(n->loc, "%s '%s' must be numeric, but it is %s", role, n->name,
                       item_kind(v));
            note_item(v);
        }
        return NULL;
    }
    return v;
}

// Report problems in an arithmetic expression; true if it can be lowered.
static bool num_expr_valid(AstNode *e) {
    if (!e) return false;
    long long value;
    int scale;
    switch (e->kind) {
        case AST_IDENTIFIER: {
            if (!numeric_item(e, "Operand")) return false;
            for (AstNode *s = e->children; s; s = s->next)
                if (!num_expr_valid(s)) return false;
            return true;
        }
        case AST_LITERAL_NUMBER:
            if (!parse_num_literal(e->name, &value, &scale)) {
                diag_error(e->loc, "Invalid numeric literal '%s'", e->name);
                return false;
            }
            return true;
        case AST_FIGURATIVE_CONSTANT:
            if (figurative_char(e) == '0') return true;
            diag_error(e->loc, "%s is not numeric", e->name);
            return false;
        case AST_EXPR_NEG:
        case AST_EXPR_RESCALE:
            return num_expr_valid(e->children);
        case AST_EXPR_POW: {
            bool base = num_expr_valid(e->children);
            bool exp = e->children && num_expr_valid(e->children->next);
            return base && exp;
        }
        case AST_EXPR_ADD:
        case AST_EXPR_SUB:
        case AST_EXPR_MUL:
        case AST_EXPR_DIV: {
            bool left = num_expr_valid(e->children);
            bool right = e->children && num_expr_valid(e->children->next);
            AstNode *d = e->children ? e->children->next : NULL;
            long long dv;
            int ds;
            if (e->kind == AST_EXPR_DIV && d && ((d->kind == AST_LITERAL_NUMBER && parse_num_literal(d->name, &dv, &ds) && dv == 0) ||
                                                 figurative_char(d) == '0'))
                diag_warning(W_DIVIDE_BY_ZERO, d->loc, "Division by zero: this always raises the size error condition");
            return left && right;
        }
        default:
            diag_error(e->loc, "Invalid operand in arithmetic expression");
            return false;
    }
}

// Largest scale of any operand in the expression
static int max_leaf_scale(AstNode *e) {
    if (!e) return 0;
    if (e->kind == AST_IDENTIFIER) {
        VarInfo *v = find_item(e, false);
        return v ? v->decimal_places : 0;
    }
    if (e->kind == AST_LITERAL_NUMBER) {
        long long value;
        int scale = 0;
        parse_num_literal(e->name, &value, &scale);
        return scale;
    }
    if (e->kind == AST_FIGURATIVE_CONSTANT) return 0;
    int m = max_leaf_scale(e->children);
    if (e->children) {
        int r = max_leaf_scale(e->children->next);
        if (r > m) m = r;
    }
    return m;
}

static void emit_num(IrProgram *ir, IrOpcode op, long long imm, int flags) {
    IrInstr *i = emit(ir, op);
    i->imm = imm;
    i->flags = flags;
}

static void emit_rescale(IrProgram *ir, int shift, bool rounded) {
    if (shift != 0) emit_num(ir, IR_NUM_RESCALE, shift, rounded ? 1 : 0);
}

static void emit_load(IrProgram *ir, const FieldRef *f) {
    IrInstr *i = emit(ir, IR_NUM_LOAD);
    i->var_name = strdup(f->label);
    i->size = f->var->size;
    i->usage_type = f->var->usage_type;
    i->flags = var_flags(f->var);
}

static const char *mask_label(IrProgram *ir, VarInfo *v) {
    if (!v->mask_label[0]) emit_data_lit(ir, "mask", v->mask, v->mask_label, sizeof(v->mask_label));
    return v->mask_label;
}

static void emit_store(IrProgram *ir, const FieldRef *f, bool keep) {
    if (is_num_edited(f->var)) {
        IrInstr *e = emit(ir, IR_NUM_STORE);
        e->var_name = strdup(f->label);
        e->lit_name = strdup(mask_label(ir, f->var));
        e->size = (int)strlen(f->var->mask);
        e->flags = (f->var->blank_zero ? 1 : 0) | (keep ? 2 : 0) | ((unsigned char)f->var->float_char << 8) |
                   (currency_char != '$' ? (unsigned char)currency_char << 16 : 0) |
                   (decimal_comma ? 1 << 24 : 0);
        return;
    }
    IrInstr *i = emit(ir, IR_NUM_STORE);
    i->var_name = strdup(f->label);
    i->size = f->var->size;
    i->digits = f->var->digits;
    i->usage_type = f->var->usage_type;
    i->flags = var_flags(f->var) | (keep ? IR_NUM_KEEP : 0);
}

// Compute the scale of a valid expression; when ir is non-NULL also emit the
// code that pushes its value. W is the working scale: products are cut back
// to W digits after the point and quotients are computed to W digits.
// The single function keeps the computed scale and the emitted code in step.
// The integer digits a value of the expression may have (from the PICTUREs
// and literals): the scales of intermediate results are kept within 18
// digits in all (see num_expr)
static int int_digits(AstNode *e) {
    if (!e) return 1;
    int d;
    switch (e->kind) {
        case AST_IDENTIFIER: {
            VarInfo *v = find_item(e, false);
            d = v ? v->digits - v->decimal_places : 18;
            break;
        }
        case AST_LITERAL_NUMBER: {
            const char *p = e->name;
            if (*p == '-' || *p == '+') p++;
            while (*p == '0' && p[1] && p[1] != '.') p++;
            d = 0;
            while (isdigit((unsigned char)p[d])) d++;
            break;
        }
        case AST_EXPR_NEG:
        case AST_EXPR_RESCALE:
            d = int_digits(e->children);
            break;
        case AST_EXPR_ADD:
        case AST_EXPR_SUB: {
            int l = int_digits(e->children), r = int_digits(e->children->next);
            d = (l > r ? l : r) + 1;
            break;
        }
        case AST_EXPR_MUL:
            d = int_digits(e->children) + int_digits(e->children->next);
            break;
        case AST_EXPR_DIV: {
            // a literal divisor below 1 makes the quotient larger
            d = int_digits(e->children);
            AstNode *b = e->children->next;
            if (b->kind == AST_LITERAL_NUMBER) {
                const char *p = strchr(b->name, '.');
                long long value;
                int scale;
                if (p && parse_num_literal(b->name, &value, &scale) && value != 0) {
                    long long m = value < 0 ? -value : value, lim = 1;
                    for (int k = 0; k < scale; k++) lim *= 10;
                    while (m < lim) { d++; m *= 10; }
                }
            }
            break;
        }
        default:
            d = 18;
    }
    if (d < 1) d = 1;
    return d > 18 ? 18 : d;
}

// The least scale intermediate results keep when 18 digits would not hold
// the scale the operands have (the receiver's scale and 2 more); W itself
// outside lower_assign
static int expr_floor = 18;

// The scale for a value of int_digits d: at most W, and within 18 digits
// unless that is below the floor
static int fit_scale(int want, int W, int d) {
    int lim = 18 - d;
    if (lim < expr_floor) lim = expr_floor;
    if (lim < 0) lim = 0;
    if (want > W) want = W;         // (never raised: a scale may be negative, PICTURE P)
    if (want > lim) want = lim;
    return want;
}

static int num_expr(IrProgram *ir, AstNode *e, int W) {
    switch (e->kind) {
        case AST_IDENTIFIER: {
            if (ir) {
                FieldRef f;
                if (resolve_ref(ir, e, &f)) emit_load(ir, &f);
                else emit_num(ir, IR_NUM_CONST, 0, 0);  // keep the stack balanced
            }
            VarInfo *v = find_item(e, false);
            return v ? v->decimal_places : 0;
        }
        case AST_FIGURATIVE_CONSTANT:   // ZERO
            if (ir) emit_num(ir, IR_NUM_CONST, 0, 0);
            return 0;
        case AST_LITERAL_NUMBER: {
            long long value;
            int scale;
            parse_num_literal(e->name, &value, &scale);
            if (ir) emit_num(ir, IR_NUM_CONST, value, 0);
            return scale;
        }
        case AST_EXPR_NEG: {
            int s = num_expr(ir, e->children, W);
            if (ir) emit_num(ir, IR_NUM_NEG, 0, 0);
            return s;
        }
        case AST_EXPR_RESCALE: {        // truncate to decimal_places digits after the point
            int s = num_expr(ir, e->children, W);
            if (ir) emit_rescale(ir, e->decimal_places - s, false);
            return e->decimal_places;
        }
        case AST_EXPR_ADD:
        case AST_EXPR_SUB: {
            AstNode *l = e->children, *r = l->next;
            int sl = num_expr(NULL, l, W), sr = num_expr(NULL, r, W);
            int s = sl > sr ? sl : sr;
            if (s > 18 - int_digits(e)) s = fit_scale(s, s, int_digits(e));
            if (ir) {
                num_expr(ir, l, W);
                emit_rescale(ir, s - sl, false);
                num_expr(ir, r, W);
                emit_rescale(ir, s - sr, false);
                emit_num(ir, e->kind == AST_EXPR_ADD ? IR_NUM_ADD : IR_NUM_SUB, 0, 0);
            }
            return s;
        }
        case AST_EXPR_MUL: {
            AstNode *l = e->children, *r = l->next;
            int sl = num_expr(ir, l, W), sr = num_expr(ir, r, W);
            int p = sl + sr, shift = p - fit_scale(p, W, int_digits(e));
            if (ir) emit_num(ir, IR_NUM_MUL, shift, 0);
            return p - shift;
        }
        case AST_EXPR_DIV: {
            AstNode *l = e->children, *r = l->next;
            int sl = num_expr(NULL, l, W), sr = num_expr(NULL, r, W);
            int q = fit_scale(W, W, int_digits(e));
            int shift = q - sl + sr;
            if (ir) {
                num_expr(ir, l, W);
                if (shift < 0) emit_rescale(ir, shift, false);
                num_expr(ir, r, W);
                emit_num(ir, IR_NUM_DIV, shift < 0 ? 0 : shift, 0);
            }
            return q;
        }
        case AST_EXPR_POW: {
            AstNode *base = e->children;
            long long n;
            int exp_scale;
            if (base->next->kind != AST_LITERAL_NUMBER ||
                !parse_num_literal(base->next->name, &n, &exp_scale) || exp_scale != 0 || n < 0 || n > 18) {
                // Exponent known at run time: the result at the working scale
                int sb = num_expr(ir, base, W);
                int se = num_expr(ir, base->next, W);
                if (ir) {
                    IrInstr *p = emit(ir, IR_NUM_POW);
                    p->imm = W;
                    p->digits = sb;
                    p->size = se;
                }
                return W;
            }
            if (n == 0) {
                if (ir) emit_num(ir, IR_NUM_CONST, 1, 0);
                return 0;
            }
            int sb = num_expr(ir, base, W), s = sb;
            for (long long k = 1; k < n; k++) {
                num_expr(ir, base, W);
                int p = s + sb, shift = p > W ? p - W : 0;
                if (ir) emit_num(ir, IR_NUM_MUL, shift, 0);
                s = p - shift;
            }
            return s;
        }
        default:
            return 0;
    }
}

// Working scale for computing tree into target
static int working_scale(AstNode *tree, const VarInfo *target) {
    int w = max_leaf_scale(tree);
    if (target && target->decimal_places > w) w = target->decimal_places;
    w += 2;
    return w > 18 ? 18 : w;
}

// Evaluate tree and store it into the receiver (an identifier node). The
// receiver's subscripts are evaluated after the value, just before storing.
// Numeric or numeric edited item that receives a numeric value
static VarInfo *receiving_item(AstNode *n) {
    VarInfo *v = find_item(n, true);
    if (!v) return NULL;
    if ((!v->is_numeric && !is_num_edited(v)) || v->is_cond || n->loop) {
        if (n->loop) diag_error(n->loc, "Receiving item '%s' must be numeric, but it is reference modified", n->name);
        else {
            diag_error(n->loc, "Receiving item '%s' must be numeric or numeric edited, but it is %s",
                       n->name, item_kind(v));
            note_item(v);
        }
        return NULL;
    }
    return v;
}

static void lower_assign(IrProgram *ir, AstNode *tree, AstNode *receiver, bool keep) {
    VarInfo *t = receiving_item(receiver);
    if (!t || !num_expr_valid(tree)) return;
    int saved_floor = expr_floor;
    expr_floor = t->decimal_places + 2;
    int s = num_expr(ir, tree, working_scale(tree, t));
    expr_floor = saved_floor;
    emit_rescale(ir, t->decimal_places - s, receiver->rounded);
    FieldRef f;
    if (!resolve_ref(ir, receiver, &f)) return;
    emit_store(ir, &f, keep);
}

// Expression trees built from statement operands (freed with ast_free)
static AstNode *mk_bin(AstKind kind, AstNode *l, AstNode *r) {
    AstNode *n = ast_new(kind, NULL, l->loc);
    n->children = l;
    l->next = r;
    return n;
}

// first op ops[0] op ops[1] ... (left-associative), each operand copied
static AstNode *fold_ops(AstKind kind, AstNode *first, AstNode **ops, int n) {
    AstNode *tree = first;
    for (int i = 0; i < n; i++) tree = mk_bin(kind, tree, ast_clone(ops[i]));
    return tree;
}

static void lower_block(IrProgram *ir, AstNode *block);
static void debug_item_event(IrProgram *ir, AstNode *ref, int line);
static bool debugging_here(void);
static void debug_file_event(IrProgram *ir, AstNode *stmt, int f);
static void debug_after_statement(IrProgram *ir, AstNode *stmt);

// Resolve a procedure name: a paragraph of the current section first, then a
// section or a paragraph whose name is unique in the program.
static bool proc_referenced[MAX_PROCS];       // named by a PERFORM, GO TO, ALTER, SORT...

static int resolve_proc_name(AstNode *ref);

static int resolve_proc(AstNode *ref) {
    int p = resolve_proc_name(ref);
    if (p >= 0) proc_referenced[p] = true;
    return p;
}

static int resolve_proc_name(AstNode *ref) {
    if (ref->extra) {
        // paragraph OF section
        for (int i = 0; i < num_procs; i++) {
            if (!procs[i].is_section && procs[i].section &&
                strcasecmp(procs[i].section, ref->extra->name) == 0 &&
                strcasecmp(procs[i].name, ref->name) == 0) return i;
        }
        diag_error(ref->loc, "Section '%s' has no paragraph '%s'", ref->extra->name, ref->name);
        for (int i = 0; i < num_procs; i++)
            if (!procs[i].is_section && strcasecmp(procs[i].name, ref->name) == 0) {
                diag_note(proc_loc_of(i), "there is '%s' in section '%s'", procs[i].name,
                          procs[i].section ? procs[i].section : "(none)");
                break;
            }
        return -1;
    }
    if (current_section) {
        for (int i = 0; i < num_procs; i++) {
            if (!procs[i].is_section && procs[i].section &&
                strcasecmp(procs[i].section, current_section) == 0 &&
                strcasecmp(procs[i].name, ref->name) == 0) return i;
        }
    }
    int found = -1, count = 0;
    for (int i = 0; i < num_procs; i++) {
        if (strcasecmp(procs[i].name, ref->name) == 0) {
            if (found < 0) found = i;
            count++;
        }
    }
    if (found < 0) {
        diag_error(ref->loc, "No paragraph or section is named '%s'", ref->name);
        const char *names[4096];
        int n = 0;
        for (int i = 0; i < num_procs && n < 4096; i++) names[n++] = procs[i].name;
        const char *near = diag_closest(ref->name, names, n);
        for (int i = 0; near && i < num_procs; i++)
            if (procs[i].name == near) { diag_note(proc_loc_of(i), "did you mean '%s'?", near); break; }
        for (int i = 0; i < num_vars; i++)
            if (!vars[i].is_filler && strcasecmp(vars[i].name, ref->name) == 0 && vars[i].decl) {
                diag_note(vars[i].decl->loc, "'%s' is a data item", vars[i].name);
                break;
            }
    } else if (count > 1) {
        diag_error(ref->loc, "Paragraph name '%s' is ambiguous: %d sections have one", ref->name, count);
        for (int i = 0; i < num_procs; i++)
            if (strcasecmp(procs[i].name, ref->name) == 0)
                diag_note(proc_loc_of(i), "it could be '%s OF %s'", procs[i].name, procs[i].section ? procs[i].section : "?");
        diag_note_plain("qualify the paragraph name with OF and its section name");
        return -1;
    }
    return found;
}

// ON SIZE ERROR / NOT ON SIZE ERROR branches after the receivers are stored
static void lower_size_error_phrases(IrProgram *ir, AstNode *stmt) {
    if (!stmt->on_error && !stmt->not_on_error) return;
    char no_err_lbl[32], end_lbl[32];
    new_label(no_err_lbl, sizeof(no_err_lbl), "size_ok");
    new_label(end_lbl, sizeof(end_lbl), "size_end");

    emit_jump(ir, IR_JUMP_IF_NO_SIZE_ERR, no_err_lbl);
    lower_block(ir, stmt->on_error);
    emit_jump(ir, IR_JUMP, end_lbl);
    emit_label(ir, no_err_lbl);
    lower_block(ir, stmt->not_on_error);
    emit_label(ir, end_lbl);
}

// Working scale for a value stored into several receivers
static int working_scale_all(AstNode *tree, AstNode **recv, int n) {
    int w = max_leaf_scale(tree);
    for (int i = 0; i < n; i++) {
        VarInfo *v = find_item(recv[i], false);
        if (v && v->decimal_places > w) w = v->decimal_places;
    }
    w += 2;
    if (w < 0) w = 0;
    return w > 18 ? 18 : w;
}

// Store the value on top of the numeric stack (scale s) into a receiver,
// keeping the value for the next receiver
static void store_copy(IrProgram *ir, AstNode *recv, int s, bool keep) {
    VarInfo *t = receiving_item(recv);
    if (!t) return;
    emit(ir, IR_ERR_RESTORE);
    emit(ir, IR_NUM_DUP);
    emit_rescale(ir, t->decimal_places - s, recv->rounded);
    FieldRef f;
    if (!resolve_ref(ir, recv, &f)) {
        emit(ir, IR_NUM_DROP);
        return;
    }
    emit_store(ir, &f, keep);
}

// Compute tree once and store it into every receiver (GIVING, COMPUTE)
static void lower_giving(IrProgram *ir, AstNode *tree, AstNode **recv, int n, bool keep) {
    for (int i = 0; i < n; i++) {
        VarInfo *v = find_item(recv[i], false);
        if (v) v->assigned = true;
    }
    if (!num_expr_valid(tree)) return;
    int saved_floor = expr_floor, most = 0;
    for (int i = 0; i < n; i++) {
        VarInfo *v = find_item(recv[i], false);
        if (v && v->decimal_places > most) most = v->decimal_places;
    }
    expr_floor = most + 2;
    int s = num_expr(ir, tree, working_scale_all(tree, recv, n));
    expr_floor = saved_floor;
    emit(ir, IR_ERR_SAVE);
    for (int i = 0; i < n; i++) store_copy(ir, recv[i], s, keep);
    emit(ir, IR_NUM_DROP);
}

// receiver = receiver op value, for each receiver, with the value of tree
// computed once (ADD TO, SUBTRACT FROM, MULTIPLY BY, DIVIDE INTO)
static void lower_in_place(IrProgram *ir, AstKind op, AstNode *tree, AstNode **recv, int n, bool keep) {
    if (!num_expr_valid(tree)) return;
    for (int i = 0; i < n; i++)
        if (!numeric_item(recv[i], "Receiving item")) return;
    int W = working_scale_all(tree, recv, n);
    int sx = num_expr(ir, tree, W);
    emit(ir, IR_ERR_SAVE);
    for (int i = 0; i < n; i++) {
        VarInfo *t = find_item(recv[i], false);
        int sr = t->decimal_places, s;
        FieldRef f;
        emit(ir, IR_ERR_RESTORE);
        emit(ir, IR_NUM_DUP);
        if (op == AST_EXPR_ADD || op == AST_EXPR_SUB) {
            s = sx > sr ? sx : sr;
            emit_rescale(ir, s - sx, false);
            if (!resolve_ref(ir, recv[i], &f)) { emit(ir, IR_NUM_DROP); continue; }
            emit_load(ir, &f);
            emit_rescale(ir, s - sr, false);
            if (op == AST_EXPR_ADD) {
                emit_num(ir, IR_NUM_ADD, 0, 0);
            } else {
                emit_num(ir, IR_NUM_SUB, 0, 0);    // value - receiver
                emit_num(ir, IR_NUM_NEG, 0, 0);    // receiver - value
            }
        } else if (op == AST_EXPR_MUL) {
            if (!resolve_ref(ir, recv[i], &f)) { emit(ir, IR_NUM_DROP); continue; }
            emit_load(ir, &f);
            int p = sx + sr, shift = p > W ? p - W : 0;
            emit_num(ir, IR_NUM_MUL, shift, 0);
            s = p - shift;
        } else {
            // receiver / value: the receiver goes below the value
            if (!resolve_ref(ir, recv[i], &f)) { emit(ir, IR_NUM_DROP); continue; }
            emit_load(ir, &f);
            int shift = W - sr + sx;
            if (shift < 0) {
                emit_rescale(ir, shift, false);
                shift = 0;
            }
            emit(ir, IR_NUM_SWAP);
            emit_num(ir, IR_NUM_DIV, shift, 0);
            s = W;
        }
        emit_rescale(ir, sr - s, recv[i]->rounded);
        if (!resolve_ref(ir, recv[i], &f)) { emit(ir, IR_NUM_DROP); continue; }
        emit_store(ir, &f, keep);
    }
    emit(ir, IR_NUM_DROP);
}

// ── CORRESPONDING ────────────────────────────────────────────────────────────

static void lower_move(IrProgram *ir, AstNode *src, AstNode *dest_node, const char *src_lit);

// Items that do not take part in CORRESPONDING (with their subordinates)
static bool corr_excluded(const VarInfo *v) {
    return v->is_filler || v->redefines >= 0 || v->occurs || v->is_index || v->is_cond;
}

// Reference to item idx with the subscripts of the group reference it is in
static AstNode *corr_ref(int idx, const AstNode *group_ref, bool rounded) {
    AstNode *r = item_ref(idx, group_ref->loc);
    r->children = ast_clone_list(group_ref->children);
    r->rounded = rounded;
    return r;
}

// Pair the subordinate items of g1 and g2 that correspond and lower each
// pair: kind is AST_STATEMENT_MOVE, ADD or SUBTRACT
static void corr_pairs(IrProgram *ir, AstKind kind, int g1, int g2,
                       const AstNode *ref1, const AstNode *ref2, bool rounded, bool keep) {
    for (int c1 = vars[g1].first_child; c1 >= 0; c1 = vars[c1].next_sibling) {
        if (corr_excluded(&vars[c1])) continue;
        int c2 = -1;
        for (int k = vars[g2].first_child; k >= 0; k = vars[k].next_sibling)
            if (!corr_excluded(&vars[k]) && strcasecmp(vars[k].name, vars[c1].name) == 0) { c2 = k; break; }
        if (c2 < 0) continue;
        bool group1 = vars[c1].is_group, group2 = vars[c2].is_group;
        if (group1 && group2) {
            corr_pairs(ir, kind, c1, c2, ref1, ref2, rounded, keep);
            continue;
        }
        AstNode *a = corr_ref(c1, ref1, false);
        AstNode *b = corr_ref(c2, ref2, rounded);
        if (kind == AST_STATEMENT_MOVE) {
            lower_move(ir, a, b, "");
        } else if (!group1 && !group2 && vars[c1].is_numeric && vars[c2].is_numeric) {
            AstNode *recv[1] = { b };
            lower_in_place(ir, kind == AST_STATEMENT_ADD ? AST_EXPR_ADD : AST_EXPR_SUB, a, recv, 1, keep);
        }
        ast_free(a);
        ast_free(b);
    }
}

static void lower_corresponding(IrProgram *ir, AstNode *node, AstNode *from, AstNode *to, bool keep) {
    VarInfo *g1 = find_item(from, true), *g2 = find_item(to, true);
    if (!g1 || !g2) return;
    if (!g1->is_group || !g2->is_group) {
        diag_error(node->loc, "CORRESPONDING requires two group items");
        return;
    }
    corr_pairs(ir, node->kind, (int)(g1 - vars), (int)(g2 - vars), from, to, to->rounded, keep);
}

// ADD, SUBTRACT, MULTIPLY, DIVIDE and COMPUTE. Sending operands are
// evaluated once, before any receiver is changed; with GIVING (and COMPUTE)
// the result is computed once and stored into each receiver.
// Child layouts are documented at the statement parsers.
static void lower_arith_node(IrProgram *ir, AstNode *node) {
    AstNode *ops[128];
    int n = 0;
    for (AstNode *c = node->children; c && n < 128; c = c->next) ops[n++] = c;
    bool giving = node->is_numeric;
    // With either SIZE ERROR phrase, a receiver with a size error is left unchanged
    bool keep = node->on_error != NULL || node->not_on_error != NULL;

    emit(ir, IR_SIZE_ERR_CLEAR);

    if (node->name && strcmp(node->name, "CORR") == 0 && n >= 2) {
        lower_corresponding(ir, node, ops[0], ops[1], keep);
        lower_size_error_phrases(ir, node);
        return;
    }

    switch (node->kind) {
        case AST_STATEMENT_ADD: {
            int nsrc = node->decimal_places;
            AstNode *sum = fold_ops(AST_EXPR_ADD, ast_clone(ops[0]), ops + 1, nsrc - 1);
            if (giving) lower_giving(ir, sum, ops + nsrc, n - nsrc, keep);
            else lower_in_place(ir, AST_EXPR_ADD, sum, ops + nsrc, n - nsrc, keep);
            ast_free(sum);
            break;
        }
        case AST_STATEMENT_SUBTRACT: {
            int nsrc = node->decimal_places;
            if (giving) {
                AstNode *tree = fold_ops(AST_EXPR_SUB, ast_clone(ops[nsrc]), ops, nsrc);
                lower_giving(ir, tree, ops + nsrc + 1, n - nsrc - 1, keep);
                ast_free(tree);
            } else {
                AstNode *sum = fold_ops(AST_EXPR_ADD, ast_clone(ops[0]), ops + 1, nsrc - 1);
                lower_in_place(ir, AST_EXPR_SUB, sum, ops + nsrc, n - nsrc, keep);
                ast_free(sum);
            }
            break;
        }
        case AST_STATEMENT_MULTIPLY:
            if (giving) {
                AstNode *tree = mk_bin(AST_EXPR_MUL, ast_clone(ops[0]), ast_clone(ops[1]));
                lower_giving(ir, tree, ops + 2, n - 2, keep);
                ast_free(tree);
            } else {
                lower_in_place(ir, AST_EXPR_MUL, ops[0], ops + 1, n - 1, keep);
            }
            break;
        case AST_STATEMENT_DIVIDE: {
            bool is_by = node->name && strcasecmp(node->name, "BY") == 0;
            bool has_rem = node->is_signed;
            if (!giving) {
                lower_in_place(ir, AST_EXPR_DIV, ops[0], ops + 1, n - 1, keep);
                break;
            }
            AstNode *dividend = is_by ? ops[0] : ops[1];
            AstNode *divisor  = is_by ? ops[1] : ops[0];
            int last_dest = has_rem ? n - 1 : n;
            AstNode *tree = mk_bin(AST_EXPR_DIV, ast_clone(dividend), ast_clone(divisor));
            lower_giving(ir, tree, ops + 2, last_dest - 2, keep);
            ast_free(tree);
            if (has_rem) {
                // remainder = dividend - q * divisor, where q is the quotient
                // truncated to the scale of the quotient receiver
                VarInfo *qv = find_item(ops[2], false);
                AstNode *q = ast_new(AST_EXPR_RESCALE, NULL, node->loc);
                q->decimal_places = qv ? qv->decimal_places : 0;
                q->children = mk_bin(AST_EXPR_DIV, ast_clone(dividend), ast_clone(divisor));
                AstNode *rem = mk_bin(AST_EXPR_SUB, ast_clone(dividend),
                                      mk_bin(AST_EXPR_MUL, q, ast_clone(divisor)));
                VarInfo *rv = find_item(ops[n - 1], false);
                if (rv) rv->assigned = true;
                lower_assign(ir, rem, ops[n - 1], keep);
                ast_free(rem);
            }
            break;
        }
        case AST_STATEMENT_COMPUTE:
            lower_giving(ir, ops[0], ops + 1, n - 1, keep);
            break;
        default:
            break;
    }

    lower_size_error_phrases(ir, node);
}

// ── Conditions ───────────────────────────────────────────────────────────────

// Return the opposite of a comparison operator.
static const char *negate_op(const char *op) {
    if (!op)              return "<>";
    if (!strcmp(op, "="))  return "<>";
    if (!strcmp(op, "<>")) return "=";
    if (!strcmp(op, ">"))  return "<=";
    if (!strcmp(op, "<"))  return ">=";
    if (!strcmp(op, ">=")) return "<";
    if (!strcmp(op, "<=")) return ">";
    return op;
}

// The operator with its operands swapped (a < b  is  b > a)
static const char *mirror_op(const char *op) {
    if (!strcmp(op, ">"))  return "<";
    if (!strcmp(op, "<"))  return ">";
    if (!strcmp(op, ">=")) return "<=";
    if (!strcmp(op, "<=")) return ">=";
    return op;
}

static bool is_arith_expr(const AstNode *n) {
    return n->kind == AST_EXPR_ADD || n->kind == AST_EXPR_SUB || n->kind == AST_EXPR_MUL ||
           n->kind == AST_EXPR_DIV || n->kind == AST_EXPR_POW || n->kind == AST_EXPR_NEG;
}

static bool is_numeric_operand(AstNode *n) {
    if (n->kind == AST_LITERAL_NUMBER || is_arith_expr(n)) return true;
    if (n->kind == AST_IDENTIFIER && !n->loop) {
        VarInfo *v = find_item(n, false);
        return v && v->is_numeric && !v->is_cond;
    }
    return false;
}

// Address and length of an operand compared as alphanumeric. A numeric
// item compares as its unsigned digits (P positions as zeros), converted
// into the text buffer named buf.
static bool alnum_operand(IrProgram *ir, AstNode *n, FieldRef *f, const char *buf) {
    if (n->kind == AST_IDENTIFIER) {
        if (!resolve_ref(ir, n, f)) return false;
        VarInfo *v = f->var;
        if (v->is_numeric && !f->refmod) {
            int s = v->decimal_places;
            int ndig = s < 0 ? v->digits - s : (s > v->digits ? s : v->digits);
            emit_load(ir, f);
            if (s < 0) emit_rescale(ir, -s, false);
            IrInstr *t = emit(ir, IR_NUM_TO_TEXT);
            t->imm = ndig;
            t->var_name = strdup(buf);
            snprintf(f->label, sizeof(f->label), "%s", buf);
            f->size = ndig;
        }
        return true;
    }
    if (n->kind == AST_LITERAL_NUMBER || n->kind == AST_STRING_LITERAL) {
        memset(f, 0, sizeof(*f));
        emit_data_lit(ir, "cmplit", n->name, f->label, sizeof(f->label));
        f->size = (int)strlen(n->name);
        return true;
    }
    diag_error(n->loc, "An arithmetic expression cannot be compared with a nonnumeric operand");
    return false;
}

// Jump to target when (left op right) is false
// A relation between a numeric item and a literal its PICTURE puts out of
// reach (or between two literals): always true or always false
static bool in_condition_name;               // lowering the test of a condition-name

static void check_constant_relation(AstNode *left, AstNode *right, const char *op) {
    if (!diag_warning_enabled(W_CONSTANT_CONDITION) || in_condition_name) return;
    long long rv;
    int rs;
    if (left->kind == AST_LITERAL_NUMBER && right->kind == AST_LITERAL_NUMBER) {
        diag_warning(W_CONSTANT_CONDITION, left->loc, "Both operands of the relation are literals, so it never changes");
        return;
    }
    if (left->kind == AST_LITERAL_NUMBER) { AstNode *t = left; left = right; right = t; op = mirror_op(op); }
    if (left->kind != AST_IDENTIFIER || right->kind != AST_LITERAL_NUMBER || left->loop || left->children) return;
    VarInfo *v = find_item(left, false);
    if (!v || !v->is_numeric || v->is_cond || v->decimal_places < 0 || v->is_index) return;
    if (!parse_num_literal(right->name, &rv, &rs) || rs > 18) return;
    // compare as scaled integers at the larger scale
    int scale = v->decimal_places > rs ? v->decimal_places : rs;
    if (scale > 18 || v->digits > 18) return;
    // (128 bits: 18 digits at a scale of up to 18 more)
    __int128 max = 1;
    for (int k = 0; k < v->digits; k++) max *= 10;
    max -= 1;                                           // at the item's scale
    for (int k = v->decimal_places; k < scale; k++) max *= 10;
    __int128 lit = rv;
    for (int k = rs; k < scale; k++) lit *= 10;
    __int128 min = v->is_signed ? -max : 0;
    int truth = -1;                                     // 0 always false, 1 always true
    bool eq = !strcmp(op, "="), ne = !strcmp(op, "<>");
    if (lit > max || lit < min) {
        bool above = lit > max;
        if (eq) truth = 0;
        else if (ne) truth = 1;
        else if (!strcmp(op, ">") || !strcmp(op, ">=")) truth = above ? 0 : 1;
        else truth = above ? 1 : 0;
    } else if (lit == max && !strcmp(op, ">")) truth = 0;
    else if (lit == max && !strcmp(op, "<=")) truth = 1;
    else if (lit == min && !strcmp(op, "<")) truth = 0;
    else if (lit == min && !strcmp(op, ">=")) truth = 1;
    else if (rs > v->decimal_places) {
        // a literal with more decimal places than the item can hold
        long long unit = 1;
        for (int k = v->decimal_places; k < scale; k++) unit *= 10;
        if (lit % unit != 0) truth = eq ? 0 : ne ? 1 : -1;
    }
    if (truth < 0) return;
    // the item's largest value (all nines), written with its decimal point
    char bound[48];
    {
        int dp = v->decimal_places, n = 0;
        int ip = v->digits - dp;
        if (ip <= 0) bound[n++] = '0';
        for (int k = 0; k < ip && n < 40; k++) bound[n++] = '9';
        if (dp > 0) {
            bound[n++] = '.';
            for (int k = 0; k < dp && n < 46; k++) bound[n++] = (k < -ip) ? '0' : '9';
        }
        bound[n] = '\0';
    }
    const char *pic = pic_of(v) ? pic_of(v) : "?";
    SourceLocation at = v->decl ? v->decl->loc : left->loc;
    if (diag_warning(W_CONSTANT_CONDITION, right->loc, "The relation is always %s", truth ? "true" : "false")) {
        if (lit > max) diag_note(at, "'%s' is at most %s (PICTURE %s)", v->name, bound, pic);
        else if (lit < min) diag_note(at, "'%s' is at least %s%s (PICTURE %s)", v->name, v->is_signed ? "-" : "",
                                      v->is_signed ? bound : "0", pic);
        else if (lit == max) diag_note(at, "'%s' is never above %s (PICTURE %s)", v->name, bound, pic);
        else if (lit == min) diag_note(at, "'%s' is never below %s (PICTURE %s)", v->name, v->is_signed ? right->name : "0", pic);
        else diag_note(at, "'%s' has %d decimal place%s (PICTURE %s)", v->name, v->decimal_places,
                       v->decimal_places == 1 ? "" : "s", pic);
    }
}

// The same item on both sides, with no subscripts or reference modification
static bool same_plain_item(const AstNode *a, const AstNode *b) {
    if (a->kind != AST_IDENTIFIER || b->kind != AST_IDENTIFIER || a->loop || b->loop ||
        a->children || b->children || strcasecmp(a->name, b->name) != 0)
        return false;
    VarInfo *va = find_item(a, false), *vb = find_item(b, false);
    return va && va == vb && !va->is_cond;
}

// Relations whose outcome or meaning the categories of their operands decide
static void check_relation_operands(AstNode *left, AstNode *right, const char *op) {
    if (in_condition_name) return;
    bool eq = !strcmp(op, "="), ne = !strcmp(op, "<>");
    if (same_plain_item(left, right)) {
        bool truth = eq || !strcmp(op, ">=") || !strcmp(op, "<=");
        diag_warning(W_CONSTANT_CONDITION, right->loc, "Both sides of the relation are '%s', so it is always %s",
                     left->name, truth ? "true" : "false");
        return;
    }
    bool ln = is_numeric_operand(left), rn = is_numeric_operand(right);
    if (ln == rn) return;
    AstNode *num = ln ? left : right, *other = ln ? right : left;
    if (other->kind == AST_STRING_LITERAL && num->kind == AST_IDENTIFIER && (eq || ne)) {
        // a numeric item holds digits only
        bool digits = other->name[0] != '\0';
        for (const char *p = other->name; *p; p++) if (!isdigit((unsigned char)*p)) digits = false;
        if (!digits) {
            diag_warning(W_CONSTANT_CONDITION, other->loc,
                         "'%s' is numeric and holds digits only, so it is never equal to \"%s\": the relation is always %s",
                         num->name, other->name, eq ? "false" : "true");
            return;
        }
    }
    char nn[96];
    if (num->kind == AST_IDENTIFIER) snprintf(nn, sizeof(nn), "'%s' is numeric", num->name);
    else if (num->kind == AST_LITERAL_NUMBER) snprintf(nn, sizeof(nn), "%s is a number", num->name);
    else snprintf(nn, sizeof(nn), "the expression is numeric");
    if (other->kind == AST_STRING_LITERAL)
        diag_warning(W_COMPARE, other->loc, "%s and the literal \"%s\" is not: they are compared as characters",
                     nn, other->name);
    else if (other->kind == AST_IDENTIFIER)
        diag_warning(W_COMPARE, other->loc, "%s and '%s' is not: they are compared as characters", nn, other->name);
}

static void emit_relation_false_jump(IrProgram *ir, AstNode *left, AstNode *right,
                                     const char *op, const char *target) {
    int fl = figurative_char(left), fr = figurative_char(right);
    IrInstr *jump = NULL;
    if ((is_numeric_operand(left) || fl == '0') && (is_numeric_operand(right) || fr == '0') &&
        !(fl == '0' && fr == '0')) {
        // ZERO compared with a numeric operand is the number 0
        if (num_expr_valid(left) && num_expr_valid(right)) {
            if (same_plain_item(left, right)) check_relation_operands(left, right, op);
            else check_constant_relation(left, right, op);
            int W = max_leaf_scale(left);
            if (max_leaf_scale(right) > W) W = max_leaf_scale(right);
            W = W + 2 > 18 ? 18 : W + 2;
            int sl = num_expr(NULL, left, W), sr = num_expr(NULL, right, W);
            int s = sl > sr ? sl : sr;
            num_expr(ir, left, W);
            emit_rescale(ir, s - sl, false);
            num_expr(ir, right, W);
            emit_rescale(ir, s - sr, false);
            jump = emit(ir, IR_NUM_CMP_JUMP_IF_FALSE);
        }
    } else if (fl != -1 || fr != -1) {
        if (fl != -1 && fr != -1) {
            diag_error(left->loc, "Two figurative constants cannot be compared");
        } else {
            if (fl != -1) {
                AstNode *t = left; left = right; right = t;
                fr = fl;
                op = mirror_op(op);
            }
            FieldRef f;
            if (alnum_operand(ir, left, &f, "$numtext")) {
                jump = emit(ir, IR_FILL_CMP_JUMP_IF_FALSE);
                jump->var_name = strdup(f.label);
                jump->size = f.size;
                if (fr == FIG_PATTERN) {
                    char lit[32];
                    emit_data_lit(ir, "patlit", right->text, lit, sizeof(lit));
                    jump->lit_name = strdup(lit);
                    jump->lit_len = (int)strlen(right->text);
                } else {
                    jump->pad_char = (char)fr;
                }
            }
        }
    } else {
        FieldRef lf, rf;
        check_relation_operands(left, right, op);
        if (alnum_operand(ir, left, &lf, "$numtext") && alnum_operand(ir, right, &rf, "$numtext2")) {
            jump = emit(ir, IR_ALNUM_CMP_JUMP_IF_FALSE);
            jump->var_name = strdup(lf.label);
            jump->size = lf.size;
            jump->lit_name = strdup(rf.label);
            jump->lit_len = rf.size;
        }
    }
    if (jump) {
        jump->cmp_op = strdup(op);
        jump->jump_target = strdup(target);
    }
}

static AstNode *make_relation(AstNode *left, const char *op, AstNode *right) {
    AstNode *c = ast_new(AST_CONDITION, NULL, left->loc);
    c->text = strdup(op);
    c->children = left;
    left->next = right;
    return c;
}

// The data item of a condition-name, with the condition-name's subscripts
static AstNode *condition_item_ref(const VarInfo *cv, const AstNode *cond_ref) {
    AstNode *item = item_ref(cv->parent, cond_ref->loc);
    item->children = ast_clone_list(cond_ref->children);
    return item;
}

// Condition-name as an explicit condition on its data item:
// item = v1 OR item = v2 OR (item >= lo AND item <= hi) ...
static AstNode *condition_name_tree(const VarInfo *cv, const AstNode *cond_ref) {
    SourceLocation loc = cond_ref->loc;

    AstNode *tree = NULL;
    for (AstNode *v = cv->cond_values; v; v = v->next) {
        AstNode *alt;
        AstNode *lo = ast_clone(v);
        ast_free(lo->extra);
        lo->extra = NULL;
        if (v->extra) {
            AstNode *hi = ast_clone(v->extra);
            AstNode *ge = make_relation(condition_item_ref(cv, cond_ref), ">=", lo);
            AstNode *le = make_relation(condition_item_ref(cv, cond_ref), "<=", hi);
            alt = ast_new(AST_COND_AND, NULL, loc);
            alt->children = ge;
            ge->next = le;
        } else {
            alt = make_relation(condition_item_ref(cv, cond_ref), "=", lo);
        }
        if (tree) {
            AstNode *or = ast_new(AST_COND_OR, NULL, loc);
            or->children = tree;
            tree->next = alt;
            tree = or;
        } else {
            tree = alt;
        }
    }
    return tree;
}

// Class condition: NUMERIC, ALPHABETIC, ALPHABETIC-LOWER, ALPHABETIC-UPPER
static void emit_class_jump(IrProgram *ir, AstNode *c, const char *target, bool when) {
    AstNode *operand = c->children;
    if (operand->kind != AST_IDENTIFIER) {
        diag_error(operand->loc, "A class condition requires a data item");
        return;
    }
    FieldRef f;
    if (!resolve_ref(ir, operand, &f)) return;
    VarInfo *v = f.var;
    bool numeric = v->is_numeric && !f.refmod;
    // Class codes of runtime_class_test (runtime/ABI.md section 3)
    int code;
    if (strcmp(c->name, "USER") == 0) {
        ClassInfo *cls = NULL;
        for (int k = 0; k < num_classes; k++)
            if (strcasecmp(classes[k].name, c->text) == 0) cls = &classes[k];
        if (!cls) {
            diag_error(c->loc, "'%s' is not a class-name", c->text);
            return;
        }
        char members[32];
        emit_data_lit(ir, "class", cls->members, members, sizeof(members));
        IrInstr *j = emit(ir, IR_CLASS_JUMP);
        j->var_name = strdup(f.label);
        j->size = f.size;
        j->imm = 9;
        j->lit_name = strdup(members);
        j->lit_len = cls->count;
        j->flags = when ? 1 : 0;
        j->jump_target = strdup(target);
        return;
    }
    if (strcmp(c->name, "NUMERIC") == 0) {
        if (numeric && (v->usage_type == USAGE_COMP || v->usage_type == USAGE_COMP5)) {
            if (when) emit_jump(ir, IR_JUMP, target); // binary items are always numeric
            return;
        }
        if (numeric && v->usage_type == USAGE_COMP3) code = 5;
        else if (numeric && v->is_signed) {
            int f = var_flags(v);
            code = (f & IR_NUM_SIGN_SEPARATE) ? ((f & IR_NUM_SIGN_LEADING) ? 8 : 7)
                                              : ((f & IR_NUM_SIGN_LEADING) ? 6 : 1);
        } else {
            code = 0;
        }
    } else if (strcmp(c->name, "ALPHABETIC") == 0) {
        code = 2;
    } else if (strcmp(c->name, "ALPHABETIC-LOWER") == 0) {
        code = 3;
    } else {
        code = 4;
    }
    IrInstr *j = emit(ir, IR_CLASS_JUMP);
    j->var_name = strdup(f.label);
    j->size = f.size;
    j->imm = code;
    j->flags = when ? 1 : 0;
    j->jump_target = strdup(target);
}

// Emit a jump to target taken when cond evaluates to `when`; falls through
// otherwise. AND and OR short-circuit.
static void emit_cond_jump(IrProgram *ir, AstNode *cond, const char *target, bool when) {
    if (!cond) return;
    switch (cond->kind) {
        case AST_BOOL_LITERAL:
            if ((strcmp(cond->name, "TRUE") == 0) == when) emit_jump(ir, IR_JUMP, target);
            break;
        case AST_CONDITION: {
            const char *op = cond->text ? cond->text : "=";
            emit_relation_false_jump(ir, cond->children, cond->children->next,
                                     when ? negate_op(op) : op, target);
            break;
        }
        case AST_COND_NOT:
            emit_cond_jump(ir, cond->children, target, !when);
            break;
        case AST_COND_AND:
        case AST_COND_OR: {
            AstNode *l = cond->children, *r = l->next;
            bool is_and = cond->kind == AST_COND_AND;
            if (is_and != when) {
                // AND jumping on false, OR jumping on true: either side decides
                emit_cond_jump(ir, l, target, when);
                emit_cond_jump(ir, r, target, when);
            } else {
                char skip[32];
                new_label(skip, sizeof(skip), "cond_skip");
                emit_cond_jump(ir, l, skip, !when);
                emit_cond_jump(ir, r, target, when);
                emit_label(ir, skip);
            }
            break;
        }
        case AST_COND_CLASS:
            emit_class_jump(ir, cond, target, when);
            break;
        case AST_COND_SIGN: {
            const char *op = strcmp(cond->name, "POSITIVE") == 0 ? ">" :
                             strcmp(cond->name, "NEGATIVE") == 0 ? "<" : "=";
            if (!is_numeric_operand(cond->children)) {
                diag_error(cond->loc, "A sign condition requires a numeric operand");
                break;
            }
            AstNode *zero = ast_new(AST_LITERAL_NUMBER, "0", cond->loc);
            emit_relation_false_jump(ir, cond->children, zero, when ? negate_op(op) : op, target);
            ast_free(zero);
            break;
        }
        case AST_COND_NAME: {
            VarInfo *cv = find_item(cond, true);
            if (!cv) break;
            if (!cv->is_cond) {
                diag_error(cond->loc, "'%s' is not a condition-name, so it is not a condition by itself", cond->name);
                note_item(cv);
                diag_note_plain("compare it with a value, for example IF %s = ...", cond->name);
                break;
            }
            if (cv->switch_value >= 0) {
                // true when the switch is in the state the name stands for
                IrInstr *j = emit(ir, IR_SWITCH_JUMP);
                j->imm = cv->switch_number;
                j->flags = when ? cv->switch_value : !cv->switch_value;
                j->jump_target = strdup(target);
                break;
            }
            AstNode *tree = condition_name_tree(cv, cond);
            bool saved = in_condition_name;
            in_condition_name = true;
            emit_cond_jump(ir, tree, target, when);
            in_condition_name = saved;
            ast_free(tree);
            break;
        }
        default:
            diag_error(cond->loc, "Invalid condition");
            break;
    }
}

// ── Statements ───────────────────────────────────────────────────────────────

// Alphanumeric move of size bytes from src_label into a field
static void emit_move_alpha(IrProgram *ir, const FieldRef *dest, const char *src_label, int src_size) {
    IrInstr *m = new_instr(IR_CALL_MOVE_ALPHA, dest->label, src_label, NULL,
                           src_size, dest->size, 0, 0, USAGE_DISPLAY);
    m->flags = (dest->var->justified && !dest->refmod) ? 1 : 0;
    append_instr(ir, m);
}

static void emit_fill(IrProgram *ir, const FieldRef *dest, int ch) {
    append_instr(ir, new_instr(IR_CALL_MOVE_FILL, dest->label, NULL, NULL, 0, dest->size, 0,
                               (char)ch, USAGE_DISPLAY));
}

static void emit_edit_alnum(IrProgram *ir, const FieldRef *dest, const char *src_label, int src_size);

// A figurative constant moved to an alphanumeric edited item is edited like
// a literal of the item's length (insertion characters stay)
static void emit_edit_figurative(IrProgram *ir, const FieldRef *dest, const char *pattern, int pattern_len) {
    int n = dest->size;
    char *text = malloc((size_t)n);
    if (!text) return;
    for (int k = 0; k < n; k++) text[k] = pattern[k % pattern_len];
    char label[32];
    new_label(label, sizeof(label), "figlit");
    IrInstr *t = emit(ir, IR_DATA_BYTES);       // any bytes, LOW-VALUE included
    t->var_name = strdup(label);
    t->size = n;
    t->content = text;
    emit_edit_alnum(ir, dest, label, n);
}

// Fill a field with a figurative constant (character or ALL "pattern")
static void emit_fill_figurative(IrProgram *ir, const FieldRef *dest, const AstNode *fig) {
    int ch = figurative_char(fig);
    if (is_alnum_edited(dest->var) && !dest->refmod && dest->size > 0) {
        char c = (char)ch;
        if (ch == FIG_PATTERN) emit_edit_figurative(ir, dest, fig->text, (int)strlen(fig->text));
        else emit_edit_figurative(ir, dest, &c, 1);
        return;
    }
    if (ch != FIG_PATTERN) {
        emit_fill(ir, dest, ch);
        return;
    }
    char lit[32];
    emit_data_lit(ir, "patlit", fig->text, lit, sizeof(lit));
    append_instr(ir, new_instr(IR_CALL_MOVE_FILL, dest->label, lit, NULL,
                               (int)strlen(fig->text), dest->size, 0, 0, USAGE_DISPLAY));
}

// Alphanumeric editing of size bytes at src_label into an alphanumeric
// edited receiver
static void emit_edit_alnum(IrProgram *ir, const FieldRef *dest, const char *src_label, int src_size) {
    IrInstr *e = emit(ir, IR_CALL_EDIT_ALNUM);
    e->var_name = strdup(dest->label);
    e->lit_name = strdup(src_label);
    e->lit_len = src_size;
    e->size = dest->size;
    e->content = strdup(mask_label(ir, dest->var));
}

// Push the value of a numeric or numeric edited identifier; returns its scale
static int push_item_value(IrProgram *ir, AstNode *src, VarInfo *sv, bool *ok) {
    FieldRef sf;
    *ok = resolve_ref(ir, src, &sf);
    if (!*ok) return 0;
    if (is_num_edited(sv)) {
        IrInstr *d = emit(ir, IR_NUM_DEEDIT);
        d->var_name = strdup(sf.label);
        d->size = sf.size;
    } else {
        emit_load(ir, &sf);
    }
    return sv->decimal_places;
}

// MOVE: the value on the stack (scale s) to the receiver's scale. Digits
// the receiver cannot hold are dropped first, so scaling up cannot overflow.
static void move_rescale(IrProgram *ir, const VarInfo *dv, int s) {
    int shift = dv->decimal_places - s;
    if (shift > 0) {
        int keep = dv->digits - dv->decimal_places + s;     // receiver integer digits + s
        if (keep >= 0 && keep < 19) emit_num(ir, IR_NUM_TRUNC, keep, 0);
    }
    emit_rescale(ir, shift, false);
}

// An alphanumeric source (field or literal) moves to a numeric item as an
// unsigned integer of its length (high-order digits beyond the receiver are lost)
static void move_text_to_numeric(IrProgram *ir, const FieldRef *sf, VarInfo *dv, AstNode *dest_node) {
    static VarInfo text_view;
    memset(&text_view, 0, sizeof(text_view));
    text_view.usage_type = USAGE_DISPLAY;
    text_view.is_numeric = true;
    text_view.size = sf->size > 18 ? 18 : sf->size;
    text_view.digits = text_view.size;
    FieldRef tf = *sf;
    tf.var = &text_view;
    if (sf->size > 18) {
        char shifted[96];
        snprintf(shifted, sizeof(shifted), "%s+%d", sf->label, sf->size - 18);
        snprintf(tf.label, sizeof(tf.label), "%.63s", shifted);
    }
    emit_load(ir, &tf);
    move_rescale(ir, dv, 0);
    FieldRef dest;
    if (resolve_receiver(ir, dest_node, &dest)) emit_store(ir, &dest, false);
}

// MOVE src TO dest (one receiver). Receiver subscripts are evaluated just
// before the data is moved.
// Digits of a numeric literal: integer (without leading zeros) and decimal
static void literal_digits(const char *t, int *int_digits, int *frac_digits, bool *negative) {
    *int_digits = *frac_digits = 0;
    *negative = *t == '-';
    while (*t == '+' || *t == '-') t++;
    while (*t == '0') t++;
    bool frac = false;
    for (; *t; t++) {
        if (*t == '.') frac = true;
        else if (isdigit((unsigned char)*t)) { if (frac) (*frac_digits)++; else (*int_digits)++; }
    }
}

// Integer digit positions of a numeric item (99PP: 4)
static int int_capacity(const VarInfo *v) {
    int n = v->digits - v->decimal_places;
    return n < 0 ? 0 : n;
}

// Warnings for a MOVE that loses characters or digits
static void check_move(const AstNode *src, const VarInfo *dv, const AstNode *dest_node) {
    if (dest_node->loop || dv->is_cond || dv->is_group || dv->size <= 0 || dv->occurs < 0) return;
    bool dest_value = dv->is_numeric || is_num_edited(dv);
    if (src->kind == AST_STRING_LITERAL && !dv->is_numeric) {
        int len = (int)strlen(src->name);
        if (len > dv->size && diag_warning(W_TRUNCATE, src->loc, "The literal \"%s\" has %d characters; '%s' holds %d, so it is truncated",
                                           src->name, len, dv->name, dv->size))
            note_item(dv);
        return;
    }
    if (src->kind == AST_LITERAL_NUMBER && dest_value) {
        int idig, fdig;
        bool neg;
        literal_digits(src->name, &idig, &fdig, &neg);
        int fcap = dv->decimal_places > 0 ? dv->decimal_places : 0;
        // decimal zeros at the end lose nothing
        const char *dot = strchr(src->name, '.');
        if (dot) {
            const char *e = src->name + strlen(src->name);
            while (e > dot + 1 && e[-1] == '0') { e--; fdig--; }
        }
        bool shown = false;
        if (idig > int_capacity(dv))
            shown = diag_warning(W_TRUNCATE, src->loc, "%s does not fit '%s': its high-order digits are lost", src->name, dv->name);
        else if (fdig > fcap)
            shown = diag_warning(W_TRUNCATE, src->loc, "%s has %d decimal place%s; '%s' keeps %d, so the rest are truncated",
                                 src->name, fdig, fdig == 1 ? "" : "s", dv->name, fcap);
        else if (neg && !dv->is_signed && dv->is_numeric)
            shown = diag_warning(W_TRUNCATE, src->loc, "'%s' has no sign, so the value of %s is stored without its sign",
                                 dv->name, src->name);
        if (shown) note_item(dv);
        return;
    }
    if (src->kind == AST_LITERAL_NUMBER && !dest_value) {
        int len = (int)strlen(src->name);
        if (len > dv->size && diag_warning(W_TRUNCATE, src->loc, "%s has %d characters; '%s' holds %d, so it is truncated",
                                           src->name, len, dv->name, dv->size))
            note_item(dv);
        return;
    }
    if (src->kind != AST_IDENTIFIER || src->loop) return;
    VarInfo *sv = find_item(src, false);
    if (!sv || sv->is_cond || sv->occurs < 0) return;
    if (sv->is_group && dv->is_numeric && !src->children) {
        if (diag_warning(W_GROUP_MOVE, src->loc, "'%s' is a group: its characters are moved to the numeric item '%s' "
                         "as they are, not converted", sv->name, dv->name))
            note_item(dv);
        return;
    }
    if (!diag_warning_enabled(W_NARROWING)) return;
    if (sv->is_numeric && dv->is_numeric) {
        if (int_capacity(sv) > int_capacity(dv) &&
            diag_warning(W_NARROWING, src->loc, "'%s' has %d integer digits, '%s' %d: high-order digits can be lost",
                         sv->name, int_capacity(sv), dv->name, int_capacity(dv)))
            note_item(dv);
        else if (sv->decimal_places > dv->decimal_places && dv->decimal_places >= 0 &&
                 diag_warning(W_NARROWING, src->loc, "'%s' has %d decimal place%s, '%s' %d: the rest are truncated",
                              sv->name, sv->decimal_places, sv->decimal_places == 1 ? "" : "s", dv->name,
                              dv->decimal_places))
            note_item(dv);
        else if (sv->is_signed && !dv->is_signed &&
                 diag_warning(W_NARROWING, src->loc, "'%s' is signed and '%s' is not: a negative value loses its sign",
                              sv->name, dv->name))
            note_item(dv);
    } else if (!dest_value && !sv->is_numeric && sv->size > dv->size &&
               diag_warning(W_NARROWING, src->loc, "'%s' has %d characters, '%s' %d: the rest are truncated",
                            sv->name, sv->size, dv->name, dv->size)) {
        note_item(dv);
    }
}

static void lower_move(IrProgram *ir, AstNode *src, AstNode *dest_node, const char *src_lit) {
    VarInfo *dv = find_item(dest_node, true);
    if (!dv) return;
    check_move(src, dv, dest_node);
    bool whole = !dest_node->loop && !dv->is_cond;
    bool dest_numeric = dv->is_numeric && whole;
    bool dest_num_edited = is_num_edited(dv) && whole;
    bool dest_alnum_edited = is_alnum_edited(dv) && whole;
    bool dest_takes_value = dest_numeric || dest_num_edited;
    FieldRef dest;

    int fig = figurative_char(src);
    if (fig != -1) {
        if (dest_numeric && fig != '0') {
            diag_error(src->loc, "%s cannot be moved to the numeric item '%s'", src->name, dv->name);
            note_item(dv);
            diag_note_plain("only ZERO is a numeric figurative constant");
        } else if (dest_takes_value && fig == '0') {
            emit_num(ir, IR_NUM_CONST, 0, 0);
            if (resolve_receiver(ir, dest_node, &dest)) emit_store(ir, &dest, false);
        } else if (resolve_receiver(ir, dest_node, &dest)) {
            emit_fill_figurative(ir, &dest, src);
        }
        return;
    }

    if (src->kind == AST_STRING_LITERAL || (src->kind == AST_LITERAL_NUMBER && !dest_takes_value)) {
        const char *t = src->name;
        bool all_digits = true;
        for (const char *c = t; *c; c++) all_digits = all_digits && isdigit((unsigned char)*c);
        if (dest_numeric && !all_digits && dv->usage_type == USAGE_DISPLAY && dv->decimal_places == 0 &&
            !(dv->sign_mode & SIGN_SEPARATE) && dv->size > 0 && dv->size < 256) {
            // A literal that is not all digits into an integer DISPLAY item:
            // its characters as an unsigned integer, right-justified,
            // zero-filled, truncated on the left (as IBM and GnuCOBOL do;
            // the standard leaves the result undefined). A digit carrying
            // the sign gets the positive sign of the dialect.
            char image[256];
            int n = dv->size, len = (int)strlen(t);
            for (int k = 0; k < n; k++) {
                int j = len - n + k;
                image[k] = j >= 0 ? t[j] : '0';
            }
            image[n] = '\0';
            int sp = (dv->sign_mode & SIGN_LEADING) ? 0 : n - 1;
            if (dv->is_signed && (ir->runtime_flags & IR_RT_SIGN_EBCDIC) && isdigit((unsigned char)image[sp]))
                image[sp] = image[sp] == '0' ? '{' : (char)('A' + image[sp] - '1');
            char lit[32];
            emit_data_lit(ir, "movlit", image, lit, sizeof(lit));
            if (resolve_receiver(ir, dest_node, &dest)) emit_move_alpha(ir, &dest, lit, n);
            return;
        }
        if (dest_takes_value) {
            // as an unsigned integer, edited for a numeric edited receiver
            FieldRef sf;
            memset(&sf, 0, sizeof(sf));
            snprintf(sf.label, sizeof(sf.label), "%s", src_lit);
            sf.size = (int)strlen(src->name);
            move_text_to_numeric(ir, &sf, dv, dest_node);
            return;
        }
        if (!resolve_receiver(ir, dest_node, &dest)) return;
        if (dest_alnum_edited) emit_edit_alnum(ir, &dest, src_lit, (int)strlen(src->name));
        else emit_move_alpha(ir, &dest, src_lit, (int)strlen(src->name));
        return;
    }

    if (src->kind == AST_LITERAL_NUMBER) {
        if (!num_expr_valid(src)) return;
        int s = num_expr(ir, src, 0);
        move_rescale(ir, dv, s);
        if (resolve_receiver(ir, dest_node, &dest)) emit_store(ir, &dest, false);
        return;
    }

    if (src->kind != AST_IDENTIFIER) return;
    VarInfo *sv = find_item(src, true);
    if (!sv) return;
    bool src_whole = !src->loop && !sv->is_cond;
    bool src_has_value = (sv->is_numeric || is_num_edited(sv)) && src_whole;

    if ((sv->is_group && src_whole) || (dv->is_group && whole)) {
        // A group on either side: an alphanumeric move without conversion
        // or editing
        FieldRef sf;
        if (!resolve_ref(ir, src, &sf) || !resolve_receiver(ir, dest_node, &dest)) return;
        emit_move_alpha(ir, &dest, sf.label, sf.size);
        return;
    }

    if (src_has_value && dest_takes_value) {
        // Numeric move (de-editing a numeric edited source)
        bool ok;
        int s = push_item_value(ir, src, sv, &ok);
        if (!ok) return;
        move_rescale(ir, dv, s);
        if (resolve_receiver(ir, dest_node, &dest)) emit_store(ir, &dest, false);
    } else if (!dest_numeric && !dest_num_edited && sv->is_numeric && src_whole) {
        // A numeric item moves as its unsigned digits, with P positions as
        // zeros (99PP holding 1200 moves as "1200", PP99 as "0012")
        int digits = sv->digits, s = sv->decimal_places;
        int ndig = s < 0 ? digits - s : (s > digits ? s : digits);
        FieldRef sf;
        if (!resolve_ref(ir, src, &sf)) return;
        emit_load(ir, &sf);
        if (s < 0) emit_rescale(ir, -s, false);
        IrInstr *t = emit(ir, IR_NUM_TO_TEXT);
        t->imm = ndig;
        t->var_name = strdup("$numtext");
        if (!resolve_receiver(ir, dest_node, &dest)) return;
        if (dest_alnum_edited) emit_edit_alnum(ir, &dest, "$numtext", ndig);
        else emit_move_alpha(ir, &dest, "$numtext", ndig);
    } else if (!dest_numeric && !dest_num_edited) {
        FieldRef sf;
        if (!resolve_ref(ir, src, &sf) || !resolve_receiver(ir, dest_node, &dest)) return;
        if (dest_alnum_edited) emit_edit_alnum(ir, &dest, sf.label, sf.size);
        else emit_move_alpha(ir, &dest, sf.label, sf.size);
    } else {
        // An alphanumeric source moves to a numeric item as an unsigned
        // integer of its length (high-order digits beyond the receiver are lost)
        FieldRef sf;
        if (!resolve_ref(ir, src, &sf)) return;
        if (sf.size < 0) {
            diag_error(src->loc, "MOVE of a variable-length item to numeric '%s' is not supported yet", dv->name);
            return;
        }
        move_text_to_numeric(ir, &sf, dv, dest_node);
    }
}

// x = value (VARYING initialization) and x = x + by (augmentation)
static void lower_varying_set(IrProgram *ir, AstNode *var, AstNode *value, bool add) {
    emit(ir, IR_SIZE_ERR_CLEAR);
    AstNode *tree = add ? mk_bin(AST_EXPR_ADD, ast_clone(var), ast_clone(value)) : ast_clone(value);
    lower_assign(ir, tree, var, false);
    ast_free(tree);
    debug_item_event(ir, var, var->loc.line);
}

// The inline PERFORMs being lowered, innermost last, for EXIT PERFORM
// [CYCLE]: where each leaves the loop and where it ends an iteration (the
// labels are emitted only when used)
typedef struct {
    char exit_lbl[32], cycle_lbl[32];
    char times_done[32];        // PERFORM TIMES: its own exit (restores its counter)
    bool exit_used, cycle_used;
} PerformExits;
static PerformExits perform_exits[64];
static int perform_depth;

// EXIT PARAGRAPH / EXIT SECTION: the end of the current paragraph or section
static int exit_para = -1, exit_sec = -1;       // the procedure, -1 outside one
static bool exit_para_used, exit_sec_used;

static void lower_exit_jump(IrProgram *ir, AstNode *node) {
    const char *kind = node->name;
    char lbl[48];
    if (!strcmp(kind, "PERFORM") || !strcmp(kind, "CYCLE")) {
        if (perform_depth == 0 || perform_depth > 64) {
            diag_error(node->loc, "EXIT PERFORM%s is not inside an inline PERFORM", !strcmp(kind, "CYCLE") ? " CYCLE" : "");
            return;
        }
        PerformExits *x = &perform_exits[perform_depth - 1];
        if (!strcmp(kind, "CYCLE")) {
            x->cycle_used = true;
            emit_jump(ir, IR_JUMP, x->cycle_lbl);
        } else {
            x->exit_used = !x->times_done[0];
            emit_jump(ir, IR_JUMP, x->times_done[0] ? x->times_done : x->exit_lbl);
        }
        return;
    }
    bool section = !strcmp(kind, "SECTION");
    int proc = section ? exit_sec : exit_para;
    if (proc < 0) {
        diag_error(node->loc, "EXIT %s is not inside a %s", section ? "SECTION" : "PARAGRAPH",
                   section ? "section" : "paragraph");
        return;
    }
    snprintf(lbl, sizeof(lbl), "$exit_%s_%d", section ? "sec" : "para", proc);
    if (section) exit_sec_used = true; else exit_para_used = true;
    emit_jump(ir, IR_JUMP, lbl);
}

// The statements of one PERFORM iteration: the procedure range or the inline body
static void lower_perform_body(IrProgram *ir, AstNode *node, int first, int last) {
    if (node->extra) {
        IrInstr *p = emit(ir, IR_PERFORM);
        p->imm = first;
        p->proc_end = last;
    } else {
        lower_block(ir, node->children);
        if (perform_depth > 0 && perform_depth <= 64 && perform_exits[perform_depth - 1].cycle_used) {
            emit_label(ir, perform_exits[perform_depth - 1].cycle_lbl);
            perform_exits[perform_depth - 1].cycle_used = false;    // (each label once)
        }
    }
}

// PERFORM VARYING with AFTER phrases, in the order of the standard
// (clause 1 is VARYING, clauses 2..n the AFTER phrases):
// TEST BEFORE:  set all identifiers to their FROM values;
//   L1: if cond1 done
//   Lk: if condk { augment id(k-1); set idk to its FROM value; goto L(k-1) }
//   body; augment idn; goto Ln
// TEST AFTER:   set all identifiers; B: body;
//   for k = n..1: if not condk { augment idk; set id(k+1..n) to FROM; goto B }
//   done
// The identifiers of a condition (and of its expressions and subscripts)
static void condition_idents(AstNode *n, AstNode **out, int *count, int max) {
    for (; n; n = n->next) {
        if (n->kind == AST_IDENTIFIER && *count < max) out[(*count)++] = n;
        condition_idents(n->children, out, count, max);
        if (n->kind != AST_IDENTIFIER) condition_idents(n->extra, out, count, max);
    }
}

// A PERFORM UNTIL condition: jump to target when it is `when`. Under the
// Debug module, each evaluation of an item the condition names runs its
// debugging procedures (after the evaluation, whatever the outcome).
static void until_cond_jump(IrProgram *ir, AstNode *cond, const char *target, bool when) {
    AstNode *ids[64];
    int n = 0;
    if (debugging_here()) {
        AstNode *saved = cond->next;
        cond->next = NULL;              // this condition only
        condition_idents(cond, ids, &n, 64);
        cond->next = saved;
    }
    if (n == 0) {
        emit_cond_jump(ir, cond, target, when);
        return;
    }
    char yes[32], no[32];
    new_label(yes, sizeof(yes), "until_yes");
    new_label(no, sizeof(no), "until_no");
    emit_cond_jump(ir, cond, yes, when);
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) emit_label(ir, yes);
        // each item once per evaluation
        for (int k = 0; k < n; k++) {
            VarInfo *v = find_item(ids[k], false);
            bool seen = false;
            for (int j = 0; j < k && !seen; j++) seen = find_item(ids[j], false) == v;
            if (v && !seen) debug_item_event(ir, ids[k], cond->loc.line);
        }
        emit_jump(ir, IR_JUMP, pass == 0 ? no : target);
    }
    emit_label(ir, no);
}

static void lower_varying(IrProgram *ir, AstNode *node, AstNode *clauses, int first, int last) {
    AstNode *cl[16];
    int n = 0;
    for (AstNode *c = clauses; c && n < 16; c = c->next) cl[n++] = c;
    #define VAR(k)  (cl[k]->children)
    #define FROM(k) (cl[k]->children->next)
    #define BY(k)   (cl[k]->children->next->next)
    #define COND(k) (cl[k]->children->next->next->next)

    char done_lbl[32];
    new_label(done_lbl, sizeof(done_lbl), "vary_done");
    for (int k = 0; k < n; k++) lower_varying_set(ir, VAR(k), FROM(k), false);

    if (!node->is_signed) {
        char test_lbl[16][32];
        for (int k = 0; k < n; k++) new_label(test_lbl[k], sizeof(test_lbl[k]), "vary_test");
        for (int k = 0; k < n; k++) {
            emit_label(ir, test_lbl[k]);
            if (k == 0) {
                until_cond_jump(ir, COND(0), done_lbl, true);
            } else {
                char cont[32];
                new_label(cont, sizeof(cont), "vary_cont");
                until_cond_jump(ir, COND(k), cont, false);
                lower_varying_set(ir, VAR(k - 1), BY(k - 1), true);
                lower_varying_set(ir, VAR(k), FROM(k), false);
                emit_jump(ir, IR_JUMP, test_lbl[k - 1]);
                emit_label(ir, cont);
            }
        }
        lower_perform_body(ir, node, first, last);
        lower_varying_set(ir, VAR(n - 1), BY(n - 1), true);
        emit_jump(ir, IR_JUMP, test_lbl[n - 1]);
    } else {
        char body_lbl[32];
        new_label(body_lbl, sizeof(body_lbl), "vary_body");
        emit_label(ir, body_lbl);
        lower_perform_body(ir, node, first, last);
        for (int k = n - 1; k >= 0; k--) {
            char next_lbl[32];
            new_label(next_lbl, sizeof(next_lbl), "vary_next");
            until_cond_jump(ir, COND(k), next_lbl, true);
            lower_varying_set(ir, VAR(k), BY(k), true);
            for (int j = k + 1; j < n; j++) lower_varying_set(ir, VAR(j), FROM(j), false);
            emit_jump(ir, IR_JUMP, body_lbl);
            emit_label(ir, next_lbl);
        }
    }
    emit_label(ir, done_lbl);
    #undef VAR
    #undef FROM
    #undef BY
    #undef COND
}

static void lower_perform_form(IrProgram *ir, AstNode *node);

static void lower_perform(IrProgram *ir, AstNode *node) {
    if (node->extra) {                  // a procedure range
        lower_perform_form(ir, node);
        return;
    }
    // An inline PERFORM: the exits of EXIT PERFORM [CYCLE]
    if (perform_depth < 64) {
        PerformExits *x = &perform_exits[perform_depth];
        memset(x, 0, sizeof(*x));
        new_label(x->exit_lbl, sizeof(x->exit_lbl), "perform_exit");
        new_label(x->cycle_lbl, sizeof(x->cycle_lbl), "perform_cycle");
    }
    perform_depth++;
    lower_perform_form(ir, node);
    perform_depth--;
    if (perform_depth < 64 && perform_exits[perform_depth].exit_used)
        emit_label(ir, perform_exits[perform_depth].exit_lbl);
}

static void lower_perform_form(IrProgram *ir, AstNode *node) {
    int first = -1, last = -1;
    if (node->extra) {
        first = resolve_proc(node->extra);
        last = node->extra->next ? resolve_proc(node->extra->next) : first;
        if (first < 0 || last < 0) return;
        // (a range of several procedures may be entered past the current one
        // by GO TO: only the procedure itself, or its own section)
        bool inside = current_proc == first ||
                      (first == last && procs[first].is_section && current_proc >= 0 &&
                       procs[current_proc].section && !strcasecmp(procs[current_proc].section, procs[first].name));
        if (inside &&
            diag_warning(W_RECURSIVE_PERFORM, node->extra->loc, "PERFORM of '%s' from inside it: COBOL-85 leaves a "
                         "recursive PERFORM undefined (control does not return where expected)", procs[first].name))
            diag_note(proc_loc_of(first), "'%s' is here", procs[first].name);
        if (last < first &&
            diag_warning(W_PERFORM_RANGE, node->extra->next->loc, "'%s' comes before '%s', so control does not return from the range at its end",
                         procs[last].name, procs[first].name))
            diag_note(proc_loc_of(last), "'%s' is here", procs[last].name);
    }
    const char *form = node->name ? node->name : "ONCE";

    if (strcmp(form, "TIMES") == 0) {
        AstNode *count = node->loop;
        IrInstr *begin;
        long long n;
        if (is_int_literal(count, &n)) {
            if (n <= 0) return; // zero iterations
            begin = emit(ir, IR_TIMES_BEGIN);
            begin->imm = n;
        } else {
            if (!num_expr_valid(count)) return;
            push_int(ir, count);    // integer part
            begin = emit(ir, IR_TIMES_BEGIN);
            begin->flags = 1;       // count on the numeric stack
        }
        char loop_lbl[32], done_lbl[32];
        new_label(loop_lbl, sizeof(loop_lbl), "times");
        new_label(done_lbl, sizeof(done_lbl), "times_done");
        begin->var_name = strdup(loop_lbl);
        begin->jump_target = strdup(done_lbl);
        if (!node->extra && perform_depth > 0 && perform_depth <= 64)
            snprintf(perform_exits[perform_depth - 1].times_done, sizeof(perform_exits[0].times_done), "%s", done_lbl);
        lower_perform_body(ir, node, first, last);
        IrInstr *end = emit(ir, IR_TIMES_END);
        end->var_name = strdup(loop_lbl);
        end->jump_target = strdup(done_lbl);
    } else if (strcmp(form, "UNTIL") == 0) {
        char loop_lbl[32], done_lbl[32];
        new_label(loop_lbl, sizeof(loop_lbl), "until");
        new_label(done_lbl, sizeof(done_lbl), "until_done");
        emit_label(ir, loop_lbl);
        if (!node->is_signed) until_cond_jump(ir, node->loop, done_lbl, true);
        lower_perform_body(ir, node, first, last);
        if (node->is_signed) until_cond_jump(ir, node->loop, done_lbl, true);
        emit_jump(ir, IR_JUMP, loop_lbl);
        emit_label(ir, done_lbl);
    } else if (strcmp(form, "VARYING") == 0) {
        lower_varying(ir, node, node->loop, first, last);
    } else {
        lower_perform_body(ir, node, first, last);
    }
}

// GO TO proc, or GO TO proc-1 ... proc-n DEPENDING ON identifier
// Mark the paragraphs named in ALTER statements (their GO TO is alterable)
static void mark_altered(AstNode *n) {
    for (; n; n = n->next) {
        if (n->kind == AST_STATEMENT_ALTER) {
            for (AstNode *a = n->children; a; a = a->next) {
                int para = resolve_proc(a);
                if (para >= 0) altered[para] = true;
            }
            continue;
        }
        mark_altered(n->children);
        mark_altered(n->on_error);
        mark_altered(n->not_on_error);
        mark_altered(n->extra);
        mark_altered(n->loop);
    }
}

static void lower_goto(IrProgram *ir, AstNode *node) {
    if (!node->extra) {
        int target = node->children ? resolve_proc(node->children) : -1;
        if (node->children && target < 0) return;
        if (current_proc >= 0 && (altered[current_proc] || !node->children)) {
            // The paragraph's GO TO can be changed by ALTER
            IrInstr *g = emit(ir, IR_GOTO_ALTERABLE);
            g->imm = current_proc;
            g->proc_end = target;
            return;
        }
        IrInstr *g = emit(ir, IR_GOTO);
        g->imm = target;
        return;
    }
    if (!numeric_item(node->extra, "GO TO DEPENDING ON item") || !num_expr_valid(node->extra)) return;
    int k = 1;
    for (AstNode *proc = node->children; proc; proc = proc->next, k++) {
        int target = resolve_proc(proc);
        if (target < 0) return;
        char next_lbl[32];
        new_label(next_lbl, sizeof(next_lbl), "goto_next");
        push_int(ir, node->extra);
        emit_num(ir, IR_NUM_CONST, k, 0);
        IrInstr *cmp = emit(ir, IR_NUM_CMP_JUMP_IF_FALSE);
        cmp->cmp_op = strdup("=");
        cmp->jump_target = strdup(next_lbl);
        IrInstr *g = emit(ir, IR_GOTO);
        g->imm = target;
        emit_label(ir, next_lbl);
    }
}

// INITIALIZE: numeric elementary items to zero, the others to spaces;
// FILLER items, redefining items and index-names are left alone. A table is
// initialized by initializing its first occurrence and copying it to the rest.
static void init_item(IrProgram *ir, const char *base, int base_offset, int idx, bool whole);

// REPLACING phrases of the INITIALIZE being lowered (NULL: none)
static AstNode *init_replacing;

// The category of an elementary item, as INITIALIZE REPLACING names it
static const char *item_category(const VarInfo *v) {
    if (v->is_numeric) return "NUMERIC";
    if (is_num_edited(v)) return "NUMERIC-EDITED";
    if (is_alnum_edited(v)) return "ALPHANUMERIC-EDITED";
    for (AstNode *c = v->decl->children; c; c = c->next) {
        if (c->kind != AST_PIC_CLAUSE || !c->name) continue;
        for (const char *p = c->name; *p; p++) {
            char ch = (char)toupper((unsigned char)*p);
            if (ch != 'A' && ch != 'B' && ch != '(' && ch != ')' && !isdigit((unsigned char)ch))
                return "ALPHANUMERIC";
        }
        return "ALPHABETIC";
    }
    return "ALPHANUMERIC";
}

// INITIALIZE ... REPLACING: move the phrase's operand to one elementary item
static void init_replace(IrProgram *ir, FieldRef *f, AstNode *src) {
    VarInfo *v = f->var;
    if (v->is_numeric || is_num_edited(v)) {
        if (src->kind == AST_STRING_LITERAL) {
            diag_error(src->loc, "A nonnumeric literal cannot replace numeric data");
            return;
        }
        bool ok = true;
        int s;
        if (src->kind == AST_IDENTIFIER) {
            VarInfo *sv = find_item(src, true);
            if (!sv) return;
            s = push_item_value(ir, src, sv, &ok);
        } else {
            if (!num_expr_valid(src)) return;
            s = num_expr(ir, src, 0);
        }
        if (!ok) return;
        move_rescale(ir, v, s);
        emit_store(ir, f, false);
        return;
    }
    if (src->kind == AST_IDENTIFIER) {
        FieldRef sf;
        if (!resolve_ref(ir, src, &sf)) return;
        if (sf.var->is_numeric && !src->loop) {
            diag_error(src->loc, "A numeric item cannot replace nonnumeric data");
            return;
        }
        if (is_alnum_edited(v)) emit_edit_alnum(ir, f, sf.label, sf.size);
        else emit_move_alpha(ir, f, sf.label, sf.size);
        return;
    }
    char lit[32];
    emit_data_lit(ir, "lit", src->name, lit, sizeof(lit));
    if (is_alnum_edited(v)) emit_edit_alnum(ir, f, lit, (int)strlen(src->name));
    else emit_move_alpha(ir, f, lit, (int)strlen(src->name));
}

static void init_contents(IrProgram *ir, const char *base, int base_offset, int idx) {
    VarInfo *v = &vars[idx];
    char label[64];
    snprintf(label, sizeof(label), "%s+%d", base, v->offset - base_offset);
    if (v->is_group) {
        for (int c = v->first_child; c >= 0; c = vars[c].next_sibling) init_item(ir, base, base_offset, c, false);
        return;
    }
    FieldRef f = { .var = v, .size = v->size };
    snprintf(f.label, sizeof(f.label), "%s", label);
    if (init_replacing) {
        // Only items of a category named by a REPLACING phrase change
        const char *category = item_category(v);
        for (AstNode *r = init_replacing; r; r = r->next)
            if (strcmp(r->name, category) == 0) { init_replace(ir, &f, r->children); break; }
        return;
    }
    if (v->is_numeric || is_num_edited(v)) {
        emit_num(ir, IR_NUM_CONST, 0, 0);
        emit_store(ir, &f, false);
    } else if (is_alnum_edited(v)) {
        emit_edit_figurative(ir, &f, " ", 1);
    } else {
        emit_fill(ir, &f, ' ');
    }
}

static void init_item(IrProgram *ir, const char *base, int base_offset, int idx, bool whole) {
    VarInfo *v = &vars[idx];
    if (!whole && (v->is_filler || v->redefines >= 0)) return;
    init_contents(ir, base, base_offset, idx);
    if (v->occurs > 1 && !whole) {
        IrInstr *r = emit(ir, IR_CALL_REPLICATE);
        char label[64];
        snprintf(label, sizeof(label), "%s+%d", base, v->offset - base_offset);
        r->var_name = strdup(label);
        r->size = v->size;
        r->imm = v->occurs;
    }
}

static void lower_initialize(IrProgram *ir, AstNode *target) {
    FieldRef f;
    if (!resolve_ref(ir, target, &f)) return;
    if (target->loop) {
        emit_fill(ir, &f, ' ');     // a reference-modified item is alphanumeric
        return;
    }
    // f.label addresses the target occurrence; subordinate items are at their
    // offset relative to it. Tables inside the target are replicated.
    init_item(ir, f.label, f.var->offset, (int)(f.var - vars), true);
}

// SET receivers TO TRUE / TO value / UP BY / DOWN BY
static void lower_set(IrProgram *ir, AstNode *node) {
    emit(ir, IR_SIZE_ERR_CLEAR);
    const char *form = node->name ? node->name : "TRUE";
    for (AstNode *recv = node->children; recv; recv = recv->next) {
        if (strcmp(form, "SWITCH") == 0) {
            int number = 0;
            for (int k = 0; k < num_switch_names; k++)
                if (strcasecmp(switch_names[k].name, recv->name) == 0) number = switch_names[k].number;
            if (!number) {
                diag_error(recv->loc, "'%s' is not the mnemonic-name of a switch", recv->name);
                continue;
            }
            IrInstr *s = emit(ir, IR_SWITCH_SET);
            s->imm = number;
            s->flags = recv->text && strcmp(recv->text, "ON") == 0;
            continue;
        }
        if (strcmp(form, "TRUE") == 0) {
            // Move the condition-name's first value to its data item
            VarInfo *cv = find_item(recv, true);
            if (!cv || !cv->is_cond) continue;
            AstNode *value = cv->cond_values;
            AstNode *item = condition_item_ref(cv, recv);
            char lit_name[32] = {0};
            if (value->kind == AST_STRING_LITERAL || value->kind == AST_LITERAL_NUMBER)
                emit_data_lit(ir, "lit", value->name, lit_name, sizeof(lit_name));
            lower_move(ir, value, item, lit_name);
            ast_free(item);
            continue;
        }
        AstNode *tree;
        if (strcmp(form, "TO") == 0) tree = ast_clone(node->extra);
        else tree = mk_bin(strcmp(form, "UP") == 0 ? AST_EXPR_ADD : AST_EXPR_SUB,
                           ast_clone(recv), ast_clone(node->extra));
        lower_assign(ir, tree, recv, false);
        ast_free(tree);
    }
}

// SEARCH (serial, from the current index value) and SEARCH ALL (from 1).
// SEARCH ALL is done serially too: on a table ordered by its keys it finds
// the same element a binary search would.
static void lower_search(IrProgram *ir, AstNode *node) {
    AstNode *table = node->extra;
    VarInfo *tv = find_item(table, true);
    if (!tv) return;
    if (!tv->occurs || tv->first_index < 0) {
        diag_error(table->loc, "SEARCH needs a table with INDEXED BY ('%s')", table->name);
        return;
    }
    // The search index: the first index-name of the table, or the VARYING
    // item when it is one of the table's own index-names
    int search_index = tv->first_index;
    VarInfo *vv = node->loop ? find_item(node->loop, false) : NULL;
    if (vv && vv->is_index && vv->decl == tv->decl) search_index = (int)(vv - vars);
    AstNode *index = item_ref(search_index, table->loc);
    char loop_lbl[32], end_lbl[32], at_end_lbl[32];
    new_label(loop_lbl, sizeof(loop_lbl), "search");
    new_label(end_lbl, sizeof(end_lbl), "search_end");
    new_label(at_end_lbl, sizeof(at_end_lbl), "search_at_end");

    emit(ir, IR_SIZE_ERR_CLEAR);
    if (node->name && strcmp(node->name, "ALL") == 0) {
        AstNode *one = ast_new(AST_LITERAL_NUMBER, "1", table->loc);
        lower_assign(ir, one, index, false);
        ast_free(one);
    }
    emit_label(ir, loop_lbl);
    // The last occurrence: the DEPENDING ON item's value, or the fixed count
    char count[16];
    snprintf(count, sizeof(count), "%d", tv->occurs);
    AstNode *last = tv->depending ? ast_clone(tv->depending) : ast_new(AST_LITERAL_NUMBER, count, table->loc);
    AstNode *limit = make_relation(ast_clone(index), ">", last);
    emit_cond_jump(ir, limit, at_end_lbl, true);
    ast_free(limit);

    for (AstNode *when = node->children; when; when = when->next) {
        char next_lbl[32];
        new_label(next_lbl, sizeof(next_lbl), "search_next");
        emit_cond_jump(ir, when->extra, next_lbl, false);
        lower_block(ir, when->children);
        emit_jump(ir, IR_JUMP, end_lbl);
        emit_label(ir, next_lbl);
    }
    AstNode *one = ast_new(AST_LITERAL_NUMBER, "1", table->loc);
    lower_varying_set(ir, index, one, true);
    // VARYING another index or data item advances with the search index;
    // VARYING the search index itself does not advance it twice
    if (node->loop && find_item(node->loop, false) != &vars[search_index])
        lower_varying_set(ir, node->loop, one, true);
    ast_free(one);
    emit_jump(ir, IR_JUMP, loop_lbl);

    emit_label(ir, at_end_lbl);
    lower_block(ir, node->on_error);
    emit_label(ir, end_lbl);
    ast_free(index);
}

// ── Files ────────────────────────────────────────────────────────────────────

static int find_file(const AstNode *ref) {
    for (int f = 0; f < num_files; f++)
        if (strcasecmp(files[f].name, ref->name) == 0) { files[f].referenced = true; return f; }
    diag_error(ref->loc, "'%s' is not a file", ref->name);
    return -1;
}

static IrInstr *emit_file_op(IrProgram *ir, IrOpcode op, int file) {
    // An EXTERNAL file is shared: this program's FILE STATUS item first
    if (files[file].external && files[file].status_var >= 0) {
        VarInfo *st = &vars[files[file].status_var];
        char dest[64], src[64];
        record_label(dest, sizeof(dest), files[file].fcb_record, 40);
        record_label(src, sizeof(src), st->record, st->offset);
        emit_qword(ir, dest, 1, 0, src, 0);
    }
    IrInstr *i = emit(ir, op);
    i->var_name = strdup(files[file].fcb_label);
    return i;
}

static void emit_io_jump(IrProgram *ir, int low, int high, const char *target) {
    IrInstr *j = emit(ir, IR_IO_JUMP);
    j->imm = low;
    j->size = high;
    j->jump_target = strdup(target);
}

static AstNode no_phrases;          // an I-O statement without AT END / INVALID KEY

static void emit_perform_use(IrProgram *ir, int k) {
    if (uses[k].symbol) {
        // USE GLOBAL of a containing program: through its entry
        IrInstr *c = emit(ir, IR_CALL_USE);
        char sym[220];
        snprintf(sym, sizeof(sym), "@%s", uses[k].symbol);
        c->var_name = strdup(sym);
        return;
    }
    IrInstr *p = emit(ir, IR_PERFORM);
    p->imm = uses[k].first;
    p->proc_end = uses[k].last;
    p->digits = 4;                      // (for the Debug module: USE PROCEDURE)
}

// An I-O statement without AT END / INVALID KEY whose exception nothing
// handles: no FILE STATUS item, no applicable USE procedure
static void check_unhandled(const AstNode *stmt, int f, bool at_end) {
    if (f < 0 || stmt->on_error || files[f].status_var >= 0 || files[f].is_sort) return;
    if (use_for_file(files[f].name) >= 0) return;
    for (int k = 1; k <= 4; k++) if (use_for_mode(k) >= 0) return;
    if (diag_warning(W_UNCHECKED_IO, stmt->loc, "Nothing handles the %s condition of this statement, so the program cannot tell when it occurs",
                     at_end ? "at end" : "invalid key"))
        diag_note_plain("add %s, a FILE STATUS clause for '%s', or a USE procedure", at_end ? "AT END" : "INVALID KEY",
                        files[f].name);
}

// Branches after an I-O statement on file f: on_error runs for statuses
// low..high (AT END, INVALID KEY), not_on_error for a successful status
// (00-09). Any other unsuccessful status runs the applicable USE procedure:
// the one for the file, else the one for its open mode (open_mode: the mode
// of an OPEN statement, 0: the mode the file is open in).
static void lower_io_phrases(IrProgram *ir, AstNode *stmt, int low, int high, int f, int open_mode) {
    int own = f >= 0 ? use_for_file(files[f].name) : -1;
    bool any_mode = false;
    for (int k = 1; k <= 4; k++) any_mode |= use_for_mode(k) >= 0;
    bool use = f >= 0 && (own >= 0 || (open_mode ? use_for_mode(open_mode) >= 0 : any_mode));
    if (!stmt->on_error && !stmt->not_on_error && !use) return;
    char exc_lbl[32], end_lbl[32], ok_lbl[32];
    new_label(exc_lbl, sizeof(exc_lbl), "io_exception");
    new_label(end_lbl, sizeof(end_lbl), "io_end");
    new_label(ok_lbl, sizeof(ok_lbl), "io_ok");
    if (stmt->on_error && high >= low) emit_io_jump(ir, low, high, exc_lbl);
    emit_io_jump(ir, 0, 9, ok_lbl);
    if (use) {
        if (own >= 0) emit_perform_use(ir, own);
        else if (open_mode) emit_perform_use(ir, use_for_mode(open_mode));
        else {
            for (int m = 1; m <= 4; m++) {
                int k = use_for_mode(m);
                if (k < 0) continue;
                char skip[32];
                new_label(skip, sizeof(skip), "use_skip");
                IrInstr *j = emit_file_op(ir, IR_FILE_MODE_JUMP, f);
                j->imm = m;
                j->jump_target = strdup(skip);
                emit_perform_use(ir, k);
                emit_jump(ir, IR_JUMP, end_lbl);
                emit_label(ir, skip);
            }
        }
    }
    emit_jump(ir, IR_JUMP, end_lbl);    // other errors run neither phrase
    emit_label(ir, ok_lbl);
    lower_block(ir, stmt->not_on_error);
    emit_jump(ir, IR_JUMP, end_lbl);
    emit_label(ir, exc_lbl);
    lower_block(ir, stmt->on_error);
    emit_label(ir, end_lbl);
}

// The file a record name belongs to, and the record's field
static int record_file(IrProgram *ir, AstNode *rec, FieldRef *f) {
    VarInfo *v = find_item(rec, true);
    if (!v) return -1;
    if (v->file < 0 || v->parent >= 0) {
        diag_error(rec->loc, "'%s' is not a record of a file", rec->name);
        return -1;
    }
    return resolve_ref(ir, rec, f) ? v->file : -1;
}

// MOVE src TO the data item dest (an operand of a file clause or statement)
static void move_to_item(IrProgram *ir, AstNode *src, int dest) {
    char lit[32] = "";
    if (src->kind == AST_STRING_LITERAL || src->kind == AST_LITERAL_NUMBER)
        emit_data_lit(ir, "lit", src->name, lit, sizeof(lit));
    AstNode *d = item_ref(dest, src->loc);
    emit(ir, IR_SIZE_ERR_CLEAR);
    lower_move(ir, src, d, lit);
    ast_free(d);
}

// MOVE the data item src TO dest
static void move_from_item(IrProgram *ir, int src, AstNode *dest) {
    AstNode *s = item_ref(src, dest->loc);
    emit(ir, IR_SIZE_ERR_CLEAR);
    lower_move(ir, s, dest, "");
    ast_free(s);
}

static void move_int_to_item(IrProgram *ir, long long value, int dest, SourceLocation loc) {
    char num[32];
    snprintf(num, sizeof(num), "%lld", value);
    AstNode *n = ast_new(AST_LITERAL_NUMBER, num, loc);
    move_to_item(ir, n, dest);
    ast_free(n);
}

// The LINAGE values of the file into its LINAGE block (before OPEN and WRITE)
static void store_linage(IrProgram *ir, int f) {
    FileInfo *fi = &files[f];
    if (fi->linage_record < 0) return;
    AstNode *lin = fi->fd->loop;
    AstNode *ops[4] = { lin->on_error, lin->not_on_error, lin->extra, lin->loop };
    for (int k = 0; k < 4; k++)
        if (ops[k]) move_to_item(ir, ops[k], fi->lin_var[k]);
}

// Indexed file f: the key whose data item is item, or that starts where item
// starts and is at least as long (START and READ KEY IS a leading part);
// -1 if none
static int key_of_item(int f, AstNode *item, int *len) {
    VarInfo *v = find_item(item, true);
    if (!v) return -1;
    for (int k = 0; k < files[f].nkeys; k++)
        if (&vars[files[f].key_item[k]] == v) { *len = v->size; return k; }
    for (int k = 0; k < files[f].nkeys; k++) {
        VarInfo *kv = &vars[files[f].key_item[k]];
        if (kv->record == v->record && kv->offset == v->offset && v->size <= kv->size) {
            *len = v->size;
            return k;
        }
    }
    diag_error(item->loc, "'%s' is not a record key of file '%s'", v->name, files[f].name);
    return -1;
}

// ── SORT and MERGE ───────────────────────────────────────────────────────────

static int sort_counter = 0;

// Collating weights of ALPHABET name as a data label, or "0" for the native
// order ("$collate": the program collating sequence)
static void sort_weights(IrProgram *ir, AstNode *program_env, const char *alphabet, char *label, size_t n) {
    if (!alphabet) {
        snprintf(label, n, "%s", has_collation ? "$collate" : "0");
        return;
    }
    for (AstNode *e = program_env ? program_env->children : NULL; e; e = e->next) {
        if (e->kind != AST_SPECIAL || strcmp(e->name, "ALPHABET") != 0 || strcasecmp(e->text, alphabet) != 0)
            continue;
        if (!e->children) break;        // NATIVE, STANDARD-1: the native order
        snprintf(label, n, "$sortcoll_%d", sort_counter);
        IrInstr *t = emit(ir, IR_DATA_BYTES);
        t->var_name = strdup(label);
        t->size = 256;
        unsigned char *w = malloc(256);
        alphabet_weights(e, w);
        t->content = (char *)w;
        return;
    }
    snprintf(label, n, "0");
}

static AstNode *environment_division = NULL;

// The sort file of a SORT, MERGE, RELEASE or RETURN, or -1
static int sort_file(AstNode *ref, bool report) {
    for (int f = 0; f < num_files; f++)
        if (strcasecmp(files[f].name, ref->name) == 0) {
            if (files[f].is_sort) { files[f].referenced = true; return f; }
            break;
        }
    if (report) diag_error(ref->loc, "'%s' is not a sort file (SD)", ref->name);
    return -1;
}

static void emit_sort_op(IrProgram *ir, IrOpcode op, int f, const char *area) {
    IrInstr *i = emit_file_op(ir, op, f);
    if (area) i->lit_name = strdup(area);
}

static void lower_sort(IrProgram *ir, AstNode *node) {
    int sd = sort_file(node->extra, true);
    if (sd < 0) return;
    FileInfo *fi = &files[sd];
    int area_rec = vars[fi->first_record].record;
    char area[48];
    record_label(area, sizeof(area), area_rec, 0);

    // Key table: count, weights, then offset, length, usage, flags,
    // descending and numeric of each key
    char weights[48];
    sort_weights(ir, environment_division, node->text, weights, sizeof(weights));
    char table[4096], body[4000] = "";
    int nkeys = 0, bn = 0;
    for (AstNode *k = node->children; k; k = k->next) {
        VarInfo *kv = find_item(k, true);
        if (!kv) return;
        int top = (int)(kv - vars);
        while (vars[top].parent >= 0) top = vars[top].parent;
        if (vars[top].file != sd || item_dims(kv, (int[16]){0}, (int[16]){0}) > 0) {
            diag_error(k->loc, "Sort key '%s' must be an item of a record of '%s' (without OCCURS)", kv->name, fi->name);
            return;
        }
        bool numeric = kv->is_numeric && !kv->mask;
        bn += snprintf(body + bn, sizeof(body) - bn, " %d %d %d %d %d %d", kv->offset, kv->size,
                       numeric ? (int)kv->usage_type : 0, numeric ? var_flags(kv) : 0,
                       k->is_signed ? 1 : 0, numeric ? 1 : 0);
        nkeys++;
    }
    snprintf(table, sizeof(table), "%d %s%s", nkeys, weights, body);
    char label[32];
    snprintf(label, sizeof(label), "$sortkeys_%d", sort_counter++);
    IrInstr *q = new_instr(IR_DATA_QWORDS, label, NULL, table, 0, 0, 0, 0, USAGE_DISPLAY);
    append_instr(ir, q);
    emit_sort_op(ir, IR_SORT_INIT, sd, label);

    AstNode *using = NULL, *giving = NULL;
    for (AstNode *l = node->loop; l; l = l->next) {
        if (strcmp(l->name, "USING") == 0) using = l;
        else giving = l;
    }
    // Input: the USING files' records, or the INPUT PROCEDURE's RELEASEs
    if (using) {
        for (AstNode *fn = using->children; fn; fn = fn->next) {
            int u = find_file(fn);
            if (u < 0) continue;
            if (files[u].is_sort) {
                diag_error(fn->loc, "USING file '%s' is a sort file", fn->name);
                continue;
            }
            char loop[32], done[32], uarea[48];
            new_label(loop, sizeof(loop), "sort_in");
            new_label(done, sizeof(done), "sort_in_end");
            record_label(uarea, sizeof(uarea), vars[files[u].first_record].record, 0);
            emit_file_op(ir, IR_FILE_OPEN, u)->imm = 1;
            emit_label(ir, loop);
            IrInstr *r = emit_file_op(ir, IR_FILE_READ, u);
            r->lit_name = strdup(uarea);
            emit_io_jump(ir, 10, 99, done);
            AstNode *from = item_ref(files[u].largest_record, fn->loc);
            AstNode *to = item_ref(fi->largest_record, fn->loc);
            emit(ir, IR_SIZE_ERR_CLEAR);
            lower_move(ir, from, to, "");
            ast_free(from);
            ast_free(to);
            IrInstr *rel = emit_file_op(ir, IR_SORT_RELEASE, sd);
            rel->lit_name = strdup(area);
            rel->size = fi->max_size;
            rel->content = strdup(files[u].fcb_label);   // the length of the record read
            emit_jump(ir, IR_JUMP, loop);
            emit_label(ir, done);
            emit_file_op(ir, IR_FILE_CLOSE, u)->imm = 0;
        }
    } else if (node->on_error) {
        int first = resolve_proc(node->on_error);
        int last = node->on_error->loop ? resolve_proc(node->on_error->loop) : first;
        if (first < 0 || last < 0) return;
        IrInstr *p = emit(ir, IR_PERFORM);
        p->imm = first;
        p->proc_end = last;
        p->digits = 1;                  // (for the Debug module: SORT INPUT)
    }
    emit_sort_op(ir, IR_SORT_SORT, sd, NULL);
    // Output: to the GIVING files, or the OUTPUT PROCEDURE's RETURNs
    if (giving) {
        int outs[16], nout = 0;
        for (AstNode *fn = giving->children; fn && nout < 16; fn = fn->next) {
            int g = find_file(fn);
            if (g < 0) continue;
            outs[nout++] = g;
            emit_file_op(ir, IR_FILE_OPEN, g)->imm = 2;
        }
        char loop[32], done[32];
        new_label(loop, sizeof(loop), "sort_out");
        new_label(done, sizeof(done), "sort_out_end");
        emit_label(ir, loop);
        emit_sort_op(ir, IR_SORT_RETURN, sd, area);
        emit_io_jump(ir, 10, 99, done);
        for (int k = 0; k < nout; k++) {
            int g = outs[k];
            AstNode *from = item_ref(fi->largest_record, giving->loc);
            AstNode *to = item_ref(files[g].largest_record, giving->loc);
            emit(ir, IR_SIZE_ERR_CLEAR);
            lower_move(ir, from, to, "");
            ast_free(from);
            ast_free(to);
            AstNode *len = item_ref(files[g].reclen_var, giving->loc);
            move_from_item(ir, fi->reclen_var, len);
            ast_free(len);
            char garea[48];
            record_label(garea, sizeof(garea), vars[files[g].first_record].record, 0);
            IrInstr *w = emit_file_op(ir, IR_FILE_WRITE, g);
            w->lit_name = strdup(garea);
            w->size = files[g].max_size;
            w->flags = files[g].variable ? 2 : 0;
        }
        emit_jump(ir, IR_JUMP, loop);
        emit_label(ir, done);
        for (int k = 0; k < nout; k++) emit_file_op(ir, IR_FILE_CLOSE, outs[k])->imm = 0;
    } else if (node->not_on_error) {
        int first = resolve_proc(node->not_on_error);
        int last = node->not_on_error->loop ? resolve_proc(node->not_on_error->loop) : first;
        if (first < 0 || last < 0) return;
        IrInstr *p = emit(ir, IR_PERFORM);
        p->imm = first;
        p->proc_end = last;
        p->digits = strcmp(node->name, "MERGE") == 0 ? 3 : 2;   // SORT / MERGE OUTPUT
    }
    emit_sort_op(ir, IR_SORT_END, sd, NULL);
}

static void lower_release_return(IrProgram *ir, AstNode *node) {
    if (node->kind == AST_STATEMENT_RELEASE) {
        if (node->loop) {
            char lit[32] = {0};
            if (node->loop->kind == AST_STRING_LITERAL || node->loop->kind == AST_LITERAL_NUMBER)
                emit_data_lit(ir, "lit", node->loop->name, lit, sizeof(lit));
            emit(ir, IR_SIZE_ERR_CLEAR);
            lower_move(ir, node->loop, node->extra, lit);
        }
        VarInfo *v = find_item(node->extra, true);
        if (!v) return;
        int top = (int)(v - vars);
        if (v->parent >= 0 || v->file < 0 || !files[v->file].is_sort) {
            diag_error(node->extra->loc, "'%s' is not a record of a sort file", v->name);
            return;
        }
        FieldRef rf;
        if (!resolve_ref(ir, node->extra, &rf)) return;
        int sd = vars[top].file;
        IrInstr *rel = emit_file_op(ir, IR_SORT_RELEASE, sd);
        rel->lit_name = strdup(rf.label);
        rel->size = files[sd].variable ? rf.size : files[sd].max_size;
        AstNode *dep = files[sd].fd ? files[sd].fd->extra : NULL;
        if (dep) {
            // RECORD VARYING DEPENDING ON: the length from the item
            move_to_item(ir, dep, files[sd].reclen_var);
            rel->content = strdup(files[sd].fcb_label);
        }
        return;
    }
    int sd = sort_file(node->extra, true);
    if (sd < 0) return;
    char area[48];
    record_label(area, sizeof(area), vars[files[sd].first_record].record, 0);
    emit_sort_op(ir, IR_SORT_RETURN, sd, area);
    debug_file_event(ir, node, sd);
    AstNode *dep = files[sd].fd ? files[sd].fd->extra : NULL;
    if (dep || node->loop) {
        char skip[32];
        new_label(skip, sizeof(skip), "return_skip");
        emit_io_jump(ir, 10, 99, skip);
        if (dep) move_from_item(ir, files[sd].reclen_var, dep);
        if (node->loop) {
            AstNode *rec = item_ref(files[sd].largest_record, node->loc);
            emit(ir, IR_SIZE_ERR_CLEAR);
            lower_move(ir, rec, node->loop, "");
            ast_free(rec);
        }
        emit_label(ir, skip);
    }
    lower_io_phrases(ir, node, 10, 19, -1, 0);
}

static void debug_file_event(IrProgram *ir, AstNode *stmt, int f);

static bool random_or_dynamic(int f) {
    return files[f].select->access != FILE_ACCESS_SEQUENTIAL;
}

static void lower_file_statement(IrProgram *ir, AstNode *node) {
    switch (node->kind) {
        case AST_STATEMENT_OPEN:
        case AST_STATEMENT_CLOSE:
            for (AstNode *fn = node->children; fn; fn = fn->next) {
                int f = find_file(fn);
                if (f < 0) continue;
                bool open = node->kind == AST_STATEMENT_OPEN;
                if (open) store_linage(ir, f);
                IrInstr *i = emit_file_op(ir, open ? IR_FILE_OPEN : IR_FILE_CLOSE, f);
                i->imm = open ? fn->decimal_places | (fn->digits << 8) : fn->digits;
                debug_file_event(ir, node, f);
                lower_io_phrases(ir, &no_phrases, 1, 0, f, open ? (fn->decimal_places & 7) : 0);
            }
            break;
        case AST_STATEMENT_READ: {
            int f = find_file(node->extra);
            if (f < 0) return;
            FileInfo *fi = &files[f];
            int org = fi->select->org;
            bool keyed = (org == FILE_ORG_RELATIVE || org == FILE_ORG_INDEXED) &&
                         (fi->select->access == FILE_ACCESS_RANDOM ||
                          (fi->select->access == FILE_ACCESS_DYNAMIC && !node->is_numeric));
            AstNode *relkey = org == FILE_ORG_RELATIVE ? file_key(f, "RELATIVE") : NULL;
            if (keyed && relkey) move_to_item(ir, relkey, fi->key_var);
            if (keyed && org == FILE_ORG_INDEXED) {
                int len, k = 0;
                if (node->children) k = key_of_item(f, node->children, &len);
                if (k < 0) return;
                move_int_to_item(ir, k, fi->keyref_var, node->loc);
            }
            char area[48];
            record_label(area, sizeof(area), vars[fi->first_record].record, 0);
            IrInstr *r = emit_file_op(ir, IR_FILE_READ, f);
            r->lit_name = strdup(area);
            r->imm = keyed ? 1 : 0;
            debug_file_event(ir, node, f);
            // After a successful READ: the record number into the RELATIVE KEY
            // (sequential reading), the record length into the DEPENDING ON
            // item, the record into the INTO item
            AstNode *dep = fi->fd ? fi->fd->extra : NULL;
            if ((relkey && !keyed) || dep || node->loop) {
                char skip[32];
                new_label(skip, sizeof(skip), "read_skip");
                emit_io_jump(ir, 10, 99, skip);
                if (relkey && !keyed) move_from_item(ir, fi->key_var, relkey);
                if (dep) move_from_item(ir, fi->reclen_var, dep);
                if (node->loop) {
                    AstNode *rec = item_ref(fi->largest_record, node->loc);
                    emit(ir, IR_SIZE_ERR_CLEAR);
                    lower_move(ir, rec, node->loop, "");
                    ast_free(rec);
                }
                emit_label(ir, skip);
            }
            check_unhandled(node, f, !keyed);
            if (node->is_signed) lower_io_phrases(ir, node, 20, 29, f, 0);
            else lower_io_phrases(ir, node, 10, 19, f, 0);
            break;
        }
        case AST_STATEMENT_WRITE:
        case AST_STATEMENT_REWRITE: {
            if (node->loop) {
                // FROM: move the source to the record first
                char lit[32] = {0};
                if (node->loop->kind == AST_STRING_LITERAL || node->loop->kind == AST_LITERAL_NUMBER)
                    emit_data_lit(ir, "lit", node->loop->name, lit, sizeof(lit));
                emit(ir, IR_SIZE_ERR_CLEAR);
                lower_move(ir, node->loop, node->extra, lit);
            }
            FieldRef rf;
            int f = record_file(ir, node->extra, &rf);
            if (f < 0) return;
            FileInfo *fi = &files[f];
            int org = fi->select->org;
            AstNode *relkey = org == FILE_ORG_RELATIVE ? file_key(f, "RELATIVE") : NULL;
            if (relkey && random_or_dynamic(f)) move_to_item(ir, relkey, fi->key_var);
            AstNode *dep = fi->fd ? fi->fd->extra : NULL;
            if (dep) move_to_item(ir, dep, fi->reclen_var);
            bool is_write = node->kind == AST_STATEMENT_WRITE;
            if (!is_write) {
                if (debugging_mode) debug_after_statement(ir, node);    // (before the record is written)
                IrInstr *w = emit_file_op(ir, IR_FILE_REWRITE, f);
                w->lit_name = strdup(rf.label);
                w->size = fi->variable ? rf.size : fi->max_size;   // fixed: the whole area
                w->flags = dep ? 2 : 0;
                debug_file_event(ir, node, f);
                if (org == FILE_ORG_RELATIVE || org == FILE_ORG_INDEXED) check_unhandled(node, f, false);
                lower_io_phrases(ir, node, 20, 29, f, 0);
                break;
            }
            store_linage(ir, f);
            int mode = 0;                   // runtime ADV_* modes
            bool on_stack = false;
            long long lines = 0;
            if (node->name) {
                bool before = strcmp(node->name, "BEFORE") == 0;
                AstNode *count = node->children;
                if (count->kind == AST_BOOL_LITERAL) {
                    mode = before ? 3 : 4;  // PAGE
                } else {
                    mode = before ? 1 : 2;
                    if (!is_int_literal(count, &lines)) {
                        if (!num_expr_valid(count)) return;
                        push_int(ir, count);
                        on_stack = true;
                    }
                }
            }
            if (debugging_mode) debug_after_statement(ir, node);    // (before the record is written)
            IrInstr *w = emit_file_op(ir, IR_FILE_WRITE, f);
            w->lit_name = strdup(rf.label);
            w->size = fi->variable || fi->select->decimal_places == 1 || mode ? rf.size : fi->max_size;
            w->imm = mode;
            w->flags = (on_stack ? 1 : 0) | (dep ? 2 : 0);
            w->digits = (int)lines;
            debug_file_event(ir, node, f);
            if (relkey && !random_or_dynamic(f)) {
                char skip[32];
                new_label(skip, sizeof(skip), "write_skip");
                emit_io_jump(ir, 10, 99, skip);
                move_from_item(ir, fi->key_var, relkey);
                emit_label(ir, skip);
            }
            if ((org == FILE_ORG_RELATIVE || org == FILE_ORG_INDEXED) && !node->is_signed) {
                AstNode bare = *node;
                bare.on_error = NULL;
                check_unhandled(&bare, f, false);
            }
            if (node->is_signed || (!node->on_error && !node->not_on_error)) {
                lower_io_phrases(ir, node->is_signed ? node : &no_phrases, 20, 29, f, 0);
                break;
            }
            // [NOT] AT END-OF-PAGE: after a successful WRITE
            lower_io_phrases(ir, &no_phrases, 1, 0, f, 0);
            char eop[32], end[32];
            new_label(eop, sizeof(eop), "eop");
            new_label(end, sizeof(end), "eop_end");
            emit_io_jump(ir, 10, 99, end);
            IrInstr *j = emit_file_op(ir, IR_FILE_EOP_JUMP, f);
            j->jump_target = strdup(eop);
            lower_block(ir, node->not_on_error);
            emit_jump(ir, IR_JUMP, end);
            emit_label(ir, eop);
            lower_block(ir, node->on_error);
            emit_label(ir, end);
            break;
        }
        case AST_STATEMENT_DELETE:
        case AST_STATEMENT_START: {
            int f = find_file(node->extra);
            if (f < 0) return;
            FileInfo *fi = &files[f];
            int org = fi->select->org;
            if (org != FILE_ORG_RELATIVE && org != FILE_ORG_INDEXED) {
                diag_error(node->loc, "%s needs a RELATIVE or INDEXED file",
                                 node->kind == AST_STATEMENT_DELETE ? "DELETE" : "START");
                return;
            }
            AstNode *relkey = org == FILE_ORG_RELATIVE ? file_key(f, "RELATIVE") : NULL;
            bool start = node->kind == AST_STATEMENT_START;
            if (relkey && (start || random_or_dynamic(f))) move_to_item(ir, relkey, fi->key_var);
            if (start && org == FILE_ORG_RELATIVE && !relkey) {
                diag_error(node->loc, "START needs the RELATIVE KEY of file '%s'", fi->name);
                return;
            }
            IrInstr *op = emit_file_op(ir, start ? IR_FILE_START : IR_FILE_DELETE, f);
            debug_file_event(ir, node, f);
            if (start) {
                op->imm = strcmp(node->text, ">") == 0 ? 1 : strcmp(node->text, "=") == 0 ? 0 : 2;
                op->digits = 0;
                op->size = org == FILE_ORG_INDEXED && fi->nkeys > 0 ? vars[fi->key_item[0]].size : 0;
                if (org == FILE_ORG_INDEXED && node->children) {
                    int len, k = key_of_item(f, node->children, &len);
                    if (k < 0) return;
                    op->digits = k;
                    op->size = len;
                }
                if (relkey) {
                    // The record number found becomes the RELATIVE KEY
                    char skip[32];
                    new_label(skip, sizeof(skip), "start_skip");
                    emit_io_jump(ir, 10, 99, skip);
                    move_from_item(ir, fi->key_var, relkey);
                    emit_label(ir, skip);
                }
            }
            // DELETE in sequential access has no INVALID KEY phrase
            if (start || fi->select->access != FILE_ACCESS_SEQUENTIAL) check_unhandled(node, f, false);
            lower_io_phrases(ir, node, 20, 29, f, 0);
            break;
        }
        default:
            break;
    }
}

// ── Intrinsic function evaluation ───────────────────────────────────────────

static bool arg_is_alnum(const AstNode *a) {
    if (a->kind == AST_STRING_LITERAL) return true;
    if (a->kind == AST_FIGURATIVE_CONSTANT) return figurative_char(a) != '0';
    if (a->kind != AST_IDENTIFIER) return false;
    VarInfo *v = find_item(a, false);
    return v && (!v->is_numeric || v->is_group || (a->loop != NULL));
}

static bool contains_div(const AstNode *e) {
    if (!e) return false;
    if (e->kind == AST_EXPR_DIV || e->kind == AST_EXPR_POW) return true;
    if (e->kind == AST_IDENTIFIER || e->kind == AST_LITERAL_NUMBER) return false;
    for (const AstNode *c = e->children; c; c = c->next)
        if (contains_div(c)) return true;
    return false;
}

// The scale a numeric argument is evaluated at
static int arg_scale(AstNode *a) {
    int s = max_leaf_scale(a);
    if (a->kind != AST_IDENTIFIER && a->kind != AST_LITERAL_NUMBER && s > 9) s = 9;
    if (contains_div(a) && s < 9) s = 9;
    return s > 17 ? 17 : s;
}

static int arg_length(const AstNode *a) {
    if (a->kind == AST_STRING_LITERAL) return (int)strlen(a->name);
    if (a->kind == AST_FIGURATIVE_CONSTANT) return 1;
    VarInfo *v = find_item(a, false);
    if (!v) return 1;
    if (a->loop) {
        AstNode *start = a->loop->children, *len = start ? start->next : NULL;
        long long sv, lv;
        if (len && is_int_literal(len, &lv)) return (int)lv;
        if (start && is_int_literal(start, &sv)) return v->size - (int)sv + 1;
    }
    return v->size;
}

// The arguments of function reference f, an identifier with ALL subscripts
// standing for each of its occurrences: the argument nodes themselves (a
// function's temporary belongs to its node), and copies for ALL, which
// free_args frees
static AstNode *expanded_copies[4096];
static int num_expanded_copies = 0;

static void free_args(void) {
    for (int k = 0; k < num_expanded_copies; k++) ast_free(expanded_copies[k]);
    num_expanded_copies = 0;
}

static void expand_all_args(const AstNode *f, AstNode **out, int *n, int max) {
    for (AstNode *a = f->on_error; a && *n < max; a = a->next) {
        bool all = false;
        if (a->kind == AST_IDENTIFIER)
            for (const AstNode *s = a->children; s; s = s->next)
                if (s->kind == AST_SPECIAL) all = true;
        if (!all) {
            out[(*n)++] = a;
            continue;
        }
        VarInfo *v = find_item(a, true);
        if (!v) continue;
        int counts[16], strides[16], subs[16];
        int nd = item_dims(v, counts, strides);
        int k = 0;
        bool is_all[16] = {false};
        long long fixed[16] = {0};
        for (const AstNode *s = a->children; s && k < nd; s = s->next, k++) {
            if (s->kind == AST_SPECIAL) is_all[k] = true;
            else if (!is_int_literal(s, &fixed[k])) {
                diag_error(s->loc, "Only literal subscripts go with ALL subscripts");
                return;
            }
        }
        for (int d = 0; d < nd; d++) subs[d] = is_all[d] ? 1 : (int)fixed[d];
        for (;;) {
            if (*n >= max) return;
            AstNode *c = ast_clone(a);
            c->next = NULL;
            ast_free(c->children);
            c->children = NULL;
            AstNode *tail = NULL;
            for (int d = 0; d < nd; d++) {
                char num[16];
                snprintf(num, sizeof(num), "%d", subs[d]);
                AstNode *lit = ast_new(AST_LITERAL_NUMBER, num, a->loc);
                if (!c->children) c->children = lit;
                else tail->next = lit;
                tail = lit;
            }
            out[(*n)++] = c;
            if (num_expanded_copies < 4096) expanded_copies[num_expanded_copies++] = c;
            int d = nd - 1;
            while (d >= 0 && (!is_all[d] || ++subs[d] > counts[d])) {
                if (is_all[d]) subs[d] = 1;
                d--;
            }
            if (d < 0) break;
        }
    }
}

// Evaluate function reference f into its temporary item
static bool eval_function(IrProgram *ir, AstNode *f) {
    const FuncInfo *fi = function_info(f->name + 1);
    int t = function_temp(f);
    if (!fi) return false;
    AstNode *args[1024];
    int nargs = 0;
    expand_all_args(f, args, &nargs, 1024);
    bool ok = true;
    if (nargs < fi->min_args || nargs > fi->max_args) {
        diag_error(f->loc, "FUNCTION %s takes %d to %d arguments, %d given",
                         fi->name, fi->min_args, fi->max_args, nargs);
        ok = false;
    }
    AstNode *tref = item_ref(t, f->loc);
    FieldRef tf;
    if (ok) {
        tf.var = &vars[t];
        record_label(tf.label, sizeof(tf.label), vars[t].record, 0);
        tf.size = vars[t].size;
        tf.refmod = false;
    }
    if (ok && fi->id == 14) {
        // LENGTH: known when compiling
        FieldRef af;
        int len = args[0]->kind == AST_IDENTIFIER && args[0]->name[0] != '!' ? 0 : arg_length(args[0]);
        if (args[0]->kind == AST_IDENTIFIER && resolve_ref(ir, args[0], &af)) len = af.size;
        if (len < 0) {
            diag_error(f->loc, "FUNCTION LENGTH of an item of variable length is not supported yet");
            ok = false;
        } else {
            move_int_to_item(ir, len, t, f->loc);
        }
    } else if (ok && fi->id == 41) {
        // WHEN-COMPILED: the time of this compilation
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char text[64], lit[32];
        snprintf(text, sizeof(text), "%04d%02d%02d%02d%02d%02d0000000",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
        emit_data_lit(ir, "lit", text, lit, sizeof(lit));
        emit_move_alpha(ir, &tf, lit, 21);
    } else if (ok) {
        // The arguments into temporaries (functions among them evaluated
        // first), then the argument table, then the call
        char labels[1024][64];
        int sizes[1024], scales[1024];
        for (int k = 0; k < nargs && ok; k++) {
            bool alnum = arg_is_alnum(args[k]);
            if ((fi->args == 'n' && alnum) || (fi->args == 'a' && !alnum && fi->id != 26)) {
                // (ORD of a numeric DISPLAY item: its character)
                if (!(fi->args == 'a' && args[k]->kind == AST_IDENTIFIER)) {
                    diag_error(args[k]->loc, "Argument %d of FUNCTION %s must be %s", k + 1, fi->name,
                                     fi->args == 'n' ? "numeric" : "alphanumeric");
                    ok = false;
                    break;
                }
                alnum = true;
            }
            if (fi->args == 'a' || fi->id == 26) alnum = true;
            if (alnum) {
                FieldRef af;
                if (args[k]->kind == AST_STRING_LITERAL || args[k]->kind == AST_FIGURATIVE_CONSTANT) {
                    char lit[32];
                    const char *text = args[k]->kind == AST_STRING_LITERAL ? args[k]->name : " ";
                    if (args[k]->kind == AST_FIGURATIVE_CONSTANT) {
                        static char one[2];
                        one[0] = (char)figurative_char(args[k]);
                        one[1] = '\0';
                        text = one;
                    }
                    emit_data_lit(ir, "lit", text, lit, sizeof(lit));
                    snprintf(labels[k], sizeof(labels[k]), "%s", lit);
                    sizes[k] = (int)strlen(text);
                } else if (resolve_ref(ir, args[k], &af)) {
                    snprintf(labels[k], sizeof(labels[k]), "%s", af.label);
                    sizes[k] = af.size;
                } else {
                    ok = false;
                }
                scales[k] = -1;
            } else {
                if (!num_expr_valid(args[k])) { ok = false; break; }
                int s = arg_scale(args[k]);
                int at = temp_numeric(s, f);
                AstNode *aref = item_ref(at, f->loc);
                lower_assign(ir, args[k], aref, false);
                ast_free(aref);
                record_label(labels[k], sizeof(labels[k]), vars[at].record, 0);
                sizes[k] = 8;
                scales[k] = s;
            }
        }
        for (int k = 0; k < nargs && ok; k++) {
            IrInstr *a = emit(ir, IR_FUNC_ARG);
            a->imm = k;
            a->var_name = strdup(labels[k]);
            a->size = sizes[k];
            a->digits = scales[k];
            a->flags = scales[k] < 0 ? 1 : 0;
        }
        if (ok) {
            IrInstr *c = emit(ir, IR_FUNC_CALL);
            c->imm = fi->id | (decimal_comma && (fi->id == 24 || fi->id == 25) ? 0x100 : 0);
            c->size = nargs;
            c->digits = vars[t].is_numeric ? vars[t].decimal_places : 0;
            c->var_name = strdup(tf.label);
            c->lit_len = vars[t].size;
        }
    }
    ast_free(tref);
    return ok;
}

// ── CALL and CANCEL ─────────────────────────────────────────────────────────

// The name operand of a CALL or CANCEL into an IR_CALL_PROGRAM-like instruction
static bool program_name_operand(IrProgram *ir, AstNode *name, IrInstr *op) {
    if (name->kind == AST_STRING_LITERAL) {
        char lit[32];
        emit_data_lit(ir, "pname", name->name, lit, sizeof(lit));
        op->lit_name = strdup(lit);
        op->lit_len = (int)strlen(name->name);
        return true;
    }
    FieldRef f;
    if (!resolve_ref(ir, name, &f)) return false;
    if (f.var && f.var->is_numeric) {
        diag_error(name->loc, "The program name '%s' must be alphanumeric", name->name);
        return false;
    }
    op->var_name = strdup(f.label);
    op->size = f.size;
    return true;
}

static void lower_call(IrProgram *ir, AstNode *node) {
    if (node->kind == AST_STATEMENT_CANCEL) {
        for (AstNode *n = node->children; n; n = n->next) {
            IrInstr *c = new_instr(IR_CANCEL_PROGRAM, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY);
            if (!program_name_operand(ir, n, c)) { free(c); continue; }
            append_instr(ir, c);
        }
        return;
    }
    // The arguments: BY REFERENCE their addresses, BY CONTENT a copy's
    int argc = 0;
    for (AstNode *a = node->children; a; a = a->next, argc++) {
        FieldRef f;
        if (!resolve_ref(ir, a, &f)) return;
        if (f.size < 0) f.size = f.var->size;      // BY CONTENT: the longest it can be
        char label[64];
        snprintf(label, sizeof(label), "%s", f.label);
        if (a->is_signed) {
            int r = num_records++;
            record_size[r] = f.size;
            char rec[32];
            snprintf(rec, sizeof(rec), "%%r%d", r);
            append_instr(ir, new_instr(IR_DATA_VAR, rec, NULL, NULL, 0, f.size > 0 ? f.size : 1, 0, 0, USAGE_DISPLAY));
            record_label(label, sizeof(label), r, 0);
            FieldRef dest = { .var = f.var, .size = f.size };
            snprintf(dest.label, sizeof(dest.label), "%s", label);
            emit_move_alpha(ir, &dest, f.label, f.size);
        }
        IrInstr *arg = emit(ir, IR_CALL_ARG);
        arg->imm = argc;
        arg->var_name = strdup(label);
    }
    if (argc > 64) {
        diag_error(node->loc, "CALL with more than 64 arguments");
        return;
    }
    IrInstr *call = new_instr(IR_CALL_PROGRAM, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY);
    if (!program_name_operand(ir, node->extra, call)) { free(call); return; }
    call->imm = argc;
    call->flags = node->on_error ? 1 : 0;
    append_instr(ir, call);
    if (node->on_error || node->not_on_error) {
        char exc[32], end[32];
        new_label(exc, sizeof(exc), "call_exception");
        new_label(end, sizeof(end), "call_end");
        emit_io_jump(ir, 1, 1, exc);
        lower_block(ir, node->not_on_error);
        emit_jump(ir, IR_JUMP, end);
        emit_label(ir, exc);
        lower_block(ir, node->on_error);
        emit_label(ir, end);
    }
}

// ── INSPECT ──────────────────────────────────────────────────────────────────

static int inspect_counter = 0;

static void emit_qword(IrProgram *ir, const char *dest, int mode, long long imm,
                       const char *src, int size) {
    IrInstr *q = emit(ir, IR_STORE_QWORD);
    q->var_name = strdup(dest);
    q->flags = mode;
    q->imm = imm;
    if (src) q->lit_name = strdup(src);
    q->size = size;
}

// INSPECT operand as an address and a length. A figurative constant becomes
// fill_len copies of its character (or of its ALL literal).
static bool inspect_operand(IrProgram *ir, AstNode *op, char *label, size_t lsz,
                            int *len, int fill_len) {
    if (op->kind == AST_IDENTIFIER) {
        FieldRef f;
        if (!resolve_ref(ir, op, &f)) return false;
        snprintf(label, lsz, "%s", f.label);
        *len = f.size;
        return true;
    }
    int fig = figurative_char(op);
    if (fig != -1) {
        // A figurative constant: fill_len characters (one when 0), as bytes
        // since LOW-VALUE is a NUL
        const char *pattern = fig == FIG_PATTERN ? op->text : NULL;
        int plen = pattern ? (int)strlen(pattern) : 1;
        int n = fill_len > 0 ? fill_len : plen;
        char *bytes = malloc((size_t)n);
        if (!bytes) return false;
        for (int i = 0; i < n; i++) bytes[i] = pattern ? pattern[i % plen] : (char)fig;
        new_label(label, lsz, "insplit");
        IrInstr *t = emit(ir, IR_DATA_BYTES);
        t->var_name = strdup(label);
        t->size = n;
        t->content = bytes;
        *len = n;
        return true;
    }
    emit_data_lit(ir, "insplit", op->name, label, lsz);
    *len = (int)strlen(op->name);
    return true;
}

static void lower_inspect_pass(IrProgram *ir, AstNode *node, bool tallying);

// TALLYING and REPLACING together act as two INSPECT statements: all of the
// TALLYING first, then the REPLACING
static void lower_inspect(IrProgram *ir, AstNode *node) {
    bool has_tally = false, has_replace = false;
    for (AstNode *item = node->children; item; item = item->next) {
        if (item->loop) has_tally = true;
        else has_replace = true;
    }

    // A signed numeric item is inspected as if it were unsigned: its digits
    // are inspected without the overpunched sign, which is put back after
    VarInfo *sv = find_item(node->extra, false);
    bool unsign = sv && sv->is_numeric && sv->is_signed && sv->usage_type == USAGE_DISPLAY &&
                  !node->extra->loop && !(var_flags(sv) & IR_NUM_SIGN_SEPARATE);
    FieldRef f;
    if (unsign) {
        if (!resolve_ref(ir, node->extra, &f)) return;
        emit_load(ir, &f);                  // the value, kept for its sign
        emit(ir, IR_NUM_DUP);
        IrInstr *st = emit(ir, IR_NUM_STORE);   // absolute value, plain digits
        st->var_name = strdup(f.label);
        st->size = sv->size;
        st->digits = sv->digits;
        st->usage_type = USAGE_DISPLAY;
        st->flags = 0;
    }

    if (has_tally) lower_inspect_pass(ir, node, true);
    if (has_replace) lower_inspect_pass(ir, node, false);

    if (unsign) {
        char pos_lbl[32], end_lbl[32];
        new_label(pos_lbl, sizeof(pos_lbl), "insp_pos");
        new_label(end_lbl, sizeof(end_lbl), "insp_end");
        if (!resolve_ref(ir, node->extra, &f)) return;
        FieldRef plain = f;
        static VarInfo unsigned_view;
        unsigned_view = *sv;
        unsigned_view.is_signed = false;
        plain.var = &unsigned_view;
        emit(ir, IR_NUM_DUP);
        emit_num(ir, IR_NUM_CONST, 0, 0);
        IrInstr *cmp = emit(ir, IR_NUM_CMP_JUMP_IF_FALSE);
        cmp->cmp_op = strdup("<");
        cmp->jump_target = strdup(pos_lbl);
        emit_load(ir, &plain);
        emit_num(ir, IR_NUM_NEG, 0, 0);
        emit_store(ir, &f, false);
        emit_jump(ir, IR_JUMP, end_lbl);
        emit_label(ir, pos_lbl);
        emit_load(ir, &plain);
        emit_store(ir, &f, false);
        emit_label(ir, end_lbl);
        emit(ir, IR_NUM_DROP);
    }
}

static void lower_inspect_pass(IrProgram *ir, AstNode *node, bool tallying) {
    FieldRef subject;
    if (!resolve_ref(ir, node->extra, &subject)) return;
    if (subject.var->is_numeric && subject.var->usage_type != USAGE_DISPLAY && !subject.refmod) {
        diag_error(node->loc, "INSPECT requires a DISPLAY item");
        return;
    }

    int id = inspect_counter++;
    char table[32], counts[32];
    snprintf(table, sizeof(table), "$insp_%d", id);
    snprintf(counts, sizeof(counts), "$inspc_%d", id);

    // Entries: one per operand, one per character of a CONVERTING operand
    int n = 0;
    AstNode *tallied[256];
    char entry[64];
    for (AstNode *item = node->children; item; item = item->next) {
        if ((item->loop != NULL) != tallying) continue;
        bool convert = strcmp(item->name, "CONVERT") == 0;
        bool chars = strcmp(item->name, "CHARACTERS") == 0;
        int kind = chars ? 0 : strcmp(item->name, "LEADING") == 0 ? 2 :
                   strcmp(item->name, "FIRST") == 0 ? 3 : 1;
        char pat[64] = "", repl[64] = "", before[64] = "", after[64] = "";
        int plen = 1, rlen = 0, blen = 0, alen = 0;
        if (item->extra && !inspect_operand(ir, item->extra, pat, sizeof(pat), &plen, 0)) return;
        if (plen < 0 && (item->on_error || convert)) {
            diag_error(item->loc, "INSPECT operands of variable length are not supported yet");
            return;
        }
        if (item->on_error &&
            !inspect_operand(ir, item->on_error, repl, sizeof(repl), &rlen, plen)) return;
        if (item->not_on_error && !inspect_operand(ir, item->not_on_error, before, sizeof(before), &blen, 0)) return;
        if (item->children && !inspect_operand(ir, item->children, after, sizeof(after), &alen, 0)) return;
        if (item->on_error && rlen != plen) {
            diag_error(item->loc, "INSPECT replacement must be as long as what it replaces");
            return;
        }

        int count = convert ? plen : 1;
        for (int k = 0; k < count && n < 256; k++, n++) {
            int base = n * 96;
            #define FIELD(off) (snprintf(entry, sizeof(entry), "%s+%d", table, base + (off)), entry)
            char p_k[80], r_k[80];
            snprintf(p_k, sizeof(p_k), "%s+%d", pat, convert ? k : 0);
            snprintf(r_k, sizeof(r_k), "%s+%d", repl, convert ? k : 0);
            emit_qword(ir, FIELD(0), 0, convert ? 1 : kind, NULL, 0);
            emit_qword(ir, FIELD(8), 0, item->loop ? n : -1, NULL, 0);
            if (pat[0]) emit_qword(ir, FIELD(16), 1, 0, p_k, 0);
            emit_qword(ir, FIELD(24), convert ? 0 : 2, 1, NULL, plen);
            if (repl[0]) emit_qword(ir, FIELD(32), 1, 0, r_k, 0);
            if (before[0]) {
                emit_qword(ir, FIELD(40), 1, 0, before, 0);
                emit_qword(ir, FIELD(48), 2, 0, NULL, blen);
            } else {
                emit_qword(ir, FIELD(40), 0, 0, NULL, 0);
            }
            if (after[0]) {
                emit_qword(ir, FIELD(56), 1, 0, after, 0);
                emit_qword(ir, FIELD(64), 2, 0, NULL, alen);
            } else {
                emit_qword(ir, FIELD(56), 0, 0, NULL, 0);
            }
            snprintf(entry, sizeof(entry), "%s+%d", counts, n * 8);
            emit_qword(ir, entry, 0, 0, NULL, 0);
            tallied[n] = item->loop;
            #undef FIELD
        }
    }
    IrInstr *t = emit(ir, IR_DATA_BSS);
    t->var_name = strdup(table);
    t->size = n * 96;
    IrInstr *c = emit(ir, IR_DATA_BSS);
    c->var_name = strdup(counts);
    c->size = n * 8;

    IrInstr *call = emit(ir, IR_CALL_INSPECT);
    call->var_name = strdup(subject.label);
    call->size = subject.size;
    call->lit_name = strdup(table);
    call->imm = n;
    call->content = strdup(counts);

    // TALLYING: add each operand's count to its counter
    static VarInfo count_var;
    count_var.size = 8;
    count_var.usage_type = USAGE_COMP5;
    count_var.is_numeric = count_var.is_signed = true;
    count_var.digits = 18;
    for (int e = 0; e < n; e++) {
        if (!tallied[e]) continue;
        VarInfo *cv = numeric_item(tallied[e], "TALLYING counter");
        if (!cv) return;
        emit(ir, IR_SIZE_ERR_CLEAR);
        FieldRef cnt = { .var = &count_var, .size = 8 };
        snprintf(cnt.label, sizeof(cnt.label), "%s+%d", counts, e * 8);
        emit_load(ir, &cnt);
        emit_rescale(ir, cv->decimal_places, false);
        FieldRef f;
        if (!resolve_ref(ir, tallied[e], &f)) return;
        emit_load(ir, &f);
        emit_num(ir, IR_NUM_ADD, 0, 0);
        if (!resolve_ref(ir, tallied[e], &f)) return;
        emit_store(ir, &f, false);
    }
}

// A COMP-5 qword of a runtime state block, as a numeric operand
static FieldRef state_qword(const char *state, int offset) {
    static VarInfo qword_var;
    qword_var.size = 8;
    qword_var.usage_type = USAGE_COMP5;
    qword_var.is_numeric = qword_var.is_signed = true;
    qword_var.digits = 18;
    FieldRef f = { .var = &qword_var, .size = 8 };
    snprintf(f.label, sizeof(f.label), "%s+%d", state, offset);
    return f;
}

// Store the value on the stack (an integer) into a numeric item
static void store_integer(IrProgram *ir, AstNode *item) {
    VarInfo *v = find_item(item, true);
    if (!v) return;
    emit_rescale(ir, v->decimal_places, false);
    FieldRef f;
    if (resolve_ref(ir, item, &f)) emit_store(ir, &f, false);
}

// The receiving item of STRING / UNSTRING data: alphanumeric
static bool check_text_receiver(AstNode *ref, const char *role) {
    VarInfo *v = find_item(ref, true);
    if (!v) return false;
    if (!ref->loop && (v->is_numeric || v->mask || v->is_cond)) {
        diag_error(ref->loc, "%s '%s' must be an alphanumeric item", role, ref->name);
        return false;
    }
    return true;
}

// STRING: a table of (source, delimiter) entries for runtime_string
// (runtime/ABI.md section 11)
static void lower_string(IrProgram *ir, AstNode *node) {
    int id = inspect_counter++;
    char table[32], entry[64];
    snprintf(table, sizeof(table), "$str_%d", id);
    emit(ir, IR_SIZE_ERR_CLEAR);
    if (!check_text_receiver(node->extra, "STRING receiving item")) return;
    if (node->loop && !numeric_item(node->loop, "STRING pointer")) return;
    int n = 0;
    for (AstNode *src = node->children; src; src = src->next, n++) {
        char label[64], delim[64] = "";
        int len, dlen = 0;
        if (src->kind == AST_IDENTIFIER) {
            VarInfo *sv = find_item(src, true);
            if (!sv) return;
            if (sv->is_numeric && !src->loop && (sv->usage_type != USAGE_DISPLAY || sv->decimal_places != 0)) {
                diag_error(src->loc, "A numeric STRING operand must be an integer DISPLAY item ('%s')", src->name);
                return;
            }
        }
        if (!inspect_operand(ir, src, label, sizeof(label), &len, 0)) return;
        if (src->on_error && !inspect_operand(ir, src->on_error, delim, sizeof(delim), &dlen, 0)) return;
        #define FIELD(off) (snprintf(entry, sizeof(entry), "%s+%d", table, n * 32 + (off)), entry)
        emit_qword(ir, FIELD(0), 1, 0, label, 0);
        emit_qword(ir, FIELD(8), 2, 0, NULL, len);
        if (delim[0]) {
            emit_qword(ir, FIELD(16), 1, 0, delim, 0);
            emit_qword(ir, FIELD(24), 2, 0, NULL, dlen);
        } else {
            emit_qword(ir, FIELD(16), 0, 0, NULL, 0);
        }
        #undef FIELD
    }
    IrInstr *t = emit(ir, IR_DATA_BSS);
    t->var_name = strdup(table);
    t->size = n * 32;

    if (node->loop) push_int(ir, node->loop);
    else emit_num(ir, IR_NUM_CONST, 1, 0);
    FieldRef target;
    if (!resolve_ref(ir, node->extra, &target)) return;
    IrInstr *call = emit(ir, IR_CALL_STRING);
    call->var_name = strdup(target.label);
    call->size = target.size;
    call->lit_name = strdup(table);
    call->imm = n;
    if (node->loop) store_integer(ir, node->loop);
    else emit(ir, IR_NUM_DROP);
    lower_size_error_phrases(ir, node);
}

// UNSTRING: a state block for runtime_unstring_start / runtime_unstring_next;
// each field found is moved to its receiver here, through an address and
// length slot, so the receiver's MOVE rules apply
static void lower_unstring(IrProgram *ir, AstNode *node) {
    AstNode *spec = node->extra;
    int id = inspect_counter++;
    char state[32], delims[32], entry[64];
    snprintf(state, sizeof(state), "$unst_%d", id);
    snprintf(delims, sizeof(delims), "$unsd_%d", id);
    emit(ir, IR_SIZE_ERR_CLEAR);
    if (!check_text_receiver(spec->extra, "UNSTRING sending item")) return;
    if (spec->loop && !numeric_item(spec->loop, "UNSTRING pointer")) return;
    if (spec->on_error && !numeric_item(spec->on_error, "UNSTRING TALLYING item")) return;

    FieldRef src;
    if (!resolve_ref(ir, spec->extra, &src)) return;
    #define AT(base, off) (snprintf(entry, sizeof(entry), "%s+%d", base, off), entry)
    emit_qword(ir, AT(state, 0), 1, 0, src.label, 0);
    emit_qword(ir, AT(state, 8), 2, 0, NULL, src.size);
    int nd = 0;
    for (AstNode *d = spec->children; d; d = d->next, nd++) {
        char label[64];
        int len;
        if (!inspect_operand(ir, d, label, sizeof(label), &len, 0)) return;
        emit_qword(ir, AT(delims, nd * 24), 1, 0, label, 0);
        emit_qword(ir, AT(delims, nd * 24 + 8), 2, 0, NULL, len);
        emit_qword(ir, AT(delims, nd * 24 + 16), 0, d->is_signed ? 1 : 0, NULL, 0);
    }
    if (nd) emit_qword(ir, AT(state, 16), 1, 0, delims, 0);
    emit_qword(ir, AT(state, 24), 0, nd, NULL, 0);
    #undef AT
    IrInstr *t = emit(ir, IR_DATA_BSS);
    t->var_name = strdup(state);
    t->size = 80;
    if (nd) {
        t = emit(ir, IR_DATA_BSS);
        t->var_name = strdup(delims);
        t->size = nd * 24;
    }

    char end_lbl[32], skip_lbl[32];
    new_label(end_lbl, sizeof(end_lbl), "unstring_end");
    new_label(skip_lbl, sizeof(skip_lbl), "unstring_skip");
    if (spec->loop) push_int(ir, spec->loop);
    else emit_num(ir, IR_NUM_CONST, 1, 0);
    IrInstr *start = emit(ir, IR_CALL_UNSTRING_START);
    start->var_name = strdup(state);
    start->jump_target = strdup(skip_lbl);

    static VarInfo digits_view;
    for (AstNode *r = node->children; r; r = r->next) {
        VarInfo *rv = find_item(r, true);
        if (!rv) return;
        bool numeric = rv->is_numeric && !r->loop;
        if (!numeric && !check_text_receiver(r, "UNSTRING receiving item")) return;
        if (numeric && rv->usage_type != USAGE_DISPLAY) {
            diag_error(r->loc, "A numeric UNSTRING receiving item must be DISPLAY ('%s')", r->name);
            return;
        }
        IrInstr *next = emit(ir, IR_CALL_UNSTRING_NEXT);
        next->var_name = strdup(state);
        next->size = numeric ? rv->digits : rv->size;
        next->jump_target = strdup(end_lbl);

        // The field, moved as an alphanumeric item
        int slot = addr_slots++;
        IrInstr *sl = emit(ir, IR_UNSTRING_SLOTS);
        sl->var_name = strdup(state);
        sl->imm = slot;
        char field[32];
        snprintf(field, sizeof(field), "*A_%d", slot);
        FieldRef dest;
        if (!resolve_ref(ir, r, &dest)) return;
        if (numeric) {
            // as an unsigned integer
            memset(&digits_view, 0, sizeof(digits_view));
            digits_view.usage_type = USAGE_DISPLAY;
            digits_view.is_numeric = true;
            digits_view.size = -(slot + 1);
            digits_view.digits = 18;
            FieldRef ff = { .var = &digits_view, .size = -(slot + 1) };
            snprintf(ff.label, sizeof(ff.label), "%s", field);
            emit_load(ir, &ff);
            move_rescale(ir, rv, 0);
            emit_store(ir, &dest, false);
        } else {
            emit_move_alpha(ir, &dest, field, -(slot + 1));
        }

        if (r->on_error) {              // DELIMITER IN: the delimiter, or spaces
            if (!check_text_receiver(r->on_error, "DELIMITER IN item")) return;
            int dslot = addr_slots++;
            IrInstr *ds = emit(ir, IR_UNSTRING_SLOTS);
            ds->var_name = strdup(state);
            ds->imm = dslot;
            ds->flags = 1;
            char dfield[32];
            snprintf(dfield, sizeof(dfield), "*A_%d", dslot);
            FieldRef dd;
            if (!resolve_ref(ir, r->on_error, &dd)) return;
            emit_move_alpha(ir, &dd, dfield, -(dslot + 1));
        }
        if (r->not_on_error) {          // COUNT IN: the field length
            if (!numeric_item(r->not_on_error, "COUNT IN item")) return;
            FieldRef len = state_qword(state, 48);
            emit_load(ir, &len);
            store_integer(ir, r->not_on_error);
        }
        if (spec->on_error) {           // TALLYING IN: one more receiver
            FieldRef tf;
            if (!resolve_ref(ir, spec->on_error, &tf)) return;
            VarInfo *tv = tf.var;
            emit_load(ir, &tf);
            emit_num(ir, IR_NUM_CONST, 1, 0);
            emit_rescale(ir, tv->decimal_places, false);
            emit_num(ir, IR_NUM_ADD, 0, 0);
            if (resolve_ref(ir, spec->on_error, &tf)) emit_store(ir, &tf, false);
        }
    }
    emit(ir, IR_UNSTRING_END)->var_name = strdup(state);
    emit_label(ir, end_lbl);
    if (spec->loop) {
        FieldRef pos = state_qword(state, 32);
        emit_load(ir, &pos);
        emit_num(ir, IR_NUM_CONST, 1, 0);
        emit_num(ir, IR_NUM_ADD, 0, 0);
        store_integer(ir, spec->loop);
    }
    emit_label(ir, skip_lbl);
    emit(ir, IR_UNSTRING_OVERFLOW)->var_name = strdup(state);
    lower_size_error_phrases(ir, node);
}

// Digit positions DISPLAY shows for a numeric item: its PICTURE digits, or
// for a COMP item limited only by its bytes the digits those bytes can hold
static int display_digits(const VarInfo *v) {
    if (v->usage_type == USAGE_COMP && !dialect->binary_truncate) {
        static const int capacity[] = { 0, 3, 5, 8, 10, 13, 15, 17, 18 };
        if (v->size >= 1 && v->size <= 8 && capacity[v->size] > v->digits) return capacity[v->size];
    }
    return v->digits;
}

static void lower_display(IrProgram *ir, AstNode *node) {
    for (AstNode *arg = node->children; arg; arg = arg->next) {
        const char *content = arg->name ? arg->name : "";
        if (arg->kind == AST_STRING_LITERAL || arg->kind == AST_LITERAL_NUMBER) {
            char lit_name[32];
            emit_data_lit(ir, "lit", content, lit_name, sizeof(lit_name));
            append_instr(ir, new_instr(IR_CALL_DISPLAY_STR, NULL, lit_name, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
        } else if (arg->kind == AST_FIGURATIVE_CONSTANT && figurative_char(arg) == FIG_PATTERN) {
            char lit_name[32];
            emit_data_lit(ir, "lit", arg->text, lit_name, sizeof(lit_name));
            append_instr(ir, new_instr(IR_CALL_DISPLAY_STR, NULL, lit_name, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
        } else if (arg->kind == AST_FIGURATIVE_CONSTANT) {
            char pad_char = (char)figurative_char(arg);
            append_instr(ir, new_instr(IR_CALL_DISPLAY_FILL, NULL, NULL, NULL, 0, 1, 0, pad_char, USAGE_DISPLAY));
        } else if (arg->kind == AST_IDENTIFIER) {
            FieldRef f;
            if (!resolve_ref(ir, arg, &f)) continue;
            if (f.var->is_numeric && !f.refmod && dialect->display == DISPLAY_PRETTY) {
                // GnuCOBOL style: a sign for a signed item, every digit
                // position and the decimal point, through an edit mask
                // ("S99.99"); P positions show as zeros
                int s = f.var->decimal_places, d = display_digits(f.var);
                int int_digits = d - s > 0 ? d - s : 0, frac = s > 0 ? s : 0;
                static char mask[48];
                static VarInfo pretty;
                int n = 0;
                if (f.var->is_signed) mask[n++] = 'S';
                for (int k = 0; k < int_digits && n < 40; k++) mask[n++] = '9';
                if (frac) {
                    mask[n++] = '.';
                    for (int k = 0; k < frac && n < 40; k++) mask[n++] = '9';
                }
                mask[n] = '\0';
                memset(&pretty, 0, sizeof(pretty));
                pretty.mask = mask;
                pretty.num_edited = true;
                pretty.size = n;
                FieldRef z = { .var = &pretty, .size = n };
                snprintf(z.label, sizeof(z.label), "$numtext");
                emit(ir, IR_SIZE_ERR_CLEAR);
                emit_load(ir, &f);
                if (s < 0) emit_rescale(ir, -s, false);
                emit_store(ir, &z, false);
                append_instr(ir, new_instr(IR_CALL_DISPLAY_VAR_STR, z.label, NULL, NULL, 0, n, 0, 0, USAGE_DISPLAY));
            } else if (f.var->is_numeric && !f.refmod && f.var->usage_type != USAGE_DISPLAY) {
                // A binary or packed item shows as the DISPLAY item of the same
                // PICTURE would: its digits, a signed one with the sign
                // overpunched on the last digit
                static VarInfo zoned;
                memset(&zoned, 0, sizeof(zoned));
                zoned.usage_type = USAGE_DISPLAY;
                zoned.is_numeric = true;
                zoned.is_signed = f.var->is_signed;
                int d = display_digits(f.var);
                zoned.digits = zoned.size = d > 0 && d <= 18 ? d : 18;
                FieldRef z = { .var = &zoned, .size = zoned.size };
                snprintf(z.label, sizeof(z.label), "$numtext");
                emit(ir, IR_SIZE_ERR_CLEAR);
                emit_load(ir, &f);
                emit_store(ir, &z, false);
                append_instr(ir, new_instr(IR_CALL_DISPLAY_VAR_STR, z.label, NULL, NULL, 0, z.size, 0, 0, USAGE_DISPLAY));
            } else {
                // Everything else (numeric DISPLAY items included) shows its
                // stored characters
                append_instr(ir, new_instr(IR_CALL_DISPLAY_VAR_STR, f.label, NULL, NULL, 0, f.size, 0, 0, USAGE_DISPLAY));
            }
        }
    }
    if (!node->is_numeric)          // WITH NO ADVANCING keeps the line open
        append_instr(ir, new_instr(IR_EMIT_NEWLINE, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
}

// Move n unsigned digits from the "$numtext" buffer into a receiver, as from
// an unsigned numeric DISPLAY item of n digits
static void move_from_digits(IrProgram *ir, int n, AstNode *dest_node) {
    VarInfo *dv = find_item(dest_node, true);
    if (!dv) return;
    FieldRef dest;
    if ((dv->is_numeric || is_num_edited(dv)) && !dest_node->loop) {
        static VarInfo digits_view;
        memset(&digits_view, 0, sizeof(digits_view));
        digits_view.usage_type = USAGE_DISPLAY;
        digits_view.is_numeric = true;
        digits_view.size = digits_view.digits = n;
        FieldRef src = { .var = &digits_view, .size = n };
        snprintf(src.label, sizeof(src.label), "$numtext");
        emit(ir, IR_SIZE_ERR_CLEAR);
        emit_load(ir, &src);
        emit_rescale(ir, dv->decimal_places, false);
        if (resolve_ref(ir, dest_node, &dest)) emit_store(ir, &dest, false);
    } else if (resolve_ref(ir, dest_node, &dest)) {
        if (is_alnum_edited(dv) && !dest_node->loop) emit_edit_alnum(ir, &dest, "$numtext", n);
        else emit_move_alpha(ir, &dest, "$numtext", n);
    }
}

static void lower_accept(IrProgram *ir, AstNode *node) {
    AstNode *var_node = node->children;
    if (node->name && var_node) {
        // ACCEPT FROM DATE (YYMMDD), DAY (YYDDD), TIME (HHMMSShh), DAY-OF-WEEK
        // (1-7); DATE YYYYMMDD and DAY YYYYDDD (extensions)
        static const struct { const char *name; int kind, digits; } sources[] = {
            { "DATE", 0, 6 }, { "DAY", 1, 5 }, { "TIME", 2, 8 }, { "DAY-OF-WEEK", 3, 1 },
            { "DATE YYYYMMDD", 4, 8 }, { "DAY YYYYDDD", 5, 7 },
        };
        for (size_t k = 0; k < sizeof(sources) / sizeof(sources[0]); k++) {
            if (strcmp(node->name, sources[k].name) != 0) continue;
            emit(ir, IR_CALL_ACCEPT_DATE)->imm = sources[k].kind;
            move_from_digits(ir, sources[k].digits, var_node);
            return;
        }
    }
    if (!var_node || var_node->kind != AST_IDENTIFIER) {
        diag_error(node->loc, "ACCEPT requires a data item to receive the input");
        return;
    }
    FieldRef f;
    if (!resolve_ref(ir, var_node, &f)) return;
    if (f.var->is_numeric && !f.refmod) {
        IrInstr *a = new_instr(IR_CALL_ACCEPT_NUM, f.label, NULL, NULL, 0, f.var->size,
                               f.var->decimal_places > 0 ? f.var->decimal_places : 0, 0,
                               f.var->usage_type);
        a->digits = f.var->digits;
        a->flags = var_flags(f.var);
        append_instr(ir, a);
    } else {
        append_instr(ir, new_instr(IR_CALL_ACCEPT, f.label, NULL, NULL, 0, f.size, 0, ' ', USAGE_DISPLAY));
    }
}

static void lower_node_kind(IrProgram *ir, AstNode *node);

// Statements that can end the run with a run-time error: they tell the
// runtime where the program is first
static bool can_fail(AstKind k) {
    return k == AST_STATEMENT_CALL || k == AST_STATEMENT_OPEN || k == AST_STATEMENT_CLOSE ||
           k == AST_STATEMENT_READ || k == AST_STATEMENT_WRITE || k == AST_STATEMENT_REWRITE ||
           k == AST_STATEMENT_DELETE || k == AST_STATEMENT_START || k == AST_STATEMENT_SORT ||
           k == AST_STATEMENT_RELEASE || k == AST_STATEMENT_RETURN;
}

static void lower_node(IrProgram *ir, AstNode *node) {
    if (!node) return;
    int saved = stmt_kind_now;
    const AstNode *saved_stmt = where_stmt;
    if (node->kind != AST_SENTENCE && node->kind != AST_THEN_BLOCK && node->kind != AST_ELSE_BLOCK) {
        stmt_kind_now = node->kind;
        where_stmt = node;
        // (with -fcheck any statement can: a numeric item without a number)
        if (can_fail(node->kind) || ir_runtime_checks)
            emit(ir, IR_SET_WHERE)->var_name = strdup(where_label(ir, node->loc));
    }
    lower_node_kind(ir, node);
    stmt_kind_now = saved;
    where_stmt = saved_stmt;
}

// ── Report Writer (runtime/ABI.md section 16) ─────────────────────────────────
// A report is produced by code generated from its description: INITIATE,
// GENERATE and TERMINATE become ordinary moves, comparisons and WRITE
// statements of the lines (records the parser declared) to the report file.
// The state of a report is kept in temporary items.

static FieldRef var_field(int idx);

typedef struct {
    AstNode *rd;
    int file;               // its file
    char rec_name[96];      // the file's record ("$RWR-file")
    int pc, lc;             // PAGE-COUNTER and LINE-COUNTER
    int state;              // 0 not initiated, 1 initiated, 2 a GENERATE was done
    int pl;                 // the physical line of the file (LINE-COUNTER less NEXT GROUP PLUS)
    int eject;              // 1: the next line starts a new page
    int body;               // 1: a body group is on the page
    int force;              // 1: the next body group goes on a new page (NEXT GROUP NEXT PAGE)
    int level;              // the level of a control break
    int tg, adv;            // the line being presented, the lines to advance
    int nctl;
    AstNode *ctl[16];       // the CONTROL data-names, major first
    int saved[16], swap[16];// their values at the last GENERATE; a copy during CF
    AstNode *rh, *ph, *pf, *rf, *chf, *cff;    // (chf, cff: CONTROL HEADING / FOOTING FINAL)
    AstNode *ch[16], *cf[16];
} RwReport;

typedef struct {
    AstNode *field;
    int report;
    AstNode *group;
    int var;                // the printed item
    int acc;                // the sum counter
    int reset_level;        // the control level whose CONTROL FOOTING resets it (-1: FINAL, -2: never)
} RwSum;

static RwReport rw_reports[16];
static void rw_suggest(const char *name, bool details);
static int rw_num_reports;
static RwSum rw_sums[256];
static int rw_num_sums;
static struct { AstNode *group; int flag; } rw_gi[64];      // GROUP INDICATE: 1 until printed
static int rw_num_gi;
static int rw_blank = -1;                   // a space, written for the first line of a page
static int rw_suppress = -1;                // SUPPRESS PRINTING was executed

static AstNode *rw_id(int var) {
    char b[24];
    snprintf(b, sizeof(b), "#%d", var);
    return ast_new(AST_IDENTIFIER, b, (SourceLocation){0});
}

static AstNode *rw_lit(long long v) {
    char b[32];
    snprintf(b, sizeof(b), "%lld", v);
    return ast_new(AST_LITERAL_NUMBER, b, (SourceLocation){0});
}

static void rw_set(IrProgram *ir, int var, AstNode *tree) {
    emit(ir, IR_SIZE_ERR_CLEAR);
    AstNode *r = rw_id(var);
    lower_assign(ir, tree, r, false);
    ast_free(tree);
    ast_free(r);
}

static void rw_set_const(IrProgram *ir, int var, long long v) { rw_set(ir, var, rw_lit(v)); }
static void rw_set_var(IrProgram *ir, int var, int from) { rw_set(ir, var, rw_id(from)); }
static void rw_add_const(IrProgram *ir, int var, long long v) { rw_set(ir, var, mk_bin(AST_EXPR_ADD, rw_id(var), rw_lit(v))); }

// Jump to label when (left op right); the operands are freed
static void rw_jump_if(IrProgram *ir, AstNode *left, const char *op, AstNode *right, const char *label) {
    AstNode *c = make_relation(left, op, right);
    emit_cond_jump(ir, c, label, true);
    ast_free(c);
}

static void rw_move(IrProgram *ir, AstNode *src, int dest) {
    char lit[32] = {0};
    if (src->kind == AST_STRING_LITERAL || src->kind == AST_LITERAL_NUMBER)
        emit_data_lit(ir, "lit", src->name ? src->name : "", lit, sizeof(lit));
    emit(ir, IR_SIZE_ERR_CLEAR);
    AstNode *d = rw_id(dest);
    lower_move(ir, src, d, lit);
    ast_free(d);
}

static int rw_var_named(const char *name) {
    for (int i = 0; i < num_vars; i++)
        if (!vars[i].is_filler && vars[i].name && strcasecmp(vars[i].name, name) == 0) return i;
    return -1;
}

static int rw_gi_flag(const AstNode *group) {
    for (int k = 0; k < rw_num_gi; k++) if (rw_gi[k].group == group) return rw_gi[k].flag;
    return -1;
}

// WRITE the report's record FROM line AFTER ADVANCING count LINES (count
// NULL: PAGE)
static void rw_write(IrProgram *ir, RwReport *r, int line, AstNode *count) {
    SourceLocation none = {0};
    AstNode *w = ast_new(AST_STATEMENT_WRITE, NULL, none);
    w->extra = ast_new(AST_IDENTIFIER, r->rec_name, none);
    w->loop = rw_id(line);
    w->name = strdup("AFTER");
    w->children = count ? count : ast_new(AST_BOOL_LITERAL, "PAGE", none);
    lower_node(ir, w);
    ast_free(w);
}

// Present a line on line tg of the page
static void rw_present(IrProgram *ir, RwReport *r, int line) {
    char eject[32], done[32], first[32];
    new_label(eject, sizeof(eject), "rw_eject");
    new_label(done, sizeof(done), "rw_presented");
    new_label(first, sizeof(first), "rw_first");
    rw_jump_if(ir, rw_id(r->eject), "=", rw_lit(1), eject);
    rw_set(ir, r->adv, mk_bin(AST_EXPR_SUB, rw_id(r->tg), rw_id(r->pl)));
    rw_write(ir, r, line, rw_id(r->adv));
    emit_jump(ir, IR_JUMP, done);
    emit_label(ir, eject);
    rw_set_const(ir, r->eject, 0);
    rw_jump_if(ir, rw_id(r->tg), "=", rw_lit(1), first);
    rw_write(ir, r, rw_blank, NULL);            // line 1 of the new page, then down to tg
    rw_set(ir, r->adv, mk_bin(AST_EXPR_SUB, rw_id(r->tg), rw_lit(1)));
    rw_write(ir, r, line, rw_id(r->adv));
    emit_jump(ir, IR_JUMP, done);
    emit_label(ir, first);
    rw_write(ir, r, line, NULL);
    emit_label(ir, done);
    rw_set_var(ir, r->pl, r->tg);
}

static bool rw_body(int type) { return type == RW_CH || type == RW_DE || type == RW_CF; }

static void rw_print_group(IrProgram *ir, RwReport *r, AstNode *g);

// The end of a page: PAGE FOOTING, then PAGE HEADING on the next page
static void rw_page_break(IrProgram *ir, RwReport *r) {
    if (r->pf) rw_print_group(ir, r, r->pf);
    rw_set_const(ir, r->eject, 1);
    rw_set_const(ir, r->lc, 0);
    rw_set_const(ir, r->pl, 0);
    rw_add_const(ir, r->pc, 1);
    rw_set_const(ir, r->body, 0);
    for (int k = 0; k < rw_num_gi; k++)
        for (AstNode *g = r->rd->children; g; g = g->next)
            if (rw_gi[k].group == g) rw_set_const(ir, rw_gi[k].flag, 1);
    if (r->ph) rw_print_group(ir, r, r->ph);
}

// The fields of a line take their values: SOURCE, SUM, and (GROUP INDICATE)
// spaces or their value
static void rw_fill(IrProgram *ir, RwReport *r, AstNode *g, AstNode *line) {
    int gi = rw_gi_flag(g);
    for (AstNode *f = line->children; f; f = f->next) {
        int var = rw_var_named(f->name);
        if (var < 0) continue;
        char skip[32] = "", done[32] = "";
        if (f->rw->group_indicate && gi >= 0) {
            new_label(skip, sizeof(skip), "rw_gi_blank");
            new_label(done, sizeof(done), "rw_gi_done");
            rw_jump_if(ir, rw_id(gi), "=", rw_lit(0), skip);
        }
        if (f->rw->source) rw_move(ir, f->rw->source, var);
        else if (f->rw->sum) {
            for (int k = 0; k < rw_num_sums; k++)
                if (rw_sums[k].field == f) rw_move(ir, rw_id(rw_sums[k].acc), var);
        } else if (f->children && f->rw->group_indicate) rw_move(ir, f->children, var);
        if (*skip) {
            emit_jump(ir, IR_JUMP, done);
            emit_label(ir, skip);
            AstNode *sp = ast_new(AST_FIGURATIVE_CONSTANT, "SPACES", (SourceLocation){0});
            rw_move(ir, sp, var);
            ast_free(sp);
            emit_label(ir, done);
        }
    }
    (void)r;
}

static int rw_use_before(const char *group) {
    for (int k = 0; k < num_uses; k++)
        if (uses[k].mode == USE_REPORTING && uses[k].file && group && strcasecmp(uses[k].file, group) == 0)
            return k;
    return -1;
}

// Present a report group: USE BEFORE REPORTING, page control, its lines,
// NEXT GROUP
static void rw_print_group(IrProgram *ir, RwReport *r, AstNode *g) {
    RwDesc *gr = g->rw, *rd = r->rd->rw;
    int type = gr->type;
    bool paged = rd->page > 0;
    char skip[32];
    new_label(skip, sizeof(skip), "rw_group_end");
    int use = rw_use_before(g->name);
    if (use >= 0) {
        rw_set_const(ir, rw_suppress, 0);
        emit_perform_use(ir, use);
        rw_jump_if(ir, rw_id(rw_suppress), "=", rw_lit(1), skip);
    }
    AstNode *lines[64];
    int n = 0;
    for (AstNode *l = g->children; l && n < 64; l = l->next)
        if (l->kind == AST_REPORT_LINE) lines[n++] = l;
    if (n == 0) { emit_label(ir, skip); return; }
    int limit = type == RW_CF ? rd->footing : rd->last_detail;
    if (paged && rw_body(type)) {
        // NEXT GROUP NEXT PAGE of the group before, LINE NEXT PAGE of this one
        char no_force[32];
        new_label(no_force, sizeof(no_force), "rw_no_force");
        if (lines[0]->rw->line_kind != RW_AT_NEXT_PAGE)
            rw_jump_if(ir, rw_id(r->force), "=", rw_lit(0), no_force);
        rw_page_break(ir, r);
        rw_set_const(ir, r->force, 0);
        char fitted[32];
        new_label(fitted, sizeof(fitted), "rw_fitted");
        emit_jump(ir, IR_JUMP, fitted);
        emit_label(ir, no_force);
        // Does the group fit on this page?
        if (lines[0]->rw->line_kind == RW_AT_LINE) {
            // an absolute first line: on this page if it is below the last line
            rw_jump_if(ir, rw_id(r->eject), "=", rw_lit(1), fitted);
            rw_jump_if(ir, rw_id(r->lc), "<", rw_lit(lines[0]->rw->line), fitted);
        } else {
            int total = 0;
            for (int k = 0; k < n && lines[k]->rw->line_kind == RW_AT_PLUS; k++) total += lines[k]->rw->line;
            rw_jump_if(ir, rw_id(r->body), "=", rw_lit(0), fitted);
            rw_jump_if(ir, mk_bin(AST_EXPR_ADD, rw_id(r->lc), rw_lit(total)), "<=", rw_lit(limit), fitted);
        }
        rw_page_break(ir, r);
        emit_label(ir, fitted);
    }
    for (int k = 0; k < n; k++) {
        RwDesc *l = lines[k]->rw;
        if (l->line_kind == RW_AT_LINE || l->line_kind == RW_AT_NEXT_PAGE) {
            rw_set_const(ir, r->tg, l->line ? l->line : (paged ? rd->first_detail : 1));
        } else if (k == 0 && paged && type != RW_RF) {
            // the first line of a group that is relative: at the first line of
            // its area if the page has nothing of the area yet
            int start = type == RW_PH || type == RW_RH ? rd->heading :
                        type == RW_PF ? rd->footing + 1 : rd->first_detail;
            char normal[32], go[32];
            new_label(normal, sizeof(normal), "rw_rel");
            new_label(go, sizeof(go), "rw_line");
            if (rw_body(type)) rw_jump_if(ir, rw_id(r->body), "=", rw_lit(1), normal);
            rw_jump_if(ir, rw_id(r->lc), ">=", rw_lit(start), normal);
            rw_set_const(ir, r->tg, start);
            emit_jump(ir, IR_JUMP, go);
            emit_label(ir, normal);
            rw_set(ir, r->tg, mk_bin(AST_EXPR_ADD, rw_id(r->lc), rw_lit(l->line)));
            emit_label(ir, go);
        } else {
            rw_set(ir, r->tg, mk_bin(AST_EXPR_ADD, rw_id(r->lc), rw_lit(l->line)));
        }
        // LINE-COUNTER is the line being presented while its fields take their values
        rw_set_var(ir, r->lc, r->tg);
        rw_fill(ir, r, g, lines[k]);
        rw_present(ir, r, rw_var_named(lines[k]->text));
    }
    if (paged && rw_body(type)) rw_set_const(ir, r->body, 1);
    int gi = rw_gi_flag(g);
    if (gi >= 0) rw_set_const(ir, gi, 0);
    // NEXT GROUP
    if (gr->next_kind == RW_AT_PLUS) rw_add_const(ir, r->lc, gr->next);
    else if (gr->next_kind == RW_AT_NEXT_PAGE) { if (paged) rw_set_const(ir, r->force, 1); }
    else if (gr->next_kind == RW_AT_LINE) {
        char later[32], done[32];
        new_label(later, sizeof(later), "rw_next_line");
        new_label(done, sizeof(done), "rw_next_done");
        rw_jump_if(ir, rw_id(r->lc), "<", rw_lit(gr->next), later);
        if (paged) rw_set_const(ir, r->force, 1);
        emit_jump(ir, IR_JUMP, done);
        emit_label(ir, later);
        rw_set_const(ir, r->lc, gr->next);
        emit_label(ir, done);
    }
    emit_label(ir, skip);
}

// A CONTROL FOOTING: presented, then its sum counters roll into the
// counters that sum them, then the counters of this level are reset
static void rw_control_footing(IrProgram *ir, RwReport *r, AstNode *g, int level) {
    if (g) {
        rw_print_group(ir, r, g);
        for (int k = 0; k < rw_num_sums; k++) {
            if (rw_sums[k].report != (int)(r - rw_reports)) continue;
            for (AstNode *op = rw_sums[k].field->rw->sum; op; op = op->next)
                for (int j = 0; j < rw_num_sums; j++)
                    if (rw_sums[j].group == g && op->name && strcasecmp(op->name, rw_sums[j].field->name) == 0)
                        rw_set(ir, rw_sums[k].acc, mk_bin(AST_EXPR_ADD, rw_id(rw_sums[k].acc), rw_id(rw_sums[j].acc)));
        }
    }
    for (int k = 0; k < rw_num_sums; k++)
        if (rw_sums[k].report == (int)(r - rw_reports) && rw_sums[k].reset_level == level)
            rw_set_const(ir, rw_sums[k].acc, 0);
}

// The values of the controls: copies made and compared as their bytes
static void rw_copy(IrProgram *ir, int dest_var, AstNode *ctl, bool to_control, int temp) {
    FieldRef c, t = var_field(temp);
    if (!resolve_ref(ir, ctl, &c)) return;
    if (to_control) emit_move_alpha(ir, &c, t.label, t.size);
    else emit_move_alpha(ir, &t, c.label, c.size);
    (void)dest_var;
}

static bool rw_is_counter(int report, const char *name) {
    for (int k = 0; k < rw_num_sums; k++)
        if (rw_sums[k].report == report && strcasecmp(rw_sums[k].field->name, name) == 0) return true;
    return false;
}

// "did you mean" among the reports (and their DETAIL groups)
static void rw_suggest(const char *name, bool details) {
    const char *names[256] = { 0 };
    const AstNode *where[256];
    int n = 0;
    for (int k = 0; k < rw_num_reports && n < 256; k++) {
        where[n] = rw_reports[k].rd;
        names[n++] = rw_reports[k].rd->name;
        for (AstNode *g = rw_reports[k].rd->children; details && g && n < 256; g = g->next)
            if (g->name && g->rw->type == RW_DE) { where[n] = g; names[n++] = g->name; }
    }
    const char *near = diag_closest(name, names, n);
    for (int k = 0; near && k < n; k++)
        if (names[k] == near) { diag_note(where[k]->loc, "did you mean '%s'?", near); break; }
}

static void lower_generate(IrProgram *ir, AstNode *node) {
    const char *name = node->extra->name;
    RwReport *r = NULL;
    AstNode *detail = NULL;
    for (int k = 0; k < rw_num_reports && !r; k++) {
        if (strcasecmp(rw_reports[k].rd->name, name) == 0) r = &rw_reports[k];
        for (AstNode *g = rw_reports[k].rd->children; g && !r; g = g->next)
            if (g->name && strcasecmp(g->name, name) == 0) {
                if (g->rw->type != RW_DE) {
                    diag_error(node->extra->loc, "GENERATE needs a DETAIL report group or a report, but '%s' is a %s group",
                               name, g->rw->type == RW_PH ? "PAGE HEADING" : g->rw->type == RW_PF ? "PAGE FOOTING" :
                               g->rw->type == RW_RH ? "REPORT HEADING" : g->rw->type == RW_RF ? "REPORT FOOTING" :
                               g->rw->type == RW_CH ? "CONTROL HEADING" : "CONTROL FOOTING");
                    return;
                }
                r = &rw_reports[k];
                detail = g;
            }
    }
    if (!r) {
        diag_error(node->extra->loc, "'%s' is not a report or a DETAIL report group", name);
        rw_suggest(name, true);
        return;
    }
    int ri = (int)(r - rw_reports);
    char cont[32], sum[32], end[32];
    new_label(cont, sizeof(cont), "rw_gen_next");
    new_label(sum, sizeof(sum), "rw_gen_sum");
    new_label(end, sizeof(end), "rw_gen_end");
    rw_jump_if(ir, rw_id(r->state), "=", rw_lit(2), cont);
    // The first GENERATE: REPORT HEADING, PAGE HEADING, the CONTROL HEADINGs
    rw_set_const(ir, r->lc, 0);
    rw_set_const(ir, r->pl, 0);
    rw_set_const(ir, r->eject, 0);
    rw_set_const(ir, r->body, 0);
    if (r->rh) rw_print_group(ir, r, r->rh);
    if (r->ph) rw_print_group(ir, r, r->ph);
    if (r->chf) rw_print_group(ir, r, r->chf);
    for (int i = 0; i < r->nctl; i++) if (r->ch[i]) rw_print_group(ir, r, r->ch[i]);
    for (int i = 0; i < r->nctl; i++) rw_copy(ir, 0, r->ctl[i], false, r->saved[i]);
    rw_set_const(ir, r->state, 2);
    emit_jump(ir, IR_JUMP, sum);
    // A control break: the major control that changed
    emit_label(ir, cont);
    char brk_at[16][32], do_break[32];
    new_label(do_break, sizeof(do_break), "rw_break");
    for (int i = 0; i < r->nctl; i++) {
        new_label(brk_at[i], sizeof(brk_at[i]), "rw_break_at");
        FieldRef c, t = var_field(r->saved[i]);
        if (!resolve_ref(ir, r->ctl[i], &c)) continue;
        IrInstr *j = emit(ir, IR_ALNUM_CMP_JUMP_IF_FALSE);      // not equal: a break at level i
        j->var_name = strdup(c.label);
        j->size = c.size;
        j->lit_name = strdup(t.label);
        j->lit_len = t.size;
        j->cmp_op = strdup("=");
        j->jump_target = strdup(brk_at[i]);
    }
    emit_jump(ir, IR_JUMP, sum);
    for (int i = 0; i < r->nctl; i++) {
        emit_label(ir, brk_at[i]);
        rw_set_const(ir, r->level, i);
        emit_jump(ir, IR_JUMP, do_break);
    }
    emit_label(ir, do_break);
    if (r->nctl > 0) {
        // the CONTROL FOOTINGs, minor first, with the values the controls had
        for (int i = 0; i < r->nctl; i++) {
            rw_copy(ir, 0, r->ctl[i], false, r->swap[i]);
            rw_copy(ir, 0, r->ctl[i], true, r->saved[i]);
        }
        for (int j = r->nctl - 1; j >= 0; j--) {
            char skip[32];
            new_label(skip, sizeof(skip), "rw_cf_skip");
            rw_jump_if(ir, rw_id(r->level), ">", rw_lit(j), skip);
            rw_control_footing(ir, r, r->cf[j], j);
            emit_label(ir, skip);
        }
        for (int i = 0; i < r->nctl; i++) rw_copy(ir, 0, r->ctl[i], true, r->swap[i]);
        // the CONTROL HEADINGs, major first, with the new values
        for (int j = 0; j < r->nctl; j++) {
            if (!r->ch[j]) continue;
            char skip[32];
            new_label(skip, sizeof(skip), "rw_ch_skip");
            rw_jump_if(ir, rw_id(r->level), ">", rw_lit(j), skip);
            rw_print_group(ir, r, r->ch[j]);
            emit_label(ir, skip);
        }
        for (int i = 0; i < r->nctl; i++) rw_copy(ir, 0, r->ctl[i], false, r->saved[i]);
        for (int k = 0; k < rw_num_gi; k++)
            for (AstNode *g = r->rd->children; g; g = g->next)
                if (rw_gi[k].group == g) rw_set_const(ir, rw_gi[k].flag, 1);
    }
    emit_label(ir, sum);
    // Summing: the SUM operands that are not sum counters
    for (int k = 0; k < rw_num_sums; k++) {
        if (rw_sums[k].report != ri) continue;
        RwDesc *f = rw_sums[k].field->rw;
        if (f->upon && (!detail || strcasecmp(f->upon, detail->name) != 0)) continue;
        for (AstNode *op = f->sum; op; op = op->next) {
            if (op->name && rw_is_counter(ri, op->name)) continue;
            rw_set(ir, rw_sums[k].acc, mk_bin(AST_EXPR_ADD, rw_id(rw_sums[k].acc), ast_clone(op)));
        }
    }
    if (detail) rw_print_group(ir, r, detail);
    emit_label(ir, end);
}

static RwReport *rw_report_named(const AstNode *ref) {
    for (int k = 0; k < rw_num_reports; k++)
        if (strcasecmp(rw_reports[k].rd->name, ref->name) == 0) return &rw_reports[k];
    diag_error(ref->loc, "'%s' is not a report (no RD entry has this name)", ref->name);
    rw_suggest(ref->name, false);
    return NULL;
}

static void lower_initiate(IrProgram *ir, AstNode *node) {
    for (AstNode *n = node->children; n; n = n->next) {
        RwReport *r = rw_report_named(n);
        if (!r) continue;
        rw_set_const(ir, r->pc, 1);
        rw_set_const(ir, r->lc, 0);
        rw_set_const(ir, r->state, 1);
        rw_set_const(ir, r->pl, 0);
        rw_set_const(ir, r->eject, 0);
        rw_set_const(ir, r->body, 0);
        rw_set_const(ir, r->force, 0);
        for (int k = 0; k < rw_num_sums; k++)
            if (rw_sums[k].report == (int)(r - rw_reports)) rw_set_const(ir, rw_sums[k].acc, 0);
        for (int k = 0; k < rw_num_gi; k++)
            for (AstNode *g = r->rd->children; g; g = g->next)
                if (rw_gi[k].group == g) rw_set_const(ir, rw_gi[k].flag, 1);
        AstNode *sp = ast_new(AST_FIGURATIVE_CONSTANT, "SPACES", (SourceLocation){0});
        rw_move(ir, sp, rw_blank);
        ast_free(sp);
    }
}

static void lower_terminate(IrProgram *ir, AstNode *node) {
    for (AstNode *n = node->children; n; n = n->next) {
        RwReport *r = rw_report_named(n);
        if (!r) continue;
        char end[32];
        new_label(end, sizeof(end), "rw_term_end");
        // Without a GENERATE, TERMINATE presents nothing
        rw_jump_if(ir, rw_id(r->state), "<", rw_lit(2), end);
        for (int j = r->nctl - 1; j >= 0; j--) rw_control_footing(ir, r, r->cf[j], j);
        rw_control_footing(ir, r, r->cff, -1);
        if (r->pf) rw_print_group(ir, r, r->pf);
        if (r->rf) {
            // a REPORT FOOTING whose first line is above the line reached: on a new page
            AstNode *first = NULL;
            for (AstNode *l = r->rf->children; l && !first; l = l->next) if (l->kind == AST_REPORT_LINE) first = l;
            if (first && r->rd->rw->page && (first->rw->line_kind == RW_AT_LINE || first->rw->line_kind == RW_AT_NEXT_PAGE)) {
                char same[32];
                new_label(same, sizeof(same), "rw_rf_here");
                if (first->rw->line_kind == RW_AT_LINE)
                    rw_jump_if(ir, rw_id(r->lc), "<", rw_lit(first->rw->line), same);
                rw_set_const(ir, r->eject, 1);
                rw_set_const(ir, r->lc, 0);
                rw_set_const(ir, r->pl, 0);
                rw_add_const(ir, r->pc, 1);
                emit_label(ir, same);
            }
            rw_print_group(ir, r, r->rf);
        }
        emit_label(ir, end);
        rw_set_const(ir, r->state, 0);
    }
}

// The reports of a program: their groups, controls, sum counters and state
static void rw_setup(AstNode *program) {
    rw_num_reports = rw_num_sums = rw_num_gi = 0;
    rw_blank = rw_suppress = -1;
    for (AstNode *div = program->children; div; div = div->next) {
        if (div->kind != AST_DIVISION || strcasecmp(div->name, "DATA") != 0) continue;
        for (AstNode *sec = div->children; sec; sec = sec->next) {
            if (sec->kind != AST_SECTION || !sec->name || strcmp(sec->name, "REPORT") != 0) continue;
            for (AstNode *rd = sec->children; rd; rd = rd->next) {
                if (rw_num_reports == 16) {
                    diag_error(rd->loc, "Too many reports in one program (16 limit)");
                    break;
                }
                RwReport *r = &rw_reports[rw_num_reports];
                memset(r, 0, sizeof(*r));
                r->rd = rd;
                r->file = -1;
                for (int f = 0; f < num_files; f++)
                    if (rd->rw->file && strcasecmp(files[f].name, rd->rw->file) == 0) r->file = f;
                if (r->file < 0) continue;              // (reported by the parser)
                snprintf(r->rec_name, sizeof(r->rec_name), "$RWR-%s", files[r->file].name);
                r->pc = r->lc = -1;
                for (int i = 0; i < num_vars; i++) {
                    if (vars[i].parent < 0 || strcasecmp(vars[vars[i].parent].name, rd->name) != 0) continue;
                    if (strcasecmp(vars[i].name, "PAGE-COUNTER") == 0) r->pc = i;
                    if (strcasecmp(vars[i].name, "LINE-COUNTER") == 0) r->lc = i;
                }
                if (r->pc < 0 || r->lc < 0) continue;
                r->state = temp_numeric(0, rd);
                r->pl = temp_numeric(0, rd);
                r->eject = temp_numeric(0, rd);
                r->body = temp_numeric(0, rd);
                r->force = temp_numeric(0, rd);
                r->level = temp_numeric(0, rd);
                r->tg = temp_numeric(0, rd);
                r->adv = temp_numeric(0, rd);
                for (AstNode *c = rd->extra; c && r->nctl < 16; c = c->next) {
                    VarInfo *v = find_item(c, true);
                    if (!v) continue;
                    r->ctl[r->nctl] = c;
                    r->saved[r->nctl] = temp_alnum(v->size, rd);
                    r->swap[r->nctl] = temp_alnum(v->size, rd);
                    r->nctl++;
                }
                for (AstNode *g = rd->children; g; g = g->next) {
                    RwDesc *d = g->rw;
                    AstNode **slot = NULL;
                    int level = -2;
                    if (d->type == RW_CH || d->type == RW_CF) {
                        if (d->control && strcasecmp(d->control, "FINAL") == 0) {
                            if (!rd->rw->final_control)
                                diag_error(g->loc, "CONTROL %s FINAL, but the CONTROL clause of '%s' does not name FINAL",
                                           d->type == RW_CH ? "HEADING" : "FOOTING", rd->name);
                            slot = d->type == RW_CH ? &r->chf : &r->cff;
                            level = -1;
                        } else {
                            for (int i = 0; i < r->nctl; i++)
                                if (d->control && strcasecmp(r->ctl[i]->name, d->control) == 0) {
                                    slot = d->type == RW_CH ? &r->ch[i] : &r->cf[i];
                                    level = i;
                                }
                            if (!slot) {
                                diag_error(g->loc, "'%s' is not a control of the report '%s'", d->control ? d->control : "?", rd->name);
                                diag_note(rd->loc, "the CONTROL clause of '%s' names its controls", rd->name);
                            }
                        }
                    } else if (d->type == RW_RH) slot = &r->rh;
                    else if (d->type == RW_PH) slot = &r->ph;
                    else if (d->type == RW_PF) slot = &r->pf;
                    else if (d->type == RW_RF) slot = &r->rf;
                    if ((d->type == RW_PH || d->type == RW_PF) && !rd->rw->page)
                        diag_error(g->loc, "A PAGE %s group needs a PAGE clause in the RD of '%s'",
                                   d->type == RW_PH ? "HEADING" : "FOOTING", rd->name);
                    if (slot && *slot) diag_error(g->loc, "The report '%s' has two groups of this type", rd->name);
                    else if (slot) *slot = g;
                    // its sum counters and GROUP INDICATE fields
                    bool gi = false;
                    for (AstNode *c = g->children; c; c = c->next) {
                        AstNode *fields = c->kind == AST_REPORT_LINE ? c->children : c;
                        for (AstNode *f = fields; f; f = c->kind == AST_REPORT_LINE ? f->next : NULL) {
                            gi |= f->rw->group_indicate;
                            if (!f->rw->sum || rw_num_sums >= 256) continue;
                            RwSum *sum = &rw_sums[rw_num_sums++];
                            sum->field = f;
                            sum->report = rw_num_reports;
                            sum->group = g;
                            sum->var = rw_var_named(f->name);
                            int dp = sum->var >= 0 && vars[sum->var].decimal_places > 0 ? vars[sum->var].decimal_places : 0;
                            sum->acc = temp_numeric(dp, rd);
                            sum->reset_level = d->type == RW_CF ? level : -2;
                            if (f->rw->reset) {
                                if (strcasecmp(f->rw->reset, "FINAL") == 0) sum->reset_level = -1;
                                else {
                                    sum->reset_level = -3;
                                    for (int i = 0; i < r->nctl; i++)
                                        if (strcasecmp(r->ctl[i]->name, f->rw->reset) == 0) sum->reset_level = i;
                                    if (sum->reset_level == -3)
                                        diag_error(f->loc, "RESET ON '%s': it is not a control of the report '%s'",
                                                   f->rw->reset, rd->name);
                                }
                            }
                        }
                    }
                    if (gi && rw_num_gi < 64) {
                        rw_gi[rw_num_gi].group = g;
                        rw_gi[rw_num_gi++].flag = temp_numeric(0, rd);
                    }
                }
                rw_num_reports++;
            }
        }
    }
    if (rw_num_reports) {
        rw_blank = temp_alnum(1, program);
        rw_suppress = temp_numeric(0, program);
    }
}

static void lower_node_kind(IrProgram *ir, AstNode *node) {

    switch (node->kind) {
        case AST_SENTENCE: {
            char end_lbl[32];
            new_label(end_lbl, sizeof(end_lbl), "sentence_end");
            const char *outer = sentence_end;
            sentence_end = end_lbl;
            lower_block(ir, node->children);
            sentence_end = outer;
            emit_label(ir, end_lbl);
            break;
        }
        case AST_STATEMENT_PERFORM:
            lower_perform(ir, node);
            break;
        case AST_STATEMENT_GOTO:
            debug_after_statement(ir, node);    // (before the transfer)
            lower_goto(ir, node);
            break;
        case AST_STATEMENT_STRING:
            lower_string(ir, node);
            break;
        case AST_STATEMENT_UNSTRING:
            lower_unstring(ir, node);
            break;
        case AST_STATEMENT_ALTER:
            for (AstNode *a = node->children; a; a = a->next) {
                int para = resolve_proc(a), target = resolve_proc(a->children);
                if (para < 0 || target < 0) continue;
                IrInstr *al = emit(ir, IR_ALTER);
                al->imm = para;
                al->proc_end = target;
            }
            break;
        case AST_STATEMENT_NEXT_SENTENCE:
            if (sentence_end) emit_jump(ir, IR_JUMP, sentence_end);
            break;
        case AST_STATEMENT_EVALUATE: {
            char end_lbl[32];
            new_label(end_lbl, sizeof(end_lbl), "eval_end");
            for (AstNode *when = node->children; when; when = when->next) {
                char next_lbl[32];
                new_label(next_lbl, sizeof(next_lbl), "when_next");
                emit_cond_jump(ir, when->extra, next_lbl, false);
                lower_block(ir, when->children);
                emit_jump(ir, IR_JUMP, end_lbl);
                emit_label(ir, next_lbl);
            }
            emit_label(ir, end_lbl);
            break;
        }
        case AST_STATEMENT_SET:
            lower_set(ir, node);
            break;
        case AST_STATEMENT_INITIALIZE:
            init_replacing = node->extra;
            for (AstNode *t = node->children; t; t = t->next) lower_initialize(ir, t);
            init_replacing = NULL;
            break;
        case AST_STATEMENT_SEARCH:
            lower_search(ir, node);
            break;
        case AST_STATEMENT_INSPECT:
            lower_inspect(ir, node);
            break;
        case AST_STATEMENT_OPEN:
        case AST_STATEMENT_CLOSE:
        case AST_STATEMENT_READ:
        case AST_STATEMENT_DELETE:
        case AST_STATEMENT_START:
        case AST_STATEMENT_WRITE:
        case AST_STATEMENT_REWRITE:
            lower_file_statement(ir, node);
            break;
        case AST_STATEMENT_SORT:
            lower_sort(ir, node);
            break;
        case AST_STATEMENT_CALL:
        case AST_STATEMENT_CANCEL:
            lower_call(ir, node);
            break;
        case AST_STATEMENT_RELEASE:
        case AST_STATEMENT_RETURN:
            lower_release_return(ir, node);
            break;
        case AST_STATEMENT_EXIT:        // marks the end of a procedure; does nothing
            if (node->name && (!strcmp(node->name, "PERFORM") || !strcmp(node->name, "CYCLE") ||
                               !strcmp(node->name, "PARAGRAPH") || !strcmp(node->name, "SECTION")))
                lower_exit_jump(ir, node);
            if (node->name && strcmp(node->name, "PROGRAM") == 0) emit(ir, IR_EXIT_PROGRAM);
            if (node->name && strcmp(node->name, "GOBACK") == 0) {
                // returns from a called program; in the main one it has no
                // effect, and the run ends
                emit(ir, IR_EXIT_PROGRAM);
                append_instr(ir, new_instr(IR_CALL_EXIT, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
            }
            break;
        case AST_STATEMENT_CONTINUE:
            break;
        case AST_STATEMENT_IF: {
            AstNode *cond      = node->children;
            AstNode *then_blk  = cond  ? cond->next  : NULL;
            AstNode *else_blk  = then_blk ? then_blk->next : NULL;

            char else_lbl[32], end_lbl[32];
            new_label(else_lbl, sizeof(else_lbl), "if_else");
            new_label(end_lbl, sizeof(end_lbl), "if_end");
            bool has_else = (else_blk && else_blk->kind == AST_ELSE_BLOCK);

            // Jump to else/end when condition is FALSE
            emit_cond_jump(ir, cond, has_else ? else_lbl : end_lbl, false);
            if (then_blk) lower_block(ir, then_blk->children);
            if (has_else) {
                emit_jump(ir, IR_JUMP, end_lbl);
                emit_label(ir, else_lbl);
                lower_block(ir, else_blk->children);
            }
            emit_label(ir, end_lbl);
            break;
        }
        case AST_STATEMENT_DISPLAY:
            lower_display(ir, node);
            break;
        case AST_STATEMENT_MOVE: {
            AstNode *src = node->children;
            if (!src) break;
            if (node->name && strcmp(node->name, "CORR") == 0) {
                emit(ir, IR_SIZE_ERR_CLEAR);
                for (AstNode *dest = src->next; dest; dest = dest->next)
                    lower_corresponding(ir, node, src, dest, false);
                break;
            }
            // A literal source is emitted once and shared by all receivers
            char lit_name[32] = {0};
            if (src->kind == AST_STRING_LITERAL || src->kind == AST_LITERAL_NUMBER)
                emit_data_lit(ir, "lit", src->name ? src->name : "", lit_name, sizeof(lit_name));
            emit(ir, IR_SIZE_ERR_CLEAR);
            for (AstNode *dest = src->next; dest && dest->kind == AST_IDENTIFIER; dest = dest->next)
                lower_move(ir, src, dest, lit_name);
            break;
        }
        case AST_STATEMENT_ADD:
        case AST_STATEMENT_SUBTRACT:
        case AST_STATEMENT_MULTIPLY:
        case AST_STATEMENT_DIVIDE:
        case AST_STATEMENT_COMPUTE:
            lower_arith_node(ir, node);
            break;
        case AST_STATEMENT_STOP:
            append_instr(ir, new_instr(IR_CALL_EXIT, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
            break;
        case AST_STATEMENT_INITIATE:
            lower_initiate(ir, node);
            break;
        case AST_STATEMENT_GENERATE:
            lower_generate(ir, node);
            break;
        case AST_STATEMENT_TERMINATE:
            lower_terminate(ir, node);
            break;
        case AST_STATEMENT_SUPPRESS:
            if (rw_suppress < 0) diag_error(node->loc, "SUPPRESS PRINTING outside a USE BEFORE REPORTING procedure");
            else rw_set_const(ir, rw_suppress, 1);
            break;
        case AST_STATEMENT_ACCEPT:
            lower_accept(ir, node);
            break;
        default:
            break;
    }
}

static void debug_after_statement(IrProgram *ir, AstNode *stmt);
static void debug_item_event(IrProgram *ir, AstNode *ref, int line);

static void lower_block(IrProgram *ir, AstNode *block) {
    for (AstNode *n = block; n; n = n->next) {
        // WRITE and REWRITE: the debugging procedures of the items they name
        // run before the record is written (after the move of FROM)
        bool before = n->kind == AST_STATEMENT_WRITE || n->kind == AST_STATEMENT_REWRITE;
        lower_node(ir, n);
        if (debugging_mode && n->kind != AST_SENTENCE && n->kind != AST_PARAGRAPH && n->kind != AST_SECTION &&
            n->kind != AST_STATEMENT_GOTO && !before)
            debug_after_statement(ir, n);
    }
}

// Initial value of one occurrence of a data item from its VALUE clause
// (subs: its subscripts, one per table dimension)
static void lower_initial_occurrence(IrProgram *ir, int idx, const int subs[], int ndims) {
    VarInfo *v = &vars[idx];
    AstNode *val = v->value;
    AstNode *ref = item_ref(idx, val->loc);
    for (int d = ndims - 1; d >= 0; d--) {
        char num[16];
        snprintf(num, sizeof(num), "%d", subs[d]);
        AstNode *sub = ast_new(AST_LITERAL_NUMBER, num, val->loc);
        sub->next = ref->children;
        ref->children = sub;
    }
    FieldRef f;
    if (!resolve_ref(ir, ref, &f)) {
        ast_free(ref);
        return;
    }
    ast_free(ref);
    bool numeric = v->is_numeric;
    int fig = figurative_char(val);
    if (fig != -1) {
        if (numeric) {
            emit_num(ir, IR_NUM_CONST, 0, 0);
            emit_store(ir, &f, false);
        } else {
            emit_fill_figurative(ir, &f, val);
        }
    } else if (val->kind == AST_LITERAL_NUMBER && numeric) {
        if (!num_expr_valid(val)) return;
        int s = num_expr(ir, val, 0);
        move_rescale(ir, v, s);
        emit_store(ir, &f, false);
    } else if (val->kind == AST_STRING_LITERAL && !numeric) {
        char lit_name[32];
        emit_data_lit(ir, "initlit", val->name, lit_name, sizeof(lit_name));
        // JUSTIFIED does not apply to the initial value: it is left-aligned
        append_instr(ir, new_instr(IR_CALL_MOVE_ALPHA, f.label, lit_name, NULL,
                                   (int)strlen(val->name), f.size, 0, 0, USAGE_DISPLAY));
    }
}

// Initial value of a data item: every occurrence of an item in a table
static void lower_initial_value(IrProgram *ir, int idx) {
    VarInfo *v = &vars[idx];
    if (!v->value || v->is_cond || v->is_index || record_ptr_slot[v->record] >= 0 ||
        record_extern[v->record]) return;
    int counts[16], strides[16], subs[16];
    int ndims = item_dims(v, counts, strides);
    long total = 1;
    for (int d = 0; d < ndims; d++) { subs[d] = 1; total *= counts[d]; }
    if (total > 100000) {
        diag_error(v->decl->loc, "VALUE in a table of more than 100000 occurrences is not supported ('%s')", v->name);
        return;
    }
    for (long k = 0; k < total; k++) {
        lower_initial_occurrence(ir, idx, subs, ndims);
        for (int d = ndims - 1; d >= 0; d--) {
            if (++subs[d] <= counts[d]) break;
            subs[d] = 1;
        }
    }
}

// A program's entry symbol ("cob_" and its name, - as _) and descriptor
void ir_program_symbols(const char *program_name, char *entry, char *info, size_t size) {
    char m[128];
    size_t k = 0;
    for (const char *p = program_name; *p && k < sizeof(m) - 1; p++)
        m[k++] = isalnum((unsigned char)*p) ? (char)toupper((unsigned char)*p) : '_';
    m[k] = '\0';
    snprintf(entry, size, "cob_%s", m);
    snprintf(info, size, "cobinfo_%s", m);
}

// ── The Debug module ────────────────────────────────────────────────────────

static FieldRef var_field(int idx) {
    FieldRef f = { .var = &vars[idx], .size = vars[idx].size };
    record_label(f.label, sizeof(f.label), vars[idx].record, vars[idx].offset);
    return f;
}

static void move_text_to_var(IrProgram *ir, const char *text, int idx) {
    char lit[32];
    emit_data_lit(ir, "lit", text, lit, sizeof(lit));
    FieldRef d = var_field(idx);
    emit_move_alpha(ir, &d, lit, (int)strlen(text));
}

// DEBUG-ITEM for a debugging procedure, then the procedure: the name, the
// line, the contents (a text, or the field contents), the subscripts
static void emit_debug_call(IrProgram *ir, int u, const char *name, int line, const char *text,
                            const FieldRef *contents, AstNode *subs) {
    // only with the object time debugging switch on (runtime/ABI.md section 14)
    char off[32];
    new_label(off, sizeof(off), "debug_off");
    emit(ir, IR_DEBUG_SWITCH_JUMP)->jump_target = strdup(off);
    FieldRef all = var_field(debug_item);
    IrInstr *fill = emit(ir, IR_CALL_MOVE_FILL);
    fill->var_name = strdup(all.label);
    fill->pad_char = ' ';
    fill->size = all.size;
    char num[16];
    snprintf(num, sizeof(num), "%6d", line);
    move_text_to_var(ir, num, debug_line);
    move_text_to_var(ir, name, debug_name);
    int k = 0;
    for (AstNode *x = subs; x && k < 3; x = x->next, k++) {
        AstNode *d = item_ref(debug_sub[k], x->loc);
        emit(ir, IR_SIZE_ERR_CLEAR);
        lower_assign(ir, x, d, false);
        ast_free(d);
    }
    FieldRef dc = var_field(debug_contents);
    if (contents) emit_move_alpha(ir, &dc, contents->label, contents->size);
    else if (text && *text) move_text_to_var(ir, text, debug_contents);
    IrInstr *p = emit(ir, IR_PERFORM);
    p->imm = debug_uses[u].first;
    p->proc_end = debug_uses[u].last;
    emit_label(ir, off);
}

static bool proc_declarative[MAX_PROCS];
static int proc_line[MAX_PROCS];
static SourceLocation proc_loc[MAX_PROCS];
static SourceLocation proc_loc_of(int p) { return proc_loc[p]; }

// Debugging procedures for procedure p: a bit per debug use
static unsigned debug_uses_of_proc(int p) {
    if (p < 0 || p >= num_procs || proc_declarative[p]) return 0;
    unsigned m = 0;
    for (int u = 0; u < num_debug_uses; u++) {
        AstNode *use = debug_uses[u].use;
        if (debug_uses[u].first < 0) continue;
        if (use->text && strcmp(use->text, "ALL") == 0) { m |= 1u << u; continue; }
        for (AstNode *t = use->children; t; t = t->next)
            if (!t->is_signed && strcasecmp(t->name, procs[p].name) == 0) m |= 1u << u;
    }
    return m;
}

// Debugging procedures for a USE procedure (a declarative section that is
// not a debugging one) that a USE runs
static unsigned debug_uses_of_use_proc(int p) {
    if (p < 0 || p >= num_procs) return 0;
    for (int u = 0; u < num_debug_uses; u++)
        if (debug_uses[u].first == p) return 0;
    unsigned m = 0;
    for (int u = 0; u < num_debug_uses; u++) {
        AstNode *use = debug_uses[u].use;
        if (debug_uses[u].first < 0) continue;
        if (use->text && strcmp(use->text, "ALL") == 0) { m |= 1u << u; continue; }
        for (AstNode *t = use->children; t; t = t->next)
            if (!t->is_signed && strcasecmp(t->name, procs[p].name) == 0) m |= 1u << u;
    }
    return m;
}

// Insert the debugging procedures for the procedures they name: before a
// GO TO, PERFORM or ALTER of one, and where control falls into one (or the
// program starts with it)
static void insert_procedure_debugging(IrProgram *ir) {
    IrInstr *prev = NULL;
    int current = -1;
    for (IrInstr *i = ir->head; i; prev = i, i = i->next) {
        int target = -1;
        const char *text = "";
        bool start = false, use_proc = false;
        if (i->op == IR_PROC_ENTRY) {
            target = (int)i->imm;
            start = current < 0;
            if (current >= 0 && proc_declarative[current] && !proc_declarative[target]) target = -1;
            text = start ? "START PROGRAM" : "FALL THROUGH";
        } else if (current >= 0 && proc_declarative[current]) {
            continue;
        } else if (i->op == IR_GOTO) {
            target = (int)i->imm;
            if (current < 0) text = "START PROGRAM";
        } else if (i->op == IR_PERFORM) {
            target = (int)i->imm;
            static const char *kinds[] = { "PERFORM LOOP", "SORT INPUT", "SORT OUTPUT", "MERGE OUTPUT",
                                           "USE PROCEDURE" };
            text = kinds[i->digits >= 0 && i->digits <= 4 ? i->digits : 0];
            if (i->digits == 4) use_proc = true;
        } else if (i->op == IR_GOTO_ALTERABLE) {
            // The target is known at run time: a test for each target that
            // has a debugging procedure (the first one, and every ALTER's)
            IrProgram tmp = {0};
            int targets[64], nt = 0;
            if (i->proc_end >= 0) targets[nt++] = i->proc_end;
            for (IrInstr *a = ir->head; a; a = a->next)
                if (a->op == IR_ALTER && a->imm == i->imm && a->proc_end >= 0 && nt < 64) targets[nt++] = a->proc_end;
            for (int t = 0; t < nt; t++) {
                bool dup = false;
                for (int j = 0; j < t; j++) dup |= targets[j] == targets[t];
                unsigned m = dup ? 0 : debug_uses_of_proc(targets[t]);
                if (!m) continue;
                char skip[32];
                new_label(skip, sizeof(skip), "debug_skip");
                IrInstr *j = emit(&tmp, IR_ALTER_SLOT_JUMP);
                j->imm = i->imm;
                j->proc_end = targets[t];
                j->jump_target = strdup(skip);
                for (int u = 0; u < num_debug_uses; u++)
                    if (m & (1u << u)) emit_debug_call(&tmp, u, procs[targets[t]].name, proc_line[targets[t]], "", NULL, NULL);
                emit_label(&tmp, skip);
            }
            if (tmp.head) {
                if (prev) prev->next = tmp.head;
                else ir->head = tmp.head;
                tmp.tail->next = i;
                prev = tmp.tail;
            }
            continue;
        } else if (i->op == IR_ALTER) {
            target = (int)i->imm;
            text = i->proc_end >= 0 && i->proc_end < num_procs ? procs[i->proc_end].name : "";
        }
        if (i->op == IR_PROC_ENTRY) current = (int)i->imm;
        unsigned m = use_proc ? debug_uses_of_use_proc(target) : debug_uses_of_proc(target);
        if (!m) continue;
        IrProgram tmp = {0};
        for (int u = 0; u < num_debug_uses; u++)
            if (m & (1u << u)) emit_debug_call(&tmp, u, procs[target].name, proc_line[target], text, NULL, NULL);
        if (!tmp.head) continue;
        if (prev) prev->next = tmp.head;
        else ir->head = tmp.head;
        tmp.tail->next = i;
        prev = tmp.tail;
    }
}

// Identifiers a statement refers to (not those of statements inside it)
static void statement_refs(AstNode *n, AstNode **out, int *count, int max, bool top) {
    for (; n; n = n->next) {
        if (*count >= max) return;
        if (!top && (n->kind == AST_THEN_BLOCK || n->kind == AST_ELSE_BLOCK || n->kind == AST_SENTENCE ||
                     n->kind == AST_WHEN)) {
            if (n->kind == AST_WHEN) statement_refs(n->extra, out, count, max, false);
            continue;
        }
        if (n->kind == AST_IDENTIFIER) {
            out[(*count)++] = n;
            statement_refs(n->children, out, count, max, false);
            continue;
        }
        statement_refs(n->children, out, count, max, false);
        statement_refs(n->extra, out, count, max, false);
        statement_refs(n->loop, out, count, max, false);
        if (top) break;
    }
}

static bool debugging_here(void) {
    return debugging_mode && num_debug_uses && current_proc >= 0 && !proc_declarative[current_proc];
}

// After an I-O statement on file f (before its AT END / INVALID KEY
// phrases): the debugging procedures for the file
static void debug_file_event(IrProgram *ir, AstNode *stmt, int f) {
    if (!debugging_here() || f < 0) return;
    // (OPEN, CLOSE, READ, DELETE, START and RETURN: a WRITE or REWRITE is a
    // reference to its record)
    if (stmt->kind == AST_STATEMENT_WRITE || stmt->kind == AST_STATEMENT_REWRITE) return;
    for (int u = 0; u < num_debug_uses; u++) {
        if (debug_uses[u].first < 0) continue;
        for (AstNode *t = debug_uses[u].use->children; t; t = t->next) {
            if (strcasecmp(t->name, files[f].name) != 0) continue;
            bool read = stmt->kind == AST_STATEMENT_READ || stmt->kind == AST_STATEMENT_RETURN;
            FieldRef rec;
            if (read && files[f].largest_record >= 0) {
                rec = var_field(files[f].largest_record);
                if (rec.size > DEBUG_CONTENTS_SIZE) rec.size = DEBUG_CONTENTS_SIZE;
            }
            // (READ: only when a record was read)
            char skip[32];
            new_label(skip, sizeof(skip), "debug_file");
            if (read) emit_io_jump(ir, 10, 99, skip);
            emit_debug_call(ir, u, files[f].name, stmt->loc.line, "", read ? &rec : NULL, NULL);
            emit_label(ir, skip);
        }
    }
}

// The debugging procedures for item reference ref (a VARYING item that
// changes)
static void debug_item_event(IrProgram *ir, AstNode *ref, int line) {
    if (!debugging_here() || ref->kind != AST_IDENTIFIER) return;
    VarInfo *rv = find_item(ref, false);
    if (!rv) return;
    for (int u = 0; u < num_debug_uses; u++) {
        if (debug_uses[u].first < 0) continue;
        for (AstNode *t = debug_uses[u].use->children; t; t = t->next) {
            if (find_item(t, false) != rv) continue;
            FieldRef c;
            bool have = resolve_ref(ir, ref, &c) && c.size > 0;
            if (have && c.size > DEBUG_CONTENTS_SIZE) c.size = DEBUG_CONTENTS_SIZE;
            emit_debug_call(ir, u, ref->name, line, "", have ? &c : NULL, ref->children);
            break;
        }
    }
}

// An operand of an arithmetic statement that is only a sending item (not a
// receiver): for USE FOR DEBUGGING without ALL REFERENCES it does not count
static bool arith_source(const AstNode *stmt, const AstNode *r) {
    int first_receiver;
    bool giving = stmt->is_numeric;
    switch (stmt->kind) {
        case AST_STATEMENT_ADD:      first_receiver = stmt->decimal_places; break;
        case AST_STATEMENT_SUBTRACT: first_receiver = stmt->decimal_places + (giving ? 1 : 0); break;
        case AST_STATEMENT_MULTIPLY:
        case AST_STATEMENT_DIVIDE:   first_receiver = giving ? 2 : 1; break;
        case AST_STATEMENT_COMPUTE:  first_receiver = 1; break;
        default: return false;
    }
    if (stmt->name && strcmp(stmt->name, "CORR") == 0) return false;
    int k = 0;
    for (const AstNode *c = stmt->children; c; c = c->next, k++)
        if (c == r) return k < first_receiver;
    return true;            // inside an expression or a subscript: sent
}

// After a statement: the debugging procedures for the items it refers to
static void debug_after_statement(IrProgram *ir, AstNode *stmt) {
    if (!debugging_here()) return;
    if (stmt->kind == AST_STATEMENT_PERFORM) return;    // (its VARYING items: lower_varying_set)
    // Statements that only read the items they name: for ALL REFERENCES only
    bool reads_only = stmt->kind == AST_STATEMENT_GOTO || stmt->kind == AST_STATEMENT_IF ||
                      stmt->kind == AST_STATEMENT_EVALUATE || stmt->kind == AST_STATEMENT_DISPLAY ||
                      stmt->kind == AST_STATEMENT_SEARCH;
    AstNode *refs[256];
    int n = 0;
    statement_refs(stmt, refs, &n, 256, true);
    for (int u = 0; u < num_debug_uses; u++) {
        if (debug_uses[u].first < 0) continue;
        for (AstNode *t = debug_uses[u].use->children; t; t = t->next) {
            bool is_file = false;
            for (int f = 0; f < num_files; f++)
                if (strcasecmp(files[f].name, t->name) == 0) is_file = true;
            if (is_file) continue;              // debug_file_event
            VarInfo *tv = find_item(t, false);
            if (!tv || tv->is_cond || (reads_only && !t->is_signed)) continue;
            for (int k = 0; k < n; k++) {
                AstNode *r = refs[k];
                if (r->name[0] == '!' || r->name[0] == '#') continue;
                if (is_file) {
                    if (strcasecmp(r->name, t->name) != 0) continue;
                    FieldRef rec;
                    bool read = stmt->kind == AST_STATEMENT_READ;
                    int f = -1;
                    for (int j = 0; j < num_files; j++)
                        if (strcasecmp(files[j].name, t->name) == 0) f = j;
                    if (read && f >= 0 && files[f].largest_record >= 0) {
                        rec = var_field(files[f].largest_record);
                        if (rec.size > DEBUG_CONTENTS_SIZE) rec.size = DEBUG_CONTENTS_SIZE;
                    }
                    emit_debug_call(ir, u, t->name, stmt->loc.line, "", read && f >= 0 ? &rec : NULL, NULL);
                    break;
                }
                if (find_item(r, false) != tv) continue;
                // MOVE and arithmetic: without ALL REFERENCES, only as a receiver
                if (!t->is_signed && stmt->kind == AST_STATEMENT_MOVE && r == stmt->children) continue;
                if (!t->is_signed && arith_source(stmt, r)) continue;
                char name[128];
                int len = snprintf(name, sizeof(name), "%s", r->name);
                for (AstNode *q = r->extra; q && len < (int)sizeof(name) - 1; q = q->next)
                    len += snprintf(name + len, sizeof(name) - len, " IN %s", q->name);
                FieldRef c;
                bool have = resolve_ref(ir, r, &c) && c.size > 0;
                if (have && c.size > DEBUG_CONTENTS_SIZE) c.size = DEBUG_CONTENTS_SIZE;
                emit_debug_call(ir, u, name, stmt->loc.line, "", have ? &c : NULL, r->children);
                break;
            }
        }
    }
}

// Segmentation: a section numbered 50 to 99 is an independent segment,
// in its initial state whenever control comes to it from another segment
// (GO TO, PERFORM, or falling into it): its altered GO TO statements go to
// their first targets again
static void reset_independent_segments(IrProgram *ir, AstNode *program) {
    static int segment[MAX_PROCS];
    int n = 0;
    for (AstNode *div = program->children; div; div = div->next) {
        if (div->kind != AST_DIVISION || strcasecmp(div->name, "PROCEDURE") != 0) continue;
        for (AstNode *x = div->children; x; x = x->next) {
            if (x->kind != AST_SECTION && x->kind != AST_PARAGRAPH) continue;
            int seg = x->kind == AST_SECTION && x->decimal_places ? x->digits : 0;
            if (n < MAX_PROCS) segment[n++] = seg;
            if (x->kind == AST_SECTION)
                for (AstNode *c = x->children; c; c = c->next)
                    if (c->kind == AST_PARAGRAPH && n < MAX_PROCS) segment[n++] = seg;
        }
    }
    bool any = false;
    for (int k = 0; k < n; k++) any |= segment[k] >= 50;
    if (!any) return;
    #define SEG(p) ((p) >= 0 && (p) < n ? segment[p] : 0)
    IrInstr *prev = NULL;
    int current = -1;
    for (IrInstr *i = ir->head; i; prev = i, i = i->next) {
        int target = -1, from = SEG(current);
        if (i->op == IR_GOTO || i->op == IR_PERFORM) target = (int)i->imm;
        else if (i->op == IR_PROC_ENTRY) {
            target = (int)i->imm;
            current = target;
            // falling into it: from the procedure before
            from = SEG(target - 1);
        }
        int seg = SEG(target);
        if (target < 0 || seg < 50 || seg == from) continue;
        if (i->op == IR_PROC_ENTRY && (target == 0 || !prev)) continue;
        // the resets, before the transfer
        for (IrInstr *g = ir->head; g; g = g->next) {
            if (g->op != IR_GOTO_ALTERABLE || SEG(g->imm) != seg) continue;
            IrInstr *a = calloc(1, sizeof(IrInstr));
            a->op = IR_ALTER;
            a->imm = g->imm;
            a->proc_end = g->proc_end;
            a->next = i;
            if (prev) prev->next = a;
            else ir->head = a;
            prev = a;
        }
    }
    #undef SEG
}

// Elementary WORKING-STORAGE items without VALUE that statements read but
// none gives a value (-Wuninitialized)
static void check_uninitialized(void) {
    if (!diag_warning_enabled(W_UNINITIALIZED)) return;
    // FILE STATUS items get their values from I-O statements, DEBUG-ITEM
    // from the Debug module
    for (int f = 0; f < num_files; f++)
        if (files[f].status_var >= 0) vars[files[f].status_var].assigned = true;
    if (debug_item >= 0) vars[debug_item].assigned = true;
    // RENAMES (level 66) names storage of other items: not followed
    for (int k = 0; k < num_vars; k++)
        if (vars[k].level == 66)
            for (int i = 0; i < num_vars; i++)
                if (vars[i].record == vars[k].record) vars[i].assigned = true;
    for (int i = 0; i < num_vars; i++) {
        VarInfo *v = &vars[i];
        if (v->assigned || !v->first_read.line || v->is_group || v->is_cond || v->is_index || v->is_filler ||
            v->value || v->linkage || v->owner || !v->decl || v->qual_file >= 0 || v->redefines >= 0) continue;
        bool skip = false;
        for (int a = i; a >= 0 && !skip; a = vars[a].parent) {
            const VarInfo *x = &vars[a];
            if (x->assigned || (x != v && x->value) || x->redefines >= 0 || x->file >= 0 || x->linkage || x->owner ||
                (x->decl && (x->decl->is_external || x->decl->is_global)))
                skip = true;
            for (int k = 0; k < num_vars && !skip; k++)
                if (vars[k].redefines == a) skip = true;
        }
        if (skip) continue;
        if (diag_warning(W_UNINITIALIZED, v->first_read, "'%s' is used, but no statement gives it a value", v->name))
            diag_note(v->decl->loc, "'%s' is declared here without a VALUE clause", v->name);
    }
}

// ── Control flow warnings ────────────────────────────────────────────────────
// Statements after an unconditional transfer of control in the same
// procedure (-Wunreachable), and procedures that are neither named by a
// statement nor entered by falling through (-Wunused-procedure)

static bool proc_ends[MAX_PROCS];             // its last statement transfers control

static bool is_transfer(const AstNode *st, bool subprogram) {
    if (st->kind == AST_STATEMENT_GOTO) return !st->extra;          // not GO TO ... DEPENDING
    if (st->kind == AST_STATEMENT_STOP) return true;                // STOP RUN
    if (st->kind == AST_STATEMENT_EXIT && st->name && strcmp(st->name, "GOBACK") == 0) return true;
    if (st->kind == AST_STATEMENT_EXIT) return subprogram && st->name && strcmp(st->name, "PROGRAM") == 0;
    return false;
}

static const char *transfer_text(const AstNode *st) {
    return st->kind == AST_STATEMENT_GOTO ? "GO TO" : st->kind == AST_STATEMENT_STOP ? "STOP RUN" :
           st->name && strcmp(st->name, "GOBACK") == 0 ? "GOBACK" : "EXIT PROGRAM";
}

// The sentences of one procedure: true if control does not fall out of them
static bool body_ends(AstNode *first, bool subprogram) {
    const AstNode *transfer = NULL;
    bool reported = false;
    for (AstNode *s = first; s; s = s->next) {
        if (s->kind != AST_SENTENCE) continue;
        for (AstNode *st = s->children; st; st = st->next) {
            if (transfer && !reported) {
                reported = true;
                if (diag_warning(W_UNREACHABLE, st->loc, "This statement is never executed"))
                    diag_note(transfer->loc, "control never passes this %s", transfer_text(transfer));
            }
            if (!transfer && is_transfer(st, subprogram)) transfer = st;
        }
    }
    return transfer != NULL;
}

static void check_flow(AstNode *program, bool subprogram) {
    for (AstNode *div = program->children; div; div = div->next) {
        if (div->kind != AST_DIVISION || strcasecmp(div->name, "PROCEDURE") != 0) continue;
        bool falls = !body_ends(div->children, subprogram);     // sentences before the first procedure
        int index = 0;
        for (AstNode *n = div->children; n; n = n->next) {
            if (n->kind != AST_SECTION && n->kind != AST_PARAGRAPH) continue;
            int proc = index++;
            if (proc >= MAX_PROCS) return;
            proc_ends[proc] = body_ends(n->kind == AST_SECTION ? n->children : n->children, subprogram);
            if (n->kind == AST_SECTION)
                for (AstNode *c = n->children; c; c = c->next)
                    if (c->kind == AST_PARAGRAPH && index < MAX_PROCS) proc_ends[index++] = body_ends(c->children, subprogram);
        }
        bool started = false;
        for (int p = 0; p < num_procs && p < index; p++) {
            if (proc_declarative[p]) continue;
            if (!started) { started = true; falls = falls || p == 0; }
            bool reached = proc_referenced[p] || falls;
            // A section's own sentences: report the section only when it has some
            bool has_body = false;
            if (!reached) {
                int k = 0;
                for (AstNode *n = div->children; n && !has_body; n = n->next) {
                    if (n->kind != AST_SECTION && n->kind != AST_PARAGRAPH) continue;
                    if (k == p) {
                        if (n->kind == AST_PARAGRAPH) has_body = true;
                        else for (AstNode *c = n->children; c; c = c->next) has_body |= c->kind == AST_SENTENCE;
                        break;
                    }
                    k++;
                    if (n->kind == AST_SECTION)
                        for (AstNode *c = n->children; c; c = c->next)
                            if (c->kind == AST_PARAGRAPH) { if (k == p) { has_body = true; break; } k++; }
                }
                if (has_body)
                    diag_warning(W_UNUSED_PROCEDURE, proc_loc[p], "%s '%s' is never performed and cannot be reached",
                                 procs[p].is_section ? "Section" : "Paragraph", procs[p].name);
            }
            falls = reached && !proc_ends[p];
        }
    }
}

IrProgram *lower_to_ir(AstNode *program, const Dialect *program_dialect, const IrUnit *unit) {
    IrProgram *ir = calloc(1, sizeof(IrProgram));
    if (!ir) return NULL;
    dialect = program_dialect;
    if (dialect->sign_encoding == SIGN_ENCODING_EBCDIC) ir->runtime_flags |= IR_RT_SIGN_EBCDIC;
    if (ir_runtime_checks) ir->runtime_flags |= IR_RT_CHECK_DATA;
    ir->subprogram = !unit->main;
    ir->program_name = strdup(program->name ? program->name : "MAIN");
    where_program = ir->program_name;
    char entry[160], info[160];
    ir_program_symbols(ir->program_name, entry, info, sizeof(entry));
    ir->entry_symbol = strdup(entry);
    ir->info_symbol = strdup(info);
    ir->initial = (program->digits & 2) != 0;

    num_vars = 0;
    names_indexed = 0;
    num_records = 0;
    memset(record_ptr_slot, 0xFF, sizeof(record_ptr_slot));
    for (int r = 0; r < MAX_VARS; r++) {
        free(record_extern[r]); free(record_export[r]); free(record_external[r]);
        record_extern[r] = record_export[r] = record_external[r] = NULL;
    }
    current_unit = unit;
    self_symbol = ir->entry_symbol;
    num_fn_temps = 0;
    memset(record_pending, 0, sizeof(record_pending));
    num_status_fixups = 0;
    lit_counter = 0;
    if_counter = 0;
    addr_slots = 0;
    inspect_counter = 0;

    build_special_names(program);
    collect_uses(program);
    sort_counter = 0;
    environment_division = NULL;
    for (AstNode *div = program->children; div; div = div->next)
        if (div->kind == AST_DIVISION && strcasecmp(div->name, "ENVIRONMENT") == 0) environment_division = div;
    debugging_mode = false;
    for (AstNode *e = environment_division ? environment_division->children : NULL; e; e = e->next)
        if (e->kind == AST_SPECIAL && strcmp(e->name, "DEBUGGING") == 0) debugging_mode = true;
    build_data(ir, program);

    // PROCEDURE DIVISION USING: the address slots of the LINKAGE items
    for (AstNode *div = program->children; div; div = div->next) {
        if (div->kind != AST_DIVISION || strcasecmp(div->name, "PROCEDURE") != 0) continue;
        for (AstNode *u = div->extra; u; u = u->next) {
            VarInfo *v = find_item(u, true);
            if (!v) continue;
            if (!v->linkage || v->parent >= 0 || (v->level != 1 && v->level != 77)) {
                diag_error(u->loc, "USING item '%s' must be a level 01 or 77 LINKAGE SECTION item", v->name);
                continue;
            }
            if (ir->nusing < 64) ir->using_slots[ir->nusing++] = record_ptr_slot[v->record];
        }
        if (div->extra && !ir->subprogram)
            diag_error(div->loc, "The main program %s cannot have PROCEDURE DIVISION USING", ir->program_name);
    }

    // Emit initializations: the main program at start, a subprogram on its
    // first CALL (every CALL for an INITIAL program, the first after CANCEL)
    if (ir->subprogram) {
        IrInstr *e = emit(ir, IR_PROGRAM_ENTRY);
        e->jump_target = strdup("$init_done");
    } else {
        append_instr(ir, new_instr(IR_CALL_INIT, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
    }
    // EXTERNAL records and files: their storage, by name
    for (int r = 0; r < num_records; r++) {
        if (!record_external[r]) continue;
        char lit[32];
        emit_data_lit(ir, "extname", record_external[r], lit, sizeof(lit));
        IrInstr *x = emit(ir, IR_EXTERNAL);
        x->imm = record_ptr_slot[r];
        x->lit_name = strdup(lit);
        x->lit_len = (int)strlen(record_external[r]);
        x->size = record_size[r];
        for (int f = 0; f < num_files; f++)
            if (files[f].fcb_record == r && files[f].external) {
                char tpl[32];
                snprintf(tpl, sizeof(tpl), "$fcbtpl_%d", f);
                x->content = strdup(tpl);
            }
    }
    for (int k = 0; k < num_status_fixups; k++) {
        FileInfo *fi = &files[external_status_fixups[k][0]];
        VarInfo *st = &vars[external_status_fixups[k][1]];
        char dest[64], src[64];
        record_label(dest, sizeof(dest), fi->fcb_record, 40);
        record_label(src, sizeof(src), st->record, st->offset);
        emit_qword(ir, dest, 1, 0, src, 0);
    }
    emit(ir, IR_SIZE_ERR_CLEAR);
    for (int i = 0; i < num_vars; i++) lower_initial_value(ir, i);
    {
        FieldRef di = var_field(debug_item);
        IrInstr *fill = emit(ir, IR_CALL_MOVE_FILL);
        fill->var_name = strdup(di.label);
        fill->pad_char = ' ';
        fill->size = di.size;
    }
    if (ir->subprogram) {
        emit_label(ir, "$init_done");
        emit(ir, IR_RESET_PERFORM);
    }
    if (has_collation) {
        IrInstr *t = emit(ir, IR_DATA_BYTES);
        t->var_name = strdup("$collate");
        t->size = 256;
        char *bytes = malloc(256);
        memcpy(bytes, collate_weight, 256);
        t->content = bytes;
        emit(ir, IR_CALL_SET_COLLATION)->var_name = strdup("$collate");
    }

    rw_setup(program);

    // Lower PROCEDURE DIVISION: procedures laid out in source order
    num_procs = 0;
    memset(proc_referenced, 0, sizeof(proc_referenced));
    current_section = NULL;
    sentence_end = NULL;
    for (AstNode *div = program->children; div; div = div->next) {
        if (div->kind != AST_DIVISION || strcasecmp(div->name, "PROCEDURE") != 0) continue;
        for (AstNode *n = div->children; n; n = n->next) {
            if (n->kind == AST_SECTION || n->kind == AST_PARAGRAPH) {
                if (num_procs >= MAX_PROCS) { diag_error(n->loc, "Too many procedures (%d limit)", MAX_PROCS); break; }
                proc_declarative[num_procs] = n->kind == AST_SECTION && n->is_signed;
                proc_line[num_procs] = n->loc.line;
                proc_loc[num_procs] = n->loc;
                procs[num_procs++] = (ProcInfo){ n->name, NULL, n->kind == AST_SECTION };
            }
            if (n->kind == AST_SECTION) {
                for (AstNode *p = n->children; p; p = p->next) {
                    if (p->kind != AST_PARAGRAPH) continue;
                    if (num_procs >= MAX_PROCS) { diag_error(p->loc, "Too many procedures (%d limit)", MAX_PROCS); break; }
                    proc_declarative[num_procs] = n->is_signed;
                    proc_line[num_procs] = p->loc.line;
                    proc_loc[num_procs] = p->loc;
                    procs[num_procs++] = (ProcInfo){ p->name, n->name, false };
                }
            }
        }

        memset(altered, 0, sizeof(altered));
        mark_altered(div->children);

        // Declarative sections run only through USE; the program starts at
        // the first procedure after END DECLARATIVES
        if (has_declaratives) {
            int k = 0, first_normal = -1;
            for (AstNode *n = div->children; n; n = n->next) {
                if (n->kind != AST_SECTION && n->kind != AST_PARAGRAPH) continue;
                int first = k, last = k;
                if (n->kind == AST_SECTION)
                    for (AstNode *p = n->children; p; p = p->next) if (p->kind == AST_PARAGRAPH) last++;
                for (int u = 0; u < num_uses; u++)
                    if (uses[u].section == n) { uses[u].first = first; uses[u].last = last; }
                for (int u = 0; u < num_debug_uses; u++)
                    if (debug_uses[u].section == n) { debug_uses[u].first = first; debug_uses[u].last = last; }
                if (first_normal < 0 && !(n->kind == AST_SECTION && n->is_signed)) first_normal = first;
                k = last + 1;
            }
            if (first_normal >= 0) emit(ir, IR_GOTO)->imm = first_normal;
            else emit(ir, IR_CALL_EXIT);
        }

        int index = 0;
        for (AstNode *n = div->children; n; n = n->next) {
            if (n->kind == AST_SENTENCE) {
                lower_node(ir, n);
                continue;
            }
            int proc = index++;
            current_proc = proc;
            IrInstr *entry = emit(ir, IR_PROC_ENTRY);
            entry->imm = proc;
            entry->var_name = strdup(n->name);
            current_section = (n->kind == AST_SECTION) ? n->name : NULL;
            exit_sec = n->kind == AST_SECTION ? proc : -1;
            exit_para = n->kind == AST_PARAGRAPH ? proc : -1;
            exit_sec_used = exit_para_used = false;
            for (AstNode *c = n->children; c; c = c->next) {
                if (c->kind != AST_PARAGRAPH) {
                    lower_node(ir, c);
                    continue;
                }
                int para = index++;
                current_proc = para;
                IrInstr *pe = emit(ir, IR_PROC_ENTRY);
                pe->imm = para;
                pe->var_name = strdup(c->name);
                exit_para = para;
                exit_para_used = false;
                lower_block(ir, c->children);
                if (exit_para_used) {
                    char lbl[48];
                    snprintf(lbl, sizeof(lbl), "$exit_para_%d", para);
                    emit_label(ir, lbl);
                }
                emit(ir, IR_PROC_EXIT)->imm = para;
            }
            if (exit_para_used && n->kind == AST_PARAGRAPH) {
                char lbl[48];
                snprintf(lbl, sizeof(lbl), "$exit_para_%d", proc);
                emit_label(ir, lbl);
            }
            if (exit_sec_used) {
                char lbl[48];
                snprintf(lbl, sizeof(lbl), "$exit_sec_%d", proc);
                emit_label(ir, lbl);
            }
            emit(ir, IR_PROC_EXIT)->imm = proc;
            exit_sec = exit_para = -1;
            current_section = NULL;
        }
    }
    // Falling off the end of the PROCEDURE DIVISION ends the run unit, or
    // returns from a called program
    if (ir->subprogram) emit(ir, IR_EXIT_PROGRAM);
    else append_instr(ir, new_instr(IR_CALL_EXIT, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
    // USE GLOBAL sections: an entry each, for the programs this one contains
    for (int k = 0; k < num_uses; k++) {
        if (uses[k].global_index < 0 || uses[k].symbol || uses[k].first < 0) continue;
        bool done = false;
        for (int j = 0; j < k; j++) done |= uses[j].section == uses[k].section && !uses[j].symbol;
        if (done) continue;
        char sym[200];
        snprintf(sym, sizeof(sym), "cobuse_%s_%d", ir->entry_symbol + 4, uses[k].global_index);
        IrInstr *e = emit(ir, IR_USE_ENTRY);
        e->var_name = strdup(sym);
        emit_perform_use(ir, k);
        emit(ir, IR_USE_RETURN);
    }
    ir->addr_slots = addr_slots;
    current_proc = -1;
    free_args();
    // The temporary records of function values and arguments
    for (int r = 0; r < num_records; r++) {
        if (!record_pending[r]) continue;
        char label[32];
        snprintf(label, sizeof(label), "%%r%d", r);
        append_instr(ir, new_instr(IR_DATA_VAR, label, NULL, NULL, 0, record_size[r], 0, 0, USAGE_DISPLAY));
    }

    reset_independent_segments(ir, program);
    if (debugging_mode && num_debug_uses) insert_procedure_debugging(ir);

    check_flow(program, ir->subprogram);
    check_uninitialized();
    // Files no statement names (shared files may be used by other programs)
    for (int f = 0; f < num_files; f++) {
        if (files[f].referenced || files[f].owner || files[f].external || !files[f].select) continue;
        if (files[f].fd && files[f].fd->is_global) continue;
        diag_warning(W_UNUSED_FILE, files[f].select->loc, "File '%s' is never opened or used", files[f].name);
    }

    // An altered paragraph must consist of a GO TO statement
    for (int p = 0; p < num_procs; p++) {
        if (!altered[p]) continue;
        bool has_goto = false;
        for (IrInstr *i = ir->head; i && !has_goto; i = i->next)
            has_goto = i->op == IR_GOTO_ALTERABLE && i->imm == p;
        if (!has_goto)
            diag_error(proc_loc[p], "Paragraph '%s' is named in an ALTER statement, so it must consist of a single GO TO statement", procs[p].name);
    }

    return ir;
}

void ir_free(IrProgram *ir) {
    if (!ir) return;
    IrInstr *i = ir->head;
    while (i) {
        IrInstr *next = i->next;
        free((void*)i->var_name);
        free((void*)i->lit_name);
        free((void*)i->content);
        free((void*)i->cmp_op);
        free((void*)i->jump_target);
        free(i);
        i = next;
    }
    free(ir->program_name);
    free(ir->entry_symbol);
    free(ir->info_symbol);
    for (int i = 0; i < num_vars; i++) {
        free(vars[i].name);
    }
    free(ir);
}
