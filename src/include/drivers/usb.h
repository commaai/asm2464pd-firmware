/*
 * usb.h - USB Interface Driver
 *
 * USB host interface controller for the ASM2464PD USB4/Thunderbolt to NVMe
 * bridge. Implements USB Mass Storage Class using Bulk-Only Transport (BOT)
 * protocol to expose NVMe drives as SCSI devices to the host.
 *
 * ===========================================================================
 * HARDWARE CAPABILITIES
 * ===========================================================================
 *   USB 3.2 Gen2x2 (20 Gbps) SuperSpeed+
 *   USB4/Thunderbolt 3/4 tunneling
 *   8 configurable endpoints (EP0-EP7)
 *   Hardware DMA for bulk and control transfers
 *
 * ===========================================================================
 * USB STATE MACHINE (IDATA[0x6A])
 * ===========================================================================
 * The USB controller maintains a state machine for enumeration:
 *
 *   State 0: DISCONNECTED  - No USB connection detected
 *   State 1: ATTACHED      - Cable connected, VBUS present
 *   State 2: POWERED       - Bus powered, awaiting reset
 *   State 3: DEFAULT       - Default address (0) assigned after reset
 *   State 4: ADDRESS       - Unique device address assigned (SET_ADDRESS)
 *   State 5: CONFIGURED    - Configuration selected, ready for data transfer
 *
 * State Transitions via MMIO:
 *   - 0x9000 bit 7: Cable connected detection
 *   - 0x9000 bit 0: USB activity (SET enables USB handler at ISR 0x0E68)
 *   - 0x9101 bits: Control different ISR code paths
 *   - 0xCE89 bits: DMA state machine for enumeration progress
 *   - 0x92C2 bit 6: Power state (CLEAR for ISR, SET for main loop)
 *
 * ===========================================================================
 * CONTROL TRANSFER TWO-PHASE PROTOCOL
 * ===========================================================================
 * USB control transfers use a two-phase hardware protocol:
 *
 *   Phase 1 - SETUP (0x9091 bit 0):
 *     - Hardware receives 8-byte setup packet from host
 *     - Writes setup packet to 0x9E00-0x9E07
 *     - Sets 0x9091 bit 0 to signal firmware
 *     - ISR at 0xCDE7 calls 0xA5A6 (setup packet handler)
 *     - Firmware parses bmRequestType, bRequest, wValue, wIndex, wLength
 *     - For GET_DESCRIPTOR: sets 0x07E1 = 5, loops writing 0x01 to 0x9091
 *     - Hardware clears bit 0 when ready for data phase
 *
 *   Phase 2 - DATA (0x9091 bit 1):
 *     - Hardware sets bit 1 to indicate data phase ready
 *     - ISR at 0xCDE7 calls 0xD088 (DMA response handler)
 *     - Checks 0x07E1 == 5 for GET_DESCRIPTOR
 *     - Triggers descriptor DMA from ROM to USB buffer
 *
 *   Setup Packet Format (0x9E00-0x9E07):
 *     Byte 0: bmRequestType (direction[7], type[6:5], recipient[4:0])
 *     Byte 1: bRequest (request code: 0x06 = GET_DESCRIPTOR)
 *     Byte 2-3: wValue (descriptor type[15:8], index[7:0])
 *     Byte 4-5: wIndex (language ID for strings)
 *     Byte 6-7: wLength (max bytes to return)
 *
 * ===========================================================================
 * DESCRIPTOR DMA ARCHITECTURE
 * ===========================================================================
 * USB descriptors are stored in code ROM and transferred via hardware DMA:
 *
 *   1. Firmware reads setup packet from 0x9E00-0x9E07
 *   2. Determines descriptor type from wValue high byte
 *   3. Looks up descriptor address in ROM (table at 0x0864)
 *   4. Writes source address to 0x905B/0x905C (high/low)
 *   5. Writes length to 0x9004 (EP0 transfer length)
 *   6. Writes 0x01 to 0x9092 (USB DMA trigger)
 *   7. Hardware DMAs descriptor from ROM to USB TX buffer
 *   8. Sets 0xE712 bits 0,1 when transfer complete
 *
 *   Descriptor ROM Locations:
 *     0x0627: Device descriptor (18 bytes)
 *     0x063B: Language ID string (4 bytes)
 *     0x58CF: USB3 Configuration descriptor (121 bytes with alt settings)
 *     0x5948: USB2 Configuration descriptor (32 bytes, 512-byte packets)
 *     0x593x: String descriptors
 *
 *   NOTE: The emulator does NOT search ROM for descriptors. The FIRMWARE
 *   handles all descriptor lookup and DMA configuration. The hardware just
 *   moves bytes from the address firmware specifies.
 *
 * ===========================================================================
 * VENDOR COMMAND FLOW (E4 Read / E5 Write)
 * ===========================================================================
 * Vendor commands allow host software to read/write XDATA directly:
 *
 *   E4 (XDATA Read):
 *     - Host sends CDB: [E4, size, addr_hi, addr_mid, addr_lo, 0]
 *     - Firmware reads CDB from 0x910D-0x9112
 *     - Configures PCIe DMA source address (0x50XXXX format)
 *     - Triggers DMA via 0xB296 write (value 0x08)
 *     - Hardware copies XDATA[addr] to USB buffer at 0x8000
 *     - Host reads response from bulk endpoint
 *
 *   E5 (XDATA Write):
 *     - Host sends CDB: [E5, value, addr_hi, addr_mid, addr_lo, 0]
 *     - Firmware reads CDB and value from MMIO
 *     - Writes single byte to XDATA[addr]
 *     - No data phase needed
 *
 *   CDB Register Layout (0x910D-0x9112):
 *     0x910D: Command type (0xE4 or 0xE5)
 *     0x910E: Size (E4) or Value (E5)
 *     0x910F: Address bits 23:16 (always 0x50 for XDATA)
 *     0x9110: Address bits 15:8
 *     0x9111: Address bits 7:0
 *     0x9112: Reserved
 *
 * ===========================================================================
 * USB DMA STATE MACHINE (0xCE89)
 * ===========================================================================
 * The DMA state register controls enumeration and transfer progress:
 *
 *   Bit 0 (USB_DMA_STATE_READY):
 *     - Firmware polls at 0x348C waiting for this bit
 *     - SET to exit enumeration wait loop
 *
 *   Bit 1 (USB_DMA_STATE_CBW):
 *     - Checked at 0x3493; 1=CBW received, 0=bulk data
 *
 *   Bit 2 (USB_DMA_STATE_ERROR):
 *     - DMA error in copy loop (stock: 0x3546)
 *
 *   Read callback increments internal counter and returns appropriate
 *   bits based on enumeration progress.
 *
 * ===========================================================================
 * REGISTER MAP (0x9000-0x91FF)
 * ===========================================================================
 *   0x9000  USB_STATUS       Main status (bit 0: activity, bit 7: connected)
 *   0x9001  USB_CONTROL      Control register
 *   0x9002  USB_CONFIG       Configuration (bit 1 must be CLEAR for 0x9091 check)
 *   0x9003  USB_EP0_STATUS   EP0 status
 *   0x9004  USB_EP0_LEN_LO   EP0 transfer length low (descriptor DMA length)
 *   0x9005  USB_EP0_LEN_HI   EP0 transfer length high
 *   0x9006  USB_EP0_CONFIG   EP0 mode (bit 0: enable, bit 7: ready)
 *   0x9007  USB_SCSI_LEN_LO  SCSI buffer length low
 *   0x9008  USB_SCSI_LEN_HI  SCSI buffer length high
 *   0x905B  USB_EP_BUF_HI    DMA source address high (descriptor ROM addr)
 *   0x905C  USB_EP_BUF_LO    DMA source address low
 *   0x9091  USB_CTRL_PHASE   Control transfer phase (bit 0: setup, bit 1: data)
 *   0x9092  USB_DMA_TRIGGER  Write 0x01 to start descriptor DMA
 *   0x9093  USB_EP_CFG1      Endpoint config 1
 *   0x9094  USB_EP_CFG2      Endpoint config 2
 *   0x9096  USB_EP_READY     Endpoint ready flags
 *   0x9100  USB_LINK_STATUS  USB speed (0=FS, 1=HS, 2=SS, 3=SS+)
 *   0x9101  USB_PERIPH       Peripheral status/interrupt routing:
 *                              Bit 0: EP0 control active
 *                              Bit 1: Descriptor request (triggers 0x033B)
 *                              Bit 3: Bulk transfer request
 *                              Bit 5: Vendor command path (0x5333)
 *                              Bit 6: USB init / suspended
 *   0x910D-0x9112            CDB registers for vendor commands
 *   0x9118  USB_EP_STATUS    Endpoint status bitmap (8 endpoints)
 *   0x911B  USB_BUFFER_ALT   Buffer alternate
 *   0x9E00-0x9E07            USB setup packet buffer (8 bytes)
 *
 * ===========================================================================
 * DMA/TRANSFER STATUS REGISTERS
 * ===========================================================================
 *   0xCE00  SCSI_DMA_CTRL    DMA control (write 0x03 to start, poll for 0)
 *   0xCE55  SCSI_TAG_VALUE   Transfer slot count for loop iterations
 *   0xCE86  XFER_STATUS      USB status (bit 4 checked at 0x349D)
 *   0xCE88  XFER_CTRL        DMA trigger (write resets CE89 state)
 *   0xCE89  USB_DMA_STATE    USB/DMA status (bits control state transitions)
 *   0xCE6C  XFER_STATUS_6C   USB controller ready (bit 7 must be SET)
 *   0xE712  LINK_STATUS_E712 Link/EP0 status (bit 0=busy, bit 1=done)
 *
 * ===========================================================================
 * BUFFER REGIONS
 * ===========================================================================
 *   0x8000-0x8FFF  USB_SCSI_BUF    USB/SCSI data buffer (4KB)
 *   0x9E00-0x9FFF  USB_CTRL_BUF    USB control transfer buffer (512 bytes)
 *   0xD800-0xDFFF  USB_EP_BUF      USB endpoint data buffer (2KB)
 *
 * ===========================================================================
 * ENDPOINT DISPATCH (ISR at 0x0E96-0x0EFB)
 * ===========================================================================
 *   Dispatch table at CODE 0x5A6A (256 bytes) maps status byte to EP index
 *   Bit mask table at 0x5B6A (8 bytes) maps EP index to clear mask
 *   Offset table at 0x5B72 (8 bytes) maps EP index to register offset
 *
 *   Algorithm:
 *   1. Read endpoint status from USB_EP_STATUS (0x9118)
 *   2. Look up primary EP index via ep_index_table[status]
 *   3. If index >= 8, exit (no endpoints need service)
 *   4. Read secondary status from USB_EP_BASE + ep_index1
 *   5. Look up secondary EP index
 *   6. Calculate combined offset = ep_offset_table[ep_index1] + ep_index2
 *   7. Call endpoint handler with combined offset
 *   8. Clear endpoint status via bit mask write
 *   9. Loop up to 32 times
 *
 * ===========================================================================
 * KEY XDATA GLOBALS
 * ===========================================================================
 *   0x05A3  G_CMD_SLOT_INDEX      Current command slot (0-9)
 *   0x05B1  G_CMD_TABLE_BASE      Command table (10 entries × 34 bytes)
 *   0x06E6  STATE_FLAG            Processing complete/error flag
 *   0x07E1  G_USB_REQUEST_TYPE    USB request type for descriptor handler
 *   0x07EC  G_USB_CMD_CONFIG      USB command configuration
 *   0x0AA0  G_DMA_XFER_STATUS     DMA transfer status/size
 *   0x0AD6  G_USB_SPEED_MODE      USB speed mode (set during enumeration)
 *   0x0AF7  G_PCIE_ENUM_DONE      PCIe enumeration complete flag
 *
 * ===========================================================================
 * ISR CODE PATHS
 * ===========================================================================
 *   0x0E68: Main USB ISR entry - checks 0x9000 bit 0 for USB activity
 *   0x0E6E: USB handling path (bit 0 SET)
 *   0x0E71: Uses 0x9118 as index into table at 0x5AC9
 *   0x0EF4: Vendor handler path when 0x9101 bit 5 SET
 *   0x5333: Vendor command processor
 *   0x35B7: PCIe vendor handler for E4/E5 commands
 *   0xA5A6: Setup packet handler (phase 1)
 *   0xD088: DMA response handler (phase 2)
 *   0xCDE7: Main loop checks 0x9091 for control transfer phases
 */
