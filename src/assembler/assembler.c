
/* To implement the assembler, the AltoSubsystems_Oct79.pdf manual
 * was used. It can be found at:
 *   https://bitsavers.computerhistory.org/pdf/xerox/alto/
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "assembler/assembler.h"
#include "microcode/microcode.h"
#include "parser/parser.h"
#include "common/allocator.h"
#include "common/table.h"
#include "common/serdes.h"
#include "common/utils.h"

/* For attributes of literal symbols. */
#define LSA_L                       (1 << 2)
#define LSA_BUS                     (1 << 1)
#define LSA_ALU                     (1 << 0)
#define LSA_MASK                         0x7

/* For types of literal symbols. */
#define LST_ILLEGAL                        0
#define LST_UNDEF_ADDR                    01
#define LST_DEF_ADDR                      02
#define LST_RLOC_LHS                      03
#define LST_RLOC_RHS                      04
#define LST_CONSTANT                      05
#define LST_BUS_SOURCE                    06
#define LST_F1                            07
#define LST_DATA_F1_LHS                  010
#define LST_L_DEFINING_F1                011
#define LST_F2                           012
#define LST_DATA_F2_LHS                  013
#define LST_DATA_F2                      014
#define LST_DATA_F2_RHS                  015
#define LST_END                          016
#define LST_L_RHS                        017
#define LST_L_LHS                        020
#define LST_F3                           021
#define LST_DATA_F3_LHS                  022
#define LST_DATA_F3_RHS                  023
#define LST_ALUF                         024
#define LST_T_LHS                        025
#define LST_T_RHS                        026
#define LST_UNUSED                       027
#define LST_PREDEF_ADDR                  030
#define LST_LMRSHLMLSH                   031
#define LST_MASK_CONST                   032
#define LST_ASSIGN_F2                    033
#define LST_ASSIGN_F1                    034
#define LST_XMAR                         035

/* Macros. */
/* According to pages 82 and 83 of AltoSybsystems_Oct79.pdf.
 *
 *   The value of a symbol is a 3 word quantity. The first word contains
 *   a type (6 bits) and a value (10 bits) which determines the
 *   interpretation of the symbol in all cases except when it is
 *   encountered as the source in a data transfer clause (assignment).
 *   The second word contains the type and value used in this case.
 *   The third word contains the bits specifying the definitional
 *   requirements and source attributes applied when the symbol is
 *   encountered in an assignment. The definitional requirements are
 *   represented by single bits where zero means "must be defined" and
 *   one means "don't care".
 *
 *     Destination-imposed requirements:
 *       Bit 0: 0 if L output must be defined
 *       Bit 1: 0 if BUS must be defined
 *       Bit 2: 0 if ALU output must be defined.
 *       Bits 3-7: Unused (?)
 *     Source attributes:
 *       Bit 8: L is defined.
 *       Bit 9: Bus is defined
 *       Bit 10: ALU output is defined
 *       Bit 14: ALU output is defined if BUS is defined
 *
 * Note: recall bits are numbered in reverse order in the Alto.
 */

#define LITERAL_ATTRB_REQUIRE(n) (((n) >> 13) & 0x7)
#define LITERAL_ATTRB_DEFINE(n) (((n) >> 5) & 0x7)
#define LITERAL_ATTRB_EXTRA(n) (((n) >> 1) & 0x1)
#define LITERAL_SYMB_TYPE(n) (((n) >> 10) & 0x3F)
#define LITERAL_SYMB_VALUE(n) ((n) & 0x3FF)

/* Data structures and types. */

