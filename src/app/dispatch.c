/*
 * ASM2464PD Firmware - Dispatch Table Functions
 *
 * This file contains all the dispatch stub functions that route
 * calls to the appropriate handlers via bank switching.
 *
 * The dispatch functions follow a simple pattern:
 *   1. Load target address into DPTR (mov dptr, #ADDR)
 *   2. Jump to bank switch handler:
 *      - ajmp 0x0300 (jump_bank_0) for Bank 0 targets
 *      - ajmp 0x0311 (jump_bank_1) for Bank 1 targets
 *
 * Each dispatch stub is exactly 5 bytes:
 *   90 HH LL  - mov dptr, #ADDR
 *   61 00/11  - ajmp 0x0300 or 0x0311
 *
 * ============================================================================
 * DISPATCH TABLE LAYOUT (0x0322-0x0650)
 * ============================================================================
 *
 * 0x0322-0x03A7: Bank 0 dispatch stubs (ajmp 0x0300)
 * 0x03A9-0x0411: Bank 1 dispatch stubs (ajmp 0x0311)
 * 0x0412-0x04DE: Mixed bank dispatch stubs
 * 0x04DF-0x0650: Event/interrupt dispatch stubs
 */

#include "app/dispatch.h"
#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"
#include "drivers/pd.h"
#include "drivers/flash.h"
#include "drivers/uart.h"

/* External function declarations */
extern void pcie_adapter_config(void);

/*===========================================================================
 * Bank Switch Functions (0x0300-0x0321)
 *===========================================================================*/

/*
 * jump_bank_0 - Bank 0 dispatch function
 * Address: 0x0300-0x0310 (17 bytes)
 *
 * Sets DPX=0 (bank 0) and dispatches to target address.
 * R0 is set to 0x0A which may be used by target functions.
 *
 * Original disassembly:
 *   0300: push 0x08      ; push R0
 *   0302: mov a, #0x03
 *   0304: push 0xe0      ; push ACC
 *   0306: push 0x82      ; push DPL
 *   0308: push 0x83      ; push DPH
 *   030a: mov 0x08, #0x0a  ; R0 = 0x0A
 *   030d: mov 0x96, #0x00  ; DPX = 0x00 (bank 0)
 *   0310: ret              ; pops DPH:DPL from stack, jumps there
 */
void jump_bank_0(uint16_t reg_addr)
{
    /* Bank 0 dispatch - target address is in bank 0 (file 0x0000-0xFFFF) */
    (void)reg_addr;
    DPX = 0x00;
}

/*
 * jump_bank_1 - Bank 1 dispatch function
 * Address: 0x0311-0x0321 (17 bytes)
 *
 * Sets DPX=1 (bank 1) and dispatches to target address.
 * R0 is set to 0x1B which may be used by target functions.
 *
 * Bank 1 functions handle error conditions and are at file offset
 * 0xFF6B-0x17E77 (CPU addresses 0x8000-0xFFFF with DPX=1).
 *
 * Original disassembly:
 *   0311: push 0x08
 *   0313: mov a, #0x03
 *   0315: push 0xe0
 *   0317: push 0x82
 *   0319: push 0x83
 *   031b: mov 0x08, #0x1b  ; R0 = 0x1B
 *   031e: mov 0x96, #0x01  ; DPX = 0x01 (bank 1)
 *   0321: ret
 */
void jump_bank_1(uint16_t reg_addr)
{
    /* Bank 1 dispatch - target address is in bank 1 (file 0x10000+)
     *
     * NOTE: Bank 1 code not yet implemented in our firmware.
     * Setting DPX=1 would cause execution to read NOPs from unmapped
     * memory and crash. Keeping DPX=0 makes this a no-op for now.
     * Bank 1 handlers will need to be implemented in bank 0 or
     * proper dual-bank firmware support added later.
     */
    (void)reg_addr;
    /* DPX = 0x01; -- DISABLED: No bank 1 code available */
}

/*
 * dispatch_0206 - USB/DMA status dispatch
 * Address: 0x0206-0x024A (69 bytes)
 *
 * This is an inline dispatch function (not a bank jump stub).
 * It reads R5/R7 and performs register operations for USB endpoint control.
 * Called after queue_idx_get_3291 which sets R7 and clears R5.
 *
 * When (R5 & 0x06) != 0: Write 0xA0 to REG 0xC8D4, copy XDATA 0x0056-0057 to
 *                        REG 0x905B-905C and 0xD802-D803
 * When (R5 & 0x06) == 0: Use R7|0x80 to configure REG 0xC8D4 and 0xC4ED,
 *                        then copy 0xC4EE-0xC4EF to 0xD802-D803
 *
 * Since this is called after queue_idx_get_3291 which clears R5,
 * the (R5 & 0x06) == 0 path is always taken.
 */
void dispatch_0206(void)
{
    uint8_t idx;
    uint8_t r2, r3;

    /* Get R7 value - in SDCC, we use the return value of queue_idx_get_3291 */
    /* which was called before us. Since we can't easily get R7, we'll read
     * from the same source: I_QUEUE_IDX */
    idx = *((__idata uint8_t *)0x0D);

    /* Path for (R5 & 0x06) == 0 - which is the common case */
    /* Write (R7 | 0x80) to REG 0xC8D4 */
    REG_DMA_CONFIG = idx | 0x80;

    /* Read REG 0xC4ED, mask with 0xC0, OR with R7, write back */
    r2 = REG_NVME_DMA_CTRL_ED;
    r2 = (r2 & 0xC0) | idx;
    REG_NVME_DMA_CTRL_ED = r2;

    /* Read REG 0xC4EE-0xC4EF and write to 0xD802-D803 */
    r3 = REG_NVME_DMA_ADDR_LO;
    r2 = REG_NVME_DMA_ADDR_HI;
    REG_USB_EP_BUF_DATA = r3;
    REG_USB_EP_BUF_PTR_LO = r2;
}

/*===========================================================================
 * Bank 0 Dispatch Functions (0x0322-0x03A7)
 * These all jump to 0x0300 (jump_bank_0)
 *===========================================================================*/

/*
 * handler_ca51 - System state handler
 * Address: 0xCA51-0xCAAF (95 bytes)
 *
 * Called via dispatch_0322. Handles system state transitions based on
 * G_EVENT_CTRL_09FA and G_TLP_BASE_LO (0x0AE1).
 *
 * Original disassembly:
 *   ca51: mov dptr, #0x09fa
 *   ca54: movx a, @dptr
 *   ca55: cjne a, #0x04, ca62   ; if 0x09FA == 4, do special init
 *   ca58-ca61: call helpers with R7=4 then R7=0
 *   ca62: mov dptr, #0x0ae1
 *   ca65: movx a, @dptr
 *   ca66: cjne a, #0x01, ca7d   ; if 0x0AE1 == 1, call 0x055c and set regs
 *   ca7d: cjne a, #0x02, ca8d   ; if 0x0AE1 == 2, clear bit 1 of 0x91C0
 *   ca8d: if 0x0AE1 == 4, do register config sequence
 *   caaf: ret
 */
static void handler_ca51(void)
{
    uint8_t event_ctrl;
    uint8_t tlp_base;

    /* Check if 0x09FA == 4 */
    event_ctrl = G_EVENT_CTRL_09FA;
    if (event_ctrl == 0x04) {
        /* Call helpers - simplified, original calls 0xDB66 then 0xE527 */
        /* TODO: Implement helper_db66 and helper_e527 if needed */
    }

    /* Check 0x0AE1 state */
    tlp_base = G_TLP_BASE_LO;

    if (tlp_base == 0x01) {
        /* lcall 0x055c -> targets Bank1:0xB91A - Bank 1 not implemented */
        jump_bank_1(0xB91A);
        /* Set bit 6 of 0x92E1, clear bit 6 of 0x92C2 */
        REG_POWER_EVENT_92E1 = (REG_POWER_EVENT_92E1 & 0xBF) | 0x40;
        REG_POWER_STATUS &= 0xBF;
        return;
    }

    if (tlp_base == 0x02) {
        /* Clear bit 1 of 0x91C0 */
        REG_USB_PHY_CTRL_91C0 &= 0xFD;
        return;
    }

    if (tlp_base == 0x04) {
        /* Clear bit 0 of 0xCC30 */
        REG_CPU_MODE &= 0xFE;
        /* Set bits 0-4 of 0xE710 (mask 0xE0, or 0x1F) */
        REG_LINK_WIDTH_E710 = (REG_LINK_WIDTH_E710 & 0xE0) | 0x1F;
        /* Clear bit 1 of 0x91C0 */
        REG_USB_PHY_CTRL_91C0 &= 0xFD;
        /* Call 0xBC00 with DPTR=0xCC3B - simplified, just modify the register */
        /* TODO: Implement helper_bc00 if needed */
        return;
    }
}

/* 0x0322: Target 0xCA51 - system_state_handler */
void dispatch_0322(void) { handler_ca51(); }

/* 0x0327: Target 0xB1CB - usb_power_init */
void dispatch_0327(void) { jump_bank_0(0xB1CB); }

/* 0x032C: Target 0x92C5 - REG_PHY_POWER config handler */
void phy_power_config_handler(void) { jump_bank_0(0x92C5); }  /* was: dispatch_032c */

/* 0x0331: Target 0xC4B3 - error_log_handler */
void dispatch_0331(void) { jump_bank_0(0xC4B3); }

/* 0x0336: Target 0xBF0F - reg_restore_handler */
void dispatch_0336(void) { jump_bank_0(0xBF0F); }

/*
 * usb_dma_trigger_a57a - Trigger USB DMA transfer
 * Address: 0xA57A-0xA580 (7 bytes)
 *
 * Writes 0x01 to REG_USB_DMA_TRIGGER (0x9092) to start USB DMA.
 *
 * Original disassembly:
 *   a57a: mov dptr, #0x9092
 *   a57d: mov a, #0x01
 *   a57f: movx @dptr, a
 *   a580: ret
 */
static void usb_dma_trigger_a57a(void)
{
    REG_USB_DMA_TRIGGER = 0x01;
}

/*
 * usb_dma_phase_d088 - USB DMA phase handler
 * Address: 0xD088-0xD0D8 (81 bytes)
 *
 * Checks G_USB_CTRL_STATE_07E1 and triggers DMA if state is 5.
 * This is called when 0x9091 bit 1 is set (data phase ready).
 *
 * Key logic:
 *   - If state == 5: call usb_dma_trigger_a57a and return
 *   - Otherwise: perform additional state machine handling
 *
 * Original disassembly (simplified path for state == 5):
 *   d088: mov dptr, #0x07e1
 *   d08b: movx a, @dptr        ; read state
 *   d08c: mov r7, a
 *   d08d: cjne a, #0x05, d094  ; if state != 5, skip
 *   d090: lcall 0xa57a         ; trigger DMA
 *   d093: ret
 */
static void usb_dma_phase_d088(void)
{
    uint8_t state = G_USB_CTRL_STATE_07E1;

    if (state == 0x05) {
        /* State 5: Ready to send descriptor, trigger DMA */
        usb_dma_trigger_a57a();
        return;
    }

    /* For other states, the original has more complex handling.
     * States 4 and 2 have special paths; otherwise calls 0xA57A anyway.
     * For now, trigger DMA for these as well to match basic behavior. */
    usb_dma_trigger_a57a();
}

/*
 * usb_setup_phase_a5a6 - USB setup phase handler
 * Address: 0xA5A6-0xA5E8 (67 bytes)
 *
 * Called when USB setup packet is received (0x9091 bit 0 set).
 * Initializes USB control transfer state.
 *
 * Key operations:
 *   - Clear G_USB_CTRL_STATE_07E1 to 0
 *   - Set G_TLP_STATE_07E9 to 1
 *   - Clear bit 1 of REG_USB_CONFIG (0x9002)
 *   - Various other state initializations
 *   - Write 0x01 to 0x9091 to acknowledge setup phase
 *
 * Original disassembly (key parts):
 *   a5a6: clr a
 *   a5a7: mov dptr, #0x07e1
 *   a5aa: movx @dptr, a        ; clear 0x07E1
 *   a5ab: mov dptr, #0x07e9
 *   a5ae: inc a                ; a = 1
 *   a5af: movx @dptr, a        ; set 0x07E9 = 1
 *   a5b0: mov dptr, #0x9002
 *   a5b3: movx a, @dptr
 *   a5b4: anl a, #0xfd         ; clear bit 1
 *   a5b6: movx @dptr, a
 *   ...
 *   a5e2: mov dptr, #0x9091
 *   a5e5: mov a, #0x01
 *   a5e7: movx @dptr, a        ; acknowledge setup phase
 */
static void usb_setup_phase_a5a6(void)
{
    uint8_t val;

    /* Clear USB control state */
    G_USB_CTRL_STATE_07E1 = 0;

    /* Set TLP state to 1 */
    G_TLP_STATE_07E9 = 1;

    /* Clear bit 1 of REG_USB_CONFIG */
    val = REG_USB_CONFIG;
    val &= 0xFD;
    REG_USB_CONFIG = val;

    /* Check G_PHY_LANE_CFG_0AE4 - if zero, do additional setup */
    if (G_PHY_LANE_CFG_0AE4 == 0) {
        /* Clear bit 0 of REG_POWER_MISC_CTRL (0x92C4) */
        val = REG_POWER_MISC_CTRL;
        val &= 0xFE;
        REG_POWER_MISC_CTRL = val;

        /* Write 0x04 then 0x02 to REG_TIMER1_CSR (0xCC17) */
        REG_TIMER1_CSR = 0x04;
        REG_TIMER1_CSR = 0x02;
    }

    /* Clear system flags */
    G_SYS_FLAGS_07EB = 0;

    /* Check and clear bit 2 of 0x9220 if set */
    val = REG_USB_STATUS_9220;
    if (val & 0x04) {
        val &= 0xFB;
        REG_USB_STATUS_9220 = val;
    }

    /* Clear TLP address offset */
    G_TLP_ADDR_OFFSET_LO = 0;

    /* Acknowledge setup phase by writing 0x01 to 0x9091 */
    REG_USB_CTRL_PHASE = 0x01;
}

