/*
 * phy.c - PHY/Link Control Driver
 *
 * See drivers/phy.h for hardware documentation.
 */

#include "drivers/phy.h"
#include "drivers/uart.h"
#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

/*
 * phy_init_sequence - Full PHY initialization sequence
 * Address: 0xcb54-0xcb97 (68 bytes)
 *
 * Initializes USB and link PHY for operation. This is called during
 * system initialization to bring up the PHY.
 *
 * Sequence:
 * 1. Clear USB control 0x920C bits 0,1
 * 2. Set link config 0xC20C bit 6
 * 3. Clear link control 0xC208 bit 4
 * 4. Enable power 0x92C0 bit 0, 0x92C1 bit 0
 * 5. Set PHY power 0x92C5 bit 2
 * 6. Configure USB PHY 0x9241 bit 4, then bits 6,7
 *
 * Original disassembly:
 *   cb54: mov dptr, #0x920c   ; USB control
 *   cb57: movx a, @dptr
 *   cb58: anl a, #0xfd        ; clear bit 1
 *   cb5a: movx @dptr, a
 *   cb5b: movx a, @dptr
 *   cb5c: anl a, #0xfe        ; clear bit 0
 *   cb5e: movx @dptr, a
 *   cb5f: mov dptr, #0xc20c   ; PHY link config
 *   cb62: movx a, @dptr
 *   cb63: anl a, #0xbf        ; clear bit 6
 *   cb65: orl a, #0x40        ; set bit 6
 *   cb67: movx @dptr, a
 *   cb68: mov dptr, #0xc208   ; PHY link control
 *   cb6b: movx a, @dptr
 *   cb6c: anl a, #0xef        ; clear bit 4
 *   cb6e: movx @dptr, a
 *   cb6f: mov dptr, #0x92c0   ; Power control 0
 *   cb72: movx a, @dptr
 *   cb73: anl a, #0xfe        ; clear bit 0
 *   cb75: orl a, #0x01        ; set bit 0
 *   cb77: movx @dptr, a
 *   cb78: inc dptr            ; 0x92C1
 *   cb79: movx a, @dptr
 *   cb7a: anl a, #0xfe        ; clear bit 0
 *   cb7c: orl a, #0x01        ; set bit 0
 *   cb7e: movx @dptr, a
 *   cb7f: mov dptr, #0x92c5   ; Power control 5
 *   cb82: movx a, @dptr
 *   cb83: anl a, #0xfb        ; clear bit 2
 *   cb85: orl a, #0x04        ; set bit 2
 *   cb87: movx @dptr, a
 *   cb88: mov dptr, #0x9241   ; USB PHY config
 *   cb8b: movx a, @dptr
 *   cb8c: anl a, #0xef        ; clear bit 4
 *   cb8e: orl a, #0x10        ; set bit 4
 *   cb90: movx @dptr, a
 *   cb91: movx a, @dptr
 *   cb92: anl a, #0x3f        ; clear bits 6,7
 *   cb94: orl a, #0xc0        ; set bits 6,7
 *   cb96: movx @dptr, a
 *   cb97: ret
 */
void phy_init_sequence(void)
{
    uint8_t val;

    /* Clear USB control 0x920C bits 0,1 */
    val = REG_USB_CTRL_920C;
    val &= 0xFD;  /* Clear bit 1 */
    REG_USB_CTRL_920C = val;
    val = REG_USB_CTRL_920C;
    val &= 0xFE;  /* Clear bit 0 */
    REG_USB_CTRL_920C = val;

    /* Set PHY link config 0xC20C bit 6 */
    val = REG_PHY_LINK_CONFIG_C20C;
    val = (val & 0xBF) | 0x40;
    REG_PHY_LINK_CONFIG_C20C = val;

    /* Clear PHY link control 0xC208 bit 4 */
    val = REG_PHY_LINK_CTRL_C208;
    val &= 0xEF;
    REG_PHY_LINK_CTRL_C208 = val;

    /* Enable power 0x92C0 bit 0 */
    val = REG_POWER_ENABLE;
    val = (val & ~POWER_ENABLE_BIT) | POWER_ENABLE_BIT;
    REG_POWER_ENABLE = val;

    /* Enable clock 0x92C1 bit 0 */
    val = REG_CLOCK_ENABLE;
    val = (val & ~CLOCK_ENABLE_BIT) | CLOCK_ENABLE_BIT;
    REG_CLOCK_ENABLE = val;

    /* Set PHY power 0x92C5 bit 2 */
    val = REG_PHY_POWER;
    val = (val & ~PHY_POWER_ENABLE) | PHY_POWER_ENABLE;
    REG_PHY_POWER = val;

    /* Configure USB PHY 0x9241 bit 4 */
    val = REG_USB_PHY_CONFIG_9241;
    val = (val & 0xEF) | 0x10;
    REG_USB_PHY_CONFIG_9241 = val;

    /* Configure USB PHY 0x9241 bits 6,7 */
    val = REG_USB_PHY_CONFIG_9241;
    val = (val & 0x3F) | 0xC0;
    REG_USB_PHY_CONFIG_9241 = val;
}

/*
 * phy_config_link_params - Configure PHY link parameters
 * Address: 0x5284-0x52a6 (35 bytes)
 *
 * Sets up PHY extended registers for link training parameters.
 *
 * From ghidra.c FUN_CODE_5284:
 *   DAT_EXTMEM_c65b = DAT_EXTMEM_c65b & 0xf7 | 8;   // set bit 3
 *   DAT_EXTMEM_c656 = DAT_EXTMEM_c656 & 0xdf;       // clear bit 5
 *   DAT_EXTMEM_c65b = DAT_EXTMEM_c65b & 0xdf | 0x20; // set bit 5
 *   DAT_EXTMEM_c62d = DAT_EXTMEM_c62d & 0xe0 | 7;   // set low 3 bits to 7
 *
 * Original disassembly:
 *   5284: mov dptr, #0xc65b   ; PHY extended config
 *   5287: movx a, @dptr
 *   5288: anl a, #0xf7        ; clear bit 3
 *   528a: orl a, #0x08        ; set bit 3
 *   528c: movx @dptr, a
 *   528d: mov dptr, #0xc656   ; PHY extended config
 *   5290: movx a, @dptr
 *   5291: anl a, #0xdf        ; clear bit 5
 *   5293: movx @dptr, a
 *   5294: mov dptr, #0xc65b   ; PHY extended config
 *   5297: movx a, @dptr
 *   5298: anl a, #0xdf        ; clear bit 5
 *   529a: orl a, #0x20        ; set bit 5
 *   529c: movx @dptr, a
 *   529d: mov dptr, #0xc62d   ; PHY extended config
 *   52a0: movx a, @dptr
 *   52a1: anl a, #0xe0        ; clear bits 0-4
 *   52a3: orl a, #0x07        ; set bits 0-2 (lane config = 7)
 *   52a5: movx @dptr, a
 *   52a6: ret
 */
