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
#include "app/dispatch.h"
#include "app/scsi.h"
#include "drivers/timer.h"
#include "drivers/phy.h"
#include "drivers/flash.h"
#include "drivers/usb.h"
#include "drivers/pcie.h"
#include "drivers/nvme.h"
#include "drivers/uart.h"
#include "utils.h"
#include "drivers/power.h"
#include "drivers/pd.h"
#include "drivers/usb_descriptors.h"

/* Forward declarations for app layer functions not in headers yet */
extern void event_state_handler(void);         /* app/event_handler.c */
extern void error_state_config(void);          /* app/event_handler.c */
extern void protocol_nop_handler(void);        /* app/protocol.c */

/*===========================================================================
 * Forward declarations
 *===========================================================================*/

/* Main loop */
void main_loop(void);

/* Initialization */
void process_init_table(void);

/* Local functions */
void reg_set_bit_0(uint16_t reg_addr);
void reg_set_bit_0_cpu_exec(void);
void reserved_stub_handler(void);
void main_polling_handler(void);

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
 * Note: These are C wrappers for comparison logic.
 * The actual firmware function at 0x0d22 is cmp32() in utils.c.
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
        /* Boot mode 0x80 - check I_USB_STATE */
        state_6a = I_USB_STATE;

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
        state_6a = I_USB_STATE;

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
 * Note: Local helper used by process_init_table.
 * The actual firmware function at 0x0be6 is banked_store_byte() in utils.c.
 */
static void write_xdata_reg(uint8_t addr_h, uint8_t addr_l, uint8_t value)
{
    uint16_t addr = ((uint16_t)addr_h << 8) | addr_l;
    XDATA8(addr) = value;
}

/*
 * process_init_table - Process initialization data table
 * Address: 0x43a3-0x43d1 (47 bytes) - core table loop
 *
 * The original firmware processes a compressed initialization table at code
 * address 0x0620 that initializes various XDATA registers. Since we don't
 * have the same table embedded in our firmware, we perform the equivalent
 * initialization directly.
 *
 * Original table at 0x0620 does:
 *   - Write to 0x0AF0 region (1 byte)
 *   - Write to 0x0864 region (18 bytes)
 *   - Write to 0x0876 region (21 bytes - "00000000" string + metadata)
 *
 * Note: The full initialization is handled by hardware emulation/real HW.
 * This stub allows the firmware to proceed to main_loop.
 */
