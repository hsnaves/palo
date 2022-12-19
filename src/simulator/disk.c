
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "simulator/disk.h"
#include "simulator/utils.h"
#include "microcode/microcode.h"
#include "common/utils.h"

/* Constants. */
#define MAX_SECTORS      (406 * 2 * 12)

/* TODO: Change these times. */
#define SEEK_DURATION                10
#define SECTOR_DURATION              20
#define WORD_DURATION                20

/* The bits for address word. */
#define AW_SECTOR_SHIFT              12
#define AW_SECTOR_MASK           0x000F
#define AW_CYLINDER_SHIFT             3
#define AW_CYLINDER_MASK         0x01FF
#define AW_HEAD_SHIFT                 2
#define AW_DISK_SHIFT                 1
#define AW_RESTORE_SHIFT              0

/* The bits of KSTAT register. */
#define KSTAT_SECTOR_SHIFT           12
#define KSTAT_SECTOR_MASK        0x000F
#define KSTAT_ALWAYS_ONE         0x0F00
#define KSTAT_SEEK_FAIL          0x0080
#define KSTAT_SEEKING            0x0040
#define KSTAT_NOT_READY          0x0020
#define KSTAT_LATE               0x0010
#define KSTAT_IDLE               0x0008
#define KSTAT_CHECKSUM_ERROR     0x0004
#define KSTAT_COMPLETION_MASK    0x0003
#define KSTAT_GOOD_STATUS        0x0000
#define KSTAT_HW_ERROR           0x0001
#define KSTAT_CHECK_ERROR        0x0002
#define KSTAT_ILLEGAL_SECTOR     0x0003

/* The bits of KCOMM register. */
#define KCOMM_XFEROFF              0x10
#define KCOMM_WDINHB               0x08
#define KCOMM_BCLKSRC              0x04
#define KCOMM_WFFO                 0x02
#define KCOMM_SENDADR              0x01

/* The bits of KADR register. */
#define KADR_VALID_SHIFT              8
#define KADR_VALID_MASK          0x00FF
#define KADR_VALID_VALUE             72
#define KADR_HEADER_SHIFT             6
#define KADR_LABEL_SHIFT              4
#define KADR_DATA_SHIFT               2
#define KADR_SINGLE_SHIFT             2
#define KADR_BLOCK_MASK          0x0003
#define KADR_NO_XFER             0x0002
#define KADR_DISK_MOD            0x0001

/* Functions. */

void disk_initvar(struct disk *dsk)
{
    unsigned int dnum;

    for (dnum = 0; dnum < NUM_DISK_DRIVES; dnum++) {
        dsk->drives[dnum].sectors = NULL;
    }
}

void disk_destroy(struct disk *dsk)
{
    struct disk_drive *dd;
    unsigned int dnum;

    for (dnum = 0; dnum < NUM_DISK_DRIVES; dnum++) {
        dd = &dsk->drives[dnum];
        if (dd->sectors)
            free((void *) dd->sectors);
        dd->sectors = NULL;
    }
}

int disk_create(struct disk *dsk)
{
    struct disk_drive *dd;
    unsigned int dnum;

    disk_initvar(dsk);

    for (dnum = 0; dnum < NUM_DISK_DRIVES; dnum++) {
        dd = &dsk->drives[dnum];

        dd->size = MAX_SECTORS;
        dd->sectors = (struct disk_sector *)
            malloc(MAX_SECTORS * sizeof(struct disk_sector));

        if (unlikely(!dd->sectors)) {
            report_error("disk: create: memory exhausted");
            disk_destroy(dsk);
            return FALSE;
        }

        dd->dg.num_cylinders = 203;
        dd->dg.num_heads = 2;
        dd->dg.num_sectors = 12;
        dd->length = dd->dg.num_cylinders;
        dd->length *= dd->dg.num_heads;
        dd->length *= dd->dg.num_sectors;
        dd->loaded = FALSE;
    }

    disk_reset(dsk);
    return TRUE;
}

