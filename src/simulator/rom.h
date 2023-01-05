
#ifndef __SIMULATOR_ROM_H
#define __SIMULATOR_ROM_H

#include <stdint.h>
#include "microcode/microcode.h"

/* Constants. */
#define ACSROM_SIZE                      256

/* Declaration of the global variables. */

/* Table used to implement F2_EMU_ACSOURCE and F2_EMU_IDISP. */
extern const uint8_t ACSROM[ACSROM_SIZE];

/* The constant ROM (for both Alto I and Alto II). */
extern const uint16_t CROM[CONSTANT_SIZE];

/* Microcode ROM for the Alto I. */
extern const uint32_t UROM1[MICROCODE_SIZE];

/* Microcode ROM for the Alto II (resident in ROM0). */
extern const uint32_t UROM2_0[MICROCODE_SIZE];

/* Microcode ROM for the Alto II with 2K rom (resident in ROM1). */
extern const uint32_t UROM2_1[MICROCODE_SIZE];

#endif /* __SIMULATOR_ROM_H */