void phy_config_link_params(void)
{
    REG_PHY_EXT_5B = (REG_PHY_EXT_5B & ~PHY_EXT_ENABLE) | PHY_EXT_ENABLE;
    REG_PHY_EXT_56 = REG_PHY_EXT_56 & ~PHY_EXT_SIGNAL_CFG;
    REG_PHY_EXT_5B = (REG_PHY_EXT_5B & ~PHY_EXT_MODE) | PHY_EXT_MODE;
    REG_PHY_EXT_2D = (REG_PHY_EXT_2D & ~PHY_EXT_LANE_MASK) | PHY_EXT_LANE_MASK;
}

/*
 * phy_poll_link_ready - Poll PHY status for link ready
 * Address: 0x4fdb-0x4fe1 (poll loop in handler_4fb6)
 *
 * Polls PHY extended status register 0xC6B3 bits 4,5 until
 * at least one is set, indicating link is ready.
 *
 * Returns: non-zero if link ready (bits 4,5), 0 if not ready
 *
 * Original disassembly (poll loop):
 *   4fdb: mov dptr, #0xc6b3   ; PHY status
 *   4fde: movx a, @dptr
 *   4fdf: anl a, #0x30        ; mask bits 4,5
 *   4fe1: jz 0x4fdb           ; loop if zero
 */
uint8_t phy_poll_link_ready(void)
{
    uint8_t val = REG_PHY_EXT_B3;
    val &= PHY_EXT_LINK_READY;  /* Mask bits 4,5 */
    return val;
}

/*
 * phy_check_usb_state - Check USB PHY state from 0x91C0 bit 1
 * Address: 0x3031-0x303a (10 bytes)
 *
 * Checks if USB PHY bit 1 is set in 0x91C0, returns shifted result.
 * This is called from power_get_status_bit6 when suspended bit is set.
 *
 * Returns: 0 if PHY state not set, 1 if set
 *
 * Original disassembly:
 *   3031: mov dptr, #0x91c0   ; USB PHY control
 *   3034: movx a, @dptr
 *   3035: anl a, #0x02        ; mask bit 1
 *   3037: mov r7, a           ; save
 *   3038: clr c
 *   3039: rrc a               ; shift right (bit 1 -> bit 0)
 *   303a: jz 0x303f           ; jump if zero
 */
uint8_t phy_check_usb_state(void)
{
    uint8_t val = REG_USB_PHY_CTRL_91C0;
    val &= 0x02;  /* Mask bit 1 */
    val >>= 1;    /* Shift right */
    return val;
}

/* Forward declarations for helper functions called from init helpers */
extern void helper_ca51(void);        /* 0xCA51 - called from phy_mode_helper_c45b */
extern void helper_b77b(uint8_t r4, uint8_t r5, uint8_t r6, uint8_t r7);  /* 0xB77B */
extern void helper_bc18(uint8_t r5, uint8_t r7);  /* 0xBC18 */
extern void helper_e4a0(void);        /* 0xE4A0 */
extern void helper_9534(uint8_t r5, uint8_t r7);  /* 0x9534 */
extern void helper_dfd6(void);        /* 0xDFD6 */
extern void helper_e438(void);        /* 0xE438 */
extern void helper_e1fd(void);        /* 0xE1FD */
extern void helper_956a(void);        /* 0x956A */
extern uint8_t helper_959a(void);     /* 0x959A */

/*
 * pd_mode_init_94ca - Initialize PD mode register configuration
 * Address: 0x94CA-0x94E9 (32 bytes)
 *
 * Writes mode value to G_VENDOR_CTRL_07B9, then configures CC9x registers.
 *
 * Original disassembly:
 *   94ca: mov dptr, #0x07b9
 *   94cd: movx @dptr, a          ; write A to 0x07B9
 *   94ce: mov dptr, #0xcc98
 *   94d1: movx a, @dptr
 *   94d2: anl a, #0xf8           ; clear bits 0-2
 *   94d4: orl a, #0x06           ; set bits 1,2
 *   94d6: movx @dptr, a
 *   94d7: mov dptr, #0xcc9a
 *   94da: clr a
 *   94db: movx @dptr, a          ; write 0 to CC9A
 *   94dc: inc dptr               ; CC9B
 *   94dd: mov a, #0x50
 *   94df: movx @dptr, a          ; write 0x50 to CC9B
 *   94e0: mov dptr, #0xcc99
 *   94e3: mov a, #0x04
 *   94e5: movx @dptr, a          ; write 4 to CC99
 *   94e6: mov a, #0x02
 *   94e8: movx @dptr, a          ; write 2 to CC99
 *   94e9: ret
 */
static void pd_mode_init_94ca(uint8_t mode)
{
    uint8_t val;

    /* Write mode to G_VENDOR_CTRL_07B9 */
    G_VENDOR_CTRL_07B9 = mode;

    /* Configure REG_CPU_DMA_READY (0xCC98): clear bits 0-2, set bits 1,2 */
    val = REG_CPU_DMA_READY;
    val = (val & 0xF8) | 0x06;
    REG_CPU_DMA_READY = val;

    /* Write 0 to REG_XFER_DMA_DATA_LO (0xCC9A) */
    REG_XFER_DMA_DATA_LO = 0;

    /* Write 0x50 to REG_XFER_DMA_DATA_HI (0xCC9B) */
    REG_XFER_DMA_DATA_HI = 0x50;

    /* Write 4 then 2 to REG_XFER_DMA_CFG (0xCC99) */
    REG_XFER_DMA_CFG = 0x04;
    REG_XFER_DMA_CFG = 0x02;
}

/*
 * phy_mode_helper_c45b - PHY mode finalization helper
 * Address: 0xC45B-0xC464 (10 bytes)
 *
 * Called after pd_mode_init_94ca. Decrements A (which is 2), writes to CC99
 * and G_TLP_BASE_LO, then calls helper_ca51.
 *
 * Original disassembly:
 *   c45b: dec a                  ; A = 1
 *   c45c: movx @dptr, a          ; write 1 to CC99
 *   c45d: mov dptr, #0x0ae1
 *   c460: movx @dptr, a          ; write 1 to G_TLP_BASE_LO
 *   c461: lcall 0xca51
 *   c464: ret
 */
static void phy_mode_helper_c45b(void)
{
    /* After pd_mode_init_94ca, the value to write is 1 (decremented from 2) */
    REG_XFER_DMA_CFG = 0x01;
    G_TLP_BASE_LO = 0x01;
    /* TODO: Call helper_ca51() when implemented */
    /* helper_ca51(); */
}

