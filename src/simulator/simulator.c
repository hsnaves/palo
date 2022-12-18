#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "simulator/simulator.h"
#include "microcode/microcode.h"
#include "common/utils.h"

/* Constants. */
#define NUM_R_REGISTERS             32
#define NUM_S_REGISTERS       (8 * 32)

/* For the MPC. */
#define MPC_BANK_MASK            0xC00
#define MPC_ADDR_MASK            0x3FF

/* For the memory. */
#define NUM_MICROCODE_BANKS          4
#define NUM_BANKS                    4
#define NUM_BANK_SLOTS  TASK_NUM_TASKS
#define MEMORY_TOP              0xFE00
#define XM_BANK_START           0xFFE0
#define XM_BANK_END (XM_BANK_START + NUM_BANK_SLOTS)

/* For fixing the microcode in RAM. */
#define MC_INVERT_MASK      0x00088400

/* Static tables. */

/* Table used to implement F2_EMU_ACSOURCE and F2_EMU_IDISP. */
static const uint8_t ACSROM[] = {
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
    0x02, 0x05, 0x03, 0x06, 0x07, 0x0E, 0x0E, 0x0E,
    0x0E, 0x04, 0x04, 0x0E, 0x0E, 0x0E, 0x01, 0x0E,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E,
    0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0E, 0x0F,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
    0x01, 0x00, 0x02, 0x03, 0x04, 0x05, 0x0E, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0d, 0x06, 0x0F,
    0x01, 0x00, 0x02, 0x03, 0x04, 0x05, 0x0E, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0d, 0x06, 0x0F
};


/* Functions. */

void simulator_initvar(struct simulator *sim)
{
    sim->r = NULL;
    sim->s = NULL;
    sim->consts = NULL;
    sim->microcode = NULL;
    sim->task_mpc = NULL;
    sim->mem = NULL;
    sim->xm_banks = NULL;
    sim->sreg_banks = NULL;

    disk_initvar(&sim->dsk);
    display_initvar(&sim->displ);
    ethernet_initvar(&sim->ether);
    keyboard_initvar(&sim->keyb);
    mouse_initvar(&sim->mous);
}

void simulator_destroy(struct simulator *sim)
{
    disk_destroy(&sim->dsk);
    display_destroy(&sim->displ);
    ethernet_destroy(&sim->ether);
    keyboard_destroy(&sim->keyb);
    mouse_destroy(&sim->mous);

    if (sim->r) free((void *) sim->r);
    sim->r = NULL;

    if (sim->s) free((void *) sim->s);
    sim->s = NULL;

    if (sim->consts) free((void *) sim->consts);
    sim->microcode = NULL;

    if (sim->microcode) free((void *) sim->microcode);
    sim->microcode = NULL;

    if (sim->task_mpc) free((void *) sim->task_mpc);
    sim->task_mpc = NULL;

    if (sim->mem) free((void *) sim->mem);
    sim->mem = NULL;

    if (sim->xm_banks) free((void *) sim->xm_banks);
    sim->xm_banks = NULL;

    if (sim->sreg_banks) free((void *) sim->sreg_banks);
    sim->sreg_banks = NULL;
}

int simulator_create(struct simulator *sim, enum system_type sys_type)
{
    simulator_initvar(sim);

    sim->r = malloc(NUM_R_REGISTERS * sizeof(uint16_t));
    sim->s = malloc(NUM_S_REGISTERS * sizeof(uint16_t));
    sim->consts = (uint16_t *)
        malloc(CONSTANT_SIZE * sizeof(uint16_t));
    sim->microcode = (uint32_t *)
        malloc(NUM_MICROCODE_BANKS * MICROCODE_SIZE * sizeof(uint32_t));
    sim->task_mpc = (uint16_t *)
        malloc(TASK_NUM_TASKS * sizeof(uint16_t));
    sim->mem = (uint16_t *)
        malloc(NUM_BANKS * MEMORY_SIZE * sizeof(uint16_t));
    sim->xm_banks = (uint16_t *)
        malloc(NUM_BANK_SLOTS * sizeof(uint16_t));
    sim->sreg_banks = (uint8_t *)
        malloc(NUM_BANK_SLOTS * sizeof(uint8_t));

    if (unlikely(!sim->r || !sim->s
                 || !sim->consts || !sim->microcode || !sim->task_mpc
                 || !sim->mem || !sim->xm_banks || !sim->sreg_banks)) {
        report_error("sim: create: could not allocate memory");
        simulator_destroy(sim);
        return FALSE;
    }

    if (unlikely(!disk_create(&sim->dsk))) {
        report_error("sim: create: could not create disk controller");
        simulator_destroy(sim);
        return FALSE;
    }

    if (unlikely(!display_create(&sim->displ))) {
        report_error("sim: create: could not create display controller");
        simulator_destroy(sim);
        return FALSE;
    }

    if (unlikely(!ethernet_create(&sim->ether))) {
        report_error("sim: create: could not create ethernet controller");
        simulator_destroy(sim);
        return FALSE;
    }

    if (unlikely(!keyboard_create(&sim->keyb))) {
        report_error("sim: create: could not create keyboard controller");
        simulator_destroy(sim);
        return FALSE;
    }

    if (unlikely(!mouse_create(&sim->mous))) {
        report_error("sim: create: could not create mouse controller");
        simulator_destroy(sim);
        return FALSE;
    }

    sim->sys_type = sys_type;
    return TRUE;
}

int simulator_load_constant_rom(struct simulator *sim,
                                const char *filename)
{
    FILE *fp;
    uint16_t i;
    uint16_t val;
    int c;

    if (!filename) return TRUE;
    fp = fopen(filename, "rb");
    if (unlikely(!fp)) {
        report_error("simulator: load_constant_rom: "
                     "cannot open `%s`", filename);
        return FALSE;
    }

    for (i = 0; i < CONSTANT_SIZE; i++) {
        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val = (uint16_t) (c & 0xFF);

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val |= ((uint16_t) (c & 0xFF)) << 8;

        sim->consts[i] = val;
    }

    c = fgetc(fp);
    if (unlikely(c != EOF)) {
        report_error("simulator: load_constant_rom: "
                     "invalid file size `%s`",
                     filename);
        fclose(fp);
        return FALSE;
    }

    fclose(fp);
    return TRUE;

error_eof:
    report_error("simulator: load_constant_rom: "
                 "premature end of file `%s`",
                 filename);
    fclose(fp);
    return FALSE;
}

