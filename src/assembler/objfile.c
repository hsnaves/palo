
#include <stdint.h>
#include <stdlib.h>

#include "assembler/objfile.h"
#include "microcode/microcode.h"
#include "common/allocator.h"
#include "common/table.h"
#include "common/serdes.h"
#include "common/utils.h"

/* Constants. */
#define REG_SIZE          (2 * (R_MASK + 1))

/* Functions. */

void objfile_initvar(struct objfile *objf)
{
    allocator_initvar(&objf->salloc);
    allocator_initvar(&objf->oalloc);
    table_initvar(&objf->symbols);

    objf->consts = NULL;
    objf->microcode = NULL;
    objf->has_microcode = NULL;

    objf->const_symbs = NULL;
    objf->reg_symbs = NULL;
    objf->label_symbs = NULL;
    objf->mu_c_symbs = NULL;
    objf->mu_r_symbs = NULL;
}

void objfile_destroy(struct objfile *objf)
{
    if (objf->consts) free((void *) objf->consts);
    objf->consts = NULL;

    if (objf->microcode) free((void *) objf->microcode);
    objf->microcode = NULL;

    if (objf->has_microcode) free((void *) objf->has_microcode);
    objf->has_microcode = NULL;

    if (objf->const_symbs) free((void *) objf->const_symbs);
    objf->const_symbs = NULL;

    if (objf->reg_symbs) free((void *) objf->reg_symbs);
    objf->reg_symbs = NULL;

    if (objf->label_symbs) free((void *) objf->label_symbs);
    objf->label_symbs = NULL;

    if (objf->mu_c_symbs) free((void *) objf->mu_c_symbs);
    objf->mu_c_symbs = NULL;

    if (objf->mu_r_symbs) free((void *) objf->mu_r_symbs);
    objf->mu_r_symbs = NULL;

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
    objf->has_microcode = (uint8_t *)
        malloc(MICROCODE_SIZE * sizeof(uint8_t));

    objf->const_symbs = (struct objsymb **)
        malloc(CONSTANT_SIZE * sizeof(struct objsymb *));
    objf->reg_symbs = (struct objsymb **)
        malloc(REG_SIZE * sizeof(struct objsymb *));
    objf->label_symbs = (struct objsymb **)
        malloc(MICROCODE_SIZE * sizeof(struct objsymb *));
    objf->mu_c_symbs = (struct objsymb **)
        malloc(MICROCODE_SIZE * sizeof(struct objsymb *));
    objf->mu_r_symbs = (struct objsymb **)
        malloc(MICROCODE_SIZE * sizeof(struct objsymb *));

    if (unlikely(!objf->consts || !objf->microcode
                 || !objf->has_microcode || !objf->const_symbs
                 || !objf->reg_symbs || !objf->label_symbs
                 || !objf->mu_c_symbs || !objf->mu_r_symbs)) {
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
        objf->const_symbs[i] = NULL;
    }

    for (i = 0; i < REG_SIZE; i++) {
        objf->reg_symbs[i] = NULL;
    }

    for (i = 0; i < MICROCODE_SIZE; i++) {
        /* Jump to the last address in rom. */
        objf->microcode[i] = 0xFFF77BFF;
        objf->has_microcode[i] = 0;
        objf->label_symbs[i] = NULL;
        objf->mu_c_symbs[i] = NULL;
        objf->mu_r_symbs[i] = NULL;
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

    osym = (struct objsymb *)
        allocator_alloc(&objf->oalloc, sizeof(struct objsymb), TRUE);
    if (unlikely(!osym)) {
        report_error("objfile: new_objsymb: "
                     "memory exhausted");
        return NULL;
    }
    osym->type = type;
    osym->value = value;

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
    osym->n.next = NULL;

    if (!other) {
        if (unlikely(!table_add(&objf->symbols, &osym->n))) {
            report_error("objsfile: new_objsymb: "
                         "could not add symbol to hash table");
            return NULL;
        }
    }

    /* Adds the symbol to the list of symbols. */
    objf->num_symbs++;
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

    if (unlikely(address >= CONSTANT_SIZE)) {
        report_error("objfile: add_constant: "
                     "invalid address: %u", address);
        return FALSE;
    }

    if (unlikely(!name)) {
        report_error("objfile: add_constant: "
                     "name is NULL");
        return FALSE;
    }

    osym = new_objsymb(objf, OBJSYMB_CONSTANT, address, name);
    if (unlikely(!osym)) {
        report_error("objfile: add_constant: "
                     "could not create symbol");
        return FALSE;
    }

    objf->consts[address] = value;
    objf->const_symbs[address] = osym;

    return TRUE;
}

int objfile_add_register(struct objfile *objf,
                         const struct string *name,
                         uint16_t address)
{
    struct objsymb *osym;

    if (unlikely(address >= REG_SIZE)) {
        report_error("objfile: add_register: "
                     "invalid address: %u", address);
        return FALSE;
    }

    if (unlikely(!name)) {
        report_error("objfile: add_register: "
                     "name is NULL");
        return FALSE;
    }

    osym = new_objsymb(objf, OBJSYMB_REGISTER, address, name);
    if (unlikely(!osym)) {
        report_error("objfile: add_register: "
                     "could not create symbol");
        return FALSE;
    }

    objf->reg_symbs[address] = osym;

    return TRUE;
}

int objfile_add_label(struct objfile *objf,
                      const struct string *name,
                      uint16_t address)
{
    struct objsymb *osym;

    if (unlikely(address >= MICROCODE_SIZE)) {
        report_error("objfile: add_label: "
                     "invalid address: %u", address);
        return FALSE;
    }

    if (unlikely(!name)) {
        report_error("objfile: add_label: "
                     "name is NULL");
        return FALSE;
    }

    if (objf->label_symbs[address]) {
        report_error("objfile: add_label: "
                     "label already defined for given address: %u",
                     address);
        return FALSE;
    }

    osym = new_objsymb(objf, OBJSYMB_LABEL, address, name);
    if (unlikely(!osym)) {
        report_error("objfile: add_label: "
                     "could not create symbol");
        return FALSE;
    }

    objf->label_symbs[address] = osym;

    return TRUE;
}

int objfile_add_microcode(struct objfile *objf,
                          uint16_t address, uint32_t mcode)
{
    struct microcode mc;
    uint16_t rsel;

    if (unlikely(address >= MICROCODE_SIZE)) {
        report_error("objfile: add_microcode: "
                     "invalid address: %u", address);
        return FALSE;
    }

    if (objf->has_microcode[address]) {
        report_error("objfile: add_microcode: "
                     "microcode already defined for given address: %u",
                     address);
        return FALSE;
    }

    /* The sys_type, and the task are irrelevant for the
     * microcode_predecode().
     */
    microcode_predecode(&mc, ALTO_I, address, mcode, TASK_EMULATOR);

    rsel = mc.rsel;
    if (mc.bs == BS_TASK_SPECIFIC1 || mc.bs == BS_TASK_SPECIFIC2)
        rsel = mc.rsel + (R_MASK + 1);

    objf->microcode[address] = mcode;
    objf->has_microcode[address] = TRUE;

    objf->mu_c_symbs[address] = objf->const_symbs[mc.const_addr];
    objf->mu_r_symbs[address] = objf->reg_symbs[rsel];

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
        other = objf->const_symbs[addr];
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
        other = objf->reg_symbs[addr];
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

int objfile_dump_constant_rom(const struct objfile *objf,
                              const char *filename)
{
    struct serdes sd;
    size_t size;

    size = CONSTANT_SIZE * sizeof(uint16_t);
    if (unlikely(!serdes_create(&sd, size))) {
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
    if (unlikely(!serdes_create(&sd, size))) {
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

