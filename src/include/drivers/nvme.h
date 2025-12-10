/*
 * nvme.h - NVMe Command and Queue Management
 *
 * The NVMe subsystem handles command construction, queue management, and
 * completion processing for NVMe storage devices connected via the PCIe
 * bridge. It translates SCSI commands from the USB Mass Storage layer
 * into NVMe commands.
 *
 * COMMAND FLOW:
 *   SCSI Command → nvme_build_cmd() → Submission Queue → NVMe Controller
 *                                                              ↓
 *   SCSI Status  ← nvme_check_completion() ← Completion Queue ←
 *
 * QUEUE ARCHITECTURE:
 *   - Admin Queue (QID 0): Controller management commands
 *   - I/O Queues (QID 1+): Read/Write/Flush commands
 *   - Hardware doorbells trigger queue processing
 *   - Circular buffer implementation with head/tail pointers
 *
 * SCSI-TO-NVME TRANSLATION:
 *   SCSI READ(10/12/16)  → NVMe Read command
 *   SCSI WRITE(10/12/16) → NVMe Write command
 *   SCSI SYNC CACHE      → NVMe Flush command
 *   SCSI INQUIRY         → NVMe Identify (cached)
 *   SCSI READ CAPACITY   → From Identify Namespace data
 *
 * KEY DATA STRUCTURES (IDATA):
 *   0x09-0x0D: Current command parameters
 *   0x16-0x17: Transfer length (16-bit)
 *   0x6B-0x6F: Queue state variables
 *
 * KEY REGISTERS:
 *   0xCC88-0xCC8A: Command engine control
 *   0xCC89: Command state (bit patterns control flow)
 *   0xE400-0xE42F: NVMe command configuration
 *
 * USAGE:
 *   1. nvme_init_registers() - Initialize NVMe subsystem
 *   2. nvme_build_cmd() - Construct NVMe command from SCSI
 *   3. nvme_submit_cmd() - Submit to hardware queue
 *   4. nvme_check_completion() - Poll/process completions
 */
#ifndef _NVME_H_
#define _NVME_H_

#include "../types.h"

/* NVMe initialization */
void nvme_set_usb_mode_bit(void);
void nvme_init_step(void);
void nvme_init_registers(void);
void nvme_wait_for_ready(void);
void nvme_initialize(__xdata uint8_t *ptr);

/* NVMe configuration */
__xdata uint8_t *nvme_get_config_offset(void);
void nvme_calc_buffer_offset(uint8_t index);
void nvme_load_transfer_data(void);
__xdata uint8_t *nvme_calc_idata_offset(void);

/* NVMe status */
uint8_t nvme_check_scsi_ctrl(void);
uint8_t nvme_get_cmd_param_upper(void);
uint8_t nvme_get_dev_status_upper(void);
uint8_t nvme_get_data_ctrl_upper(void);
uint8_t nvme_get_link_status_masked(void);
uint8_t nvme_get_idata_0d_r7(void);
uint8_t nvme_get_dma_status_masked(void);
uint8_t nvme_get_pcie_count_config(void);
uint8_t nvme_get_idata_009f(void);

/* NVMe data operations */
void nvme_subtract_idata_16(uint8_t hi, uint8_t lo);
void nvme_inc_circular_counter(void);
void nvme_set_ep_queue_ctrl_84(void);
void nvme_clear_status_bit1(void);
void nvme_set_data_ctrl_bit7(void);
void nvme_store_idata_16(uint8_t hi, uint8_t lo);
void nvme_add_to_global_053a(void);
void nvme_set_int_aux_bit1(void);
void nvme_set_ep_ctrl_bits(__xdata uint8_t *ptr);
void nvme_set_usb_ep_ctrl_bit2(__xdata uint8_t *ptr);
void nvme_set_buffer_flags(void);

/* NVMe address calculation */
__xdata uint8_t *nvme_calc_addr_01xx(uint8_t offset);
__xdata uint8_t *nvme_calc_addr_012b(uint8_t offset);
__xdata uint8_t *nvme_calc_addr_04b7(void);
__xdata uint8_t *nvme_calc_dptr_0500_base(uint8_t val);
__xdata uint8_t *nvme_calc_dptr_direct_with_carry(uint8_t val);
__xdata uint8_t *nvme_add_8_to_addr(uint8_t addr_lo, uint8_t addr_hi);
__xdata uint8_t *nvme_get_addr_012b(void);
__xdata uint8_t *nvme_calc_dptr_0100_base(uint8_t val);