int simulator_load_microcode_rom(struct simulator *sim,
                                 const char *filename, uint8_t bank)
{
    FILE *fp;
    uint16_t i, offset;
    uint32_t val;
    int c;

    if (unlikely(bank >= 2)) {
        report_error("simulator: load_microcode_rom: "
                     "invalid bank `%u`", bank);
        return FALSE;
    }

    offset = (bank) ? MICROCODE_SIZE : 0;

    if (!filename) return TRUE;
    fp = fopen(filename, "rb");
    if (unlikely(!fp)) {
        report_error("simulator: load_microcode_rom: "
                     "cannot open `%s`", filename);
        return FALSE;
    }

    for (i = 0; i < MICROCODE_SIZE; i++) {
        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val = (uint32_t) (c & 0xFF);

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val |= ((uint32_t) (c & 0xFF)) << 8;

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val |= ((uint32_t) (c & 0xFF)) << 16;

        c = fgetc(fp);
        if (unlikely(c == EOF)) goto error_eof;
        val |= ((uint32_t) (c & 0xFF)) << 24;

        sim->microcode[offset + i] = val;
    }

    c = fgetc(fp);
    if (unlikely(c != EOF)) {
        report_error("simulator: load_microcode_rom: "
                     "invalid file size `%s`",
                     filename);
        fclose(fp);
        return FALSE;
    }

    fclose(fp);
    return TRUE;

error_eof:
    report_error("simulator: load_microcode_rom: "
                 "premature end of file `%s`",
                 filename);
    fclose(fp);
    return FALSE;
}

void simulator_reset(struct simulator *sim)
{
    uint8_t task;

    memset(sim->r, 0, NUM_R_REGISTERS * sizeof(uint16_t));
    memset(sim->s, 0, NUM_S_REGISTERS * sizeof(uint16_t));
    memset(sim->mem, 0, NUM_BANKS * MEMORY_SIZE * sizeof(uint16_t));
    memset(sim->xm_banks, 0, NUM_BANK_SLOTS * sizeof(uint16_t));
    memset(sim->sreg_banks, 0, NUM_BANK_SLOTS * sizeof(uint8_t));

    for (task = 0; task < TASK_NUM_TASKS; task++) {
        sim->task_mpc[task] = (uint16_t) task;
    }

    disk_reset(&sim->dsk);
    display_reset(&sim->displ);
    ethernet_reset(&sim->ether);
    keyboard_reset(&sim->keyb);
    mouse_reset(&sim->mous);

    sim->error = FALSE;

    sim->t = 0;
    sim->l = 0;
    sim->m = 0;
    sim->mar = 0;
    sim->ir = 0;
    sim->mir = 0;
    sim->mpc = 0;
    sim->ctask = TASK_EMULATOR;
    sim->ntask = TASK_EMULATOR;
    sim->aluC0 = FALSE;
    sim->skip = FALSE;
    sim->carry = FALSE;
    sim->rmr = 0xFFFFU;
    sim->rdram = FALSE;
    sim->wrtram = FALSE;
    sim->swmode = FALSE;
    sim->soft_reset = FALSE;
    sim->cram_addr = 0x0;
    sim->cycle = 0;
    sim->mem_cycle = 0;
    sim->mem_task = TASK_EMULATOR;
    sim->mem_low = 0xFFFFU;
    sim->mem_high = 0xFFFFU;
    sim->mem_extended = 0;
    sim->mem_which = 0;

    /* Sets the next interrupt cycle. */
    sim->intr_cycle = sim->dsk.intr_cycle;
    if (sim->intr_cycle > sim->displ.intr_cycle)
        sim->intr_cycle = sim->displ.intr_cycle;
    if (sim->intr_cycle > sim->ether.intr_cycle)
        sim->intr_cycle = sim->ether.intr_cycle;
}

uint16_t simulator_read(const struct simulator *sim, uint16_t address,
                        uint8_t task, int extended_memory)
{
    if (address >= MEMORY_TOP) {
        if (address >= MOUSE_BASE && address < MOUSE_END) {
            return mouse_read(&sim->mous, address);
        }

        if (address >= KEYBOARD_BASE && address < KEYBOARD_END) {
            return keyboard_read(&sim->keyb, address);
        }

        if (address >= XM_BANK_START && address < XM_BANK_END) {
            /* NB: While not specified in documentation, some code (IFS in
             * particular) relies on the fact that the upper 12 bits of the
             * bank registers are all 1s.
             */
            return ((uint16_t) 0xFFF0)
                | sim->xm_banks[address - XM_BANK_START];
        }

        /* Returns some garbage. */
        return 0xBEEF;
    } else {
        const uint16_t *base_mem;
        int bank_number;
        bank_number = extended_memory
            ? (sim->xm_banks[task] & 0x3)
            : ((sim->xm_banks[task] >> 2) & 0x3);
        base_mem = &sim->mem[bank_number * MEMORY_SIZE];
        return base_mem[address];
    }
}

void simulator_write(struct simulator *sim, uint16_t address,
                     uint16_t data, uint8_t task, int extended_memory)
{
    if (address >= MEMORY_TOP) {
        if (address >= MOUSE_BASE && address < MOUSE_END) {
            /* Nothing to do here. */
            return;
        }

        if (address >= KEYBOARD_BASE && address < KEYBOARD_END) {
            /* Nothing to do here. */
            return;
        }

        if (address >= XM_BANK_START && address < XM_BANK_END) {
            /* NB: While not specified in documentation, some code (IFS in
             * particular) relies on the fact that the upper 12 bits of the
             * bank registers are all 1s.
             */
            sim->xm_banks[address - XM_BANK_START] = data;
            return;
        }
    } else {
        uint16_t *base_mem;
        int bank_number;
        bank_number = extended_memory
            ? (sim->xm_banks[task] & 0x3)
            : ((sim->xm_banks[task] >> 2) & 0x3);
        base_mem = &sim->mem[bank_number * MEMORY_SIZE];
        base_mem[address] = data;
    }
}

