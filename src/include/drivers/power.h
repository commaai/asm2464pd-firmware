/*
 * power.h - Power Management Driver
 *
 * The power management subsystem controls device power states, clock
 * gating, and suspend/resume handling for the ASM2464PD bridge. It
 * coordinates power transitions between USB, PCIe, and internal blocks.
 *
 * POWER STATES:
 *   Active (D0):    Full operation, all clocks running
 *   Idle:           Reduced activity, some clocks gated
 *   Suspended (D3): Minimal power, wake-on-event capability
 *
 * USB POWER STATES:
 *   U0: Active operation
 *   U1: Standby (fast exit latency)
 *   U2: Sleep (longer exit latency)
 *   U3: Suspend (lowest power, host-initiated wake)
 *
 * PCIE POWER STATES:
 *   L0:  Active
 *   L0s: Standby (fast recovery)
 *   L1:  Low power idle
 *   L2:  Auxiliary power only
 *
 * STATE MACHINE:
 *   power_state_machine_d02a() implements the main power state
 *   transitions, coordinating between USB and PCIe link states.
 *
 * CLOCK CONTROL:
 *   - power_enable_clocks(): Enable peripheral clocks
 *   - power_disable_clocks(): Gate unused clocks
 *   - Clock domains: USB, PCIe, DMA, CPU
 *
 * SUSPEND/RESUME:
 *   - power_set_suspended(): Enter low-power state
 *   - power_clear_suspended(): Wake and restore operation
 *   - Event flags trigger wake from suspend
 *
 * USAGE:
 *   power_config_init();          // Initialize power subsystem
 *   power_enable_clocks();        // Enable all clocks
 *   // ... normal operation ...
 *   power_set_suspended();        // Enter suspend
 *   // ... wake event ...
 *   power_clear_suspended();      // Resume operation
 */
#ifndef _POWER_H_
#define _POWER_H_

#include "../types.h"

/* Power state control */
void power_set_suspended(void);
void power_clear_suspended(void);
void power_set_state(void);
uint8_t power_get_status_bit6(void);

/* Clock control */
void power_enable_clocks(void);
void power_disable_clocks(void);
void power_set_clock_bit1(void);

/* Power initialization */
void power_config_init(void);
void power_check_status_e647(void);
void power_check_status(uint8_t param);

/* Power state machine */
uint8_t power_state_machine_d02a(uint8_t max_iterations);
uint8_t power_check_state_dde2(void);

/* Power event handlers */
void power_set_suspended_and_event_cad6(void);
void power_toggle_usb_bit2_caed(void);
void power_set_phy_bit1_cafb(void);
void phy_power_init_d916(uint8_t param);
void power_clear_init_flag(void);
void power_set_event_ctrl(void);

/* USB power */
void usb_power_init(void);

/* Power status */
uint8_t power_get_state_nibble_cb0f(void);
void power_set_link_status_cb19(void);
void power_set_status_bit6_cb23(void);
void power_clear_interface_flags_cb2d(void);

/* PHY power configuration */
void power_phy_init_config_cb37(void);
void power_check_event_ctrl_c9fa(void);
void power_reset_sys_state_c9ef(void);
void power_config_d630(uint8_t param);

#endif /* _POWER_H_ */
