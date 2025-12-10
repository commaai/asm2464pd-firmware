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
void scsi_setup_transfer_result(__xdata uint8_t *param);
void scsi_process_transfer(uint8_t param_lo, uint8_t param_hi);
void scsi_transfer_start(uint8_t param);
void scsi_transfer_check(void);
uint8_t scsi_transfer_start_alt(void);
uint8_t scsi_transfer_check_5069(uint8_t param);
void scsi_transfer_helper_4f77(uint8_t param);
void scsi_init_transfer_mode(uint8_t param);

/* SCSI state machine */
void scsi_state_dispatch(void);
void scsi_state_handler(void);
void scsi_state_dispatch_52b1(void);
void scsi_state_switch_4784(void);
void scsi_setup_action(uint8_t param);

/* SCSI DMA operations */
void scsi_dma_mode_setup(void);
void scsi_dma_dispatch(uint8_t param);
void scsi_dma_start_with_param(uint8_t param);
void scsi_dma_set_mode(uint8_t param);
void scsi_dma_check_mask(uint8_t param);
uint8_t scsi_dma_dispatch_helper(void);
void scsi_dma_config_4a57(void);
void scsi_dma_init_4be6(void);
uint8_t scsi_dma_transfer_process(uint8_t param);

/* SCSI command processing */
void scsi_command_dispatch(uint8_t flag, uint8_t param);
void scsi_cbw_parse(void);
uint8_t scsi_cbw_validate(void);
void scsi_dispatch_5426(void);

/* SCSI CSW (Command Status Wrapper) */
void scsi_csw_build(void);
void scsi_csw_send(uint8_t param_hi, uint8_t param_lo);
void scsi_csw_write_residue(void);
void scsi_csw_build_ext_488f(void);
void scsi_send_csw(uint8_t status, uint8_t param);

/* SCSI queue handling */
void scsi_queue_dispatch(uint8_t param);
void scsi_queue_process(void);
uint8_t scsi_queue_check_52c7(uint8_t index);
void scsi_queue_scan_handler(void);
void scsi_queue_setup_4b25(uint8_t param);
void scsi_endpoint_queue_process(void);

/* SCSI buffer operations */
void scsi_buffer_threshold_config(void);
void scsi_buffer_setup_4e25(void);
void scsi_transfer_dispatch(void);

/* SCSI slot table operations */
uint8_t scsi_read_slot_table(uint8_t offset);
void scsi_clear_slot_entry(uint8_t slot_offset, uint8_t data_offset);
void scsi_slot_config_46f8(uint8_t r7_val, uint8_t r5_val);
void scsi_init_slot_53d4(void);
void scsi_tag_setup_50ff(uint8_t tag_offset, uint8_t tag_value);

/* SCSI NVMe integration */
void scsi_nvme_queue_process(void);
void scsi_nvme_completion_read(void);
void scsi_nvme_setup_49e9(uint8_t param);

/* SCSI system status */
void scsi_sys_status_update(uint8_t param);
void scsi_decrement_pending(void);
uint8_t scsi_check_link_status(void);
void scsi_flash_ready_check(void);

/* SCSI endpoint configuration */
void scsi_ep_init_handler(void);
void scsi_ep_config_4e6d(void);

/* SCSI address/data helpers */
uint8_t scsi_addr_calc_5038(uint8_t param);
uint8_t scsi_xdata_read_5043(uint8_t param);
uint8_t scsi_xdata_read_5046(uint8_t low_addr);
uint8_t scsi_xdata_setup_504f(void);
uint8_t scsi_addr_adjust_5058(uint8_t param, uint8_t carry);
uint8_t scsi_xdata_read_505d(uint8_t param);
uint8_t scsi_xdata_read_5061(uint8_t low_addr, uint8_t carry);
uint8_t scsi_usbc_signature_check_51f9(__xdata uint8_t *ptr);
void scsi_reg_write_5398(uint8_t val);
uint8_t scsi_read_ctrl_indexed(void);

/* SCSI misc helpers */
void scsi_helper_5455(void);
void scsi_clear_mode_545c(void);
void scsi_helper_5462(void);
void scsi_loop_process_573b(void);
void scsi_core_process(void);
void scsi_handle_init_4d92(void);

/* SCSI debug */
void scsi_uart_print_hex(uint8_t value);
void scsi_uart_print_digit(uint8_t digit);

#endif /* _SCSI_H_ */
