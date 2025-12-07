/*
 * ASM2464PD Firmware - SCSI Command Handler
 *
 * Handles SCSI/USB Mass Storage commands for the NVMe bridge.
 * The ASM2464PD presents NVMe storage as a USB Mass Storage device,
 * translating SCSI commands to NVMe operations.
 *
 * ============================================================================
 * USB MASS STORAGE PROTOCOL
 * ============================================================================
 *
 * The USB Mass Storage class uses Command Block Wrapper (CBW) and
 * Command Status Wrapper (CSW) structures:
 *
 * CBW (31 bytes):
 *   Bytes 0-3:   Signature 'USBC' (0x55, 0x53, 0x42, 0x43)
 *   Bytes 4-7:   Tag
 *   Bytes 8-11:  Data transfer length
 *   Byte 12:     Flags (bit 7 = direction)
 *   Byte 13:     LUN
 *   Byte 14:     Command length
 *   Bytes 15-30: Command block (SCSI CDB)
 *
 * CSW (13 bytes):
 *   Bytes 0-3:   Signature 'USBS' (0x55, 0x53, 0x42, 0x53)
 *   Bytes 4-7:   Tag (same as CBW)
 *   Bytes 8-11:  Data residue
 *   Byte 12:     Status (0=pass, 1=fail, 2=phase error)
 *
 * ============================================================================
 * SCSI COMMANDS SUPPORTED
 * ============================================================================
 *
 * Essential commands:
 *   0x00 - TEST UNIT READY
 *   0x03 - REQUEST SENSE
 *   0x12 - INQUIRY
 *   0x1A - MODE SENSE (6)
 *   0x1B - START STOP UNIT
 *   0x23 - READ FORMAT CAPACITIES
 *   0x25 - READ CAPACITY (10)
 *   0x28 - READ (10)
 *   0x2A - WRITE (10)
 *   0x2F - VERIFY (10)
 *   0x35 - SYNCHRONIZE CACHE (10)
 *   0x5A - MODE SENSE (10)
 *   0x9E - SERVICE ACTION IN (READ CAPACITY 16)
 *   0xA0 - REPORT LUNS
 *
 * ============================================================================
 * REGISTER MAP
 * ============================================================================
 *
 *   0x9007-0x9008: Status/result registers
 *   0x9093-0x9094: Mode configuration
 *
 * ============================================================================
 * GLOBAL VARIABLES
 * ============================================================================
 *
 *   IDATA[0x09]: Command data buffer (4 bytes)
 *
 * ============================================================================
 */

#include "../types.h"
#include "../sfr.h"
#include "../registers.h"
#include "../globals.h"
#include "../structs.h"

/* External functions from usb.c */
extern uint16_t usb_read_transfer_params(void);

/* IDATA command buffer at 0x09 (4 bytes) */
static __idata uint8_t * const scsi_cmd_buffer = (__idata uint8_t *)0x09;

/* USB Mass Storage CBW signatures (CSW signatures are in structs.h) */
#define USB_CBW_SIGNATURE_0     0x55    /* 'U' */
#define USB_CBW_SIGNATURE_1     0x53    /* 'S' */
#define USB_CBW_SIGNATURE_2     0x42    /* 'B' */
#define USB_CBW_SIGNATURE_3     0x43    /* 'C' */

/*
 * scsi_validate_cbw_signature - Validate CBW 'USBC' signature
 * Address: 0x5200-0x5215 (22 bytes)
 *
 * Calls helper to get signature bytes, validates against 'SBC' (0x53, 0x42, 0x43).
 * Note: First byte 'U' (0x55) is checked elsewhere.
 * Returns 1 if valid, 0 if invalid.
 *
 * Original disassembly:
 *   5200: lcall 0xa3e0          ; get signature helper (returns DPTR, A)
 *   5203: cjne a, #0x53, 0x5213 ; check 'S'
 *   5206: inc dptr
 *   5207: movx a, @dptr         ; read next byte
 *   5208: cjne a, #0x42, 0x5213 ; check 'B'
 *   520b: inc dptr
 *   520c: movx a, @dptr         ; read next byte
 *   520d: cjne a, #0x43, 0x5213 ; check 'C'
 *   5210: mov r7, #0x01         ; return 1 (valid)
 *   5212: ret
 *   5213: mov r7, #0x00         ; return 0 (invalid)
 *   5215: ret
 */
