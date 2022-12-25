
/* To implement the assembler, the AltoSubsystems_Oct79.pdf manual
 * was used. It can be found at:
 *   https://bitsavers.computerhistory.org/pdf/xerox/alto/
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "parser/parser.h"
#include "parser/lexer.h"
#include "common/allocator.h"
#include "common/table.h"
#include "common/utils.h"

/* Forward declarations. */
static int parse_statements(struct parser *p);

/* Functions. */
void parser_initvar(struct parser *p)
{
    lexer_initvar(&p->l);
    table_initvar(&p->symbols);
}

void parser_destroy(struct parser *p)
{
    table_destroy(&p->symbols);
    lexer_destroy(&p->l);
}

int parser_create(struct parser *p,
                  struct allocator *salloc,
                  struct allocator *oalloc)
{
    parser_initvar(p);

    if (unlikely(!table_create(&p->symbols))) {
        report_error("parser: create: could not create symbol table");
        parser_destroy(p);
        return FALSE;
    }

    if (unlikely(!lexer_create(&p->l, salloc, oalloc))) {
        report_error("parser: create: could not create lexer");
        parser_destroy(p);
        return FALSE;
    }

    p->salloc = salloc;
    p->oalloc = oalloc;

    p->tk = NULL;
    p->num_errors = 0;
    p->first = p->last = NULL;

    return TRUE;
}

int parser_parse(struct parser *p, const char *filename)
{
    const char *dup_filename;
    size_t len;
    int ret;

    len = strlen(filename);
    dup_filename = allocator_dup(p->salloc, filename, len);
    if (unlikely(!dup_filename)) {
        report_error("parser: parse: memory exhausted");
        return ERROR;
    }

    ret = lexer_open(&p->l, dup_filename);
    if (unlikely(ret == ERROR)) {
        report_error("parser: parse: memory exhausted");
        return ERROR;
    }

    if (ret == FAIL) {
        report_error("parser: parse: could not open `%s` for parsing",
                     dup_filename);
        return FAIL;
    }

    p->tk = NULL;
    p->num_errors = 0;
    p->first = p->last = NULL;
    table_clear(&p->symbols);

    ret = parse_statements(p);
    lexer_close(&p->l);
    return ret;
}

void parser_report_errors(struct parser *p)
{
    struct statement *st;
    if (p->num_errors == 0) return;

    st = p->first;
    while (st) {
        const char *filename;
        unsigned int line_num;

        if (st->st_type != ST_ERROR) {
            st = st->next;
            continue;
        }

        filename = st->filename;
        line_num = st->line_num;

        switch (st->v.err.err_type) {
        case ERR_NONE:
            break;
        case ERR_INVALID_FILE:
            report_error("parser: %s:%d: invalid filename `%s`",
                         filename, line_num, st->v.err.name.s);
            break;
        case ERR_INVALID_OCTAL:
            report_error("parser: %s:%d: invalid octal `%s`",
                         filename, line_num, st->v.err.name.s);
            break;
        case ERR_ALREADY_DEFINED:
            report_error("parser: %s:%d: already defined `%s`",
                         filename, line_num, st->v.err.name.s);
            break;
        case ERR_EXPECT_NAME:
            report_error("parser: %s:%d: expecting name but got `%s`",
                         filename, line_num, st->v.err.name.s);
            break;
        case ERR_EXPECT_PUNCTUATION:
            report_error("parser: %s:%d: expecting `%c` but got `%s`",
                         filename, line_num,
                         st->v.err.punctuation, st->v.err.name.s);
            break;
        case ERR_EXPECT_OCTAL:
            report_error("parser: %s:%d: expecting octal but got `%s`",
                         filename, line_num, st->v.err.name.s);
            break;
        }
        st = st->next;
    }
}

/* Allocates a new statement.
 * Returns the allocated statement.
 */
