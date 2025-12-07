/*
 * ASM2464PD Firmware - PHY/Link Control Driver
 *
 * Controls the USB4/Thunderbolt PHY and PCIe link initialization.
 * Handles PHY power states, link training, and signal configuration.
 *
 *===========================================================================
 * PHY/LINK ARCHITECTURE
 *===========================================================================
 *
 * Hardware Configuration:
 * - USB4/Thunderbolt PHY with multi-lane support
 * - PCIe Gen3/Gen4 link capability
 * - Integrated signal conditioning
 * - Link training state machine
 *
 * Register Map:
 * +-----------+----------------------------------------------------------+
 * | Address   | Description                                              |
 * +-----------+----------------------------------------------------------+
 * | USB PHY Registers (0x91xx-0x92xx)                                    |
 * +-----------+----------------------------------------------------------+
 * | 0x91C0    | USB PHY control 0 - bit 1: PHY state indicator           |
 * | 0x91C1    | USB PHY control 1 - PHY configuration                    |
 * | 0x91D1    | USB PHY control D1 - PHY mode                            |
 * | 0x9201    | USB control - bits 0,1: enable flags                     |
 * | 0x920C    | USB control 0C - bits 0,1: PHY config                    |
 * | 0x9241    | USB PHY config - bits 4,6,7: state                       |
 * +-----------+----------------------------------------------------------+
 * | Link PHY Registers (0xC2xx)                                          |
 * +-----------+----------------------------------------------------------+
 * | 0xC208    | PHY link control - bit 4: link state                     |
 * | 0xC20C    | PHY link config - bit 6: enable                          |
 * +-----------+----------------------------------------------------------+
 * | PHY Extended Registers (0xC6xx)                                      |
 * +-----------+----------------------------------------------------------+
 * | 0xC62D    | PHY extended config - lane config                        |
 * | 0xC656    | PHY extended config - signal settings                    |
 * | 0xC65B    | PHY extended config - bit 3: enable, bit 5: mode         |
 * | 0xC6B3    | PHY status - bits 4,5: link ready (polled)               |
 * +-----------+----------------------------------------------------------+
 *
 * PHY Initialization Sequence (0xcb54-0xcb97):
 * +----------------------------------------------------------------------+
 * |                    PHY INIT SEQUENCE                                 |
 * +----------------------------------------------------------------------+
 * |  1. Clear bits 0,1 of USB control 0x920C                            |
 * |  2. Set bit 6 of PHY link config 0xC20C                             |
 * |  3. Clear bit 4 of PHY link control 0xC208                          |
 * |  4. Enable power via 0x92C0 bit 0, 0x92C1 bit 0                     |
 * |  5. Set PHY power 0x92C5 bit 2                                       |
 * |  6. Configure USB PHY 0x9241 bits 4, 6, 7                            |
 * +----------------------------------------------------------------------+
 *
 * PHY Link Parameters (0x5284-0x52a6):
 * +----------------------------------------------------------------------+
 * |                    LINK PARAMETER CONFIG                             |
 * +----------------------------------------------------------------------+
 * |  1. Set 0xC65B bit 3 (enable PHY extended)                          |
 * |  2. Clear 0xC656 bit 5 (signal config)                              |
 * |  3. Set 0xC65B bit 5 (PHY mode)                                      |
 * |  4. Set 0xC62D bits 0-2 to 0x07 (lane config)                       |
 * +----------------------------------------------------------------------+
 *
 * Link Status Polling (from handler_4fb6):
 * - Polls 0xC6B3 bits 4,5 until non-zero (link ready)
 * - Checks 0xCC32 bit 0 for system state during init
 *
 *===========================================================================
 * IMPLEMENTATION STATUS
 *===========================================================================
 * phy_init_sequence         [DONE] 0xcb54-0xcb97 - Full PHY init
 * phy_config_link_params    [DONE] 0x5284-0x52a6 - Configure link params
 * phy_poll_link_ready       [DONE] Based on 0x4fdb-0x4fe1 - Poll for ready
 * phy_check_usb_state       [DONE] 0x3031-0x303a - Check USB PHY state
 *
 * Total: 4 functions implemented
 *===========================================================================
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

/*
 * phy_init_sequence - Full PHY initialization sequence
 * Address: 0xcb54-0xcb97 (68 bytes)
 *
 * Initializes USB and link PHY for operation. This is called during
 * system initialization to bring up the PHY.
 *
 * Sequence:
 * 1. Clear USB control 0x920C bits 0,1
 * 2. Set link config 0xC20C bit 6
 * 3. Clear link control 0xC208 bit 4
 * 4. Enable power 0x92C0 bit 0, 0x92C1 bit 0
 * 5. Set PHY power 0x92C5 bit 2
 * 6. Configure USB PHY 0x9241 bit 4, then bits 6,7
 *
 * Original disassembly:
 *   cb54: mov dptr, #0x920c   ; USB control
 *   cb57: movx a, @dptr
 *   cb58: anl a, #0xfd        ; clear bit 1
 *   cb5a: movx @dptr, a
 *   cb5b: movx a, @dptr
 *   cb5c: anl a, #0xfe        ; clear bit 0
 *   cb5e: movx @dptr, a
 *   cb5f: mov dptr, #0xc20c   ; PHY link config
 *   cb62: movx a, @dptr
 *   cb63: anl a, #0xbf        ; clear bit 6
 *   cb65: orl a, #0x40        ; set bit 6
 *   cb67: movx @dptr, a
 *   cb68: mov dptr, #0xc208   ; PHY link control
 *   cb6b: movx a, @dptr
 *   cb6c: anl a, #0xef        ; clear bit 4
 *   cb6e: movx @dptr, a
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
 *   cb88: mov dptr, #0x9241   ; USB PHY config
 *   cb8b: movx a, @dptr
 *   cb8c: anl a, #0xef        ; clear bit 4
 *   cb8e: orl a, #0x10        ; set bit 4
 *   cb90: movx @dptr, a
 *   cb91: movx a, @dptr
 *   cb92: anl a, #0x3f        ; clear bits 6,7
 *   cb94: orl a, #0xc0        ; set bits 6,7
 *   cb96: movx @dptr, a
 *   cb97: ret
 */
