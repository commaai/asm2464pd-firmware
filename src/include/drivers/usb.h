/*
 * usb.h - USB driver declarations
 */
#ifndef _USB_H_
#define _USB_H_

#include "../types.h"

/* USB initialization and control */
void usb_enable(void);
void usb_setup_endpoint(void);

/* USB endpoint processing */
void usb_ep_process(void);
void usb_buffer_handler(void);
void usb_ep_dispatch_loop(void);
void usb_master_handler(void);
void usb_ep_queue_process(void);

/* USB endpoint configuration */
void usb_ep_config_bulk(void);
void usb_ep_config_int(void);
void usb_ep_config_bulk_mode(void);
void usb_ep_config_int_mode(void);
void usb_set_ep0_mode_bit(void);
void usb_set_ep0_config_bit0(void);
void usb_write_ep_config(uint8_t hi, uint8_t lo);
void usb_write_ep_ctrl_by_mode(uint8_t mode);

/* USB transfer control */
void usb_set_transfer_flag(void);
void usb_set_done_flag(void);
void usb_set_transfer_active_flag(void);
void usb_set_transfer_flag_1(void);
uint8_t usb_setup_transfer_mode5(void);

/* USB status and data */
uint8_t usb_get_nvme_data_ctrl(void);
void usb_set_nvme_ctrl_bit7(__xdata uint8_t *ptr);
uint8_t usb_get_sys_status_offset(void);
void usb_copy_status_to_buffer(void);
uint16_t usb_read_status_pair(void);
uint16_t usb_read_transfer_params(void);
uint8_t usb_get_nvme_data_ctrl_masked(void);
uint8_t usb_get_nvme_dev_status_masked(uint8_t input);
uint8_t usb_get_indexed_status(void);

/* USB address calculation */
__xdata uint8_t *usb_calc_addr_with_offset(uint8_t offset);
__xdata uint8_t *usb_clear_idata_indexed(void);
__xdata uint8_t *usb_calc_dptr_0108(uint8_t index);
__xdata uint8_t *usb_calc_dptr_with_0c(uint8_t val);
__xdata uint8_t *usb_calc_dptr_direct(uint8_t val);
__xdata uint8_t *usb_calc_queue_addr(uint8_t index);
__xdata uint8_t *usb_calc_queue_addr_next(uint8_t index);
__xdata uint8_t *usb_calc_indexed_addr(void);
__xdata uint8_t *usb_get_config_offset_0456(void);
__xdata uint8_t *usb_calc_ep_queue_ptr(void);
__xdata uint8_t *usb_calc_idx_counter_ptr(uint8_t val);
__xdata uint8_t *usb_calc_status_table_ptr(void);
__xdata uint8_t *usb_calc_work_area_ptr(void);
__xdata uint8_t *usb_calc_addr_plus_0f(uint8_t addr_lo, uint8_t addr_hi);
__xdata uint8_t *usb_calc_addr_01xx(uint8_t lo);
__xdata uint8_t *usb_calc_addr_012b_plus(uint8_t offset);
__xdata uint8_t *usb_calc_addr_04b7_plus(void);

/* USB buffer operations */
void usb_set_status_bit7(__xdata uint8_t *addr);
void usb_store_idata_16(uint8_t hi, uint8_t lo);
void usb_add_masked_counter(uint8_t value);
uint8_t usb_read_queue_status_masked(void);
uint8_t usb_shift_right_3(uint8_t val);

/* USB endpoint status */
uint8_t usb_calc_ep_status_addr(void);
uint8_t usb_get_ep_config_indexed(void);
uint16_t usb_read_buf_addr_pair(void);
uint8_t usb_get_idata_0x12_field(void);
uint8_t usb_dec_indexed_counter(void);
uint8_t usb_read_ep_status_indexed(uint8_t input);
uint8_t usb_get_ep_config_by_status(void);
uint16_t usb_get_buf_addr(void);
uint8_t usb_get_idata12_high_bits(void);
uint8_t usb_check_scsi_ctrl_nonzero(void);
uint8_t usb_get_ep_config_txn(void);
uint8_t usb_check_idata_16_17_nonzero(void);

/* USB initialization */
void usb_init_pcie_txn_state(void);
void usb_xfer_ctrl_init(void);
void usb_ep_queue_init(uint8_t param);
void usb_reset_interface_full(void);

/* USB data operations */
void usb_xdata_copy_with_offset(uint8_t addr_lo, uint8_t addr_hi);
void usb_nvme_dev_status_update(void);
uint16_t usb_marshal_idata_to_xdata(void);
void usb_copy_idata_09_to_6b(void);
void usb_copy_idata_6b_to_6f(void);
void usb_calc_buf_offset(uint8_t index);
uint8_t usb_lookup_code_table_5cad(uint8_t input);
void usb_calc_dma_work_offset(__xdata uint8_t *ptr);
void usb_set_dma_mode_params(uint8_t val);
void usb_load_pcie_txn_count(__xdata uint8_t *ptr);
void usb_subtract_from_idata16(uint8_t hi, uint8_t lo);
uint8_t usb_get_nvme_cmd_type(void);
void usb_core_protocol_dispatch(void);
void usb_inc_param_counter(void);
void usb_copy_idata_16_to_xdata(__xdata uint8_t *ptr);
void usb_clear_nvme_status_bit1(void);
void usb_add_nvme_param_20(void);
uint8_t usb_lookup_xdata_via_code(__code uint8_t *code_ptr, uint8_t offset);
void usb_set_reg_bit7(__xdata uint8_t *ptr);
void usb_store_idata_16_17(uint8_t hi, uint8_t lo);
void usb_add_index_counter(uint8_t val);
uint8_t usb_check_signature(__xdata uint8_t *ptr);

/* USB DMA */
void usb_dma_transfer_setup(uint8_t mode, uint8_t size, uint8_t flags);
void usb_scsi_dma_check_init(uint8_t param);

/* USB status registers */
void usb_set_status_9093(void);
void usb_read_flash_status_bits(void);
uint8_t usb_set_xfer_mode_check_ctrl(uint8_t val, uint8_t compare);

/* USB buffer dispatch */
void usb_buffer_dispatch(void);

/* USB descriptor and speed */
void usb_get_descriptor_length(uint8_t param);
void usb_convert_speed(uint8_t param);

#endif /* _USB_H_ */