static
struct statement *new_statement(struct parser *p)
{
    struct statement *st;
    st = (struct statement *)
        allocator_alloc(p->oalloc, sizeof(struct statement), FALSE);
    if (unlikely(!st)) {
        report_error("parser: parse: memory exhausted");
        return NULL;
    }
    st->filename = p->l.file->filename;
    if (p->tk)
        st->line_num = p->tk->line_num;
    else
        st->line_num = p->l.file->line_num;
    st->next = NULL;
    return st;
}

/* Allocates a new parser_node
 * Returns the allocated parser_node.
 */
static
struct parser_node  *new_parser_node(struct parser *p)
{
    struct parser_node *pn;
    pn = (struct parser_node *)
        allocator_alloc(p->oalloc, sizeof(struct parser_node), TRUE);
    if (unlikely(!pn)) {
        report_error("parser: parse: memory exhausted");
        return NULL;
    }
    return pn;
}

/* Allocates a new symbol_info
 * Returns the allocated symbol_info.
 */
static
struct symbol_info *new_symbol_info(struct parser *p)
{
    struct symbol_info *si;
    si = (struct symbol_info *)
        allocator_alloc(p->oalloc, sizeof(struct symbol_info), TRUE);
    if (unlikely(!si)) {
        report_error("parser: parse: memory exhausted");
        return NULL;
    }
    return si;
}

/* Allocates a new clause
 * Returns the allocated clause.
 */
static
struct clause *new_clause(struct parser *p)
{
    struct clause *cl;
    cl = (struct clause *)
        allocator_alloc(p->oalloc, sizeof(struct clause), FALSE);
    if (unlikely(!cl)) {
        report_error("parser: parse: memory exhausted");
        return NULL;
    }
    return cl;
}

/* Appends a statement to the parser. */
static
void append_statement(struct parser *p, struct statement *st)
{
    if (!p->first) p->first = st;
    if (p->last) {
        p->last->next = st;
    }
    p->last = st;
}

/* Adds a new error to the parsed file.
 * The error type is given by `err_type`.
 * Returns the error statement.
 */
static
struct statement *add_error(struct parser *p, enum error_type err_type)
{
    struct statement *st;
    st = new_statement(p);
    if (unlikely(!st)) return NULL;

    st->st_type = ST_ERROR;
    st->v.err.err_type = err_type;

    append_statement(p, st);
    p->num_errors++;
    return st;
}

/* Adds a symbol to the symbol table.
 * The name of the symbol is given in `name`. The parameter
 * `is_new` tells the function to expect a new symbol (if it is
 * already in the table, it is an error).
 * The symbol_info is returned in `out`.
 * Returns OK, FAIL or ERROR.
 */
static
int add_symbol(struct parser *p, struct string *name,
               int is_new, struct symbol_info **out)
{
    struct statement *st;
    struct string_node *n;
    struct symbol_info *si;

    n = table_find(&p->symbols, name);
    if (n) {
        size_t offset;
        offset = offsetof(struct symbol_info, n);
        si = (struct symbol_info *) &(((char *) n)[-offset]);
    } else {
        si = NULL;
    }

    if (si && is_new) {
        st = add_error(p, ERR_ALREADY_DEFINED);
        if (unlikely(!st)) return ERROR;
        st->v.err.name = *name;
        return FAIL;
    }

    if (!si) {
        si = new_symbol_info(p);
        if (unlikely(!si)) return ERROR;
        si->n.str = *name;

        /* Add the symbol_info to the table of symbols. */
        if (unlikely(!table_add(&p->symbols, &si->n))) {
            report_error("parser: parse: memory exhausted");
            return ERROR;
        }
    }

    *out = si;
    return OK;
}

/* Obtains a new token from the lexer.
 * Returns the token.
 */
static
struct token *get_token(struct parser *p)
{
    struct token *tk;
    tk = lexer_token(&p->l);
    if (unlikely(!tk))
        report_error("parser: parser: lexer error");
    p->tk = tk;
    return tk;
}

