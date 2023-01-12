
#ifndef __PARSER_LEXER_H
#define __PARSER_LEXER_H

#include <stdio.h>
#include <stddef.h>
#include "common/allocator.h"
#include "common/table.h"

/* Constants. */
#define OK                                 1
#define FAIL                               2
#define ERROR                              0

/* Data structures and types. */

/* Structure to represent a token. */
struct token {
    struct string str;            /* The token string. */
    const char *filename;         /* The filename location. */
    unsigned int line_num;        /* The line number location. */
    int is_punct;                 /* If it is a punctuation token. */
    struct token *next;           /* The next token. */
};

/* To represent an open file being parsed by the lexer. */
struct lexer_file {
    FILE *fp;                     /* The pointer to the FILE structure. */
    const char *filename;         /* The name of the file. */
    unsigned int line_num;        /* The current line number. */
    int reached_eof;              /* If it reached the EOF. */
    int discard;                  /* Discard the remaining of the line. */

    struct token *tk_first;       /* The first available token. */
    struct token *tk_last;        /* The last available token. */
    struct token *tk_current;     /* Current peeked token. */

    struct lexer_file *next;      /* Link to create a list of files. */
};

/* Structure to represent the lexer for the parser. */
struct lexer {
    struct allocator *salloc;     /* To make copies of the strings. */
    struct allocator *oalloc;     /* To allocate objects. */

    struct table tokens;          /* The table with all the parsed
                                   * tokens (from all files).
                                   */

    struct lexer_file *file;      /* The current file being parsed. */

    char *tbuf;                   /* Temporary buffer. */
    size_t tbuf_size;             /* The size of the buffer. */
    size_t tbuf_len;              /* The number of used characters. */

    struct token *free_tokens;    /* Recycled tokens. */
    struct lexer_file *free_files;/* Recycled files. */
};

/* Functions. */

/* Initializes the lexer variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void lexer_initvar(struct lexer *l);

/* Destroys the lexer object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void lexer_destroy(struct lexer *l);

/* Creates a new lexer object.
 * The `salloc` and `oalloc` are two allocators needed by
 * the lexer. One is for strings and the other for objects.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int lexer_create(struct lexer *l,
                 struct allocator *salloc,
                 struct allocator *oalloc);

/* Clears the state of the lexer. Among other things, it clears
 * the table of tokens.
 */
void lexer_clear(struct lexer *l);

/* Opens a file for parsing.
 * The name of the file is given by the parameter `filename`.
 * Returns OK, FAIL or ERROR.
 */
int lexer_open(struct lexer *l, const char *filename);

/* Closes the currently open file. */
void lexer_close(struct lexer *l);

/* Parses (peeks) the next token in the lexer.
 * If `advance` is TRUE, the lexer_peek() advances one more token.
 * Returns the token.
 */
struct token *lexer_peek(struct lexer *l, int advance);

/* Parses (and consumes) the next token in the lexer.
 * Returns the token.
 */
struct token *lexer_token(struct lexer *l);

#endif /* __PARSER_LEXER_H */
