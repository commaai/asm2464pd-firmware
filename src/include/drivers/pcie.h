/*
 * pcie.h - PCIe/NVMe Bridge Driver
 *
 * PCIe interface controller for the ASM2464PD USB4/Thunderbolt to NVMe bridge.
 * Handles PCIe Transaction Layer Packet (TLP) operations for communicating
 * with downstream NVMe devices, and provides DMA engine for USB vendor commands.
 *
 * ===========================================================================
 * DATA FLOW
 * ===========================================================================
 *   USB Host <-> USB Controller <-> DMA Engine <-> PCIe Controller <-> NVMe SSD
 *                                       |
 *                                       v
 *                                   XDATA Memory
 *                                  (0x0000-0xFFFF)
 *
 * ===========================================================================
 * PCIe CAPABILITIES
 * ===========================================================================
 *   PCIe Gen3/Gen4 support (up to 16 GT/s per lane)
 *   x4 lane configuration
 *   Memory-mapped NVMe registers
 *   Hardware TLP generation and completion handling
 *   DMA engine for USB-to-XDATA transfers
 *
 * ===========================================================================
 * DMA FOR USB VENDOR COMMANDS (E4/E5)
 * ===========================================================================
 * The PCIe DMA engine handles USB vendor command data transfer:
 *
 * E4 READ COMMAND:
 *   1. USB controller receives CDB: [E4, size, addr_hi, addr_mid, addr_lo, 0]
 *   2. Firmware reads CDB from USB registers (0x910D-0x9112)
 *   3. Firmware configures DMA source: address format 0x50XXXX (XDATA space)
 *   4. Firmware writes 0x08 to PCIE_STATUS (0xB296) to trigger DMA
 *   5. Hardware DMAs data from XDATA[addr] to USB buffer (0x8000)
 *   6. Hardware sets bits 1,2 in PCIE_STATUS when complete
 *   7. Host reads response from USB bulk endpoint
 *
 * E5 WRITE COMMAND:
 *   1. USB controller receives CDB: [E5, value, addr_hi, addr_mid, addr_lo, 0]
 *   2. Firmware reads CDB value from 0xC47A (preserved across firmware clear)
 *   3. Firmware reads target address from 0xCEB2/0xCEB3
 *   4. Firmware writes 0x08 to PCIE_STATUS (0xB296) to trigger
 *   5. Hardware writes single byte to XDATA[addr]
 *   6. No data phase needed - completes immediately
 *
 * DMA Trigger Values:
 *   0x08: E4/E5 vendor command DMA (XDATA read/write)
 *   0x0F: Standard PCIe TLP transaction
 *
 * ===========================================================================
 * PCIe DMA STATUS REGISTER (0xB296)
 * ===========================================================================
 * This register serves dual purposes:
 *
 *   As Status (Read):
 *     Bit 0: Error flag - transaction failed
 *     Bit 1: Complete flag - checked at 0xBFE6 (ANL #0x02)
 *     Bit 2: Done flag - checked at 0xE3A7 (JNB ACC.2)
 *     Bits 1+2: Both set after successful completion
 *
 *   As Trigger (Write):
 *     0x08: Trigger USB vendor command DMA (E4/E5)
 *     0x0F: Trigger standard PCIe transaction
 *
 *   Firmware clears status by writing individual bits (0x01, 0x02, 0x04)
 *   before triggering new transactions.
 *
 * ===========================================================================
 * PCIe LINK STATE (0xB480)
 * ===========================================================================
 * Critical for USB enumeration path selection:
 *
 *   Bit 0: PCIe link active
 *     - SET: PCIe link is up, enables descriptor DMA path at 0x185C
 *     - CLEAR: Causes firmware at 0x20DA to clear G_PCIE_ENUM_DONE (0x0AF7)
 *
 *   Bit 1: PCIe tunnel ready
 *     - SET when USB4/Thunderbolt tunnel established
 *
 *   When 0xB480 bit 0 is CLEAR, firmware takes alternate path that doesn't
 *   use CEB2/CEB3 address registers. Must be SET for proper descriptor DMA.
 *
 * ===========================================================================
 * TLP FORMAT/TYPE CODES
 * ===========================================================================
 *   Config Space:
 *     0x04  Type 0 Configuration Read (local device)
 *     0x05  Type 1 Configuration Read (downstream device)
 *     0x44  Type 0 Configuration Write (local device)
 *     0x45  Type 1 Configuration Write (downstream device)
 *   Memory Access:
 *     0x00  Memory Read Request (32-bit address)
 *     0x40  Memory Write Request (32-bit address)
 *
 * ===========================================================================
 * REGISTER MAP (0xB200-0xB4FF)
 * ===========================================================================
 *
 * TLP Registers (0xB200-0xB25F):
 *   0xB210  PCIE_FMT_TYPE    TLP format/type byte
 *   0xB213  PCIE_TLP_CTRL    TLP control (0x01 to enable)
 *   0xB216  PCIE_TLP_LENGTH  TLP length/mode (usually 0x20)
 *   0xB217  PCIE_BYTE_EN     Byte enable mask (0x0F for all bytes)
 *   0xB218  PCIE_ADDR_0      Address bits 7:0
 *   0xB219  PCIE_ADDR_1      Address bits 15:8
 *   0xB21A  PCIE_ADDR_2      Address bits 23:16
 *   0xB21B  PCIE_ADDR_3      Address bits 31:24
 *   0xB220  PCIE_DATA        Data register (4 bytes)
 *   0xB22A  PCIE_LINK_STATUS Link status (speed in bits 7:5)
 *   0xB22B  PCIE_CPL_STATUS  Completion status code
 *   0xB22C  PCIE_CPL_DATA    Completion data
 *   0xB238  PCIE_BUSY        Transaction busy flag (poll for 0)
 *   0xB254  PCIE_TRIGGER     Transaction trigger (write 0x0F)
 *
 * DMA Status (0xB290-0xB2FF):
 *   0xB296  PCIE_STATUS      Status/Trigger register:
 *                              Read: Bit 0=error, Bit 1=complete, Bit 2=done
 *                              Write 0x08: Trigger E4/E5 DMA
 *                              Write 0x0F: Trigger PCIe TLP
 *
 * Link Control (0xB400-0xB4FF):
 *   0xB401  PCIE_TUNNEL_EN   Tunnel enable (0x01 = enabled)
 *   0xB432  PCIE_LINK_CFG    Link configuration (0x07 = active)
 *   0xB480  PCIE_LINK_STATE  Link state:
 *                              Bit 0: Link active (must be SET)
 *                              Bit 1: Tunnel ready
 *
 * ===========================================================================
 * TRANSACTION SEQUENCE
 * ===========================================================================
 *   1. Setup TLP:
 *      - Write format/type to PCIE_FMT_TYPE
 *      - Write 0x01 to PCIE_TLP_CTRL (enable)
 *      - Write byte enables to PCIE_BYTE_EN
 *      - Write address to PCIE_ADDR_0..3
 *      - For writes: write data to PCIE_DATA
 *   2. Trigger:
 *      - pcie_clear_and_trigger() writes 0x01, 0x02, 0x04 to clear flags
 *      - Then writes 0x0F to PCIE_TRIGGER
 *   3. Poll:
 *      - Loop calling pcie_get_completion_status() until non-zero
 *      - Call pcie_write_status_complete()
 *   4. Result:
 *      - Check PCIE_STATUS bit 1 for completion
 *      - Check PCIE_STATUS bit 0 for errors
 *      - For reads: read data from PCIE_CPL_DATA
 *
 * ===========================================================================
 * KEY XDATA GLOBALS
 * ===========================================================================
 *   0x053F  G_PCIE_LINK_PORT0    PCIe link state port 0 (must be non-zero)
 *   0x0553  G_PCIE_LINK_PORT1    PCIe link state port 1
 *   0x05A3  G_CMD_SLOT_INDEX     Current command slot (0-9)
 *   0x05A6  PCIE_TXN_COUNT       Transaction counter (16-bit)
 *   0x05AE  PCIE_DIRECTION       Bit 0 = 0 for read, 1 for write
 *   0x05AF  PCIE_ADDR_CACHE      Target PCIe address (4 bytes)
 *   0x05B1  G_CMD_TABLE_BASE     Command table (state != 4 for DMA path)
 *   0x06E6  STATE_FLAG           Error flag
 *   0x06EA  ERROR_CODE           Error code (0xFE = PCIe error)
 *   0x0AF7  G_PCIE_ENUM_DONE     PCIe enumeration complete (must be 1)
 *
 * ===========================================================================
 * E4/E5 ADDRESS FORMAT
 * ===========================================================================
 * The USB host sends addresses in a specific format:
 *
 *   CDB byte 2: Address bits 23:16 (always 0x50 for XDATA access)
 *   CDB byte 3: Address bits 15:8
 *   CDB byte 4: Address bits 7:0
 *
 *   Full address: 0x50XXXX where XXXX is the XDATA address
 *
 *   Example: To read XDATA[0x1234]:
 *     CDB = [E4, size, 0x50, 0x12, 0x34, 0x00]
 *     DMA copies XDATA[0x1234] to USB buffer at 0x8000
 *
 * ===========================================================================
 * ISR/HANDLER CODE PATHS
 * ===========================================================================
 *   0x35B7: PCIe vendor handler entry (pcie_vendor_handler_35b7)
 *   0x35DA: E4 command check
 *   0x35DF: Call E4 read handler at 0x54BB
 *   0x35E2: Setup PCIe registers
 *   0x35F9: Call PCIe transfer at 0x3C1E
 *   0x3601: Check DMA status at G_DMA_XFER_STATUS (0x0AA0)
 *   0x36E4: Vendor handler exit
 *
 * ===========================================================================
 * DISPATCH TABLE (0x0570-0x0650)
 * ===========================================================================
 *   Maps event indices to handlers. Each entry is 5 bytes.
 *   Entries marked "Bank 1" use DPX=1 for extended addressing.
 */
