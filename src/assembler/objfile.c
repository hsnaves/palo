
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "assembler/objfile.h"
#include "microcode/microcode.h"
#include "common/allocator.h"
#include "common/table.h"
#include "common/serdes.h"
#include "common/utils.h"

/* Constants. */
#define REG_SIZE          (2 * (R_MASK + 1))

/* Data structures and types. */

/* Callback argument for the disassembler. */
struct disasm_cb_arg {
    const struct objfile *objf;   /* Reference to the objfile. */
    unsigned int index;           /* Index of the current microcode. */
    const char *const_name;       /* Name of the decoded constant. */
    decoder_cb orig_dec_cb;       /* Original callback. */
    void *orig_arg;               /* Original argument for callback. */
};

/* Functions. */

void objfile_initvar(struct objfile *objf)
{
    allocator_initvar(&objf->salloc);
    allocator_initvar(&objf->oalloc);
    table_initvar(&objf->symbols);

    objf->consts = NULL;
    objf->microcode = NULL;

    objf->const_chain = NULL;
    objf->reg_chain = NULL;
    objf->label_chain = NULL;
    objf->mu_chain = NULL;

    objf->tbuf = NULL;
}

void objfile_destroy(struct objfile *objf)
{
    if (objf->consts) free((void *) objf->consts);
    objf->consts = NULL;

    if (objf->microcode) free((void *) objf->microcode);
    objf->microcode = NULL;

    if (objf->const_chain) free((void *) objf->const_chain);
    objf->const_chain = NULL;

    if (objf->reg_chain) free((void *) objf->reg_chain);
    objf->reg_chain = NULL;

    if (objf->label_chain) free((void *) objf->label_chain);
    objf->label_chain = NULL;

    if (objf->mu_chain) free((void *) objf->mu_chain);
    objf->mu_chain = NULL;

    if (objf->tbuf) free((void *) objf->tbuf);
    objf->tbuf = NULL;

    table_destroy(&objf->symbols);
    allocator_destroy(&objf->oalloc);
    allocator_destroy(&objf->salloc);
}

int objfile_create(struct objfile *objf)
{
    objfile_initvar(objf);

    if (unlikely(!allocator_create(&objf->salloc, 0))) {
        report_error("objfile: create: "
                     "could not create string allocator");
        objfile_destroy(objf);
        return FALSE;
    }

    if (unlikely(!allocator_create(&objf->oalloc, DEFAULT_ALIGNMENT))) {
        report_error("objfile: create: "
                     "could not create object allocator");
        objfile_destroy(objf);
        return FALSE;
    }

    if (unlikely(!table_create(&objf->symbols))) {
        report_error("objfile: create: "
                     "could not create hash table");
        objfile_destroy(objf);
        return FALSE;
    }

    objf->consts = (uint16_t *)
        malloc(CONSTANT_SIZE * sizeof(uint16_t));
    objf->microcode = (uint32_t *)
        malloc(MICROCODE_SIZE * sizeof(uint32_t));

    objf->const_chain = (struct objsymb **)
        malloc(CONSTANT_SIZE * sizeof(struct objsymb *));
    objf->reg_chain = (struct objsymb **)
        malloc(REG_SIZE * sizeof(struct objsymb *));
    objf->label_chain = (struct objsymb **)
        malloc(MICROCODE_SIZE * sizeof(struct objsymb *));
    objf->mu_chain = (struct objsymb **)
        malloc(MICROCODE_SIZE * sizeof(struct objsymb *));

    objf->tbuf_size = 4096;
    objf->tbuf = (char *) malloc(objf->tbuf_size);

    if (unlikely(!objf->consts || !objf->microcode
                 || !objf->const_chain || !objf->reg_chain
                 || !objf->label_chain || !objf->mu_chain
                 || !objf->tbuf)) {
        report_error("objfile: create: memory exhausted");
        objfile_destroy(objf);
        return FALSE;
    }

    objfile_clear(objf);

    return TRUE;
}

