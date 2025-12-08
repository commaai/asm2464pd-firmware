/*
 * ASM2464PD Firmware - Protocol State Machine
 *
 * Implements the main protocol state machine and event handling for the
 * USB4/Thunderbolt to NVMe bridge. This module coordinates between USB,
 * NVMe, DMA, and flash subsystems.
 *
 * ============================================================================
 * PROTOCOL STATE MACHINE (0x3900)
 * ============================================================================
 *
 * The state machine reads from XDATA[0x0002] and maps states to actions:
 *   0x28 ('(') -> action code 3
 *   0x2A ('*') -> action code 1
 *   0x88       -> action code 2
 *   0x8A       -> action code 0
 *   other      -> poll register and halt
 *
 * ============================================================================
 * EVENT HANDLER (0x3ADB)
 * ============================================================================
 *
 * Handles DMA events and state transitions:
 *   - Stores event parameter to 0x0AAA
 *   - Reads DMA status from 0xC8D6
 *   - Manages flash reset state
 *   - Updates state counters
 *
 * ============================================================================
 * CORE HANDLER (0x4FF2)
 * ============================================================================
 *
 * Core processing handler that coordinates USB events:
 *   - Bit 0 of param controls processing path
 *   - Calls USB event handler and interface reset
 *   - Manages state variables at IDATA[0x16-0x17]
 *
 * ============================================================================
 * GLOBAL VARIABLES
 * ============================================================================
 *
 *   0x0002: Current state code
 *   0x0AAA: G_FLASH_RESET (flash reset flag)
 *   0x0AAB: State helper variable
 *   0x0AAC: State counter/index
 *   0xC8D6: REG_DMA_STATUS
 *
 * ============================================================================
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"
#include "structs.h"

/* Forward declarations */
extern void dma_clear_status(void);
extern uint8_t usb_event_handler(void);
extern void usb_reset_interface(uint8_t param);
extern void power_check_status(uint8_t param);

/* Stub helper functions - these need to be implemented properly */
static void nvme_calc_addr_01xx(uint8_t param) { (void)param; }
static void FUN_CODE_1bec(void) { }
static void nvme_get_config_offset(void) { }
static void FUN_CODE_1b30(uint8_t param) { (void)param; }
static void nvme_calc_idata_offset(void) { }
static uint8_t FUN_CODE_1b8d(uint8_t param) { (void)param; return 0; }
static uint8_t FUN_CODE_1b0b(uint8_t param) { (void)param; return 0; }
static void FUN_CODE_1b3f(uint8_t param) { (void)param; }
static uint8_t usb_get_ep_config_indexed(void) { return 0; }
static void FUN_CODE_1c43(uint8_t param) { (void)param; }
static void nvme_add_to_global_053a(void) { }
static void usb_set_transfer_flag(void) { }

/* Protocol state codes */
#define STATE_CODE_PAREN_OPEN   0x28    /* '(' */
#define STATE_CODE_ASTERISK     0x2A    /* '*' */
#define STATE_CODE_88           0x88
#define STATE_CODE_8A           0x8A

/* Action codes returned by state machine */
#define ACTION_CODE_0           0x00
#define ACTION_CODE_1           0x01
#define ACTION_CODE_2           0x02
#define ACTION_CODE_3           0x03

/* Note: XDATA locations for protocol state are now defined in globals.h:
 *   G_IO_CMD_STATE (0x0002) - I/O command state byte
 *   G_FLASH_RESET_0AAA (0x0AAA) - Flash reset flag
 *   G_STATE_HELPER_0AAB (0x0AAB) - State helper variable
 *   G_STATE_COUNTER_0AAC (0x0AAC) - State counter/index
 */

/*
 * FUN_CODE_2bea - State action dispatcher
 * Address: 0x2bea (external)
 *
 * Called with action code in R7, dispatches to appropriate handler.
 */
extern void state_action_dispatch(uint8_t action_code);

/*
 * FUN_CODE_16a2, FUN_CODE_16b7 - Transfer helper functions
 * Address: 0x16a2, 0x16b7 (external)
 */
extern void transfer_func_16a2(void);
extern void transfer_func_16b7(uint8_t param);
extern void transfer_func_17ed(void);

/*
 * FUN_CODE_1679 - Flash/transfer helper
 * Address: 0x1679 (external)
 */
extern void flash_func_1679(void);

/*
 * Helper function forward declarations
 */
extern void helper_4e6d(void);  /* 0x4e6d - Buffer configuration */

/*
 * FUN_CODE_15ac, FUN_CODE_15af - State helper functions
 * Address: 0x15ac, 0x15af (external)
 */
extern uint8_t state_helper_15ac(void);
extern uint8_t state_helper_15af(void);

/*
 * usb_calc_queue_addr - Calculate USB queue address
 * Address: 0x176b (external)
 * Returns pointer to queue address.
 */
extern __xdata uint8_t *usb_calc_queue_addr(uint8_t param);

/*
 * flash_func_0bc8 - Flash operation (does not return)
 * Address: 0x0bc8 (external)
 */
extern void flash_func_0bc8(void);  /* Note: does not return in original firmware */


/*
 * reg_wait_bit_clear - Wait for register bit to clear
 * Address: 0x0461 region (external)
 */
extern void reg_wait_bit_clear(uint16_t addr, uint8_t mask, uint8_t flags, uint8_t timeout);

/*
 * usb_func_1b14, usb_func_1b20, usb_func_1b23 - USB helper functions
 * Address: 0x1b14, 0x1b20, 0x1b23 (external)
 */
extern uint8_t usb_func_1b14(uint8_t param);
extern uint8_t usb_func_1b20(uint8_t param);
extern uint8_t usb_func_1b23(void);

/*
 * xdata_load_dword_noarg - Load 32-bit value from XDATA (DPTR set by caller)
 * Address: 0x0d84 (external)
 */
extern void xdata_load_dword_noarg(void);

/*
 * protocol_state_machine - Main protocol state machine
 * Address: 0x3900-0x39DE (approximate)
 *
 * Reads the current state from XDATA[0x0002] and maps it to an action code.
 * The action code is then passed to state_action_dispatch for execution.
 *
 * State mapping:
 *   0x28 ('(') -> action 3 (open/start)
 *   0x2A ('*') -> action 1 (process)
 *   0x88       -> action 2 (wait)
 *   0x8A       -> action 0 (idle)
 *
 * Original disassembly (0x390e-0x3925):
 *   390e: mov dptr, #0x0002
 *   3911: movx a, @dptr       ; read state code
 *   3912: lcall 0x0def        ; helper to setup
 *   3915-3925: jump table based on state code
 */
void protocol_state_machine(void)
{
    uint8_t state_code;
    uint8_t action_code;

    /* Read current state from XDATA[0x0002] */
    state_code = G_IO_CMD_STATE;

    /* Map state code to action code */
    switch (state_code) {
        case STATE_CODE_PAREN_OPEN:  /* 0x28 '(' */
            action_code = ACTION_CODE_3;
            break;

        case STATE_CODE_ASTERISK:    /* 0x2A '*' */
            action_code = ACTION_CODE_1;
            break;

        case STATE_CODE_88:          /* 0x88 */
            action_code = ACTION_CODE_2;
            break;

        case STATE_CODE_8A:          /* 0x8A */
            action_code = ACTION_CODE_0;
            break;

        default:
            /* Unknown state - should not happen in normal operation */
            /* Original code calls reg_poll and halts */
            return;
    }

    /* Dispatch to action handler */
    state_action_dispatch(action_code);

    /* Store result to IDATA[0x6A] */
    I_STATE_6A = 0;  /* Cleared by original code at 0x4951 */
}

/*
 * handler_3adb - Event handler for DMA and state transitions
 * Address: 0x3ADB-0x3BA5 (approximate)
 *
 * Handles DMA events and coordinates state transitions between
 * flash, DMA, and transfer subsystems.
 *
 * Parameters:
 *   param - Event parameter stored to 0x0AAA
 *
 * Original disassembly (0x3adb-0x3aff):
 *   3adb: mov dptr, #0x0aaa
 *   3ade: mov a, r7
 *   3adf: movx @dptr, a       ; store param
 *   3ae0: lcall 0x16a2        ; transfer helper
 *   3ae3: movx a, @dptr       ; read result
 *   3ae4: mov dptr, #0x0aac
 *   3ae7: lcall 0x16b7        ; transfer helper
 *   3aea: movx a, @dptr
 *   3aeb: mov dptr, #0x0aab
 *   3aee: movx @dptr, a       ; store to 0x0AAB
 *   3aef: mov dptr, #0xc8d6   ; REG_DMA_STATUS
 *   3af2: movx a, @dptr
 *   3af3: anl a, #0xf7        ; clear bit 3
 *   3af5: orl a, #0x08        ; set bit 3
 *   3af7: movx @dptr, a
 *   3af8: movx a, @dptr
 *   3af9: anl a, #0xfb        ; clear bit 2
 *   3afb: movx @dptr, a
 */
void handler_3adb(uint8_t param)
{
    uint8_t dma_status;
    uint8_t state_counter;
    uint8_t state_helper;
    uint8_t computed_val;
    uint8_t state_flag;
    uint16_t calc_addr;

    /* Store event parameter to flash reset flag */
    G_FLASH_RESET_0AAA = param;

    /* Call transfer helper to get status */
    transfer_func_16a2();

    /* Read state counter and update helper */
    state_counter = G_STATE_COUNTER_0AAC;
    transfer_func_16b7(G_FLASH_RESET_0AAA);
    state_helper = G_STATE_COUNTER_0AAC;
    G_STATE_HELPER_0AAB = state_helper;

    /* Update DMA status register */
    dma_status = REG_DMA_STATUS;
    dma_status = (dma_status & ~DMA_STATUS_ERROR) | DMA_STATUS_ERROR;  /* Clear bit 3, set bit 3 */
    REG_DMA_STATUS = dma_status;

    dma_status = REG_DMA_STATUS;
    dma_status = dma_status & ~DMA_STATUS_DONE;  /* Clear bit 2 */
    REG_DMA_STATUS = dma_status;

    /* Calculate address based on state counter */
    computed_val = (uint8_t)((uint16_t)state_counter * 0x10);

    /* Compute base address: 0xB800 or 0xB840 based on flash reset flag */
    if (G_FLASH_RESET_0AAA != 0) {
        calc_addr = 0xB840;
    } else {
        calc_addr = 0xB800;
    }
    calc_addr += computed_val;

    /* Wait for ready */
    reg_wait_bit_clear(0x0461, 0x00, 0x01, computed_val);

    /* Check if state changed */
    state_flag = state_helper_15ac() & 0x01;
    state_helper = G_STATE_HELPER_0AAB;

    if (state_helper != state_flag) {
        /* State changed - handle transition */
        transfer_func_17ed();
        computed_val = state_helper_15af();

        if (G_FLASH_RESET_0AAA != 0) {
            computed_val += 0x04;
        }
        I_STATE_6A = computed_val;

        flash_func_1679();
        G_FLASH_RESET_0AAA = 0x01;

        transfer_func_17ed();
        computed_val = state_helper_15af();
        computed_val = (computed_val >> 1) & 0x07;

        usb_calc_queue_addr(I_STATE_6A);
        G_FLASH_RESET_0AAA = computed_val;

        /* Flash function does not return */
        flash_func_0bc8();
    }

    /* Clear DMA status and continue */
    dma_clear_status();

    /* Update state if counter changed */
    if (G_STATE_COUNTER_0AAC != G_FLASH_RESET_0AAA) {
        transfer_func_16a2();
        G_FLASH_RESET_0AAA = G_STATE_COUNTER_0AAC;
        transfer_func_16b7(G_STATE_HELPER_0AAB);
    }
}

/*
 * core_handler_4ff2 - Core processing handler
 * Address: 0x4FF2-0x502D (60 bytes)
 *
 * Coordinates USB event processing based on input flags.
 * Bit 0 of param_2 determines the processing path.
 *
 * Parameters:
 *   param_1 - 16-bit parameter (not fully used in simplified version)
 *   param_2 - Control flags, bit 0 selects processing path
 *
 * Original disassembly (0x4ff2-0x502d):
 *   4ff2: mov a, r7
 *   4ff3: jnb 0xe0.0, 0x5009  ; if bit 0 clear, jump
 *   4ff6: clr a
 *   4ff7-4ffa: clear R4-R7
 *   4ffb: mov r0, #0x0e
 *   4ffd: lcall 0x1b20        ; usb_func_1b20
 *   5000: add a, #0x11
 *   5002: lcall 0x1b14        ; usb_func_1b14
 *   5005: add a, #0x16
 *   5007: sjmp 0x5020
 *   5009: lcall 0x1b23        ; usb_func_1b23
 *   500c: add a, #0x11
 *   500e: lcall 0x1bc3        ; usb_reset_interface
 *   5011: lcall 0x0d84        ; xdata_load_dword
 *   5014: mov r0, #0x0e
 *   5016: lcall 0x1b20
 *   5019: add a, #0x15
 *   501b: lcall 0x1b14
 *   501e: add a, #0x1b
 *   5020: lcall 0x1bc3        ; usb_reset_interface
 *   5023: movx a, @dptr
 *   5024: mov r6, a
 *   5025: inc dptr
 *   5026: movx a, @dptr
 *   5027: mov r0, #0x16
 *   5029: mov @r0, 0x06       ; store R6 to IDATA[0x16]
 *   502b: inc r0
 *   502c: mov @r0, a          ; store A to IDATA[0x17]
 *   502d: ret
 */
void core_handler_4ff2(uint8_t param_2)
{
    uint8_t result;
    uint8_t val_hi, val_lo;

    if ((param_2 & 0x01) == 0) {
        /* Path when bit 0 is clear */
        result = usb_func_1b20(0x0E);
        result = usb_func_1b14(result + 0x11);
        result = result + 0x16;
    } else {
        /* Path when bit 0 is set */
        result = usb_func_1b23();
        result = result + 0x11;
        usb_reset_interface(result);

        xdata_load_dword_noarg();

        result = usb_func_1b20(0x0E);
        result = usb_func_1b14(result + 0x15);
        result = result + 0x1B;
    }

    /* Final interface reset */
    usb_reset_interface(result);

    /* Read 16-bit value and store to IDATA[0x16:0x17] */
    /* This would read from DPTR set by usb_reset_interface */
    /* For now, read from a known location */
    val_lo = 0;  /* Would be from @DPTR */
    val_hi = 0;  /* Would be from @DPTR+1 */

    I_CORE_STATE_L = val_lo;
    I_CORE_STATE_H = val_hi;
}

/*
 * protocol_dispatch - Protocol dispatcher
 * Address: 0x0458 (approximate)
 *
 * Main dispatch point for protocol handling.
 * Called from main loop to process protocol events.
 */
void protocol_dispatch(void)
{
    /* Check if there are events to process */
    uint8_t state = G_IO_CMD_STATE;

    if (state != 0) {
        protocol_state_machine();
    }
}

/*
 * protocol_init - Initialize protocol subsystem
 * Address: 0x39e4+ (FUN_CODE_39e4 in ghidra.c)
 *
 * Initializes DMA channels, clears state counters, and prepares
 * the protocol subsystem for operation.
 */
