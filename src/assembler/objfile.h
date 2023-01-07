
#ifndef __ASSEMBLER_OBJFILE_H
#define __ASSEMBLER_OBJFILE_H

#include <stdint.h>

#include "microcode/microcode.h"
#include "common/allocator.h"
#include "common/table.h"
#include "common/serdes.h"
#include "common/string_buffer.h"

/* Data structures and types. */

/* The possible types of the object symbols. */
enum objsymb_type {
    OBJSYMB_CONSTANT,
    OBJSYMB_REGISTER,
    OBJSYMB_LABEL,
    OBJSYMB_MU,
};

/* A structure representing an symbol in the object file. */
struct objsymb {
    struct string_node n;         /* To be added to the symbol table. */
    enum objsymb_type type;       /* The type of the symbol. */
    uint16_t value;               /* The value of the symbol. */
    struct objsymb *next;         /* The next symbol in the list. */
};

/* Structure to represent the object file. */
struct objfile {
    struct allocator salloc;      /* Allocator for strings. */
    struct allocator oalloc;      /* Allocator for objects. */
    struct table symbols;         /* Hash table for resolving symbols. */
    uint16_t *consts;             /* The value of the constants. */
    uint32_t *microcode;          /* The microcode. */
    uint8_t *has_microcode;       /* Boolean mask telling which addresses
                                   * are used.
                                   */

    unsigned int num_symbs;       /* The number of symbols. */
    struct objsymb *first_symb;   /* The first symbol defined. */
    struct objsymb *last_symb;    /* The last symbol defined. */

    struct objsymb **const_symbs; /* Constant symbols. */
    struct objsymb **reg_symbs;   /* Register symbols. */
    struct objsymb **label_symbs; /* Label symbols. */
    struct objsymb **mu_c_symbs;  /* Microcode constant symbols. */
    struct objsymb **mu_r_symbs;  /* Microcode register symbols. */

    char *tbuf;                   /* Temporary buffer. */
    size_t tbuf_size;             /* Size of the temporary buffer. */
};

/* Functions. */

/* Initializes the objfile variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void objfile_initvar(struct objfile *objf);

/* Destroys the objfile object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void objfile_destroy(struct objfile *objf);

/* Creates a new objfile object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int objfile_create(struct objfile *objf);

/* Clears the contents of the objfile. */
void objfile_clear(struct objfile *objf);

/* Adds a constant to the object file.
 * The name of the constant is given by `name`, its address by `address`,
 * and its value by `value`.
 * Returns TRUE on success.
 */
int objfile_add_constant(struct objfile *objf,
                         const struct string *name,
                         uint16_t address, uint16_t value);

/* Adds a register definition the object file.
 * The name of the register is given by `name`, and its address by
 * `address`. Here the address is just the index of the register.
 * Returns TRUE on success.
 */
int objfile_add_register(struct objfile *objf,
                         const struct string *name,
                         uint16_t address);

/* Adds a label definition the object file.
 * The name of the label is given by `name`, and its address by
 * `address`.
 * Returns TRUE on success.
 */
int objfile_add_label(struct objfile *objf,
                      const struct string *name,
                      uint16_t address);

/* Adds a microcode to the object file.
 * The microcode address is given by `address`, and the microcode itself
 * is given by `mcode`.
 * Returns TRUE on success.
 */
int objfile_add_microcode(struct objfile *objf,
                          uint16_t address, uint32_t mcode);

/* Adds a microcode, but ensures that the symbols resolve correctly.
 * It accepts the same parameters as objfile_add_microcode(), and in
 * addition, it accepts `c_name` and `r_name`. The `c_name` parameter,
 * if not NULL, indicates the name of the constant used by this microcode.
 * Similarly, `r_name` indicates the name of the register used by this
 * microcode.
 * Returns TRUE on success.
 */
int objfile_add_microcode_symbols(struct objfile *objf,
                                  const struct string *c_name,
                                  const struct string *r_name,
                                  uint16_t address, uint32_t mcode);

/* Resolves a symbol by the type `type` and the name `name`.
 * Returns the resolved symbol (or NULL if there is no such symbol).
 */
struct objsymb *objfile_resolve(const struct objfile *objf,
                                enum objsymb_type type,
                                const struct string *name);

/* Checks if the contants in `consts` match the contants in the obfile.
 * Returns TRUE if they match
 */
int objfile_check_constants(const struct objfile *objf,
                            const uint8_t consts[CONSTANT_SIZE]);

/* Serializes the objfile `objf` into `sd`. */
void objfile_serialize(const struct objfile *objf, struct serdes *sd);

/* Deserializes the objfile `obf` from `sd`.
 * Returns TRUE on success.
 */
int objfile_deserialize(struct objfile *objf, struct serdes *sd);

/* Writes out the binary file.
 * The name of the file to write is given by `filename`.
 * Returns TRUE on success.
 */
int objfile_write_binary(const struct objfile *objf,
                         const char *filename);

/* Loads the binary file.
 * The name of the file to load is given by `filename`.
 * Returns TRUE on success.
 */
int objfile_load_binary(struct objfile *objf,
                        const char *filename);

/* Dumps the constant rom to a file.
 * The file is named `filename`.
 * Returns TRUE on success.
 */
int objfile_dump_constant_rom(const struct objfile *objf,
                              const char *filename);

/* Dumps the microcode rom to a file.
 * The file is named `filename`.
 * Returns TRUE on success.
 */
int objfile_dump_microcode_rom(const struct objfile *objf,
                               const char *filename);

/* Disassembles a microinstruction given by `mc`.
 * The output is written to `output`.
 */
void objfile_disassemble(const struct objfile *objf,
                         const struct microcode *mc,
                         struct string_buffer *output);


#endif /* __ASSEMBLER_OBJFILE_H */
