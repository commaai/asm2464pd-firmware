/*
 * flash.h - SPI Flash Driver
 *
 * SPI Flash controller for the ASM2464PD USB4/Thunderbolt to NVMe bridge.
 * Provides hardware-accelerated SPI transactions with a 4KB DMA buffer.
 *
 * ===========================================================================
 * FLASH MEMORY LAYOUT
 * ===========================================================================
 * Total flash size: 256KB (0x40000 bytes) typical
 *
 *   0x000000-0x007FFF  Bank 0 firmware, shared region (32KB)
 *   0x008000-0x00FF6A  Bank 0 firmware, bank-specific (32KB - 0x95)
 *   0x00FF6B-0x017F6A  Bank 1 firmware (32KB at code 0x8000-0xFFFF)
 *   0x020000-0x02FFFF  Configuration data
 *   0x030000+          Reserved/User data
 *
 * Code Address to File Offset Mapping:
 *   - 0x0000-0x7FFF: File offset 0x0000-0x7FFF (32KB shared)
 *   - 0x8000-0xFF6A (Bank 0): File offset 0x8000-0xFFFF
 *   - 0x8000-0xFF6A (Bank 1): File offset = 0xFF6B + (code_addr - 0x8000)
 *
 * ===========================================================================
 * CODE ROM MIRROR (0xE400-0xE700)
 * ===========================================================================
 * XDATA region 0xE400-0xE700 provides read access to code ROM:
 *
 *   Formula: code_addr = xdata_addr - 0xDDFC
 *
 *   Example mappings:
 *     XDATA 0xE423 → Code ROM 0x0627 (device descriptor)
 *     XDATA 0xE437 → Code ROM 0x063B (language ID string)
 *     XDATA 0xE6xx → Code ROM 0x08xx (additional descriptors)
 *
 * This is used for reading USB descriptors stored in code ROM without
 * needing explicit flash read operations. The hardware provides a
 * transparent mapping between XDATA reads and code ROM.
 *
 * ===========================================================================
 * SPI FLASH COMMANDS
 * ===========================================================================
 *   0x03  Read Data - Sequential read from flash to buffer
 *   0x02  Page Program - Write up to 256 bytes (must be erased first)
 *   0x20  Sector Erase (4KB) - Erases 4KB sector to 0xFF
 *   0xD8  Block Erase (64KB) - Erases 64KB block to 0xFF
 *   0xC7  Chip Erase - Erases entire chip to 0xFF
 *   0x06  Write Enable - Required before any write/erase
 *   0x04  Write Disable - Disables write operations
 *   0x05  Read Status - Returns status register
 *   0x9F  Read JEDEC ID - Returns manufacturer/device ID
 *
 * Write Behavior:
 *   - SPI flash can only clear bits (1→0), not set them
 *   - Programming ANDs new value with existing: flash[addr] &= value
 *   - Must erase before writing to set bits back to 1
 *
 * ===========================================================================
 * REGISTER MAP (0xC89F-0xC8AF)
 * ===========================================================================
 *   0xC89F  FLASH_CON        Control register (transaction setup)
 *   0xC8A1  FLASH_ADDR_LO    Flash address bits 7:0
 *   0xC8A2  FLASH_ADDR_MD    Flash address bits 15:8
 *   0xC8A3  FLASH_DATA_LEN   Data length low byte
 *   0xC8A4  FLASH_DATA_LEN_HI  Data length high byte
 *   0xC8A6  FLASH_DIV        SPI clock divisor
 *   0xC8A9  FLASH_CSR        Control/Status register
 *                            Bit 0: Busy (0=idle, 1=operation in progress)
 *                            Write 0x01 to start transaction
 *   0xC8AA  FLASH_CMD        SPI command byte (see command list above)
 *   0xC8AB  FLASH_ADDR_HI    Flash address bits 23:16
 *   0xC8AC  FLASH_ADDR_MID   Flash address bits 15:8 (alternate)
 *   0xC8AD  FLASH_ADDR_LOW   Flash address bits 7:0 (alternate)
 *   0xC8AE  FLASH_DATA       Data register for byte-by-byte access
 *                            Read: Returns byte at current address, auto-increments
 *                            Write: Writes byte to flash (during page program)
 *
 * ===========================================================================
 * FLASH BUFFER (0x7000-0x7FFF)
 * ===========================================================================
 * 4KB buffer shared between CPU and flash controller:
 *
 *   For reads:
 *     1. Set flash address and length
 *     2. Issue read command
 *     3. Controller DMAs flash data to buffer
 *     4. CPU reads from XDATA 0x7000+
 *
 *   For writes:
 *     1. CPU writes data to XDATA 0x7000+
 *     2. Set flash address and length
 *     3. Issue write enable (0x06)
 *     4. Issue page program (0x02)
 *     5. Controller DMAs buffer to flash
 *
 *   Buffer control registers (0x7041, 0x78AF-0x78B2):
 *     0x7041  FLASH_BUF_CTRL   Buffer control (bit 6 = enable)
 *     0x78AF-0x78B2           Buffer configuration
 *
 * ===========================================================================
 * TRANSACTION SEQUENCE
 * ===========================================================================
 *   Read Operation:
 *     1. Clear FLASH_CON to 0x00
 *     2. Configure FLASH_MODE (enable DMA if using buffer)
 *     3. Write address to ADDR_HI, ADDR_MID, ADDR_LO
 *     4. Write 0x03 to FLASH_CMD
 *     5. Write length to FLASH_DATA_LEN
 *     6. Write 0x01 to FLASH_CSR to start
 *     7. Poll FLASH_CSR bit 0 until clear (0x00 = complete)
 *     8. Read data from buffer (0x7000+) or FLASH_DATA
 *
 *   Write Operation:
 *     1. Issue Write Enable (command 0x06)
 *     2. Wait for completion
 *     3. Write data to buffer or FLASH_DATA
 *     4. Set flash address
 *     5. Write 0x02 to FLASH_CMD (Page Program)
 *     6. Write length to FLASH_DATA_LEN
 *     7. Write 0x01 to FLASH_CSR to start
 *     8. Poll FLASH_CSR bit 0 until clear
 *
 *   Erase Operation:
 *     1. Issue Write Enable (command 0x06)
 *     2. Set flash address (sector/block aligned)
 *     3. Write erase command (0x20 sector, 0xD8 block, 0xC7 chip)
 *     4. Write 0x01 to FLASH_CSR to start
 *     5. Poll FLASH_CSR bit 0 until clear
 *
 * ===========================================================================
 * KEY XDATA GLOBALS
 * ===========================================================================
 *   0x07B7-0x07B8  G_FLASH_OP_STATUS   Operation status
 *   0x07BD         G_FLASH_OP_COUNT    Operation counter
 *   0x07C1-0x07C7  G_FLASH_STATE       State/config variables
 *   0x07DF         G_FLASH_COMPLETE    Completion flag
 *   0x07E3         G_FLASH_ERROR       Error code
 *
 * ===========================================================================
 * EMULATOR BEHAVIOR
 * ===========================================================================
 *   - Flash CSR (0xC8A9) always returns 0x00 (operations complete instantly)
 *   - Write operations AND new value with existing (can only clear bits)
 *   - Erase operations set bytes to 0xFF
 *   - Code ROM mirror provides read access via 0xE4xx-0xE6xx XDATA
 */
