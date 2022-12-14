
#ifndef __PARSER_PARSER_H
#define __PARSER_PARSER_H

#include <stddef.h>
#include <stdint.h>
#include "parser/lexer.h"
#include "common/allocator.h"
#include "common/table.h"

/* Data structures and types. */

/* Forward declaration. */
struct symbol_info;

/* A structure to represent a linked list of strings with
 * some extra information.
 */
struct parser_node {
    struct string name;           /* The name of the node. */
    struct parser_node *next;     /* The next node. */
    void *extra;                  /* Extra information. */
};

/* An enumeration with the possible types of declaration types. */
enum declaration_type {
    DECL_SYMBOL,
    DECL_CONSTANT,
    DECL_M_CONSTANT,
    DECL_R_MEMORY
};

/* Represents a declaration (symbol definition, constant declaration, or
 * R memory declaration).
 */
struct declaration {
    enum declaration_type d_type; /* The declaration type. */
    struct string name;           /* The name being declared. */
    uint16_t n1, n2, n3;          /* Parameters of the declaration. */
    struct symbol_info *si;       /* The symbol information. */
};

/* Represents an address predefinition statement. */
struct address_predefinition {
    uint16_t n, k, l;             /* The parameters of the statement. */
    int extended;                 /* If using the extended predefinition. */
    struct parser_node *labels;   /* The defined labels. */
    unsigned int num_labels;      /* The number of labels defined. */
};

/* The types of clauses. */
enum clause_type {
    CL_GOTO,                      /* GOTO statements. */
    CL_FUNCTION,                  /* Nondata functions. */
    CL_ASSIGNMENT                 /* Assignments. */
};

/* Represents a clause in an executable statement. */
struct clause {
    enum clause_type c_type;      /* The clause type. */
    struct string name;           /* The name of the clause. */
    struct parser_node *lhs;      /* Pointer to the first destination. */
    struct clause *next;          /* The next clause. */
};

/* Represents an executable statement. */
struct executable_statement {
    struct string label;          /* The label of this statement. */
    struct clause *clauses;       /* The list of clauses. */
    struct symbol_info *si;       /* Symbol info for the label. */
    uint16_t address;             /* Address of this statement. */
};

/* Possible errors found during parsing. */
enum error_type {
    ERR_NONE,                     /* No error. */
    ERR_INVALID_FILE,             /* Invalid filename. */
    ERR_INVALID_OCTAL,            /* Invalid octal number. */
    ERR_ALREADY_DEFINED,          /* Name already defined. */
    ERR_EXPECT_NAME,              /* Expecting a name. */
    ERR_EXPECT_PUNCTUATION,       /* Expecting punctuation. */
    ERR_EXPECT_OCTAL              /* Expecting an octal number. */
};

/* Represents an erroneous statement. */
struct erroneous_statement {
    enum error_type err_type;     /* The error type. */
    struct string name;           /* More information about the error. */
    int punctuation;              /* Extra information about the error. */
};

/* An enumeration with the possible types of statements. */
enum statement_type {
    ST_DECLARATION,               /* Declaration statement (begins with '$'). */
    ST_ADDRESS_PREDEFINITION,     /* Address predefinition statement ('!'). */
    ST_EXECUTABLE,                /* Executable statement. */
    ST_ERROR                      /* Invalid statement. */
};

/* A structure representing a statement. */
struct statement {
    enum statement_type st_type;  /* The type of the statement. */
    union {
        struct declaration decl;
        struct address_predefinition addr;
        struct executable_statement exec;
        struct erroneous_statement err;
    } v;                          /* The information about the statement. */

    const char *filename;         /* The file containing the statement. */
    unsigned int line_num;        /* The location within the file. */

    struct statement *next;       /* A pointer to the next statement. */
};

/* Structure to represent a symbol. */
struct symbol_info {
    struct string_node n;         /* The node in the hash table. */
    struct statement *decl;       /* If the symbol was defined in a
                                   * declaration, this is pointer to
                                   * the declaration defining it.
                                   */
    struct statement *addr;       /* If the symbol was defined in an
                                   * address predefinition, this should
                                   * be pointing to the corresponding
                                   * statement.
                                   */
    struct statement *exec;       /* If the symbol was defined as a label,
                                   * this is a pointer to the statement
                                   * containing it.
                                   */
    uint16_t address;             /* The address of the symbol. */
    void *extra;                  /* Extra information. */
};

/* Structure to represent the parser. */
struct parser {
    struct allocator *salloc;     /* To make copies of the strings. */
    struct allocator *oalloc;     /* To allocate objects. */

    struct lexer l;               /* The lexer. */
    struct table symbols;         /* Hash table with the symbol information. */

    struct statement *first;      /* First statement in the file. */
    struct statement *last;       /* Last statement in the file. */

    struct token *tk;             /* Current token. */
    unsigned int num_errors;      /* The number of errors. */
};

/* Functions. */

/* Initializes the parser variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void parser_initvar(struct parser *p);

/* Destroys the parser object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void parser_destroy(struct parser *p);

/* Creates a new parser object.
 * The `salloc` and `oalloc` are two allocators needed by
 * the parser. One is for strings and the other for objects.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int parser_create(struct parser *p,
                  struct allocator *salloc,
                  struct allocator *oalloc);

/* Parses a given filename.
 * The filename to be parsed is given in `filename`.
 * Returns OK, FAIL or ERROR.
 */
int parser_parse(struct parser *p, const char *filename);

/* Reports the errors found in the parsed file to the `stderr`. */
void parser_report_errors(struct parser *p);

#endif /* __PARSER_H */