/*
 * timer_setup_e592 - Configure CPU timer registers
 * Address: 0xE592-0xE5A2 (17 bytes)
 *
 * Writes parameters to CC82, CC83, then configures CC81.
 *
 * Original disassembly:
 *   e592: mov dptr, #0xcc82
 *   e595: mov a, r6              ; param_hi
 *   e596: movx @dptr, a
 *   e597: inc dptr               ; CC83
 *   e598: mov a, r7              ; param_lo
 *   e599: movx @dptr, a
 *   e59a: mov dptr, #0xcc81
 *   e59d: lcall 0x94e3           ; write 4, then 2 to CC81
 *   e5a0: dec a                  ; A = 1
 *   e5a1: movx @dptr, a          ; write 1 to CC81
 *   e5a2: ret
 */
static void timer_setup_e592(uint8_t param_hi, uint8_t param_lo)
{
    /* Write param_hi to CC82 */
    REG_CPU_CTRL_CC82 = param_hi;

    /* Write param_lo to CC83 */
    REG_CPU_CTRL_CC83 = param_lo;

    /* Write 4, then 2, then 1 to CC81 (REG_CPU_INT_CTRL) */
    REG_CPU_INT_CTRL = 0x04;
    REG_CPU_INT_CTRL = 0x02;
    REG_CPU_INT_CTRL = 0x01;
}

/*
 * helper_e0f8 - DMA/Timer helper
 * Address: 0xE0F8-0xE119 (34 bytes)
 *
 * Configures CC91, CC90, CC92, CC93 registers.
 *
 * Original disassembly:
 *   e0f8: mov dptr, #0xcc91
 *   e0fb: lcall 0x94e3           ; write 4, then 2 to CC91
 *   e0fe: lcall 0x956a           ; helper
 *   e101: mov dptr, #0xcc90
 *   e104: lcall 0x959a           ; helper, returns val in A
 *   e107: orl a, #0x05           ; set bits 0,2
 *   e109: movx @dptr, a          ; write to CC90
 *   e10a: mov dptr, #0xcc92
 *   e10d: clr a
 *   e10e: movx @dptr, a          ; write 0 to CC92
 *   e10f: inc dptr               ; CC93
 *   e110: mov a, #0xc8
 *   e112: movx @dptr, a          ; write 0xC8 to CC93
 *   e113: mov dptr, #0xcc91
 *   e116: mov a, #0x01
 *   e118: movx @dptr, a          ; write 1 to CC91
 *   e119: ret
 */
static void helper_e0f8_impl(void)
{
    uint8_t val;

    /* Write 4, then 2 to CC91 */
    REG_CPU_DMA_INT = 0x04;
    REG_CPU_DMA_INT = 0x02;

    /* TODO: call helper_956a() when implemented */
    /* helper_956a(); */

    /* Read/modify CC90 - simplified since helper_959a is not implemented */
    val = REG_CPU_DMA_CTRL_CC90;
    val |= 0x05;  /* Set bits 0,2 */
    REG_CPU_DMA_CTRL_CC90 = val;

    /* Write 0 to CC92 */
    REG_CPU_DMA_DATA_LO = 0;

    /* Write 0xC8 to CC93 */
    REG_CPU_DMA_DATA_HI = 0xC8;

    /* Write 1 to CC91 */
    REG_CPU_DMA_INT = 0x01;
}

/*
 * init_e44d - Additional PD initialization
 * Address: 0xE44D-0xE45F (19 bytes)
 *
 * Sets up parameters and calls initialization helpers.
 *
 * Original disassembly:
 *   e44d: mov r7, #0x00
 *   e44f: mov r6, #0x80
 *   e451: mov r5, #0x01
 *   e453: mov r4, #0x00
 *   e455: lcall 0xb77b           ; helper_b77b(0, 1, 0x80, 0)
 *   e458: mov a, #0x04
 *   e45a: movx @dptr, a          ; write 4 to DPTR (set by b77b)
 *   e45b: mov r5, #0x03
 *   e45d: mov r7, #0x03
 *   e45f: ljmp 0xbc18            ; tail call helper_bc18(3, 3)
 */
static void init_e44d(void)
{
    /* TODO: Call helper_b77b(0, 1, 0x80, 0) when implemented */
    /* helper_b77b(0, 1, 0x80, 0); */

    /* For now, write 4 to a default register (placeholder) */
    /* The actual register depends on helper_b77b's output */

    /* TODO: Call helper_bc18(3, 3) when implemented */
    /* helper_bc18(3, 3); */
}

/*
 * mode_0x3a_init_e239 - Mode 0x3A specific initialization
 * Address: 0xE239-0xE256 (30 bytes)
 *
 * Called for USB mode 0x3A. Initializes various state.
 *
 * Original disassembly:
 *   e239: lcall 0xe4a0           ; helper
 *   e23c: clr a
 *   e23d: mov r5, a              ; r5 = 0
 *   e23e: mov r7, #0x0e          ; r7 = 14
 *   e240: lcall 0x9534           ; helper_9534(0, 14)
 *   e243: mov dptr, #0x07ba
 *   e246: mov a, #0x0e
 *   e248: movx @dptr, a          ; G_PD_INIT_07BA = 14
 *   e249: lcall 0xdfd6           ; helper
 *   e24c: mov a, r7
 *   e24d: jz 0xe256              ; if r7 == 0, return
 *   e24f: mov r7, #0x10
 *   e251: mov r6, #0x27
 *   e253: lcall 0xe592           ; timer_setup_e592(0x27, 0x10)
 *   e256: ret
 */
static void mode_0x3a_init_e239(void)
{
    /* TODO: Call helper_e4a0() when implemented */
    /* helper_e4a0(); */

    /* TODO: Call helper_9534(0, 14) when implemented */
    /* helper_9534(0, 14); */

    /* Write 14 to G_PD_INIT_07BA */
    G_PD_INIT_07BA = 0x0E;

    /* TODO: Call helper_dfd6() when implemented */
    /* helper_dfd6(); */

    /* Conditional timer setup - for now always call */
    timer_setup_e592(0x27, 0x10);
}

/*
 * usb_mode_update_e3f6 - Update USB mode state
 * Address: 0xE3F6-0xE40C (23 bytes)
 *
 * Saves mode value and conditionally updates USB mode.
 *
 * Original disassembly:
 *   e3f6: mov dptr, #0x0aa2
 *   e3f9: mov a, r7              ; mode value (0xFF for common path)
 *   e3fa: movx @dptr, a          ; G_STATE_PARAM_0AA2 = mode
 *   e3fb: lcall 0xe438           ; helper
 *   e3fe: mov a, r7
 *   e3ff: jz 0xe40c              ; if r7 == 0, return
 *   e401: mov dptr, #0x0aa2
 *   e404: movx a, @dptr          ; read G_STATE_PARAM_0AA2
 *   e405: mov dptr, #0x7000
 *   e408: movx @dptr, a          ; write to G_FLASH_BUF_BASE (USB mode)
 *   e409: lcall 0xe1fd           ; helper
 *   e40c: ret
 */