/*
 * handler_cde7 - USB control transfer handler
 * Address: 0xCDE7-0xCE3C (86 bytes)
 *
 * Main USB control transfer state machine. Called via dispatch_033b from
 * the external interrupt handler when USB peripheral status bit 1 is set.
 *
 * Checks REG_USB_CTRL_PHASE (0x9091) bits and calls appropriate handlers:
 *   - Bit 0 set AND bit 2 clear: Setup phase - call usb_setup_phase_a5a6
 *   - Bit 1 set (with 0x9002 bit 1 clear): Data phase - call usb_dma_phase_d088
 *   - Bit 2 set: Status phase - call 0xDCD5 handler
 *   - Bit 3 set: Call 0xB286 handler
 *   - Bit 4 set: Call 0xB612 handler
 *
 * Original disassembly:
 *   cde7: mov dptr, #0x9091
 *   cdea: movx a, @dptr
 *   cdeb: jnb acc.0, cdf5      ; if bit 0 clear, skip setup phase
 *   cdee: movx a, @dptr
 *   cdef: jb acc.2, cdf5       ; if bit 2 set, skip setup phase
 *   cdf2: lcall 0xa5a6         ; call setup phase handler
 *   cdf5: mov dptr, #0x9002
 *   cdf8: movx a, @dptr
 *   cdf9: jb acc.1, ce0c       ; if 0x9002 bit 1 set, skip data phase
 *   cdfc: mov dptr, #0x9091
 *   cdff: movx a, @dptr
 *   ce00: jnb acc.1, ce0c      ; if bit 1 clear, skip data phase
 *   ce03: lcall 0xd088         ; call DMA phase handler
 *   ce06: mov dptr, #0x9091
 *   ce09: mov a, #0x02
 *   ce0b: movx @dptr, a        ; acknowledge data phase
 *   ce0c: ... (status phase handling)
 *   ce3c: ret
 */
static void handler_cde7(void)
{
    uint8_t flags;

    /* Check for setup phase: bit 0 set AND bit 2 clear */
    flags = REG_USB_CTRL_PHASE;
    if ((flags & 0x01) && !(flags & 0x04)) {
        usb_setup_phase_a5a6();
    }

    /* Check for data phase: 0x9002 bit 1 clear AND 0x9091 bit 1 set */
    if (!(REG_USB_CONFIG & 0x02)) {
        flags = REG_USB_CTRL_PHASE;
        if (flags & 0x02) {
            usb_dma_phase_d088();
            /* Acknowledge data phase */
            REG_USB_CTRL_PHASE = 0x02;
        }
    }

    /* Check for status phase: bit 2 set */
    flags = REG_USB_CTRL_PHASE;
    if (flags & 0x04) {
        /* TODO: Call status phase handler at 0xDCD5 */
        /* For now, just acknowledge */
        REG_USB_CTRL_PHASE = 0x04;
    }

    /* Check bit 3 */
    flags = REG_USB_CTRL_PHASE;
    if (flags & 0x08) {
        /* TODO: Call handler at 0xB286 */
        REG_USB_CTRL_PHASE = 0x08;
    }

    /* Check bit 4 */
    flags = REG_USB_CTRL_PHASE;
    if (flags & 0x10) {
        /* TODO: Call handler at 0xB612 */
        REG_USB_CTRL_PHASE = 0x10;
    }
}

/* 0x033B: Target 0xCDE7 - USB control transfer handler */
void dispatch_033b(void) { handler_cde7(); }

/* 0x0340: Target 0xBF8E - buffer_dispatch_bf8e */
void buffer_dispatch_bf8e(void) { jump_bank_0(0xBF8E); }  /* was: dispatch_0340 */

/*
 * helper_cc4c - Set bit 1 of USB PHY control
 * Address: 0xCC4C-0xCC55 (10 bytes)
 *
 * Modifies 0x91C0: clears bit 0, sets bit 1 (value = (val & 0xFD) | 0x02)
 *
 * Original disassembly:
 *   cc4c: mov dptr, #0x91c0
 *   cc4f: movx a, @dptr
 *   cc50: anl a, #0xfd
 *   cc52: orl a, #0x02
 *   cc54: movx @dptr, a
 *   cc55: ret
 */
static void helper_cc4c(void)
{
    REG_USB_PHY_CTRL_91C0 = (REG_USB_PHY_CTRL_91C0 & 0xFD) | 0x02;
}

/*
 * helper_cc59_with_dptr - Set bit 0 of register at DPTR
 * Address: 0xCC59-0xCC5F (7 bytes)
 *
 * Reads DPTR, clears bit 0, sets bit 0 (effectively just sets bit 0).
 * Used with DPTR=0xCC30 in nvme_queue_handler.
 *
 * Original disassembly:
 *   cc59: movx a, @dptr
 *   cc5a: anl a, #0xfe
 *   cc5c: orl a, #0x01
 *   cc5e: movx @dptr, a
 *   cc5f: ret
 */
static void helper_cc59_with_dptr(volatile __xdata uint8_t *reg)
{
    *reg = (*reg & 0xFE) | 0x01;
}

/*
 * helper_cc2d - Write value to E710, then toggle bit 2 of USB status
 * Address: 0xCC2D-0xCC3B (15 bytes)
 *
 * Writes A to DPTR (0xE710), then modifies 0x9000: sets bit 2, then clears it.
 *
 * Original disassembly:
 *   cc2d: movx @dptr, a       ; write to E710
 *   cc2e: mov dptr, #0x9000
 *   cc31: movx a, @dptr
 *   cc32: anl a, #0xfb       ; clear bit 2
 *   cc34: orl a, #0x04       ; set bit 2
 *   cc36: movx @dptr, a
 *   cc37: movx a, @dptr
 *   cc38: anl a, #0xfb       ; clear bit 2
 *   cc3a: movx @dptr, a
 *   cc3b: ret
 */
static void helper_cc2d(uint8_t val)
{
    REG_LINK_WIDTH_E710 = val;
    REG_USB_STATUS = (REG_USB_STATUS & 0xFB) | 0x04;  /* set bit 2 */
    REG_USB_STATUS &= 0xFB;  /* clear bit 2 */
}

/*
 * helper_cc3d - Set bit 6 of power status, return DPTR=0x92E1, A=0x10
 * Address: 0xCC3D-0xCC4B (15 bytes)
 *
 * Modifies 0x92C2: clears bit 6, sets bit 6 (always sets it).
 * Sets DPTR to 0x92E1 and A to 0x10 for subsequent write.
 *
 * Original disassembly:
 *   cc3d: mov dptr, #0x92c2
 *   cc40: movx a, @dptr
 *   cc41: anl a, #0xbf      ; clear bit 6
 *   cc43: orl a, #0x40      ; set bit 6
 *   cc45: movx @dptr, a
 *   cc46: mov dptr, #0x92e1
 *   cc49: mov a, #0x10
 *   cc4b: ret
 *
 * In C we return the value 0x10 to be written to 0x92E1.
 */
static uint8_t helper_cc3d(void)
{
    REG_POWER_STATUS = (REG_POWER_STATUS & 0xBF) | 0x40;
    return 0x10;
}

/*
 * helper_cc4f_with_dptr - Set bit 1 of register at DPTR
 * Address: 0xCC4F-0xCC55 (7 bytes)
 *
 * Same as cc4c entry point but takes a DPTR argument.
 * Used with DPTR=0xCC3B in nvme_queue_handler.
 *
 * Original disassembly:
 *   cc4f: movx a, @dptr
 *   cc50: anl a, #0xfd
 *   cc52: orl a, #0x02
 *   cc54: movx @dptr, a
 *   cc55: ret
 */
static void helper_cc4f_with_dptr(volatile __xdata uint8_t *reg)
{
    *reg = (*reg & 0xFD) | 0x02;
}

/*
 * helper_cc63 - Clear bits 0-1 of register at DPTR
 * Address: 0xCC63-0xCC69 (7 bytes)
 *
 * Called at 0x9C00 with DPTR=0x92CF.
 *
 * Original disassembly:
 *   cc63: movx a, @dptr
 *   cc64: anl a, #0xfc       ; clear bits 0-1
 *   cc66: orl a, #0x03       ; set bits 0-1
 *   cc68: movx @dptr, a
 *   cc69: ret
 *
 * Wait - this clears then sets, so it just sets bits 0-1.
 */
static void helper_cc63_with_dptr(volatile __xdata uint8_t *reg)
{
    *reg = (*reg & 0xFC) | 0x03;
}

/*
 * helper_54bb - Clear transfer control flag
 * Address: 0x54BB-0x54C2 (8 bytes)
 *
 * Clears G_XFER_CTRL_0AF7 to 0.
 *
 * Original disassembly:
 *   54bb: clr a
 *   54bc: mov dptr, #0x0af7
 *   54bf: movx @dptr, a
 *   54c0: ret
 */
static void helper_54bb(void)
{
    G_XFER_CTRL_0AF7 = 0;
}

/*
 * helper_cc56 - Set PHY config bit
 * Address: 0xCC56-0xCC5F (10 bytes)
 *
 * Sets bit 0 of REG_PHY_CFG_C6A8.
 *
 * Original disassembly:
 *   cc56: mov dptr, #0xc6a8
 *   cc59: movx a, @dptr
 *   cc5a: anl a, #0xfe
 *   cc5c: orl a, #0x01
 *   cc5e: movx @dptr, a
 *   cc5f: ret
 */
static void helper_cc56(void)
{
    REG_PHY_CFG_C6A8 = (REG_PHY_CFG_C6A8 & 0xFE) | 0x01;
}

/*
 * helper_d12a - Timer and state initialization
 * Address: 0xD12A-0xD149 (32 bytes)
 *
 * Clears G_STATE_WORK_0B3D and copies G_CMD_INDEX_SRC to G_CMD_SLOT_INDEX.
 *
 * Original disassembly:
 *   d12a: clr a
 *   d12b: mov dptr, #0x0b3d
 *   d12e: movx @dptr, a         ; clear 0x0B3D
 *   d12f: mov dptr, #0x0b3e
 *   d132: movx a, @dptr         ; read G_CMD_INDEX_SRC
 *   d133: mov r7, a
 *   d134: mov dptr, #0x05a4
 *   d137: movx @dptr, a         ; write to G_CMD_SLOT_INDEX
 *   ...
 */
static void helper_d12a(void)
{
    G_STATE_WORK_0B3D = 0;
    G_CMD_SLOT_INDEX = G_CMD_INDEX_SRC;
}

/*
 * helper_d387 - PHY and timer configuration
 * Address: 0xD387-0xD3A1 (27 bytes)
 *
 * Checks G_STATE_0AE8 and modifies REG_TIMER3_DIV.
 *
 * Original disassembly:
 *   d387: mov dptr, #0x0ae8
 *   d38a: movx a, @dptr
 *   d38b: jnz d3a1              ; if non-zero, return
 *   d38d: mov dptr, #0xcd28
 *   d390: movx a, @dptr
 *   d391: anl a, #0xfe
 *   d393: movx @dptr, a         ; clear bit 0
 *   ...
 */
static void helper_d387(void)
{
    if (G_STATE_0AE8 != 0) {
        return;
    }
    REG_TIMER3_DIV &= 0xFE;
}

/*
 * helper_c24c - Conditional state clear
 * Address: 0xC24C-0xC267 (28 bytes)
 *
 * Checks G_USB_STATE_CLEAR_06E3 and conditionally clears state.
 *
 * Original disassembly:
 *   c24c: mov dptr, #0x06e3
 *   c24f: movx a, @dptr
 *   c250: jz c267               ; if zero, skip to return
 *   c252: clr a
 *   c253: movx @dptr, a         ; clear 0x06E3
 *   c254: lcall 0x54bb          ; call helper_54bb
 *   ...
 */
static void helper_c24c(void)
{
    if (G_USB_STATE_CLEAR_06E3 == 0) {
        return;
    }
    G_USB_STATE_CLEAR_06E3 = 0;
    helper_54bb();
}

/*
 * helper_494d - Final USB/PCIe register initialization
 * Address: 0x494D-0x4975 (41 bytes)
 *
 * Modifies REG_NVME_DOORBELL and REG_USB_MSC_CFG.
 *
 * Original disassembly:
 *   494d: mov dptr, #0xc858
 *   4950: movx a, @dptr
 *   4951: anl a, #0xfe
 *   4953: movx @dptr, a         ; clear bit 0 of 0xC858
 *   4954: mov dptr, #0x9056
 *   4957: movx a, @dptr
 *   4958: anl a, #0xfb
 *   495a: movx @dptr, a         ; clear bit 2 of 0x9056
 *   ...
 */
static void helper_494d(void)
{
    REG_NVME_DOORBELL &= 0xFE;
    REG_USB_MSC_CFG &= 0xFB;
}

/*
 * helper_cc60 - Set link status bits
 * Address: 0xCC60-0xCC69 (10 bytes)
 *
 * Sets bits 0-1 of REG_LINK_STATUS_E716 to 0b11.
 *
 * Original disassembly:
 *   cc60: mov dptr, #0xe716
 *   cc63: movx a, @dptr
 *   cc64: anl a, #0xfc
 *   cc66: orl a, #0x03
 *   cc68: movx @dptr, a
 *   cc69: ret
 */