#ifndef _USB_H_
#define _USB_H_

#include "../types.h"

/* USB initialization and control */
void usb_enable(void);                      /* 0x1b7e-0x1b87 */
void usb_setup_endpoint(void);              /* 0x1bd5-0x1bdb */

/* USB endpoint processing */
void usb_ep_process(void);                  /* 0x52a7-0x52c6 */
void usb_buffer_handler(void);              /* 0xd810-0xd851 */
void usb_ep_dispatch_loop(void);            /* 0x0e96-0x0efb */
void usb_master_handler(void);              /* 0x10e0-0x117a */
void usb_ep_queue_process(void);            /* 0x1196-0x11xx */

/* USB endpoint configuration */
void usb_ep_config_bulk(void);              /* 0x1cfc-0x1d06 */
void usb_ep_config_int(void);               /* 0x1d07-0x1d11 */
void usb_ep_config_bulk_mode(void);         /* 0x1d12-0x1d1c */
void usb_ep_config_int_mode(void);          /* 0x1d12-0x1d1c (shared) */
void usb_set_ep0_mode_bit(void);            /* 0x1bde-0x1be7 */
void usb_set_ep0_config_bit0(void);         /* 0x1bde-0x1be7 (alias) */
void usb_write_ep_config(uint8_t hi, uint8_t lo);       /* 0x1bc1-0x1bdd */
void usb_write_ep_ctrl_by_mode(uint8_t mode);           /* 0x1bf6-0x1cfb */