void objfile_clear(struct objfile *objf)
{
    size_t i;

    objf->num_symbs = 0;
    objf->first_symb = NULL;
    objf->last_symb = NULL;

    for (i = 0; i < CONSTANT_SIZE; i++) {
        objf->consts[i] = 0xFFFFU;
        objf->const_chain[i] = NULL;
    }

    for (i = 0; i < REG_SIZE; i++) {
        objf->reg_chain[i] = NULL;
    }

    for (i = 0; i < MICROCODE_SIZE; i++) {
        /* Jump to the last address in rom. */
        objf->microcode[i] = 0xFFF77BFF;
        objf->label_chain[i] = NULL;
        objf->mu_chain[i] = NULL;
    }

    table_clear(&objf->symbols);
}

/* Allocates a new objsymb.
 * The parameter `type` is the type of the symbol, while `value`
 * represents the value of the symbol.
 * The name of the symbol is given by the parameter `name`.
 * If the `name` is not provided, the symbol is not added to the
 * the hash table.
 * Returns the allocated objsymb.
 */
static
struct objsymb *new_objsymb(struct objfile *objf,
                            enum objsymb_type type, uint16_t value,
                            const struct string *name)
{
    struct objsymb *osym;
    const struct objsymb *other;
    const struct string_node *n;

    if (name) {
        other = objfile_resolve(objf, type, name);
        if (other) {
            if (other->value != value) {
                report_error("objfile: new_objsymb: "
                             "repeated name with different values: "
                             "%s -> %u vs %u (type: %u)",
                             other->n.str.s, other->value,
                             value, type);
                return NULL;
            }
        }
    } else {
        other = NULL;
    }

    osym = (struct objsymb *)
        allocator_alloc(&objf->oalloc, sizeof(struct objsymb), TRUE);
    if (unlikely(!osym)) {
        report_error("objfile: new_objsymb: "
                     "memory exhausted");
        return NULL;
    }
    osym->type = type;
    osym->value = value;

    if (name) {
        n = table_find(&objf->symbols, name);
        if (n) {
            /* To avoid allocating memory.
             * That is, strings are interned.
             */
            osym->n = *n;
        } else {
            /* Make a copy of the name. */
            osym->n.str.s = allocator_dup(&objf->salloc,
                                          name->s, name->len);
            if (unlikely(!osym->n.str.s)) {
                report_error("objfile: new_objsymb: "
                             "memory exhausted for strings");
                return NULL;
            }
            osym->n.str.len = name->len;
            osym->n.str.hash = name->hash;
        }

        if (!other) {
            if (unlikely(!table_add(&objf->symbols, &osym->n))) {
                report_error("objsfile: new_objsymb: "
                             "could not add symbol to hash table");
                return NULL;
            }
        }
    }

    /* Adds the symbol to the list of symbols. */
    osym->index = ++(objf->num_symbs);
    if (objf->last_symb) {
        objf->last_symb->next = osym;
    }
    objf->last_symb = osym;
    if (!objf->first_symb) {
        objf->first_symb = osym;
    }

    return osym;
}

int objfile_add_constant(struct objfile *objf,
                         const struct string *name,
                         uint16_t address, uint16_t value)
{
    struct objsymb *osym;

    if (unlikely(!name)) {
        report_error("objfile: add_constant: name is NULL");
        return FALSE;
    }

    if (unlikely(address >= CONSTANT_SIZE)) {
        report_error("objfile: add_constant: "
                     "invalid address %07o for constant `%s`",
                     address, name->s);
        return FALSE;
    }

    osym = new_objsymb(objf, OBJSYMB_CONSTANT, address, name);
    if (unlikely(!osym)) {
        report_error("objfile: add_constant: "
                     "could not create symbol");
        return FALSE;
    }

    objf->consts[address] = value;

    osym->chain_next = objf->const_chain[address];
    objf->const_chain[address] = osym;

    return TRUE;
}

int objfile_add_register(struct objfile *objf,
                         const struct string *name,
                         uint16_t address)
{
    struct objsymb *osym;

    if (unlikely(!name)) {
        report_error("objfile: add_register: name is NULL");
        return FALSE;
    }

    if (unlikely(address >= REG_SIZE)) {
        report_error("objfile: add_register: "
                     "invalid address %07o for register `%s`",
                     address, name->s);
        return FALSE;
    }

    osym = new_objsymb(objf, OBJSYMB_REGISTER, address, name);
    if (unlikely(!osym)) {
        report_error("objfile: add_register: "
                     "could not create symbol");
        return FALSE;
    }

    osym->chain_next = objf->reg_chain[address];
    objf->reg_chain[address] = osym;

    return TRUE;
}