void protocol_init(void)
{
    uint8_t i;

    /* Clear system control */
    G_SYSTEM_CTRL = 0;

    /* Clear DMA status */
    dma_clear_status();

    /* Clear state counters */
    G_FLASH_RESET_0AAA = 0;
    G_STATE_HELPER_0AAB = 0;
    G_STATE_COUNTER_0AAC = 0;

    /* Initialize DMA channels 0-3 */
    for (i = 0; i < 4; i++) {
        /* Channel initialization would go here */
        /* Original calls transfer_func_17e3, dma_config_channel, etc. */
    }

    /* Clear state variables */
    G_SYS_STATUS_PRIMARY = 0;
}

/*
 * helper_0d78 - Read 4 bytes from IDATA at R0 into r4-r7
 * Address: 0x0d78-0x0d83 (12 bytes)
 *
 * Reads 4 consecutive bytes from IDATA pointer and returns in r4-r7.
 * This is a helper for copying IDATA blocks.
 *
 * Parameters:
 *   idata_ptr: IDATA pointer to read from
 *   out_r4-r7: Output values (passed by reference)
 */
static void helper_0d78(__idata uint8_t *idata_ptr, uint8_t *r4, uint8_t *r5, uint8_t *r6, uint8_t *r7)
{
    *r4 = idata_ptr[0];
    *r5 = idata_ptr[1];
    *r6 = idata_ptr[2];
    *r7 = idata_ptr[3];
}

/*
 * helper_0db9 - Write r4-r7 to 4 bytes at IDATA at R0
 * Address: 0x0db9-0x0dc4 (12 bytes)
 *
 * Writes r4-r7 to 4 consecutive bytes at IDATA pointer.
 * This is a helper for copying IDATA blocks.
 *
 * Parameters:
 *   idata_ptr: IDATA pointer to write to
 *   r4-r7: Values to write
 */
static void helper_0db9(__idata uint8_t *idata_ptr, uint8_t r4, uint8_t r5, uint8_t r6, uint8_t r7)
{
    idata_ptr[0] = r4;
    idata_ptr[1] = r5;
    idata_ptr[2] = r6;
    idata_ptr[3] = r7;
}

/*
 * helper_1bcb - USB 4-byte IDATA copy helper
 * Address: 0x1bcb-0x1bd4 (10 bytes)
 *
 * Copies 4 bytes from IDATA[0x6b-0x6e] to IDATA[0x6f-0x72].
 * Used for USB endpoint state management.
 *
 * Original disassembly:
 *   1bcb: mov r0, #0x6b
 *   1bcd: lcall 0x0d78    ; read 4 bytes from IDATA[0x6b] into r4-r7
 *   1bd0: mov r0, #0x6f
 *   1bd2: ljmp 0x0db9     ; write r4-r7 to IDATA[0x6f]
 */
void helper_1bcb(void)
{
    __idata uint8_t *src = (__idata uint8_t *)0x6b;
    __idata uint8_t *dst = (__idata uint8_t *)0x6f;
    uint8_t r4, r5, r6, r7;

    /* Read 4 bytes from IDATA[0x6b-0x6e] */
    helper_0d78(src, &r4, &r5, &r6, &r7);

    /* Write 4 bytes to IDATA[0x6f-0x72] */
    helper_0db9(dst, r4, r5, r6, r7);
}

/*
 * helper_523c - Queue processing helper
 * Address: 0x523c-0x525f (36 bytes)
 *
 * Stores queue parameters and optionally triggers USB endpoint.
 *
 * Parameters:
 *   r7: Queue type/index (stored to 0x0203)
 *   r5: Queue flags (stored to 0x020D)
 *   r3: Additional flag (stored to 0x020E)
 *
 * Original disassembly:
 *   523c: mov dptr, #0x0203
 *   523f: mov a, r7
 *   5240: movx @dptr, a       ; store r7 to 0x0203
 *   5241: mov dptr, #0x020d
 *   5244: mov a, r5
 *   5245: movx @dptr, a       ; store r5 to 0x020D
 *   5246: inc dptr
 *   5247: mov a, r3
 *   5248: movx @dptr, a       ; store r3 to 0x020E
 *   5249: mov dptr, #0x07e5
 *   524c: mov a, #0x01
 *   524e: movx @dptr, a       ; set 0x07E5 = 1
 *   524f: mov dptr, #0x9000
 *   5252: movx a, @dptr       ; read USB status
 *   5253: jb 0xe0.0, 0x525f   ; if bit 0 set, return
 *   5256: mov dptr, #0xd80c
 *   5259: mov a, #0x01
 *   525b: movx @dptr, a       ; set 0xD80C = 1
 *   525c: lcall 0x1bcb        ; call USB helper
 *   525f: ret
 */
void helper_523c(uint8_t r3, uint8_t r5, uint8_t r7)
{
    /* Store queue type to 0x0203 */
    G_DMA_MODE_SELECT = r7;

    /* Store queue flags to 0x020D */
    G_DMA_PARAM1 = r5;

    /* Store additional flag to 0x020E */
    G_DMA_PARAM2 = r3;

    /* Set ready flag at 0x07E5 */
    G_TRANSFER_ACTIVE = 0x01;

    /* Check USB status bit 0 */
    if (!(REG_USB_STATUS & USB_STATUS_ACTIVE)) {
        /* Bit 0 not set - trigger endpoint and call helper */
        REG_USB_EP_CSW_STATUS = 0x01;
        helper_1bcb();
    }
}

/*
 * Forward declarations for helpers
 */
static void helper_50db(void);
static void helper_5409(void);

/*
 * helper_53a7 - DMA completion handler
 * Address: 0x53a7-0x53bf (25 bytes)
 *
 * Handles DMA completion state. Calls helper_50db, then decrements
 * counter at 0x000A if > 1, otherwise clears it and calls helper_5409.
 *
 * Original disassembly:
 *   53a7: lcall 0x50db          ; Call status update helper
 *   53aa: mov dptr, #0x000a
 *   53ad: movx a, @dptr         ; Read counter
 *   53ae: setb c
 *   53af: subb a, #0x01         ; Compare with 1
 *   53b1: jc 0x53b7             ; If counter <= 1, jump to clear
 *   53b3: movx a, @dptr         ; Read counter again
 *   53b4: dec a                 ; Decrement
 *   53b5: movx @dptr, a         ; Store back
 *   53b6: ret
 *   53b7: clr a
 *   53b8: mov dptr, #0x000a
 *   53bb: movx @dptr, a         ; Clear counter
 *   53bc: lcall 0x5409          ; Call cleanup helper
 *   53bf: ret
 */
void helper_53a7(void)
{
    uint8_t counter;

    /* Call status update helper */
    helper_50db();

    /* Read counter at 0x000A */
    counter = G_EP_CHECK_FLAG;

    if (counter > 1) {
        /* Decrement counter */
        G_EP_CHECK_FLAG--;
    } else {
        /* Clear counter and call cleanup */
        G_EP_CHECK_FLAG = 0;
        helper_5409();
    }
}

/*
 * helper_53c0 - DMA buffer write helper
 * Address: 0x53c0-0x53d3 (20 bytes)
 *
 * Copies 4 bytes from IDATA[0x6f-0x72] to XDATA 0xD808-0xD80B.
 * Used to write DMA buffer configuration to hardware.
 *
 * Original disassembly:
 *   53c0: mov r0, #0x72
 *   53c2: mov a, @r0            ; Read IDATA[0x72]
 *   53c3: mov dptr, #0xd808
 *   53c6: movx @dptr, a         ; Write to 0xD808
 *   53c7: dec r0
 *   53c8: mov a, @r0            ; Read IDATA[0x71]
 *   53c9: inc dptr
 *   53ca: movx @dptr, a         ; Write to 0xD809
 *   53cb: dec r0
 *   53cc: mov a, @r0            ; Read IDATA[0x70]
 *   53cd: inc dptr
 *   53ce: movx @dptr, a         ; Write to 0xD80A
 *   53cf: dec r0
 *   53d0: mov a, @r0            ; Read IDATA[0x6F]
 *   53d1: inc dptr
 *   53d2: movx @dptr, a         ; Write to 0xD80B
 *   53d3: ret
 */
void helper_53c0(void)
{
    /* Copy 4 bytes from IDATA[0x6F-0x72] to CSW residue 0xD808-0xD80B */
    /* Note: Original reads backwards from 0x72 to 0x6F */
    USB_CSW->residue0 = I_BUF_CTRL_GLOBAL;   /* IDATA[0x72] -> 0xD808 */
    USB_CSW->residue1 = I_BUF_THRESH_HI;     /* IDATA[0x71] -> 0xD809 */
    USB_CSW->residue2 = I_BUF_THRESH_LO;     /* IDATA[0x70] -> 0xD80A */
    USB_CSW->residue3 = I_BUF_FLOW_CTRL;     /* IDATA[0x6F] -> 0xD80B */
}

/*
 * helper_039a - Register initialization for 0xD810
 * Address: 0x039a-0x039d (5 bytes)
 *
 * Sets DPTR = 0xD810 and jumps to 0x0300 which is a common
 * register initialization routine that:
 * - Pushes r0, ACC, DPL, DPH
 * - Sets r0 = 0x0a
 * - Sets SFR 0x96 = 0
 * - Returns
 *
 * This is part of a register initialization table where each entry
 * sets DPTR to a different register address and calls the common code.
 *
 * The effect is to initialize register 0xD810 by writing 0 to it
 * (value in ACC at entry to 0x0300 is 0x03, but cleared before write).
 *
 * Original disassembly:
 *   039a: mov dptr, #0xd810
 *   039d: ajmp 0x0300
 */
void helper_039a(void)
{
    /* Clear the register at 0xD810 */
    REG_USB_EP_CTRL_10 = 0;
}

/*
 * helper_50db - Status update and queue management helper
 * Address: 0x50db-0x5111 (55 bytes)
 *
 * Reads queue index from 0x0AF5, checks if < 0x20, then performs
 * various queue state updates using helper functions.
 *
 * Original disassembly:
 *   50db: mov dptr, #0x0af5
 *   50de: movx a, @dptr         ; Read queue index
 *   50df: mov r7, a
 *   50e0: clr c
 *   50e1: subb a, #0x20         ; Check if < 32
 *   50e3: jnc 0x5111            ; If >= 32, return
 *   50e5: lcall 0x31d5          ; Call helper
 *   50e8: clr a
 *   50e9: movx @dptr, a         ; Clear value at dptr
 *   ... (more queue management)
 *   5111: ret
 */
/*
 * helper_31d5 - Queue address calculation
 * Address: 0x31d5
 * Returns DPTR computed from R7 value.
 */
static __xdata uint8_t *helper_31d5(uint8_t idx)
{
    /* Computes DPTR = 0x0A2C + idx (based on typical patterns) */
    return (__xdata uint8_t *)(0x0A2C + idx);
}

/*
 * helper_31e2 - Address calculation with 0x0C base
 * Address: 0x31e2
 * Takes A as index, computes DPTR
 */
static __xdata uint8_t *helper_31e2(uint8_t idx)
{
    /* Computes DPTR = 0x0A2C + 0x0C + idx */
    return (__xdata uint8_t *)(0x0A38 + idx);
}

/*
 * helper_325f - Address calculation with 0x2F base
 * Address: 0x325f
 * Takes A as index, computes DPTR
 */
static __xdata uint8_t *helper_325f(uint8_t idx)
{
    /* Computes DPTR = 0x0A2C + 0x2F + idx = 0x0A5B + idx */
    return (__xdata uint8_t *)(0x0A5B + idx);
}

/*
 * helper_31e0 - Alternate address calculation
 * Address: 0x31e0
 */
static __xdata uint8_t *helper_31e0(void)
{
    /* Returns address based on R6 */
    return (__xdata uint8_t *)0x0A2C;
}

static void helper_50db(void)
{
    uint8_t queue_idx;
    uint8_t val_r6, val_r5;
    __xdata uint8_t *ptr;

    /* Read queue index */
    queue_idx = G_EP_DISPATCH_OFFSET;

    /* Only process if queue index < 0x20 */
    if (queue_idx >= 0x20) {
        return;
    }

    /* Call helper_31d5 with queue_idx, then clear value */
    ptr = helper_31d5(queue_idx);
    *ptr = 0;

    /* Compute address: 0x0C + queue_idx, call helper_31e2 */
    ptr = helper_31e2(0x0C + queue_idx);
    val_r6 = *ptr;

    /* Compute address: 0x2F + queue_idx, call helper_325f */
    ptr = helper_325f(0x2F + queue_idx);
    val_r5 = *ptr;

    /* Call helper_31e0 and write val_r6 */
    ptr = helper_31e0();
    *ptr = val_r6;

    /* Compute address: 0x2F + val_r6, call helper_325f and write val_r5 */
    ptr = helper_325f(0x2F + val_r6);
    *ptr = val_r5;

    /* Check if IDATA[0x0D] == R7, if so update IDATA[0x0D] with R6 */
    if (I_QUEUE_IDX == queue_idx) {
        I_QUEUE_IDX = val_r6;
    }
}

/*
 * helper_5409 - Queue/state cleanup helper
 * Address: 0x5409-0x5415 (13 bytes)
 *
 * Clears various state variables and jumps to helper_039a.
 *
 * Original disassembly:
 *   5409: clr a
 *   540a: mov dptr, #0x0b2e
 *   540d: movx @dptr, a         ; Clear 0x0B2E
 *   540e: mov r0, #0x6a
 *   5410: mov @r0, a            ; Clear IDATA[0x6A]
 *   5411: mov dptr, #0x06e6
 *   5414: movx @dptr, a         ; Clear 0x06E6
 *   5415: ljmp 0x039a           ; Jump to helper_039a
 */
static void helper_5409(void)
{
    /* Clear state variables */
    G_USB_TRANSFER_FLAG = 0;
    I_STATE_6A = 0;
    G_STATE_FLAG_06E6 = 0;

    /* Call cleanup handler */
    helper_039a();
}

/*
 * helper_0206 - DMA buffer configuration helper
 * Address: 0x0206-0x02c4+ (complex)
 *
 * Sets up DMA buffer configuration based on flags in r5 and value in r7.
 * Writes to various DMA control registers (0xD800-0xD80F, 0xC8D4, etc.)
 *
 * Parameters:
 *   r5: Flag byte (bits control different modes)
 *       - bit 1 (0x02): ?
 *       - bit 2 (0x04): Use XDATA 0x0056-0x0057 source
 *       - bit 4 (0x10): Extended mode
 *   r7: DMA channel/index value
 *
 * Original disassembly (simplified):
 *   0206: Check (r5 & 0x06) != 0
 *   020b-0229: If yes, set 0xC8D4=0xA0, copy 0x0056-0x0057 to 0x905B-0x905C and 0xD802-0xD803
 *   022b-0246: If no, set 0xC8D4=(r7|0x80), configure 0xC4ED-0xC4EF, copy to 0xD802-0xD803
 *   0247-0255: Clear 0xD804-0xD807, 0xD80F
 *   0256-02c4: Check r5 bit 4, further configuration based on 0x07E5 state
 */
