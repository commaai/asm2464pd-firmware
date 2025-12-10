/*
 * ASM2464PD Firmware - Queue Handler Functions (0xA000-0xBFFF)
 *
 * This file contains queue management and handler functions for:
 * - PCIe extended register access
 * - Power state helpers
 * - USB/NVMe state coordination
 *
 * Many functions access PCIe configuration registers through banked
 * memory at addresses 0x12xx using helper functions 0x0bc8/0x0be6.
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

/*===========================================================================
 * PCIe Extended Register Access Helpers
 *
 * The original firmware uses helpers 0x0bc8 (read) and 0x0be6 (write)
 * for banked memory access:
 *   R3 = bank (0x02 for PCIe config)
 *   R2 = high byte (0x12)
 *   R1 = low byte (offset)
 *
 * In C, we use XDATA pointers to the equivalent memory-mapped registers.
 * The PCIe extended registers at 0x12xx are mapped to XDATA 0xB2xx.
 *===========================================================================*/

/* PCIe extended registers used here are defined in registers.h:
 *   REG_PCIE_LINK_STATE_EXT      (0xB234) - Link state machine state
 *   REG_PCIE_LINK_CFG        (0xB235) - Link configuration
 *   REG_PCIE_LINK_PARAM      (0xB236) - Link parameter
 *   REG_PCIE_LINK_STATUS_EXT (0xB237) - Extended link status
 *   REG_PCIE_EXT_CFG_0       (0xB23C) - Extended config 0 (lane config)
 *   REG_PCIE_EXT_CFG_1       (0xB23D) - Extended config 1 (lane config)
 *   REG_PCIE_EXT_CFG_2       (0xB23E) - Extended config 2 (lane config)
 *   REG_PCIE_EXT_STATUS_RD   (0xB240) - Extended status read
 *   REG_PCIE_EXT_STATUS_ALT  (0xB24E) - Extended status alternate
 */

/*===========================================================================
 * Power State Helper Functions (0xaa00-0xaa35)
 *
 * These small functions check USB interrupt status and modify power registers.
 * Entry points are at different offsets but share common code paths.
 *===========================================================================*/

/*
 * power_state_check_a9f9 - Check bit 7 of USB interrupt mask and return status
 * Address: 0xa9f9-0xa9fc (4 bytes - actually shared code)
 *
 * This is a code fragment that checks REG_USB_INT_MASK_9090 bit 7.
 * Returns 0x05 in R7.
 *
 * Disassembly shows this is actually the end of another function,
 * returning after an add instruction.
 */
uint8_t power_state_return_05(void)
{
    return 0x05;
}

/*
 * power_state_helper_aa02 - Clear power control bit 0 if USB bit 7 set
 * Address: 0xaa02-0xaa12 (17 bytes)
 *
 * Disassembly:
 *   aa02: mov dptr, #0xe030  ; (partial - mid-function entry)
 *   aa05: mov a, @r1
 *   aa06: inc r1
 *   aa07: mov dptr, #0x92c8  ; REG_POWER_CTRL_92C8
 *   aa0a: movx a, @dptr
 *   aa0b: anl a, #0xfe       ; Clear bit 0
 *   aa0d: movx @dptr, a
 *   aa0e: sjmp 0xaa33        ; -> return 0x04
 *   ...
 *   aa10: mov r7, #0x05
 *   aa12: ret
 *
 * Returns: 0x04 if cleared, 0x05 otherwise
 */
uint8_t power_state_helper_aa02(void)
{
    uint8_t val;

    /* Check bit 7 of USB interrupt mask */
    if (REG_USB_INT_MASK_9090 & USB_INT_MASK_GLOBAL) {
        /* Clear bit 0 of power control */
        val = REG_POWER_CTRL_92C8;
        val &= 0xFE;
        REG_POWER_CTRL_92C8 = val;
        return 0x04;
    }

    return 0x05;
}

/*
 * power_state_helper_aa13 - Clear power control bit 1 if USB bit 7 set
 * Address: 0xaa13-0xaa22 (16 bytes)
 *
 * Disassembly:
 *   aa13: mov dptr, #0x9090
 *   aa16: movx a, @dptr
 *   aa17: jnb 0xe0.7, 0xaa23 ; If bit 7 clear, return 0x05
 *   aa1a: mov dptr, #0x92c8
 *   aa1d: movx a, @dptr
 *   aa1e: anl a, #0xfd       ; Clear bit 1
 *   aa20: movx @dptr, a
 *   aa21: sjmp 0xaa33        ; -> return 0x04
 *   aa23: mov r7, #0x05
 *   aa25: ret
 */
uint8_t power_state_helper_aa13(void)
{
    uint8_t val;

    /* Check bit 7 of USB interrupt mask */
    if (REG_USB_INT_MASK_9090 & USB_INT_MASK_GLOBAL) {
        /* Clear bit 1 of power control */
        val = REG_POWER_CTRL_92C8;
        val &= 0xFD;
        REG_POWER_CTRL_92C8 = val;
        return 0x04;
    }

    return 0x05;
}

/*
 * power_state_helper_aa1d - Read power control and clear bit 1
 * Address: 0xaa1d-0xaa21 (5 bytes)
 *
 * Entry point into aa13 at the register modification.
 * Same as aa13 but without the USB check (bit 7 already verified by caller).
 */
uint8_t power_state_helper_aa1d(void)
{
    uint8_t val;

    val = REG_POWER_CTRL_92C8;
    val &= 0xFD;  /* Clear bit 1 */
    REG_POWER_CTRL_92C8 = val;

    return 0x04;
}

/*
 * power_state_helper_aa26 - Check USB bit 7 and return status
 * Address: 0xaa26-0xaa35 (16 bytes)
 *
 * Disassembly:
 *   aa26: mov dptr, #0x9090
 *   aa29: movx a, @dptr
 *   aa2a: jb 0xe0.7, 0xaa33  ; If bit 7 set, return 0x04
 *   aa2d: mov r7, #0x05
 *   aa2f: ret
 *   aa30: mov r7, #0x05
 *   aa32: ret
 *   aa33: mov r7, #0x04
 *   aa35: ret
 */
