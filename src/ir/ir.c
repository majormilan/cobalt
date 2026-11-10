/* src/ir/ir.c */
#include "ir.h"
#include "ast.h"
#include "diag.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
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
} VarInfo;

static bool is_num_edited(const VarInfo *v) { return v->mask && v->num_edited; }
static bool is_alnum_edited(const VarInfo *v) { return v->mask && !v->num_edited; }

#define MAX_VARS 4096
static VarInfo vars[MAX_VARS];
static int num_vars = 0;
static int record_size[MAX_VARS];
static int num_records = 0;
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
} FileInfo;

#define FILE_LINE     1     // runtime FCB flags (runtime/ABI.md section 7)
#define FILE_OPTIONAL 2

static FileInfo files[64];
static int num_files = 0;

// Procedures (sections and paragraphs) in source order
typedef struct {
    const char *name;
    const char *section;    // enclosing section of a paragraph, or NULL
    bool is_section;
} ProcInfo;

static ProcInfo procs[1024];
static int num_procs = 0;
static const char *current_section = NULL;   // during lowering
static int current_proc = -1;                // procedure being lowered
static bool altered[1024];                   // paragraphs named in an ALTER statement
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
                cobalt_error_loc(NULL, e->loc.line, e->loc.column,
                                 "Switch '%s' is not SWITCH-1 to SWITCH-9", e->text);
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
                cobalt_error_loc(NULL, e->loc.line, e->loc.column, "Undeclared alphabet '%s'", e->text);
                continue;
            }
            if (alpha->children) alphabet_weights(alpha, weights);
        }
        for (AstNode *n = e->children; n; n = n->next) {
            int ch = -1;
            for (int c = 0; c < 256 && ch < 0; c++)
                if (n->digits >= 1 && weights[c] == n->digits - 1) ch = c;
            if (ch < 0) {
                cobalt_error_loc(NULL, n->loc.line, n->loc.column,
                                 "Ordinal position %d of symbolic character '%s' is out of range", n->digits, n->name);
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
        cobalt_error_loc(NULL, decl->loc.line, decl->loc.column,
                         "Invalid RENAMES in '%s': the items must be in the record before it, "
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
            if (sel->kind != AST_SELECT || num_files >= 64) continue;
            files[num_files++] = (FileInfo){ sel->name, sel, -1, -1 };
        }
    }

    // Record descriptions: WORKING-STORAGE entries, and the entries of every
    // FD in the FILE SECTION (decl_file tells which file, or -1)
    AstNode *decls[MAX_VARS];
    int decl_file[MAX_VARS];
    int ndecls = 0;
    for (AstNode *div = program->children; div; div = div->next) {
        if (div->kind != AST_DIVISION || strcasecmp(div->name, "DATA") != 0) continue;
        for (AstNode *sec = div->children; sec; sec = sec->next) {
            if (sec->kind != AST_SECTION) continue;
            for (AstNode *entry = sec->children; entry; entry = entry->next) {
                if (entry->kind == AST_FILE_DECL) {
                    int f = -1;
                    for (int k = 0; k < num_files; k++)
                        if (strcasecmp(files[k].name, entry->name) == 0) f = k;
                    if (f < 0)
                        cobalt_error_loc(NULL, entry->loc.line, entry->loc.column,
                                         "FD '%s' has no SELECT", entry->name);
                    for (AstNode *d = entry->children; d && ndecls < MAX_VARS; d = d->next) {
                        decls[ndecls] = d;
                        decl_file[ndecls++] = f;
                    }
                } else if (ndecls < MAX_VARS) {
                    decls[ndecls] = entry;
                    decl_file[ndecls++] = -1;
                }
            }
        }
    }

    {
        {
            for (int di = 0; di < ndecls; di++) {
                AstNode *decl = decls[di];
                int file = decl_file[di];
                if (decl->kind != AST_VAR_DECL) continue;
                int level = decl->text ? atoi(decl->text) : 1;

                if (level == 88) {
                    if (last_item < 0) {
                        cobalt_error_loc(NULL, decl->loc.line, decl->loc.column,
                                         "Condition-name '%s' has no data item", decl->name);
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
                        cobalt_error_loc(NULL, decl->loc.line, decl->loc.column,
                                         "RENAMES entry '%s' does not follow a record", decl->name);
                    } else if (num_renames < 256) {
                        renames_record[num_renames] = record;
                        renames[num_renames++] = decl;
                    }
                    continue;
                }
                if (level != 1 && level != 77 && (level < 2 || level > 49)) {
                    cobalt_error_loc(NULL, decl->loc.line, decl->loc.column,
                                     "Level %d is not supported", level);
                    continue;
                }

                VarInfo *v = new_var(decl->name);
                if (!v) return;
                int idx = (int)(v - vars);
                last_item = idx;
                v->level = level;
                v->decl = decl;
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
                            cobalt_error_loc(NULL, decl->loc.line, decl->loc.column,
                                             "Level 77 is not allowed in an FD");
                        if (files[file].first_record < 0) files[file].first_record = idx;
                        else v->redefines = files[file].first_record;
                    }
                } else {
                    while (depth > 0 && vars[stack[depth - 1]].level >= level) depth--;
                    if (depth == 0) {
                        cobalt_error_loc(NULL, decl->loc.line, decl->loc.column,
                                         "Level %02d item '%s' is not inside a group", level, decl->name);
                        continue;
                    }
                    v->parent = stack[depth - 1];
                    if (!vars[v->parent].is_group) {
                        cobalt_error_loc(NULL, decl->loc.line, decl->loc.column,
                                         "'%s' has a PICTURE, so it cannot contain '%s'",
                                         vars[v->parent].name, decl->name);
                    }
                    add_child(v->parent, idx);
                }
                if (depth < 64) stack[depth++] = idx;

                AstNode *redef = find_clause(decl, AST_REDEFINES);
                if (redef) {
                    v->redefines = find_redefined(redef->name, v->parent, idx);
                    if (v->redefines < 0)
                        cobalt_error_loc(NULL, decl->loc.line, decl->loc.column,
                                         "REDEFINES target '%s' is not the preceding item at this level", redef->name);
                }

                AstNode *occurs = find_clause(decl, AST_OCCURS);
                if (occurs) {
                    v->occurs = atoi(occurs->name);
                    v->depending = occurs->extra;
                    if (v->depending && (occurs->digits < 0 || occurs->digits >= v->occurs))
                        cobalt_error_loc(NULL, decl->loc.line, decl->loc.column,
                                         "OCCURS %d TO %d is not a valid range", occurs->digits, v->occurs);
                    if (v->occurs < 1 || level == 1 || level == 77) {
                        cobalt_error_loc(NULL, decl->loc.line, decl->loc.column,
                                         "Invalid OCCURS on '%s'", decl->name);
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
                cobalt_error_loc(NULL, v->decl->loc.line, v->decl->loc.column,
                                 "SIGN clause requires a signed numeric DISPLAY item ('%s')", v->name);
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
            cobalt_error_loc(NULL, v->decl->loc.line, v->decl->loc.column,
                             "'%s' needs a PICTURE clause (it has no subordinate items)", v->name);
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
    }
    for (int k = 0; k < num_renames; k++) build_renames(renames[k], renames_record[k]);
    for (int r = 0; r < num_records; r++) {
        char label[32];
        snprintf(label, sizeof(label), "%%r%d", r);
        append_instr(ir, new_instr(IR_DATA_VAR, label, NULL, NULL, 0,
                                   record_size[r] > 0 ? record_size[r] : 1, 0, 0, USAGE_DISPLAY));
    }

    // File control blocks
    for (int f = 0; f < num_files; f++) {
        FileInfo *fi = &files[f];
        if (fi->first_record < 0) {
            cobalt_error_loc(NULL, fi->select->loc.line, fi->select->loc.column,
                             "File '%s' has no FD with a record description", fi->name);
            continue;
        }
        fi->largest_record = fi->first_record;
        for (int i = 0; i < num_vars; i++)
            if (vars[i].file == f && vars[i].parent < 0 && vars[i].size > vars[fi->largest_record].size)
                fi->largest_record = i;
        char fcb[16], name_lit[32];
        snprintf(fcb, sizeof(fcb), "%%f%d", f);
        emit_data_lit(ir, "fname", fi->select->text, name_lit, sizeof(name_lit));
        IrInstr *d = new_instr(IR_DATA_FILE, fcb, name_lit, NULL, 0,
                               record_size[vars[fi->first_record].record], 0, 0, USAGE_DISPLAY);
        d->flags = (fi->select->decimal_places == 1 ? FILE_LINE : 0) |
                   (fi->select->is_signed ? FILE_OPTIONAL : 0);
        if (fi->select->extra) {
            VarInfo *st = find_item(fi->select->extra, true);
            if (st && (st->is_numeric || st->size != 2))
                cobalt_error_loc(NULL, fi->select->extra->loc.line, fi->select->extra->loc.column,
                                 "FILE STATUS item '%s' must be a two-character alphanumeric item", st->name);
            else if (st) {
                char label[48];
                snprintf(label, sizeof(label), "%%r%d+%d", st->record, st->offset);
                d->content = strdup(label);
            }
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

// Find the data item an identifier refers to, applying OF/IN qualification.
// Reports unknown and ambiguous names when report is set.
static VarInfo *find_item(const AstNode *ref, bool report) {
    if (ref->name[0] == '#') return &vars[atoi(ref->name + 1)];
    VarInfo *found = NULL;
    int count = 0;
    for (int i = 0; i < num_vars; i++) {
        if (vars[i].is_filler || strcasecmp(vars[i].name, ref->name) != 0) continue;
        int a = vars[i].parent;
        bool ok = true;
        for (const AstNode *q = ref->extra; q && ok; q = q->next) {
            while (a >= 0 && strcasecmp(vars[a].name, q->name) != 0) a = vars[a].parent;
            if (a < 0) ok = false;
            else a = vars[a].parent;
        }
        if (!ok) continue;
        if (!found) found = &vars[i];
        count++;
    }
    if (report && !found)
        cobalt_error_loc(NULL, ref->loc.line, ref->loc.column, "Undeclared identifier '%s'", ref->name);
    else if (report && count > 1)
        cobalt_error_loc(NULL, ref->loc.line, ref->loc.column,
                         "'%s' is ambiguous; qualify it with OF or IN", ref->name);
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
static int odo_length(IrProgram *ir, const VarInfo *v, const VarInfo *t) {
    VarInfo *dv = find_item(t->depending, true);
    if (resolving_receiver && dv)
        for (int a = dv->parent; a >= 0; a = vars[a].parent)
            if (&vars[a] == v) return v->size;
    if (!dv || !dv->is_numeric || dv->decimal_places > 0) {
        if (dv) cobalt_error_loc(NULL, t->depending->loc.line, t->depending->loc.column,
                                 "DEPENDING ON item '%s' must be an integer", dv->name);
        return v->size;
    }
    int slot = addr_slots++;
    push_int(ir, t->depending);
    emit_num(ir, IR_NUM_CONST, t->size, 0);
    emit_num(ir, IR_NUM_MUL, 0, 0);
    emit_num(ir, IR_NUM_CONST, t->offset - v->offset, 0);
    emit_num(ir, IR_NUM_ADD, 0, 0);
    emit(ir, IR_LEN_SET)->imm = slot;
    return -(slot + 1);
}

static bool resolve_ref(IrProgram *ir, AstNode *ref, FieldRef *out) {
    memset(out, 0, sizeof(*out));
    VarInfo *v = find_item(ref, true);
    if (!v) return false;
    if (v->is_cond) {
        cobalt_error_loc(NULL, ref->loc.line, ref->loc.column, "Condition-name '%s' is not a data item", ref->name);
        return false;
    }
    out->var = v;

    int counts[16], strides[16];
    int ndims = item_dims(v, counts, strides);
    int nsubs = 0;
    for (AstNode *s = ref->children; s; s = s->next) nsubs++;
    if (nsubs != ndims) {
        cobalt_error_loc(NULL, ref->loc.line, ref->loc.column,
                         "'%s' needs %d subscript%s, %d given", ref->name, ndims, ndims == 1 ? "" : "s", nsubs);
        return false;
    }

    int offset = v->offset;
    bool dynamic = false;
    int k = 0;
    for (AstNode *s = ref->children; s; s = s->next, k++) {
        long long n;
        if (is_int_literal(s, &n)) {
            if (n < 1 || n > counts[k]) {
                cobalt_error_loc(NULL, s->loc.line, s->loc.column,
                                 "Subscript %lld of '%s' is out of range 1 to %d", n, ref->name, counts[k]);
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
            cobalt_error_loc(NULL, start->loc.line, start->loc.column, "Reference modifier start is out of range");
            return false;
        }
        if (length && !is_int_literal(length, &len_val)) {
            if (!num_expr_valid(length)) return false;
            len_const = false;
            dynamic = true;
        } else if (length && (len_val < 1 || (start_const && start_val + len_val - 1 > v->size))) {
            cobalt_error_loc(NULL, length->loc.line, length->loc.column, "Reference modifier length is out of range");
            return false;
        }
        if (start_const) offset += (int)start_val - 1;
        if (start_const && len_const) size = length ? (int)len_val : v->size - (int)start_val + 1;
        else if (len_const && length) size = (int)len_val;
    }

    // A group holding a table with OCCURS DEPENDING ON: its length follows
    // the current number of occurrences (the table ends the group)
    VarInfo *odo = ref->loop ? NULL : odo_table_in(v);
    if (!dynamic) {
        snprintf(out->label, sizeof(out->label), "%%r%d+%d", v->record, offset);
        out->size = odo ? odo_length(ir, v, odo) : size;
        return true;
    }

    int slot = addr_slots++;
    char base[32];
    snprintf(base, sizeof(base), "%%r%d+%d", v->record, offset);
    IrInstr *set = emit(ir, IR_ADDR_SET);
    set->imm = slot;
    set->var_name = strdup(base);
    k = 0;
    for (AstNode *s = ref->children; s; s = s->next, k++) {
        long long n;
        if (is_int_literal(s, &n)) continue;
        push_int(ir, s);
        IrInstr *add = emit(ir, IR_ADDR_ADD);
        add->imm = slot;
        add->size = strides[k];
    }
    if (ref->loop && !start_const) {
        push_int(ir, start);
        IrInstr *add = emit(ir, IR_ADDR_ADD);
        add->imm = slot;
        add->size = 1;
    }
    if (ref->loop && !(start_const && len_const) && !(len_const && length)) {
        // Length known only at run time
        if (length) {
            push_int(ir, length);
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
        cobalt_error_loc(NULL, n->loc.line, n->loc.column, "%s '%s' is not numeric", role, n->name);
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
                cobalt_error_loc(NULL, e->loc.line, e->loc.column, "Invalid numeric literal '%s'", e->name);
                return false;
            }
            return true;
        case AST_FIGURATIVE_CONSTANT:
            if (figurative_char(e) == '0') return true;
            cobalt_error_loc(NULL, e->loc.line, e->loc.column, "%s is not numeric", e->name);
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
            return left && right;
        }
        default:
            cobalt_error_loc(NULL, e->loc.line, e->loc.column, "Invalid operand in arithmetic expression");
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
            int p = sl + sr, shift = p > W ? p - W : 0;
            if (ir) emit_num(ir, IR_NUM_MUL, shift, 0);
            return p - shift;
        }
        case AST_EXPR_DIV: {
            AstNode *l = e->children, *r = l->next;
            int sl = num_expr(NULL, l, W), sr = num_expr(NULL, r, W);
            int shift = W - sl + sr;
            if (ir) {
                num_expr(ir, l, W);
                if (shift < 0) emit_rescale(ir, shift, false);
                num_expr(ir, r, W);
                emit_num(ir, IR_NUM_DIV, shift < 0 ? 0 : shift, 0);
            }
            return W;
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
        cobalt_error_loc(NULL, n->loc.line, n->loc.column, "Receiving item '%s' is not numeric", n->name);
        return NULL;
    }
    return v;
}

static void lower_assign(IrProgram *ir, AstNode *tree, AstNode *receiver, bool keep) {
    VarInfo *t = receiving_item(receiver);
    if (!t || !num_expr_valid(tree)) return;
    int s = num_expr(ir, tree, working_scale(tree, t));
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

// Resolve a procedure name: a paragraph of the current section first, then a
// section or a paragraph whose name is unique in the program.
static int resolve_proc(AstNode *ref) {
    if (ref->extra) {
        // paragraph OF section
        for (int i = 0; i < num_procs; i++) {
            if (!procs[i].is_section && procs[i].section &&
                strcasecmp(procs[i].section, ref->extra->name) == 0 &&
                strcasecmp(procs[i].name, ref->name) == 0) return i;
        }
        cobalt_error_loc(NULL, ref->loc.line, ref->loc.column, "Procedure '%s' not found in section '%s'",
                         ref->name, ref->extra->name);
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
        cobalt_error_loc(NULL, ref->loc.line, ref->loc.column, "Procedure '%s' not found", ref->name);
    } else if (count > 1) {
        cobalt_error_loc(NULL, ref->loc.line, ref->loc.column,
                         "Procedure name '%s' is ambiguous (qualify it with OF section)", ref->name);
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
    if (!num_expr_valid(tree)) return;
    int s = num_expr(ir, tree, working_scale_all(tree, recv, n));
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
        cobalt_error_loc(NULL, node->loc.line, node->loc.column, "CORRESPONDING requires two group items");
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
    cobalt_error_loc(NULL, n->loc.line, n->loc.column,
                     "An arithmetic expression cannot be compared with a nonnumeric operand");
    return false;
}

// Jump to target when (left op right) is false
static void emit_relation_false_jump(IrProgram *ir, AstNode *left, AstNode *right,
                                     const char *op, const char *target) {
    int fl = figurative_char(left), fr = figurative_char(right);
    IrInstr *jump = NULL;
    if ((is_numeric_operand(left) || fl == '0') && (is_numeric_operand(right) || fr == '0') &&
        !(fl == '0' && fr == '0')) {
        // ZERO compared with a numeric operand is the number 0
        if (num_expr_valid(left) && num_expr_valid(right)) {
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
            cobalt_error_loc(NULL, left->loc.line, left->loc.column, "Two figurative constants cannot be compared");
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
        cobalt_error_loc(NULL, operand->loc.line, operand->loc.column,
                         "A class condition requires a data item");
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
            cobalt_error_loc(NULL, c->loc.line, c->loc.column, "'%s' is not a class-name", c->text);
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
                cobalt_error_loc(NULL, cond->loc.line, cond->loc.column, "A sign condition requires a numeric operand");
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
                cobalt_error_loc(NULL, cond->loc.line, cond->loc.column, "'%s' is not a condition-name", cond->name);
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
            emit_cond_jump(ir, tree, target, when);
            ast_free(tree);
            break;
        }
        default:
            cobalt_error_loc(NULL, cond->loc.line, cond->loc.column, "Invalid condition");
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
static void lower_move(IrProgram *ir, AstNode *src, AstNode *dest_node, const char *src_lit) {
    VarInfo *dv = find_item(dest_node, true);
    if (!dv) return;
    bool whole = !dest_node->loop && !dv->is_cond;
    bool dest_numeric = dv->is_numeric && whole;
    bool dest_num_edited = is_num_edited(dv) && whole;
    bool dest_alnum_edited = is_alnum_edited(dv) && whole;
    bool dest_takes_value = dest_numeric || dest_num_edited;
    FieldRef dest;

    int fig = figurative_char(src);
    if (fig != -1) {
        if (dest_numeric && fig != '0') {
            cobalt_error_loc(NULL, src->loc.line, src->loc.column,
                             "%s cannot be moved to numeric '%s'", src->name, dv->name);
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
            cobalt_error_loc(NULL, src->loc.line, src->loc.column,
                             "MOVE of a variable-length item to numeric '%s' is not supported yet", dv->name);
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
}

// The statements of one PERFORM iteration: the procedure range or the inline body
static void lower_perform_body(IrProgram *ir, AstNode *node, int first, int last) {
    if (node->extra) {
        IrInstr *p = emit(ir, IR_PERFORM);
        p->imm = first;
        p->proc_end = last;
    } else {
        lower_block(ir, node->children);
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
                emit_cond_jump(ir, COND(0), done_lbl, true);
            } else {
                char cont[32];
                new_label(cont, sizeof(cont), "vary_cont");
                emit_cond_jump(ir, COND(k), cont, false);
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
            emit_cond_jump(ir, COND(k), next_lbl, true);
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

static void lower_perform(IrProgram *ir, AstNode *node) {
    int first = -1, last = -1;
    if (node->extra) {
        first = resolve_proc(node->extra);
        last = node->extra->next ? resolve_proc(node->extra->next) : first;
        if (first < 0 || last < 0) return;
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
        lower_perform_body(ir, node, first, last);
        IrInstr *end = emit(ir, IR_TIMES_END);
        end->var_name = strdup(loop_lbl);
        end->jump_target = strdup(done_lbl);
    } else if (strcmp(form, "UNTIL") == 0) {
        char loop_lbl[32], done_lbl[32];
        new_label(loop_lbl, sizeof(loop_lbl), "until");
        new_label(done_lbl, sizeof(done_lbl), "until_done");
        emit_label(ir, loop_lbl);
        if (!node->is_signed) emit_cond_jump(ir, node->loop, done_lbl, true);
        lower_perform_body(ir, node, first, last);
        if (node->is_signed) emit_cond_jump(ir, node->loop, done_lbl, true);
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
            cobalt_error_loc(NULL, src->loc.line, src->loc.column,
                             "A nonnumeric literal cannot replace numeric data");
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
            cobalt_error_loc(NULL, src->loc.line, src->loc.column,
                             "A numeric item cannot replace nonnumeric data");
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
                cobalt_error_loc(NULL, recv->loc.line, recv->loc.column,
                                 "'%s' is not the mnemonic-name of a switch", recv->name);
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
        cobalt_error_loc(NULL, table->loc.line, table->loc.column,
                         "SEARCH needs a table with INDEXED BY ('%s')", table->name);
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
        if (strcasecmp(files[f].name, ref->name) == 0) return f;
    cobalt_error_loc(NULL, ref->loc.line, ref->loc.column, "'%s' is not a file", ref->name);
    return -1;
}

static IrInstr *emit_file_op(IrProgram *ir, IrOpcode op, int file) {
    IrInstr *i = emit(ir, op);
    char fcb[16];
    snprintf(fcb, sizeof(fcb), "%%f%d", file);
    i->var_name = strdup(fcb);
    return i;
}

static void emit_io_jump(IrProgram *ir, int low, int high, const char *target) {
    IrInstr *j = emit(ir, IR_IO_JUMP);
    j->imm = low;
    j->size = high;
    j->jump_target = strdup(target);
}

// Branches after an I-O statement: on_error runs for statuses low..high (AT
// END), not_on_error for a successful status (00-09)
static void lower_io_phrases(IrProgram *ir, AstNode *stmt, int low, int high) {
    if (!stmt->on_error && !stmt->not_on_error) return;
    char exc_lbl[32], end_lbl[32];
    new_label(exc_lbl, sizeof(exc_lbl), "io_exception");
    new_label(end_lbl, sizeof(end_lbl), "io_end");
    emit_io_jump(ir, low, high, exc_lbl);
    emit_io_jump(ir, 10, 99, end_lbl);      // other errors run neither phrase
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
        cobalt_error_loc(NULL, rec->loc.line, rec->loc.column, "'%s' is not a record of a file", rec->name);
        return -1;
    }
    return resolve_ref(ir, rec, f) ? v->file : -1;
}

static void lower_file_statement(IrProgram *ir, AstNode *node) {
    switch (node->kind) {
        case AST_STATEMENT_OPEN:
        case AST_STATEMENT_CLOSE:
            for (AstNode *fn = node->children; fn; fn = fn->next) {
                int f = find_file(fn);
                if (f < 0) continue;
                IrInstr *i = emit_file_op(ir, node->kind == AST_STATEMENT_OPEN ? IR_FILE_OPEN : IR_FILE_CLOSE, f);
                i->imm = fn->decimal_places;
            }
            break;
        case AST_STATEMENT_READ: {
            int f = find_file(node->extra);
            if (f < 0) return;
            char area[48];
            snprintf(area, sizeof(area), "%%r%d+0", vars[files[f].first_record].record);
            IrInstr *r = emit_file_op(ir, IR_FILE_READ, f);
            r->lit_name = strdup(area);
            if (node->loop) {
                // READ ... INTO: the record is moved after a successful read
                char skip[32];
                new_label(skip, sizeof(skip), "read_into_skip");
                emit_io_jump(ir, 10, 99, skip);
                AstNode *rec = item_ref(files[f].largest_record, node->loc);
                emit(ir, IR_SIZE_ERR_CLEAR);
                lower_move(ir, rec, node->loop, "");
                ast_free(rec);
                emit_label(ir, skip);
            }
            lower_io_phrases(ir, node, 10, 19);
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
            if (node->kind == AST_STATEMENT_REWRITE) {
                IrInstr *w = emit_file_op(ir, IR_FILE_REWRITE, f);
                w->lit_name = strdup(rf.label);
                w->size = rf.size;
                break;
            }
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
            IrInstr *w = emit_file_op(ir, IR_FILE_WRITE, f);
            w->lit_name = strdup(rf.label);
            w->size = rf.size;
            w->imm = mode;
            w->flags = on_stack ? 1 : 0;
            w->digits = (int)lines;
            break;
        }
        default:
            break;
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
        cobalt_error_loc(NULL, node->loc.line, node->loc.column, "INSPECT requires a DISPLAY item");
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
            cobalt_error_loc(NULL, item->loc.line, item->loc.column,
                             "INSPECT operands of variable length are not supported yet");
            return;
        }
        if (item->on_error &&
            !inspect_operand(ir, item->on_error, repl, sizeof(repl), &rlen, plen)) return;
        if (item->not_on_error && !inspect_operand(ir, item->not_on_error, before, sizeof(before), &blen, 0)) return;
        if (item->children && !inspect_operand(ir, item->children, after, sizeof(after), &alen, 0)) return;
        if (item->on_error && rlen != plen) {
            cobalt_error_loc(NULL, item->loc.line, item->loc.column,
                             "INSPECT replacement must be as long as what it replaces");
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
        cobalt_error_loc(NULL, ref->loc.line, ref->loc.column,
                         "%s '%s' must be an alphanumeric item", role, ref->name);
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
                cobalt_error_loc(NULL, src->loc.line, src->loc.column,
                                 "A numeric STRING operand must be an integer DISPLAY item ('%s')", src->name);
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
            cobalt_error_loc(NULL, r->loc.line, r->loc.column,
                             "A numeric UNSTRING receiving item must be DISPLAY ('%s')", r->name);
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
        // ACCEPT FROM DATE (YYMMDD), DAY (YYDDD), TIME (HHMMSShh), DAY-OF-WEEK (1-7)
        static const struct { const char *name; int kind, digits; } sources[] = {
            { "DATE", 0, 6 }, { "DAY", 1, 5 }, { "TIME", 2, 8 }, { "DAY-OF-WEEK", 3, 1 },
        };
        for (size_t k = 0; k < sizeof(sources) / sizeof(sources[0]); k++) {
            if (strcmp(node->name, sources[k].name) != 0) continue;
            emit(ir, IR_CALL_ACCEPT_DATE)->imm = sources[k].kind;
            move_from_digits(ir, sources[k].digits, var_node);
            return;
        }
    }
    if (!var_node || var_node->kind != AST_IDENTIFIER) {
        cobalt_warn("ACCEPT statement requires a variable", NULL);
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

static void lower_node(IrProgram *ir, AstNode *node) {
    if (!node) return;

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
        case AST_STATEMENT_WRITE:
        case AST_STATEMENT_REWRITE:
            lower_file_statement(ir, node);
            break;
        case AST_STATEMENT_EXIT:        // marks the end of a procedure; does nothing
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
        case AST_STATEMENT_ACCEPT:
            lower_accept(ir, node);
            break;
        default:
            break;
    }
}

static void lower_block(IrProgram *ir, AstNode *block) {
    for (AstNode *n = block; n; n = n->next) lower_node(ir, n);
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
    if (!v->value || v->is_cond || v->is_index) return;
    int counts[16], strides[16], subs[16];
    int ndims = item_dims(v, counts, strides);
    long total = 1;
    for (int d = 0; d < ndims; d++) { subs[d] = 1; total *= counts[d]; }
    if (total > 100000) {
        cobalt_error_loc(NULL, v->decl->loc.line, v->decl->loc.column,
                         "VALUE in a table of more than 100000 occurrences is not supported ('%s')", v->name);
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

IrProgram *lower_to_ir(AstNode *program, const Dialect *program_dialect) {
    IrProgram *ir = calloc(1, sizeof(IrProgram));
    if (!ir) return NULL;
    dialect = program_dialect;
    if (dialect->sign_encoding == SIGN_ENCODING_EBCDIC) ir->runtime_flags |= IR_RT_SIGN_EBCDIC;

    num_vars = 0;
    num_records = 0;
    lit_counter = 0;
    if_counter = 0;
    addr_slots = 0;
    inspect_counter = 0;

    build_special_names(program);
    build_data(ir, program);

    // Emit initializations
    append_instr(ir, new_instr(IR_CALL_INIT, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
    if (has_collation) {
        IrInstr *t = emit(ir, IR_DATA_BYTES);
        t->var_name = strdup("$collate");
        t->size = 256;
        char *bytes = malloc(256);
        memcpy(bytes, collate_weight, 256);
        t->content = bytes;
        emit(ir, IR_CALL_SET_COLLATION)->var_name = strdup("$collate");
    }
    emit(ir, IR_SIZE_ERR_CLEAR);
    for (int i = 0; i < num_vars; i++) lower_initial_value(ir, i);

    // Lower PROCEDURE DIVISION: procedures laid out in source order
    num_procs = 0;
    current_section = NULL;
    sentence_end = NULL;
    for (AstNode *div = program->children; div; div = div->next) {
        if (div->kind != AST_DIVISION || strcasecmp(div->name, "PROCEDURE") != 0) continue;
        for (AstNode *n = div->children; n; n = n->next) {
            if (n->kind == AST_SECTION || n->kind == AST_PARAGRAPH) {
                if (num_procs >= 1024) { cobalt_error("Too many procedures (1024 limit)"); break; }
                procs[num_procs++] = (ProcInfo){ n->name, NULL, n->kind == AST_SECTION };
            }
            if (n->kind == AST_SECTION) {
                for (AstNode *p = n->children; p; p = p->next) {
                    if (p->kind != AST_PARAGRAPH) continue;
                    if (num_procs >= 1024) { cobalt_error("Too many procedures (1024 limit)"); break; }
                    procs[num_procs++] = (ProcInfo){ p->name, n->name, false };
                }
            }
        }

        memset(altered, 0, sizeof(altered));
        mark_altered(div->children);

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
                lower_block(ir, c->children);
                emit(ir, IR_PROC_EXIT)->imm = para;
            }
            emit(ir, IR_PROC_EXIT)->imm = proc;
            current_section = NULL;
        }
    }
    // Falling off the end of the PROCEDURE DIVISION ends the run unit
    append_instr(ir, new_instr(IR_CALL_EXIT, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
    current_proc = -1;

    // An altered paragraph must consist of a GO TO statement
    for (int p = 0; p < num_procs && p < 1024; p++) {
        if (!altered[p]) continue;
        bool has_goto = false;
        for (IrInstr *i = ir->head; i && !has_goto; i = i->next)
            has_goto = i->op == IR_GOTO_ALTERABLE && i->imm == p;
        if (!has_goto)
            cobalt_error("ALTER names '%s', which does not consist of a GO TO statement", procs[p].name);
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
    for (int i = 0; i < num_vars; i++) {
        free(vars[i].name);
    }
    free(ir);
}