#ifndef _PCIE_H_
#define _PCIE_H_

#include "../types.h"

/* PCIe initialization */
uint8_t pcie_init(void);                    /* 0xc20c-0xc244 */
uint8_t pcie_init_alt(void);                /* 0xc245-0xc26f */
void pcie_init_b296_regs(void);             /* 0x9916-0x9922 */
void pcie_init_idata_65_63(void);           /* 0x9923-0x992f */

/* PCIe transaction control */
void pcie_clear_and_trigger(void);          /* 0x999d-0x99ae */
uint8_t pcie_get_completion_status(void);   /* 0x99eb-0x99f5 */
uint8_t pcie_get_link_speed(void);          /* 0x9a60-0x9a6b */
uint8_t pcie_get_link_speed_masked(void);   /* 0x9a30-0x9a3a */
void pcie_set_byte_enables(uint8_t byte_en);/* 0x9a74-0x9a7e */
void pcie_set_byte_enables_0f(void);        /* 0x9a95-0x9a9b */
uint8_t pcie_read_completion_data(void);    /* 0x9902-0x990b */
void pcie_write_status_complete(void);      /* 0x990c-0x9915 */
void pcie_write_status_error(void);         /* 0x99f6-0x99ff */
void pcie_write_status_done(void);          /* 0x9a00-0x9a08 */
uint8_t pcie_check_status_complete(void);   /* 0x9a09-0x9a0f */
uint8_t pcie_check_status_error(void);      /* 0x9a10-0x9a1f */

