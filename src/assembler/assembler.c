
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "assembler/assembler.h"
#include "microcode/microcode.h"
#include "parser/parser.h"
#include "common/allocator.h"
#include "common/table.h"
#include "common/utils.h"

/* For attributes of literal symbols. */
#define LSA_L               (1 << 2)
#define LSA_BUS             (1 << 1)
#define LSA_ALU             (1 << 0)
#define LSA_MASK                 0x7

/* For types of literal symbols. */
#define LST_ILLEGAL                0
#define LST_UNDEF_ADDR            01
#define LST_DEF_ADDR              02
#define LST_RLOC_LHS              03
#define LST_RLOC_RHS              04
#define LST_CONSTANT              05
#define LST_BUS_SOURCE            06
#define LST_F1                    07
#define LST_DATA_F1_LHS          010
#define LST_L_DEFINING_F1        011
#define LST_F2                   012
#define LST_DATA_F2_LHS          013
#define LST_DATA_F2              014
#define LST_DATA_F2_RHS          015
#define LST_END                  016
#define LST_L_RHS                017
#define LST_L_LHS                020
#define LST_F3                   021
#define LST_DATA_F3_LHS          022
#define LST_DATA_F3_RHS          023
#define LST_ALUF                 024
#define LST_T_LHS                025
#define LST_T_RHS                026
#define LST_UNUSED               027
#define LST_PREDEF_ADDR          030
#define LST_LMRSHLMLSH           031
#define LST_MASK_CONST           032
#define LST_ASSIGN_F2            033
#define LST_ASSIGN_F1            034
#define LST_XMAR                 035

/* Macros. */
#define LITERAL_ATTRB_REQUIRE(n) (((n) >> 13) & 0x7)
#define LITERAL_ATTRB_DEFINE(n) (((n) >> 5) & 0x7)
#define LITERAL_ATTRB_EXTRA(n) (((n) >> 1) & 0x1)
#define LITERAL_SYMB_TYPE(n) (((n) >> 10) & 0x3F)
#define LITERAL_SYMB_VALUE(n) ((n) & 0x3FF)

/* Data structures and types. */

/* To build up the instruction. */
struct instruction {
    unsigned int f1, f2, f3;      /* F1, F2, and F3 fields .*/
    unsigned int rsel, aluf, bs;  /* RSEL, ALUF, and BS fields. */
    int has_f1, has_f2, has_f3;   /* Indicators of field presence. */
    int has_rsel, has_aluf, has_bs;

    int load_t, load_l;           /* Load L and Load T fields. */
    int has_constant;             /* Constant in RHS. */
    int has_m_constant;           /* M constant in RHS. */
    int has_special_constant;     /* Zero constant. */
    struct statement *goto_st;    /* The destination address. */

    struct assembler *as;         /* Pointer to the assembler. */
    struct statement *st;         /* Current statement. */
    struct statement *next_st;    /* The next statement. */
};

/* Functions. */
void assembler_initvar(struct assembler *as)
{
    allocator_initvar(&as->salloc);
    allocator_initvar(&as->oalloc);
    parser_initvar(&as->p);

    as->consts = NULL;
    as->const_sts = NULL;
    as->microcode = NULL;
    as->micro_sts = NULL;
}

void assembler_destroy(struct assembler *as)
{
    if (as->consts) free((void *) as->consts);
    as->consts = NULL;

    if (as->const_sts) free((void *) as->const_sts);
    as->const_sts = NULL;

    if (as->microcode) free((void *) as->microcode);
    as->microcode = NULL;

    if (as->micro_sts) free((void *) as->micro_sts);
    as->micro_sts = NULL;

    parser_destroy(&as->p);
    allocator_destroy(&as->oalloc);
    allocator_destroy(&as->salloc);
}

int assembler_create(struct assembler *as)
{
    assembler_initvar(as);

    if (unlikely(!allocator_create(&as->salloc, 0))) {
        report_error("assembler: create: "
                     "could not create string allocator");
        assembler_destroy(as);
        return FALSE;
    }

    if (unlikely(!allocator_create(&as->oalloc, DEFAULT_ALIGNMENT))) {
        report_error("assembler: create: "
                     "could not create object allocator");
        assembler_destroy(as);
        return FALSE;
    }

    if (unlikely(!parser_create(&as->p, &as->salloc, &as->oalloc))) {
        report_error("assembler: create: "
                     "could not create parser");
        assembler_destroy(as);
        return FALSE;
    }

    as->consts = (uint16_t *)
        malloc(CONSTANT_SIZE * sizeof(uint16_t));
    as->const_sts = (struct statement **)
        malloc(CONSTANT_SIZE * sizeof(struct statement *));
    as->microcode = (uint32_t *)
        malloc(MICROCODE_SIZE * sizeof(uint32_t));
    as->micro_sts = (struct statement **)
        malloc(MICROCODE_SIZE * sizeof(struct statement *));

    if (unlikely(!as->consts || !as->const_sts
                 || !as->microcode || !as->micro_sts)) {
        report_error("assembler: create: memory exhausted");
        assembler_destroy(as);
        return FALSE;
    }

    return TRUE;
}