uint8_t power_state_helper_aa26(void)
{
    /* Check bit 7 of USB interrupt mask */
    if (REG_USB_INT_MASK_9090 & USB_INT_MASK_GLOBAL) {
        return 0x04;
    }

    return 0x05;
}

/*===========================================================================
 * NVMe/Command State Handler (0xaa36-0xab0c)
 *===========================================================================*/

/* Forward declarations */
extern void helper_96ae(void);
extern void cmd_trigger_params(uint8_t r7, uint8_t r5);
extern void cmd_param_setup(uint8_t r7, uint8_t r5);
extern void helper_dd0e(void);
extern void helper_95a0(uint8_t r7);

/*
 * Helper to clear command count registers
 * Address: 0x95f9-0x9604 (12 bytes)
 *
 * Clears 6 consecutive bytes starting at DPTR (E42A-E42F).
 * Returns 0 in A.
 */
static void clear_cmd_count_regs(void)
{
    REG_CMD_COUNT_LOW = 0;    /* 0xE42A */
    REG_CMD_COUNT_HIGH = 0;   /* 0xE42B */
    REG_CMD_LENGTH_LOW = 0;   /* 0xE42C */
    REG_CMD_LENGTH_HIGH = 0;  /* 0xE42D */
    REG_CMD_RESP_TAG = 0;     /* 0xE42E */
    REG_CMD_RESP_STATUS = 0;  /* 0xE42F */
}

/*
 * nvme_cmd_state_handler - NVMe command state handler
 * Address: 0xaa36-0xab0c (~215 bytes)
 *
 * This function handles NVMe command state transitions.
 * It reads G_CMD_MODE (0x07CA) to determine the command type
 * and sets up command registers at 0xE426-0xE435.
 *
 * Flow:
 *   1. Call helper_96ae() to check status
 *   2. If status check fails, call error handler and return
 *   3. Configure command engine based on G_CMD_MODE
 *   4. Set up LBA registers (E426-E429)
 *   5. Clear count registers (E42A-E42F)
 *   6. Set control/timeout registers (E430-E435)
 *   7. Update G_CMD_STATUS based on mode
 */
void nvme_cmd_state_handler(void)
{
    uint8_t mode;
    uint8_t param;
    uint8_t flash_cmd_type;
    uint8_t event_flags;
    uint8_t tlp_ctrl;

    /* Call status check helper - original checks (~A | R2) == 0 */
    /* For now, assume check passes and continue */
    helper_96ae();

    /* Read command mode */
    mode = G_CMD_MODE;

    /* Set parameter based on mode: 0x05 if mode==2, else 0x04 */
    param = (mode == 2) ? 0x05 : 0x04;

    /* Configure command engine */
    cmd_trigger_params(0x0F, param);
    cmd_param_setup(0x01, 0x01);

    /* Write LBA registers */
    REG_CMD_LBA_0 = 0x4C;      /* 0xE426 = 'L' */
    REG_CMD_LBA_1 = 0x17;      /* 0xE427 */

    /* Read mode again and set LBA_2 */
    mode = G_CMD_MODE;
    if (mode == 2) {
        REG_CMD_LBA_2 = 0x40;  /* 0xE428 = '@' */
    } else {
        REG_CMD_LBA_2 = 0x00;
    }

    /* Read flash command type */
    flash_cmd_type = G_FLASH_CMD_TYPE;

    /* Set LBA_3 based on flash cmd type and event flags */
    if (flash_cmd_type == 0) {
        event_flags = G_EVENT_FLAGS;
        if (event_flags & 0x80) {
            /* Bit 7 set: use 'T' */
            REG_CMD_LBA_3 = 0x54;  /* 0xE429 = 'T' */
        } else {
            /* Bit 7 clear: use 'P' */
            REG_CMD_LBA_3 = 0x50;  /* 0xE429 = 'P' */
        }
    } else {
        /* Flash cmd type non-zero: use 'P' */
        REG_CMD_LBA_3 = 0x50;
    }

    /* Clear count registers E42A-E42F */
    clear_cmd_count_regs();

    /* Write control and timeout from globals */
    REG_CMD_CTRL = G_CMD_CTRL_PARAM;      /* 0xE430 = 0x0A57 value */
    REG_CMD_TIMEOUT = G_CMD_TIMEOUT_PARAM; /* 0xE431 = 0x0A58 value */

    /* Check if mode == 2 for extended setup */
    mode = G_CMD_MODE;
    if (mode == 2) {
        /* Read event flags and compute TLP control */
        event_flags = G_EVENT_FLAGS;
        param = event_flags & 0x03;

        /* Set TLP control base value */
        tlp_ctrl = (param != 0) ? 0x03 : 0x02;
        G_TLP_COUNT_HI = tlp_ctrl;

        /* If event flags bit 7 set, add 0x08 */
        if (event_flags & 0x80) {
            tlp_ctrl = G_TLP_COUNT_HI;
            tlp_ctrl |= 0x08;
            G_TLP_COUNT_HI = tlp_ctrl;
        }

        /* Set param registers based on flash cmd type */
        if (flash_cmd_type != 0) {
            REG_CMD_PARAM_L = 0x02;  /* 0xE432 */
        } else {
            REG_CMD_PARAM_L = G_TLP_COUNT_HI;
        }
    }

    /* Set remaining param registers */
    REG_CMD_PARAM_H = 0x00;       /* 0xE433 */
    REG_CMD_EXT_PARAM_0 = 0x80;   /* 0xE434 */

    /* Set E435 based on flash_cmd_type and mode flags */
    if (flash_cmd_type == 0) {
        event_flags = G_EVENT_FLAGS;
        param = event_flags & 0x03;
        if (param != 0) {
            REG_CMD_EXT_PARAM_1 = 0x6D;  /* 'm' */
        } else {
            REG_CMD_EXT_PARAM_1 = 0x65;  /* 'e' */
        }
    } else {
        REG_CMD_EXT_PARAM_1 = 0x65;  /* 'e' */
    }

    /* Update command status based on mode */
    mode = G_CMD_MODE;
    if (mode == 2) {
        G_CMD_STATUS = 0x16;
    } else {
        G_CMD_STATUS = 0x12;
    }
}