/* Obtains the RSEL value (which can be modified by F2_EMU_ACSOURCE,
 * F2_EMU_ACDEST, and F2_EMU_LOAD_DNS).
 * The current predecoded microcode is in `mc`.
 * Returns the RSEL value.
 */
static
uint16_t get_modified_rsel(struct simulator *sim,
                           const struct microcode *mc)
{
    uint16_t rsel;
    uint16_t ir;

    rsel = mc->rsel;
    if (mc->task == TASK_EMULATOR) {
        ir = sim->ir;
        /* Modify the last 3 bits according to the
         * corresponding field in the IR register.
         */
        if (mc->f2 == F2_EMU_ACSOURCE) {
            rsel &= ~0x3;
            rsel |= (~(ir >> 13)) & 0x3;
        } else if (mc->f2 == F2_EMU_ACDEST
                   || mc->f2 == F2_EMU_LOAD_DNS) {
            rsel &= ~0x3;
            rsel |= (~(ir >> 11)) & 0x3;
        }
    }

    return rsel;
}

/* Decodes an address to access the microcode RAM.
 * The parameter `low_half` specifies whether to read the lower
 * or the upper half of the word.
 * Returns the RAM address.
 */
static
uint16_t decode_ram_address(struct simulator *sim,
                            int *low_half)
{
    uint16_t addr;
    uint8_t bank;

    if ((sim->cram_addr & 0x0800) != 0) {
        report_error("simulator: step: "
                     "reading from (or writing to) ROM "
                     "is not supported");
        sim->error = TRUE;
        return 0;
    }

    *low_half = ((sim->cram_addr & 0x0400) == 0);
    addr = sim->cram_addr & 0x3FF;

    if (sim->sys_type == ALTO_II_3KRAM) {
        bank = (sim->cram_addr >> 12) & 3;
        if (bank == 3) {
            report_error("simulator: step: "
                         "RAM bank 3 not supported");
            sim->error = TRUE;
            return 0xFFFFU;
        }
        /* Plus 1 for the ROM bank. */
        bank += 1;
    } else if (sim->sys_type == ALTO_II_2KROM) {
        /* Plus 2 for the ROM banks. */
        bank = 2;
    } else {
        /* Plus 1 for the ROM bank. */
        bank = 1;
    }

    addr += (bank << 10);
    return addr;
}

/* Performs any pending reads from the microcode RAM.
 * Returns the value read from the microcode RAM.
 */
static
uint16_t do_rdram(struct simulator *sim)
{
    uint32_t mcode;
    uint16_t addr, val;
    int low_half;

    if (!sim->rdram) return 0xFFFFU;
    addr = decode_ram_address(sim, &low_half);
    if (sim->error) return 0xFFFFU;

    mcode = sim->microcode[addr];
    mcode ^= MC_INVERT_MASK;

    val = (low_half) ? mcode : (mcode >> 16);
    sim->rdram = FALSE;
    return val;
}

/* Performs any pending writes to the microcode RAM.
 * The value of the alu isin `alu`.
 */
static
void do_wrtram(struct simulator *sim, uint16_t alu)
{
    uint32_t mcode;
    uint16_t addr;
    int low_half;

    if (!sim->wrtram) return;
    addr = decode_ram_address(sim, &low_half);
    if (sim->error) return;

    mcode = ((uint32_t) sim->m) << 16;
    mcode |= alu;
    mcode ^= MC_INVERT_MASK;

    sim->microcode[addr] = mcode;
    sim->wrtram = FALSE;
}

/* Auxiliary function to obtain the value of the bus.
 * The current predecoded microcode is in `mc`.
 * The parameter `modified_rsel` specifies the modified RSEL value.
 * Returns the value of the bus.
 */
static
uint16_t read_bus(struct simulator *sim, const struct microcode *mc,
                  uint16_t modified_rsel)
{
    uint16_t output;
    uint16_t t;
    uint8_t rb;

    output = do_rdram(sim);

    if (mc->use_constant) {
        /* Not used the modified RSEL here. */
        output &= sim->consts[mc->const_addr];
        return output;
    }

    if (mc->bs_use_crom) {
        output &= sim->consts[mc->const_addr];
    }

    switch (mc->bs) {
    case BS_READ_R:
        output &= sim->r[modified_rsel];
        break;
    case BS_LOAD_R:
        /* The load is performed at the end, for now set the
         * bus to zero.
         */
        output &= 0;
        break;
    case BS_NONE:
        if (mc->task == TASK_EMULATOR && mc->f1 == F1_EMU_RSNF) {
            output &= ethernet_rsnf(&sim->ether);
        } else if (mc->task == TASK_ETHERNET) {
            if (mc->f1 == F1_ETH_EILFCT) {
                output &= ethernet_eilfct(&sim->ether);
            } else if (mc->f1 == F1_ETH_EPFCT) {
                output &= ethernet_epfct(&sim->ether);
            }
        }
        break;
    case BS_READ_MD:
        /* TODO: check for delays. */
        output &= (sim->mem_which) ? sim->mem_high : sim->mem_low;
        sim->mem_which = (1 ^ sim->mem_which);
        break;
    case BS_READ_MOUSE:
        output &= mouse_poll_bits(&sim->mous);
        break;
    case BS_READ_DISP:
        t = sim->ir & 0x00FFU;
        if ((sim->ir & 0x300) != 0 && (sim->ir & 0x80) != 0) {
            t |= 0xFF00U;
        }
        output &= t;
        break;

    default:
        if (mc->ram_task) {
            rb = sim->sreg_banks[mc->task];
            if (mc->bs == BS_RAM_READ_S_LOCATION) {
                /* Do not use modified_rsel here. */
                if (mc->rsel == 0) {
                    output &= sim->m;
                } else {
                    output &= sim->s[rb * NUM_R_REGISTERS + mc->rsel];
                }
                break;
            } else if (mc->bs == BS_RAM_LOAD_S_LOCATION) {
                /* Random garbage appears on the bus. */
                output &= 0xBEEF;
                break;
            }
        } else if (mc->task == TASK_ETHERNET && mc->bs == BS_ETH_EIDFCT) {
            output &= ethernet_eidfct(&sim->ether);
            break;
        } else if ((mc->task == TASK_DISK_SECTOR)
                   || (mc->task == TASK_DISK_WORD)) {
            if (mc->bs == BS_DSK_READ_KSTAT) {
                output &= disk_read_kstat(&sim->dsk);
                break;
            } else if (mc->bs == BS_DSK_READ_KDATA) {
                output &= disk_read_kdata(&sim->dsk);
                break;
            }
        }

        report_error("simulator: step: "
                     "invalid bus source");
        sim->error = TRUE;
        return 0;
    }

    return output;
}