static void helper_cc60(void)
{
    REG_LINK_STATUS_E716 = (REG_LINK_STATUS_E716 & 0xFC) | 0x03;
}

/*
 * helper_cc79 - Clear USB transfer flags
 * Address: 0xCC79-0xCC82 (10 bytes)
 *
 * Clears G_USB_TRANSFER_FLAG (0x0B2E) and G_SYS_FLAGS_07E8.
 *
 * Original disassembly:
 *   cc79: clr a
 *   cc7a: mov dptr, #0x0b2e
 *   cc7d: movx @dptr, a
 *   cc7e: mov dptr, #0x07e8
 *   cc81: movx @dptr, a
 *   cc82: ret
 */
static void helper_cc79(void)
{
    G_USB_TRANSFER_FLAG = 0;
    G_SYS_FLAGS_07E8 = 0;
}

/*
 * init_bda4 - State initialization function
 * Address: 0xBDA4-0xBE20 (125 bytes)
 *
 * Called when power status bit 6 is clear. Clears many XDATA state variables
 * and calls several initialization functions.
 *
 * Original disassembly:
 *   bda4: clr a
 *   bda5-bdf9: clear state variables (0x07ED, 0x07EE, 0x0AF5, etc.)
 *   bdfa: lcall 0x54bb         ; clear 0x0AF7
 *   bdfd: lcall 0xcc56         ; set bit 0 of 0xC6A8
 *   be00: mov dptr, #0x92c8    ; clear bits 0,1 of 0x92C8
 *   be03-be0a: read-modify-write 0x92C8
 *   be0b: mov dptr, #0xcd31    ; write 0x04 then 0x02 to 0xCD31
 *   be14: lcall 0xd12a
 *   be17: lcall 0xd387
 *   be1a: lcall 0xdf86
 *   be1d: lcall 0xc24c
 *   be20: ljmp 0x494d
 */
static void init_bda4(void)
{
    /* Clear state variables (0xBDA4-0xBDF9) */
    G_SYS_FLAGS_07ED = 0;
    G_SYS_FLAGS_07EE = 0;
    G_EP_DISPATCH_OFFSET = 0;
    G_SYS_FLAGS_07EB = 0;
    G_STATE_FLAG_0AF1 = 0;
    G_TLP_STATE_0ACA = 0;
    G_USB_CTRL_STATE_07E1 = 0x05;  /* Special value */
    G_USB_TRANSFER_FLAG = 0;
    G_TLP_MASK_0ACB = 0;
    G_CMD_WORK_E3 = 0;
    G_USB_STATE_07E6 = 0;
    G_USB_STATE_07E7 = 0;     /* inc dptr from 0x07E6 */
    G_TLP_STATE_07E9 = 0;
    G_STATE_0B2D = 0;
    G_USB_STATE_07E2 = 0;
    G_EP_STATUS_CTRL = 0;
    G_WORK_0006 = 0;
    G_SYS_FLAGS_07E8 = 0;
    G_TRANSFER_ACTIVE = 0;
    G_TRANSFER_BUSY_0B3B = 0;
    G_XFER_FLAG_07EA = 0;

    /* lcall 0x54bb - clear 0x0AF7 */
    helper_54bb();

    /* lcall 0xcc56 - set bit 0 of 0xC6A8 */
    helper_cc56();

    /* 0xBE00-0xBE0A: Clear bits 0 and 1 of 0x92C8 (two separate R-M-W cycles) */
    REG_POWER_CTRL_92C8 &= 0xFE;  /* clear bit 0 */
    REG_POWER_CTRL_92C8 &= 0xFD;  /* clear bit 1 */

    /* 0xBE0B-0xBE13: Write 0x04 then 0x02 to 0xCD31 */
    REG_CPU_TIMER_CTRL_CD31 = 0x04;
    REG_CPU_TIMER_CTRL_CD31 = 0x02;

    /* Call helper functions */
    helper_d12a();          /* lcall 0xd12a */
    helper_d387();          /* lcall 0xd387 */
    /* helper_df86(); */    /* lcall 0xdf86 - TODO: implement */
    helper_c24c();          /* lcall 0xc24c */
    helper_494d();          /* ljmp 0x494d */
}

/*
 * nvme_queue_handler - NVMe queue status handler
 * Address: 0x9B95-0x9CF8 (356 bytes)
 *
 * Called via dispatch_0345. Handles NVMe queue state transitions and
 * timeout monitoring. Uses IDATA 0x38-0x3B as a 32-bit counter.
 *
 * Key logic:
 *   - Clear USB transfer flag (0x0B2E)
 *   - Clear bit 1 of timer control (0xCC3B)
 *   - Check TLP block size (0x0ACC) bit 1 - skip if set
 *   - Check power status (0x92C2) bit 6 - skip if clear
 *   - Perform power mode transitions based on 0x92F8
 *   - Main loop: count iterations, check for timeout (0x0005D000)
 *   - If REG 0x92FB == 1: initialization sequence
 *   - On timeout: reset sequence via helper_cc3d
 *
 * Original disassembly summary:
 *   9b95-9ba0: Clear 0x0B2E, modify 0xCC3B
 *   9ba1-9bac: Check 0x0ACC bit 1, 0x92C2 bit 6
 *   9baf-9c13: Power mode sequence (0x92F8, 0x92CF, 0x92C1, E712 polling)
 *   9c14-9c1c: Write 1 to 0x0AE1, call 0xCA51
 *   9c1d-9c25: Clear IDATA 0x38-0x3B
 *   9c26-9cd8: Main timeout loop with IDATA counter
 *   9cdb-9cf7: Timeout handler (helper_cc3d, reset sequence)
 *   9cf8: ret
 */
static void nvme_queue_handler(void)
{
    uint32_t counter;
    uint32_t inner_counter;
    uint8_t val;

    /* 0x9B95-0x9BA0: Clear 0x0B2E, clear bit 1 of 0xCC3B */
    G_USB_TRANSFER_FLAG = 0;
    REG_TIMER_CTRL_CC3B &= 0xFD;

    /* 0x9BA1-0x9BA5: Read 0x0ACC, check bit 1 */
    val = G_TLP_BLOCK_SIZE_0ACC;
    if (val & 0x02) {
        goto clear_counter;
    }

    /* 0x9BA8-0x9BAC: Read 0x92C2, check bit 6 */
    val = REG_POWER_STATUS;
    if (!(val & 0x40)) {
        goto clear_counter;
    }

    /* 0x9BAF-0x9BB8: Read 0x92F8, mask 0x0C, shift right 2 */
    val = REG_POWER_STATUS_92F8;
    val = (val & 0x0C) >> 2;
    if (val == 0) {
        goto set_ae1_and_call;
    }

    /* 0x9BBC-0x9BC7: Check 0x0AF0 bit 1, conditionally clear 0xC20F */
    if (G_FLASH_CFG_0AF0 & 0x02) {
        REG_PHY_LINK_MISC_C20F = 0;
    }

    /* 0x9BC8-0x9BDD: Modify 0x92CF and 0x92C1 */
    REG_POWER_CTRL_92CF &= 0xFC;              /* clear bits 0-1 */
    REG_POWER_CTRL_92CF = (REG_POWER_CTRL_92CF & 0xFB) | 0x04;  /* set bit 2 */
    REG_CLOCK_ENABLE = (REG_CLOCK_ENABLE & 0xEF) | 0x10;      /* set bit 4 */

    /* 0x9BDE-0x9BE4: Call helper at 0xE581 with R5=0x0A, R4=0, R7=0 */
    /* TODO: Implement helper_e581 - for now skip */

    /* 0x9BE7-0x9BF4: Poll 0xE712 waiting for bit 0, then check bit 1 */
    do {
        val = REG_USB_EP0_COMPLETE;
    } while (!(val & 0x01));

    val = REG_USB_EP0_COMPLETE & 0x02;
    if (val == 0) {
        /* Loop back - simplified: just continue */
        goto set_ae1_and_call;
    }

    /* 0x9BF6-0x9C06: Clear bit 4 of 0x92C1, modify 0x92CF */
    REG_CLOCK_ENABLE &= 0xEF;
    helper_cc63_with_dptr(&REG_POWER_CTRL_92CF);
    REG_POWER_CTRL_92CF &= 0xFB;  /* clear bit 2 */

    /* 0x9C07-0x9C13: Check 0x0AF0 bit 1, conditionally write 0xC8 to 0xC20F */
    if (G_FLASH_CFG_0AF0 & 0x02) {
        REG_PHY_LINK_MISC_C20F = 0xC8;
    }

set_ae1_and_call:
    /* 0x9C14-0x9C1C: Write 1 to 0x0AE1, call 0xCA51 */
    G_TLP_BASE_LO = 1;
    /* TODO: Call handler at 0xCA51 */

clear_counter:
    /* 0x9C1D-0x9C25: Clear IDATA 0x38-0x3B (32-bit counter) */
    I_WORK_38 = 0;
    I_WORK_39 = 0;
    I_WORK_3A = 0;
    I_WORK_3B = 0;

    /* Main loop at 0x9C26-0x9CD8 */
    while (1) {
        /* Load 32-bit counter from IDATA 0x38-0x3B (big-endian) */
        counter = ((uint32_t)I_WORK_38 << 24) | ((uint32_t)I_WORK_39 << 16) |
                  ((uint32_t)I_WORK_3A << 8) | I_WORK_3B;

        /* Compare with timeout value 0x0005D000 (387072) */
        if (counter >= 0x0005D000UL) {
            goto timeout_handler;
        }

        /* 0x9C3F-0x9C45: Check if 0x92FB == 1 */
        if (REG_POWER_MODE_92FB == 0x01) {
            /* 0x9C47-0x9C61: Initialization path */
            helper_cc4c();
            REG_USB_PHY_CTRL_91D1 = 0x01;
            helper_cc59_with_dptr(&REG_CPU_MODE);
            val = (REG_LINK_WIDTH_E710 & 0xE0) | 0x04;
            helper_cc2d(val);
            init_bda4();
            return;
        }

        /* 0x9C64-0x9C6B: Check 0x91D1 bit 0 */
        if (REG_USB_PHY_CTRL_91D1 & 0x01) {
            return;  /* Exit if bit 0 is set */
        }

        /* 0x9C6E-0x9C72: Check 0xE750 bit 2 */
        if (REG_QUEUE_STATUS_E750 & 0x04) {
            /* 0x9C75-0x9CB1: Inner loop with counter 0x3C-0x3F */
            I_QUEUE_WAIT_3C = 0;
            I_QUEUE_WAIT_3D = 0;
            I_QUEUE_WAIT_3E = 0;
            I_QUEUE_WAIT_3F = 0;

            while (1) {
                /* Load inner counter (big-endian) */
                inner_counter = ((uint32_t)I_QUEUE_WAIT_3C << 24) |
                                ((uint32_t)I_QUEUE_WAIT_3D << 16) |
                                ((uint32_t)I_QUEUE_WAIT_3E << 8) |
                                I_QUEUE_WAIT_3F;

                /* Compare with 0x00020000 (131072) */
                if (inner_counter >= 0x00020000UL) {
                    goto timeout_handler;
                }

                /* Check 0x91D1 bit 0 again */
                if (REG_USB_PHY_CTRL_91D1 & 0x01) {
                    return;
                }

                /* Increment inner counter */
                inner_counter++;
                I_QUEUE_WAIT_3F = inner_counter & 0xFF;
                I_QUEUE_WAIT_3E = (inner_counter >> 8) & 0xFF;
                I_QUEUE_WAIT_3D = (inner_counter >> 16) & 0xFF;
                I_QUEUE_WAIT_3C = (inner_counter >> 24) & 0xFF;
            }
        }

        /* 0x9CB3-0x9CBD: Check 0x0AE2 and 0xCC33 */
        if (G_SYSTEM_STATE_0AE2 == 0) {
            if (REG_CPU_EXEC_STATUS_2 & 0x04) {
                return;  /* Exit if 0xCC33 bit 2 is set */
            }
        }

        /* 0x9CC0-0x9CD8: Increment main counter and loop */
        counter++;
        I_WORK_3B = counter & 0xFF;
        I_WORK_3A = (counter >> 8) & 0xFF;
        I_WORK_39 = (counter >> 16) & 0xFF;
        I_WORK_38 = (counter >> 24) & 0xFF;
    }

timeout_handler:
    /* 0x9CDB-0x9CF7: Timeout/reset sequence */
    /* Call helper_cc3d, write result to 0x92E1 */
    val = helper_cc3d();
    REG_POWER_EVENT_92E1 = val;

    /* Call helper_cc4c */
    helper_cc4c();

    /* Read 0x91C0, clear bit 1, write back */
    REG_USB_PHY_CTRL_91C0 &= 0xFD;

    /* Write 0x01 to 0x91D1 */
    REG_USB_PHY_CTRL_91D1 = 0x01;

    /* Write 0x04 to 0x9300 */
    REG_BUF_CFG_9300 = 0x04;

    /* Call helper_cc4f with DPTR=0xCC3B */
    helper_cc4f_with_dptr(&REG_TIMER_CTRL_CC3B);
}

/* 0x0345: Target 0x9B95 - nvme_queue_handler */
void dispatch_0345(void) { nvme_queue_handler(); }

/*
 * handler_c465 - PHY handler with computed dispatch
 * Address: 0xC465-0xC537 (211 bytes)
 *
 * Called via dispatch_034a. Complex function with computed jump table
 * at 0x0DC7 based on REG_POWER_MODE_92FB value.
 *
 * Main flow:
 *   c465: lcall init_bda4
 *   c468: read 0x91C0
 *   c46c: if bit 1 set, return (jump to c4ce)
 *   c46f-c47a: read 0x92FB, computed dispatch via 0x0DC7
 *   c48f-c4ca: various state handling paths
 *   c4cb: lcall init_bda4
 *   c4ce: ret
 *
 * Key paths based on 0x9301 bit 6:
 *   - If bit 6 set: call helper_cc4c, modify 0xCC30, 0xE710, 0xCC3B
 *   - If bit 6 clear and 0x91D1 bit 3 clear: loop back to c46f
 *   - Otherwise: fall through to init_bda4 and return
 */