/* Peeks one token from the lexer.
 * Returns the token.
 */
static
struct token *peek_token(struct parser *p, int advance)
{
    struct token *tk;
    tk = lexer_peek(&p->l, advance);
    if (unlikely(!tk))
        report_error("parser: parser: lexer error");
    return tk;
}

/* Consumes tokens until a semicolon is found.
 * Returns TRUE on success.
 */
static
int skip_semicolon(struct parser *p)
{
    struct token *tk;
    while (TRUE) {
        tk = get_token(p);
        if (unlikely(!tk)) return FALSE;
        if (!tk->is_punct) continue;
        if (tk->str.s[0] == ';')
            return TRUE;
    }
}

/* Parses a name.
 * The name is returned in `name` parameter.
 * Returns OK, FAIL or ERROR.
 */
static
int parse_name(struct parser *p, struct string *name)
{
    struct statement *st;
    struct token *tk;

    tk = get_token(p);
    if (unlikely(!tk)) return ERROR;

    if (tk->is_punct) {
        st = add_error(p, ERR_EXPECT_NAME);
        if (unlikely(!st)) return ERROR;
        st->v.err.name = tk->str;
        if (tk->str.s[0] != ';') {
            if (unlikely(!skip_semicolon(p)))
                return ERROR;
        }
        return FAIL;
    }

    *name = tk->str;
    return OK;
}

/* Parses an octal number (auxiliary function).
 * The string containing the representation of the octal number is
 * given by the string `s` of length `len`.
 * The parameter `num` is where the number will be stored.
 * Returns TRUE or FALSE.
 */
static
int parse_octal_aux(const char *s, size_t len, uint16_t *num)
{
    uint16_t _num;
    size_t i;
    char c;

    _num = 0;
    for (i = 0; i < len; i++) {
        c = s[i];
        if (c >= '0' && c <= '7') {
            _num *= 8;
            _num += ((uint16_t) (c - '0'));
        } else return FALSE;
    }
    *num = _num;
    return (len > 0);
}

/* Parses an octal number.
 * The parameter `num` is where the number will be stored.
 * The paremeter `skip_first` tells this function to skip the
 * first character in the token.
 * Returns OK, FAIL or ERROR.
 */
static
int parse_octal(struct parser *p, uint16_t *num, int skip_first)
{
    struct statement *st;
    const char *s;
    size_t len;
    struct token *tk;

    tk = get_token(p);
    if (unlikely(!tk)) return ERROR;

    if (tk->is_punct) {
        st = add_error(p, ERR_EXPECT_OCTAL);
        if (unlikely(!st)) return ERROR;
        st->v.err.name = tk->str;
        if (tk->str.s[0] != ';') {
            if (unlikely(!skip_semicolon(p)))
                return ERROR;
        }
        return FAIL;
    }

    if (skip_first) {
        s = &tk->str.s[1];
        len = tk->str.len - 1;
    } else {
        s = tk->str.s;
        len = tk->str.len;
    }

    if (!parse_octal_aux(s, len, num)) {
        st = add_error(p, ERR_INVALID_OCTAL);
        if (unlikely(!st)) return ERROR;
        st->v.err.name = tk->str;
        return FAIL;
    }

    return OK;
}

/* Consumes a punctuation character.
 * Returns OK, FAIL or ERROR.
 */
static
int consume_punctuation(struct parser *p, char punctuation)
{
    struct statement *st;
    struct token *tk;

    tk = get_token(p);
    if (unlikely(!tk)) return ERROR;

    if (tk->str.s[0] != punctuation) {
        st = add_error(p, ERR_EXPECT_PUNCTUATION);
        if (unlikely(!st)) return ERROR;
        st->v.err.name = tk->str;
        st->v.err.punctuation = punctuation;
        if (tk->str.s[0] != ';') {
            if (unlikely(!skip_semicolon(p)))
                return ERROR;
        }
        return FAIL;
    }

    return OK;
}

