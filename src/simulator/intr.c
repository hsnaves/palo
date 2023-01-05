
#include <stdint.h>

#include "simulator/intr.h"
#include "common/utils.h"

/* Computes the next interrupt cycle. */
int32_t compute_intr_cycle(int32_t cycle, int n,
                           const int32_t *intr_cycles)
{
    int32_t diff, tmp;
    int i;

    diff = -1;
    for (i = 0; i < n; i++) {
        if (intr_cycles[i] >= 0) {
            tmp = intr_cycles[i] - cycle;
            diff = INTR_CYCLE(diff);
            diff = MIN(diff, tmp);
        }
    }

    if (diff < 0) return -1;
    return INTR_CYCLE(diff + cycle);
}