int objfile_add_label(struct objfile *objf,
                      const struct string *name,
                      uint16_t address)
{
    struct objsymb *osym;

    if (unlikely(!name)) {
        report_error("objfile: add_label: name is NULL");
        return FALSE;
    }

    if (unlikely(address >= MICROCODE_SIZE)) {
        report_error("objfile: add_label: "
                     "invalid address %07o for label `%s`",
                     address, name->s);
        return FALSE;
    }

    if (objf->label_chain[address]) {
        report_error("objfile: add_label: "
                     "label `%s` already defined for "
                     "the given address %07o",
                     name->s, address);
        return FALSE;
    }

    osym = new_objsymb(objf, OBJSYMB_LABEL, address, name);
    if (unlikely(!osym)) {
        report_error("objfile: add_label: "
                     "could not create symbol");
        return FALSE;
    }

    /* No need to set-up the chain here, as labels are unique. */
    objf->label_chain[address] = osym;

    return TRUE;
}

int objfile_add_microcode(struct objfile *objf,
                          uint16_t address, uint32_t mcode)
{
    struct objsymb *osym;

    if (unlikely(address >= MICROCODE_SIZE)) {
        report_error("objfile: add_microcode: "
                     "invalid address %07o", address);
        return FALSE;
    }

    if (objf->mu_chain[address]) {
        report_error("objfile: add_microcode: "
                     "microcode already defined for "
                     "the given address %07o",
                     address);
        return FALSE;
    }

    osym = new_objsymb(objf, OBJSYMB_MU, address, NULL);
    if (unlikely(!osym)) {
        report_error("objfile: add_microcode: "
                     "could not create symbol");
        return FALSE;
    }

    objf->microcode[address] = mcode;
    /* No need to set-up the chain here, as entries are unique. */
    objf->mu_chain[address] = osym;

    return TRUE;
}

int objfile_add_microcode_symbols(struct objfile *objf,
                                  const struct string *c_name,
                                  const struct string *r_name,
                                  uint16_t address, uint32_t mcode)
{
    const struct objsymb *osym;
    const struct objsymb *other;
    struct microcode mc;
    uint16_t addr, val;
    uint16_t rsel;

    /* The sys_type, and the task are irrelevant for the
     * microcode_predecode().
     */
    microcode_predecode(&mc, ALTO_I, address, mcode, TASK_EMULATOR);

    rsel = mc.rsel;
    if (mc.bs == BS_TASK_SPECIFIC1 || mc.bs == BS_TASK_SPECIFIC2)
        rsel = mc.rsel + (R_MASK + 1);

    if (c_name && (mc.use_constant || mc.bs_use_crom)) {
        osym = objfile_resolve(objf, OBJSYMB_CONSTANT, c_name);
        if (unlikely(!osym)) {
            report_error("objfile: add_microcode_symbols: "
                         "could not find constant");
            return FALSE;
        }

        addr = mc.const_addr;
        other = objf->const_chain[addr];
        if (unlikely(!other)) {
            report_error("objfile: add_microcode_symbols: "
                         "no defined constant");
            return FALSE;
        }

        /* Can compare string pointers here, because strings
         * are interned and thus unique.
         */
        if (osym->n.str.s != other->n.str.s) {
            val = objf->consts[addr];
            if (unlikely(!objfile_add_constant(objf, c_name, addr, val))) {
                report_error("objfile: add_microcode_symbols: "
                             "could not add constant");
                return FALSE;
            }
        }
    }

    if (r_name) {
        osym = objfile_resolve(objf, OBJSYMB_REGISTER, r_name);
        if (unlikely(!osym)) {
            report_error("objfile: add_microcode_symbols: "
                         "could not find register");
            return FALSE;
        }

        addr = rsel;
        other = objf->reg_chain[addr];
        if (unlikely(!other)) {
            report_error("objfile: add_microcode_symbols: "
                         "no defined register");
            return FALSE;
        }

        /* Can compare string pointers here, because strings
         * are interned and thus unique.
         */
        if (osym->n.str.s != other->n.str.s) {
            if (unlikely(!objfile_add_register(objf, r_name, addr))) {
                report_error("objfile: add_microcode_symbols: "
                             "could not add register");
                return FALSE;
            }
        }
    }

    if (unlikely(!objfile_add_microcode(objf, address, mcode))) {
        report_error("objfile: add_microcode_symbols: "
                     "could not add microcode");
        return FALSE;
    }

    return TRUE;
}