/*
 * nvme_cmd_error_handler - Error handler for command state
 * Address: 0xab0d-0xab15 (9 bytes)
 *
 * Called when command check fails. Calls helper_dd0e and helper_95a0.
 */
void nvme_cmd_error_handler(void)
{
    helper_dd0e();
    helper_95a0(0x01);
}

/*===========================================================================
 * PCIe Link State Functions (0xa2c2-0xa3da)
 *===========================================================================*/

/*
 * pcie_link_config_a2c2 - Configure PCIe link state registers
 * Address: 0xa2c2-0xa2ea (41 bytes)
 *
 * Modifies PCIe extended registers 0x1235-0x1237:
 *   - Reg 0x35: (val & 0xC0) | 0x01
 *   - Reg 0x35: (val & 0x3F) | 0x40
 *   - Reg 0x36: 0xD2
 *   - Reg 0x37: (val & 0xE0)
 *
 * Disassembly:
 *   a2c2: mov r3, #0x02      ; Bank 2
 *   a2c4: mov r2, #0x12      ; High byte
 *   a2c6: mov r1, #0x35      ; Offset
 *   a2c8: lcall 0x0bc8       ; Read
 *   a2cb: anl a, #0xc0       ; Mask bits 6-7
 *   a2cd: orl a, #0x01       ; Set bit 0
 *   a2cf: lcall 0x0be6       ; Write
 *   a2d2: lcall 0x0bc8       ; Read again
 *   a2d5: anl a, #0x3f       ; Mask bits 0-5
 *   a2d7: orl a, #0x40       ; Set bit 6
 *   a2d9: lcall 0x0be6       ; Write
 *   a2dc: mov a, #0xd2       ; Value 0xD2
 *   a2de: inc r1             ; Offset = 0x36
 *   a2df: lcall 0x0be6       ; Write 0xD2 to reg 0x36
 *   a2e2: inc r1             ; Offset = 0x37
 *   a2e3: lcall 0x0bc8       ; Read reg 0x37
 *   a2e6: anl a, #0xe0       ; Mask bits 5-7
 *   a2e8: ljmp 0x0be6        ; Write and return
 */
void pcie_link_config_a2c2(void)
{
    uint8_t val;

    /* Configure register 0x35 */
    val = REG_PCIE_LINK_CFG;
    val = (val & 0xC0) | 0x01;
    REG_PCIE_LINK_CFG = val;

    val = REG_PCIE_LINK_CFG;
    val = (val & 0x3F) | 0x40;
    REG_PCIE_LINK_CFG = val;

    /* Write 0xD2 to register 0x36 */
    REG_PCIE_LINK_PARAM = 0xD2;

    /* Configure register 0x37 */
    val = REG_PCIE_LINK_STATUS_EXT;
    val &= 0xE0;
    REG_PCIE_LINK_STATUS_EXT = val;
}

/*
 * pcie_set_state_a2df - Set PCIe state register (entry at 0xa2df)
 * Address: 0xa2df-0xa2ea (12 bytes)
 *
 * This is an entry point into pcie_link_config_a2c2 that writes
 * a value to register 0x36 and then modifies register 0x37.
 *
 * The param is the value to write to register 0x36.
 *
 * Disassembly:
 *   a2df: lcall 0x0be6       ; Write A to reg (R1=0x36)
 *   a2e2: inc r1             ; R1 = 0x37
 *   a2e3: lcall 0x0bc8       ; Read reg 0x37
 *   a2e6: anl a, #0xe0       ; Mask bits 5-7
 *   a2e8: ljmp 0x0be6        ; Write and return
 */
void pcie_set_state_a2df(uint8_t state)
{
    uint8_t val;

    /* Write state to register 0x36 */
    REG_PCIE_LINK_PARAM = state;

    /* Read-modify-write register 0x37: keep only bits 5-7 */
    val = REG_PCIE_LINK_STATUS_EXT;
    val &= 0xE0;
    REG_PCIE_LINK_STATUS_EXT = val;
}

/*
 * pcie_lane_write_cc_a2eb - Write 0xCC to lane config registers
 * Address: 0xa2eb-0xa2fe (20 bytes)
 *
 * Writes 0xCC to registers 0x3C and 0x3D, 0x08 to register 0x3E,
 * then calls 0xe7fb and reads register 0x34.
 *
 * Disassembly:
 *   a2eb: mov r1, #0x3c
 *   a2ed: mov a, #0xcc
 *   a2ef: lcall 0x0be6       ; Write 0xCC to 0x3C
 *   a2f2: inc r1             ; R1 = 0x3D
 *   a2f3: lcall 0x0be6       ; Write 0xCC to 0x3D
 *   a2f6: mov a, #0x08
 *   a2f8: inc r1             ; R1 = 0x3E
 *   a2f9: lcall 0x0be6       ; Write 0x08 to 0x3E
 *   a2fc: lcall 0xe7fb       ; Call helper
 *   a2ff: (continues to read 0x34)
 */
void pcie_lane_write_cc_a2eb(void)
{
    /* Write 0xCC to registers 0x3C and 0x3D */
    REG_PCIE_EXT_CFG_0 = 0xCC;
    REG_PCIE_EXT_CFG_1 = 0xCC;

    /* Write 0x08 to register 0x3E */
    REG_PCIE_EXT_CFG_2 = 0x08;

    /* Call delay/init helper - implemented elsewhere */
    /* helper_e7fb(); */
}