/* Parses an include file statement.
 * Returns OK, FAIL or ERROR.
 */
static
int parse_include_file(struct parser *p)
{
    struct statement *st;
    struct string name;
    unsigned int line_num;
    int ret;

    /* According to page 77 of AltoSubsystems_Oct79.pdf
     *
     *   Include statements have the form:
     *     #filename;
     *   They cause the contents of the specified file to replace the
     *   include statement. Nesting to three levels is allowed.
     *
     * Note: we do not impose limits on nesting here.
     */

    ret = consume_punctuation(p, '#');
    if (ret != OK) return ret;

    ret = parse_name(p, &name);
    if (ret != OK) return ret;

    line_num = p->tk->line_num;

    ret = consume_punctuation(p, ';');
    if (ret != OK) return ret;

    ret = lexer_open(&p->l, name.s);
    if (unlikely(ret == ERROR)) {
        report_error("parser: parse: memory exhausted");
        return ERROR;
    }

    if (ret == FAIL) {
        st = add_error(p, ERR_INVALID_FILE);
        if (unlikely(!st)) return ERROR;
        st->v.err.name = name;
        st->line_num = line_num;
        return FAIL;
    }

    ret = parse_statements(p);
    lexer_close(&p->l);
    return ret;
}

/* Parses a declaration.
 * Returns OK, FAIL or ERROR.
 */
static
int parse_declaration(struct parser *p)
{
    struct statement *st;
    struct string name;
    struct token *tk;
    struct symbol_info *si;
    int ret;

    /* According to pages 77 and 78 of AltoSubsystems_Oct79.pdf
     *
     *   Declarations are of three types: symbol definitions, constant
     *   definitions, and R memory names.
     *
     *   Symbol definitions have the form:
     *     $name$Ln1,n2,n3;
     *   The symbol "name" is defined, with values n1, n2, and n3.
     *   ...
     *
     *   Normal constants are declared thus:
     *     $name$n;
     *   ...
     *
     *   R memory names are defined with:
     *     $name$Rn;    0<=n<40B
     *     (100B if your Alto has a RAM board, as most do)
     *   An R location may have several names.
     */

    ret = consume_punctuation(p, '$');
    if (ret != OK) return ret;

    st = new_statement(p);
    if (unlikely(!st)) return ERROR;
    st->st_type = ST_DECLARATION;

    ret = parse_name(p, &name);
    if (ret != OK) return ret;

    st->v.decl.name = name;

    /* Add symbol information now, but populate it only after
     * the declaration is successfuly parsed.
     */
    ret = add_symbol(p, &name, TRUE, &si);
    if (unlikely(ret == ERROR)) return ERROR;
    if (ret == FAIL) {
        return (!skip_semicolon(p)) ? ERROR : FAIL;
    }

    ret = consume_punctuation(p, '$');
    if (ret != OK) return ret;

    tk = peek_token(p, FALSE);
    if (unlikely(!tk)) return ERROR;

    if (tk->str.s[0] == 'L') {
        st->v.decl.d_type = DECL_SYMBOL;

        ret = parse_octal(p, &st->v.decl.n1, TRUE);
        if (ret != OK) return ret;

        ret = consume_punctuation(p, ',');
        if (ret != OK) return ret;

        ret = parse_octal(p, &st->v.decl.n2, FALSE);
        if (ret != OK) return ret;

        ret = consume_punctuation(p, ',');
        if (ret != OK) return ret;

        ret = parse_octal(p, &st->v.decl.n3, FALSE);
        if (ret != OK) return ret;
    } else if (tk->str.s[0] == 'R') {
        st->v.decl.d_type = DECL_R_MEMORY;

        ret = parse_octal(p, &st->v.decl.n1, TRUE);
        if (ret != OK) return ret;
    } else if (tk->str.s[0] == 'M') {
        st->v.decl.d_type = DECL_M_CONSTANT;

        ret = parse_octal(p, &st->v.decl.n1, TRUE);
        if (ret != OK) return ret;

        ret = consume_punctuation(p, ':');
        if (ret != OK) return ret;

        ret = parse_octal(p, &st->v.decl.n2, FALSE);
        if (ret != OK) return ret;
    } else {
        st->v.decl.d_type = DECL_CONSTANT;

        ret = parse_octal(p, &st->v.decl.n1, FALSE);
        if (ret != OK) return ret;
    }

    ret = consume_punctuation(p, ';');
    if (ret != OK) return ret;

    /* Symbol was created in a declaration. */
    si->decl = st;
    st->v.decl.si = si;

    append_statement(p, st);
    return OK;
}