/* Finds an empty slot for the constant.
 * The parameter `val` is the value of the constant,
 * the parameter `bs` is for the lower 3 bits of address,
 * and is only used when `has_bs_mask` is set to TRUE.
 * Returns the slot number.
 */
static
unsigned int find_constant_slot(struct assembler *as, uint16_t val,
                                unsigned int bs, int has_bs_mask)
{
    unsigned int i;
    for (i = 0; i < CONSTANT_SIZE; i++) {
        if (as->const_sts[i]) {
            if (as->consts[i] != val) continue;
        }
        if (!has_bs_mask) break;
        if ((i & 7) == bs) break;
    }
    return i;
}

int assembler_resolve_constants(struct assembler *as)
{
    struct statement *st;
    unsigned int i, bs;
    uint16_t val;
    int has_bs_mask;

    memset(as->consts, -1, CONSTANT_SIZE * sizeof(uint16_t));
    memset(as->const_sts, 0, CONSTANT_SIZE * sizeof(struct statement *));

    st = as->p.first;
    while (st) {
        switch (st->st_type) {
        case ST_DECLARATION:
            switch (st->v.decl.d_type) {
            case DECL_SYMBOL:
                if (LITERAL_SYMB_TYPE(st->v.decl.n2) != LST_CONSTANT)
                    goto skip;
                val = (uint16_t) LITERAL_SYMB_VALUE(st->v.decl.n2);
                bs = 0;
                has_bs_mask = FALSE;
                break;

            case DECL_CONSTANT:
                val = (uint16_t) st->v.decl.n1;
                bs = 0;
                has_bs_mask = FALSE;
                break;

            case DECL_M_CONSTANT:
                val = (uint16_t) st->v.decl.n2;
                bs = (unsigned int) st->v.decl.n1;
                has_bs_mask = TRUE;
                break;

            default:
                goto skip;
            }
            i = find_constant_slot(as, val, bs, has_bs_mask);
            if (i >= CONSTANT_SIZE) {
                report_error("assembler: resolve_constants: %s:%d: overflow",
                             st->filename, st->line_num);
                return FALSE;
            }
            as->const_sts[i] = st;
            as->consts[i] = val;
            st->v.decl.si->address = i;
            break;

        default:
            break;
        }

    skip:
        st = st->next;
    }

    return TRUE;
}

/* Finds an empty slot for the microcode and assignes the labels.
 * The pointer to the address_predefinition is in `apdef`.
 * Returns the slot number.
 */
static
unsigned int find_microcode_slot(struct assembler *as,
                                 struct address_predefinition *apdef)
{
    unsigned int i, j, num, num_labels;
    unsigned int mask1, mask2, not_mask2;
    unsigned int len, start, val1, val2;
    struct parser_node *pn;
    struct symbol_info *si;

    pn = apdef->labels;
    num_labels = apdef->num_labels;

    if (apdef->extended) {
        /* Not sure if this is correct.
         * This is guess work, which was entirely based on the
         * addresses of the generated ROM file.
         */
        mask1 = apdef->k;
        mask2 = apdef->n;
        start = apdef->l;

        not_mask2 = 1;
        while (not_mask2 <= mask2)
            not_mask2 <<= 1;
        not_mask2 = (not_mask2 - 1) ^ mask2;

        for (i = 0; i < MICROCODE_SIZE; i++) {
            if ((i & mask1) != start) continue;
            if (as->micro_sts[i]) continue;

            j = 0;
            for (num = 0; num < num_labels; num++) {
                if (num > 0) {
                    val1 = i & not_mask2;
                    val2 = (i + j) & mask2;
                    while ((((i + j) & not_mask2) != val1)
                           || ((i + j) & mask2) == val2)
                        j++;
                    if ((((i + j) & mask2) == (i & mask2)))
                        break;
                }

                if (as->micro_sts[i + j]) break;
            }
            if (num == num_labels) break;
        }

        if (i == MICROCODE_SIZE) return i;

        j = 0;
        for (num = 0; num < num_labels; num++) {
            if (!pn) break;
            si = (struct symbol_info *) pn->extra;
            pn = pn->next;

            if (num > 0) {
                val1 = i & not_mask2;
                val2 = (i + j) & mask2;
                while ((((i + j) & not_mask2) != val1)
                       || ((i + j) & mask2) == val2)
                    j++;
            }

            if (si) {
                si->address = i + j;
                as->micro_sts[si->address] = si->exec;
            }
        }
    } else {
        mask1 = apdef->n;
        len = apdef->k;
        for (i = 0; i < MICROCODE_SIZE; i++) {
            if ((i + len - 1) >= MICROCODE_SIZE) continue;
            if (((i + len - 1) & mask1) != mask1) continue;
            for (j = 0; j < len; j++) {
                if (as->micro_sts[i + j]) break;
            }
            if (j == len) break;
        }

        if (i == MICROCODE_SIZE) return i;

        for (j = 0; j < len; j++) {
            if (!pn) break;
            si = (struct symbol_info *) pn->extra;
            pn = pn->next;

            if (!si) continue;
            si->address = i + j;
            as->micro_sts[si->address] = si->exec;
        }
    }
    return i;
}

