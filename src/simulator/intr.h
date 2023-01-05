
#ifndef __SIMULATOR_INTR_H
#define __SIMULATOR_INTR_H

#include <stdint.h>

/* Macros. */
#define INTR_CYCLE(x) ((x) & 0x7FFFFFFF)

/* Functions. */

/* Computes the next interrupt cycle.
 * The `cycle` parameters specifies the current cycle.
 * The `n` parameter specifies the length of the array `intr_cycles`.
 * The array `intr_cycles` specifies the possible interrupt cycles and
 * the result of this function is the most imminent interrupt cycle
 * among all the valid values (i.e., non-negative) in this array.
 * The monst imminent interrupt cycle is the one closest to `cycle`.
 * A negative value in `intr_cycles` means to ignore the value.
 * When all the values in `intr_cycles` are negative, this function
 * returns a negative value as well.
 * Returns the next interrupt cycle.
 */
int32_t compute_intr_cycle(int32_t cycle, int n,
                           const int32_t *intr_cycles);


#endif /* __SIMULATOR_INTR_H */