/* Parses an address predefinition.
 * Returns OK, FAIL or ERROR.
 */
static
int parse_address_predefinition(struct parser *p)
{
    struct statement *st;
    struct string name;
    struct token *tk;
    struct symbol_info *si;
    struct parser_node *pn, *last;
    struct symbol_info *symbol_infos;
    int ret, extended;

    /* According to page 78 of AltoSubsystems_Oct79.pdf,
     *
     *   Address predefinitions allow groups of instructions to be
     *   placed in specific locations in the control memory, as is
     *   required by the OR branching scheme used by the Alto. Their
     *   syntax is:
     *     !n,k,name0,name1,name2,...,name{k-1};
     *   ...
     *   A more general variant of the predefinition facility is
     *   available. The syntax is:
     *      %mask2, mask1, init, L1, L2, ..., Ln;
     *   ...
     */
    tk = peek_token(p, FALSE);
    if (unlikely(!tk)) return ERROR;

    extended = (tk->str.s[0] != '!');

    ret = consume_punctuation(p, (extended) ? '%' : '!');
    if (ret != OK) return ret;

    st = new_statement(p);
    if (unlikely(!st)) return ERROR;
    st->st_type = ST_ADDRESS_PREDEFINITION;
    st->v.addr.extended = extended;
    st->v.addr.labels = NULL;
    st->v.addr.num_labels = 0;

    ret = parse_octal(p, &st->v.addr.n, FALSE);
    if (ret != OK) return ret;

    ret = consume_punctuation(p, ',');
    if (ret != OK) return ret;

    ret = parse_octal(p, &st->v.addr.k, FALSE);
    if (ret != OK) return ret;

    if (extended) {
        ret = consume_punctuation(p, ',');
        if (ret != OK) return ret;

        ret = parse_octal(p, &st->v.addr.l, FALSE);
        if (ret != OK) return ret;
    }

    last = NULL;
    symbol_infos = NULL;

    tk = peek_token(p, FALSE);
    if (unlikely(!tk)) return ERROR;
    while (TRUE) {
        if (tk->str.s[0] == ';') break;

        ret = consume_punctuation(p, ',');
        if (ret != OK) return ret;

        tk = peek_token(p, FALSE);
        if (unlikely(!tk)) return ERROR;
        if (tk->str.s[0] != ',' && tk->str.s[0] != ';') {
            ret = parse_name(p, &name);
            if (ret != OK) return ret;

            /* According to AltoSubsystems_Oct79.pdf,
             *  A predefinition must be the first mention of the
             *  name in the source text.
             */
            ret = add_symbol(p, &name, TRUE, &si);
            if (unlikely(ret == ERROR)) return ERROR;
            if (ret == FAIL) {
                return (!skip_semicolon(p)) ? ERROR : FAIL;
            }

            /* Do not set the addr field yet, only when
             * this statement is successfully parsed.
             */
            si->extra = (void *) symbol_infos;
            symbol_infos = si;

            tk = peek_token(p, FALSE);
            if (unlikely(!tk)) return ERROR;
        } else {
            name.s = "";
            name.len = 0;
            name.hash = string_hash(name.s, name.len);

            ret = add_symbol(p, &name, FALSE, &si);
            if (unlikely(ret == ERROR)) return ERROR;
            if (ret == FAIL) {
                if (tk->str.s[0] != ';')
                    return (!skip_semicolon(p)) ? ERROR : FAIL;
                return FAIL;
            }
        }

        pn = new_parser_node(p);
        if (unlikely(!pn)) return ERROR;

        pn->name = si->n.str;
        pn->next = NULL;
        pn->extra = si;

        if (last) last->next = pn;
        if (!st->v.addr.labels)
            st->v.addr.labels = pn;
        st->v.addr.num_labels++;
        last = pn;
    }

    ret = consume_punctuation(p, ';');
    if (ret != OK) return ret;

    /* Set the `addr` field of all symbol_infos. */
    si = symbol_infos;
    while (si) {
        symbol_infos = (struct symbol_info *) si->extra;
        si->extra = NULL;
        si->addr = st;
        si = symbol_infos;
    }

    append_statement(p, st);
    return OK;
}