/* Auxiliary function to perform the ALU computation.
 * The current predecoded microcode is in `mc`.
 * The value of the bus is in `bus`, and the carry output is written
 * to `carry`.
 * Returns the value of the ALU.
 */
static
uint16_t compute_alu(struct simulator *sim, const struct microcode *mc,
                     uint16_t bus, int *carry)
{
    uint32_t res;
    uint32_t a, b;

    a = (uint32_t) bus;
    b = (uint32_t) sim->t;

    switch (mc->aluf) {
    case ALU_BUS:
        res = a;
        break;
    case ALU_T:
        res = b;
        break;
    case ALU_BUS_OR_T:
        res = a | b;
        break;
    case ALU_BUS_AND_T:
    case ALU_BUS_AND_T_WB:
        res = a & b;
        break;
    case ALU_BUS_XOR_T:
        res = a ^ b;
        break;
    case ALU_BUS_PLUS_1:
        res = a + 1;
        break;
    case ALU_BUS_MINUS_1:
        res = a + 0xFFFFU;
        break;
    case ALU_BUS_PLUS_T:
        res = a + b;
        break;
    case ALU_BUS_MINUS_T:
        res = a + ((~b) & 0xFFFFU) + 1;
        break;
    case ALU_BUS_MINUS_T_MINUS_1:
        res = a + ((~b) & 0xFFFFU);
        break;
    case ALU_BUS_PLUS_T_PLUS_1:
        res = a + b + 1;
        break;
    case ALU_BUS_PLUS_SKIP:
        res = a + ((uint32_t) (sim->skip ? 1 : 0));
        break;
    case ALU_BUS_AND_NOT_T:
        res = a & (~b) & 0xFFFFU;
        break;
    default:
        report_error("simulator: step: "
                     "invalid ALUF = %o", mc->aluf);
        sim->error = TRUE;
        *carry = 0;
        return 0xDEAD;
    }

    *carry = ((res & 0x10000) != 0);
    return (uint16_t) res;
}

/* Auxiliary function to perform the shift computation.
 * The current predecoded microcode is in `mc`.
 * This function might modify the value of `load_r`, in case it is
 * performing a NOVA style shift.
 * The value of the nova carry is given by `nova_carry`, and since it is
 * a pointer, it will be populated with the new value of the nova carry
 * on return.
 * Returns the value of the shifter.
 */
static
uint16_t do_shift(struct simulator *sim, const struct microcode *mc,
                  int *load_r, int *nova_carry)
{
    uint16_t res;
    int dns, has_magic;
    int carry;

    dns = (mc->f2 == F2_EMU_LOAD_DNS);
    has_magic = (mc->f2 == F2_EMU_MAGIC);

    if (dns) {
        *load_r = ((sim->ir & 0x0008) == 0);

        switch ((sim->ir >> 4) & 3) {
        case 0: /* not affected. */
            carry = sim->carry;
        case 1: /* Z */
            carry = 0;
        case 2: /* O */
            carry = 1;
        case 3: /* C */
            carry = !(sim->carry);
        }

        switch ((sim->ir >> 8) & 7) {
        case 0:
        case 2:
        case 7:
            /* COM, MOV, AND - carry unaffected. */
            break;
        case 1:
        case 3:
        case 4:
        case 5:
        case 6:
            if (sim->aluC0) {
                carry = !carry;
            }
            break;
        }
    } else {
        carry = 0;
        *nova_carry = 0;
    }

    switch (mc->f1) {
    case F1_LLSH1:
        res = sim->l << 1;
        if (has_magic) {
            res |= (sim->t & 0x8000) ? 1 : 0;
        } else if (dns) {
            /* Nova style shift */
            res |= (carry) ? 1 : 0;
            *nova_carry = (sim->l & 0x8000) ? 1 : 0;
        }
        break;
    case F1_LRSH1:
        res = sim->l >> 1;
        if (has_magic) {
            res |= (sim->t & 1) ? 0x8000 : 0;
        } else if (dns) {
            /* Nova style shift */
            res |= (carry) ? 0x8000 : 0;
            *nova_carry = sim->l & 1;
        }
        break;
    case F1_LLCY8:
        res = (sim->l << 8);
        res |= (sim->l >> 8);
        break;
    default:
        res = sim->l;
        break;
    }

    return res;
}

/* Obtains the pending tasks.
 * Returns a bitset of pending tasks.
 */
static
uint16_t get_pending(struct simulator *sim)
{
    uint16_t pending;
    pending = (1 << TASK_EMULATOR);
    pending |= sim->dsk.pending;
    pending |= sim->displ.pending;
    pending |= sim->ether.pending;
    return pending;
}

/* Performas a BLOCK. */
static
void do_block(struct simulator *sim, uint8_t task)
{
    disk_block_task(&sim->dsk, task);
    display_block_task(&sim->displ, task);
    ethernet_block_task(&sim->ether, task);
}

/* Performs the F1 function.
 * The current predecoded microcode is in `mc`.
 * The value of the bus is in `bus`, and of the alu in `alu`.
 * The next task after the following microinstruction is returned
 * in  `nntask`.
 */
