
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "simulator/disk.h"
#include "simulator/intr.h"
#include "microcode/microcode.h"
#include "common/serdes.h"
#include "common/string_buffer.h"
#include "common/utils.h"

/* Constants. */
#define MAX_SECTORS           (406 * 2 * 12)

/* TODO: Check. */
#define SEEK_DURATION                   5882 /*     1 ms / 170 ns */
#define SECTOR_DURATION                19607 /* 3.333 ms / 170 ns */
#define WORD_DURATION                     56 /*    10 us / 170 ns */
#define SECLATE_DURATION                 505 /*    86 us / 170 ns */

/* The bits for address word. */
#define AW_SECTOR_SHIFT                   12
#define AW_SECTOR_MASK                0x000F
#define AW_CYLINDER_SHIFT                  3
#define AW_CYLINDER_MASK              0x01FF
#define AW_HEAD_SHIFT                      2
#define AW_DISK_SHIFT                      1
#define AW_RESTORE_SHIFT                   0

/* The bits of KSTAT register. */
#define KSTAT_SECTOR_SHIFT                12
#define KSTAT_SECTOR_MASK             0x000F
#define KSTAT_ALWAYS_ONE              0x0F00
#define KSTAT_SEEK_FAIL               0x0080
#define KSTAT_SEEKING                 0x0040
#define KSTAT_NOT_READY               0x0020
#define KSTAT_LATE                    0x0010
#define KSTAT_IDLE                    0x0008
#define KSTAT_CHECKSUM_ERROR          0x0004
#define KSTAT_COMPLETION_MASK         0x0003
#define KSTAT_GOOD_STATUS             0x0000
#define KSTAT_HW_ERROR                0x0001
#define KSTAT_CHECK_ERROR             0x0002
#define KSTAT_ILLEGAL_SECTOR          0x0003

/* The bits of KCOMM register. */
#define KCOMM_SHIFT                       10
#define KCOMM_XFEROFF                   0x10
#define KCOMM_WDINHB                    0x08
#define KCOMM_BCLKSRC                   0x04
#define KCOMM_WFFO                      0x02
#define KCOMM_SENDADR                   0x01
#define KCOMM_MASK                      0x1F

/* The bits of KADR register. */
#define KADR_VALID_SHIFT                   8
#define KADR_VALID_MASK               0x00FF
#define KADR_VALID_VALUE                  72
#define KADR_HEADER_SHIFT                  6
#define KADR_LABEL_SHIFT                   4
#define KADR_DATA_SHIFT                    2
#define KADR_SINGLE_SHIFT                  2
#define KADR_BLOCK_MASK               0x0003
#define KADR_NO_XFER                  0x0002
#define KADR_DISK_MOD                 0x0001

/* The layout of the disk sector (in words). */
#define DS_HEADER                         44
#define DS_LABEL                          58
#define DS_DATA                           78
#define DS_END                           347

/* The possible word types in a sector. */
#define WT_GAP                             0
#define WT_DATA                            1
#define WT_SYNC                            2

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

