/*
 * power.c - Power Management Driver
 *
 * See drivers/power.h for hardware documentation.
 */

#include "drivers/power.h"
#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"
#include "structs.h"
#include "utils.h"
#include "app/dispatch.h"

/* External declarations */
extern void usb_mode_config_d07f(uint8_t param);     /* event_handler.c */
extern void nvme_queue_config_e214(void);            /* nvme.c */
extern void delay_short_e89d(void);                  /* utils.c */
extern void pcie_clear_address_regs(void);           /* pcie.c - 0x9a9c */
extern void helper_e3b7(uint8_t param);              /* timer.c - 0xe3b7 */

/*
 * power_set_suspended - Set power status suspended bit (bit 6)
 * Address: 0xcb23-0xcb2c (10 bytes)
 *
 * Sets bit 6 of power status register to indicate device is suspended.
 *
 * Original disassembly:
 *   cb23: mov dptr, #0x92c2   ; Power status
 *   cb26: movx a, @dptr       ; read current
 *   cb27: anl a, #0xbf        ; clear bit 6
 *   cb29: orl a, #0x40        ; set bit 6
 *   cb2b: movx @dptr, a       ; write back
 *   cb2c: ret
 */
void power_set_suspended(void)
{
    uint8_t val = REG_POWER_STATUS;
    val = (val & ~POWER_STATUS_SUSPENDED) | POWER_STATUS_SUSPENDED;
    REG_POWER_STATUS = val;
}

/*
 * power_get_status_bit6 - Check if device is suspended (bit 6 of 0x92C2)
 * Address: 0x3023-0x302e (12 bytes)
 *
 * Reads power status register and extracts bit 6 (suspended flag).
 * Returns non-zero if suspended.
 *
 * Original disassembly:
 *   3023: mov dptr, #0x92c2   ; Power status
 *   3026: movx a, @dptr
 *   3027: anl a, #0x40        ; mask bit 6
 *   3029: mov r7, a           ; save result
 *   302a: swap a              ; shift right 4
 *   302b: rrc a               ; shift right 1 more
 *   302c: rrc a               ; shift right 1 more
 *   302d: anl a, #0x03        ; mask low 2 bits
 */
uint8_t power_get_status_bit6(void)
{
    uint8_t val = REG_POWER_STATUS;
    val &= POWER_STATUS_SUSPENDED;
    return val;
}

/*
 * power_enable_clocks - Enable power and clocks
 * Address: 0xcb6f-0xcb87 (25 bytes)
 *
 * Enables main power (0x92C0 bit 0) and clock config (0x92C1 bit 0),
 * then enables PHY power (0x92C5 bit 2).
 *
 * Original disassembly:
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
 */
void power_enable_clocks(void)
{
    uint8_t val;

    /* Enable main power (0x92C0 bit 0) */
    val = REG_POWER_ENABLE;
    val = (val & ~POWER_ENABLE_BIT) | POWER_ENABLE_BIT;
    REG_POWER_ENABLE = val;

    /* Enable clock config (0x92C1 bit 0) */
    val = REG_CLOCK_ENABLE;
    val = (val & ~CLOCK_ENABLE_BIT) | CLOCK_ENABLE_BIT;
    REG_CLOCK_ENABLE = val;

    /* Enable PHY power (0x92C5 bit 2) */
    val = REG_PHY_POWER;
    val = (val & ~PHY_POWER_ENABLE) | PHY_POWER_ENABLE;
    REG_PHY_POWER = val;
}

/*
 * power_config_init - Initialize power configuration
 * Address: 0xcb37-0xcb4a (20 bytes)
 *
 * Sets up power configuration registers for normal operation.
 * Writes 0x05 to 0x92C6, 0x00 to 0x92C7, then clears bits 0,1 of 0x9201.
 *
 * Original disassembly:
 *   cb37: mov dptr, #0x92c6   ; Power control 6
 *   cb3a: mov a, #0x05
 *   cb3c: movx @dptr, a
 *   cb3d: inc dptr            ; 0x92C7
 *   cb3e: clr a
 *   cb3f: movx @dptr, a
 *   cb40: mov dptr, #0x9201   ; USB control
 *   cb43: movx a, @dptr
 *   cb44: anl a, #0xfe        ; clear bit 0
 *   cb46: movx @dptr, a
 *   cb47: movx a, @dptr
 *   cb48: anl a, #0xfd        ; clear bit 1
 *   cb4a: movx @dptr, a
 */
void power_config_init(void)
{
    uint8_t val;

    /* Set clock gating config */
    REG_POWER_CTRL_92C6 = 0x05;
    REG_POWER_CTRL_92C7 = 0x00;

    /* Clear bits 0,1 of 0x9201 */
    val = REG_USB_CTRL_9201;
    val &= 0xFE;  /* Clear bit 0 */
    REG_USB_CTRL_9201 = val;
    val = REG_USB_CTRL_9201;
    val &= 0xFD;  /* Clear bit 1 */
    REG_USB_CTRL_9201 = val;
}

/*
 * power_set_clock_bit1 - Set clock configuration bit 1
 * Address: 0xcb4b-0xcb53 (9 bytes)
 *
 * Sets bit 1 of power control register 0x92C1 for clock configuration.
 *
 * Original disassembly:
 *   cb4b: mov dptr, #0x92c1   ; Power control 1
 *   cb4e: movx a, @dptr
 *   cb4f: anl a, #0xfd        ; clear bit 1
 *   cb51: orl a, #0x02        ; set bit 1
 *   cb53: movx @dptr, a
 */
