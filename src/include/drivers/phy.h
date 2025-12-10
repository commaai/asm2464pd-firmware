/*
 * phy.h - Physical Layer (PHY) Driver
 *
 * The PHY subsystem manages the physical layer interfaces for both
 * USB and PCIe connections. It handles link training, signal integrity,
 * and low-level electrical configuration.
 *
 * PHY COMPONENTS:
 *   USB PHY:
 *   - USB 3.2 Gen2x2 (20 Gbps) SuperSpeed+ support
 *   - USB4/Thunderbolt tunneling capability
 *   - LFPS (Low Frequency Periodic Signaling) handling
 *
 *   PCIe PHY:
 *   - PCIe Gen3/Gen4 support (up to 16 GT/s)
 *   - x4 lane configuration
 *   - Equalization and link training
 *
 * LINK TRAINING SEQUENCE:
 *   1. phy_init_sequence() - Reset and configure PHY
 *   2. phy_config_link_params() - Set speed/width targets
 *   3. phy_link_training() - Execute LTSSM (Link Training)
 *   4. phy_poll_link_ready() - Wait for link up
 *
 * POWER STATE MANAGEMENT:
 *   The PHY supports various power states for USB (U0-U3) and
 *   PCIe (L0, L0s, L1, L2). State transitions are coordinated
 *   with the power management subsystem.
 *
 * BANK SWITCHING:
 *   The 8051 has limited code address space (64KB). Code beyond
 *   0x8000 uses bank switching:
 *   - bank_read(): Read from alternate bank
 *   - bank_write(): Write to alternate bank
 *
 * USAGE:
 *   phy_init_sequence();
 *   phy_config_link_params();
 *   while (!phy_poll_link_ready()) { }
 *   // Link is now active
 */
#ifndef _PHY_H_
#define _PHY_H_

#include "../types.h"

/* PHY initialization */
void phy_init_sequence(void);
void phy_config_link_params(void);
void phy_register_config(void);
void phy_link_training(void);

/* PHY status */
uint8_t phy_poll_link_ready(void);
uint8_t phy_check_usb_state(void);

/* PCIe control state (in phy.c) */
void pcie_save_ctrl_state(void);
void pcie_restore_ctrl_state(void);
void pcie_lane_config(uint8_t lane_mask);

/* Bank operations */
void bank_read(void) __naked;
void bank_write(void) __naked;

#endif /* _PHY_H_ */
