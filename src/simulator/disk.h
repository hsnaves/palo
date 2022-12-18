
#ifndef __SIMULATOR_DISK_H
#define __SIMULATOR_DISK_H

#include <stdint.h>

/* Constants. */
#define NUM_DISK_DRIVES           2

/* Data structures and types. */

/* Structure representing the disk geometry. */
struct disk_geometry {
    uint16_t num_cylinders;       /* Number of cylinders. */
    uint16_t num_heads;           /* Number of heads per cylinder. */
    uint16_t num_sectors;         /* Number of sectors per head. */
};

/* Structure representing a disk sector. */
struct disk_sector {
    uint16_t header[2];           /* Sector header. */
    uint16_t label[8];            /* The sector label. */
    uint16_t data[256];           /* Sector data. */
};

/* A single disk driver structure. */
struct disk_drive {
    struct disk_geometry dg;      /* The disk geometry. */
    struct disk_sector *sectors;  /* The disk sectors. */
    uint16_t length;              /* Total length of the disk in sectors. */
    uint16_t size;                /* Total allocated size (in sectors). */
};

/* Structure representing the disk controller for the simulator. */
struct disk {
    struct disk_drive drives[2];  /* The two disk drives. */
    uint16_t kstat;               /* KSTAT register. */
    uint16_t kdata;               /* KDATA register. */
    uint16_t kadr;                /* KADR register. */
    uint16_t kcomm;               /* KCOMM register. */

    uint32_t intr_cycle;          /* Cycle of the next interrupt. */
    int dw_pending;               /* If the disk word task is pending. */
    int ds_pending;               /* If the disk sector task is pending. */
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

/* Reads the contents of the disk from a disk pack file named `filename`.
 * Returns TRUE on success.
 */
int disk_load_image(struct disk *dsk, unsigned int drive_num,
                    const char *filename);

/* Writes the contents of the disk to a file named `filename`.
 * Returns TRUE on success.
 */
int disk_save_image(const struct disk *dsk, unsigned int drive_num,
                    const char *filename);

/* Resets the disk controller. */
void disk_reset(struct disk *dsk);

/* Processes the disk interrupts. */
void disk_interrupt(struct disk *dsk);

#endif /* __SIMULATOR_DISK_H */