void power_set_clock_bit1(void)
{
    uint8_t val = REG_CLOCK_ENABLE;
    val = (val & ~CLOCK_ENABLE_BIT1) | CLOCK_ENABLE_BIT1;
    REG_CLOCK_ENABLE = val;
}

/*
 * power_check_status - Check and update power status
 * Address: 0xe647-0xe65e (24 bytes) - Bank 0 function called via dispatch
 *
 * This function checks device power status and waits for ready.
 * Called via jump_bank_0 from dispatch stub.
 *
 * Algorithm:
 * 1. Call 0xC45F to check status
 * 2. If non-zero, write 0x04 to @dptr; if zero, write 0x03
 * 3. Poll 0xB296 bit 2 until set (wait for ready)
 * 4. Call 0xC48F for final processing
 *
 * Original disassembly:
 *   e647: lcall 0xc45f          ; Check status
 *   e64a: jz 0xe651             ; Jump if zero
 *   e64c: mov a, #0x04          ; Non-zero result
 *   e64e: movx @dptr, a
 *   e64f: sjmp 0xe654
 *   e651: mov a, #0x03          ; Zero result
 *   e653: movx @dptr, a
 *   e654: mov dptr, #0xb296     ; Poll status
 *   e657: movx a, @dptr
 *   e658: jnb e0.2, 0xe654      ; Loop until bit 2 set
 *   e65b: lcall 0xc48f          ; Final processing
 *   e65e: ret
 */
void power_check_status_e647(void)
{
    uint8_t status;

    /* Check initial status - would call 0xC45F */
    /* The result determines which value to write */

    /* Poll 0xB296 bit 2 until set */
    do {
        status = REG_PCIE_STATUS;
    } while (!(status & 0x04));

    /* Final processing - would call 0xC48F */
}

/*
 * power_check_status - Check power status with parameter
 * Address: TBD - stub implementation
 *
 * Called from SCSI/protocol code with queue index parameter.
 */
void power_check_status(uint8_t param)
{
    (void)param;
    /* TODO: Implement actual power status check */
}

/*
 * power_set_state - Set power state and config
 * Address: Called at entry, calls FUN_CODE_53c0
 *
 * Sets power state by calling 0x53C0 helper and setting 0x90A1 to 1.
 *
 * From ghidra.c:
 *   FUN_CODE_53c0();
 *   DAT_EXTMEM_90a1 = 1;
 */
void power_set_state(void)
{
    /* FUN_CODE_53c0 copies 4 bytes from IDATA[0x72-0x6F] to XDATA[0xD808-0xD80B] */
    /* This appears to be CSW residue setup */
    USB_CSW->residue0 = *(__idata uint8_t *)0x72;
    USB_CSW->residue1 = *(__idata uint8_t *)0x71;
    USB_CSW->residue2 = *(__idata uint8_t *)0x70;
    USB_CSW->residue3 = *(__idata uint8_t *)0x6F;

    /* Set power state active */
    REG_USB_SIGNAL_90A1 = 1;
}

/*
 * power_clear_suspended - Clear suspended bit (bit 6)
 * Address: 0xcb2d-0xcb36 (10 bytes)
 *
 * Clears bit 6 of power status register to indicate device is no longer suspended.
 *
 * Original disassembly:
 *   cb2d: mov dptr, #0x92c2   ; Power status
 *   cb30: movx a, @dptr       ; read current
 *   cb31: anl a, #0xbf        ; clear bit 6
 *   cb33: movx @dptr, a       ; write back
 *   cb34: ret
 */
void power_clear_suspended(void)
{
    uint8_t val = REG_POWER_STATUS;
    val &= ~POWER_STATUS_SUSPENDED;
    REG_POWER_STATUS = val;
}

/*
 * power_disable_clocks - Disable clocks for power save
 * Address: 0xcb88-0xcb9a (19 bytes)
 *
 * Disables power and clocks for power saving.
 *
 * Original disassembly:
 *   cb88: mov dptr, #0x92c0   ; Power control 0
 *   cb8b: movx a, @dptr
 *   cb8c: anl a, #0xfe        ; clear bit 0
 *   cb8e: movx @dptr, a
 *   cb8f: inc dptr            ; 0x92C1
 *   cb90: movx a, @dptr
 *   cb91: anl a, #0xfe        ; clear bit 0
 *   cb93: movx @dptr, a
 *   cb94: mov dptr, #0x92c5   ; Power control 5
 *   cb97: movx a, @dptr
 *   cb98: anl a, #0xfb        ; clear bit 2
 *   cb9a: movx @dptr, a
 */
void power_disable_clocks(void)
{
    uint8_t val;

    /* Disable main power (0x92C0 bit 0) */
    val = REG_POWER_ENABLE;
    val &= ~POWER_ENABLE_BIT;
    REG_POWER_ENABLE = val;

    /* Disable clock config (0x92C1 bit 0) */
    val = REG_CLOCK_ENABLE;
    val &= ~CLOCK_ENABLE_BIT;
    REG_CLOCK_ENABLE = val;

    /* Disable PHY power (0x92C5 bit 2) */
    val = REG_PHY_POWER;
    val &= ~PHY_POWER_ENABLE;
    REG_PHY_POWER = val;
}

/* Forward declarations for helper functions */
extern void nvme_queue_config_e214(void);
extern void power_init_complete_e8ef(uint8_t param);
extern void delay_short_e89d(void);                    /* 0xe89d - Short delay */
extern void delay_wait_e80a(uint16_t delay, uint8_t flag);  /* 0xe80a - Delay with params */

