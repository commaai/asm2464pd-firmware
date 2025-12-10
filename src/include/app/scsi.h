/*
 * scsi.h - SCSI/USB Mass Storage Protocol
 *
 * The SCSI subsystem implements the USB Mass Storage Class (MSC) protocol
 * using Bulk-Only Transport (BOT). It receives SCSI commands from the USB
 * host and translates them to NVMe operations on the connected storage.
 *
 * USB MASS STORAGE PROTOCOL:
 *   Host → CBW (Command Block Wrapper) → Device
 *   Host ↔ Data Phase (IN or OUT)      ↔ Device
 *   Host ← CSW (Command Status Wrapper) ← Device
 *
 * CBW STRUCTURE (31 bytes):
 *   Signature: "USBC" (0x43425355)
 *   Tag: Host-assigned transaction ID
 *   DataTransferLength: Expected data bytes
 *   Flags: Direction (0x80 = IN, 0x00 = OUT)
 *   LUN: Logical Unit Number
 *   CBWCBLength: Command block length (6-16)
 *   CBWCB[16]: SCSI Command Descriptor Block
 *
 * CSW STRUCTURE (13 bytes):
 *   Signature: "USBS" (0x53425355)
 *   Tag: Matches CBW tag
 *   DataResidue: Difference between expected and actual
 *   Status: 0x00=Passed, 0x01=Failed, 0x02=Phase Error
 *
 * SUPPORTED SCSI COMMANDS:
 *   0x00: TEST UNIT READY
 *   0x03: REQUEST SENSE
 *   0x12: INQUIRY
 *   0x1A: MODE SENSE(6)
 *   0x25: READ CAPACITY(10)
 *   0x28: READ(10)
 *   0x2A: WRITE(10)
 *   0x35: SYNCHRONIZE CACHE
 *   0xA0: REPORT LUNS
 *
 * STATE MACHINE:
 *   scsi_state_dispatch() drives the main state machine:
 *   IDLE → CBW_RECEIVED → DATA_PHASE → CSW_PENDING → IDLE
 *
 * DMA INTEGRATION:
 *   scsi_dma_* functions coordinate bulk data transfers between
 *   USB endpoints and internal buffers for NVMe translation.
 *
 * SLOT/TAG MANAGEMENT:
 *   Multiple commands can be queued using slots. Each slot tracks
 *   command state, allowing pipelined operation for better throughput.
 */
#ifndef _SCSI_H_
#define _SCSI_H_

#include "../types.h"

/* SCSI transfer control */
void scsi_setup_transfer_result(__xdata uint8_t *param);    /* 0x4013-0x4054 */
void scsi_process_transfer(uint8_t param_lo, uint8_t param_hi); /* 0x4042-0x40d8 */
void scsi_transfer_start(uint8_t param);        /* 0x40d9-0x419c */
void scsi_transfer_check(void);                 /* 0x419d-0x425e */
uint8_t scsi_transfer_start_alt(void);          /* 0x425f-0x43d2 */
uint8_t scsi_transfer_check_5069(uint8_t param);/* 0x5069-0x50a1 */
void scsi_transfer_helper_4f77(uint8_t param);  /* 0x4f77-0x4fb5 */
void scsi_init_transfer_mode(uint8_t param);    /* 0x43d3-0x4468 */

/* SCSI state machine */
void scsi_state_dispatch(void);                 /* 0x4469-0x4531 */
void scsi_state_handler(void);                  /* 0x4784-0x480b */
void scsi_state_dispatch_52b1(void);            /* 0x52b1-0x52c6 */
void scsi_state_switch_4784(void);              /* 0x4784-0x480b */
void scsi_setup_action(uint8_t param);          /* 0x46f8-0x4783 */

/* SCSI DMA operations */
void scsi_dma_mode_setup(void);                 /* 0x4a57-0x4abe */
void scsi_dma_dispatch(uint8_t param);          /* 0x4b25-0x4b5e */
void scsi_dma_start_with_param(uint8_t param);  /* 0x4be6-0x4c3f */
void scsi_dma_set_mode(uint8_t param);          /* 0x4c40-0x4c97 */
void scsi_dma_check_mask(uint8_t param);        /* 0x4c98-0x4d91 */
uint8_t scsi_dma_dispatch_helper(void);         /* 0x4d44-0x4d91 */
void scsi_dma_config_4a57(void);                /* 0x4a57-0x4abe */
void scsi_dma_init_4be6(void);                  /* 0x4be6-0x4c3f */
uint8_t scsi_dma_transfer_process(uint8_t param);   /* 0x4e25-0x4e6c */
void scsi_dma_queue_setup(uint8_t param);       /* 0x2F67-0x2F7F */
void scsi_dma_transfer_state(void);             /* 0x2DB7-0x2F66 */
void scsi_dma_tag_setup_3212(uint8_t idx, uint16_t reg_addr);  /* 0x3212-0x3225 */

/* SCSI core dispatch */
void scsi_core_dispatch(uint8_t param);         /* 0x4FF2-0x502D */

