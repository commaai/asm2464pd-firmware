/*
 * utils.h - Utility function declarations
 */
#ifndef _UTILS_H_
#define _UTILS_H_

#include "types.h"

/* Stub functions */
void pcie_short_delay(void);
void cmd_engine_wait_idle(void);
void link_state_init_stub(void);

/* IDATA dword operations */
uint32_t idata_load_dword(__idata uint8_t *ptr);
uint32_t idata_load_dword_alt(__idata uint8_t *ptr);
void idata_store_dword(__idata uint8_t *ptr, uint32_t val);

/* XDATA dword operations */
uint32_t xdata_load_dword(__xdata uint8_t *ptr);
uint32_t xdata_load_dword_alt(__xdata uint8_t *ptr);
void xdata_store_dword(__xdata uint8_t *ptr, uint32_t val);

/* Triple byte operations */
uint32_t xdata_load_triple(__xdata uint8_t *ptr);
void xdata_store_triple(__xdata uint8_t *ptr, uint32_t val);

/* Pointer math */
__xdata uint8_t *dptr_index_mul(__xdata uint8_t *base, uint8_t index, uint8_t element_size);

/* Register operations */
void reg_clear_bits_and_init(__xdata uint8_t *reg);
uint8_t reg_read_indexed_0a84(uint8_t offset, uint8_t base);
uint8_t reg_extract_bit6(__xdata uint8_t *dest, uint8_t val);
void reg_set_bits_1_2(__xdata uint8_t *reg);
uint8_t reg_extract_bit7(__xdata uint8_t *dest, uint8_t val);
uint8_t reg_clear_bit3_link_ctrl(__xdata uint8_t *reg);
uint8_t reg_write_indexed(uint8_t dph, uint8_t dpl, uint8_t val);
uint8_t reg_extract_bits_6_7(__xdata uint8_t *dest, uint8_t val);
uint8_t reg_extract_bit0(__xdata uint8_t *dest, uint8_t val);
void reg_set_bit6(__xdata uint8_t *reg);
void reg_set_bit1(__xdata uint8_t *reg);
__xdata uint8_t *reg_set_event_flag(void);
void reg_set_bit3(__xdata uint8_t *reg);
uint8_t reg_nibble_swap_store(__xdata uint8_t *reg);
uint8_t reg_nibble_extract(__xdata uint8_t *reg);
void reg_write_and_set_link_bit0(__xdata uint8_t *reg, uint8_t val);
void reg_timer_setup_and_set_bits(void);
void reg_timer_init_and_start(void);
void reg_timer_clear_bits(void);
void reg_set_bit5(__xdata uint8_t *reg);
void reg_clear_bits_5_6(__xdata uint8_t *reg);
uint8_t reg_read_cc3e_clear_bit1(void);
void reg_set_bit6_generic(__xdata uint8_t *reg);
void reg_clear_bit1_cc3b(void);
void reg_set_bit2(__xdata uint8_t *reg);
void reg_set_bit7(__xdata uint8_t *reg);
void reg_clear_state_flags(void);

/* Bank read functions */
uint8_t reg_read_bank_1235(void);
uint8_t reg_read_bank_0200(void);
uint8_t reg_read_bank_1200(void);
uint8_t reg_read_and_clear_bit3(uint8_t offset);
uint8_t reg_read_bank_1603(void);
uint8_t reg_read_bank_1504_clear(void);
uint8_t reg_read_bank_1200_alt(void);
uint8_t reg_read_event_mask(void);
uint8_t reg_read_bank_1407(void);
uint8_t reg_read_link_width(void);
uint8_t reg_read_link_status_e716(void);
uint8_t reg_read_cpu_mode_next(void);
uint8_t reg_read_phy_mode_lane_config(void);
uint8_t reg_delay_param_setup(void);
uint8_t reg_read_phy_lanes(void);

/* System initialization */
void init_sys_flags_07f0(void);

/* Delay functions */
void delay_short_e89d(void);
void delay_wait_e80a(uint16_t delay, uint8_t flag);

/* Comparison */
void cmp32(void) __naked;

/* Code/PDATA memory operations */
uint32_t code_load_dword(__code uint8_t *ptr);
void pdata_store_dword(__pdata uint8_t *ptr, uint32_t val);

/* Banked memory operations */
void banked_store_dword(uint8_t dpl, uint8_t dph, uint8_t bank, uint32_t val);
uint8_t banked_load_byte(uint8_t addrlo, uint8_t addrhi, uint8_t memtype);
void banked_store_byte(uint8_t addrlo, uint8_t addrhi, uint8_t memtype, uint8_t val);

/* Table search */
void table_search_dispatch_alt(void) __naked;
void table_search_dispatch(void) __naked;

/* PCIe transaction helpers */
void pcie_txn_index_load(void);
void pcie_txn_array_calc(void);

/* Helper functions */
void dptr_setup_stub(void);
void dptr_calc_ce40_indexed(uint8_t a, uint8_t b);
void dptr_calc_ce40_param(uint8_t param);
uint8_t get_ep_config_indexed(void);
void addr_setup_0059(uint8_t offset);
void mem_write_via_ptr(uint8_t value);
void dptr_calc_work43(void);
void dma_queue_ptr_setup(void);

/* System status pointers */
__xdata uint8_t *get_sys_status_ptr_0456(uint8_t param);
__xdata uint8_t *get_sys_status_ptr_0400(uint8_t param);

/* Misc helpers */
uint8_t xdata_read_0100(uint8_t low_byte, uint8_t carry);
uint8_t xdata_write_load_triple_1564(uint8_t value, uint8_t r1_addr, uint8_t r2_addr, uint8_t r3_mode);

/* Timer configuration */
void timer0_configure(uint8_t div_bits, uint8_t threshold_hi, uint8_t threshold_lo);
void timer0_reset(void);

#endif /* _UTILS_H_ */
