/*
 * pcie.h - PCIe/NVMe Bridge Driver
 *
 * The PCIe subsystem manages the connection between the USB interface and
 * NVMe storage devices. It handles PCIe link initialization, Transaction
 * Layer Packet (TLP) construction, and NVMe command/completion queue
 * management.
 *
 * HARDWARE ARCHITECTURE:
 *   USB Controller <---> PCIe Bridge <---> NVMe Controller <---> Storage
 *                            |
 *                     TLP Engine + DMA
 *
 * PCIe CAPABILITIES:
 *   - PCIe Gen3/Gen4 support (up to 16 GT/s per lane)
 *   - x4 lane configuration
 *   - Memory-mapped NVMe registers
 *   - Hardware TLP generation and completion handling
 *
 * TLP (Transaction Layer Packet) TYPES:
 *   - Memory Read/Write: Access NVMe controller registers
 *   - Configuration Read/Write: PCIe config space access
 *   - Completion: Response packets from NVMe controller
 *
 * KEY REGISTERS (0xB200-0xB2FF):
 *   0xB216: PCIE_TLP_LENGTH  - TLP payload length
 *   0xB217: PCIE_BYTE_EN     - Byte enable mask
 *   0xB22A: PCIE_LINK_STATUS - Link speed/width status
 *   0xB22C: PCIE_CPL_DATA    - Completion data register
 *   0xB254: PCIE_TRIGGER     - Transaction trigger
 *   0xB296: PCIE_STATUS      - Transaction status
 *     Bit 0: Error flag
 *     Bit 1: Complete flag
 *     Bit 2: Busy flag
 *
 * TRANSACTION FLOW:
 *   1. Setup TLP header (address, type, length)
 *   2. Write data to TLP buffer (for writes)
 *   3. Clear status flags via pcie_clear_and_trigger()
 *   4. Poll for completion via pcie_get_completion_status()
 *   5. Read completion data (for reads)
 *
 * NVME QUEUE MANAGEMENT:
 *   The driver manages NVMe submission and completion queues through
 *   PCIe memory-mapped I/O. Queue doorbells are rung via TLP writes
 *   to NVMe controller registers.
 *
 * USAGE:
 *   1. pcie_init() - Initialize PCIe link and controller
 *   2. pcie_setup_memory_tlp() - Configure TLP for memory access
 *   3. pcie_clear_and_trigger() - Execute transaction
 *   4. pcie_wait_for_completion() - Wait for response
 */
#ifndef _PCIE_H_
#define _PCIE_H_

#include "../types.h"

/* PCIe initialization */
uint8_t pcie_init(void);
uint8_t pcie_init_alt(void);
void pcie_init_b296_regs(void);
void pcie_init_idata_65_63(void);

/* PCIe transaction control */
void pcie_clear_and_trigger(void);
uint8_t pcie_get_completion_status(void);
uint8_t pcie_get_link_speed(void);
uint8_t pcie_get_link_speed_masked(void);
void pcie_set_byte_enables(uint8_t byte_en);
void pcie_set_byte_enables_0f(void);
uint8_t pcie_read_completion_data(void);
void pcie_write_status_complete(void);
void pcie_write_status_error(void);
void pcie_write_status_done(void);
uint8_t pcie_check_status_complete(void);
uint8_t pcie_check_status_error(void);

/* PCIe address and buffer setup */
void pcie_set_idata_params(void);
void pcie_clear_address_regs(void);
void pcie_clear_address_regs_full(void);
void pcie_setup_buffer_params(uint16_t addr);
void pcie_setup_buffer_params_ext(uint8_t idx);
void pcie_setup_buffer_from_config(void);
void pcie_clear_reg_at_offset(uint8_t offset);

/* PCIe transaction counters */
void pcie_inc_txn_counters(void);
void pcie_inc_txn_count(void);
uint8_t pcie_get_txn_count_hi(void);
uint8_t pcie_get_txn_count_with_mult(void);
uint8_t pcie_check_txn_count(void);
void pcie_store_txn_idx(uint8_t idx);
void pcie_store_r6_to_05a6(uint8_t val);

/* PCIe completion handling */
uint8_t pcie_wait_for_completion(void);
uint8_t pcie_poll_and_read_completion(void);