/* Parses an executable statement.
 * Returns OK, FAIL or ERROR.
 */
static
int parse_executable_statement(struct parser *p)
{
    struct token *tk;
    struct statement *st;
    struct string name;
    struct clause *cl, *last;
    struct symbol_info *si;
    struct parser_node *lhs, *lhs_last;
    int is_first, is_goto;
    int ret;

    /* According to page 79 of AltoSubsystems_Oct79.pdf
     *
     *   Executable code statements consist of an optional label followed
     *   by a number of clauses separated by commas, and terminated with a
     *   semi-colon
     *     label: clause, clause, clause, ...;
     *   ....
     *
     *   Clauses are of three types: gotos, nondata functions, and
     *   assignments.
     *
     *   Goto clauses are of the form ':label' and cause the value of the
     *   label to be assembled into the NEXT field of the instruction. If
     *   the label is undefined, a chain of forward references is
     *   constructed which will be fixed up when the symbol is encountered
     *   as a label.
     *
     *   Nondata functions must be defined (by a literal symbol definition)
     *   before being encountered in a code clause. This type of clause
     *   assembles into the F1, F2, or F3 fields, and represent either a
     *   branch condition or a control function (e.g., BUS=0, TASK).
     *
     *   All data transfers (assignments) are specified by assignments of
     *   the form:
     *     dest1<- dest2<- ... <- source
     *   ...
     */

    st = new_statement(p);
    if (unlikely(!st)) return ERROR;

    st->st_type = ST_EXECUTABLE;
    st->v.exec.label.s = NULL;
    st->v.exec.label.len = 0;
    st->v.exec.label.hash = 0;
    st->v.exec.clauses = NULL;
    st->v.exec.si = NULL;

    si = NULL;
    is_first = TRUE;
    last = NULL;
    while (TRUE) {
        tk = peek_token(p, FALSE);
        if (unlikely(!tk)) return ERROR;

        is_goto = (tk->str.s[0] == ':');
        if (is_goto) {
            tk = get_token(p);
            if (unlikely(!tk)) return ERROR;
        }

        ret = parse_name(p, &name);
        if (ret != OK) return ret;

        tk = peek_token(p, FALSE);
        if (unlikely(!tk)) return ERROR;

        if (is_first && !is_goto) {
            if (tk->str.s[0] == ':') {
                tk = get_token(p);
                if (unlikely(!tk)) return ERROR;

                /* label declaration. */
                st->v.exec.label = name;
                is_first = FALSE;

                ret = add_symbol(p, &name, FALSE, &si);
                if (unlikely(ret == ERROR)) return ERROR;
                if (ret == FAIL) {
                    return (!skip_semicolon(p)) ? ERROR : FAIL;
                }

                continue;
            }
        }
        is_first = FALSE;

        cl = new_clause(p);
        if (unlikely(!cl)) return ERROR;

        cl->name = name;
        cl->lhs = NULL;
        cl->next = NULL;

        if (last) last->next = cl;
        last = cl;
        if (!st->v.exec.clauses)
            st->v.exec.clauses = cl;

        if (is_goto) {
            cl->c_type = CL_GOTO;
            cl->name = name;
            goto consume_comma;
        }

        if (tk->str.s[0] != '_') {
            cl->c_type = CL_FUNCTION;
            cl->name = name;
            goto consume_comma;
        }

        ret = consume_punctuation(p, '_');
        if (ret != OK) return ret;

        cl->c_type = CL_ASSIGNMENT;

        lhs = new_parser_node(p);
        if (unlikely(!lhs)) return ERROR;

        lhs->name = name;
        lhs->next = NULL;
        lhs->extra = NULL;
        lhs_last = lhs;
        cl->lhs = lhs;

        while (TRUE) {
            ret = parse_name(p, &name);
            if (ret != OK) return ret;

            tk = peek_token(p, FALSE);
            if (unlikely(!tk)) return ERROR;

            if (tk->str.s[0] != '_') {
                cl->name = name;
                break;
            }

            ret = consume_punctuation(p, '_');
            if (ret != OK) return ret;

            lhs = new_parser_node(p);
            if (unlikely(!lhs)) return ERROR;

            lhs->name = name;
            lhs->next = NULL;
            lhs->extra = NULL;

            lhs_last->next = lhs;
            lhs_last = lhs;
        }

    consume_comma:
        tk = peek_token(p, FALSE);
        if (unlikely(!tk)) return ERROR;

        if (tk->str.s[0] == ';') break;

        ret = consume_punctuation(p, ',');
        if (ret != OK) return ret;
    }

    ret = consume_punctuation(p, ';');
    if (ret != OK) return ret;

    if (si) {
        /* Set the symbol information. */
        if (si->exec) {
            /* In case label is already defined. */
            st = add_error(p, ERR_ALREADY_DEFINED);
            if (unlikely(!st)) return ERROR;
            st->v.err.name = si->n.str;
            return FAIL;
        }
        si->exec = st;
        st->v.exec.si = si;
    }

    append_statement(p, st);
    return OK;
}