uint8_t scsi_validate_cbw_signature(void)
{
    /* This would call helper at 0xa3e0 to get signature location */
    /* For now, assume the signature check is against CBW buffer */

    /* Simplified validation - actual implementation would read from
     * the CBW buffer location returned by helper */
    __xdata uint8_t *cbw_ptr;

    /* The helper at 0xa3e0 returns pointer to signature bytes */
    /* Checking bytes 1-3 ('S', 'B', 'C') */
    cbw_ptr = (__xdata uint8_t *)0xA3E0;  /* Placeholder - actual address from helper */

    if (cbw_ptr[0] != USB_CBW_SIGNATURE_1) return 0;  /* 'S' */
    if (cbw_ptr[1] != USB_CBW_SIGNATURE_2) return 0;  /* 'B' */
    if (cbw_ptr[2] != USB_CBW_SIGNATURE_3) return 0;  /* 'C' */

    return 1;
}

/*
 * scsi_setup_status_regs - Setup status registers for command processing
 * Address: 0x5216-0x523b (38 bytes)
 *
 * Calls helpers to setup command state, writes to status registers.
 *
 * Original disassembly:
 *   5216: lcall 0x31a5          ; helper 1
 *   5219: lcall 0x322e          ; helper 2 (returns carry on error)
 *   521c: jc 0x5224             ; if error, jump
 *   521e: lcall 0x31a5          ; helper 1 again
 *   5221: mov r7, a             ; result in R7
 *   5222: sjmp 0x5229           ; jump to write regs
 *   5224: mov r0, #0x09         ; error path: R0 = 0x09
 *   5226: lcall 0x0d78          ; idata_load_dword(0x09) -> R4-R7
 *   5229: mov dptr, #0x9007     ; status register
 *   522c: mov a, r6
 *   522d: movx @dptr, a         ; write R6
 *   522e: inc dptr
 *   522f: mov a, r7
 *   5230: movx @dptr, a         ; write R7
 *   5231: mov dptr, #0x9093     ; mode register
 *   5234: mov a, #0x08
 *   5236: movx @dptr, a         ; write 0x08
 *   5237: inc dptr
 *   5238: mov a, #0x02
 *   523a: movx @dptr, a         ; write 0x02
 *   523b: ret
 */
void scsi_setup_status_regs(void)
{
    uint16_t transfer_params;
    uint8_t r6_val, r7_val;
    uint32_t cmd_buffer_val;
    uint8_t error;

    /* Call helper at 0x31a5 - read transfer params */
    transfer_params = usb_read_transfer_params();
    r6_val = (uint8_t)(transfer_params >> 8);  /* High byte */
    r7_val = (uint8_t)(transfer_params & 0xFF); /* Low byte */

    /* Call helper at 0x322e - compare IDATA[0x09..0x0C] with (0, 0, R6, R7) */
    /* Load 32-bit value from command buffer */
    cmd_buffer_val = scsi_cmd_buffer[0];
    cmd_buffer_val |= ((uint32_t)scsi_cmd_buffer[1]) << 8;
    cmd_buffer_val |= ((uint32_t)scsi_cmd_buffer[2]) << 16;
    cmd_buffer_val |= ((uint32_t)scsi_cmd_buffer[3]) << 24;

    /* Compare with transfer params (zero-extended to 32-bit) */
    error = (cmd_buffer_val != transfer_params) ? 1 : 0;

    if (error) {
        /* Load R6/R7 from IDATA[0x09] bytes 2-3 */
        r6_val = scsi_cmd_buffer[2];
        r7_val = scsi_cmd_buffer[3];
    } else {
        /* Call helper at 0x31a5 again and use A (low byte) as R7 */
        transfer_params = usb_read_transfer_params();
        r7_val = (uint8_t)(transfer_params & 0xFF);
    }

    /* Write to SCSI buffer length registers */
    REG_USB_SCSI_BUF_LEN_L = r6_val;
    REG_USB_SCSI_BUF_LEN_H = r7_val;

    /* Write to endpoint config registers */
    REG_USB_EP_CFG1 = 0x08;
    REG_USB_EP_CFG2 = 0x02;
}

