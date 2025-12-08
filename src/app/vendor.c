/*
 * ASM2464PD Firmware - Vendor SCSI Command Handlers
 *
 * Implements vendor-specific SCSI commands (0xE0-0xE8) used by the
 * tinygrad Python library for device control and firmware updates.
 *
 * ============================================================================
 * VENDOR COMMAND OVERVIEW
 * ============================================================================
 *
 * The ASM2464PD uses vendor SCSI commands for special operations:
 *
 *   0xE0 - Config Read   : Read 128-byte config blocks
 *   0xE1 - Config Write  : Write 128-byte config blocks (vendor/product info)
 *   0xE2 - Flash Read    : Read N bytes from SPI flash
 *   0xE3 - Firmware Write: Flash firmware to SPI (0x50=part1, 0xD0=part2)
 *   0xE4 - XDATA Read    : Read bytes from XDATA memory space
 *   0xE5 - XDATA Write   : Write single byte to XDATA memory space
 *   0xE6 - NVMe Admin    : Passthrough NVMe admin commands
 *   0xE8 - Reset/Commit  : System reset or commit flashed firmware
 *
 * ============================================================================
 * CDB FORMAT FOR VENDOR COMMANDS
 * ============================================================================
 *
 * 0xE4 (XDATA Read):
 *   Byte 0: 0xE4 (opcode)
 *   Byte 1: Size (number of bytes to read)
 *   Byte 2: Address high byte (bits 16-23)
 *   Byte 3-4: Address low word (big-endian, bits 0-15)
 *   Returns: N bytes from XDATA[address]
 *
 * 0xE5 (XDATA Write):
 *   Byte 0: 0xE5 (opcode)
 *   Byte 1: Value to write
 *   Byte 2: Address high byte (bits 16-23)
 *   Byte 3-4: Address low word (big-endian, bits 0-15)
 *   No data transfer
 *
 * 0xE1 (Config Write):
 *   Byte 0: 0xE1 (opcode)
 *   Byte 1: 0x50 (subcommand)
 *   Byte 2: Block number (0 or 1)
 *   Data OUT: 128 bytes config data
 *
 * 0xE3 (Firmware Flash):
 *   Byte 0: 0xE3 (opcode)
 *   Byte 1: 0x50 (part1) or 0xD0 (part2)
 *   Byte 2-5: Length (big-endian, 32-bit)
 *   Data OUT: Firmware data
 *
 * 0xE8 (Reset/Commit):
 *   Byte 0: 0xE8 (opcode)
 *   Byte 1: 0x00 (CPU reset), 0x01 (soft reset), 0x51 (commit firmware)
 *
 * ============================================================================
 * ORIGINAL FIRMWARE ADDRESSES
 * ============================================================================
 *
 * vendor_cmd_dispatch     : Bank 1 0xB3xx (file offset 0x133xx)
 * vendor_cmd_e4_read      : Bank 1 0xB4xx (file offset 0x134xx)
 * vendor_cmd_e5_write     : Bank 1 0xB4xx (file offset 0x134xx)
 * vendor_cmd_e1_config    : Bank 1 0xC0xx (file offset 0x140xx)
 * vendor_cmd_e3_flash     : Bank 1 0xCFxx (file offset 0x14Fxx)
 * vendor_cmd_e8_reset     : Bank 1 0xE2xx (file offset 0x162xx)
 *
 * ============================================================================
 */

#include "../types.h"
#include "../sfr.h"
#include "../registers.h"
#include "../globals.h"
#include "../structs.h"

/* USB control buffer where CBW is received */
#define USB_CBW_BUFFER      ((__xdata uint8_t *)USB_CTRL_BUF_BASE)  /* 0x9E00 */

/* Flash buffer for data staging */
#define VENDOR_FLASH_BUFFER ((__xdata uint8_t *)FLASH_BUFFER_BASE) /* 0x7000 */

/* Config block size */
#define CONFIG_BLOCK_SIZE   128

/* Firmware flash addresses */
#define FW_FLASH_PART1_START  0x0080   /* Part 1 starts at 0x80 in flash */
#define FW_FLASH_PART2_START  0x10000  /* Part 2 starts at 64KB in flash (bank 1) */

/* External functions */
extern void scsi_send_csw(uint8_t status, uint32_t residue);
extern void flash_write_enable(void);
extern void flash_write_page(uint32_t addr, uint8_t len);
extern void flash_read(uint32_t addr, uint8_t len);
extern void flash_erase_sector(uint32_t addr);
extern void dma_setup_usb_rx(uint16_t len);
extern void dma_setup_usb_tx(uint16_t len);
extern void dma_wait_complete(void);

