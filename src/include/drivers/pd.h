/*
 * pd.h - USB Power Delivery State Machine
 *
 * PD (Power Delivery) debug and state management functions for the ASM2464PD.
 * These functions handle USB-PD protocol state transitions and debug output.
 *
 * Debug messages are output via UART to track PD state changes.
 */
#ifndef _PD_H_
#define _PD_H_

#include "../types.h"

/*
 * pd_debug_print_flp - Print flash/link power status
 * Bank 1 Address: 0xB103-0xB148 (approx 70 bytes)
 *
 * Outputs "[flp=XX]" where XX is the flash/link power status from 0xC6DB bit 0.
 * Also performs additional status register updates.
 */
void pd_debug_print_flp(void);

/*
 * pd_internal_state_init - Initialize internal PD state machine
 * Address: 0xB806-0xB86A (approx 100 bytes)
 *
 * Outputs "[InternalPD_StateInit]" and initializes various PD state variables
 * in XDATA 0x07Bx-0x07Ex region.
 */
void pd_internal_state_init(void);

/*
 * pd_state_handler - Main PD state machine handler
 * Bank 1 Address: 0xB0F5-0xB103 (14 bytes)
 *
 * Called during PD state transitions, clears registers and calls state helper.
 */
void pd_state_handler(void);

#endif /* _PD_H_ */
