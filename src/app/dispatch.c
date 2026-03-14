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
#include "drivers/usb.h"

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

/* 0x0322: Target 0xCA0D - system_state_handler */
void dispatch_0322(void) { jump_bank_0(0xCA0D); }

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
    val = XDATA_REG8(0x9220);
    if (val & 0x04) {
        val &= 0xFB;
        XDATA_REG8(0x9220) = val;
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

/* 0x0345: Target 0x9C2B - nvme_queue_handler */
void dispatch_0345(void) { jump_bank_0(0x9C2B); }

/* 0x034A: Target 0xC66A - phy_handler */
void dispatch_034a(void) { jump_bank_0(0xC66A); }

/* 0x034F: Target 0xE94D - handler_e94d (stub) */
void dispatch_034f(void) { jump_bank_0(0xE94D); }

/* 0x0354: Target 0xE925 - handler_e925 (stub) */
void dispatch_0354(void) { jump_bank_0(0xE925); }

/*
 * helper_cc60 - Set link status bits 0-1 to 0b11
 * Address: 0xCC60-0xCC69 (10 bytes)
 *
 * Modifies REG_LINK_STATUS_E716: clears bits 0-1 then sets them to 0b11.
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
    uint8_t val = REG_LINK_STATUS_E716;
    val = (val & 0xFC) | 0x03;
    REG_LINK_STATUS_E716 = val;
}

/*
 * helper_cc56 - Set PHY config bit 0
 * Address: 0xCC56-0xCC5F (10 bytes)
 *
 * Sets bit 0 of REG_PHY_CFG_C6A8 (clears bits 0, then sets bit 0).
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
    uint8_t val = REG_PHY_CFG_C6A8;
    val = (val & 0xFE) | 0x01;
    REG_PHY_CFG_C6A8 = val;
}

/* Forward declarations for functions called by init_bda4 */
static void helper_d12a(void);
static void helper_d387(void);
static void helper_df86(void);
static void pcie_reinit_c24c(void);
static void helper_494d(void);

/*
 * init_bda4 - State initialization function
 * Address: 0xBDA4-0xBE20 (125 bytes)
 *
 * Called when power status bit 6 is clear. Clears many XDATA state variables
 * and calls several initialization functions.
 *
 * Original disassembly:
 *   bda4: clr a
 *   bda5: mov dptr, #0x07ed
 *   bda8: movx @dptr, a        ; clear 0x07ed
 *   bda9: mov dptr, #0x07ee
 *   bdac: movx @dptr, a        ; clear 0x07ee
 *   bdad: mov dptr, #0x0af5
 *   bdb0: movx @dptr, a
 *   bdb1: mov dptr, #0x07eb
 *   bdb4: movx @dptr, a
 *   bdb5: mov dptr, #0x0af1
 *   bdb8: movx @dptr, a
 *   bdb9: mov dptr, #0x0aca
 *   bdbc: movx @dptr, a
 *   bdbd: mov dptr, #0x07e1
 *   bdc0: mov a, #0x05
 *   bdc2: movx @dptr, a
 *   bdc3: clr a
 *   bdc4: mov dptr, #0x0b2e
 *   bdc7: movx @dptr, a
 *   bdc8: mov dptr, #0x0acb
 *   bdcb: movx @dptr, a
 *   bdcc: mov dptr, #0x07e3
 *   bdcf: movx @dptr, a
 *   bdd0: mov dptr, #0x07e6
 *   bdd3: movx @dptr, a
 *   bdd4: inc dptr              ; 0x07e7
 *   bdd5: movx @dptr, a
 *   bdd6: mov dptr, #0x07e9
 *   bdd9: movx @dptr, a
 *   bdda: mov dptr, #0x0b2d
 *   bddd: movx @dptr, a
 *   bdde: mov dptr, #0x07e2
 *   bde1: movx @dptr, a
 *   bde2: mov dptr, #0x0003
 *   bde5: movx @dptr, a
 *   bde6: mov dptr, #0x0006
 *   bde9: movx @dptr, a
 *   bdea: mov dptr, #0x07e8
 *   bded: movx @dptr, a
 *   bdee: mov dptr, #0x07e5
 *   bdf1: movx @dptr, a
 *   bdf2: mov dptr, #0x0b3b
 *   bdf5: movx @dptr, a
 *   bdf6: mov dptr, #0x07ea
 *   bdf9: movx @dptr, a
 *   bdfa: lcall 0x54bb         ; vendor_clear_enum_flag
 *   bdfd: lcall 0xcc56         ; helper_cc56 (set PHY cfg bit 0)
 *   be00: mov dptr, #0x92c8    ; REG_POWER_CTRL_92C8
 *   be03: movx a, @dptr
 *   be04: anl a, #0xfe         ; clear bit 0
 *   be06: movx @dptr, a
 *   be07: movx a, @dptr
 *   be08: anl a, #0xfd         ; clear bit 1
 *   be0a: movx @dptr, a
 *   be0b: mov dptr, #0xcd31    ; REG_CPU_TIMER_CTRL_CD31
 *   be0e: mov a, #0x04
 *   be10: movx @dptr, a
 *   be11: mov a, #0x02
 *   be13: movx @dptr, a
 *   be14: lcall 0xd12a         ; helper_d12a
 *   be17: lcall 0xd387         ; helper_d387
 *   be1a: lcall 0xdf86         ; helper_df86
 *   be1d: lcall 0xc24c         ; pcie_reinit_c24c
 *   be20: ljmp 0x494d          ; tail call to helper_494d
 */
static void init_bda4(void)
{
    uint8_t val;

    /* Clear state variables */
    G_SYS_FLAGS_07ED = 0;
    G_SYS_FLAGS_07EE = 0;
    G_EP_DISPATCH_OFFSET = 0;
    G_SYS_FLAGS_07EB = 0;
    G_STATE_FLAG_0AF1 = 0;
    G_LINK_POWER_STATE_0ACA = 0;
    G_USB_CTRL_STATE_07E1 = 0x05;  /* Ready to send */
    G_USB_TRANSFER_FLAG = 0;
    G_TLP_MASK_0ACB = 0;
    G_CMD_WORK_E3 = 0;
    G_USB_STATUS_07E6 = 0;
    G_USB_STATUS_07E7 = 0;
    G_TLP_STATE_07E9 = 0;
    G_LINK_EVENT_0B2D = 0;
    G_STATE_FLAG_07E2 = 0;
    G_EP_STATUS_CTRL = 0;
    G_WORK_0006 = 0;
    G_SYS_FLAGS_07E8 = 0;
    G_TRANSFER_ACTIVE = 0;
    G_TRANSFER_BUSY_0B3B = 0;
    G_XFER_FLAG_07EA = 0;

    /* Call helper functions */
    vendor_clear_enum_flag();   /* lcall 0x54bb */
    helper_cc56();              /* lcall 0xcc56 - set PHY cfg bit 0 */

    /* Clear bits 0 and 1 of REG_POWER_CTRL_92C8 */
    val = REG_POWER_CTRL_92C8;
    val &= 0xFE;               /* clear bit 0 */
    REG_POWER_CTRL_92C8 = val;
    val = REG_POWER_CTRL_92C8;
    val &= 0xFD;               /* clear bit 1 */
    REG_POWER_CTRL_92C8 = val;

    /* Timer trigger sequence */
    REG_CPU_TIMER_CTRL_CD31 = 0x04;
    REG_CPU_TIMER_CTRL_CD31 = 0x02;

    /* Call remaining init helpers */
    helper_d12a();              /* lcall 0xd12a */
    helper_d387();              /* lcall 0xd387 */
    helper_df86();              /* lcall 0xdf86 */
    pcie_reinit_c24c();         /* lcall 0xc24c */

    /* Tail call to setup function */
    helper_494d();              /* ljmp 0x494d */
}

/*
 * helper_d12a - PCIe/Timer initialization helper
 * Address: 0xD12A-0xD17A (81 bytes)
 *
 * Clears 0x0B3D, copies 0x05A5 → 0x05A3, then configures
 * CC16-CC19 timer registers based on 0x0AE4/0x0AEA state.
 * If 0x0AE4 == 0, also clears bit 0 of 0x92C4 and 0x9201.
 *
 * Original disassembly:
 *   d12a: clr a
 *   d12b: mov dptr, #0x0b3d
 *   d12e: movx @dptr, a
 *   d12f: mov dptr, #0x05a5
 *   d132: movx a, @dptr
 *   d133: mov dptr, #0x05a3
 *   d136: movx @dptr, a
 *   d137: mov dptr, #0x0ae4
 *   d13a: movx a, @dptr
 *   d13b: mov r7, a
 *   d13c: jz 0xd147
 *   d13e: mov dptr, #0x0aea
 *   d141: movx a, @dptr
 *   d142: clr c
 *   d143: subb a, #0x03
 *   d145: jnc 0xd166
 *   d147: mov dptr, #0xcc17
 *   d14a: lcall 0xc033     ; read_mmio_reg helper
 *   d14d: mov dptr, #0xcc16
 *   d150: movx a, @dptr
 *   d151: anl a, #0xf8
 *   d153: orl a, #0x04
 *   d155: movx @dptr, a
 *   d156: mov dptr, #0xcc18
 *   d159: mov a, #0x01
 *   d15b: movx @dptr, a
 *   d15c: inc dptr          ; cc19
 *   d15d: mov a, #0x90
 *   d15f: movx @dptr, a
 *   d160: mov dptr, #0x0b3d
 *   d163: mov a, #0x01
 *   d165: movx @dptr, a
 *   d166: mov a, r7         ; (0x0ae4 value)
 *   d167: jnz 0xd17a
 *   d169: mov dptr, #0x92c4
 *   d16c: movx a, @dptr
 *   d16d: anl a, #0xfe
 *   d16f: movx @dptr, a
 *   d170: mov dptr, #0x9201
 *   d173: lcall 0xc024     ; read_mmio_reg helper
 *   d176: movx a, @dptr
 *   d177: anl a, #0xfe
 *   d179: movx @dptr, a
 *   d17a: ret
 */
static void helper_d12a(void)
{
    uint8_t ae4_val;
    uint8_t val;

    /* Clear 0x0B3D */
    XDATA8(0x0B3D) = 0;

    /* Copy 0x05A5 → 0x05A3 */
    XDATA8(0x05A3) = XDATA8(0x05A5);

    /* Read 0x0AE4 */
    ae4_val = XDATA8(0x0AE4);

    /* If ae4_val != 0 and 0x0AEA >= 3, skip timer config */
    if (ae4_val != 0 && XDATA8(0x0AEA) >= 3) {
        /* Skip to the ae4_val check below */
    } else {
        /* Configure timer registers CC16-CC19 */
        /* lcall 0xC033 reads from DPTR (0xCC17) */
        (void)XDATA8(0xCC17);  /* Read to trigger hardware side-effect */

        val = XDATA8(0xCC16);
        val = (val & 0xF8) | 0x04;
        XDATA8(0xCC16) = val;

        XDATA8(0xCC18) = 0x01;
        XDATA8(0xCC19) = 0x90;

        XDATA8(0x0B3D) = 0x01;
    }

    /* If 0x0AE4 == 0, clear bit 0 of 0x92C4 and 0x9201 */
    if (ae4_val == 0) {
        val = XDATA8(0x92C4);
        val &= 0xFE;
        XDATA8(0x92C4) = val;

        /* lcall 0xC024 reads from DPTR (0x9201) */
        (void)XDATA8(0x9201);
        val = XDATA8(0x9201);
        val &= 0xFE;
        XDATA8(0x9201) = val;
    }
}

/*
 * helper_d387 - PCIe link speed configuration
 * Address: 0xD387-0xD3CE (72 bytes)
 *
 * Configures CC22 register based on link speed value in 0x0AE8.
 * If 0x0AE8 == 0x0F, returns immediately.
 * If 0x0AE8 == 0, writes 0x0F to 0x0AE8 and returns.
 * If 0x0AE8 >= 0x0B, also writes 0x0F and returns.
 * Otherwise uses 0x0AE8 as index into computed jump table.
 *
 * Original disassembly:
 *   d387: mov dptr, #0x0ae8
 *   d38a: movx a, @dptr
 *   d38b: mov r7, a
 *   d38c: xrl a, #0x0f
 *   d38e: jz 0xd3ce         ; if == 0x0F, return
 *   d390: lcall 0xc02b      ; read helper
 *   d393: mov dptr, #0xcc22
 *   d396: movx a, @dptr
 *   d397: anl a, #0xef      ; clear bit 4
 *   d399: movx @dptr, a
 *   d39a: movx a, @dptr
 *   d39b: anl a, #0xf8      ; clear bits 0-2
 *   d39d: orl a, #0x07      ; set bits 0-2
 *   d39f: movx @dptr, a
 *   d3a0: mov a, r7
 *   d3a1: jz 0xd3ad         ; if ae8 == 0, goto write 0x0F
 *   d3a3: mov dptr, #0x0ae8
 *   d3a6: movx a, @dptr
 *   d3a7: mov r7, a
 *   d3a8: setb c
 *   d3a9: subb a, #0x0b
 *   d3ab: jc 0xd3b4         ; if ae8 < 0x0B, use jump table
 *   d3ad: mov dptr, #0x0ae8
 *   d3b0: mov a, #0x0f
 *   d3b2: movx @dptr, a     ; write 0x0F
 *   d3b3: ret
 *   ...
 *   d3ce: ret
 */
static void helper_d387(void)
{
    uint8_t ae8_val;
    uint8_t val;

    ae8_val = XDATA8(0x0AE8);

    /* If already 0x0F, nothing to do */
    if (ae8_val == 0x0F) {
        return;
    }

    /* Configure CC22: clear bit 4, set bits 0-2 */
    val = XDATA8(0xCC22);
    val &= 0xEF;
    XDATA8(0xCC22) = val;
    val = XDATA8(0xCC22);
    val = (val & 0xF8) | 0x07;
    XDATA8(0xCC22) = val;

    /* If ae8 == 0 or ae8 >= 0x0B, write 0x0F */
    if (ae8_val == 0 || ae8_val >= 0x0B) {
        XDATA8(0x0AE8) = 0x0F;
        return;
    }

    /* For values 1-10, would use jump table for speed-specific config */
    /* For the default boot case, ae8 is typically 0, handled above */
    XDATA8(0x0AE8) = 0x0F;
}

/*
 * helper_df86 - DMA channel configuration
 * Address: 0xDF86-0xDFAD (40 bytes)
 *
 * Configures DMA channels via CC1C-CC1F and CC5C-CC5F registers.
 * Calls 0xE3C6 helper first, then sets up two DMA channels.
 *
 * Original disassembly:
 *   df86: lcall 0xe3c6     ; helper (writes CC0C-CC0F)
 *   df89: mov dptr, #0xcc1c
 *   df8c: movx a, @dptr
 *   df8d: anl a, #0xf8
 *   df8f: orl a, #0x06
 *   df91: movx @dptr, a
 *   df92: mov dptr, #0xcc1e
 *   df95: clr a
 *   df96: movx @dptr, a
 *   df97: inc dptr          ; cc1f
 *   df98: mov a, #0x8b
 *   df9a: movx @dptr, a
 *   df9b: mov dptr, #0xcc5c
 *   df9e: movx a, @dptr
 *   df9f: anl a, #0xf8
 *   dfa1: orl a, #0x04
 *   dfa3: movx @dptr, a
 *   dfa4: mov dptr, #0xcc5e
 *   dfa7: clr a
 *   dfa8: movx @dptr, a
 *   dfa9: inc dptr          ; cc5f
 *   dfaa: mov a, #0xc7
 *   dfac: movx @dptr, a
 *   dfad: ret
 */
static void helper_df86(void)
{
    uint8_t val;

    /* TODO: Call 0xE3C6 helper (writes CC0C-CC0F) - stub for now */
    /* helper_e3c6(); */

    /* Configure DMA channel 1: CC1C-CC1F */
    val = XDATA8(0xCC1C);
    val = (val & 0xF8) | 0x06;
    XDATA8(0xCC1C) = val;
    XDATA8(0xCC1E) = 0x00;
    XDATA8(0xCC1F) = 0x8B;

    /* Configure DMA channel 2: CC5C-CC5F */
    val = XDATA8(0xCC5C);
    val = (val & 0xF8) | 0x04;
    XDATA8(0xCC5C) = val;
    XDATA8(0xCC5E) = 0x00;
    XDATA8(0xCC5F) = 0xC7;
}

/*
 * pcie_reinit_c24c - PCIe reinitialization check
 * Address: 0xC24C-0xC2B7 (108 bytes)
 *
 * Checks reinit flag (0x06E3). If set:
 * - Clears flag and sets 0x06E4=1, 0x06E5=1
 * - Clears 0x05A4, 0x06E8, 0x05A9, 0x05AA
 * - Calls vendor_clear_enum_flag
 * - Writes B401 bit 0, clears it
 * - Calls pcie_phy_reinit (0xCC83) which includes pcie_adapter_config
 * - Clears CA06 bit 4
 * Then returns.
 *
 * Original disassembly:
 *   c24c: mov dptr, #0x06e3
 *   c24f: movx a, @dptr
 *   c250: jz 0xc2b7         ; if 0, skip all
 *   c252: clr a
 *   c253: movx @dptr, a     ; clear 0x06e3
 *   c254: inc dptr           ; 0x06e4
 *   c255: inc a              ; a=1
 *   c256: movx @dptr, a     ; 0x06e4=1
 *   c257: inc dptr           ; 0x06e5
 *   c258: movx @dptr, a     ; 0x06e5=1
 *   c259: clr a
 *   c25a: mov dptr, #0x05a4
 *   c25d: movx @dptr, a     ; clear 0x05a4
 *   c25e: mov dptr, #0x06e8
 *   c261: movx @dptr, a     ; clear 0x06e8
 *   c262: mov dptr, #0x05a9
 *   c265: movx @dptr, a     ; clear 0x05a9
 *   c266: inc dptr           ; 0x05aa
 *   c267: movx @dptr, a     ; clear 0x05aa
 *   c268: lcall 0x54bb      ; vendor_clear_enum_flag
 *   c26b: mov dptr, #0xb401
 *   c26e: lcall 0x9941      ; write_mmio_set_bit0 helper
 *   c271: movx a, @dptr     ; read back B401
 *   c272: anl a, #0xfe      ; clear bit 0
 *   c274: movx @dptr, a     ; write B401
 *   c275: lcall 0xcc83      ; pcie_phy_reinit (calls pcie_adapter_config)
 *   c278: mov dptr, #0xca06
 *   c27b: movx a, @dptr
 *   c27c: anl a, #0xef      ; clear bit 4
 *   c27e: lcall 0x993d      ; write_mmio helper
 *   ...
 *   c2b7: ret
 */
static void pcie_reinit_c24c(void)
{
    uint8_t val;

    /* Check reinit flag */
    if (G_USB_STATE_CLEAR_06E3 == 0) {
        return;
    }

    /* Clear flag and set 06E4=1, 06E5=1 */
    G_USB_STATE_CLEAR_06E3 = 0;
    XDATA8(0x06E4) = 1;
    XDATA8(0x06E5) = 1;

    /* Clear state */
    XDATA8(0x05A4) = 0;
    XDATA8(0x06E8) = 0;
    XDATA8(0x05A9) = 0;
    XDATA8(0x05AA) = 0;

    /* Clear vendor enum flag */
    vendor_clear_enum_flag();

    /* Set then clear B401 bit 0 */
    val = XDATA8(0xB401);
    val |= 0x01;
    XDATA8(0xB401) = val;
    val = XDATA8(0xB401);
    val &= 0xFE;
    XDATA8(0xB401) = val;

    /* pcie_phy_reinit: clear CA06 bit 4, call pcie_adapter_config, then more setup */
    /* Inline the 0xCC83 function behavior */
    val = XDATA8(0xCA06);
    val &= 0xEF;
    XDATA8(0xCA06) = val;

    /* Call pcie_adapter_config */
    pcie_adapter_config();

    /* After adapter config: set up B401, B482, B480, B430 */
    val = XDATA8(0xB401);
    val |= 0x01;
    XDATA8(0xB401) = val;

    val = XDATA8(0xB482);
    val |= 0x01;
    XDATA8(0xB482) = val;
    val = XDATA8(0xB482);
    val = (val & 0x0F) | 0xF0;
    XDATA8(0xB482) = val;

    val = XDATA8(0xB401);
    val &= 0xFE;
    XDATA8(0xB401) = val;

    val = XDATA8(0xB480);
    val |= 0x01;
    XDATA8(0xB480) = val;

    val = XDATA8(0xB430);
    val &= 0xFE;
    XDATA8(0xB430) = val;

    /* Clear CA06 bit 4 */
    val = XDATA8(0xCA06);
    val &= 0xEF;
    XDATA8(0xCA06) = val;

    /* TODO: remaining c27e-c2b7 includes more register config
     * via helper calls - needs full implementation */
}

/*
 * helper_494d - Post-init setup
 * Address: 0x494D-0x4xxx
 *
 * Sets up 900B and C42A registers for link configuration.
 * Called as tail call from init_bda4.
 *
 * Original disassembly (partial):
 *   494d: mov dptr, #0x900b
 *   4950: lcall 0x324b     ; helper
 *   4953: mov dptr, #0xc42a
 *   4956: lcall 0x3172     ; helper
 *   4959: mov dptr, #0x900b
 *   495c: lcall 0x3172     ; helper
 *   495f: mov dptr, #0xc42a
 *   4962: lcall 0x324b     ; helper
 *   4965: movx a, @dptr
 *   4966: anl a, #0xf7     ; clear bit 3
 *   4968: orl a, #0x08     ; set bit 3
 *   496a: movx @dptr, a
 *   ...
 */
static void helper_494d(void)
{
    uint8_t val;

    /* TODO: Full implementation with all register operations.
     * For now, implement the key register writes. */

    /* Write to C42A: clear bit 3 then set bit 3 (effectively set bit 3) */
    val = XDATA8(0xC42A);
    val = (val & 0xF7) | 0x08;
    XDATA8(0xC42A) = val;

    /* Clear bits in 900B */
    val = XDATA8(0x900B);
    val &= 0xFD;  /* clear bit 1 */
    XDATA8(0x900B) = val;
    val = XDATA8(0x900B);
    val &= 0xFB;  /* clear bit 2 */
    XDATA8(0x900B) = val;

    /* Clear bit 0 of C42A */
    val = XDATA8(0xC42A);
    val &= 0xFE;
    XDATA8(0xC42A) = val;

    /* Clear bit 0 of 900B */
    val = XDATA8(0x900B);
    val &= 0xFE;
    XDATA8(0x900B) = val;
}

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

/* 0x035E: Target 0xE6BD - handler_e6bd */
void dispatch_035e(void) { jump_bank_0(0xE6BD); }

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

/* 0x0395: Target 0xDA8F - handler_da8f */
void dispatch_0395(void) { jump_bank_0(0xDA8F); }

/* 0x039A: Target 0xD810 - usb_buffer_handler */
void dispatch_039a(void) { jump_bank_0(0xD810); }

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

/* 0x04CB: Target 0xE7C1 - pcie_param_handler */
void dispatch_04cb(void) { jump_bank_0(0xE7C1); }

/* 0x04D0: Target 0xCE79 - timer_link_handler */
void dispatch_04d0(void) { jump_bank_0(0xCE79); }

/* 0x04D5: Target 0xD3A2 - handler_d3a2 */
void dispatch_04d5(void) { jump_bank_0(0xD3A2); }

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