/*
 * Forward declarations
 */
static void vendor_cmd_e0_config_read(void);
static void vendor_cmd_e1_config_write(void);
static void vendor_cmd_e2_flash_read(void);
static void vendor_cmd_e3_firmware_write(void);
static void vendor_cmd_e4_xdata_read(void);
static void vendor_cmd_e5_xdata_write(void);
static void vendor_cmd_e6_nvme_admin(void);
static void vendor_cmd_e8_reset(void);

/*
 * vendor_cmd_dispatch - Main vendor command dispatcher
 * Address: Bank 1 0xB349-0xB3FF (file offset 0x13349-0x133FF)
 *
 * Called from SCSI command handler when opcode >= 0xE0.
 * Parses CBW and routes to appropriate handler.
 *
 * Original disassembly:
 *   b349: lcall 0xa71b          ; setup transfer
 *   b34c: mov a, #0x02
 *   b34e: movx @dptr, a         ; write status
 *   b34f: mov dptr, #0x9e00     ; USB CBW buffer
 *   b352: clr a
 *   b353: movx @dptr, a         ; clear first byte
 *   b354: inc dptr
 *   b355: movx @dptr, a         ; clear second byte
 *   b356: lcall 0xa6fd          ; get command state
 *   b359: dec a                 ; switch on state
 *   b35a: jz 0xb3a8             ; state 1 handler
 *   b35c: dec a
 *   b35d: jz 0xb3b9             ; state 2 handler
 *   ... routing logic ...
 *
 * Returns: CSW status (0=pass, 1=fail)
 */
uint8_t vendor_cmd_dispatch(void)
{
    uint8_t opcode;
    uint8_t status = 0;  /* Default: success */

    /* Get opcode from CBW (byte 15 = first byte of SCSI CDB) */
    opcode = USB_CBW_BUFFER[15];

    /* Dispatch based on opcode */
    switch (opcode) {
        case 0xE0:
            vendor_cmd_e0_config_read();
            break;

        case 0xE1:
            vendor_cmd_e1_config_write();
            break;

        case 0xE2:
            vendor_cmd_e2_flash_read();
            break;

        case 0xE3:
            vendor_cmd_e3_firmware_write();
            break;

        case 0xE4:
            vendor_cmd_e4_xdata_read();
            break;

        case 0xE5:
            vendor_cmd_e5_xdata_write();
            break;

        case 0xE6:
            vendor_cmd_e6_nvme_admin();
            break;

        case 0xE8:
            vendor_cmd_e8_reset();
            break;

        default:
            /* Unknown vendor command */
            status = 1;  /* Fail */
            break;
    }

    return status;
}

/*
 * vendor_cmd_e0_config_read - Read config block from flash
 * Address: Bank 1 (various)
 *
 * CDB format:
 *   Byte 0: 0xE0
 *   Byte 1: 0x50 (subcommand)
 *   Byte 2: Block number (0 or 1)
 *
 * Reads 128-byte config block from flash and returns to host.
 */
static void vendor_cmd_e0_config_read(void)
{
    uint8_t subcommand;
    uint8_t block_num;
    uint32_t flash_addr;

    subcommand = USB_CBW_BUFFER[16];  /* CDB byte 1 */
    block_num = USB_CBW_BUFFER[17];   /* CDB byte 2 */

    if (subcommand != 0x50) {
        scsi_send_csw(1, 0);  /* Fail */
        return;
    }

    /* Config blocks at flash offset 0 and 128 */
    flash_addr = (uint32_t)block_num * CONFIG_BLOCK_SIZE;

    /* Read config from flash to buffer */
    flash_read(flash_addr, CONFIG_BLOCK_SIZE);

    /* Setup DMA to send data to host */
    dma_setup_usb_tx(CONFIG_BLOCK_SIZE);
    dma_wait_complete();

    scsi_send_csw(0, 0);  /* Success */
}

/*
 * vendor_cmd_e1_config_write - Write config block to flash
 * Address: Bank 1 0xC0xx (file offset 0x140xx)
 *
 * CDB format:
 *   Byte 0: 0xE1
 *   Byte 1: 0x50 (subcommand)
 *   Byte 2: Block number (0 or 1)
 *
 * Receives 128 bytes from host and writes to flash config block.
 *
 * Original disassembly (0x140d2-0x14113):
 *   140d2: mov dptr, #0x0b02   ; state variable
 *   140d5: movx a, @dptr
 *   140d6: xrl a, #0x01        ; check state == 1
 *   140d8: jnz 0x140ef         ; not state 1, try next
 *   140da: inc dptr            ; 0x0b03
 *   140db: movx a, @dptr       ; get subcommand
 *   140dc: cjne a, #0xe2, 0x140ed  ; not 0xE2 subcommand
 *   ... flash write logic ...
 */