static
void do_f1(struct simulator *sim, const struct microcode *mc,
           uint16_t bus, uint16_t alu, uint8_t *nntask)
{
    uint16_t addr;
    uint8_t pending, tmp;

    *nntask = sim->ntask;

    switch (mc->f1) {
    case F1_NONE:
        /* Nothing to do. */
        return;
    case F1_CONSTANT:
    case F1_LLSH1:
    case F1_LRSH1:
    case F1_LLCY8:
        /* Already handled. */
        return;
    case F1_LOAD_MAR:
        /* TODO: Check if this load is not violating any
         * memory timing requirement.
         */
        sim->mar = alu;
        sim->mem_cycle = 0; /* This will be incremented at
                             * update_cycles() to 1, which
                             * is the correct value.
                             */
        sim->mem_task = mc->task;
        sim->mem_low = 0xFFFFU;
        sim->mem_high = 0xFFFFU;
        sim->mem_extended = (sim->sys_type != ALTO_I)
                          ? (mc->f2 == F2_STORE_MD) : FALSE;
        sim->mem_which = 0;

        /* Perform the reading now. */
        addr = sim->mar;
        sim->mem_low = simulator_read(sim, addr, sim->mem_task,
                                      sim->mem_extended);

        addr = (sim->sys_type == ALTO_I) ? 1 | addr : 1 ^ addr;
        sim->mem_high = simulator_read(sim, addr, sim->mem_task,
                                       sim->mem_extended);

        /* TODO: Check that for TASK_MEMORY_REFRESH, loading MAR
         * with RSEL = 037 performs a BLOCK.
         */
        return;
    case F1_TASK:
        /* Switch tasks. */
        /* TODO: Maybe prevent two consecutive switches? */
        pending = get_pending(sim);
        for (tmp = TASK_NUM_TASKS; tmp--;) {
            if (pending & (1 << tmp)) {
                *nntask = tmp;
                break;
            }
        }
        return;
    case F1_BLOCK:
        if (mc->task == TASK_EMULATOR) {
            report_error("simulator: step: "
                         "emulator task cannot block");
            sim->error = TRUE;
            return;
        }

        /* Prevent the current task from running. */
        do_block(sim, mc->task);

        return;
    }

    if (mc->ram_task) {
        switch (mc->f1) {
        case F1_RAM_SWMODE:
            if (mc->task != TASK_EMULATOR) {
                report_error("simulator: step: "
                             "SWMODE only allowed in emulator task");
                sim->error = TRUE;
                return;
            }
            break;
        case F1_RAM_WRTRAM:
            sim->wrtram = TRUE;
            break;
        case F1_RAM_RDRAM:
            sim->rdram = TRUE;
            break;
        case F1_RAM_LOAD_SRB:
            if (mc->task == TASK_EMULATOR) break;
            tmp = (uint8_t) ((bus >> 1) & 0x7);
            if (sim->sys_type != ALTO_II_3KRAM)
                tmp = 0;
            sim->sreg_banks[mc->task] = tmp;
            break;
        }
    }

    switch (mc->task) {
    case TASK_EMULATOR:
        switch (mc->f1) {
        case F1_EMU_SWMODE:
            sim->swmode = TRUE;
            break;
        case F1_EMU_LOAD_RMR:
            sim->rmr = bus;
            break;
        case F1_EMU_LOAD_ESRB:
            tmp = (uint8_t) ((bus >> 1) & 0x7);
            if (sim->sys_type != ALTO_II_3KRAM)
                tmp = 0;
            sim->sreg_banks[mc->task] = tmp;
            break;
        case F1_EMU_RSNF:
            /* Already handled. */
            break;
        case F1_EMU_STARTF:
            if (bus & 0x8000) {
                sim->soft_reset = TRUE;
            } else {
                switch (bus) {
                case 0x01:
                case 0x02:
                case 0x03:
                    ethernet_startf(&sim->ether, bus);
                    break;

                default:
                    report_error("simulator: step: "
                                 "invalid STARTF value");
                    sim->error = TRUE;
                    return;
                }
            }
            break;
        default:
            report_error("simulator: step: "
                         "invalid F1 function %o for emulator",
                         mc->f1);
            sim->error = TRUE;
            return;
        }
        break;

    case TASK_DISK_SECTOR:
    case TASK_DISK_WORD:
        switch (mc->f1) {
        case F1_DSK_STROBE:
            disk_strobe(&sim->dsk);
            break;
        case F1_DSK_LOAD_KSTAT:
            disk_load_kstat(&sim->dsk, bus);
            break;
        case F1_DSK_INCRECNO:
            disk_increcno(&sim->dsk);
            break;
        case F1_DSK_CLRSTAT:
            disk_clrstat(&sim->dsk);
            break;
        case F1_DSK_LOAD_KCOMM:
            disk_load_kcomm(&sim->dsk, bus);
            break;
        case F1_DSK_LOAD_KADR:
            disk_load_kadr(&sim->dsk, bus);
            break;
        case F1_DSK_LOAD_KDATA:
            disk_load_kdata(&sim->dsk, bus);
            break;
        default:
            report_error("simulator: step: "
                         "invalid F1 function %o for disk tasks",
                         mc->f1);
            sim->error = TRUE;
            return;
        }
        break;

    case TASK_ETHERNET:
        switch (mc->f1) {
        case F1_ETH_EILFCT:
        case F1_ETH_EPFCT:
            /* Already handled. */
            break;
        case F1_ETH_EWFCT:
            ethernet_ewfct(&sim->ether);
            break;
        default:
            report_error("simulator: step: "
                         "invalid F1 function %o for ethernet",
                         mc->f1);
            sim->error = TRUE;
            return;
        }
        break;
    }
}

/* Performs the F2 function.
 * The current predecoded microcode is in `mc`.
 * The value of the bus is in `bus`, and of the alu in `alu`.
 * The value of the shifter is in `shifter_output`, and the
 * value of the nova style carry is in `nova_carry`.
 * Retuns the bits that should be modified for the NEXT part of the
 * following instruction.
 */
