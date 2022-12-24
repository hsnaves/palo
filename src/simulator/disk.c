
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "simulator/disk.h"
#include "simulator/utils.h"
#include "microcode/microcode.h"
#include "common/utils.h"

/* Constants. */
#define MAX_SECTORS      (406 * 2 * 12)

/* TODO: Check. */
#define SEEK_DURATION              5882 /*     1 ms / 170 ns */
#define SECTOR_DURATION           19607 /* 3.333 ms / 170 ns */
#define WORD_DURATION                56 /*    10 us / 170 ns */
#define SECLATE_DURATION            505 /*    86 us / 170 ns */

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
#define KCOMM_SHIFT                  10
#define KCOMM_XFEROFF              0x10
#define KCOMM_WDINHB               0x08
#define KCOMM_BCLKSRC              0x04
#define KCOMM_WFFO                 0x02
#define KCOMM_SENDADR              0x01
#define KCOMM_MASK                 0x1F

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

/* The layout of the disk sector (in words). */
#define DS_HEADER                    44
#define DS_LABEL                     58
#define DS_DATA                      78
#define DS_END                      347

/* The possible word types in a sector. */
#define WT_GAP                        0
#define WT_DATA                       1
#define WT_SYNC                       2
#define WT_CHECKSUM                   3

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

        dd->head = 0;
        dd->cylinder = 0;
        dd->target_cylinder = 0;
        dd->sector = 0;
        dd->sector_word = 0;

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

int disk_unload(struct disk *dsk, unsigned int drive_num)
{
    struct disk_drive *dd;
    if (drive_num >= NUM_DISK_DRIVES) {
        report_error("disk: unload: invalid drive number %u",
                     drive_num);
        return FALSE;
    }

    dd = &dsk->drives[drive_num];
    dd->loaded = FALSE;
    return TRUE;
}

void disk_reset(struct disk *dsk)
{
    struct disk_drive *dd;
    int i;

    dsk->kstat = 0;
    dsk->kdata_read = 0;
    dsk->kdata = 0;
    dsk->has_kdata = FALSE;
    dsk->kadr = 0;
    dsk->kcomm = 0;

    dsk->disk = 0;

    for (i = 0; i < 2; i++) {
        dd = &dsk->drives[i];

        dd->head = 0;
        dd->cylinder = 0;
        dd->target_cylinder = 0;
        dd->sector = 0;
        dd->sector_word = 0;
    }

    dsk->rec_no = 0;
    dsk->restore = FALSE;
    dsk->sync_word_written = FALSE;
    dsk->bitclk_enable = FALSE;
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
    dsk->kcomm = (bus >> KCOMM_SHIFT) & KCOMM_MASK;

    if (dsk->kcomm & KCOMM_WDINHB) {
        dsk->wdinit = TRUE;
    }

    dsk->bitclk_enable = (dsk->kcomm & KCOMM_WFFO);

    /* TODO: Not sure why is this the case?
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
    struct disk_drive *dd;

    /* This causes the KADR register to be loaded from BUS[8-15].
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
     *
     * Indeed, the schematics reading does seem correct, so the hardware
     * itself does not implement the XOR between A[15] and C[15]. This is
     * done in software, in the microcode of the Disk sector task:
     * (KWDCT contains the disk command, MAR contains the address of
     *  DCB + 11)
     *
     *      T<- KWDCT;
     *      L<- ONE AND T;
     *      L<- -400 AND T, SH=0;
     *      T<-MD, SH=0, :INVERT;
     *
     *  ;    SH=0 MAPS INVERT TO NOINVERT
     *  INVERT:    L<-2 XOR T, TASK, :BADCOMM;
     *  NOINVERT:  L<-T, TASK, :BADCOMM;
     *
     */
    dsk->kadr = (bus & 0xFF);

    dsk->rec_no = 0;
    dsk->sync_word_written = FALSE;

    dd = &dsk->drives[dsk->disk];
    dd->head = (dsk->kdata >> AW_HEAD_SHIFT) & 1;

    /* No XORing with KADR[15] done here.
     * TODO: In the ContrAlto source, the disk is modified AFTER
     * the head, which is odd. We are copying this behavior,
     * but we think it might be incorrect.
     */
    dsk->disk = (dsk->kdata >> AW_DISK_SHIFT) & 1;

    if ((dsk->kdata >> AW_RESTORE_SHIFT) & 1) {
        dsk->restore = TRUE;
    }
}