static void handler_c465(void)
{
    uint8_t val;

    /* lcall 0xbda4 - init state */
    init_bda4();

    /* Read 0x91C0, check bit 1 */
    val = REG_USB_PHY_CTRL_91C0;
    if (val & 0x02) {
        return;  /* bit 1 set, return early */
    }

    /* Main loop - simplified, skipping computed dispatch */
    while (1) {
        /* Read 0x92FB - power mode register */
        val = REG_POWER_MODE_92FB;

        /* Original does computed dispatch via 0x0DC7 based on val */
        /* For now, handle the main path at 0xC48F-0xC4CA */

        /* Check 0x9301 bit 6 */
        if (REG_BUF_CFG_9301 & 0x40) {
            /* Path at c4b2: call helper_cc4c, modify registers */
            helper_cc4c();
            helper_cc59_with_dptr(&REG_CPU_MODE);  /* set bit 0 of 0xCC30 */
            REG_LINK_WIDTH_E710 = (REG_LINK_WIDTH_E710 & 0xE0) | 0x04;
            REG_TIMER_CTRL_CC3B &= 0xFD;  /* clear bit 1 */
            break;
        }

        /* Check 0x91D1 bit 3 */
        if (!(REG_USB_PHY_CTRL_91D1 & 0x08)) {
            /* Loop back - but add safety exit to prevent infinite loop */
            break;
        }

        break;  /* Exit loop for safety */
    }

    /* lcall 0xbda4 - final init */
    init_bda4();
}

/* 0x034A: Target 0xC465 - phy_handler */
void dispatch_034a(void) { handler_c465(); }

/*
 * handler_e682 - PHY and transfer flag clear
 * Address: 0xE682-0xE688 (7 bytes)
 *
 * Called via dispatch_0354. Clears PHY config and USB transfer flags.
 *
 * Original disassembly:
 *   e682: lcall 0xcc56    ; helper_cc56() - set bit 0 of PHY config
 *   e685: lcall 0xcc79    ; helper_cc79() - clear USB transfer flags
 *   e688: ret
 */
static void handler_e682(void)
{
    helper_cc56();
    helper_cc79();
}

/*
 * handler_e6aa - Clear state and call state handler
 * Address: 0xE6AA-0xE6AE (5 bytes)
 *
 * Called via dispatch_034f. Sets R7=0 and jumps to state handler at 0xC324.
 *
 * Original disassembly:
 *   e6aa: clr a           ; a = 0
 *   e6ab: mov r7, a       ; r7 = 0
 *   e6ac: ljmp 0xc324     ; tail call to state handler
 *
 * The state handler at 0xC324 writes R7 to 0x0A7D and processes state.
 */
static void handler_e6aa(void)
{
    /* Write 0 to 0x0A7D and process state */
    G_EP_DISPATCH_VAL3 = 0;
    /* TODO: Implement full state_handler_c324 */
}

/* 0x034F: Target 0xE6AA - state clear handler */
void dispatch_034f(void) { handler_e6aa(); }

/* 0x0354: Target 0xE682 - PHY clear handler */
void dispatch_0354(void) { handler_e682(); }

/*
 * handler_e423 - Status check and conditional init handler
 * Address: 0xE423-0xE437 (21 bytes)
 *
 * Called via dispatch_0359. Checks power status and initializes if needed.
 *
 * Original disassembly:
 *   e423: lcall 0xcc60         ; call helper (set link status bits)
 *   e426: mov dptr, #0x92c2
 *   e429: movx a, @dptr        ; read REG_POWER_STATUS
 *   e42a: anl a, #0x40         ; isolate bit 6
 *   e42c: mov r7, a            ; save to R7
 *   e42d: swap a               ; swap nibbles: 0x40 -> 0x04
 *   e42e: rrc a                ; rotate right: 0x04 -> 0x02
 *   e42f: rrc a                ; rotate right: 0x02 -> 0x01
 *   e430: anl a, #0x03         ; mask to bits 0-1
 *   e432: jnz 0xe437           ; if non-zero (bit 6 was set), skip init
 *   e434: lcall 0xbda4         ; call init function
 *   e437: ret
 *
 * Logic: If (REG_POWER_STATUS & 0x40) == 0, call init_bda4()
 */
static void handler_e423(void)
{
    uint8_t status;

    /* Call helper to set link status bits */
    helper_cc60();

    /* Read power status and check bit 6 */
    status = REG_POWER_STATUS & 0x40;

    /* If bit 6 is clear, call init function */
    if (status == 0) {
        init_bda4();
    }
}

/* 0x0359: Target 0xE423 - status check and init handler */
void dispatch_0359(void)
{
    handler_e423();
}

/*
 * handler_e6bd - Stack manipulation stub
 * Address: 0xE6BD-0xE6BF (3 bytes)
 *
 * Original disassembly:
 *   e6bd: push 0x2b        ; push IDATA 0x2B to stack
 *   e6bf: ret
 *
 * This is an 8051-specific stack manipulation that cannot be directly
 * replicated in C. The function pushes a byte from the bit-addressable
 * IDATA region (0x2B) onto the stack then returns. May be a placeholder.
 */
void dispatch_035e(void)
{
    /* 8051 stack manipulation - no direct C equivalent */
}

/* 0x0363: Target 0xE969 - handler_e969 (stub) */
void dispatch_0363(void) { jump_bank_0(0xE969); }

/* 0x0368: Target 0xDF15 - handler_df15 */
void dispatch_0368(void) { jump_bank_0(0xDF15); }

/* 0x036D: Target 0xE96F - handler_e96f (stub) */
void dispatch_036d(void) { jump_bank_0(0xE96F); }

/* 0x0372: Target 0xE970 - handler_e970 (stub) */
void dispatch_0372(void) { jump_bank_0(0xE970); }

/* 0x0377: Target 0xE952 - handler_e952 (stub) */
void dispatch_0377(void) { jump_bank_0(0xE952); }

/* 0x037C: Target 0xE941 - handler_e941 (stub) */
void dispatch_037c(void) { jump_bank_0(0xE941); }

/* 0x0381: Target 0xE947 - handler_e947 (stub) */
void dispatch_0381(void) { jump_bank_0(0xE947); }

/* 0x0386: Target 0xE92C - handler_e92c (stub) */
void dispatch_0386(void) { jump_bank_0(0xE92C); }

/* 0x038B: Target 0xD2BD - handler_d2bd */
void dispatch_038b(void) { jump_bank_0(0xD2BD); }

/* 0x0390: Target 0xCD10 - handler_cd10 */
void dispatch_0390(void) { jump_bank_0(0xCD10); }

/*
 * handler_d5fb - USB poll wait handler
 * Address: 0xD5FB-0xD63C (66 bytes)
 *
 * Called via dispatch_0395. Handles USB endpoint polling state.
 *
 * Original disassembly:
 *   d5fb: mov dptr, #0x0b3d
 *   d5fe: movx a, @dptr      ; read G_STATE_WORK_0B3D
 *   d5ff: jz 0xd63c          ; if zero, return
 *   d601-d607: check 0x9091 bit 0, if set return
 *   d608-d60e: check 0x07E1 == 1
 *   d610-d624: check USB/endpoint status
 *   d631-d63b: write 0x04, 0x02, 0x01 to timer control
 *   d63c: ret
 */
static void handler_d5fb(void)
{
    /* Check G_STATE_WORK_0B3D */
    if (G_STATE_WORK_0B3D == 0) {
        return;
    }

    /* Check 0x9091 bit 0 (setup phase) */
    if (REG_USB_CTRL_PHASE & 0x01) {
        return;
    }

    /* Check 0x07E1 == 1 */
    if (G_USB_CTRL_STATE_07E1 != 0x01) {
        return;
    }

    /* Check USB status at 0x9000 */
    if (REG_USB_STATUS & 0x01) {
        /* Check 0xC471 bit 0 */
        if (REG_NVME_QUEUE_BUSY & 0x01) {
            return;
        }
        /* Check 0x000A */
        if (G_USB_CTRL_000A != 0) {
            return;
        }
    } else {
        /* Check 0x9101 bit 6 */
        if (REG_USB_PERIPH_STATUS & 0x40) {
            return;
        }
        /* Check I_USB_STATE (IDATA 0x6A) */
        if (I_USB_STATE != 0) {
            return;
        }
    }

    /* Write timer control sequence: 0x04, 0x02, 0x01 */
    REG_TIMER1_CSR = 0x04;
    REG_TIMER1_CSR = 0x02;
    REG_TIMER1_CSR = 0x01;
}

/* 0x0395: Target 0xD5FB - usb_poll_wait handler */
void dispatch_0395(void) { handler_d5fb(); }

/*
 * handler_d92e - USB buffer initialization handler
 * Address: 0xD92E-0xD968 (59 bytes)
 *
 * Called via dispatch_039a. Performs USB PHY and buffer initialization.
 *
 * Original disassembly:
 *   d92e: lcall 0xcc3d    ; set bit 6 of 0x92C2, return DPTR=0x92E1, A=0x10
 *   d931: lcall 0xcc2d    ; write A to @DPTR (0x10 to 0x92E1), toggle bit 2 of 0x9000
 *   d934: lcall 0xcc4c    ; set bit 1 of 0x91C0
 *   d937: mov dptr, #0x9090
 *   d93a: movx a, @dptr
 *   d93b: anl a, #0x7f    ; clear bit 7
 *   d93d: movx @dptr, a
 *   d93e: mov a, r7
 *   d93f: jz 0xd94a       ; if r7==0, skip helper call
 *   ...
 *   d94a: mov dptr, #0x9300
 *   d94d: mov a, #0x04
 *   d94f: movx @dptr, a   ; write 0x04 to 0x9300
 *   d950-d967: register init sequence
 *   d968: ret
 */
static void handler_d92e(void)
{
    /* lcall 0xcc3d + lcall 0xcc2d combined:
     * cc3d sets bit 6 of 0x92C2, sets DPTR=0x92E1, A=0x10
     * cc2d writes A (0x10) to @DPTR (0x92E1), then toggles bit 2 of 0x9000
     */
    REG_POWER_STATUS = (REG_POWER_STATUS & 0xBF) | 0x40;  /* set bit 6 */
    REG_POWER_EVENT_92E1 = 0x10;
    REG_USB_STATUS = (REG_USB_STATUS & 0xFB) | 0x04;  /* set bit 2 */
    REG_USB_STATUS &= 0xFB;  /* clear bit 2 */

    /* lcall 0xcc4c - set bit 1 of 0x91C0 */
    helper_cc4c();

    /* Clear bit 7 of 0x9090 */
    REG_USB_INT_MASK_9090 &= 0x7F;

    /* Skip conditional helper call (R7 check) - simplified */

    /* Register init sequence */
    REG_BUF_CFG_9300 = 0x04;
    REG_USB_PHY_CTRL_91D1 = 0x02;
    REG_BUF_CFG_9301 = 0x40;
    REG_BUF_CFG_9301 = 0x80;
    REG_USB_PHY_CTRL_91D1 = 0x08;
    REG_USB_PHY_CTRL_91D1 = 0x01;
}

/* 0x039A: Target 0xD92E - usb_buffer_handler */
void dispatch_039a(void) { handler_d92e(); }

/* 0x039F: Target 0xD916 - pcie_dispatch_d916 */
void pcie_dispatch_d916(uint8_t param) { (void)param; jump_bank_0(0xD916); }  /* was: dispatch_039f */

/* 0x03A4: Target 0xCB37 - power_ctrl_cb37 */
void dispatch_03a4(void) { jump_bank_0(0xCB37); }

/*===========================================================================
 * Bank 1 Dispatch Functions (0x03A9-0x0411)
 * These all jump to 0x0311 (jump_bank_1)
 * Bank 1 CPU addr = file offset - 0x7F6B (e.g., 0x89DB -> file 0x10946)
 *===========================================================================*/

/* 0x03A9: Target Bank1:0x89DB (file 0x109DB) - handler_89db */
void dispatch_03a9(void) { jump_bank_1(0x89DB); }

/* 0x03AE: Target Bank1:0xEF3E (file 0x16F3E) - handler_ef3e */
void dispatch_03ae(void) { jump_bank_1(0xEF3E); }

/* 0x03B3: Target Bank1:0xA327 (file 0x12327) - handler_a327 */
void dispatch_03b3(void) { jump_bank_1(0xA327); }

/* 0x03B8: Target Bank1:0xBD76 (file 0x13D76) - handler_bd76 */
void dispatch_03b8(void) { jump_bank_1(0xBD76); }

/* 0x03BD: Target Bank1:0xDDE0 (file 0x15DE0) - handler_dde0 */
void dispatch_03bd(void) { jump_bank_1(0xDDE0); }

/* 0x03C2: Target Bank1:0xE12B (file 0x1612B) - handler_e12b */
void dispatch_03c2(void) { jump_bank_1(0xE12B); }

/* 0x03C7: Target Bank1:0xEF42 (file 0x16F42) - handler_ef42 */
void dispatch_03c7(void) { jump_bank_1(0xEF42); }

/* 0x03CC: Target Bank1:0xE632 (file 0x16632) - handler_e632 */
void dispatch_03cc(void) { jump_bank_1(0xE632); }

/* 0x03D1: Target Bank1:0xD440 (file 0x15440) - handler_d440 */
void dispatch_03d1(void) { jump_bank_1(0xD440); }

