#ifndef COBALT_SYMTAB_H
#define COBALT_SYMTAB_H

#include "lexer.h" // For SourceLocation
#include "ast.h"   // For AstNodeKind (if needed for symbol types)
#include <stdbool.h>

// Forward declarations
typedef struct SymbolTable SymbolTable;

// Enum for Symbol Kind (e.g., Variable, Paragraph, Section, Program)
typedef enum {
    SYM_VAR,
    SYM_PARAGRAPH,
    SYM_SECTION,
    SYM_PROGRAM,
    // Add other kinds as needed (e.g., File, Screen, Constant)
} SymbolKind;

// Enum for Variable Type (e.g., Numeric, Alphanumeric, Group)
typedef enum {
    VAR_TYPE_ALPHANUMERIC, // PIC X
    VAR_TYPE_NUMERIC,      // PIC 9
    VAR_TYPE_GROUP,        // Group item
    VAR_TYPE_COMP,         // COMP-1, COMP-2, COMP-3, COMP-5
    // Add other COBOL variable types
} VariableType;

// Structure to hold information about a declared symbol
typedef struct Symbol {
    char *name;
    SymbolKind kind;
    SourceLocation loc; // Location of declaration

    // --- Specifics for Variables (if kind == SYM_VAR) ---
    VariableType var_type;
    int level;          // COBOL level number (01, 05, 77, etc.)
    int pic_len;        // Length derived from PIC clause (e.g., PIC X(10) -> pic_len = 10)
    char *pic_string;   // Original PIC string (e.g., "X(10)", "9(3)V99")
    bool is_signed;     // For numeric, if PIC includes S
    bool is_indexed;    // If used with OCCURS clause (future)
    // Add more attributes like USAGE, REDEFINES, VALUE, etc.

    // --- Other flags ---
    bool is_declared;   // True if fully declared (not just referenced)
    bool is_used;       // True if referenced in executable code
    struct Symbol *condition_of; // level 88: the item it is a condition of
    
    // Pointer to next symbol in a list (e.g., for hash table chaining)
    struct Symbol *next; 
} Symbol;

// Structure for a single scope (e.g., hash table of symbols)
#define HASH_TABLE_SIZE 1024 // Using a fixed size for simplicity

typedef struct Scope {
    Symbol *entries[HASH_TABLE_SIZE]; // Using a fixed size for simplicity, collisions handled by linked list
    struct Scope *parent_scope; // Pointer to the previous scope in the stack
} Scope;

// Structure for the entire symbol table (stack of scopes)
struct SymbolTable {
    Scope *current_scope; // Pointer to the top of the scope stack
    struct SymbolTable *parent; // Pointer to the enclosing SymbolTable (for nested programs/copybooks)
    // Add more program-wide symbol table attributes here
};


// Function declarations for symbol table operations
SymbolTable *symtab_init(void);
void symtab_free(SymbolTable *table); // Frees all scopes and symbols
void symtab_push_scope(SymbolTable *table);
void symtab_pop_scope(SymbolTable *table);
Symbol *symtab_declare_symbol(SymbolTable *table, const char *name, SymbolKind kind, SourceLocation loc);
Symbol *symtab_resolve_symbol(SymbolTable *table, const char *name);
Symbol *symtab_find_symbol_in_current_scope(SymbolTable *table, const char *name);
void symtab_mark_symbol_as_used(Symbol *sym);
// The declared name of the kind closest to a misspelled name, or NULL
const char *symtab_closest(SymbolTable *table, const char *name, SymbolKind kind);
// For reporting unused symbols (Task 2)
void symtab_check_unused_symbols(SymbolTable *table);

// Function to parse a PIC string and populate symbol attributes
void symtab_parse_pic_string(Symbol *sym, const char *pic_string);

#endif // COBALT_SYMTAB_H
