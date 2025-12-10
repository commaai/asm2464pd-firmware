/*
 * usb.h - USB Interface Driver
 *
 * USB host interface controller for the ASM2464PD USB4/Thunderbolt to NVMe
 * bridge. Implements USB Mass Storage Class using Bulk-Only Transport (BOT)
 * protocol to expose NVMe drives as SCSI devices to the host.
 *
 * HARDWARE CAPABILITIES
 *   USB 3.2 Gen2x2 (20 Gbps) SuperSpeed+
 *   USB4/Thunderbolt 3/4 tunneling
 *   8 configurable endpoints (EP0-EP7)
 *   Hardware DMA for bulk transfers
 *
 * DATA FLOW
 *   USB Host <-> USB Controller <-> Endpoint Buffers <-> DMA Engine
 *                     |                    |
 *                     v                    v
 *             Status Registers      SCSI/NVMe Translation
 *
 * ENDPOINT ARCHITECTURE
 *   EP0:     Control endpoint (enumeration, class requests)
 *   EP1-EP2: Bulk IN/OUT for Mass Storage data
 *   EP3-EP7: Reserved for additional interfaces
 *
 * REGISTER MAP (0x9000-0x91FF)
 *   0x9000  USB_STATUS       Main status (bit 0: activity, bit 7: connected)
 *   0x9001  USB_CONTROL      Control register
 *   0x9002  USB_CONFIG       Configuration
 *   0x9003  USB_EP0_STATUS   EP0 status
 *   0x9004  USB_EP0_LEN_LO   EP0 transfer length low
 *   0x9005  USB_EP0_LEN_HI   EP0 transfer length high
 *   0x9006  USB_EP0_CONFIG   EP0 mode (bit 0: USB mode)
 *   0x9007  USB_SCSI_LEN_LO  SCSI buffer length low
 *   0x9008  USB_SCSI_LEN_HI  SCSI buffer length high
 *   0x9091  INT_FLAGS_EX0    Extended interrupt flags
 *   0x9093  USB_EP_CFG1      Endpoint config 1
 *   0x9094  USB_EP_CFG2      Endpoint config 2
 *   0x9096  USB_EP_BASE      Indexed by endpoint number
 *   0x9101  USB_PERIPH       Peripheral status (bit 6: busy)
 *   0x9118  USB_EP_STATUS    Endpoint status bitmap (8 endpoints)
 *   0x911B  USB_BUFFER_ALT   Buffer alternate
 *
 * BUFFER CONTROL (0xD800-0xD8FF)
 *   0xD804-0xD807  Transfer status copy area
 *   0xD80C         Buffer transfer start
 *
 * ENDPOINT DISPATCH
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
 * WORK AREA GLOBALS (0x0000-0x0BFF)
 *   0x000A  EP_CHECK_FLAG         Endpoint processing check
 *   0x014E  Circular buffer index (5-bit)
 *   0x0218  Buffer address low
 *   0x0219  Buffer address high
 *   0x0464  SYS_STATUS_PRIMARY    Primary status for indexing
 *   0x0465  SYS_STATUS_SECONDARY  Secondary status
 *   0x054E  EP_CONFIG_ARRAY       Endpoint config array base
 *   0x0564  EP_QUEUE_CTRL         Endpoint queue control
 *   0x0565  EP_QUEUE_STATUS       Endpoint queue status
 *   0x05A6  PCIE_TXN_COUNT_LO     PCIe transaction count low
 *   0x05A7  PCIE_TXN_COUNT_HI     PCIe transaction count high
 *   0x06E6  STATE_FLAG            Processing complete/error flag
 *   0x07E4  SYS_FLAGS_BASE        System flags base (must be 1)
 *   0x0A7B  EP_DISPATCH_VAL1      First endpoint index
 *   0x0A7C  EP_DISPATCH_VAL2      Second endpoint index
 *   0x0AF2  TRANSFER_FLAG         Transfer active flag
 *   0x0AF5  EP_DISPATCH_OFFSET    Combined dispatch offset
 *   0x0AFA  TRANSFER_PARAM_LO     Transfer parameters low
 *   0x0AFB  TRANSFER_PARAM_HI     Transfer parameters high
 *   0x0B2E  USB_TRANSFER_FLAG     USB transfer in progress
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
__xdata uint8_t *usb_clear_idata_indexed(void);                 /* 0x3168-0x3180 */
__xdata uint8_t *usb_calc_dptr_0108(uint8_t index);             /* 0x31d5-0x31df */
__xdata uint8_t *usb_calc_dptr_with_0c(uint8_t val);            /* 0x31e0-0x31e9 */
__xdata uint8_t *usb_calc_dptr_direct(uint8_t val);             /* 0x31ea-0x31f3 */
__xdata uint8_t *usb_calc_queue_addr(uint8_t index);            /* 0x176b-0x1778 */
__xdata uint8_t *usb_calc_queue_addr_next(uint8_t index);       /* 0x1779-0x1786 */
__xdata uint8_t *usb_calc_indexed_addr(void);                   /* 0x179d-0x17a8 */
__xdata uint8_t *usb_get_config_offset_0456(void);              /* 0x1be8-0x1bf5 */
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
uint8_t usb_read_ep_status_indexed(uint8_t input);      /* 0x1b14-0x1b2a */
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

/* USB data operations */
void usb_xdata_copy_with_offset(uint8_t addr_lo, uint8_t addr_hi);  /* 0x1b14-0x1b2a */
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
void usb_inc_param_counter(void);                       /* 0x1970-0x197f */
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

/* USB descriptor handling */
void usb_get_descriptor_length(uint8_t param);          /* 0xa637-0xa650 */
void usb_convert_speed(uint8_t param);                  /* 0xa651-0xa6ff */

#endif /* _USB_H_ */