/* 0x03D6: Target Bank1:0xC65F (file 0x1465F) - handler_c65f */
void dispatch_03d6(void) { jump_bank_1(0xC65F); }

/* 0x03DB: Target Bank1:0xEF46 (file 0x16F46) - handler_ef46 */
void dispatch_03db(void) { jump_bank_1(0xEF46); }

/* 0x03E0: Target Bank1:0xE01F (file 0x1601F) - handler_e01f */
void dispatch_03e0(void) { jump_bank_1(0xE01F); }

/* 0x03E5: Target Bank1:0xCA52 (file 0x14A52) - handler_ca52 */
void dispatch_03e5(void) { jump_bank_1(0xCA52); }

/* 0x03EA: Target Bank1:0xEC9B (file 0x16C9B) - handler_ec9b */
void dispatch_03ea(void) { jump_bank_1(0xEC9B); }

/* 0x03EF: Target Bank1:0xC98D (file 0x1498D) - handler_c98d */
void dispatch_03ef(void) { jump_bank_1(0xC98D); }

/* 0x03F4: Target Bank1:0xDD1A (file 0x15D1A) - handler_dd1a */
void dispatch_03f4(void) { jump_bank_1(0xDD1A); }

/* 0x03F9: Target Bank1:0xDD7E (file 0x15D7E) - handler_dd7e */
void dispatch_03f9(void) { jump_bank_1(0xDD7E); }

/* 0x03FE: Target Bank1:0xDA30 (file 0x15A30) - handler_da30 */
void dispatch_03fe(void) { jump_bank_1(0xDA30); }

/* 0x0403: Target Bank1:0xBC5E (file 0x13C5E) - handler_bc5e */
void dispatch_0403(void) { jump_bank_1(0xBC5E); }

/* 0x0408: Target Bank1:0xE89B (file 0x1689B) - handler_e89b */
void dispatch_0408(void) { jump_bank_1(0xE89B); }

/* 0x040D: Target Bank1:0xDBE7 (file 0x15BE7) - handler_dbe7 */
void dispatch_040d(void) { jump_bank_1(0xDBE7); }

/*===========================================================================
 * Mixed Bank Dispatch Functions (0x0412-0x04DE)
 *===========================================================================*/

/* 0x0412: Target 0xE617 - handler_e617 */
void dispatch_0412(uint8_t param) { (void)param; jump_bank_0(0xE617); }

/* 0x0417: Target 0xE62F - handler_e62f */
void dispatch_0417(void) { jump_bank_0(0xE62F); }

/* 0x041C: Target 0xE647 - handler_e647 */
void dispatch_041c(uint8_t param) { (void)param; jump_bank_0(0xE647); }

/* 0x0421: Target 0xE65F - handler_e65f */
void dispatch_0421(uint8_t param) { (void)param; jump_bank_0(0xE65F); }

/* 0x0426: Target 0xE762 (Bank 0) - Note: different from handler_e762 in Bank 1! */
void dispatch_0426(void) { jump_bank_0(0xE762); }

/* 0x042B: Target 0xE4F0 - handler_e4f0 */
void dispatch_042b(void) { jump_bank_0(0xE4F0); }

/* 0x0430: Target 0x9037 - nvme_config_handler */
void dispatch_0430(void) { jump_bank_0(0x9037); }

/* 0x0435: Target 0xD127 - handler_d127 */
void dispatch_0435(void) { jump_bank_0(0xD127); }

/* 0x043A: Target 0xE677 - handler_e677 */
void dispatch_043a(void) { jump_bank_0(0xE677); }

/* 0x043F: Target 0xE2A6 - handler_e2a6 */
void dispatch_043f(void) { jump_bank_0(0xE2A6); }

/* 0x0444: Target 0xA840 - handler_a840 */
void dispatch_0444(void) { jump_bank_0(0xA840); }

/* 0x0449: Target 0xDD78 - handler_dd78 */
void dispatch_0449(void) { jump_bank_0(0xDD78); }

/* 0x044E: Target 0xE91D - pcie_dispatch_e91d */
void pcie_dispatch_e91d(void) { jump_bank_0(0xE91D); }  /* was: dispatch_044e */

/* 0x0453: Target 0xE902 - handler_e902 */
void dispatch_0453(void) { jump_bank_0(0xE902); }

/* 0x0458: Target 0xE77A - handler_e77a */
void dispatch_0458(void) { jump_bank_0(0xE77A); }

/* 0x045D: Target 0xC00D - pcie_tunnel_enable (defined in pcie.c) */
void dispatch_045d(void) { jump_bank_0(0xC00D); }

/* 0x0467: Target 0xE57D - handler_e57d */
void dispatch_0467(void) { jump_bank_0(0xE57D); }

/* 0x046C: Target 0xCDC6 - handler_cdc6 */
void dispatch_046c(void) { jump_bank_0(0xCDC6); }

/* 0x0471: Target 0xE8A9 - handler_e8a9 */
void dispatch_0471(void) { jump_bank_0(0xE8A9); }

/* 0x0476: Target 0xE8D9 - handler_e8d9 */
void dispatch_0476(void) { jump_bank_0(0xE8D9); }

/* 0x047B: Target 0xD436 - handler_d436 */
void dispatch_047b(void) { jump_bank_0(0xD436); }

/* 0x0480: Target 0xE84D - handler_e84d */
void dispatch_0480(void) { jump_bank_0(0xE84D); }

/* 0x0485: Target 0xE85C - handler_e85c */
void dispatch_0485(void) { jump_bank_0(0xE85C); }

/* 0x048A: Target Bank1:0xECE1 (file 0x16CE1) - handler_ece1 */
void dispatch_048a(void) { jump_bank_1(0xECE1); }

/* 0x048F: Target Bank1:0xEF1E (file 0x16F1E) - handler_ef1e */
void dispatch_048f(void) { jump_bank_1(0xEF1E); }

/* 0x0494: Target Bank1:0xE56F (file 0x1656F) - event_handler_e56f */
void dispatch_0494(void) { jump_bank_1(0xE56F); }

/* 0x0499: Target Bank1:0xC0A5 (file 0x140A5) - handler_c0a5 */
void dispatch_0499(void) { jump_bank_1(0xC0A5); }

/* 0x049E: Target 0xE957 - sys_timer_handler_e957 */
void dispatch_049e(void) { jump_bank_0(0xE957); }

/* 0x04A3: Target 0xE95B - handler_e95b */
void dispatch_04a3(void) { jump_bank_0(0xE95B); }

/* 0x04A8: Target 0xE79B - handler_e79b */
void dispatch_04a8(void) { jump_bank_0(0xE79B); }

/* 0x04AD: Target 0xE7AE - handler_e7ae */
void dispatch_04ad(void) { jump_bank_0(0xE7AE); }

/* 0x04B2: Target 0xE971 - reserved_stub */
void dispatch_04b2(void) { jump_bank_0(0xE971); }

/* 0x04B7: Target 0xE597 - handler_e597 */
void dispatch_04b7(void) { jump_bank_0(0xE597); }

/* 0x04BC: Target 0xE14B - handler_e14b */
void dispatch_04bc(void) { jump_bank_0(0xE14B); }

/* 0x04C1: Target 0xBE02 - dma_handler_be02 */
void dispatch_04c1(void) { jump_bank_0(0xBE02); }

/* 0x04C6: Target 0xDBF5 - handler_dbf5 */
void dispatch_04c6(void) { jump_bank_0(0xDBF5); }

/*
 * handler_dfae - Timer/link handler
 * Address: 0xDFAE-0xDFD5 (40 bytes)
 *
 * Called via dispatch_04cb. Checks timer status and handles link events.
 *
 * Original disassembly:
 *   dfae: mov dptr, #0xcc17
 *   dfb1: lcall 0xc033    ; read timer status, returns flags in R7
 *   dfb4: mov a, r7
 *   dfb5: jnb acc.0, dfbf ; if bit 0 clear, skip
 *   dfb8-dfbe: clear bit 0 of 0x92C4
 *   dfbf: mov a, r7
 *   dfc0: jnb acc.1, dfd5 ; if bit 1 clear, return
 *   dfc3-dfd2: call helpers, check 0x046E, optionally call 0xC930
 *   dfd5: ret
 */
static void handler_dfae(void)
{
    uint8_t flags;

    /* Call helper at 0xC033 with DPTR=0xCC17 - simplified, just read register */
    flags = REG_TIMER1_CSR;  /* 0xCC17 is timer control */

    /* If bit 0 set, clear bit 0 of 0x92C4 */
    if (flags & 0x01) {
        REG_POWER_MISC_CTRL &= 0xFE;
    }

    /* If bit 1 set, handle link event */
    if (flags & 0x02) {
        /* Simplified: check G_LINK_FLAG_046E, clear if set */
        if (G_LINK_FLAG_046E != 0) {
            G_LINK_FLAG_046E = 0;
            /* TODO: Call 0xC930 link handler */
        }
    }
}

/*
 * helper_e31a - PHY vendor initialization
 * Address: 0xE31A-0xE333 (26 bytes)
 *
 * Checks PHY vendor control bit 5, and if not set, triggers USB init.
 *
 * Original disassembly:
 *   e31a: mov dptr, #0xc656   ; PHY vendor control
 *   e31d: movx a, @dptr       ; Read register
 *   e31e: jb acc.5, 0xe333    ; If bit 5 set, skip init
 *   e321: mov dptr, #0x06e3   ; USB state flag
 *   e324: mov a, #0x01
 *   e326: movx @dptr, a       ; Set to 1 to trigger USB init
 *   e327: mov dptr, #0xc656   ; PHY control
 *   e32a: lcall 0xc049        ; Configure PHY
 *   e32d: mov dptr, #0xc65b   ; PHY control 2
 *   e330: lcall 0xc049        ; Configure PHY
 *   e333: ret
 */
/*
 * helper_c049 - Set bit 5 of PHY register
 * Address: 0xC049-0xC04F (7 bytes)
 *
 * Reads register at DPTR, forces bit 5 to 1, writes back.
 * Called with DPTR pointing to PHY control register.
 *
 * Original disassembly:
 *   c049: movx a, @dptr    ; Read register
 *   c04a: anl a, #0xdf     ; Clear bit 5
 *   c04c: orl a, #0x20     ; Set bit 5
 *   c04e: movx @dptr, a    ; Write back
 *   c04f: ret
 */
static void helper_c049(__xdata uint8_t *reg)
{
    uint8_t val = *reg;
    val = (val & 0xDF) | 0x20;  /* Force bit 5 set */
    *reg = val;
}

static void helper_e31a(void)
{
    uint8_t phy_status;

    /* Read PHY vendor control register */
    phy_status = REG_PHY_EXT_56;

    uart_puts("[e31a:");
    uart_puthex(phy_status);

    /* If bit 5 is already set, PHY is ready, skip init */
    if (phy_status & 0x20) {
        uart_puts("=skip]");
        return;
    }

    uart_puts("=init]");

    /* Bit 5 not set - trigger USB initialization */
    G_USB_STATE_CLEAR_06E3 = 1;

    /* Configure PHY registers - set bit 5 on control registers */
    helper_c049(&REG_PHY_EXT_56);    /* 0xC656 */
    helper_c049(&REG_PHY_EXT_5B);    /* 0xC65B */
}

/*
 * usb_phy_setup_c24c - Comprehensive USB PHY setup
 * Address: 0xC24C-0xC2B7 (108 bytes)
 *
 * Initializes USB PHY and related state when G_USB_STATE_CLEAR_06E3 is set.
 * This is the main USB initialization function.
 *
 * Original disassembly:
 *   c24c: mov dptr, #0x06e3   ; Check USB state flag
 *   c24f: movx a, @dptr       ; Read flag
 *   c250: jz 0xc2b7           ; If 0, skip all init
 *   c252: clr a; movx @dptr, a; [clear 06E3-06E5, 05A4, 06E8, 05A9-05AA]
 *   c268: lcall 0x54bb        ; Clear 0x0AF7
 *   c26b: [setup B401, call helpers, clear buffers]
 *   c2ae: lcall 0x3a2b        ; Final setup
 *   c2b1: mov dptr, #0x05b1; mov a, #0x10; movx @dptr, a  ; Set init flag
 *   c2b7: ret
 */
/*
 * helper_9941 - Set bit 0 of register at DPTR
 * Address: 0x9941-0x9947 (7 bytes)
 *
 * Original disassembly:
 *   9941: movx a, @dptr    ; Read
 *   9942: anl a, #0xfe     ; Clear bit 0
 *   9944: orl a, #0x01     ; Set bit 0
 *   9946: movx @dptr, a    ; Write back
 *   9947: ret
 */
static void helper_9941(__xdata uint8_t *reg)
{
    uint8_t val = *reg;
    val = (val & 0xFE) | 0x01;  /* Set bit 0 */
    *reg = val;
}

/*
 * helper_e612 - Clear PHY register bit 0 if param has bit 0 set
 * Address: 0xe612-0xe61d (12 bytes)
 *
 * If R7 bit 0 is set, clears bit 0 of 0xC659
 */
static void helper_e612(uint8_t param)
{
    if (param & 0x01) {
        REG_PHY_EXT_59 = REG_PHY_EXT_59 & 0xFE;  /* 0xC659: clear bit 0 */
    }
}

/*
 * helper_cb08 - Read B402 with bit 1 cleared
 * Address: 0xcb08-0xcb0e (7 bytes)
 *
 * Reads REG_PCIE_TUNNEL_STATUS (0xB402) and returns value with bit 1 cleared.
 * Note: dptr is set to 0xB402 after this call.
 *
 * Original disassembly:
 *   cb08: mov dptr, #0xb402  ; dptr = B402
 *   cb0b: movx a, @dptr      ; A = XDATA[B402]
 *   cb0c: anl a, #0xfd       ; A &= 0xFD (clear bit 1)
 *   cb0e: ret
 */