int disk_load_image(struct disk *dsk, unsigned int drive_num,
                    const char *filename)
{
    FILE *fp;
    struct disk_drive *dd;
    uint16_t *ds;
    uint16_t i, j, max_j;
    uint16_t w;
    int c;

    if (drive_num >= NUM_DISK_DRIVES) {
        report_error("disk: load_image: invalid drive number %u",
                     drive_num);
        return FALSE;
    }

    dd = &dsk->drives[drive_num];

    fp = fopen(filename, "rb");
    if (!fp) {
        report_error("disk: load_image: could not open `%s`",
                     filename);
        return FALSE;
    }

    max_j = sizeof(struct disk_sector) / sizeof(uint16_t);
    for (i = 0; i < dd->length; i++) {
        ds = &dd->sectors[i].header[0];

        c = fgetc(fp);
        if (c == EOF) goto error;

        c = fgetc(fp);
        if (c == EOF) goto error;

        /* Discard the first word and use the loop index instead. */

        for (j = 0; j < max_j; j++) {
            /* Process data in little endian format. */
            c = fgetc(fp);
            if (c == EOF) goto error;
            w = (uint16_t) (c & 0xFF);

            c = fgetc(fp);
            if (c == EOF) goto error;
            w |= (uint16_t) ((c & 0xFF) << 8);

            ds[j] = w;
        }
    }

    c = fgetc(fp);
    if (c != EOF) goto error;

    dd->loaded = TRUE;
    fclose(fp);
    return TRUE;

error:
    report_error("disk: load_image: premature end of file in `%s`",
                 filename);
    fclose(fp);
    return FALSE;
}

int disk_save_image(const struct disk *dsk, unsigned int drive_num,
                    const char *filename)
{
    FILE *fp;
    const struct disk_drive *dd;
    const uint16_t *ds;
    uint16_t i, j, max_j;
    uint16_t w;
    int c;

    if (drive_num >= NUM_DISK_DRIVES) {
        report_error("disk: save_image: invalid drive number %u",
                     drive_num);
        return FALSE;
    }

    dd = &dsk->drives[drive_num];

    fp = fopen(filename, "wb");
    if (!fp) {
        report_error("disk: save_image: could not open `%s`"
                     "for writing", filename);
        return FALSE;
    }

    max_j = sizeof(struct disk_sector) / sizeof(uint16_t);
    for (i = 0; i < dd->length; i++) {
        ds = (const uint16_t *) &dd->sectors[i].header[0];

        /* Discard the first word. */
        c = fputc((int) (i & 0xFF), fp);
        if (c == EOF) goto error;

        c = fputc((int) ((i >> 8) & 0xFF), fp);
        if (c == EOF) goto error;

        for (j = 0; j < max_j; j++) {
            w = ds[j];

            /* Process data in little endian format. */
            c = fputc((int) (w & 0xFF), fp);
            if (c == EOF) goto error;

            c = fputc((int) ((w >> 8) & 0xFF), fp);
            if (c == EOF) goto error;
        }
    }

    fclose(fp);
    return TRUE;

error:
    report_error("disk: save_image: error while writing `%s`",
                 filename);
    fclose(fp);
    return FALSE;
}

void disk_reset(struct disk *dsk)
{
    dsk->kstat = 0;
    dsk->kdata_read = 0;
    dsk->kdata = 0;
    dsk->has_kdata = FALSE;
    dsk->kadr = 0;
    dsk->kcomm = 0;

    dsk->disk = 0;
    dsk->head = 0;
    dsk->cylinder = 0;
    dsk->sector = 0;
    dsk->sector_word = 0;

    dsk->rec_no = 0;
    dsk->data_transfer = FALSE;
    dsk->restore = FALSE;
    dsk->sync_word_written = FALSE;
    dsk->wdinit = FALSE;

    dsk->intr_cycle = 1;
    dsk->ds_intr_cycle = 1;
    dsk->dw_intr_cycle = -1;
    dsk->seek_intr_cycle = -1;
    dsk->seclate_intr_cycle = -1;
    dsk->pending = 0;
}

uint16_t disk_read_kstat(struct disk *dsk)
{
    /* Format of KSTAT from the Alto HW reference manual.
     * KSTAT has the format of a disk status word S.
     * FIELD     VALUES              SIGNIFICANCE
     * S[0-3]    0-13B               Current sector number.
     * S[4-7]    17B                 One can tell whether status has been
     *                               stored by setting this field
     *                               initially to 0 and then checking for
     *                               non-zero.
     * S[8]      0-1                 1 means seek failed, possibly due to
     *                               illegal cylinder address.
     * S[9]      0-1                 1 means seek in progress.
     * S[10]     0-1                 1 means disk unit not ready.
     * S[11]     0-1                 1 means data or sector processing was
     *                               late during last sector. Data and
     *                               current sector number unreliable.
     * S[12]     0-1                 1 means disk interface was not
     *                               transferring data last sector.
     * S[13]     0-1                 1 mean checksum error. Command
     *                               allowed to proceed.
     * S[14-15]  0-3                 0 means command completed correctly.
     *                               1 means hardware error (see S[8-11])
     *                                 or sector overflow.
     *                               2 means check error. Command terminated
     *                                 instantly.
     *                               3 means disk command specified illegal
     *                                 sector.
     *
     * From ContrAlto source code:
     *   Bits 4-7 of KSTAT are always 1s (it's a shortcut allowing the disk
     *   microcode to write "-1" to bits 4-7 of the disk status word at 522
     *   without extra code).
     */
    return (KSTAT_ALWAYS_ONE | dsk->kstat);
}

