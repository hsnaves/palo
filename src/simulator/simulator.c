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
#define MEMORY_TOP              0xFDFF
#define XM_BANK_START           0xFFE0

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
    sim->sr_banks = NULL;

    disk_initvar(&sim->dsk);
    display_initvar(&sim->dpl);
    ethernet_initvar(&sim->eth);
    keyboard_initvar(&sim->kb);
    mouse_initvar(&sim->ms);
}

void simulator_destroy(struct simulator *sim)
{
    disk_destroy(&sim->dsk);
    display_destroy(&sim->dpl);
    ethernet_destroy(&sim->eth);
    keyboard_destroy(&sim->kb);
    mouse_destroy(&sim->ms);

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

    if (sim->sr_banks) free((void *) sim->sr_banks);
    sim->sr_banks = NULL;
}

int simulator_create(struct simulator *sim, enum system_type st)
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
    sim->sr_banks = (uint8_t *)
        malloc(NUM_BANK_SLOTS * sizeof(uint8_t));

    if (unlikely(!sim->r || !sim->s
                 || !sim->consts || !sim->microcode || !sim->task_mpc
                 || !sim->mem || !sim->xm_banks || !sim->sr_banks)) {
        report_error("sim: create: could not allocate memory");
        simulator_destroy(sim);
        return FALSE;
    }

    if (unlikely(!disk_create(&sim->dsk))) {
        report_error("sim: create: could not create disk controller");
        simulator_destroy(sim);
        return FALSE;
    }

    if (unlikely(!display_create(&sim->dpl))) {
        report_error("sim: create: could not create display controller");
        simulator_destroy(sim);
        return FALSE;
    }

    if (unlikely(!ethernet_create(&sim->eth))) {
        report_error("sim: create: could not create ethernet controller");
        simulator_destroy(sim);
        return FALSE;
    }

    if (unlikely(!keyboard_create(&sim->kb))) {
        report_error("sim: create: could not create keyboard controller");
        simulator_destroy(sim);
        return FALSE;
    }

    if (unlikely(!mouse_create(&sim->ms))) {
        report_error("sim: create: could not create mouse controller");
        simulator_destroy(sim);
        return FALSE;
    }

    sim->st = st;
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
    memset(sim->sr_banks, 0, NUM_BANK_SLOTS * sizeof(uint8_t));

    for (task = 0; task < TASK_NUM_TASKS; task++) {
        sim->task_mpc[task] = (uint16_t) task;
    }

    sim->error = FALSE;

    sim->t = 0;
    sim->l = 0;
    sim->m = 0;
    sim->mar = 0;
    sim->ir = 0;
    sim->mir = 0;
    sim->mpc = 0;
    sim->ctask = 0;
    sim->ntask = 0;
    sim->pending = (1 << TASK_EMULATOR);
    sim->aluC0 = FALSE;
    sim->skip = FALSE;
    sim->carry = FALSE;
    sim->dns = FALSE;
    sim->rmr = 0xFFFF;
    sim->mem_cycle = 0;
    sim->mem_task = TASK_EMULATOR;
    sim->mem_low = 0xFFFF;
    sim->mem_high = 0xFFFF;
    sim->mem_extended = 0;
    sim->mem_which = 0;
    sim->cycle = 0;
}