/* To build up the instruction. */
struct instruction {
    uint16_t f1, f2, f3;          /* F1, F2, and F3 fields .*/
    uint16_t rsel, aluf, bs;      /* RSEL, ALUF, and BS fields. */
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

/* Finds an empty address for the constant.
 * The parameter `val` is the value of the constant,
 * the parameter `bs` is for the lower 3 bits of address,
 * and is only used when `has_bs_mask` is set to TRUE.
 * Returns the address number.
 */
static
uint16_t find_constant_address(struct assembler *as, uint16_t val,
                               uint16_t bs, int has_bs_mask)
{
    uint16_t address;

    /* According to page 77 of AltoSubsystems_Oct79.pdf,
     *
     *   Normal constants are declared thus:
     *     $name$n;
     *   This declares a 16 bit unsigned constant with value n. The
     *   assembler assigns the constant to the first free location in
     *   constant  memory, unless the value has appeared before under
     *   another name in which case the value of the name is the address
     *   of the previously declared constant.
     *   An alternative constant definition is used for mask constants
     *   which have a specific bus source (recall that the constant memory
     *   address is the concatenation of the rselect and bus source fields
     *   of the microinstruction). The syntax is:
     *     $name$Mn:v;   4<=n<=7, 0<=v<2**16
     *   Here n specifies the desired bus source value, v is the constant
     *   value.
     */
    for (address = 0; address < CONSTANT_SIZE; address++) {
        if (as->const_sts[address]) {
            if (as->consts[address] != val) continue;
        }
        if (!has_bs_mask) break;
        if ((address & 7) == bs) break;
    }
    return address;
}

int assembler_resolve_constants(struct assembler *as)
{
    struct statement *st;
    uint16_t address, bs;
    uint16_t val;
    int has_bs_mask;

    memset(as->consts, -1, CONSTANT_SIZE * sizeof(uint16_t));
    memset(as->const_sts, 0, CONSTANT_SIZE * sizeof(struct statement *));

    for (st = as->p.first; st; st = st->next) {
        switch (st->st_type) {
        case ST_DECLARATION:
            switch (st->v.decl.d_type) {
            case DECL_SYMBOL:
                /* To handle the special case of 0 constant. */
                if (LITERAL_SYMB_TYPE(st->v.decl.n2) != LST_CONSTANT)
                    continue;
                val = LITERAL_SYMB_VALUE(st->v.decl.n2);
                bs = 0;
                has_bs_mask = FALSE;
                break;

            case DECL_CONSTANT:
                val = st->v.decl.n1;
                bs = 0;
                has_bs_mask = FALSE;
                break;

            case DECL_M_CONSTANT:
                val = st->v.decl.n2;
                bs = st->v.decl.n1;
                has_bs_mask = TRUE;
                break;

            default:
                continue;
            }
            address = find_constant_address(as, val, bs, has_bs_mask);
            if (address >= CONSTANT_SIZE) {
                report_error("assembler: resolve_constants: %s:%d: overflow",
                             st->filename, st->line_num);
                return FALSE;
            }
            st->chain = as->const_sts[address];
            as->const_sts[address] = st;
            as->consts[address] = val;
            st->v.decl.si->address = address;
            break;

        default:
            break;
        }
    }

    return TRUE;
}

/* Finds an empty address for the microcode and assignes the labels.
 * The pointer to the address_predefinition is in `apdef`. The
 * parameters `filename` and `line_num` are for error reporting.
 * Returns the address number.
 */
static
uint16_t find_microcode_address(struct assembler *as,
                                struct address_predefinition *apdef,
                                const char *filename,
                                unsigned int line_num)
{
    uint16_t address, j, num, num_labels;
    uint16_t mask1, mask2, not_mask2;
    uint16_t len, start, val1, val2;
    struct parser_node *pn;
    struct symbol_info *si;

    pn = apdef->labels;
    num_labels = apdef->num_labels;

    if (apdef->extended) {
        /* According to page 78 of AltoSubsystems_Oct79.pdf,
         *
         *   A more general variant of the predefinition facility is
         *   available. The syntax is:
         *      %mask2, mask1, init, L1, L2, ..., Ln;
         *   The effect of this is to find a block of instructions
         *   starting at location P, where P and mask1 = init, and assign
         *   the L's to 'successive' locations under mask2.
         *   For example:
         *     %1,1,0,x0,x1;
         *   would force x0 to an even position, x1 to odd (the normal
         *   predefinition for most branches).
         *     %360,377,17,L0,L1,...,L15;
         *   would place L0 at xx17, L1 at xx37, L2 at xx57, etc.
         *   As before, if there are unused slots (e.g., 'L2,,L4') they
         *   are available for reassignment, and MU complains if there
         *   are too many labels for the block.
         */
        mask1 = apdef->k;
        mask2 = apdef->n;
        start = apdef->l;

        not_mask2 = 1;
        while (not_mask2 <= mask2)
            not_mask2 <<= 1;
        not_mask2 = (not_mask2 - 1) ^ mask2;

        for (address = 0; address < MICROCODE_SIZE; address++) {
            if ((address & mask1) != start) continue;
            if (as->micro_sts[address]) continue;

            j = 0;
            for (num = 0; num < num_labels; num++) {
                if (num > 0) {
                    val1 = address & not_mask2;
                    val2 = (address + j) & mask2;
                    while ((((address + j) & not_mask2) != val1)
                           || ((address + j) & mask2) == val2)
                        j++;
                    if ((((address + j) & mask2) == (address & mask2)))
                        break;
                }

                if (as->micro_sts[address + j]) break;
            }
            if (num == num_labels) break;
        }

        if (address == MICROCODE_SIZE) {
            report_error("assembler: resolve_labels: %s:%d: "
                         "no free addresses available",
                         filename, line_num);
            return address;
        }

        j = 0;
        for (num = 0; num < num_labels; num++) {
            if (!pn) break;
            si = pn->si;
            pn = pn->next;

            if (num > 0) {
                val1 = address & not_mask2;
                val2 = (address + j) & mask2;
                while ((((address + j) & not_mask2) != val1)
                       || ((address + j) & mask2) == val2)
                    j++;
            }

            if (si) {
                si->address = address + j;
                as->micro_sts[si->address] = si->exec;
            }
        }
    } else {
        /* According to page 78 of AltoSubsystems_Oct79.pdf,
         *
         *   Address predefinitions allow groups of instructions to be
         *   placed in specific locations in the control memory, as is
         *   required by the OR branching scheme used by the Alto. Their
         *   syntax is:
         *     !n,k,name0,name1,name2,...,name{k-1};
         *   This declaration causes a block of consecutive locations to
         *   be allocated in the instruction memory, and the names assigned
         *   to them. n defines the location of the block, in that if L
         *   is the address of the last location of the block, L and n = n.
         *   Usually, n will be 2**p - 1 for some small p. For example, if
         *   the predefinition
         *     !3,4,foo0,foo1,foo2,foo3;
         *   is encountered in the source text before any executable
         *   statement, the labels foo0-foo3 will be assigned to control
         *   memory locations 0-3. If there are too few names, they are
         *   assigned to the low addresses in the block. If there are too
         *   many, they are discarded, and an error is indicated. If there
         *   are missing labels, e.g. 'foo0,,foo2', the locations remain
         *   available for the normal instruction allocation process. A
         *   predefinition must be the first mention of the name in the
         *   source text (forward references or labels encountered before
         *   a predefinition of a given name cause an error when the
         *   predefinition is encountered.)
         */
        mask1 = apdef->n;
        len = apdef->k;

        if (num_labels > len) {
            /* Issue a warning here. */
            report_error("assembler: resolve_labels: %s:%d: "
                         "discarding excess labels (k < num_labels)",
                         filename, line_num);
        }

        for (address = 0; address < MICROCODE_SIZE; address++) {
            if ((address + len - 1) >= MICROCODE_SIZE) continue;
            if (((address + len - 1) & mask1) != mask1) continue;
            for (j = 0; j < len; j++) {
                if (as->micro_sts[address + j]) break;
            }
            if (j == len) break;
        }

        if (address == MICROCODE_SIZE) {
            report_error("assembler: resolve_labels: %s:%d: "
                         "no free addresses available",
                         filename, line_num);
            return address;
        }

        for (j = 0; j < len; j++) {
            if (!pn) break;
            si = pn->si;
            pn = pn->next;

            if (!si) continue;
            si->address = address + j;
            as->micro_sts[si->address] = si->exec;
        }
    }
    return address;
}

int assembler_resolve_labels(struct assembler *as)
{
    struct statement *st;
    struct address_predefinition apdef;
    struct symbol_info *si;
    uint16_t address;

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
            address = find_microcode_address(as, &st->v.addr,
                                             st->filename,
                                             st->line_num);
            if (address == MICROCODE_SIZE)
                return FALSE;
            break;

        case ST_EXECUTABLE:
            si = st->v.exec.si;
            if (si) {
                /* If the label has no address yet, we force the
                 * address resolution to use the fake address
                 * predefintion below.
                 */
                if (!si->addr) si = NULL;
            }

            if (si) {
                /* Use the address of the label. */
                address = si->address;
            } else {
                /* We create a fake address predefinition just
                 * to call find_microcode_address() to resolve the
                 * address of this statement.
                 *
                 * According to page 79 of AltoSubsystems_Oct79.pdf,
                 *
                 *   If a label has been predefined, the instruction is
                 *   placed at the control memory location reserved for
                 *   it. Otherwise, it is assigned to the lowest unused
                 *   location.
                 */
                apdef.n = 0;
                apdef.k = 1;
                apdef.l = 0;
                apdef.extended = FALSE;
                apdef.labels = NULL;
                apdef.num_labels = 0;
                address = find_microcode_address(as, &apdef,
                                                 st->filename,
                                                 st->line_num);
                if (address >= MICROCODE_SIZE)
                    return FALSE;
                si = st->v.exec.si;
                if (si) si->address = address;
            }
            st->v.exec.address = address;
            as->micro_sts[address] = st;
            break;

        default:
            break;
        }

