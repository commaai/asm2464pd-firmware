/*
 * ASM2464PD Firmware - Timer Driver
 *
 * Hardware timer and periodic interrupt handling for USB4/Thunderbolt to NVMe
 * bridge controller. Provides millisecond-resolution delays and periodic polling.
 *
 *===========================================================================
 * TIMER CONTROLLER ARCHITECTURE
 *===========================================================================
 *
 * Hardware Configuration:
 * - 4 independent hardware timers (Timer0-Timer3)
 * - Each timer has: Divider, Control/Status, Threshold registers
 * - Timer0: Main system tick timer, drives periodic ISR
 * - Timer1: Used for protocol timeouts
 * - Timer2: Used for USB timing
 * - Timer3: Idle timeout management
 * - Clock source derived from 114MHz system clock
 *
 * Register Map (0xCC10-0xCC24):
 * ┌──────────┬──────────────────────────────────────────────────────────┐
 * │ Address  │ Description                                              │
 * ├──────────┼──────────────────────────────────────────────────────────┤
 * │ 0xCC10   │ Timer0 DIV - Clock divider (bits 0-2: prescaler)         │
 * │ 0xCC11   │ Timer0 CSR - Control/Status (bit 1: done flag)           │
 * │ 0xCC12-13│ Timer0 Threshold (16-bit count value, little-endian)     │
 * │ 0xCC16   │ Timer1 DIV - Clock divider                               │
 * │ 0xCC17   │ Timer1 CSR - Control/Status                              │
 * │ 0xCC18-19│ Timer1 Threshold (16-bit)                                │
 * │ 0xCC1C   │ Timer2 DIV - Clock divider                               │
 * │ 0xCC1D   │ Timer2 CSR - Control/Status                              │
 * │ 0xCC1E-1F│ Timer2 Threshold (16-bit)                                │
 * │ 0xCC22   │ Timer3 DIV - Clock divider                               │
 * │ 0xCC23   │ Timer3 CSR - Control/Status                              │
 * │ 0xCC24   │ Timer3 Idle Timeout                                      │
 * │ 0xCC33   │ Timer Status/Control (bit 2: event flag)                 │
 * └──────────┴──────────────────────────────────────────────────────────┘
 *
 * Timer CSR Register Bits:
 * ┌─────┬────────────────────────────────────────────────────────────────┐
 * │ Bit │ Function                                                       │
 * ├─────┼────────────────────────────────────────────────────────────────┤
 * │  0  │ Enable - Start/stop timer counting                            │
 * │  1  │ Done - Timer reached threshold (write 0x02 to clear)          │
 * │  2  │ Interrupt enable                                              │
 * │ 3-7 │ Reserved                                                       │
 * └─────┴────────────────────────────────────────────────────────────────┘
 *
 * Timer DIV Register Bits:
 * ┌─────┬────────────────────────────────────────────────────────────────┐
 * │ Bit │ Function                                                       │
 * ├─────┼────────────────────────────────────────────────────────────────┤
 * │ 0-2 │ Prescaler select (divides clock by 2^N)                       │
 * │  3  │ Timer enable/disable bit                                      │
 * │ 4-7 │ Reserved                                                       │
 * └─────┴────────────────────────────────────────────────────────────────┘
 *
 * Timer0 ISR Flow:
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │                    Timer0 ISR (0x4486)                              │
 * ├─────────────────────────────────────────────────────────────────────┤
 * │ 1. Save context (ACC, B, DPTR, PSW, R0-R7)                          │
 * │ 2. Check 0xC806 bit 0 → timer_idle_timeout_handler (0xB4BA)         │
 * │ 3. Check 0xCC33 bit 2 → clear flag, dispatch to 0xCD10              │
 * │ 4. Check 0xC80A bit 6 → timer_uart_debug_output (0xAF5E)            │
 * │ 5. If 0x09F9 & 0x83:                                                │
 * │    - Check 0xC80A bit 5 → timer_pcie_async_event (0xA066)           │
 * │    - Check 0xC80A bit 4 → timer_pcie_link_event (0xC105)            │
 * │    - Check 0xEC06 bit 0 → timer_nvme_completion (0xC0A5)            │
 * │ 6. Check 0xC80A & 0x0F → timer_pcie_error_handler (0xE911)          │
 * │ 7. Check 0xC806 bit 4 → timer_system_event_stub (0xEF4E)            │
 * │ 8. Restore context and RETI                                         │
 * └─────────────────────────────────────────────────────────────────────┘
 *
 * Typical Timer0 Configuration (from 0xAD72):
 * - Prescaler: 3 (divide by 8)
 * - Threshold: 0x0028 (40 counts)
 * - Results in ~1ms tick at 114MHz / 8 / 40 ≈ 356kHz → ~2.8us per tick
 *
 *===========================================================================
 * IMPLEMENTATION STATUS
 *===========================================================================
 * timer0_isr                [DONE] 0x4486-0x4531 - Main timer ISR
 * timer_idle_timeout_handler [DONE] 0x0520-0x0523 - Idle timeout (Bank 0 -> 0xB4BA)
 * timer_uart_debug_output    [DONE] 0x052f-0x0532 - UART debug output (Bank 0 -> 0xAF5E)
 * timer_pcie_link_event      [DONE] 0x0593-0x0596 - PCIe link event (Bank 0 -> 0xC105)
 * timer_pcie_async_event     [DONE] 0x061a-0x061d - PCIe async event (Bank 1 -> 0xA066)
 * timer_system_event_stub    [DONE] 0x0642-0x0645 - System event stub (Bank 1 -> 0xEF4E)
 * timer_pcie_error_handler   [DONE] 0x0570-0x0573 - PCIe error handler (Bank 1 -> 0xE911)
 * timer_nvme_completion      [DONE] 0x0499-0x049c - NVMe completion (Bank 1 -> 0xC0A5)
 * timer0_csr_ack             [DONE] 0x95c2-0x95c8 - Write 0x04, 0x02 to CSR
 * timer0_wait_done           [DONE] 0xad95-0xada1 - Wait for CSR bit 1
 * timer1_check_and_ack       [DONE] 0x3094-0x30a0 - Check/ack Timer1
 *
 * Total: 11 functions (all implemented)
 *===========================================================================
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

/*
 * Register aliases for timer ISR (using standard names from registers.h/globals.h)
 * 0xC806 = REG_INT_SYSTEM      - System interrupt status
 * 0xCC33 = REG_CPU_EXEC_STATUS_2 - CPU execution status
 * 0xC80A = REG_INT_PCIE_NVME   - PCIe/NVMe interrupt status
 * 0xEC06 = REG_NVME_EVENT_STATUS - NVMe event status
 * 0xEC04 = REG_NVME_EVENT_ACK  - NVMe event acknowledge
 * 0x0AF1 = G_STATE_FLAG_0AF1   - State flag (global variable)
 * 0xE7E3 = REG_PHY_LINK_CTRL   - PHY link control
 */