struct objsymb *objfile_resolve(const struct objfile *objf,
                                enum objsymb_type type,
                                const struct string *name)
{
    const struct string_node *n;
    struct objsymb *osym;
    size_t offset;

    n = table_find(&objf->symbols, name);
    if (!n) return NULL;

    while (n) {
        offset = offsetof(struct objsymb, n);
        osym = (struct objsymb *) &(((char *) n)[-offset]);
        if (osym->type == type) return osym;
        n = n->next;
    }

    return NULL;
}

int objfile_check_constants(const struct objfile *objf,
                            const uint8_t consts[CONSTANT_SIZE])
{
    return (memcmp(objf->consts, consts,
                   CONSTANT_SIZE * sizeof(uint8_t)) == 0);
}

/* Reverses the bits according to some mask.
 * This code was based on the source code of READMU.C:
 * https://xeroxalto.computerhistory.org/Indigo/AltoSource/.READMU.C!1.html
 * This is used by serialize_file().
 */
static
uint16_t revbits(uint16_t x, int n, uint16_t mask)
{
    uint16_t y;
    int j;

    y = 0;
    for (j = 0; j < n; j++) {
        y = y << 1;
        y += (x & mask);
        x = x >> 1;
    }
    return y;
}

/* Serializes the constant into the binary file.
 * The address of the constant is in `address` and the constant value
 * itself in `value`.
 */
static
void serialize_constant(struct serdes *sd, uint16_t address,
                        uint16_t value)
{
    uint16_t tmp1, tmp2;

    /* What a strange transformation! */
    tmp1 = (revbits((address >> 4) & 0xF, 4, 1) << 1)
        + ((address & 0xE) << 4) + (address & 1);

    serdes_put16(sd, 2); /* block_type */
    serdes_put16(sd, 1); /* mem_num */
    serdes_put16(sd, tmp1); /* value */

    tmp2 = ~value;

    serdes_put16(sd, 1); /* block_type */
    serdes_put16(sd, 0); /* line_num */
    serdes_put16(sd, tmp2); /* value */
}

/* Serializes the microcode into the binary file.
 * The address of the microcode is in `address` and the microcode
 * itself in `mcode`.
 */
static
void serialize_microcode(struct serdes *sd, uint16_t address,
                         uint32_t mcode)
{
    uint16_t tmp1, tmp2;
    uint16_t line_num;

    tmp1 = revbits((~address) & 0xFF, 8, 1)
        + (address & 0xFF00);

    serdes_put16(sd, 2); /* block_type */
    serdes_put16(sd, 2); /* mem_num */
    serdes_put16(sd, tmp1); /* value */

    line_num = 5 + (mcode >> 16);

    /* What is happening here? */
    mcode ^= 0x88400;
    tmp1 = revbits(mcode & 0xFFFF, 4, 0x1111);
    tmp2 = revbits((mcode >> 16) & 0xFFFF, 4, 0x1111);
    mcode = ((uint32_t) tmp1) + (((uint32_t) tmp2) << 16);

    serdes_put16(sd, 1); /* block_type */
    serdes_put16(sd, line_num); /* line_num */
    serdes_put32(sd, mcode); /* value */
}

/* Serializes a symbol definition.
 * The address of the symbol value is in `value`, and the memory number
 * on which is defined is in `mem_num`. The name of the symbol is given
 * by `name`.
 */