/* USB transfer control */
void usb_set_transfer_flag(void);           /* 0x1d1d-0x1d23 */
void usb_set_done_flag(void);               /* 0x1787-0x178d */
void usb_set_transfer_active_flag(void);    /* 0x312a-0x3139 */
void usb_set_transfer_flag_1(void);         /* 0x178e-0x179c */
uint8_t usb_setup_transfer_mode5(void);     /* 0x8a3d-0x8a7d */

/* USB status and data */
uint8_t usb_get_nvme_data_ctrl(void);       /* 0x1d24-0x1d2a */
void usb_set_nvme_ctrl_bit7(__xdata uint8_t *ptr);      /* 0x1d2b-0x1d31 */
uint8_t usb_get_sys_status_offset(void);    /* 0x1743-0x1751 */
void usb_copy_status_to_buffer(void);       /* 0x3147-0x3167 */
uint16_t usb_read_status_pair(void);        /* 0x3181-0x3188 */
uint16_t usb_read_transfer_params(void);    /* 0x31a5-0x31ac */
uint8_t usb_get_nvme_data_ctrl_masked(void);            /* 0x1d24-0x1d2a (variant) */
uint8_t usb_get_nvme_dev_status_masked(uint8_t input);  /* 0x1b47-0x1b5f */
uint8_t usb_get_indexed_status(void);       /* 0x17a9-0x17c0 */