void disk_load_kstat(struct disk *dsk, uint16_t bus)
{
    /* KSTAT[12-15] are loaded from BUS[12-15] (Actually BUS[13] is ORed
     * into KSTAT[13]). But from ContrAlto source code:
     *
     *   From the schematic (and ucode source, based on the values it
     *   actually uses for BUS[13]), BUS[13] is also inverted.  So there's
     *   that, too.
     */
    dsk->kstat &= ~(KSTAT_COMPLETION_MASK | KSTAT_IDLE);
    dsk->kstat |= (bus & (KSTAT_COMPLETION_MASK | KSTAT_IDLE));
    dsk->kstat |= ((~bus) & KSTAT_CHECKSUM_ERROR); /* invert BUS[13]. */
}

uint16_t disk_read_kdata(struct disk *dsk)
{
    /* Reads the value of kdata_read (not kdata). */
    return dsk->kdata_read;
}

void disk_load_kdata(struct disk *dsk, uint16_t bus)
{
    /* From the Alto HW reference manual:
     * The KDATA register is loaded from BUS[0-15]. This register is the
     * data output register to disk, and is also used to hold the disk
     * address during KADR<- and seek commands. When used as a disk address
     * it has the format of word A (as follows):
     * FIELD     RANGE               SIGNIFICANCE
     * A[0-3]    0-13B               Sector number.
     * A[4-12]   0-625B (Model 44)   Cylinder number.
     *           0-312B (Model 31)
     *
     * A[13]     0-1                 Head number.
     * A[14]     0-1                 Disk number (see also C[15]).
     *                               0 is removable pack on model 44.
     *                               1 is optional second Model 31 drive.
     * A[15]     0-1                 0 normally.
     *                               1 if cylinder 0 is to be addressed
     *                               via a hardware "restore" operation.
     */

    /* Not yet latch the value of kdata. */
    dsk->kdata = bus;
    dsk->has_kdata = TRUE;
}

void disk_load_kcomm(struct disk *dsk, uint16_t bus)
{
    /* Causes the KCOMM register to be loaded from BUS[1-5].
     * The KCOMM register has the following interpretation:
     * (1) XFEROFF = 1: inhibits data transmission to/from the disk.
     * (2) WDINHB = 1: prevents the disk word task from awakening.
     * (3) BCLKSRC = 1: takes bit clock from disk input or crystal clock,
     *                  as appropriate. BCLKSRC = 1 forces use of crystal
     *                  clock.
     * (4) WFFO = 0: holds the disk bit counter at -1 until a 1-bit is
     *               read. WFFO = 1 allows the bit counter to proceed
     *               normally.
     * (5) SENDADR = 1: causes KDATA[4-12] and KDATA[15] to be transmitted
     *                  to disk unit as track address. SENDADR = 0 inhibits
     *                  such transmission.
     */
    dsk->kcomm = (bus >> 10) & 0x1F;

    if (dsk->kcomm & KCOMM_WDINHB) {
        dsk->wdinit = TRUE;
    }

    /* TODO: Not sure why is thisthe case?
     * This was copied from the ContrAlto source code.
     */
    if (dsk->kcomm & KCOMM_SENDADR) {
        if (((dsk->kdata >> AW_DISK_SHIFT) & 1) != 0) {
            dsk->kstat &= ~KSTAT_SEEKING;
        }
    }
}