int disk_load_image(struct disk *dsk, unsigned int drive_num,
                    const char *filename)
{
    struct disk_drive *dd;
    struct disk_sector *ds;
    uint16_t *wptrs[3];
    uint16_t max_js[3];
    uint16_t *wptr;
    uint16_t i, j, max_j, k;
    uint16_t w;
    FILE *fp;
    int c;

    if (unlikely(drive_num >= NUM_DISK_DRIVES)) {
        report_error("disk: load_image: invalid drive number %u",
                     drive_num);
        return FALSE;
    }

    dd = &dsk->drives[drive_num];

    fp = fopen(filename, "rb");
    if (unlikely(!fp)) {
        report_error("disk: load_image: could not open `%s`",
                     filename);
        return FALSE;
    }

    max_js[0] = (uint16_t) DS_HEADER_DSIZE;
    max_js[1] = (uint16_t) DS_LABEL_DSIZE;
    max_js[2] = (uint16_t) DS_DATA_DSIZE;
    for (i = 0; i < dd->length; i++) {
        /* Discard the first word. */
        c = fgetc(fp);
        if (c == EOF) goto error;

        c = fgetc(fp);
        if (c == EOF) goto error;

        ds = &dd->sectors[i];
        wptrs[0] = &ds->header[0];
        wptrs[1] = &ds->label[0];
        wptrs[2] = &ds->data[0];

        for (k = 0; k < 3; k++) {
            wptr = wptrs[k];
            max_j = max_js[k];
            /* Read in reverse order to match the Diablo disk format. */
            for (j = max_j - 1; j-- > 1;) {
                /* Process data in little-endian format. */
                c = fgetc(fp);
                if (c == EOF) goto error;
                w = (uint16_t) (c & 0xFF);

                c = fgetc(fp);
                if (c == EOF) goto error;
                w |= (uint16_t) ((c & 0xFF) << 8);

                wptr[j] = w;
            }
            wptr[0] = (uint16_t) 1; /* sync word. */
            wptr[max_j - 1] = compute_checksum(&wptr[1], max_j - 2);
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
    const struct disk_drive *dd;
    const struct disk_sector *ds;
    const uint16_t *wptrs[3];
    uint16_t max_js[3];
    const uint16_t *wptr;
    uint16_t i, j, max_j, k;
    uint16_t w;
    FILE *fp;
    int c;

    if (unlikely(drive_num >= NUM_DISK_DRIVES)) {
        report_error("disk: save_image: invalid drive number %u",
                     drive_num);
        return FALSE;
    }

    dd = &dsk->drives[drive_num];

    fp = fopen(filename, "wb");
    if (unlikely(!fp)) {
        report_error("disk: save_image: could not open `%s`"
                     "for writing", filename);
        return FALSE;
    }

    max_js[0] = (uint16_t) DS_HEADER_DSIZE;
    max_js[1] = (uint16_t) DS_LABEL_DSIZE;
    max_js[2] = (uint16_t) DS_DATA_DSIZE;
    for (i = 0; i < dd->length; i++) {
        /* Write the index as the first word. */
        c = fputc((int) (i & 0xFF), fp);
        if (c == EOF) goto error;

        c = fputc((int) ((i >> 8) & 0xFF), fp);
        if (c == EOF) goto error;

        ds = &dd->sectors[i];
        wptrs[0] = &ds->header[0];
        wptrs[1] = &ds->label[0];
        wptrs[2] = &ds->data[0];

        for (k = 0; k < 3; k++) {
            wptr = wptrs[k];
            max_j = max_js[k];
            /* Write in reverse order to match the Diablo disk format. */
            for (j = max_j - 1; j-- > 1;) {
                w = wptr[j];

                /* Process data in little-endian format. */
                c = fputc((int) (w & 0xFF), fp);
                if (c == EOF) goto error;

                c = fputc((int) ((w >> 8) & 0xFF), fp);
                if (c == EOF) goto error;
            }

            w = compute_checksum(&wptr[1], max_j - 2);
            if (wptr[max_j - 1] != w) {
                report_error("disk: save_image: "
                             "invalid checksum on sector %u", i);
            }
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

uint16_t disk_read_kstat(const struct disk *dsk)
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

uint16_t disk_read_kdata(const struct disk *dsk)
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

uint16_t disk_func_init(const struct disk *dsk, uint8_t task)
{
    if (task != TASK_DISK_WORD) return 0;
    return (dsk->wdinit) ? 0x1F : 0;
}

uint16_t disk_func_rwc(const struct disk *dsk, uint8_t task)
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

uint16_t disk_func_recno(const struct disk *dsk, uint8_t task)
{
    static const uint16_t RECNO_MAP[] = { 0, 2, 3, 1 };
    uint16_t next_extra;
    next_extra = disk_func_init(dsk, task);

    next_extra |= RECNO_MAP[dsk->rec_no & 3];
    return next_extra;
}

uint16_t disk_func_xfrdat(const struct disk *dsk, uint8_t task)
{
    uint16_t next_extra;
    next_extra = disk_func_init(dsk, task);

    next_extra |= ((dsk->kadr & KADR_NO_XFER) == 0) ? 1 : 0;
    return next_extra;
}

uint16_t disk_func_swrnrdy(const struct disk *dsk, uint8_t task)
{
    uint16_t next_extra;
    const struct disk_drive *dd;

    next_extra = disk_func_init(dsk, task);

    dd = &dsk->drives[dsk->disk];
    if (!dd->loaded || (dsk->kstat & KSTAT_SEEKING)) {
        next_extra |= 1;
    }
    return next_extra;
}

uint16_t disk_func_nfer(const struct disk *dsk, uint8_t task)
{
    uint16_t next_extra;
    const struct disk_drive *dd;

    next_extra = disk_func_init(dsk, task);

    dd = &dsk->drives[dsk->disk];
    if (!dd->loaded || (dsk->kstat & KSTAT_SEEKING))
        return next_extra;

    if (dsk->kstat & (KSTAT_LATE | KSTAT_SEEK_FAIL | KSTAT_NOT_READY))
        return next_extra;

    next_extra |= 1;
    return next_extra;
}

uint16_t disk_func_strobon(const struct disk *dsk, uint8_t task)
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

/* Obtains a word from the sector.
 * The parameter `ds` contains the sector data (header, label, and data).
 * The index of the word in the sector is given by `sector_word`.
 * The `word_type` parameter is to disambiguate between the different
 * types of words.
 * Returns a pointer to the word within the sector. If NULL, `word_type`
 * must be WT_GAP.
 */
static
uint16_t *get_sector_word(struct disk_sector *ds,
                          uint16_t sector_word, int *word_type)
{
    uint16_t i;

    /* First gap. */
    if (sector_word < DS_HEADER) {
        *word_type = WT_GAP;
        return NULL;
    }

    /* Header part. */
    if (sector_word < DS_HEADER + DS_HEADER_DSIZE) {
        i = sector_word - DS_HEADER;
        if (i == 0)
            *word_type = WT_SYNC;
        else
            *word_type = WT_DATA;
        return &ds->header[i];
    }

    /* Second gap. */
    if (sector_word < DS_LABEL) {
        *word_type = WT_GAP;
        return NULL;
    }

    /* Label part. */
    if (sector_word < DS_LABEL + DS_LABEL_DSIZE) {
        i = sector_word - DS_LABEL;
        if (i == 0)
            *word_type = WT_SYNC;
        else
            *word_type = WT_DATA;
        return &ds->label[i];
    }

    /* Third gap. */
    if (sector_word < DS_DATA) {
        *word_type = WT_GAP;
        return NULL;
    }

    /* Data part. */
    if (sector_word < DS_DATA + DS_DATA_DSIZE) {
        i = sector_word - DS_DATA;
        if (i == 0)
            *word_type = WT_SYNC;
        else
            *word_type = WT_DATA;
        return &ds->data[i];
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
    w = get_sector_word(ds, dd->sector_word, &word_type);
    wv = (word_type == WT_GAP) ? 0 : w[0];

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

/* Updates the intr_cycle.
 * Returns TRUE on success.
 */
static
int update_intr_cycle(struct disk *dsk)
{
    int32_t intr_cycles[4];

    intr_cycles[0] = dsk->ds_intr_cycle;
    intr_cycles[1] = dsk->dw_intr_cycle;
    intr_cycles[2] = dsk->seek_intr_cycle;
    intr_cycles[3] = dsk->seclate_intr_cycle;

    if (unlikely(!compute_intr_cycle(dsk->intr_cycle, TRUE,
                                     4, intr_cycles,
                                     &dsk->intr_cycle))) {
        report_error("disk: update_intr_cycle: "
                     "error in computing interrupt cycle");
        return FALSE;
    }

    return TRUE;
}

int disk_interrupt(struct disk *dsk)
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

    return update_intr_cycle(dsk);
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

void disk_print_registers(const struct disk *dsk,
                          struct decoder *dec)
{
    struct string_buffer *output;
    const struct disk_drive *dd;
    char buffer[16];
    uint16_t valid, sector;

    output = dec->output;
    sprintf(buffer, "DATAREAD%s", dsk->has_kdata ? "*" : "");
    decode_tagged_value(dec->vdec, buffer, DECODE_VALUE, dsk->kdata_read);
    decode_tagged_value(dec->vdec, "KSTAT",
                        DECODE_VALUE, disk_read_kstat(dsk));
    decode_tagged_value(dec->vdec, "KDATA", DECODE_VALUE, dsk->kdata);
    valid = (KADR_VALID_VALUE << KADR_VALID_SHIFT);
    decode_tagged_value(dec->vdec, "KADR",
                        DECODE_VALUE, (dsk->kadr | valid));
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec, "KCOMM",
                        DECODE_VALUE, (dsk->kcomm << KCOMM_SHIFT));
    decode_tagged_value(dec->vdec, "DISK", DECODE_VALUE, dsk->disk);
    decode_tagged_value(dec->vdec, "RECNO", DECODE_VALUE, dsk->rec_no);
    decode_tagged_value(dec->vdec, "RESTORE", DECODE_BOOL, dsk->restore);
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec, "SYNC",
                        DECODE_BOOL, dsk->sync_word_written);
    decode_tagged_value(dec->vdec, "BITCLK",
                        DECODE_BOOL, dsk->bitclk_enable);
    decode_tagged_value(dec->vdec, "WDINIT",
                        DECODE_BOOL, dsk->wdinit);
    decode_tagged_value(dec->vdec, "SECL_EN",
                        DECODE_BOOL, dsk->seclate_enable);
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec, "XFEROFF",
                        DECODE_BOOL, (dsk->kcomm & KCOMM_XFEROFF));
    decode_tagged_value(dec->vdec, "WDINHIB",
                        DECODE_BOOL, (dsk->kcomm & KCOMM_WDINHB));
    decode_tagged_value(dec->vdec, "BCLKSRC",
                        DECODE_BOOL, (dsk->kcomm & KCOMM_BCLKSRC));
    decode_tagged_value(dec->vdec, "WFFO",
                        DECODE_BOOL, (dsk->kcomm & KCOMM_WFFO));
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec, "SENDADR",
                        DECODE_BOOL, (dsk->kcomm & KCOMM_SENDADR));
    sector = (dsk->kstat >> KSTAT_SECTOR_SHIFT) & KSTAT_SECTOR_MASK;
    decode_tagged_value(dec->vdec, "ST_SECTOR",
                        DECODE_VALUE, sector);
    decode_tagged_value(dec->vdec, "CHKSERR",
                        DECODE_BOOL, (dsk->kstat & KSTAT_CHECKSUM_ERROR));
    decode_tagged_value(dec->vdec, "SEEK_FAIL",
                        DECODE_BOOL, (dsk->kstat & KSTAT_SEEK_FAIL));
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec, "SEEKING",
                        DECODE_BOOL, (dsk->kstat & KSTAT_SEEKING));
    decode_tagged_value(dec->vdec, "NOT_READY",
                        DECODE_BOOL, (dsk->kstat & KSTAT_NOT_READY));
    decode_tagged_value(dec->vdec, "DATALATE",
                        DECODE_BOOL, (dsk->kstat & KSTAT_LATE));
    decode_tagged_value(dec->vdec, "IDLE",
                        DECODE_BOOL, (dsk->kstat & KSTAT_IDLE));
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec,  "COMPLTION",
                        DECODE_VALUE, (dsk->kstat & KSTAT_COMPLETION_MASK));
    decode_tagged_value(dec->vdec,  "NO_XFER",
                        DECODE_BOOL, (dsk->kadr & KADR_NO_XFER));
    decode_tagged_value(dec->vdec,  "DISK_MOD",
                        DECODE_BOOL, (dsk->kadr & KADR_DISK_MOD));
    decode_tagged_value(dec->vdec,  "HEADERCMD", DECODE_VALUE,
                        (dsk->kadr >> KADR_HEADER_SHIFT) & KADR_BLOCK_MASK);
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec,  "LABELCMD", DECODE_VALUE,
                        (dsk->kadr >> KADR_LABEL_SHIFT) & KADR_BLOCK_MASK);
    decode_tagged_value(dec->vdec,  "DATACMD", DECODE_VALUE,
                        (dsk->kadr >> KADR_DATA_SHIFT) & KADR_BLOCK_MASK);
    dd = (const struct disk_drive *) &dsk->drives[dsk->disk];
    decode_tagged_value(dec->vdec,  "CYLIN",
                        DECODE_VALUE, dd->cylinder);
    decode_tagged_value(dec->vdec,  "T_CYLIN",
                        DECODE_VALUE, dd->target_cylinder);
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec,  "LOADED",
                        DECODE_BOOL, dd->loaded);
    decode_tagged_value(dec->vdec,  "HEAD",
                        DECODE_VALUE, dd->head);
    decode_tagged_value(dec->vdec,  "SECTOR",
                        DECODE_VALUE, dd->sector);
    decode_tagged_value(dec->vdec,  "SEC_WORD",
                        DECODE_VALUE, dd->sector);
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec,  "N_HEAD",
                        DECODE_VALUE, dd->dg.num_heads);
    decode_tagged_value(dec->vdec,  "N_SECTOR",
                        DECODE_VALUE, dd->dg.num_sectors);
    decode_tagged_value(dec->vdec,  "N_CYLIN",
                        DECODE_VALUE, dd->dg.num_cylinders);
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec, "PEND", DECODE_VALUE, dsk->pending);
    decode_tagged_value(dec->vdec, "ICYC",
                        DECODE_SVALUE32, dsk->intr_cycle);
    decode_tagged_value(dec->vdec, "DS_ICYC",
                        DECODE_SVALUE32, dsk->ds_intr_cycle);
    decode_tagged_value(dec->vdec, "DW_ICYC",
                        DECODE_SVALUE32, dsk->dw_intr_cycle);
    string_buffer_print(output, "\n");

    decode_tagged_value(dec->vdec, "SK_ICYC",
                        DECODE_SVALUE32, dsk->seek_intr_cycle);
    decode_tagged_value(dec->vdec, "SL_ICYC",
                        DECODE_SVALUE32, dsk->seclate_intr_cycle);
    string_buffer_print(output, "\n");
}