        st = st->next;
    }

    return TRUE;
}

#define CREATE_SET_FUNCTION(field) \
static int set_ ## field(struct instruction *insn, uint16_t val)     \
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
    uint16_t lst, val;

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

/* Resolves the RHS of an assignment expression.
 * The parameter `name` specifies the RHS string to be resolved.
 * The parameter `req` specifies the requirements for the RHS,
 * and it consists of the bitwise OR of a bunch of LSA_* constants.
 * It might be broken down intwo two component substrings
 * If that is the case the parameters `first` and `second`
 * will return the symbol information of the two parts. If
 * `name` does not need to be broken, only `first` is populated
 * and `second` returns NULL.
 * Lastly, `gate_alu` returns TRUE if the ALU should be gated
 * to return the BUS.
 */
static
void resolve_rhs(struct assembler *as,
                 const struct string *name, uint16_t req,
                 struct symbol_info **first, struct symbol_info **second,
                 int *gate_alu)
{
    struct string copy;
    struct symbol_info *si;
    struct declaration *decl;
    uint16_t def;
    size_t i;

    /*
     * According to pages 79 of AltoSybsystems_Oct79.pdf.
     *
     *   If neither of the above conditions hold, the source can legally
     *   be only a bus source concatenated with an ALU function. The source
     *   token is repeatedly broken into two substrings, and each is looked
     *   up in the symbol table. If two substrings can be found which
     *   satisfy the requirements, the field assignment implied by both are
     *   made: otherwise an error is generated. This method of evaluation
     *   is simple, but it has pitfalls. For instance, 'L<- 2 + T' is legal
     *   (provided that the constant "2" has been defined) but 'L<- T + 2'
     *   is not (and the BUS operand must always be on the left). Note that
     *   'L<- foo + T + 1' specifies a bus source of 'foo' and an ALU
     *   function of '+T+1'.
     *
     *   CAVEAT: The T register maybe loaded from either the BUS or the
     *   output of the ALU, depending on the ALU function. The assembler
     *   does not check to see whether an assignment is of the form
     *   'T<- ALU' specifies an ALU function that actually loads T from the
     *   ALU. For example, the clause 'L<- T<- MD - T' is accepted, but its
     *   effect is to load T directly from MD. If this is what you intend,
     *   it makes matters clearer if you write 'L<- MD - T, T<- MD'; if it
     *   is not what you intend, you are in trouble. Beware!
     *
     * Page 82 also states:
     *
     *   When the source token is encountered, if it is a defined symbol
     *   it is tested by checking the definitional requirements of the
     *   destinations against the corresponding attributes in the source.
     *   If all destination requirements are satisfied, the clause is
     *   complete. If the only unsatisfied requirement is ALU definition,
     *   and the BUS is defined, the ALU function is set to gate the BUS
     *   through (thereby defining the ALU), and the clause is complete.
     *   If this doesn't work, or the source token is not a defined symbol,
     *   the source string is dismembered in search for two substrings,
     *   the first of which defines the BUS (bit 9), and the second of
     *   which defines the ALU output if the BUS is defined (bit 14). If
     *   two substrings are found, the implied assignments are made, and
     *   the clause is complete. Otherwise, an error is indicated.

     */

    /* Tries to resolve the symbol as is. */
    si = resolve_symbol(as, name);
    if (!si) goto break_name;
    if (!si->decl) goto break_name;

    decl = &si->decl->v.decl;
    if (decl->d_type == DECL_SYMBOL) {
        def = LITERAL_ATTRB_DEFINE(decl->n3);
    } else {
        def = LSA_BUS;
    }

    if ((req | def) == (LSA_L | LSA_BUS)) {
        /* Gate the ALU results. */
        def |= LSA_ALU;
        *gate_alu = TRUE;
    } else {
        *gate_alu = FALSE;
    }

    if ((req | def) != (LSA_L | LSA_BUS | LSA_ALU))
        goto break_name;

    *first = si;
    *second = NULL;
    return;

break_name:

    /* Break the RHS into 2 parts, and resolve each
     * one independently.
     */
    for (i = 1; i + 1 < name->len; i++) {
        copy.s = name->s;
        copy.len = i;
        copy.hash = string_hash(copy.s, copy.len);

        si = resolve_symbol(as, &copy);
        if (!si) continue;
        if (!si->decl) continue;

        decl = &si->decl->v.decl;
        if (decl->d_type == DECL_SYMBOL) {
            def = LITERAL_ATTRB_DEFINE(decl->n3);
        } else {
            def = LSA_BUS;
        }

        if (def != LSA_BUS) continue;
        *first = si;

        copy.s = &name->s[i];
        copy.len = name->len - i;
        copy.hash = string_hash(copy.s, copy.len);

        si = resolve_symbol(as, &copy);
        if (!si) continue;
        if (!si->decl) continue;

        decl = &si->decl->v.decl;
        if (decl->d_type != DECL_SYMBOL) continue;
        if (!LITERAL_ATTRB_EXTRA(decl->n3)) continue;

        def |= LSA_ALU;

        if ((req | def) != (LSA_L | LSA_BUS | LSA_ALU))
            continue;

        *second = si;
        *gate_alu = FALSE;
        return;
    }

    *first = NULL;
    *second = NULL;
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
    uint16_t lst;
    uint16_t req;
    uint16_t val;
    int has_load_t;
    int gate_alu;

    /* According to pages 79 of AltoSybsystems_Oct79.pdf.
     *
     *   This type of clause is assembled by looking up the destinations,
     *   checking their legality, and making the field assignments implied
     *   by the symbol types. Each destination imposes definitional
     *   requirements on the source (e.g., ALU output must be defined, BUS
     *   must be defined). These requirements must be satisfied by the
     *   source in order for the statement to be legal.
     *
     *   When the source is encountered, it is looked up in the symbol
     *   table. If it is legal and satisfies the definitional requirements
     *   imposed by the destinations, the necessary field assignments are
     *   made, and processing continues. If the entire source defines the
     *   BUS, and the only remaining requirement is that the ALU output
     *   must be defined (e.g., L<- MD), the ALUF field is set to 0 (ALU
     *   output = BUS), and processing continues.
     *
     * Page 82 also states:
     *
     *   Assignment processing proceeds by ANDing together the attribute
     *   words for all the destinations. The result contains zeros in the
     *   bits 0-2 for things that must be defined and ones elsewhere.
     */

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
            st->v.exec.r_name = &decl->name;
        } else {
            report_error("assembler: assemble: %s:%d: "
                         "%s has no valid declaration as RDEST (%d)",
                         st->filename, st->line_num,
                         decl->name.s, decl->d_type);
            return FALSE;
        }
    }

    resolve_rhs(insn->as, &cl->name, req, &si, &si_extra, &gate_alu);
    if (!si) {
        report_error("assembler: assemble: %s:%d: "
                     "%s is not a valid RHS",
                     st->filename, st->line_num,
                     cl->name.s);
        return FALSE;
    }

    decl = &si->decl->v.decl;

    if (decl->d_type == DECL_SYMBOL) {
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
            st->v.exec.c_name = &decl->name;
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
        insn->has_constant = TRUE;
        if (!set_rsel(insn, CONST_ADDR_RSEL(decl->si->address)))
            return FALSE;
        if (!set_bs(insn, CONST_ADDR_BS(decl->si->address)))
            return FALSE;
        st->v.exec.c_name = &decl->name;
    } else if (decl->d_type == DECL_M_CONSTANT) {
        insn->has_m_constant = TRUE;
        if (!set_rsel(insn, CONST_ADDR_RSEL(decl->si->address)))
            return FALSE;
        st->v.exec.c_name = &decl->name;
    } else if (decl->d_type == DECL_R_MEMORY) {
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
        st->v.exec.r_name = &decl->name;
    }

    if (si_extra) {
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
    }

    if (gate_alu) {
        /* Gate the ALU results. */
        if (!set_aluf(insn, ALU_BUS))
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
    uint16_t new_f1, new_f2;
    uint32_t microcode;
    int error;

    memset(&insn, 0, sizeof(insn));
    insn.as = as;
    insn.st = st;
    insn.next_st = next_st;

    error = FALSE;
    st->v.exec.c_name = NULL;
    st->v.exec.r_name = NULL;
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
    microcode &= MC_NEXT_M;

    microcode |= (insn.rsel & MC_RSEL_M) << MC_RSEL_S;
    microcode |= (insn.aluf & MC_ALUF_M) << MC_ALUF_S;
    microcode |= (insn.bs & MC_BS_M) << MC_BS_S;
    microcode |= (insn.f1 & MC_F1_M) << MC_F1_S;
    microcode |= (insn.f2 & MC_F2_M) << MC_F2_S;
    if (insn.load_t) microcode |= (1 << MC_T_S);
    if (insn.load_l) microcode |= (1 << MC_L_S);

    as->microcode[st->v.exec.address] = microcode;
    return TRUE;
}