static
void serialize_symbol(struct serdes *sd, uint16_t value,
                      uint16_t mem_num, const struct string *name)
{
    serdes_put16(sd, 5); /* block_type */
    serdes_put16(sd, mem_num); /* memory_num */
    serdes_put16(sd, value);
    serdes_put8_array(sd, (const uint8_t *) name->s, name->len);

    /* Append the end of the string. */
    if (name->len & 1) {
        serdes_put8(sd, 0);
    } else {
        serdes_put16(sd, 0);
    }
}

void objfile_serialize(const struct objfile *objf, struct serdes *sd)
{
    const struct objsymb *osym;
    uint16_t address, value;
    uint32_t mcode;

    /* Define R memory. */
    serdes_put16(sd, 4); /* block_type */
    serdes_put16(sd, 3); /* mem_num */
    serdes_put16(sd, 16); /* num_bits */
    serdes_put_string(sd, "R");

    /* Define CONSTANT memory. */
    serdes_put16(sd, 4); /* block_type */
    serdes_put16(sd, 1); /* mem_num */
    serdes_put16(sd, 16); /* num_bits */
    serdes_put_string(sd, "CONSTANT");
    serdes_put8(sd, 0); /* for alignment */

    for (address = 0; address < CONSTANT_SIZE; address++) {
        if (!objf->const_chain[address]) continue;

        value = objf->consts[address];
        serialize_constant(sd, address, value);
    }

    /* Define INSTRUCTION memory. */
    serdes_put16(sd, 4); /* block_type */
    serdes_put16(sd, 2); /* mem_num */
    serdes_put16(sd, 32); /* num_bits */
    serdes_put_string(sd, "INSTRUCTION");

    for (osym = objf->first_symb; osym; osym = osym->next) {
        switch (osym->type) {
        case OBJSYMB_CONSTANT:
            serialize_symbol(sd, osym->value, 1, &osym->n.str);
            break;
        case OBJSYMB_LABEL:
            serialize_symbol(sd, osym->value, 2, &osym->n.str);
            break;
        case OBJSYMB_REGISTER:
            serialize_symbol(sd, osym->value, 3, &osym->n.str);
            break;
        case OBJSYMB_MU:
            address = osym->value;
            mcode = objf->microcode[address];
            serialize_microcode(sd, address, mcode);
            break;
        default:
            continue;
        }
    }

    /* End of file. */
    serdes_put16(sd, 0);
}

/* Deserializes a memory definition.
 * The memory number is returned in the parameter `memory_num`.
 * Returns TRUE on success.
 */
static
int deserialize_memory(struct objfile *objf, struct serdes *sd,
                       uint16_t *memory_num)
{
    uint16_t mem_num, mem_width;
    uint16_t exp_mem_num, exp_mem_width;
    size_t len;

    mem_num = serdes_get16(sd);
    mem_width = serdes_get16(sd);
    len = serdes_get_string(sd, objf->tbuf, objf->tbuf_size);
    if (!(len & 1)) serdes_get8(sd); /* for alignment */
    if (len + 1 > objf->tbuf_size) {
        report_error("objfile: deserialize: string too large");
        return FALSE;
    }

    if (strcmp(objf->tbuf, "R") == 0) {
        exp_mem_num = 3;
        exp_mem_width = 16;
    } else if (strcmp(objf->tbuf, "CONSTANT") == 0) {
        exp_mem_num = 1;
        exp_mem_width = 16;
    } else if (strcmp(objf->tbuf, "INSTRUCTION") == 0) {
        exp_mem_num = 2;
        exp_mem_width = 32;
    } else {
        report_error("objfile: deserialize: "
                     "invalid memory `%s`", objf->tbuf);
        return FALSE;
    }

    if (unlikely(mem_num != exp_mem_num)) {
        report_error("objfile: deserialize: "
                     "invalid number for `%s`: %u",
                     objf->tbuf, mem_num);
        return FALSE;
    }

    if (unlikely(mem_width != exp_mem_width)) {
        report_error("objfile: deserialize: "
                     "invalid width for `%s`: %u",
                     objf->tbuf, mem_width);
        return FALSE;
    }

    *memory_num = mem_num;
    return TRUE;
}

