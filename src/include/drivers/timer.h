/*
 * timer.h - Timer and System Event Driver
 *
 * Hardware timer and periodic interrupt handling for the ASM2464PD
 * USB4/Thunderbolt to NVMe bridge. Provides millisecond-resolution delays
 * and periodic polling for system events.
 *
 * HARDWARE CONFIGURATION
 *   - 4 independent hardware timers (Timer0-Timer3)
 *   - Each timer has: Divider, Control/Status, Threshold registers
 *   - Timer0: Main system tick timer, drives periodic ISR
 *   - Timer1: Protocol timeouts
 *   - Timer2: USB timing
 *   - Timer3: Idle timeout management
 *   - Clock source: 114MHz system clock
 *
 * REGISTER MAP (0xCC10-0xCC24)
 *   0xCC10  Timer0 DIV       Clock divider (bits 0-2: prescaler)
 *   0xCC11  Timer0 CSR       Control/Status (bit 1: done flag)
 *   0xCC12-13 Timer0 Threshold (16-bit count value, little-endian)
 *   0xCC16  Timer1 DIV       Clock divider
 *   0xCC17  Timer1 CSR       Control/Status
 *   0xCC18-19 Timer1 Threshold (16-bit)
 *   0xCC1C  Timer2 DIV       Clock divider
 *   0xCC1D  Timer2 CSR       Control/Status
 *   0xCC1E-1F Timer2 Threshold (16-bit)
 *   0xCC22  Timer3 DIV       Clock divider
 *   0xCC23  Timer3 CSR       Control/Status
 *   0xCC24  Timer3 Idle Timeout
 *   0xCC33  Timer Status/Control (bit 2: event flag)
 *
 * TIMER CSR REGISTER BITS
 *   Bit 0: Enable - Start/stop timer counting
 *   Bit 1: Done - Timer reached threshold (write 0x02 to clear)
 *   Bit 2: Interrupt enable
 *   Bits 3-7: Reserved
 *
 * TIMER DIV REGISTER BITS
 *   Bits 0-2: Prescaler select (divides clock by 2^N)
 *   Bit 3: Timer enable/disable bit
 *   Bits 4-7: Reserved
 *
 * TYPICAL TIMER0 CONFIGURATION (from 0xAD72)
 *   - Prescaler: 3 (divide by 8)
 *   - Threshold: 0x0028 (40 counts)
 *   - Results in ~1ms tick at 114MHz / 8 / 40 ≈ 356kHz → ~2.8us per tick
 *
 * TIMER0 ISR FLOW (0x4486)
 *   1. Save context (ACC, B, DPTR, PSW, R0-R7)
 *   2. Check 0xC806 bit 0 → timer_idle_timeout_handler (0xB4BA)
 *   3. Check 0xCC33 bit 2 → clear flag, dispatch to 0xCD10
 *   4. Check 0xC80A bit 6 → timer_uart_debug_output (0xAF5E)
 *   5. If 0x09F9 & 0x83:
 *      - Check 0xC80A bit 5 → timer_pcie_async_event (0xA066)
 *      - Check 0xC80A bit 4 → timer_pcie_link_event (0xC105)
 *      - Check 0xEC06 bit 0 → timer_nvme_completion (0xC0A5)
 *   6. Check 0xC80A & 0x0F → timer_pcie_error_handler (0xE911)
 *   7. Check 0xC806 bit 4 → timer_system_event_stub (0xEF4E)
 *   8. Restore context and RETI
 *
 * INTERRUPT SOURCES
 *   0xC806  INT_SYSTEM     System interrupt status
 *   0xCC33  CPU_EXEC_STAT  CPU execution status
 *   0xC80A  INT_PCIE_NVME  PCIe/NVMe interrupt status
 *   0xEC06  NVME_EVENT_ST  NVMe event status
 *   0xEC04  NVME_EVENT_ACK NVMe event acknowledge
 *
 * EVENT HANDLERS
 *   - timer_idle_timeout_handler(): Detect host inactivity
 *   - timer_pcie_link_event(): PCIe link state changes
 *   - timer_nvme_completion(): Poll NVMe completion queues
 *   - timer_uart_debug_output(): Periodic debug messages
 */
#ifndef _TIMER_H_
#define _TIMER_H_

#include "../types.h"

/* Timer ISR and control */
void timer0_isr(void) __interrupt(1) __using(0);                /* 0x0520-0x0523 (vector) */
void timer0_csr_ack(void);                      /* 0x95c2-0x95c8 */
void timer0_wait_done(void);                    /* 0xad95-0xada1 */
void timer1_check_and_ack(void);                /* 0x3094-0x30a0 */

/* Timer event handlers */
void timer_idle_timeout_handler(void);          /* 0x04d0-0x04d4 -> 0xce79 */
void timer_uart_debug_output(void);             /* 0x0520-0x0524 -> 0xb4ba */
void timer_pcie_link_event(void);               /* 0x0642-0x0646 */
void timer_pcie_async_event(void);              /* 0xe883-0xe88d (Bank 1) */
void timer_system_event_stub(void);             /* 0x0499-0x049c */
void timer_pcie_error_handler(void);            /* 0x052f-0x0532 */
void timer_nvme_completion(void);               /* 0x0593-0x0596 */
void timer_link_status_handler(void);           /* 0x061a-0x061d */

/* System handlers */
void system_interrupt_handler(void);            /* 0x4486-0x4531 */
void system_timer_handler(void);                /* 0x0570-0x0573 */

/* Timer configuration */
void timer_wait(uint8_t timeout_lo, uint8_t timeout_hi, uint8_t mode);  /* 0xe726-0xe72f (Bank 1) */
void timer_config_trampoline(uint8_t p1, uint8_t p2, uint8_t p3);       /* 0x0511-0x0514 */
void timer_event_init(void);                    /* 0x4532-0x45ff */
void timer_trigger_e726(void);                  /* 0xe726-0xe72f (Bank 1) */
void timer_phy_config_e57d(uint8_t param);      /* 0xe57d-0xe5fd (Bank 1) */

/* Delay functions */
void delay_loop_adb0(void);                     /* 0xadb0-0xade5 */

#endif /* _TIMER_H_ */