void helper_0206(uint8_t r5, uint8_t r7)
{
    uint8_t val, r2, r3;

    if (r5 & 0x06) {
        /* Path when r5 bits 1-2 are set */
        REG_DMA_CONFIG = 0xA0;

        /* Copy buffer info from 0x0056-0x0057 to 0x905B-0x905C and 0xD802-0xD803 */
        r2 = G_USB_ADDR_HI_0056;
        r3 = G_USB_ADDR_LO_0057;
        REG_USB_EP_BUF_HI = r2;
        REG_USB_EP_BUF_LO = r3;
        REG_USB_EP_BUF_DATA = r2;
        REG_USB_EP_BUF_PTR_LO = r3;
    } else {
        /* Path when r5 bits 1-2 are clear */
        REG_DMA_CONFIG = r7 | 0x80;

        /* Read and modify NVMe DMA control */
        val = REG_NVME_DMA_CTRL_ED;
        val = (val & 0xC0) | r7;
        REG_NVME_DMA_CTRL_ED = val;

        /* Read NVMe DMA addr and write to USB endpoint buffer */
        r3 = REG_NVME_DMA_ADDR_LO;
        val = REG_NVME_DMA_ADDR_HI;
        REG_USB_EP_BUF_DATA = val;
        REG_USB_EP_BUF_PTR_LO = r3;
    }

    /* Clear CSW tag bytes and control 0F */
    USB_CSW->tag0 = 0;
    USB_CSW->tag1 = 0;
    USB_CSW->tag2 = 0;
    USB_CSW->tag3 = 0;
    REG_USB_EP_CTRL_0F = 0;

    /* Check r5 bit 4 for extended mode */
    if (r5 & 0x10) {
        /* Extended mode - set 0xD800 = 4, copy from 0x0054 to 0xD807 */
        REG_USB_EP_BUF_CTRL = 0x04;
        USB_CSW->tag3 = G_BUFFER_LENGTH_HIGH;
        /* r4 = 0x08 for final processing */
    } else {
        /* Normal mode - set 0xD800 = 3 */
        REG_USB_EP_BUF_CTRL = 0x03;

        /* Check state at 0x07E5 */
        if (G_TRANSFER_ACTIVE == 0) {
            /* Check r5 bit 2 */
            if (r5 & 0x04) {
                /* Set DMA config = 0xA0, USB EP status = 0x28 */
                REG_DMA_CONFIG = 0xA0;
                USB_CSW->tag2 = 0x28;
            }
            /* Further processing at 0x028c-0x02c4 omitted for now */
        }
    }
}

/*
 * helper_020b - DMA buffer configuration (direct entry)
 * Address: 0x020b-0x02fc
 *
 * This is an alternate entry point that skips the initial (r5 & 0x06) check
 * in helper_0206. It always takes the "bits set" path which:
 *   - Sets REG_DMA_CONFIG = 0xA0
 *   - Copies from G_USB_ADDR_HI_0056/G_USB_ADDR_LO_0057 to endpoint buffers
 *
 * Called directly 13 times in the firmware when the caller knows they
 * want the 0xA0 DMA config path with addresses from 0x0056-0x0057.
 *
 * Parameters:
 *   r5: Flag byte (bits control modes after initial setup)
 *       - bit 1 (0x02): Alternative mode with table copy
 *       - bit 2 (0x04): Set DMA config to 0xA0, EP status to 0x28
 *       - bit 4 (0x10): Extended mode (D800 = 4 instead of 3)
 *   r7: DMA channel/index value (used in later processing)
 */
void helper_020b(uint8_t r5, uint8_t r7)
{
    uint8_t r2, r3, r4;

    /* Direct path - always write 0xA0 to DMA config */
    /* 0x020b: mov dptr, #0xc8d4 / mov a, #0xa0 / movx @dptr, a */
    REG_DMA_CONFIG = 0xA0;

    /* Copy buffer info from 0x0056-0x0057 to 0x905B-0x905C and 0xD802-0xD803 */
    /* 0x0211-0x0228 */
    r2 = G_USB_ADDR_HI_0056;
    r3 = G_USB_ADDR_LO_0057;
    REG_USB_EP_BUF_HI = r2;
    REG_USB_EP_BUF_LO = r3;
    REG_USB_EP_BUF_DATA = r2;
    REG_USB_EP_BUF_PTR_LO = r3;

    /* 0x0247-0x0255: Clear D804-D807 and D80F */
    USB_CSW->tag0 = 0;
    USB_CSW->tag1 = 0;
    USB_CSW->tag2 = 0;
    USB_CSW->tag3 = 0;
    REG_USB_EP_CTRL_0F = 0;

    /* 0x0256: Check r5 bit 4 for extended mode */
    if (r5 & 0x10) {
        /* Extended mode - 0x025a-0x026a */
        REG_USB_EP_BUF_CTRL = 0x04;
        USB_CSW->tag3 = G_BUFFER_LENGTH_HIGH;
        r4 = 0x08;
    } else {
        /* Normal mode - 0x026c-0x02c3 */
        REG_USB_EP_BUF_CTRL = 0x03;

        /* Check state at 0x07E5 */
        if (G_TRANSFER_ACTIVE == 0) {
            /* r5 bits 1-2 clear, G_TRANSFER_ACTIVE == 0: r4 = 0x10 */
            r4 = 0x10;

            /* Check r5 bit 2 */
            if (r5 & 0x04) {
                /* 0x027e-0x028a: Set DMA config = 0xA0, USB EP tag2 = 0x28 */
                REG_DMA_CONFIG = 0xA0;
                USB_CSW->tag2 = 0x28;
            } else if (r5 & 0x02) {
                /* 0x028d-0x0290: bit 1 set but not bit 2 - just continue */
                /* Fall through to common code */
            }
        } else {
            /* G_TRANSFER_ACTIVE != 0: 0x0292-0x02c3 */
            /* This path copies from table 0x0201 to D810-D821 */
            r4 = 0x22;

            /* TODO: The table copy loop at 0x0296-0x02b0 reads from
             * addresses 0x0201-0x0212 and writes to 0xD810-0xD821.
             * This appears to be initialization data embedded in code.
             * For now, leaving as stub since this path may be rare. */
        }
    }

    /* 0x02c4-0x02fc: Final processing based on r5 and r4 */
    /* TODO: Remaining logic involves calls to 0x3133, 0x31c5, 0x3249,
     * register writes to 0xC509, 0x905A, 0x90E1, and final call to 0x0006 */
    (void)r4;  /* Suppress unused warning until fully implemented */
    (void)r7;
}

/*
 * helper_45d0 - Transfer control helper
 * Address: 0x45d0-0x4663+ (complex)
 *
 * Handles transfer control operations. Clears 0x044D, then computes
 * an index based on param (r7) + 0x7C, calls helper functions, and
 * manages queue state.
 *
 * Original disassembly:
 *   45d0: clr a
 *   45d1: mov dptr, #0x044d
 *   45d4: movx @dptr, a          ; Clear 0x044D
 *   45d5: mov a, #0x7c
 *   45d7: add a, r7              ; a = param + 0x7c
 *   45d8: lcall 0x166f           ; Call helper with computed index
 *   45db: movx a, @dptr          ; Read result
 *   45dc: mov r6, a
 *   45dd: cjne a, #0x01, 0x45e9  ; If result != 1, skip
 *   ... (complex state machine logic)
 */
void helper_45d0(uint8_t param)
{
    uint8_t result;

    /* Clear state at 0x044D */
    G_LOG_INIT_044D = 0;

    /* TODO: The full implementation requires:
     * - Call to 0x166f with (param + 0x7c) to get index
     * - Compare result == 1 for special path
     * - Multiple helper calls (0x1752, 0x15d4, 0x1646, 0x17cd)
     * - Queue management with checks against 2 and 4
     */
    (void)result;
    (void)param;
}

/*
 * helper_0421 - Register initialization for 0xE65F
 * Address: 0x0421-0x0424 (5 bytes)
 *
 * Part of register initialization table. Sets DPTR = 0xE65F and
 * jumps to 0x0300 to perform common initialization.
 *
 * The effect is to clear/initialize register 0xE65F.
 *
 * Original disassembly:
 *   0421: mov dptr, #0xe65f
 *   0424: ajmp 0x0300
 */
void helper_0421(uint8_t param)
{
    (void)param;
    /* Clear/initialize the register at 0xE65F */
    REG_DEBUG_INT_E65F = 0;
}

/*
 * helper_0417 - Register initialization for 0xE62F
 * Address: 0x0417-0x041a (5 bytes)
 *
 * Part of register initialization table. Sets DPTR = 0xE62F and
 * jumps to 0x0300 to perform common initialization.
 *
 * The effect is to clear/initialize register 0xE62F.
 *
 * Original disassembly:
 *   0417: mov dptr, #0xe62f
 *   041a: ajmp 0x0300
 */
void helper_0417(void)
{
    /* Clear/initialize the register at 0xE62F */
    REG_DEBUG_INT_E62F = 0;
}

/*
 * helper_16f3 - DMA status bit clear
 * Address: 0x16f3-0x16fe (12 bytes)
 *
 * Clears bits 3 and 2 of DMA status register 0xC8D6.
 * This is used to acknowledge/clear DMA interrupt flags.
 *
 * Original disassembly:
 *   16f3: mov dptr, #0xc8d6
 *   16f6: movx a, @dptr          ; Read DMA status
 *   16f7: anl a, #0xf7           ; Clear bit 3 (0xF7 = 11110111)
 *   16f9: movx @dptr, a          ; Write back
 *   16fa: movx a, @dptr          ; Read again
 *   16fb: anl a, #0xfb           ; Clear bit 2 (0xFB = 11111011)
 *   16fd: movx @dptr, a          ; Write back
 *   16fe: ret
 */
void helper_16f3(void)
{
    uint8_t status;

    /* Read DMA status register */
    status = REG_DMA_STATUS;

    /* Clear bit 3 (error flag) */
    status &= ~DMA_STATUS_ERROR;
    REG_DMA_STATUS = status;

    /* Read again and clear bit 2 (done flag) */
    status = REG_DMA_STATUS;
    status &= ~DMA_STATUS_DONE;
    REG_DMA_STATUS = status;
}

/* Forward declarations for helper_3f4a dependencies */
extern void usb_func_1c5d(__xdata uint8_t *ptr);  /* 0x1c5d */
extern void usb_set_dma_mode_params(uint8_t val); /* 0x1c4a */
extern uint8_t nvme_get_pcie_count_config(void);  /* 0x1c90 */
extern uint8_t helper_466b(void);                 /* 0x466b - check state */
extern uint8_t helper_043f(void);                 /* 0x043f - check callback */
extern void helper_36ab(void);                    /* 0x36ab - setup transfer */
extern void helper_04da(uint8_t param);           /* 0x04da - param setup */
extern uint8_t usb_read_transfer_params(void);    /* 0x31a5 */
extern uint8_t helper_322e(void);                 /* 0x322e - compare helper */
extern uint8_t helper_313f(uint8_t r0_val);       /* 0x313f - count check */
extern void helper_31ad(__xdata uint8_t *ptr);    /* 0x31ad - transfer helper */
extern void scsi_completion_handler(void);        /* 0x5216 */
extern uint8_t nvme_get_dma_status_masked(uint8_t index); /* 0x3298 */

/*
 * helper_3f4a - Initial status check for state_action_dispatch
 * Address: 0x3f4a-0x40d8 (~400 bytes)
 *
 * This is a complex status check function with multiple return values:
 *   0 - Check failed, action cannot proceed
 *   1 - Transfer completed successfully
 *   2 - Return via R3=2 path (pending state)
 *   5 - PCIe link not ready or transfer error
 *  11 (0x0B) - Transfer in progress
 *
 * Called at the start of state_action_dispatch to check if the action can proceed.
 */
uint8_t helper_3f4a(void)
{
    uint8_t status;
    uint8_t val_06e5, val_044b;

    /* 0x3f4a: Check 0x07EF - if non-zero, return 0 */
    if (G_SYS_FLAGS_07EF != 0) {
        /* 0x3fda path: return 0 */
        helper_523c(0, 0x3A, 2);
        return 5;
    }

    /* 0x3f53: Call usb_func_1c5d with dptr=0x0464 */
    usb_func_1c5d(&G_SYS_STATUS_PRIMARY);

    /* 0x3f59: Clear 0x07E5 */
    G_TRANSFER_ACTIVE = 0;

    /* 0x3f5e: Call usb_set_dma_mode_params(0) */
    usb_set_dma_mode_params(0);

    /* 0x3f61: Check 0x0002 */
    if (G_IO_CMD_STATE != 0) {
        /* 0x3f67: Clear 0x0B2F */
        G_USB_TRANSFER_FLAG = 0;
        /* Then jump to 0x3f82 */
    } else {
        /* 0x3f6e: Check 0xB480 bit 0 (PCIe link status) */
        if (!(REG_TUNNEL_LINK_CTRL & 0x01)) {
            return 5;  /* PCIe link not ready */
        }

        /* 0x3f78: Call nvme_get_pcie_count_config() */
        status = nvme_get_pcie_count_config();

        /* 0x3f7b: Check bit 7 of result */
        if (status & 0x80) {
            /* Return 2 with R3=2, R5=4 via 0x3fd3 -> 0x3fde */
            helper_523c(2, 4, 2);
            return 5;
        }
    }

    /* 0x3f82: Check G_XFER_STATE_0AF6 */
    if (G_XFER_STATE_0AF6 == 0) {
        /* 0x3f88: Call helper_466b */
        status = helper_466b();
        if (status != 0) {
            return 0x0B;  /* Return 11 */
        }
    }

    /* 0x3f91: Call nvme_get_pcie_count_config and check if == 4 */
    status = nvme_get_pcie_count_config();
    if (status == 4) {
        /* 0x3fe6: Branch for mode 4 */
        val_06e5 = G_MAX_LOG_ENTRIES;
        val_044b = G_LOG_COUNTER_044B;

        if (val_06e5 == val_044b) {
            /* Check 0x0AF8 */
            if (G_POWER_INIT_FLAG == 0) {
                /* Check 0xB480 bit 0 */
                if (!(REG_TUNNEL_LINK_CTRL & 0x01)) {
                    helper_04da(2);
                }

                /* 0x4004: Call helper_36ab */
                helper_36ab();

                /* Check 0x0AF8 again */
                if (G_POWER_INIT_FLAG != 0) {
                    return 0x0B;
                }
            }
        }
        return 0;
    }

    /* 0x3f98: Check 0x06E8 */
    if (G_WORK_06E8 != 0) {
        goto check_044c;
    }

    /* 0x3f9e: Call helper_043f */
    status = helper_043f();
    if (status == 0) {
        /* Jump to 0x3fda - return 0 */
        helper_523c(0, 0x3A, 2);
        return 5;
    }

    /* 0x3fa4: Check table entry at 0x0464 index */
    {
        uint8_t idx = G_SYS_STATUS_PRIMARY;
        uint16_t table_addr = 0x057E + (idx * 0x0A);
        uint8_t table_val = *(__xdata uint8_t *)table_addr;

        if (table_val == 0x0F) {
            /* Jump to 0x3fda */
            helper_523c(0, 0x3A, 2);
            return 5;
        }
    }

check_044c:
    /* 0x3fba: Check 0x044C */
    if (G_LOG_ACTIVE_044C == 0) {
        /* Check 0x0002 */
        if (G_IO_CMD_STATE == 0) {
            /* Check 0x0AF6 */
            if (G_XFER_STATE_0AF6 != 0) {
                return 0x0B;
            }
        }

        /* 0x3fcc: Clear 0x044C, set R3=1 */
        G_LOG_ACTIVE_044C = 0;
        /* R3=1, R5=4, R7=2 -> return 5 via helper_523c */
        helper_523c(1, 4, 2);
        return 5;
    }

    /* 0x3fd7: Return 0x0B */
    return 0x0B;
}