/*
 * pcie_read_link_state_a2ff - Read PCIe link state from register 0x34
 * Address: 0xa2ff-0xa307 (9 bytes)
 *
 * Sets up R1=0x34, R3=0x02, R2=0x12 and jumps to 0x0bc8 (read).
 * Returns the value of PCIe extended register 0x34.
 */
uint8_t pcie_read_link_state_a2ff(void)
{
    return REG_PCIE_LINK_STATE_EXT;
}

/*
 * pcie_setup_lane_a308 - Set up lane config with 0x0F
 * Address: 0xa308-0xa31b (20 bytes)
 *
 * Modifies register 0x34: (val & 0xF0) | 0x0F
 * Then modifies register 0x35: (val & 0x3F) | 0x80
 *
 * Disassembly:
 *   a308: anl a, #0xf0       ; Entry with A from caller
 *   a30a: orl a, #0x0f       ; Set low nibble
 *   a30c: lcall 0x0be6       ; Write to 0x34
 *   a30f: inc r1             ; R1 = 0x35
 *   a310: lcall 0x0bc8       ; Read 0x35
 *   a313: anl a, #0x3f       ; Mask bits 0-5
 *   a315: orl a, #0x80       ; Set bit 7
 *   a317: lcall 0x0be6       ; Write
 *   a31a: inc r1             ; R1 = 0x36
 *   a31b: ret
 */
void pcie_setup_lane_a308(uint8_t link_state)
{
    uint8_t val;

    /* Modify register 0x34 */
    val = (link_state & 0xF0) | 0x0F;
    REG_PCIE_LINK_STATE_EXT = val;

    /* Modify register 0x35 */
    val = REG_PCIE_LINK_CFG;
    val = (val & 0x3F) | 0x80;
    REG_PCIE_LINK_CFG = val;
}

/*
 * pcie_setup_lane_a310 - Set up lane config (entry at read)
 * Address: 0xa310-0xa31b (12 bytes)
 *
 * Reads register 0x35, modifies it, writes back.
 * This is an entry point within pcie_setup_lane_a308.
 */
void pcie_setup_lane_a310(uint8_t lane)
{
    uint8_t val;
    (void)lane;  /* Lane param may be used for multi-lane setup */

    /* Modify register 0x35: set bit 7 */
    val = REG_PCIE_LINK_CFG;
    val = (val & 0x3F) | 0x80;
    REG_PCIE_LINK_CFG = val;
}

/*
 * pcie_lane_setup_a31c - Alternative lane configuration
 * Address: 0xa31c-0xa333 (24 bytes)
 *
 * Writes to current register, reads next, modifies with (val & 0xC0) | 0x04,
 * then reads again and modifies with (val & 0x3F) | 0x40.
 */
void pcie_lane_setup_a31c(uint8_t val)
{
    uint8_t reg_val;

    /* Write value to current register (assumed R1 is set by caller) */
    REG_PCIE_LINK_CFG = val;

    /* Read-modify-write: (val & 0xC0) | 0x04 */
    reg_val = REG_PCIE_LINK_PARAM;
    reg_val = (reg_val & 0xC0) | 0x04;
    REG_PCIE_LINK_PARAM = reg_val;

    /* Read-modify-write: (val & 0x3F) | 0x40 */
    reg_val = REG_PCIE_LINK_PARAM;
    reg_val = (reg_val & 0x3F) | 0x40;
    REG_PCIE_LINK_PARAM = reg_val;
}

/*
 * pcie_read_status_a334 - Read PCIe status register 0x35
 * Address: 0xa334-0xa33d (10 bytes)
 *
 * Sets R1=0x35, R3=0x02, R2=0x12 and jumps to 0x0bc8.
 */
uint8_t pcie_read_status_a334(void)
{
    return REG_PCIE_LINK_CFG;
}

/*
 * pcie_read_status_a33d - Read PCIe status (alternate)
 * Address: 0xa33d-0xa343 (7 bytes)
 *
 * Sets R3=0x02, R2=0x12 and jumps to 0x0bc8.
 * R1 is expected to be set by caller.
 */
uint8_t pcie_read_status_a33d(uint8_t reg_offset)
{
    return PCIE_EXT_REG(reg_offset);
}

/*
 * pcie_setup_all_lanes_a344 - Set up all lane bits
 * Address: 0xa344-0xa34e (11 bytes)
 *
 * Modifies register (val & 0xF0) | 0x0F, then reads next.
 */
void pcie_setup_all_lanes_a344(uint8_t val)
{
    uint8_t reg_val;

    /* Modify with full lane mask */
    reg_val = (val & 0xF0) | 0x0F;
    REG_PCIE_LINK_STATE_EXT = reg_val;
}

/*
 * pcie_get_status_a348 - Get status with modification
 * Address: 0xa348-0xa34e (7 bytes)
 *
 * Entry point that writes modified value and reads next register.
 */
uint8_t pcie_get_status_a348(uint8_t val)
{
    /* Write modified value to register 0x34 */
    REG_PCIE_LINK_STATE_EXT = (val & 0xF0) | 0x0F;

    /* Read register 0x35 */
    return REG_PCIE_LINK_CFG;
}

/*
 * pcie_get_status_a34f - Read status from register 0x4E
 * Address: 0xa34f-0xa357 (9 bytes)
 *
 * Reads from PCIe extended register 0x4E.
 */
uint8_t pcie_get_status_a34f(void)
{
    return REG_PCIE_EXT_STATUS_ALT;
}

/*
 * pcie_modify_and_read_a358 - Modify register and read next
 * Address: 0xa358-0xa364 (13 bytes)
 *
 * Read current register, (val & 0xFD) | 0x02, write back, read from same.
 */