/* Helper to read 32 bits from DPTR */
static void xdata_read32(__xdata uint8_t *ptr, uint8_t *r4, uint8_t *r5, uint8_t *r6, uint8_t *r7);
/* Helper to write 32 bits to DPTR */
static void xdata_write32(__xdata uint8_t *ptr, uint8_t r4, uint8_t r5, uint8_t r6, uint8_t r7);

/* 0x0d84: Read 4 bytes from DPTR into R4-R7 */
static void xdata_read32(__xdata uint8_t *ptr, uint8_t *r4, uint8_t *r5, uint8_t *r6, uint8_t *r7)
{
    *r4 = ptr[0];
    *r5 = ptr[1];
    *r6 = ptr[2];
    *r7 = ptr[3];
}

/* 0x0dc5: Write R4-R7 to 4 bytes at DPTR */
static void xdata_write32(__xdata uint8_t *ptr, uint8_t r4, uint8_t r5, uint8_t r6, uint8_t r7)
{
    ptr[0] = r4;
    ptr[1] = r5;
    ptr[2] = r6;
    ptr[3] = r7;
}

/*
 * power_state_machine_d02a - Power state initialization loop
 * Address: 0xd02a-0xd07e (85 bytes)
 *
 * Initializes power states by iterating through a table at 0xB220.
 * Each iteration reads 4 bytes and copies them to 0x8000 + (index * 4).
 *
 * Parameters:
 *   max_iterations (R7): Maximum number of iterations
 *
 * Returns:
 *   0x00: Success - all iterations completed
 *   0xFF: Failure - delay_short returned non-zero
 *
 * Algorithm:
 *   G_POWER_STATE_MAX_0A61 = max_iterations
 *   G_POWER_STATE_IDX_0A62 = 0
 *   loop:
 *     if (G_POWER_STATE_IDX_0A62 >= G_POWER_STATE_MAX_0A61) return 0
 *     delay_short_e89d()
 *     if (result != 0) return 0xFF
 *     read 4 bytes from REG_PCIE_DATA (0xB220)
 *     write to table at 0x8000 + (G_POWER_STATE_IDX_0A62 * 4)
 *     if (IDATA[0x65] != 0) increment IDATA[0x64]
 *     G_POWER_STATE_IDX_0A62++
 *     goto loop
 */
uint8_t power_state_machine_d02a(uint8_t max_iterations)
{
    uint8_t r4, r5, r6, r7;
    uint8_t idx;
    __xdata uint8_t *table_ptr;

    /* Store max and clear index */
    G_POWER_STATE_MAX_0A61 = max_iterations;
    G_POWER_STATE_IDX_0A62 = 0;

    while (1) {
        /* Check if we've completed all iterations */
        if (G_POWER_STATE_IDX_0A62 >= G_POWER_STATE_MAX_0A61) {
            return 0;  /* Success */
        }

        /* Call delay helper */
        delay_short_e89d();

        /* Check if delay returned error (R7 != 0 in assembly) */
        /* The delay function modifies IDATA[0x65], if non-zero it's an error */
        if (I_WORK_65 != 0) {
            return 0xFF;  /* Failure */
        }

        /* Read 4 bytes from REG_PCIE_DATA (0xB220) */
        xdata_read32((__xdata uint8_t *)0xB220, &r4, &r5, &r6, &r7);

        /* Calculate table address: 0x8000 + (index * 4) */
        idx = G_POWER_STATE_IDX_0A62;
        table_ptr = (__xdata uint8_t *)(0x8000 + ((uint16_t)idx * 4));

        /* Write 4 bytes to the table */
        xdata_write32(table_ptr, r4, r5, r6, r7);

        /* Check IDATA[0x65] and conditionally increment IDATA[0x64] */
        /* mov r0, #0x64; inc @r0; mov a, @r0; dec r0; jnz d074 */
        /* If value at 0x65 != 0, increment value at 0x64 */
        if (*(__idata uint8_t *)0x65 != 0) {
            (*(__idata uint8_t *)0x64)++;
        }

        /* Increment index and continue */
        G_POWER_STATE_IDX_0A62++;
    }
}

/*
 * power_check_state_dde2 - Check power state after initialization
 * Address: 0xdde2-0xde15 (52 bytes)
 *
 * Performs power state initialization and validates the result.
 * Calls pcie_clear_address_regs and power_state_machine_d02a,
 * then checks specific values in the power state table.
 *
 * Returns:
 *   0xFF: Error - power_state_machine_d02a failed
 *   0x01: Active - bits 0-6 of table[0x0D] are non-zero
 *   0x02: Complete - table[0x08]==1, table[0x0A]==2, table[0x09]==8
 *   0x00: Default - none of the above conditions met
 *
 * Original disassembly:
 *   dde2: lcall 0x9a9c       ; pcie_clear_address_regs()
 *   dde5: mov r7, #0x04
 *   dde7: lcall 0xd02a       ; power_state_machine_d02a(4)
 *   ddea: mov a, r7
 *   ddeb: jnz 0xde13         ; If != 0, return 0xFF
 *   dded: mov dptr, #0x800d
 *   ddf0: movx a, @dptr
 *   ddf1: anl a, #0x7f       ; Clear bit 7
 *   ddf3: jnz 0xde10         ; If non-zero, return 0x01
 *   ddf5: mov dptr, #0x8008
 *   ddf8: movx a, @dptr
 *   ddf9: cjne a, #0x01, 0xde0d
 *   ddfc: mov dptr, #0x800a
 *   ddff: movx a, @dptr
 *   de00: cjne a, #0x02, 0xde0d
 *   de03: mov dptr, #0x8009
 *   de06: movx a, @dptr
 *   de07: cjne a, #0x08, 0xde0d  ; All match: return 0x02
 *   de0a: mov r7, #0x02
 *   de0c: ret
 *   de0d: mov r7, #0x00      ; Default: return 0x00
 *   de0f: ret
 *   de10: mov r7, #0x01      ; Active: return 0x01
 *   de12: ret
 *   de13: mov r7, #0xff      ; Error: return 0xFF
 *   de15: ret
 */