/*
 * helper_1d1d - Setup helper for state_action_dispatch
 * Address: 0x1d1d-0x1d23 (7 bytes)
 *
 * Sets USB transfer flag to 1 to indicate transfer active.
 *
 * Original disassembly:
 *   1d1d: mov dptr, #0x0b2e    ; G_USB_TRANSFER_FLAG
 *   1d20: mov a, #0x01
 *   1d22: movx @dptr, a        ; Write 1
 *   1d23: ret
 */
void helper_1d1d(void)
{
    G_USB_TRANSFER_FLAG = 1;
}

/*
 * helper_1c9f - Core processing and buffer setup
 * Address: 0x1c9f-0x1cad (15 bytes)
 *
 * Calls core_handler_4ff2 with param=0, then calls helper_4e6d to
 * configure buffers. Returns OR of IDATA[0x16] and IDATA[0x17].
 *
 * Original disassembly:
 *   1c9f: lcall 0x4ff2         ; core_handler_4ff2(0)
 *   1ca2: lcall 0x4e6d         ; helper_4e6d
 *   1ca5: mov r0, #0x16
 *   1ca7: mov a, @r0           ; R4 = [0x16]
 *   1ca8: mov r4, a
 *   1ca9: inc r0
 *   1caa: mov a, @r0           ; R5 = [0x17]
 *   1cab: mov r5, a
 *   1cac: orl a, r4            ; A = R4 | R5
 *   1cad: ret
 */
uint8_t helper_1c9f(void)
{
    /* Call core handler with param=0 */
    core_handler_4ff2(0);

    /* Configure buffer base addresses */
    helper_4e6d();

    /* Return non-zero if either byte is non-zero */
    return I_CORE_STATE_L | I_CORE_STATE_H;
}

/*
 * helper_4f77 - Processing helper with state comparison
 * Address: 0x4f77-0x4fb5 (63 bytes)
 *
 * Takes a parameter (0 or 0x80) based on action code bit 1.
 * Stores param to 0x0A84, then performs state-dependent checks.
 *
 * Original disassembly:
 *   4f77: mov dptr, #0x0a84
 *   4f7a: mov a, r7            ; store param to 0x0A84
 *   4f7b: movx @dptr, a
 *   4f7c: lcall 0x1b7e         ; load idata[0x09:0x0c] to R4-R7
 *   4f7f: clr c
 *   4f80: lcall 0x0d22         ; subtract_16 (IDATA[0x16:0x17] - R6:R7)
 *   4f83: jnz 0x4f94           ; if non-zero, continue
 *   4f85-4f91: check if 0x0A84 == 0x0AF3, return 1 if equal
 *   4f94: if 0x0A84 == 0x80, call 0x1b7e, setb c, call 0x0d22
 *   4fa7: else call 0x1b7e, setb c, call 0x0d22
 *   4fb3: return 0 if carry set, else return 1
 */
void helper_4f77(uint8_t param)
{
    uint8_t stored_param;
    uint8_t state_val;

    /* Store param to 0x0A84 */
    G_STATE_WORK_0A84 = param;

    /* Read IDATA[0x16:0x17] and compare */
    /* The actual comparison logic is complex, involving subtract_16 */
    stored_param = G_STATE_WORK_0A84;

    /* Check if param matches state at 0x0AF3 */
    state_val = G_XFER_STATE_0AF3;

    if (stored_param == state_val) {
        /* Match - early return would be 1 */
        return;
    }

    if (stored_param == 0x80) {
        /* Special 0x80 case */
        /* Perform additional checks */
    }

    /* Default case - no special handling needed */
}

/*
 * helper_11a2 - Transfer helper
 * Address: 0x11a2
 *
 * Performs transfer operation, returns status.
 * Called during DMA/buffer transfers.
 */
uint8_t helper_11a2(uint8_t param)
{
    (void)param;
    /* TODO: Implement transfer logic from 0x11a2 */
    return 1;  /* Default: success */
}

/*
 * helper_5359 - Buffer setup
 * Address: 0x5359
 *
 * Sets up buffer configuration for transfers.
 */
void helper_5359(uint8_t param)
{
    (void)param;  /* Currently unused */
    /* TODO: Implement buffer setup from 0x5359 */
}

/*
 * helper_1cd4 - Status helper with bit 1 flag
 * Address: 0x1cd4
 *
 * Returns status with bit 1 indicating a flag state.
 */
uint8_t helper_1cd4(void)
{
    /* TODO: Implement status check from 0x1cd4 */
    return 0;
}

/*
 * helper_1cc8 - Register setup
 * Address: 0x1cc8
 *
 * Configures registers for DMA/transfer operations.
 */
void helper_1cc8(void)
{
    /* TODO: Implement register setup from 0x1cc8 */
}

/*
 * helper_1c22 - Carry flag helper
 * Address: 0x1c22
 *
 * Helper that returns carry flag state for comparison operations.
 */
void helper_1c22(void)
{
    /*
     * Based on 0x1c22-0x1c29:
     *   1c22: mov dptr, #0x0171
     *   1c25: movx a, @dptr      ; Read G_QUEUE_STATUS
     *   1c26: setb c             ; Set carry
     *   1c27: subb a, #0x00      ; A = A - 0 - C = A - 1
     *   1c29: ret
     *
     * This decrements the value at 0x0171 (due to setb c before subb).
     * The carry flag will be clear if value was > 0, set if value was 0.
     * But the result isn't stored back, so this is just a read operation
     * that affects carry flag for caller.
     */
    uint8_t val = G_SCSI_CTRL;  /* 0x0171 */
    (void)val;  /* Carry flag logic not directly translatable to C */
}

/*
 * helper_1b9a - Table lookup helper
 * Address: 0x1b9a-0x1ba4 (11 bytes)
 *
 * Looks up value from table at 0x054E, using val * 0x14 as offset.
 * Returns value at table[val * 0x14].
 *
 * Original:
 *   1b9a: mov dptr, #0x054e   ; Table base
 *   1b9d: mov 0xf0, #0x14     ; B = 20 (record size)
 *   1ba0: lcall 0x0dd1        ; DPTR += A * B
 *   1ba3: movx a, @dptr       ; Read from computed address
 *   1ba4: ret
 */
static uint8_t helper_1b9a(uint8_t val)
{
    uint16_t addr;
    __xdata uint8_t *ptr;

    /* Table base 0x054E, record size 0x14 (20 bytes) */
    addr = 0x054E + ((uint16_t)val * 0x14);
    ptr = (__xdata uint8_t *)addr;
    return *ptr;
}

/*
 * helper_1b9d - Table lookup helper (shared entry point)
 * Address: 0x1b9d-0x1ba4
 *
 * Same as 1b9a but called with DPTR already set.
 * Since we're calling directly, we use 0x054F as the base
 * (the A register in original code was already set).
 *
 * Actually, helper_4e6d calls this at 0x4EAB with dptr=0x054F:
 *   4ea8: mov dptr, #0x054f
 *   4eab: lcall 0x1b9d
 *
 * So this expects DPTR to be pre-set by caller.
 * For our C implementation, we pass the table index.
 */
static uint8_t helper_1b9d(uint8_t val)
{
    uint16_t addr;
    __xdata uint8_t *ptr;

    /* Called with DPTR = 0x054F from 4e6d context */
    /* Table base 0x054F, record size 0x14 (20 bytes) */
    addr = 0x054F + ((uint16_t)val * 0x14);
    ptr = (__xdata uint8_t *)addr;
    return *ptr;
}

/*
 * helper_4e6d - Buffer base address configuration
 * Address: 0x4e6d-0x4eb2 (70 bytes)
 *
 * Sets up buffer base addresses for DMA transfers based on
 * G_SYS_STATUS_PRIMARY and G_SYS_STATUS_SECONDARY values.
 *
 * Key operations:
 * - Reads G_SYS_STATUS_PRIMARY (0x0464), sets base = 0xA0 or 0xA8
 * - Writes base address to G_BUF_BASE_HI/LO (0x021A-0x021B)
 * - Reads G_SYS_STATUS_SECONDARY (0x0465)
 * - Computes index via helper_1b9a and stores to G_DMA_WORK_0216
 * - Computes table entry at 0x054C + (index * 20)
 * - Writes buffer address to G_BUF_ADDR_HI/LO (0x0218-0x0219)
 * - Computes another value via helper_1b9d and stores to 0x0217
 *
 * Original disassembly:
 *   4e6d: mov dptr, #0x0464   ; Read G_SYS_STATUS_PRIMARY
 *   4e70: movx a, @dptr
 *   4e71: mov r6, #0xa0       ; Default base = 0xA0
 *   4e73: cjne a, #0x01, 4e78 ; If status != 1, skip
 *   4e76: mov r6, #0xa8       ; Use base 0xA8 for status == 1
 *   4e78: mov r7, #0x00
 *   4e7a: mov dptr, #0x021a   ; Write base to G_BUF_BASE_HI
 *   4e7d: mov a, r6
 *   4e7e: movx @dptr, a
 *   4e7f: inc dptr            ; Write 0 to G_BUF_BASE_LO
 *   4e80: mov a, r7
 *   4e81: movx @dptr, a
 *   ...continues with table lookup and address computation
 */
void helper_4e6d(void)
{
    uint8_t status;
    uint8_t base_hi;
    uint8_t index;
    uint8_t offset;
    uint16_t table_addr;
    __xdata uint8_t *ptr;

    /* Read primary status to select buffer base */
    status = G_SYS_STATUS_PRIMARY;

    /* Set base address: 0xA800 for status=1, 0xA000 otherwise */
    if (status == 1) {
        base_hi = 0xA8;
    } else {
        base_hi = 0xA0;
    }

    /* Store buffer base address */
    G_BUF_BASE_HI = base_hi;
    G_BUF_BASE_LO = 0;

    /* Read secondary status and compute address offset */
    index = G_SYS_STATUS_SECONDARY;
    offset = helper_1b9a(index);
    G_DMA_WORK_0216 = offset;

    /* Compute table entry: 0x054C + (index * 0x14) */
    table_addr = 0x054C + ((uint16_t)index * 0x14);
    ptr = (__xdata uint8_t *)table_addr;

    /* Read address from table and store to buffer address globals */
    G_BUF_ADDR_HI = ptr[0];
    G_BUF_ADDR_LO = ptr[1];

    /* Read from 0x054F + computed offset and store to 0x0217 */
    index = G_SYS_STATUS_SECONDARY;
    offset = helper_1b9d(index);
    G_DMA_OFFSET = offset;
}

/*
 * transfer_helper_1709 - Write 0xFF to CE43 and return DPTR
 * Address: 0x1709-0x1712 (10 bytes)
 *
 * Writes 0xFF to register 0xCE43 (SCSI buffer control) and
 * returns DPTR pointing to 0xCE42 for caller's use.
 *
 * Original disassembly:
 *   1709: mov dptr, #0xce43
 *   170c: mov a, #0xff
 *   170e: movx @dptr, a          ; Write 0xFF to 0xCE43
 *   170f: mov dptr, #0xce42      ; DPTR = 0xCE42
 *   1712: ret
 *
 * This appears to reset/initialize SCSI buffer control registers.
 */
void transfer_helper_1709(void)
{
    /* Write 0xFF to CE43 */
    REG_SCSI_DMA_PARAM3 = 0xFF;

    /* The DPTR is left at 0xCE42 for caller to use */
    /* In C we can't set DPTR directly, but caller will use next address */
}

/*
 * helper_466b - Check transfer state
 * Address: 0x466b
 *
 * Returns non-zero if transfer is busy/in-progress, 0 if idle.
 * Called from helper_3f4a when G_XFER_STATE_0AF6 == 0.
 */
uint8_t helper_466b(void)
{
    uint8_t val;

    /* Check G_SYS_FLAGS_07EF - if non-zero, return 0 (not busy) */
    val = G_SYS_FLAGS_07EF;
    if (val != 0) {
        return 0;
    }

    /* Check transfer busy flag - if non-zero, return 1 (busy) */
    val = G_TRANSFER_BUSY_0B3B;
    if (val != 0) {
        return 1;
    }

    /* Check bit 5 of PHY_EXT_56 register */
    val = REG_PHY_EXT_56;
    if ((val & 0x20) == 0) {
        /* bit 5 not set: call 0x04E9, then return 1 */
        return 1;
    }

    /* bit 5 set: call 0x1743, store result, continue checking... */
    /* For now, return 0 as default busy check */
    return 0;
}

/*
 * helper_043f - Check callback/operation status
 * Address: 0x043f
 *
 * Performs callback status check.
 * Returns non-zero on success, 0 on failure.
 */
uint8_t helper_043f(void)
{
    /* TODO: Implement callback check from 0x043f */
    return 1;  /* Default: success */
}

/*
 * helper_36ab - Setup transfer operation
 * Address: 0x36ab
 *
 * Initializes transfer state and parameters.
 * Called during transfer setup in helper_3f4a.
 */
void helper_36ab(void)
{
    /* TODO: Implement transfer setup from 0x36ab */
}

/*
 * helper_04da - Parameter setup
 * Address: 0x04da
 *
 * Takes a parameter and performs state/parameter setup.
 */
void helper_04da(uint8_t param)
{
    uint8_t val;

    /*
     * Based on 0xE3B7:
     * Read CC17, call helper, check param bits
     * If bit 0 set: clear bit 0 of 0x92C4
     * If bit 1 set: call BCEBs, then C2E6 with R7=0
     */
    val = REG_TIMER1_CSR;  /* Read CC17 */

    /* Check bit 0 of param */
    if (param & 0x01) {
        val = REG_POWER_CTRL_92C4;
        val &= 0xFE;  /* Clear bit 0 */
        REG_POWER_CTRL_92C4 = val;
    }

    /* Check bit 1 of param */
    if (param & 0x02) {
        /* Would call 0xBCEB and 0xC2E6 */
    }
}

/*
 * helper_322e - Compare helper
 * Address: 0x322e
 *
 * Compares values and returns carry flag result.
 * Returns 1 if carry set (comparison failed), 0 if clear (success).
 */
uint8_t helper_322e(void)
{
    /* TODO: Implement compare logic from 0x322e */
    return 0;  /* Default: comparison OK */
}

/*
 * helper_313f - Check if 32-bit value at IDATA address is non-zero
 * Address: 0x313f-0x3146 (8 bytes)
 *
 * Original disassembly:
 *   313f: lcall 0x0d78    ; idata_load_dword - load IDATA[R0] into R4-R7
 *   3142: mov a, r4
 *   3143: orl a, r5
 *   3144: orl a, r6
 *   3145: orl a, r7
 *   3146: ret             ; Returns non-zero if any byte is non-zero
 */
uint8_t helper_313f(uint8_t r0_val)
{
    __idata uint8_t *ptr = (__idata uint8_t *)r0_val;
    return ptr[0] | ptr[1] | ptr[2] | ptr[3];
}

/*
 * helper_31ad - Transfer parameter helper
 * Address: 0x31ad
 *
 * Processes transfer parameters at the given pointer.
 */
void helper_31ad(__xdata uint8_t *ptr)
{
    /*
     * Based on 0x31ad-0x31c2:
     * Reads value from ptr[r7], computes new address (ptr_hi + r6),
     * reads from that address, stores to address (0x80 + r6) + r7
     *
     * This appears to copy transfer parameters between two address ranges.
     * The 0x80xx addresses are in the USB buffer area.
     */
    uint8_t val;

    /* Read first byte from source pointer */
    val = ptr[0];

    /* Write to USB buffer area (0x8000 base) */
    /* This is a simplified implementation - the original uses register
     * values (r6, r7) for computed addressing */
    G_BUF_ADDR_HI = val;  /* Store to buffer address globals */
}

