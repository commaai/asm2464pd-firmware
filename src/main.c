/*
 * ASM2464PD Firmware - Main Entry Point
 *
 * This is the main entry point for the ASM2464PD USB4/Thunderbolt NVMe controller.
 * The firmware is designed for the 8051-compatible CPU core running at ~114MHz.
 *
 * Each function corresponds to a function in the original firmware with its
 * address range documented in the comment header.
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

/*===========================================================================
 * Forward declarations
 *===========================================================================*/

/* Main loop */
void main_loop(void);

/* Initialization */
void process_init_table(void);

/* Dispatch/utility functions */
extern void jump_bank_0(uint16_t reg_addr);    /* app/dispatch.c - 0x0300-0x0310 */
extern void jump_bank_1(uint16_t reg_addr);    /* app/dispatch.c - 0x0311-0x0321 */
void reg_set_bit_0(uint16_t reg_addr);
void reg_set_bit_0_cpu_exec(void);

/* Main loop handler stubs - some moved to driver files */
extern void timer_link_status_handler(void);   /* drivers/timer.c - 0x04d0 -> 0xCE79 */
extern void phy_config_link_params(void);      /* drivers/phy.c */
void reserved_stub_handler(void);              /* 0x04b2 -> 0xE971 - Reserved/unused */
void main_polling_handler(void);               /* 0x4fb6 - Core polling and dispatch */
void usb_power_init(void);                     /* 0x0327 -> 0xB1CB - USB/power init */
extern void event_state_handler(void);         /* app/state_helpers.c - 0x0494 -> Bank1:0xE56F */
extern void error_state_config(void);          /* app/state_helpers.c - 0x0606 -> Bank1:0xB230 */
extern void phy_register_config(void);         /* drivers/phy.c - 0x0589 -> 0xD894 */
extern void flash_command_handler(void);       /* drivers/flash.c - 0x0525 -> 0xBAA0 */
extern void system_init_from_flash(void);      /* drivers/flash.c - Bank 1: 0x8d77 */
extern void usb_buffer_dispatch(void);         /* drivers/usb.c - 0x039a -> 0xD810 */
extern void system_interrupt_handler(void);    /* drivers/timer.c - 0x0520 -> 0xB4BA */
extern void pcie_nvme_event_handler(void);     /* drivers/pcie.c - 0x052f -> 0xAF5E */
extern void pcie_error_dispatch(void);         /* drivers/pcie.c - 0x0570 -> Bank1:0xE911 */
extern void pcie_event_bit5_handler(void);     /* drivers/pcie.c - 0x061a -> Bank1:0xA066 */
extern void pcie_timer_bit4_handler(void);     /* drivers/pcie.c - 0x0593 -> 0xC105 */
extern void system_timer_handler(void);        /* drivers/timer.c - 0x0642 -> 0xEF4E */

/* External functions */
extern void uart_init(void);
extern void usb_ep_dispatch_loop(void);
extern void usb_buffer_handler(void);          /* drivers/usb.c - 0x0448 */
extern void usb_get_descriptor_length(uint8_t param);  /* drivers/usb.c */
extern void usb_convert_speed(uint8_t param);  /* drivers/usb.c */
extern uint8_t nvme_build_cmd(uint8_t param);  /* drivers/nvme.c - 0x31da */
extern void handler_9d90(void);                /* app/protocol.c */
extern void handler_db09(void);                /* drivers/pcie.c */

/* Dispatch stubs from app/dispatch.c */
extern void dispatch_04b7(void);               /* 0x04b7 -> Bank0:0xE597 */
extern void dispatch_04bc(void);               /* 0x04bc -> Bank0:0xE14B */
extern void phy_power_config_handler(void);    /* 0x032c -> Bank0:0x92C5 */
extern void dispatch_0539(void);               /* 0x0539 -> Bank0:0x8D77 */
extern void dispatch_04f8(void);               /* 0x04f8 -> Bank0:0xDE16 */
extern void pcie_tunnel_setup(void);           /* 0xCD6C */
extern void dispatch_0435(void);               /* 0x0435 -> Bank0:0xD127 */
extern void handler_bf8e(void);                /* 0x0340 -> Bank0:0xBF8E */

/* External handlers */
extern void scsi_flash_ready_check(void);      /* app/scsi.c - 0x5305 */
extern void pcie_handler_unused_eef9(void);    /* drivers/pcie.c - Bank1:0xEEF9 */
extern void init_sys_flags_07f0(void);         /* 0x4be6 - inline config handler */

/*===========================================================================
 * Boot State Verification - startup_0016
 *===========================================================================*/

/*
 * Helper: Load both signature sets for comparison
 * Address: 0x1b7e-0x1b87 (10 bytes)
 *
 * Loads IDATA[0x09-0x0c] into one set and IDATA[0x6b-0x6e] into another
 * for 32-bit comparison.
 */
static void load_both_signatures(uint32_t *sig1, uint32_t *sig2)
{
    /* Load IDATA[0x09-0x0c] (boot signature) */
    *sig1 = I_BOOT_SIG_0 | ((uint32_t)I_BOOT_SIG_1 << 8) |
            ((uint32_t)I_BOOT_SIG_2 << 16) | ((uint32_t)I_BOOT_SIG_3 << 24);
    /* Load IDATA[0x6b-0x6e] (transfer/backup signature) */
    *sig2 = I_TRANSFER_6B | ((uint32_t)I_TRANSFER_6C << 8) |
            ((uint32_t)I_TRANSFER_6D << 16) | ((uint32_t)I_TRANSFER_6E << 24);
}

/*
 * Helper: Compare two 32-bit signatures
 * Address: 0x0d22-0x0d32 (17 bytes)
 *
 * Compares (r0:r1:r2:r3) - (r4:r5:r6:r7) with initial borrow.
 * Returns carry if comparison indicates condition met.
 *
 * Original: setb c means compare >= (sig2 >= sig1)
 *           clr c means compare > (sig2 > sig1)
 */