/* USB address calculation */
__xdata uint8_t *usb_calc_addr_with_offset(uint8_t offset);     /* 0x1752-0x175c */
__xdata uint8_t *usb_clear_idata_indexed(void);                 /* 0x3169-0x3180 */
__xdata uint8_t *usb_calc_dptr_0108(uint8_t index);             /* 0x31d5-0x31df */
__xdata uint8_t *usb_calc_dptr_with_0c(uint8_t val);            /* 0x31e0-0x31e9 */
__xdata uint8_t *usb_calc_dptr_direct(uint8_t val);             /* 0x31ea-0x31f3 */
__xdata uint8_t *usb_calc_queue_addr(uint8_t index);            /* 0x176b-0x1778 */
__xdata uint8_t *usb_calc_queue_addr_next(uint8_t index);       /* 0x1779-0x1786 */
__xdata uint8_t *usb_calc_indexed_addr(void);                   /* 0x179d-0x17a8 */
__xdata uint8_t *usb_get_config_offset_0456(void);              /* 0x1be9-0x1bf5 */
__xdata uint8_t *usb_calc_ep_queue_ptr(void);                   /* 0x1b2b-0x1b37 */
__xdata uint8_t *usb_calc_idx_counter_ptr(uint8_t val);         /* 0x1b38-0x1b46 */
__xdata uint8_t *usb_calc_status_table_ptr(void);               /* 0x17d8-0x17e7 */
__xdata uint8_t *usb_calc_work_area_ptr(void);                  /* 0x17e8-0x17f7 */
__xdata uint8_t *usb_calc_addr_plus_0f(uint8_t addr_lo, uint8_t addr_hi);  /* 0x17f8-0x1807 */
__xdata uint8_t *usb_calc_addr_01xx(uint8_t lo);                /* 0x1b2e-0x1b37 */
__xdata uint8_t *usb_calc_addr_012b_plus(uint8_t offset);       /* 0x1b30-0x1b37 */
__xdata uint8_t *usb_calc_addr_04b7_plus(void);                 /* 0x1808-0x1817 */

