/*
 * timer.c - Timer and System Event Driver
 *
 * See drivers/timer.h for hardware documentation.
 */

#include "drivers/timer.h"
#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"
#include "app/dispatch.h"
#include "utils.h"
#include "drivers/cmd.h"

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

/* External helper functions */
extern void config_helper_dual(uint8_t r7, uint8_t r5);

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
    if (status & EVENT_FLAGS_ANY) {
        /* Check 0xC80A bit 5 - async PCIe event */
        if (REG_INT_PCIE_NVME & INT_PCIE_NVME_EVENT) {
            timer_pcie_async_event();
        }

        /* Check 0xC80A bit 4 - PCIe link event */
        if (REG_INT_PCIE_NVME & INT_PCIE_NVME_TIMER) {
            timer_pcie_link_event();
        }

        /* Check NVMe event at 0xEC06 bit 0 */
        if (REG_NVME_EVENT_STATUS & NVME_EVENT_PENDING) {
            /* Acknowledge NVMe event */
            REG_NVME_EVENT_ACK = 0x01;

            /* Check PHY status at 0x0AF1 bit 5 */
            if (G_STATE_FLAG_0AF1 & STATE_FLAG_PHY_READY) {
                /* Clear bits 6 and 7 of PHY link control */
                status = REG_PHY_LINK_CTRL;
                status &= ~PHY_LINK_CTRL_BIT6;  /* Clear bit 6 */
                REG_PHY_LINK_CTRL = status;
                status = REG_PHY_LINK_CTRL;
                status &= ~PHY_LINK_CTRL_BIT7;  /* Clear bit 7 */
                REG_PHY_LINK_CTRL = status;
            }

            timer_nvme_completion();
        }

        /* Check 0xC80A low nibble for PCIe/NVMe errors */
        status = REG_INT_PCIE_NVME;
        if (status & INT_PCIE_NVME_EVENTS) {
            timer_pcie_error_handler();
        }
    }

    /* Check 0xC806 bit 4 - system event */
    status = REG_INT_SYSTEM;
    if (status & INT_SYSTEM_TIMER) {
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
    REG_TIMER0_CSR = TIMER_CSR_CLEAR;  /* Clear interrupt flag */
    REG_TIMER0_CSR = TIMER_CSR_EXPIRED;  /* Clear done flag */
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
    while (!(REG_TIMER0_CSR & TIMER_CSR_EXPIRED))
        ;

    /* Acknowledge by writing 0x02 */
    REG_TIMER0_CSR = TIMER_CSR_EXPIRED;
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
    if (REG_TIMER1_CSR & TIMER_CSR_EXPIRED) {
        /* Acknowledge by writing 0x02 */
        REG_TIMER1_CSR = TIMER_CSR_EXPIRED;
        /* Call dispatch handler - 0x04D5 */
        /* lcall 0x04d5 would go here */
    }
    /* Note: setb EA done by caller or at end of routine */
}

/*
 * timer_link_status_handler - Timer/Link status handler
 * Address: 0x04d0-0x04d4 (5 bytes) -> dispatches to 0xCE79
 *
 * Function at 0xCE79 (94 bytes):
 * Handles timer and link status checks:
 * 1. Checks REG_LINK_STATUS_CC3F bits 1,2 and calls helper if set
 * 2. Configures timer/link registers (0xCC30, 0xCC33, 0xCC3B, etc.)
 * 3. Clears bits in PHY_CONFIG (0xC233)
 * 4. Performs timing delays with polling
 * 5. Polls status registers until link ready
 *
 * Original disassembly:
 *   ce79: mov dptr, #0xcc3f
 *   ce7c: movx a, @dptr       ; read REG_LINK_STATUS_CC3F
 *   ce7d: jb 0xe0.1, 0xce84   ; if bit 1 set, call helper
 *   ce80: movx a, @dptr
 *   ce81: jnb 0xe0.2, 0xce87  ; if bit 2 not set, skip helper
 *   ce84: lcall 0xd0d3        ; clear bits in CC3F, set flags
 *   ce87: lcall 0xcf28        ; configure timer regs
 *   ce8a: lcall 0x0610        ; dispatch to bank 1 0xED02
 *   ce8d: mov dptr, #0xc233
 *   ce90: movx a, @dptr
 *   ce91: anl a, #0xfc        ; clear bits 0-1
 *   ce93: movx @dptr, a
 *   ce94: lcall 0xbd5e        ; set bit 2, clear bit 2 of @DPTR
 *   ce97-cea6: timing delay loop
 *   cea7-ceab: more timing delay
 *   ceb0-cec5: poll 0xE712 and 0xCC11 until ready
 *   cec6: lcall 0xe8ef
 *   cecb: lcall 0xdd42
 *   cece: ljmp 0xd996
 */