static
uint16_t do_f2(struct simulator *sim, const struct microcode *mc,
               uint16_t bus, uint16_t alu, uint16_t shifter_output,
               int nova_carry)
{
    uint16_t next_extra;
    uint16_t addr;

    /* Computes the F2 function. */
    switch (mc->f2) {
    case F2_NONE:
        /* Nothing to do. */
        return 0;
    case F2_CONSTANT:
        /* Already handled. */
        return 0;
    case F2_BUSEQ0:
        return (bus == 0) ? 1 : 0;
    case F2_SHLT0:
        return (shifter_output & 0x8000) ? 1 : 0;
    case F2_SHEQ0:
        return (shifter_output == 0) ? 1 : 0;
    case F2_BUS:
        return (bus & MPC_ADDR_MASK);
    case F2_ALUCY:
        return (sim->aluC0) ? 1 : 0;
    case F2_STORE_MD:
        /* TODO: Check the cycle times. */
        if (mc->f1 != F1_LOAD_MAR || sim->sys_type == ALTO_I) {
            /* TODO: On Alto I MAR<- and <-MD in the same
             * microinstruction should be illegal.
             */
            addr = sim->mar;
            if (sim->mem_which) {
                addr = (sim->sys_type == ALTO_I) ? 1 | addr : 1 ^ addr;
            }
            simulator_write(sim, addr, bus, sim->mem_task,
                            sim->mem_extended);
            sim->mem_which = (1 ^ sim->mem_which);
        }
        return 0;
    }

    switch (mc->task) {
    case TASK_EMULATOR:
        switch (mc->f2) {
        case F2_EMU_MAGIC:
        case F2_EMU_ACDEST:
            /* Already handled. */
            return 0;
        case F2_EMU_BUSODD:
            return (bus & 1);
        case F2_EMU_LOAD_DNS:
            switch (sim->ir & 7) {
            case 0:
                sim->skip = FALSE;
                break;
            case 1: /* SKP */
                sim->skip = TRUE;
                break;
            case 2: /* SZC */
                sim->skip = (!nova_carry);
                break;
            case 3: /* SNC */
                sim->skip = nova_carry;
                break;
            case 4: /* SZR */
                sim->skip = (shifter_output == 0);
                break;
            case 5: /* SNR */
                sim->skip = (shifter_output != 0);
                break;
            case 6: /* SEZ */
                sim->skip = (shifter_output == 0 || (!nova_carry));
                break;
            case 7: /* SBN */
                sim->skip = (shifter_output != 0 && nova_carry);
                break;
            }
            if ((sim->ir & 0x0008) == 0) {
                sim->carry = nova_carry;
            }
            return 0;
        case F2_EMU_LOAD_IR:
            sim->ir = bus;
            sim->skip = FALSE;
            next_extra = (bus >> 8) & 0x7;
            if (bus & 0x8000) next_extra |= 0x8;
            return next_extra;
        case F2_EMU_IDISP:
            if (sim->ir & 0x8000) {
                next_extra = 3 - ((sim->ir >> 6) & 3);
            } else {
                /* Using the ACSROM makes it a little faster. */
                next_extra = ACSROM[((sim->ir >> 8) & 0x7F) + 0x80];
            }
            return next_extra;
        case F2_EMU_ACSOURCE:
            if (sim->ir & 0x8000) {
                next_extra = 3 - ((sim->ir >> 6) & 3);
            } else {
                /* Using the ACSROM makes it a little faster. */
                next_extra = ACSROM[(sim->ir >> 8) & 0x7F];
            }
            return next_extra;
        default:
            report_error("simulator: step: "
                         "invalid F2 function %o for emulator",
                         mc->f2);
            sim->error = TRUE;
            return 0;
        }
        break;

    case TASK_DISK_SECTOR:
    case TASK_DISK_WORD:
        switch (mc->f2) {
        case F2_DSK_INIT:
            return disk_init(&sim->dsk, mc->task);
        case F2_DSK_RWC:
            return disk_rwc(&sim->dsk, mc->task);;
        case F2_DSK_RECNO:
            return disk_recno(&sim->dsk, mc->task);
        case F2_DSK_XFRDAT:
            return disk_xfrdat(&sim->dsk, mc->task);
        case F2_DSK_SWRNRDY:
            return disk_swrnrdy(&sim->dsk, mc->task);
        case F2_DSK_NFER:
            return disk_nfer(&sim->dsk, mc->task);
        case F2_DSK_STROBON:
            return disk_strobon(&sim->dsk, mc->task);
        default:
            report_error("simulator: step: "
                         "invalid F2 function %o for disk tasks",
                         mc->f2);
            sim->error = TRUE;
            return 0;
        }
        break;

    case TASK_ETHERNET:
        switch (mc->f2) {
        case F2_ETH_EODFCT:
            ethernet_eodfct(&sim->ether, bus);
            return 0;
        case F2_ETH_EOSFCT:
            ethernet_eosfct(&sim->ether);
            return 0;
        case F2_ETH_ERBFCT:
            return ethernet_erbfct(&sim->ether);
        case F2_ETH_EEFCT:
            ethernet_eefct(&sim->ether);
            return 0;
        case F2_ETH_EBFCT:
            return ethernet_ebfct(&sim->ether);
        case F2_ETH_ECBFCT:
            return ethernet_ecbfct(&sim->ether);
        case F2_ETH_EISFCT:
            ethernet_eisfct(&sim->ether);
            return 0;
        default:
            report_error("simulator: step: "
                         "invalid F2 function %o for ethernet",
                         mc->f2);
            sim->error = TRUE;
            return 0;
        }
        break;

    case TASK_DISPLAY_WORD:
        switch (mc->f2) {
        case F2_DW_LOAD_DDR:
            display_load_ddr(&sim->displ, bus);
            return 0;
        default:
            report_error("simulator: step: "
                         "invalid F2 function %o for display word",
                         mc->f2);
            sim->error = TRUE;
            return 0;
        }
        break;

    case TASK_CURSOR:
        switch (mc->f2) {
        case F2_CUR_LOAD_XPREG:
            display_load_xpreg(&sim->displ, bus);
            return 0;
        case F2_CUR_LOAD_CSR:
            display_load_csr(&sim->displ, bus);
            return 0;
        default:
            report_error("simulator: step: "
                         "invalid F2 function %o for cursor",
                         mc->f2);
            sim->error = TRUE;
            return 0;
        }
        break;

    case TASK_DISPLAY_HORIZONTAL:
        switch (mc->f2) {
        case F2_DH_EVENFIELD:
            return display_even_field(&sim->displ);
        case F2_DH_SETMODE:
            return display_set_mode(&sim->displ, bus);
        default:
            report_error("simulator: step: "
                         "invalid F2 function %o for display horizontal",
                         mc->f2);
            sim->error = TRUE;
            return 0;
        }
        break;

    case TASK_DISPLAY_VERTICAL:
        switch (mc->f2) {
        case F2_DV_EVENFIELD:
            return display_even_field(&sim->displ);
        default:
            report_error("simulator: step: "
                         "invalid F2 function %o for display vertical",
                         mc->f2);
            sim->error = TRUE;
            return 0;
        }
        break;

    default:
        report_error("simulator: step: "
                     "invalid F2 function %o",
                     mc->f2);
        sim->error = TRUE;
        return 0;
    }

    return 0;
}