static void usb_mode_update_e3f6(uint8_t mode)
{
    /* Write mode to G_STATE_PARAM_0AA2 */
    G_STATE_PARAM_0AA2 = mode;

    /* TODO: Call helper_e438() when implemented */
    /* helper_e438(); */

    /* Conditional update - for non-zero mode, update USB mode */
    if (mode != 0) {
        G_FLASH_BUF_BASE = G_STATE_PARAM_0AA2;
        /* TODO: Call helper_e1fd() when implemented */
        /* helper_e1fd(); */
    }
}

/*
 * pd_internal_state_init_b806 - Initialize PD state and print message
 * Address: 0xB806-0xB85F (90 bytes)
 *
 * Prints "[InternalPD_StateInit]" to UART and initializes PD-related globals.
 * Called when REG_FLASH_READY_STATUS bit 5 is set.
 *
 * Original disassembly:
 *   b806: mov r3, #0xff          ; string length (0xFF = null terminated)
 *   b808: mov r2, #0x2a          ; string addr high
 *   b80a: mov r1, #0x01          ; string addr low (0x2A01)
 *   b80c: lcall 0x53fa           ; uart_print_string
 *   b80f: clr a
 *   b810: mov dptr, #0x07b4      ; clear G_PD_STATE_07B4
 *   b813: movx @dptr, a
 *   b814: inc dptr               ; clear 0x07B5
 *   b815: movx @dptr, a
 *   b816: mov dptr, #0x07c0      ; clear G_CMD_ADDR_LO
 *   b819: movx @dptr, a
 *   b81a: inc dptr               ; clear 0x07C1
 *   b81b: movx @dptr, a
 *   ... (continues clearing more globals)
 */
static void pd_internal_state_init_b806(void)
{
    /* Print "[InternalPD_StateInit]" - string is at code address 0x2A00 */
    uart_puts("[InternalPD_StateInit]");

    /* Clear PD state variables */
    G_PD_STATE_07B4 = 0;      /* 0x07B4 */
    G_PD_STATE_07B5 = 0;      /* 0x07B5 */
    G_CMD_ADDR_LO = 0;        /* 0x07C0 */
    G_CMD_SLOT_C1 = 0;        /* 0x07C1 */
    G_CMD_STATUS = 0;         /* 0x07C4 */
    G_CMD_WORK_C2 = 0;        /* 0x07C2 */
    G_CMD_ADDR_HI = 0;        /* 0x07BF */
    G_PD_STATE_07BE = 0;      /* 0x07BE */
    G_PD_STATE_07E0 = 0;      /* 0x07E0 */
    G_PD_INIT_07BA = 1;       /* 0x07BA = 1 (set init flag) */

    /* Set default PD mode */
    G_PD_MODE_07D2 = 0x01;

    /* Check 0x07DB and conditionally set 0x07C7 */
    if (G_PD_COUNTER_07DB == 0) {
        G_CMD_WORK_C7 = 0x02;
    }
    G_PD_COUNTER_07DB = 0;
}

/*
 * Helper: 0x967c - Set bit 6 of register at current DPTR
 * Reads @DPTR, clears bit 6, sets bit 6, writes back
 */
static void helper_set_bit6(__xdata uint8_t *reg)
{
    uint8_t val = *reg;
    val = (val & 0xBF) | 0x40;
    *reg = val;
}

/*
 * Helper: 0x955b - Set bit 0 of register at current DPTR
 * Reads @DPTR, clears bit 0, sets bit 0, writes back
 */
static void helper_set_bit0(__xdata uint8_t *reg)
{
    uint8_t val = *reg;
    val = (val & 0xFE) | 0x01;
    *reg = val;
}

/*
 * Helper: 0x9574 - Reset DMA controller
 * Clears CC88 bits 0-2, writes 0 to CC8A
 */
static void helper_dma_reset_9574(void)
{
    REG_XFER_DMA_CTRL &= 0xF8;  /* Clear bits 0-2 */
    REG_XFER_DMA_ADDR_LO = 0;
}

/*
 * Helper: 0x94a3 - Start DMA with value (inc DPTR version)
 * Writes value to CC8B, then writes 1 to CC89 to start
 */
static void helper_dma_start_94a3(uint8_t value)
{
    REG_XFER_DMA_ADDR_HI = value;
    REG_XFER_DMA_CMD = XFER_DMA_CMD_START;
}

/*
 * Helper: 0x95a2 - Clear DMA completion flag
 * Writes 2 to CC89
 */
static void helper_dma_clear_95a2(void)
{
    REG_XFER_DMA_CMD = XFER_DMA_CMD_DONE;
}

/*
 * Helper: 0x9666 - Write to E420 and read E409 masked
 * Writes value to E420, reads E409 & 0xF1, returns value
 */
static uint8_t helper_e420_write_9666(uint8_t value)
{
    REG_CMD_TRIGGER = value;
    return REG_CMD_CTRL_E409 & 0xF1;
}

/*
 * Helper: 0x9545 - Extract bit 7 to bit 0
 * swap a, rrc 3x, AND 0x01
 * This extracts bit 7: swap puts bit 7 in bit 3, then rrc 3x puts it in bit 0
 */
static uint8_t helper_extract_bit7_9545(uint8_t value)
{
    /* swap moves bits 4-7 to bits 0-3, then shift right 3 extracts bit 7 as bit 0 */
    return ((value >> 4) >> 3) & 0x01;
}

/*
 * Helper: 0xd7fd - PD command config initialization
 * Address: 0xD7FD-0xD83A (62 bytes)
 *
 * Sets bit 7 of E40B, then if bit 7 sticks, configures E401, E406, E407, E408
 *
 * Original disassembly:
 *   d7fd: mov dptr, #0xe40b
 *   d800: movx a, @dptr
 *   d801: anl a, #0x7f
 *   d803: orl a, #0x80
 *   d805: movx @dptr, a       ; E40B = (E40B & 0x7F) | 0x80
 *   d806: movx a, @dptr       ; re-read E40B
 *   d807: anl a, #0x80        ; check bit 7
 *   d809: lcall 0x9545        ; extract bit 7 to bit 0
 *   d80c: jz 0xd83a           ; if 0, return
 *   d80e-d81c: Configure E401 (two read-modify-writes)
 *   d81d-d82b: Configure E406 (two read-modify-writes)
 *   d82c-d832: Configure E407
 *   d833-d839: Configure E408
 *   d83a: ret
 */
