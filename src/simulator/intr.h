
#ifndef __SIMULATOR_INTR_H
#define __SIMULATOR_INTR_H

#include <stdint.h>

/* Macros. */
#define INTR_CYCLE(x) ((x) & 0x7FFFFFFF)
#define INTR_DIFF_NEG(x) ((x) & 0x40000000)

/* Functions. */

/* Computes the next interrupt cycle.
 * The `cycle` parameters specifies the current cycle. The `must_advance`
 * is for validation. This is to ensure that the resulting output
 * (the next interrupt cycle) is after the current cycle in `cycle`.
 * The `n` parameter specifies the length of the array `intr_cycles`.
 * The array `intr_cycles` specifies the possible interrupt cycles and
 * the result of this function is the most imminent interrupt cycle
 * among all the valid values (i.e., non-negative) in this array.
 * The monst imminent interrupt cycle is the one closest to `cycle`.
 * A negative value in `intr_cycles` means to ignore the value.
 * When all the values in `intr_cycles` are negative, the resulting
 * value is negative as well. The output of the function, i.e., the
 * next interrupt cycle is returned via the parameter `intr_cycle`.
 * Returns TRUE if the computation was successful.
 */
int compute_intr_cycle(int32_t cycle, int must_advance,
                       int n, const int32_t *intr_cycles,
                       int32_t *intr_cycle);


#endif /* __SIMULATOR_INTR_H */