/* Writes back the registers.
 * The current predecoded microcode is in `mc`.
 * The value of the modified RSEL is in `modified_rsel`. The flag
 * `load_r` tells this function to load the new value of the R register.
 * The bus value is in `bus`, the alu in `alu`, the output of the
 * shifter is in `shifter_output`, and the carry from the ALU in `aluC0`.
 */
static
void wb_registers(struct simulator *sim,
                  const struct microcode *mc,
                  uint16_t modified_rsel, int load_r,
                  uint16_t bus, uint16_t alu,
                  uint16_t shifter_output, int aluC0)
{
    uint8_t rb;

    /* Writes back the R register. */
    if (load_r) {
        sim->r[modified_rsel] = shifter_output;
    }

    /* Writes back the S register. */
    if (!mc->use_constant) {
        if (mc->ram_task && mc->bs == BS_RAM_LOAD_S_LOCATION) {
            rb = sim->sreg_banks[mc->task];
            sim->s[rb * NUM_R_REGISTERS + mc->rsel] = sim->m;
        }
    }

    /* Writes back the L, M and ALUC0 registers. */
    if (mc->load_l) {
        sim->l = alu;
        if (mc->task == TASK_EMULATOR) {
            sim->m = alu;
        }
        sim->aluC0 = aluC0;
    }

    /* Writes back the T register. */
    if (mc->load_t) {
        if (mc->load_t_from_alu) {
            sim->t = alu;
        } else {
            sim->t = bus;
        }
        sim->cram_addr = alu;
    }
}

/* Updates the micro program counter and the next task.
 * The bits that are to be modified in the NEXT field of the following
 * instruction are given by `next_extra`. The task following the next
 * instruction is given by `nntask`.
 */
static
void update_program_counters(struct simulator *sim,
                             uint16_t next_extra, uint8_t nntask)
{
    uint32_t mcode;
    uint16_t mpc;
    uint8_t task;

    /* Updates the current task. */
    sim->ctask = sim->ntask;

    /* Updates the MPC and MIR. */
    task = sim->ctask;
    mpc = sim->task_mpc[task];
    mcode = sim->microcode[mpc];
    sim->task_mpc[task] = (mpc & MPC_BANK_MASK)
        | MICROCODE_NEXT(mcode) | next_extra;

    sim->mir = mcode;
    sim->mpc = mpc;

    /* Updates the next task. */
    sim->ntask = nntask;
}

/* Updates the simulator and memory cycles. */
static
void update_cycles(struct simulator *sim)
{
    sim->cycle++;

    /* Updates the memory cycle. */
    if (sim->mem_cycle != 0xFFFF) {
        if (sim->mem_cycle >= 10) {
            sim->mem_cycle = 0xFFFF;
        } else {
            sim->mem_cycle += 1;
        }
    }
}

/* Performs a SWMODE. */
static
void do_swmode(struct simulator *sim)
{
    /* TODO: Implement this. */
}

/* Performs a soft reset. */
static
void do_soft_reset(struct simulator *sim)
{
    uint16_t addr, ram_addr;
    uint8_t task;

    memset(sim->xm_banks, 0, NUM_BANK_SLOTS * sizeof(uint16_t));

    if (sim->sys_type == ALTO_II_2KROM) {
        ram_addr = 2048;
    } else {
        ram_addr = 1024;
    }

    for (task = 0; task < TASK_NUM_TASKS; task++) {
        if ((1 << task) & sim->rmr) {
            addr = ram_addr;
            addr |= (uint16_t) task;
        } else {
            addr = (uint16_t) task;
        }
        sim->task_mpc[task] = addr;
    }

    sim->ctask = TASK_EMULATOR;
    sim->ntask = TASK_EMULATOR;
    sim->mpc = sim->task_mpc[sim->ctask];
    sim->mir = sim->microcode[sim->mpc];
    sim->task_mpc[sim->ctask] = (sim->mpc & MPC_BANK_MASK)
        | MICROCODE_NEXT(sim->mir);

    /* This is a hack copied from ContrAlto source code. */
    sim->dsk.pending |= (1 << TASK_DISK_SECTOR);
    sim->dsk.pending &= ~(1 << TASK_DISK_WORD);
    sim->rmr = 0xFFFF;

    /* TODO: Finish this implementation. */
}

/* Checks for interrupts.
 * The parameter `prev_cycle` records the number of cycles before
 * the execution of the last microinstruction.
 */
static
void check_for_interrupts(struct simulator *sim, uint32_t prev_cycle)
{
    uint32_t diff, intr_diff;
    uint32_t tmp;

    diff = sim->cycle - prev_cycle;
    intr_diff = sim->intr_cycle - prev_cycle;

    while (diff > intr_diff) {
        /* Updates prev_cycle to match the current interrupt time. */
        prev_cycle += intr_diff;
        diff -= intr_diff;

        /* Dispatch the interrupts. */
        if (sim->intr_cycle == sim->dsk.intr_cycle) {
            disk_interrupt(&sim->dsk);
        }
        if (sim->intr_cycle == sim->displ.intr_cycle) {
            display_interrupt(&sim->displ);
        }
        if (sim->intr_cycle == sim->ether.intr_cycle) {
            ethernet_interrupt(&sim->ether);
        }

        /* Computes the next interrupt cycle. */
        intr_diff = sim->dsk.intr_cycle - prev_cycle;
        tmp = sim->displ.intr_cycle - prev_cycle;
        if (intr_diff > tmp) intr_diff = tmp;
        tmp = sim->ether.intr_cycle - prev_cycle;
        if (intr_diff > tmp) intr_diff = tmp;

        sim->intr_cycle = intr_diff + prev_cycle;
    }
}

