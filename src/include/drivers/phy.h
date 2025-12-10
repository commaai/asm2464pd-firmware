/*
 * phy.h - Physical Layer (PHY) Driver
 *
 * Controls the USB4/Thunderbolt PHY and PCIe link on the ASM2464PD bridge.
 * Handles PHY power states, link training, and signal configuration.
 *
 * USB PHY REGISTERS (0x91xx-0x92xx)
 *   0x91C0  USB_PHY_CTRL0    Bit 1: PHY state indicator
 *   0x91C1  USB_PHY_CTRL1    PHY configuration
 *   0x91D1  USB_PHY_MODE     PHY mode select
 *   0x9201  USB_CTRL         Bits 0,1: enable flags
 *   0x920C  USB_CTRL_0C      Bits 0,1: PHY config
 *   0x9241  USB_PHY_CONFIG   Bits 4,6,7: state
 *
 * LINK PHY REGISTERS (0xC2xx)
 *   0xC208  PHY_LINK_CTRL    Bit 4: link state
 *   0xC20C  PHY_LINK_CONFIG  Bit 6: enable
 *
 * PHY EXTENDED REGISTERS (0xC6xx)
 *   0xC62D  PHY_EXT_LANE     Lane configuration
 *   0xC656  PHY_EXT_SIGNAL   Signal settings
 *   0xC65B  PHY_EXT_MODE     Bit 3: enable, Bit 5: mode
 *   0xC6B3  PHY_STATUS       Bits 4,5: link ready (poll this)
 *
 * PHY INIT SEQUENCE (phy_init_sequence, 0xcb54-0xcb97)
 *   1. Clear bits 0,1 of USB control 0x920C
 *   2. Set bit 6 of PHY link config 0xC20C
 *   3. Clear bit 4 of PHY link control 0xC208
 *   4. Enable power via 0x92C0 bit 0, 0x92C1 bit 0
 *   5. Set PHY power 0x92C5 bit 2
 *   6. Configure USB PHY 0x9241 bits 4, 6, 7
 *
 * LINK PARAMETER CONFIG (phy_config_link_params, 0x5284-0x52a6)
 *   1. Set 0xC65B bit 3 (enable PHY extended)
 *   2. Clear 0xC656 bit 5 (signal config)
 *   3. Set 0xC65B bit 5 (PHY mode)
 *   4. Set 0xC62D bits 0-2 to 0x07 (lane config)
 *
 * LINK STATUS POLLING
 *   Poll 0xC6B3 bits 4,5 until non-zero (link ready)
 *   Check 0xCC32 bit 0 for system state during init
 */
#ifndef _PHY_H_
#define _PHY_H_

#include "../types.h"

/* PHY initialization */
void phy_init_sequence(void);                   /* 0xcb54-0xcb97 */
void phy_config_link_params(void);              /* 0x5284-0x52a6 */
void phy_register_config(void);                 /* 0xcb98-0xcb9f */
void phy_link_training(void);                   /* 0x3031-0x303a */

/* PHY status */
uint8_t phy_poll_link_ready(void);              /* 0x4fdb-0x4fe1 */
uint8_t phy_check_usb_state(void);              /* 0x302f-0x3030 */

/* PCIe control state */
void pcie_save_ctrl_state(void);                /* 0xe84d-0xe85b (Bank 1) */
void pcie_restore_ctrl_state(void);             /* 0xe85c-0xe868 (Bank 1) */
void pcie_lane_config(uint8_t lane_mask);       /* 0xd436-0xd47e */

/* Bank operations */
void bank_read(void) __naked;                   /* 0x0300-0x0310 */
void bank_write(void) __naked;                  /* 0x0311-0x0321 */

#endif /* _PHY_H_ */
