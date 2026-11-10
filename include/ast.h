/* include/ast.h */
#ifndef COBALT_AST_H
#define COBALT_AST_H
#include <stdio.h>
#include <stdbool.h> // For bool type
#include <stddef.h>  // For size_t type

// Storage usage of a data item. The values are the runtime usage codes
// (runtime/ABI.md section 5), so they are passed to the runtime unchanged.
typedef enum {
    USAGE_DISPLAY = 0,
    USAGE_COMP    = 1,      // COMP, COMP-4, BINARY: big-endian binary
    USAGE_COMP3   = 2,      // COMP-3, PACKED-DECIMAL
    USAGE_COMP5   = 3,      // COMP-5: native binary
} AstUsageType;

typedef enum {
    AST_PROGRAM,
    AST_DIVISION,
    AST_SECTION,
    AST_STATEMENT_DISPLAY,
    AST_STATEMENT_STOP,
    AST_VAR_DECL,           // level 66: on_error / not_on_error = RENAMES item [THRU item]
    AST_STRING_LITERAL,
    AST_IDENTIFIER,

    AST_PARAGRAPH,
    AST_STATEMENT_MOVE,
    AST_STATEMENT_ADD,
    AST_STATEMENT_SUBTRACT,
    AST_STATEMENT_MULTIPLY,
    AST_STATEMENT_DIVIDE,
    AST_STATEMENT_COMPUTE,
    AST_STATEMENT_IF,
    AST_STATEMENT_PERFORM,
    AST_STATEMENT_ACCEPT,
    AST_STATEMENT_CONTINUE,
    AST_IDENTIFICATION_PARAGRAPH,
    AST_LITERAL_NUMBER,     // numeric literal
    AST_FIGURATIVE_CONSTANT, // e.g., ZEROS, SPACES
    AST_FILE_DECL,          // FD: name = file; children = record description entries
    AST_SPECIAL,            // SPECIAL-NAMES / OBJECT-COMPUTER entry; name: SWITCH, ALPHABET,
                            //   CLASS, CURRENCY, DECIMAL-POINT or COLLATING; text: the
                            //   alphabet / class name, currency character or collating
                            //   alphabet; children: character specs (extra = THRU end,
                            //   is_signed = ALSO); SWITCH: text = implementor name,
                            //   on_error / not_on_error = ON / OFF condition-names
    AST_SELECT,             // SELECT: name = file; text = ASSIGN name; is_numeric =
                            //   ASSIGN literal; is_signed = OPTIONAL; decimal_places =
                            //   1 for LINE SEQUENTIAL; extra = FILE STATUS item;
                            //   org = FILE_ORG_*, access = FILE_ACCESS_*; children:
                            //   AST_FILE_KEY entries
    AST_USE,                // USE statement of a declarative section (the section's extra):
                            //   name: ERROR, DEBUGGING or REPORTING; is_signed: GLOBAL;
                            //   ERROR: text = INPUT, OUTPUT, I-O or EXTEND, or children =
                            //   the files; DEBUGGING: children = the procedures, files and
                            //   items (is_signed: ALL REFERENCES OF), text = "ALL" for ALL
                            //   PROCEDURES; REPORTING: children = the report group
    AST_FILE_KEY,           // name: RELATIVE, RECORD or ALTERNATE; extra: the key item;
                            //   is_signed: WITH DUPLICATES
    AST_CONDITION,
    AST_PIC_CLAUSE,
    AST_USAGE_CLAUSE,
    AST_USAGE_TYPE_COMP,
    AST_USAGE_TYPE_DISPLAY,
    AST_THEN_BLOCK,   // wrapper for THEN-branch statements inside AST_STATEMENT_IF
    AST_ELSE_BLOCK,   // wrapper for ELSE-branch statements inside AST_STATEMENT_IF
    // Arithmetic expression nodes (used by COMPUTE)
    // children[0] = left operand, children[0]->next = right operand
    AST_EXPR_ADD,
    AST_EXPR_SUB,
    AST_EXPR_MUL,
    AST_EXPR_DIV,
    AST_EXPR_POW,           // children[0] ** children[1]
    AST_EXPR_NEG,           // unary minus: children[0]
    AST_EXPR_RESCALE,       // children[0] truncated to decimal_places digits after the point

    // Procedure structure
    AST_SENTENCE,           // statements up to a period: children
    AST_STATEMENT_GOTO,     // children: procedure names; extra: DEPENDING ON identifier
    AST_STATEMENT_NEXT_SENTENCE,
    AST_STATEMENT_EXIT,
    AST_VARYING,            // children: identifier, FROM operand, BY operand, condition

    // Conditions (AST_CONDITION is a relation: text = operator, children = operands)
    AST_COND_AND,           // children: left, right
    AST_COND_OR,            // children: left, right
    AST_COND_NOT,           // children: condition
    AST_COND_CLASS,         // name: NUMERIC, ALPHABETIC, ...; children: identifier
    AST_COND_SIGN,          // name: POSITIVE, NEGATIVE or ZERO; children: expression
    AST_COND_NAME,          // level-88 condition-name: name
    AST_BOOL_LITERAL,       // name: TRUE or FALSE (an always true/false condition)
    AST_STATEMENT_EVALUATE, // children: AST_WHEN nodes
    AST_WHEN,               // extra: condition (NULL for WHEN OTHER); children: statements
    AST_STATEMENT_SET,      // name: TRUE, TO, UP or DOWN; children: receivers;
                            //   extra: sending operand (TO / UP BY / DOWN BY)

    // Data hierarchy
    AST_OCCURS,             // name: count; children: INDEXED BY names
    AST_REDEFINES,          // name: the redefined item
    AST_JUSTIFIED,          // JUSTIFIED RIGHT
    AST_REFMOD,             // reference modification: children: start, [length]
    AST_STATEMENT_INITIALIZE, // children: identifiers
    AST_STATEMENT_STRING,   // children: sources, each with on_error: its delimiter (NULL: SIZE);
                            //   extra: INTO identifier; loop: POINTER identifier;
                            //   on_error / not_on_error: [NOT] ON OVERFLOW
    AST_STATEMENT_UNSTRING, // extra: AST_UNSTRING_SPEC; children: receivers, each with
                            //   on_error: DELIMITER IN, not_on_error: COUNT IN;
                            //   on_error / not_on_error of the statement: [NOT] ON OVERFLOW
    AST_UNSTRING_SPEC,      // extra: the source; children: delimiters (is_signed: ALL);
                            //   loop: POINTER identifier; on_error: TALLYING IN identifier
    AST_STATEMENT_ALTER,    // children: altered paragraphs, each with children: the new target
    AST_STATEMENT_INSPECT,  // extra: subject; children: AST_INSPECT_ITEM operands
    AST_INSPECT_ITEM,       // name: CHARACTERS, ALL, LEADING, FIRST or CONVERT;
                            //   loop: TALLYING counter (NULL when replacing);
                            //   extra: pattern (CONVERT: the FROM operand);
                            //   on_error: replacement (CONVERT: the TO operand);
                            //   not_on_error: BEFORE delimiter; children: AFTER delimiter
    AST_STATEMENT_OPEN,     // children: files, each with decimal_places = open mode
    AST_STATEMENT_CLOSE,    // children: files
    AST_STATEMENT_READ,     // extra: file; loop: INTO identifier;
                            //   on_error: AT END; not_on_error: NOT AT END
    AST_STATEMENT_WRITE,    // extra: record; loop: FROM identifier; name: BEFORE or
                            //   AFTER; children: line count, or PAGE (bool literal)
    AST_STATEMENT_REWRITE,  // extra: record; loop: FROM identifier; on_error / not_on_error:
                            //   [NOT] INVALID KEY (WRITE: is_signed for INVALID KEY, else
                            //   the phrases are [NOT] AT END-OF-PAGE)
    AST_STATEMENT_DELETE,   // extra: file; on_error / not_on_error: [NOT] INVALID KEY
    AST_STATEMENT_SORT,     // name: SORT or MERGE; extra: the sort file; children: keys
                            //   (is_signed: DESCENDING); text: COLLATING SEQUENCE alphabet;
                            //   on_error / not_on_error: INPUT / OUTPUT PROCEDURE (loop:
                            //   THRU); loop: AST_SPECIAL "USING" / "GIVING" (children: files)
    AST_STATEMENT_RELEASE,  // extra: sort record; loop: FROM
    AST_STATEMENT_RETURN,   // extra: sort file; loop: INTO; on_error / not_on_error: [NOT] AT END
    AST_STATEMENT_START,    // extra: file; text: =, > or >=; children: KEY data-name;
                            //   on_error / not_on_error: [NOT] INVALID KEY
    AST_STATEMENT_SEARCH,   // name: "ALL" for SEARCH ALL; extra: table identifier;
                            //   loop: VARYING identifier; on_error: AT END statements;
                            //   children: AST_WHEN nodes
} AstKind;


