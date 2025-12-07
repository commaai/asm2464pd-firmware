/*
 * ASM2464PD Firmware - Power Management Driver
 *
 * Power state management for USB4/Thunderbolt to NVMe bridge controller.
 * Controls device power states, clock gating, and USB suspend/resume.
 *
 *===========================================================================
 * POWER MANAGEMENT ARCHITECTURE
 *===========================================================================
 *
 * Hardware Configuration:
 * - Multiple power domains (USB, PCIe, NVMe, PHY)
 * - Clock gating for power savings
 * - USB suspend/resume handling
 * - Link power states (L0, L1, L2)
 *
 * Register Map (0x92C0-0x92CF):
 * ┌──────────┬──────────────────────────────────────────────────────────┐
 * │ Address  │ Description                                              │
 * ├──────────┼──────────────────────────────────────────────────────────┤
 * │ 0x92C0   │ Power Control 0 - Main power enable (bit 7: enable)      │
 * │ 0x92C1   │ Power Control 1 - Clock config (bit 1: clock select)     │
 * │ 0x92C2   │ Power Status - State flags (bit 6: suspended)            │
 * │ 0x92C4   │ Power Control 4 - Main power control                     │
 * │ 0x92C5   │ Power Control 5 - PHY power (bit 2: enable)              │
 * │ 0x92C6   │ Power Control 6 - Clock gating                           │
 * │ 0x92C7   │ Power Control 7 - Clock gating extension                 │
 * │ 0x92C8   │ Power Control 8 - Additional controls                    │
 * │ 0x92CF   │ Power Config - Configuration bits                        │
 * │ 0x92F8   │ Power Extended Status                                    │
 * └──────────┴──────────────────────────────────────────────────────────┘
 *
 * Power Status Register (0x92C2) Bits:
 * ┌─────┬────────────────────────────────────────────────────────────────┐
 * │ Bit │ Function                                                       │
 * ├─────┼────────────────────────────────────────────────────────────────┤
 * │  6  │ Suspended - Device in suspend state                           │
 * │ 4-5 │ Link state bits                                               │
 * │ 0-3 │ Reserved                                                       │
 * └─────┴────────────────────────────────────────────────────────────────┘
 *
 * Power Control Flow:
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │                    POWER STATE MACHINE                              │
 * ├─────────────────────────────────────────────────────────────────────┤
 * │  ACTIVE ←──────────────────────────────→ SUSPEND                   │
 * │    │                                         │                      │
 * │    └── Check 0x92C2 bit 6 ──────────────────┘                      │
 * │                                                                     │
 * │  Resume sequence:                                                   │
 * │  1. Set 0x92C0 bit 7 (enable power)                                │
 * │  2. Set 0x92C1 bit 1 (enable clocks)                               │
 * │  3. Configure USB PHY (0x91D1, 0x91C1)                             │
 * │  4. Set 0x92C5 bit 2 (PHY power)                                   │
 * │                                                                     │
 * │  Suspend sequence:                                                  │
 * │  1. Set 0x92C2 bit 6 (mark suspended)                              │
 * │  2. Clear clock enables                                            │
 * │  3. Gate clocks via 0x92C6/0x92C7                                  │
 * └─────────────────────────────────────────────────────────────────────┘
 *
 *===========================================================================
 * IMPLEMENTATION STATUS
 *===========================================================================
 * power_set_suspended         [DONE] 0xcb23-0xcb2c - Set suspended bit
 * power_get_status_bit6       [DONE] 0x3023-0x302e - Check suspended
 * power_enable_clocks         [DONE] 0xcb6f-0xcb7e - Enable power/clocks
 * power_config_init           [DONE] 0xcb37-0xcb4a - Init power config
 * power_set_clock_bit1        [DONE] 0xcb4b-0xcb53 - Set clock config
 *
 * Total: 5 functions implemented
 *===========================================================================
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

/*
 * power_set_suspended - Set power status suspended bit (bit 6)
 * Address: 0xcb23-0xcb2c (10 bytes)
 *
 * Sets bit 6 of power status register to indicate device is suspended.
 *
 * Original disassembly:
 *   cb23: mov dptr, #0x92c2   ; Power status
 *   cb26: movx a, @dptr       ; read current
 *   cb27: anl a, #0xbf        ; clear bit 6
 *   cb29: orl a, #0x40        ; set bit 6
 *   cb2b: movx @dptr, a       ; write back
 *   cb2c: ret
 */
void power_set_suspended(void)
{
    uint8_t val = REG_POWER_STATUS_92C2;
    val = (val & 0xBF) | 0x40;  /* Set bit 6 */
    REG_POWER_STATUS_92C2 = val;
}