uint8_t pcie_modify_and_read_a358(void)
{
    uint8_t val;

    /* Read-modify-write with clear bit 1, set bit 1 (effectively just read) */
    val = REG_PCIE_EXT_STATUS_ALT;
    val = (val & 0xFD) | 0x02;
    REG_PCIE_EXT_STATUS_ALT = val;

    return REG_PCIE_EXT_STATUS_ALT;
}

/*
 * pcie_modify_and_read_a35f - Clear and set bit 1
 * Address: 0xa35f-0xa364 (6 bytes)
 *
 * Entry point that just does the write and read.
 */
uint8_t pcie_modify_and_read_a35f(void)
{
    uint8_t val;

    val = REG_PCIE_EXT_STATUS_ALT;
    val = (val & 0xFD) | 0x02;
    REG_PCIE_EXT_STATUS_ALT = val;

    return val;
}

/*
 * pcie_write_66_a365 - Write 0x66 to lane config registers
 * Address: 0xa365-0xa371 (13 bytes)
 *
 * Writes 0x66 to registers 0x3C and 0x3D, increments R1 twice and returns.
 */
void pcie_write_66_a365(void)
{
    REG_PCIE_EXT_CFG_0 = 0x66;
    REG_PCIE_EXT_CFG_1 = 0x66;
}

/*
 * pcie_get_status_a372 - Read status from register 0x40
 * Address: 0xa372-0xa37a (9 bytes)
 */
uint8_t pcie_get_status_a372(void)
{
    return REG_PCIE_EXT_STATUS_RD;
}

/*
 * pcie_store_status_a37b - Store status to global
 * Address: 0xa37b-0xa38a (16 bytes)
 *
 * Stores A to G_PCIE_STATUS_0B35, copies from IDATA[0x05] to R7,
 * calls 0xce23, and sets up for return.
 */
void pcie_store_status_a37b(uint8_t status)
{
    G_PCIE_STATUS_0B35 = status;

    /* Get value from IDATA[0x05] */
    /* Note: In C, this would be passed differently. */
    /* helper_ce23(I_WORK_05); */
}

/*
 * pcie_setup_a38b - Set up PCIe with source parameter
 * Address: 0xa38b-0xa393 (9 bytes)
 *
 * Sets R3=0x02, R2=0x12, writes 0x01 via helper.
 */
void pcie_setup_a38b(uint8_t source)
{
    (void)source;
    /* Write 0x01 to the source-indexed register */
    /* The actual register is determined by caller's R1 */
}

/*
 * pcie_check_int_source_a3c4 - Check interrupt source
 * Address: 0xa3c4-0xa3ca (7 bytes)
 *
 * Similar pattern to a38b but reads instead of writes.
 */
uint8_t pcie_check_int_source_a3c4(uint8_t source)
{
    (void)source;
    return 0;  /* No interrupt pending by default */
}

/*===========================================================================
 * PCIe Register Modify Helper (0xa3db)
 *===========================================================================*/

/*
 * pcie_reg_set_bit4_a3db - Modify PCIe register: clear bit 4, set bit 4
 * Address: 0xa3db-0xa3e1 (7 bytes)
 *
 * Takes value from caller in A, modifies bits, writes via banked store.
 * Effectively just ensures bit 4 is set (clears then sets).
 *
 * Disassembly:
 *   a3db: anl a, #0xef       ; Clear bit 4
 *   a3dd: orl a, #0x10       ; Set bit 4
 *   a3df: ljmp 0x0be6        ; banked_store_byte
 *
 * Note: This is part of a larger function sequence that sets up R1, R2, R3
 * for banked memory access before calling this. The effective operation
 * is to ensure bit 4 is set in the target register.
 */
void pcie_reg_set_bit4_a3db(uint8_t val)
{
    uint8_t result;

    /* Clear bit 4, then set bit 4 (effectively ensures bit 4 is always set) */
    result = (val & 0xEF) | 0x10;

    /* Write to PCIe extended register via banked access */
    /* The register offset is set up by the caller in R1 */
    /* For now, we assume this writes to the current PCIE_EXT_REG context */
    REG_PCIE_LINK_CFG = result;  /* Most common target based on call context */
}

/*===========================================================================
 * PCIe Config Init Function (0xa3f5)
 *===========================================================================*/

/* Forward declaration for helper_dd42 */
extern void helper_dd42(uint8_t param);
extern void helper_e7c1(uint8_t param);
extern void dispatch_event_e0d9(uint8_t param);

/*
 * pcie_config_init_a3f5 - Initialize PCIe configuration registers
 * Address: 0xa3f5-0xa470+ (~124 bytes)
 *
 * Checks G_STATE_FLAG_0AF1 bit 0, and if set:
 *   - Modifies REG_LINK_STATUS_E716: (val & 0xFC) | 0x03
 *   - Modifies REG_CPU_CTRL_CA81: val & 0xFE (clear bit 0)
 *   - Modifies REG_CPU_MODE_NEXT: (val & 0x1F) | 0x60
 * Then calls helper functions dd42, e7c1, 057a and continues.
 *
 * Disassembly:
 *   a3f5: mov dptr, #0x0af1  ; G_STATE_FLAG_0AF1
 *   a3f8: movx a, @dptr      ; Read
 *   a3f9: jnb 0xe0.0, 0xa415 ; If bit 0 clear, skip init
 *   a3fc: mov dptr, #0xe716  ; REG_LINK_STATUS_E716
 *   a3ff: movx a, @dptr      ; Read
 *   a400: anl a, #0xfc       ; Clear bits 0,1
 *   a402: orl a, #0x03       ; Set bits 0,1
 *   a404: movx @dptr, a      ; Write
 *   a405: mov dptr, #0xca81  ; REG_CPU_CTRL_CA81
 *   a408: movx a, @dptr      ; Read
 *   a409: anl a, #0xfe       ; Clear bit 0
 *   a40b: movx @dptr, a      ; Write
 *   a40c: mov dptr, #0xca06  ; REG_CPU_MODE_NEXT
 *   a40f: movx a, @dptr      ; Read
 *   a410: anl a, #0x1f       ; Keep bits 0-4
 *   a412: orl a, #0x60       ; Set bits 5,6
 *   a414: movx @dptr, a      ; Write
 *   a415: clr a              ; R7 = 0
 *   a416: mov r7, a
 *   a417: lcall 0xdd42       ; Call helper
 *   a41a: mov r7, #0x01
 *   a41c: lcall 0xe7c1       ; Call helper
 *   a41f: clr a
 *   a420: mov r7, a
 *   a421: lcall 0x057a       ; Call helper
 *   ...
 */
