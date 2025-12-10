/*
 * timer.h - Timer and System Event Driver
 *
 * The timer subsystem provides periodic interrupts for system scheduling,
 * timeout handling, and event-driven processing. It uses the 8051's
 * Timer 0 and Timer 1 hardware peripherals.
 *
 * TIMER ARCHITECTURE:
 *   Timer 0 (T0) - System tick and timeout handling
 *   Timer 1 (T1) - Auxiliary timing (UART baud rate, etc.)
 *
 * INTERRUPT HANDLING:
 *   Timer 0 ISR (vector 1) processes:
 *   - System event dispatch
 *   - Idle timeout detection
 *   - PCIe link monitoring
 *   - NVMe completion polling
 *   - Debug output scheduling
 *
 * EVENT SYSTEM:
 *   The timer drives a cooperative event system where handlers are
 *   called based on system state flags. This provides pseudo-threading
 *   without a full RTOS.
 *
 *   Event Handlers:
 *   - timer_idle_timeout_handler(): Detect host inactivity
 *   - timer_pcie_link_event(): PCIe link state changes
 *   - timer_nvme_completion(): Poll NVMe completion queues
 *   - timer_uart_debug_output(): Periodic debug messages
 *
 * KEY REGISTERS (8051 SFRs):
 *   TH0/TL0: Timer 0 counter
 *   TH1/TL1: Timer 1 counter
 *   TMOD: Timer mode configuration
 *   TCON: Timer control and flags
 *
 * USAGE:
 *   1. timer_event_init() - Initialize timer subsystem
 *   2. Timer 0 ISR runs automatically on interrupt
 *   3. timer_wait() - Blocking delay with timeout
 *   4. system_timer_handler() - Process pending events
 */
#ifndef _TIMER_H_
#define _TIMER_H_

#include "../types.h"

/* Timer ISR and control */
void timer0_isr(void) __interrupt(1) __using(0);
void timer0_csr_ack(void);
void timer0_wait_done(void);
void timer1_check_and_ack(void);

/* Timer event handlers */
void timer_idle_timeout_handler(void);
void timer_uart_debug_output(void);
void timer_pcie_link_event(void);
void timer_pcie_async_event(void);
void timer_system_event_stub(void);
void timer_pcie_error_handler(void);
void timer_nvme_completion(void);
void timer_link_status_handler(void);

/* System handlers */
void system_interrupt_handler(void);
void system_timer_handler(void);

/* Timer configuration */
void timer_wait(uint8_t timeout_lo, uint8_t timeout_hi, uint8_t mode);
void timer_config_trampoline(uint8_t p1, uint8_t p2, uint8_t p3);
void timer_event_init(void);
void timer_trigger_e726(void);
void timer_phy_config_e57d(uint8_t param);

/* Delay functions */
void delay_loop_adb0(void);

#endif /* _TIMER_H_ */