uint16_t simulator_read(const struct simulator *sim, uint16_t address,
                        uint8_t task, int extended_memory)
{
    if (address >= XM_BANK_START
        && address <= (XM_BANK_START + NUM_BANK_SLOTS)) {
        /* NB: While not specified in documentation, some code (IFS in
         * particular) relies on the fact that the upper 12 bits of the
         * bank registers are all 1s.
         */
        return ((uint16_t) 0xFFF0) | sim->xm_banks[address - XM_BANK_START];
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
    if (address >= XM_BANK_START
        && address <= (XM_BANK_START + NUM_BANK_SLOTS)) {
        /* NB: While not specified in documentation, some code (IFS in
         * particular) relies on the fact that the upper 12 bits of the
         * bank registers are all 1s.
         */
        sim->xm_banks[address - XM_BANK_START] = data;
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

/* Obtains the RSEL value (which can be modified by ACSOURCE and ACDEST).
 * Returns the RSEL value.
 */
static
uint16_t get_rsel(struct simulator *sim)
{
    uint32_t mir;
    uint16_t ctask;
    uint16_t rsel;
    uint16_t f2;
    uint16_t ir;

    mir = sim->mir;
    ctask = sim->ctask;

    rsel = MICROCODE_RSEL(mir);
    f2 = MICROCODE_F2(mir);

    if (ctask == TASK_EMULATOR) {
        ir = sim->ir;
        /* Modify the last 3 bits according to the
         * corresponding field in the IR register.
         */
        if (f2 == F2_EMU_ACSOURCE) {
            rsel &= ~0x3;
            rsel |= (~(ir >> 13)) & 0x3;
        } else if (f2 == F2_EMU_ACDEST || f2 == F2_EMU_LOAD_DNS) {
            rsel &= ~0x3;
            rsel |= (~(ir >> 11)) & 0x3;
        }
    }

    return rsel;
}

/* Auxiliary function to obtain the value of the bus.
 * The parameter `rsel` specifies the modified RSEL value.
 * Returns the value of the bus.
 */
static
uint16_t read_bus(struct simulator *sim, uint16_t rsel)
{
    uint32_t mir;
    uint16_t ctask;
    uint16_t bs;
    uint16_t f1, f2;
    uint16_t output;
    uint16_t t;
    uint8_t rb;

    mir = sim->mir;
    ctask = sim->ctask;

    bs = MICROCODE_BS(mir);
    f1 = MICROCODE_F1(mir);
    f2 = MICROCODE_F2(mir);

    if (f1 == F1_CONSTANT || f2 == F2_CONSTANT)
        return sim->consts[CONST_ADDR(rsel, bs)];

    if (BS_USE_CROM(bs)) {
        output = sim->consts[CONST_ADDR(rsel, bs)];
    } else {
        output = 0xFFFF;
    }

    switch (bs) {
    case BS_READ_R:
        output &= sim->r[rsel];
        break;
    case BS_LOAD_R:
        /* The load is performed at the end, for now set the
         * bus to zero.
         */
        output &= 0;
        break;
    case BS_NONE:
        if (ctask == TASK_EMULATOR && f1 == F1_EMU_RSNF) {
            output &= (sim->eth.address & 0xFF00);
        } else if (ctask == TASK_ETHERNET) {
            /* Implement this. */
            if (f1 == F1_ETH_EILFCT) {
            } else if (f1 == F1_ETH_EPFCT) {
            }
        }
        break;
    case BS_READ_MD:
        /* TODO: check for delays. */
        output = (sim->mem_which) ? sim->mem_high : sim->mem_low;
        sim->mem_which = (1 ^ sim->mem_which);
        break;
    case BS_READ_MOUSE:
        output &= (0xfff0 & mouse_poll_bits(&sim->ms));
        break;
    case BS_READ_DISP:
        t = sim->ir & 0x00FF;
        if ((sim->ir & 0x300) != 0 && (sim->ir & 0x80) != 0) {
            t |= 0xFF00;
        }
        output &= t;
        break;

    default:
        if ((1 << ctask) & TASK_RAM_MASK) {
            rb = sim->sr_banks[ctask];
            if (bs == BS_RAM_READ_S_LOCATION) {
                if (rsel == 0) {
                    output &= sim->m;
                } else {
                    output &= sim->s[rb * NUM_R_REGISTERS + rsel];
                }
                break;
            } else if (bs == BS_RAM_LOAD_S_LOCATION) {
                /* To be handled later. */
                break;
            }
        } else if (ctask == TASK_ETHERNET && bs == BS_ETH_EIDFCT) {
            /* Implement this. */
            output &= 0xFFFF;
            break;
        } else if ((ctask == TASK_DISK_SECTOR)
                   || (ctask == TASK_DISK_WORD)) {
            /* Implement this. */
            if (bs == BS_DSK_READ_KSTAT) {
                output &= 0xFFFF;
                break;
            } else if (bs == BS_DSK_READ_KDATA) {
                output &= 0xFFFF;
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

/* Auxiliary function to perform the ALU computation. */
static
uint16_t compute_alu(struct simulator *sim, uint16_t bus, int *carry)
{
    uint32_t res;
    uint32_t a, b;
    uint16_t aluf;

    aluf = MICROCODE_ALUF(sim->mir);

    a = (uint32_t) bus;
    b = (uint32_t) sim->t;

    switch (aluf) {
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
        res = a + 0xFFFF;
        break;
    case ALU_BUS_PLUS_T:
        res = a + b;
        break;
    case ALU_BUS_MINUS_T:
        res = a + ((~b) & 0xFFFF) + 1;
        break;
    case ALU_BUS_MINUS_T_MINUS_1:
        res = a + ((~b) & 0xFFFF);
        break;
    case ALU_BUS_PLUS_T_PLUS_1:
        res = a + b + 1;
        break;
    case ALU_BUS_PLUS_SKIP:
        res = a + ((uint32_t) (sim->skip ? 1 : 0));
        break;
    case ALU_BUS_AND_NOT_T:
        res = a & (~b) & 0xFFFF;
        break;
    default:
        report_error("simulator: step: "
                     "invalid ALUF = %o", aluf);
        sim->error = TRUE;
        *carry = 0;
        return 0xDEAD;
    }

    *carry = ((res & 0x10000) != 0);
    return (uint16_t) res;
}

/* Auxiliary function to perform the shift computation. */
static
uint16_t do_shift(struct simulator *sim, int *nova_carry)
{
    uint32_t mir;
    uint16_t f1, f2;
    uint16_t res;
    int has_magic;

    mir = sim->mir;
    f1 = MICROCODE_F1(mir);
    f2 = MICROCODE_F2(mir);

    has_magic = (f2 == F2_EMU_MAGIC);

    switch (f1) {
    case F1_LLSH1:
        res = sim->l << 1;
        if (has_magic) {
            res |= (sim->t & 0x8000) ? 1 : 0;
        } else if (sim->dns) {
            /* Nova style shift */
            res |= (*nova_carry) ? 1 : 0;
            *nova_carry = (sim->l & 0x8000) ? 1 : 0;
        }
        break;
    case F1_LRSH1:
        res = sim->l >> 1;
        if (has_magic) {
            res |= (sim->t & 1) ? 0x8000 : 0;
        } else if (sim->dns) {
            /* Nova style shift */
            res |= (nova_carry) ? 0x8000 : 0;
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

/* Performs the F1 function. */
static
void do_f1(struct simulator *sim, uint16_t bus,
           uint16_t alu, uint16_t shifter_output)
{
    uint32_t mir;
    uint16_t f1, f2;
    uint16_t addr;
    uint8_t ctask, tmp;

    mir = sim->mir;
    ctask = sim->ctask;

    f1 = MICROCODE_F1(mir);
    f2 = MICROCODE_F2(mir);

    switch (f1) {
    case F1_NONE:
        /* Nothing to do. */
        break;
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
                             * update_program_counters() to 1, which
                             * is the correct value.
                             */
        sim->mem_task = sim->ctask;
        sim->mem_low = 0xFFFF;
        sim->mem_high = 0xFFFF;
        sim->mem_extended = (sim->st != ALTO_I)
                          ? (f2 == F2_STORE_MD) : FALSE;
        sim->mem_which = 0;

        /* Perform the reading now. */
        addr = sim->mar;
        sim->mem_low = simulator_read(sim, addr, sim->mem_task,
                                      sim->mem_extended);

        addr = (sim->st == ALTO_I) ? 1 | addr : 1 ^ addr;
        sim->mem_high = simulator_read(sim, addr, sim->mem_task,
                                       sim->mem_extended);
        return;
    case F1_TASK:
        /* Switch tasks. */
        /* TODO: Maybe prevent two consecutive switches? */
        for (tmp = TASK_NUM_TASKS; tmp--;) {
            if (sim->pending & (1 << tmp)) {
                sim->ntask = tmp;
                break;
            }
        }
        return;
    case F1_BLOCK:
        if (ctask == TASK_EMULATOR) {
            report_error("simulator: step: "
                         "emulator task cannot block");
            sim->error = TRUE;
            return;
        }

        /* Prevent the current task from running. */
        sim->pending &= ~(1 << ctask);

        /* There are other side effects to consider for specific tasks. */
        return;
    }

    switch (ctask) {
    case TASK_EMULATOR:
        switch (f1) {
        case F1_EMU_SWMODE:
            break;
        case F1_EMU_WRTRAM:
            break;
        case F1_EMU_RDRAM:
            break;
        case F1_EMU_LOAD_RMR:
            sim->rmr = bus;
            break;
        case F1_EMU_LOAD_ESRB:
            tmp = (uint8_t) ((bus >> 1) & 0x7);
            if (sim->st != ALTO_II_3KRAM)
                tmp = 0;
            sim->sr_banks[ctask] = tmp;
            break;
        case F1_EMU_RSNF:
            /* Already handled. */
            break;
        case F1_EMU_STARTF:
            break;
        default:
            if (f1 > F1_CONSTANT) {
                report_error("simulator: step: "
                             "invalid F1 function %o for emulator",
                         f1);
                sim->error = TRUE;
                return;
            }
        }
        break;
    }
}

/* Performs the F2 function.
 * Retuns the bits that should be modified for the NEXT part of the
 * following instruction.
 */
static
uint16_t do_f2(struct simulator *sim, uint16_t bus,
               uint16_t alu, uint16_t shifter_output)
{
    uint32_t mir;
    uint16_t f1, f2;
    uint16_t next_extra;
    uint16_t addr;
    uint8_t ctask;

    mir = sim->mir;
    ctask = sim->ctask;

    f1 = MICROCODE_F1(mir);
    f2 = MICROCODE_F2(mir);

    /* Computes the F2 function. */
    switch (f2) {
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
        if (f1 != F1_LOAD_MAR || sim->st == ALTO_I) {
            addr = sim->mar;
            if (sim->mem_which) {
                addr = (sim->st == ALTO_I) ? 1 | addr : 1 ^ addr;
            }
            simulator_write(sim, addr, bus, sim->mem_task,
                            sim->mem_extended);
            sim->mem_which = (1 ^ sim->mem_which);
        }
        return 0;
    }

    switch (ctask) {
    case TASK_EMULATOR:
        switch (f2) {
        case F2_EMU_MAGIC:
        case F2_EMU_ACDEST:
            /* Already handled. */
            break;
        case F2_EMU_BUSODD:
            return (bus & 1);
        case F2_EMU_LOAD_DNS:
            break;
        case F2_EMU_LOAD_IR:
            sim->ir = bus;
            sim->skip = FALSE;
            next_extra = (bus >> 8) & 0x7;
            if (bus & 0x8000) next_extra |= 0x8;
            return next_extra;
        case F2_EMU_IDISP:
            break;
        case F2_EMU_ACSOURCE:
            break;
        }
        break;
    }

    return 0;
}

/* Writes back the registers. */
static
void wb_registers(struct simulator *sim,
                  uint16_t rsel, uint16_t bus, uint16_t alu,
                  uint16_t shifter_output, int alu_carry)
{
    uint32_t mir;
    uint16_t aluf;
    uint16_t bs;
    uint16_t f1, f2;
    uint8_t ctask, rb;
    int load_l;
    int load_t;
    int use_constant;

    mir = sim->mir;
    ctask = sim->ctask;

    aluf = MICROCODE_ALUF(mir);
    bs = MICROCODE_BS(mir);
    f1 = MICROCODE_F1(mir);
    f2 = MICROCODE_F2(mir);
    load_l = MICROCODE_L(mir);
    load_t = MICROCODE_T(mir);

    /* Writes back the results to the registers. */

    /* Writes back the R register. */
    use_constant = (f1 == F1_CONSTANT) || (f2 == F2_CONSTANT);
    if (!use_constant) {
        if (bs == BS_LOAD_R) {
            sim->r[rsel] = shifter_output;
        } else if (((1 << ctask) & TASK_RAM_MASK)
                   && bs == BS_RAM_LOAD_S_LOCATION) {
            rb = sim->sr_banks[ctask];
            sim->s[rb * NUM_R_REGISTERS + rsel] = sim->m;
        }
    }

    /* Writes back the L, M and ALUC0 registers. */
    if (load_l) {
        sim->l = alu;
        if (ctask == TASK_EMULATOR) {
            sim->m = alu;
        }
        sim->aluC0 = alu_carry;
    }

    /* Writes back the T register. */
    if (load_t) {
        if (LOAD_T_FROM_ALU(aluf)) {
            sim->t = alu;
        } else {
            sim->t = bus;
        }
    }
}

/* Updates the micro program counter and the current task.
 * The bits that are to be modified in the NEXT field of the following
 * instruction are given by `next_extra`.
 */
static
void update_program_counters(struct simulator *sim, uint16_t next_extra)
{
    uint32_t mcode;
    uint16_t mpc;
    uint8_t ctask;

    /* Updates the MPC and MIR. */
    ctask = sim->ctask;
    mpc = sim->task_mpc[ctask];
    mcode = sim->microcode[mpc];
    sim->task_mpc[ctask] = (mpc & MPC_BANK_MASK)
        | MICROCODE_NEXT(mcode) | next_extra;

    sim->mir = mcode;
    sim->mpc = mpc;

    /* Updates the current task. */
    sim->ctask = sim->ntask;
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

void simulator_step(struct simulator *sim)
{
    uint16_t rsel;
    uint16_t bus;
    uint16_t alu;
    uint16_t shifter_output;
    uint16_t next_extra;
    int alu_carry;
    int nova_carry;

    if (sim->error) {
        report_error("simulator: step: "
                     "simulator is in error state");
        return;
    }

    /* Obtain the rsel (which might be modified by some F2
     * functions when in the EMULATOR task.
     */
    rsel = get_rsel(sim);

    /* Compute the bus. */
    bus = read_bus(sim, rsel);
    if (sim->error) return;

    /* Compute the ALU. */
    alu = compute_alu(sim, bus, &alu_carry);
    if (sim->error) return;

    /* Compute the shifter output. */
    nova_carry = sim->carry;
    shifter_output = do_shift(sim, &nova_carry);

    /* Compute the F1 function. */
    do_f1(sim, bus, alu, shifter_output);
    if (sim->error) return;

    /* Compute the F2 function. */
    next_extra = do_f2(sim, bus, alu, shifter_output);
    if (sim->error) return;

    /* Write back the registers. */
    wb_registers(sim, rsel, bus, alu, shifter_output, alu_carry);

    /* Update the micro program counter and the current task. */
    update_program_counters(sim, next_extra);
}

/* Auxiliary function used by simulator_disassemble().
 * Callback to print constants.
 */
static
void sim_constant_cb(const struct decoder *dec, uint16_t val,
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
void sim_register_cb(const struct decoder *dec, uint16_t val,
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
void sim_goto_cb(const struct decoder *dec, uint16_t val,
                 struct decode_buffer *output)
{
    decode_buffer_print(output, ":%05o", val);
}

void simulator_disassemble(struct simulator *sim,
                           char *output, size_t output_size)
{
    struct decoder dec;
    struct decode_buffer out;

    out.buf = output;
    out.buf_size = output_size;
    decode_buffer_reset(&out);

    decode_buffer_print(&out,
                        "%02o-%06o %011o --- ",
                        sim->ctask, sim->mpc, sim->mir);

    dec.address = sim->mpc;
    dec.microcode = sim->mir;
    dec.task = sim->ctask;
    dec.arg = sim;
    dec.const_cb = &sim_constant_cb;
    dec.reg_cb = &sim_register_cb;
    dec.goto_cb = &sim_goto_cb;

    decoder_decode(&dec, &out);
}

void simulator_print_registers(struct simulator *sim,
                               char *output, size_t output_size)
{
    struct decode_buffer out;
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
                        "SKIP : %-6o   DNS  : %-6o\n",
                        sim->aluC0 ? 1 : 0,
                        sim->carry ? 1 : 0,
                        sim->skip ? 1 : 0,
                        sim->dns ? 1 : 0);

    decode_buffer_print(&out,
                        "XM_B : %06o   SR_B : %03o      "
                        "PEND : %06o   RMR  : %06o\n",
                        sim->xm_banks[sim->ctask],
                        sim->sr_banks[sim->ctask],
                        sim->pending, sim->rmr);

    decode_buffer_print(&out,
                        "CYCLE: %lu",
                        sim->cycle);

    if (sim->error) {
        decode_buffer_print(&out, "\nsimulator in error state");
    }
}
