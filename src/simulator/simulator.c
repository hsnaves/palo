#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "simulator/simulator.h"
#include "simulator/intr.h"
#include "microcode/microcode.h"
#include "microcode/nova.h"
#include "simulator/rom.h"
#include "common/serdes.h"
#include "common/utils.h"

/* Constants. */
#define NUM_R_REGISTERS                   32
#define NUM_S_REGISTERS             (8 * 32)

/* For the MPC. */
#define MPC_BANK_SHIFT                    10
#define MPC_BANK_MASK                  0x003
#define MPC_ADDR_MASK                  0x3FF

/* For the memory. */
#define NUM_MICROCODE_BANKS                4
#define NUM_BANKS                          4
#define NUM_BANK_SLOTS        TASK_NUM_TASKS
#define MEMORY_TOP                    0xFE00
#define XM_BANK_START                 0xFFE0
#define XM_BANK_END (XM_BANK_START + NUM_BANK_SLOTS)

/* For fixing the microcode in RAM. */
#define MC_INVERT_MASK            0x00088400

/* For memory access. */
#define MA_EXTENDED                        1
#define MA_HAS_STORE                       2

/* The state size when serializing. */
#define STATE_SIZE                    542426

/* Functions. */

void simulator_initvar(struct simulator *sim)
{
    sim->r = NULL;
    sim->s = NULL;
    sim->acs_rom = NULL;
    sim->consts = NULL;
    sim->microcode = NULL;
    sim->task_mpc = NULL;
    sim->task_cycle = NULL;
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

    if (sim->acs_rom) free((void *) sim->acs_rom);
    sim->acs_rom = NULL;

    if (sim->consts) free((void *) sim->consts);
    sim->consts = NULL;

    if (sim->microcode) free((void *) sim->microcode);
    sim->microcode = NULL;

    if (sim->task_mpc) free((void *) sim->task_mpc);
    sim->task_mpc = NULL;

    if (sim->task_cycle) free((void *) sim->task_cycle);
    sim->task_cycle = NULL;

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
    sim->acs_rom = (uint8_t *)
        malloc(ACSROM_SIZE * sizeof(uint8_t));
    sim->consts = (uint16_t *)
        malloc(CONSTANT_SIZE * sizeof(uint16_t));
    sim->microcode = (uint32_t *)
        malloc(NUM_MICROCODE_BANKS * MICROCODE_SIZE * sizeof(uint32_t));
    sim->task_mpc = (uint16_t *)
        malloc(TASK_NUM_TASKS * sizeof(uint16_t));
    sim->task_cycle = (int32_t *)
        malloc(TASK_NUM_TASKS * sizeof(int32_t));
    sim->mem = (uint16_t *)
        malloc(NUM_BANKS * MEMORY_SIZE * sizeof(uint16_t));
    sim->xm_banks = (uint16_t *)
        malloc(NUM_BANK_SLOTS * sizeof(uint16_t));
    sim->sreg_banks = (uint8_t *)
        malloc(NUM_BANK_SLOTS * sizeof(uint8_t));

    if (unlikely(!sim->r || !sim->s
                 || !sim->acs_rom || !sim->consts || !sim->microcode
                 || !sim->task_mpc || !sim->task_cycle
                 || !sim->mem || !sim->xm_banks
                 || !sim->sreg_banks)) {
        report_error("sim: create: could not allocate memory");
        simulator_destroy(sim);
        return FALSE;
    }

    /* Copy the ROMs into the simulator.
     * Note:  the constant ROM and the microcode ROM can be overwritten
     * later with the functions simulator_load_constant_rom() and with
     * simulator_load_microcode_rom().
     */
    memcpy(sim->acs_rom, ACSROM, ACSROM_SIZE * sizeof(uint8_t));
    memcpy(sim->consts, CROM, CONSTANT_SIZE * sizeof(uint16_t));
    if (sys_type == ALTO_I) {
        memcpy(sim->microcode, UROM1,
               MICROCODE_SIZE * sizeof(uint32_t));
    } else {
        memcpy(sim->microcode, UROM2_0,
               MICROCODE_SIZE * sizeof(uint32_t));
        if (sys_type == ALTO_II_2KROM) {
            memcpy(&sim->microcode[MICROCODE_SIZE], UROM2_1,
                   MICROCODE_SIZE * sizeof(uint32_t));
        }
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
    struct serdes sd;
    size_t size;

    size = CONSTANT_SIZE * sizeof(uint16_t);
    if (unlikely(!serdes_create(&sd, size, FALSE))) {
        report_error("simulator: load_constant_rom: "
                     "could not create deserializer");
        return FALSE;
    }

    if (unlikely(!serdes_read(&sd, filename))) {
        report_error("simulator: load_constant_rom: "
                     "could not read file");
        serdes_destroy(&sd);
        return FALSE;
    }

    if (unlikely(sd.pos != sd.size)) {
        report_error("simulator: load_constant_rom: "
                     "invalid rom file `%s`", filename);
        serdes_destroy(&sd);
        return FALSE;
    }

    serdes_rewind(&sd);
    serdes_get16_array(&sd, sim->consts, CONSTANT_SIZE);
    serdes_destroy(&sd);
    return TRUE;
}

int simulator_load_microcode_rom(struct simulator *sim,
                                 const char *filename, uint8_t bank)
{
    struct serdes sd;
    uint16_t offset;
    size_t size;

    if (unlikely(bank >= 2)) {
        report_error("simulator: load_microcode_rom: "
                     "invalid bank `%u`", bank);
        return FALSE;
    }

    offset = (bank) ? MICROCODE_SIZE : 0;

    size = MICROCODE_SIZE * sizeof(uint32_t);
    if (unlikely(!serdes_create(&sd, size, FALSE))) {
        report_error("simulator: load_microcode_rom: "
                     "could not create deserializer");
        return FALSE;
    }

    if (unlikely(!serdes_read(&sd, filename))) {
        report_error("simulator: load_microcode_rom: "
                     "could not read file");
        serdes_destroy(&sd);
        return FALSE;
    }

    if (unlikely(sd.pos != sd.size)) {
        report_error("simulator: load_microcode_rom: "
                     "invalid rom file `%s`", filename);
        serdes_destroy(&sd);
        return FALSE;
    }

    serdes_rewind(&sd);
    serdes_get32_array(&sd, &sim->microcode[offset], MICROCODE_SIZE);
    serdes_destroy(&sd);
    return TRUE;
}

void simulator_reset(struct simulator *sim)
{
    int32_t intr_cycles[3];
    uint8_t task;

    memset(sim->r, 0, NUM_R_REGISTERS * sizeof(uint16_t));
    memset(sim->s, 0, NUM_S_REGISTERS * sizeof(uint16_t));
    memset(sim->mem, 0, NUM_BANKS * MEMORY_SIZE * sizeof(uint16_t));
    memset(sim->xm_banks, 0, NUM_BANK_SLOTS * sizeof(uint16_t));
    memset(sim->sreg_banks, 0, NUM_BANK_SLOTS * sizeof(uint8_t));

    for (task = 0; task < TASK_NUM_TASKS; task++) {
        sim->task_mpc[task] = (uint16_t) task;
        sim->task_cycle[task] = 0;
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
    sim->task_switch = TRUE;

    sim->aluC0 = FALSE;
    sim->skip = FALSE;
    sim->carry = FALSE;

    sim->rmr = 0xFFFFU;
    sim->cram_addr = 0x0;
    sim->rdram = FALSE;
    sim->wrtram = FALSE;
    sim->soft_reset = FALSE;

    sim->cycle = 0;
    sim->mem_cycle = 0xFFFFU;
    sim->mem_task = TASK_EMULATOR;
    sim->mem_low = 0xFFFFU;
    sim->mem_high = 0xFFFFU;
    sim->mem_status = 0;

    /* Sets the next interrupt cycle. */
    intr_cycles[0] = sim->dsk.intr_cycle;
    intr_cycles[1] = sim->displ.intr_cycle;
    intr_cycles[2] = sim->ether.intr_cycle;

    sim->intr_cycle = compute_intr_cycle(0, 3, intr_cycles);
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

/* Updates the simulator and memory cycles. */
static
void update_cycles(struct simulator *sim)
{
    uint8_t task;

    sim->cycle++;
    sim->cycle &= 0x7FFFFFFF;

    task = sim->ctask;
    sim->task_cycle[task]++;
    sim->task_cycle[task] &= 0x7FFFFFFF;

    /* Updates the memory cycle. */
    if (sim->mem_cycle != 0xFFFF) {
        if (sim->mem_cycle >= 10) {
            sim->mem_cycle = 0xFFFF;
        } else {
            sim->mem_cycle += 1;
        }
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

    if (mc->task == TASK_EMULATOR && mc->f1 == F1_EMU_RSNF) {
        output &= ethernet_rsnf(&sim->ether);
    } else if (mc->task == TASK_ETHERNET) {
        if (mc->f1 == F1_ETH_EILFCT) {
            output &= ethernet_eilfct(&sim->ether);
        } else if (mc->f1 == F1_ETH_EPFCT) {
            output &= ethernet_epfct(&sim->ether);
        }
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
        break;
    case BS_READ_MD:
        /* Wait until cycle 5 to perform the read. */
        while (sim->mem_cycle < 5) {
            update_cycles(sim);
        }

        if (mc->sys_type == ALTO_I) {
            if (sim->mem_cycle == 5) {
                output &= sim->mem_low;
            } else if (sim->mem_cycle == 6) {
                output &= sim->mem_high;
            } else {
                report_error("simulator: step: "
                             "unexpected read memory cycle");
                sim->error = TRUE;
                return 0;
            }
        } else {
            /* Alto II. */
            if (sim->mem_cycle == 5) {
                if (sim->mem_status & MA_HAS_STORE) {
                    output &= sim->mem_high;
                } else {
                    output &= sim->mem_low;
                }
            } if (sim->mem_cycle == 6) {
                output &= sim->mem_high;
            } else {
                output &= sim->mem_low;
            }
        }
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
                output &= 0xFFFF;
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
                     "invalid ALUF = %03o", mc->aluf);
        sim->error = TRUE;
        *carry = 0;
        return 0xDEAD;
    }

    *carry = ((res & 0xFFFF0000) != 0) ? 1 : 0;
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

    if (mc->task == TASK_EMULATOR) {
        dns = (mc->f2 == F2_EMU_LOAD_DNS);
        has_magic = (mc->f2 == F2_EMU_MAGIC);
    } else {
        dns = FALSE;
        has_magic = FALSE;
    }

    if (dns) {
        *load_r = ((sim->ir & 0x0008) == 0);

        switch ((sim->ir >> 4) & 3) {
        case 0: /* not affected. */
            carry = sim->carry;
            break;
        case 1: /* Z */
            carry = 0;
            break;
        case 2: /* O */
            carry = 1;
            break;
        case 3: /* C */
            carry = !(sim->carry);
            break;
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
        *nova_carry = carry;
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
uint16_t get_pending(const struct simulator *sim)
{
    uint16_t pending;
    pending = (1 << TASK_EMULATOR);
    pending |= sim->dsk.pending;
    pending |= sim->displ.pending;
    pending |= sim->ether.pending;
    return pending;
}

/* Performs the F1 function.
 * The current predecoded microcode is in `mc`.
 * The value of the bus is in `bus`, and of the alu in `alu`.
 * The next task after the following microinstruction is returned
 * in  `nntask`. Lastly, the `swmode` parameter returns TRUE if
 * a SWMODE instruction was executed.
 */
static
void do_f1(struct simulator *sim, const struct microcode *mc,
           uint16_t bus, uint16_t alu, uint8_t *nntask, int *swmode)
{
    uint16_t addr;
    uint16_t pending;
    uint16_t min_cycles;
    uint8_t tmp;

    *nntask = sim->ntask;
    *swmode = FALSE;

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
        min_cycles = (mc->sys_type == ALTO_I) ? 7 : 5;
        while (sim->mem_cycle < min_cycles) {
            update_cycles(sim);
        }
        sim->mar = alu;
        sim->mem_cycle = 1;
        sim->mem_task = mc->task;
        sim->mem_status = 0;
        if (mc->sys_type != ALTO_I && (mc->f2 == F2_STORE_MD)) {
            sim->mem_status |= MA_EXTENDED;
        }

        /* Perform the reading now. */
        addr = sim->mar;
        sim->mem_low = simulator_read(sim, addr, sim->mem_task,
                                      sim->mem_status & MA_EXTENDED);

        addr = (mc->sys_type == ALTO_I) ? (1 | addr) : (1 ^ addr);
        sim->mem_high = simulator_read(sim, addr, sim->mem_task,
                                       sim->mem_status & MA_EXTENDED);

        /* Forr TASK_MEMORY_REFRESH, loading MAR with RSEL = 037 performs
         * a BLOCK.
         */
        if (mc->task == TASK_MEMORY_REFRESH) {
            if ((mc->sys_type == ALTO_I) && (mc->rsel == 037)) {
                sim->displ.pending &= ~(1 << mc->task);
            }
        }
        return;
    case F1_TASK:
        /* Should we not prevent two consecutive switches? */
        if (sim->task_switch) return;

        /* Switch tasks. */
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

        /* This is handled later to avoid race conditions
         * with F2 functions that check if the current
         * task is blocked (e.g., disk).
         */
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
            *swmode = TRUE;
            return;
        case F1_RAM_WRTRAM:
            sim->wrtram = TRUE;
            return;
        case F1_RAM_RDRAM:
            sim->rdram = TRUE;
            return;
        case F1_RAM_LOAD_SRB:
            if (mc->task == TASK_EMULATOR) break;
            tmp = (uint8_t) ((bus >> 1) & 0x7);
            if (mc->sys_type != ALTO_II_3KRAM)
                tmp = 0;
            sim->sreg_banks[mc->task] = tmp;
            return;
        }
    }

    switch (mc->task) {
    case TASK_EMULATOR:
        switch (mc->f1) {
        case F1_EMU_LOAD_RMR:
            sim->rmr = bus;
            break;
        case F1_EMU_LOAD_ESRB:
            tmp = (uint8_t) ((bus >> 1) & 0x7);
            if (mc->sys_type != ALTO_II_3KRAM)
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
                case 0x00:
                    /* Nothing to do here. */
                    break;
                case 0x01:
                case 0x02:
                case 0x03:
                    ethernet_startf(&sim->ether, bus);
                    break;

                case 0x04:
                    /* TODO: Implement this for Orbit. */
                    break;

                case 0x10:
                case 0x20:
                    /* TODO: Implement this for Trident. */
                    break;

                default:
                    report_error("simulator: step: "
                                 "invalid STARTF value: %o",
                                 bus);
                    return;
                }
            }
            break;
        default:
            report_error("simulator: step: "
                         "invalid F1 function %03o for emulator",
                         mc->f1);
            sim->error = TRUE;
            return;
        }
        break;

    case TASK_DISK_SECTOR:
    case TASK_DISK_WORD:
        switch (mc->f1) {
        case F1_DSK_STROBE:
            if (!disk_func_strobe(&sim->dsk, sim->cycle)) {
                report_error("simulator: step: "
                             "error on disk STROBE");
                sim->error = TRUE;
                return;
            }
            break;
        case F1_DSK_LOAD_KSTAT:
            disk_load_kstat(&sim->dsk, bus);
            break;
        case F1_DSK_INCRECNO:
            if (!disk_func_increcno(&sim->dsk)) {
                report_error("simulator: step: "
                             "error on disk INCRECNO");
                sim->error = TRUE;
                return;
            }
            break;
        case F1_DSK_CLRSTAT:
            disk_func_clrstat(&sim->dsk);
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
                         "invalid F1 function %03o for disk tasks",
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
                         "invalid F1 function %03o for ethernet",
                         mc->f1);
            sim->error = TRUE;
            return;
        }
        break;
    }
}

/* Performs the F2 function.
 * The current predecoded microcode is in `mc`.
 * The value of the bus is in `bus`, the shifter is in `shifter_output`,
 * and the value of the nova style carry is in `nova_carry`.
 * Retuns the bits that should be modified for the NEXT part of the
 * following instruction.
 */
static
uint16_t do_f2(struct simulator *sim, const struct microcode *mc,
               uint16_t bus, uint16_t shifter_output, int nova_carry)
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
        if (mc->f1 == F1_LOAD_MAR && mc->sys_type != ALTO_I) {
            /* On Alto II MAR<- and <-MD in the same microinstruction
             * becomes XMAR<-.
             */
            return 0;
        }

        addr = sim->mar;
        if (mc->sys_type == ALTO_I) {
            while (sim->mem_cycle < 5) {
                update_cycles(sim);
            }
            if (sim->mem_cycle == 5) {
                sim->mem_status |= MA_HAS_STORE;
            } else if (sim->mem_cycle == 6) {
                if (!(sim->mem_status & MA_HAS_STORE)) {
                    report_error("simulator: step: "
                                 "first write on cycle 6");
                    sim->error = TRUE;
                    return 0;
                }
                addr |= 1;
            } else {
                report_error("simulator: step: "
                             "unexpected write memory cycle");
                sim->error = TRUE;
                return 0;
            }
        } else {
            while (sim->mem_cycle < 3) {
                update_cycles(sim);
            }
            if (sim->mem_cycle == 3) {
                sim->mem_status |= MA_HAS_STORE;
            } else if (sim->mem_cycle == 4) {
                if (sim->mem_status & MA_HAS_STORE) {
                    addr ^= 1;
                }
            } else {
                report_error("simulator: step: "
                             "unexpected write memory cycle");
                sim->error = TRUE;
                return 0;
            }
        }
        simulator_write(sim, addr, bus, sim->mem_task,
                        sim->mem_status & MA_EXTENDED);
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
                next_extra = sim->acs_rom[((sim->ir >> 8) & 0x7F) + 0x80];
            }
            return next_extra;
        case F2_EMU_ACSOURCE:
            if (sim->ir & 0x8000) {
                next_extra = 3 - ((sim->ir >> 6) & 3);
            } else {
                next_extra = sim->acs_rom[(sim->ir >> 8) & 0x7F];
            }
            return next_extra;
        default:
            report_error("simulator: step: "
                         "invalid F2 function %03o for emulator",
                         mc->f2);
            sim->error = TRUE;
            return 0;
        }
        break;

    case TASK_DISK_SECTOR:
    case TASK_DISK_WORD:
        switch (mc->f2) {
        case F2_DSK_INIT:
            return disk_func_init(&sim->dsk, mc->task);
        case F2_DSK_RWC:
            return disk_func_rwc(&sim->dsk, mc->task);;
        case F2_DSK_RECNO:
            return disk_func_recno(&sim->dsk, mc->task);
        case F2_DSK_XFRDAT:
            return disk_func_xfrdat(&sim->dsk, mc->task);
        case F2_DSK_SWRNRDY:
            return disk_func_swrnrdy(&sim->dsk, mc->task);
        case F2_DSK_NFER:
            return disk_func_nfer(&sim->dsk, mc->task);
        case F2_DSK_STROBON:
            return disk_func_strobon(&sim->dsk, mc->task);
        default:
            report_error("simulator: step: "
                         "invalid F2 function %03o for disk tasks",
                         mc->f2);
            sim->error = TRUE;
            return 0;
        }
        break;

    case TASK_ETHERNET:
        switch (mc->f2) {
        case F2_ETH_EODFCT:
            if (unlikely(!ethernet_eodfct(&sim->ether, bus, sim->cycle))) {
                report_error("simulator: step: "
                             "could not perform EODFCT");
                sim->error = TRUE;
                return 0;
            }
            return 0;
        case F2_ETH_EOSFCT:
            ethernet_eosfct(&sim->ether);
            return 0;
        case F2_ETH_ERBFCT:
            return ethernet_erbfct(&sim->ether);
        case F2_ETH_EEFCT:
            ethernet_eefct(&sim->ether, sim->cycle);
            return 0;
        case F2_ETH_EBFCT:
            return ethernet_ebfct(&sim->ether);
        case F2_ETH_ECBFCT:
            return ethernet_ecbfct(&sim->ether);
        case F2_ETH_EISFCT:
            ethernet_eisfct(&sim->ether, sim->cycle);
            return 0;
        default:
            report_error("simulator: step: "
                         "invalid F2 function %03o for ethernet",
                         mc->f2);
            sim->error = TRUE;
            return 0;
        }
        break;

    case TASK_DISPLAY_WORD:
        switch (mc->f2) {
        case F2_DW_LOAD_DDR:
            if (unlikely(!display_load_ddr(&sim->displ, bus))) {
                report_error("simulator: step: "
                             "could not load DDR register");
                sim->error = TRUE;
                return 0;
            }
            return 0;
        default:
            report_error("simulator: step: "
                         "invalid F2 function %03o for display word",
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
                         "invalid F2 function %03o for cursor",
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
                         "invalid F2 function %03o for display horizontal",
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
                         "invalid F2 function %03o for display vertical",
                         mc->f2);
            sim->error = TRUE;
            return 0;
        }
        break;

    default:
        report_error("simulator: step: "
                     "invalid F2 function %03o",
                     mc->f2);
        sim->error = TRUE;
        return 0;
    }

    return 0;
}