/*
 * helper_3147 - Copy USB status registers to D804-D807
 * Address: 0x3147-0x3167 (33 bytes)
 *
 * Copies 4 bytes from 0x911F-0x9122 to 0xD804-0xD807.
 * Used to transfer USB endpoint status to DMA buffer config.
 *
 * Original disassembly:
 *   3147: mov dptr, #0x911f
 *   314a: movx a, @dptr
 *   314b: mov dptr, #0xd804
 *   314e: movx @dptr, a
 *   314f: mov dptr, #0x9120
 *   3152: movx a, @dptr
 *   3153: mov dptr, #0xd805
 *   3156: movx @dptr, a
 *   3157: mov dptr, #0x9121
 *   315a: movx a, @dptr
 *   315b: mov dptr, #0xd806
 *   315e: movx @dptr, a
 *   315f: mov dptr, #0x9122
 *   3162: movx a, @dptr
 *   3163: mov dptr, #0xd807
 *   3166: movx @dptr, a
 *   3167: ret
 */
void helper_3147(void)
{
    /* Copy USB status 0x911F-0x9122 to CSW tag 0xD804-0xD807 */
    USB_CSW->tag0 = REG_USB_STATUS_1F;
    USB_CSW->tag1 = REG_USB_STATUS_20;
    USB_CSW->tag2 = REG_USB_STATUS_21;
    USB_CSW->tag3 = REG_USB_STATUS_22;
}

/*
 * helper_3168 - Calculate address from IDATA 0x38
 * Address: 0x3168-0x3178 (17 bytes)
 *
 * Computes DPTR = 0x00C2 + IDATA[0x38], then clears value at DPTR.
 * Then computes DPTR = 0x00E5 + IDATA[0x38].
 *
 * Original disassembly:
 *   3168: mov a, #0xc2
 *   316a: add a, 0x38        ; add IDATA[0x38]
 *   316c: mov dpl, a
 *   316e: clr a
 *   316f: addc a, #0x00
 *   3171: mov dph, a
 *   3173: clr a
 *   3174: movx @dptr, a      ; clear [0x00C2 + IDATA[0x38]]
 *   3175: mov a, #0xe5
 *   3177: add a, 0x38
 *   3179: mov dpl, a
 *   317b: clr a
 *   317c: addc a, #0x00
 *   317e: mov dph, a
 *   3180: ret
 */
void helper_3168(void)
{
    uint8_t idx = I_WORK_38;
    __xdata uint8_t *ptr;

    /* Clear value at 0x00C2 + idx */
    ptr = (__xdata uint8_t *)(0x00C2 + idx);
    *ptr = 0;

    /* DPTR left pointing to 0x00E5 + idx for caller */
}

/*
 * helper_3181 - Read 2 bytes from USB status register
 * Address: 0x3181-0x3188 (8 bytes)
 *
 * Reads two bytes from 0x910D-0x910E into R6 and A.
 * Returns the pair of values.
 *
 * Original disassembly:
 *   3181: mov dptr, #0x910d
 *   3184: movx a, @dptr      ; R6 = [0x910D]
 *   3185: mov r6, a
 *   3186: inc dptr
 *   3187: movx a, @dptr      ; A = [0x910E]
 *   3188: ret
 */
uint16_t helper_3181(void)
{
    uint8_t lo, hi;

    lo = REG_USB_STATUS_0D;
    hi = REG_USB_STATUS_0E;

    return ((uint16_t)hi << 8) | lo;
}

/*
 * helper_31c3 - Calculate address 0x9096 + A
 * Address: 0x31c3-0x31cd (11 bytes)
 *
 * Computes DPTR = 0x9096 + A (input param).
 * Returns DPTR pointing to the computed address.
 *
 * Original disassembly:
 *   31c3: add a, #0x96      ; A = A + 0x96
 *   31c5: mov r3, a
 *   31c6: clr a
 *   31c7: addc a, #0x90     ; A = 0x90 + carry
 *   31c9: mov dpl, r3
 *   31cb: mov dph, a
 *   31cd: ret
 */
__xdata uint8_t *helper_31c3(uint8_t idx)
{
    return (__xdata uint8_t *)(0x9096 + idx);
}

/*
 * helper_31ce - Read, mask with 0x7F, OR with 0x80, and write back
 * Address: 0x31ce-0x31d4 (7 bytes)
 *
 * Reads value at DPTR, clears bit 7, sets bit 7, writes back.
 * Effectively sets bit 7 of the value at DPTR.
 *
 * Original disassembly:
 *   31ce: movx a, @dptr
 *   31cf: anl a, #0x7f      ; Clear bit 7
 *   31d1: orl a, #0x80      ; Set bit 7
 *   31d3: movx @dptr, a
 *   31d4: ret
 */
void helper_31ce(__xdata uint8_t *ptr)
{
    uint8_t val = *ptr;
    val = (val & 0x7F) | 0x80;  /* Clear bit 7, then set bit 7 */
    *ptr = val;
}

/*
 * helper_31d5 - Calculate queue address 0x0108 + idx
 * Address: 0x31d5-0x31df (11 bytes)
 *
 * Computes DPTR = 0x0108 + R7 (index).
 *
 * Original disassembly:
 *   31d5: mov a, #0x08
 *   31d7: add a, r7
 *   31d8: mov dpl, a
 *   31da: clr a
 *   31db: addc a, #0x01
 *   31dd: mov dph, a
 *   31df: ret
 */
__xdata uint8_t *helper_31d5_queue(uint8_t idx)
{
    return (__xdata uint8_t *)(0x0108 + idx);
}

/*
 * helper_31e0 - Add 0x0C to index and compute address
 * Address: 0x31e0-0x31e9 (10 bytes)
 *
 * Computes DPTR = 0x000C + A.
 *
 * Original disassembly:
 *   31e0: add a, #0x0c
 *   31e2: mov dpl, a
 *   31e4: clr a
 *   31e5: addc a, #0x00
 *   31e7: mov dph, a
 *   31e9: ret
 */
__xdata uint8_t *helper_31e0_addr(uint8_t idx)
{
    return (__xdata uint8_t *)(0x000C + idx);
}

/*
 * helper_31ea - Table lookup with multiply by 10
 * Address: 0x31ea-0x31f5 (12 bytes)
 *
 * Reads index from DPTR, multiplies by 10 (0x0A), adds 0x7F,
 * computes new DPTR in 0x05xx range.
 *
 * Original disassembly:
 *   31ea: movx a, @dptr      ; Read index from DPTR
 *   31eb: mov b, #0x0a       ; B = 10
 *   31ee: mul ab             ; A*B -> BA
 *   31ef: add a, #0x7f       ; A = A + 0x7F
 *   31f1: mov dpl, a
 *   ... (continues with dph computation)
 */
uint8_t helper_31ea(__xdata uint8_t *ptr)
{
    uint8_t idx = *ptr;
    uint16_t addr;

    /* Table base 0x057F + (idx * 0x0A) */
    addr = 0x057F + ((uint16_t)idx * 0x0A);

    return *(__xdata uint8_t *)addr;
}

/*
 * helper_36ab_impl - Transfer setup handler
 * Address: 0x36ab-0x37c2 (~280 bytes)
 *
 * Configures SCSI/DMA registers for transfer operations.
 * Checks flags at 0x053E and 0x0552, then initializes CE7x registers.
 *
 * This is the main transfer setup function that prepares the
 * SCSI buffer and DMA engine for data transfers.
 */
void helper_36ab_impl(void)
{
    uint8_t val;

    /* Check if either 0x053E or 0x0552 is non-zero */
    if (G_SCSI_TRANSFER_FLAG == 0 &&
        G_SCSI_STATUS_FLAG == 0) {
        /* Both zero - skip transfer setup */
        return;
    }

    /* Initialize CE73-CE74: Set CE73=0x20, CE74=0x00 */
    REG_SCSI_BUF_CTRL0 = 0x20;
    REG_SCSI_BUF_CTRL1 = 0x00;

    /* Initialize CE80-CE82: CE81=0xFF, CE80=0x7F, CE82=0x3F */
    REG_SCSI_CMD_LIMIT_HI = 0xFF;
    REG_SCSI_CMD_LIMIT_LO = 0x7F;
    REG_SCSI_CMD_MODE = 0x3F;

    /* Read 0x0547 and compute CE44 value */
    val = G_SCSI_DEVICE_IDX;
    val = val - 0x09;  /* subb with carry clear */

    /* Read CE44, mask upper nibble, OR with computed value */
    {
        uint8_t ce44_val = REG_SCSI_DMA_PARAM4;
        ce44_val = (ce44_val & 0xF0) | (val & 0x0F);
        REG_SCSI_DMA_PARAM4 = ce44_val;
    }

    /* Get value from 0x057A table and configure CE44 upper nibble */
    val = helper_31ea(&G_EP_LOOKUP_TABLE);
    {
        uint8_t ce44_val = REG_SCSI_DMA_PARAM4;
        ce44_val = (ce44_val & 0x0F) | ((val << 4) & 0xF0);
        REG_SCSI_DMA_PARAM4 = ce44_val;
    }

    /* Update CE45 */
    {
        uint8_t ce45_val = REG_SCSI_DMA_PARAM5;
        val = G_SCSI_DEVICE_IDX - 0x09;
        ce45_val = (ce45_val & 0xF0) | (val & 0x0F);
        REG_SCSI_DMA_PARAM5 = ce45_val;
    }

    /* Read from 0x0543 (4 bytes) and write to CE76 */
    /* This uses the dword load helper 0x0d84 */
    {
        uint8_t b0 = G_SCSI_LBA_0;
        uint8_t b1 = G_SCSI_LBA_1;
        uint8_t b2 = G_SCSI_LBA_2;
        uint8_t b3 = G_SCSI_LBA_3;

        REG_SCSI_BUF_ADDR0 = b0;
        REG_SCSI_BUF_ADDR1 = b1;
        REG_SCSI_BUF_ADDR2 = b2;
        REG_SCSI_BUF_ADDR3 = b3;
    }

    /* Read 0x053F-0x0542 and write to CE75 */
    {
        uint8_t b0 = G_SCSI_BUF_LEN_0;
        REG_SCSI_BUF_LEN_LO = b0;
    }

    /* Read 0x053D and write to CE70 */
    REG_SCSI_TRANSFER_CTRL = G_SCSI_CMD_TYPE;

    /* Check 0x054F - if non-zero, call helper with CEF9 */
    if (G_SCSI_MODE_FLAG != 0) {
        /* Would call 0x3133 with dptr=0xCEF9 */
        /* Simplified: just set CEF9 to some value */
    }

    /* Clear CE72 */
    REG_SCSI_TRANSFER_MODE = 0;

    /* Clear bits in CE83 */
    {
        uint8_t ce83_val = REG_SCSI_CMD_FLAGS;
        ce83_val &= 0xEF;  /* Clear bit 4 */
        REG_SCSI_CMD_FLAGS = ce83_val;

        ce83_val = REG_SCSI_CMD_FLAGS;
        ce83_val &= 0xDF;  /* Clear bit 5 */
        REG_SCSI_CMD_FLAGS = ce83_val;

        ce83_val = REG_SCSI_CMD_FLAGS;
        ce83_val &= 0xBF;  /* Clear bit 6 */
        REG_SCSI_CMD_FLAGS = ce83_val;
    }

    /* Continue with more register setup... */
    /* Additional setup would continue here based on full disassembly */
}

/*
 * FUN_CODE_23f7 - Complex state helper / Log entry processor
 * Address: 0x23f7-0x27xx (~893 bytes)
 *
 * This is a major state machine handler that processes log entries
 * and manages system state transitions. It's called from multiple
 * places in the firmware to handle state changes.
 *
 * Parameters:
 *   param: Index or state code (typically 6 or 9)
 *
 * Key operations:
 *   - Stores param to 0x0AA2
 *   - Calls various transfer and DMA helpers
 *   - Manages state based on value at 0x0AA2/0x0AA3
 *   - Handles different modes (1, 2, 5, 6, 9) differently
 */
void FUN_CODE_23f7(uint8_t param)
{
    uint8_t state_val;
    uint8_t temp;

    /* Store param to 0x0AA2 and call helper 0x1659 */
    G_STATE_PARAM_0AA2 = param;

    /* Read result and store to 0x0AA3, then process */
    /* This calls dma_complex_transfer internally */

    /* Read 0x0AA3 and OR with 0x80, write to DMA ctrl register */
    state_val = G_STATE_RESULT_0AA3;
    state_val |= 0x80;
    REG_DMA_CTRL = state_val;  /* DMA ctrl register */

    /* Read 0x0AA2 and check state */
    state_val = G_STATE_PARAM_0AA2;

    /* Main state dispatch based on state_val */
    if (state_val == 0x06) {
        /* State 6: Check 0x0574 */
        if (G_LOG_PROCESS_STATE == 0) {
            /* Call 0x15dc and process result */
        }
        /* Call 0x17a9 and 0x1d43 */
        /* Then read 0x0574 again and continue... */

    } else if (state_val == 0x05) {
        /* State 5: Read 0x0464 and branch on value */
        temp = G_SYS_STATUS_PRIMARY;
        if (temp == 0x01) {
            /* Setup with R5=0x92, R4=0x00, R6=0x80, R7=0x00 */
        } else {
            /* Setup with R5=0x82, R4=0x00 */
        }
        /* Call 0x14e5 and 0x17fd */

    } else if (state_val == 0x01) {
        /* State 1: Similar to state 5 with different params */
        temp = G_SYS_STATUS_PRIMARY;
        if (temp == 0x01) {
            /* R5=0x92, R4=0x00 */
        } else {
            /* R5=0x82, R4=0x00 */
        }
        /* Call helpers and process */

    } else if (state_val == 0x09) {
        /* State 9: Check 0x0574 for sub-states */
        temp = G_LOG_PROCESS_STATE;
        if (temp == 0x01 || temp == 0x02 || temp == 0x07 || temp == 0x08) {
            /* Read 0x0575 and continue with table lookup */
        } else {
            /* Return 0xFF (error) */
            return;
        }

    } else if (state_val == 0x02) {
        /* State 2: Similar processing */
        /* ... */
    }

    /* Most paths end by jumping to common exit at 0x25fa or 0x25fd */
    /* The function is very complex with many branches */
}

/*
 * FUN_CODE_2814 - Queue processing state machine
 * Address: 0x2814-0x29B0 (~412 bytes)
 *
 * This function handles queue processing for NVMe commands.
 * It checks action code at 0x0A83 and manages queue state.
 *
 * Parameters:
 *   param_1 (R4): DMA load parameter 1
 *   param_2 (R5): DMA load parameter 2
 *   param_3 (R7): Action code
 *
 * Returns:
 *   R7: Result code (0x05, 0x0B, 0x0C, etc.)
 */
extern uint8_t FUN_CODE_1c9f(uint8_t param);
extern uint8_t FUN_CODE_11a2(uint8_t param);
extern void nvme_clear_status_bit1(void);
extern uint8_t nvme_get_data_ctrl_upper(void);
extern void FUN_CODE_1b07(void);
extern uint8_t usb_calc_addr_009f(void);
extern void nvme_calc_addr_012b(uint8_t param);
extern uint8_t nvme_get_dev_status_upper(void);
extern uint8_t nvme_get_cmd_param_upper(void);
extern void FUN_CODE_5359(uint8_t param);