void simulator_step(struct simulator *sim)
{
    struct microcode mc;
    uint32_t prev_cycle;
    uint16_t modified_rsel;
    uint16_t bus;
    uint16_t alu;
    uint16_t shifter_output;
    uint16_t next_extra;
    uint8_t nntask;
    int aluC0;
    int nova_carry;
    int load_r;
    int swmode;
    int soft_reset;

    if (sim->error) {
        report_error("simulator: step: "
                     "simulator is in error state");
        return;
    }

    /* Copy this to detect interrupts later. */
    prev_cycle = sim->cycle;

    /* Copy the swmode in a local variable. */
    swmode = sim->swmode;
    sim->swmode = FALSE;

    /* Copy the soft_reset in a local variable. */
    soft_reset = sim->soft_reset;
    sim->soft_reset = FALSE;

    microcode_predecode(&mc,
                        sim->sys_type,
                        sim->mpc,
                        sim->mir,
                        sim->ctask);

    load_r = (!mc.use_constant && mc.bs == BS_LOAD_R);

    /* Obtain the rsel (which might be modified by some F2
     * functions when in the EMULATOR task.
     */
    modified_rsel = get_modified_rsel(sim, &mc);

    /* Compute the bus. */
    bus = read_bus(sim, &mc, modified_rsel);
    if (sim->error) return;

    /* Compute the ALU. */
    alu = compute_alu(sim, &mc, bus, &aluC0);
    if (sim->error) return;

    /* Perform pending writes to the microcode RAM. */
    do_wrtram(sim, alu);

    /* Compute the shifter output. */
    shifter_output = do_shift(sim, &mc, &load_r, &nova_carry);

    /* Compute the F1 function. */
    do_f1(sim, &mc, bus, alu, &nntask);
    if (sim->error) return;

    /* Compute the F2 function. */
    next_extra = do_f2(sim, &mc, bus, alu,
                       shifter_output, nova_carry);
    if (sim->error) return;

    /* Write back the registers. */
    wb_registers(sim, &mc, modified_rsel, load_r,
                 bus, alu, shifter_output, aluC0);

    /* Update the micro program counter and the next task. */
    update_program_counters(sim, next_extra, nntask);

    /* Updates the cycles. */
    update_cycles(sim);

    /* Perform the SWMODE. */
    if (swmode) do_swmode(sim);

    /* Perform the soft reset. */
    if (soft_reset) do_soft_reset(sim);

    check_for_interrupts(sim, prev_cycle);
}

/* Auxiliary function used by simulator_disassemble().
 * Callback to print constants.
 */
static
void disasm_constant_cb(struct decoder *dec, uint16_t val,
                            struct decode_buffer *output)
{
    struct simulator *sim;
    sim = (struct simulator *) dec->arg;
    decode_buffer_print(output, "%o", sim->consts[val]);
}

/* Auxiliary function used by simulator_disassemble().
 * Callback to print R registers.
 */
static
void disasm_register_cb(struct decoder *dec, uint16_t val,
                        struct decode_buffer *output)
{
    if (val <= R_MASK) {
        decode_buffer_print(output, "R%o", val);
    } else {
        decode_buffer_print(output, "S%o", val & R_MASK);
    }
}

/* Auxiliary function used by simulator_disassemble().
 * Callback to print GOTO statements.
 */
static
void disasm_goto_cb(struct decoder *dec, uint16_t val,
                    struct decode_buffer *output)
{
    decode_buffer_print(output, ":%05o", val);
}

void simulator_disassemble(struct simulator *sim,
                           char *output, size_t output_size)
{
    struct microcode mc;
    struct decoder dec;
    struct decode_buffer out;

    out.buf = output;
    out.buf_size = output_size;
    decode_buffer_reset(&out);

    microcode_predecode(&mc,
                        sim->sys_type,
                        sim->mpc,
                        sim->mir,
                        sim->ctask);

    decode_buffer_print(&out,
                        "%02o-%06o %011o --- ",
                        sim->ctask, sim->mpc, sim->mir);

    dec.arg = sim;
    dec.const_cb = &disasm_constant_cb;
    dec.reg_cb = &disasm_register_cb;
    dec.goto_cb = &disasm_goto_cb;

    decoder_decode(&dec, &mc, &out);
}

void simulator_print_registers(struct simulator *sim,
                               char *output, size_t output_size)
{
    struct decode_buffer out;
    uint16_t pending;
    unsigned int i;

    out.buf = output;
    out.buf_size = output_size;
    decode_buffer_reset(&out);

    decode_buffer_print(&out,
                        "CTASK: %02o       NTASK: %02o       "
                        "MPC  : %06o   NMPC : %06o\n",
                        sim->ctask, sim->ntask,
                        sim->mpc, sim->task_mpc[sim->ctask]);

    decode_buffer_print(&out,
                        "T    : %06o   L    : %06o   "
                        "MAR  : %06o   IR   : %06o\n",
                        sim->t, sim->l, sim->mar, sim->ir);

    for (i = 0; i < NUM_R_REGISTERS; i++) {
        decode_buffer_print(&out,
                            "R%-4o: %06o",
                            i, sim->r[i]);
        if ((i % 4) == 3) {
            decode_buffer_print(&out, "\n");
        } else {
            decode_buffer_print(&out, "   ");
        }
    }

    decode_buffer_print(&out,
                        "ALUC0: %-6o   CARRY: %-6o   "
                        "SKIP : %-6o\n",
                        sim->aluC0 ? 1 : 0,
                        sim->carry ? 1 : 0,
                        sim->skip ? 1 : 0);

    pending = get_pending(sim);
    decode_buffer_print(&out,
                        "XM_B : %06o   SR_B : %03o      "
                        "PEND : %06o   RMR  : %06o\n",
                        sim->xm_banks[sim->ctask],
                        sim->sreg_banks[sim->ctask],
                        pending, sim->rmr);

    decode_buffer_print(&out,
                        "CYCLE: %u",
                        sim->cycle);

    if (sim->error) {
        decode_buffer_print(&out, "\nsimulator in error state");
    }
}