static void helper_pd_cmd_config_d7fd(void)
{
    /* d7fd-d805: Set bit 7 of E40B */
    REG_CMD_CONFIG = (REG_CMD_CONFIG & 0x7F) | 0x80;

    /* d806-d80c: Read E40B, check if bit 7 is set */
    if (helper_extract_bit7_9545(REG_CMD_CONFIG & 0x80) == 0) {
        return;  /* Bit 7 not set, return */
    }

    /* d80e-d81c: Configure E401 (two separate read-modify-writes) */
    REG_CMD_CTRL_E401 = (REG_CMD_CTRL_E401 & 0xF8) | 0x04;
    REG_CMD_CTRL_E401 = (REG_CMD_CTRL_E401 & 0x07) | 0xB0;

    /* d81d-d82b: Configure E406 (two separate read-modify-writes) */
    REG_CMD_CTRL_E406 = (REG_CMD_CTRL_E406 & 0xF0) | 0x06;
    REG_CMD_CTRL_E406 = (REG_CMD_CTRL_E406 & 0x0F) | 0xA0;

    /* d82c-d832: Configure E407 */
    REG_CMD_CTRL_E407 = (REG_CMD_CTRL_E407 & 0xE0) | 0x15;

    /* d833-d839: Configure E408 */
    REG_CMD_CTRL_E408 = (REG_CMD_CTRL_E408 & 0xE0) | 0x1C;
}

/*
 * pd_usb_init_b02f - PD/USB Initialization Helper
 * Address: 0xB02F-0xB0FD (207 bytes)
 *
 * Performs extensive register configuration for PD/USB initialization.
 * Called when REG_FLASH_READY_STATUS bit 5 is set.
 *
 * Original disassembly:
 *   b02f: mov dptr, #0xe40b      ; REG_CMD_CONFIG
 *   b032: lcall 0x967c           ; set bit 6 of E40B
 *   b035: mov dptr, #0xe40a
 *   b038: mov a, #0x0f
 *   b03a: movx @dptr, a          ; write 0x0F to E40A
 *   b03b-b045: clear bits 0,1 of E413
 *   b046-b04c: clear bit 7 of E400
 *   b04d: lcall 0x9574           ; reset DMA
 *   b050-b052: lcall 0x94a3(0x0a); start DMA
 *   b055-b059: poll CC89 bit 1   ; wait for DMA complete
 *   b05c: lcall 0x95a2           ; clear completion flag
 *   b05f-b062: set bit 0 of E40B
 *   b065-b06b: reset DMA, start with 0x3C
 *   b06e-b072: poll CC89 bit 1
 *   b075: lcall 0x95a2           ; clear completion flag
 *   b078-b084: poll E402 bit 3
 *   ... (more register configuration)
 *   b0fd: ret
 */
static void pd_usb_init_b02f(void)
{
    /* b02f-b032: Set bit 6 of REG_CMD_CONFIG (E40B) */
    helper_set_bit6(&REG_CMD_CONFIG);

    /* b035-b03a: Write 0x0F to REG_CMD_CFG_E40A */
    REG_CMD_CFG_E40A = 0x0F;

    /* b03b-b041: Clear bit 0 of E413 */
    REG_CMD_CFG_E413 &= 0xFE;

    /* b042-b045: Clear bit 1 of E413 */
    REG_CMD_CFG_E413 &= 0xFD;

    /* b046-b04c: Clear bit 7 of E400 */
    REG_CMD_CTRL_E400 &= 0x7F;

    /* b04d: Reset DMA controller */
    helper_dma_reset_9574();

    /* b050-b052: Start DMA with value 0x0A */
    helper_dma_start_94a3(0x0A);

    /* b055-b059: Poll CC89 bit 1 until set (DMA complete) */
    while (!(REG_XFER_DMA_CMD & XFER_DMA_CMD_DONE)) {
        /* Wait for DMA ready */
    }

    /* b05c: Clear DMA completion flag */
    helper_dma_clear_95a2();

    /* b05f-b062: Set bit 0 of E40B */
    helper_set_bit0(&REG_CMD_CONFIG);

    /* b065: Reset DMA controller again */
    helper_dma_reset_9574();

    /* b068-b06b: Start DMA with value 0x3C */
    helper_dma_start_94a3(0x3C);

    /* b06e-b072: Poll CC89 bit 1 until set (DMA complete) */
    while (!(REG_XFER_DMA_CMD & XFER_DMA_CMD_DONE)) {
        /* Wait for DMA ready */
    }

    /* b075: Clear DMA completion flag */
    helper_dma_clear_95a2();

    /* b078-b084: Poll E402 bit 3 until clear */
    while (REG_CMD_STATUS_E402 & 0x08) {
        /* Wait for command not busy */
    }

    /* b086-b08c: Clear bit 0 of E409 */
    REG_CMD_CTRL_E409 &= 0xFE;

    /* b08d: Set bit 6 of E409 */
    helper_set_bit6(&REG_CMD_CTRL_E409);

    /* b090-b097: Write 0x40 to E420, read E409 & 0xF1, OR with 0x06, write to E409 */
    REG_CMD_CTRL_E409 = helper_e420_write_9666(0x40) | 0x06;

    /* b098-b09b: Set bit 6 of E400 */
    helper_set_bit6(&REG_CMD_CTRL_E400);

    /* b09e-b0a3: Write 0xA1 to E411 */
    REG_CMD_CFG_E411 = 0xA1;

    /* b0a4-b0a7: Write 0x79 to E412 */
    REG_CMD_CFG_E412 = 0x79;

    /* b0a8-b0b0: Read E400, AND 0xC3, OR 0x3C, write back */
    REG_CMD_CTRL_E400 = (REG_CMD_CTRL_E400 & 0xC3) | 0x3C;

    /* b0b1-b0b7: Clear bit 7 of E409 */
    REG_CMD_CTRL_E409 &= 0x7F;

    /* b0b8-b0c0: Toggle bit 5 of C809 (clear then set) */
    REG_INT_CTRL = (REG_INT_CTRL & 0xDF) | 0x20;

    /* b0c1: Call helper 0xd7fd */
    helper_pd_cmd_config_d7fd();

    /* b0c4-b0c9: Write 0x8A to E40E */
    REG_CMD_CFG_E40E = 0x8A;

    /* b0ca-b0d5: Poll E302 until bits 6,7 are set */
    while ((REG_PHY_MODE_E302 & 0xC0) == 0) {
        /* Wait for PHY ready */
    }

    /* b0d7-b0df: Set bit 7 of E400 */
    REG_CMD_CTRL_E400 = (REG_CMD_CTRL_E400 & 0x7F) | 0x80;

    /* b0e0-b0e6: Clear bit 0 of E40B */
    REG_CMD_CONFIG &= 0xFE;

    /* b0e7-b0ed: Clear bit 4 of E66A */
    REG_PD_CTRL_E66A &= 0xEF;

    /* b0ee-b0f3: Write 0x28 to E40D */
    REG_CMD_CFG_E40D = 0x28;

    /* b0f4-b0fc: Set bits 5,6 of E413, clear bit 4 */
    REG_CMD_CFG_E413 = (REG_CMD_CFG_E413 & 0x8F) | 0x60;
}

