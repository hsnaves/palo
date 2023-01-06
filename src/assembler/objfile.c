
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

    objf->const_symbs = NULL;
    objf->reg_symbs = NULL;
    objf->label_symbs = NULL;
    objf->mu_symbs = NULL;
}

void objfile_destroy(struct objfile *objf)
{
    if (objf->consts) free((void *) objf->consts);
    objf->consts = NULL;

    if (objf->microcode) free((void *) objf->microcode);
    objf->microcode = NULL;

    if (objf->const_symbs) free((void *) objf->const_symbs);
    objf->const_symbs = NULL;

    if (objf->reg_symbs) free((void *) objf->reg_symbs);
    objf->reg_symbs = NULL;

    if (objf->label_symbs) free((void *) objf->label_symbs);
    objf->label_symbs = NULL;

    if (objf->mu_symbs) free((void *) objf->mu_symbs);
    objf->mu_symbs = NULL;

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

    objf->const_symbs = (struct objsymb **)
        malloc(CONSTANT_SIZE * sizeof(struct objsymb *));
    objf->reg_symbs = (struct objsymb **)
        malloc(REG_SIZE * sizeof(struct objsymb *));
    objf->label_symbs = (struct objsymb **)
        malloc(MICROCODE_SIZE * sizeof(struct objsymb *));
    objf->mu_symbs = (struct objsymb **)
        malloc(MICROCODE_SIZE * sizeof(struct objsymb *));

    if (unlikely(!objf->consts || !objf->microcode
                 || !objf->const_symbs || !objf->reg_symbs
                 || !objf->label_symbs || !objf->mu_symbs)) {
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
        objf->label_symbs[i] = NULL;
        objf->mu_symbs[i] = NULL;
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
    const struct string_node *n;
    const struct objsymb *other;
    size_t offset;

    n = NULL;
    other = NULL;
    if (name) {
        n = table_find(&objf->symbols, name);
        if (n) {
            offset = offsetof(struct objsymb, n);
            other = (const struct objsymb *) &(((char *) n)[-offset]);

            if (other->type == type) {
                if (other->value != value) {
                    report_error("objfile: new_objsymb: "
                                 "repeated name with different values: "
                                 "%s -> %u vs %u (type: %u)",
                                 other->n.str.s, other->value,
                                 value, type);
                    return NULL;
                }
            } else {
                other = NULL;
            }
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

    if (name) {
        if (n) {
            /* To avoid allocating memory. */
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

    osym = new_objsymb(objf, OBJSYMB_CONSTANT, address, name);
    if (unlikely(!osym)) {
        report_error("objfile: add_constant: "
                     "could not create symbol");
        return FALSE;
    }

    objf->consts[address] = value;
    osym->chain = objf->const_symbs[address];
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

    osym = new_objsymb(objf, OBJSYMB_REGISTER, address, name);
    if (unlikely(!osym)) {
        report_error("objfile: add_register: "
                     "could not create symbol");
        return FALSE;
    }

    osym->chain = objf->reg_symbs[address];
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

    osym->chain = NULL;
    objf->label_symbs[address] = osym;

    return TRUE;
}

int objfile_add_microcode(struct objfile *objf, uint16_t address,
                          uint32_t microcode)
{
    struct objsymb *osym;

    if (unlikely(address >= MICROCODE_SIZE)) {
        report_error("objfile: add_microcode: "
                     "invalid address: %u", address);
        return FALSE;
    }

    if (objf->mu_symbs[address]) {
        report_error("objfile: add_microcode: "
                     "microcode already defined for given address: %u",
                     address);
        return FALSE;
    }

    osym = new_objsymb(objf, OBJSYMB_MU, address, NULL);
    if (unlikely(!osym)) {
        report_error("objfile: add_microcode: "
                     "could not create symbol");
        return FALSE;
    }

    objf->microcode[address] = microcode;
    objf->mu_symbs[address] = osym;

    return TRUE;
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

