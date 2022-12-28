
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "parser/lexer.h"
#include "common/utils.h"

/* Constants. */
#define TBUF_SIZE                       4096

/* Functions. */
void lexer_initvar(struct lexer *l)
{
    l->tbuf = NULL;

    table_initvar(&l->tokens);
}

void lexer_destroy(struct lexer *l)
{
    while (l->file) lexer_close(l);

    if (l->tbuf) free((void *) l->tbuf);
    l->tbuf = NULL;

    table_destroy(&l->tokens);
}

int lexer_create(struct lexer *l,
                 struct allocator *salloc,
                 struct allocator *oalloc)
{
    lexer_initvar(l);

    l->tbuf = (char *) malloc(TBUF_SIZE);
    if (unlikely(!l->tbuf)) {
        report_error("lexer: create: memory exhausted");
        lexer_destroy(l);
        return FALSE;
    }

    if (unlikely(!table_create(&l->tokens))) {
        report_error("lexer: create: could not create table");
        lexer_destroy(l);
        return FALSE;
    }

    l->salloc = salloc;
    l->oalloc = oalloc;
    l->file = NULL;
    l->free_tokens = NULL;
    l->free_files = NULL;
    l->tbuf_size = TBUF_SIZE;
    l->tbuf_len = 0;

    return TRUE;
}

int lexer_open(struct lexer *l, const char *filename)
{
    struct lexer_file *file;
    FILE *fp;

    fp = fopen(filename, "r");
    if (!fp) return FAIL;

    if (!l->free_files) {
        file = (struct lexer_file *)
            allocator_alloc(l->oalloc, sizeof(struct lexer_file), FALSE);

        if (unlikely(!file)) {
            fclose(fp);
            report_error("lexer: open: memory exhausted");
            return ERROR;
        }
    } else {
        file = l->free_files;
        l->free_files = file->next;
    }

    file->fp = fp;
    file->filename = filename;
    file->line_num = 1;
    file->reached_eof = FALSE;
    file->discard = FALSE;
    file->tk_first = NULL;
    file->tk_last = NULL;
    file->tk_current = NULL;
    file->next = l->file;
    l->file = file;
    return OK;
}

void lexer_close(struct lexer *l)
{
    struct lexer_file *file;
    struct token *tk;

    file = l->file;
    if (!file) return;

    fclose(file->fp);
    file->fp = NULL;

    /* Recycle the tokens. */
    while (file->tk_first) {
        tk = file->tk_first;
        file->tk_first = tk->next;
        tk->next = l->free_tokens;
        l->free_tokens = tk;
    }

    l->file = file->next;

    /* Recycle the file. */
    file->next = l->free_files;
    l->free_files = file;
}

/* Tests if a given character is a punctuation character. */
static
int is_punctuation(char c)
{
   return (c == '_' || c == '$' || c == ':' || c == ';'
           || c == '#' || c == '!' || c == '%' || c ==',');
}

/* Adds a new token based on the contents of the temporary buffer.
 * The length of the token string is given by `len`, and the line
 * number of the token is given by `line_num`. If `len` is smaller
 * than the current temporary buffer length (tbuf_len), then the
 * remaining of the buffer is copied to the front.
 * The parameter `is_punct` indicates if the token is a punctuation
 * character.
 * Returns TRUE on success.
 */
static
int add_token(struct lexer *l, size_t len,
              unsigned int line_num, int is_punct)
{
    struct lexer_file *file;
    struct token *tk;
    struct string_node *n;
    struct string query;

    /* Checks if the string is in the table first. */
    query.s = l->tbuf;
    query.len = len;
    query.hash = string_hash(query.s, query.len);

    n = table_find(&l->tokens, &query);
    if (!n) {
        /* Allocate a new string_node. */
        n = (struct string_node *)
            allocator_alloc(l->oalloc, sizeof(struct string_node), FALSE);
        if (unlikely(!n)) return FALSE;

        /* Make a copy of the string in the temporary buffer. */
        n->str.s = allocator_dup(l->salloc, query.s, query.len);
        if (unlikely(!n->str.s)) return FALSE;

        n->str.len = query.len;
        n->str.hash = query.hash;
        n->next = NULL;
        if (unlikely(!table_add(&l->tokens, n)))
            return FALSE;
    }

    if (!l->free_tokens) {
        /* Allocate a new token. */
        tk = (struct token *)
            allocator_alloc(l->oalloc, sizeof(struct token), FALSE);
        if (unlikely(!tk)) return FALSE;
    } else {
        /* Reuse tokens. */
        tk = l->free_tokens;
        l->free_tokens = tk->next;
    }

    if (len < l->tbuf_len) {
        /* If there is more stuff in the temporary buffer, move
         * it to the front.
         */
        memcpy(l->tbuf, &l->tbuf[len], l->tbuf_len - len);
        l->tbuf_len -= len;
    } else {
        l->tbuf_len = 0;
    }

    file = l->file;

    tk->str = n->str;
    tk->filename = file->filename;
    tk->line_num = line_num;
    tk->is_punct = is_punct;
    tk->next = NULL;

    /* Add the token to the queue. */
    if (!file->tk_first) file->tk_first = tk;
    if (!file->tk_last) {
        file->tk_last = tk;
    } else {
        file->tk_last->next = tk;
        file->tk_last = tk;
    }
    return TRUE;
}