/* Parses a single statement in the current file.
 * Returns OK, FAIL or ERROR.
 */
static
int parse_statement(struct parser *p)
{
    struct token *tk;
    char c;

    /* According to page 77 of AltoSubsystems_Oct79.pdf
     *
     *   Statements are of four basic types: include statements,
     *   declarations, address predefinitions, and executable code.
     */

    tk = peek_token(p, FALSE);
    if (unlikely(!tk)) return ERROR;

    c = tk->str.s[0];
    if (c == '\0' || c  == ';') {
        tk = get_token(p);
        return (tk != NULL) ? OK : ERROR;
    }

    if (c == '#')
        return parse_include_file(p);
    else if (c == '$')
        return parse_declaration(p);
    else if (c == '!' || c == '%')
        return parse_address_predefinition(p);
    else
        return parse_executable_statement(p);
}

/* Parses the statements in a given file.
 * Returns OK, FAIL or ERROR.
 */
static
int parse_statements(struct parser *p)
{
    struct token *tk;
    int ret, success;

    /* According to page 77 of AltoSubsystems_Oct79.pdf
     *
     *   An Alto microprogram consists of a number of statements and
     *   comments. Statements are terminated by semicolons, and everything
     *   between the semicolon and the next Return is treated as a
     *   comment. Statements can thus span several lines of text (the
     *   current limit is 500 characters). All other control characters
     *   and blanks are ignored. Bravo formatting is also ignored.
     *
     * Note: we do not impose character limits here.
     */
    success = TRUE;
    while (TRUE) {
        tk = peek_token(p, FALSE);
        if (unlikely(!tk)) return ERROR;

        if (tk->str.s[0] == '\0') break;
        ret = parse_statement(p);
        if (unlikely(ret == ERROR)) return ERROR;
        if (ret == FAIL) success = FALSE;
    }

    return (success) ? OK : FAIL;
}
