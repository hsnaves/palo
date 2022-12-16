
#include <stdint.h>
#include <stdlib.h>

#include "simulator/disk.h"
#include "common/utils.h"

/* Functions. */

void disk_initvar(struct disk *dsk)
{
    dsk->drives[0].sector_data = NULL;
    dsk->drives[1].sector_data = NULL;
}

void disk_destroy(struct disk *dsk)
{
}

int disk_create(struct disk *dsk)
{
    disk_initvar(dsk);
    return TRUE;
}