void phy_init_sequence(void)
{
    uint8_t val;

    /* Clear USB control 0x920C bits 0,1 */
    val = REG_USB_CTRL_920C;
    val &= 0xFD;  /* Clear bit 1 */
    REG_USB_CTRL_920C = val;
    val = REG_USB_CTRL_920C;
    val &= 0xFE;  /* Clear bit 0 */
    REG_USB_CTRL_920C = val;

    /* Set PHY link config 0xC20C bit 6 */
    val = REG_PHY_LINK_CONFIG_C20C;
    val = (val & 0xBF) | 0x40;
    REG_PHY_LINK_CONFIG_C20C = val;

    /* Clear PHY link control 0xC208 bit 4 */
    val = REG_PHY_LINK_CTRL_C208;
    val &= 0xEF;
    REG_PHY_LINK_CTRL_C208 = val;

    /* Enable power 0x92C0 bit 0 */
    val = REG_POWER_ENABLE;
    val = (val & ~POWER_ENABLE_BIT) | POWER_ENABLE_BIT;
    REG_POWER_ENABLE = val;

    /* Enable clock 0x92C1 bit 0 */
    val = REG_CLOCK_ENABLE;
    val = (val & ~CLOCK_ENABLE_BIT) | CLOCK_ENABLE_BIT;
    REG_CLOCK_ENABLE = val;

    /* Set PHY power 0x92C5 bit 2 */
    val = REG_PHY_POWER;
    val = (val & ~PHY_POWER_ENABLE) | PHY_POWER_ENABLE;
    REG_PHY_POWER = val;

    /* Configure USB PHY 0x9241 bit 4 */
    val = REG_USB_PHY_CONFIG_9241;
    val = (val & 0xEF) | 0x10;
    REG_USB_PHY_CONFIG_9241 = val;

    /* Configure USB PHY 0x9241 bits 6,7 */
    val = REG_USB_PHY_CONFIG_9241;
    val = (val & 0x3F) | 0xC0;
    REG_USB_PHY_CONFIG_9241 = val;
}