/* USB buffer operations */
void usb_set_status_bit7(__xdata uint8_t *addr);        /* 0x31ce-0x31d4 */
void usb_store_idata_16(uint8_t hi, uint8_t lo);        /* 0x1d32-0x1d38 */
void usb_add_masked_counter(uint8_t value);             /* 0x1d39-0x1d42 */
uint8_t usb_read_queue_status_masked(void);             /* 0x17c1-0x17cc */
uint8_t usb_shift_right_3(uint8_t val);                 /* 0x17cd-0x17d7 */

/* USB endpoint status */
uint8_t usb_calc_ep_status_addr(void);                  /* 0x1b88-0x1b95 */
uint8_t usb_get_ep_config_indexed(void);                /* 0x1b96-0x1ba4 */
uint16_t usb_read_buf_addr_pair(void);                  /* 0x1ba5-0x1bad */
uint8_t usb_get_idata_0x12_field(void);                 /* 0x1bae-0x1bc0 */
uint8_t usb_dec_indexed_counter(void);                  /* 0x1af9-0x1b13 */
uint8_t usb_read_ep_status_indexed(uint8_t input);      /* 0x1b15-0x1b2a */
uint8_t usb_get_ep_config_by_status(void);              /* 0x1818-0x1827 */
uint16_t usb_get_buf_addr(void);                        /* 0x1828-0x1837 */
uint8_t usb_get_idata12_high_bits(void);                /* 0x1838-0x1847 */
uint8_t usb_check_scsi_ctrl_nonzero(void);              /* 0x1848-0x1857 */
uint8_t usb_get_ep_config_txn(void);                    /* 0x1858-0x1867 */
uint8_t usb_check_idata_16_17_nonzero(void);            /* 0x1868-0x1877 */

/* USB initialization */
void usb_init_pcie_txn_state(void);                     /* 0x1d43-0x1d70 */
void usb_xfer_ctrl_init(void);                          /* 0x1a00-0x1aac */
void usb_ep_queue_init(uint8_t param);                  /* 0x1aad-0x1af6 */
void usb_reset_interface_full(void);                    /* 0x1878-0x18ff */
void usb_reset_interface(uint8_t param);                /* event_handler.c - sets up DPTR */