void disk_serialize(const struct disk *dsk, struct serdes *sd)
{
    unsigned int drive_num;
    const struct disk_drive *dd;

    serdes_put16(sd, dsk->kstat);
    serdes_put16(sd, dsk->kdata_read);
    serdes_put16(sd, dsk->kdata);
    serdes_put_bool(sd, dsk->has_kdata);
    serdes_put16(sd, dsk->kadr);
    serdes_put16(sd, dsk->kcomm);
    serdes_put16(sd, dsk->disk);
    serdes_put8(sd, dsk->rec_no);
    serdes_put_bool(sd, dsk->restore);
    serdes_put_bool(sd, dsk->sync_word_written);
    serdes_put_bool(sd, dsk->bitclk_enable);
    serdes_put_bool(sd, dsk->wdinit);
    serdes_put_bool(sd, dsk->seclate_enable);
    serdes_put32(sd, dsk->intr_cycle);
    serdes_put32(sd, dsk->ds_intr_cycle);
    serdes_put32(sd, dsk->dw_intr_cycle);
    serdes_put32(sd, dsk->seek_intr_cycle);
    serdes_put32(sd, dsk->seclate_intr_cycle);
    serdes_put16(sd, dsk->pending);

    for (drive_num = 0; drive_num < NUM_DISK_DRIVES; drive_num++) {
        dd = &dsk->drives[drive_num];

        /* We do not serialize the contents of the disk. */
        serdes_put16(sd, dd->head);
        serdes_put16(sd, dd->cylinder);
        serdes_put16(sd, dd->target_cylinder);
        serdes_put16(sd, dd->sector);
        serdes_put16(sd, dd->sector_word);
    }
}

