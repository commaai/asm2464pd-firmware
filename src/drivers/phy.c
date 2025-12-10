/*
 * phy.c - PHY/Link Control Driver
 *
 * See drivers/phy.h for hardware documentation.
 */

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

/*
 * phy_register_config - PHY/Register Configuration
 * Address: 0x0589-0x058d (5 bytes) -> dispatches to bank 0 0xD894
 *
 * Function at 0xD894:
 * PHY and system register configuration handler.
 * Configures PCIe/USB interface registers.
 *
 * Algorithm:
 *   1. Call 0xBC8F, mask with 0xFD, call 0x0BE6 (write)
 *   2. Read 0xC809, clear bit 1, set bit 1, write back
 *   3. Call 0xB031 helper
 *   4. R1=0x02, call 0xBCB1, mask with 0xFE, call 0x0BE6
 *   5. Inc R1, write 0x01 via 0x0BE6
 *   6. Dec R1, call 0x0BC8, mask with 0xFD, call 0x0BE6
 *   7. Inc R1, write 0x02 via 0x0BE6
 *   8. R2=0x12, R1=0x1E, call 0x0BC8
 *   9. Mask with 0xFE, set bit 0, ljmp 0x0BE6
 *
 * Original disassembly:
 *   d894: lcall 0xbc8f
 *   d897: anl a, #0xfd           ; clear bit 1
 *   d899: lcall 0x0be6           ; write register
 *   d89c: mov dptr, #0xc809
 *   d89f: movx a, @dptr
 *   d8a0: anl a, #0xfd           ; clear bit 1
 *   d8a2: orl a, #0x02           ; set bit 1
 *   d8a4: movx @dptr, a
 *   d8a5: lcall 0xb031           ; helper
 *   ... (continues with register configuration)
 */
void phy_register_config(void)
{
    uint8_t val;

    /* Configure interrupt control 2 - clear bit 1, set bit 1 */
    val = REG_INT_CTRL;
    val = (val & 0xFD) | 0x02;
    REG_INT_CTRL = val;

    /* Read state flag and check bit 1 */
    val = G_STATE_FLAG_0AF1;
    if (val & STATE_FLAG_INIT) {
        /* If bit 1 set, call handler 0x057A with R7=0x03 */
        /* This would handle a specific condition */
    }

    /* Configure PCIe control register */
    val = REG_PCIE_CTRL_B402;
    val = val & ~PCIE_CTRL_B402_BIT0;  /* Clear bit 0 */
    REG_PCIE_CTRL_B402 = val;
    val = REG_PCIE_CTRL_B402;
    val = val & ~PCIE_CTRL_B402_BIT1;  /* Clear bit 1 */
    REG_PCIE_CTRL_B402 = val;
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