/*
 * phy_register_config - PD/PHY Register Configuration
 * Address: 0x050C-0x050F (dispatch) -> 0xC3FA-0xC45A (97 bytes)
 *
 * Called from main loop at 0x1FAD. Checks REG_FLASH_READY_STATUS bit 5
 * and if set, initializes PD state and prints "[InternalPD_StateInit]".
 *
 * Original disassembly:
 *   c3fa: mov dptr, #0xe795      ; REG_FLASH_READY_STATUS
 *   c3fd: movx a, @dptr          ; read status
 *   c3fe: jnb 0xe0.5, 0xc45a     ; if bit 5 NOT set, skip to return
 *   c401: lcall 0xb02f           ; PD USB initialization helper
 *   c404: lcall 0xb806           ; pd_internal_state_init (prints message)
 *   c407: lcall 0xe44d           ; additional initialization
 *   c40a: mov dptr, #0x7000      ; check USB mode
 *   c40d: movx a, @dptr
 *   c40e: cjne a, #0x3a, 0xc429  ; branch on mode value
 *   c411-c41a: mode 0x3A - set 07B9=1, 07B5=1
 *   c41b: lcall 0xe239           ; mode 0x3A specific init
 *   c41e-c424: print "[Internal_StateInit_1]" (string at 0x2A18)
 *   c427: sjmp 0xc44b            ; goto common path
 *   c429: cjne a, #0x3b, 0xc43b  ; check mode 0x3B
 *   c431: mov a, #0x02
 *   c433: lcall 0x94ca           ; pd_mode_init_94ca(2)
 *   c436: lcall 0xc45b           ; phy_mode_helper_c45b()
 *   c439: sjmp 0xc44b            ; goto common path
 *   c43b: cjne a, #0x3c, 0xc450  ; check mode 0x3C
 *   c443: mov a, #0x03
 *   c445: lcall 0x94ca           ; pd_mode_init_94ca(3)
 *   c448: lcall 0xc45b           ; phy_mode_helper_c45b()
 *   c44b: mov r7, #0xff          ; common path - mode = 0xFF
 *   c44d: ljmp 0xe3f6            ; usb_mode_update_e3f6(0xFF)
 *   c450: mov r7, #0x9c          ; else branch
 *   c452: mov r6, #0x18
 *   c454: lcall 0xe592           ; timer_setup_e592(0x18, 0x9C)
 *   c457: lcall 0xe0f8           ; helper_e0f8_impl()
 *   c45a: ret
 */
void phy_register_config(void)
{
    uint8_t flash_status;
    uint8_t usb_mode;

    /* Read REG_FLASH_READY_STATUS (0xE795) */
    flash_status = REG_FLASH_READY_STATUS;

    /* Check bit 5 - if NOT set, return immediately (c3fe: jnb 0xe0.5, 0xc45a) */
    if (!(flash_status & 0x20)) {
        return;
    }

    /* Call PD USB initialization helper (0xB02F) */
    pd_usb_init_b02f();

    /* Initialize PD state and print "[InternalPD_StateInit]" (0xB806) */
    pd_internal_state_init_b806();

    /* Additional initialization (0xE44D) */
    init_e44d();

    /* Read USB mode from G_FLASH_BUF_BASE (0x7000) */
    usb_mode = G_FLASH_BUF_BASE;

    /* Branch on USB mode value */
    if (usb_mode == 0x3A) {
        /* Mode 0x3A: Set globals and call mode-specific init */
        G_VENDOR_CTRL_07B9 = 0x01;
        G_PD_STATE_07B5 = 0x01;

        /* Call mode 0x3A specific initialization (0xE239) */
        mode_0x3a_init_e239();

        /* Print "[Internal_StateInit_1]" - string at 0x2A18 */
        uart_puts("[Internal_StateInit_1]");

        /* Fall through to common path (0xC44B) */
        usb_mode_update_e3f6(0xFF);

    } else if (usb_mode == 0x3B) {
        /* Mode 0x3B: Initialize with mode value 2 */
        pd_mode_init_94ca(0x02);
        phy_mode_helper_c45b();

        /* Fall through to common path (0xC44B) */
        usb_mode_update_e3f6(0xFF);

    } else if (usb_mode == 0x3C) {
        /* Mode 0x3C: Initialize with mode value 3 */
        pd_mode_init_94ca(0x03);
        phy_mode_helper_c45b();

        /* Fall through to common path (0xC44B) */
        usb_mode_update_e3f6(0xFF);

    } else {
        /* Default mode: Timer setup and DMA configuration */
        timer_setup_e592(0x18, 0x9C);
        helper_e0f8_impl();
        /* Return without calling usb_mode_update_e3f6 */
    }
}

/*===========================================================================
 * PCIe/PHY Lane Configuration Functions (eGPU Priority)
 *===========================================================================*/

/* Forward declarations for helpers */
extern void pcie_lane_config_helper(uint8_t); /* 0xc089 */

/*
 * pcie_save_ctrl_state - Save PCIe control bit 1 state
 * Address: 0xe84d-0xe85b (15 bytes)
 *
 * Saves bit 1 of REG_PCIE_CTRL_B402 to G_PCIE_CTRL_SAVE_0B44.
 * Then reads B402, clears bit 1, and writes back.
 *
 * Original disassembly:
 *   e84d: mov dptr, #0xb402
 *   e850: movx a, @dptr
 *   e851: anl a, #0x02        ; mask bit 1
 *   e853: mov dptr, #0x0b44
 *   e856: movx @dptr, a       ; save to 0x0b44
 *   e857: lcall 0xccac        ; read B402 & 0xfd
 *   e85a: movx @dptr, a       ; store result
 *   e85b: ret
 */
void pcie_save_ctrl_state(void)
{
    uint8_t val;

    /* Save bit 1 of B402 */
    val = REG_PCIE_CTRL_B402;
    G_PCIE_CTRL_SAVE_0B44 = val & PCIE_CTRL_B402_BIT1;

    /* Read B402, clear bit 1, store */
    val = REG_PCIE_CTRL_B402;
    G_PCIE_CTRL_SAVE_0B44 = val & ~PCIE_CTRL_B402_BIT1;
}

/*
 * pcie_restore_ctrl_state - Restore PCIe control bit 1 state
 * Address: 0xe85c-0xe868 (13 bytes)
 *
 * If saved state (0x0b44) is non-zero, reads B402, sets bit 1, writes back.
 *
 * Original disassembly:
 *   e85c: mov dptr, #0x0b44
 *   e85f: movx a, @dptr
 *   e860: jz 0xe868           ; if zero, return
 *   e862: lcall 0xccac        ; read B402 & 0xfd
 *   e865: orl a, #0x02        ; set bit 1
 *   e867: movx @dptr, a       ; write to 0x0b44
 *   e868: ret
 */