void disk_load_kadr(struct disk *dsk, uint16_t bus)
{
    /* This causes the KADR register to be loaded from BUS[8-14].
     * This register has the format of word C (as follows). In addition,
     * it causes the head address bit to be loaded from KDATA[13].
     * FIELD     RANGE               SIGNIFICANCE
     * C[0-7]    110B                Checked to verify this is a valid disk
     *                               command.
     * C[8-9]    0-3                 0 if header block to be read.
     *                               1 if header block to be checked.
     *                               2 or 3 if header block to be written.
     * C[10-11]  0-3                 0 if label block to be read.
     *                               1 if label block to be checked.
     *                               2 or 3 if label block to be written.
     * C[12-13]  0-3                 0 if data block to be read.
     *                               1 if data block to be checked.
     *                               2 or 3 if data block to be written.
     * C[14]     0-1                 0 normally.
     *                               1 if the command is to terminate
     *                               immediately after the correct cylinder
     *                               position is reached (before any data is
     *                               transferred). [NO XFER]
     * C[15]     0-1                 XORed with A[14] to yield hardware disk
     *                               number.
     *
     * Note from the ContrAlto source:
     *   The HW reference claims that the drive is selected by bit 14 of
     *   KDATA XOR'd with bit 15 of KADR but I can find no evidence in the
     *   schematics that this is actually so. Page 18 of the controller
     *   schematic ("DISK ADDRESSING") shows that the current DATA(14)
     *   (KDATA bit 14) value is gated into the DISK select lines (running
     *   to the drive) whenever a KADR<- F1 is executed. It is possible that
     *   the HW ref is telling the truth but the XORing is done by the Sector
     *   Task uCode and not the hardware, but where this is actually
     *   occurring is not obvious. At any rate: The below behavior appears to
     *   work correctly, so I'm sticking with it.
     */
    dsk->kadr = (bus & 0xFF);

    dsk->rec_no = 0;
    dsk->sync_word_written = FALSE;
    dsk->head = (dsk->kdata >> AW_HEAD_SHIFT) & 1;

    /* No XORing with KADR[15] done here. */
    dsk->disk = (dsk->kdata >> AW_DISK_SHIFT) & 1;

    dsk->data_transfer = ((dsk->kadr & KADR_NO_XFER) == 0);

    if ((dsk->kdata >> AW_RESTORE_SHIFT) & 1) {
        dsk->restore = TRUE;
    }
}

int disk_strobe(struct disk *dsk, int32_t cycle)
{
    uint16_t cylinder;
    struct disk_drive *dd;

    if (!(dsk->kcomm & KCOMM_SENDADR)) {
        return FALSE;
    }

    cylinder = (dsk->kdata >> AW_CYLINDER_SHIFT) & AW_CYLINDER_MASK;
    if (dsk->restore) cylinder = 0;

    dd = &dsk->drives[dsk->disk];
    if ((!dd->loaded) || (cylinder >= dd->dg.num_cylinders)) {
        dsk->kstat &= ~KSTAT_SEEKING;
        dsk->kstat |= KSTAT_SEEK_FAIL;
        return TRUE;
    }

    if (cylinder == dsk->cylinder) {
        dsk->kstat &= ~(KSTAT_SEEKING | KSTAT_SEEK_FAIL);
        return TRUE;
    }

    dsk->kstat &= ~KSTAT_SEEK_FAIL;
    dsk->kstat |= KSTAT_SEEKING;

    dsk->target_cylinder = cylinder;

    dsk->seek_intr_cycle = INTR_CYCLE(cycle + SEEK_DURATION);
    return TRUE;
}

int disk_increcno(struct disk *dsk)
{
    dsk->rec_no++;
    dsk->sync_word_written = FALSE;
    return (dsk->rec_no <= 3);
}

void disk_clrstat(struct disk *dsk)
{
    /* Clears S[13], S[11], S[10], S[8],
     * that is, checksum error, seclate, disk not ready
     * and seek failed errors.
     */
    dsk->kstat &= ~(KSTAT_CHECKSUM_ERROR | KSTAT_LATE
                    | KSTAT_NOT_READY | KSTAT_SEEK_FAIL);
}

uint16_t disk_init(struct disk *dsk, uint8_t task)
{
    if (task != TASK_DISK_WORD) return 0;
    return (dsk->wdinit) ? 0x1F : 0;
}

uint16_t disk_rwc(struct disk *dsk, uint8_t task)
{
    uint16_t next_extra;
    next_extra = disk_init(dsk, task);

    /* TODO: Implement this. */
    return next_extra;
}

uint16_t disk_recno(struct disk *dsk, uint8_t task)
{
    static const uint16_t RECNO_MAP[] = { 0, 2, 3, 1 };
    uint16_t next_extra;
    next_extra = disk_init(dsk, task);

    next_extra |= RECNO_MAP[dsk->rec_no];
    return next_extra;
}

uint16_t disk_xfrdat(struct disk *dsk, uint8_t task)
{
    uint16_t next_extra;
    next_extra = disk_init(dsk, task);

    next_extra |= (dsk->data_transfer) ? 1 : 0;
    return next_extra;
}

