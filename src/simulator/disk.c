
#include <stdint.h>
#include <stdlib.h>

#include "simulator/disk.h"
#include "common/utils.h"

/* Functions. */

void disk_initvar(struct disk *d)
{
    d->drives[0].sector_data = NULL;
    d->drives[1].sector_data = NULL;
}

void disk_destroy(struct disk *d)
{
}

int disk_create(struct disk *d)
{
    disk_initvar(d);
    return TRUE;
}

