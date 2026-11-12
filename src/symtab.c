#include "symtab.h"
#include "diag.h" // For error reporting
#include <stdlib.h> // For malloc, free
#include <string.h> // For strdup, strcmp
#include <stdio.h> // For exit

// --- Hashing function for Scope entries ---
static unsigned int hash_string(const char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash % HASH_TABLE_SIZE;
}

// --- SymbolTable operations ---

SymbolTable *symtab_init(void) {
    SymbolTable *table = malloc(sizeof(SymbolTable));
    if (!table) {
        cobalt_error("Failed to allocate memory for SymbolTable.");
        exit(1);
    }
    table->current_scope = NULL;
    table->parent = NULL; // No parent for the top-level symbol table
    symtab_push_scope(table); // Push the global scope
    return table;
}

static void free_scope_entries(Scope *scope) {
    if (!scope) return;
    for (int i = 0; i < HASH_TABLE_SIZE; ++i) {
        Symbol *current = scope->entries[i];
        while (current) {
            Symbol *next = current->next;
            free(current->name);
            if (current->pic_string) free(current->pic_string);
            free(current);
            current = next;
        }
    }
}

void symtab_free(SymbolTable *table) {
    if (!table) return;

    // Free all scopes in the stack
    Scope *current_scope_ptr = table->current_scope;
    while (current_scope_ptr) {
        Scope *prev_scope_in_stack = current_scope_ptr->parent_scope; // Stack is now managed by parent_scope field
        free_scope_entries(current_scope_ptr);
        free(current_scope_ptr);
        current_scope_ptr = prev_scope_in_stack;
    }
    free(table);
}

void symtab_push_scope(SymbolTable *table) {
    Scope *new_scope = malloc(sizeof(Scope));
    if (!new_scope) {
        cobalt_error("Failed to allocate memory for new Scope.");
        exit(1);
    }
    memset(new_scope, 0, sizeof(Scope)); // Initialize all entries to NULL
    new_scope->parent_scope = table->current_scope; // Link to the previous scope in the stack
    table->current_scope = new_scope;
}

void symtab_pop_scope(SymbolTable *table) {
    if (!table->current_scope) {
        cobalt_error("Attempted to pop from an empty symbol table scope stack.");
        return;
    }
    Scope *old_scope = table->current_scope;
    table->current_scope = old_scope->parent_scope; // Restore previous scope
    free_scope_entries(old_scope);
    free(old_scope);
}

Symbol *symtab_declare_symbol(SymbolTable *table, const char *name, SymbolKind kind, SourceLocation loc) {
    if (!table->current_scope) {
        diag_error(loc, "Cannot declare symbol '%s': no active scope.", name);
        return NULL;
    }

    // Check for redeclaration in current scope
    if (symtab_find_symbol_in_current_scope(table, name)) {
        diag_error(loc, "Redeclaration of symbol '%s'.", name);
        return NULL; // Or return existing symbol, depending on desired error recovery
    }

    Symbol *new_sym = malloc(sizeof(Symbol));
    if (!new_sym) {
        diag_error(loc, "Failed to allocate memory for symbol '%s'.", name);
        exit(1);
    }
    memset(new_sym, 0, sizeof(Symbol)); // Initialize all fields to 0/NULL

    new_sym->name = strdup(name);
    new_sym->kind = kind;
    new_sym->loc = loc;
    new_sym->is_declared = true; // Mark as declared
    new_sym->is_used = false; // Initially not used
    new_sym->condition_of = NULL;

    unsigned int index = hash_string(name);
    new_sym->next = table->current_scope->entries[index];
    table->current_scope->entries[index] = new_sym;

    return new_sym;
}

Symbol *symtab_resolve_symbol(SymbolTable *table, const char *name) {
    Scope *current_scope_ptr = table->current_scope;
    while (current_scope_ptr) {
        unsigned int index = hash_string(name);
        Symbol *sym = current_scope_ptr->entries[index];
        while (sym) {
            if (strcmp(sym->name, name) == 0) {
                return sym;
            }
            sym = sym->next;
        }
        // Move to the next outer scope (which is the 'parent_scope' pointer in the Scope stack)
        current_scope_ptr = current_scope_ptr->parent_scope;
    }
    // If not found in current SymbolTable, try parent SymbolTable (for nested programs)
    if (table->parent) {
        return symtab_resolve_symbol(table->parent, name); // Recursive call to parent table
    }
    return NULL; // Symbol not found
}