void disk_deserialize(struct disk *dsk, struct serdes *sd)
{
    unsigned int drive_num;
    struct disk_drive *dd;

    dsk->kstat = serdes_get16(sd);
    dsk->kdata_read = serdes_get16(sd);
    dsk->kdata = serdes_get16(sd);
    dsk->has_kdata= serdes_get_bool(sd);
    dsk->kadr = serdes_get16(sd);
    dsk->kcomm = serdes_get16(sd);
    dsk->disk = serdes_get16(sd);
    dsk->rec_no = serdes_get8(sd);
    dsk->restore = serdes_get_bool(sd);
    dsk->sync_word_written = serdes_get_bool(sd);
    dsk->bitclk_enable = serdes_get_bool(sd);
    dsk->wdinit = serdes_get_bool(sd);
    dsk->seclate_enable = serdes_get_bool(sd);
    dsk->intr_cycle = serdes_get32(sd);
    dsk->ds_intr_cycle = serdes_get32(sd);
    dsk->dw_intr_cycle = serdes_get32(sd);
    dsk->seek_intr_cycle = serdes_get32(sd);
    dsk->seclate_intr_cycle = serdes_get32(sd);
    dsk->pending = serdes_get16(sd);

    for (drive_num = 0; drive_num < NUM_DISK_DRIVES; drive_num++) {
        dd = &dsk->drives[drive_num];

        /* We do not serialize the contents of the disk. */
        dd->head = serdes_get16(sd);
        dd->cylinder = serdes_get16(sd);
        dd->target_cylinder = serdes_get16(sd);
        dd->sector = serdes_get16(sd);
        dd->sector_word = serdes_get16(sd);
    }
}