uint8_t FUN_CODE_2814(uint8_t param_1, uint8_t param_2, uint8_t action_code)
{
    uint8_t result;
    uint8_t temp;

    /* Store action code to 0x0A83 */
    G_ACTION_CODE_0A83 = action_code;

    /* Call helper_3f4a to check if we can proceed */
    result = helper_3f4a();
    I_WORK_3A = result;

    if (result != 0) {
        /* Non-zero result - check for special cases */
        if (result == 0x05 && G_TRANSFER_ACTIVE == 0x01) {
            /* Set bit 6 of flags at 0x0052 */
            G_SYS_FLAGS_0052 |= 0x40;
        }
        return result;
    }

    /* Result is 0 - proceed with queue processing */
    temp = G_ACTION_CODE_0A83;
    result = FUN_CODE_1c9f(temp);

    if (result == 0) {
        /* FUN_CODE_1c9f returned 0 - check transfer active */
        if (G_TRANSFER_ACTIVE != 0) {
            G_SYS_FLAGS_0052 |= 0x40;
            return 0x05;
        }
        return 0x0C;
    }

    /* FUN_CODE_1c9f returned non-zero - configure DMA */
    G_DMA_LOAD_PARAM1 = param_1;
    G_DMA_LOAD_PARAM2 = param_2;

    /* Set up command parameters at 0x0470 */
    {
        __xdata uint8_t *cmd_ptr = (__xdata uint8_t *)0x0470;
        *cmd_ptr = 0x0A;  /* Command type */
    }

    /* Call transfer helper */
    FUN_CODE_11a2(0);

    if (result == 0) {
        /* Transfer not ready */
        if (G_TRANSFER_ACTIVE != 0) {
            G_SYS_FLAGS_0052 |= 0x40;
            return 0x05;
        }
        return 0x0C;
    }

    /* Configure NVMe controller based on action code bit 1 */
    I_WORK_3E = I_QUEUE_IDX;
    FUN_CODE_1b3f(0);
    I_WORK_3C = I_WORK_3E;  /* Store endpoint offset */

    nvme_clear_status_bit1();

    /* Update NVMe control register based on action code bit 1 */
    temp = G_ACTION_CODE_0A83;
    {
        uint8_t ctrl_val = REG_NVME_CTRL_STATUS;
        if ((temp & 0x02) == 0) {
            ctrl_val = (ctrl_val & 0xFE) | 0x01;  /* Set bit 0 */
        } else {
            ctrl_val = ctrl_val & 0xFE;  /* Clear bit 0 */
        }
        REG_NVME_CTRL_STATUS = ctrl_val;
    }

    /* Clear queue config bits 0-1 */
    REG_NVME_QUEUE_CFG &= 0xFC;

    /* Copy IDATA[0x16:0x17] to NVMe count registers */
    REG_NVME_COUNT_HIGH = I_CORE_STATE_H;
    REG_NVME_ERROR = I_CORE_STATE_L;

    /* Update NVMe config with endpoint offset */
    {
        uint8_t cfg_val = REG_NVME_CONFIG;
        cfg_val = (cfg_val & 0xC0) | (I_WORK_3E & 0x3F);
        REG_NVME_CONFIG = cfg_val;
    }

    /* Copy USB address to NVMe command registers */
    REG_NVME_CMD = G_USB_ADDR_HI_0056;
    REG_NVME_CMD_OPCODE = G_USB_ADDR_LO_0057;

    /* Combine state helpers */
    temp = G_STATE_HELPER_41;
    temp |= nvme_get_data_ctrl_upper();
    G_STATE_HELPER_41 = temp;

    /* Clear NVMe control status bit 1 */
    REG_NVME_CTRL_STATUS &= 0xFD;

    /* Check action code bit 1 for special processing */
    if ((G_ACTION_CODE_0A83 & 0x02) == 0) {
        /* Mode without bit 1 - additional DMA setup */
        FUN_CODE_1b07();
        temp = usb_calc_addr_009f();
        /* Additional processing... */
    }

    /* Call queue helper */
    FUN_CODE_5359(0x01);
    I_WORK_3B = result;

    /* Continue processing... */
    return 0x0B;
}

/*
 * FUN_CODE_2a10 - NVMe command dispatch state machine
 * Address: 0x2a10-0x2be9 (~473 bytes)
 *
 * Main NVMe command dispatch loop that processes queued commands.
 * Checks queue status and dispatches to appropriate handlers.
 */
extern void interface_ready_check(uint8_t p1, uint8_t p2, uint8_t p3);
extern uint8_t FUN_CODE_5046(void);
extern uint8_t FUN_CODE_5043(uint8_t param);
extern void FUN_CODE_505d(uint8_t param);
extern void FUN_CODE_5038(void);
extern void FUN_CODE_504f(void);
extern void FUN_CODE_0511(uint8_t p1, uint8_t p2, uint8_t p3);
extern void FUN_CODE_050c(uint8_t param);
extern void dma_queue_state_handler(void);
extern void usb_get_xfer_status(void);
extern void startup_init(void);

void FUN_CODE_2a10(uint8_t param_1)
{
    uint8_t queue_status;
    uint8_t cmd_entry;
    uint8_t work_val;
    uint8_t counter;

    /* Read queue status from 0xC451 and combine with DMA entry */
    queue_status = REG_NVME_QUEUE_STATUS_51 & 0x1F;
    cmd_entry = REG_DMA_ENTRY;
    REG_DMA_ENTRY = (cmd_entry & 0xE0) | queue_status;

    /* Read and store command direction end register */
    work_val = REG_CMDQ_DIR_END & 0x3F;
    G_ACTION_CODE_0A83 = work_val;

    /* Get queue entry and mask */
    cmd_entry = FUN_CODE_5046();
    G_STATE_WORK_0A85 = cmd_entry & 0x7F;

    /* Check USB status bit 0 */
    if ((REG_USB_STATUS & 0x01) == 0) {
        /* USB not ready - exit */
        return;
    }

    /* Check command status bit 1 */
    if ((REG_NVME_CMD_STATUS_50 & 0x02) == 0) {
        /* Command not ready - exit */
        return;
    }

    /* Check if queue entry is 0x74 or 0x75 (valid command codes) */
    work_val = G_STATE_WORK_0A85;
    if (work_val != 0x74 && work_val != 0x75) {
        /* Invalid command code - exit */
        return;
    }

    /* Initialize state machine flags */
    G_STATE_WORK_0B3D = 0x01;
    G_STATE_CTRL_0B3E = 0x01;

    /* Call interface ready check with timeout params */
    interface_ready_check(0x00, 0x32, 0x05);

    /* Copy system work byte to state work */
    G_STATE_WORK_0A84 = G_STATE_WORK_002D;

    /* Clear counter variables */
    G_STATE_CTRL_0B3F = 0;
    G_STATE_WORK_0A86 = 0;

    /* Main processing loop */
    work_val = G_STATE_WORK_0A84;
    while (work_val != 0x22) {  /* Loop until '"' (0x22) */
        /* Get next queue entry */
        cmd_entry = FUN_CODE_5043(work_val);
        G_STATE_WORK_0A85 = cmd_entry & 0x7F;

        if ((cmd_entry & 0x7F) == 0x60) {
            /* Command code 0x60 - special processing */
            work_val = G_STATE_WORK_0A84;
            FUN_CODE_505d(work_val);

            /* Update counter */
            counter = G_STATE_WORK_0A86;
            G_STATE_WORK_0A86 = counter + 1;

        } else if ((cmd_entry & 0x7F) == 0x74 || (cmd_entry & 0x7F) == 0x75) {
            /* Command codes 0x74/0x75 - process command */
            work_val = G_STATE_WORK_0A84;
            FUN_CODE_505d(work_val);
            FUN_CODE_5038();

            /* Update counter */
            counter = G_STATE_WORK_0A86;
            G_STATE_WORK_0A86 = counter + 1;
        }

        /* Advance to next entry */
        FUN_CODE_504f();
        work_val = G_STATE_WORK_0A85;
        G_STATE_WORK_0A84 = work_val;
    }

    /* Clear error flag */
    G_STATE_FLAG_06E6 = 0;

    /* Check if any commands were processed */
    counter = G_STATE_WORK_0A86;
    if (counter != 0) {
        /* Commands were processed - call dispatch */
        FUN_CODE_0511(0x00, 0x28, 0x03);

        /* Wait loop for completion */
        while (G_STATE_WORK_0A86 > G_STATE_CTRL_0B3F) {
            /* Check link status */
            if ((REG_CPU_LINK_CEF3 & 0x08) != 0) {
                /* Link ready - continue */
            } else {
                /* Check timer */
                if ((REG_TIMER0_CSR & 0x02) != 0) {
                    G_STATE_FLAG_06E6 = 1;
                    G_STATE_CTRL_0B3F = G_STATE_WORK_0A86;
                }
            }

            /* If no error, update link and call handler */
            if (G_STATE_FLAG_06E6 == 0) {
                REG_CPU_LINK_CEF3 = 0x08;
                dma_queue_state_handler();
            }
        }

        /* Call completion handler */
        FUN_CODE_050c(G_STATE_CTRL_0B3F - G_STATE_WORK_0A86);
    }

    /* Update USB control register */
    {
        uint8_t usb_ctrl = REG_USB_CTRL_9201;
        REG_USB_CTRL_9201 = (usb_ctrl & 0xEF) | 0x10;
    }

    usb_get_xfer_status();

    /* Clear bit in USB control */
    REG_USB_CTRL_9201 &= 0xEF;

    /* Update PCIe status register */
    {
        __xdata uint8_t *pcie_status = (__xdata uint8_t *)0xB298;
        uint8_t status = *pcie_status;
        *pcie_status = (status & 0xFB) | 0x04;
        *pcie_status = *pcie_status & 0xFB;
    }

    /* Decrement endpoint check flag */
    G_EP_CHECK_FLAG--;

    /* Store action code to dispatch offset */
    G_EP_DISPATCH_OFFSET = G_ACTION_CODE_0A83;

    /* Call startup init */
    startup_init();

    /* Update NVMe status registers */
    work_val = G_ACTION_CODE_0A83;
    {
        __xdata uint8_t *nvme_status = (__xdata uint8_t *)0xC488;
        *nvme_status = work_val;
    }
    {
        __xdata uint8_t *nvme_status2 = (__xdata uint8_t *)0xC4E9;
        *nvme_status2 = work_val;
    }

    /* Clear init flag and SCSI DMA param */
    G_LOG_INIT_044D = 0;
    REG_SCSI_DMA_PARAM0 = 0;
}

/*
 * FUN_CODE_2f67 - SCSI DMA queue parameter setup
 * Address: 0x2F67-0x2F7F
 *
 * Sets up SCSI DMA parameters and advances the queue index.
 * Called from FUN_CODE_2db7 to prepare DMA transfers.
 */
void FUN_CODE_2f67(uint8_t param_1)
{
    /* Combine I_WORK_3A with parameter and store to CE01 */
    REG_SCSI_DMA_PARAM = I_WORK_3A | param_1;

    /* Set DMA control to mode 3 */
    REG_SCSI_DMA_CTRL = 0x03;

    /* Increment and mask queue index (5-bit wrap) */
    I_WORK_3A = (I_WORK_3A + 1) & 0x1F;

    /* Call power status check with new index */
    power_check_status(I_WORK_3A);
}

/*
 * FUN_CODE_2db7 - SCSI DMA transfer state machine
 * Address: 0x2DB7-0x2F66
 *
 * Handles SCSI DMA transfers based on transfer ready status.
 * Manages queue state and coordinates with NVMe subsystem.
 */