uint8_t power_check_state_dde2(void)
{
    uint8_t result;

    /* Clear PCIe address registers */
    pcie_clear_address_regs();

    /* Run power state machine with 4 iterations */
    result = power_state_machine_d02a(4);

    /* If state machine failed, return error */
    if (result != 0) {
        return 0xFF;
    }

    /* Check USB buffer status bits 0-6 */
    if ((REG_USB_BUF_STATUS_800D & 0x7F) != 0) {
        return 0x01;  /* Active state */
    }

    /* Check for specific pattern: [0x08]==1, [0x0A]==2, [0x09]==8 */
    if (REG_USB_BUF_CTRL_8008 == 0x01 &&
        REG_USB_BUF_CTRL_800A == 0x02 &&
        REG_USB_BUF_CTRL_8009 == 0x08) {
        return 0x02;  /* Complete state */
    }

    return 0x00;  /* Default */
}

/*
 * power_set_suspended_and_event_cad6 - Set suspend bit and power event
 * Address: 0xcad6-0xcae5 (16 bytes)
 *
 * Sets the suspended bit in power status and writes 0x10 to power event.
 * Part of the USB suspend/resume sequence.
 *
 * Original disassembly:
 *   cad6: mov dptr, #0x92c2   ; Power status register
 *   cad9: movx a, @dptr       ; Read current value
 *   cada: anl a, #0xbf        ; Clear bit 6
 *   cadc: orl a, #0x40        ; Set bit 6
 *   cade: movx @dptr, a       ; Write back
 *   cadf: mov dptr, #0x92e1   ; Power event register
 *   cae2: mov a, #0x10        ; Event value
 *   cae4: movx @dptr, a       ; Write event
 *   cae5: ret
 */
void power_set_suspended_and_event_cad6(void)
{
    uint8_t val;

    /* Set bit 6 of power status (suspended flag) */
    val = REG_POWER_STATUS;
    REG_POWER_STATUS = (val & 0xBF) | 0x40;

    /* Write 0x10 to power event register */
    REG_POWER_EVENT_92E1 = 0x10;
}

/*
 * power_toggle_usb_bit2_caed - Toggle USB bit 2
 * Address: 0xcaed-0xcafa (14 bytes)
 *
 * Reads USB status, sets bit 2, writes back, then reads again,
 * clears bit 2, writes back. This toggles bit 2 as a pulse.
 *
 * Original disassembly:
 *   caed: mov dptr, #0x9000   ; USB status register
 *   caf0: movx a, @dptr       ; Read current
 *   caf1: anl a, #0xfb        ; Clear bit 2
 *   caf3: orl a, #0x04        ; Set bit 2
 *   caf5: movx @dptr, a       ; Write back
 *   caf6: movx a, @dptr       ; Read again
 *   caf7: anl a, #0xfb        ; Clear bit 2
 *   caf9: movx @dptr, a       ; Write back
 *   cafa: ret
 */
void power_toggle_usb_bit2_caed(void)
{
    uint8_t val;

    /* Set bit 2 */
    val = REG_USB_STATUS;
    REG_USB_STATUS = (val & 0xFB) | 0x04;

    /* Clear bit 2 */
    val = REG_USB_STATUS;
    REG_USB_STATUS = val & 0xFB;
}

/*
 * power_set_phy_bit1_cafb - Set PHY control bit 1
 * Address: 0xcafb-0xcb04 (10 bytes)
 *
 * Sets bit 1 in PHY control register 0x91C0.
 *
 * Original disassembly:
 *   cafb: mov dptr, #0x91c0   ; PHY control register
 *   cafe: movx a, @dptr       ; Read current
 *   caff: anl a, #0xfd        ; Clear bit 1
 *   cb01: orl a, #0x02        ; Set bit 1
 *   cb03: movx @dptr, a       ; Write back
 *   cb04: ret
 */
void power_set_phy_bit1_cafb(void)
{
    uint8_t val;

    val = REG_USB_PHY_CTRL_91C0;
    REG_USB_PHY_CTRL_91C0 = (val & ~USB_PHY_CTRL_BIT1) | USB_PHY_CTRL_BIT1;
}

/*
 * phy_power_init_d916 - Initialize PHY power settings
 * Address: 0xd916-0xd955 (64 bytes)
 *
 * Initializes PHY power configuration for USB power management.
 * Calls helper functions for suspend state and USB/PHY setup.
 *
 * Parameters:
 *   param (R7): If non-zero, calls delay_wait_e80a(0x0257, 5)
 *
 * Algorithm:
 *   1. Call power_set_suspended_and_event_cad6()  - set suspend and event
 *   2. Call power_toggle_usb_bit2_caed()          - toggle USB bit 2
 *   3. Call power_set_phy_bit1_cafb()             - set PHY bit 1
 *   4. Read 0x9090, clear bit 7, write back
 *   5. If R7 != 0, call delay_wait_e80a(0x0257, 5)
 *   6. Write 0x04 to 0x9300
 *   7. Write 0x02 to 0x91D1, then 0x40, then 0x80 to 0x9301
 *   8. Write 0x08 to 0x91D1, then 0x01
 *   9. Clear G_SYSTEM_STATE_0AE2
 */