void process_init_table(void)
{
    /* Perform equivalent initialization that the table would do */

    /* Initialize xfer state region (0x0AF0) */
    G_XFER_STATE_0AF3 = 0x00;

    /* Initialize serial number region (0x0864-0x0875) */
    /* Write "0000000000000000" pattern */
    G_SERIAL_NUM_0864 = '0';
    XDATA8(0x0865) = '0';
    XDATA8(0x0866) = '0';
    XDATA8(0x0867) = '0';
    XDATA8(0x0868) = '0';
    XDATA8(0x0869) = '0';
    XDATA8(0x086A) = '0';
    XDATA8(0x086B) = '0';
    XDATA8(0x086C) = '0';
    XDATA8(0x086D) = '0';
    XDATA8(0x086E) = '0';
    XDATA8(0x086F) = '0';
    XDATA8(0x0870) = '0';
    XDATA8(0x0871) = '0';
    XDATA8(0x0872) = '0';
    XDATA8(0x0873) = '0';

    /* Initialize vendor info region (0x0876-0x088A) */
    /* "Asmedia" + padding */
    XDATA8(0x0876) = 'A';
    XDATA8(0x0877) = 's';
    XDATA8(0x0878) = 'm';
    XDATA8(0x0879) = 'e';
    XDATA8(0x087A) = 'd';
    XDATA8(0x087B) = 'i';
    XDATA8(0x087C) = 'a';
    /* Pad rest with zeros */
    XDATA8(0x087D) = 0x00;
    XDATA8(0x087E) = 0x00;
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
    /* Note: IDATA clear, SP init (0x72), and DPX init (0x00) are done in crt0.s
     * matching the original firmware at 0x436b-0x4377 */

    /* Process initialization data table */
    process_init_table();

    /* Basic system initialization */
    G_SYSTEM_CTRL = 0x33;

    /* Initialize USB endpoint configuration */
    G_EP_CONFIG_BASE = 0x20;
    G_EP_CONFIG_ARRAY = 0x04;
    G_EP_CONFIG_05A8 = 0x02;
    G_EP_CONFIG_05F8 = 0x04;

    /* Initialize system flags */
    G_USB_CMD_CONFIG = 0x01;
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

    // Setup uart. This is done at 0xe597 in the original fw, but doesn't look like it's implemented here yet.
    // This seems to turn off parity and enable the FIFO?
    REG_UART_FCR |= (1 << 1);
    REG_UART_LCR &= ~(1 << 3);
    // XDATA_REG8(0xCA2E) |= (1 << 0); // this is also done in the same function but doesn't seem necessary?

    /* One-time initialization - called once before main loop (0x1F7C-0x1FAD) */
    timer_link_status_handler();     /* 0x527a */
    phy_config_link_params();        /* 0x04c6 */
    reserved_stub_handler();         /* 0x52ef */
    main_polling_handler();          /* 0x04a8 */
    state_init_4ffb();               /* 0x4ffb -> Sets G_EVENT_FLAGS = 0x04, then 0x87 via dispatch_0520 */
    usb_power_init();                /* 0x0327 */

    /* One-time event flags check (0x1F96-0x1FAD) - only runs on first pass */
    events = G_EVENT_FLAGS;
    if (events & EVENT_FLAGS_ANY) {  /* 0x83 mask */
        if (events & (EVENT_FLAG_ACTIVE | EVENT_FLAG_PENDING)) {  /* 0x81 mask */
            event_state_handler();       /* 0x048a */
        }
        pd_debug_print_flp();            /* 0x05e3 -> Bank1:0xB103 */
        error_state_config();            /* 0x0566 */
        phy_register_config();           /* 0x050c */
    }

    /* Clear interrupt priority for EXT0 and EXT1 (0x1FB0-0x1FB2) */
    IP &= ~0x05;    /* Clear bits 0,2: clr 0xB8.0, clr 0xB8.2 */

    /* Enable external interrupts (0x1FB4-0x1FB8) */
    EX0 = 1;        /* setb 0xA8.0 */
    EX1 = 1;        /* setb 0xA8.2 */
    EA = 1;         /* setb 0xA8.7 */

    /* Main loop starts at 0x1FBA - jumps back here, NOT to 0x1F7C */
    while (1) {

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
                if (REG_USB_PHY_CTRL_91C0 & USB_PHY_CTRL_BIT1) {
                    dispatch_0322();  /* 0x0322 -> system_state_handler */
                }
            }
        }