int assembler_resolve_labels(struct assembler *as)
{
    struct statement *st;
    struct address_predefinition apdef;
    struct symbol_info *si;
    unsigned int i;

    memset(as->micro_sts, 0,
           MICROCODE_SIZE * sizeof(struct statement *));

    st = as->p.first;
    while (st) {
        switch (st->st_type) {
        case ST_DECLARATION:
            if (st->v.decl.d_type != DECL_SYMBOL)
                break;
            break;

        case ST_ADDRESS_PREDEFINITION:
            i = find_microcode_slot(as, &st->v.addr);
            if (i == MICROCODE_SIZE)
                goto error_overflow;
            break;

        case ST_EXECUTABLE:
            si = st->v.exec.si;
            if (si) {
                /* To resolve the address. */
                if (!si->addr) si = NULL;
            }

            if (si) {
                i = st->v.exec.si->address;
            } else {
                apdef.n = 0;
                apdef.k = 1;
                apdef.l = 0;
                apdef.extended = FALSE;
                apdef.labels = NULL;
                apdef.num_labels = 0;
                i = find_microcode_slot(as, &apdef);
                if (i >= MICROCODE_SIZE)
                    goto error_overflow;
                si = st->v.exec.si;
                if (si) si->address = i;
            }
            st->v.exec.address = i;
            as->micro_sts[i] = st;
            break;

        default:
            break;
        }

        st = st->next;
    }

    return TRUE;

error_overflow:
    report_error("assembler: resolve_labels: %s:%d: overflow",
                 st->filename, st->line_num);
    return FALSE;
}

/* Resolves a symbol by name to symbol_info.
 * Returns the symbol information.
 */
static
struct symbol_info *resolve_symbol(struct assembler *as,
                                   const struct string *name)
{
    struct string_node *n;
    struct symbol_info *si;

    n = table_find(&as->p.symbols, name);
    if (n) {
        size_t offset;
        offset = offsetof(struct symbol_info, n);
        si = (struct symbol_info *) &(((char *) n)[-offset]);
    } else {
        si = NULL;
    }
    return si;
}

/* Resolves the RHS of an assignment expression.
 * Returns the
 */
static
void resolve_rhs(struct assembler *as,
                 const struct string *name,
                 struct symbol_info **first,
                 struct symbol_info **second)
{
    struct string copy;
    struct symbol_info *si;
    size_t i;

    /* Tries to resolve the symbol as is. */
    si = resolve_symbol(as, name);
    if (si) {
        *first = si;
        *second = NULL;
        return;
    }

    /* Break the RHS into 2 parts, and resolve each
     * one independently.
     */
    for (i = 1; i + 1 < name->len; i++) {
        copy.s = name->s;
        copy.len = i;
        copy.hash = string_hash(copy.s, copy.len);

        si = resolve_symbol(as, &copy);
        if (!si) continue;
        *first = si;

        copy.s = &name->s[i];
        copy.len = name->len - i;
        copy.hash = string_hash(copy.s, copy.len);

        si = resolve_symbol(as, &copy);
        if (!si) continue;

        *second = si;
        return;
    }

    *first = NULL;
    *second = NULL;
}

#define CREATE_SET_FUNCTION(field) \
static int set_ ## field(struct instruction *insn, unsigned int val) \
{                                                                    \
    struct statement *st;                                            \
    st = insn->st;                                                   \
    if (insn->has_ ## field && val != insn->field) {                 \
        report_error("assembler: assemble: %s:%d: "                  \
                     "can only have one " #field " per statement",   \
                     st->filename, st->line_num);                    \
        return FALSE;                                                \
    }                                                                \
    insn->has_ ## field = TRUE;                                      \
    insn->field = val;                                               \
    return TRUE;                                                     \
}

CREATE_SET_FUNCTION(f1)
CREATE_SET_FUNCTION(f2)
CREATE_SET_FUNCTION(f3)
CREATE_SET_FUNCTION(rsel)
CREATE_SET_FUNCTION(aluf)
CREATE_SET_FUNCTION(bs)

/* Processes a GOTO clause.
 * Returns TRUE on success.
 */
static
int process_goto_clause(struct instruction *insn,
                        struct clause *cl)
{
    struct statement *st;
    struct symbol_info *si;
    struct declaration *decl;

    st = insn->st;
    if (insn->goto_st) {
        report_error("assembler: assemble: %s:%d: "
                     "can only have one GOTO per statement",
                     st->filename, st->line_num);
        return FALSE;
    }

    si = resolve_symbol(insn->as, &cl->name);
    if (!si) {
        report_error("assembler: assemble: %s:%d: "
                     "could not find GOTO target %s",
                     st->filename, st->line_num,
                     cl->name.s);
        return FALSE;
    }

    if (si->exec) {
        insn->goto_st = si->exec;
    } else if (si->decl) {
        decl = &si->decl->v.decl;
        if (decl->d_type != DECL_SYMBOL)
            goto error_invalid_label;

        if (LITERAL_SYMB_TYPE(decl->n1) != LST_DEF_ADDR)
            goto error_invalid_label;

        insn->goto_st = si->decl;
    } else {
        goto error_invalid_label;
    }