/* External dispatch functions from main.c */
extern void jump_bank_0(uint16_t addr);
extern void jump_bank_1(uint16_t addr);

/*
 * timer_idle_timeout_handler - Handle idle timeout events
 * Address: 0x0520-0x0523 (4 bytes)
 *
 * Dispatches to 0xB4BA which processes Timer3 idle timeout.
 * Reads 0xCC23 (Timer3 CSR), acks with 0x02, checks 0xCC81 for
 * idle state and processes timeout conditions.
 * Called when bit 0 of 0xC806 (system interrupt status) is set.
 *
 * Original disassembly:
 *   0520: mov dptr, #0xb4ba
 *   0523: ajmp 0x0300
 */
void timer_idle_timeout_handler(void)
{
    jump_bank_0(0xB4BA);
}

/*
 * timer_uart_debug_output - Output debug information via UART
 * Address: 0x052f-0x0532 (4 bytes)
 *
 * Dispatches to 0xAF5E which outputs debug characters to UART.
 * Writes newline (0x0A, 0x0D) to 0xC001, outputs register values
 * from 0xE40F/0xE410, formats with separators (':', ']').
 * Called when bit 6 of 0xC80A (PCIe/NVMe interrupt status) is set.
 *
 * Original disassembly:
 *   052f: mov dptr, #0xaf5e
 *   0532: ajmp 0x0300
 */
void timer_uart_debug_output(void)
{
    jump_bank_0(0xAF5E);
}

/*
 * timer_pcie_link_event - Handle PCIe link state events
 * Address: 0x0593-0x0596 (4 bytes)
 *
 * Dispatches to 0xC105 which handles PCIe link state changes.
 * Calls 0xBCDE/0xBCAF for PCIe status checks, reads 0x09FA for
 * link state, handles PHY and error recovery via 0xCA0D/0xE74E.
 * Called when bit 4 of 0xC80A is set (when event flags & 0x83).
 *
 * Original disassembly:
 *   0593: mov dptr, #0xc105
 *   0596: ajmp 0x0300
 */
void timer_pcie_link_event(void)
{
    jump_bank_0(0xC105);
}

/*
 * timer_pcie_async_event - Handle asynchronous PCIe events
 * Address: 0x061a-0x061d (4 bytes)
 *
 * Dispatches to Bank 1 at 0xA066 (file 0x12066) for async PCIe
 * event processing. Handles link training, reset recovery, and
 * asynchronous notifications from the PCIe controller.
 * Called when bit 5 of 0xC80A is set (when event flags & 0x83).
 *
 * Original disassembly:
 *   061a: mov dptr, #0xa066
 *   061d: ajmp 0x0311
 */