static uint8_t compare_signatures_ge(uint32_t sig1, uint32_t sig2)
{
    /* setb c before call: check sig2 >= sig1 */
    return sig2 >= sig1;
}

static uint8_t compare_signatures_gt(uint32_t sig1, uint32_t sig2)
{
    /* clr c before call: check sig2 > sig1 */
    return sig2 > sig1;
}

/*
 * startup_0016 - Boot state verification and initialization
 * Address: 0x0016-0x0103 (238 bytes)
 *
 * This is a boot state machine that checks IDATA signatures to determine
 * whether this is a warm boot (signatures match) or cold boot (need to
 * reinitialize). It also handles different boot modes based on XDATA[0x0AF3].
 *
 * Boot states written to XDATA[0x0001]:
 *   0: Normal boot, signatures zero
 *   1: Cold boot, secondary signature non-zero
 *   2: Boot mode == 5
 *   3: Signature mismatch (setb c path)
 *   4: Boot mode == 2 or 4
 *   5: Boot mode == 5 (alt path)
 *   6: Signature mismatch (clr c path)
 *   7: Signature mismatch (second compare)
 *
 * Original disassembly start:
 *   0016: clr a
 *   0017: mov dptr, #0x0001
 *   001a: movx @dptr, a        ; clear boot state
 *   001b: mov r0, #0x6b
 *   001d: lcall 0x0d78         ; load IDATA[0x6b-0x6e]
 *   ...
 */
void startup_0016(void)
{
    uint32_t sig_boot, sig_transfer;
    uint8_t boot_mode;
    uint8_t state_6a;

    /* Clear boot state */
    G_IO_CMD_TYPE = 0;

    /* Load transfer signature from IDATA[0x6b-0x6e] */
    sig_transfer = I_TRANSFER_6B | ((uint32_t)I_TRANSFER_6C << 8) |
                   ((uint32_t)I_TRANSFER_6D << 16) | ((uint32_t)I_TRANSFER_6E << 24);

    /* Check if transfer signature is zero */
    if (sig_transfer == 0) {
        /* Load boot signature from IDATA[0x09-0x0c] */
        sig_boot = I_BOOT_SIG_0 | ((uint32_t)I_BOOT_SIG_1 << 8) |
                   ((uint32_t)I_BOOT_SIG_2 << 16) | ((uint32_t)I_BOOT_SIG_3 << 24);

        if (sig_boot == 0) {
            /* Both signatures zero - normal boot */
            goto boot_dispatch;
        }
        /* Boot signature non-zero but transfer zero - set state 1 */
        G_IO_CMD_TYPE = 1;
        goto boot_dispatch;
    }

    /* Transfer signature non-zero - check boot mode */
    boot_mode = G_XFER_STATE_0AF3;

    if (boot_mode == 0x80) {
        /* Boot mode 0x80 - check I_STATE_6A */
        state_6a = I_STATE_6A;

        switch (state_6a) {
        case 1:
        case 3:
        case 8:
            /* Compare signatures */
            goto compare_signatures_mode80;

        case 2:
        case 4:
            /* Set state 4 */
            G_IO_CMD_TYPE = 4;
            goto boot_dispatch;

        case 5:
            /* Set state 2 */
            G_IO_CMD_TYPE = 2;
            goto boot_dispatch;

        default:
            goto boot_dispatch;
        }
    } else {
        /* Boot mode != 0x80 - different handling */
        state_6a = I_STATE_6A;

        switch (state_6a) {
        case 1:
        case 3:
        case 8:
            /* Jump to 0x00FE path - set state 6 if comparison fails */
            goto compare_signatures_alt;

        case 2:
        case 4:
            /* Compare signatures (at 0x00CA) */
            goto compare_signatures_mode_other;

        case 5:
            /* Set state 5 */
            G_IO_CMD_TYPE = 5;
            goto boot_dispatch;

        default:
            goto boot_dispatch;
        }
    }

compare_signatures_mode80:
    /* Compare IDATA[0x6b-0x6e] with IDATA[0x09-0x0c] byte by byte */
    if (I_TRANSFER_6B == I_BOOT_SIG_0 &&
        I_TRANSFER_6C == I_BOOT_SIG_1 &&
        I_TRANSFER_6D == I_BOOT_SIG_2 &&
        I_TRANSFER_6E == I_BOOT_SIG_3) {
        /* Signatures match - normal boot */
        goto boot_dispatch;
    }

    /* Signatures mismatch - load both and compare with borrow */
    load_both_signatures(&sig_boot, &sig_transfer);

    /* First comparison: setb c (check sig_transfer >= sig_boot) */
    if (!compare_signatures_ge(sig_boot, sig_transfer)) {
        /* Condition not met - set state 3 */
        G_IO_CMD_TYPE = 3;
        goto boot_dispatch;
    }

    /* Second comparison: clr c (check sig_transfer > sig_boot) */
    load_both_signatures(&sig_boot, &sig_transfer);
    if (!compare_signatures_gt(sig_boot, sig_transfer)) {
        /* Condition not met - goto state 4 path */
        goto boot_dispatch;
    }

    /* Fall through to state 4 */
    G_IO_CMD_TYPE = 4;
    goto boot_dispatch;

compare_signatures_alt:
    /* At 0x00FE: Set state 6 and dispatch */
    G_IO_CMD_TYPE = 6;
    goto boot_dispatch;

compare_signatures_mode_other:
    /* Compare IDATA[0x6b-0x6e] with IDATA[0x09-0x0c] byte by byte */
    if (I_TRANSFER_6B == I_BOOT_SIG_0 &&
        I_TRANSFER_6C == I_BOOT_SIG_1 &&
        I_TRANSFER_6D == I_BOOT_SIG_2 &&
        I_TRANSFER_6E == I_BOOT_SIG_3) {
        /* Signatures match */
        goto boot_dispatch;
    }

    /* Mismatch - load both and compare */
    load_both_signatures(&sig_boot, &sig_transfer);

    /* First comparison: setb c */
    if (!compare_signatures_ge(sig_boot, sig_transfer)) {
        /* Set state 7 */
        G_IO_CMD_TYPE = 7;
        goto boot_dispatch;
    }

    /* Second comparison: clr c */
    load_both_signatures(&sig_boot, &sig_transfer);
    if (!compare_signatures_gt(sig_boot, sig_transfer)) {
        goto boot_dispatch;
    }

    /* Set state 6 */
    G_IO_CMD_TYPE = 6;
    /* Fall through to dispatch */

boot_dispatch:
    /* At 0x0104: Read boot state and call table dispatch
     * The table dispatch at 0x0DEF uses boot state to index into
     * a jump table for further initialization.
     * For now, this is a stub - the actual dispatch is complex.
     */
    ;
}