    return TRUE;

error_invalid_label:
    report_error("assembler: assemble: %s:%d: "
                 "GOTO target %s is not a valid label",
                 st->filename, st->line_num,
                 si->n.str.s);
    return FALSE;
}

/* Processes a NONDATA FUNCTION clause.
 * Returns TRUE on success.
 */
static
int process_function_clause(struct instruction *insn,
                            struct clause *cl)
{
    struct statement *st;
    struct symbol_info *si;
    struct declaration *decl;
    unsigned int lst, val;

    st = insn->st;
    si = resolve_symbol(insn->as, &cl->name);
    if (!si) {
        report_error("assembler: assemble: %s:%d: "
                     "could not find NONDATA FUNCTION %s",
                     st->filename, st->line_num,
                     cl->name.s);
        return FALSE;
    }

    if (!si->decl) {
        report_error("assembler: assemble: %s:%d: "
                     "NONDATA FUNCTION %s has no declaration",
                     st->filename, st->line_num,
                     si->n.str.s);
        return FALSE;;
    }

    decl = &si->decl->v.decl;
    if (decl->d_type != DECL_SYMBOL) {
        report_error("assembler: assemble: %s:%d: "
                     "%s is not literal symbol",
                     st->filename, st->line_num,
                     decl->name.s);
        return FALSE;
    }

    lst = LITERAL_SYMB_TYPE(decl->n1);
    if (lst != LST_F1 && lst != LST_F2 && lst != LST_F3) {
        report_error("assembler: assemble: %s:%d: "
                     "%s is not valid NONDATA FUNCTION (%d)",
                     st->filename, st->line_num,
                     decl->name.s, lst);
        return FALSE;
    }

    val = LITERAL_SYMB_VALUE(decl->n1);
    if (lst == LST_F1)
        return set_f1(insn, val);

    if (lst == LST_F2)
        return set_f2(insn, val);

    if (lst == LST_F3)
        return set_f3(insn, val);

    return TRUE;
}

/* Processes an assignment clause.
 * Returns TRUE on success.
 */
static
int process_assignment_clause(struct instruction *insn,
                              struct clause *cl)
{
    struct statement *st;
    struct symbol_info *si, *si_extra;
    struct parser_node *pn;
    struct declaration *decl;
    unsigned int lst;
    unsigned int req, def;
    unsigned int val;
    int has_load_t;

    has_load_t = FALSE;
    st = insn->st;
    pn = cl->lhs;

    req = LSA_L | LSA_BUS | LSA_ALU;
    while (pn) {
        si = resolve_symbol(insn->as, &pn->name);
        if (!si) {
            report_error("assembler: assemble: %s:%d: "
                         "could not find LHS %s",
                         st->filename, st->line_num,
                         pn->name.s);
            return FALSE;
        }

        if (!si->decl) {
            report_error("assembler: assemble: %s:%d: "
                         "LHS %s has no declaration",
                         st->filename, st->line_num,
                         si->n.str.s);
            return FALSE;
        }

        decl = &si->decl->v.decl;
        pn = pn->next;

        if (decl->d_type == DECL_SYMBOL) {
            lst = LITERAL_SYMB_TYPE(decl->n1);
            val = LITERAL_SYMB_VALUE(decl->n1);
            req &= LITERAL_ATTRB_REQUIRE(decl->n3);

            if (lst == LST_T_LHS) {
                insn->load_t = TRUE;
                has_load_t = TRUE;
            } else if (lst == LST_L_LHS) {
                insn->load_l = TRUE;
            } else if (lst == LST_DATA_F1_LHS) {
                if (!set_f1(insn, val))
                    return FALSE;
            } else if (lst == LST_DATA_F2_LHS) {
                if (!set_f2(insn, val))
                    return FALSE;
            } else if (lst == LST_DATA_F3_LHS) {
                if (!set_f3(insn, val))
                    return FALSE;
            } else if (lst == LST_XMAR) {
                if (!set_f1(insn, F1_LOAD_MAR))
                    return FALSE;
                if (!set_f2(insn, F2_STORE_MD))
                    return FALSE;
            } else if (lst == LST_DATA_F2) {
                if (!set_f2(insn, val))
                    return FALSE;
                if (!set_bs(insn, BS_LOAD_R))
                    return FALSE;
                if (!set_rsel(insn, R_ZERO))
                    return FALSE;
            } else {
                report_error("assembler: assemble: %s:%d: "
                             "%s has no valid declaration as "
                             "LHS (%d)",
                             st->filename, st->line_num,
                             decl->name.s, lst);
                return FALSE;
            }
        } else if (decl->d_type == DECL_R_MEMORY) {
            if (decl->n1 <= R_MASK) {
                if (!set_rsel(insn, decl->n1 & R_MASK))
                    return FALSE;
                if (!set_bs(insn, BS_LOAD_R))
                    return FALSE;
            } else {
                if (!set_rsel(insn, decl->n1 & R_MASK))
                    return FALSE;
                if (!set_bs(insn, BS_TASK_SPECIFIC2))
                    return FALSE;
            }
        } else {
            report_error("assembler: assemble: %s:%d: "
                         "%s has no valid declaration as RDEST (%d)",
                         st->filename, st->line_num,
                         decl->name.s, decl->d_type);
            return FALSE;
        }
    }