void FUN_CODE_2db7(void)
{
    uint8_t ready_status;
    uint8_t status_6c;
    uint8_t bit_flag;

    /* Clear transfer state flag */
    G_XFER_STATE_0AF6 = 0;

    /* Copy endpoint index from IDATA 0x0D to I_WORK_3C */
    I_WORK_3C = I_QUEUE_IDX;

    /* Read transfer ready status and extract bit 2 */
    ready_status = REG_XFER_READY;
    bit_flag = (ready_status >> 2) & 0x01;

    /* Read status CE6C and check bit 7 */
    status_6c = REG_XFER_STATUS_CE6C;

    if (status_6c & 0x80) {
        /* Bit 7 set - transfer ready path */
        uint8_t tag_val;
        uint8_t work_val;

        /* Read tag from CE3A and store to I_WORK_3B */
        tag_val = REG_SCSI_DMA_TAG_CE3A;
        I_WORK_3B = tag_val;

        /* Write tag to DMA status register (CE6E) */
        REG_SCSI_DMA_STATUS = tag_val;

        if (bit_flag) {
            /* Bit 2 of CE89 is set - NVMe address calculation path */
            REG_SCSI_DMA_CTRL = 0x01;

            /* Calculate address offset: 0x94 + I_WORK_3B */
            nvme_calc_addr_01xx(0x94 + I_WORK_3B);

            /* Clear DMA control */
            REG_SCSI_DMA_CTRL = 0;

            /* Set flag at 0x07EA */
            G_XFER_FLAG_07EA = 1;

            /* Clear counter at computed XDATA offset (0x0171 + I_WORK_3C) */
            {
                __xdata uint8_t *counter_ptr;
                uint16_t addr = 0x0071 + I_WORK_3C;
                if (addr >= 0x0100) {
                    counter_ptr = (__xdata uint8_t *)addr;
                } else {
                    counter_ptr = (__xdata uint8_t *)(0x0100 + addr);
                }
                *counter_ptr = 0;
            }
        } else {
            /* Bit 2 not set - status primary path */
            uint8_t saved_status;
            uint8_t param;

            saved_status = G_SYS_STATUS_PRIMARY;
            FUN_CODE_1bec();
            I_WORK_3A = G_SYS_STATUS_PRIMARY;

            /* Calculate parameter based on primary status */
            param = 0;
            if (saved_status == 0x01) {
                param = 0x40;
            }

            /* Call queue parameter setup */
            FUN_CODE_2f67(param);

            nvme_get_config_offset();
            G_SYS_STATUS_PRIMARY = I_WORK_3A;
            G_STATE_FLAG_06E6 = 1;
        }

        /* Store I_WORK_3B at computed XDATA offset (0x0059 + I_WORK_3C) */
        {
            __xdata uint8_t *ptr1 = (__xdata uint8_t *)(0x0059 + I_WORK_3C);
            *ptr1 = I_WORK_3B;
        }

        /* Set flag at computed XDATA offset (0x007C + I_WORK_3C) */
        {
            __xdata uint8_t *ptr2 = (__xdata uint8_t *)(0x007C + I_WORK_3C);
            *ptr2 = 1;
        }

        /* Set flag at computed XDATA offset (0x009F + I_WORK_3C) */
        {
            __xdata uint8_t *ptr3 = (__xdata uint8_t *)(0x009F + I_WORK_3C);
            *ptr3 = 1;
        }

        /* Set G_NVME_QUEUE_READY and determine final value */
        G_NVME_QUEUE_READY = 1;
        work_val = 0x60;
        if (bit_flag) {
            work_val = 0x74;
        }

        /* Call helper 0x1B30 with I_WORK_3C + 8 and store result */
        FUN_CODE_1b30(I_WORK_3C + 8);
        G_NVME_QUEUE_READY = work_val;

        /* Calculate IDATA offset and update endpoint index */
        nvme_calc_idata_offset();
        I_QUEUE_IDX = G_NVME_QUEUE_READY;
    } else {
        /* Bit 7 not set - check/setup path */
        uint8_t check_result;

        check_result = FUN_CODE_11a2(0x01);

        if (!check_result) {
            /* Check failed - set log flag and compare counters */
            uint8_t count_9f;
            uint8_t count_71;

            G_LOG_INIT_044D = 1;

            /* Read counter from 0x009F + I_WORK_3C */
            count_9f = FUN_CODE_1b8d(0x9F + I_WORK_3C);

            /* Read counter from 0x0071 + I_WORK_3C */
            count_71 = FUN_CODE_1b0b(0x71 + I_WORK_3C);

            if (count_71 < count_9f) {
                /* count_71 < count_9f - set high bit */
                FUN_CODE_1b30(I_WORK_3C + 8);
                {
                    __xdata uint8_t *flag_ptr = (__xdata uint8_t *)0x044D;
                    *flag_ptr = *flag_ptr | 0x80;
                }
            } else {
                /* count_71 >= count_9f - set to 0xC3 */
                FUN_CODE_1b30(I_WORK_3C + 8);
                {
                    __xdata uint8_t *flag_ptr = (__xdata uint8_t *)0x044D;
                    *flag_ptr = 0xC3;
                }
            }
            return;
        }

        /* Check passed - proceed with setup */
        {
            uint8_t helper_val;
            uint8_t work_val2;
            uint8_t nvme_param;
            uint8_t counter_val;
            uint8_t new_val;

            helper_val = G_STATE_HELPER_41;
            REG_SCSI_DMA_CFG_CE36 = helper_val;

            if (!bit_flag) {
                /* Bit 2 not set - call external function and save result */
                FUN_CODE_5359(0x01);
                I_WORK_3A = 0x01;  /* Result placeholder */
            }

            /* Call helper with computed offset and read result */
            FUN_CODE_1b3f(I_WORK_3C + 0x4E);
            I_WORK_3D = REG_SCSI_DMA_CFG_CE36;

            /* Combine NVMe param with I_WORK_3D and store to CE3A */
            nvme_param = G_NVME_PARAM_053A;
            REG_SCSI_DMA_TAG_CE3A = nvme_param | I_WORK_3D;

            if (bit_flag) {
                /* Bit 2 set - NVMe address path */
                REG_SCSI_DMA_CTRL = 0x01;
                nvme_calc_addr_01xx(I_WORK_3D + 0x94);
                REG_SCSI_DMA_CTRL = G_NVME_PARAM_053A;
                G_XFER_FLAG_07EA = 1;
            } else {
                /* Bit 2 not set - queue setup path */
                uint8_t saved_status;
                uint8_t param;

                saved_status = G_SYS_STATUS_PRIMARY;
                param = 0;
                if (saved_status == 0x01) {
                    param = 0x40;
                }

                FUN_CODE_2f67(param);
                G_STATE_FLAG_06E6 = 1;
            }

            /* Set work value based on bit flag */
            work_val2 = 0x60;
            if (bit_flag) {
                work_val2 = 0x74;
            }

            /* Call helper and store work value */
            FUN_CODE_1b30(I_WORK_3C + 8);
            G_XFER_FLAG_07EA = work_val2;

            /* Read counter from 0x0071 + I_WORK_3C, decrement and store */
            counter_val = FUN_CODE_1b0b(0x71 + I_WORK_3C);
            counter_val--;
            G_XFER_FLAG_07EA = counter_val;

            if (counter_val == 0) {
                /* Counter hit zero - finalize setup */
                nvme_calc_idata_offset();
                I_QUEUE_IDX = G_XFER_FLAG_07EA;
                new_val = usb_get_ep_config_indexed();
                FUN_CODE_1c43(new_val + I_WORK_3D);
            } else {
                /* Counter not zero - update queue entry */
                new_val = usb_get_ep_config_indexed();
                new_val = (new_val + I_WORK_3D) & 0x1F;

                FUN_CODE_1b3f(I_WORK_3C + 0x4E);
                G_XFER_FLAG_07EA = new_val;

                FUN_CODE_1b3f(I_WORK_3C + 0x4E);
                if (G_XFER_FLAG_07EA == 0) {
                    nvme_add_to_global_053a();
                }

                FUN_CODE_1b30(I_WORK_3C + 8);
                G_XFER_FLAG_07EA = G_XFER_FLAG_07EA | 0x80;
            }

            /* Clear queue ready flag */
            G_NVME_QUEUE_READY = 0;

            /* Check bit 6 of CE60 and set log flag if set */
            if (REG_XFER_STATUS_CE60 & 0x40) {
                G_LOG_INIT_044D = 1;
            }
        }
    }

    /* Call transfer flag setup */
    usb_set_transfer_flag();
}

/*===========================================================================
 * BANK 1 EVENT HANDLERS
 *
 * These functions handle events and status updates. They reside in Bank 1
 * (code offset 0x10000+) and are called via the bank switching mechanism
 * (jump_bank_1 at 0x0311).
 *===========================================================================*/

/*
 * event_state_machine_e56f - Event state machine for 0x81 events
 * Bank 1 Address: 0xE56F (file offset 0x1656F)
 * Size: ~174 bytes (0x1656F-0x1661C)
 *
 * Called when events & 0x81 is set. This is a complex event state machine
 * with multiple execution paths:
 *   - Checks bit 3 of XDATA[DPTR], optionally calls 0xE6F0 with R7=1
 *   - Reads state from 0x09EF, 0x0991, 0x098E
 *   - May jump to 0xEE11 (bank 1) for further processing
 *   - Writes 0x84 to 0x097A on some paths
 *   - Uses lookup table at 0x5C9D for dispatch
 *
 * Key registers accessed:
 *   - 0x097A: State/control register (writes 0x84)
 *   - 0x09EF: Event flags
 *   - 0x0991: State variable
 *   - 0x098E: Mode indicator
 *   - 0x0214: Return value storage
 *
 * Calls to: 0xE6F0, 0xABC9, 0x43D3, 0xAA71, 0x544C, 0xAA1D,
 *           0xAA13, 0xAA4E, 0x425F
 */
void event_state_machine_e56f(void)
{
    /* Complex event state machine - stub pending full RE */
}

/*
 * event_queue_process_e762 - Process event queue entries
 * Bank 1 Address: 0xE762 (file offset 0x16762)
 *
 * Handles events by managing state counters at 0x0AA2-0x0AA5.
 * Part of the event queue management system.
 *
 * Key operations:
 *   - Reads from 0x0AA3/0x0AA2 (state counters)
 *   - Computes R6:R7 = R6:R7 + state counter
 *   - Calls helper 0xEA19 to process event
 *   - If result != 0, returns 1
 *   - Otherwise increments 0x0AA5 and loops back
 *
 * Returns: 0 if no events, 1 if event processed
 */
void event_queue_process_e762(void)
{
    uint8_t val_aa3, val_aa2;
    uint8_t count;

    /* Read state counters */
    val_aa3 = G_STATE_COUNTER_HI;
    val_aa2 = *(__xdata uint8_t *)0x0AA2;

    /* Event processing loop - simplified */
    count = *(__xdata uint8_t *)0x0AA5;
    if (count < 0x20) {
        /* Increment event counter */
        *(__xdata uint8_t *)0x0AA5 = count + 1;
    }
}

/*
 * status_update_handler_e677 - Handle status updates
 * Bank 1 Address: 0xE677 (file offset 0x16677)
 *
 * Handles status updates by checking mode and performing
 * register operations via helper functions at 0xC244, 0xC247, etc.
 *
 * Key operations based on R7 parameter:
 *   - If R7 == 4: calls 0xC244, clears A, jumps to 0x6692
 *   - Otherwise: accesses 0x09E5, calls 0xC247, calls 0x0BC8
 *   - Processes status at 0x09E8
 *   - Calls 0x0BE6 for register write
 *
 * This handler is part of the bank 1 status management system.
 */
void status_update_handler_e677(void)
{
    /* Status handler - stub pending full RE */
}

/*===========================================================================
 * Protocol State Machine Functions (0x2000-0x3FFF range)
 *===========================================================================*/

/* Forward declarations for helper functions */
extern void parse_descriptor(uint8_t param);   /* 0x04da */
extern void usb_state_setup_4c98(void);        /* 0x4c98 */
/* helper_5359 defined earlier in this file */
extern void scsi_dma_tag_setup_3212(uint8_t idx, uint16_t reg_addr); /* 0x3212 */
extern uint8_t mul_add_index_0dd1(uint8_t base, uint8_t mult);       /* 0x0dd1 */
extern void usb_set_transfer_active_flag(void); /* 0x312a */
extern void nvme_read_status(void);            /* 0x31ce */
extern void usb_helper_51ef(void);             /* 0x51ef */
extern void usb_helper_5112(void);             /* 0x5112 */

/*
 * helper_3212 - SCSI DMA tag setup
 * Address: 0x3212-0x3225 (20 bytes)
 *
 * Sets up DPTR from index calculation.
 * param_idx is loop index, param_reg is target register (0xCE8A typically).
 *
 * Original disassembly:
 *   3212: mov dpl, r7
 *   3214: mov dph, r6
 *   3216: clr a
 *   3217: subb a, 0x39      ; A = 0 - IDATA[0x39]
 *   3219: add a, 0x38       ; A = A + IDATA[0x38] = loop_idx - max_count
 *   321b: push 0x82         ; save DPL
 *   321d: push 0x83         ; save DPH
 *   321f: mov dpl, a        ; DPL = index
 *   3221: clr a
 *   3222: addc a, #0x8a     ; DPH = 0x8A (could be 0x8B if carry)
 *   3224: mov dph, a
 *   3225: ret
 */
void scsi_dma_tag_setup_3212(uint8_t idx, uint16_t reg_addr)
{
    /* Calculate negative index: idx - max */
    int8_t neg_idx = (int8_t)I_WORK_38 - (int8_t)I_WORK_39;

    /* Set up SCSI DMA index register */
    /* This sets up an index for DMA tag operations */
    (void)idx;
    (void)reg_addr;
    (void)neg_idx;
    /* The actual DMA setup is done by the hardware based on the index */
}

/*
 * helper_313a - Check if 32-bit value at IDATA[0x6B] is zero
 * Address: 0x313a-0x3146 (13 bytes)
 *
 * Calls xdata_load_dword_0db9, then loads from IDATA[0x6B] and
 * returns OR of all 4 bytes (non-zero if any byte is non-zero).
 *
 * Original disassembly:
 *   313a: lcall 0x0db9      ; xdata_load_dword
 *   313d: mov r0, #0x6b
 *   313f: lcall 0x0d78      ; Load 4 bytes from @R0
 *   3142: mov a, r4
 *   3143: orl a, r5
 *   3144: orl a, r6
 *   3145: orl a, r7
 *   3146: ret
 */
uint8_t helper_313a_check_nonzero(void)
{
    /* Read 4 bytes from IDATA[0x6B-0x6E] and check if non-zero */
    uint8_t b0 = I_TRANSFER_6B;
    uint8_t b1 = I_TRANSFER_6C;
    uint8_t b2 = I_TRANSFER_6D;
    uint8_t b3 = I_TRANSFER_6E;

    return (b0 | b1 | b2 | b3);
}

/*
 * usb_ep_loop_3419 - USB endpoint processing loop
 * Address: 0x3419-0x3577 (351 bytes)
 *
 * Main USB endpoint processing function called from main_loop when
 * REG_USB_STATUS bit 0 is NOT set.
 *
 * Algorithm:
 * 1. If G_USB_STATE_0B41 != 0, call parse_descriptor(1)
 * 2. If I_STATE_6A != 0, skip to continuation at 0x3577
 * 3. Clear state variables: 0x0B01, 0x053B, 0x00C2, 0x0517, 0x014E, 0x00E5
 * 4. Clear REG_XFER_CTRL_CE88
 * 5. Wait for REG_XFER_READY bit 0 to be set
 * 6. Check abort conditions (bit 1 of CE89, bit 4 of CE86, AF8 != 1)
 * 7. Call usb_state_setup_4c98
 * 8. Read REG_SCSI_TAG_VALUE and set up tag loop
 * 9. Loop through tags, setting up SCSI DMA for each
 * 10. Set final state flags and return
 */