void phy_power_init_d916(uint8_t param)
{
    uint8_t val;

    /* Call helper functions */
    power_set_suspended_and_event_cad6();
    power_toggle_usb_bit2_caed();
    power_set_phy_bit1_cafb();

    /* Clear bit 7 of 0x9090 */
    val = REG_USB_INT_MASK_9090;
    REG_USB_INT_MASK_9090 = val & 0x7F;

    /* If param != 0, do a delay */
    if (param != 0) {
        delay_wait_e80a(0x0257, 5);
    }

    /* Configure buffer and PHY */
    REG_BUF_CFG_9300 = 0x04;
    REG_USB_PHY_CTRL_91D1 = 0x02;
    REG_BUF_CFG_9301 = 0x40;
    REG_BUF_CFG_9301 = 0x80;
    REG_USB_PHY_CTRL_91D1 = 0x08;
    REG_USB_PHY_CTRL_91D1 = 0x01;

    /* Clear system state */
    G_SYSTEM_STATE_0AE2 = 0;
}

/*
 * power_clear_init_flag - Clear power init flag
 * Address: 0x545c-0x5461 (6 bytes)
 *
 * Sets the power init flag to 0.
 * Called during USB power initialization.
 *
 * Original (from ghidra):
 *   DAT_EXTMEM_0af8 = 0;
 */
void power_clear_init_flag(void)
{
    G_POWER_INIT_FLAG = 0;
}

/*
 * power_set_event_ctrl - Set event control to 4
 * Address: 0xbbb6-0xbbbf (10 bytes)
 *
 * Sets event control register to 4.
 * Called during USB power initialization state handling.
 *
 * Original (from ghidra):
 *   G_EVENT_CTRL_09FA = 4;
 */
void power_set_event_ctrl(void)
{
    G_EVENT_CTRL_09FA = 4;
}

/*
 * usb_power_init - Initialize USB power settings
 * Address: Full initialization sequence from ghidra
 *
 * Initializes USB power configuration for operation.
 * Called during system initialization via handler_0327.
 *
 * This function performs:
 * 1. Power control register setup (0x92C0 bit 7)
 * 2. USB PHY configuration (0x91D1, 0x91C0, 0x91C1, 0x91C3)
 * 3. Buffer configuration (0x9300-0x9305)
 * 4. USB endpoint and mode setup
 * 5. NVMe command register init
 * 6. PHY power-up sequence with polling
 */
void usb_power_init(void)
{
    uint8_t val;
    uint8_t status;

    /* Set power control bit 7 (enable main power) */
    val = REG_POWER_ENABLE;
    REG_POWER_ENABLE = (val & ~POWER_ENABLE_MAIN) | POWER_ENABLE_MAIN;

    /* Configure USB PHY */
    REG_USB_PHY_CTRL_91D1 = 0x0F;

    /* Configure buffer settings */
    REG_BUF_CFG_9300 = 0x0C;
    REG_BUF_CFG_9301 = 0xC0;
    REG_BUF_CFG_9302 = 0xBF;

    /* Set interrupt flags */
    REG_INT_FLAGS_EX0 = 0x1F;

    /* Configure endpoint */
    REG_USB_EP_CFG1 = 0x0F;

    /* Configure USB PHY control 1 */
    REG_USB_PHY_CTRL_91C1 = 0xF0;

    /* More buffer configuration */
    REG_BUF_CFG_9303 = 0x33;
    REG_BUF_CFG_9304 = 0x3F;
    REG_BUF_CFG_9305 = 0x40;

    /* Configure USB */
    REG_USB_CONFIG = 0xE0;
    REG_USB_EP0_LEN_H = 0xF0;
    REG_USB_MODE = 1;

    /* Clear EP control bit 0 */
    val = REG_USB_EP_MGMT;
    REG_USB_EP_MGMT = val & 0xFE;

    /* Trigger USB MSC operation and clear status bit */
    REG_USB_MSC_CTRL = 1;
    val = REG_USB_MSC_STATUS;
    REG_USB_MSC_STATUS = val & 0xFE;

    /* Call initialization handlers */
    usb_mode_config_d07f(0);
    nvme_queue_config_e214();

    /* Configure USB PHY control 3 - clear bit 5 */
    val = REG_USB_PHY_CTRL_91C3;
    REG_USB_PHY_CTRL_91C3 = val & 0xDF;

    /* PHY power-up sequence */
    /* Set bit 0 then clear it */
    val = REG_USB_PHY_CTRL_91C0;
    REG_USB_PHY_CTRL_91C0 = (val & 0xFE) | 0x01;
    val = REG_USB_PHY_CTRL_91C0;
    REG_USB_PHY_CTRL_91C0 = val & 0xFE;

    /* Clear init flag */
    power_clear_init_flag();

    /* Poll for completion - check XDATA 0xE318 and timer */
    /* Simplified polling - original uses FUN_CODE_e50d(1,0x8f,4) and complex wait */
    do {
        status = REG_PHY_COMPLETION_E318;
        if ((status & 0x10) != 0) break;
        val = REG_TIMER0_CSR;
    } while ((val & 0x02) == 0);

    /* Call completion handler */
    power_init_complete_e8ef(status & 0x10);

    /* Final state handling based on PHY status */
    val = REG_USB_PHY_CTRL_91C0;
    if ((val & 0x18) == 0x10) {
        /* PHY in expected state */
        if (G_EVENT_FLAGS == EVENT_FLAG_POWER) {
            power_set_event_ctrl();
            G_EVENT_FLAGS = EVENT_FLAG_PENDING;
            return;
        }
    } else {
        /* PHY not in expected state */
        power_set_event_ctrl();
        REG_USB_PHY_CTRL_91C0 = 2;
    }
}