    resolve_rhs(insn->as, &cl->name, &si, &si_extra);
    if (!si) {
        report_error("assembler: assemble: %s:%d: "
                     "%s is not a valid RHS",
                     st->filename, st->line_num,
                     cl->name.s);
        return FALSE;
    }

    if (!si->decl) {
        report_error("assembler: assemble: %s:%d: "
                     "RHS %s has no declaration",
                     st->filename, st->line_num,
                     si->n.str.s);
        return FALSE;
    }

    decl = &si->decl->v.decl;

    def = 0;
    if (decl->d_type == DECL_SYMBOL) {
        def = LITERAL_ATTRB_DEFINE(decl->n3);
        lst = LITERAL_SYMB_TYPE(decl->n2);
        val = LITERAL_SYMB_VALUE(decl->n2);

        if (lst == LST_BUS_SOURCE) {
            if (!set_bs(insn, val))
                return FALSE;
        } else if (lst == LST_L_DEFINING_F1) {
            if (!set_f1(insn, val))
                return FALSE;
        } else if (lst == LST_ASSIGN_F2) {
            if (!set_f2(insn, val))
                return FALSE;
            if (!set_bs(insn, BS_NONE))
                return FALSE;
        } else if (lst == LST_ASSIGN_F1) {
            if (!set_f1(insn, val))
                return FALSE;
            if (!set_bs(insn, BS_NONE))
                return FALSE;
        } else if (lst == LST_DATA_F2_RHS) {
            if (!set_f2(insn, val))
                return FALSE;
            if (!set_bs(insn, BS_READ_R))
                return FALSE;
            if (!set_rsel(insn, R_ZERO))
                return FALSE;
        } else if (lst == LST_T_RHS) {
            if (!set_aluf(insn, ALU_T))
                return FALSE;
        } else if (lst == LST_L_RHS) {
        } else if (lst == LST_CONSTANT) {
            insn->has_special_constant = TRUE;
        } else if (lst == LST_LMRSHLMLSH) {
            if (!set_f1(insn, val))
                return FALSE;
            if (!set_f2(insn, F2_EMU_MAGIC))
                return FALSE;
        } else {
            report_error("assembler: assemble: %s:%d: "
                         "unknown RHS literal symbol (%u)",
                         st->filename, st->line_num,
                         lst);
            return FALSE;
        }
    } else if (decl->d_type == DECL_CONSTANT) {
        def = LSA_BUS;
        insn->has_constant = TRUE;
        if (!set_rsel(insn, CONST_ADDR_RSEL(decl->si->address)))
            return FALSE;
        if (!set_bs(insn, CONST_ADDR_BS(decl->si->address)))
            return FALSE;
    } else if (decl->d_type == DECL_M_CONSTANT) {
        def = LSA_BUS;
        insn->has_m_constant = TRUE;
        if (!set_rsel(insn, CONST_ADDR_RSEL(decl->si->address)))
            return FALSE;
    } else if (decl->d_type == DECL_R_MEMORY) {
        def = LSA_BUS;
        if (decl->n1 <= R_MASK) {
            if (!set_rsel(insn, decl->n1 & R_MASK))
                return FALSE;
            if (!set_bs(insn, BS_READ_R))
                return FALSE;
        } else {
            if (!set_rsel(insn, decl->n1 & R_MASK))
                return FALSE;
            if (!set_bs(insn, BS_TASK_SPECIFIC1))
                return FALSE;
        }
    }

    if (si_extra) {
        if (!si_extra->decl) {
            report_error("assembler: assemble: %s:%d: "
                         "RHS suffix %s has no declaration",
                         st->filename, st->line_num,
                         si_extra->n.str.s);
            return FALSE;
        }
        decl = &si_extra->decl->v.decl;

        if (decl->d_type != DECL_SYMBOL) {
            report_error("assembler: assemble: %s:%d: "
                         "RHS suffix %s literal symbol declaration",
                         st->filename, st->line_num,
                         decl->name.s);
            return FALSE;
        }

        lst = LITERAL_SYMB_TYPE(decl->n2);
        if (lst == LST_ALUF) {
            if (!set_aluf(insn, LITERAL_SYMB_VALUE(decl->n2)))
                return FALSE;

            if (has_load_t && !LOAD_T_FROM_ALU(insn->aluf)) {
                /* Issue a warning here. */
                report_error("assembler: assemble: %s:%d: "
                             "cannot load T from this ALUF (warning)",
                             st->filename, st->line_num);
                /* Do not return FALSE, as it is only a warning. */
            }
        } else {
            report_error("assembler: assemble: %s:%d: "
                         "unknown RHS suffix literal symbol (%u)",
                         st->filename, st->line_num,
                         lst);
            return FALSE;
        }

        def |= LITERAL_ATTRB_DEFINE(decl->n3);
        if (LITERAL_ATTRB_EXTRA(decl->n3))
            def |= LSA_ALU;
    }

