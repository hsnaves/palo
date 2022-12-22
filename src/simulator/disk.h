
#ifndef __SIMULATOR_DISK_H
#define __SIMULATOR_DISK_H

#include <stdint.h>

#include "common/utils.h"

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

    uint16_t head;                /* Current head. */
    uint16_t cylinder;            /* Current cylinder. */
    uint16_t target_cylinder;     /* The target cylinder for seek. */
    uint16_t sector;              /* Current sector. */
    uint16_t sector_word;         /* Current word in the sector. */

    int loaded;                   /* Disk was loaded. */
};

/* Structure representing the disk controller for the simulator. */
struct disk {
    struct disk_drive drives[2];  /* The two disk drives. */
    uint16_t kstat;               /* KSTAT register. */
    uint16_t kdata_read;          /* KDATA register (to read). */
    uint16_t kdata;               /* KDATA register (written value). */
    int has_kdata;                /* KDATA was written. */
    uint16_t kadr;                /* KADR register. */
    uint16_t kcomm;               /* KCOMM register. */

    uint16_t disk;                /* Current disk. */

    int rec_no;                   /* Record number. */
    int restore;                  /* Restore operation. */
    int sync_word_written;        /* The sync word was written. */
    int bitclk_enable;            /* Disk bit counter enabled. */
    int wdinit;                   /* WDINIT bit used by task. */
    int seclate_enable;           /* To enable SECLATE. */

    int32_t intr_cycle;           /* Cycle of the next interrupt. */
    int32_t ds_intr_cycle;        /* Disk sector interrupt cycle. */
    int32_t dw_intr_cycle;        /* Disk word interrupt cycle. */
    int32_t seek_intr_cycle;      /* Seek interrupt cycle. */
    int32_t seclate_intr_cycle;   /* SECLATE interrupt cycle. */
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

/* Unloads the disk.
 * Returns TRUE on success.
 */
int disk_unload(struct disk *dsk, unsigned int drive_num);

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

/* Executes a F1_DSK_STROBE.
 * The `cycle` parameter specifies the current cycle.
 * Returns TRUE on success.
 */
int disk_func_strobe(struct disk *dsk, int32_t cycle);

/* Executes a F1_DSK_INCRECNO.
 * Returns TRUE on success.
 */
int disk_func_increcno(struct disk *dsk);

/* Executes a F1_DSK_CLRSTAT. */
void disk_func_clrstat(struct disk *dsk);

/* Executes a F2_DSK_INIT.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_func_init(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_RWC.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_func_rwc(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_RECNO.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_func_recno(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_XFRDAT.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_func_xfrdat(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_SWRNRDY.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_func_swrnrdy(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_NFER (not fatal error).
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_func_nfer(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_STROBON.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_func_strobon(struct disk *dsk, uint8_t task);

/* Processes a BLOCK instruction.
 * The task to be blocked is in the parameter `task`.
 */
void disk_block_task(struct disk *dsk, uint8_t task);

/* Processes the disk interrupts. */
void disk_interrupt(struct disk *dsk);

/* Callback for when the simulation switches to a disk task.
 * The new task is given by `task`.
 */
void disk_on_switch_task(struct disk *dsk, uint8_t task);

/* Prints the state of the registers.
 * The output is written to `output`.
 */
void disk_print_registers(struct disk *dsk,
                          struct string_buffer *output);

#endif /* __SIMULATOR_DISK_H */