void pcie_restore_ctrl_state(void)
{
    uint8_t val;

    if (G_PCIE_CTRL_SAVE_0B44 != 0) {
        val = REG_PCIE_CTRL_B402;
        val = (val & ~PCIE_CTRL_B402_BIT1) | PCIE_CTRL_B402_BIT1;
        G_PCIE_CTRL_SAVE_0B44 = val;
    }
}

/*
 * pcie_lane_config - Configure PCIe lane parameters
 * Address: 0xd436-0xd47e (73 bytes)
 *
 * Configures PCIe lane settings for USB4/Thunderbolt tunneling.
 * This is critical for eGPU passthrough functionality.
 *
 * Parameters:
 *   lane_mask: Lane configuration mask (0x0F = all lanes enabled)
 *
 * Algorithm:
 *   1. Save param to G_FLASH_ERROR_0 (0x0aa8)
 *   2. Call pcie_save_ctrl_state (0xe84d) - save B402 bit 1
 *   3. Reload param, call pcie_lane_config_helper (0xc089)
 *   4. If param != 0x0F, set bit 0 of REG_PCIE_TUNNEL_CTRL (0xb401)
 *   5. Call pcie_restore_ctrl_state (0xe85c)
 *   6. Read param, mask with 0x0E, merge into REG_PCIE_LANE_CONFIG low nibble
 *   7. Read REG_PCIE_LINK_PARAM_B404, XOR with 0x0F, swap nibbles,
 *      merge into REG_PCIE_LANE_CONFIG high nibble
 *
 * Original disassembly:
 *   d436: mov dptr, #0x0aa8
 *   d439: mov a, r7
 *   d43a: movx @dptr, a       ; save param
 *   d43b: lcall 0xe84d        ; pcie_save_ctrl_state
 *   d43e: mov dptr, #0x0aa8
 *   d441: movx a, @dptr       ; reload param
 *   d442: mov r7, a
 *   d443: lcall 0xc089        ; pcie_lane_config_helper
 *   d446: mov dptr, #0x0aa8
 *   d449: movx a, @dptr
 *   d44a: xrl a, #0x0f        ; XOR with 0x0F
 *   d44c: jz 0xd458           ; if result is 0x0F, skip
 *   d44e: mov dptr, #0xb401
 *   d451: lcall 0xcc8b        ; set bit 0
 *   d454: movx a, @dptr
 *   d455: anl a, #0xfe        ; clear bit 0
 *   d457: movx @dptr, a
 *   d458: lcall 0xe85c        ; pcie_restore_ctrl_state
 *   d45b: mov dptr, #0x0aa8
 *   d45e: movx a, @dptr
 *   d45f: anl a, #0x0e        ; mask bits 1-3
 *   d461: mov r7, a
 *   d462: mov dptr, #0xb436
 *   d465: movx a, @dptr
 *   d466: anl a, #0xf0        ; keep high nibble
 *   d468: orl a, r7           ; merge low nibble
 *   d469: movx @dptr, a
 *   d46a: mov dptr, #0xb404
 *   d46d: movx a, @dptr
 *   d46e: anl a, #0x0f        ; mask low nibble
 *   d470: xrl a, #0x0f        ; invert
 *   d472: swap a              ; swap nibbles
 *   d473: anl a, #0xf0        ; keep high nibble
 *   d475: mov r7, a
 *   d476: mov dptr, #0xb436
 *   d479: movx a, @dptr
 *   d47a: anl a, #0x0f        ; keep low nibble
 *   d47c: orl a, r7           ; merge high nibble
 *   d47d: movx @dptr, a
 *   d47e: ret
 */
void pcie_lane_config(uint8_t lane_mask)
{
    uint8_t val, val2;

    /* Save parameter */
    G_FLASH_ERROR_0 = lane_mask;

    /* Save PCIe control state */
    pcie_save_ctrl_state();

    /* Reload param and call lane config helper */
    val = G_FLASH_ERROR_0;
    pcie_lane_config_helper(val);

    /* If param != 0x0F, configure tunnel control */
    val = G_FLASH_ERROR_0;
    if ((val ^ 0x0F) != 0) {
        /* Set then clear bit 0 of tunnel control */
        REG_PCIE_TUNNEL_CTRL = (REG_PCIE_TUNNEL_CTRL & ~PCIE_TUNNEL_ENABLE) | PCIE_TUNNEL_ENABLE;
        val = REG_PCIE_TUNNEL_CTRL;
        val &= ~PCIE_TUNNEL_ENABLE;
        REG_PCIE_TUNNEL_CTRL = val;
    }

    /* Restore PCIe control state */
    pcie_restore_ctrl_state();

    /* Configure lane config register - low nibble */
    val = G_FLASH_ERROR_0;
    val &= 0x0E;  /* Mask bits 1-3 */
    val2 = REG_PCIE_LANE_CONFIG;
    val2 = (val2 & PCIE_LANE_CFG_HI_MASK) | val;
    REG_PCIE_LANE_CONFIG = val2;

    /* Configure lane config register - high nibble from B404 */
    val = REG_PCIE_LINK_PARAM_B404;
    val &= PCIE_LINK_PARAM_MASK;  /* Mask low nibble */
    val ^= 0x0F;                   /* Invert */
    val = (val << 4);              /* Swap to high nibble */
    val2 = REG_PCIE_LANE_CONFIG;
    val2 = (val2 & PCIE_LANE_CFG_LO_MASK) | val;
    REG_PCIE_LANE_CONFIG = val2;
}

/*
 * phy_link_training - Configure PHY for PCIe link training
 * Address: 0xD702-0xD743 (66 bytes)
 *
 * Configures PHY lane registers (0x78-0x7B in bank 2) based on
 * lane enable bits. Called during PCIe link setup.
 *
 * Uses bank-switched register access via helpers at 0x0BC8/0x0BE6
 * with r3=2 (bank 2), r2=addr high, r1=0xAF (addr low base).
 *
 * Original disassembly:
 *   d702: lcall 0xcc92     ; bank read 0x0278AF
 *   d705: lcall 0xcc9b     ; mask/prep
 *   d708: jnb e0.0, d70d   ; check lane 0
 *   d70b: mov r5, #0x01
 *   d70d: mov r2, #0x78    ; lane 0
 *   d70f: lcall 0xcc56     ; bank write
 *   ... (similar for lanes 1-3 at 0x79, 0x7a, 0x7b)
 *   d743: ljmp 0x0be6      ; final write
 */
