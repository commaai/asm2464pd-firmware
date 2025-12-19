/*
 * phy.h - Physical Layer (PHY) Driver
 *
 * Controls the USB4/Thunderbolt PHY and PCIe link on the ASM2464PD bridge.
 * Handles PHY power states, link training, and signal configuration.
 *
 * ===========================================================================
 * PHY ARCHITECTURE
 * ===========================================================================
 * The ASM2464PD has multiple PHY domains:
 *
 *   USB PHY (0x91xx-0x92xx):
 *     - USB4/Thunderbolt 3/4 high-speed serializer/deserializer
 *     - Supports Full Speed, High Speed, SuperSpeed, SuperSpeed+
 *     - Controls connection detection and speed negotiation
 *
 *   Link PHY (0xC2xx):
 *     - PCIe/USB4 tunnel link layer
 *     - Manages link training state machine
 *     - Handles retrain and recovery
 *
 *   Extended PHY (0xC6xx):
 *     - Lane configuration and signal optimization
 *     - Equalization and pre-emphasis settings
 *
 * ===========================================================================
 * USB PHY REGISTERS (0x91xx-0x92xx)
 * ===========================================================================
 *   0x91C0  USB_PHY_CTRL0    USB PHY control
 *                            Bit 1: PHY state indicator (must be SET for
 *                                   USB state machine at 0x203B to progress)
 *                            Firmware clears at 0xCA8C, needs re-set
 *   0x91C1  USB_PHY_CTRL1    PHY configuration
 *   0x91D0  USB_PHY_CONFIG   PHY config register
 *   0x91D1  USB_PHY_MODE     PHY mode select (bit 3: interrupt pending)
 *   0x9201  USB_CTRL         USB control (bits 0,1: enable flags)
 *   0x920C  USB_CTRL_0C      USB/PHY control (bits 0,1: PHY config)
 *   0x9241  USB_PHY_CONFIG   PHY configuration (bits 4,6,7: state)
 *
 * ===========================================================================
 * LINK PHY REGISTERS (0xC2xx)
 * ===========================================================================
 *   0xC208  PHY_LINK_CTRL    Link control
 *                            Bit 4: Link state indicator
 *   0xC20C  PHY_LINK_CONFIG  Link configuration
 *                            Bit 6: Enable PHY link
 *
 * ===========================================================================
 * PHY EXTENDED REGISTERS (0xC6xx)
 * ===========================================================================
 *   0xC620  PHY_EXT_CTRL     Extended PHY control
 *   0xC62D  PHY_EXT_LANE     Lane configuration (bits 0-2: lane mask)
 *   0xC655  PHY_EXT_CFG55    Extended config (default 0x08)
 *   0xC656  PHY_EXT_SIGNAL   Signal settings
 *                            Bit 5: Signal config (clear for normal)
 *   0xC65A  PHY_EXT_CFG5A    Extended config (default 0x09)
 *   0xC65B  PHY_EXT_MODE     Extended PHY mode
 *                            Bit 3: Enable PHY extended
 *                            Bit 5: PHY mode select
 *   0xC6B3  PHY_STATUS       PHY status register
 *                            Bits 4,5: Link ready (poll these)
 *                            Non-zero = link is up and ready
 *                            Default: 0x30 (both bits set)
 *   0xC6DB  PHY_FLP_STATUS   Flash/link power status (bit 0)
 *
 * ===========================================================================
 * PHY INIT STATUS (0xCD31)
 * ===========================================================================
 *   Read: Returns PHY ready status
 *         Bit 0: Ready (1 = PHY initialization complete)
 *         Bit 1: Busy  (0 = idle)
 *         Returns 0x01 in emulator (always ready)
 *
 *   Write: PHY/hardware control commands
 *          Used during USB operations and descriptor DMA
 *
 * ===========================================================================
 * PHY INIT SEQUENCE (phy_init_sequence, 0xCB54-0xCB97)
 * ===========================================================================
 *   1. Clear bits 0,1 of USB control 0x920C
 *   2. Set bit 6 of PHY link config 0xC20C
 *   3. Clear bit 4 of PHY link control 0xC208
 *   4. Enable power via 0x92C0 bit 0, 0x92C1 bit 0
 *   5. Set PHY power 0x92C5 bit 2
 *   6. Configure USB PHY 0x9241 bits 4, 6, 7
 *
 * ===========================================================================
 * LINK PARAMETER CONFIG (phy_config_link_params, 0x5284-0x52A6)
 * ===========================================================================
 *   1. Set 0xC65B bit 3 (enable PHY extended)
 *   2. Clear 0xC656 bit 5 (signal config)
 *   3. Set 0xC65B bit 5 (PHY mode)
 *   4. Set 0xC62D bits 0-2 to 0x07 (lane config - all lanes enabled)
 *
 * ===========================================================================
 * LINK STATUS POLLING
 * ===========================================================================
 * Firmware polls PHY status during initialization:
 *
 *   Poll 0xC6B3 bits 4,5 until non-zero:
 *     - If bits 4,5 both CLEAR: link training in progress
 *     - If any bit SET: link ready
 *     - Default value 0x30 indicates link ready
 *
 *   Check 0xCC32 bit 0 for system state during init:
 *     - Used to determine boot path
 *
 * ===========================================================================
 * PHY COMPLETION (0xE302)
 * ===========================================================================
 *   PHY completion status register:
 *     Bit 6: PHY operation complete (default 0x40 = complete)
 *
 * ===========================================================================
 * EMULATOR BEHAVIOR
 * ===========================================================================
 *   0xC6B3: Returns 0x30 (bits 4,5 set) - link always ready
 *   0xCD31: Returns 0x01 (bit 0 set) - PHY always ready
 *   0xE302: Returns 0x40 (bit 6 set) - operation complete
 *
 *   USB PHY control (0x91C0):
 *     Returns with bit 1 SET when USB connected
 *     Needed for USB state machine progression at 0x203B
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