static uint8_t helper_cb08(void)
{
    return REG_PCIE_TUNNEL_STATUS & 0xFD;
}

/*
 * helper_cac3 - Read tunnel config register 0x6041
 * Address: 0xcac3-0xcac9 (7 bytes)
 *
 * Generic memory read helper that reads from XDATA[0x6041].
 * Used by c7a4 for read-modify-write of tunnel config.
 *
 * Original disassembly:
 *   cac3: mov r2, #0x60
 *   cac5: mov r3, #0x02
 *   cac7: mov r1, #0x41
 *   cac9: ljmp 0x0ba0   ; Memory read helper
 */
static uint8_t helper_cac3(void)
{
    return REG_TUNNEL_HW_CFG_6041;
}

/*
 * helper_0bbe - Write to tunnel config register 0x6041
 * Address: 0x0bbe-0x0bc6 (called with R2=0x60, R3=0x02, R1=0x41)
 *
 * Generic memory write helper that writes A to XDATA[0x6041].
 * Used by c7a4 for read-modify-write of tunnel config.
 */
static void helper_0bbe(uint8_t val)
{
    REG_TUNNEL_HW_CFG_6041 = val;
}

/*
 * helper_d530 - Tunnel link configuration helper
 * Address: 0xd530-0xd555+ (complex)
 *
 * Configures tunnel link registers based on R5/R4/R7 parameters.
 * Called from bea0 with R5=0xC7, R4=0x00, R7=0x02.
 */
static void helper_d530(void)
{
    /* TODO: Implement full helper_d530 */
    /* This function configures tunnel PHY registers 0x78xx/0x79xx */
}

/*
 * helper_e581 - Tunnel completion wait helper
 * Address: 0xe581-0xe591 (17 bytes)
 *
 * Waits for CC11 bit 1 to be set, then writes 0x02 to CC11.
 * Called from bea0 state machine.
 *
 * Original disassembly:
 *   e581: lcall 0xe292       ; Write R6:R7 to CC82:CC83, set CC81 bits
 *   e584: mov dptr, #0xcc11  ; Loop start
 *   e587: movx a, @dptr
 *   e588: jnb acc.1, 0xe584  ; Wait for bit 1
 *   e58b: mov dptr, #0xcc11
 *   e58e: mov a, #0x02
 *   e590: movx @dptr, a
 *   e591: ret
 */
static void helper_e581(uint8_t r4, uint8_t r5, uint8_t r7)
{
    (void)r4;
    (void)r5;
    (void)r7;
    /* TODO: Implement full e581 with e292 sub-call */
    /* For now, just write completion flag */
    /* REG_CC11 = 0x02; */
}

/*
 * helper_bea0 - USB link state machine
 * Address: 0xbea0-0xbf1b (124 bytes)
 *
 * Complex state machine for USB link mode transitions.
 * Updates B434 lane configuration based on link mode (R7 param).
 *
 * State variables:
 *   0x0AA9 - Link mode parameter (from R7)
 *   0x0AAA - Loop counter (0-3)
 *   0x0AAB - Lane mask from B434
 *   0x0AAC - State index (doubles each iteration)
 *
 * Original disassembly summary:
 *   bea0: Write R7 to 0x0AA9
 *   bea5: Set 0x0AAC = 1
 *   beab: Read B434 & 0x0F, store to 0x0AAB
 *   beb5: Clear 0x0AAA
 *   beba: Loop start - check 0x0AA9 < 0x0F
 *   bec2: If >= 0x0F, branch to bedb
 *   bec4-bed9: Complex bit manipulation with 0x0AAB, 0x0AAC, 0x0AA9
 *   bedb-bee2: Alternative path when >= 0x0F
 *   bee9-bef1: Update 0x0AAB
 *   bef2-befb: Update B434 low nibble with 0x0AAB
 *   befc: Call d530 (tunnel config)
 *   bf05: Call e581 with R5=0xC7, R4=0, R7=0x02 (wait for completion)
 *   bf08-bf0e: Double 0x0AAC (shift left)
 *   bf0f-bf14: Increment 0x0AAA
 *   bf15-bf19: Loop while 0x0AAA < 4
 *   bf1b: ret
 */
static void helper_bea0(uint8_t link_mode)
{
    uint8_t lane_mask;
    uint8_t state_idx;
    uint8_t loop_cnt;
    uint8_t temp;

    /* bea0-bea4: Write link_mode to 0x0AA9 */
    G_TLP_COUNT_LO = link_mode;  /* 0x0AA9 */

    /* bea5-beaa: Set state index to 1 */
    G_STATE_COUNTER_0AAC = 1;    /* 0x0AAC */

    /* beab-beb4: Read B434 low nibble as lane mask */
    lane_mask = REG_PCIE_LINK_STATE & 0x0F;
    G_STATE_HELPER_0AAB = lane_mask;  /* 0x0AAB */

    /* beb5-beb9: Clear loop counter */
    G_TLP_STATUS = 0;            /* 0x0AAA */

    /* Loop up to 4 times (beba-bf19) */
    while (1) {
        /* beba-bec0: Read 0x0AA9 and check if < 0x0F */
        link_mode = G_TLP_COUNT_LO;

        if (link_mode < 0x0F) {
            /* bec4-bed9: Path when link_mode < 0x0F */
            temp = G_STATE_HELPER_0AAB;  /* Read lane mask */
            if (temp != link_mode) {
                /* Complex bit manipulation */
                state_idx = G_STATE_COUNTER_0AAC ^ 0x0F;  /* XOR with 0x0F */
                temp = (link_mode | state_idx) & temp;
            } else {
                /* bee2 path - values match, just return */
                return;
            }
        } else {
            /* bedb-bee8: Path when link_mode >= 0x0F */
            temp = G_STATE_HELPER_0AAB;
            if (temp == 0x0F) {
                /* bee2 - exact match, return */
                return;
            }
            temp = G_STATE_COUNTER_0AAC | temp;  /* ORL with state_idx */
        }

        /* bee9-beec: Update lane mask */
        G_STATE_HELPER_0AAB = temp;

        /* beed-befb: Update B434 low nibble with new lane mask */
        temp = G_STATE_HELPER_0AAB;
        lane_mask = REG_PCIE_LINK_STATE & 0xF0;  /* Keep high nibble */
        REG_PCIE_LINK_STATE = lane_mask | temp;  /* Set low nibble */

        /* befc: Call d530 tunnel config */
        helper_d530();

        /* bf05: Call e581 wait for completion */
        helper_e581(0x00, 0xC7, 0x02);

        /* bf08-bf0e: Double state index (shift left = add to self) */
        state_idx = G_STATE_COUNTER_0AAC;
        G_STATE_COUNTER_0AAC = state_idx + state_idx;

        /* bf0f-bf14: Increment loop counter */
        loop_cnt = G_TLP_STATUS;
        G_TLP_STATUS = loop_cnt + 1;

        /* bf15-bf19: Check if loop_cnt >= 4 */
        if ((loop_cnt + 1) >= 4) {
            break;
        }
    }
}

/*
 * helper_c7a4 - USB link mode configuration
 * Address: 0xc7a4-0xc808 (101 bytes)
 *
 * Configures USB link mode with full register setup.
 * Called during USB PHY initialization to set link parameters.
 *
 * Original disassembly:
 *   c7a4-c7b2: Write param to 0x0AA7, save B402 bit 1 to 0x0AA8
 *   c7b3-c7b6: Call cb08, write result to B402 (clears bit 1)
 *   c7b7-c7c5: If param != 0x0F, set bit 6 of 0x6041
 *   c7c8: Call bea0 state machine
 *   c7cb-c7d9: If param != 0x0F, clear bit 6 of 0x6041
 *   c7dc-c7e7: If original B402 bit 1 was set, restore it
 *   c7e8-c808: Update B436 based on param and B404
 */
static void helper_c7a4(uint8_t param)
{
    uint8_t link_state_save;
    uint8_t temp;

    /* c7a4-c7a8: Write param to G_USB_LINK_MODE (0x0AA7) */
    G_USB_LINK_MODE = param;

    /* c7a9-c7b2: Read B402 bit 1, save to 0x0AA8 */
    link_state_save = REG_PCIE_TUNNEL_STATUS & 0x02;
    G_USB_LINK_STATE = link_state_save;

    /* c7b3-c7b6: Call cb08, write result back to B402 (clears bit 1) */
    temp = helper_cb08();
    REG_PCIE_TUNNEL_STATUS = temp;

    /* c7b7-c7be: Check if param != 0x0F */
    param = G_USB_LINK_MODE;
    if (param != 0x0F) {
        /* c7c0-c7c5: Read 0x6041, set bit 6, write back */
        temp = helper_cac3();
        temp |= 0x40;
        helper_0bbe(temp);
    }

    /* c7c8: Call bea0 state machine */
    helper_bea0(param);

    /* c7cb-c7d2: Check if param != 0x0F again */
    param = G_USB_LINK_MODE;
    if (param != 0x0F) {
        /* c7d4-c7d9: Read 0x6041, clear bit 6, write back */
        temp = helper_cac3();
        temp &= 0xBF;
        helper_0bbe(temp);
    }

    /* c7dc-c7e7: If original B402 bit 1 was set, restore it */
    if (G_USB_LINK_STATE != 0) {
        temp = helper_cb08();
        temp |= 0x02;
        REG_PCIE_TUNNEL_STATUS = temp;
    }

    /* c7e8-c7f3: Mask param with 0x0E, update B436 low nibble */
    param = G_USB_LINK_MODE;
    param &= 0x0E;
    temp = REG_PCIE_LANE_CONFIG & 0xF0;  /* Keep high nibble */
    REG_PCIE_LANE_CONFIG = temp | param;

    /* c7f4-c808: Read B404, XOR low nibble with 0x0F, swap nibbles,
     * keep high nibble only, then merge into B436 */
    temp = REG_PCIE_LINK_PARAM_B404 & 0x0F;
    temp ^= 0x0F;                         /* Invert low nibble */
    temp = (temp << 4) & 0xF0;           /* Swap: move to high nibble */
    param = REG_PCIE_LANE_CONFIG & 0x0F;  /* Get current low nibble */
    REG_PCIE_LANE_CONFIG = param | temp;  /* Combine */
}

/*
 * helper_3a2b - USB DMA initialization
 * Address: 0x3a2b-0x3a60 (54 bytes)
 *
 * Initializes DMA and USB link state for enumeration.
 */
static void helper_3a2b(void)
{
    /* Clear log counter and system flags */
    G_LOG_COUNTER_044B = 0;       /* 0x044B */
    G_SYS_INIT_FLAG = 0;          /* 0x0000 */
    G_LINK_FLAG_046E = 0;         /* 0x046E */

    /* Configure DMA registers */
    REG_DMA_STATUS2 = REG_DMA_STATUS2 & 0xFE;  /* 0xC8D8: clear bit 0 */
    REG_DMA_CTRL = 0;             /* 0xC8D7 = 0 */
    REG_DMA_QUEUE_IDX = 0;        /* 0xC8D5 = 0 */
    G_DMA_STATE_057A = 0;         /* 0x057A = 0 */
}

/*
 * helper_c6d7 - Tunnel adapter configuration
 * Address: 0xc6d7-0xc73d (103 bytes)
 *
 * Copies adapter configuration from globals 0x0A52-0x0A54 to B4xx tunnel registers.
 * Configures both primary (B410-B423) and secondary (B41A-B42B) tunnel paths.
 *
 * Original disassembly:
 *   c6d7: mov dptr, #0x0a53   ; G_PCIE_ADAPTER_CFG_HI
 *   c6da: movx a, @dptr       ; Read adapter config
 *   c6db: mov r7, a
 *   c6dc: mov dptr, #0xb410   ; REG_TUNNEL_CFG_A_LO
 *   c6df: movx @dptr, a       ; Write to B410
 *   ... (continues with B411-B42B configuration)
 */
static void helper_c6d7(void)
{
    uint8_t cfg_hi, cfg_lo, cfg_mode;

    /* Read adapter configuration from globals */
    cfg_hi = G_PCIE_ADAPTER_CFG_HI;  /* 0x0A53 */
    cfg_lo = G_PCIE_ADAPTER_CFG_LO;  /* 0x0A52 */
    cfg_mode = G_PCIE_ADAPTER_MODE;  /* 0x0A54 */

    /* Configure primary tunnel path (B410-B423) */
    REG_TUNNEL_CFG_A_LO = cfg_hi;      /* B410 = 0x0A53 */
    REG_TUNNEL_CFG_A_HI = cfg_lo;      /* B411 = 0x0A52 */
    /* B412 from CAD8 helper - credits */
    REG_TUNNEL_CFG_MODE = cfg_mode;    /* B413 = 0x0A54 */

    /* Set tunnel capabilities (fixed values) */
    REG_TUNNEL_CAP_0 = 0x06;           /* B415 = 0x06 */
    REG_TUNNEL_CAP_1 = 0x04;           /* B416 = 0x04 */
    REG_TUNNEL_CAP_2 = 0x00;           /* B417 = 0x00 */

    /* Configure secondary tunnel path (B41A-B42B) */
    REG_TUNNEL_LINK_CFG_LO = cfg_hi;   /* B41A = 0x0A53 */
    REG_TUNNEL_LINK_CFG_HI = cfg_lo;   /* B41B = 0x0A52 */
    /* B418 from CAD8 helper - path credits */
    REG_TUNNEL_PATH_MODE = cfg_mode;   /* B419 = 0x0A54 */

    /* Tunnel status registers */
    REG_TUNNEL_STATUS_0 = cfg_hi;      /* B422 = 0x0A53 */
    REG_TUNNEL_STATUS_1 = cfg_lo;      /* B423 = 0x0A52 */

    /* Set secondary capabilities (fixed values) */
    REG_TUNNEL_CAP2_0 = 0x06;          /* B425 = 0x06 */
    REG_TUNNEL_CAP2_1 = 0x04;          /* B426 = 0x04 */
    REG_TUNNEL_CAP2_2 = 0x00;          /* B427 = 0x00 */

    /* Configure auxiliary path */
    REG_TUNNEL_PATH2_CRED = cfg_hi;    /* B428 = 0x0A53 */
    REG_TUNNEL_PATH2_MODE = cfg_lo;    /* B429 = 0x0A52 */
}

