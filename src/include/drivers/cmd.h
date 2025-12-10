/*
 * cmd.h - Hardware Command Engine Driver
 *
 * The command engine is a hardware accelerator that handles low-level
 * command sequencing and synchronization between the USB, PCIe, and
 * NVMe subsystems. It provides atomic command issue/completion tracking.
 *
 * COMMAND ENGINE ARCHITECTURE:
 *   Software → cmd_write_issue_bits() → Hardware Sequencer
 *                                              ↓
 *   Software ← cmd_wait_completion() ← Completion Status
 *
 * COMMAND FLOW:
 *   1. Check busy status (cmd_check_busy)
 *   2. Configure command parameters
 *   3. Issue command (cmd_start_trigger)
 *   4. Wait for completion (cmd_wait_completion)
 *   5. Read result/status
 *
 * KEY REGISTERS:
 *   0xCC88: Command engine status/control
 *   0xCC89: Command state register
 *     - 0x01: Read operation
 *     - 0x02: Write operation
 *   0xCC8A: Command auxiliary register
 *   0xC801: Interrupt control (bit 4: command complete)
 *
 * LBA HANDLING:
 *   The command engine includes helpers for LBA (Logical Block Address)
 *   manipulation used in SCSI-to-NVMe translation:
 *   - cmd_combine_lba_param(): Combine bytes into LBA
 *   - cmd_extract_bit5/bits67(): Extract command type bits
 *
 * SLOT MANAGEMENT:
 *   Commands are tracked via slots that maintain state across
 *   asynchronous operations. Slot addresses are calculated
 *   dynamically based on current queue depth.
 *
 * USAGE:
 *   1. cmd_engine_clear() - Reset command engine state
 *   2. cmd_setup_with_params() - Configure command
 *   3. cmd_start_trigger() - Execute command
 *   4. cmd_wait_completion() - Block until done
 */
#ifndef _CMD_H_
#define _CMD_H_

#include "../types.h"

/* Command engine control */
uint8_t cmd_check_busy(void);
void cmd_start_trigger(void);
void cmd_write_issue_bits(uint8_t param) __reentrant;
void cmd_engine_clear(void);
uint8_t cmd_wait_completion(void);

/* Command setup */
void cmd_setup_read_write(void);
void cmd_issue_tag_and_wait(uint8_t issue, uint8_t tag);
void cmd_setup_with_params(uint8_t issue_val, uint8_t tag_val);
void cmd_config_e40b(void);
void cmd_call_e120_setup(void);
void cmd_clear_cc9a_setup(void);
void cmd_call_e73a_setup(void);
void cmd_config_e400_e420(void);
void cmd_setup_e424_e425(uint8_t issue);

/* Command parameters */
uint8_t cmd_combine_lba_param(uint8_t val);
uint8_t cmd_combine_lba_alt(uint8_t val);
void cmd_set_op_counter(void);
uint16_t cmd_calc_slot_addr(void);
uint16_t cmd_calc_dptr_offset(uint8_t r2, uint8_t r3, uint8_t r5);
uint8_t cmd_extract_bit5(uint8_t hi, uint8_t lo);
uint8_t cmd_extract_bits67(uint8_t val);
uint8_t cmd_extract_bits67_write(uint8_t val);
uint8_t cmd_read_indexed(uint8_t hi, uint8_t lo);

/* Command state management */
void cmd_write_cc89_01(void);
void cmd_write_cc89_02(void);
void cmd_clear_5_bytes(__xdata uint8_t *ptr);
void cmd_set_c801_bit4(void);
void cmd_clear_cc88_cc8a(void);
uint8_t cmd_check_op_counter(void);
void cmd_config_e405_e421(uint8_t param);
uint8_t cmd_clear_bits(__xdata uint8_t *reg);
void cmd_setup_delay(void);
uint16_t cmd_set_op_counter_1(void);
uint8_t cmd_wait_and_store_counter(uint8_t counter);
uint16_t cmd_set_dptr_inc2(uint8_t hi, uint8_t lo);
uint8_t cmd_call_e73a_with_params(void);
uint8_t cmd_read_dptr_offset1(uint8_t hi, uint8_t lo);
void cmd_update_slot_index(void);
void cmd_set_flag_07de(void);
void cmd_store_addr_hi(uint8_t lo, uint8_t hi_adj);
uint16_t cmd_load_addr(void);
uint8_t cmd_read_state_shift(void);
uint8_t cmd_clear_trigger_bits(void);
void cmd_write_trigger_wait(uint8_t trigger_val);
void cmd_set_trigger_bit6(void);
void cmd_call_dd12_config(void);

/* Endpoint configuration */
void cfg_init_ep_mode(void);
void cfg_store_ep_config(uint8_t val);
void cfg_inc_reg_value(__xdata uint8_t *reg);
uint8_t cfg_get_b296_bit2(void);
void cfg_set_ep_flag_1(void);

#endif /* _CMD_H_ */