Symbol *symtab_find_symbol_in_current_scope(SymbolTable *table, const char *name) {
    if (!table->current_scope) return NULL;
    unsigned int index = hash_string(name);
    Symbol *sym = table->current_scope->entries[index];
    while (sym) {
        if (strcmp(sym->name, name) == 0) {
            return sym;
        }
        sym = sym->next;
    }
    return NULL;
}

// A condition-name in use uses its conditional variable too
void symtab_mark_symbol_as_used(Symbol *sym) {
    for (int depth = 0; sym && depth < 4; sym = sym->condition_of, depth++)
        sym->is_used = true;
}

// Function to parse a PIC string and populate symbol attributes
void symtab_parse_pic_string(Symbol *sym, const char *pic_string) {
    if (!sym || !pic_string) {
        return;
    }

    sym->pic_len = 0;
    sym->is_signed = false;
    sym->var_type = VAR_TYPE_ALPHANUMERIC; // Default to alphanumeric

    const char *p = pic_string;
    bool found_9 = false;
    bool found_X = false;

    while (*p != '\0') {
        char current_char = *p;
        int repeat_factor = 1;

        // Check for repeat factor (e.g., X(10), 9(5))
        if (*(p + 1) == '(') {
            char *endptr;
            long val = strtol(p + 2, &endptr, 10);
            if (endptr != p + 2 && *endptr == ')') {
                repeat_factor = (int)val;
                p = endptr; // Move past ')'
            }
        }

        switch (current_char) {
            case 'S': // Signed numeric
            case 's':
                sym->is_signed = true;
                break;
            case '9': // Numeric
                found_9 = true;
                sym->pic_len += repeat_factor;
                break;
            case 'X': // Alphanumeric
            case 'x':
                found_X = true;
                sym->pic_len += repeat_factor;
                break;
            case 'V': // Implied decimal point (no physical storage)
            case 'v':
                // Does not affect pic_len
                break;
            case 'P': // Scaling character (future: might affect value, not length in basic sense)
            case 'p':
                // For now, treat as length char, may need refinement
                sym->pic_len += repeat_factor;
                break;
            // Add other PIC clause characters as needed (e.g., Z, *, $, +, -, B, 0, /)
            default:
                // For simplicity, any other char contributes to length for now
                // This might need refinement for complex PICTURE clauses (e.g. Zeros, Comma, etc.)
                sym->pic_len += repeat_factor;
                break;
        }

        p++;
    }

    if (found_9 && !found_X) {
        sym->var_type = VAR_TYPE_NUMERIC;
    } else if (found_9 && found_X) {
        // Mixed, usually treated as alphanumeric for basic type checking
        sym->var_type = VAR_TYPE_ALPHANUMERIC;
    }
    // If only X is found, or neither, it remains VAR_TYPE_ALPHANUMERIC (default)
}


// Function to collect all symbols for unused checking (simple version)
// This will iterate through all scopes currently on the stack of the given SymbolTable.
void symtab_check_unused_symbols(SymbolTable *table) {
    Scope *current_scope_ptr = table->current_scope;
    while (current_scope_ptr) {
        for (int i = 0; i < HASH_TABLE_SIZE; ++i) {
            Symbol *current = current_scope_ptr->entries[i];
            while (current) {
                // Only warn for data items: procedures are also entered by
                // falling through, and PROGRAM-ID is the entry point
                // Group items (no PICTURE) are normally used through their
                // subordinate items, so only elementary items are reported
                if (current->is_declared && !current->is_used && current->kind == SYM_VAR &&
                    current->pic_string && !diag_in_copied_text(current->loc)) {
                    diag_warning(W_UNUSED, current->loc, "Data item '%s' is never used", current->name);
                }
                current = current->next;
            }
        }
        // Move to the next outer scope (which is the 'parent_scope' pointer in the Scope stack)
        current_scope_ptr = current_scope_ptr->parent_scope;
    }
    // TODO: If nested SymbolTables exist, recursively call symtab_check_unused_symbols on table->parent
}
const char *symtab_closest(SymbolTable *table, const char *name, SymbolKind kind) {
    int n = 0, cap = 256;
    const char **names = malloc(cap * sizeof(char *));
    for (Scope *sc = table->current_scope; sc; sc = sc->parent_scope)
        for (int i = 0; i < HASH_TABLE_SIZE; i++)
            for (Symbol *s = sc->entries[i]; s; s = s->next) {
                if (s->kind != kind || !s->is_declared || !s->loc.line) continue;
                if (n == cap) { cap *= 2; names = realloc(names, cap * sizeof(char *)); }
                names[n++] = s->name;
            }
    const char *found = diag_closest(name, names, n);
    free(names);
    return found;
}