/* Deserializes a set address block.
 * The memory number is returned in the parameter `memory_num`,
 * and the memory address in `memory_addr`.
 * Returns TRUE on success.
 */
static
int deserialize_address(struct serdes *sd, uint16_t *memory_num,
                        uint16_t *memory_addr)
{
    uint16_t mem_num, mem_addr;
    mem_num = serdes_get16(sd);
    mem_addr = serdes_get16(sd);

    if (mem_num == 1) {
        mem_addr = (revbits((mem_addr >> 1) & 0xF, 4, 1) << 4)
            + ((mem_addr >> 4) & 0xE) + (mem_addr & 1);
    } else if (mem_num == 2) {
        mem_addr = revbits((~mem_addr) & 0xFF, 8, 1)
            + (mem_addr & 0xFF00);
    } else {
        report_error("objfile: deserialize: "
                     "invalid memory number: %u",
                     mem_num);
        return FALSE;
    }

    *memory_num = mem_num;
    *memory_addr = mem_addr;
    return TRUE;
}

/* Deserializes a data block.
 * The current memory number is in the parameter `memory_num`,
 * and the current memory address in `memory_addr`.
 * Returns TRUE on success.
 */
static
int deserialize_data(struct objfile *objf, struct serdes *sd,
                     uint16_t memory_num, uint16_t memory_addr)
{
    uint32_t mcode;
    uint16_t line_num, exp_line_num;
    uint16_t w, tmp1, tmp2;

    line_num = serdes_get16(sd);
    if (memory_num == 1) {
        w = serdes_get16(sd);
        w = ~w;
        exp_line_num = 0;

        objf->consts[memory_addr] = w;
    } else if (memory_num == 2) {
        mcode = serdes_get32(sd);

        tmp1 = revbits(mcode & 0xFFFF, 4, 0x1111);
        tmp2 = revbits((mcode >> 16) & 0xFFFF, 4, 0x1111);
        mcode = ((uint32_t) tmp1) + (((uint32_t) tmp2) << 16);
        mcode ^= 0x88400;

        exp_line_num = 5 + (mcode >> 16);

        if (unlikely(!objfile_add_microcode(objf, memory_addr, mcode))) {
            report_error("objfile: deserialize: "
                         "cannot add microcode");
            return FALSE;
        }
    } else {
        report_error("objfile: deserialize: "
                     "invalid memory number: %u",
                     memory_num);
        return FALSE;
    }

    if (unlikely(line_num != exp_line_num)) {
        report_error("objfile: deserialize: "
                     "invalid line_num: expecting %u, but got %u",
                     exp_line_num, line_num);
        return FALSE;
    }

    return TRUE;
}

/* Deserializes a symbol definition block.
 * Returns TRUE on success.
 */
static
int deserialize_symbol(struct objfile *objf, struct serdes *sd)
{
    struct string name;
    uint16_t mem_num, addr, val;
    size_t len;

    mem_num = serdes_get16(sd);
    addr = serdes_get16(sd);

    len = serdes_get_string(sd, objf->tbuf, objf->tbuf_size);
    if (!(len & 1)) serdes_get8(sd); /* for alignment */
    if (len + 1 > objf->tbuf_size) {
        report_error("objfile: deserialize: "
                     "string too large");
        return FALSE;
    }

    name.s = objf->tbuf;
    name.len = len;
    name.hash = string_hash(name.s, name.len);

    if (mem_num == 1) {
        val = objf->consts[addr];
        if (unlikely(!objfile_add_constant(objf, &name,
                                           addr, val))) {
            report_error("objfile: deserialize: "
                         "cannot add constant");
            return FALSE;
        }
    } else if (mem_num == 2) {
        if (unlikely(!objfile_add_label(objf, &name, addr))) {
            report_error("objfile: deserialize: "
                         "cannot add label");
            return FALSE;
        }
    } else if (mem_num == 3) {
        if (unlikely(!objfile_add_register(objf, &name, addr))) {
            report_error("objfile: deserialize: "
                         "cannot add register");
            return FALSE;
        }
    } else {
        report_error("objfile: deserialize: "
                     "invalid memory number: %u",
                     mem_num);
        return FALSE;
    }

    return TRUE;
}