/*
 * helper_cc83 - Critical USB tunnel register setup
 * Address: 0xcc83-0xccda (88 bytes)
 *
 * Configures USB/PCIe tunnel hardware for USB enumeration.
 * This is called from usb_phy_setup_c24c and is critical for USB to work.
 *
 * Original disassembly:
 *   cc83: mov dptr, #0xca06   ; REG_CPU_MODE_NEXT
 *   cc86: movx a, @dptr
 *   cc87: anl a, #0xef        ; Clear bit 4
 *   cc89: movx @dptr, a
 *   cc8a: lcall 0xc6d7        ; Tunnel adapter config
 *   cc8d: mov r3, #0x02       ; Write to XDATA
 *   cc8f: mov r2, #0x40
 *   cc91: mov r1, #0x84
 *   cc93: mov a, #0x22
 *   cc95: lcall 0x0bbe        ; Write 0x22 to 0x4084
 *   cc98: mov r2, #0x50
 *   cc9a: lcall 0x0bbe        ; Write 0x22 to 0x5084
 *   cc9d: mov dptr, #0xb401   ; REG_PCIE_TUNNEL_CTRL
 *   cca0: lcall 0x9941        ; Set bit 0
 *   cca3: mov dptr, #0xb482   ; REG_TUNNEL_ADAPTER_MODE
 *   cca6: lcall 0x9941        ; Set bit 0
 *   cca9: movx a, @dptr       ; Read B482
 *   ccaa: anl a, #0x0f        ; Keep low nibble
 *   ccac: orl a, #0xf0        ; Set high nibble to 0xF0
 *   ccae: movx @dptr, a       ; Write back
 *   ccaf: mov dptr, #0xb401
 *   ccb2: movx a, @dptr
 *   ccb3: anl a, #0xfe        ; Clear bit 0
 *   ccb5: lcall 0x993d        ; Write back
 *   ccb8: mov dptr, #0xb430   ; REG_TUNNEL_LINK_STATE
 *   ccbb: movx a, @dptr
 *   ccbc: anl a, #0xfe        ; Clear bit 0
 *   ccbe: movx @dptr, a
 *   ccbf: mov dptr, #0xb298   ; REG_PCIE_TUNNEL_CFG
 *   ccc2: movx a, @dptr
 *   ccc3: anl a, #0xef        ; Clear bit 4
 *   ccc5: orl a, #0x10        ; Set bit 4
 *   ccc7: movx @dptr, a
 *   ccc8: mov r2, #0x60
 *   ccca: mov r1, #0x43
 *   cccc: mov a, #0x70
 *   ccce: lcall 0x0bbe        ; Write 0x70 to 0x6043
 *   ccd1: mov r1, #0x25
 *   ccd3: lcall 0x0ba0        ; Read 0x6025
 *   ccd6: anl a, #0x7f        ; Clear bit 7
 *   ccd8: orl a, #0x80        ; Set bit 7
 *   ccda: ljmp 0x0bbe         ; Write back to 0x6025
 */
static void helper_cc83(void)
{
    uint8_t val;

    /* CC83-CC89: Clear bit 4 of CA06 */
    REG_CPU_MODE_NEXT = REG_CPU_MODE_NEXT & 0xEF;

    /* CC8A: Call tunnel adapter configuration */
    helper_c6d7();

    /* CC8D-CC9A: Write 0x22 to tunnel hardware config registers */
    REG_TUNNEL_HW_CFG_4084 = 0x22;
    REG_TUNNEL_HW_CFG_5084 = 0x22;

    /* CC9D-CCA0: Set bit 0 of B401 */
    helper_9941(&REG_PCIE_TUNNEL_CTRL);

    /* CCA3-CCA6: Set bit 0 of B482 */
    helper_9941(&REG_TUNNEL_ADAPTER_MODE);

    /* CCA9-CCAE: Configure B482 - keep low nibble, set high nibble to 0xF0 */
    val = REG_TUNNEL_ADAPTER_MODE;
    val = (val & 0x0F) | 0xF0;
    REG_TUNNEL_ADAPTER_MODE = val;

    /* CCAF-CCB5: Clear bit 0 of B401 */
    val = REG_PCIE_TUNNEL_CTRL;
    val = val & 0xFE;
    REG_PCIE_TUNNEL_CTRL = val;

    /* CCB8-CCBE: Clear bit 0 of B430 */
    val = REG_TUNNEL_LINK_STATE;
    val = val & 0xFE;
    REG_TUNNEL_LINK_STATE = val;

    /* CCBF-CCC7: Set bit 4 of B298 (tunnel enable - critical!) */
    val = REG_PCIE_TUNNEL_CFG;
    val = (val & 0xEF) | 0x10;
    REG_PCIE_TUNNEL_CFG = val;

    /* CCC8-CCCE: Write 0x70 to 0x6043 */
    REG_TUNNEL_HW_CFG_6043 = 0x70;

    /* CCD1-CCDA: Read 0x6025, set bit 7, write back */
    val = REG_TUNNEL_HW_CFG_6025;
    val = (val & 0x7F) | 0x80;
    REG_TUNNEL_HW_CFG_6025 = val;
}

/*
 * helper_clear_usb_work_area - Clear USB work area 0x05B0-0x06E1
 *
 * Clears the PCIe transaction table and work area.
 * This is a loop in the original firmware at 0xc294-0xc2ab.
 */
static void helper_clear_usb_work_area(void)
{
    __xdata uint8_t *ptr;
    uint16_t i;

    /* Clear 0x05B0-0x06E1 (306 bytes) */
    ptr = (__xdata uint8_t *)0x05B0;
    for (i = 0; i < 0x132; i++) {
        ptr[i] = 0;
    }
}

static void usb_phy_setup_c24c(void)
{
    /* Check if USB initialization is requested */
    if (G_USB_STATE_CLEAR_06E3 == 0) {
        return;  /* No init requested */
    }

    /* c252-c267: Clear USB state flags */
    G_USB_STATE_CLEAR_06E3 = 0;   /* 0x06E3 */
    G_LOG_ENTRY_COUNT_06E4 = 1;   /* 0x06E4 = 1 */
    G_MAX_LOG_ENTRIES = 1;        /* 0x06E5 = 1 */
    G_CMD_SLOT_STATE = 0;         /* 0x05A4 */
    G_WORK_06E8 = 0;              /* 0x06E8 */
    G_CMD_WORK_05A9 = 0;          /* 0x05A9 */
    G_CMD_WORK_05AA = 0;          /* 0x05AA */

    /* c268: lcall 0x54bb - Clear transfer control flag */
    helper_54bb();

    /* c26b-c274: Pulse bit 0 on B401 (set then clear) */
    helper_9941(&REG_PCIE_TUNNEL_CTRL);   /* c26e: Set bit 0 */
    REG_PCIE_TUNNEL_CTRL &= 0xFE;         /* c271-c274: Clear bit 0 */

    /* c275: lcall 0xCC83 - critical register setup */
    helper_cc83();

    /* c278-c27e: Clear bit 4 of CA06 again after cc83 returns */
    REG_CPU_MODE_NEXT &= 0xEF;

    /* c281-c283: lcall 0xe612 with R7=1 - clear PHY register */
    helper_e612(0x01);

    /* c286-c288: lcall 0xc7a4 with R7=0x0F - USB link mode setup */
    helper_c7a4(0x0F);

    /* c28b-c292: Clear IDATA 0x62 and XDATA 0x06E2 */
    I_PCIE_TXN_DATA_1 = 0;
    G_XFER_STATE_06E2 = 0;

    /* c294-c2ab: Clear USB work area 0x05B0-0x06E1 (306 bytes) */
    helper_clear_usb_work_area();

    /* c2ae: lcall 0x3a2b - USB/DMA link initialization */
    helper_3a2b();

    /* c2b1-c2b6: Set USB link flag - this signals USB init is complete */
    G_PCIE_ADDR_2 = 0x10;         /* 0x05B1 = 0x10 */

    uart_puts("[c24c:done]");

    /*
     * helper_e34d - Enable PHY interrupt for USB enumeration
     * Address: 0xe34d-0xe365 (25 bytes)
     *
     * This function is called during USB init (when R6==0 at 0x921f) to enable
     * the PHY/USB enumeration interrupt. Critical for USB to enumerate!
     *
     * Original disassembly:
     *   e34d: mov dptr, #0xc6bd   ; PHY link control
     *   e350: movx a, @dptr
     *   e351: anl a, #0xfe        ; Clear bit 0
     *   e353: movx @dptr, a
     *   e354: mov dptr, #0xc801   ; REG_INT_ENABLE
     *   e357: lcall 0xc049        ; Set bit 5 (0x20)
     *   e35a: mov dptr, #0xcc33   ; CPU exec status 2
     *   e35d: mov a, #0x04
     *   e35f: movx @dptr, a
     *   e360: inc dptr            ; CC34
     *   e361: movx a, @dptr
     *   e362: anl a, #0xfb        ; Clear bit 2
     *   e364: movx @dptr, a
     *   e365: ret
     */
    {
        uint8_t val;

        /* e34d-e353: Clear bit 0 of C6BD (PHY link control) */
        val = REG_PHY_LINK_CTRL_BD;
        REG_PHY_LINK_CTRL_BD = val & 0xFE;

        /* e354-e357: Set bit 5 of REG_INT_ENABLE (via c049) */
        /* c049 does: read, clear bit 5, set bit 5, write - net effect is set bit 5 */
        helper_c049(&REG_INT_ENABLE);

        /* e35a-e35f: Write 0x04 to CC33 */
        REG_CPU_EXEC_STATUS_2 = 0x04;

        /* e360-e364: Clear bit 2 of CC34 */
        val = REG_CPU_EXEC_CTRL_2;
        REG_CPU_EXEC_CTRL_2 = val & 0xFB;

        uart_puts("[e34d:int-enable]");
    }

    /* DEBUG: Call USB soft-connect to enable enumeration
     * In original firmware, this is called via state machine when XDATA[0x07E4] == 5
     * Function cc27 enables USB D+ pull-up for host detection.
     *
     * cc27 Original disassembly:
     *   cc27: mov dptr, #0x9090  ; USB interrupt mask
     *   cc2a: movx a, @dptr
     *   cc2b: anl a, #0x7f       ; Clear bit 7 (enable pull-up)
     *   cc2d: movx @dptr, a
     *   cc2e: mov dptr, #0x9000  ; USB status
     *   cc31: movx a, @dptr
     *   cc32: anl a, #0xfb       ; Clear bit 2
     *   cc34: orl a, #0x04       ; Set bit 2
     *   cc36: movx @dptr, a
     *   cc37: movx a, @dptr
     *   cc38: anl a, #0xfb       ; Clear bit 2 again
     *   cc3a: movx @dptr, a
     *   cc3b: ret
     */
    {
        uint8_t val;
        /* Clear bit 7 of 9090 - enables USB D+ pull-up */
        val = REG_USB_INT_MASK_9090;
        REG_USB_INT_MASK_9090 = val & 0x7F;

        /* Toggle bit 2 of 9000 */
        val = REG_USB_STATUS;
        val = (val & 0xFB) | 0x04;  /* Set bit 2 */
        REG_USB_STATUS = val;
        val = REG_USB_STATUS;
        REG_USB_STATUS = val & 0xFB;  /* Clear bit 2 */

        uart_puts("[cc27:soft-connect]");
    }
}

/*
 * handler_e570 - USB PHY initialization handler
 * Address: 0xE570-0xE580 (17 bytes)
 *
 * Called via dispatch_04d5 during USB initialization.
 * Clears state variables and calls PHY init helpers.
 *
 * Original disassembly:
 *   e570: clr a
 *   e571: mov dptr, #0x05a5   ; PCIE transaction count
 *   e574: movx @dptr, a       ; Clear 0x05A5
 *   e575: inc dptr            ; 0x05A6
 *   e576: movx @dptr, a       ; Clear 0x05A6
 *   e577: mov dptr, #0x0b2e   ; USB transfer flag
 *   e57a: movx @dptr, a       ; Clear 0x0B2E
 *   e57b: lcall 0xe31a        ; PHY vendor init
 *   e57e: ljmp 0xc24c         ; USB PHY setup
 */
static void handler_e570(void)
{
    /* Debug: Show we're entering USB init */
    uart_puts("[USB]");

    /* Clear PCIE transaction counters */
    G_PCIE_TXN_COUNT_LO = 0;   /* 0x05A5 */
    G_PCIE_TXN_COUNT_HI = 0;   /* 0x05A6 */

    /* Clear USB transfer flag */
    G_USB_TRANSFER_FLAG = 0;  /* 0x0B2E */

    /* Call PHY vendor init - sets G_USB_STATE_CLEAR_06E3 if needed */
    helper_e31a();

    /* Jump to USB PHY setup - does actual init if flag is set */
    usb_phy_setup_c24c();
}

