
#ifndef __SIMULATOR_DISK_H
#define __SIMULATOR_DISK_H

/* Note: the schematics document 216389H_Disk_Control_May78.pdf , which
 * can be found at http://www.bitsavers.org/pdf/xerox/alto/schematics/
 * provide a lot of useful information regarding the behavior of the
 * the disk controller. Another major source of documentation is the
 * microcode itself, which can be found at:
 * http://www.bitsavers.org/pdf/xerox/alto/microcode/altoIIcode3.mu.txt
 * Lastly, sometimes we also refer to the Diablo drive maintenance manual:
 * 81503-03_Series_30_Disk_Drive_Maintenance_Jul78.pdf ,
 * and to the product description document:
 * 81502A_Series_30_Disk_Drives_Product_Description_Sep75.pdf
 * both of which can be found at:
 * https://bitsavers.org/pdf/diablo/disk/model_30
 */

#include <stdint.h>

#include "common/serdes.h"
#include "common/utils.h"

/* Constants. */
#define CONSTANT_SIZE                    256
#define NUM_DISK_DRIVES                    2
#define DS_HEADER_DSIZE                    4 /* Sync + header + checksum. */
#define DS_LABEL_DSIZE                    10 /* Sync + label + checksum. */
#define DS_DATA_DSIZE                    258 /* Sync + data + checksum. */

/* Data structures and types. */

/* Structure representing the disk geometry. */
struct disk_geometry {
    uint16_t num_cylinders;       /* Number of cylinders. */
    uint16_t num_heads;           /* Number of heads per cylinder. */
    uint16_t num_sectors;         /* Number of sectors per head. */
};

/* Structure representing a disk sector.
 * The words are stored in reverse to match the Diable disk format.
 */
struct disk_sector {
    uint16_t header[DS_HEADER_DSIZE];
    uint16_t label[DS_LABEL_DSIZE];
    uint16_t data[DS_DATA_DSIZE];
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
    struct disk_drive drives[NUM_DISK_DRIVES]; /* The two disk drives. */
    uint16_t kstat;               /* KSTAT register. */
    uint16_t kdata_read;          /* KDATA register (to read). */
    uint16_t kdata;               /* KDATA register (written value). */
    int has_kdata;                /* KDATA was written. */
    uint16_t kadr;                /* KADR register. */
    uint16_t kcomm;               /* KCOMM register. */

    uint16_t disk;                /* Current disk. */

    uint8_t rec_no;               /* Record number. */
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
 * This will initiate a seek operation to move the disk heads
 * to a specific cylinder (previously specified in KDATA register)
 * The `cycle` parameter specifies the current cycle.
 * Returns TRUE on success.
 */
int disk_func_strobe(struct disk *dsk, int32_t cycle);

/* Executes a F1_DSK_INCRECNO.
 * Increases the record number. Each sector has 3 data records:
 * 1 - header: 2 words with the address of the sector,
 * 2 - label: 8 words and contains metadata about the sector,
 * 3 - data: 256 words of data.
 * Returns TRUE on success.
 */
int disk_func_increcno(struct disk *dsk);

/* Executes a F1_DSK_CLRSTAT.
 * Clear the error bits in the KSTAT register.
 */
void disk_func_clrstat(struct disk *dsk);

/* Executes a F2_DSK_INIT.
 * Checks for WDINIT bit flag.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_func_init(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_RWC.
 * Checks type type of the current record operation:
 * R - READ, W - WRITE, C - CHECK.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_func_rwc(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_RECNO.
 * This will return the record number, but in a strange encoding, due
 * to the arrangement of the flip-flops on page 10 of DISK CONTROL
 * schematics. According to the schematics, the right sequence is:
 * 0 -> 2 -> 3 -> 1 (recall RECNO(0) is the high order bit,
 *                   per Alto convention).
 *
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_func_recno(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_XFRDAT.
 * Checks if it is transferring data.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_func_xfrdat(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_SWRNRDY.
 * SWRNRDY = Seek / Write / Read not ready.
 * According to the Diablo product description manual:
 *
 *   READY TO SEEK, READ, OR WRITE - (Ready to
 *   S/R/W). A 0 volt level (LO) on this line indicates that the
 *   disk drive is in the File Ready condition (see below) and it
 *   is not in the process of executing a seek operation.
 *
 *   FILE READY:
 *     1. Drive supplied with proper power.
 *     2. Loaded with a disk cartridge.
 *     3. LOAD/RUN switch in RUN position.
 *     4. Disk Start-up cycle is completed.
 *     5. Write Check flip/flop is reset.
 *
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_func_swrnrdy(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_NFER.
 * Checks if a fatal error did NOT happen.
 * The current task is in `task` parameter.
 * Returns the bits to be modified in the NEXT part of the following
 * microinstruction.
 */
uint16_t disk_func_nfer(struct disk *dsk, uint8_t task);

/* Executes a F2_DSK_STROBON.
 * Checks if the disk is seeking.
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

/* Serializes the disk object to `sd`. */
void disk_serialize(const struct disk *dsk, struct serdes *sd);

/* Deserializes the disk object from `sd`. */
void disk_deserialize(struct disk *dsk, struct serdes *sd);

#endif /* __SIMULATOR_DISK_H */
