/*
 * cmd.h - Hardware Command Engine Driver
 *
 * Hardware command engine for the ASM2464PD USB4/Thunderbolt to NVMe bridge.
 * Abstracts the process of building NVMe commands and tracking execution,
 * providing atomic command issue/completion tracking.
 *
 * ARCHITECTURE
 *   Software → cmd_write_issue_bits() → Hardware Sequencer
 *                                              ↓
 *   Software ← cmd_wait_completion() ← Completion Status
 *
 * COMMAND FLOW
 *   1. Check busy status (cmd_check_busy)
 *   2. Configure command parameters
 *   3. Issue command (cmd_start_trigger)
 *   4. Wait for completion (cmd_wait_completion)
 *   5. Read result/status
 *
 * REGISTER MAP (0xE400-0xE43F)
 *   0xE402  STATUS_FLAGS   Bit 1: busy, Bit 2: error, Bit 3: additional
 *   0xE403  CONTROL        Command state (written from G_CMD_STATUS)
 *   0xE41C  BUSY_STATUS    Bit 0: command busy
 *   0xE420  TRIGGER        0x80 (mode2/3) or 0x40 (mode1) to start
 *   0xE422  PARAM/OPCODE   Command parameter byte
 *   0xE423  STATUS         Command status byte
 *   0xE424  ISSUE          Command issue register, bits written per mode
 *   0xE425  TAG            Command tag (typically 4)
 *   0xE426  LBA_0          LBA byte 0 (from G_CMD_LBA_1)
 *   0xE427  LBA_1          Computed from G_CMD_LBA_2
 *   0xE428  LBA_2          Computed from G_CMD_LBA_3
 *
 * COMMAND CONTROL REGISTERS
 *   0xCC88  CMD_ENGINE_STATUS  Command engine status/control
 *   0xCC89  CMD_STATE          Command state register
 *                              0x01: Read operation
 *                              0x02: Write operation
 *   0xCC8A  CMD_AUX            Command auxiliary register
 *   0xC801  INT_CTRL           Interrupt control (bit 4: command complete)
 *
 * GLOBAL VARIABLES (Command Work Area 0x07B0-0x07FF)
 *   0x07B7  G_CMD_SLOT_INDEX   Command slot index (3-bit, 0-7)
 *   0x07BD  G_CMD_OP_COUNTER   Operation counter
 *   0x07C3  G_CMD_STATE        Command state (3-bit)
 *   0x07C4  G_CMD_STATUS       Command status (0x02, 0x06, etc.)
 *   0x07CA  G_CMD_MODE         Command mode (1=mode1, 2=mode2, 3=mode3)
 *   0x07D3  G_CMD_PARAM_0      Parameter 0 (for opcode)
 *   0x07D4  G_CMD_PARAM_1      Parameter 1
 *   0x07DA  G_CMD_LBA_0        LBA byte 0 (low)
 *   0x07DB  G_CMD_LBA_1        LBA byte 1
 *   0x07DC  G_CMD_LBA_2        LBA byte 2
 *   0x07DD  G_CMD_LBA_3        LBA byte 3 (high)
 *
 * COMMAND EXECUTION FLOW
 *   1. Set up parameters in globals (G_CMD_LBA_*, G_CMD_MODE, etc.)
 *   2. Call cmd_setup_with_params() to configure registers
 *   3. Call cmd_wait_completion() to wait for command to complete
 *   4. Check return value for success/error
 *
 *   Internal flow:
 *   Set 0xE422 → Set 0xE423 → Set 0xE424/0xE425
 *   (param)      (status)     (issue/tag)
 *       │                         │
 *       v                         v
 *   Set LBA → Set trigger → Wait on 0xE41C bit 0
 *   0xE426-28   0xE420       and 0xE402 bit 1
 *
 * BUSY CHECK LOGIC (0xe09a function)
 *   1. Read 0xE402, check bit 1 (busy flag) → return busy (1)
 *   2. Read 0xE41C, check bit 0 → return busy (1)
 *   3. Read 0xE402, check bit 2 (error count) → return busy (1)
 *   4. Read 0xE402, check bit 3 → if not set return not busy (0)
 *   5. Otherwise return busy (1)
 *
 * LBA HANDLING
 *   The command engine includes helpers for LBA (Logical Block Address)
 *   manipulation used in SCSI-to-NVMe translation:
 *   - cmd_combine_lba_param(): Combine bytes into LBA
 *   - cmd_extract_bit5/bits67(): Extract command type bits
 *
 * SLOT MANAGEMENT
 *   Commands are tracked via slots that maintain state across
 *   asynchronous operations. Slot addresses are calculated
 *   dynamically based on current queue depth.
 */
#ifndef _CMD_H_
#define _CMD_H_

#include "../types.h"