typedef struct {
    const char *filename; // File where the token/symbol is located
    int line;
    int column;
} SourceLocation;

// File organization and access mode (AST_SELECT)
enum { FILE_ORG_SEQUENTIAL, FILE_ORG_LINE_SEQUENTIAL, FILE_ORG_RELATIVE, FILE_ORG_INDEXED };
enum { FILE_ACCESS_SEQUENTIAL, FILE_ACCESS_RANDOM, FILE_ACCESS_DYNAMIC };

// SIGN clause bits (AstNode.sign_mode)
#define SIGN_GIVEN    1     // the clause is present (TRAILING alone is just this)
#define SIGN_LEADING  2
#define SIGN_SEPARATE 4

// AST_IDENTIFIER references: children = subscripts, extra = qualifiers
// (innermost first), loop = AST_REFMOD.
typedef struct AstNode {
    AstKind kind;
    char *name;             // owned
    char *text;             // owned
    SourceLocation loc;     // ← new: where this node starts in source

    // Semantic information for VAR_DECL nodes
    size_t data_size;       // Size in bytes
    AstUsageType usage_type; // e.g., DISPLAY, COMP
    bool is_numeric;        // True if the data item is numeric
    bool is_signed;         // True if the data item is signed
    int decimal_places;     // Number of decimal places for numeric items (V in PIC)
    int digits;             // Number of digit positions (9s) for numeric items
    char *mask;             // edited items: edit mask (see sema.c), else NULL
    char float_char;        // numeric edited: floating $, + or -, else 0
    bool blank_when_zero;   // BLANK WHEN ZERO
    int sign_mode;          // SIGN clause: SIGN_* bits, 0 without the clause
    bool is_group;          // data entry with subordinate entries (set by sema)
    int org, access;        // AST_SELECT: FILE_ORG_*, FILE_ACCESS_*

    // Arithmetic statements
    bool rounded;           // ROUNDED on this receiving identifier
    struct AstNode *on_error;     // ON SIZE ERROR statements (NULL if absent)
    struct AstNode *not_on_error; // NOT ON SIZE ERROR statements (NULL if absent)

    // Statement-specific parts (owned): PERFORM range / GO TO DEPENDING item,
    // and PERFORM loop control
    struct AstNode *extra;
    struct AstNode *loop;

    struct AstNode *children;
    struct AstNode *next;
} AstNode;

AstNode *ast_new(AstKind kind, const char *name, SourceLocation loc);
void ast_free(AstNode *node);
AstNode *ast_clone(const AstNode *node);       // deep copy of one node (not its siblings)
AstNode *ast_clone_list(const AstNode *node);  // deep copy of a sibling list
void ast_dump(AstNode *node, FILE *out, int indent);

#endif