/* Parses the tokens.
 * This function might generate more than one token at a time.
 * Returns TRUE on success.
 */
static
int parse(struct lexer *l)
{
    struct lexer_file *file;
    unsigned int line_num;
    int c, is_full;
    int prev_is_lt, prev_is_cr;
    size_t len;

    file = l->file;

    l->tbuf_len = 0;
    line_num = file->line_num;
    prev_is_lt = FALSE;
    prev_is_cr = FALSE;

    if (file->reached_eof)
        return add_token(l, 0, line_num, TRUE);

    while (TRUE) {
        is_full = (l->tbuf_len + 4 >= l->tbuf_size);
        c = fgetc(file->fp);

        if (prev_is_lt) {
            if (c == '-') {
                c = '_';
            } else {
                /* Add the '<' character from before. */
                if (!is_full)
                    l->tbuf[l->tbuf_len++] = '<';
            }
            prev_is_lt = FALSE;
        }

        if (c == EOF) {
            file->reached_eof = TRUE;
            return add_token(l, l->tbuf_len,
                             line_num, (l->tbuf_len == 0));
        }

        /* Update the line number. */
        if (c == '\r' || c == '\n') {
            if (c != '\n' || !prev_is_cr)
                file->line_num++;
            prev_is_cr = (c == '\r');
            file->discard = FALSE;
            continue;
        } else {
            prev_is_cr = FALSE;
        }

        /* Ignore characters until a newline is found. */
        if (file->discard) continue;

        /* Ignore space characters. */
        if (isspace(c)) continue;

        if (l->tbuf_len == 0) line_num = file->line_num;
        l->tbuf[l->tbuf_len] = (char) c;

        if (is_punctuation((char) c)) {
            len = l->tbuf_len++;
            if (len > 0) {
                if (unlikely(!add_token(l, len, line_num, FALSE)))
                    return FALSE;
            }
            line_num = file->line_num;
            if (unlikely(!add_token(l, l->tbuf_len, line_num, TRUE)))
                return FALSE;

            if (c == ';')
                file->discard = TRUE;
            return TRUE;
        }

        if (c != '<') {
            if (!is_full) l->tbuf_len++;
        } else {
            prev_is_lt = TRUE;
        }
    }

    return TRUE;
}

struct token *lexer_peek(struct lexer *l, int advance)
{
    struct lexer_file *file;
    struct token *tk;

    file = l->file;
    if (unlikely(!file)) {
        report_error("lexer: peek: no file is open");
        return NULL;
    }

    if (!file->tk_current) {
        tk = file->tk_last;
        if (unlikely(!parse(l))) {
            report_error("lexer: peek: could not parse");
            return NULL;
        }

        if (!tk)
            file->tk_current = file->tk_first;
        else
            file->tk_current = tk->next;
    }
    tk = file->tk_current;
    if (advance)
        file->tk_current = tk->next;
    return tk;
}

struct token *lexer_token(struct lexer *l)
{
    struct lexer_file *file;
    struct token *tk;

    file = l->file;
    if (unlikely(!file)) {
        report_error("lexer: token: no file is open");
        return NULL;
    }

    if (!file->tk_first) {
        if (unlikely(!parse(l))) {
            report_error("lexer: token: could not parse");
            return NULL;
        }
        file->tk_current = file->tk_first;
    }

    tk = file->tk_first;
    if (file->tk_current == tk)
        file->tk_current = tk->next;
    if (file->tk_last == tk)
        file->tk_last = NULL;
    file->tk_first = tk->next;

    /* Recycle token. */
    tk->next = l->free_tokens;
    l->free_tokens = tk;

    return tk;
}