static void vendor_cmd_e1_config_write(void)
{
    uint8_t subcommand;
    uint8_t block_num;
    uint32_t flash_addr;

    subcommand = USB_CBW_BUFFER[16];  /* CDB byte 1 */
    block_num = USB_CBW_BUFFER[17];   /* CDB byte 2 */

    if (subcommand != 0x50) {
        scsi_send_csw(1, 0);  /* Fail */
        return;
    }

    /* Config blocks at flash offset 0 and 128 */
    flash_addr = (uint32_t)block_num * CONFIG_BLOCK_SIZE;

    /* Setup DMA to receive data from host to flash buffer */
    dma_setup_usb_rx(CONFIG_BLOCK_SIZE);
    dma_wait_complete();

    /* Erase flash sector if needed (typically config is in first sector) */
    if (flash_addr < 0x1000) {
        flash_erase_sector(0);
    }

    /* Write config to flash */
    flash_write_enable();
    flash_write_page(flash_addr, CONFIG_BLOCK_SIZE);

    scsi_send_csw(0, 0);  /* Success */
}

/*
 * vendor_cmd_e2_flash_read - Read arbitrary data from SPI flash
 * Address: Bank 1 (various)
 *
 * CDB format:
 *   Byte 0: 0xE2
 *   Byte 1-4: Length (big-endian)
 *
 * Reads N bytes starting from flash address 0.
 */
static void vendor_cmd_e2_flash_read(void)
{
    uint32_t length;

    /* Get length from CDB (big-endian) */
    length = ((uint32_t)USB_CBW_BUFFER[16] << 24) |
             ((uint32_t)USB_CBW_BUFFER[17] << 16) |
             ((uint32_t)USB_CBW_BUFFER[18] << 8) |
             ((uint32_t)USB_CBW_BUFFER[19]);

    /* Read from flash address 0 */
    /* Note: For large reads, this would need to be chunked */
    if (length > 0x1000) {
        length = 0x1000;  /* Limit to buffer size */
    }

    flash_read(0, (uint8_t)length);

    /* Setup DMA to send data to host */
    dma_setup_usb_tx((uint16_t)length);
    dma_wait_complete();

    scsi_send_csw(0, 0);  /* Success */
}

/*
 * vendor_cmd_e3_firmware_write - Flash firmware to SPI
 * Address: Bank 1 0xCF5D (file offset 0x14F5D)
 *
 * CDB format:
 *   Byte 0: 0xE3
 *   Byte 1: 0x50 (part1) or 0xD0 (part2)
 *   Byte 2-5: Length (big-endian, 32-bit)
 *
 * Receives firmware data from host and writes to flash.
 * Part 1 starts at flash offset 0x80.
 * Part 2 starts at flash offset 0x10000 (64KB).
 *
 * Original disassembly (0x140f9-0x14113):
 *   140f9: cjne a, #0xe3, 0x14114  ; check opcode
 *   140fc: lcall 0xcf5d            ; call flash handler
 *   140ff: mov a, r7               ; get result
 *   14100: jnz 0x14104             ; if error, handle
 *   14102: sjmp 0x14115            ; success path
 *   ...
 */