/*
 * power_get_state_nibble_cb0f - Get power state nibble
 * Address: 0xcb0f-0xcb18 (10 bytes)
 *
 * Reads the high nibble of power status register 0x92F7 and returns
 * it as the low nibble (shifted right by 4).
 *
 * Original disassembly:
 *   cb0f: mov dptr, #0x92f7   ; Power status extended register
 *   cb12: movx a, @dptr       ; Read value
 *   cb13: anl a, #0xf0        ; Keep high nibble
 *   cb15: swap a              ; Swap nibbles
 *   cb16: anl a, #0x0f        ; Keep low nibble (was high)
 *   cb18: ret
 *
 * Returns: High nibble of 0x92F7 as value 0-15
 */
uint8_t power_get_state_nibble_cb0f(void)
{
    uint8_t val;

    val = REG_POWER_STATUS_92F7;
    val = (val & 0xF0) >> 4;  /* Get high nibble */
    return val;
}

/*
 * power_set_link_status_cb19 - Set link status bits 0-1
 * Address: 0xcb19-0xcb22 (10 bytes)
 *
 * Sets bits 0-1 of the link status register 0xE716 to 0b11.
 *
 * Original disassembly:
 *   cb19: mov dptr, #0xe716   ; Link status register
 *   cb1c: movx a, @dptr       ; Read current
 *   cb1d: anl a, #0xfc        ; Clear bits 0-1
 *   cb1f: orl a, #0x03        ; Set bits 0-1
 *   cb21: movx @dptr, a       ; Write back
 *   cb22: ret
 */
void power_set_link_status_cb19(void)
{
    uint8_t val;

    val = REG_LINK_STATUS_E716;
    val = (val & 0xFC) | 0x03;
    REG_LINK_STATUS_E716 = val;
}

/*
 * power_set_status_bit6_cb23 - Set power status bit 6 (suspended)
 * Address: 0xcb23-0xcb2c (10 bytes)
 *
 * Sets bit 6 of power status register 0x92C2.
 * This is similar to power_set_suspended_and_event_cad6 but doesn't
 * write to the event register.
 *
 * Original disassembly:
 *   cb23: mov dptr, #0x92c2   ; Power status register
 *   cb26: movx a, @dptr       ; Read current
 *   cb27: anl a, #0xbf        ; Clear bit 6
 *   cb29: orl a, #0x40        ; Set bit 6
 *   cb2b: movx @dptr, a       ; Write back
 *   cb2c: ret
 */
void power_set_status_bit6_cb23(void)
{
    uint8_t val;

    val = REG_POWER_STATUS;
    val = (val & 0xBF) | 0x40;
    REG_POWER_STATUS = val;
}

/*
 * power_clear_interface_flags_cb2d - Clear interface ready flags
 * Address: 0xcb2d-0xcb36 (10 bytes)
 *
 * Clears the interface ready flag (0x0B2F) and system flag (0x07EB).
 * Called during power state transitions to reset interface state.
 *
 * Original disassembly:
 *   cb2d: clr a               ; A = 0
 *   cb2e: mov dptr, #0x0b2f   ; Interface ready flag
 *   cb31: movx @dptr, a       ; Clear it
 *   cb32: mov dptr, #0x07eb   ; System flags
 *   cb35: movx @dptr, a       ; Clear it
 *   cb36: ret
 */
void power_clear_interface_flags_cb2d(void)
{
    G_INTERFACE_READY_0B2F = 0;
    G_SYS_FLAGS_07EB = 0;
}