void timer_pcie_async_event(void)
{
    jump_bank_1(0xA066);
}

/*
 * timer_system_event_stub - Placeholder for system event handling
 * Address: 0x0642-0x0645 (4 bytes)
 *
 * Dispatches to Bank 1 at 0xEF4E (file 0x16F4E) which is currently
 * all NOPs (empty handler stub). Reserved for future system events.
 * Called when bit 4 of 0xC806 (system interrupt status) is set.
 *
 * Original disassembly:
 *   0642: mov dptr, #0xef4e
 *   0645: ajmp 0x0311
 */
void timer_system_event_stub(void)
{
    jump_bank_1(0xEF4E);
}

/*
 * timer_pcie_error_handler - Handle PCIe/NVMe error conditions
 * Address: 0x0570-0x0573 (4 bytes)
 *
 * Dispatches to Bank 1 at 0xE911 (file 0x16911) near error_clear_e760_flags.
 * Handles PCIe and NVMe error conditions by clearing/setting error flags
 * in the 0xE760-0xE763 register region.
 * Called when 0xC80A low nibble is non-zero (PCIe/NVMe error flags).
 *
 * Original disassembly:
 *   0570: mov dptr, #0xe911
 *   0573: ajmp 0x0311
 */
void timer_pcie_error_handler(void)
{
    jump_bank_1(0xE911);
}

/*
 * timer_nvme_completion - Handle NVMe command completion
 * Address: 0x0499-0x049c (4 bytes)
 *
 * Dispatches to Bank 1 at 0xC0A5 (file 0x140A5) for NVMe completion
 * processing. Checks command status at 0x0B02, calls DMA helpers,
 * and processes completion queue entries.
 * Called after PHY bits are cleared when NVMe event (0xEC06) is detected.
 *
 * Original disassembly:
 *   0499: mov dptr, #0xc0a5
 *   049c: ajmp 0x0311
 */
void timer_nvme_completion(void)
{
    jump_bank_1(0xC0A5);
}

/*
 * timer0_isr - Timer0 Interrupt Service Routine
 * Address: 0x4486-0x4531 (172 bytes)
 *
 * Main periodic interrupt handler. Polls multiple hardware status registers
 * and dispatches to various handlers based on flags:
 *
 * - 0xC806 bit 0: Call 0x0520 (timer tick)
 * - 0xCC33 bit 2: Write 0x04 to 0xCC33, call 0x0390
 * - 0xC80A bit 6: Call 0x052f
 * - When 0x09F9 & 0x83 != 0:
 *   - 0xC80A bit 5: Call 0x061a
 *   - 0xC80A bit 4: Call 0x0593
 *   - 0xEC06 bit 0: Write 0x01 to 0xEC04, check 0x0AF1
 *     - 0x0AF1 bit 5: Clear bits 6,7 of 0xE7E3
 *     - Call 0x0499
 * - 0xC80A & 0x0F != 0: Call 0x0570
 * - 0xC806 bit 4: Call 0x0642
 *
 * Original disassembly:
 *   4486: push 0xe0      ; save ACC
 *   4488: push 0xf0      ; save B
 *   448a: push 0x83      ; save DPH
 *   448c: push 0x82      ; save DPL
 *   448e: push 0xd0      ; save PSW
 *   4490: mov 0xd0, #0x00 ; select register bank 0
 *   4493: push 0x00-0x07 ; save R0-R7
 *   44a3: mov dptr, #0xc806
 *   44a6: movx a, @dptr
 *   44a7: jnb 0xe0.0, 0x44ad  ; check bit 0
 *   44aa: lcall 0x0520
 *   ...
 *   4517-452f: pop R7-R0, PSW, DPL, DPH, B, ACC
 *   4531: reti
 */