/* Command engine control */
uint8_t cmd_check_busy(void);                   /* 0xe09a-0xe0c3 */
void cmd_start_trigger(void);                   /* 0x9605-0x960e */
void cmd_write_issue_bits(uint8_t param) __reentrant;           /* 0x960f-0x9616 */
void cmd_engine_clear(void);                    /* 0x9675-0x9683 */
uint8_t cmd_wait_completion(void);              /* 0x968f-0x969c */

/* Command setup */
void cmd_setup_read_write(void);                /* 0x965d-0x9663 */
void cmd_issue_tag_and_wait(uint8_t issue, uint8_t tag);        /* 0xe1c6-0xe1ed */
void cmd_setup_with_params(uint8_t issue_val, uint8_t tag_val); /* 0xb640-0xb68b */
void cmd_config_e40b(void);                     /* 0x95a8-0x95b5 */
void cmd_call_e120_setup(void);                 /* 0x9b31-0x9b5a */
void cmd_clear_cc9a_setup(void);                /* 0x955d-0x9565 */
void cmd_call_e73a_setup(void);                 /* 0x9566-0x9583 */
void cmd_config_e400_e420(void);                /* 0x9584-0x959f */
void cmd_setup_e424_e425(uint8_t issue);        /* 0x95a0-0x95b5 */

/* Command parameters */
uint8_t cmd_combine_lba_param(uint8_t val);     /* 0x95b6-0x95c8 */
uint8_t cmd_combine_lba_alt(uint8_t val);       /* 0x95c9-0x95d9 */
void cmd_set_op_counter(void);                  /* 0x95da-0x95ea */
uint16_t cmd_calc_slot_addr(void);              /* 0x95eb-0x95f8 */
uint16_t cmd_calc_dptr_offset(uint8_t r2, uint8_t r3, uint8_t r5);  /* 0x95f9-0x9604 */
uint8_t cmd_extract_bit5(uint8_t hi, uint8_t lo);               /* 0x9617-0x9620 */
uint8_t cmd_extract_bits67(uint8_t val);        /* 0x9621-0x962d */
uint8_t cmd_extract_bits67_write(uint8_t val);  /* 0x962e-0x9634 */
uint8_t cmd_read_indexed(uint8_t hi, uint8_t lo);               /* 0x9635-0x9646 */

/* Command state management */
void cmd_write_cc89_01(void);                   /* 0x9647-0x964e */
void cmd_write_cc89_02(void);                   /* 0x964f-0x9655 */
void cmd_clear_5_bytes(__xdata uint8_t *ptr);   /* 0x9656-0x965c */
void cmd_set_c801_bit4(void);                   /* 0x9664-0x966a */
void cmd_clear_cc88_cc8a(void);                 /* 0x966b-0x9674 */
uint8_t cmd_check_op_counter(void);             /* 0x9684-0x968e */
void cmd_config_e405_e421(uint8_t param);       /* 0x969d-0x96a5 */
uint8_t cmd_clear_bits(__xdata uint8_t *reg);   /* 0x96a6-0x96ad */
void cmd_setup_delay(void);                     /* 0x96ae-0x96b6 */
uint16_t cmd_set_op_counter_1(void);            /* 0x96b7-0x96be */
uint8_t cmd_wait_and_store_counter(uint8_t counter);            /* 0x96bf-0x96cc */
uint16_t cmd_set_dptr_inc2(uint8_t hi, uint8_t lo);             /* 0x96cd-0x96d3 */
uint8_t cmd_call_e73a_with_params(void);        /* 0x96d4-0x96e0 */
uint8_t cmd_read_dptr_offset1(uint8_t hi, uint8_t lo);          /* 0x96e1-0x96ed */
void cmd_update_slot_index(void);               /* 0x96ee-0x96f6 */
void cmd_set_flag_07de(void);                   /* 0x96f7-0x9702 */
void cmd_store_addr_hi(uint8_t lo, uint8_t hi_adj);             /* 0x9703-0x9712 */
uint16_t cmd_load_addr(void);                   /* 0x9713-0x971d */
uint8_t cmd_read_state_shift(void);             /* 0x971e-0x9728 */
uint8_t cmd_clear_trigger_bits(void);           /* 0x9729-0x972f */
void cmd_write_trigger_wait(uint8_t trigger_val);               /* 0x9730-0x9739 */
void cmd_set_trigger_bit6(void);                /* 0x973a-0x9740 */
void cmd_call_dd12_config(void);                /* 0xdd12-0xdd41 */

/* Endpoint configuration */
void cfg_init_ep_mode(void);                    /* 0x99f6-0x99ff */
void cfg_store_ep_config(uint8_t val);          /* 0x99d8-0x99df */
void cfg_inc_reg_value(__xdata uint8_t *reg);   /* 0x99d1-0x99d4 */
uint8_t cfg_get_b296_bit2(void);                /* 0x99eb-0x99f5 */
void cfg_set_ep_flag_1(void);                   /* 0x99c7-0x99cd */

#endif /* _CMD_H_ */