/*
 * power_phy_init_config_cb37 - Initialize power and PHY configuration
 * Address: 0xcb37-0xcb97 (97 bytes)
 *
 * Configures power management and PHY registers for power state transition.
 * This is a comprehensive initialization function that sets up:
 * - Power control registers (92C6, 92C7)
 * - USB control registers (9201, 920C)
 * - Clock enable register (92C1)
 * - Link/PHY control registers (C208, C20C)
 * - Power enable/clock (92C0, 92C1)
 * - PHY power and USB PHY config (92C5, 9241)
 *
 * Original disassembly:
 *   cb37: mov dptr, #0x92c6   ; Power control 1
 *   cb3a: mov a, #0x05
 *   cb3c: movx @dptr, a       ; Write 0x05
 *   cb3d: inc dptr            ; 0x92c7
 *   cb3e: clr a
 *   cb3f: movx @dptr, a       ; Write 0x00
 *   cb40: mov dptr, #0x9201   ; USB control
 *   cb43: movx a, @dptr
 *   cb44: anl a, #0xfe        ; Clear bit 0
 *   cb46: movx @dptr, a
 *   cb47: movx a, @dptr
 *   cb48: anl a, #0xfd        ; Clear bit 1
 *   cb4a: movx @dptr, a
 *   cb4b: mov dptr, #0x92c1   ; Clock enable
 *   cb4e: movx a, @dptr
 *   cb4f: anl a, #0xfd        ; Clear bit 1
 *   cb51: orl a, #0x02        ; Set bit 1
 *   cb53: movx @dptr, a
 *   cb54: mov dptr, #0x920c   ; USB control 2
 *   cb57: movx a, @dptr
 *   cb58: anl a, #0xfd        ; Clear bit 1
 *   cb5a: movx @dptr, a
 *   cb5b: movx a, @dptr
 *   cb5c: anl a, #0xfe        ; Clear bit 0
 *   cb5e: movx @dptr, a
 *   cb5f: mov dptr, #0xc20c   ; PHY link config
 *   cb62: movx a, @dptr
 *   cb63: anl a, #0xbf        ; Clear bit 6
 *   cb65: orl a, #0x40        ; Set bit 6
 *   cb67: movx @dptr, a
 *   cb68: mov dptr, #0xc208   ; PHY link ctrl
 *   cb6b: movx a, @dptr
 *   cb6c: anl a, #0xef        ; Clear bit 4
 *   cb6e: movx @dptr, a
 *   cb6f: mov dptr, #0x92c0   ; Power enable
 *   cb72: movx a, @dptr
 *   cb73: anl a, #0xfe        ; Clear bit 0
 *   cb75: orl a, #0x01        ; Set bit 0
 *   cb77: movx @dptr, a
 *   cb78: inc dptr            ; 0x92c1 (clock enable)
 *   cb79: movx a, @dptr
 *   cb7a: anl a, #0xfe        ; Clear bit 0
 *   cb7c: orl a, #0x01        ; Set bit 0
 *   cb7e: movx @dptr, a
 *   cb7f: mov dptr, #0x92c5   ; PHY power
 *   cb82: movx a, @dptr
 *   cb83: anl a, #0xfb        ; Clear bit 2
 *   cb85: orl a, #0x04        ; Set bit 2
 *   cb87: movx @dptr, a
 *   cb88: mov dptr, #0x9241   ; USB PHY config
 *   cb8b: movx a, @dptr
 *   cb8c: anl a, #0xef        ; Clear bit 4
 *   cb8e: orl a, #0x10        ; Set bit 4
 *   cb90: movx @dptr, a
 *   cb91: movx a, @dptr
 *   cb92: anl a, #0x3f        ; Clear bits 6-7
 *   cb94: orl a, #0xc0        ; Set bits 6-7
 *   cb96: movx @dptr, a
 *   cb97: ret
 */
void power_phy_init_config_cb37(void)
{
    uint8_t val;

    /* Power control registers: 92C6 = 0x05, 92C7 = 0x00 */
    REG_POWER_CTRL_92C6 = 0x05;
    REG_POWER_CTRL_92C7 = 0x00;

    /* USB control 9201: Clear bits 0 and 1 */
    val = REG_USB_CTRL_9201;
    val &= 0xFE;  /* Clear bit 0 */
    REG_USB_CTRL_9201 = val;

    val = REG_USB_CTRL_9201;
    val &= 0xFD;  /* Clear bit 1 */
    REG_USB_CTRL_9201 = val;

    /* Clock enable 92C1: Clear bit 1, set bit 1 */
    val = REG_CLOCK_ENABLE;
    val = (val & 0xFD) | 0x02;
    REG_CLOCK_ENABLE = val;

    /* USB control 920C: Clear bits 0 and 1 */
    val = REG_USB_CTRL_920C;
    val &= 0xFD;  /* Clear bit 1 */
    REG_USB_CTRL_920C = val;

    val = REG_USB_CTRL_920C;
    val &= 0xFE;  /* Clear bit 0 */
    REG_USB_CTRL_920C = val;

    /* PHY link config C20C: Clear bit 6, set bit 6 */
    val = REG_PHY_LINK_CONFIG_C20C;
    val = (val & 0xBF) | 0x40;
    REG_PHY_LINK_CONFIG_C20C = val;

    /* PHY link ctrl C208: Clear bit 4 */
    val = REG_PHY_LINK_CTRL_C208;
    val &= 0xEF;
    REG_PHY_LINK_CTRL_C208 = val;

    /* Power enable 92C0: Clear bit 0, set bit 0 */
    val = REG_POWER_ENABLE;
    val = (val & 0xFE) | 0x01;
    REG_POWER_ENABLE = val;

    /* Clock enable 92C1: Clear bit 0, set bit 0 */
    val = REG_CLOCK_ENABLE;
    val = (val & 0xFE) | 0x01;
    REG_CLOCK_ENABLE = val;

    /* PHY power 92C5: Clear bit 2, set bit 2 */
    val = REG_PHY_POWER;
    val = (val & 0xFB) | 0x04;
    REG_PHY_POWER = val;

    /* USB PHY config 9241: Clear bit 4, set bit 4 */
    val = REG_USB_PHY_CONFIG_9241;
    val = (val & 0xEF) | 0x10;
    REG_USB_PHY_CONFIG_9241 = val;

    /* USB PHY config 9241: Clear bits 6-7, set bits 6-7 */
    val = REG_USB_PHY_CONFIG_9241;
    val = (val & 0x3F) | 0xC0;
    REG_USB_PHY_CONFIG_9241 = val;
}