/*===========================================================================
 * Initialization Data Table Processor
 *===========================================================================*/

/*
 * write_xdata_reg - Write value to XDATA register
 * Address: 0x0be6 (helper function)
 *
 * Writes the value in A to the XDATA address specified by R3:R2 (high:low).
 * Uses R1 as loop counter for sequential writes.
 *
 * Original disassembly:
 *   0be6: push 0x82        ; push DPL
 *   0be8: push 0x83        ; push DPH
 *   0bea: mov dph, r3
 *   0bec: mov dpl, r2
 *   0bee: movx @dptr, a    ; write to XDATA
 *   0bef: pop 0x83
 *   0bf1: pop 0x82
 *   0bf3: ret
 */
static void write_xdata_reg(uint8_t addr_h, uint8_t addr_l, uint8_t value)
{
    uint16_t addr = ((uint16_t)addr_h << 8) | addr_l;
    XDATA8(addr) = value;
}

/*
 * process_init_table - Process initialization data table
 * Address: 0x4352-0x43d1 (128 bytes)
 *
 * Processes a compressed initialization table at code address 0x0648.
 * The table contains register addresses and values to initialize hardware.
 *
 * Table format:
 *   Byte 0: Command byte
 *     - If 0x00: End of table, exit
 *     - Bits 7:6 = Type: 0xE0=write XDATA, 0x40/0x80/0xC0=other operations
 *     - Bits 5:0 = Count or flags
 *
 *   For type 0xE0 (write to XDATA):
 *     Next 3 bytes: addr_high, addr_low, count
 *     Following bytes: values to write sequentially
 *
 *   For other types:
 *     Handle IDATA bit operations
 *
 * Original disassembly:
 *   4352: mov dptr, #0x0648    ; point to init table
 *   4355: clr a
 *   4356: mov r6, #0x01
 *   4358: movc a, @a+dptr      ; read command byte
 *   4359: jz 0x4329            ; if zero, jump to main_loop
 *   435b: inc dptr
 *   435c: mov r7, a            ; save command
 *   435d: anl a, #0x3f         ; get count/flags
 *   ...
 */
void process_init_table(void)
{
    __code uint8_t *table_ptr = (__code uint8_t *)0x0648;
    uint8_t cmd;
    uint8_t count;
    uint8_t addr_h, addr_l;
    uint8_t type;
    uint8_t r6;  /* outer loop counter */

    while (1) {
        r6 = 1;

        /* Read command byte */
        cmd = *table_ptr;

        /* End of table? */
        if (cmd == 0x00) {
            return;
        }

        table_ptr++;

        /* Extract type (bits 7:6) and flags (bits 5:0) */
        type = cmd & 0xE0;
        count = cmd & 0x3F;

        /* Check if bit 5 is set - extended count follows */
        if (cmd & 0x20) {
            count = cmd & 0x1F;  /* Use only bits 4:0 */
            r6 = *table_ptr;
            table_ptr++;
            if (r6 != 0) {
                r6++;  /* Increment if non-zero */
            }
        }

        /* Process based on type */
        if (type == 0xE0) {
            /* Type 0xE0: Write values to sequential XDATA addresses */
            /* Read address high and low */
            addr_h = *table_ptr++;
            addr_l = *table_ptr++;
            count = *table_ptr++;  /* Count of bytes to write */

            /* Write loop */
            do {
                do {
                    uint8_t value = *table_ptr++;
                    write_xdata_reg(addr_h, addr_l, value);

                    /* Increment address */
                    addr_l++;
                    if (addr_l == 0) {
                        addr_h++;
                    }

                    count--;
                } while (count != 0);

                r6--;
            } while (r6 != 0);
        } else if (type == 0x00) {
            /* Type 0x00: Bit operations on IDATA */
            /* Read address from table */
            uint8_t idata_addr = *table_ptr++;
            uint8_t mask_index = (cmd & 0x07) + 0x0C;  /* Calculate mask table offset */

            /* Read mask from a lookup table embedded in code at 0x433e */
            /* Simplified: just skip this complex bit manipulation for now */
            /* The original uses: movc a, @a+pc at address 0x433d */

            /* Original: reads idata[addr], applies mask, writes back */
            /* This involves either ORing or ANDing based on carry flag */
            count--;
            while (count != 0) {
                table_ptr++;
                count--;
            }
        } else {
            /* Other types (0x40, 0x80, 0xC0) */
            /* Read address */
            addr_h = *table_ptr++;
            addr_l = *table_ptr++;

            /* For 0x40/0x80: direct register operations */
            /* Skip the data bytes */
            while (count != 0) {
                table_ptr++;
                count--;
            }
        }
    }
}