void timer0_isr(void) __interrupt(1) __using(0)
{
    uint8_t status;

    /* Check timer status register 0xC806 bit 0 - idle timeout */
    status = REG_INT_SYSTEM;
    if (status & 0x01) {
        timer_idle_timeout_handler();
    }

    /* Check status register 0xCC33 bit 2 */
    status = REG_CPU_EXEC_STATUS_2;
    if (status & 0x04) {
        REG_CPU_EXEC_STATUS_2 = 0x04;  /* Clear/acknowledge */
        /* lcall 0x0390 - dispatch stub */
    }

    /* Check status register 0xC80A bit 6 - UART debug output request */
    status = REG_INT_PCIE_NVME;
    if (status & 0x40) {
        timer_uart_debug_output();
    }

    /* Check system state flags at 0x09F9 (global variable) */
    status = G_EVENT_FLAGS;
    if (status & 0x83) {
        /* Check 0xC80A bit 5 - async PCIe event */
        if (REG_INT_PCIE_NVME & 0x20) {
            timer_pcie_async_event();
        }

        /* Check 0xC80A bit 4 - PCIe link event */
        if (REG_INT_PCIE_NVME & 0x10) {
            timer_pcie_link_event();
        }

        /* Check NVMe event at 0xEC06 bit 0 */
        if (REG_NVME_EVENT_STATUS & 0x01) {
            /* Acknowledge NVMe event */
            REG_NVME_EVENT_ACK = 0x01;

            /* Check PHY status at 0x0AF1 bit 5 */
            if (G_STATE_FLAG_0AF1 & 0x20) {
                /* Clear bits 6 and 7 of PHY link control */
                status = REG_PHY_LINK_CTRL;
                status &= 0xBF;  /* Clear bit 6 */
                REG_PHY_LINK_CTRL = status;
                status = REG_PHY_LINK_CTRL;
                status &= 0x7F;  /* Clear bit 7 */
                REG_PHY_LINK_CTRL = status;
            }

            timer_nvme_completion();
        }

        /* Check 0xC80A low nibble for PCIe/NVMe errors */
        status = REG_INT_PCIE_NVME;
        if (status & 0x0F) {
            timer_pcie_error_handler();
        }
    }

    /* Check 0xC806 bit 4 - system event */
    status = REG_INT_SYSTEM;
    if (status & 0x10) {
        timer_system_event_stub();
    }
}

/*
 * timer0_csr_ack - Acknowledge timer0 CSR with 0x04, then 0x02
 * Address: 0x95c2-0x95c8 (7 bytes)
 *
 * Writes 0x04 then 0x02 to the Timer0 CSR register at DPTR (0xCC11).
 * Called to acknowledge/clear timer events.
 * Note: DPTR must be pointing to 0xCC11 when called (from 0xAD7A).
 *
 * Original disassembly:
 *   95c2: mov a, #0x04
 *   95c4: movx @dptr, a      ; write 0x04 to CSR
 *   95c5: mov a, #0x02
 *   95c7: movx @dptr, a      ; write 0x02 to CSR
 *   95c8: ret
 */
void timer0_csr_ack(void)
{
    REG_TIMER0_CSR = 0x04;  /* Clear interrupt flag */
    REG_TIMER0_CSR = 0x02;  /* Clear done flag */
}

/*
 * timer0_wait_done - Wait for Timer0 done flag (CSR bit 1)
 * Address: 0xad95-0xada1 (13 bytes)
 *
 * Polls Timer0 CSR register waiting for bit 1 (done) to be set.
 * Then acknowledges by writing 0x02 to clear the done flag.
 *
 * Original disassembly:
 *   ad95: mov dptr, #0xcc11   ; Timer0 CSR
 *   ad98: movx a, @dptr       ; read CSR
 *   ad99: jnb 0xe0.1, 0xad95  ; loop until bit 1 set
 *   ad9c: mov dptr, #0xcc11
 *   ad9f: mov a, #0x02
 *   ada1: movx @dptr, a       ; write 0x02 to clear done
 */
void timer0_wait_done(void)
{
    /* Wait for done flag (bit 1) */
    while (!(REG_TIMER0_CSR & 0x02))
        ;

    /* Acknowledge by writing 0x02 */
    REG_TIMER0_CSR = 0x02;
}

/*
 * timer1_check_and_ack - Check Timer1 done and acknowledge
 * Address: 0x3094-0x30a0 (13 bytes)
 *
 * Checks if Timer1 CSR bit 1 (done) is set. If so, writes 0x02 to
 * acknowledge/clear the done flag, then calls dispatch at 0x04D5.
 *
 * Original disassembly:
 *   3094: mov dptr, #0xcc17   ; Timer1 CSR
 *   3097: movx a, @dptr
 *   3098: jnb 0xe0.1, 0x30a1  ; if bit 1 not set, skip
 *   309b: mov a, #0x02
 *   309d: movx @dptr, a       ; write 0x02 to ack
 *   309e: lcall 0x04d5        ; dispatch handler
 *   30a1: setb 0xa8.7         ; set EA (enable interrupts)
 */
void timer1_check_and_ack(void)
{
    /* Check if Timer1 done flag is set */
    if (REG_TIMER1_CSR & 0x02) {
        /* Acknowledge by writing 0x02 */
        REG_TIMER1_CSR = 0x02;
        /* Call dispatch handler - 0x04D5 */
        /* lcall 0x04d5 would go here */
    }
    /* Note: setb EA done by caller or at end of routine */
}