void pcie_config_init_a3f5(void)
{
    uint8_t val;

    /* Check G_STATE_FLAG_0AF1 bit 0 */
    if (G_STATE_FLAG_0AF1 & 0x01) {
        /* Modify REG_LINK_STATUS_E716: (val & 0xFC) | 0x03 */
        val = REG_LINK_STATUS_E716;
        val = (val & 0xFC) | 0x03;
        REG_LINK_STATUS_E716 = val;

        /* Modify REG_CPU_CTRL_CA81: clear bit 0 */
        val = REG_CPU_CTRL_CA81;
        val &= 0xFE;
        REG_CPU_CTRL_CA81 = val;

        /* Modify REG_CPU_MODE_NEXT: (val & 0x1F) | 0x60 */
        val = REG_CPU_MODE_NEXT;
        val = (val & 0x1F) | 0x60;
        REG_CPU_MODE_NEXT = val;
    }

    /* Call helper functions */
    helper_dd42(0x00);
    helper_e7c1(0x01);
    dispatch_event_e0d9(0x00);

    /* Continue with state update - implementation follows at 0xa424+ */
    /* The full function is longer but the core PCIe init is above */
}

/*===========================================================================
 * System State Clear Function (0xbfc4)
 *===========================================================================*/

/* Forward declarations */
extern void scsi_state_clear(void);
extern void phy_set_config_bit0(void);

/*
 * system_state_clear_bfc4 - Clear system state globals
 * Address: 0xbfc4-0xbfff (60 bytes)
 *
 * Clears multiple system state globals by writing A (0) to them,
 * then calls two helper functions and modifies REG_POWER_CTRL_92C8.
 *
 * Disassembly:
 *   bfc4: mov dptr, #0x0b2e  ; G_USB_TRANSFER_FLAG
 *   bfc7: movx @dptr, a      ; Clear
 *   bfc8: mov dptr, #0x07e5  ; G_TRANSFER_ACTIVE
 *   bfcb: movx @dptr, a
 *   bfcc: mov dptr, #0x0003  ; G_EP_STATUS_CTRL
 *   bfcf: movx @dptr, a
 *   bfd0: mov dptr, #0x0006  ; G_WORK_0006
 *   bfd3: movx @dptr, a
 *   bfd4: mov dptr, #0x07eb  ; G_SYS_FLAGS_07EB
 *   bfd7: movx @dptr, a
 *   bfd8: mov dptr, #0x07e8  ; G_SYS_FLAGS_07E8
 *   bfdb: movx @dptr, a
 *   bfdc: mov dptr, #0x0b3c  ; G_STATE_CTRL_0B3C
 *   bfdf: movx @dptr, a
 *   bfe0: mov dptr, #0x07ed  ; G_SYS_FLAGS_07ED
 *   bfe3: movx @dptr, a
 *   bfe4: lcall 0x545c       ; Call helper
 *   bfe7: lcall 0xcb05       ; Call helper
 *   bfea: mov dptr, #0x92c8  ; REG_POWER_CTRL_92C8
 *   bfed: movx a, @dptr      ; Read
 *   ...
 */
void system_state_clear_bfc4(void)
{
    /* Clear A (value to write) - in original code A=0 from prior context */
    uint8_t val = 0;

    /* Clear system state globals */
    G_USB_TRANSFER_FLAG = val;      /* 0x0B2E */
    G_TRANSFER_ACTIVE = val;        /* 0x07E5 */
    G_EP_STATUS_CTRL = val;         /* 0x0003 */
    G_WORK_0006 = val;              /* 0x0006 */
    XDATA_VAR8(0x07EB) = val;       /* G_SYS_FLAGS_07EB */
    G_SYS_FLAGS_07E8 = val;         /* 0x07E8 */
    XDATA_VAR8(0x0B3C) = val;       /* G_STATE_CTRL_0B3C */
    G_SYS_FLAGS_07ED = val;         /* 0x07ED */

    /* Call helper functions */
    scsi_state_clear();
    phy_set_config_bit0();

    /* Read-modify power control register */
    val = REG_POWER_CTRL_92C8;
    /* Continue with modification... */
}

/*===========================================================================
 * USB Descriptor Buffer Helpers (0xa637-0xa660)
 *===========================================================================*/

/*
 * usb_descriptor_helper_a644 - Calculate descriptor buffer address
 * Address: 0xa644-0xa650 (13 bytes)
 *
 * Calculates buffer address for USB descriptor operations.
 * Sets DPTR = 0x9E00 + offset with adjustment.
 *
 * Parameters are passed in R7 (offset) and prior A value.
 */
void usb_descriptor_helper_a644(uint8_t adjustment, uint8_t offset)
{
    /* Calculate address: 0x9E00 + offset + (A - 0x58) adjustment */
    uint16_t addr = 0x9E00 + offset + (adjustment - 0x58);
    (void)addr;  /* Address is used via DPTR in original */
}

/*
 * usb_descriptor_helper_a648 - Entry point at add instruction
 * Address: 0xa648-0xa650 (9 bytes)
 *
 * Alternate entry point for address calculation.
 */
void usb_descriptor_helper_a648(void)
{
    /* Simplified stub - actual implementation uses DPTR directly */
}