int assembler_assemble(struct assembler *as)
{
    struct statement *st, *next_st;
    uint16_t address;
    int error;

    for (address = 0; address < MICROCODE_SIZE; address++) {
        /* Jump to the last address in rom. */
        as->microcode[address] = 0xFFF77BFF;
    }

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

int assembler_produce_objfile(const struct assembler *as,
                              struct objfile *objf)
{
    const struct statement *st;
    uint16_t address, value;
    uint32_t mcode;

    objfile_clear(objf);
    for (st = as->p.first; st; st = st->next) {
        switch (st->st_type) {
        case ST_DECLARATION:
            switch (st->v.decl.d_type) {
            case DECL_SYMBOL:
                if (LITERAL_SYMB_TYPE(st->v.decl.n2) != LST_CONSTANT)
                    continue;
                /* fallthrough */
            case DECL_CONSTANT:
            case DECL_M_CONSTANT:
                address = st->v.decl.si->address;
                value = as->consts[address];
                if (unlikely(!objfile_add_constant(objf,
                                                   &st->v.decl.name,
                                                   address, value))) {
                    report_error("assembler: produce_objfile: "
                                 "could not add constant");
                    return FALSE;
                }
                break;
            case DECL_R_MEMORY:
                address = st->v.decl.n1;
                if (unlikely(!objfile_add_register(objf,
                                                   &st->v.decl.name,
                                                   address))) {
                    report_error("assembler: produce_objfile: "
                                 "could not add register");
                    return FALSE;
                }
                break;
            default:
                break;
            }
            break;
        case ST_EXECUTABLE:
            address = st->v.exec.address;
            mcode = as->microcode[address];

            if (st->v.exec.label.s) {
                if (unlikely(!objfile_add_label(objf,
                                                &st->v.exec.label,
                                                address))) {
                    report_error("assembler: produce_objfile: "
                                 "could not add label");
                    return FALSE;
                }
            }
            if (unlikely(!objfile_add_microcode_symbols(
                             objf,
                             st->v.exec.c_name,
                             st->v.exec.r_name,
                             address,
                             mcode))) {
                report_error("assembler: produce_objfile: "
                             "could not add microcode");
                return FALSE;
            }
            break;
        default:
            break;
        }
    }

    return TRUE;
}

/* Prints the constants in the listing. */
static
void print_constants(struct assembler *as, FILE *fp)
{
    struct statement *st;
    uint16_t address;
    char buffer[64];

    fprintf(fp, "--- CONSTANTS ---\n");
    fprintf(fp, "ADDRESS  VALUE     NAME          "
                "DEFINITION             LOCATION\n");
    for (address = 0; address < CONSTANT_SIZE; address++) {
        st = as->const_sts[address];
        if (!st) {
            fprintf(fp, "%03o      %o\n",
                    address, as->consts[address]);
            continue;
        }

        while (st) {
            if (st == as->const_sts[address]) {
                fprintf(fp, "%03o      %-6o    ",
                        address, as->consts[address]);
            } else {
                fprintf(fp, "                   ");
            }
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
            st = st->chain;
        }
    }
}

/* Prints the R memory declarations in the listing. */
static
void print_r_memory_declarations(struct assembler *as, FILE *fp)
{
    struct statement *st;

    fprintf(fp, "--- R MEMORY DECLARATIONS ---\n");
    fprintf(fp, "NAME          DEFINITION  LOCATION\n");
    for (st = as->p.first; st; st = st->next) {
        if (st->st_type != ST_DECLARATION) continue;
        if (st->v.decl.d_type != DECL_R_MEMORY) continue;

        fprintf(fp, "$%-12s ", st->v.decl.name.s);
        fprintf(fp, "$R%-2o        ", st->v.decl.n1);
        fprintf(fp, "%s:%u\n", st->filename, st->line_num);
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
    uint16_t req;

    fprintf(fp, "--- LITERAL SYMBOLS ---\n");
    fprintf(fp, "NAME          TYPE  VAL   "
                "RHS_TYPE RHS_VAL REQ DEF EXTRA LOCATION\n");
    for (st = as->p.first; st; st = st->next) {
        if (st->st_type != ST_DECLARATION) continue;
        if (st->v.decl.d_type != DECL_SYMBOL) continue;

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
    }

    fprintf(fp, "\n");
    fprintf(fp, "-- SYMBOL TYPES ---\n");
    fprintf(fp, "TYPE  LEGAL AS    FIELDS      DESCRIPTION\n");
    fprintf(fp, "00    NEVER                   ILLEGAL\n");
    fprintf(fp, "01    ADDRESS                 UNDEFINED ADDRESS\n");
    fprintf(fp, "02    ADDRESS     NEXT        DEFINED ADDDRESS\n");
    fprintf(fp, "03    LHS         RSEL        R LOCATION LHS"
                "[BS<- 0]\n");
    fprintf(fp, "04    RHS         RSEL        R LOCATION RHS\n");
    fprintf(fp, "05    RHS         RSEL,BS     CONSTANT\n");
    fprintf(fp, "06    RHS         BS          BUS SOURCE\n");
    fprintf(fp, "07    CLAUSE      F1          NONDATA F1\n");
    fprintf(fp, "10    LHS         F1          DATA F1 LHS\n");
    fprintf(fp, "11    RHS         F1          L DEFINING F1\n");
    fprintf(fp, "12    CLAUSE      F2          NONDATA F2\n");
    fprintf(fp, "13    LHS         F2          DATA F2 LHS\n");
    fprintf(fp, "14    LHS         F2          DATA F2 LHS "
                "[BS<- 1, RSEL<- 0]\n");
    fprintf(fp, "15    RHS         F2          DATA F2 (RHS) "
                "[BS<- 0, RSEL<- 0]\n");
    fprintf(fp, "16    CLAUSE                  END [Not used]\n");
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
    unsigned int j;
    uint32_t microcode;
    uint16_t address;

    fprintf(fp, "--- MICROCODE ---\n");
    fprintf(fp, "ADDRESS   MICROCODE    RSEL ALUF BS F1 F2 T L NEXT "
                "LABEL      STATEMENT\n");

    for (address = 0; address < MICROCODE_SIZE; address++) {
        microcode = as->microcode[address];
        fprintf(fp, "%05o     %011o  ", address, microcode);
        fprintf(fp, "%02o   %02o   %o  %02o %02o %o %o %04o ",
                MICROCODE_RSEL(microcode),
                MICROCODE_ALUF(microcode),
                MICROCODE_BS(microcode),
                MICROCODE_F1(microcode),
                MICROCODE_F2(microcode),
                MICROCODE_T(microcode),
                MICROCODE_L(microcode),
                MICROCODE_NEXT(microcode));

        st = as->micro_sts[address];
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
