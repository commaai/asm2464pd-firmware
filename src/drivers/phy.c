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
 * phy_config_link_params    [SKIP] 0x5284-0x52a6 - In main.c
 * phy_poll_link_ready       [DONE] Based on 0x4fdb-0x4fe1 - Poll for ready
 * phy_check_usb_state       [DONE] 0x3031-0x303a - Check USB PHY state
 *
 * Total: 3 functions implemented (phy_config_link_params in main.c)
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
    val = REG_POWER_CTRL_92C0;
    val = (val & 0xFE) | 0x01;
    REG_POWER_CTRL_92C0 = val;

    /* Enable clock 0x92C1 bit 0 */
    val = REG_POWER_CTRL_92C1;
    val = (val & 0xFE) | 0x01;
    REG_POWER_CTRL_92C1 = val;

    /* Set PHY power 0x92C5 bit 2 */
    val = REG_POWER_CTRL_92C5;
    val = (val & 0xFB) | 0x04;
    REG_POWER_CTRL_92C5 = val;

    /* Configure USB PHY 0x9241 bit 4 */
    val = REG_USB_PHY_CONFIG_9241;
    val = (val & 0xEF) | 0x10;
    REG_USB_PHY_CONFIG_9241 = val;

    /* Configure USB PHY 0x9241 bits 6,7 */
    val = REG_USB_PHY_CONFIG_9241;
    val = (val & 0x3F) | 0xC0;
    REG_USB_PHY_CONFIG_9241 = val;
}

/* NOTE: phy_config_link_params() is in main.c at 0x5284-0x52a6 */

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
    val &= 0x30;  /* Mask bits 4,5 */
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