/*===========================================================================
 * Main Entry Point
 *===========================================================================*/

/*
 * Main entry point
 * Address: 0x431a-0x43d2 (184 bytes)
 *
 * Called from reset vector at address 0x0000
 *
 * The firmware entry performs:
 * 1. Clear all internal RAM (256 bytes)
 * 2. Initialize stack pointer to 0x72
 * 3. Call cmd_dispatch (0x0300 via 0x030a)
 * 4. Process initialization data table at 0x0648
 * 5. Enter main processing loop at 0x2f80
 *
 * Original disassembly:
 *   431a: mov r0, #0xff
 *   431c: clr a
 *   431d: mov @r0, a
 *   431e: djnz r0, 0x431d
 *   4320: mov SP, #0x72
 *   4323: lcall 0x030a
 *   4326: ljmp 0x4352   ; continue initialization
 */
void main(void)
{
    __asm
        ; Clear all internal RAM (0x00-0xFF)
        mov     r0, #0xFF
        clr     a
    _clear_ram:
        mov     @r0, a
        djnz    r0, _clear_ram

        ; Initialize stack pointer (leaves room for 256-0x72 = 142 bytes of stack)
        mov     SP, #0x72
    __endasm;

    /* Call initialization dispatcher at 0x030a */
    /* This sets up DPX register and dispatches based on parameter */
    DPX = 0x00;

    /* Process initialization data table at 0x0648 */
    process_init_table();

    /* Basic system initialization */
    G_SYSTEM_CTRL = 0x33;

    /* Initialize USB endpoint configuration */
    G_EP_CONFIG_BASE = 0x20;
    G_EP_CONFIG_ARRAY = 0x04;
    G_EP_CONFIG_05A8 = 0x02;
    G_EP_CONFIG_05F8 = 0x04;

    /* Initialize system flags */
    G_SYS_FLAGS_07EC = 0x01;
    G_SYS_FLAGS_07ED = 0x00;
    G_SYS_FLAGS_07EE = 0x00;
    G_SYS_FLAGS_07EF = 0x00;

    /* Initialize NVMe */
    REG_NVME_LBA_LOW = 0x02;

    /* Enter main processing loop (never returns) */
    main_loop();
}

/*===========================================================================
 * Main Processing Loop
 *===========================================================================*/

/* External dispatch functions from dispatch.c */
extern void dispatch_0322(void);      /* 0x0322 -> 0xCA0D */
extern void dispatch_04e9(void);      /* 0x04e9 -> 0xE8E4 (handler_e8e4) */
extern void dispatch_0516(void);      /* 0x0516 -> 0xE30E */
extern void dispatch_0430(void);      /* 0x0430 -> 0x9037 */
extern void dispatch_045d(void);      /* 0x045d -> 0xC00D */
extern void dispatch_04d5(void);      /* 0x04d5 -> 0xD3A2 */
extern void dispatch_04e4(void);      /* 0x04e4 -> 0xE2EC */
extern void dispatch_061f(void);      /* 0x061f -> Bank1:0xE25E */
extern void dispatch_0601(void);      /* 0x0601 -> 0xEA7C */
extern void dispatch_052a(void);      /* 0x052a -> 0xE961 */
extern void dispatch_0359(void);      /* 0x0359 -> 0xDEE3 */
extern void dispatch_043a(void);      /* 0x043a -> 0xE677 */
extern void dispatch_0507(void);      /* 0x0507 -> 0xE50D */

/* External functions from firmware - USB endpoint loop */
extern void usb_ep_loop_180d(uint8_t param);  /* 0x180d */
extern void usb_ep_loop_3419(void);           /* 0x3419 */

/* Return-value wrapper: dispatch_0516 returns result in R7 (0 or 1) */
static uint8_t dispatch_0516_ret(void)
{
    dispatch_0516();
    /* The dispatch function stores result in R7; access via inline asm */
    __asm
        mov dpl, r7
    __endasm;
    return 0;  /* DPL is already set by asm */
}

/* Return-value wrapper: dispatch_0430 returns result in R7 */
static uint8_t dispatch_0430_ret(void)
{
    dispatch_0430();
    __asm
        mov dpl, r7
    __endasm;
    return 0;
}

/*
 * Main processing loop
 * Address: 0x2f80-0x3129 (937 bytes)
 *
 * This is the main polling loop that handles:
 * - Timer/system events
 * - USB events
 * - NVMe events
 * - Power management
 *
 * Original start:
 *   2f80: clr a
 *   2f81: mov dptr, #0x0a59
 *   2f84: movx @dptr, a
 *   2f85: mov dptr, #0xcc32
 *   2f88: lcall 0x5418
 *   2f8b: lcall 0x04d0
 *   ...
 */