/* USB data operations */
void usb_xdata_copy_with_offset(uint8_t addr_lo, uint8_t addr_hi);  /* 0x1b15-0x1b2a */
void usb_nvme_dev_status_update(void);                  /* 0x1b47-0x1b5f */
uint16_t usb_marshal_idata_to_xdata(void);              /* 0x1b60-0x1b7d */
void usb_copy_idata_09_to_6b(void);                     /* 0x1b7e-0x1b85 */
void usb_copy_idata_6b_to_6f(void);                     /* 0x1b86-0x1b87 */
void usb_calc_buf_offset(uint8_t index);                /* 0x1900-0x190f */
uint8_t usb_lookup_code_table_5cad(uint8_t input);      /* 0x5cad-0x5cbd */
void usb_calc_dma_work_offset(__xdata uint8_t *ptr);    /* 0x1910-0x191f */
void usb_set_dma_mode_params(uint8_t val);              /* 0x1920-0x192f */
void usb_load_pcie_txn_count(__xdata uint8_t *ptr);     /* 0x1930-0x193f */
void usb_subtract_from_idata16(uint8_t hi, uint8_t lo); /* 0x1940-0x194f */
uint8_t usb_get_nvme_cmd_type(void);                    /* 0x1950-0x195f */
void usb_core_protocol_dispatch(void);                  /* 0x1960-0x196f */
void usb_inc_param_counter(void);                       /* 0x1971-0x197f */
void usb_copy_idata_16_to_xdata(__xdata uint8_t *ptr);  /* 0x1980-0x198f */
void usb_clear_nvme_status_bit1(void);                  /* 0x1990-0x199f */
void usb_add_nvme_param_20(void);                       /* 0x19a0-0x19af */
uint8_t usb_lookup_xdata_via_code(__code uint8_t *code_ptr, uint8_t offset);  /* 0x19b0-0x19bf */
void usb_set_reg_bit7(__xdata uint8_t *ptr);            /* 0x19c0-0x19cf */
void usb_store_idata_16_17(uint8_t hi, uint8_t lo);     /* 0x19d0-0x19df */
void usb_add_index_counter(uint8_t val);                /* 0x19e0-0x19ef */
uint8_t usb_check_signature(__xdata uint8_t *ptr);      /* 0x19f0-0x19ff */

/* USB DMA integration */
void usb_dma_transfer_setup(uint8_t mode, uint8_t size, uint8_t flags);  /* 0x3200-0x32ff */
void usb_scsi_dma_check_init(uint8_t param);            /* 0x3300-0x33ff */

/* USB status registers */
void usb_set_status_9093(void);                         /* 0x3400-0x340f */
void usb_read_flash_status_bits(void);                  /* 0x3410-0x341f */
uint8_t usb_set_xfer_mode_check_ctrl(uint8_t val, uint8_t compare);  /* 0x3420-0x342f */

/* USB buffer dispatch */
void usb_buffer_dispatch(void);                         /* 0xd852-0xd8ff */

/* USB vendor command processing */
void usb_vendor_command_processor(void);                /* 0x5333-0x5352 */
void usb_setup_transfer_flag_3169(void);                /* 0x3169-0x3178 */
void usb_set_ep0_bit7_320d(void);                       /* 0x320D-0x3213 */
void vendor_dispatch_4583(void);                        /* 0x4583-0x45C5 */
void pcie_vendor_handler_35b7(uint8_t param);           /* 0x35B7-0x36E4 */

/* Vendor command helper functions */
void vendor_copy_slot_index(void);                      /* 0x17b1-0x17ba */
__xdata uint8_t *vendor_get_cmd_table_ptr(void);        /* 0x1551-0x155b */
void vendor_clear_enum_flag(void);                      /* 0x54bb-0x54c0 */
uint8_t vendor_wait_dma_complete(uint8_t param);        /* 0x3c1e-0x3c6c */
void vendor_set_complete_flag(void);                    /* 0x1741-0x1747 */

/* USB descriptor handling */
void usb_get_descriptor_length(uint8_t param);          /* 0xa637-0xa650 */
void usb_convert_speed(uint8_t param);                  /* 0xa651-0xa6ff */

#endif /* _USB_H_ */
