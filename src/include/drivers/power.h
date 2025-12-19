/*
 * power.h - Power Management Driver
 *
 * Controls device power states, clock gating, and suspend/resume for the
 * ASM2464PD USB4/Thunderbolt to NVMe bridge. Coordinates transitions between
 * USB, PCIe, and internal power domains.
 *
 * ===========================================================================
 * POWER DOMAINS
 * ===========================================================================
 *   USB:    USB4/Thunderbolt interface power
 *   PCIe:   Downstream NVMe link power
 *   PHY:    High-speed serializer/deserializer
 *   Core:   8051 CPU and peripherals
 *
 * ===========================================================================
 * POWER STATUS REGISTER (0x92C2)
 * ===========================================================================
 * Critical register for USB/power state machine coordination:
 *
 *   Bit 6: Suspended/Power state flag
 *     - Used for ISR vs main loop path selection
 *     - ISR at 0xE42A needs bit 6 CLEAR to call descriptor init (0xBDA4)
 *     - Main loop at 0x202A needs bit 6 SET to call 0x0322 for transfer
 *
 *   Emulator behavior:
 *     - Tracks read count via usb_92c2_read_count
 *     - First 1-2 reads: Return 0x40 (bit 6 SET) - ISR in progress
 *     - After 2+ reads: Return 0x40 (bit 6 SET) - main loop ready
 *     - During USB control transfers: coordinates ISR→main loop transition
 *
 *   Bits 4-5: Link state indicator
 *   Bits 0-3: Power domain status
 *
 * ===========================================================================
 * REGISTER MAP (0x92C0-0x92FF)
 * ===========================================================================
 *   0x92C0  POWER_ENABLE     Power domain enable
 *                            Bit 0: Main power enable
 *                            Default: 0x81
 *   0x92C1  CLOCK_ENABLE     Clock domain enable
 *                            Bit 0: Clock enable
 *                            Bit 1: Clock select
 *                            Default: 0x03
 *   0x92C2  POWER_STATUS     Power/USB state (see above)
 *                            Default: 0x40 (bit 6 set)
 *   0x92C4  POWER_CTRL4      Main power control
 *   0x92C5  PHY_POWER        PHY power control
 *                            Bit 2: PHY power enable
 *                            Default: 0x04
 *   0x92C6  CLOCK_GATE       Clock gating control
 *   0x92C7  CLOCK_GATE_EXT   Clock gating extension
 *   0x92C8  POWER_CTRL8      Additional controls
 *   0x92CF  POWER_CONFIG     Configuration bits
 *   0x92E0  POWER_DOMAIN     Power domain status
 *                            Default: 0x02
 *   0x92F7  POWER_STATUS2    Power status 2
 *                            Default: 0x40
 *   0x92F8  POWER_EXT_STATUS Extended status
 *                            Bits 2-3: Status bits
 *                            Default: 0x0C
 *   0x92FB  POWER_SEQ_DONE   Power sequence complete
 *                            Checked at 0x9C42
 *                            Default: 0x01 (complete)
 *
 * ===========================================================================
 * POWER STATE MACHINE
 * ===========================================================================
 * ACTIVE <-> SUSPEND transitions via 0x92C2 bit 6:
 *
 *   Resume sequence (SUSPEND → ACTIVE):
 *     1. Set 0x92C0 bit 0 (enable main power)
 *     2. Set 0x92C1 bit 0 (enable clocks)
 *     3. Configure USB PHY (0x91D1, 0x91C1)
 *     4. Set 0x92C5 bit 2 (enable PHY power)
 *     5. Clear 0x92C2 bit 6 (mark active)
 *
 *   Suspend sequence (ACTIVE → SUSPEND):
 *     1. Set 0x92C2 bit 6 (mark suspended)
 *     2. Clear clock enables (0x92C1)
 *     3. Gate clocks via 0x92C6/0x92C7
 *     4. Disable PHY (clear 0x92C5 bit 2)
 *
 * ===========================================================================
 * USB LINK POWER STATES
 * ===========================================================================
 * USB 3.x defines these power states:
 *
 *   U0: Active operation - full bandwidth available
 *   U1: Standby - fast exit latency (~1 µs)
 *   U2: Sleep - longer exit latency (~500 µs)
 *   U3: Suspend - lowest power, longest exit (~10 ms)
 *
 * State transitions controlled via USB PHY registers and link commands.
 *
 * ===========================================================================
 * PCIe LINK POWER STATES (ASPM)
 * ===========================================================================
 * PCIe Active State Power Management states:
 *
 *   L0:  Active - full bandwidth available
 *   L0s: Standby - fast recovery (~2 µs)
 *   L1:  Low power idle - longer recovery (~32 µs)
 *   L1.1/L1.2: Extended low power substates
 *   L2:  Auxiliary power only - device presence maintained
 *
 * ===========================================================================
 * PD (POWER DELIVERY) INTERRUPT REGISTERS
 * ===========================================================================
 * USB Power Delivery event detection:
 *
 *   0xCA0D  PD_INT_STATUS1   PD interrupt status 1
 *                            Bit 3: Interrupt pending
 *   0xCA0E  PD_INT_STATUS2   PD interrupt status 2
 *                            Bit 2: Interrupt pending
 *   0xCA81  PD_EXT_STATUS    Extended PD status
 *
 * ===========================================================================
 * POWER EVENT REGISTERS
 * ===========================================================================
 *   0xE40F  PD_EVENT_TYPE    PD event type (for debug output)
 *   0xE410  PD_SUB_EVENT     PD sub-event (for debug output)
 *
 * These control debug output at 0xAE89/0xAF5E functions.
 *
 * ===========================================================================
 * EMULATOR DEFAULTS
 * ===========================================================================
 *   0x92C0: 0x81 (power enabled)
 *   0x92C1: 0x03 (clocks enabled)
 *   0x92C2: 0x40 (bit 6 set for main loop path)
 *   0x92C5: 0x04 (PHY powered)
 *   0x92E0: 0x02 (power domain active)
 *   0x92F7: 0x40 (power status)
 *   0x92F8: 0x0C (bits 2-3 set)
 *   0x92FB: 0x01 (power sequence complete)
 */