/* PCIe address and buffer setup */
void pcie_set_idata_params(void);           /* 0x9930-0x994a */
void pcie_clear_address_regs(void);         /* 0x9a9c-0x9aa2 */
void pcie_clear_address_regs_full(void);    /* 0x9a8a-0x9a94 */
void pcie_setup_buffer_params(uint16_t addr);           /* 0x9aa9-0x9ab2 */
void pcie_setup_buffer_params_ext(uint8_t idx);         /* 0x9aa3-0x9ab2 */
void pcie_setup_buffer_from_config(void);   /* 0x9ab3-0x9ab9 */
void pcie_clear_reg_at_offset(uint8_t offset);          /* 0x994c-0x9953 */

/* PCIe transaction counters */
void pcie_inc_txn_counters(void);           /* 0x9954-0x9961 */
void pcie_inc_txn_count(void);              /* 0x995a-0x9961 */
uint8_t pcie_get_txn_count_hi(void);        /* 0x9962-0x9969 */
uint8_t pcie_get_txn_count_with_mult(void); /* 0x996a-0x9976 */
uint8_t pcie_check_txn_count(void);         /* 0x9977-0x997f */
void pcie_store_txn_idx(uint8_t idx);       /* 0x9980-0x9989 */
void pcie_store_r6_to_05a6(uint8_t val);    /* 0x998a-0x9995 */

