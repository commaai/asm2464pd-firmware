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

/*===========================================================================
 * Forward declarations
 *===========================================================================*/

/* Main loop */
void main_loop(void);

/* Dispatch/utility functions */
void nvme_util_get_status_flags(uint16_t reg_addr);
void nvme_util_get_error_flags(uint16_t reg_addr);
void reg_set_bit_0(uint16_t reg_addr);

/* Main loop handler stubs */
void handler_04d0(void);
void phy_config_link_params(void);
void handler_04b2(void);
void handler_4fb6(void);
void handler_0327(void);
void handler_0494(void);
void handler_0606(void);
void handler_0589(void);
void handler_0525(void);

/* External functions */
extern void uart_init(void);

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

    /* TODO: Process initialization data table at 0x0648 */
    /* The original firmware iterates over a compressed data table
     * that contains register addresses and values to initialize.
     * For now, we do basic initialization directly. */

    /* Basic system initialization */
    REG_SYSTEM_CTRL_0000 = 0x33;

    /* Initialize USB endpoint configuration */
    XDATA8(0x054B) = 0x20;
    XDATA8(0x054E) = 0x04;
    XDATA8(0x05A8) = 0x02;
    XDATA8(0x05F8) = 0x04;

    /* Initialize system flags */
    XDATA8(0x07EC) = 0x01;
    XDATA8(0x07ED) = 0x00;
    XDATA8(0x07EE) = 0x00;
    XDATA8(0x07EF) = 0x00;

    /* Initialize NVMe */
    REG_NVME_LBA_0 = 0x02;

    /* Enter main processing loop (never returns) */
    main_loop();
}

/*===========================================================================
 * Main Processing Loop
 *===========================================================================*/

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

    /* Clear loop state flag */
    XDATA8(0x0A59) = 0x00;

    while (1) {
        /* Set bit 0 of REG_CPU_EXEC_CTRL (0xCC32) - timer/system handler */
        reg_set_bit_0(0xCC32);

        /* Call dispatch stubs */
        handler_04d0();
        phy_config_link_params();
        handler_04b2();
        handler_4fb6();
        handler_0327();

        /* Check event flags at 0x09f9 */
        events = XDATA8(0x09F9);
        if (events & 0x83) {
            if (events & 0x81) {
                handler_0494();
            }
            handler_0606();
            handler_0589();
            handler_0525();
        }

        /* Interrupt priority manipulation */
        /* clr 0xb8.0 (PX0 - external int 0 priority) */
        /* clr 0xb8.2 (PX1 - external int 1 priority) */
        /* setb 0xa8.0 (EX0 - external int 0 enable) */
        /* setb 0xa8.2 (EX1 - external int 1 enable) */
        /* setb/clr 0xa8.7 (EA - global interrupt enable) */
        IP &= ~0x05;    /* Clear PX0, PX1 */
        EX0 = 1;
        EX1 = 1;
        EA = 1;

        /* Temporarily disable interrupts for critical section */
        EA = 0;

        /* Check more state at 0x0ae2 */
        /* ... additional processing ... */

        /* Re-enable interrupts */
        EA = 1;
    }
}

/*===========================================================================
 * Dispatch / Utility Functions
 *===========================================================================*/

/*
 * nvme_util_get_status_flags - Dispatch function for status operations
 * Address: 0x0300-0x0310 (17 bytes)
 *
 * This is a dispatch function that:
 * 1. Pushes R0, ACC, DPL, DPH onto stack
 * 2. Sets R0 = 0x0A (dispatch parameter)
 * 3. Sets DPX = 0x00
 * 4. Returns (which pops DPTR from stack and jumps there)
 *
 * The calling convention is: set DPTR to target address, then call this.
 * The RET instruction will pop the pushed DPTR and jump to that address.
 *
 * Original disassembly:
 *   0300: push 0x08      ; push R0
 *   0302: mov a, #0x03
 *   0304: push 0xe0      ; push ACC
 *   0306: push 0x82      ; push DPL
 *   0308: push 0x83      ; push DPH
 *   030a: mov 0x08, #0x0a  ; R0 = 0x0A (cmd_dispatch entry)
 *   030d: mov 0x96, #0x00  ; DPX = 0x00
 *   0310: ret
 */
void nvme_util_get_status_flags(uint16_t reg_addr)
{
    /* This is essentially a register read dispatcher */
    /* For now, just read from the register address */
    /* The original uses a complex stack-based dispatch */
    (void)reg_addr;
    DPX = 0x00;
}

