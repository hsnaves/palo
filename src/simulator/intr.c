
#include <stdint.h>

#include "simulator/intr.h"
#include "common/utils.h"

/* Functions. */

/* Computes the next interrupt cycle. */
int compute_intr_cycle(int32_t cycle, int must_advance,
                       int n, const int32_t *intr_cycles,
                       int32_t *intr_cycle)
{
    int32_t diff, tmp;
    int i;

    diff = -1;
    for (i = 0; i < n; i++) {
        if (intr_cycles[i] >= 0) {
            tmp = INTR_CYCLE(intr_cycles[i] - cycle);
            /* Some error must have happened. */
            if (unlikely(((tmp == 0) && must_advance)
                         || (INTR_DIFF_NEG(tmp)))) {
                report_error("intr: compute_intr_cycle: "
                             "entry %d has value %d, cycle is %d",
                             i, intr_cycles[i], cycle);
                return FALSE;
            }
            diff = INTR_CYCLE(diff);
            diff = MIN(diff, tmp);
        }
    }

    if (diff < 0) {
        *intr_cycle = -1;
    } else {
        *intr_cycle = INTR_CYCLE(diff + cycle);
    }

    return TRUE;
}