/* PCIe completion handling */
uint8_t pcie_wait_for_completion(void);     /* 0x9996-0x999c */
uint8_t pcie_poll_and_read_completion(void);/* 0x99af-0x99bb */

/* TLP operations */
uint8_t pcie_setup_memory_tlp(void);        /* 0x99bc-0x99c5 */
void pcie_write_tlp_addr_low(uint8_t val);  /* 0x99c6-0x99cd */
void pcie_setup_config_tlp(void);           /* 0x99ce-0x99d4 */
uint8_t pcie_tlp_handler_b104(void);        /* 0xb104-0xb1ca */
void pcie_tlp_handler_b28c(void);           /* 0xb28c-0xb401 */
uint8_t pcie_tlp_handler_b402(void);        /* 0xb402-0xb623 */
uint8_t pcie_tlp_init_and_transfer(void);   /* 0x99d5-0x99df */
void tlp_init_addr_buffer(void);            /* 0x99e0-0x99ea */
uint8_t tlp_write_flash_cmd(uint8_t cmd);   /* 0x9a18-0x9a1f */

/* PCIe event and interrupt handlers */
void pcie_event_handler(void);              /* 0xa522-0xa62c */
void pcie_tunnel_enable(void);              /* 0xc00d-0xc0ff */
void pcie_tunnel_setup(void);               /* 0xcd6c-0xcdff */
void pcie_adapter_config(void);             /* 0xadc3-0xae54 */
void pcie_interrupt_handler(void);          /* 0xa53a-0xa5ff */
uint8_t pcie_queue_handler_a62d(void);      /* 0xa62d-0xa636 */
void pcie_set_interrupt_flag(void);         /* 0xa637-0xa643 */

/* PCIe configuration table lookups */
__xdata uint8_t *pcie_config_table_lookup(void);        /* 0x9a3b-0x9a45 */
__xdata uint8_t *pcie_idata_table_lookup(void);         /* 0x9a46-0x9a6b */
__xdata uint8_t *pcie_param_table_lookup(uint8_t idx);  /* 0x9a6c-0x9aa2 */
__xdata uint8_t *pcie_lookup_r3_multiply(uint8_t idx);  /* 0x9a53-0x9a5f */
__xdata uint8_t *pcie_lookup_r6_multiply(uint8_t idx);  /* 0x9a33-0x9a3a */
__xdata uint8_t *pcie_offset_table_lookup(void);        /* 0x994e-0x9953 */

/* PCIe data register operations */
void pcie_write_data_reg_with_val(uint8_t val, uint8_t r6, uint8_t r7);  /* 0x9aba-0x9acf */
void pcie_write_data_reg(uint8_t r4, uint8_t r5, uint8_t r6, uint8_t r7);/* 0x9ad0-0x9aef */
uint8_t pcie_calc_queue_idx(uint8_t val);   /* 0x9af0-0x9aff */