/*
 * nvme_util_get_error_flags - Dispatch function for error operations
 * Address: 0x0311-0x0321 (17 bytes)
 *
 * Similar to nvme_util_get_status_flags but sets:
 * - R0 = 0x1B (different dispatch parameter)
 * - DPX = 0x01 (different bank)
 *
 * Original disassembly:
 *   0311: push 0x08
 *   0313: mov a, #0x03
 *   0315: push 0xe0
 *   0317: push 0x82
 *   0319: push 0x83
 *   031b: mov 0x08, #0x1b
 *   031e: mov 0x96, #0x01
 *   0321: ret
 */
void nvme_util_get_error_flags(uint16_t reg_addr)
{
    (void)reg_addr;
    DPX = 0x01;
}

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

/*===========================================================================
 * Main Loop Handler Stubs
 * These are dispatch stubs that set DPTR and call nvme_util_get_status_flags
 * or nvme_util_get_error_flags
 *===========================================================================*/

/*
 * Handler at 0x04d0
 * Address: 0x04d0-0x04d4 (5 bytes)
 *
 * Calls nvme_util_get_status_flags with register 0xCE79
 *
 * Original disassembly:
 *   04d0: mov dptr, #0xce79
 *   04d3: ajmp 0x0300
 */
void handler_04d0(void)
{
    nvme_util_get_status_flags(0xCE79);
}

/*
 * phy_config_link_params - Configure PHY/link control registers
 * Address: 0x5284-0x52a6 (35 bytes)
 *
 * Configures PHY link parameters by manipulating registers in 0xC6xx region.
 * Sets specific bit patterns for link training/configuration.
 *
 * From ghidra.c FUN_CODE_5284:
 *   DAT_EXTMEM_c65b = DAT_EXTMEM_c65b & 0xf7 | 8;   // set bit 3
 *   DAT_EXTMEM_c656 = DAT_EXTMEM_c656 & 0xdf;       // clear bit 5
 *   DAT_EXTMEM_c65b = DAT_EXTMEM_c65b & 0xdf | 0x20; // set bit 5
 *   DAT_EXTMEM_c62d = DAT_EXTMEM_c62d & 0xe0 | 7;   // set low 3 bits to 7
 *
 * Original disassembly:
 *   5284: mov dptr, #0xc65b
 *   5287: movx a, @dptr
 *   5288: anl a, #0xf7
 *   528a: orl a, #0x08
 *   528c: movx @dptr, a
 *   528d: mov dptr, #0xc656
 *   5290: movx a, @dptr
 *   5291: anl a, #0xdf
 *   5293: movx @dptr, a
 *   5294: mov dptr, #0xc65b
 *   5297: movx a, @dptr
 *   5298: anl a, #0xdf
 *   529a: orl a, #0x20
 *   529c: movx @dptr, a
 *   529d: mov dptr, #0xc62d
 *   52a0: movx a, @dptr
 *   52a1: anl a, #0xe0
 *   52a3: orl a, #0x07
 *   52a5: movx @dptr, a
 *   52a6: ret
 */
void phy_config_link_params(void)
{
    REG_PHY_EXT_5B = (REG_PHY_EXT_5B & 0xF7) | 0x08;
    REG_PHY_EXT_56 = REG_PHY_EXT_56 & 0xDF;
    REG_PHY_EXT_5B = (REG_PHY_EXT_5B & 0xDF) | 0x20;
    REG_PHY_EXT_2D = (REG_PHY_EXT_2D & 0xE0) | 0x07;
}

/*
 * Handler at 0x04b2
 * Address: 0x04b2-0x04b6 (5 bytes)
 *
 * Calls nvme_util_get_status_flags with register 0xE971
 *
 * Original disassembly:
 *   04b2: mov dptr, #0xe971
 *   04b5: ajmp 0x0300
 */
void handler_04b2(void)
{
    nvme_util_get_status_flags(0xE971);
}

/*
 * Handler at 0x4fb6
 * Address: 0x4fb6-0x50da (292 bytes)
 *
 * Core polling and dispatch handler. Calls multiple sub-handlers and
 * polls for PHY status before continuing.
 *
 * From ghidra.c FUN_CODE_4fb6:
 *   - Calls various dispatch stubs (FUN_CODE_5305, 04b7, 04bc, 4be6, 032c, 0539, 04f8, 063d)
 *   - If DAT_EXTMEM_0ae3 != 0, clears bit 0 of 0xCC32
 *   - Polls DAT_EXTMEM_c6b3 until bits 4 or 5 are set
 *   - Calls more handlers (0462, 0435, 0340)
 *   - Sets DAT_EXTMEM_06e6 = 1
 */