uint16_t disk_swrnrdy(struct disk *dsk, uint8_t task)
{
    uint16_t next_extra;
    struct disk_drive *dd;

    next_extra = disk_init(dsk, task);

    dd = &dsk->drives[dsk->disk];
    if (dd->loaded && !(dsk->kstat & KSTAT_SEEKING)) {
        next_extra |= 1;
    }
    return next_extra;
}

uint16_t disk_nfer(struct disk *dsk, uint8_t task)
{
    uint16_t next_extra;
    struct disk_drive *dd;

    next_extra = disk_init(dsk, task);

    dd = &dsk->drives[dsk->disk];
    if (!dd->loaded || (dsk->kstat & KSTAT_SEEKING))
        return next_extra;

    if (dsk->kstat & (KSTAT_LATE | KSTAT_SEEK_FAIL | KSTAT_NOT_READY))
        return next_extra;

    next_extra |= 1;
    return next_extra;
}

uint16_t disk_strobon(struct disk *dsk, uint8_t task)
{
    uint16_t next_extra;
    next_extra = disk_init(dsk, task);

    next_extra |= (dsk->kstat & KSTAT_SEEKING) ? 1 : 0;
    return next_extra;
}

void disk_block_task(struct disk *dsk, uint8_t task)
{
    /* TODO: Implement this. */
    dsk->pending &= ~(1 << task);
}

/* Disk sector interrupt routine. */
static
void ds_interrupt(struct disk *dsk)
{
    struct disk_drive *dd;

    dsk->sector = dsk->sector + 1;
    if (dsk->sector == 12) dsk->sector = 0;

    dsk->kstat &= ~(AW_SECTOR_MASK << AW_SECTOR_SHIFT);
    dsk->kstat |= (dsk->sector << AW_SECTOR_SHIFT);

    dd = &dsk->drives[dsk->disk];
    if (!dd->loaded) {
        dsk->kstat |= KSTAT_NOT_READY;
    } else {
        dsk->kstat &= ~KSTAT_NOT_READY;
    }

    dsk->sector_word = 0;
    dsk->sync_word_written = FALSE;

    dsk->kdata_read = 0;

    if (dsk->kstat & KSTAT_SEEKING) {
        dsk->pending |= (1 << TASK_DISK_SECTOR);

        /* TODO: Program the seclate here. */

        dsk->dw_intr_cycle =
            INTR_CYCLE(dsk->intr_cycle + WORD_DURATION);

        dsk->ds_intr_cycle = -1;
    } else {
        dsk->ds_intr_cycle =
            INTR_CYCLE(dsk->intr_cycle + SECTOR_DURATION);
    }
}

/* Disk word interrupt routine. */
static
void dw_interrupt(struct disk *dsk)
{
    /* TODO: Implement this. */
    dsk->dw_intr_cycle = -1;
}

/* Seek interrupt routine. */
static
void seek_interrupt(struct disk *dsk)
{
    if (dsk->cylinder < dsk->target_cylinder) {
        dsk->cylinder++;
    } else if (dsk->cylinder > dsk->target_cylinder) {
        dsk->cylinder--;
    }

    if (dsk->cylinder == dsk->target_cylinder) {
        dsk->kstat &= ~KSTAT_SEEKING;
        dsk->restore = FALSE;
        dsk->seek_intr_cycle = -1;
    } else {
        dsk->seek_intr_cycle =
            INTR_CYCLE(dsk->intr_cycle + SEEK_DURATION);
    }
}

/* SECLATE interrupt routine. */
static
void seclate_interrupt(struct disk *dsk)
{
    /* TODO: Implement this. */
    dsk->seclate_intr_cycle = -1;
}

/* Updates the intr_cycle. */
static
void update_intr_cycle(struct disk *dsk)
{
    int32_t intr_cycles[4];

    intr_cycles[0] = dsk->ds_intr_cycle;
    intr_cycles[1] = dsk->dw_intr_cycle;
    intr_cycles[2] = dsk->seek_intr_cycle;
    intr_cycles[3] = dsk->seclate_intr_cycle;

    dsk->intr_cycle = compute_intr_cycle(dsk->intr_cycle,
                                         4, intr_cycles);
}

void disk_interrupt(struct disk *dsk)
{
    if (dsk->intr_cycle == dsk->ds_intr_cycle)
        ds_interrupt(dsk);

    if (dsk->intr_cycle == dsk->dw_intr_cycle)
        dw_interrupt(dsk);

    if (dsk->intr_cycle == dsk->seek_intr_cycle)
        seek_interrupt(dsk);

    if (dsk->intr_cycle == dsk->seclate_intr_cycle)
        seclate_interrupt(dsk);

    update_intr_cycle(dsk);
}