int disk_func_strobe(struct disk *dsk, int32_t cycle)
{
    uint16_t cylinder;
    struct disk_drive *dd;

    if (!(dsk->kcomm & KCOMM_SENDADR)) {
        report_error("disk: func_strobe: "
                     "STROBE while SENDADR bit of KCOMM not 1");
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

    if (cylinder == dd->cylinder) {
        dsk->kstat &= ~(KSTAT_SEEKING | KSTAT_SEEK_FAIL);
        return TRUE;
    }

    dsk->kstat &= ~KSTAT_SEEK_FAIL;
    dsk->kstat |= KSTAT_SEEKING;

    dd->target_cylinder = cylinder;

    dsk->seek_intr_cycle = INTR_CYCLE(cycle + SEEK_DURATION);
    return TRUE;
}

int disk_func_increcno(struct disk *dsk)
{
    dsk->rec_no = (dsk->rec_no + 1) & 3;
    dsk->sync_word_written = FALSE;
    if (dsk->rec_no == 0) {
        report_error("disk: func_increcno: "
                     "rec_no overflow");
        return FALSE;
    }
    return TRUE;
}

void disk_func_clrstat(struct disk *dsk)
{
    /* Clears S[13], S[11], S[10], S[8],
     * that is, checksum error, data late, disk not ready
     * and seek failed errors.
     */
    dsk->kstat &= ~(KSTAT_CHECKSUM_ERROR | KSTAT_LATE
                    | KSTAT_NOT_READY | KSTAT_SEEK_FAIL);
}

uint16_t disk_func_init(struct disk *dsk, uint8_t task)
{
    if (task != TASK_DISK_WORD) return 0;
    return (dsk->wdinit) ? 0x1F : 0;
}

uint16_t disk_func_rwc(struct disk *dsk, uint8_t task)
{
    uint16_t next_extra;
    uint16_t oper;
    int shift;
    next_extra = disk_func_init(dsk, task);

    shift = KADR_HEADER_SHIFT - (KADR_SINGLE_SHIFT * (dsk->rec_no & 3));
    oper = (dsk->kadr >> shift) & KADR_BLOCK_MASK;

    switch (oper) {
    case 0: /* READ */
        break;
    case 1: /* CHECK */
        next_extra |= 2;
        break;
    case 2:
    case 3: /* WRITE */
        next_extra |= 3;
        break;
    }

    return next_extra;
}

uint16_t disk_func_recno(struct disk *dsk, uint8_t task)
{
    static const uint16_t RECNO_MAP[] = { 0, 2, 3, 1 };
    uint16_t next_extra;
    next_extra = disk_func_init(dsk, task);

    next_extra |= RECNO_MAP[dsk->rec_no & 3];
    return next_extra;
}

uint16_t disk_func_xfrdat(struct disk *dsk, uint8_t task)
{
    uint16_t next_extra;
    next_extra = disk_func_init(dsk, task);

    next_extra |= ((dsk->kadr & KADR_NO_XFER) == 0) ? 1 : 0;
    return next_extra;
}

uint16_t disk_func_swrnrdy(struct disk *dsk, uint8_t task)
{
    uint16_t next_extra;
    struct disk_drive *dd;

    next_extra = disk_func_init(dsk, task);

    dd = &dsk->drives[dsk->disk];
    if (!dd->loaded || (dsk->kstat & KSTAT_SEEKING)) {
        next_extra |= 1;
    }
    return next_extra;
}

uint16_t disk_func_nfer(struct disk *dsk, uint8_t task)
{
    uint16_t next_extra;
    struct disk_drive *dd;

    next_extra = disk_func_init(dsk, task);

    dd = &dsk->drives[dsk->disk];
    if (!dd->loaded || (dsk->kstat & KSTAT_SEEKING))
        return next_extra;

    if (dsk->kstat & (KSTAT_LATE | KSTAT_SEEK_FAIL | KSTAT_NOT_READY))
        return next_extra;

    next_extra |= 1;
    return next_extra;
}

uint16_t disk_func_strobon(struct disk *dsk, uint8_t task)
{
    uint16_t next_extra;
    next_extra = disk_func_init(dsk, task);

    next_extra |= (dsk->kstat & KSTAT_SEEKING) ? 1 : 0;
    return next_extra;
}

void disk_block_task(struct disk *dsk, uint8_t task)
{
    if (task == TASK_DISK_WORD) {
        dsk->wdinit = FALSE;
    }
    dsk->pending &= ~(1 << task);
}

/* Disk sector interrupt routine. */
static
void ds_interrupt(struct disk *dsk)
{
    struct disk_drive *dd;

    dd = &dsk->drives[dsk->disk];

    dd->sector = dd->sector + 1;
    if (dd->sector == 12) dd->sector = 0;

    dsk->kstat &= ~(AW_SECTOR_MASK << AW_SECTOR_SHIFT);
    dsk->kstat |= (dd->sector << AW_SECTOR_SHIFT);

    if (!dd->loaded) {
        dsk->kstat |= KSTAT_NOT_READY;
    } else {
        dsk->kstat &= ~KSTAT_NOT_READY;
    }

    dd->sector_word = 0;
    dsk->sync_word_written = FALSE;

    dsk->kdata_read = 0;


    if (!(dsk->kstat & KSTAT_SEEKING)) {
        dsk->pending |= (1 << TASK_DISK_SECTOR);

        dsk->seclate_enable = TRUE;
        dsk->kstat &= ~(KSTAT_LATE);

        dsk->dw_intr_cycle =
            INTR_CYCLE(dsk->intr_cycle + WORD_DURATION);

        dsk->ds_intr_cycle = -1;

        dsk->seclate_intr_cycle =
            INTR_CYCLE(dsk->intr_cycle + SECLATE_DURATION);
    } else {
        dsk->ds_intr_cycle =
            INTR_CYCLE(dsk->intr_cycle + SECTOR_DURATION);
    }
}

/* Computes the checksum of parts of the sector.
 * The array with the data is given by `data`. This array is of
 * length `len`.
 * Returns the checksum.
 */
static
uint16_t compute_checksum(const uint16_t *data, uint16_t len)
{
    uint16_t i, output;
    output = 0x0151;
    for (i = 0; i < len; i++) {
        output ^= data[i];
    }
    return output;
}

/* Obtains a word from the sector.
 * The parameter `ds` contains the sector data (header, label, and data).
 * The index of the word in the sector is given by `sector_word`.
 * The `word_type` parameter is to disambiguate between the different
 * types of words. The parameter `checksum` returns the checksum, when it
 * is a checksum word.
 * Returns a pointer to the word within the sector. If NULL, `word_type`
 * is one of WT_GAP, WT_SYNC, or WT_CHECKSUM
 */
static
uint16_t *get_sector_word(struct disk_sector *ds, uint16_t sector_word,
                          int *word_type, uint16_t *checksum)
{
    /* First gap. */
    if (sector_word < DS_HEADER) {
        *word_type = WT_GAP;
        return NULL;
    }

    /* The sync word. */
    if (sector_word <= DS_HEADER) {
        *word_type = WT_SYNC;
        return NULL;
    }

    /* Header part. */
    if (sector_word <= DS_HEADER + 2) {
        *word_type = WT_DATA;

        /* Data is in reverse. */
        return &ds->header[DS_HEADER + 2 - sector_word];
    }

    /* Checksum of header. */
    if (sector_word <= DS_HEADER + 3) {
        *word_type = WT_CHECKSUM;
        *checksum = compute_checksum(&ds->header[0], 2);
        return NULL;
    }

    /* Second gap. */
    if (sector_word < DS_LABEL) {
        *word_type = WT_GAP;
        return NULL;
    }

    /* The sync word. */
    if (sector_word <= DS_LABEL) {
        *word_type = WT_SYNC;
        return NULL;
    }

    /* Label part. */
    if (sector_word <= DS_LABEL + 8) {
        *word_type = WT_DATA;

        /* Data is in reverse. */
        return &ds->label[DS_LABEL + 8 - sector_word];
    }

    /* Checksum of label. */
    if (sector_word <= DS_LABEL + 9) {
        *word_type = WT_CHECKSUM;
        *checksum = compute_checksum(&ds->label[0], 8);
        return NULL;
    }

    /* Third gap. */
    if (sector_word < DS_DATA) {
        *word_type = WT_GAP;
        return NULL;
    }

    /* The sync word. */
    if (sector_word <= DS_DATA) {
        *word_type = WT_SYNC;
        return NULL;
    }

    /* Data part. */
    if (sector_word <= DS_DATA + 256) {
        *word_type = WT_DATA;

        /* Data is in reverse. */
        return &ds->data[DS_DATA + 256 - sector_word];
    }

    /* Checksum of label. */
    if (sector_word <= DS_DATA + 257) {
        *word_type = WT_CHECKSUM;
        *checksum = compute_checksum(&ds->data[0], 256);
        return NULL;
    }

    /* Last gap. */
    *word_type = WT_GAP;
    return NULL;
}

/* Disk word interrupt routine. */
static
void dw_interrupt(struct disk *dsk)
{
    struct disk_drive *dd;
    struct disk_sector *ds;
    uint16_t checksum;
    uint16_t vda, oper;
    uint16_t *w, wv;
    int shift;
    int bWakeup, seclate;
    int wdInhib, bClkSource;
    int wffo, xferOff;
    int is_write;
    int word_type;

    dd = &dsk->drives[dsk->disk];

    vda = dd->cylinder;
    vda *= dd->dg.num_heads;
    vda += dd->head;
    vda *= dd->dg.num_sectors;
    vda += dd->sector;

    ds = &dd->sectors[vda];
    w = get_sector_word(ds, dd->sector_word, &word_type, &checksum);
    switch (word_type) {
    case WT_DATA: wv = *w; break;
    case WT_SYNC: wv = 1; break;
    case WT_CHECKSUM: wv = checksum; break;
    case WT_GAP:
    default:
        wv = 0;
        break;
    }

    seclate = (dsk->kstat & KSTAT_LATE);
    wdInhib = (dsk->kcomm & KCOMM_WDINHB);
    bClkSource = (dsk->kcomm & KCOMM_BCLKSRC);
    wffo = (dsk->kcomm & KCOMM_WFFO);
    xferOff = (dsk->kcomm & KCOMM_XFEROFF);

    shift = KADR_HEADER_SHIFT - (KADR_SINGLE_SHIFT * (dsk->rec_no & 3));
    oper = (dsk->kadr >> shift) & KADR_BLOCK_MASK;
    is_write = (oper >= 2);

    bWakeup = (!seclate && !wdInhib && !bClkSource);

    if (!seclate && (wffo || dsk->bitclk_enable)) {
        if (!xferOff) {
            if (!is_write) {
                dsk->kdata_read = wv;
            } else {
                if (dsk->has_kdata) {
                    dsk->kdata_read = dsk->kdata;
                    dsk->has_kdata = FALSE;
                }

                if (dsk->sync_word_written) {
                    if (w) {
                        *w = dsk->kdata;
                    }
                }
            }
        }

        if (!wdInhib) {
            bWakeup = TRUE;
        }
    }

    if (!is_write && !wffo && wv == 1) {
        dsk->bitclk_enable = TRUE;
    } else if (is_write && wffo && (dsk->kdata == 1)
               && !dsk->sync_word_written) {

        dsk->sync_word_written = TRUE;

        /* Copies the cheat from ContrAlto. */
        switch (dsk->rec_no & 3) {
        case 0: dd->sector_word = DS_HEADER; break;
        case 1: dd->sector_word = DS_LABEL; break;
        case 2: dd->sector_word = DS_DATA; break;
        }

    }

    dd->sector_word++;

    if (bWakeup) {
        dsk->pending |= (1 << TASK_DISK_WORD);
    }

    if (dd->sector_word < DS_END) {
        dsk->dw_intr_cycle =
            INTR_CYCLE(dsk->intr_cycle + WORD_DURATION);
    } else {
        dsk->dw_intr_cycle = -1;

        dsk->ds_intr_cycle =
            INTR_CYCLE(dsk->intr_cycle + 1);
    }
}

/* Seek interrupt routine. */
static
void seek_interrupt(struct disk *dsk)
{
    struct disk_drive *dd;

    dd = &dsk->drives[dsk->disk];

    if (dd->cylinder < dd->target_cylinder) {
        dd->cylinder++;
    } else if (dd->cylinder > dd->target_cylinder) {
        dd->cylinder--;
    }

    if (dd->cylinder == dd->target_cylinder) {
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
    if (dsk->seclate_enable) {
        dsk->kstat |= KSTAT_LATE;
    }
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
    int has_ds, has_dw, has_seek, has_seclate;

    has_ds = (dsk->intr_cycle == dsk->ds_intr_cycle);
    has_dw = (dsk->intr_cycle == dsk->dw_intr_cycle);
    has_seek = (dsk->intr_cycle == dsk->seek_intr_cycle);
    has_seclate = (dsk->intr_cycle == dsk->seclate_intr_cycle);

    if (has_ds) ds_interrupt(dsk);
    if (has_dw) dw_interrupt(dsk);
    if (has_seek) seek_interrupt(dsk);
    if (has_seclate) seclate_interrupt(dsk);

    update_intr_cycle(dsk);
}

void disk_on_switch_task(struct disk *dsk, uint8_t task)
{
    /* According to the ContrAlto source:
     *   Deal with SECLATE semantics:  If the Disk Sector task wakes up
     *   and runs before the Disk Controller hits the SECLATE trigger time,
     *   then SECLATE remains false.
     *   Otherwise, when the trigger time is hit SECLATE is raised until
     *   the beginning of the next sector.
     */
    if (task == TASK_DISK_SECTOR) {
        dsk->seclate_enable = FALSE;
    }
}

void disk_print_registers(struct disk *dsk,
                          struct string_buffer *output)
{
    const struct disk_drive *dd;
    uint16_t valid, sector;

    string_buffer_print(output,
                        "DATA : %07o[%s]\n",
                        dsk->kdata_read,
                        dsk->has_kdata ? "*" : " ");

    valid = (KADR_VALID_VALUE << KADR_VALID_SHIFT);
    string_buffer_print(output,
                        "KSTAT: %07o    KDATA: %07o    "
                        "KADR : %07o    KCOMM: %07o\n",
                        disk_read_kstat(dsk),
                        dsk->kdata,
                        (dsk->kadr | valid),
                        (dsk->kcomm << KCOMM_SHIFT));

    string_buffer_print(output,
                        "SYNC : %-7o    BTCLK: %-7o    "
                        "WDINT: %-7o    LT_EN: %o\n",
                        dsk->sync_word_written ? 1 : 0,
                        dsk->bitclk_enable ? 1 : 0,
                        dsk->wdinit ? 1 : 0,
                        dsk->seclate_enable ? 1 : 0);

    string_buffer_print(output,
                        "RESTR: %-7o    DISK : %-7o    "
                        "RECNO: %o\n",
                        dsk->restore ? 1 : 0,
                        dsk->disk, dsk->rec_no);

    string_buffer_print(output,
                        "PEND : %07o    ICYC : %-10d "
                        "DSIC : %-10d DWIC : %-10d\n",
                        dsk->pending, dsk->intr_cycle,
                        dsk->ds_intr_cycle, dsk->dw_intr_cycle);

    string_buffer_print(output,
                        "SKIC : %-10d SLIC : %-10d\n",
                        dsk->seek_intr_cycle, dsk->seclate_intr_cycle);

    dd = (const struct disk_drive *) &dsk->drives[dsk->disk];
    string_buffer_print(output, "\n=======   Disk %u    =======\n", dsk->disk);

    string_buffer_print(output,
                        "CYL  : %07o    TCYL : %07o\n",
                        dd->cylinder, dd->target_cylinder);

    string_buffer_print(output,
                        "HEAD : %-7o    SECT : %07o    "
                        "WORD : %07o\n",
                        dd->head, dd->sector,
                        dd->sector_word);

    string_buffer_print(output,
                        "NHEAD: %07o    NSEC : %07o    "
                        "NCYL : %07o    LOAD : %o\n",
                        dd->dg.num_heads, dd->dg.num_sectors,
                        dd->dg.num_cylinders, dd->loaded ? 1 : 0);

    sector = (dsk->kstat >> KSTAT_SECTOR_SHIFT) & KSTAT_SECTOR_MASK;
    string_buffer_print(output, "\n======= KSTAT parts =======\n");
    string_buffer_print(output,
                        "  SECTOR: %03o   CHKSERR: %o  "
                        "COMPLETION: %03o  SEEK_FAIL: %o\n",
                        sector,
                        (dsk->kstat & KSTAT_CHECKSUM_ERROR) ? 1 : 0,
                        (dsk->kstat & KSTAT_COMPLETION_MASK),
                        (dsk->kstat & KSTAT_SEEK_FAIL) ? 1 : 0);

    string_buffer_print(output,
                        "  SEEK  : %o     NOTRDY : %o  "
                        "DATALATE  : %o    IDLE     : %o\n",
                        (dsk->kstat & KSTAT_SEEKING) ? 1 : 0,
                        (dsk->kstat & KSTAT_NOT_READY) ? 1 : 0,
                        (dsk->kstat & KSTAT_LATE) ? 1 : 0,
                        (dsk->kstat & KSTAT_IDLE) ? 1 : 0);

    string_buffer_print(output, "======= KADR parts  =======\n");
    string_buffer_print(output,
                        "  NXFER : %o     DISKMOD: %o  "
                        "HEADER_CMD: %o    LABEL_CMD: %o\n",
                        (dsk->kadr & KADR_NO_XFER) ? 1 : 0,
                        (dsk->kadr & KADR_DISK_MOD) ? 1 : 0,
                        (dsk->kadr >> KADR_HEADER_SHIFT) & KADR_BLOCK_MASK,
                        (dsk->kadr >> KADR_LABEL_SHIFT) & KADR_BLOCK_MASK);

    string_buffer_print(output,
                        "                            DATA_CMD  : %o\n",
                        (dsk->kadr >> KADR_DATA_SHIFT) & KADR_BLOCK_MASK);

    string_buffer_print(output, "======= KCOMM parts =======\n");
    string_buffer_print(output,
                        "  XROFF : %o     WDINHIB: %o  "
                        "BCLKSRC   : %o     SENDADR  : %o\n",
                        (dsk->kcomm & KCOMM_XFEROFF) ? 1 : 0,
                        (dsk->kcomm & KCOMM_WDINHB) ? 1 : 0,
                        (dsk->kcomm & KCOMM_BCLKSRC) ? 1 : 0,
                        (dsk->kadr >> KADR_DATA_SHIFT) & KADR_BLOCK_MASK);

    string_buffer_print(output,
                        "  WFFO  : %o",
                        (dsk->kcomm & KCOMM_WFFO) ? 1 : 0);
}
