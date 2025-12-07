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
