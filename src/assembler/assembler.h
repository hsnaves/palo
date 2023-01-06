
#ifndef __ASSEMBLER_ASSEMBLER_H
#define __ASSEMBLER_ASSEMBLER_H

#include <stddef.h>
#include <stdint.h>
#include "parser/parser.h"
#include "assembler/objfile.h"
#include "common/allocator.h"

/* Data structures and types. */

/* Structure to represent the assembler. */
struct assembler {
    struct allocator salloc;      /* Allocator for strings. */
    struct allocator oalloc;      /* Allocator for objects. */
    struct parser p;              /* The parser. */
    uint16_t *consts;             /* The value of the constants. */
    struct statement **const_sts; /* The statements declarating constants. */

    uint32_t *microcode;          /* The microcode. */
    struct statement **micro_sts; /* The statements of the microcode. */
};

/* Functions. */

/* Initializes the assembler variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void assembler_initvar(struct assembler *as);

/* Destroys the assembler object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void assembler_destroy(struct assembler *as);

/* Creates a new assembler object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int assembler_create(struct assembler *as);

/* Resolves the constant addresses from the source files.
 * Returns TRUE on success.
 */
int assembler_resolve_constants(struct assembler *as);

/* Resolves the label addresses from the source files.
 * Returns TRUE on success.
 */
int assembler_resolve_labels(struct assembler *as);

/* Assembles the microcode.
 * Returns TRUE on success.
 */
int assembler_assemble(struct assembler *as);

/* Converts the output of the assembler to an objfile.
 * The resulting objfile is in the parameter `objf`.
 * Returns TRUE on success.
 */
int assembler_produce_objfile(const struct assembler *as,
                              struct objfile *objf);

/* Prints the assembly listing.
 * The listing is printed to file `filename`.
 * Returns TRUE on success.
 */
int assembler_print_listing(struct assembler *as, const char *filename);

#endif /* __ASSEMBLER_ASSEMBLER_H */