void handler_4fb6(void)
{
    /* Call dispatch handlers */
    nvme_util_get_status_flags(0xD3CB);  /* FUN_CODE_5305 */
    nvme_util_get_status_flags(0xE597);  /* FUN_CODE_04b7 */
    nvme_util_get_status_flags(0xE14B);  /* FUN_CODE_04bc */
    nvme_util_get_status_flags(0x92C5);  /* FUN_CODE_032c */

    /* Check state flag and conditionally clear bit 0 of CPU exec status */
    if (REG_STATE_FLAG_0AE3 != 0) {
        REG_CPU_EXEC_STATUS = REG_CPU_EXEC_STATUS & 0xFE;
    }

    /* Poll PHY status register until bits 4 or 5 are set */
    while ((REG_PHY_EXT_B3 & 0x30) == 0);

    /* More dispatch calls */
    nvme_util_get_status_flags(0xBF8E);  /* FUN_CODE_0340 */

    /* Set flag indicating processing complete */
    REG_STATE_FLAG_06E6 = 1;
}

/*
 * Handler at 0x0327
 * Address: 0x0327-0x032a (5 bytes)
 *
 * Calls nvme_util_get_status_flags with register 0xB1CB
 * Sets DPX register as part of the dispatch system
 *
 * Original disassembly:
 *   0327: mov dptr, #0xb1cb
 *   032a: ajmp 0x0300
 */
void handler_0327(void)
{
    nvme_util_get_status_flags(0xB1CB);
}

/*
 * Handler at 0x0494
 * Address: 0x0494-0x0498 (5 bytes)
 *
 * Calls nvme_util_get_error_flags with register 0xE56F
 * Called when events & 0x81 is set
 *
 * Original disassembly:
 *   0494: mov dptr, #0xe56f
 *   0497: ajmp 0x0311
 */
void handler_0494(void)
{
    nvme_util_get_error_flags(0xE56F);
}

/*
 * Handler at 0x0606
 * Address: 0x0606-0x060a (5 bytes)
 *
 * Calls nvme_util_get_error_flags with register 0xB230
 *
 * Original disassembly:
 *   0606: mov dptr, #0xb230
 *   0609: ajmp 0x0311
 */
void handler_0606(void)
{
    nvme_util_get_error_flags(0xB230);
}

/*
 * Handler at 0x0589
 * Address: 0x0589-0x058d (5 bytes)
 *
 * Calls nvme_util_get_status_flags with register 0xD894
 *
 * Original disassembly:
 *   0589: mov dptr, #0xd894
 *   058c: ajmp 0x0300
 */
void handler_0589(void)
{
    nvme_util_get_status_flags(0xD894);
}

/*
 * Handler at 0x0525
 * Address: 0x0525-0x0529 (5 bytes)
 *
 * Calls nvme_util_get_status_flags with register 0xBAA0
 *
 * Original disassembly:
 *   0525: mov dptr, #0xbaa0
 *   0528: ajmp 0x0300
 */
void handler_0525(void)
{
    nvme_util_get_status_flags(0xBAA0);
}

/*===========================================================================
 * Interrupt Service Routines
 *===========================================================================*/

/*
 * External Interrupt 0 Handler
 * Address: 0x0e5b-0x1195 (826 bytes)
 *
 * This is the main USB/peripheral interrupt handler. It dispatches to various
 * sub-handlers based on interrupt status registers:
 *
 * Entry:
 *   0e5b-0e76: Push ACC, B, DPH, DPL, PSW, R0-R7
 *   0e65: Set PSW=0 (register bank 0)
 *
 * Dispatch checks:
 *   0e78: Read 0xC802, if bit 0 set -> ljmp 0x10e0
 *   0e82: Read 0x9101, if bit 5 set -> ljmp 0x0f2f
 *   0e8c: Read 0x9000, if bit 0 set -> ljmp 0x0f1c
 *   0e96-0efb: USB endpoint processing loop (0x37 < 0x20)
 *              Uses tables at 0x5a6a, 0x5b72
 *              Writes to 0x0a7b, 0x0a7c, 0x0af5
 *              Calls 0x5442
 *
 * Sub-handlers at various addresses (0x0f1c, 0x0f2f, 0x10e0, etc.)
 *
 * Exit:
 *   117b-1193: Pop R7-R0, PSW, DPL, DPH, B, ACC
 *   1195: RETI
 */
