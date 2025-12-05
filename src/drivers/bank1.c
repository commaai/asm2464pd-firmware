/*
 * ASM2464PD Firmware - Bank 1 Functions
 *
 * Bank 1 contains error handling and extended functionality that resides
 * in the second code bank (file offset 0x10000-0x17F0C).
 *
 * CODE BANKING:
 *   The ASM2464PD has ~98KB of firmware but the 8051 only addresses 64KB.
 *   Bank 1 is accessed by setting DPX=1, which maps CPU addresses 0x8000-0xFFFF
 *   to file offset 0x10000-0x17F0C.
 *
 * DISPATCH MECHANISM:
 *   Bank 1 functions are called via jump_bank_1 (0x0311):
 *   1. Caller loads DPTR with target address (e.g., 0xE911)
 *   2. Caller does ajmp 0x0311
 *   3. jump_bank_1 pushes DPTR, sets DPX=1, R0=0x1B
 *   4. RET pops DPTR and jumps to target in bank 1
 *
 * FILE OFFSET CALCULATION:
 *   file_offset = cpu_addr + 0x8000
 *   Example: CPU 0xE911 -> file 0x16911
 *
 * HANDLER TARGETS:
 *   The dispatch targets are often mid-function jump points, not function starts.
 *   This allows shared error handling code to be entered at different points
 *   depending on the error type.
 *
 * Known Bank 1 Dispatch Targets:
 *   0xE911 - Called by handler_0570 (PCIe/NVMe error, file 0x16911)
 *   0xE56F - Called by handler_0494 (event error, file 0x1656F)
 *   0xB230 - Called by handler_0606 (error handler, file 0x13230)
 *   0xA066 - Called by handler_061a (file 0x12066)
 *   0xEF4E - Called by handler_0642 (system error, file 0x16F4E)
 *   0xEDBD - (file 0x16DBD)
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

/*
 * Note on reversing Bank 1 functions:
 *
 * When reversing a bank 1 function, use radare2 with:
 *   r2 -a 8051 -q -c 's <file_offset>; pd 50' fw.bin
 *
 * Where file_offset = cpu_addr + 0x8000 for addresses >= 0x8000
 *
 * Example for 0xE911:
 *   r2 -a 8051 -q -c 's 0x16911; pd 50' fw.bin
 */

/* Bank 1 functions will be added here as they are reversed */

