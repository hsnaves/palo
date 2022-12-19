
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

    int32_t intr_cycle;           /* Cycle of the next interrupt. */
    uint16_t pending;             /* The task pending mask. */
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

/* Reads the KSTAT register.
 * Returns the contents of the KSTAT register.
 */
uint16_t disk_read_kstat(struct disk *dsk);

/* Writes to the KSTAT register.
 * The value on the bus is given by `bus`.
 */
void disk_load_kstat(struct disk *dsk, uint16_t bus);

/* Reads the KDATA register.
 * Returns the contents of the KDATA register.
 */
uint16_t disk_read_kdata(struct disk *dsk);

/* Writes to the KDATA register.
 * The value on the bus is given by `bus`.
 */
void disk_load_kdata(struct disk *dsk, uint16_t bus);

/* Writes to the KCOMM register.
 * The value on the bus is given by `bus`.
 */
void disk_load_kcomm(struct disk *dsk, uint16_t bus);

/* Writes to the KADR register.
 * The value on the bus is given by `bus`.
 */
void disk_load_kadr(struct disk *dsk, uint16_t bus);

/* Executes a F1_DSK_STROBE. */
void disk_strobe(struct disk *dsk);

/* Executes a F1_DSK_INCRECNO. */
void disk_increcno(struct disk *dsk);

/* Executes a F1_DSK_CLRSTAT. */
void disk_clrstat(struct disk *dsk);

/* Executes a F2_DSK_INIT.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_init(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_RWC.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_rwc(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_RECNO.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_recno(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_XFRDAT.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_xfrdat(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_SWRNRDY.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_swrnrdy(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_NFER.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_nfer(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_STROBON.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_strobon(struct disk *dsk, uint8_t task);

/* Processes a BLOCK instruction.
 * The task to be blocked is in the parameter `task`.
 */
void disk_block_task(struct disk *dsk, uint8_t task);

/* Processes the disk interrupts. */
void disk_interrupt(struct disk *dsk);

#endif /* __SIMULATOR_DISK_H */