/* TLP (Transaction Layer Packet) operations */
uint8_t pcie_setup_memory_tlp(void);
void pcie_write_tlp_addr_low(uint8_t val);
void pcie_setup_config_tlp(void);
uint8_t pcie_tlp_handler_b104(void);
void pcie_tlp_handler_b28c(void);
uint8_t pcie_tlp_handler_b402(void);
uint8_t pcie_tlp_init_and_transfer(void);
void tlp_init_addr_buffer(void);
uint8_t tlp_write_flash_cmd(uint8_t cmd);

/* PCIe event and interrupt handlers */
void pcie_event_handler(void);
void pcie_tunnel_enable(void);
void pcie_tunnel_setup(void);
void pcie_adapter_config(void);
void pcie_interrupt_handler(void);
uint8_t pcie_queue_handler_a62d(void);
void pcie_set_interrupt_flag(void);

/* PCIe configuration table lookups */
__xdata uint8_t *pcie_config_table_lookup(void);
__xdata uint8_t *pcie_idata_table_lookup(void);
__xdata uint8_t *pcie_param_table_lookup(uint8_t idx);
__xdata uint8_t *pcie_lookup_r3_multiply(uint8_t idx);
__xdata uint8_t *pcie_lookup_r6_multiply(uint8_t idx);
__xdata uint8_t *pcie_offset_table_lookup(void);

/* PCIe data register operations */
void pcie_write_data_reg_with_val(uint8_t val, uint8_t r6, uint8_t r7);
void pcie_write_data_reg(uint8_t r4, uint8_t r5, uint8_t r6, uint8_t r7);
uint8_t pcie_calc_queue_idx(uint8_t val);

/* PCIe store operations */
void pcie_store_to_05b8(uint8_t idx, uint8_t val);
void pcie_read_and_store_idata(__xdata uint8_t *ptr);
void pcie_store_r7_to_05b7(uint8_t idx, uint8_t val);
void pcie_set_0a5b_flag(__xdata uint8_t *ptr, uint8_t val);
void pcie_inc_0a5b(void);
void pcie_lookup_and_store_idata(uint8_t idx, uint16_t base);
void pcie_write_config_and_trigger(__xdata uint8_t *ptr, uint8_t val);
uint8_t pcie_get_status_bit2(void);
void pcie_add_2_to_idata(uint8_t val);

/* PCIe address operations */
void pcie_addr_store_839c(uint8_t p1, uint8_t p2, uint8_t p3, uint8_t p4);
void pcie_addr_store_83b9(uint8_t p1, uint8_t p2, uint8_t p3, uint8_t p4);

/* PCIe state and error handlers */
void pcie_state_clear_ed02(void);
void pcie_handler_unused_eef9(void);
void pcie_nvme_event_handler(void);
void pcie_error_dispatch(void);
void pcie_event_bit5_handler(void);
void pcie_timer_bit4_handler(void);

/* PCIe initialization helpers */
uint8_t pcie_init_read_e8f9(void);
uint8_t pcie_init_write_e902(void);
void pcie_handler_e890(void);
void pcie_txn_setup_e775(void);
void pcie_channel_setup_e19e(void);
void pcie_dma_config_e330(void);
void pcie_channel_disable_e5fe(void);
void pcie_disable_and_trigger_e74e(void);
void pcie_wait_and_ack_e80a(void);
void pcie_trigger_cc11_e8ef(void);
void clear_pcie_status_bytes_e8cd(void);
uint8_t get_pcie_status_flags_e00c(void);

/* Flash DMA trigger (PCIe-initiated) */
void flash_dma_trigger_handler(void);

/* NVMe queue operations via PCIe */
void nvme_cmd_setup_b624(void);
void nvme_cmd_setup_b6cf(void);
void nvme_cmd_setup_b779(void);
void nvme_queue_b825(void);
void nvme_queue_b833(void);
void nvme_queue_b838(void);
void nvme_queue_b850(void);
void nvme_queue_b851(void);
void nvme_queue_submit(void);
void nvme_queue_poll(void);
void nvme_completion_poll(void);

#endif /* _PCIE_H_ */