void ext0_isr(void) __interrupt(INT_EXT0) __using(1)
{
    uint8_t status;

    /* Check USB master interrupt status */
    status = REG_INT_USB_MASTER;
    if (status & 0x01) {
        /* USB master interrupt - handle at 0x10e0 path */
        goto usb_master_handler;
    }

    /* Check USB peripheral status */
    status = REG_USB_PERIPH_STATUS;
    if (status & 0x20) {
        /* Peripheral interrupt - handle at 0x0f2f path */
        goto peripheral_handler;
    }

    /* Check USB endpoint status */
    status = REG_USB_STATUS;
    if (status & 0x01) {
        /* USB endpoint interrupt - handle at 0x0f1c path */
        goto endpoint_handler;
    }

    /* USB endpoint processing loop */
    /* TODO: Implement table-driven endpoint dispatch */
    return;

usb_master_handler:
    /* Handle USB master events */
    status = REG_INT_SYSTEM;
    /* TODO: Implement USB master handling */
    return;

peripheral_handler:
    /* Handle peripheral events via REG_USB_PERIPH_STATUS */
    /* TODO: Implement peripheral handling */
    return;

endpoint_handler:
    /* Handle USB endpoint events */
    /* TODO: Implement endpoint handling */
    return;
}

/*
 * Timer 0 Interrupt Handler
 * Address: needs identification
 */
void timer0_isr(void) __interrupt(INT_TIMER0) __using(1)
{
    /* Placeholder */
}

/*
 * External Interrupt 1 Handler
 * Address: 0x4486-0x4531 (171 bytes)
 *
 * Handles NVMe, PCIe and system events via various status registers:
 *
 * Entry:
 *   4486-44a1: Push ACC, B, DPH, DPL, PSW, R0-R7
 *   4490: Set PSW=0 (register bank 0)
 *
 * Dispatch checks:
 *   44a3: Read 0xC806, if bit 0 set -> call 0x0520
 *   44ad: Read 0xCC33, if bit 2 set -> write 0x04 to 0xCC33, call 0x0390
 *   44ba: Read 0xC80A, if bit 6 set -> call 0x052f
 *   44c4: Read 0x09F9 & 0x83, if != 0:
 *         - if 0xC80A bit 5 set -> call 0x061a
 *         - if 0xC80A bit 4 set -> call 0x0593
 *         - if 0xEC06 bit 0 set -> handle NVMe/PCIe event
 *   450d: Read 0xC80A & 0x0F, if != 0 -> call 0x0570
 *   4510: Read 0xC806, if bit 4 set -> call 0x0642
 *
 * Exit:
 *   4517-452f: Pop R7-R0, PSW, DPL, DPH, B, ACC
 *   4531: RETI
 */
void ext1_isr(void) __interrupt(INT_EXT1) __using(1)
{
    uint8_t status;
    uint8_t events;

    /* Check system interrupt status bit 0 */
    status = REG_INT_SYSTEM;
    if (status & 0x01) {
        /* Call handler at 0x0520 */
        /* TODO: Implement handler */
    }

    /* Check CPU execution status 2 bit 2 */
    status = REG_CPU_EXEC_STATUS_2;
    if (status & 0x04) {
        REG_CPU_EXEC_STATUS_2 = 0x04;  /* Clear interrupt */
        /* Call handler at 0x0390 */
        /* TODO: Implement handler */
    }

    /* Check PCIe/NVMe status bit 6 */
    status = REG_INT_PCIE_NVME;
    if (status & 0x40) {
        /* Call handler at 0x052f */
        /* TODO: Implement handler */
    }

    /* Check event flags */
    events = REG_EVENT_FLAGS & 0x83;
    if (events != 0) {
        status = REG_INT_PCIE_NVME;

        if (status & 0x20) {
            /* Call handler at 0x061a */
            /* TODO: Implement handler */
        }

        if (status & 0x10) {
            /* Call handler at 0x0593 */
            /* TODO: Implement handler */
        }

        /* Check NVMe event status */
        if (REG_NVME_EVENT_STATUS & 0x01) {
            REG_NVME_EVENT_ACK = 0x01;  /* Acknowledge */
            /* Additional NVMe processing */
        }
    }

    /* Check for additional PCIe events */
    status = REG_INT_PCIE_NVME & 0x0F;
    if (status != 0) {
        /* Call handler at 0x0570 */
        /* TODO: Implement handler */
    }

    /* Check system status bit 4 */
    status = REG_INT_SYSTEM;
    if (status & 0x10) {
        /* Call handler at 0x0642 */
        /* TODO: Implement handler */
    }
}

/*
 * Timer 1 Interrupt Handler
 * Address: needs identification
 */
void timer1_isr(void) __interrupt(INT_TIMER1) __using(1)
{
    /* Placeholder */
}

/*
 * Serial Interrupt Handler
 * Address: needs identification
 *
 * Note: ASM2464PD uses dedicated UART, this may be unused
 */
void serial_isr(void) __interrupt(INT_SERIAL) __using(1)
{
    /* Placeholder - likely unused */
}