void timer_link_status_handler(void)
{
    uint8_t status;
    uint8_t val;

    /* Read link status register */
    status = REG_CPU_CTRL_CC3F;

    /* Check if bit 1 or bit 2 is set - if so, call helper to clear flags */
    if ((status & 0x02) || (status & 0x04)) {
        /* Helper at 0xD0D3:
         * - Calls 0xBD2A (set bit 2, clear bit 2)
         * - Delay loop with R4:R5=0x0009
         * - Reads 0xCC3F, clears bit 1
         * - Modifies register, sets bit 5, clears bit 6
         * - More delay loops
         * - Clears bit 7 of 0xCC3D
         */
        REG_CPU_CTRL_CC3F = (REG_CPU_CTRL_CC3F & 0xFB) | 0x04;  /* Set bit 2 */
        /* Note: Full helper implementation would include delays */
        REG_CPU_CTRL_CC3F = REG_CPU_CTRL_CC3F & 0xFD;  /* Clear bit 1 */
        REG_CPU_CTRL_CC3D = REG_CPU_CTRL_CC3D & 0x7F;  /* Clear bit 7 */
    }

    /* Helper at 0xCF28: Configure timer/link registers
     * - Reads 0xCC30, calls 0xBCEB, 0xBD49, sets bit 2
     * - Writes 0x04 to 0xCC33
     * - Clears bit 2 of 0xE324
     * - Clears bit 0 of 0xCC3B
     * - Sets bits in 0xCC39, 0xCC3A
     * - Clears bit 0 of 0xCC3E
     * - Configures 0xCA81
     */
    val = REG_CPU_MODE;
    val = (val & 0xFB) | 0x04;  /* Set bit 2 */
    REG_CPU_MODE = val;

    REG_CPU_EXEC_STATUS_2 = 0x04;

    REG_LINK_CTRL_E324 = REG_LINK_CTRL_E324 & 0xFB;  /* Clear bit 2 */
    REG_TIMER_CTRL_CC3B = REG_TIMER_CTRL_CC3B & ~TIMER_CTRL_ENABLE;

    /* Set bits 5,6 in 0xCC3A */
    REG_TIMER_ENABLE_B = (REG_TIMER_ENABLE_B & 0x9F) | 0x60;

    REG_CPU_CTRL_CC3E = REG_CPU_CTRL_CC3E & 0xFE;  /* Clear bit 0 */

    /* Dispatch to bank 1 handler at 0xED02 via 0x0610 */
    jump_bank_1(0xED02);

    /* Clear bits 0-1 of PHY config register */
    REG_PHY_CONFIG = REG_PHY_CONFIG & 0xFC;

    /* Helper at 0xBD5E: read @DPTR, clear bit 2, set bit 2 */
    val = REG_PHY_CONFIG;
    val = (val & 0xFB) | 0x04;  /* Set bit 2 */
    REG_PHY_CONFIG = val;

    /* Timing delay - R4:R5=0x0014, R7=0x02 */
    /* In original firmware this calls 0xE80A delay function */

    /* Clear bit 2 of PHY config */
    REG_PHY_CONFIG = REG_PHY_CONFIG & 0xFB;

    /* More timing delay - R4:R5=0x000A, R7=0x03 */

    /* Polling loop: wait for status bits in link status and timer */
    do {
        status = REG_LINK_STATUS_E712;
        /* Check bit 0 - if set, call helper and exit */
        if (status & 0x01) {
            break;
        }
        /* Check bit 1 (from value ANDed with 0x02, shifted right) */
        if ((status & 0x02) != 0) {
            break;
        }
        /* Check bit 1 of timer CSR - if not set, continue polling */
    } while ((REG_TIMER0_CSR & TIMER_CSR_EXPIRED) == 0);

    /* Final handlers - would call 0xE8EF, 0xDD42, then jump to 0xD996 */
    /* These handle completion of the timer/link setup */
}