void main_loop(void)
{
    uint8_t events;
    uint8_t state;
    uint8_t loop_state;
    uint8_t result;
    uint8_t usb_status;

    /* Clear loop state flag at entry */
    G_LOOP_STATE = 0x00;

    while (1) {
        /* Set bit 0 of REG_CPU_EXEC_STATUS - timer/system handler */
        reg_set_bit_0_cpu_exec();

        /* Call dispatch stubs and handlers */
        timer_link_status_handler();
        phy_config_link_params();
        reserved_stub_handler();
        main_polling_handler();
        usb_power_init();

        /* Check event flags (0x2f9a-0x2fb1) */
        events = G_EVENT_FLAGS;
        if (events & EVENT_FLAGS_ANY) {  /* 0x83 mask */
            if (events & (EVENT_FLAG_ACTIVE | EVENT_FLAG_PENDING)) {  /* 0x81 mask */
                event_state_handler();
            }
            error_state_config();
            phy_register_config();
            flash_command_handler();
        }

        /* Clear interrupt priority for EXT0 and EXT1 (0x2fb4-0x2fb6) */
        IP &= ~0x05;    /* Clear bits 0,2: clr 0xB8.0, clr 0xB8.2 */

        /* Enable external interrupts (0x2fb8-0x2fbc) */
        EX0 = 1;        /* setb 0xA8.0 */
        EX1 = 1;        /* setb 0xA8.2 */
        EA = 1;         /* setb 0xA8.7 */

        /* ===== Loop body starts at 0x2fbe ===== */

        /* Disable interrupts for critical section (0x2fbe) */
        EA = 0;

        /* Check system state at 0x0AE2 (0x2fc0-0x2fc9) */
        state = G_SYSTEM_STATE_0AE2;
        if (state == 0 || state == 0x10) {
            goto state_ready;  /* Jump to 0x303f */
        }

        /* State machine processing when state != 0 and state != 0x10 (0x2fcb-0x2ffa) */
        loop_state = G_LOOP_STATE;
        if (loop_state != 0) {
            goto check_loop_state;  /* Jump to 0x2ffb */
        }

        /* G_LOOP_STATE == 0: Check G_STATE_0AE8 (0x2fd1-0x2fe6) */
        if (G_STATE_0AE8 != 0) {
            /* Set G_LOOP_STATE = 0x02 and continue (0x2fdf-0x2fe5) */
            G_LOOP_STATE = 0x02;
            goto check_loop_state;
        }

        /* Check G_EVENT_CTRL_09FA == 0x04 (0x2fd7-0x2fdd) */
        if (G_EVENT_CTRL_09FA == 0x04) {
            /* Set G_LOOP_STATE = 0x01 and initialize (0x2fe7-0x2ff8) */
            G_LOOP_STATE = 0x01;
            G_STATE_0B39 = 0x00;
            G_IO_CMD_STATE = 0xFF;  /* XDATA(0x0002) = 0xFF */
            dispatch_04e9();        /* 0x04e9 -> handler_e8e4 */
        } else {
            /* Set G_LOOP_STATE = 0x02 (0x2fdf-0x2fe4) */
            G_LOOP_STATE = 0x02;
        }

check_loop_state:
        /* Check G_LOOP_STATE value (0x2ffb-0x303d) */
        loop_state = G_LOOP_STATE;

        if (loop_state == 0x01) {
            /* State 0x01: dispatch 0x0516 and check result (0x3002-0x301a) */
            result = dispatch_0516_ret();
            if (result != 0) {
                result = dispatch_0430_ret();
                if (result != 0) {
                    G_LOOP_STATE = 0x02;
                    G_STATE_FLAG_06E6 = G_STATE_FLAG_06E6 - 1;  /* dec a at 0x3017 */
                    dispatch_045d();
                }
            }
        } else if (loop_state == 0x02) {
            /* State 0x02: Check USB status registers (0x3023-0x303d) */
            usb_status = REG_POWER_STATUS & 0x40;  /* 0x92C2 bit 6 */
            /* swap a; rrc a; rrc a; anl a, #0x03 -> extract bits 5:4 >> 4 & 0x03 */
            usb_status = (usb_status >> 4) & 0x03;
            if (usb_status != 0) {
                /* Check REG_USB_PHY_CTRL_91C0 bit 1 (0x3031-0x303d) */
                if (REG_USB_PHY_CTRL_91C0 & 0x02) {
                    dispatch_0322();  /* 0x0322 -> system_state_handler */
                }
            }
        }

state_ready:
        /* Enable interrupts and continue processing (0x303f) */
        EA = 1;

        /* Call event handler (0x3041) */
        dispatch_0507();  /* 0x0507 -> handler_e50d */

        /* Check event flags again (0x3044-0x3060) */
        events = G_EVENT_FLAGS;
        if (events & EVENT_FLAGS_ANY) {  /* 0x83 mask */
            if (G_MISC_FLAG_06EC != 0) {
                EA = 0;
                dispatch_061f();  /* 0x061f -> Bank1:handler_e25e */
                if (G_STATE_0AB6 != 0) {
                    dispatch_0601();  /* 0x0601 -> handler_ea7c */
                }
                EA = 1;
            }
        }

        /* Check command slot handler (0x3062-0x306d) */
        if (G_CMD_SLOT_INDEX != 0) {
            EA = 0;
            dispatch_052a();  /* 0x052a -> handler_e961 */
            EA = 1;
        }

        /* Check using 0x541f pattern (returns in R7) - repeated pattern (0x306f-0x30a1) */
        /* Pattern: lcall 0x541f; jz skip; clr EA; lcall handler; setb EA */

        /* Check and call dispatch_0359 if needed (0x3072-0x3079) */
        /* This uses internal check at 0x541f which returns result in R7 */
        /* Simplified: just call the dispatchers in sequence */

        /* Check and call dispatch_480c if needed (0x307e-0x3085) */
        /* External function at 0x480c */

        /* USB state check (0x308a-0x30a1) */
        if (G_USB_STATE_0B41 != 0) {
            EA = 0;
            if (REG_TIMER1_CSR & 0x02) {  /* 0xCC17 bit 1 */
                REG_TIMER1_CSR = 0x02;
                dispatch_04d5();  /* 0x04d5 -> handler_d3a2 */
            }
            EA = 1;
        }

        /* Check G_STATE_0AE9 (0x30a3-0x30b0) */
        if (G_STATE_0AE9 == 0x0F) {
            EA = 0;
            dispatch_04e4();  /* 0x04e4 -> handler_e2ec */
            EA = 1;
        }

        /* Check I_STATE_6A loop condition (0x30b2-0x30b9) */
        if (I_STATE_6A == 0x0B) {
            /* Exit condition met - continue with shutdown sequence */
            EA = 0;
            G_LOOP_STATE_0A5A = I_STATE_6A;

            /* Check power init flag (0x30c2-0x30c8) */
            if (G_POWER_INIT_FLAG != 0) {
                goto loop_end;
            }

            /* Check REG_TIMER2_CSR bits (0x30ca-0x3107) */
            if (!(REG_TIMER2_CSR & 0x01)) {  /* 0xCC1D bit 0 */
                if (!(REG_TIMER2_CSR & 0x02)) {  /* bit 1 */
                    /* Call dispatch_043a and handle log counter (0x30d5-0x30e3) */
                    dispatch_043a();
                    REG_TIMER2_CSR = 0x01;
                    G_STATE_0B39 = 0x00;
                    goto loop_end;
                } else {
                    /* Log counter handling (0x30e5-0x30f3) */
                    if (G_LOG_COUNTER_044B == 0) {
                        G_LOG_ACTIVE_044C++;
                    }
                    dispatch_0430();
                    goto loop_end;
                }
            } else {
                /* Bit 0 set: call 0x0516 and process (0x30f5-0x3107) */
                result = dispatch_0516_ret();
                if (result != 0) {
                    result = dispatch_0430_ret();
                    if (result != 0) {
                        goto loop_end;
                    }
                }
            }
            G_LOOP_STATE_0A5A = 0x01;
        }

loop_end:
        /* Final USB status check (0x3107-0x3125) */
        if (G_LOOP_STATE_0A5A != 0) {
            I_STATE_6A = 0x00;

            /* Check REG_USB_STATUS (0x9000) bit 0 (0x3111-0x3122) */
            if (REG_USB_STATUS & USB_STATUS_ACTIVE) {
                usb_ep_loop_180d(0x00);  /* 0x180d with R7=0 */
            } else {
                usb_ep_loop_3419();       /* 0x3419 */
            }
            dispatch_043a();  /* 0x043a -> handler_e677 */
        }

        /* Re-enable interrupts and loop back (0x3125-0x3127) */
        EA = 1;
        /* ljmp 0x2fbe - continue to next iteration */
    }
}