/* 0x04CB: Target 0xDFAE - timer/link handler */
void dispatch_04cb(void) { handler_dfae(); }

/* 0x04D0: Target 0xCE79 - timer_link_handler */
void dispatch_04d0(void) { jump_bank_0(0xCE79); }

/* 0x04D5: Target 0xE570 - USB PHY init handler */
void dispatch_04d5(void) { handler_e570(); }

/* 0x04DA: Target 0xE3B7 - handler_e3b7 */
void dispatch_04da(void) { jump_bank_0(0xE3B7); }

/*===========================================================================
 * Event/Interrupt Dispatch Functions (0x04DF-0x0650)
 *===========================================================================*/

/* 0x04DF: Target 0xE95F - handler_e95f (stub) */
void dispatch_04df(void) { jump_bank_0(0xE95F); }

/* 0x04E4: Target 0xE2EC - handler_e2ec */
void dispatch_04e4(void) { jump_bank_0(0xE2EC); }

/* 0x04E9: Target 0xE8E4 - handler_e8e4 */
void dispatch_04e9(void) { jump_bank_0(0xE8E4); }

/* 0x04EE: Target 0xE6FC - pcie_dispatch_e6fc */
void pcie_dispatch_e6fc(void) { jump_bank_0(0xE6FC); }  /* was: dispatch_04ee */

/* 0x04F3: Target 0x8A89 - handler_8a89 */
void dispatch_04f3(void) { jump_bank_0(0x8A89); }

/* 0x04F8: Target 0xDE16 - handler_de16 */
void dispatch_04f8(void) { jump_bank_0(0xDE16); }

/* 0x04FD: Target 0xE96C - pcie_dispatch_e96c (stub) */
void pcie_dispatch_e96c(void) { jump_bank_0(0xE96C); }  /* was: dispatch_04fd */

/* 0x0502: Target 0xD7CD - handler_d7cd */
void dispatch_0502(void) { jump_bank_0(0xD7CD); }

/* 0x0507: Target 0xE50D - handler_e50d */
void dispatch_0507(void) { jump_bank_0(0xE50D); }

/* 0x050C: Target 0xE965 - handler_e965 (stub) */
void dispatch_050c(void) { jump_bank_0(0xE965); }

/* 0x0511: Target 0xE95D - handler_e95d (stub) */
void dispatch_0511(void) { jump_bank_0(0xE95D); }

/* 0x0516: Target 0xE96E - handler_e96e (stub) */
void dispatch_0516(void) { jump_bank_0(0xE96E); }

/* 0x051B: Target 0xE1C6 - handler_e1c6 */
void dispatch_051b(void) { jump_bank_0(0xE1C6); }

/* 0x0520: Target 0x8A81 - system_init_from_flash */
void dispatch_0520(void) { system_init_from_flash(); }

/* 0x0525: Target 0x8D77 - system_init_from_flash (NOTE: Bank 0 but address is in Bank 1 range!) */
void dispatch_0525(void) { system_init_from_flash(); }

/* 0x052A: Target 0xE961 - handler_e961 (stub) */
void dispatch_052a(void) { jump_bank_0(0xE961); }

/* 0x052F: Target 0xAF5E - debug_output_handler */
void dispatch_052f(void) { jump_bank_0(0xAF5E); }

/* 0x0534: Target 0xD6BC - scsi_dispatch_d6bc */
void scsi_dispatch_d6bc(void) { jump_bank_0(0xD6BC); }  /* was: dispatch_0534 */

/* 0x0539: Target 0xE963 - handler_e963 (stub) */
void dispatch_0539(void) { jump_bank_0(0xE963); }

/* 0x053E: Target 0xE967 - handler_e967 (stub) */
void dispatch_053e(void) { jump_bank_0(0xE967); }

/* 0x0543: Target 0xE953 - handler_e953 (stub) */
void dispatch_0543(void) { jump_bank_0(0xE953); }

/* 0x0548: Target 0xE955 - handler_e955 (stub) */
void dispatch_0548(void) { jump_bank_0(0xE955); }

/* 0x054D: Target 0xE96A - handler_e96a (stub) */
void dispatch_054d(void) { jump_bank_0(0xE96A); }

/* 0x0552: Target 0xE96B - handler_e96b (stub) */
void dispatch_0552(void) { jump_bank_0(0xE96B); }

/* 0x0557: Target 0xDA51 - handler_da51 */
void dispatch_0557(void) { jump_bank_0(0xDA51); }

/* 0x055C: Target 0xE968 - handler_e968 (stub) */
void dispatch_055c(void) { jump_bank_0(0xE968); }

/* 0x0561: Target 0xE966 - handler_e966 (stub) */
void dispatch_0561(void) { jump_bank_0(0xE966); }

/* 0x0566: Target 0xE964 - handler_e964 (stub) */
void dispatch_0566(void) { jump_bank_0(0xE964); }

/* 0x056B: Target 0xE962 - handler_e962 (stub) */
void dispatch_056b(void) { jump_bank_0(0xE962); }

/* 0x0570: Target Bank1:0xE911 (file 0x16911) - error_handler_e911 */
void dispatch_0570(void) { jump_bank_1(0xE911); }

/* 0x0575: Target Bank1:0xEDBD (file 0x16DBD) - handler_edbd */
void dispatch_0575(void) { jump_bank_1(0xEDBD); }

/* 0x057A: Target Bank1:0xE0D9 (file 0x160D9) - handler_e0d9 */
void dispatch_057a(void) { jump_bank_1(0xE0D9); }

/* 0x057F: Target 0xB8DB - handler_b8db */
void dispatch_057f(void) { jump_bank_0(0xB8DB); }

/* 0x0584: Target Bank1:0xEF24 (file 0x16F24) - handler_ef24 */
void dispatch_0584(void) { jump_bank_1(0xEF24); }

/* 0x0589: Target 0xD894 - phy_register_config */
void dispatch_0589(void) { jump_bank_0(0xD894); }

/* 0x058E: Target 0xE0C7 - handler_e0c7 */
void dispatch_058e(void) { jump_bank_0(0xE0C7); }

/* 0x0593: Target 0xC105 - handler_c105 */
void dispatch_0593(void) { jump_bank_0(0xC105); }

/* 0x0598: Target Bank1:0xE06B (file 0x1606B) - handler_e06b */
void dispatch_0598(void) { jump_bank_1(0xE06B); }

/* 0x059D: Target Bank1:0xE545 (file 0x16545) - handler_e545 */
void dispatch_059d(void) { jump_bank_1(0xE545); }

/* 0x05A2: Target 0xC523 - pcie_handler_c523 */
void dispatch_05a2(void) { jump_bank_0(0xC523); }

/* 0x05A7: Target 0xD1CC - handler_d1cc */
void dispatch_05a7(void) { jump_bank_0(0xD1CC); }

/* 0x05AC: Target Bank1:0xE74E (file 0x1674E) - handler_e74e */
void dispatch_05ac(void) { jump_bank_1(0xE74E); }

/* 0x05B1: Target 0xD30B - handler_d30b */
void dispatch_05b1(void) { jump_bank_0(0xD30B); }

/* 0x05B6: Target Bank1:0xE561 (file 0x16561) - handler_e561 */
void dispatch_05b6(void) { jump_bank_1(0xE561); }

/* 0x05BB: Target 0xD5A1 - handler_d5a1 */
void dispatch_05bb(void) { jump_bank_0(0xD5A1); }

/* 0x05C0: Target 0xC593 - pcie_handler_c593 */
void dispatch_05c0(void) { jump_bank_0(0xC593); }

/* 0x05C5: Target Bank1:0xE7FB (file 0x167FB) - handler_e7fb */
void dispatch_05c5(void) { jump_bank_1(0xE7FB); }

/* 0x05CA: Target Bank1:0xE890 (file 0x16890) - handler_e890 */
void dispatch_05ca(void) { jump_bank_1(0xE890); }

/* 0x05CF: Target 0xC17F - pcie_handler_c17f */
void dispatch_05cf(void) { jump_bank_0(0xC17F); }

/* 0x05D4: Target 0xB031 - handler_b031 */
void dispatch_05d4(void) { jump_bank_0(0xB031); }

/* 0x05D9: Target Bank1:0xE175 (file 0x16175) - handler_e175 */
void dispatch_05d9(void) { jump_bank_1(0xE175); }

/* 0x05DE: Target Bank1:0xE282 (file 0x16282) - handler_e282 */
void dispatch_05de(void) { jump_bank_1(0xE282); }

/* 0x05E3: Target Bank1:0xB103 - pd_debug_print_flp
 * Bank 1 Address: 0xB103-0xB148 (approx 70 bytes) [actual addr: 0x1306E]
 * Original: mov dptr, #0xb103; ajmp 0x0311
 */
void dispatch_05e3(void) { pd_debug_print_flp(); }

/* 0x05E8: Target Bank1:0x9D90 (file 0x11D90) - protocol_nop_handler */
void dispatch_05e8(void) { jump_bank_1(0x9D90); }

/* 0x05ED: Target Bank1:0xD556 (file 0x15556) - handler_d556 */
void dispatch_05ed(void) { jump_bank_1(0xD556); }

/* 0x05F2: Target 0xDBBB - handler_dbbb */
void dispatch_05f2(void) { jump_bank_0(0xDBBB); }

/* 0x05F7: Target Bank1:0xD8D5 (file 0x158D5) - handler_d8d5 */
void dispatch_05f7(void) { jump_bank_1(0xD8D5); }

/* 0x05FC: Target Bank1:0xDAD9 (file 0x15AD9) - handler_dad9 */
void dispatch_05fc(void) { jump_bank_1(0xDAD9); }

/* 0x0601: Target 0xEA7C - handler_ea7c */
void dispatch_0601(void) { jump_bank_0(0xEA7C); }

/* 0x0606: Target 0xC089 - pcie_handler_c089 */
void dispatch_0606(void) { jump_bank_0(0xC089); }

/* 0x060B: Target Bank1:0xE1EE (file 0x161EE) - handler_e1ee */
void dispatch_060b(void) { jump_bank_1(0xE1EE); }

/* 0x0610: Target Bank1:0xED02 (file 0x16D02) - handler_ed02 */
void dispatch_0610(void) { jump_bank_1(0xED02); }

/* 0x0615: Target Bank1:0xEEF9 (file 0x16EF9) - handler_eef9 (NOPs) */
void dispatch_0615(void) { jump_bank_1(0xEEF9); }

/* 0x061A: Target Bank1:0xA066 (file 0x12066) - error_handler_a066 */
void dispatch_061a(void) { jump_bank_1(0xA066); }

/* 0x061F: Target Bank1:0xE25E (file 0x1625E) - handler_e25e */
void dispatch_061f(void) { jump_bank_1(0xE25E); }

/* 0x0624: Target Bank1:0xE2C9 (file 0x162C9) - handler_e2c9 */
void dispatch_0624(void) { jump_bank_1(0xE2C9); }

/* 0x0629: Target Bank1:0xE352 (file 0x16352) - handler_e352 */
void dispatch_0629(void) { jump_bank_1(0xE352); }

/* 0x062E: Target Bank1:0xE374 (file 0x16374) - handler_e374 */
void dispatch_062e(void) { jump_bank_1(0xE374); }

/* 0x0633: Target Bank1:0xE396 (file 0x16396) - handler_e396 */
void dispatch_0633(void) { jump_bank_1(0xE396); }

/* 0x0638: Target Bank1:0xE478 (file 0x16478) - pcie_transfer_handler */
void pcie_transfer_handler(void) { jump_bank_1(0xE478); }  /* was: dispatch_0638 */

/* 0x063D: Target Bank1:0xE496 (file 0x16496) - handler_e496 */
void dispatch_063d(void) { jump_bank_1(0xE496); }

/* 0x0642: Target Bank1:0xEF4E (file 0x16F4E) - error_handler_ef4e (NOPs) */
void dispatch_0642(void) { jump_bank_1(0xEF4E); }

/* 0x0647: Target Bank1:0xE4D2 (file 0x164D2) - handler_e4d2 */
void dispatch_0647(void) { jump_bank_1(0xE4D2); }

/* 0x064C: Target Bank1:0xE5CB (file 0x165CB) - handler_e5cb */
void dispatch_064c(void) { jump_bank_1(0xE5CB); }


/* ============================================================
 * Dispatch Event Handler Implementations
 * ============================================================ */

/*
 * dispatch_handler_0557 - PCIe event dispatch handler
 * Address: 0x0557 -> targets 0xee94 (bank 1)
 *
 * Original disassembly at 0x16e94:
 *   ee94: acall 0xe97f   ; calls helper at 0x1697f
 *   ee96: rr a           ; rotate result right
 *   ee97: ljmp 0xed82    ; -> 0x16d82 -> ljmp 0x7a12 (NOP slide to 0x8000)
 *
 * The helper at 0x1697f:
 *   e97f: mov r1, #0xe6  ; setup parameter
 *   e981: ljmp 0x538d    ; call bank 0 dispatch function
 *
 * This function is part of the PCIe event handling chain. It checks event
 * state and returns non-zero (in R7) if dispatch/processing should continue.
 *
 * The caller in pcie.c uses the return value:
 *   if (result) { pcie_queue_handler_a62d(); ... }
 *
 * Returns: Non-zero if event processing should continue, 0 otherwise.
 */
uint8_t dispatch_handler_0557(void)
{
    /* Check event flags to determine if dispatch is needed.
     * The original function calls into bank 0/1 dispatch logic
     * that reads from event control registers.
     *
     * For now, return 0 (no dispatch) as a safe default.
     * A more complete implementation would check:
     * - G_EVENT_CTRL_09FA state
     * - PCIe link status
     * - Pending transfer state
     */
    return 0;  /* No dispatch needed - conservative default */
}