#ifndef _POWER_H_
#define _POWER_H_

#include "../types.h"

/* Power state control */
void power_set_suspended(void);                 /* 0xcb23-0xcb2c */
void power_clear_suspended(void);               /* 0xcb2d-0xcb36 */
void power_set_state(void);                     /* 0x53c0-0x53d3 */
uint8_t power_get_status_bit6(void);            /* 0x3023-0x302e */

/* Clock control */
void power_enable_clocks(void);                 /* 0xcb6f-0xcb87 */
void power_disable_clocks(void);                /* 0xcb88-0xcb9a */
void power_set_clock_bit1(void);                /* 0xcb4b-0xcb53 */

/* Power initialization */
void power_config_init(void);                   /* 0xcb37-0xcb4a */
void power_check_status_e647(void);             /* 0xe647-0xe65e (Bank 1) */
void power_check_status(uint8_t param);         /* stub */

/* Power state machine */
uint8_t power_state_machine_d02a(uint8_t max_iterations);   /* 0xd02a-0xd07e */
uint8_t power_check_state_dde2(void);           /* 0xdde2-0xde15 */

/* Power event handlers */
void power_set_suspended_and_event_cad6(void);  /* 0xcad6-0xcaec */
void power_toggle_usb_bit2_caed(void);          /* 0xcaed-0xcafa */
void power_set_phy_bit1_cafb(void);             /* 0xcafb-0xcb08 */
void phy_power_init_d916(uint8_t param);        /* 0xd916-0xd995 */
void power_clear_init_flag(void);               /* 0xcb09-0xcb14 */
void power_set_event_ctrl(void);                /* 0xcb15-0xcb22 */

/* USB power */
void usb_power_init(void);                      /* 0x0327-0x032a */

/* Power status */
uint8_t power_get_state_nibble_cb0f(void);      /* 0xcb0f-0xcb14 */
void power_set_link_status_cb19(void);          /* 0xcb19-0xcb22 */
void power_set_status_bit6_cb23(void);          /* 0xcb23-0xcb2c */
void power_clear_interface_flags_cb2d(void);    /* 0xcb2d-0xcb36 */

/* PHY power configuration */
void power_phy_init_config_cb37(void);          /* 0xcb37-0xcb4a */
void power_check_event_ctrl_c9fa(void);         /* 0xc9fa-0xca0c */
void power_reset_sys_state_c9ef(void);          /* 0xc9ef-0xc9f9 */
void power_config_d630(uint8_t param);          /* 0xd630-0xd6a0 */
void power_state_handler_ca0d(void);            /* 0xca0d-0xca70 */

#endif /* _POWER_H_ */