/*
 * system_interrupt_handler - System Interrupt Handler
 * Address: 0x0520-0x0524 (5 bytes) -> dispatches to bank 0 0xB4BA
 *
 * Function at 0xB4BA:
 * System interrupt handler called when system status bit 0 is set.
 * Handles link status changes and timer events.
 *
 * Algorithm:
 *   1. Read 0xCC23, check bit 1
 *   2. If bit 1 set: call 0xE3D8, write 0x02 to 0xCC23
 *   3. Read 0xCC81, check bit 1
 *   4. If bit 1 set: read 0x07BD, compare with 0x0E or 0x0D
 *   5. Configure 0xCC81 with value 0x02
 *   6. Check 0x07BC and dispatch accordingly
 *
 * Original disassembly:
 *   b4ba: mov dptr, #0xcc23
 *   b4bd: movx a, @dptr
 *   b4be: jnb 0xe0.1, 0xb4ca     ; if bit 1 not set, skip
 *   b4c1: lcall 0xe3d8           ; helper
 *   b4c4: mov dptr, #0xcc23
 *   b4c7: mov a, #0x02
 *   b4c9: movx @dptr, a          ; write 0x02
 *   ... (continues with state machine)
 */
void system_interrupt_handler(void)
{
    uint8_t val;
    uint8_t state;

    /* Read timer 3 CSR and check bit 1 */
    val = REG_TIMER3_CSR;
    if (val & TIMER_CSR_EXPIRED) {
        /* Call helper 0xE3D8 */
        /* Write 0x02 to acknowledge */
        REG_TIMER3_CSR = TIMER_CSR_EXPIRED;
    }

    /* Read CPU interrupt control and check ACK bit */
    val = REG_CPU_INT_CTRL;
    if (val & CPU_INT_CTRL_ACK) {
        /* Read state from G_FLASH_OP_COUNTER */
        state = G_FLASH_OP_COUNTER;

        if (state == 0x0E || state == 0x0D) {
            /* Acknowledge interrupt */
            REG_CPU_INT_CTRL = CPU_INT_CTRL_ACK;

            /* Check G_FLASH_CMD_TYPE for further dispatch */
            val = G_FLASH_CMD_TYPE;
            if (val != 0) {
                /* Call helper 0xE529 with R7=0x3B */
            }
            /* Call helper 0xD676 */
        } else {
            /* Call 0xE90B for other states */
            REG_CPU_INT_CTRL = CPU_INT_CTRL_ACK;
        }
    }

    /* Read CPU DMA interrupt and check bit 1 */
    val = REG_CPU_DMA_INT;
    if (val & 0x02) {
        REG_CPU_DMA_INT = 0x02;  /* Acknowledge */
    }
}

/*
 * system_timer_handler - System Timer Handler
 * Address: 0x0642-0x0646 (5 bytes)
 *
 * Dispatches to bank 1 code at 0xEF4E (file offset 0x16F4E)
 * Called from ext1_isr when system status bit 4 is set.
 *
 * Original disassembly:
 *   0642: mov dptr, #0xef4e
 *   0645: ajmp 0x0311
 */
extern void error_handler_system_timer(void);  /* Bank 1: file 0x16F4E */
void system_timer_handler(void)
{
    error_handler_system_timer();
}

/*
 * timer_wait - Wait for timer to expire
 * Address: 0xE80A-0xE81A (17 bytes)
 *
 * Sets up Timer0 with given threshold and mode, then polls until done.
 *
 * Parameters:
 *   timeout_lo - Low byte of threshold (r4)
 *   timeout_hi - High byte of threshold (r5)
 *   mode       - Timer prescaler mode bits 0-2 (r7)
 *
 * Original disassembly:
 *   e80a: lcall 0xe50d        ; timer_setup
 *   e80d: mov dptr, #0xcc11   ; poll loop
 *   e810: movx a, @dptr
 *   e811: jnb 0xe0.1, 0xe80d  ; wait for bit 1
 *   e814: mov dptr, #0xcc11
 *   e817: mov a, #0x02
 *   e819: movx @dptr, a       ; clear done flag
 *   e81a: ret
 */