void usb_ep_loop_3419(void)
{
    uint8_t val;
    uint8_t tag_val;
    uint8_t max_count;
    uint8_t loop_idx;
    uint8_t usb_param;
    uint8_t config_val;
    uint8_t param_save;

    /* Step 1: Check G_USB_STATE_0B41 and call parse_descriptor if set */
    val = G_USB_STATE_0B41;
    param_save = 0;
    if (val != 0) {
        param_save = 1;
        parse_descriptor(0x01);
    }

    /* Step 2: Check I_STATE_6A */
    val = I_STATE_6A;
    if (val != 0) {
        /* Skip to continuation at 0x3577 - just return */
        return;
    }

    /* Step 3: Clear state variables */
    G_USB_INIT_0B01 = 0;
    G_NVME_STATE_053B = 0;
    G_INIT_STATE_00C2 = 0;
    G_EP_INIT_0517 = 0;
    G_USB_INDEX_COUNTER = 0;   /* 0x014E */
    G_INIT_STATE_00E5 = 0;

    /* Step 4: Clear REG_XFER_CTRL_CE88 */
    REG_XFER_CTRL_CE88 = 0;

    /* Step 5: Wait for REG_XFER_READY bit 0 */
    do {
        val = REG_XFER_READY;
    } while ((val & XFER_READY_BIT) == 0);

    /* Step 6a: Check bit 1 of CE89 for abort */
    val = REG_XFER_READY;
    if (val & XFER_READY_DONE) {
        goto abort_path;
    }

    /* Step 6b: Check bit 4 of CE86 */
    val = REG_XFER_STATUS_CE86;
    if (val & 0x10) {
        goto abort_path;
    }

    /* Step 6c: Check if G_POWER_INIT_FLAG == 1 */
    val = G_POWER_INIT_FLAG;
    if (val != 0x01) {
        goto abort_path;
    }

    /* Step 7: Call usb_state_setup_4c98 */
    usb_state_setup_4c98();

    /* Step 8: Set up tag loop */
    G_NVME_PARAM_053A = 0;

    /* Read tag value from CE55, copy to 009F */
    tag_val = REG_SCSI_TAG_VALUE;
    G_USB_WORK_009F = tag_val;

    /* Call helper_5359 */
    usb_param = G_USB_WORK_009F;
    helper_5359(usb_param);

    /* Store result to G_USB_PARAM_0B00 and 0x012B */
    G_USB_PARAM_0B00 = usb_param;
    *(__xdata uint8_t *)0x012B = usb_param;

    /* Compare with 0x0AFF and determine loop count */
    val = G_XFER_RETRY_CNT;  /* 0x0AFF */
    usb_param = G_USB_WORK_009F;

    if (usb_param >= val + 1) {
        /* usb_param > val: set loop to val, calculate excess */
        I_WORK_39 = val;
        G_SCSI_CTRL = usb_param - val;  /* 0x0171 */
        G_NVME_PARAM_053A = 0x20;
    } else {
        /* usb_param <= val: use usb_param as loop count */
        I_WORK_39 = usb_param;
        G_SCSI_CTRL = 0;
    }

    /* Clear REG_SCSI_DMA_CFG_CE36 and loop index */
    REG_SCSI_DMA_CFG_CE36 = 0;
    I_WORK_38 = 0;

    /* Step 9: Tag loop */
    max_count = I_WORK_39;
    for (loop_idx = 0; loop_idx < max_count; loop_idx++) {
        I_WORK_38 = loop_idx;

        /* Call scsi_dma_tag_setup at 0x3212 with DPTR=0xCE8A */
        scsi_dma_tag_setup_3212(loop_idx, 0xCE8A);

        /* Read G_USB_PARAM_0B00, combine with CE01 upper bits */
        usb_param = G_USB_PARAM_0B00;
        val = REG_SCSI_DMA_PARAM;  /* CE01 */
        REG_SCSI_DMA_PARAM = usb_param | (val & 0xC0);

        /* Calculate config from G_SYS_STATUS_SECONDARY * 0x14 */
        config_val = G_SYS_STATUS_SECONDARY;
        /* mul_add_index: calculate index * 0x14 + offset */
        config_val = G_EP_CONFIG_ARRAY;  /* 0x054E - read array entry */

        /* Store loop index * config value to CE3A */
        I_WORK_3A = loop_idx * config_val;
        REG_SCSI_DMA_TAG_CE3A = I_WORK_3A;

        /* Trigger SCSI DMA command */
        REG_SCSI_DMA_CTRL = 0x03;

        /* Wait for completion (CE00 == 0) */
        do {
            val = REG_SCSI_DMA_CTRL;
        } while (val != 0);

        /* Increment USB param (5-bit counter) */
        usb_param = G_USB_PARAM_0B00;
        G_USB_PARAM_0B00 = (usb_param + 1) & 0x1F;

        /* Check if CE89 bit 2 is clear, then call power_check_status */
        val = REG_XFER_READY;
        if ((val & 0x04) == 0) {
            usb_param = G_USB_PARAM_0B00;
            power_check_status(usb_param);
        }
    }

    /* Step 10: Set final state flags */
    G_USB_INIT_0B01 = 1;
    G_SYS_FLAGS_07E8 = 1;
    G_USB_TRANSFER_FLAG = 1;  /* 0x0B2E */
    G_STATE_FLAG_06E6 = 1;

    G_INTERFACE_READY_0B2F = 0;  /* 0x0B2F */
    G_IO_CMD_TYPE = 0;           /* 0x0001 */

    /* Call helper_3147 - copy USB status to buffer */
    helper_3147();

    /* Clear buffer transfer start */
    G_BUF_XFER_START = 0;  /* 0xD80C */

    /* Clear IDATA flow control bytes */
    I_BUF_FLOW_CTRL = 0;   /* 0x6F */
    I_BUF_THRESH_LO = 0;   /* 0x70 */
    I_BUF_THRESH_HI = 0;   /* 0x71 */
    I_BUF_CTRL_GLOBAL = 0; /* 0x72 */

    /* Check CE89 bit 2 for final state */
    val = REG_XFER_READY;
    if (val & 0x04) {
        /* Bit 2 set */
        *(__xdata uint8_t *)0x0108 = 0x30;
        I_STATE_6A = 4;
        G_XFER_FLAG_07EA = 1;
    } else {
        /* Bit 2 clear */
        *(__xdata uint8_t *)0x0108 = 0x20;
        I_STATE_6A = 3;
    }
    return;

abort_path:
    /* Jump to 0x3562 - abort/error handler */
    usb_helper_51ef();

    if (param_save != 0) {
        G_SYS_FLAGS_07E8 = 1;
        usb_helper_5112();
        return;
    }

    usb_set_transfer_active_flag();
    nvme_read_status();
}

/*
 * helper_3bcd - DMA state transfer handler
 * Address: 0x3bcd-0x3cb7 (~235 bytes)
 *
 * Handles DMA state transitions and queue processing.
 * Called from protocol state machine handlers.
 *
 * Parameters:
 *   param - Control parameter stored to G_STATE_COUNTER_HI (0x0AA3)
 *
 * Returns: Value from G_STATE_COUNTER_0AA5 (0x0AA5)
 */
uint8_t helper_3bcd(uint8_t param)
{
    uint8_t val;
    uint8_t status;

    /* Store param to G_STATE_COUNTER_HI */
    G_STATE_COUNTER_HI = param;

    /* Call helper_5359 with param 1 */
    helper_5359(0x01);

    /* DMA transfer operations */
    /* (Simplified - actual implementation has complex DMA transfer logic) */

    /* Clear DMA status 2 bit 0 */
    val = REG_DMA_STATUS2;
    REG_DMA_STATUS2 = val & 0xFE;

    /* Read system status and call power check */
    val = G_SYS_STATUS_PRIMARY;
    power_check_status(val);

    /* Clear state variables */
    *(__xdata uint8_t *)0x00E2 = 0;
    *(__xdata uint8_t *)0x0105 = 0;
    *(__xdata uint8_t *)0x0128 = 0;
    *(__xdata uint8_t *)0x00BF = 1;
    *(__xdata uint8_t *)0x04D7 = 0;
    *(__xdata uint8_t *)0x04F7 = 0;
    G_STATE_COUNTER_0AA5 = 0x80;

    /* State processing loop (simplified) */
    do {
        status = G_SYS_STATUS_PRIMARY;
        if (status != 0) {
            /* Non-zero status - process completion */
            break;
        }

        val = *(__xdata uint8_t *)0x00E2;
    } while (val != 0x01);

    /* Check completion flags */
    val = *(__xdata uint8_t *)0x04D7;
    if (val == 0) {
        val = *(__xdata uint8_t *)0x04F7;
        if (val == 0) {
            G_STATE_COUNTER_0AA5 = 0;
        }
    }

    return G_STATE_COUNTER_0AA5;
}

/*
 * helper_38d4 - State machine event handler
 * Address: 0x38d4-0x38ff (~44 bytes)
 *
 * Handles state machine events and transitions.
 */
void helper_38d4(uint8_t param)
{
    uint8_t val;

    /* Check state and dispatch */
    val = G_IO_CMD_STATE;

    if (val == 0x28) {  /* '(' */
        /* Handle state 0x28 */
    } else if (val == 0x2A) {  /* '*' */
        /* Handle state 0x2A */
    }

    (void)param;
}

/*
 * helper_3cb8 - USB event handler with state dispatch
 * Address: 0x3cb8-0x3d4f (~152 bytes)
 *
 * Entry point for USB event handling, calls usb_event_handler
 * and dispatches based on command state.
 */
void helper_3cb8(uint8_t state_code)
{
    uint8_t val;

    /* Call USB event handler */
    usb_event_handler();

    /* Store state code */
    G_IO_CMD_STATE = state_code;
    G_IO_CMD_TYPE = 0;

    /* Call USB state setup */
    usb_state_setup_4c98();

    /* Check state and set flags */
    val = G_IO_CMD_STATE;
    if (val != 0) {
        G_XFER_STATE_0AF6 = 0;
    }

    /* Dispatch based on state code */
    if (val == 0x28) {  /* '(' */
        state_action_dispatch(3);
    } else if (val == 0x2A) {  /* '*' */
        state_action_dispatch(1);
    } else if (val == 0x88) {
        state_action_dispatch(2);
    } else if (val == 0x8A) {
        state_action_dispatch(0);
    }
}

/*
 * helper_3130 - Set bit 0 of USB EP0 config register
 * Address: 0x3130-0x3139 (10 bytes)
 *
 * This helper function sets bit 0 of register 0x9006 (REG_USB_EP0_CONFIG).
 * The operation is: read register, clear bit 0, set bit 0, write back.
 * Net effect is to always set bit 0 to 1.
 *
 * Original disassembly:
 *   3130: mov dptr, #0x9006
 *   3133: movx a, @dptr
 *   3134: anl a, #0xfe       ; clear bit 0
 *   3136: orl a, #0x01       ; set bit 0
 *   3138: movx @dptr, a
 *   3139: ret
 */
void helper_3130(void)
{
    uint8_t val;

    val = REG_USB_EP0_CONFIG;
    val &= 0xFE;  /* Clear bit 0 */
    val |= 0x01;  /* Set bit 0 */
    REG_USB_EP0_CONFIG = val;
}

/*
 * helper_31c5 - Calculate DPTR for 0x90xx register range
 * Address: 0x31c5-0x31cd (9 bytes)
 *
 * Sets DPTR to point to 0x9000 + offset. Used to access registers
 * in the 0x90xx range based on a calculated offset.
 *
 * Parameters:
 *   offset - Offset into the 0x9000 register range
 *
 * Returns: Pointer to __xdata at (0x9000 + offset)
 *
 * Original disassembly:
 *   31c5: mov r3, a          ; save offset
 *   31c6: clr a
 *   31c7: addc a, #0x90      ; high byte = 0x90
 *   31c9: mov 0x82, r3       ; DPL = offset
 *   31cb: mov 0x83, a        ; DPH = 0x90
 *   31cd: ret
 */
__xdata uint8_t *helper_31c5(uint8_t offset)
{
    return (__xdata uint8_t *)(0x9000 + offset);
}

/*
 * helper_3226 - Set DPTR from address bytes (low from A, high from R2)
 * Address: 0x3226-0x322d (8 bytes)
 *
 * Combines a low byte (passed in A/param) with a high byte (in R2/global)
 * to form a 16-bit address pointer.
 *
 * This is called from helper_3212 to set up register pointers.
 *
 * Parameters:
 *   addr_low - Low byte of address
 *   addr_high - High byte of address (caller sets this before call)
 *
 * Returns: Pointer to __xdata at (addr_high << 8) | addr_low
 *
 * Original disassembly:
 *   3226: mov r1, a          ; r1 = low byte
 *   3227: clr a
 *   3228: addc a, r2         ; a = r2 (high byte)
 *   3229: mov 0x82, r1       ; DPL = low byte
 *   322b: mov 0x83, a        ; DPH = high byte
 *   322d: ret
 */
__xdata uint8_t *helper_3226(uint8_t addr_low, uint8_t addr_high)
{
    return (__xdata uint8_t *)((addr_high << 8) | addr_low);
}

/*
 * nvme_call_and_signal_3219 - Call USB buffer helper and signal completion
 * Address: 0x3219-0x3222 (10 bytes)
 *
 * Calls the USB buffer setup function at 0x53c0, then writes 1 to
 * the USB signal register 0x90A1 to signal operation complete.
 *
 * Original disassembly:
 *   3219: lcall 0x53c0       ; helper_53c0
 *   321c: mov dptr, #0x90a1
 *   321f: mov a, #0x01
 *   3221: movx @dptr, a
 *   3222: ret
 */
void nvme_call_and_signal_3219(void)
{
    helper_53c0();
    REG_USB_SIGNAL_90A1 = 0x01;
}

/*
 * nvme_ep_config_init_3267 - Initialize USB endpoint configuration
 * Address: 0x3267-0x3271 (11 bytes)
 *
 * Sets up USB endpoint configuration registers:
 *   - 0x9093 = 0x02 (endpoint config 1)
 *   - 0x9094 = 0x10 (endpoint config 2)
 *
 * Original disassembly:
 *   3267: mov dptr, #0x9093
 *   326a: mov a, #0x02
 *   326c: movx @dptr, a
 *   326d: inc dptr           ; now 0x9094
 *   326e: mov a, #0x10
 *   3270: movx @dptr, a
 *   3271: ret
 */
void nvme_ep_config_init_3267(void)
{
    REG_USB_EP_CFG1 = 0x02;   /* 0x9093 */
    REG_USB_EP_CFG2 = 0x10;   /* 0x9094 */
}

/*
 * usb_link_status_read_328a - Read USB link status bits 0-1
 * Address: 0x328a-0x3290 (7 bytes)
 *
 * Reads the USB link status register at 0x9100 and returns
 * only the lower 2 bits (masked with 0x03).
 *
 * Returns: (REG_USB_LINK_STATUS & 0x03)
 *
 * Original disassembly:
 *   328a: mov dptr, #0x9100
 *   328d: movx a, @dptr
 *   328e: anl a, #0x03
 *   3290: ret
 */
uint8_t usb_link_status_read_328a(void)
{
    return REG_USB_LINK_STATUS & 0x03;
}

/*
 * queue_idx_get_3291 - Get queue index from IDATA[0x0D]
 * Address: 0x3291-0x3297 (7 bytes)
 *
 * Reads the queue index (I_QUEUE_IDX) from IDATA address 0x0D
 * and returns it in R7, with R5 cleared to 0.
 *
 * Returns: I_QUEUE_IDX (via R7 in original code)
 *
 * Original disassembly:
 *   3291: mov r0, #0x0d
 *   3293: mov a, @r0         ; read IDATA[0x0D]
 *   3294: mov r7, a
 *   3295: clr a
 *   3296: mov r5, a
 *   3297: ret
 */
uint8_t queue_idx_get_3291(void)
{
    return I_QUEUE_IDX;
}

/*
 * dma_status3_read_3298 - Read DMA status 3 upper bits
 * Address: 0x3298-0x329e (7 bytes)
 *
 * Reads the DMA status 3 register at 0xC8D9 and returns
 * only bits 3-7 (masked with 0xF8).
 *
 * Returns: (REG_DMA_STATUS3 & 0xF8)
 *
 * Original disassembly:
 *   3298: mov dptr, #0xc8d9
 *   329b: movx a, @dptr
 *   329c: anl a, #0xf8
 *   329e: ret
 */
uint8_t dma_status3_read_3298(void)
{
    return REG_DMA_STATUS3 & 0xF8;
}

/*
 * int_aux_set_bit1_3280 - Set bit 1 of auxiliary interrupt status
 * Address: 0x3280-0x3289 (10 bytes)
 *
 * Reads the auxiliary interrupt status register, clears bits 1-2,
 * sets bit 1, and writes back. Net effect is to set bit 1 and clear bit 2.
 *
 * Original disassembly:
 *   3280: mov dptr, #0xc805
 *   3283: movx a, @dptr
 *   3284: anl a, #0xf9       ; clear bits 1,2
 *   3286: orl a, #0x02       ; set bit 1
 *   3288: movx @dptr, a
 *   3289: ret
 */
void int_aux_set_bit1_3280(void)
{
    uint8_t val;

    val = REG_INT_AUX_STATUS;
    val &= 0xF9;  /* Clear bits 1,2 */
    val |= 0x02;  /* Set bit 1 */
    REG_INT_AUX_STATUS = val;
}

/*
 * xdata_read_0a7e_329f - Read 32-bit value from XDATA 0x0A7E
 * Address: 0x329f-0x32a4 (6 bytes)
 *
 * Sets up DPTR to 0x0A7E and jumps to the dword reader at 0x0DDD.
 * Returns the 32-bit value at XDATA[0x0A7E-0x0A81] in R4-R7.
 *
 * Original disassembly:
 *   329f: mov dptr, #0x0a7e
 *   32a2: ljmp 0x0ddd        ; xdata_load_dword
 */
uint32_t xdata_read_0a7e_329f(void)
{
    /* Read 32-bit value from XDATA 0x0A7E-0x0A81 */
    __xdata uint8_t *ptr = (__xdata uint8_t *)0x0A7E;
    uint32_t result;

    result = (uint32_t)ptr[0];
    result |= (uint32_t)ptr[1] << 8;
    result |= (uint32_t)ptr[2] << 16;
    result |= (uint32_t)ptr[3] << 24;

    return result;
}

