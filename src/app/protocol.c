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
    dma_status = (dma_status & ~DMA_STATUS_ERROR) | DMA_STATUS_ERROR;  /* Clear bit 3, set bit 3 */
    REG_DMA_STATUS = dma_status;

    dma_status = REG_DMA_STATUS;
    dma_status = dma_status & ~DMA_STATUS_DONE;  /* Clear bit 2 */
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
    if (!(REG_USB_STATUS & USB_STATUS_ACTIVE)) {
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
    if (*(__idata uint8_t *)0x0D == queue_idx) {
        *(__idata uint8_t *)0x0D = val_r6;
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
extern void usb_func_1c4a(uint8_t val);           /* 0x1c4a */
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
    if (*(__xdata uint8_t *)0x07EF != 0) {
        /* 0x3fda path: return 0 */
        helper_523c(0, 0x3A, 2);
        return 5;
    }

    /* 0x3f53: Call usb_func_1c5d with dptr=0x0464 */
    usb_func_1c5d((__xdata uint8_t *)0x0464);

    /* 0x3f59: Clear 0x07E5 */
    *(__xdata uint8_t *)0x07E5 = 0;

    /* 0x3f5e: Call usb_func_1c4a(0) */
    usb_func_1c4a(0);

    /* 0x3f61: Check 0x0002 */
    if (*(__xdata uint8_t *)0x0002 != 0) {
        /* 0x3f67: Clear 0x0B2F */
        G_USB_TRANSFER_FLAG = 0;
        /* Then jump to 0x3f82 */
    } else {
        /* 0x3f6e: Check 0xB480 bit 0 (PCIe link status) */
        if (!(REG_PCIE_LINK_STATUS & 0x01)) {
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
        val_06e5 = *(__xdata uint8_t *)0x06E5;
        val_044b = *(__xdata uint8_t *)0x044B;

        if (val_06e5 == val_044b) {
            /* Check 0x0AF8 */
            if (G_POWER_INIT_FLAG == 0) {
                /* Check 0xB480 bit 0 */
                if (!(REG_PCIE_LINK_STATUS & 0x01)) {
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
    if (*(__xdata uint8_t *)0x06E8 != 0) {
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
        uint8_t idx = *(__xdata uint8_t *)0x0464;
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
    if (*(__xdata uint8_t *)0x044C == 0) {
        /* Check 0x0002 */
        if (*(__xdata uint8_t *)0x0002 == 0) {
            /* Check 0x0AF6 */
            if (G_XFER_STATE_0AF6 != 0) {
                return 0x0B;
            }
        }

        /* 0x3fcc: Clear 0x044C, set R3=1 */
        *(__xdata uint8_t *)0x044C = 0;
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
    *(__xdata uint8_t *)0x0A84 = param;

    /* Read IDATA[0x16:0x17] and compare */
    /* The actual comparison logic is complex, involving subtract_16 */
    stored_param = *(__xdata uint8_t *)0x0A84;

    /* Check if param matches state at 0x0AF3 */
    state_val = *(__xdata uint8_t *)0x0AF3;

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
void helper_5359(void)
{
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
    *(__xdata uint8_t *)0x0217 = offset;
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
    *(__xdata uint8_t *)0xCE43 = 0xFF;

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
 * helper_313f - Count check helper
 * Address: 0x313f
 *
 * Checks count at IDATA[r0_val] and returns status.
 */
uint8_t helper_313f(uint8_t r0_val)
{
    (void)r0_val;
    /* TODO: Implement count check from 0x313f */
    return 0;  /* Default: count is zero */
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