void timer_wait(uint8_t timeout_lo, uint8_t timeout_hi, uint8_t mode)
{
    uint8_t csr;

    /* Reset timer - 0xE8EF */
    REG_TIMER0_CSR = 0x04;  /* Reset */
    REG_TIMER0_CSR = 0x02;  /* Clear done flag */

    /* Configure timer - 0xE50D */
    csr = REG_TIMER0_DIV;
    csr = (csr & 0xF8) | (mode & 0x07);  /* Set prescaler bits */
    REG_TIMER0_DIV = csr;

    /* Set threshold */
    REG_TIMER0_THRESHOLD = ((uint16_t)timeout_hi << 8) | timeout_lo;

    /* Start timer */
    REG_TIMER0_CSR = 0x01;

    /* Poll until done (bit 1 set) */
    while ((REG_TIMER0_CSR & TIMER_CSR_EXPIRED) == 0) {
        /* Wait */
    }

    /* Clear done flag */
    REG_TIMER0_CSR = 0x02;
}



/* ============================================================
 * Timer Configuration Functions
 * ============================================================ */

/*
 * timer_config_trampoline - Trampoline to timer0_reset (0xe50d)
 * Address: 0x0511
 * Original: mov dptr, #0xe50d; ajmp 0x0300
 * Params: p1→threshold_hi, p2→threshold_lo, p3→div_bits
 */
void timer_config_trampoline(uint8_t p1, uint8_t p2, uint8_t p3)
{
    timer0_configure(p3, p1, p2);
}

/*
 * timer_event_init - Timer/event initialization handler
 * Address: 0xe883-0xe88d (11 bytes)
 *
 * Calls helper_e73a, then calls 0x95e1 with r7=0x10, r5=0,
 * then jumps to cmd_wait_completion.
 */
void timer_event_init(void)
{
    /* Clear command registers */
    cmd_engine_clear();

    /* Call config function with r7=0x10, r5=0 */
    config_helper_dual(0x10, 0);

    /* Wait for completion */
    cmd_wait_completion();
}

/*
 * timer_trigger_e726 - Trigger timer via 0xCD31
 * Address: 0xe726-0xe72f (10 bytes)
 *
 * Disassembly:
 *   e726: mov dptr, #0xcd31
 *   e729: mov a, #0x04
 *   e72b: movx @dptr, a
 *   e72c: mov a, #0x02
 *   e72e: movx @dptr, a
 *   e72f: ret
 *
 * Writes 0x04 then 0x02 to register 0xCD31 (timer trigger sequence)
 */
void timer_trigger_e726(void)
{
    REG_CPU_TIMER_CTRL_CD31 = 0x04;
    REG_CPU_TIMER_CTRL_CD31 = 0x02;
}

/*
 * delay_loop_adb0 - Delay loop with status check
 * Address: 0xadb0-0xade5 (~54 bytes)
 *
 * Iterates 12 times (0x0C), calling helper 0x9a53 each time.
 * Then checks IDATA[0x60] bit 0 and IDATA[0x61] to determine result code.
 * Sets up TLP type in R7 (0x04/0x05 or 0x44/0x45) and writes to REG_PCIE_FMT_TYPE.
 *
 * Algorithm:
 *   1. Clear G_ERROR_CODE_06EA, set I_WORK_51 = 0
 *   2. Loop: for (i=0; i<12; i++) call helper_9a53(i)
 *   3. Check IDATA[0x60] bit 0:
 *      - If set: R7 = (IDATA[0x61] != 0) ? 0x45 : 0x44
 *      - If clear: R7 = (IDATA[0x61] != 0) ? 0x05 : 0x04
 *   4. Write R7 to REG_PCIE_FMT_TYPE (0xB210)
 *   5. Write 0x01 to REG_PCIE_TLP_CTRL (0xB213)
 *   6. Check I_WORK_65 and return via other helpers
 *
 * Side effects:
 *   - Sets up I_WORK_65 result code
 *   - Writes to REG_PCIE_FMT_TYPE and REG_PCIE_TLP_CTRL
 */
void delay_loop_adb0(void)
{
    uint8_t i;
    uint8_t tlp_type;

    /* Clear error code and work variable */
    G_ERROR_CODE_06EA = 0;
    I_WORK_51 = 0;

    /* Loop 12 times - helper_9a53 does status polling */
    for (i = 0; i < 12; i++) {
        /* Placeholder for helper_9a53(i) call */
        /* This helper updates I_WORK_65 based on polling result */
    }

    /* Determine TLP type based on IDATA values */
    if (*(__idata uint8_t *)0x60 & 0x01) {
        /* High type range (Config space) */
        tlp_type = (*(__idata uint8_t *)0x61 != 0) ? 0x45 : 0x44;
    } else {
        /* Low type range (Memory) */
        tlp_type = (*(__idata uint8_t *)0x61 != 0) ? 0x05 : 0x04;
    }

    /* Write TLP type to PCIe format register */
    REG_PCIE_FMT_TYPE = tlp_type;

    /* Write 0x01 to PCIe TLP control register */
    REG_PCIE_TLP_CTRL = 0x01;

    /* I_WORK_65 is left with result from polling loop
     * 0 = success, non-zero = error */
}