    if ((req | def) == (LSA_L | LSA_BUS)) {
        /* Gate the ALU results. */
        def |= LSA_ALU;
        if (!set_aluf(insn, ALU_BUS))
            return FALSE;
    }

    if ((req | def) != (LSA_L | LSA_BUS | LSA_ALU)) {
        report_error("assembler: assemble: %s:%d: "
                     "requirements are not met in clause",
                     st->filename, st->line_num);
        return FALSE;
    }

    return TRUE;
}

/* Assembles a single statement.
 * A pointer to the next executable statement is also passed
 * to resolve the NEXT field in case of missing GOTO.
 * Returns TRUE if assembled properly.
 */
static
int assemble_one(struct assembler *as, struct statement *st,
                 struct statement *next_st)
{
    struct instruction insn;
    struct clause *cl;
    unsigned int new_f1, new_f2;
    uint32_t microcode;
    int error;

    memset(&insn, 0, sizeof(insn));
    insn.as = as;
    insn.st = st;
    insn.next_st = next_st;

    error = FALSE;
    cl = st->v.exec.clauses;
    while (cl) {
        switch (cl->c_type) {
        case CL_GOTO:
            if (!process_goto_clause(&insn, cl))
                error = TRUE;
            break;

        case CL_FUNCTION:
            if (!process_function_clause(&insn, cl))
                error = TRUE;
            break;

        case CL_ASSIGNMENT:
            if (!process_assignment_clause(&insn, cl))
                error = TRUE;
            break;
        }
        cl = cl->next;
    }

    new_f1 = F1_CONSTANT;
    new_f2 = F2_CONSTANT;
    if ((insn.f1 != new_f1) && (insn.f2 != new_f2)) {
        if (insn.has_constant
            || (insn.has_special_constant && (insn.bs != BS_LOAD_R))
            || (insn.has_m_constant && (!BS_USE_CROM(insn.bs)))) {
            if (insn.has_f1 && insn.has_f2) {
                error = TRUE;
                report_error("assembler: assemble: %s:%d: "
                             "could not set F1 or F2 for constant",
                             st->filename, st->line_num);
            } else if (!insn.has_f1) {
                insn.has_f1 = TRUE;
                insn.f1 = new_f1;
            } else {
                insn.has_f2 = TRUE;
                insn.f2 = new_f2;
            }
        }
    }

    if (!insn.goto_st && !insn.next_st) {
        report_error("assembler: assemble: %s:%d: "
                     "impossible to determine next instruction",
                     st->filename, st->line_num);
        return FALSE;
    }

    if (error) return FALSE;

    if (!insn.goto_st) insn.goto_st = insn.next_st;
    if (insn.goto_st->st_type == ST_EXECUTABLE) {
        microcode = insn.goto_st->v.exec.address;
    } else {
        /* To support defined labels. */
        microcode = LITERAL_SYMB_VALUE(insn.goto_st->v.decl.n1);
    }
    microcode &= 0x3FF;

    microcode |= (insn.rsel & 0x1F) << 27;
    microcode |= (insn.aluf & 0x0F) << 23;
    microcode |= (insn.bs & 0x07) << 20;
    microcode |= (insn.f1 & 0x0F) << 16;
    microcode |= (insn.f2 & 0x0F) << 12;
    if (insn.load_t) microcode |= (1 << 11);
    if (insn.load_l) microcode |= (1 << 10);

    as->microcode[st->v.exec.address] = microcode;
    return TRUE;
}

int assembler_assemble(struct assembler *as)
{
    struct statement *st, *next_st;
    int error;

    memset(as->microcode, 0, MICROCODE_SIZE * sizeof(uint32_t));

    error = FALSE;
    next_st = as->p.first;
    st = NULL;
    while (next_st) {
        if (next_st->st_type != ST_EXECUTABLE) {
            next_st = next_st->next;
            continue;
        }

        if (st) {
            if (!assemble_one(as, st, next_st))
                error = TRUE;
        }

        st = next_st;
        next_st = next_st->next;
    }

    if (st) {
        if (!assemble_one(as, st, next_st))
            error = TRUE;
    }

    return (!error);
}

int assembler_dump_constant_rom(struct assembler *as,
                                const char *filename)
{
    FILE *fp;
    unsigned int i;
    uint16_t val;
    char c;

    if (!filename) return TRUE;
    fp = fopen(filename, "wb");
    if (unlikely(!fp)) {
        report_error("assembler: dump_constant_rom: cannot open `%s`",
                     filename);
        return FALSE;
    }

    for (i = 0; i < CONSTANT_SIZE; i++) {
        val = as->consts[i];
        c = (char) (val & 0xFF);
        fwrite(&c, 1, 1, fp);
        c = (char) ((val >> 8) & 0xFF);
        fwrite(&c, 1, 1, fp);
    }

    fclose(fp);
    return TRUE;
}