/*
 * power_check_event_ctrl_c9fa - Check event control and USB state
 * Address: 0xc9fa-0xca0c (19 bytes)
 *
 * Checks bit 1 of event control (0x09FA), and if set, checks USB state
 * (0x0B41). If both conditions are met, calls helper_e3b7 with param 1.
 *
 * Original disassembly:
 *   c9fa: mov dptr, #0x09fa   ; Event control
 *   c9fd: movx a, @dptr       ; Read event control
 *   c9fe: jnb 0xe0.1, 0xca0c  ; If bit 1 clear, skip to ret
 *   ca01: mov dptr, #0x0b41   ; USB state
 *   ca04: movx a, @dptr       ; Read USB state
 *   ca05: jz 0xca0c           ; If zero, skip to ret
 *   ca07: mov r7, #0x01       ; Param = 1
 *   ca09: lcall 0xe3b7        ; Call helper
 *   ca0c: ret
 */
void power_check_event_ctrl_c9fa(void)
{
    uint8_t event_ctrl = G_EVENT_CTRL_09FA;

    /* Check if bit 1 of event control is set */
    if (event_ctrl & 0x02) {
        /* Check if USB state is non-zero */
        if (G_USB_STATE_0B41 != 0) {
            helper_e3b7(1);
        }
    }
}

/*
 * power_state_handler_ca0d - Power state transition handler
 * Address: 0xca0d-0xca70 (100 bytes)
 *
 * Called during power state transitions. Checks G_EVENT_CTRL_09FA
 * and G_SYSTEM_STATE_0AE2 to determine appropriate actions.
 *
 * States handled:
 *   - Event ctrl == 0x04: Call dispatch_057f, set bit6 on 0x92e1, clear bit6 on 0x92c2
 *   - System state == 0x01: Call dispatch_057f, set bit6 on 0x92e1, clear bit6 on 0x92c2
 *   - System state == 0x02: Clear bit1 on 0x91c0
 *   - System state == 0x04: Various register operations
 *
 * Original disassembly:
 *   ca0d: mov dptr, #0x09fa   ; G_EVENT_CTRL_09FA
 *   ca10: movx a, @dptr       ; Read event control
 *   ca11: cjne a, #0x04, ...  ; Check if == 4
 *   ... complex state machine ...
 *   ca70: ret
 */
void power_state_handler_ca0d(void)
{
    uint8_t event_ctrl = G_EVENT_CTRL_09FA;
    uint8_t sys_state = G_SYSTEM_STATE_0AE2;
    uint8_t val;

    /* If event_ctrl == 4, perform special init sequence */
    if (event_ctrl == 0x04) {
        /* dispatch_057f(); - call through dispatch stub */
        dispatch_057f();

        /* Set bit 6 on 0x92e1, clear bit 6 on 0x92c2 */
        reg_set_bit6((__xdata uint8_t *)0x92E1);
        val = REG_POWER_STATUS;
        val &= 0xBF;  /* Clear bit 6 */
        REG_POWER_STATUS = val;
        goto write_final;
    }

    /* Check system state */
    if (sys_state == 0x01) {
        dispatch_057f();
        reg_set_bit6((__xdata uint8_t *)0x92E1);
        val = REG_POWER_STATUS;
        val &= 0xBF;
        REG_POWER_STATUS = val;
        goto write_final;
    }

    if (sys_state == 0x02) {
        /* Clear bit 1 on USB PHY control */
        val = REG_USB_PHY_CTRL_91C0;
        val &= 0xFD;
        REG_USB_PHY_CTRL_91C0 = val;
        goto write_final;
    }

    if (sys_state == 0x04) {
        /* Complex register sequence */
        val = REG_CPU_MODE;
        val &= 0xFE;  /* Clear bit 0 */
        REG_CPU_MODE = val;

        val = REG_LINK_WIDTH_E710;
        val = (val & 0xE0) | 0x1F;
        REG_LINK_WIDTH_E710 = val;

        val = REG_USB_PHY_CTRL_91C0;
        val &= 0xFD;  /* Clear bit 1 */
        REG_USB_PHY_CTRL_91C0 = val;

        reg_set_bit1(&REG_TIMER_CTRL_CC3B);
    }

write_final:
    /* Set system state to 0x10 */
    G_SYSTEM_STATE_0AE2 = 0x10;
}

/*
 * power_reset_sys_state_c9ef - Reset system state flags
 * Address: 0xc9ef-0xc9f9 (11 bytes)
 *
 * Clears system flags and sets interface ready flag.
 *
 * Original disassembly:
 *   c9ef: clr a               ; a = 0
 *   c9f0: mov dptr, #0x07e8   ; G_SYS_FLAGS_07E8
 *   c9f3: movx @dptr, a       ; Write 0
 *   c9f4: mov dptr, #0x0b2f   ; G_INTERFACE_READY_0B2F
 *   c9f7: inc a               ; a = 1
 *   c9f8: movx @dptr, a       ; Write 1
 *   c9f9: ret
 */
void power_reset_sys_state_c9ef(void)
{
    G_SYS_FLAGS_07E8 = 0;
    G_INTERFACE_READY_0B2F = 1;
}


/* ============================================================
 * Power Configuration Functions
 * ============================================================ */

void power_config_d630(uint8_t param)
{
    uint8_t val;
    val = REG_POWER_CTRL_B432; val = (val & 0xF8) | 0x07; REG_POWER_CTRL_B432 = val;
    val = REG_POWER_LANE_B404; val = (val & 0xF0) | (param & 0x0F); REG_POWER_LANE_B404 = val;
}
