/*
 * power.h - Power Management Driver
 *
 * Controls device power states, clock gating, and suspend/resume for the
 * ASM2464PD USB4/Thunderbolt to NVMe bridge. Coordinates transitions between
 * USB, PCIe, and internal power domains.
 *
 * POWER DOMAINS
 *   USB:    USB4/Thunderbolt interface power
 *   PCIe:   Downstream NVMe link power
 *   PHY:    High-speed serializer/deserializer
 *   Core:   8051 CPU and peripherals
 *
 * REGISTER MAP (0x92C0-0x92FF)
 *   0x92C0  POWER_ENABLE     Bit 0: main power enable
 *   0x92C1  CLOCK_ENABLE     Bit 0: clock enable, Bit 1: clock select
 *   0x92C2  POWER_STATUS     Bit 6: suspended flag
 *                            Bits 4-5: link state
 *   0x92C4  POWER_CTRL4      Main power control
 *   0x92C5  PHY_POWER        Bit 2: PHY power enable
 *   0x92C6  CLOCK_GATE       Clock gating control
 *   0x92C7  CLOCK_GATE_EXT   Clock gating extension
 *   0x92C8  POWER_CTRL8      Additional controls
 *   0x92CF  POWER_CONFIG     Configuration bits
 *   0x92F8  POWER_EXT_STATUS Extended status
 *
 * POWER STATE MACHINE
 *   ACTIVE <-> SUSPEND transitions via 0x92C2 bit 6
 *
 *   Resume sequence:
 *     1. Set 0x92C0 bit 0 (enable power)
 *     2. Set 0x92C1 bit 0 (enable clocks)
 *     3. Configure USB PHY (0x91D1, 0x91C1)
 *     4. Set 0x92C5 bit 2 (PHY power)
 *
 *   Suspend sequence:
 *     1. Set 0x92C2 bit 6 (mark suspended)
 *     2. Clear clock enables
 *     3. Gate clocks via 0x92C6/0x92C7
 *
 * USB LINK POWER STATES
 *   U0: Active operation
 *   U1: Standby (fast exit)
 *   U2: Sleep (longer exit)
 *   U3: Suspend (lowest power)
 *
 * PCIE LINK POWER STATES
 *   L0:  Active
 *   L0s: Standby (fast recovery)
 *   L1:  Low power idle
 *   L2:  Auxiliary power only
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
void power_check_event_ctrl_c9fa(void);         /* 0xc9fa-0xca0d */
void power_reset_sys_state_c9ef(void);          /* 0xc9ef-0xc9f9 */
void power_config_d630(uint8_t param);          /* 0xd630-0xd6a0 */

#endif /* _POWER_H_ */