/*
 * power_get_status_bit6 - Check if device is suspended (bit 6 of 0x92C2)
 * Address: 0x3023-0x302e (12 bytes)
 *
 * Reads power status register and extracts bit 6 (suspended flag).
 * Returns non-zero if suspended.
 *
 * Original disassembly:
 *   3023: mov dptr, #0x92c2   ; Power status
 *   3026: movx a, @dptr
 *   3027: anl a, #0x40        ; mask bit 6
 *   3029: mov r7, a           ; save result
 *   302a: swap a              ; shift right 4
 *   302b: rrc a               ; shift right 1 more
 *   302c: rrc a               ; shift right 1 more
 *   302d: anl a, #0x03        ; mask low 2 bits
 */
uint8_t power_get_status_bit6(void)
{
    uint8_t val = REG_POWER_STATUS_92C2;
    val &= 0x40;  /* Mask bit 6 */
    return val;
}

/*
 * power_enable_clocks - Enable power and clocks
 * Address: 0xcb6f-0xcb87 (25 bytes)
 *
 * Enables main power (0x92C0 bit 0) and clock config (0x92C1 bit 0),
 * then enables PHY power (0x92C5 bit 2).
 *
 * Original disassembly:
 *   cb6f: mov dptr, #0x92c0   ; Power control 0
 *   cb72: movx a, @dptr
 *   cb73: anl a, #0xfe        ; clear bit 0
 *   cb75: orl a, #0x01        ; set bit 0
 *   cb77: movx @dptr, a
 *   cb78: inc dptr            ; 0x92C1
 *   cb79: movx a, @dptr
 *   cb7a: anl a, #0xfe        ; clear bit 0
 *   cb7c: orl a, #0x01        ; set bit 0
 *   cb7e: movx @dptr, a
 *   cb7f: mov dptr, #0x92c5   ; Power control 5
 *   cb82: movx a, @dptr
 *   cb83: anl a, #0xfb        ; clear bit 2
 *   cb85: orl a, #0x04        ; set bit 2
 *   cb87: movx @dptr, a
 */
void power_enable_clocks(void)
{
    uint8_t val;

    /* Enable main power (0x92C0 bit 0) */
    val = REG_POWER_CTRL_92C0;
    val = (val & 0xFE) | 0x01;
    REG_POWER_CTRL_92C0 = val;

    /* Enable clock config (0x92C1 bit 0) */
    val = REG_POWER_CTRL_92C1;
    val = (val & 0xFE) | 0x01;
    REG_POWER_CTRL_92C1 = val;

    /* Enable PHY power (0x92C5 bit 2) */
    val = REG_POWER_CTRL_92C5;
    val = (val & 0xFB) | 0x04;
    REG_POWER_CTRL_92C5 = val;
}

/*
 * power_config_init - Initialize power configuration
 * Address: 0xcb37-0xcb4a (20 bytes)
 *
 * Sets up power configuration registers for normal operation.
 * Writes 0x05 to 0x92C6, 0x00 to 0x92C7, then clears bits 0,1 of 0x9201.
 *
 * Original disassembly:
 *   cb37: mov dptr, #0x92c6   ; Power control 6
 *   cb3a: mov a, #0x05
 *   cb3c: movx @dptr, a
 *   cb3d: inc dptr            ; 0x92C7
 *   cb3e: clr a
 *   cb3f: movx @dptr, a
 *   cb40: mov dptr, #0x9201   ; USB control
 *   cb43: movx a, @dptr
 *   cb44: anl a, #0xfe        ; clear bit 0
 *   cb46: movx @dptr, a
 *   cb47: movx a, @dptr
 *   cb48: anl a, #0xfd        ; clear bit 1
 *   cb4a: movx @dptr, a
 */
void power_config_init(void)
{
    uint8_t val;

    /* Set clock gating config */
    REG_POWER_CTRL_92C6 = 0x05;
    XDATA_REG8(0x92C7) = 0x00;

    /* Clear bits 0,1 of 0x9201 */
    val = XDATA_REG8(0x9201);
    val &= 0xFE;  /* Clear bit 0 */
    XDATA_REG8(0x9201) = val;
    val = XDATA_REG8(0x9201);
    val &= 0xFD;  /* Clear bit 1 */
    XDATA_REG8(0x9201) = val;
}

/*
 * power_set_clock_bit1 - Set clock configuration bit 1
 * Address: 0xcb4b-0xcb53 (9 bytes)
 *
 * Sets bit 1 of power control register 0x92C1 for clock configuration.
 *
 * Original disassembly:
 *   cb4b: mov dptr, #0x92c1   ; Power control 1
 *   cb4e: movx a, @dptr
 *   cb4f: anl a, #0xfd        ; clear bit 1
 *   cb51: orl a, #0x02        ; set bit 1
 *   cb53: movx @dptr, a
 */
void power_set_clock_bit1(void)
{
    uint8_t val = REG_POWER_CTRL_92C1;
    val = (val & 0xFD) | 0x02;  /* Set bit 1 */
    REG_POWER_CTRL_92C1 = val;
}