/*
 * scsi_get_command_byte - Get SCSI command byte from CBW
 * Address: 0xa3e0 (approximate - helper function)
 *
 * Reads the SCSI opcode from the CBW command block.
 * Returns opcode in A, DPTR points to command data.
 */
uint8_t scsi_get_command_byte(void)
{
    /* The command byte is at CBW offset 15 */
    /* This would read from the USB endpoint buffer */
    return 0;  /* Placeholder */
}

/*
 * scsi_send_csw - Send Command Status Wrapper
 * Address: 0x4904-0x4974 (init), 0x314b-0x3167 (tag copy), 0x53c0-0x53d3 (residue)
 *
 * Builds and sends a 13-byte CSW response to the host.
 * CSW structure at 0xD800:
 *   Bytes 0-3:   Signature 'USBS' (0x55, 0x53, 0x42, 0x53)
 *   Bytes 4-7:   Tag (copied from CBW at 0x9120-0x9123)
 *   Bytes 8-11:  Data Residue (little-endian)
 *   Byte 12:     Status (0=pass, 1=fail, 2=phase error)
 *
 * Parameters:
 *   status: CSW status code (0=pass, 1=fail, 2=phase error)
 *   residue: Number of bytes not transferred
 *
 * Original disassembly (0x4955-0x4974):
 *   4955: mov r7, #0x53       ; 'S'
 *   4957: mov r6, #0x42       ; 'B'
 *   4959: mov r5, #0x53       ; 'S'
 *   495b: mov r4, #0x55       ; 'U'
 *   495d: mov dptr, #0xd800   ; CSW buffer
 *   4960: lcall 0x0dc5        ; xdata_store_dword - writes "USBS"
 *   4963: mov dptr, #0x901a   ; MSC packet length register
 *   4966: mov a, #0x0d        ; 13 bytes
 *   4968: movx @dptr, a
 *   4969: mov dptr, #0xc42c   ; MSC control register
 *   496c: mov a, #0x01        ; trigger transmission
 *   496e: movx @dptr, a
 *   496f: inc dptr            ; 0xC42D
 *   4970: movx a, @dptr
 *   4971: anl a, #0xfe        ; clear bit 0
 *   4973: movx @dptr, a
 *   4974: ljmp 0x0331         ; return to bank1 dispatch
 *
 * Tag copy (0x314b-0x3167):
 *   Copies 4 bytes from 0x9120-0x9123 to 0xD804-0xD807
 *
 * Residue write (0x53c0-0x53d3):
 *   Copies 4 bytes from IDATA[0x6F-0x72] to 0xD808-0xD80B
 */
void scsi_send_csw(uint8_t status, uint32_t residue)
{
    uint8_t msc_status;

    /* Write CSW signature 'USBS' to 0xD800-0xD803 */
    USB_CSW->sig0 = USB_CSW_SIGNATURE_0;  /* 'U' = 0x55 */
    USB_CSW->sig1 = USB_CSW_SIGNATURE_1;  /* 'S' = 0x53 */
    USB_CSW->sig2 = USB_CSW_SIGNATURE_2;  /* 'B' = 0x42 */
    USB_CSW->sig3 = USB_CSW_SIGNATURE_3;  /* 'S' = 0x53 */

    /* Copy tag from CBW (0x9120-0x9123) to CSW (0xD804-0xD807) */
    USB_CSW->tag0 = REG_CBW_TAG_0;
    USB_CSW->tag1 = REG_CBW_TAG_1;
    USB_CSW->tag2 = REG_CBW_TAG_2;
    USB_CSW->tag3 = REG_CBW_TAG_3;

    /* Write data residue (little-endian) to 0xD808-0xD80B */
    USB_CSW->residue0 = (uint8_t)(residue & 0xFF);
    USB_CSW->residue1 = (uint8_t)((residue >> 8) & 0xFF);
    USB_CSW->residue2 = (uint8_t)((residue >> 16) & 0xFF);
    USB_CSW->residue3 = (uint8_t)((residue >> 24) & 0xFF);

    /* Write status byte to 0xD80C */
    USB_CSW->status = status;

    /* Set CSW packet length (13 bytes) */
    REG_USB_MSC_LENGTH = USB_CSW_LENGTH;

    /* Trigger USB transmission */
    REG_USB_MSC_CTRL = 0x01;

    /* Clear bit 0 of MSC status register */
    msc_status = REG_USB_MSC_STATUS;
    REG_USB_MSC_STATUS = msc_status & 0xFE;
}