/* Performas a BLOCK. */
static
void do_block(struct simulator *sim, uint8_t task)
{
    disk_block_task(&sim->dsk, task);
    display_block_task(&sim->displ, task);
    ethernet_block_task(&sim->ether, task);
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
 * instruction is given by `nntask`. If the SWMODE instruction was
 * executed before this one, the flag `swmode` is set to TRUE.
 */
static
void update_program_counters(struct simulator *sim,
                             uint16_t next_extra, uint8_t nntask,
                             int swmode)
{
    uint32_t mcode;
    uint16_t mpc, next_addr, bank;
    uint8_t task;

    /* Updates the current task. */
    sim->task_switch = (sim->ctask != sim->ntask);
    sim->ctask = sim->ntask;

    /* Updates the MPC and MIR. */
    task = sim->ctask;
    mpc = sim->task_mpc[task];
    mcode = sim->microcode[mpc];

    next_addr = MICROCODE_NEXT(mcode) | next_extra;
    bank = (mpc >> MPC_BANK_SHIFT) & MPC_BANK_MASK;
    if (swmode) {
        switch (sim->sys_type) {
        case ALTO_I:
        case ALTO_II_1KROM:
            bank ^= 1; /* ROM0 <-> ROM1 */
            break;

        case ALTO_II_2KROM:
            switch (bank) {
            case 0: /* ROM0 */
                /* ROM1 or RAM0 */
                bank = (next_addr & 0x100) ? 1 : 2; break;
            case 1: /* ROM1 */
                /* RAM0 or ROM0 */
                bank = (next_addr & 0x100) ? 2 : 0; break;
            case 2: /* RAM0 */
                /* ROM1 or ROM0 */
                bank = (next_addr & 0x100) ? 1 : 0; break;
            }
            break;

        case ALTO_II_3KRAM:
            if (next_addr & 0x100) {
                switch (bank) {
                case 0: /* ROM0 */
                    /* RAM0 or RAM1 */
                    bank = (next_addr & 0x80) ? 1 : 2;
                    break;
                case 1: /* RAM0 */
                    bank = 2; /* RAM1 */
                    break;
                case 2: /* RAM1 */
                case 3: /* RAM2 */
                    bank = 1; /* RAM0 */
                    break;
                }
            } else {
                switch (bank) {
                case 0: /* ROM0 */
                    /* RAM2 or RAM0 */
                    bank = (next_addr & 0x80) ? 3 : 1;
                    break;
                case 1: /* RAM0 */
                case 2: /* RAM1 */
                    /* RAM2 or ROM0 */
                    bank = (next_addr & 0x80) ? 3 : 0;
                    break;
                case 3: /* RAM2 */
                    /* RAM1 or ROM0 */
                    bank = (next_addr & 0x80) ? 2 : 0;
                    break;
                }
            }
            break;
        }
    }
    sim->task_mpc[task] = (bank << MPC_BANK_SHIFT) | next_addr;

    sim->mir = mcode;
    sim->mpc = mpc;

    /* Updates the next task. */
    sim->ntask = nntask;

    if (!sim->task_switch) return;

    if (sim->ctask == TASK_DISPLAY_WORD
        || sim->ctask == TASK_DISPLAY_HORIZONTAL
        || sim->ctask == TASK_DISPLAY_VERTICAL
        || sim->ctask == TASK_CURSOR) {

        /* Dispatches the "on switch task" event to the display
         * controller.
         */
        display_on_switch_task(&sim->displ, sim->ctask);
    } else if (sim->ctask == TASK_DISK_SECTOR
               || sim->ctask == TASK_DISK_WORD) {

        /* Dispatches the "on switch task" event to the disk
         * controller.
         */
        disk_on_switch_task(&sim->dsk, sim->ctask);
    }
}

/* Performs a soft reset. */
static
void do_soft_reset(struct simulator *sim)
{
    uint16_t addr, bank;
    uint8_t task;

    memset(sim->xm_banks, 0, NUM_BANK_SLOTS * sizeof(uint16_t));

    if (sim->sys_type == ALTO_II_2KROM) {
        bank = 2;
    } else {
        bank = 1;
    }

    for (task = 0; task < TASK_NUM_TASKS; task++) {
        if ((1 << task) & sim->rmr) {
            addr = (uint16_t) task;
        } else {
            addr = (bank << MPC_BANK_SHIFT);
            addr |= (uint16_t) task;
        }
        sim->task_mpc[task] = addr;
    }

    sim->ctask = TASK_EMULATOR;
    sim->ntask = TASK_EMULATOR;
    sim->mpc = sim->task_mpc[sim->ctask];
    sim->mir = sim->microcode[sim->mpc];
    bank = (sim->mpc >> MPC_BANK_SHIFT) & MPC_BANK_MASK;
    sim->task_mpc[sim->ctask] = (bank << MPC_BANK_SHIFT)
        | MICROCODE_NEXT(sim->mir);

    /* This is a hack copied from ContrAlto source code. */
    sim->dsk.pending |= (1 << TASK_DISK_SECTOR);
    sim->dsk.pending &= ~(1 << TASK_DISK_WORD);
    sim->rmr = 0xFFFF;
}

/* Checks for interrupts.
 * The parameter `prev_cycle` records the number of cycles before
 * the execution of the last microinstruction.
 */
static
void check_for_interrupts(struct simulator *sim, int32_t prev_cycle)
{
    int32_t intr_cycles[3];
    int32_t diff, intr_diff;

    while (TRUE) {
        if (sim->intr_cycle < 0) return;

        diff = sim->cycle - prev_cycle;
        intr_diff = sim->intr_cycle - prev_cycle;
        if (diff <= intr_diff) break;

        /* Updates prev_cycle to match the current interrupt time. */
        prev_cycle += intr_diff;

        /* Dispatch the interrupts. */
        if (sim->intr_cycle == sim->dsk.intr_cycle) {
            disk_interrupt(&sim->dsk);
        }
        if (sim->intr_cycle == sim->displ.intr_cycle) {
            display_interrupt(&sim->displ);
            /* Transfer the TASK_ETHERNET pending bit
             * to the ethernet object.
             */
            if (sim->displ.pending & (1 << TASK_ETHERNET)) {
                sim->displ.pending &= ~(1 << TASK_ETHERNET);
                if (sim->ether.countdown_wakeup) {
                    sim->ether.pending |= (1 << TASK_ETHERNET);
                }
            }
        }
        if (sim->intr_cycle == sim->ether.intr_cycle) {
            ethernet_interrupt(&sim->ether);
        }

        /* Computes the next interrupt cycle. */
        intr_cycles[0] = sim->dsk.intr_cycle;
        intr_cycles[1] = sim->displ.intr_cycle;
        intr_cycles[2] = sim->ether.intr_cycle;

        sim->intr_cycle = compute_intr_cycle(prev_cycle, 3, intr_cycles);
        if (sim->intr_cycle == prev_cycle) {
            report_error("simulator: step: intr_cycle did not advance");
            sim->error = TRUE;
            return;
        }
    }
}

void simulator_step(struct simulator *sim)
{
    struct microcode mc;
    int32_t prev_cycle;
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

    /* Updates the cycles. */
    update_cycles(sim);

    /* The Ethernet cycle needs to run ethernet_before_step() before*
     * every step.
     */
    if (sim->ctask == TASK_ETHERNET) {
        ethernet_before_step(&sim->ether);
    }

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
    do_f1(sim, &mc, bus, alu, &nntask, &swmode);
    if (sim->error) return;

    /* Compute the F2 function. */
    next_extra = do_f2(sim, &mc, bus, shifter_output, nova_carry);
    if (sim->error) return;

    /* Perform the BLOCK operation. */
    if (mc.f1 == F1_BLOCK) do_block(sim, mc.task);

    /* Write back the registers. */
    wb_registers(sim, &mc, modified_rsel, load_r,
                 bus, alu, shifter_output, aluC0);

    /* Update the micro program counter and the next task. */
    update_program_counters(sim, next_extra, nntask, swmode);

    /* Perform the soft reset. */
    if (soft_reset) do_soft_reset(sim);

    check_for_interrupts(sim, prev_cycle);
}

int simulator_update(struct simulator *sim,
                     const struct keyboard *keyb,
                     const struct mouse *mous,
                     uint8_t *display_data)
{
    if (display_data) {
        memcpy(display_data, sim->displ.display_data,
               DISPLAY_DATA_SIZE * sizeof(uint8_t));
    }
    if (keyb) {
        keyboard_update_from(&sim->keyb, keyb);
    }
    if (mous) {
        mouse_update_from(&sim->mous, mous);
    }
    return TRUE;
}

/* Auxiliary function used by simulator_disassemble().
 * Callback to print constants, registers, labels, etc.
 */
static
void disasm_decode_cb(struct decoder *dec,
                      enum decode_type dec_type, uint16_t val)
{
    const struct simulator *sim;
    struct string_buffer *output;

    sim = (const struct simulator *) dec->arg;
    output = dec->output;

    switch (dec_type) {
    case DECODE_CONST:
        string_buffer_print(output, "%o", sim->consts[val]);
        break;
    case DECODE_REG:
        if (val <= R_MASK) {
            string_buffer_print(output, "R%o", val);
        } else {
            string_buffer_print(output, "S%o", val & R_MASK);
        }
        break;
    case DECODE_LABEL:
        string_buffer_print(output, "%05o", val);
        break;
    case DECODE_MEMORY:
        string_buffer_print(output, "%07o", val);
        break;
    }
}

void simulator_disassemble(const struct simulator *sim,
                           struct string_buffer *output)
{
    struct microcode mc;
    struct decoder dec;

    microcode_predecode(&mc,
                        sim->sys_type,
                        sim->mpc,
                        sim->mir,
                        sim->ctask);

    string_buffer_print(output,
                        "%03o-%07o %012o --- ",
                        sim->ctask, sim->mpc, sim->mir);

    dec.arg = (void *) sim;
    dec.dec_cb = &disasm_decode_cb;
    dec.mc = mc;
    dec.output = output;

    decoder_decode(&dec);
}

void simulator_print_registers(const struct simulator *sim,
                               struct string_buffer *output)
{
    uint16_t pending;
    unsigned int i;

    string_buffer_print(output,
                        "T    : %07o    L    : %07o    "
                        "MAR  : %07o    IR   : %07o\n",
                        sim->t, sim->l, sim->mar, sim->ir);

    for (i = 0; i < NUM_R_REGISTERS; i++) {
        string_buffer_print(output,
                            "R%-4o: %07o",
                            i, sim->r[i]);
        if ((i % 4) == 3) {
            string_buffer_print(output, "\n");
        } else {
            string_buffer_print(output, "    ");
        }
    }

    pending = get_pending(sim);
    string_buffer_print(output,
                        "ALUC0: %-7o    CARRY: %-7o    "
                        "SKIP : %-7o    PEND : %07o\n",
                        sim->aluC0 ? 1 : 0,
                        sim->carry ? 1 : 0,
                        sim->skip ? 1 : 0,
                        pending);

    string_buffer_print(output,
                        "CYC  : %-10d ICYC : %-10d "
                        "TCYC : %-10d MCYC : %d\n",
                        sim->cycle, sim->intr_cycle,
                        sim->task_cycle[sim->ctask],
                        sim->mem_cycle);

    string_buffer_print(output,
                        "NTASK: %03o        TS   : %-7d    "
                        "NMPC : %07o    SYS  : %d\n",
                        sim->ntask,
                        sim->task_switch ? 1 : 0,
                        sim->task_mpc[sim->ctask],
                        sim->sys_type);

    if (sim->error) {
        string_buffer_print(output, "\nsimulator in error state");
    }
}

void simulator_print_extra_registers(const struct simulator *sim,
                                     struct string_buffer *output)
{
    unsigned int i;
    uint8_t rb;

    rb = sim->sreg_banks[sim->ctask];
    for (i = 0; i < NUM_R_REGISTERS; i++) {
        string_buffer_print(output,
                            "S%-4o: %07o",
                            i, sim->s[rb * NUM_R_REGISTERS + i]);
        if ((i % 4) == 3) {
            string_buffer_print(output, "\n");
        } else {
            string_buffer_print(output, "    ");
        }
    }

    string_buffer_print(output,
                        "M    : %07o    XM_B  : %03o        "
                        "SR_B : %07o    RMR : %07o\n",
                        sim->m,  sim->xm_banks[sim->ctask],
                        rb, sim->rmr);

    string_buffer_print(output,
                        "CRAM : %07o    RDR  : %-7d    "
                        "WRTR : %-7d    SRES : %d\n",
                        sim->cram_addr,
                        sim->rdram ? 1 : 0,
                        sim->wrtram ? 1 : 0,
                        sim->soft_reset ? 1 : 0);

    if (sim->error) {
        string_buffer_print(output, "\nsimulator in error state");
    }
}

void simulator_nova_disassemble(const struct simulator *sim,
                                struct string_buffer *output)
{
    struct nova_insn ni;
    struct nova_decoder ndec;
    uint16_t address, insn;

    address = sim->r[6];
    insn = simulator_read(sim, address, TASK_EMULATOR, FALSE);
    nova_insn_predecode(&ni, address, insn);

    string_buffer_print(output,
                        "%07o %07o --- ",
                        ni.address, ni.insn);

    nova_decoder_decode(&ndec, &ni, output);
}

void simulator_print_nova_registers(const struct simulator *sim,
                                    struct string_buffer *output)
{
    string_buffer_print(output,
                        "R0   : %06o     R1   : %06o     "
                        "R2   : %06o     R3   : %06o\n",
                        sim->r[3], sim->r[2],
                        sim->r[1], sim->r[0]);
    string_buffer_print(output,
                        "IR   : %06o     CARRY: %o\n",
                        sim->ir, sim-> carry ? 1 : 0);
}

void simulator_serialize(const struct simulator *sim, struct serdes *sd)
{
    serdes_put32(sd, (uint32_t) sim->sys_type);
    serdes_put_bool(sd, sim->error);
    serdes_put16_array(sd, sim->r, NUM_R_REGISTERS);
    serdes_put16_array(sd, sim->s, NUM_S_REGISTERS);
    serdes_put16(sd, sim->t);
    serdes_put16(sd, sim->l);
    serdes_put16(sd, sim->m);
    serdes_put16(sd, sim->mar);
    serdes_put16(sd, sim->ir);
    serdes_put16(sd, sim->mir);
    serdes_put16(sd, sim->mpc);
    serdes_put8(sd, sim->ctask);
    serdes_put8(sd, sim->ntask);
    serdes_put_bool(sd, sim->task_switch);
    serdes_put_bool(sd, sim->aluC0);
    serdes_put_bool(sd, sim->skip);
    serdes_put_bool(sd, sim->carry);
    serdes_put16(sd, sim->rmr);
    serdes_put16(sd, sim->cram_addr);
    serdes_put_bool(sd, sim->rdram);
    serdes_put_bool(sd, sim->wrtram);
    serdes_put_bool(sd, sim->soft_reset);
    serdes_put8_array(sd, sim->acs_rom, ACSROM_SIZE);
    serdes_put16_array(sd, sim->consts, CONSTANT_SIZE);
    serdes_put32_array(sd, sim->microcode,
                       NUM_MICROCODE_BANKS * MICROCODE_SIZE);
    serdes_put16_array(sd, sim->task_mpc, TASK_NUM_TASKS);
    serdes_put32(sd, sim->cycle);
    serdes_put32_array(sd, (const uint32_t *) sim->task_cycle,
                       TASK_NUM_TASKS);
    serdes_put32(sd, sim->intr_cycle);
    serdes_put16_array(sd, sim->mem, NUM_BANKS * MEMORY_SIZE);
    serdes_put16_array(sd, sim->xm_banks, NUM_BANK_SLOTS);
    serdes_put8_array(sd, sim->sreg_banks, NUM_BANK_SLOTS);
    serdes_put16(sd, sim->mem_cycle);
    serdes_put8(sd, sim->mem_task);
    serdes_put16(sd, sim->mem_low);
    serdes_put16(sd, sim->mem_high);
    serdes_put16(sd, sim->mem_status);
    disk_serialize(&sim->dsk, sd);
    display_serialize(&sim->displ, sd);
    ethernet_serialize(&sim->ether, sd);
    keyboard_serialize(&sim->keyb, sd);
    mouse_serialize(&sim->mous, sd);
}

void simulator_deserialize(struct simulator *sim, struct serdes *sd)
{
    sim->sys_type = (enum system_type) serdes_get32(sd);
    sim->error = serdes_get_bool(sd);
    serdes_get16_array(sd, sim->r, NUM_R_REGISTERS);
    serdes_get16_array(sd, sim->s, NUM_S_REGISTERS);
    sim->t = serdes_get16(sd);
    sim->l = serdes_get16(sd);
    sim->m = serdes_get16(sd);
    sim->mar = serdes_get16(sd);
    sim->ir = serdes_get16(sd);
    sim->mir = serdes_get16(sd);
    sim->mpc = serdes_get16(sd);
    sim->ctask = serdes_get8(sd);
    sim->ntask = serdes_get8(sd);
    sim->task_switch = serdes_get_bool(sd);
    sim->aluC0 = serdes_get_bool(sd);
    sim->skip = serdes_get_bool(sd);
    sim->carry = serdes_get_bool(sd);
    sim->rmr = serdes_get16(sd);
    sim->cram_addr = serdes_get16(sd);
    sim->rdram = serdes_get_bool(sd);
    sim->wrtram = serdes_get_bool(sd);
    sim->soft_reset = serdes_get_bool(sd);
    serdes_get8_array(sd, sim->acs_rom, ACSROM_SIZE);
    serdes_get16_array(sd, sim->consts, CONSTANT_SIZE);
    serdes_get32_array(sd, sim->microcode,
                       NUM_MICROCODE_BANKS * MICROCODE_SIZE);
    serdes_get16_array(sd, sim->task_mpc, TASK_NUM_TASKS);
    sim->cycle = serdes_get32(sd);
    serdes_get32_array(sd, (uint32_t *) sim->task_cycle,
                       TASK_NUM_TASKS);
    sim->intr_cycle = serdes_get32(sd);
    serdes_get16_array(sd, sim->mem, NUM_BANKS * MEMORY_SIZE);
    serdes_get16_array(sd, sim->xm_banks, NUM_BANK_SLOTS);
    serdes_get8_array(sd, sim->sreg_banks, NUM_BANK_SLOTS);
    sim->mem_cycle = serdes_get16(sd);
    sim->mem_task = serdes_get8(sd);
    sim->mem_low = serdes_get16(sd);
    sim->mem_high = serdes_get16(sd);
    sim->mem_status = serdes_get16(sd);
    disk_deserialize(&sim->dsk, sd);
    display_deserialize(&sim->displ, sd);
    ethernet_deserialize(&sim->ether, sd);
    keyboard_deserialize(&sim->keyb, sd);
    mouse_deserialize(&sim->mous, sd);
}

int simulator_save_state(const struct simulator *sim,
                         const char *filename)
{
    struct serdes sd;

    if (unlikely(!serdes_create(&sd, STATE_SIZE, FALSE))) {
        report_error("simulator: save_state: "
                     "could not create serializer");
        return FALSE;
    }

    simulator_serialize(sim, &sd);

    if (unlikely(sd.pos != sd.size)) {
        report_error("simulator: save_state: "
                     "invalid state size: "
                     "expecting %lu but got %lu",
                     sd.size, sd.pos);
        serdes_destroy(&sd);
        return FALSE;
    }

    if (unlikely(!serdes_write(&sd, filename))) {
        report_error("simulator: save_state: "
                     "could not write file");
        serdes_destroy(&sd);
        return FALSE;
    }

    serdes_destroy(&sd);
    return TRUE;
}

int simulator_load_state(struct simulator *sim,
                         const char *filename)
{
    struct serdes sd;

    if (unlikely(!serdes_create(&sd, STATE_SIZE, FALSE))) {
        report_error("simulator: load_state: "
                     "could not create deserializer");
        return FALSE;
    }

    if (unlikely(!serdes_read(&sd, filename))) {
        report_error("simulator: load_state: "
                     "could not read file");
        serdes_destroy(&sd);
        return FALSE;
    }

    if (unlikely(sd.pos != sd.size)) {
        report_error("simulator: load_state: "
                     "invalid state file `%s`", filename);
        serdes_destroy(&sd);
        return FALSE;
    }

    serdes_rewind(&sd);
    simulator_deserialize(sim, &sd);

    if (unlikely(sd.pos != sd.size)) {
        /* This should not happen. */
        report_error("simulator: load_state: "
                     "error in deserialization");
        serdes_destroy(&sd);
        return FALSE;
    }

    serdes_destroy(&sd);
    return TRUE;
}