void phy_link_training(void)
{
    /* This function uses complex bank-switched register access.
     * Implemented via inline assembly to match original firmware. */
    __asm
        ; Read lane status from bank 2, addr 0x78, base 0xAF
        mov     r3, #0x02       ; bank 2
        mov     r2, #0x78       ; addr high
        mov     r1, #0xaf       ; addr low
        lcall   _bank_read      ; returns status in A

        ; Mask and save lane status
        anl     a, #0x7f        ; mask bit 7
        mov     r6, a           ; save to r6
        mov     r5, #0x00       ; r5 = 0 (default)

        ; Check lane 0 (bit 0)
        mov     a, r6
        jnb     acc.0, 001$
        mov     r5, #0x01
001$:
        ; Write lane 0 config
        mov     r2, #0x78
        mov     a, r5
        swap    a
        rlc     a
        rlc     a
        rlc     a
        anl     a, #0x80
        orl     a, r6
        mov     r3, #0x02
        mov     r1, #0xaf
        lcall   _bank_write

        ; Read status again for lane 1
        mov     r3, #0x02
        mov     r2, #0x78
        mov     r1, #0xaf
        lcall   _bank_read
        anl     a, #0x7f
        mov     r6, a
        mov     r5, #0x00

        ; Check lane 1 (bit 1)
        mov     a, r6
        jnb     acc.1, 002$
        mov     r5, #0x01
002$:
        ; Write lane 1 config
        mov     r2, #0x79
        mov     a, r5
        swap    a
        rlc     a
        rlc     a
        rlc     a
        anl     a, #0x80
        orl     a, r6
        mov     r3, #0x02
        mov     r1, #0xaf
        lcall   _bank_write

        ; Read status again for lane 2
        mov     r3, #0x02
        mov     r2, #0x79
        mov     r1, #0xaf
        lcall   _bank_read
        anl     a, #0x7f
        mov     r6, a
        mov     r5, #0x00

        ; Check lane 2 (bit 2)
        mov     a, r6
        jnb     acc.2, 003$
        mov     r5, #0x01
003$:
        ; a = r5, prepare for write
        mov     a, r5
        swap    a
        rlc     a
        rlc     a
        rlc     a
        anl     a, #0x80
        orl     a, r6

        ; Write lane 2 config at 0x7a
        mov     r2, #0x7a
        mov     r3, #0x02
        mov     r1, #0xaf
        lcall   _bank_write
        inc     r2              ; r2 = 0x7b

        ; Read for lane 3
        lcall   _bank_read
        anl     a, #0x7f
        mov     r6, a
        mov     r7, #0x00

        ; Check lane 3 (bit 3)
        jnb     acc.3, 004$
        mov     r7, #0x01
004$:
        ; Final write at 0x7b
        mov     a, r7
        swap    a
        rlc     a
        rlc     a
        rlc     a
        anl     a, #0x80
        orl     a, r6
        mov     r2, #0x7b
        mov     r3, #0x02
        mov     r1, #0xaf
        lcall   _bank_write
    __endasm;
}

/* Bank read helper - reads from bank-switched address
 * r3 = bank, r2 = addr high, r1 = addr low (0xAF base)
 * Returns value in A */
void bank_read(void) __naked
{
    __asm
        cjne    r3, #0x01, _br_check2
        mov     dpl, r1
        mov     dph, r2
        movx    a, @dptr
        ret
_br_check2:
        jnc     _br_bank2
        mov     a, @r1
        ret
_br_bank2:
        cjne    r3, #0xfe, _br_xdata
        movx    a, @r1
        ret
_br_xdata:
        jc      _br_code
        mov     dpl, r1
        mov     dph, r2
        clr     a
        movc    a, @a+dptr
        ret
_br_code:
        ; Bank 2+ uses SFR 0x93 for bank select
        mov     0x93, r3
        mov     dpl, r1
        mov     dph, r2
        movx    a, @dptr
        mov     0x93, #0x00
        ret
    __endasm;
}

/* Bank write helper - writes to bank-switched address
 * r3 = bank, r2 = addr high, r1 = addr low
 * A = value to write */
void bank_write(void) __naked
{
    __asm
        cjne    r3, #0x01, _bw_check2
        mov     dpl, r1
        mov     dph, r2
        movx    @dptr, a
        ret
_bw_check2:
        jnc     _bw_bank2
        mov     @r1, a
        ret
_bw_bank2:
        cjne    r3, #0xfe, _bw_done
        movx    @r1, a
        ret
_bw_done:
        jnc     _bw_exit
        ; Bank 2+ uses SFR 0x93
        mov     0x93, r3
        mov     dpl, r1
        mov     dph, r2
        movx    @dptr, a
        mov     0x93, #0x00
_bw_exit:
        ret
    __endasm;
}

/*
 * phy_read_link_width - Read REG_LINK_WIDTH_E710 and mask bits 5-7
 * Address: 0xbd49-0xbd4f (7 bytes)
 *
 * Returns the link width from bits 5-7 of REG_LINK_WIDTH_E710.
 */
uint8_t phy_read_link_width(void)
{
    return REG_LINK_WIDTH_E710 & 0xE0;
}

/*
 * phy_read_link_status - Read REG_LINK_STATUS_E716 and mask bits 0-1
 * Address: 0xbd50-0xbd56 (7 bytes)
 */
uint8_t phy_read_link_status(void)
{
    return REG_LINK_STATUS_E716 & 0xFC;
}

/*
 * phy_read_mode_lane_config - Read PHY mode and extract lane configuration
 * Address: 0xbe8b-0xbe96 (12 bytes)
 *
 * Reads REG_PHY_MODE_E302, masks with 0x30 (bits 4-5), swaps nibbles,
 * masks with 0x0F, and returns the lane configuration.
 */
uint8_t phy_read_mode_lane_config(void)
{
    uint8_t val;

    val = REG_PHY_MODE_E302;
    val = val & 0x30;            /* Keep bits 4-5 */
    val = (val >> 4) | (val << 4);  /* swap nibbles */
    val = val & 0x0F;            /* Keep low nibble */

    return val;
}

/*
 * phy_read_lanes - Read PHY mode register and return lane count as nibble
 * Address: 0xbf04-0xbf0e (11 bytes)
 */
uint8_t phy_read_lanes(void)
{
    uint8_t val;

    val = REG_PHY_MODE_E302;
    val = val & 0x30;             /* Mask bits 4-5 */
    val = (val >> 4) | (val << 4);  /* Swap nibbles */
    val = val & 0x0F;             /* Keep low nibble */

    return val;
}

/*
 * phy_write_and_set_link_bit0 - Write to DPTR, then set bit 0 in REG_LINK_CTRL_E717
 * Address: 0xbce7-0xbcf1 (11 bytes)
 *
 * Writes A to register at DPTR, then sets bit 0 in link control register 0xE717.
 */
void phy_write_and_set_link_bit0(__xdata uint8_t *reg, uint8_t val)
{
    uint8_t tmp;

    *reg = val;
    tmp = REG_LINK_CTRL_E717;
    tmp = (tmp & 0xFE) | 0x01;
    REG_LINK_CTRL_E717 = tmp;
}