/*
 * phy_config_link_params - Configure PHY link parameters
 * Address: 0x5284-0x52a6 (35 bytes)
 *
 * Sets up PHY extended registers for link training parameters.
 *
 * From ghidra.c FUN_CODE_5284:
 *   DAT_EXTMEM_c65b = DAT_EXTMEM_c65b & 0xf7 | 8;   // set bit 3
 *   DAT_EXTMEM_c656 = DAT_EXTMEM_c656 & 0xdf;       // clear bit 5
 *   DAT_EXTMEM_c65b = DAT_EXTMEM_c65b & 0xdf | 0x20; // set bit 5
 *   DAT_EXTMEM_c62d = DAT_EXTMEM_c62d & 0xe0 | 7;   // set low 3 bits to 7
 *
 * Original disassembly:
 *   5284: mov dptr, #0xc65b   ; PHY extended config
 *   5287: movx a, @dptr
 *   5288: anl a, #0xf7        ; clear bit 3
 *   528a: orl a, #0x08        ; set bit 3
 *   528c: movx @dptr, a
 *   528d: mov dptr, #0xc656   ; PHY extended config
 *   5290: movx a, @dptr
 *   5291: anl a, #0xdf        ; clear bit 5
 *   5293: movx @dptr, a
 *   5294: mov dptr, #0xc65b   ; PHY extended config
 *   5297: movx a, @dptr
 *   5298: anl a, #0xdf        ; clear bit 5
 *   529a: orl a, #0x20        ; set bit 5
 *   529c: movx @dptr, a
 *   529d: mov dptr, #0xc62d   ; PHY extended config
 *   52a0: movx a, @dptr
 *   52a1: anl a, #0xe0        ; clear bits 0-4
 *   52a3: orl a, #0x07        ; set bits 0-2 (lane config = 7)
 *   52a5: movx @dptr, a
 *   52a6: ret
 */
void phy_config_link_params(void)
{
    REG_PHY_EXT_5B = (REG_PHY_EXT_5B & ~PHY_EXT_ENABLE) | PHY_EXT_ENABLE;
    REG_PHY_EXT_56 = REG_PHY_EXT_56 & ~PHY_EXT_SIGNAL_CFG;
    REG_PHY_EXT_5B = (REG_PHY_EXT_5B & ~PHY_EXT_MODE) | PHY_EXT_MODE;
    REG_PHY_EXT_2D = (REG_PHY_EXT_2D & ~PHY_EXT_LANE_MASK) | PHY_EXT_LANE_MASK;
}

/*
 * phy_poll_link_ready - Poll PHY status for link ready
 * Based on: 0x4fdb-0x4fe1 (poll loop in handler_4fb6)
 *
 * Polls PHY extended status register 0xC6B3 bits 4,5 until
 * at least one is set, indicating link is ready.
 *
 * Returns: non-zero if link ready (bits 4,5), 0 if not ready
 *
 * Original disassembly (poll loop):
 *   4fdb: mov dptr, #0xc6b3   ; PHY status
 *   4fde: movx a, @dptr
 *   4fdf: anl a, #0x30        ; mask bits 4,5
 *   4fe1: jz 0x4fdb           ; loop if zero
 */
uint8_t phy_poll_link_ready(void)
{
    uint8_t val = REG_PHY_EXT_B3;
    val &= PHY_EXT_LINK_READY;  /* Mask bits 4,5 */
    return val;
}

/*
 * phy_check_usb_state - Check USB PHY state from 0x91C0 bit 1
 * Address: 0x3031-0x303a (10 bytes)
 *
 * Checks if USB PHY bit 1 is set in 0x91C0, returns shifted result.
 * This is called from power_get_status_bit6 when suspended bit is set.
 *
 * Returns: 0 if PHY state not set, 1 if set
 *
 * Original disassembly:
 *   3031: mov dptr, #0x91c0   ; USB PHY control
 *   3034: movx a, @dptr
 *   3035: anl a, #0x02        ; mask bit 1
 *   3037: mov r7, a           ; save
 *   3038: clr c
 *   3039: rrc a               ; shift right (bit 1 -> bit 0)
 *   303a: jz 0x303f           ; jump if zero
 */
uint8_t phy_check_usb_state(void)
{
    uint8_t val = REG_USB_PHY_CTRL_91C0;
    val &= 0x02;  /* Mask bit 1 */
    val >>= 1;    /* Shift right */
    return val;
}