/*
 * usb_descriptor_helper_a655 - Calculate and write to descriptor buffer
 * Address: 0xa655-0xa65f (11 bytes)
 *
 * Entry point into write path.
 */
void usb_descriptor_helper_a655(uint8_t offset, uint8_t value)
{
    /* Write value to descriptor buffer at calculated address */
    uint16_t addr = 0x9E00 + offset;
    XDATA_REG8(addr) = value;
}

/*===========================================================================
 * Queue Index Helpers (0xaa09-0xaa35)
 *===========================================================================*/

/*
 * queue_index_return_04 - Return 0x04
 * Address: 0xaa33-0xaa35 (3 bytes)
 */
uint8_t queue_index_return_04(void)
{
    return 0x04;
}

/*
 * queue_index_return_05 - Return 0x05
 * Address: 0xaa10-0xaa12 (also 0xaa23, 0xaa2d, 0xaa30)
 */
uint8_t queue_index_return_05(void)
{
    return 0x05;
}

/*===========================================================================
 * High-Call-Count Queue Functions (0xaa09-0xaab5)
 *===========================================================================*/

/*
 * queue_helper_aa09 - Queue index helper
 * Address: 0xaa09-0xaa0e (6 bytes)
 *
 * Part of power state handling, modifies REG_POWER_CTRL_92C8.
 */
uint8_t queue_helper_aa09(void)
{
    uint8_t val;

    val = REG_POWER_CTRL_92C8;
    val &= 0xFE;  /* Clear bit 0 */
    REG_POWER_CTRL_92C8 = val;

    return 0x04;
}

/*
 * queue_helper_aa2b - Queue helper with power check
 * Address: 0xaa2b-0xaa32 (8 bytes)
 */
uint8_t queue_helper_aa2b(void)
{
    return 0x05;
}

/*
 * queue_helper_aa42 - Queue dispatch based on mode
 * Address: 0xaa42-0xaa4c (11 bytes)
 */
void queue_helper_aa42(void)
{
    /* Dispatch based on G_CMD_MODE */
    /* Implementation depends on context */
}

/*
 * queue_helper_aa4e - Queue state update
 * Address: 0xaa4e-0xaa56 (9 bytes)
 */
void queue_helper_aa4e(void)
{
    /* State update */
}

/*
 * queue_helper_aa57 - Queue buffer setup
 * Address: 0xaa57-0xaa70 (26 bytes)
 */
void queue_helper_aa57(void)
{
    /* Buffer setup */
}

/*
 * queue_helper_aa71 - Queue transfer setup
 * Address: 0xaa71-0xaa7c (12 bytes)
 */
void queue_helper_aa71(void)
{
    /* Transfer setup */
}

/*
 * queue_helper_aa7d - Read buffer address
 * Address: 0xaa7d-0xaa8f (19 bytes)
 */
void queue_helper_aa7d(void)
{
    /* Read buffer address setup */
}

/*
 * queue_helper_aa7f - Entry within aa7d
 * Address: 0xaa7f-0xaa8f (17 bytes)
 */
void queue_helper_aa7f(void)
{
    /* Buffer address calculation */
}

/*
 * queue_helper_aa90 - Queue status check
 * Address: 0xaa90-0xaaaa (27 bytes)
 */
void queue_helper_aa90(void)
{
    /* Status check */
}

/*
 * queue_helper_aaab - Short helper
 * Address: 0xaaab-0xaaac (2 bytes)
 */
void queue_helper_aaab(void)
{
    /* Minimal operation */
}

/*
 * queue_helper_aaad - Short helper
 * Address: 0xaaad-0xaab4 (8 bytes)
 */
void queue_helper_aaad(void)
{
    /* Minimal operation */
}

/*
 * queue_helper_aab5 - Buffer write helper
 * Address: 0xaab5-0xaade (42 bytes)
 */
void queue_helper_aab5(void)
{
    /* Buffer write operations */
}

/*===========================================================================
 * Buffer/DMA Support Functions (0xb6d4-0xbf9a)
 *===========================================================================*/

/*
 * buffer_helper_b6d4 - DMA buffer helper
 * Address: 0xb6d4-0xb6ef (28 bytes)
 *
 * Called 7 times - sets up DMA buffer parameters.
 */
void buffer_helper_b6d4(void)
{
    /* DMA buffer setup */
}

/*
 * buffer_helper_b6f0 - DMA transfer helper
 * Address: 0xb6f0-0xb6f9 (10 bytes)
 *
 * Called 5 times.
 */
void buffer_helper_b6f0(void)
{
    /* DMA transfer initiation */
}

/*
 * buffer_helper_b6fa - DMA status helper
 * Address: 0xb6fa-0xb730 (55 bytes)
 *
 * Called 8 times.
 */
void buffer_helper_b6fa(void)
{
    /* DMA status check */
}

/*
 * queue_status_bc9f - Queue status check
 * Address: 0xbc9f-0xbcfd (95 bytes)
 *
 * Called 5 times.
 */
uint8_t queue_status_bc9f(void)
{
    return 0;  /* Default: no pending status */
}

/*
 * cmd_queue_handler - Queue event handler
 * Address: 0xbcfe-0xbd30 (51 bytes)
 *
 * Called 7 times.
 */
void cmd_queue_handler(void)
{
    /* Queue event handling */
}

/*
 * buffer_state_handler - State transition handler
 * Address: 0xbf9a-0xbfb7 (30 bytes)
 *
 * Called 6 times.
 */
void buffer_state_handler(void)
{
    /* State transition */
}

/*
 * transfer_state_handler - State update handler
 * Address: 0xbfb8-0xbfc3 (12 bytes)
 *
 * Called 6 times.
 */
void transfer_state_handler(void)
{
    /* State update */
}

/*===========================================================================
 * Queue Dispatch Functions (0xab27-0xabc8)
 *===========================================================================*/

