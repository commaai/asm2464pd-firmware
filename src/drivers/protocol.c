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

#include "../types.h"
#include "../sfr.h"
#include "../registers.h"
#include "../globals.h"

/* Forward declarations */
extern void dma_clear_status(void);
extern uint8_t usb_event_handler(void);
extern void usb_reset_interface(uint8_t param);

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

/* XDATA locations for protocol state */
#define XDATA_STATE_CODE        ((__xdata uint8_t *)0x0002)
#define XDATA_FLASH_RESET       ((__xdata uint8_t *)0x0AAA)
#define XDATA_STATE_HELPER_B    ((__xdata uint8_t *)0x0AAB)
#define XDATA_STATE_COUNTER     ((__xdata uint8_t *)0x0AAC)

/* IDATA locations for core handler */
#define IDATA_CORE_STATE_L      ((__idata uint8_t *)0x16)
#define IDATA_CORE_STATE_H      ((__idata uint8_t *)0x17)
#define IDATA_WORK_0E           ((__idata uint8_t *)0x0E)
#define IDATA_STATE_6A          ((__idata uint8_t *)0x6A)

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
    state_code = *XDATA_STATE_CODE;

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
    *IDATA_STATE_6A = 0;  /* Cleared by original code at 0x4951 */
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
    *XDATA_FLASH_RESET = param;

    /* Call transfer helper to get status */
    transfer_func_16a2();

    /* Read state counter and update helper */
    state_counter = *XDATA_STATE_COUNTER;
    transfer_func_16b7(*XDATA_FLASH_RESET);
    state_helper = *XDATA_STATE_COUNTER;
    *XDATA_STATE_HELPER_B = state_helper;

    /* Update DMA status register */
    dma_status = REG_DMA_STATUS;
    dma_status = (dma_status & 0xF7) | 0x08;  /* Clear bit 3, set bit 3 */
    REG_DMA_STATUS = dma_status;

    dma_status = REG_DMA_STATUS;
    dma_status = dma_status & 0xFB;  /* Clear bit 2 */
    REG_DMA_STATUS = dma_status;

    /* Calculate address based on state counter */
    computed_val = (uint8_t)((uint16_t)state_counter * 0x10);

    /* Compute base address: 0xB800 or 0xB840 based on flash reset flag */
    if (*XDATA_FLASH_RESET != 0) {
        calc_addr = 0xB840;
    } else {
        calc_addr = 0xB800;
    }
    calc_addr += computed_val;

    /* Wait for ready */
    reg_wait_bit_clear(0x0461, 0x00, 0x01, computed_val);

    /* Check if state changed */
    state_flag = state_helper_15ac() & 0x01;
    state_helper = *XDATA_STATE_HELPER_B;

    if (state_helper != state_flag) {
        /* State changed - handle transition */
        transfer_func_17ed();
        computed_val = state_helper_15af();

        if (*XDATA_FLASH_RESET != 0) {
            computed_val += 0x04;
        }
        *IDATA_STATE_6A = computed_val;  /* Using 0x54 proxy */

        flash_func_1679();
        *XDATA_FLASH_RESET = 0x01;

        transfer_func_17ed();
        computed_val = state_helper_15af();
        computed_val = (computed_val >> 1) & 0x07;

        usb_calc_queue_addr(*IDATA_STATE_6A);
        *XDATA_FLASH_RESET = computed_val;

        /* Flash function does not return */
        flash_func_0bc8();
    }

    /* Clear DMA status and continue */
    dma_clear_status();

    /* Update state if counter changed */
    if (*XDATA_STATE_COUNTER != *XDATA_FLASH_RESET) {
        transfer_func_16a2();
        *XDATA_FLASH_RESET = *XDATA_STATE_COUNTER;
        transfer_func_16b7(*XDATA_STATE_HELPER_B);
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

    *IDATA_CORE_STATE_L = val_lo;
    *IDATA_CORE_STATE_H = val_hi;
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
    uint8_t state = *XDATA_STATE_CODE;

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
    *XDATA_FLASH_RESET = 0;
    *XDATA_STATE_HELPER_B = 0;
    *XDATA_STATE_COUNTER = 0;

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
    *(__xdata uint8_t *)0x0203 = r7;

    /* Store queue flags to 0x020D */
    *(__xdata uint8_t *)0x020D = r5;

    /* Store additional flag to 0x020E */
    *(__xdata uint8_t *)0x020E = r3;

    /* Set ready flag at 0x07E5 */
    *(__xdata uint8_t *)0x07E5 = 0x01;

    /* Check USB status bit 0 */
    if (!(REG_USB_STATUS & 0x01)) {
        /* Bit 0 not set - trigger endpoint and call helper */
        *(__xdata uint8_t *)0xD80C = 0x01;
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
    counter = *(__xdata uint8_t *)0x000A;

    if (counter > 1) {
        /* Decrement counter */
        (*(__xdata uint8_t *)0x000A)--;
    } else {
        /* Clear counter and call cleanup */
        *(__xdata uint8_t *)0x000A = 0;
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
    __idata uint8_t *src = (__idata uint8_t *)0x6F;

    /* Copy 4 bytes from IDATA[0x6F-0x72] to XDATA 0xD808-0xD80B */
    /* Note: Original reads backwards from 0x72 to 0x6F */
    *(__xdata uint8_t *)0xD808 = src[3];  /* IDATA[0x72] -> 0xD808 */
    *(__xdata uint8_t *)0xD809 = src[2];  /* IDATA[0x71] -> 0xD809 */
    *(__xdata uint8_t *)0xD80A = src[1];  /* IDATA[0x70] -> 0xD80A */
    *(__xdata uint8_t *)0xD80B = src[0];  /* IDATA[0x6F] -> 0xD80B */
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
    *(__xdata uint8_t *)0xD810 = 0;
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
static void helper_50db(void)
{
    uint8_t queue_idx;

    /* Read queue index */
    queue_idx = *(__xdata uint8_t *)0x0AF5;

    /* Only process if queue index < 0x20 */
    if (queue_idx >= 0x20) {
        return;
    }

    /* TODO: Complex queue management logic with calls to:
     * - 0x31D5, 0x31E2, 0x325F, 0x31E0
     * These need to be implemented for full functionality
     */
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
    *(__xdata uint8_t *)0x0B2E = 0;
    *(__idata uint8_t *)0x6A = 0;
    *(__xdata uint8_t *)0x06E6 = 0;

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
        *(__xdata uint8_t *)0xC8D4 = 0xA0;

        /* Copy buffer info from 0x0056-0x0057 to 0x905B-0x905C and 0xD802-0xD803 */
        r2 = *(__xdata uint8_t *)0x0056;
        r3 = *(__xdata uint8_t *)0x0057;
        *(__xdata uint8_t *)0x905B = r2;
        *(__xdata uint8_t *)0x905C = r3;
        *(__xdata uint8_t *)0xD802 = r2;
        *(__xdata uint8_t *)0xD803 = r3;
    } else {
        /* Path when r5 bits 1-2 are clear */
        *(__xdata uint8_t *)0xC8D4 = r7 | 0x80;

        /* Read and modify 0xC4ED */
        val = *(__xdata uint8_t *)0xC4ED;
        val = (val & 0xC0) | r7;
        *(__xdata uint8_t *)0xC4ED = val;

        /* Read 0xC4EE-0xC4EF and write to 0xD802-0xD803 */
        r3 = *(__xdata uint8_t *)0xC4EE;
        val = *(__xdata uint8_t *)0xC4EF;
        *(__xdata uint8_t *)0xD802 = val;
        *(__xdata uint8_t *)0xD803 = r3;
    }

    /* Clear 0xD804-0xD807 and 0xD80F */
    *(__xdata uint8_t *)0xD804 = 0;
    *(__xdata uint8_t *)0xD805 = 0;
    *(__xdata uint8_t *)0xD806 = 0;
    *(__xdata uint8_t *)0xD807 = 0;
    *(__xdata uint8_t *)0xD80F = 0;

    /* Check r5 bit 4 for extended mode */
    if (r5 & 0x10) {
        /* Extended mode - set 0xD800 = 4, copy from 0x0054 to 0xD807 */
        *(__xdata uint8_t *)0xD800 = 0x04;
        *(__xdata uint8_t *)0xD807 = *(__xdata uint8_t *)0x0054;
        /* r4 = 0x08 for final processing */
    } else {
        /* Normal mode - set 0xD800 = 3 */
        *(__xdata uint8_t *)0xD800 = 0x03;

        /* Check state at 0x07E5 */
        if (*(__xdata uint8_t *)0x07E5 == 0) {
            /* Check r5 bit 2 */
            if (r5 & 0x04) {
                /* Set 0xC8D4 = 0xA0, 0xD806 = 0x28 */
                *(__xdata uint8_t *)0xC8D4 = 0xA0;
                *(__xdata uint8_t *)0xD806 = 0x28;
            }
            /* Further processing at 0x028c-0x02c4 omitted for now */
        }
    }
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
    *(__xdata uint8_t *)0x044D = 0;

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
    *(__xdata uint8_t *)0xE65F = 0;
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
    *(__xdata uint8_t *)0xE62F = 0;
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

    /* Clear bit 3 */
    status &= 0xF7;
    REG_DMA_STATUS = status;

    /* Read again and clear bit 2 */
    status = REG_DMA_STATUS;
    status &= 0xFB;
    REG_DMA_STATUS = status;
}