/*===========================================================================
 * Code Banking and Dispatch System
 *===========================================================================*/

/*
 * ASM2464PD CODE BANKING MECHANISM
 * =================================
 *
 * The ASM2464PD has ~98KB of firmware but the 8051 can only address 64KB.
 * A code banking scheme using the DPX register (0x96) provides access to
 * all code.
 *
 * MEMORY MAP:
 *   CPU Address   | DPX=0 (Bank 0)      | DPX=1 (Bank 1)
 *   --------------|---------------------|---------------------
 *   0x0000-0x7FFF | File 0x0000-0x7FFF  | File 0x0000-0x7FFF (shared)
 *   0x8000-0xFFFF | File 0x8000-0xFFFF  | File 0x10000-0x17F0C
 *
 * The lower 32KB (0x0000-0x7FFF) is always visible regardless of DPX.
 * This contains interrupt vectors, dispatch routines, and common code.
 *
 * The upper 32KB (0x8000-0xFFFF) is bank-switched:
 *   - DPX=0: Maps to file offset 0x08000-0x0FFFF (bank 0)
 *   - DPX=1: Maps to file offset 0x10000-0x17F0C (bank 1)
 *
 * DISPATCH MECHANISM:
 *   The handlers use a clever trampoline system:
 *   1. Caller loads DPTR with target function address (e.g., 0xE911)
 *   2. Caller jumps to dispatch function (0x0300 or 0x0311)
 *   3. Dispatch saves context, sets DPX, then RET pops the DPTR
 *   4. Execution continues at the target address in the selected bank
 *
 * FILE OFFSET CALCULATION:
 *   For bank 0 (DPX=0): file_offset = addr
 *   For bank 1 (DPX=1): file_offset = addr + 0x8000
 *
 * EXAMPLE:
 *   pcie_error_dispatch calls jump_bank_1(0xE911)
 *   -> DPX=1, execution jumps to 0xE911 in bank 1
 *   -> File offset = 0xE911 + 0x8000 = 0x16911
 */

/* jump_bank_0() and jump_bank_1() moved to app/dispatch.c */

/*
 * reg_set_bit_0 - Set bit 0 of a register
 * Address: 0x5418-0x541e (7 bytes)
 *
 * Reads the byte at the given XDATA address, clears bit 0,
 * sets bit 0, and writes back. Essentially: reg |= 0x01
 *
 * From ghidra.c: *param_1 = *param_1 & 0xfe | 1;
 *
 * Original disassembly:
 *   5418: movx a, @dptr    ; read from XDATA[DPTR]
 *   5419: anl a, #0xfe     ; clear bit 0
 *   541b: orl a, #0x01     ; set bit 0
 *   541d: movx @dptr, a    ; write back
 *   541e: ret
 */
void reg_set_bit_0(uint16_t reg_addr)
{
    uint8_t val = XDATA8(reg_addr);
    val = (val & 0xFE) | 0x01;
    XDATA8(reg_addr) = val;
}

/*
 * reg_set_bit_0_cpu_exec - Set bit 0 of REG_CPU_EXEC_STATUS
 * Address: Inline of 0x5418 call pattern
 *
 * Called from main_loop at 0x2f85-0x2f88:
 *   2f85: mov dptr, #0xcc32
 *   2f88: lcall 0x5418
 */
void reg_set_bit_0_cpu_exec(void)
{
    REG_CPU_EXEC_STATUS = (REG_CPU_EXEC_STATUS & ~CPU_EXEC_STATUS_ACTIVE) | CPU_EXEC_STATUS_ACTIVE;
}

/*===========================================================================
 * Main Loop Handler Stubs
 * These are dispatch stubs that set DPTR and call jump_bank_0
 * or jump_bank_1
 *===========================================================================*/

/* timer_link_status_handler() moved to drivers/timer.c */

/* phy_config_link_params() moved to drivers/phy.c */