#ifndef _FLASH_H_
#define _FLASH_H_

#include "../types.h"

/* Flash math utilities */
uint8_t flash_div8(uint8_t dividend, uint8_t divisor);      /* 0x0c0f-0x0c1c */
uint8_t flash_mod8(uint8_t dividend, uint8_t divisor);      /* 0x0c0f-0x0c1c */

/* Flash memory operations */
void flash_add_to_xdata16(__xdata uint8_t *ptr, uint16_t val);  /* 0x0c64-0x0c79 */
void flash_write_word(__xdata uint8_t *ptr, uint16_t val);      /* 0x0c7a-0x0c86 */
void flash_write_idata_word(__idata uint8_t *ptr, uint16_t val);/* 0x0c87-0x0c8e */
void flash_write_r1_xdata_word(uint8_t r1_addr, uint16_t val);  /* 0x0c8f-0x0c98 */

/* Flash status and control */
void flash_poll_busy(void);                     /* 0xbe70-0xbe76 */
uint8_t flash_set_cmd(uint8_t cmd);             /* 0xb845-0xb84f */
void flash_set_mode_enable(void);               /* 0xb8ae-0xb8b8 */
void flash_set_mode_bit4(void);                 /* 0xb85b-0xb864 */
void flash_start_transaction(void);             /* 0xbe6a-0xbe76 */
void flash_clear_mode_bits(void);               /* 0xbe77-0xbe81 */
void flash_clear_mode_bits_6_7(void);           /* 0xbe82-0xbe8a */

/* Flash address setup */
void flash_set_addr_md(__xdata uint8_t *addr_ptr);  /* 0xb865-0xb872 */
void flash_set_addr_hi(__xdata uint8_t *addr_ptr);  /* 0xb873-0xb880 */
void flash_set_data_len(__xdata uint8_t *len_ptr);  /* 0xb888-0xb894 */

/* Flash transactions */
void flash_run_transaction(uint8_t cmd);        /* 0xbe36-0xbe8a */
uint8_t flash_wait_and_poll(void);              /* 0xb1a4-0xb1ca */
void flash_read_status(void);                   /* 0xe3f9-0xe418 */
uint8_t flash_read_buffer_and_status(void);     /* 0xb895-0xb8a1 */

/* Flash buffer access */
uint8_t flash_get_buffer_byte(uint16_t offset); /* inline */
void flash_set_buffer_byte(uint16_t offset, uint8_t val);  /* inline */

/* Flash write operations */
void flash_write_enable(void);                  /* 0xb8a2-0xb8ad */
void flash_write_page(uint32_t addr, uint8_t len);  /* TBD */
void flash_read(uint32_t addr, uint8_t len);    /* TBD */
void flash_erase_sector(uint32_t addr);         /* TBD */

/* Flash dispatch stubs */
void flash_dispatch_stub_873a(void);            /* 0x873a-0x8742 */
void flash_dispatch_stub_8743(void);            /* 0x8743-0x874b */
void flash_dispatch_stub_874c(void);            /* 0x874c-0x8754 */
void flash_dispatch_stub_8d6e(void);            /* 0x8d6e-0x8d76 */

/* Flash handlers */
void flash_command_handler(void);               /* 0xb8b9-0xbe35 */
void system_init_from_flash(void);              /* 0x0100-0x01ff */

#endif /* _FLASH_H_ */