/* SCSI command processing */
void scsi_command_dispatch(uint8_t flag, uint8_t param);    /* 0x4532-0x45cf */
void scsi_cbw_parse(void);                      /* 0x45d0-0x466a */
uint8_t scsi_cbw_validate(void);                /* 0x466b-0x480b */
void scsi_dispatch_5426(void);                  /* 0x5426-0x5454 */

/* SCSI CSW (Command Status Wrapper) */
void scsi_csw_build(void);                      /* 0x53c0-0x53d3 */
void scsi_csw_send(uint8_t param_hi, uint8_t param_lo); /* 0x53e6-0x541e */
void scsi_csw_write_residue(void);              /* 0x53d4-0x53e5 */
void scsi_csw_build_ext_488f(void);             /* 0x488f-0x4903 */
void scsi_send_csw(uint8_t status, uint8_t param);  /* 0x53a7-0x53bf */

/* SCSI queue handling */
void scsi_queue_dispatch(uint8_t param);        /* 0x4904-0x4976 */
void scsi_queue_process(void);                  /* 0x4977-0x4b24 */
uint8_t scsi_queue_check_52c7(uint8_t index);   /* 0x52c7-0x52e5 */
void scsi_queue_scan_handler(void);             /* 0x480c-0x4903 */
void scsi_queue_setup_4b25(uint8_t param);      /* 0x4b25-0x4b5e */
void scsi_endpoint_queue_process(void);         /* 0x49e9-0x4a56 */

/* SCSI buffer operations */
void scsi_buffer_threshold_config(void);        /* 0x4abf-0x4b24 */
void scsi_buffer_setup_4e25(void);              /* 0x4e25-0x4e6c */
void scsi_transfer_dispatch(void);              /* 0x4e6d-0x4ef4 */

/* SCSI slot table operations */
uint8_t scsi_read_slot_table(uint8_t offset);   /* 0x5216-0x523b */
void scsi_clear_slot_entry(uint8_t slot_offset, uint8_t data_offset);   /* 0x5321-0x533c */
void scsi_slot_config_46f8(uint8_t r7_val, uint8_t r5_val); /* 0x46f8-0x4783 */
void scsi_init_slot_53d4(void);                 /* 0x53d4-0x53e5 */
void scsi_tag_setup_50ff(uint8_t tag_offset, uint8_t tag_value);    /* 0x50ff-0x5111 */

/* SCSI NVMe integration */
void scsi_nvme_queue_process(void);             /* 0x488f-0x4903 */
void scsi_nvme_completion_read(void);           /* 0x4ef5-0x4f36 */
void scsi_nvme_setup_49e9(uint8_t param);       /* 0x49e9-0x4a56 */

/* SCSI system status */
void scsi_sys_status_update(uint8_t param);     /* 0x5008-0x502d */
void scsi_decrement_pending(void);              /* 0x50a2-0x50da */
uint8_t scsi_check_link_status(void);           /* 0x5069-0x50fe */
void scsi_flash_ready_check(void);              /* 0x5112-0x5144 */

/* SCSI endpoint configuration */
void scsi_ep_init_handler(void);                /* 0x11a2-0x152x */
void scsi_ep_config_4e6d(void);                 /* 0x4e6d-0x4ef4 */

/* SCSI address/data helpers */
uint8_t scsi_addr_calc_5038(uint8_t param);     /* 0x5038-0x5042 */
uint8_t scsi_xdata_read_5043(uint8_t param);    /* 0x5043-0x504e */
uint8_t scsi_xdata_read_5046(uint8_t low_addr); /* 0x5046-0x504e */
uint8_t scsi_xdata_setup_504f(void);            /* 0x504f-0x505c */
uint8_t scsi_addr_adjust_5058(uint8_t param, uint8_t carry);    /* 0x5058-0x505c */
uint8_t scsi_xdata_read_505d(uint8_t param);    /* 0x505d-0x5068 */
uint8_t scsi_xdata_read_5061(uint8_t low_addr, uint8_t carry);  /* 0x5061-0x5068 */
uint8_t scsi_usbc_signature_check_51f9(__xdata uint8_t *ptr);   /* 0x51f9-0x5215 */
void scsi_reg_write_5398(uint8_t val);          /* 0x5398-0x53a3 */
uint8_t scsi_read_ctrl_indexed(void);           /* 0x519e-0x51c6 */

/* SCSI misc helpers */
void scsi_helper_5455(void);                    /* 0x5455-0x545b */
void scsi_clear_mode_545c(void);                /* 0x545c-0x5461 */
void scsi_helper_5462(void);                    /* 0x5462-0x5465 */
void scsi_loop_process_573b(void);              /* 0x573b-0x5764 */
void scsi_core_process(void);                   /* 0x1b07-0x1b13 */
void scsi_handle_init_4d92(void);               /* 0x4d92-0x4e6c */

/* SCSI debug */
void scsi_uart_print_hex(uint8_t value);        /* 0x51c7-0x51e5 */
void scsi_uart_print_digit(uint8_t digit);      /* 0x51e6-0x51ee */

#endif /* _SCSI_H_ */