/*
 * reserved_stub_handler - Placeholder/Reserved handler
 * Address: 0x04b2-0x04b6 (5 bytes) -> dispatches to 0xE971
 *
 * Function at 0xE971 is a stub that just returns immediately (ret).
 * The area 0xE971-0xE9A2 contains RET instructions followed by NOPs,
 * indicating this is reserved space for future functionality.
 *
 * Original disassembly:
 *   e971: ret
 *   e972: ret
 *   e973: ret
 *   ... (NOPs follow)
 */
void reserved_stub_handler(void)
{
    /* Stub function - reserved for future functionality */
    /* Original firmware at 0xE971 just returns immediately */
}

/*
 * main_polling_handler - Core polling and dispatch handler
 * Address: 0x4fb6-0x50da (292 bytes)
 *
 * This is the main polling handler called from the main loop.
 * It dispatches to multiple sub-handlers and polls for PHY status.
 *
 * Original disassembly:
 *   4fb6: lcall 0x5305        ; dispatch stub
 *   4fb9: lcall 0x04b7        ; dispatch stub
 *   4fbc: lcall 0x04bc        ; dispatch stub
 *   4fbf: lcall 0x4be6        ; dispatch stub
 *   4fc2: lcall 0x032c        ; dispatch stub
 *   4fc5: lcall 0x0539        ; dispatch stub
 *   4fc8: lcall 0x04f8        ; dispatch stub
 *   4fcb: lcall 0x063d        ; dispatch stub
 *   4fce: mov dptr, #0x0ae3   ; G_STATE_FLAG_0AE3
 *   4fd1: movx a, @dptr
 *   4fd2: jz 0x4fdb           ; if zero, skip
 *   4fd4: mov dptr, #0xcc32   ; REG_CPU_EXEC_STATUS
 *   4fd7: movx a, @dptr
 *   4fd8: anl a, #0xfe        ; clear bit 0
 *   4fda: movx @dptr, a
 *   4fdb: mov dptr, #0xc6b3   ; REG_PHY_EXT_B3
 *   4fde: movx a, @dptr
 *   4fdf: anl a, #0x30        ; check bits 4,5
 *   4fe1: jz 0x4fdb           ; loop until set
 *   4fe3: lcall 0x0462        ; dispatch stub
 *   4fe6: mov dptr, #0x06e6   ; G_STATE_FLAG_06E6
 *   4fe9: mov a, #0x01
 *   4feb: movx @dptr, a       ; set to 1
 *   4fec: lcall 0x0435        ; dispatch stub
 *   4fef: ljmp 0x0340         ; final dispatch (tail call)
 */
void main_polling_handler(void)
{
    /* Call handlers in sequence */
    scsi_flash_ready_check();      /* 0x5305 -> 0x4C40 */
    dispatch_04b7();               /* 0x04b7 -> Bank0:0xE597 */
    dispatch_04bc();               /* 0x04bc -> Bank0:0xE14B */
    init_sys_flags_07f0();         /* 0x4be6 -> inline handler */
    phy_power_config_handler();    /* 0x032c -> Bank0:0x92C5 */
    dispatch_0539();               /* 0x0539 -> Bank0:0x8D77 */
    dispatch_04f8();               /* 0x04f8 -> Bank0:0xDE16 */
    pcie_handler_unused_eef9();    /* 0x063d -> Bank1:0xEEF9 */

    /* Check state flag and conditionally clear bit 0 of CPU exec status */
    if (G_STATE_FLAG_0AE3 != 0) {
        REG_CPU_EXEC_STATUS = REG_CPU_EXEC_STATUS & 0xFE;
    }

    /* Poll PHY status register until bits 4 or 5 are set */
    /* This waits for PHY link training to complete */
    while ((REG_PHY_EXT_B3 & PHY_EXT_LINK_READY) == 0);

    pcie_tunnel_setup();           /* 0xCD6C */

    /* Set flag indicating processing complete */
    G_STATE_FLAG_06E6 = 1;

    dispatch_0435();               /* 0x0435 -> Bank0:0xD127 */
    handler_bf8e();                /* 0x0340 -> Bank0:0xBF8E (tail call) */
}


/* event_state_handler() moved to app/state_helpers.c */
/* error_state_config() moved to app/state_helpers.c */

/* phy_register_config() moved to drivers/phy.c */

/* flash_command_handler() moved to drivers/flash.c */

/* usb_buffer_dispatch() moved to drivers/usb.c */

/* system_interrupt_handler() moved to drivers/timer.c */

/* pcie_nvme_event_handler() moved to drivers/pcie.c */
/* pcie_error_dispatch() moved to drivers/pcie.c */
/* pcie_event_bit5_handler() moved to drivers/pcie.c */
/* pcie_timer_bit4_handler() moved to drivers/pcie.c */
/* system_timer_handler() moved to drivers/timer.c */

/* All ISRs (ext0_isr, ext1_isr, timer1_isr, serial_isr) moved to src/interrupt.c */

/*===========================================================================
 * BANK 1 SYSTEM INITIALIZATION
 *
 * This function initializes system configuration from flash storage.
 * It resides in Bank 1 (code offset 0x10000+) and is called during boot.
 *===========================================================================*/

/* External helpers for UART/debug output */
extern uint8_t uart_read_byte_dace(uint8_t offset);  /* 0xdace - Read from UART buffer */
extern void uart_write_byte_daeb(void);              /* 0xdaeb - Write to UART buffer */
extern uint8_t uart_check_status_daf5(void);         /* 0xdaf5 - Check UART status */
extern uint8_t uart_read_status_dae2(void);          /* 0xdae2 - Read UART status */
extern void uart_write_daff(void);                   /* 0xdaff - UART write */
extern uint8_t uart_read_dacc(void);                 /* 0xdacc - UART read */