state_ready:
        /* Enable interrupts and continue processing (0x303f) */
        EA = 1;

        /*
         * USB Setup Packet Handler - Process pending control transfers
         * Original firmware: 0xA600-0xA6A5
         *
         * When G_TLP_STATE_07E9 == 1, there's a pending setup packet to process.
         * This dispatches based on bmRequestType and bRequest fields.
         * For GET_DESCRIPTOR (bmRequestType=0x80, bRequest=0x06), configures DMA
         * and sets G_USB_CTRL_STATE_07E1 = 5 to indicate data is ready.
         */
        if (G_TLP_STATE_07E9 == 1) {
            uint8_t bmRequestType = XDATA_REG8(0x9E00);
            uint8_t bRequest = XDATA_REG8(0x9E01);

            /* Standard device request (bmRequestType bit 7 = 1 means IN direction) */
            if (bmRequestType == 0x80 && bRequest == 0x06) {
                /* GET_DESCRIPTOR request */
                uint8_t desc_type = XDATA_REG8(0x9E03);  /* wValue high = descriptor type */
                uint8_t desc_index = XDATA_REG8(0x9E02); /* wValue low = descriptor index */
                uint8_t req_len = XDATA_REG8(0x9E06);    /* wLength low */
                uint8_t desc_len;
                __code const uint8_t *desc_ptr;

                /* Look up the descriptor in code ROM */
                desc_ptr = usb_get_descriptor(desc_type, desc_index, &desc_len);

                if (desc_ptr != 0) {
                    /* Use minimum of requested length and descriptor length */
                    if (req_len < desc_len) {
                        desc_len = req_len;
                    }

                    /* Configure DMA source address (code ROM address of descriptor) */
                    REG_USB_EP_BUF_HI = (uint8_t)((uint16_t)desc_ptr >> 8);
                    REG_USB_EP_BUF_LO = (uint8_t)((uint16_t)desc_ptr & 0xFF);

                    /* Configure transfer length */
                    REG_USB_EP0_LEN_L = desc_len;

                    /* Mark descriptor as ready to send */
                    G_USB_CTRL_STATE_07E1 = 0x05;

                    /* Trigger USB DMA to send descriptor (0xA57A) */
                    REG_USB_DMA_TRIGGER = 0x01;

                    /* Clear descriptor ready state after triggering */
                    G_USB_CTRL_STATE_07E1 = 0x00;
                }
            }

            /* Clear pending state */
            G_TLP_STATE_07E9 = 0;
        }

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
            if (REG_TIMER1_CSR & TIMER_CSR_EXPIRED) {  /* 0xCC17 bit 1 */
                REG_TIMER1_CSR = TIMER_CSR_EXPIRED;
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

        /* Check I_USB_STATE loop condition (0x30b2-0x30b9) */
        if (I_USB_STATE == 0x0B) {
            /* Exit condition met - continue with shutdown sequence */
            EA = 0;
            G_LOOP_STATE_0A5A = I_USB_STATE;

            /* Check power init flag (0x30c2-0x30c8) */
            if (G_POWER_INIT_FLAG != 0) {
                goto loop_end;
            }

            /* Check REG_TIMER2_CSR bits (0x30ca-0x3107) */
            if (!(REG_TIMER2_CSR & TIMER_CSR_ENABLE)) {  /* 0xCC1D bit 0 */
                if (!(REG_TIMER2_CSR & TIMER_CSR_EXPIRED)) {  /* bit 1 */
                    /* Call dispatch_043a and handle log counter (0x30d5-0x30e3) */
                    dispatch_043a();
                    REG_TIMER2_CSR = TIMER_CSR_ENABLE;
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
            I_USB_STATE = 0x00;

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
 *   0x8000-0xFFFF | File 0x8000-0xFFFF  | File 0xFF6B-0x17E77
 *
 * The lower 32KB (0x0000-0x7FFF) is always visible regardless of DPX.
 * This contains interrupt vectors, dispatch routines, and common code.
 *
 * The upper 32KB (0x8000-0xFFFF) is bank-switched:
 *   - DPX=0: Maps to file offset 0x08000-0x0FFFF (bank 0)
 *   - DPX=1: Maps to file offset 0x0FF6B-0x17F0C (bank 1)
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
 *   For bank 1 (DPX=1): file_offset = addr + 0x7F6B
 *
 * EXAMPLE:
 *   pcie_error_dispatch calls jump_bank_1(0xE911)
 *   -> DPX=1, execution jumps to 0xE911 in bank 1
 *   -> File offset = 0xE911 + 0x8000 = 0x16911
 */

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
        REG_CPU_EXEC_STATUS = REG_CPU_EXEC_STATUS & ~CPU_EXEC_STATUS_ACTIVE;
    }

    /* Poll PHY status register until bits 4 or 5 are set */
    /* This waits for PHY link training to complete */
    while ((REG_PHY_EXT_B3 & PHY_EXT_LINK_READY) == 0);

    pcie_tunnel_setup();           /* 0xCD6C */

    /* Set flag indicating processing complete */
    G_STATE_FLAG_06E6 = 1;

    dispatch_0435();               /* 0x0435 -> Bank0:0xD127 */
    buffer_dispatch_bf8e();        /* 0x0340 -> Bank0:0xBF8E (tail call) */
}


/*===========================================================================
 * STATE INITIALIZATION
 *
 * These functions initialize the system state machine and set event flags.
 *===========================================================================*/

/*
 * state_init_4bd4 - Clear state variables and set event flags
 * Address: 0x4BD4-0x4C31 (94 bytes)
 *
 * Clears many state variables and then sets G_EVENT_FLAGS = 0x04.
 * This is the core initialization that enables the event processing loop.
 *
 * Original disassembly:
 *   4bd4: clr a
 *   4bd5: mov dptr, #0x0ae1    ; G_TLP_BASE_LO
 *   4bd8: movx @dptr, a        ; clear
 *   4bd9: mov dptr, #0x07e4    ; G_SYS_FLAGS_BASE
 *   4bdc: movx @dptr, a        ; clear
 *   4bdd: mov dptr, #0x05a5    ; G_CMD_INDEX_SRC
 *   4be0: movx @dptr, a        ; clear
 *   4be1: inc dptr             ; 0x05A6
 *   4be2: movx @dptr, a        ; clear
 *   ... (loop clears 0x05B0-0x06E0)
 *   4c22: mov dptr, #0x09f9    ; G_EVENT_FLAGS
 *   4c25: mov a, #0x04
 *   4c27: movx @dptr, a        ; G_EVENT_FLAGS = 0x04
 *   4c28: mov dptr, #0x09fa    ; G_EVENT_CTRL_09FA
 *   4c2b: movx @dptr, a        ; G_EVENT_CTRL_09FA = 0x04
 *   4c2c: clr a
 *   ...
 *   4c31: ret
 */
static void state_init_4bd4(void)
{
    uint16_t i;

    /* Clear initial state variables */
    G_TLP_BASE_LO = 0;          /* 0x0AE1 */
    G_SYS_FLAGS_BASE = 0;       /* 0x07E4 */
    G_CMD_INDEX_SRC = 0;           /* 0x05A5 */
    G_PCIE_TXN_COUNT_LO = 0;    /* 0x05A6 */

    /* Clear range 0x05B0-0x06E0 (loop in original) */
    /* This clears PCIe transaction table and work area */
    for (i = 0x05B0; i <= 0x06E0; i++) {
        XDATA8(i) = 0;
    }

    /* Clear additional state variables */
    G_STATE_0B3A = 0;           /* 0x0B3A */
    G_STATE_0B39 = 0;           /* 0x0B39 */
    G_TLP_BLOCK_SIZE_0ACC = 0;  /* 0x0ACC */
    G_USB_DESC_FLAG_0ACD = 0x0F; /* 0x0ACD = 0x0F */
    G_USB_CMD_CONFIG = 0;       /* 0x07EC */
    G_PD_FLAG_07B6 = 0;         /* 0x07B6 */
    G_CMD_SLOT_INDEX = 0;       /* 0x07B7 */
    G_FLASH_CMD_FLAG = 0;       /* 0x07B8 */

    /* Set event flags - this enables event processing */
    G_EVENT_FLAGS = 0x04;
    G_EVENT_CTRL_09FA = 0x04;

    /* Clear more state variables (after setting event flags) */
    G_FLASH_STATUS_09FB = 0;    /* 0x09FB */
}

/*
 * state_init_5372 - State initialization with PHY status
 * Address: 0x5372-0x538D (28 bytes)
 *
 * Calls state_init_4bd4, reads PHY status, and sets initialization flag.
 *
 * Original disassembly:
 *   5372: lcall 0x4bd4         ; state_init_4bd4
 *   5375: mov dptr, #0xe795    ; REG_FLASH_READY_STATUS
 *   5378: movx a, @dptr
 *   5379: anl a, #0x01         ; bit 0 -> r7
 *   537b: mov r7, a
 *   537c: movx a, @dptr
 *   537d: anl a, #0x02         ; bit 1 -> r5
 *   537f: mov r5, a
 *   5380: movx a, @dptr
 *   5381: anl a, #0x20         ; bit 5 -> r3
 *   5383: mov r3, a
 *   5384: lcall 0x051b         ; dispatch with r3,r5,r7
 *   5387: mov dptr, #0x07f6    ; G_STATE_07F6
 *   538a: mov a, #0x01
 *   538c: movx @dptr, a        ; G_STATE_07F6 = 1
 *   538d: ret
 */
static void state_init_5372(void)
{
    uint8_t flash_status;

    /* Initialize state variables and set event flags */
    state_init_4bd4();

    /* Read flash ready status and extract bits */
    flash_status = REG_FLASH_READY_STATUS;
    /* Bits 0, 1, 5 are extracted and passed to dispatch_051b */
    /* For now, we just call the dispatcher */
    dispatch_051b();

    /* Set initialization complete flag */
    G_SYS_FLAGS_07F6 = 1;
}

/*
 * state_init_4ffb - Main initialization entry point
 * Address: 0x4FFB-0x5010+ (variable)
 *
 * Called from main loop at 0x1F90. This is the main initialization
 * function that sets up the state machine and event processing.
 *
 * Original disassembly:
 *   4ffb: lcall 0x5372         ; state_init_5372
 *   4ffe: lcall 0x04ad         ; dispatch
 *   5001: lcall 0x04b2         ; reserved_stub_handler
 *   5004: lcall 0x4c8d         ; dispatch
 *   5007: lcall 0x032c         ; phy_power_config_handler
 *   500a: lcall 0x0520         ; dispatch
 *   500d: lcall 0x04e4         ; dispatch
 *   5010: lcall 0x0615         ; dispatch
 *   ...
 */
void state_init_4ffb(void)
{
    /* Initialize state and set G_EVENT_FLAGS = 0x04 */
    state_init_5372();

    /* Call additional initialization handlers */
    dispatch_04ad();               /* 0x04ad dispatch */
    reserved_stub_handler();       /* 0x04b2 -> stub */
    phy_power_config_handler();    /* 0x032c -> Bank0:0x92C5 */
    dispatch_0520();               /* 0x0520 -> system_init_from_flash (sets G_EVENT_FLAGS = 0x87) */
}

/*===========================================================================
 * BANK 1 SYSTEM INITIALIZATION
 *
 * This function initializes system configuration from flash storage.
 * It resides in Bank 1 (code offset 0xFF6B+) and is called during boot.
 *===========================================================================*/

/* UART functions declared in uart.h */

/* Forward declarations for functions defined in this file */
void sys_event_dispatch_05e8(void);
void sys_init_bbc7(void);
void sys_timer_handler_e957(void);

/* ============================================================
 * Dispatch Handler Implementations
 * ============================================================ */

/*
 * usb_poll_wait - Polling/wait dispatch entry
 * Address: 0x0395 (dispatch entry), target: 0xDA8F
 *
 * From ghidra.c: jump_bank_0(0xda8f)
 * This is a wait/poll function called during CSW sending.
 */
void usb_poll_wait(void)
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
    protocol_nop_handler();
}

/*
 * sys_init_bbc7 - System init helper
 * Address: 0xbbc7-0xbbc9 (3 bytes)
 *
 * From ghidra.c (line 16491):
 *   write_xdata_reg(0, 0x12, 0xb, 1)
 *
 * Writes to XDATA register with specific parameters.
 * WARNING: Ghidra shows this as infinite loop / no return.
 */
void sys_init_bbc7(void)
{
    /* Write configuration to register area */
    /* Parameters: 0, 0x12, 0x0b, 1 suggest:
     * - Base offset 0
     * - Value 0x12
     * - Register/mode 0x0b
     * - Count/flag 1 */
    G_PCIE_WORK_0B12 = 0x01;  /* Simplified write */
}

void sys_timer_handler_e957(void)
{
    flash_dma_trigger_handler();
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
    REG_CPU_INT_CTRL = CPU_INT_CTRL_TRIGGER;
    REG_CPU_INT_CTRL = CPU_INT_CTRL_ACK;
}

/*
 * cpu_dma_setup_e81b - CPU DMA setup and trigger
 * Address: 0xe81b-0xe82b (Bank 1)
 *
 * Sets up DMA address in registers 0xcc83-0xCC83 and triggers via CPU_INT_CTRL.
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
    /* Write DMA parameters to 0xcc83-0xCC83 */
    REG_CPU_CTRL_CC82 = param_hi;
    REG_CPU_CTRL_CC83 = param_lo;

    /* Trigger sequence: 0x04, 0x02, 0x01 to CPU_INT_CTRL */
    REG_CPU_INT_CTRL = CPU_INT_CTRL_TRIGGER;
    REG_CPU_INT_CTRL = CPU_INT_CTRL_ACK;
    REG_CPU_INT_CTRL = CPU_INT_CTRL_ENABLE;
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
    REG_CPU_DMA_INT = CPU_DMA_INT_TRIGGER;
    REG_CPU_DMA_INT = CPU_DMA_INT_ACK;
}