int assembler_dump_microcode_rom(struct assembler *as,
                                 const char *filename)
{
    FILE *fp;
    unsigned int i;
    uint32_t val;
    char c;

    if (!filename) return TRUE;
    fp = fopen(filename, "wb");
    if (unlikely(!fp)) {
        report_error("assembler: dump_microcode_rom: cannot open `%s`",
                     filename);
        return FALSE;
    }

    for (i = 0; i < MICROCODE_SIZE; i++) {
        val = as->microcode[i];
        c = (char) (val & 0xFF);
        fwrite(&c, 1, 1, fp);
        c = (char) ((val >> 8) & 0xFF);
        fwrite(&c, 1, 1, fp);
        c = (char) ((val >> 16) & 0xFF);
        fwrite(&c, 1, 1, fp);
        c = (char) ((val >> 24) & 0xFF);
        fwrite(&c, 1, 1, fp);
    }

    fclose(fp);
    return TRUE;
}

/* Prints the constants in the listing. */
static
void print_constants(struct assembler *as, FILE *fp)
{
    struct statement *st;
    unsigned int i;
    char buffer[64];

    fprintf(fp, "--- CONSTANTS ---\n");
    fprintf(fp, "ADDRESS  VALUE     NAME          "
                "DEFINITION             LOCATION\n");
    for (i = 0; i < CONSTANT_SIZE; i++) {
        st = as->const_sts[i];
        if (!st) {
            fprintf(fp, "%03o      %o\n", i, as->consts[i]);
            continue;
        }

        fprintf(fp, "%03o      %-6o    ", i, as->consts[i]);
        fprintf(fp, "$%-12s ", st->v.decl.name.s);

        buffer[0] = '\0';
        switch (st->v.decl.d_type) {
        case DECL_SYMBOL:
            sprintf(buffer, "$L%05o,%05o,%06o",
                    st->v.decl.n1, st->v.decl.n2, st->v.decl.n3);
            break;
        case DECL_CONSTANT:
            sprintf(buffer, "$%o", st->v.decl.n1);
            break;
        case DECL_M_CONSTANT:
            sprintf(buffer, "$M%o:%o", st->v.decl.n1, st->v.decl.n2);
            break;
        default:
            break;
        }

        fprintf(fp, "%-22s ", buffer);
        fprintf(fp, "%s:%u\n", st->filename, st->line_num);
    }
}

/* Prints the R memory declarations in the listing. */
static
void print_r_memory_declarations(struct assembler *as, FILE *fp)
{
    struct statement *st;

    fprintf(fp, "--- R MEMORY DECLARATIONS ---\n");
    fprintf(fp, "NAME          DEFINITION  LOCATION\n");
    st = as->p.first;
    while (st) {
        if (st->st_type != ST_DECLARATION) goto skip;
        if (st->v.decl.d_type != DECL_R_MEMORY) goto skip;

        fprintf(fp, "$%-12s ", st->v.decl.name.s);
        fprintf(fp, "$R%-2o        ", st->v.decl.n1);
        fprintf(fp, "%s:%u\n", st->filename, st->line_num);
    skip:
        st = st->next;
    }
}

/* Prints the literal symbols. */
static
void print_literal_symbols(struct assembler *as, FILE *fp)
{
    static const char *attr_name[] = {
        "---",
        "--A",
        "-B-",
        "-BA",
        "L--",
        "L-A",
        "LB-",
        "LBA"
    };
    struct statement *st;
    unsigned int req;

    fprintf(fp, "--- LITERAL SYMBOLS ---\n");
    fprintf(fp, "NAME          TYPE  VAL   "
                "RHS_TYPE RHS_VAL REQ DEF EXTRA LOCATION\n");
    st = as->p.first;
    while (st) {
        if (st->st_type != ST_DECLARATION) goto skip;
        if (st->v.decl.d_type != DECL_SYMBOL) goto skip;

        fprintf(fp, "$%-12s ", st->v.decl.name.s);
        fprintf(fp, "%02o    ", LITERAL_SYMB_TYPE(st->v.decl.n1));
        fprintf(fp, "%04o  ", LITERAL_SYMB_VALUE(st->v.decl.n1));
        fprintf(fp, "%02o       ", LITERAL_SYMB_TYPE(st->v.decl.n2));
        fprintf(fp, "%04o    ", LITERAL_SYMB_VALUE(st->v.decl.n2));
        req = LITERAL_ATTRB_REQUIRE(st->v.decl.n3);
        fprintf(fp, "%s ", attr_name[LSA_MASK ^ req]);
        fprintf(fp, "%s ", attr_name[LITERAL_ATTRB_DEFINE(st->v.decl.n3)]);
        fprintf(fp, "%o     ", LITERAL_ATTRB_EXTRA(st->v.decl.n3));

        fprintf(fp, "%s:%u\n", st->filename, st->line_num);
    skip:
        st = st->next;
    }

    fprintf(fp, "\n");
    fprintf(fp, "-- SYMBOL TYPES ---\n");
    fprintf(fp, "TYPE  LEGAL AS    FIELDS      DESCRIPTION\n");
    fprintf(fp, "00    NEVER                   ILLEGAL\n");
    fprintf(fp, "01    ADDRESS                 UNDEFINED ADDRESS\n");
    fprintf(fp, "02    ADDRESS     NEXT        DEFINED ADDDRESS\n");
    fprintf(fp, "03    LHS         RSEL        R LOCATION LHS\n");
    fprintf(fp, "04    RHS         RSEL        R LOCATION RHS\n");
    fprintf(fp, "05    RHS         RSEL,BS     CONSTANT\n");
    fprintf(fp, "06    RHS         BS          BUS SOURCE\n");
    fprintf(fp, "07    CLAUSE      F1          NONDATA F1\n");
    fprintf(fp, "10    LHS         F1          DATA F1 LHS\n");
    fprintf(fp, "11    RHS         F1          L DEFINING F1\n");
    fprintf(fp, "12    CLAUSE      F2          NONDATA F2\n");
    fprintf(fp, "13    LHS         F2          DATA F2 LHS\n");
    fprintf(fp, "14    LHS         F2          DATA F2 LHS "
                "[BS<- 0, RSEL<- 0]\n");
    fprintf(fp, "15    RHS         F2          DATA F2 (RHS) "
                "[BS<- 1, RSEL<- 0]\n");
    fprintf(fp, "16    CLAUSE                  END\n");
    fprintf(fp, "17    RHS                     READ L\n");
    fprintf(fp, "20    LHS         LOADL       LOAD L\n");
    fprintf(fp, "21    CLAUSE      F3          NONDATA F3\n");
    fprintf(fp, "22    LHS         F3          DATA F3 LHS\n");
    fprintf(fp, "23    RHS         F3          DATA F3 RHS\n");
    fprintf(fp, "24    RHS         ALUF        ALU FUNCTIONS\n");
    fprintf(fp, "25    LHS         LOADT       LOAD T\n");
    fprintf(fp, "26    RHS                     READ T\n");
    fprintf(fp, "27                            UNUSED\n");
    fprintf(fp, "30    ADDRESS                 PREDEFINED ADDRESS\n");
    fprintf(fp, "31    RHS                     LMRSH,LMLSH\n");
    fprintf(fp, "32    RHS                     READ MASK CONSTANT\n");
    fprintf(fp, "33    RHS         F2          READ F2 [BS<- 2]\n");
    fprintf(fp, "34    RHS         F1          READ F1 [BS<- 2]\n");
    fprintf(fp, "35    LHS         F1,F2       XMAR\n");
}