/* External helpers for system setup */
extern void sys_event_dispatch_05e8(void);           /* 0x05e8 - Event dispatcher */
extern void sys_init_helper_bbc7(void);              /* 0xbbc7 - System init helper */
extern void sys_timer_handler_e957(void);            /* 0xe957 - Timer/watchdog handler */

/* system_init_from_flash() moved to drivers/flash.c */


/* ============================================================
 * Functions moved from stubs.c
 * ============================================================ */

/*
 * handler_0395 - Polling/wait dispatch entry
 * Address: 0x0395 (dispatch entry), target: 0xDA8F
 *
 * From ghidra.c: jump_bank_0(0xda8f)
 * This is a wait/poll function called during CSW sending.
 */
void handler_0395(void)
{
    /* Dispatch to 0xDA8F - wait/poll function */
    /* For now, no-op since it's a poll loop */
}

void handler_0327_usb_power_init(void)
{
    usb_power_init();
}

void handler_039a_buffer_dispatch(void)
{
    usb_buffer_handler();
}

void startup_init(void)
{
    uint8_t offset;

    offset = G_EP_DISPATCH_OFFSET;
    if (offset < 0x20) {
        /* Clear dispatch offset temporarily */
        G_EP_DISPATCH_OFFSET = 0;

        /* Get descriptor length with offset + 0x0C */
        usb_get_descriptor_length(offset + 0x0C);

        /* Convert speed with offset + 0x2F */
        usb_convert_speed(offset + 0x2F);

        /* Build NVMe command */
        nvme_build_cmd(0);

        /* Restore and finalize */
        usb_convert_speed(G_EP_DISPATCH_OFFSET + 0x2F);
    }
}

void sys_event_dispatch_05e8(void)
{
    /* Dispatch to event handler at 0x9D90 in bank 1 */
    handler_9d90();
}

/*
 * sys_init_helper_bbc7 - System init helper
 * Address: 0xbbc7-0xbbc9 (3 bytes)
 *
 * From ghidra.c (line 16491):
 *   write_xdata_reg(0, 0x12, 0xb, 1)
 *
 * Writes to XDATA register with specific parameters.
 * WARNING: Ghidra shows this as infinite loop / no return.
 */
void sys_init_helper_bbc7(void)
{
    /* Write configuration to register area */
    /* Parameters: 0, 0x12, 0x0b, 1 suggest:
     * - Base offset 0
     * - Value 0x12
     * - Register/mode 0x0b
     * - Count/flag 1 */
    XDATA8(0x0B12) = 0x01;  /* Simplified write */
}

void sys_timer_handler_e957(void)
{
    handler_db09();
}

/*
 * cpu_int_ctrl_trigger_e933 - CPU interrupt control trigger
 * Address: 0xe933-0xe939 (Bank 1)
 *
 * Writes timer start sequence (0x04 then 0x02) to REG_CPU_INT_CTRL.
 *
 * Disassembly:
 *   e933: mov dptr, #0xcc81   ; REG_CPU_INT_CTRL
 *   e936: lcall 0x95c2        ; Write 0x04 then 0x02
 *   e939: ret
 *
 * The helper at 0x95c2:
 *   95c2: mov a, #0x04
 *   95c4: movx @dptr, a
 *   95c5: mov a, #0x02
 *   95c7: movx @dptr, a
 *   95c8: ret
 */
void cpu_int_ctrl_trigger_e933(void)
{
    REG_CPU_INT_CTRL = 0x04;
    REG_CPU_INT_CTRL = 0x02;
}

/*
 * cpu_dma_setup_e81b - CPU DMA setup and trigger
 * Address: 0xe81b-0xe82b (Bank 1)
 *
 * Sets up DMA address in registers 0xCC82-0xCC83 and triggers via CPU_INT_CTRL.
 *
 * Disassembly:
 *   e81b: mov dptr, #0xcc82
 *   e81e: mov a, r6          ; param_hi
 *   e81f: movx @dptr, a      ; Write to 0xCC82
 *   e820: inc dptr
 *   e821: mov a, r7          ; param_lo
 *   e822: movx @dptr, a      ; Write to 0xCC83
 *   e823: mov dptr, #0xcc81  ; REG_CPU_INT_CTRL
 *   e826: lcall 0x95c2       ; Write 0x04 then 0x02
 *   e829: dec a              ; a = 0x01
 *   e82a: movx @dptr, a      ; Write 0x01 to CC81
 *   e82b: ret
 *
 * Parameters:
 *   param_hi (r6): High byte of DMA value
 *   param_lo (r7): Low byte of DMA value
 */
void cpu_dma_setup_e81b(uint8_t param_hi, uint8_t param_lo)
{
    /* Write DMA parameters to 0xCC82-0xCC83 */
    XDATA_REG8(0xCC82) = param_hi;
    XDATA_REG8(0xCC83) = param_lo;

    /* Trigger sequence: 0x04, 0x02, 0x01 to CPU_INT_CTRL */
    REG_CPU_INT_CTRL = 0x04;
    REG_CPU_INT_CTRL = 0x02;
    REG_CPU_INT_CTRL = 0x01;
}

/*
 * cpu_dma_channel_91_trigger_e93a - Trigger DMA on channel 0xCC91
 * Address: 0xe93a-0xe940 (7 bytes)
 *
 * Disassembly:
 *   e93a: mov dptr, #0xcc91   ; DMA channel register
 *   e93d: lcall 0x95c2        ; Trigger sequence (write 0x04, then 0x02)
 *   e940: ret
 *
 * Writes trigger sequence 0x04, 0x02 to DMA channel register 0xCC91
 */
void cpu_dma_channel_91_trigger_e93a(void)
{
    XDATA_REG8(0xCC91) = 0x04;
    XDATA_REG8(0xCC91) = 0x02;
}