/* NVMe completion and doorbell */
void nvme_check_completion(__xdata uint8_t *ptr);
void nvme_ring_doorbell(__xdata uint8_t *doorbell);
void nvme_read_and_sum_index(__xdata uint8_t *ptr);
void nvme_read_status(__xdata uint8_t *ptr);

/* NVMe DMA operations */
void nvme_write_params_to_dma(uint8_t val);
void nvme_calc_addr_from_dptr(__xdata uint8_t *ptr);
void nvme_copy_idata_to_dptr(__xdata uint8_t *ptr);

/* NVMe call and signal */
void nvme_call_and_signal(void);
void usb_validate_descriptor(void);

/* NVMe commands */
void nvme_process_cmd(uint8_t param);
void nvme_io_request(uint8_t param1, __xdata uint8_t *param2, uint8_t param3, uint8_t param4);
uint8_t nvme_build_cmd(uint8_t param);
uint8_t nvme_get_ep_table_entry(__xdata uint8_t *index_ptr);
void nvme_submit_cmd(void);
void nvme_io_handler(uint8_t param);

/* NVMe helper functions */
uint8_t nvme_func_1b07(void);
uint8_t nvme_func_1b0b(uint8_t param);
uint8_t nvme_check_threshold_r5(uint8_t val);
uint8_t nvme_check_threshold_0x3e(void);
uint8_t nvme_func_1c2a(uint8_t param);
void nvme_func_1c43(uint8_t param);
uint8_t nvme_func_1c55(void);
void nvme_func_1c7e(void);
uint8_t nvme_func_1c9f(void);
void nvme_func_1cf0(void);

/* USB check functions (in nvme.c) */
void usb_check_status(uint8_t param_1, __xdata uint8_t *param_2);
void usb_configure(__xdata uint8_t *ptr);
void usb_data_handler(__xdata uint8_t *ptr);

/* NVMe queue processing */
void nvme_process_queue_entries(void);
void nvme_state_handler(void);
void nvme_queue_sync(void);
void nvme_queue_process_pending(void);
void nvme_queue_helper(void);

/* NVMe command engine */
void nvme_cmd_store_and_trigger(uint8_t param, __xdata uint8_t *ptr);
void nvme_cmd_store_direct(uint8_t param, __xdata uint8_t *ptr);
uint8_t nvme_cmd_store_and_read(uint8_t param, __xdata uint8_t *ptr);
uint8_t nvme_cmd_read_offset(__xdata uint8_t *ptr);
void nvme_cmd_issue_with_setup(uint8_t param);
void nvme_cmd_issue_alternate(uint8_t param);
void nvme_cmd_issue_simple(uint8_t param);
void nvme_cmd_issue_with_tag(uint8_t param1, uint8_t param2);
void nvme_cmd_store_pair_trigger(uint8_t param1, __xdata uint8_t *ptr, uint8_t param2);
void nvme_cmd_set_state_6(void);
void nvme_timer_init_95b6(void);
void nvme_timer_ack_95bf(void);
void nvme_timer_ack_ptr(__xdata uint8_t *ptr);
void nvme_cmd_clear_5_bytes(__xdata uint8_t *ptr);
void nvme_cmd_set_bit1_e41c(void);
void nvme_cmd_set_bit1_ptr(__xdata uint8_t *ptr);
void nvme_cmd_shift_6(__xdata uint8_t *ptr, uint8_t val);
void nvme_int_ctrl_set_bit4(void);
void nvme_cmd_clear_cc88(void);
void nvme_cmd_store_clear_cc8a(uint8_t param, __xdata uint8_t *ptr);
uint8_t nvme_flash_check_xor5(void);
void nvme_cmd_clear_e405_setup(void);
uint8_t nvme_cmd_clear_bit4_mask(__xdata uint8_t *ptr);
void nvme_cmd_set_cc89_2(void);
void nvme_cmd_shift_6_store(uint8_t val, __xdata uint8_t *ptr);
void nvme_cmd_shift_2_mask3(uint8_t val, __xdata uint8_t *ptr);
void nvme_set_flash_counter_5(void);
void nvme_cmd_dd12_0x10(void);
uint8_t nvme_lba_combine(uint8_t val);

#endif /* _NVME_H_ */
