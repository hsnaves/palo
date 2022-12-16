
#ifndef __SIMULATOR_DISK_H
#define __SIMULATOR_DISK_H

#include <stdint.h>

/* Data structures and types. */

/* A single disk driver structure. */
struct disk_drive {
    uint8_t *sector_data;
};

/* Structure representing the disk controller for the simulator. */
struct disk {
    struct disk_drive drives[2];
};

/* Functions. */

/* Initializes the disk variable.
 * Note that this does not create the object yet.
 * This obeys the initvar / destroy / create protocol.
 */
void disk_initvar(struct disk *dsk);

/* Destroys the disk object
 * (and releases all the used resources).
 * This obeys the initvar / destroy / create protocol.
 */
void disk_destroy(struct disk *dsk);

/* Creates a new disk object.
 * This obeys the initvar / destroy / create protocol.
 * Returns TRUE on success.
 */
int disk_create(struct disk *dsk);


#endif /* __SIMULATOR_DISK_H */