static void vendor_cmd_e3_firmware_write(void)
{
    uint8_t part;
    uint32_t length;
    uint32_t flash_addr;
    uint32_t bytes_written;
    uint16_t chunk_size;

    part = USB_CBW_BUFFER[16];  /* CDB byte 1: 0x50 or 0xD0 */

    /* Get length from CDB (big-endian) */
    length = ((uint32_t)USB_CBW_BUFFER[17] << 24) |
             ((uint32_t)USB_CBW_BUFFER[18] << 16) |
             ((uint32_t)USB_CBW_BUFFER[19] << 8) |
             ((uint32_t)USB_CBW_BUFFER[20]);

    /* Determine flash start address based on part */
    if (part == 0x50) {
        flash_addr = FW_FLASH_PART1_START;  /* 0x80 */
    } else if (part == 0xD0) {
        flash_addr = FW_FLASH_PART2_START;  /* 0x10000 */
    } else {
        scsi_send_csw(1, 0);  /* Invalid part */
        return;
    }

    /* Write firmware in chunks */
    bytes_written = 0;
    while (bytes_written < length) {
        /* Calculate chunk size (max 4KB per flash buffer) */
        chunk_size = (uint16_t)((length - bytes_written) > 0x1000 ?
                                0x1000 : (length - bytes_written));

        /* Receive chunk from host */
        dma_setup_usb_rx(chunk_size);
        dma_wait_complete();

        /* Erase sector if at sector boundary (4KB sectors) */
        if ((flash_addr & 0xFFF) == 0) {
            flash_erase_sector(flash_addr);
        }

        /* Write chunk to flash */
        flash_write_enable();
        flash_write_page(flash_addr, (uint8_t)chunk_size);

        flash_addr += chunk_size;
        bytes_written += chunk_size;
    }

    scsi_send_csw(0, 0);  /* Success */
}

/*
 * vendor_cmd_e4_xdata_read - Read from XDATA memory space
 * Address: Bank 1 0xB473-0xB4xx (file offset 0x13473-0x134xx)
 *
 * CDB format:
 *   Byte 0: 0xE4
 *   Byte 1: Size (number of bytes to read)
 *   Byte 2: Address bits 16-23
 *   Byte 3: Address bits 8-15
 *   Byte 4: Address bits 0-7
 *
 * Reads N bytes from XDATA[address] and returns to host.
 * This is the primary mechanism for host to read device registers/state.
 *
 * Original disassembly (0x13473-0x134b0):
 *   13473: lcall 0xb663         ; parse CDB helper
 *   13476: lcall 0x0d08         ; get parameters
 *   13479: push 04              ; save R4
 *   1347b: push 05              ; save R5
 *   1347d: push 06              ; save R6
 *   1347f: push 07              ; save R7
 *   13481: mov dptr, #0x0816    ; response buffer
 *   13484: lcall 0xb67c         ; copy address setup
 *   13487: mov r0, #0x10        ; 16 bytes max?
 *   13489: lcall 0x0d46         ; read helper
 *   ... continues with data copy to USB buffer ...
 */
static void vendor_cmd_e4_xdata_read(void)
{
    uint8_t size;
    uint32_t addr;
    uint16_t xdata_addr;
    __xdata uint8_t *src;
    __xdata uint8_t *dst;
    uint8_t i;

    /* Parse CDB */
    size = USB_CBW_BUFFER[16];         /* Byte 1: size */
    addr = ((uint32_t)USB_CBW_BUFFER[17] << 16) |  /* Byte 2: addr high */
           ((uint32_t)USB_CBW_BUFFER[18] << 8) |   /* Byte 3: addr mid */
           ((uint32_t)USB_CBW_BUFFER[19]);         /* Byte 4: addr low */

    /* Validate size */
    if (size == 0 || size > 64) {
        size = 64;  /* Limit to reasonable size */
    }

    /* Convert 24-bit address to 16-bit XDATA address */
    /* For XDATA space, only lower 16 bits are used */
    xdata_addr = (uint16_t)(addr & 0xFFFF);

    src = (__xdata uint8_t *)xdata_addr;
    dst = (__xdata uint8_t *)USB_SCSI_BUF_BASE;  /* Response buffer at 0x8000 */

    /* Copy data from XDATA to response buffer */
    for (i = 0; i < size; i++) {
        dst[i] = src[i];
    }

    /* Setup DMA to send response to host */
    dma_setup_usb_tx(size);
    dma_wait_complete();

    scsi_send_csw(0, 0);  /* Success */
}

/*
 * vendor_cmd_e5_xdata_write - Write to XDATA memory space
 * Address: Bank 1 0xB43C-0xB472 (file offset 0x1343C-0x13472)
 *
 * CDB format:
 *   Byte 0: 0xE5
 *   Byte 1: Value to write
 *   Byte 2: Address bits 16-23
 *   Byte 3: Address bits 8-15
 *   Byte 4: Address bits 0-7
 *
 * Writes a single byte to XDATA[address].
 * This is the primary mechanism for host to control device.
 *
 * Original disassembly (0x1343c-0x13472):
 *   1343c: cjne a, #0xe5, 0x13497  ; check opcode
 *   1343f: movx @dptr, a           ; acknowledge
 *   13440: mov a, 0x55             ; get state
 *   13442: jnb acc.1, 0x1346c      ; check mode bit
 *   13445: mov r1, #0x6c           ; offset
 *   13447: lcall 0xb720            ; parse address
 *   1344a: mov r7, #0x00
 *   1344c: jb acc.0, 0x3451        ; check flag
 *   1344f: mov r7, #0x01
 *   13451: mov r5, 0x57            ; get value
 *   13453: lcall 0xea7c            ; execute write
 *   ... continues ...
 */