/* PCIe store operations */
void pcie_store_to_05b8(uint8_t idx, uint8_t val);      /* 0x9b00-0x9b0f */
void pcie_read_and_store_idata(__xdata uint8_t *ptr);   /* 0x9b10-0x9b1f */
void pcie_store_r7_to_05b7(uint8_t idx, uint8_t val);   /* 0x9b20-0x9b2f */
void pcie_set_0a5b_flag(__xdata uint8_t *ptr, uint8_t val);  /* 0x9b30-0x9b3f */
void pcie_inc_0a5b(void);                   /* 0x9b40-0x9b4f */
void pcie_lookup_and_store_idata(uint8_t idx, uint16_t base);  /* 0x9b50-0x9b5f */
void pcie_write_config_and_trigger(__xdata uint8_t *ptr, uint8_t val);  /* 0x9b60-0x9b6f */
uint8_t pcie_get_status_bit2(void);         /* 0x9b70-0x9b7f */
void pcie_add_2_to_idata(uint8_t val);      /* 0x9b80-0x9b8f */

/* PCIe address operations */
void pcie_addr_store_839c(uint8_t p1, uint8_t p2, uint8_t p3, uint8_t p4);  /* 0x839c-0x83b8 */
void pcie_addr_store_83b9(uint8_t p1, uint8_t p2, uint8_t p3, uint8_t p4);  /* 0x83b9-0x83d5 */

/* PCIe state and error handlers (Bank 1) */
void pcie_state_clear_ed02(void);           /* 0xed02-0xed1f (Bank 1) */
void pcie_handler_unused_eef9(void);        /* 0xeef9-0xef0f (Bank 1) */
void pcie_nvme_event_handler(void);         /* 0xed20-0xed8f (Bank 1) */
void pcie_error_dispatch(void);             /* 0xed90-0xedff (Bank 1) */
void pcie_event_bit5_handler(void);         /* 0xee00-0xee4f (Bank 1) */
void pcie_timer_bit4_handler(void);         /* 0xee50-0xee9f (Bank 1) */

/* PCIe initialization helpers (Bank 1) */
uint8_t pcie_init_read_e8f9(void);          /* 0xe8f9-0xe901 (Bank 1) */
uint8_t pcie_init_write_e902(void);         /* 0xe902-0xe90a (Bank 1) */
void pcie_handler_e890(void);               /* 0xe890-0xe8f8 (Bank 1) */
void pcie_txn_setup_e775(void);             /* 0xe775-0xe787 (Bank 1) */
void pcie_channel_setup_e19e(void);         /* 0xe19e-0xe1c5 (Bank 1) */
void pcie_dma_config_e330(void);            /* 0xe330-0xe351 (Bank 1) */
void pcie_channel_disable_e5fe(void);       /* 0xe5fe-0xe616 (Bank 1) */
void pcie_disable_and_trigger_e74e(void);   /* 0xe74e-0xe761 (Bank 1) */
void pcie_wait_and_ack_e80a(void);          /* 0xe80a-0xe81a (Bank 1) */
void pcie_trigger_cc11_e8ef(void);          /* 0xe8ef-0xe8f8 (Bank 1) */
void clear_pcie_status_bytes_e8cd(void);    /* 0xe8cd-0xe8ee (Bank 1) */
uint8_t get_pcie_status_flags_e00c(void);   /* 0xe00c-0xe01f (Bank 1) */

/* Flash DMA trigger (PCIe-initiated) */
void flash_dma_trigger_handler(void);       /* 0xaf5e-0xafff */

/* NVMe queue operations via PCIe */
void nvme_cmd_setup_b624(void);             /* 0xb624-0xb6ce */
void nvme_cmd_setup_b6cf(void);             /* 0xb6cf-0xb778 */
void nvme_cmd_setup_b779(void);             /* 0xb779-0xb81f */
void nvme_queue_b825(void);                 /* 0xb825-0xb832 */
void nvme_queue_b833(void);                 /* 0xb833-0xb837 */
void nvme_queue_b838(void);                 /* 0xb838-0xb847 */
void nvme_queue_b850(void);                 /* 0xb850-0xb850 */
void nvme_queue_b851(void);                 /* 0xb851-0xb880 */
void nvme_queue_submit(void);               /* 0xb881-0xb8a1 */
void nvme_queue_poll(void);                 /* 0xb8a2-0xb8b8 */
void nvme_completion_poll(void);            /* 0xb8b9-0xba05 */

#endif /* _PCIE_H_ */
