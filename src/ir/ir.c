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
    int occurs;             // OCCURS count, 0 without OCCURS
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
} VarInfo;

#define MAX_VARS 4096
static VarInfo vars[MAX_VARS];
static int num_vars = 0;
static int record_size[MAX_VARS];
static int num_records = 0;
static int lit_counter = 0;
static int if_counter = 0;
static int addr_slots = 0;  // address/length slots used so far

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
// -1 if n is not a figurative constant
static int figurative_char(const AstNode *n) {
    if (n->kind != AST_FIGURATIVE_CONSTANT || !n->name) return -1;
    if (strcasecmp(n->name, "ALL") == 0)
        return (n->text && strlen(n->text) == 1) ? (unsigned char)n->text[0] : FIG_PATTERN;
    if (strncasecmp(n->name, "ZERO", 4) == 0)       return '0';
    if (strncasecmp(n->name, "SPACE", 5) == 0)      return ' ';
    if (strncasecmp(n->name, "HIGH-VALUE", 10) == 0) return 0xFF;
    if (strncasecmp(n->name, "LOW-VALUE", 9) == 0)  return 0;
    if (strncasecmp(n->name, "QUOTE", 5) == 0)      return '"';
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

static void build_data(IrProgram *ir, AstNode *program) {
    int stack[64];
    int depth = 0;
    int last_item = -1;     // data item that level-88 entries belong to

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

    // Validate the hierarchy, then lay out the records
    for (int i = 0; i < num_vars; i++) {
        VarInfo *v = &vars[i];
        if (v->is_cond || v->is_index) continue;
        if (v->is_group && v->first_child < 0)
            cobalt_error_loc(NULL, v->decl->loc.line, v->decl->loc.column,
                             "'%s' needs a PICTURE clause (it has no subordinate items)", v->name);
        if (v->value && v->occurs)
            cobalt_error_loc(NULL, v->decl->loc.line, v->decl->loc.column,
                             "VALUE cannot be used with OCCURS ('%s')", v->name);
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

    if (!dynamic) {
        snprintf(out->label, sizeof(out->label), "%%r%d+%d", v->record, offset);
        out->size = size;
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
    out->size = size;
    return true;
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

static int var_flags(const VarInfo *v) {
    return v->is_signed ? IR_NUM_SIGNED : 0;
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
            return num_expr_valid(e->children);
        case AST_EXPR_POW: {
            AstNode *exp = e->children ? e->children->next : NULL;
            if (!exp || exp->kind != AST_LITERAL_NUMBER ||
                !parse_num_literal(exp->name, &value, &scale) || scale != 0 || value < 0 || value > 18) {
                cobalt_error_loc(NULL, e->loc.line, e->loc.column,
                                 "Only integer literal exponents from 0 to 18 are supported");
                return false;
            }
            return num_expr_valid(e->children);
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

static void emit_store(IrProgram *ir, const FieldRef *f, bool keep) {
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
            int ignored;
            parse_num_literal(base->next->name, &n, &ignored);
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
static void lower_assign(IrProgram *ir, AstNode *tree, AstNode *receiver, bool keep) {
    VarInfo *t = numeric_item(receiver, "Receiving item");
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

// ADD, SUBTRACT, MULTIPLY, DIVIDE and COMPUTE: one expression per receiver.
// Child layouts are documented at the statement parsers.
static void lower_arith_node(IrProgram *ir, AstNode *node) {
    AstNode *ops[128];
    int n = 0;
    for (AstNode *c = node->children; c && n < 128; c = c->next) ops[n++] = c;
    bool giving = node->is_numeric;
    bool keep = node->on_error != NULL;

    emit(ir, IR_SIZE_ERR_CLEAR);

    switch (node->kind) {
        case AST_STATEMENT_ADD: {
            int nsrc = node->decimal_places;
            for (int i = nsrc; i < n; i++) {
                AstNode *tree = giving ? fold_ops(AST_EXPR_ADD, ast_clone(ops[0]), ops + 1, nsrc - 1)
                                       : fold_ops(AST_EXPR_ADD, ast_clone(ops[i]), ops, nsrc);
                lower_assign(ir, tree, ops[i], keep);
                ast_free(tree);
            }
            break;
        }
        case AST_STATEMENT_SUBTRACT: {
            int nsrc = node->decimal_places;
            int first_dest = giving ? nsrc + 1 : nsrc;
            for (int i = first_dest; i < n; i++) {
                AstNode *minuend = giving ? ops[nsrc] : ops[i];
                AstNode *tree = fold_ops(AST_EXPR_SUB, ast_clone(minuend), ops, nsrc);
                lower_assign(ir, tree, ops[i], keep);
                ast_free(tree);
            }
            break;
        }
        case AST_STATEMENT_MULTIPLY: {
            int first_dest = giving ? 2 : 1;
            for (int i = first_dest; i < n; i++) {
                AstNode *tree = mk_bin(AST_EXPR_MUL, ast_clone(ops[0]),
                                       ast_clone(giving ? ops[1] : ops[i]));
                lower_assign(ir, tree, ops[i], keep);
                ast_free(tree);
            }
            break;
        }
        case AST_STATEMENT_DIVIDE: {
            bool is_by = node->name && strcasecmp(node->name, "BY") == 0;
            bool has_rem = node->is_signed;
            if (!giving) {
                // DIVIDE divisor INTO dest+
                for (int i = 1; i < n; i++) {
                    AstNode *tree = mk_bin(AST_EXPR_DIV, ast_clone(ops[i]), ast_clone(ops[0]));
                    lower_assign(ir, tree, ops[i], keep);
                    ast_free(tree);
                }
                break;
            }
            AstNode *dividend = is_by ? ops[0] : ops[1];
            AstNode *divisor  = is_by ? ops[1] : ops[0];
            int last_dest = has_rem ? n - 1 : n;
            for (int i = 2; i < last_dest; i++) {
                AstNode *tree = mk_bin(AST_EXPR_DIV, ast_clone(dividend), ast_clone(divisor));
                lower_assign(ir, tree, ops[i], keep);
                ast_free(tree);
            }
            if (has_rem) {
                // remainder = dividend - quotient * divisor, using the stored quotient
                AstNode *tree = mk_bin(AST_EXPR_SUB, ast_clone(dividend),
                                       mk_bin(AST_EXPR_MUL, ast_clone(ops[2]), ast_clone(divisor)));
                lower_assign(ir, tree, ops[n - 1], keep);
                ast_free(tree);
            }
            break;
        }
        case AST_STATEMENT_COMPUTE:
            for (int i = 1; i < n; i++) lower_assign(ir, ops[0], ops[i], keep);
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

// Address and length of an operand compared as alphanumeric
static bool alnum_operand(IrProgram *ir, AstNode *n, FieldRef *f) {
    if (n->kind == AST_IDENTIFIER) {
        if (!resolve_ref(ir, n, f)) return false;
        if (f->var->is_numeric && f->var->usage_type != USAGE_DISPLAY && !f->refmod) {
            cobalt_error_loc(NULL, n->loc.line, n->loc.column,
                             "Comparing non-DISPLAY numeric '%s' with a nonnumeric operand is not supported", n->name);
            return false;
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
            if (alnum_operand(ir, left, &f)) {
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
        if (alnum_operand(ir, left, &lf) && alnum_operand(ir, right, &rf)) {
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
        AstNode *lo = ast_new(v->kind, v->name, v->loc);
        if (v->extra) {
            AstNode *hi = ast_new(v->extra->kind, v->extra->name, v->extra->loc);
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
    if (strcmp(c->name, "NUMERIC") == 0) {
        if (numeric && (v->usage_type == USAGE_COMP || v->usage_type == USAGE_COMP5)) {
            if (when) emit_jump(ir, IR_JUMP, target); // binary items are always numeric
            return;
        }
        if (numeric && v->usage_type == USAGE_COMP3) code = 5;
        else code = (numeric && v->is_signed) ? 1 : 0;
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

// Fill a field with a figurative constant (character or ALL "pattern")
static void emit_fill_figurative(IrProgram *ir, const FieldRef *dest, const AstNode *fig) {
    int ch = figurative_char(fig);
    if (ch != FIG_PATTERN) {
        emit_fill(ir, dest, ch);
        return;
    }
    char lit[32];
    emit_data_lit(ir, "patlit", fig->text, lit, sizeof(lit));
    append_instr(ir, new_instr(IR_CALL_MOVE_FILL, dest->label, lit, NULL,
                               (int)strlen(fig->text), dest->size, 0, 0, USAGE_DISPLAY));
}

// MOVE src TO dest (one receiver). Receiver subscripts are evaluated just
// before the data is moved.
static void lower_move(IrProgram *ir, AstNode *src, AstNode *dest_node, const char *src_lit) {
    VarInfo *dv = find_item(dest_node, true);
    if (!dv) return;
    bool dest_numeric = dv->is_numeric && !dest_node->loop && !dv->is_cond;

    int fig = figurative_char(src);
    if (fig != -1) {
        if (dest_numeric && fig != '0') {
            cobalt_error_loc(NULL, src->loc.line, src->loc.column,
                             "%s cannot be moved to numeric '%s'", src->name, dv->name);
            return;
        }
        FieldRef dest;
        if (dest_numeric) {
            emit_num(ir, IR_NUM_CONST, 0, 0);
            if (!resolve_ref(ir, dest_node, &dest)) return;
            emit_store(ir, &dest, false);
        } else {
            if (!resolve_ref(ir, dest_node, &dest)) return;
            emit_fill_figurative(ir, &dest, src);
        }
        return;
    }

    if (src->kind == AST_STRING_LITERAL) {
        if (dest_numeric) {
            cobalt_error_loc(NULL, src->loc.line, src->loc.column,
                             "A nonnumeric literal cannot be moved to numeric '%s'", dv->name);
            return;
        }
        FieldRef dest;
        if (!resolve_ref(ir, dest_node, &dest)) return;
        emit_move_alpha(ir, &dest, src_lit, (int)strlen(src->name));
        return;
    }

    if (src->kind == AST_LITERAL_NUMBER) {
        FieldRef dest;
        if (dest_numeric) {
            if (!num_expr_valid(src)) return;
            int s = num_expr(ir, src, 0);
            emit_rescale(ir, dv->decimal_places - s, false);
            if (!resolve_ref(ir, dest_node, &dest)) return;
            emit_store(ir, &dest, false);
        } else {
            if (!resolve_ref(ir, dest_node, &dest)) return;
            emit_move_alpha(ir, &dest, src_lit, (int)strlen(src->name));
        }
        return;
    }

    if (src->kind != AST_IDENTIFIER) return;
    VarInfo *sv = find_item(src, true);
    if (!sv) return;
    bool src_numeric = sv->is_numeric && !src->loop && !sv->is_cond;

    if (src_numeric && dest_numeric) {
        FieldRef sf, dest;
        if (!resolve_ref(ir, src, &sf)) return;
        emit_load(ir, &sf);
        emit_rescale(ir, dv->decimal_places - sv->decimal_places, false);
        if (!resolve_ref(ir, dest_node, &dest)) return;
        emit_store(ir, &dest, false);
    } else if (!dest_numeric) {
        if (src_numeric && sv->usage_type != USAGE_DISPLAY) {
            cobalt_error_loc(NULL, src->loc.line, src->loc.column,
                             "MOVE of non-DISPLAY numeric '%s' to alphanumeric is not supported yet", sv->name);
            return;
        }
        FieldRef sf, dest;
        if (!resolve_ref(ir, src, &sf) || !resolve_ref(ir, dest_node, &dest)) return;
        emit_move_alpha(ir, &dest, sf.label, sf.size);
    } else if (dv->usage_type == USAGE_DISPLAY && !dv->is_signed && dv->decimal_places == 0) {
        FieldRef sf, dest;
        if (!resolve_ref(ir, src, &sf) || !resolve_ref(ir, dest_node, &dest)) return;
        if (sf.size < 0 || sf.size > dest.size) {
            cobalt_error_loc(NULL, src->loc.line, src->loc.column,
                             "MOVE of '%s' to the shorter numeric '%s' is not supported yet", sv->name, dv->name);
            return;
        }
        append_instr(ir, new_instr(IR_CALL_MOVE_NUM, dest.label, sf.label, NULL,
                                   sf.size, dest.size, 0, 0, USAGE_DISPLAY));
    } else {
        cobalt_error_loc(NULL, src->loc.line, src->loc.column,
                         "MOVE of alphanumeric '%s' to this numeric item is not supported yet", sv->name);
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

// VARYING clause k and all clauses after it (AFTER phrases nest inside)
static void lower_varying(IrProgram *ir, AstNode *node, AstNode *clause, int first, int last) {
    AstNode *var = clause->children, *from = var->next, *by = from->next, *cond = by->next;
    bool test_after = node->is_signed;
    char loop_lbl[32], done_lbl[32];
    new_label(loop_lbl, sizeof(loop_lbl), "vary");
    new_label(done_lbl, sizeof(done_lbl), "vary_done");

    lower_varying_set(ir, var, from, false);
    emit_label(ir, loop_lbl);
    if (!test_after) emit_cond_jump(ir, cond, done_lbl, true);
    if (clause->next) lower_varying(ir, node, clause->next, first, last);
    else lower_perform_body(ir, node, first, last);
    if (test_after) emit_cond_jump(ir, cond, done_lbl, true);
    lower_varying_set(ir, var, by, true);
    emit_jump(ir, IR_JUMP, loop_lbl);
    emit_label(ir, done_lbl);
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
static void lower_goto(IrProgram *ir, AstNode *node) {
    if (!node->extra) {
        int target = resolve_proc(node->children);
        if (target < 0) return;
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
    if (v->is_numeric) {
        emit_num(ir, IR_NUM_CONST, 0, 0);
        emit_store(ir, &f, false);
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
    AstNode *index = item_ref(tv->first_index, table->loc);
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
    char count[16];
    snprintf(count, sizeof(count), "%d", tv->occurs);
    AstNode *limit = make_relation(ast_clone(index), ">", ast_new(AST_LITERAL_NUMBER, count, table->loc));
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
    if (node->loop) lower_varying_set(ir, node->loop, one, true);
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
            if (f.var->is_numeric && !f.refmod) {
                IrInstr *d = new_instr(IR_CALL_DISPLAY_NUM, f.label, NULL, NULL, 0, f.var->size,
                                       f.var->decimal_places, 0, f.var->usage_type);
                d->flags = var_flags(f.var);
                append_instr(ir, d);
            } else {
                append_instr(ir, new_instr(IR_CALL_DISPLAY_VAR_STR, f.label, NULL, NULL, 0, f.size, 0, 0, USAGE_DISPLAY));
            }
        }
    }
    append_instr(ir, new_instr(IR_EMIT_NEWLINE, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
}

static void lower_accept(IrProgram *ir, AstNode *node) {
    AstNode *var_node = node->children;
    if (!var_node || var_node->kind != AST_IDENTIFIER) {
        cobalt_warn("ACCEPT statement requires a variable", NULL);
        return;
    }
    FieldRef f;
    if (!resolve_ref(ir, var_node, &f)) return;
    if (f.var->is_numeric && !f.refmod) {
        IrInstr *a = new_instr(IR_CALL_ACCEPT_NUM, f.label, NULL, NULL, 0, f.var->size,
                               f.var->decimal_places, 0, f.var->usage_type);
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
            for (AstNode *t = node->children; t; t = t->next) lower_initialize(ir, t);
            break;
        case AST_STATEMENT_SEARCH:
            lower_search(ir, node);
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

// Initial value of one data item from its VALUE clause
static void lower_initial_value(IrProgram *ir, int idx) {
    VarInfo *v = &vars[idx];
    AstNode *val = v->value;
    if (!val || v->is_cond || v->is_index) return;
    AstNode *ref = item_ref(idx, val->loc);
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
        emit_rescale(ir, v->decimal_places - s, false);
        emit_store(ir, &f, false);
    } else if (val->kind == AST_STRING_LITERAL && !numeric) {
        char lit_name[32];
        emit_data_lit(ir, "initlit", val->name, lit_name, sizeof(lit_name));
        emit_move_alpha(ir, &f, lit_name, (int)strlen(val->name));
    }
}

IrProgram *lower_to_ir(AstNode *program) {
    IrProgram *ir = calloc(1, sizeof(IrProgram));
    if (!ir) return NULL;

    num_vars = 0;
    num_records = 0;
    lit_counter = 0;
    if_counter = 0;
    addr_slots = 0;

    build_data(ir, program);

    // Emit initializations
    append_instr(ir, new_instr(IR_CALL_INIT, NULL, NULL, NULL, 0, 0, 0, 0, USAGE_DISPLAY));
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

        int index = 0;
        for (AstNode *n = div->children; n; n = n->next) {
            if (n->kind == AST_SENTENCE) {
                lower_node(ir, n);
                continue;
            }
            int proc = index++;
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
