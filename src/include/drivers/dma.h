/*
 * dma.h - DMA driver declarations
 */
#ifndef _DMA_H_
#define _DMA_H_

#include "../types.h"

/* DMA control */
void dma_clear_status(void);
void dma_set_scsi_param3(void);
void dma_set_scsi_param1(void);
uint8_t dma_reg_wait_bit(__xdata uint8_t *ptr);
void dma_load_transfer_params(void);

/* DMA channel configuration */
void dma_config_channel(uint8_t channel, uint8_t r4_param);
void dma_setup_transfer(uint8_t r7_mode, uint8_t r5_param, uint8_t r3_param);
void dma_init_channel_b8(void);
void dma_init_channel_with_config(uint8_t config);
void dma_config_channel_0x10(void);

/* DMA status */
uint8_t dma_check_scsi_status(uint8_t mode);
void dma_clear_state_counters(void);
void dma_init_ep_queue(void);
uint8_t scsi_get_tag_count_status(void);
uint8_t dma_check_state_counter(void);
uint8_t scsi_get_queue_status(void);
uint8_t dma_shift_and_check(uint8_t val);

/* DMA transfer */
void dma_start_transfer(uint8_t aux0, uint8_t aux1, uint8_t count_hi, uint8_t count_lo);
void dma_set_error_flag(void);
void dma_setup_usb_rx(uint16_t len);
void dma_setup_usb_tx(uint16_t len);
void dma_wait_complete(void);

/* DMA address calculation */
uint8_t dma_get_config_offset_05a8(void);
__xdata uint8_t *dma_calc_offset_0059(uint8_t offset);
__xdata uint8_t *dma_calc_addr_0478(uint8_t index);
__xdata uint8_t *dma_calc_addr_0479(uint8_t index);
__xdata uint8_t *dma_calc_addr_00c2(void);
__xdata uint8_t *dma_calc_ep_config_ptr(void);
__xdata uint8_t *dma_calc_addr_046x(uint8_t offset);
__xdata uint8_t *dma_calc_addr_0466(uint8_t offset);
__xdata uint8_t *dma_calc_addr_0456(uint8_t offset);
uint16_t dma_calc_addr_002c(uint8_t offset, uint8_t high);

/* DMA SCSI operations */
uint8_t dma_shift_rrc2_mask(uint8_t val);
void dma_store_to_0a7d(uint8_t val);
void dma_calc_scsi_index(void);
uint8_t dma_write_to_scsi_ce96(void);
void dma_write_to_scsi_ce6e(void);
void dma_write_idata_to_dptr(__xdata uint8_t *ptr);
void dma_read_0461(void);
void dma_store_and_dispatch(uint8_t val);
void dma_clear_dword(__xdata uint8_t *ptr);

/* Transfer functions */
uint16_t transfer_set_dptr_0464_offset(void);
uint16_t transfer_calc_work43_offset(__xdata uint8_t *dptr);
uint16_t transfer_calc_work53_offset(void);
uint16_t transfer_get_ep_queue_addr(void);
uint16_t transfer_calc_work55_offset(void);
void transfer_func_16b0(uint8_t param);
void transfer_func_1633(uint16_t addr);

/* DMA handlers */
void dma_interrupt_handler(void);
void dma_transfer_handler(uint8_t param);
void transfer_continuation_d996(void);
void dma_poll_complete(void);
void dma_buffer_store_result_e68f(void);
void dma_poll_link_ready(void);

#endif /* _DMA_H_ */