/*
 * scsi_check_lun - Check if LUN is valid
 * Address: inline pattern
 *
 * Validates that the requested LUN is within range.
 * The ASM2464PD typically supports LUN 0 only.
 *
 * Returns 1 if valid, 0 if invalid.
 */
uint8_t scsi_check_lun(uint8_t lun)
{
    /* Only LUN 0 is valid for single NVMe device */
    return (lun == 0) ? 1 : 0;
}

/*
 * scsi_test_unit_ready - Handle TEST UNIT READY command
 * Address: various
 *
 * SCSI opcode 0x00: Check if device is ready.
 * Returns sense data if not ready.
 */
void scsi_test_unit_ready(void)
{
    /* Check if NVMe device is ready */
    /* If ready: return good status */
    /* If not: set sense data (NOT READY, MEDIUM NOT PRESENT) */
}

/*
 * scsi_inquiry - Handle INQUIRY command
 * Address: various
 *
 * SCSI opcode 0x12: Return device identification.
 * Returns vendor ID, product ID, revision.
 */
void scsi_inquiry(void)
{
    /* Return standard INQUIRY data:
     * Byte 0: Device type (0x00 = disk)
     * Byte 1: Removable (0x00 = not removable)
     * Byte 2: Version (0x05 = SPC-3)
     * Byte 3: Response format (0x02)
     * Byte 4: Additional length
     * Bytes 8-15: Vendor ID
     * Bytes 16-31: Product ID
     * Bytes 32-35: Revision
     */
}

/*
 * scsi_read_capacity_10 - Handle READ CAPACITY (10) command
 * Address: various
 *
 * SCSI opcode 0x25: Return device capacity.
 * Returns last LBA and block size.
 */
void scsi_read_capacity_10(void)
{
    /* Return:
     * Bytes 0-3: Last LBA (big-endian)
     * Bytes 4-7: Block size (512 or 4096, big-endian)
     */
}

/*
 * scsi_read_10 - Handle READ (10) command
 * Address: various
 *
 * SCSI opcode 0x28: Read data from device.
 * Translates to NVMe Read command.
 */
void scsi_read_10(void)
{
    /* Extract from CDB:
     * Bytes 2-5: LBA (big-endian)
     * Bytes 7-8: Transfer length (blocks)
     *
     * Translate to NVMe:
     * - Set up NVMe Read command
     * - Execute via NVMe submission queue
     * - Transfer data to USB endpoint
     */
}

/*
 * scsi_write_10 - Handle WRITE (10) command
 * Address: various
 *
 * SCSI opcode 0x2A: Write data to device.
 * Translates to NVMe Write command.
 */
void scsi_write_10(void)
{
    /* Extract from CDB:
     * Bytes 2-5: LBA (big-endian)
     * Bytes 7-8: Transfer length (blocks)
     *
     * Translate to NVMe:
     * - Receive data from USB endpoint
     * - Set up NVMe Write command
     * - Execute via NVMe submission queue
     */
}

/*
 * scsi_request_sense - Handle REQUEST SENSE command
 * Address: various
 *
 * SCSI opcode 0x03: Return sense data from last error.
 */
void scsi_request_sense(void)
{
    /* Return sense data:
     * Byte 0: Response code (0x70 = current, fixed format)
     * Byte 2: Sense key
     * Byte 7: Additional sense length
     * Byte 12: ASC (Additional Sense Code)
     * Byte 13: ASCQ (ASC Qualifier)
     */
}

/*
 * scsi_mode_sense_6 - Handle MODE SENSE (6) command
 * Address: various
 *
 * SCSI opcode 0x1A: Return mode pages.
 */
void scsi_mode_sense_6(void)
{
    /* Return mode page data based on page code in CDB byte 2 */
}

/*
 * scsi_synchronize_cache - Handle SYNCHRONIZE CACHE command
 * Address: various
 *
 * SCSI opcode 0x35: Flush cache to media.
 * Translates to NVMe Flush command.
 */
void scsi_synchronize_cache(void)
{
    /* Issue NVMe Flush command */
}