static void vendor_cmd_e5_xdata_write(void)
{
    uint8_t value;
    uint32_t addr;
    uint16_t xdata_addr;
    __xdata uint8_t *dst;

    /* Parse CDB */
    value = USB_CBW_BUFFER[16];        /* Byte 1: value */
    addr = ((uint32_t)USB_CBW_BUFFER[17] << 16) |  /* Byte 2: addr high */
           ((uint32_t)USB_CBW_BUFFER[18] << 8) |   /* Byte 3: addr mid */
           ((uint32_t)USB_CBW_BUFFER[19]);         /* Byte 4: addr low */

    /* Convert 24-bit address to 16-bit XDATA address */
    xdata_addr = (uint16_t)(addr & 0xFFFF);

    dst = (__xdata uint8_t *)xdata_addr;

    /* Write value to XDATA */
    *dst = value;

    scsi_send_csw(0, 0);  /* Success, no data transfer */
}

/*
 * vendor_cmd_e6_nvme_admin - NVMe admin command passthrough
 * Address: Bank 1 (various)
 *
 * CDB format:
 *   Byte 0: 0xE6
 *   Byte 1: NVMe admin opcode
 *   Additional bytes depend on command
 *
 * Supports:
 *   - Get Log Page
 *   - Identify Controller
 *   - Identify Namespace
 */
static void vendor_cmd_e6_nvme_admin(void)
{
    uint8_t nvme_opcode;

    nvme_opcode = USB_CBW_BUFFER[16];  /* NVMe admin opcode */

    /* TODO: Implement NVMe admin command passthrough */
    /* For now, return failure */
    (void)nvme_opcode;

    scsi_send_csw(1, 0);  /* Not implemented */
}

/*
 * vendor_cmd_e8_reset - System reset or firmware commit
 * Address: Bank 1 0xE21B (file offset 0x1621B)
 *
 * CDB format:
 *   Byte 0: 0xE8
 *   Byte 1: Subcommand
 *           0x00 = CPU reset
 *           0x01 = Soft/PCIe reset
 *           0x51 = Commit flashed firmware
 *
 * Original disassembly (0x140c3-0x140c9):
 *   140c3: lcall 0xe21b         ; call reset handler
 *   140c6: mov a, r7            ; get result
 *   140c7: jnz 0x140eb          ; if error, jump
 *   140c9: ret                  ; success
 *
 * Handler at 0xe21b:
 *   e21b: mov dptr, #0xea90     ; trigger register
 *   e21e: mov a, #0xa5          ; magic value
 *   e220: movx @dptr, a         ; trigger reset
 *   e221: ret
 */
static void vendor_cmd_e8_reset(void)
{
    uint8_t subcommand;

    subcommand = USB_CBW_BUFFER[16];  /* CDB byte 1 */

    /* Send CSW before reset (host needs to know we received the command) */
    scsi_send_csw(0, 0);

    switch (subcommand) {
        case 0x00:
            /* CPU reset - write magic to trigger register */
            XDATA_REG8(0xEA90) = 0xA5;
            /* Should not return from here */
            break;

        case 0x01:
            /* Soft/PCIe reset */
            /* Clear link control bits, trigger re-enumeration */
            REG_LINK_MODE_CTRL &= ~0x03;
            REG_LINK_CTRL_E324 |= 0x04;
            break;

        case 0x51:
            /* Commit flashed firmware */
            /* This typically validates the firmware and sets boot flags */
            /* The actual commit may involve writing to flash config area */
            G_USB_INIT_0B01 = 0x01;  /* Mark firmware as valid */
            /* Write completion magic */
            XDATA_REG8(0xEA90) = 0xA5;
            break;

        default:
            /* Unknown subcommand - already sent success CSW, nothing to do */
            break;
    }
}

/*
 * vendor_is_vendor_command - Check if opcode is a vendor command
 *
 * Returns 1 if opcode is 0xE0-0xE8 (vendor range), 0 otherwise.
 */
uint8_t vendor_is_vendor_command(uint8_t opcode)
{
    return (opcode >= 0xE0 && opcode <= 0xE8) ? 1 : 0;
}