/*
 * queue_dispatch_ab27 - Main queue dispatch
 * Address: 0xab27-0xab39 (19 bytes)
 *
 * Called 6 times - dispatches based on queue index.
 */
void queue_dispatch_ab27(void)
{
    /* Queue dispatch logic */
}

/*
 * queue_dispatch_ab3a - Queue secondary dispatch
 * Address: 0xab3a-0xab50 (23 bytes)
 *
 * Called 3 times.
 */
void queue_dispatch_ab3a(void)
{
    /* Secondary dispatch */
}

/*
 * queue_setup_abc9 - Queue initialization
 * Address: 0xabc9-0xac00 (56 bytes)
 *
 * Called 4 times.
 */
void queue_setup_abc9(void)
{
    /* Queue initialization */
}

/* Note: helper_96ae, cmd_trigger_params, cmd_param_setup, scsi_state_clear, phy_set_config_bit0
 * are defined in stubs.c */

/*===========================================================================
 * PCIe Register Read Functions (0xa2ff-0xa32f)
 *===========================================================================*/

/*
 * pcie_read_reg_34_a2ff - Read PCIe extended register 0x34
 * Address: 0xa2ff-0xa307 (9 bytes)
 *
 * Sets up R1=0x34, R3=0x02, R2=0x12 and jumps to helper_0bc8 (read).
 * Returns the value from PCIe extended register 0x1234.
 */
uint8_t pcie_read_reg_34_a2ff(void)
{
    return REG_PCIE_LINK_STATE_EXT;
}

/*
 * pcie_write_and_read_a308 - Modify and write back PCIe register
 * Address: 0xa308-0xa31b (20 bytes)
 *
 * Reads reg 0x34, (val & 0xF0) | 0x0F, writes back, increments R1,
 * reads reg 0x35, (val & 0x3F) | 0x80, writes back, returns.
 */
void pcie_write_and_read_a308(void)
{
    uint8_t val;

    /* Read register 0x34, modify, write back */
    val = REG_PCIE_LINK_STATE_EXT;
    val = (val & 0xF0) | 0x0F;
    REG_PCIE_LINK_STATE_EXT = val;

    /* Read register 0x35, modify, write back */
    val = REG_PCIE_LINK_CFG;
    val = (val & 0x3F) | 0x80;
    REG_PCIE_LINK_CFG = val;
}

/*===========================================================================
 * State Clear Functions (0xbfc4-0xbfff)
 *===========================================================================*/

/* External helper declarations */
extern void scsi_state_clear(void);
extern void phy_set_config_bit0(void);

/*
 * state_clear_all_bfc4 - Clear state variables to a value
 * Address: 0xbfc4-0xbff5 (~50 bytes)
 *
 * Clears multiple state variables to the value in A (passed as param):
 * - G_USB_TRANSFER_FLAG (0x0B2E)
 * - G_TRANSFER_ACTIVE (0x07E5)
 * - G_EP_STATUS_CTRL (0x0003)
 * - G_WORK_0006 (0x0006)
 * - G_SYS_FLAGS_07EB (0x07EB)
 * - G_SYS_FLAGS_07E8 (0x07E8)
 * - G_STATE_CTRL_0B3C (0x0B3C)
 * - G_SYS_FLAGS_07ED (0x07ED)
 * Then calls scsi_state_clear and phy_set_config_bit0.
 * Finally clears bits 0 and 1 of REG_POWER_CTRL_92C8.
 */
void state_clear_all_bfc4(uint8_t val)
{
    uint8_t reg_val;

    /* Clear state variables to val */
    G_USB_TRANSFER_FLAG = val;    /* 0x0B2E */
    G_TRANSFER_ACTIVE = val;      /* 0x07E5 */
    G_EP_STATUS_CTRL = val;       /* 0x0003 */
    G_WORK_0006 = val;            /* 0x0006 */
    G_SYS_FLAGS_07EB = val;       /* 0x07EB */
    G_SYS_FLAGS_07E8 = val;       /* 0x07E8 */
    G_STATE_CTRL_0B3C = val;      /* 0x0B3C */
    G_SYS_FLAGS_07ED = val;       /* 0x07ED */

    /* Call helpers */
    scsi_state_clear();
    phy_set_config_bit0();

    /* Clear bits 0 and 1 of power control register */
    reg_val = REG_POWER_CTRL_92C8;
    reg_val &= 0xFE;  /* Clear bit 0 */
    REG_POWER_CTRL_92C8 = reg_val;

    reg_val = REG_POWER_CTRL_92C8;
    reg_val &= 0xFD;  /* Clear bit 1 */
    REG_POWER_CTRL_92C8 = reg_val;
}


/* ============================================================
 * Functions moved from stubs.c
 * ============================================================ */

/*
 * queue_buf_addr_high - Calculate queue buffer address high byte
 * Address: 0x504f-0x505c (14 bytes)
 *
 * From ghidra.c: bVar1 = DAT_EXTMEM_0a84; return -(((0xf3 < bVar1) << 7) >> 7)
 * Returns 0x00 if XDATA[0x0A84] <= 0xF3, otherwise 0xFF (borrow indicator)
 */
uint8_t queue_buf_addr_high(void)
{
    uint8_t idx = G_ACTION_PARAM_0A84;
    /* Returns high byte adjustment based on index value */
    if (idx > 0xF3) {
        return 0xFF;  /* Borrow occurred */
    }
    return 0x00;
}

__xdata uint8_t * queue_calc_dptr_c44f(void)
{
    uint16_t addr;
    uint8_t idx = I_WORK_21;

    /* Calculate: 0x057E + (idx * 10) */
    addr = 0x057E + (idx * 10);

    return (__xdata uint8_t *)addr;
}

uint8_t queue_check_status_c4a9(void)
{
    uint8_t idx = I_WORK_21;
    uint8_t count = G_QUEUE_COUNT_06E5;
    return (idx < count) ? 1 : 0;
}