void timer_phy_config_e57d(uint8_t param)
{
    uint8_t val;
    if (!(param & 0x01)) return;
    val = REG_PHY_TIMER_CTRL_E764; val &= 0xFD; REG_PHY_TIMER_CTRL_E764 = val;
    val = REG_PHY_TIMER_CTRL_E764; val &= 0xFE; REG_PHY_TIMER_CTRL_E764 = val;
    val = REG_PHY_TIMER_CTRL_E764; val &= 0xF7; REG_PHY_TIMER_CTRL_E764 = val;
    val = REG_PHY_TIMER_CTRL_E764; val = (val & 0xFB) | 0x04; REG_PHY_TIMER_CTRL_E764 = val;
}

/*
 * reg_timer_setup_and_set_bits - Setup timer and set bits in 0xCC3A and 0xCC38
 * Address: 0xbcf2-0xbd04 (19 bytes)
 *
 * Sets bit 1 in REG_TIMER_ENABLE_B and REG_TIMER_ENABLE_A.
 */
void reg_timer_setup_and_set_bits(void)
{
    uint8_t val;

    /* Set bit 1 in REG_TIMER_ENABLE_B */
    val = REG_TIMER_ENABLE_B;
    val = (val & ~TIMER_ENABLE_B_BIT) | TIMER_ENABLE_B_BIT;
    REG_TIMER_ENABLE_B = val;

    /* Set bit 1 in REG_TIMER_ENABLE_A */
    val = REG_TIMER_ENABLE_A;
    val = (val & ~TIMER_ENABLE_A_BIT) | TIMER_ENABLE_A_BIT;
    REG_TIMER_ENABLE_A = val;
}

/*
 * reg_timer_init_and_start - Clear timer init flag, write 4 to timer CSR, then 2
 * Address: 0xbd05-0xbd13 (15 bytes)
 *
 * Clears G_TIMER_INIT_0B40, writes 4 to REG_TIMER3_CSR, then writes 2.
 */
void reg_timer_init_and_start(void)
{
    G_TIMER_INIT_0B40 = 0;
    REG_TIMER3_CSR = 0x04;
    REG_TIMER3_CSR = 0x02;
}

/*
 * reg_timer_clear_bits - Clear bit 1 in REG_TIMER_ENABLE_B and REG_TIMER_ENABLE_A
 * Address: 0xbd14-0xbd22 (15 bytes)
 */
void reg_timer_clear_bits(void)
{
    uint8_t val;

    val = REG_TIMER_ENABLE_B;
    REG_TIMER_ENABLE_B = val & ~TIMER_ENABLE_B_BIT;

    val = REG_TIMER_ENABLE_A;
    REG_TIMER_ENABLE_A = val & ~TIMER_ENABLE_A_BIT;
}

/*
 * timer_clear_bit1_cc3b - Clear bit 1 in REG_TIMER_CTRL_CC3B
 * Address: 0xbd41-0xbd48 (8 bytes)
 */
void timer_clear_ctrl_bit1(void)
{
    uint8_t val = REG_TIMER_CTRL_CC3B;
    REG_TIMER_CTRL_CC3B = val & ~TIMER_CTRL_START;
}

/* timer0_configure and timer0_reset are implemented in pcie.c */

/*
 * delay_short_e89d - Short delay with IDATA setup
 * Address: 0xe89d-0xe8a8 (12 bytes)
 *
 * Sets I_WORK_65 = 0x0F, I_WORK_60 = 0, then calls delay loop at 0xadb0.
 * The result is left in R7 (via I_WORK_65).
 */
void delay_short_e89d(void)
{
    I_WORK_65 = 0x0F;
    *(__idata uint8_t *)0x60 = 0;
    delay_loop_adb0();
}

/*
 * delay_wait_e80a - Delay with parameters
 * Address: 0xe80a-0xe81x
 *
 * Waits for a specified delay using timer-based polling.
 * Parameters are passed in R4:R5 (delay value) and R7 (flags).
 */
void delay_wait_e80a(uint16_t delay, uint8_t flag)
{
    (void)delay;
    (void)flag;
    /* TODO: Implement timer-based delay */
}
