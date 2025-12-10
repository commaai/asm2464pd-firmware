/*
 * flash.h - SPI Flash Driver
 *
 * The flash subsystem manages the external SPI flash memory used for
 * firmware storage, configuration data, and runtime parameters. The
 * ASM2464PD boots from this flash and can update it at runtime.
 *
 * FLASH MEMORY MAP (typical):
 *   0x000000-0x00FFFF: Bank 0 firmware (64KB)
 *   0x010000-0x01FFFF: Bank 1 firmware (64KB)
 *   0x020000-0x02FFFF: Configuration data
 *   0x030000+: Reserved/User data
 *
 * SPI PROTOCOL:
 *   The driver implements standard SPI flash commands:
 *   - Read (0x03): Sequential read from address
 *   - Page Program (0x02): Write up to 256 bytes
 *   - Sector Erase (0x20): Erase 4KB sector
 *   - Write Enable (0x06): Required before write/erase
 *   - Read Status (0x05): Check busy/write-enable flags
 *
 * FLASH OPERATIONS:
 *   Read:  flash_read(addr, len) → data in buffer
 *   Write: flash_write_enable() → flash_write_page(addr, len)
 *   Erase: flash_write_enable() → flash_erase_sector(addr)
 *
 * BUFFER ACCESS:
 *   Flash data is transferred through an internal buffer. Use
 *   flash_get_buffer_byte() and flash_set_buffer_byte() to
 *   access the buffer contents.
 *
 * MATH UTILITIES:
 *   flash_div8/flash_mod8: 8-bit division/modulo (8051 lacks
 *   hardware divide instruction)
 *
 * USAGE:
 *   flash_read(0x1000, 16);           // Read 16 bytes from 0x1000
 *   uint8_t b = flash_get_buffer_byte(0);  // Get first byte
 *
 *   flash_write_enable();
 *   flash_set_buffer_byte(0, 0xAB);
 *   flash_write_page(0x2000, 1);      // Write 1 byte to 0x2000
 */
#ifndef _FLASH_H_
#define _FLASH_H_

#include "../types.h"

/* Flash math utilities */
uint8_t flash_div8(uint8_t dividend, uint8_t divisor);
uint8_t flash_mod8(uint8_t dividend, uint8_t divisor);

/* Flash memory operations */
void flash_add_to_xdata16(__xdata uint8_t *ptr, uint16_t val);
void flash_write_word(__xdata uint8_t *ptr, uint16_t val);
void flash_write_idata_word(__idata uint8_t *ptr, uint16_t val);
void flash_write_r1_xdata_word(uint8_t r1_addr, uint16_t val);

/* Flash status and control */
void flash_poll_busy(void);
uint8_t flash_set_cmd(uint8_t cmd);
void flash_set_mode_enable(void);
void flash_set_mode_bit4(void);
void flash_start_transaction(void);
void flash_clear_mode_bits(void);
void flash_clear_mode_bits_6_7(void);

/* Flash address setup */
void flash_set_addr_md(__xdata uint8_t *addr_ptr);
void flash_set_addr_hi(__xdata uint8_t *addr_ptr);
void flash_set_data_len(__xdata uint8_t *len_ptr);

/* Flash transactions */
void flash_run_transaction(uint8_t cmd);
uint8_t flash_wait_and_poll(void);
void flash_read_status(void);
uint8_t flash_read_buffer_and_status(void);

/* Flash buffer access */
uint8_t flash_get_buffer_byte(uint16_t offset);
void flash_set_buffer_byte(uint16_t offset, uint8_t val);

/* Flash write operations */
void flash_write_enable(void);
void flash_write_page(uint32_t addr, uint8_t len);
void flash_read(uint32_t addr, uint8_t len);
void flash_erase_sector(uint32_t addr);

/* Flash dispatch stubs */
void flash_dispatch_stub_873a(void);
void flash_dispatch_stub_8743(void);
void flash_dispatch_stub_874c(void);
void flash_dispatch_stub_8d6e(void);

/* Flash handlers */
void flash_command_handler(void);
void system_init_from_flash(void);

#endif /* _FLASH_H_ */