int objfile_deserialize(struct objfile *objf, struct serdes *sd)
{
    uint16_t block_type;
    uint16_t mem_num;
    uint16_t curr_mem, curr_addr;
    int defined[3];

    for (mem_num = 1; mem_num <= 3; mem_num++) {
        defined[mem_num - 1] = FALSE;
    }

    curr_mem = 0xFFFF;
    curr_addr = 0;

    objfile_clear(objf);
    while (TRUE) {
        block_type = serdes_get16(sd);
        if (block_type == 0) break;

        switch (block_type) {
        case 4: /* Define memory */
            if (unlikely(!deserialize_memory(objf, sd, &mem_num)))
                return FALSE;

            if (unlikely(defined[mem_num - 1])) {
                report_error("objfile: deserialize: "
                             "memory %u already defined",
                             mem_num);
                return FALSE;
            }
            defined[mem_num - 1] = TRUE;
            break;

        case 2: /* Set address */
            if (unlikely(!deserialize_address(sd, &curr_mem,
                                              &curr_addr)))
                return FALSE;

            if (unlikely(!defined[curr_mem - 1])) {
                report_error("objfile: deserialize: "
                             "memory number %u not yet defined",
                             curr_mem);
                return FALSE;
            }
            break;

        case 1: /* data word */
            if (unlikely(!deserialize_data(objf, sd, curr_mem,
                                           curr_addr)))
                return FALSE;

            curr_addr++;
            break;

        case 5: /* define symbol */
            if (unlikely(!deserialize_symbol(objf, sd)))
                return FALSE;
            break;

        default:
            report_error("objfile: deserialize: "
                         "block_type: %u", block_type);
            return FALSE;
        }
    }

    return TRUE;
}

int objfile_write_binary(const struct objfile *objf,
                         const char *filename)
{
    struct serdes sd;
    size_t size;

    size = 102400;
    if (unlikely(!serdes_create(&sd, size, TRUE))) {
        report_error("objfile: write_binary: "
                     "could not create serializer");
        return FALSE;
    }

    objfile_serialize(objf, &sd);

    if (unlikely(!serdes_verify(&sd))) {
        report_error("objfile: write_binary: "
                     "could not serialize");
        serdes_destroy(&sd);
        return FALSE;
    }

    if (unlikely(!serdes_write(&sd, filename))) {
        report_error("objfile: write_binary: "
                     "could not write file");
        serdes_destroy(&sd);
        return FALSE;
    }

    serdes_destroy(&sd);
    return TRUE;
}

int objfile_load_binary(struct objfile *objf,
                        const char *filename)
{
    struct serdes sd;
    size_t pos, size;

    size = 102400;
    if (unlikely(!serdes_create(&sd, size, TRUE))) {
        report_error("objfile: load_binary: "
                     "could not create serializer");
        return FALSE;
    }

    if (unlikely(!serdes_read(&sd, filename))) {
        report_error("objfile: load_binary: "
                     "could not read file");
        serdes_destroy(&sd);
        return FALSE;
    }

    pos = sd.pos;
    serdes_rewind(&sd);
    objfile_deserialize(objf, &sd);

    if (unlikely(sd.pos != pos)) {
        report_error("objfile: load_binary: "
                     "invalid file `%s`", filename);
        serdes_destroy(&sd);
        return FALSE;
    }

    serdes_destroy(&sd);
    return TRUE;
}

int objfile_dump_constant_rom(const struct objfile *objf,
                              const char *filename)
{
    struct serdes sd;
    size_t size;

    size = CONSTANT_SIZE * sizeof(uint16_t);
    if (unlikely(!serdes_create(&sd, size, FALSE))) {
        report_error("objfile: dump_constant_rom: "
                     "could not create serializer");
        return FALSE;
    }

    serdes_put16_array(&sd, objf->consts, CONSTANT_SIZE);

    if (unlikely(!serdes_write(&sd, filename))) {
        report_error("objfile: dump_constant_rom: "
                     "could not write file");
        serdes_destroy(&sd);
        return FALSE;
    }

    serdes_destroy(&sd);
    return TRUE;
}