static
void print_microcode(struct assembler *as, FILE *fp)
{
    struct statement *st;
    struct clause *cl;
    struct parser_node *pn;
    uint32_t microcode;
    unsigned int i, j;

    fprintf(fp, "--- MICROCODE ---\n");
    fprintf(fp, "ADDRESS   MICROCODE    RSEL ALUF BS F1 F2 T L NEXT "
                "LABEL      STATEMENT\n");

    for (i = 0; i < MICROCODE_SIZE; i++) {
        microcode = as->microcode[i];
        fprintf(fp, "%05o     %011o  ", i, microcode);
        fprintf(fp, "%02o   %02o   %o  %02o %02o %o %o %04o ",
                MICROCODE_RSEL(microcode),
                MICROCODE_ALUF(microcode),
                MICROCODE_BS(microcode),
                MICROCODE_F1(microcode),
                MICROCODE_F2(microcode),
                MICROCODE_T(microcode),
                MICROCODE_L(microcode),
                MICROCODE_NEXT(microcode));

        st = as->micro_sts[i];
        if (!st) {
            fprintf(fp, "\n");
            continue;
        }

        switch (st->st_type) {
        case ST_DECLARATION:
            break;
        case ST_EXECUTABLE:
            if (st->v.exec.label.s) {
                fprintf(fp, "%s:", st->v.exec.label.s);
                j = (unsigned int) st->v.exec.label.len;
                while (j++ < 10) {
                    fprintf(fp, " ");
                }
            } else {
                fprintf(fp, "%-10s ", "");
            }
            cl = st->v.exec.clauses;
            while (cl) {
                switch (cl->c_type) {
                case CL_GOTO:
                    fprintf(fp, ":%s", cl->name.s);
                    break;
                case CL_FUNCTION:
                    fprintf(fp, "%s", cl->name.s);
                    break;
                case CL_ASSIGNMENT:
                    pn = cl->lhs;
                    while (pn) {
                        fprintf(fp, "%s<- ", pn->name.s);
                        pn = pn->next;
                    }
                    fprintf(fp, "%s", cl->name.s);
                    break;
                }
                if (cl->next) fprintf(fp, ", ");
                cl = cl->next;
            }
            break;
        default:
            break;
        }
        fprintf(fp, "\n");
    }
}

int assembler_print_listing(struct assembler *as, const char *filename)
{
    FILE *fp;

    if (!filename) return TRUE;
    fp = fopen(filename, "w");
    if (unlikely(!fp)) {
        report_error("assembler: print_listing: cannot open `%s`",
                     filename);
        return FALSE;
    }

    print_constants(as, fp);
    fprintf(fp, "\n\n");
    print_r_memory_declarations(as, fp);
    fprintf(fp, "\n\n");
    print_literal_symbols(as, fp);
    fprintf(fp, "\n\n");
    print_microcode(as, fp);

    fclose(fp);
    return TRUE;
}