int objfile_dump_microcode_rom(const struct objfile *objf,
                               const char *filename)
{
    struct serdes sd;
    size_t size;

    size = MICROCODE_SIZE * sizeof(uint32_t);
    if (unlikely(!serdes_create(&sd, size, FALSE))) {
        report_error("objfile: dump_microcode_rom: "
                     "could not create serializer");
        return FALSE;
    }

    serdes_put32_array(&sd, objf->microcode, MICROCODE_SIZE);

    if (unlikely(!serdes_write(&sd, filename))) {
        report_error("objfile: dump_microcode_rom: "
                     "could not write file");
        serdes_destroy(&sd);
        return FALSE;
    }

    serdes_destroy(&sd);
    return TRUE;
}

/* Auxiliary function used by objfile_disassemble().
 * Callback to print constants, registers, labels, etc.
 */
static
void disasm_decode_cb(struct decoder *dec,
                      enum decode_type dec_type, uint32_t val,
                      void *arg)
{
    struct string_buffer *output;
    const struct objsymb *osym;
    struct disasm_cb_arg *c_arg;
    const struct objfile *objf;
    unsigned int index;
    decoder_cb orig_dec_cb;
    void *orig_arg;

    output = dec->output;

    c_arg = (struct disasm_cb_arg *) arg;
    objf = c_arg->objf;
    index = c_arg->index;
    orig_dec_cb = c_arg->orig_dec_cb;
    orig_arg = c_arg->orig_arg;

    switch (dec_type) {
    case DECODE_CONST:
        if (val >= CONSTANT_SIZE) {
            dec->error = TRUE;
            return;
        }

        osym = objf->const_chain[val];
        if (!osym) {
            orig_dec_cb(dec, DECODE_CONST, val, orig_arg);
            return;
        }
        break;

    case DECODE_REG:
        if (val >= REG_SIZE) {
            dec->error = TRUE;
            return;
        }

        osym = objf->reg_chain[val];
        if (!osym) {
            orig_dec_cb(dec, DECODE_REG, val, orig_arg);
            return;
        }
        break;

    case DECODE_LABEL:
        osym = objf->label_chain[val & (MICROCODE_SIZE - 1)];
        if (!osym) {
            /* Do not print any label here. */
            /* orig_dec_cb(dec, DECODE_LABEL, val, orig_arg); */
            return;
        }

        /* Safe to print symbol here as it is unique. */
        string_buffer_print(output, "%s", osym->n.str.s);
        return;

    default:
        orig_dec_cb(dec, dec_type, val, orig_arg);
        return;

    }

    while (TRUE) {
        /* If the osym is no more recent than the current
         * microcode, then we can stop.
         */
        if (osym->index <= index) break;

        /* If there is no more symbols in the chain, also stop. */
        if (!osym->chain_next) break;

        osym = osym->chain_next;
    }
    string_buffer_print(output, "%s", osym->n.str.s);
    if (dec_type == DECODE_CONST)
        c_arg->const_name = osym->n.str.s;
}

void objfile_disassemble(const struct objfile *objf,
                         struct decoder *dec,
                         const struct microcode *mc)
{
    struct disasm_cb_arg new_arg;
    const struct objsymb *osym;

    if (dec->error) return;

    new_arg.objf = objf;

    /* Be conservative: when osym is NULL, accept any symbol. */
    osym = objf->mu_chain[mc->address & (MICROCODE_SIZE - 1)];
    new_arg.index = (osym) ? osym->index : objf->num_symbs;

    new_arg.const_name = NULL;
    new_arg.orig_dec_cb = dec->dec_cb;
    new_arg.orig_arg = dec->arg;

    dec->dec_cb = &disasm_decode_cb;
    dec->arg = (void *) &new_arg;
    microcode_decode(dec, mc);
    dec->dec_cb = new_arg.orig_dec_cb;
    dec->arg = new_arg.orig_arg;

    if (new_arg.const_name) {
        /* Print a comment with the constant value. */
        string_buffer_print(dec->output, "; %s = ", new_arg.const_name);
        (dec->dec_cb)(dec, DECODE_CONST, mc->const_addr, dec->arg);
    }
}
