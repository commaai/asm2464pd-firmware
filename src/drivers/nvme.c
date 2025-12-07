/*
 * ASM2464PD Firmware - NVMe Driver
 *
 * NVMe controller interface for USB4/Thunderbolt to NVMe bridge.
 * Handles NVMe command submission, completion, and queue management.
 *
 *===========================================================================
 * NVME CONTROLLER ARCHITECTURE
 *===========================================================================
 *
 * The ASM2464PD bridges USB4/Thunderbolt/USB to PCIe NVMe SSDs.
 * This driver manages the NVMe command/completion queues and data transfers.
 *
 * Block Diagram:
 * ┌───────────────────────────────────────────────────────────────────────┐
 * │                        NVMe SUBSYSTEM                                │
 * ├───────────────────────────────────────────────────────────────────────┤
 * │                                                                       │
 * │  USB/PCIe ──> SCSI Cmd ──> NVMe Cmd Builder ──> Submission Queue     │
 * │      │                          │                     │               │
 * │      │                          v                     v               │
 * │      │                    ┌──────────┐          ┌──────────┐          │
 * │      │                    │ NVMe Regs│          │ PCIe DMA │          │
 * │      │                    │ 0xC400+  │          │ Engine   │          │
 * │      │                    └──────────┘          └────┬─────┘          │
 * │      │                                               │               │
 * │      │                                               v               │
 * │      <───── SCSI Status <── NVMe Completion <── Completion Queue     │
 * │                                                                       │
 * └───────────────────────────────────────────────────────────────────────┘
 *
 * Register Map (0xC400-0xC5FF):
 * ┌──────────┬───────────────────────────────────────────────────────────┐
 * │ Address  │ Description                                               │
 * ├──────────┼───────────────────────────────────────────────────────────┤
 * │ 0xC400   │ NVME_CTRL - Control register                              │
 * │ 0xC401   │ NVME_STATUS - Status register                             │
 * │ 0xC412   │ NVME_CTRL_STATUS - Control/status combined                │
 * │ 0xC413   │ NVME_CONFIG - Configuration                               │
 * │ 0xC414   │ NVME_DATA_CTRL - Data transfer control                    │
 * │ 0xC415   │ NVME_DEV_STATUS - Device presence/ready status            │
 * │ 0xC420   │ NVME_CMD - Command register                               │
 * │ 0xC421   │ NVME_CMD_OPCODE - NVMe opcode                              │
 * │ 0xC422-24│ NVME_LBA_0/1/2 - LBA bytes 0-2                            │
 * │ 0xC425-26│ NVME_COUNT - Transfer count                               │
 * │ 0xC427   │ NVME_ERROR - Error code                                   │
 * │ 0xC428   │ NVME_QUEUE_CFG - Queue configuration                      │
 * │ 0xC429   │ NVME_CMD_PARAM - Command parameters                       │
 * │ 0xC42A   │ NVME_DOORBELL - Queue doorbell                            │
 * │ 0xC440-45│ Queue head/tail pointers                                  │
 * │ 0xC446   │ NVME_LBA_3 - LBA byte 3                                   │
 * │ 0xC462   │ DMA_ENTRY - DMA entry point                               │
 * │ 0xC470-7F│ Command queue directory                                   │
 * └──────────┴───────────────────────────────────────────────────────────┘
 *
 * NVMe Event Registers (0xEC00-0xEC0F):
 * ┌──────────┬───────────────────────────────────────────────────────────┐
 * │ 0xEC04   │ NVME_EVENT_ACK - Event acknowledge                        │
 * │ 0xEC06   │ NVME_EVENT_STATUS - Event status                          │
 * └──────────┴───────────────────────────────────────────────────────────┘
 *
 * Queue Management:
 * - Submission Queue (SQ): Commands sent to NVMe device
 * - Completion Queue (CQ): Status returned from NVMe device
 * - Circular buffer with head/tail pointers
 * - Phase bit for completion tracking
 * - Maximum 32 outstanding commands (5-bit counter)
 *
 * SCSI DMA Registers (0xCE40-0xCEFF):
 * - Used for NVMe data transfers
 * - 0xCE88-CE89: SCSI DMA control/status
 * - 0xCEB0: Transfer status
 *
 *===========================================================================
 * IMPLEMENTATION STATUS
 *===========================================================================
 * nvme_set_usb_mode_bit      [DONE] 0x1bde-0x1be7 - Set USB mode bit
 * nvme_get_config_offset     [DONE] 0x1be8-0x1bf5 - Get config offset
 * nvme_calc_buffer_offset    [DONE] 0x1bf6-0x1c0e - Calculate buffer offset
 * nvme_load_transfer_data    [DONE] 0x1bcb-0x1bd4 - Load transfer data
 * nvme_calc_idata_offset     [DONE] 0x1c0f-0x1c1a - Calculate IDATA offset
 * nvme_check_scsi_ctrl       [DONE] 0x1c22-0x1c29 - Check SCSI control
 * nvme_get_cmd_param_upper   [DONE] 0x1c77-0x1c7d - Get cmd param upper bits
 * nvme_subtract_idata_16     [DONE] 0x1c6d-0x1c76 - Subtract 16-bit value
 * nvme_calc_addr_01xx        [DONE] 0x1c88-0x1c8f - Calculate 0x01XX addr
 * nvme_inc_circular_counter  [DONE] 0x1cae-0x1cb6 - Increment circular counter
 * nvme_calc_addr_012b        [DONE] 0x1cb7-0x1cc0 - Calculate 0x012B+ addr
 * nvme_set_ep_queue_ctrl_84  [DONE] 0x1cc1-0x1cc7 - Set EP queue ctrl
 * nvme_get_dev_status_upper  [DONE] 0x1c56-0x1c5c - Get device status upper
 * nvme_get_data_ctrl_upper   [DONE] 0x1d24-0x1d2a - Get data ctrl upper bits
 * nvme_clear_status_bit1     [DONE] 0x1cd4-0x1cdb - Clear status bit 1
 * nvme_set_data_ctrl_bit7    [DONE] 0x1d2b-0x1d31 - Set data ctrl bit 7
 * nvme_store_idata_16        [DONE] 0x1d32-0x1d38 - Store 16-bit to IDATA
 * nvme_calc_addr_04b7        [DONE] 0x1ce4-0x1cef - Calculate 0x04B7+ addr
 * nvme_add_to_global_053a    [DONE] 0x1cdc-0x1ce3 - Add 0x20 to global 0x053A
 * nvme_check_completion      [DONE] 0x3244-0x3248 - Set completion bit
 * nvme_initialize            [DONE] 0x3249-0x3256 - Initialize NVMe state
 * nvme_ring_doorbell         [DONE] 0x3247      - Ring doorbell
 * nvme_init_step             [DONE] 0x3267-0x3271 - Set EP config for NVMe
 * nvme_read_status           [DONE] 0x3272-0x3278 - Set status bit 4
 * nvme_set_int_aux_bit1      [DONE] 0x3280-0x3289 - Set interrupt aux bit
 * nvme_get_link_status_masked[DONE] 0x328a-0x3290 - Get link status masked
 * nvme_set_ep_ctrl_bits      [DONE] 0x320c-0x3218 - Set EP ctrl bits 1&2
 * nvme_process_cmd           [DONE] 0x31a0      - Process NVMe command
 * nvme_io_request            [DONE] 0x31a5      - I/O memory copy
 * nvme_build_cmd             [DONE] 0x31da-0x31e0 - Build command result
 * nvme_submit_cmd            [DONE] 0x31fb      - Submit command
 * nvme_io_handler            [DONE] 0x32a4-0x3418 - Main I/O handler
 * usb_validate_descriptor    [DONE] 0x31fb-0x320b - Validate descriptor
 * nvme_get_dma_status_masked [DONE] 0x3298-0x329e - Get DMA status masked
 * nvme_call_and_signal       [DONE] 0x3219-0x3222 - Call and signal
 * nvme_set_usb_ep_ctrl_bit2  [DONE] 0x3212-0x3218 - Set EP ctrl bit 2
 * nvme_wait_for_ready        [DONE] 0x329f-0x32a3 - Wait for ready via bit poll
 * nvme_init_registers        [DONE] 0x3419-0x3576 - Initialize NVMe registers
 * nvme_func_1b07             [DONE] 0x1b07-0x1b0a - Get IDATA calculated address
 * nvme_func_1b0b             [DONE] 0x1b0b-0x1b13 - Get IDATA value from param
 * nvme_check_threshold_r5    [DONE] 0x1b2b-0x1b2c - Check threshold R5
 * nvme_check_threshold_0x3e  [DONE] 0x1b38-0x1b3d - Check threshold 0x3E
 * usb_func_1b47              [DONE] 0x1b47-0x1b56 - Update NVMe status
 * usb_check_status           [DONE] 0x1b4d-0x1b5c - Store param set status
 * usb_configure              [DONE] 0x1b58-0x1b5f - Set bit 1 on register
 * nvme_get_idata_009f        [DONE] 0x1b84-0x1b8c - Get IDATA 0x9F offset
 * usb_get_ep_config_indexed  [DONE] 0x1b91-0x1b9c - Get EP config indexed
 * usb_read_buf_addr_pair     [DONE] 0x1ba2-0x1ba9 - Read buffer address pair
 * usb_data_handler           [DONE] 0x1bde-0x1be7 - Set bit 0 on register
 * nvme_func_1c2a             [DONE] 0x1c2a-0x1c2f - Get table entry from 0x5CAD
 * nvme_func_1c43             [DONE] 0x1c43-0x1c49 - Store masked to 0x01B4
 * nvme_func_1c55             [DONE] 0x1c55-0x1c5c - Get device status upper
 * nvme_func_1c7e             [DONE] 0x1c7e-0x1c87 - Load IDATA/read reg dword
 * nvme_func_1c9f             [DONE] 0x1c9f-0x1cad - Process cmd check result
 * nvme_get_addr_012b         [DONE] 0x1cb9-0x1cc0 - Get address 0x012B
 * nvme_func_1cf0             [DONE] 0x1cf0-0x1d23 - Clear buffer/state regs
 *
 * Total: 59 functions implemented
 *===========================================================================
 *
 * NOTE: Core dispatch functions (jump_bank_0, jump_bank_1)
 * are defined in main.c as they are part of the core dispatch mechanism.
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

/*
 * nvme_set_usb_mode_bit - Set USB mode bit 0 of register 0x9006
 * Address: 0x1bde-0x1be7 (10 bytes)
 *
 * Reads 0x9006, clears bit 0, sets bit 0, writes back.
 *
 * Original disassembly:
 *   1bde: mov dptr, #0x9006
 *   1be1: movx a, @dptr
 *   1be2: anl a, #0xfe        ; clear bit 0
 *   1be4: orl a, #0x01        ; set bit 0
 *   1be6: movx @dptr, a
 *   1be7: ret
 */
void nvme_set_usb_mode_bit(void)
{
    uint8_t val;

    val = REG_USB_EP0_CONFIG;
    val = (val & 0xFE) | 0x01;
    REG_USB_EP0_CONFIG = val;
}

/*
 * nvme_get_config_offset - Get configuration offset address
 * Address: 0x1be8-0x1bf5 (14 bytes)
 *
 * Reads from 0x0464, adds 0x56 to form address in 0x04XX region,
 * and returns that address.
 *
 * Original disassembly:
 *   1be8: mov dptr, #0x0464
 *   1beb: movx a, @dptr       ; A = XDATA[0x0464]
 *   1bec: add a, #0x56        ; A = A + 0x56
 *   1bee: mov 0x82, a         ; DPL = A
 *   1bf0: clr a
 *   1bf1: addc a, #0x04       ; DPH = 0x04 + carry
 *   1bf3: mov 0x83, a
 *   1bf5: ret                 ; returns with DPTR = 0x04XX
 */
__xdata uint8_t *nvme_get_config_offset(void)
{
    uint8_t val = G_SYS_STATUS_PRIMARY;
    uint16_t addr = 0x0400 + val + 0x56;
    return (__xdata uint8_t *)addr;
}

/*
 * nvme_calc_buffer_offset - Calculate buffer offset with multiplier
 * Address: 0x1bf6-0x1c0e (25 bytes)
 *
 * Multiplies input by 0x40, adds to values from 0x021A-0x021B,
 * and stores result to 0x0568-0x0569.
 *
 * Original disassembly:
 *   1bf6: mov 0xf0, #0x40     ; B = 0x40
 *   1bf9: mul ab              ; A*B, result in B:A
 *   1bfa: mov r7, a           ; R7 = low byte
 *   1bfb: mov dptr, #0x021b
 *   1bfe: movx a, @dptr       ; A = XDATA[0x021B]
 *   1bff: add a, r7           ; A = A + R7
 *   1c00: mov r6, a           ; R6 = low result
 *   1c01: mov dptr, #0x021a
 *   1c04: movx a, @dptr       ; A = XDATA[0x021A]
 *   1c05: addc a, 0xf0        ; A = A + B + carry
 *   1c07: mov dptr, #0x0568
 *   1c0a: movx @dptr, a       ; XDATA[0x0568] = high byte
 *   1c0b: inc dptr
 *   1c0c: xch a, r6
 *   1c0d: movx @dptr, a       ; XDATA[0x0569] = low byte
 *   1c0e: ret
 */
void nvme_calc_buffer_offset(uint8_t index)
{
    uint16_t offset;
    uint16_t base;
    uint16_t result;

    /* Calculate offset = index * 0x40 */
    offset = (uint16_t)index * 0x40;

    /* Read base address (big-endian) */
    base = G_BUF_BASE_HI;
    base = (base << 8) | G_BUF_BASE_LO;

    /* Calculate result = base + offset */
    result = base + offset;

    /* Store result (big-endian) */
    G_BUF_OFFSET_HI = (uint8_t)(result >> 8);
    G_BUF_OFFSET_LO = (uint8_t)(result & 0xFF);
}

/*
 * nvme_load_transfer_data - Load transfer data from IDATA
 * Address: 0x1bcb-0x1bd4 (10 bytes)
 *
 * Loads 32-bit value from IDATA[0x6B] and stores to IDATA[0x6F].
 *
 * Original disassembly:
 *   1bcb: mov r0, #0x6b
 *   1bcd: lcall 0x0d78        ; idata_load_dword
 *   1bd0: mov r0, #0x6f
 *   1bd2: ljmp 0x0db9         ; idata_store_dword
 */
void nvme_load_transfer_data(void)
{
    uint32_t val;

    /* Load from IDATA[0x6B] */
    val = ((__idata uint8_t *)0x6B)[0];
    val |= ((uint32_t)((__idata uint8_t *)0x6B)[1]) << 8;
    val |= ((uint32_t)((__idata uint8_t *)0x6B)[2]) << 16;
    val |= ((uint32_t)((__idata uint8_t *)0x6B)[3]) << 24;

    /* Store to IDATA[0x6F] */
    ((__idata uint8_t *)0x6F)[0] = (uint8_t)(val & 0xFF);
    ((__idata uint8_t *)0x6F)[1] = (uint8_t)((val >> 8) & 0xFF);
    ((__idata uint8_t *)0x6F)[2] = (uint8_t)((val >> 16) & 0xFF);
    ((__idata uint8_t *)0x6F)[3] = (uint8_t)((val >> 24) & 0xFF);
}

/*
 * nvme_calc_idata_offset - Calculate address from IDATA offset
 * Address: 0x1c0f-0x1c1a (12 bytes)
 *
 * Returns pointer to 0x000C + IDATA[0x3C].
 *
 * Original disassembly:
 *   1c0f: mov a, #0x0c
 *   1c11: add a, 0x3c         ; A = 0x0C + IDATA[0x3C]
 *   1c13: mov 0x82, a         ; DPL
 *   1c15: clr a
 *   1c16: addc a, #0x00       ; DPH = carry
 *   1c18: mov 0x83, a
 *   1c1a: ret
 */
__xdata uint8_t *nvme_calc_idata_offset(void)
{
    uint8_t offset = *(__idata uint8_t *)0x3C;
    return (__xdata uint8_t *)(0x000C + offset);
}

/*
 * nvme_check_scsi_ctrl - Check SCSI control status
 * Address: 0x1c22-0x1c29 (8 bytes, after store)
 *
 * Reads SCSI control from 0x0171 and checks if non-zero.
 * Returns with carry set if value is zero.
 *
 * Original disassembly:
 *   1c22: mov dptr, #0x0171
 *   1c25: movx a, @dptr       ; A = G_SCSI_CTRL
 *   1c26: setb c
 *   1c27: subb a, #0x00       ; compare with 0
 *   1c29: ret
 */
uint8_t nvme_check_scsi_ctrl(void)
{
    return (G_SCSI_CTRL != 0) ? 1 : 0;
}

/*
 * nvme_get_cmd_param_upper - Get upper 3 bits of NVMe command param
 * Address: 0x1c77-0x1c7d (7 bytes)
 *
 * Reads NVMe command parameter and masks to upper 3 bits.
 *
 * Original disassembly:
 *   1c77: mov dptr, #0xc429
 *   1c7a: movx a, @dptr       ; A = REG_NVME_CMD_PARAM
 *   1c7b: anl a, #0xe0        ; mask bits 7-5
 *   1c7d: ret
 */
uint8_t nvme_get_cmd_param_upper(void)
{
    return REG_NVME_CMD_PARAM & 0xE0;
}

/*
 * nvme_subtract_idata_16 - Subtract 16-bit value from IDATA[0x16:0x17]
 * Address: 0x1c6d-0x1c76 (10 bytes)
 *
 * Subtracts R6:R7 from the 16-bit value at IDATA[0x16:0x17].
 *
 * Original disassembly:
 *   1c6d: mov r0, #0x17
 *   1c6f: mov a, @r0          ; A = IDATA[0x17]
 *   1c70: subb a, r7          ; A = A - R7 - C
 *   1c71: mov @r0, a          ; IDATA[0x17] = A
 *   1c72: dec r0
 *   1c73: mov a, @r0          ; A = IDATA[0x16]
 *   1c74: subb a, r6          ; A = A - R6 - C
 *   1c75: mov @r0, a          ; IDATA[0x16] = A
 *   1c76: ret
 */
void nvme_subtract_idata_16(uint8_t hi, uint8_t lo)
{
    uint16_t val = ((*(__idata uint8_t *)0x16) << 8) | (*(__idata uint8_t *)0x17);
    uint16_t sub = ((uint16_t)hi << 8) | lo;
    val -= sub;
    *(__idata uint8_t *)0x16 = (uint8_t)(val >> 8);
    *(__idata uint8_t *)0x17 = (uint8_t)(val & 0xFF);
}

/*
 * nvme_calc_addr_01xx - Calculate address in 0x01XX region
 * Address: 0x1c88-0x1c8f (8 bytes)
 *
 * Returns pointer to 0x0100 + input value.
 *
 * Original disassembly:
 *   1c88: mov 0x82, a         ; DPL = A
 *   1c8a: clr a
 *   1c8b: addc a, #0x01       ; DPH = 0x01 + carry
 *   1c8d: mov 0x83, a
 *   1c8f: ret
 */
__xdata uint8_t *nvme_calc_addr_01xx(uint8_t offset)
{
    return (__xdata uint8_t *)(0x0100 + offset);
}

/*
 * nvme_inc_circular_counter - Increment circular counter at 0x0B00
 * Address: 0x1cae-0x1cb6 (9 bytes)
 *
 * Reads counter, increments, masks to 5 bits (0-31 range), writes back.
 *
 * Original disassembly:
 *   1cae: mov dptr, #0x0b00
 *   1cb1: movx a, @dptr       ; read counter
 *   1cb2: inc a               ; increment
 *   1cb3: anl a, #0x1f        ; mask to 5 bits
 *   1cb5: movx @dptr, a       ; write back
 *   1cb6: ret
 */
void nvme_inc_circular_counter(void)
{
    uint8_t val = G_USB_PARAM_0B00;
    val = (val + 1) & 0x1F;
    G_USB_PARAM_0B00 = val;
}

/*
 * nvme_calc_addr_012b - Calculate address in 0x012B+ region
 * Address: 0x1cb7-0x1cc0 (10 bytes)
 *
 * Returns pointer to 0x012B + input value.
 *
 * Original disassembly:
 *   1cb7: add a, #0x2b
 *   1cb9: mov 0x82, a         ; DPL
 *   1cbb: clr a
 *   1cbc: addc a, #0x01       ; DPH = 0x01 + carry
 *   1cbe: mov 0x83, a
 *   1cc0: ret
 */
__xdata uint8_t *nvme_calc_addr_012b(uint8_t offset)
{
    return (__xdata uint8_t *)(0x012B + offset);
}

/*
 * nvme_set_ep_queue_ctrl_84 - Set endpoint queue control to 0x84
 * Address: 0x1cc1-0x1cc7 (7 bytes)
 *
 * Sets G_EP_QUEUE_CTRL to 0x84 (busy flag set).
 *
 * Original disassembly:
 *   1cc1: mov dptr, #0x0564
 *   1cc4: mov a, #0x84
 *   1cc6: movx @dptr, a       ; G_EP_QUEUE_CTRL = 0x84
 *   1cc7: ret
 */
void nvme_set_ep_queue_ctrl_84(void)
{
    G_EP_QUEUE_CTRL = 0x84;
}

/*
 * nvme_get_dev_status_upper - Get upper 2 bits of device status
 * Address: 0x1c56-0x1c5c (7 bytes)
 *
 * Reads NVMe device status register and masks to upper 2 bits.
 * These bits indicate device presence and ready state.
 *
 * Original disassembly:
 *   1c56: mov dptr, #0xc415   ; REG_NVME_DEV_STATUS
 *   1c59: movx a, @dptr       ; read status
 *   1c5a: anl a, #0xc0        ; mask bits 7-6
 *   1c5c: ret
 */
uint8_t nvme_get_dev_status_upper(void)
{
    return REG_NVME_DEV_STATUS & 0xC0;
}

/*
 * nvme_get_data_ctrl_upper - Get upper 2 bits of data control
 * Address: 0x1d24-0x1d2a (7 bytes)
 *
 * Reads NVMe data control register and masks to upper 2 bits.
 *
 * Original disassembly:
 *   1d24: mov dptr, #0xc414   ; REG_NVME_DATA_CTRL
 *   1d27: movx a, @dptr       ; read control
 *   1d28: anl a, #0xc0        ; mask bits 7-6
 *   1d2a: ret
 */
uint8_t nvme_get_data_ctrl_upper(void)
{
    return REG_NVME_DATA_CTRL & 0xC0;
}

/*
 * nvme_clear_status_bit1 - Clear bit 1 of NVMe status register
 * Address: 0x1cd4-0x1cdb (8 bytes)
 *
 * Reads status, clears bit 1, writes back.
 * Bit 1 is typically an error/interrupt flag.
 *
 * Original disassembly:
 *   1cd4: mov dptr, #0xc401   ; REG_NVME_STATUS
 *   1cd7: movx a, @dptr       ; read status
 *   1cd8: anl a, #0xfd        ; clear bit 1
 *   1cda: movx @dptr, a       ; write back
 *   1cdb: ret
 */
void nvme_clear_status_bit1(void)
{
    uint8_t val = REG_NVME_STATUS;
    val &= 0xFD;
    REG_NVME_STATUS = val;
}

/*
 * nvme_set_data_ctrl_bit7 - Set bit 7 of data control register
 * Address: 0x1d2b-0x1d31 (7 bytes)
 *
 * Reads from DPTR (caller sets it), clears bit 7, sets bit 7, writes back.
 * This is called after nvme_get_data_ctrl_upper with DPTR still pointing
 * to 0xC414.
 *
 * Original disassembly:
 *   1d2b: movx a, @dptr       ; read current value
 *   1d2c: anl a, #0x7f        ; clear bit 7
 *   1d2e: orl a, #0x80        ; set bit 7
 *   1d30: movx @dptr, a       ; write back
 *   1d31: ret
 */
void nvme_set_data_ctrl_bit7(void)
{
    uint8_t val = REG_NVME_DATA_CTRL;
    val = (val & 0x7F) | 0x80;
    REG_NVME_DATA_CTRL = val;
}

/*
 * nvme_store_idata_16 - Store 16-bit value to IDATA[0x16:0x17]
 * Address: 0x1d32-0x1d38 (7 bytes)
 *
 * Stores R6:R7 (hi:lo) to IDATA[0x16:0x17].
 *
 * Original disassembly:
 *   1d32: mov r1, #0x17
 *   1d34: mov @r1, a          ; store low byte (A = R7)
 *   1d35: mov a, r6           ; get high byte
 *   1d36: dec r1              ; point to 0x16
 *   1d37: mov @r1, a          ; store high byte
 *   1d38: ret
 */
void nvme_store_idata_16(uint8_t hi, uint8_t lo)
{
    *(__idata uint8_t *)0x17 = lo;
    *(__idata uint8_t *)0x16 = hi;
}

/*
 * nvme_calc_addr_04b7 - Calculate address in 0x04B7+ region
 * Address: 0x1ce4-0x1cef (12 bytes)
 *
 * Returns pointer to 0x04B7 + IDATA[0x23].
 *
 * Original disassembly:
 *   1ce4: mov a, #0xb7
 *   1ce6: add a, 0x23         ; A = 0xB7 + IDATA[0x23]
 *   1ce8: mov 0x82, a         ; DPL
 *   1cea: clr a
 *   1ceb: addc a, #0x04       ; DPH = 0x04 + carry
 *   1ced: mov 0x83, a
 *   1cef: ret
 */
__xdata uint8_t *nvme_calc_addr_04b7(void)
{
    uint8_t offset = *(__idata uint8_t *)0x23;
    uint16_t addr = 0x04B7 + offset;
    return (__xdata uint8_t *)addr;
}

/*
 * nvme_add_to_global_053a - Add 0x20 to value at 0x053A
 * Address: 0x1cdc-0x1ce3 (8 bytes)
 *
 * Reads value from 0x053A, adds 0x20, writes back.
 *
 * Original disassembly:
 *   1cdc: mov dptr, #0x053a
 *   1cdf: movx a, @dptr       ; read value
 *   1ce0: add a, #0x20        ; add 0x20
 *   1ce2: movx @dptr, a       ; write back
 *   1ce3: ret
 */
void nvme_add_to_global_053a(void)
{
    uint8_t val = G_NVME_PARAM_053A;
    val += 0x20;
    G_NVME_PARAM_053A = val;
}

/*
 * nvme_check_completion - Set completion bit on register
 * Address: 0x3244-0x3248 (5 bytes)
 *
 * Sets bit 0 of the register pointed to by the parameter.
 * Used to signal completion status.
 *
 * Original disassembly:
 *   3244: movx a, @dptr       ; read current value
 *   3245: anl a, #0xfe        ; clear bit 0
 *   3247: orl a, #0x01        ; set bit 0
 *   3247: movx @dptr, a       ; write back
 *   3248: ret
 */
void nvme_check_completion(__xdata uint8_t *ptr)
{
    *ptr = (*ptr & 0xFE) | 0x01;
}

/*
 * nvme_initialize - Initialize NVMe state
 * Address: 0x3249-0x3256 (14 bytes)
 *
 * Sets the target register to 1 and clears bit 0 of 0xC509.
 * Called to initialize NVMe state after command completion.
 *
 * Original disassembly:
 *   3249: mov a, #0x01
 *   324b: movx @dptr, a       ; *param = 1
 *   324c: mov dptr, #0xc509
 *   324f: movx a, @dptr       ; read 0xC509
 *   3250: anl a, #0xfe        ; clear bit 0
 *   3252: movx @dptr, a       ; write back
 *   3253: mov dptr, #0x0af5   ; setup for return
 *   3256: ret
 */
void nvme_initialize(__xdata uint8_t *ptr)
{
    uint8_t val;

    /* Set target to 1 */
    *ptr = 1;

    /* Clear bit 0 of 0xC509 */
    val = REG_NVME_LINK_STATUS;
    val &= 0xFE;
    REG_NVME_LINK_STATUS = val;
}

/*
 * nvme_ring_doorbell - Ring NVMe doorbell register
 * Address: 0x3247 (1 byte - just a write)
 *
 * Writes value to doorbell register at specified offset.
 * Used to notify NVMe device of new commands in queue.
 *
 * Original disassembly:
 *   3247: movx @dptr, a       ; write to doorbell
 *   3248: ret
 */
void nvme_ring_doorbell(__xdata uint8_t *doorbell)
{
    *doorbell = 0x00;  /* Ring by writing any value */
}

/*
 * nvme_read_and_sum_index - Read value and calculate indexed address
 * Address: 0x1c3a-0x1c49 (16 bytes)
 *
 * Reads from DPTR, adds to value from 0x0216, masks to 5 bits,
 * then writes to 0x01B4.
 *
 * Original disassembly:
 *   1c3a: movx a, @dptr        ; Read from caller's DPTR
 *   1c3b: mov r7, a
 *   1c3c: mov dptr, #0x0216
 *   1c3f: movx a, @dptr        ; Read [0x0216]
 *   1c40: mov r6, a
 *   1c41: mov a, r7
 *   1c42: add a, r6            ; A = R7 + R6
 *   1c43: anl a, #0x1f         ; Mask to 5 bits
 *   1c45: mov dptr, #0x01b4
 *   1c48: movx @dptr, a        ; Write to [0x01B4]
 *   1c49: ret
 */
void nvme_read_and_sum_index(__xdata uint8_t *ptr)
{
    uint8_t val1, val2, result;

    val1 = *ptr;
    val2 = G_DMA_WORK_0216;
    result = (val1 + val2) & 0x1F;
    G_USB_WORK_01B4 = result;
}

/*
 * nvme_write_params_to_dma - Write value to DMA mode and param registers
 * Address: 0x1c4a-0x1c54 (11 bytes)
 *
 * Writes A to 0x0203, 0x020D, and 0x020E.
 *
 * Original disassembly:
 *   1c4a: mov dptr, #0x0203
 *   1c4d: movx @dptr, a        ; [0x0203] = A
 *   1c4e: mov dptr, #0x020d
 *   1c51: movx @dptr, a        ; [0x020D] = A
 *   1c52: inc dptr
 *   1c53: movx @dptr, a        ; [0x020E] = A
 *   1c54: ret
 */
void nvme_write_params_to_dma(uint8_t val)
{
    G_DMA_MODE_SELECT = val;
    G_DMA_PARAM1 = val;
    G_DMA_PARAM2 = val;
}

/*
 * nvme_calc_addr_from_dptr - Calculate address from DPTR value + 0xA8
 * Address: 0x1c5d-0x1c6c (16 bytes)
 *
 * Reads from DPTR, adds 0xA8, forms address in 0x05XX region,
 * reads that address and stores to 0x05A6.
 *
 * Original disassembly:
 *   1c5d: movx a, @dptr        ; Read from caller's DPTR
 *   1c5e: add a, #0xa8         ; A = A + 0xA8
 *   1c60: mov 0x82, a          ; DPL
 *   1c62: clr a
 *   1c63: addc a, #0x05        ; DPH = 0x05 + carry
 *   1c65: mov 0x83, a
 *   1c67: movx a, @dptr        ; Read from 0x05XX
 *   1c68: mov dptr, #0x05a6
 *   1c6b: movx @dptr, a        ; Store to [0x05A6]
 *   1c6c: ret
 */
void nvme_calc_addr_from_dptr(__xdata uint8_t *ptr)
{
    uint8_t val = *ptr;
    uint16_t addr = 0x0500 + val + 0xA8;
    uint8_t result = *(__xdata uint8_t *)addr;
    G_PCIE_TXN_COUNT_LO = result;
}

/*
 * nvme_copy_idata_to_dptr - Copy 2 bytes from IDATA[0x16-0x17] to DPTR
 * Address: 0x1cc8-0x1cd3 (12 bytes)
 *
 * Reads IDATA[0x16:0x17] and writes to consecutive DPTR addresses.
 *
 * Original disassembly:
 *   1cc8: mov r0, #0x16
 *   1cca: mov a, @r0           ; A = IDATA[0x16]
 *   1ccb: mov r7, a
 *   1ccc: inc r0
 *   1ccd: mov a, @r0           ; A = IDATA[0x17]
 *   1cce: xch a, r7            ; Swap
 *   1ccf: movx @dptr, a        ; Write IDATA[0x16] to DPTR
 *   1cd0: inc dptr
 *   1cd1: mov a, r7
 *   1cd2: movx @dptr, a        ; Write IDATA[0x17] to DPTR+1
 *   1cd3: ret
 */
void nvme_copy_idata_to_dptr(__xdata uint8_t *ptr)
{
    uint8_t hi = *(__idata uint8_t *)0x16;
    uint8_t lo = *(__idata uint8_t *)0x17;
    ptr[0] = hi;
    ptr[1] = lo;
}

/*
 * nvme_get_pcie_count_config - Read and calculate PCIe transaction config
 * Address: 0x1c90-0x1c9e (15 bytes)
 *
 * Reads 0x05A6, multiplies by 0x22, adds to 0x05B4 index, reads result.
 *
 * Original disassembly:
 *   1c90: mov dptr, #0x05a6
 *   1c93: movx a, @dptr        ; A = [0x05A6]
 *   1c94: mov 0xf0, #0x22      ; B = 0x22
 *   1c97: mov dptr, #0x05b4
 *   1c9a: lcall 0x0dd1         ; table_index_read
 *   1c9d: movx a, @dptr
 *   1c9e: ret
 */
uint8_t nvme_get_pcie_count_config(void)
{
    uint8_t index = G_PCIE_TXN_COUNT_LO;
    uint16_t addr = 0x05B4 + (index * 0x22);
    return *(__xdata uint8_t *)addr;
}

/*
 * nvme_calc_dptr_0500_base - Calculate DPTR = 0x0500 + A (with carry)
 * Address: 0x3257-0x325e (8 bytes)
 *
 * Sets DPTR to 0x0500 + A. If carry is set from previous operation,
 * it will be added to the high byte.
 *
 * Original disassembly:
 *   3257: mov 0x82, a           ; DPL = A
 *   3259: clr a                 ; A = 0
 *   325a: addc a, #0x05         ; A = 5 + carry
 *   325c: mov 0x83, a           ; DPH = A
 *   325e: ret
 */
__xdata uint8_t *nvme_calc_dptr_0500_base(uint8_t val)
{
    return (__xdata uint8_t *)(0x0500 + val);
}

/*
 * nvme_calc_dptr_direct_with_carry - Calculate DPTR = A (with carry to high)
 * Address: 0x325f-0x3266 (8 bytes)
 *
 * Sets DPTR to A in low byte, carry in high byte.
 * Similar to usb_calc_dptr_direct but carries to high.
 *
 * Original disassembly:
 *   325f: mov 0x82, a           ; DPL = A
 *   3261: clr a                 ; A = 0
 *   3262: addc a, #0x00         ; A = 0 + carry
 *   3264: mov 0x83, a           ; DPH = A
 *   3266: ret
 */
__xdata uint8_t *nvme_calc_dptr_direct_with_carry(uint8_t val)
{
    /* Without actual carry flag, this just returns the value as an address */
    return (__xdata uint8_t *)val;
}

/*
 * nvme_setup_regs_from_a - Setup R0-R3 with A and memory at 0x06
 * Address: 0x3279-0x327f (7 bytes)
 *
 * Sets R3=A, R2=(0x06), R1=0, R0=0.
 * This is a register setup helper function.
 *
 * Original disassembly:
 *   3279: mov r3, a             ; R3 = A
 *   327a: mov r2, 0x06          ; R2 = value at address 0x06
 *   327c: clr a                 ; A = 0
 *   327d: mov r1, a             ; R1 = 0
 *   327e: mov r0, a             ; R0 = 0
 *   327f: ret
 *
 * Note: This function modifies registers directly, so C implementation
 * returns a struct to capture the 32-bit result.
 */
/* This function is typically called inline and modifies R0-R3 directly.
 * In C, we can't directly modify registers, so callers should use
 * inline assembly or handle this differently. For now, marking as stub. */

/*
 * nvme_init_step - Set EP configuration for NVMe endpoint setup
 * Address: 0x3267-0x3271 (11 bytes)
 *
 * Writes endpoint configuration values for NVMe mode.
 * Sets REG_USB_EP_CFG1 to 2 and REG_USB_EP_CFG2 to 0x10.
 *
 * Original disassembly:
 *   3267: mov dptr, #0x9093
 *   326a: mov a, #0x02
 *   326c: movx @dptr, a        ; REG_USB_EP_CFG1 = 2
 *   326d: inc dptr
 *   326e: mov a, #0x10
 *   3270: movx @dptr, a        ; REG_USB_EP_CFG2 = 0x10
 *   3271: ret
 */
void nvme_init_step(void)
{
    REG_USB_EP_CFG1 = 0x02;
    REG_USB_EP_CFG2 = 0x10;
}

/*
 * nvme_read_status - Set bit 4 on the given register pointer
 * Address: 0x3272-0x3278 (7 bytes)
 *
 * Reads the byte at ptr, clears bit 4, sets bit 4, writes back.
 * Used for status indication on NVMe registers.
 *
 * Original disassembly:
 *   3272: movx a, @dptr        ; read
 *   3273: anl a, #0xef         ; clear bit 4
 *   3275: orl a, #0x10         ; set bit 4
 *   3277: movx @dptr, a        ; write
 *   3278: ret
 */
void nvme_read_status(__xdata uint8_t *ptr)
{
    *ptr = (*ptr & 0xEF) | 0x10;
}

/*
 * nvme_set_int_aux_bit1 - Set bit 1 on interrupt auxiliary register
 * Address: 0x3280-0x3289 (10 bytes)
 *
 * Manipulates REG_INT_AUX_C805: clears bits 1 and 2, then sets bit 1.
 *
 * Original disassembly:
 *   3280: mov dptr, #0xc805
 *   3283: movx a, @dptr        ; read REG_INT_AUX_C805
 *   3284: anl a, #0xf9         ; clear bits 1 and 2
 *   3286: orl a, #0x02         ; set bit 1
 *   3288: movx @dptr, a        ; write back
 *   3289: ret
 */
void nvme_set_int_aux_bit1(void)
{
    uint8_t val = REG_INT_AUX_C805;
    val = (val & 0xF9) | 0x02;
    REG_INT_AUX_C805 = val;
}

/*
 * nvme_get_link_status_masked - Get NVMe link status masked to bits 0-1
 * Address: 0x328a-0x3290 (7 bytes)
 *
 * Reads 0x9100 (USB peripheral status) and masks to lower 2 bits.
 *
 * Original disassembly:
 *   328a: mov dptr, #0x9100
 *   328d: movx a, @dptr        ; read 0x9100
 *   328e: anl a, #0x03         ; mask bits 0-1
 *   3290: ret
 */
uint8_t nvme_get_link_status_masked(void)
{
    return REG_USB_LINK_STATUS & 0x03;
}

/*
 * nvme_set_ep_ctrl_bits - Set control bits on EP register (bits 1 and 2)
 * Address: 0x320c-0x3218 (13 bytes)
 *
 * Sets bit 1 on register, then sets bit 2 on same register.
 * Used for endpoint control configuration.
 *
 * Original disassembly:
 *   320c: movx a, @dptr        ; read
 *   320d: anl a, #0xfd         ; clear bit 1
 *   320f: orl a, #0x02         ; set bit 1
 *   3211: movx @dptr, a        ; write
 *   3212: movx a, @dptr        ; read again
 *   3213: anl a, #0xfb         ; clear bit 2
 *   3215: orl a, #0x04         ; set bit 2
 *   3217: movx @dptr, a        ; write
 *   3218: ret
 */
void nvme_set_ep_ctrl_bits(__xdata uint8_t *ptr)
{
    uint8_t val;

    /* Set bit 1 */
    val = *ptr;
    val = (val & 0xFD) | 0x02;
    *ptr = val;

    /* Set bit 2 */
    val = *ptr;
    val = (val & 0xFB) | 0x04;
    *ptr = val;
}

/*
 * nvme_set_usb_ep_ctrl_bit2 - Set just bit 2 on EP register
 * Address: 0x3212-0x3218 (7 bytes, alt entry point)
 *
 * Sets only bit 2 on the register - subset of nvme_set_ep_ctrl_bits.
 *
 * Original disassembly:
 *   3212: movx a, @dptr        ; read
 *   3213: anl a, #0xfb         ; clear bit 2
 *   3215: orl a, #0x04         ; set bit 2
 *   3217: movx @dptr, a        ; write
 *   3218: ret
 */
void nvme_set_usb_ep_ctrl_bit2(__xdata uint8_t *ptr)
{
    uint8_t val = *ptr;
    val = (val & 0xFB) | 0x04;
    *ptr = val;
}

/* External helper from protocol.c */
extern void helper_53c0(void);

/*
 * nvme_call_and_signal - Call function and signal via 0x90A1
 * Address: 0x3219-0x3222 (10 bytes)
 *
 * Calls function at 0x53c0, then writes 1 to 0x90A1.
 * The function at 0x53c0 copies IDATA[0x6F-0x72] to XDATA 0xD808-0xD80B.
 *
 * Original disassembly:
 *   3219: lcall 0x53c0         ; call function
 *   321c: mov dptr, #0x90a1
 *   321f: mov a, #0x01
 *   3221: movx @dptr, a        ; write 1 to 0x90A1
 *   3222: ret
 */
void nvme_call_and_signal(void)
{
    /* Call DMA buffer write helper */
    helper_53c0();

    /* Set the signal register */
    REG_USB_SIGNAL_90A1 = 0x01;
}

/*
 * nvme_add_8_to_addr - Add 8 to 16-bit address in R1:R2
 * Address: 0x3223-0x322d (11 bytes)
 *
 * Takes a 16-bit address in R1:R2 (R1=low, R2=high) and adds 8.
 * Returns the result in DPTR.
 *
 * Original disassembly:
 *   3223: mov a, r1           ; A = R1 (low byte)
 *   3224: add a, #0x08        ; A += 8
 *   3226: mov r1, a           ; R1 = result low
 *   3227: clr a               ; A = 0
 *   3228: addc a, r2          ; A = R2 + carry
 *   3229: mov 0x82, r1        ; DPL = R1
 *   322b: mov 0x83, a         ; DPH = A
 *   322d: ret
 */
__xdata uint8_t *nvme_add_8_to_addr(uint8_t addr_lo, uint8_t addr_hi)
{
    uint16_t addr = ((uint16_t)addr_hi << 8) | addr_lo;
    addr += 8;
    return (__xdata uint8_t *)addr;
}

/*
 * nvme_set_buffer_flags - Set buffer length and system flags
 * Address: 0x323b-0x3248 (14 bytes)
 *
 * Writes 0x0A to G_BUFFER_LENGTH_HIGH (0x0054) and sets bits 1,4,5
 * on G_SYS_FLAGS_0052 (0x0052).
 *
 * Original disassembly:
 *   323b: mov dptr, #0x0054   ; G_BUFFER_LENGTH_HIGH
 *   323e: mov a, #0x0a        ; A = 10
 *   3240: movx @dptr, a       ; write 10
 *   3241: mov dptr, #0x0052   ; G_SYS_FLAGS_0052
 *   3244: movx a, @dptr       ; read flags
 *   3245: orl a, #0x32        ; set bits 1, 4, 5
 *   3247: movx @dptr, a       ; write back
 *   3248: ret
 */
void nvme_set_buffer_flags(void)
{
    G_BUFFER_LENGTH_HIGH = 0x0A;
    G_SYS_FLAGS_0052 |= 0x32;
}

/*
 * usb_validate_descriptor - Copy descriptor validation data
 * Address: 0x31fb-0x320b (17 bytes)
 *
 * Copies values from 0xCEB2-0xCEB3 to 0x0056-0x0057.
 * Used during USB descriptor validation.
 *
 * Original disassembly:
 *   31fb: mov dptr, #0xceb2
 *   31fe: movx a, @dptr        ; read 0xCEB2
 *   31ff: mov dptr, #0x0056
 *   3202: movx @dptr, a        ; write to 0x0056
 *   3203: mov dptr, #0xceb3
 *   3206: movx a, @dptr        ; read 0xCEB3
 *   3207: mov dptr, #0x0057
 *   320a: movx @dptr, a        ; write to 0x0057
 *   320b: ret
 */
void usb_validate_descriptor(void)
{
    G_USB_ADDR_HI_0056 = REG_USB_DESC_VAL_CEB2;
    G_USB_ADDR_LO_0057 = REG_USB_DESC_VAL_CEB3;
}

/*
 * nvme_get_idata_0d_r7 - Read IDATA[0x0D] into return value
 * Address: 0x3291-0x3297 (7 bytes)
 *
 * Reads the value at IDATA[0x0D] and returns it.
 * Also clears an internal counter (R5 in assembly).
 *
 * Original disassembly:
 *   3291: mov r0, #0x0d         ; R0 = 0x0D
 *   3293: mov a, @r0            ; A = IDATA[0x0D]
 *   3294: mov r7, a             ; R7 = A
 *   3295: clr a                 ; A = 0
 *   3296: mov r5, a             ; R5 = 0
 *   3297: ret
 */
uint8_t nvme_get_idata_0d_r7(void)
{
    return *(__idata uint8_t *)0x0D;
}

/*
 * nvme_get_dma_status_masked - Get DMA status masked to upper bits
 * Address: 0x3298-0x329e (7 bytes)
 *
 * Reads DMA status register 0xC8D9 and masks to upper 5 bits.
 *
 * Original disassembly:
 *   3298: mov dptr, #0xc8d9
 *   329b: movx a, @dptr        ; read 0xC8D9
 *   329c: anl a, #0xf8         ; mask to upper bits
 *   329e: ret
 */
uint8_t nvme_get_dma_status_masked(void)
{
    return REG_DMA_STATUS3 & 0xF8;
}

/* Forward declaration for reg_wait_bit_clear from state_helpers.c */
extern void reg_wait_bit_clear(uint16_t addr, uint8_t mask, uint8_t flags, uint8_t timeout);

/*
 * nvme_process_cmd - Process NVMe command with register wait
 * Address: 0x31a0 region (referenced from ghidra line 6571)
 *
 * Calculates a lookup table address based on param, reads configuration
 * from there, and calls reg_wait_bit_clear with the values.
 *
 * The address calculation:
 * - Low byte: param * 2 + 0xAD
 * - High byte: 0x5C, or 0x5B if there's overflow from low byte
 *
 * This points to a table at 0x5CAD containing 2-byte entries with
 * register wait configuration (mask and timeout).
 */
void nvme_process_cmd(uint8_t param)
{
    uint16_t addr;
    uint8_t *ptr;
    uint8_t low_byte;
    uint8_t high_byte;

    /* Calculate address with overflow handling */
    low_byte = param * 2 + 0xAD;
    high_byte = (param * 2 > 0x52) ? 0x5B : 0x5C;
    addr = ((uint16_t)high_byte << 8) | low_byte;

    ptr = (__xdata uint8_t *)addr;

    /* Call reg_wait_bit_clear with config from table
     * ptr[0] = mask, ptr[1] = flags/timeout */
    reg_wait_bit_clear(0x0A7E, ptr[1], 0x01, ptr[0]);
}

/*
 * nvme_io_request - Copy byte from one computed address to another
 * Address: 0x31a5 region (referenced from ghidra line 6597)
 *
 * This is a memory copy operation between two computed addresses.
 * The source and destination addresses are computed from the parameters.
 *
 * Source: (param2 + offset, param1 + param4) where offset depends on carry
 * Dest: (param3 - 0x80, param4)
 */
void nvme_io_request(uint8_t param1, __xdata uint8_t *param2, uint8_t param3, uint8_t param4)
{
    uint8_t src_lo, src_hi;
    uint8_t dst_lo, dst_hi;
    uint16_t src_addr, dst_addr;

    /* Calculate source address */
    src_lo = param1 + param4;
    /* Handle carry from addition */
    src_hi = *param2 + param3;
    if ((uint16_t)param1 + param4 > 0xFF) {
        src_hi--;
    }
    src_addr = ((uint16_t)src_hi << 8) | src_lo;

    /* Calculate destination address */
    dst_lo = param4;
    dst_hi = param3 - 0x80;
    dst_addr = ((uint16_t)dst_hi << 8) | dst_lo;

    /* Copy byte */
    *(__xdata uint8_t *)dst_addr = *(__xdata uint8_t *)src_addr;
}

/*
 * nvme_build_cmd - Calculate build command result
 * Address: 0x31da-0x31e0 (7 bytes)
 *
 * Returns a computed value based on input parameter.
 * If param > 0xF3, returns 0xFF; otherwise returns 0.
 *
 * Original disassembly:
 *   31da: clr a
 *   31db: mov r7, a
 *   31dc: mov a, param
 *   31dd: clr c
 *   31de: subb a, #0xf4       ; compare with 0xF4
 *   31e0: ret                 ; carry indicates result
 */
uint8_t nvme_build_cmd(uint8_t param)
{
    return (param > 0xF3) ? 0xFF : 0x00;
}

/*
 * nvme_get_ep_table_entry - Get endpoint table entry value
 * Address: 0x31ea-0x31fa (17 bytes)
 *
 * Reads index from DPTR, then reads from table at 0x057F + (index * 10).
 * This is an endpoint configuration lookup table.
 *
 * Original disassembly:
 *   31ea: movx a, @dptr        ; read index from DPTR
 *   31eb: mov 0xf0, #0x0a      ; B = 10
 *   31ee: mul ab               ; A = index * 10
 *   31ef: add a, #0x7f         ; A += 0x7F
 *   31f1: mov 0x82, a          ; DPL = A
 *   31f3: clr a                ; A = 0
 *   31f4: addc a, #0x05        ; A = 5 + carry
 *   31f6: mov 0x83, a          ; DPH = A  (DPTR = 0x057F + index*10)
 *   31f8: movx a, @dptr        ; read from table
 *   31f9: mov r7, a            ; return in R7
 *   31fa: ret
 */
uint8_t nvme_get_ep_table_entry(__xdata uint8_t *index_ptr)
{
    uint8_t index = *index_ptr;
    uint16_t addr = 0x057F + ((uint16_t)index * 10);
    return *(__xdata uint8_t *)addr;
}

/*
 * nvme_submit_cmd - Submit command to NVMe controller
 * Address: 0x31fb region (part of larger function)
 *
 * Note: This is related to descriptor validation.
 * The actual command submission goes through nvme_ring_doorbell.
 * This function is called as part of the command flow.
 */
void nvme_submit_cmd(void)
{
    /* This function is essentially usb_validate_descriptor in the binary,
     * but in the NVMe context it's called to finalize command state */
    usb_validate_descriptor();
}

/* These functions are defined in usb.c - use extern declarations */
extern uint16_t usb_read_status_pair(void);
extern void usb_copy_status_to_buffer(void);
extern void usb_set_transfer_active_flag(void);

/*
 * nvme_io_handler - Main NVMe I/O handler state machine
 * Address: 0x32a4-0x3418 (large function)
 *
 * Handles NVMe I/O operations based on current state in IDATA[0x6A].
 * This is a complex state machine that processes NVMe commands.
 *
 * States:
 * - State 2 (0x02): Process command based on XDATA[0x0002]
 * - Other states: Set transfer active and read status
 *
 * Note: This is a simplified implementation. The full function has
 * many more branches and state transitions.
 */
void nvme_io_handler(uint8_t param)
{
    uint8_t state;
    uint8_t cmd_type;
    uint8_t usb_status;

    state = *(__idata uint8_t *)0x6A;

    if (state != 0x02) {
        goto handle_default;
    }

    /* State 2: Check command type from XDATA[0x0002] */
    cmd_type = G_IO_CMD_STATE;

    if (cmd_type == 0xE3 || cmd_type == 0xFB || cmd_type == 0xE1) {
        /* These command types (0xE3=-0x1D, 0xFB=-0x05, 0xE1=-0x1F)
         * trigger the I/O path */

        if (cmd_type == 0xE3 || cmd_type == 0xFB) {
            /* Load IDATA dword, process status, store back */
            /* This is simplified - actual implementation needs
             * idata_load_dword, usb_read_status_pair, etc. */
            goto handle_io_path;
        }

        /* Check XDATA[0x0001] for additional processing */
        if (G_IO_CMD_TYPE != 0x07) {
            nvme_set_int_aux_bit1();
            /* Additional processing would go here */
            if (param == 0) {
                /* dma_setup_transfer(0, 3, 3); */
            }
            usb_status = REG_USB_STATUS;
            if ((usb_status & 0x01) == 0) {
                nvme_call_and_signal();
            }
            *(__idata uint8_t *)0x6A = 0x05;
            return;
        }

handle_io_path:
        /* Simplified I/O path handling */
        usb_read_status_pair();
        return;
    }

    /* cmd_type == 0xF9 (-7) also goes through special handling */
    if (cmd_type == 0xF9) {
        if (G_IO_CMD_TYPE != 0x07) {
            nvme_set_int_aux_bit1();
            if (param == 0) {
                /* dma_setup_transfer(0, 3, 3); */
            }
            *(__idata uint8_t *)0x6A = 0x05;
            return;
        }
        /* Fall through to I/O path */
        goto handle_io_path;
    }

handle_default:
    /* Default state: set transfer active and update status */
    usb_set_transfer_active_flag();
    nvme_read_status((__xdata uint8_t *)&REG_USB_STATUS);

    usb_status = REG_USB_STATUS;
    if (usb_status & 0x01) {
        nvme_check_completion((__xdata uint8_t *)0x905F);
        nvme_check_completion((__xdata uint8_t *)0x905D);
    }
}

/* Forward declaration for reg_wait_bit_set from state_helpers.c */
extern void reg_wait_bit_set(uint16_t addr);

/*
 * nvme_wait_for_ready - Wait for NVMe ready state via bit poll
 * Address: 0x329f-0x32a3 (5 bytes)
 *
 * Calls reg_wait_bit_set with address 0x0A7E to wait for ready state.
 *
 * Original disassembly:
 *   329f: mov dptr, #0x0a7e
 *   32a2: ljmp 0x0ddd         ; reg_wait_bit_set
 */
void nvme_wait_for_ready(void)
{
    reg_wait_bit_set(0x0A7E);
}

/* Forward declaration for function called by init_registers */
extern void nvme_func_04da(uint8_t param);

/*
 * nvme_init_registers - Initialize NVMe and USB registers for new transfer
 * Address: 0x3419-0x3576 (large function)
 *
 * This function initializes the NVMe subsystem for a new transfer by:
 * 1. Checking USB transfer state at 0x0B41
 * 2. If IDATA[0x6A] is non-zero, jumps to continuation at 0x3577
 * 3. Otherwise clears many registers to initialize state:
 *    - 0x0B01, 0x053B, 0x00C2, 0x0517, 0x014E, 0x00E5 = 0
 *    - REG at 0xCE88 = 0
 * 4. Waits for bit 0 of 0xCE89 to be set
 * 5. Checks additional status bits and continues initialization
 *
 * Original disassembly (start):
 *   3419: mov dptr, #0x0b41
 *   341c: movx a, @dptr
 *   341d: jz 0x3424
 *   341f: mov r7, #0x01
 *   3421: lcall 0x04da
 *   3424: mov r0, #0x6a
 *   3426: mov a, @r0
 *   3427: jz 0x342c
 *   3429: ljmp 0x3577
 *   342c: clr a
 *   342d: mov dptr, #0x0b01
 *   3430: movx @dptr, a        ; [0x0B01] = 0
 *   ...
 */
void nvme_init_registers(void)
{
    uint8_t val;

    /* Check USB transfer state */
    val = G_USB_STATE_0B41;
    if (val != 0) {
        /* Call helper function with param 1 */
        nvme_func_04da(0x01);
    }

    /* Check IDATA[0x6A] state */
    val = *(__idata uint8_t *)0x6A;
    if (val != 0) {
        /* State is non-zero, skip initialization */
        /* In full implementation, this would ljmp to 0x3577 */
        return;
    }

    /* Initialize state registers to zero */
    G_USB_INIT_0B01 = 0;
    G_NVME_STATE_053B = 0;
    G_INIT_STATE_00C2 = 0;
    G_EP_INIT_0517 = 0;
    G_USB_INDEX_COUNTER = 0;  /* 0x014E */
    G_INIT_STATE_00E5 = 0;

    /* Clear SCSI DMA control register */
    REG_XFER_CTRL_CE88 = 0;

    /* Wait for bit 0 of 0xCE89 to be set */
    do {
        val = REG_XFER_READY;
    } while ((val & 0x01) == 0);

    /* Check bit 1 for error/abort condition */
    val = REG_XFER_READY;
    if (val & 0x02) {
        /* Error condition, abort initialization */
        return;
    }

    /* Check bit 4 of 0xCE86 */
    val = REG_XFER_STATUS_CE86;
    if (val & 0x10) {
        /* Abort initialization */
        return;
    }

    /* Check transfer flag at 0x0AF8 */
    val = G_POWER_INIT_FLAG;
    if (val != 0x01) {
        /* Not ready for transfer */
        return;
    }

    /* Continue with full initialization sequence */
    /* Call function 0x4C98 */
    /* ... additional initialization would go here ... */
}

/*
 * nvme_func_1b07 - Get IDATA calculated address value
 * Address: 0x1b07-0x1b0a (4 bytes)
 *
 * Returns value from address calculated from IDATA[0x3E].
 * Address = 0x0100 + IDATA[0x3E] + 0x71, with carry to high byte.
 *
 * Original disassembly:
 *   1b07: mov a, #0x71
 *   1b09: add a, 0x3e         ; A = 0x71 + IDATA[0x3E]
 *   1b0b: mov dpl, a
 *   ...
 */
uint8_t nvme_func_1b07(void)
{
    uint8_t offset = *(__idata uint8_t *)0x3E;
    uint16_t addr;

    /* Calculate address: if 0x3E + 0x71 > 0xFF, high byte is 0x01, else 0x00 */
    if (offset > 0x8E) {
        addr = 0x0100 + offset + 0x71 - 0x100;  /* Wrap around */
    } else {
        addr = 0x0100 + offset + 0x71;
    }

    return *(__xdata uint8_t *)addr;
}

/*
 * nvme_func_1b0b - Get IDATA value from parameter address
 * Address: 0x1b0b-0x1b13 (9 bytes)
 *
 * Returns value from 0x01XX address where XX is the parameter.
 *
 * Original disassembly:
 *   1b0b: mov dpl, a          ; DPL = param
 *   1b0d: clr a
 *   1b0e: addc a, #0x01       ; DPH = 0x01 + carry from PSW
 *   1b10: mov dph, a
 *   1b12: movx a, @dptr
 *   1b13: ret
 */
uint8_t nvme_func_1b0b(uint8_t param)
{
    /* Simple read from 0x01XX address */
    return *(__xdata uint8_t *)(0x0100 + param);
}

/*
 * nvme_check_threshold_r5 - Check if BANK1_R5 > 0xF7
 * Address: 0x1b2b-0x1b2c (2 bytes)
 *
 * Returns 1 if value is <= 0xF7, 0 if > 0xF7.
 * Used for boundary checking in queue operations.
 *
 * Original disassembly:
 *   1b2b: mov a, r5           ; Get BANK1_R5 (need context)
 *   1b2c: setb c
 *   1b2d: subb a, #0xf8       ; Compare with 0xF8
 *   ...
 */
uint8_t nvme_check_threshold_r5(uint8_t val)
{
    return (val > 0xF7) ? 0 : 1;
}

/*
 * nvme_check_threshold_0x3e - Check if IDATA[0x3E] > 0xB1
 * Address: 0x1b38-0x1b3d (6 bytes)
 *
 * Returns 1 if IDATA[0x3E] <= 0xB1, 0 otherwise.
 *
 * Original disassembly:
 *   1b38: mov a, 0x3e         ; A = IDATA[0x3E]
 *   1b3a: setb c
 *   1b3b: subb a, #0xb2       ; Compare with 0xB2
 *   1b3d: ret                 ; Return with carry set if A < 0xB2
 */
uint8_t nvme_check_threshold_0x3e(void)
{
    uint8_t val = *(__idata uint8_t *)0x3E;
    return (val > 0xB1) ? 0 : 1;
}

/* usb_func_1b47 is defined in usb.c */
extern void usb_func_1b47(void);

/*
 * usb_check_status - Store parameter and set control status bit 1
 * Address: 0x1b4d-0x1b5c (16 bytes)
 *
 * Stores param_1 to *param_2, then sets bit 1 on REG_NVME_CTRL_STATUS.
 *
 * Original disassembly:
 *   1b4d: movx @dptr, a       ; *param_2 = param_1
 *   1b4e: mov dptr, #0xc412
 *   1b51: movx a, @dptr
 *   1b52: anl a, #0xfd        ; Clear bit 1
 *   1b54: orl a, #0x02        ; Set bit 1
 *   1b56: movx @dptr, a
 *   1b57: ret
 */
void usb_check_status(uint8_t param_1, __xdata uint8_t *param_2)
{
    *param_2 = param_1;
    REG_NVME_CTRL_STATUS = (REG_NVME_CTRL_STATUS & ~NVME_CTRL_STATUS_READY) | NVME_CTRL_STATUS_READY;
}

/*
 * usb_configure - Set bit 1 on register pointed to
 * Address: 0x1b58-0x1b5f (8 bytes)
 *
 * Reads value, clears bit 1, sets bit 1, writes back.
 *
 * Original disassembly:
 *   1b58: movx a, @dptr
 *   1b59: anl a, #0xfd        ; Clear bit 1
 *   1b5b: orl a, #0x02        ; Set bit 1
 *   1b5d: movx @dptr, a
 *   1b5e: ret
 */
void usb_configure(__xdata uint8_t *ptr)
{
    *ptr = (*ptr & 0xFD) | 0x02;
}

/*
 * nvme_get_idata_009f - Get value from 0x00XX calculated from IDATA[0x3E]
 * Address: 0x1b84-0x1b8c (9 bytes)
 *
 * Calculates address 0x0000 + IDATA[0x3E] + 0x9F, reads and returns.
 *
 * Original disassembly:
 *   1b84: mov a, 0x3e
 *   1b86: add a, #0x9f
 *   1b88: mov dpl, a
 *   1b8a: movx a, @dptr
 *   1b8b: ret
 */
uint8_t nvme_get_idata_009f(void)
{
    uint8_t offset = *(__idata uint8_t *)0x3E;
    uint16_t addr;

    /* Calculate address with potential overflow */
    if (offset > 0x60) {
        addr = 0x0100 + offset + 0x9F - 0x100;  /* Carry to high byte */
    } else {
        addr = offset + 0x9F;
    }

    return *(__xdata uint8_t *)addr;
}

/* usb_get_ep_config_indexed is defined in usb.c */
extern uint8_t usb_get_ep_config_indexed(void);

/* usb_read_buf_addr_pair is defined in usb.c */
extern uint16_t usb_read_buf_addr_pair(void);

/*
 * usb_data_handler - Set bit 0 on register
 * Address: 0x1bde-0x1be7 (same as nvme_set_usb_mode_bit)
 * Alt entry point for different use.
 *
 * Sets bit 0 on the register pointed to.
 */
void usb_data_handler(__xdata uint8_t *ptr)
{
    *ptr = (*ptr & 0xFE) | 0x01;
}

/*
 * nvme_func_1c2a - Get table entry from 0x5CAD table
 * Address: 0x1c2a-0x1c2f (6 bytes)
 *
 * Reads from table at 0x5CAD indexed by IDATA[0x3C] * 2 + param.
 *
 * Original disassembly:
 *   1c2a: mov dptr, #0x5cad
 *   1c2d: mov a, 0x3c
 *   1c2f: mov 0xf0, #0x02     ; B = 2
 *   1c32: mul ab              ; A = IDATA[0x3C] * 2
 *   1c33: add a, param        ; A = A + param
 *   1c35: add a, dpl
 *   ...
 */
uint8_t nvme_func_1c2a(uint8_t param)
{
    uint8_t idx = *(__idata uint8_t *)0x3C;
    uint16_t addr = 0x5CAD + (idx * 2) + param;
    return *(__xdata uint8_t *)(addr + 1);  /* Returns second byte of entry */
}

/*
 * nvme_func_1c43 - Store masked value to 0x01B4
 * Address: 0x1c43-0x1c49 (7 bytes)
 *
 * Masks parameter to 5 bits and stores to 0x01B4.
 *
 * Original disassembly:
 *   1c43: anl a, #0x1f        ; Mask to 5 bits
 *   1c45: mov dptr, #0x01b4
 *   1c48: movx @dptr, a
 *   1c49: ret
 */
void nvme_func_1c43(uint8_t param)
{
    G_USB_WORK_01B4 = param & 0x1F;
}

/*
 * nvme_func_1c55 - Get device status upper bits (alias)
 * Address: 0x1c55-0x1c5c (8 bytes)
 *
 * Same as nvme_get_dev_status_upper but as separate entry point.
 * Returns upper 2 bits of REG_NVME_DEV_STATUS.
 */
uint8_t nvme_func_1c55(void)
{
    return REG_NVME_DEV_STATUS & 0xC0;
}

/*
 * nvme_func_1c7e - Load IDATA dword and read register dword
 * Address: 0x1c7e-0x1c87 (10 bytes)
 *
 * Calls idata_load_dword(0x0E), then reg_read_dword(3).
 *
 * Original disassembly:
 *   1c7e: mov r0, #0x0e
 *   1c80: lcall 0x0d78        ; idata_load_dword
 *   1c83: mov r7, #0x03
 *   1c85: ljmp 0x0de5         ; reg_read_dword
 */
void nvme_func_1c7e(void)
{
    /* Load IDATA dword from offset 0x0E */
    uint32_t val;
    val = ((__idata uint8_t *)0x0E)[0];
    val |= ((uint32_t)((__idata uint8_t *)0x0E)[1]) << 8;
    val |= ((uint32_t)((__idata uint8_t *)0x0E)[2]) << 16;
    val |= ((uint32_t)((__idata uint8_t *)0x0E)[3]) << 24;

    /* Would call reg_read_dword(3) - reads register and stores result */
    /* For now this is a placeholder */
    (void)val;
}

/*
 * nvme_func_1c9f - Process command and check result
 * Address: 0x1c9f-0x1cad (15 bytes)
 *
 * Calls core_handler_4ff2 and FUN_CODE_4e6d, returns OR of R6 and R7.
 *
 * Original disassembly:
 *   1c9f: lcall 0x4ff2        ; core_handler_4ff2
 *   1ca2: lcall 0x4e6d        ; FUN_CODE_4e6d
 *   1ca5: mov a, r7
 *   1ca6: orl a, r6           ; A = R7 | R6
 *   1ca8: mov r7, a
 *   1ca9: ret
 */
uint8_t nvme_func_1c9f(void)
{
    /* This would call core_handler_4ff2() and FUN_CODE_4e6d() */
    /* Returns combined status from R6 | R7 */
    /* Placeholder implementation */
    return 0;
}

/*
 * nvme_get_addr_012b - Calculate address in 0x012B region (alternate entry)
 * Address: 0x1cb9-0x1cc0 (8 bytes)
 *
 * Returns pointer to 0x012B (no offset added in this version).
 *
 * Original disassembly:
 *   1cb9: mov a, #0x2b
 *   1cbb: mov dpl, a
 *   1cbd: clr a
 *   1cbe: addc a, #0x01
 *   1cc0: mov dph, a
 *   1cc2: ret
 */
__xdata uint8_t *nvme_get_addr_012b(void)
{
    return (__xdata uint8_t *)0x012B;
}

/*
 * nvme_func_1cf0 - Clear buffer and state registers
 * Address: 0x1cf0-0x1d23 (52 bytes)
 *
 * Clears multiple XDATA locations for state reset.
 *
 * Original disassembly (partial):
 *   1cf0: mov dptr, #0x05b0
 *   1cf3: e4             clr a
 *   1cf4: movx @dptr, a
 *   ...
 */
void nvme_func_1cf0(void)
{
    /* Clear state registers */
    G_PCIE_ADDR_1 = 0;           /* 0x05B0 */
    G_PCIE_ADDR_2 = 0;           /* 0x05B1 */
    G_PCIE_ADDR_3 = 0;           /* 0x05B2 */
    G_PCIE_TXN_COUNT_LO = 0;     /* 0x05A6 */
    G_PCIE_TXN_COUNT_HI = 0;     /* 0x05A7 */
}

/*
 * nvme_calc_dptr_0100_base - Calculate DPTR = 0x0100 + A (with carry)
 * Address: 0x31d8-0x31df (8 bytes)
 *
 * Sets DPTR to 0x0100 + A. If carry is set from previous operation,
 * it will be added to the high byte.
 *
 * Original disassembly:
 *   31d8: mov 0x82, a           ; DPL = A
 *   31da: clr a                 ; A = 0
 *   31db: addc a, #0x01         ; A = 1 + carry
 *   31dd: mov 0x83, a           ; DPH = A
 *   31df: ret
 */
__xdata uint8_t *nvme_calc_dptr_0100_base(uint8_t val)
{
    return (__xdata uint8_t *)(0x0100 + val);
}

/*
 * nvme_process_queue_entries - Process NVMe queue entries
 * Address: 0x488f-0x4903 (117 bytes)
 *
 * Main NVMe queue processing loop. Sets error flag, then iterates through
 * up to 32 queue entries, processing each one based on link status.
 *
 * Key registers used:
 * - 0x06E6: G_STATE_FLAG_06E6 - Error/processing flag (set to 1 at start)
 * - 0xC520: REG_NVME_LINK_STATUS - Bit 1 indicates link ready
 * - 0x0464: G_SYS_STATUS_PRIMARY - Cleared when link not ready
 * - 0x0465: G_SYS_STATUS_SECONDARY - Cleared when link not ready
 * - 0x0AF8: G_POWER_INIT_FLAG - Power state check
 * - 0xC51A: REG_NVME_QUEUE_TRIGGER - Queue trigger
 * - 0xC51E: REG_NVME_QUEUE_STATUS - Queue status (bits 0-5 = queue index)
 * - IDATA 0x38: Local queue index
 * - IDATA 0x39: Loop counter
 * - IDATA 0x3A: Flags register
 *
 * Algorithm:
 * 1. Set G_STATE_FLAG_06E6 = 1
 * 2. Loop counter = 0
 * 3. While counter < 32:
 *    a. Check REG_NVME_LINK_STATUS bit 1
 *    b. If not set, clear statuses and exit
 *    c. If G_POWER_INIT_FLAG != 0:
 *       - Read from 0xC51A, call 0x4b25
 *       - Get queue index from REG_NVME_QUEUE_STATUS (& 0x3F)
 *    d. Else:
 *       - Get queue index from REG_NVME_QUEUE_STATUS (& 0x3F)
 *       - Call 0x3da1 with index
 *    e. Build flags at IDATA 0x3A = 0x75
 *    f. Calculate address 0x0171 + queue_index
 *    g. If value at that address != 0, set bit 7 of flags
 *    h. Calculate address 0x0108 + queue_index
 *    i. Write flags there
 *    j. Calculate address 0x0517 + queue_index
 *    k. Increment value there
 *    l. Write 0xFF to REG_NVME_QUEUE_TRIGGER
 *    m. Increment counter
 * 4. Return
 *
 * Original disassembly summary:
 *   488f: mov dptr, #0x06e6    ; G_STATE_FLAG_06E6
 *   4892: mov a, #0x01
 *   4894: movx @dptr, a        ; Set flag = 1
 *   4895: clr a
 *   4896: mov 0x39, a          ; Counter = 0
 *   4898: mov dptr, #0xc520    ; Loop start - REG_NVME_LINK_STATUS
 *   489b: movx a, @dptr
 *   489c: jnb 0xe0.1, 0x4903   ; If bit 1 not set, exit
 *   489f: clr a
 *   48a0-48a7: Clear 0x0464 and 0x0465
 *   48a8-48ac: Check G_POWER_INIT_FLAG
 *   ... (queue processing)
 *   48fa: inc 0x39             ; counter++
 *   48fc-4901: if counter < 32, loop back to 4898
 *   4903: ret
 */
void nvme_process_queue_entries(void)
{
    __idata uint8_t *counter = (__idata uint8_t *)0x39;
    __idata uint8_t *queue_idx = (__idata uint8_t *)0x38;
    __idata uint8_t *flags = (__idata uint8_t *)0x3A;
    uint8_t status;
    __xdata uint8_t *ptr;

    /* Set error/processing flag */
    G_STATE_FLAG_06E6 = 0x01;

    /* Initialize counter */
    *counter = 0;

    /* Process up to 32 queue entries */
    while (*counter < 0x20) {
        /* Check NVMe link status - bit 1 must be set */
        status = REG_NVME_LINK_STATUS;
        if (!(status & 0x02)) {
            /* Link not ready - exit loop */
            return;
        }

        /* Clear system status when processing */
        G_SYS_STATUS_PRIMARY = 0;
        G_SYS_STATUS_SECONDARY = 0;

        /* Check power init flag */
        if (G_POWER_INIT_FLAG != 0) {
            /* Power initialized - read trigger and call 0x4b25 */
            uint8_t trigger_val = REG_NVME_QUEUE_TRIGGER;
            (void)trigger_val;  /* TODO: call nvme_helper_4b25(trigger_val) */

            /* Get queue index from status register */
            *queue_idx = REG_NVME_QUEUE_STATUS & 0x3F;
        } else {
            /* Not initialized - get index and call 0x3da1 */
            *queue_idx = REG_NVME_QUEUE_STATUS & 0x3F;
            /* TODO: call nvme_helper_3da1(*queue_idx) */
        }

        /* Build flags: start with 0x75 */
        *flags = 0x75;

        /* Check address 0x0171 + queue_index (IDATA region via XDATA) */
        ptr = nvme_calc_dptr_0100_base(0x71 + *queue_idx);
        if (*ptr != 0) {
            /* Set bit 7 of flags if value is non-zero */
            *flags |= 0x80;
        }

        /* Write flags to address 0x0108 + queue_index */
        ptr = nvme_calc_dptr_0100_base(0x08 + *queue_idx);
        *ptr = *flags;

        /* Increment value at 0x0517 + queue_index */
        ptr = nvme_calc_dptr_0500_base(0x17 + *queue_idx);
        (*ptr)++;

        /* Write 0xFF to queue trigger to acknowledge */
        REG_NVME_QUEUE_TRIGGER = 0xFF;

        /* Increment counter */
        (*counter)++;
    }
}

/*
 * nvme_state_handler - Handle NVMe state based on device mode
 * Address: 0x4784-0x47f1 (110 bytes)
 *
 * Handles different NVMe states based on device mode. Reads IDATA 0x6A
 * for state, then handles based on G_IO_CMD_TYPE value:
 * - State 3: Initialize if G_IO_CMD_TYPE == 5
 * - State 4: Link check/restart if G_IO_CMD_TYPE == 7
 * - State 3 with G_IO_CMD_TYPE == 3: Check USB status
 *
 * Original disassembly:
 *   4784: mov r0, #0x6a         ; Get state from IDATA 0x6A
 *   4786: mov a, @r0
 *   4787: add a, #0xfd          ; a = state - 3
 *   4789: jz 0x47be             ; if state == 3, goto handle_state_3
 *   478b: dec a                 ; a = state - 4
 *   478c: jz 0x47af             ; if state == 4, goto handle_state_4
 *   478e: dec a                 ; a = state - 5
 *   478f: jnz 0x47f2            ; if state != 5, goto error
 *   ... (state 5 handling = initialization)
 *   47f1: ret
 */
void nvme_state_handler(void)
{
    __idata uint8_t *state_ptr = (__idata uint8_t *)0x6A;
    uint8_t state = *state_ptr;
    uint8_t cmd_type;

    if (state == 3) {
        /* State 3 - Check command type for mode */
        cmd_type = G_IO_CMD_TYPE;
        if (cmd_type == 3) {
            /* Mode 3: Check USB status bit 0 */
            if (REG_USB_STATUS & 0x01) {
                /* USB active - call status handlers */
                nvme_call_and_signal();
                /* TODO: call 0x0206 */
            } else {
                /* USB not active - call alternative handler */
                /* TODO: call 0x3219 */
            }
        } else {
            /* Other modes - call error handler */
            /* TODO: call 0x312a, 0x31ce */
        }
        /* Set state to 5 (done) */
        *state_ptr = 0x05;
        return;
    }

    if (state == 4) {
        /* State 4 - Check for mode 7 (link restart) */
        cmd_type = G_IO_CMD_TYPE;
        if (cmd_type == 7) {
            /* Mode 7 - jump to 0x53d4 (link restart) */
            /* TODO: call nvme_link_restart */
            return;
        }
        /* Other modes - fall through to error */
    }

    if (state == 5) {
        /* State 5 - Initialization */
        cmd_type = G_IO_CMD_TYPE;
        if (cmd_type != 5) {
            /* Wrong mode - error */
            goto handle_error;
        }

        /* Mode 5 initialization */
        /* Set up NVMe doorbell register */
        REG_NVME_DOORBELL = (REG_NVME_DOORBELL & ~NVME_DOORBELL_MODE) | NVME_DOORBELL_MODE;  /* Set bit 3 */
        REG_NVME_DOORBELL &= ~NVME_DOORBELL_TRIGGER;  /* Clear bit 0 */
        REG_NVME_DOORBELL &= ~NVME_DOORBELL_MODE;  /* Clear bit 3 */

        /* Jump to common exit */
        goto common_exit;
    }

handle_error:
    /* Error path - call error handlers */
    /* TODO: call 0x312a, 0x31ce */

common_exit:
    /* Check USB status and handle */
    if (REG_USB_STATUS & 0x01) {
        /* USB active */
        nvme_call_and_signal();
        /* TODO: call 0x0206 */
    } else {
        /* USB not active */
        /* TODO: call 0x3219 */
    }

    /* Set state to 5 */
    *state_ptr = 0x05;
}

/*
 * nvme_queue_sync - Synchronize NVMe queue state
 * Address: 0x49e9-0x4a56 (110 bytes)
 *
 * Synchronizes NVMe queue state between controller and firmware.
 * Reads queue index from 0xC512, writes 0xFF to it (acknowledge),
 * then checks sync counters and handles pending operations.
 *
 * Key registers:
 * - 0xC512: REG_NVME_QUEUE_INDEX - Current queue index
 * - 0x009F: Queue sync reference
 * - 0x0517: Queue counter
 * - 0x00C2: Init state (G_INIT_STATE_00C2)
 * - 0x00E5: Init state 2 (G_INIT_STATE_00E5)
 * - 0xCEF3: REG_CPU_LINK_CEF3 - Link control register
 * - 0x0171: G_SCSI_CTRL - SCSI control
 * - 0x0B01: G_USB_INIT_0B01 - USB init state
 *
 * Original disassembly:
 *   49e9: mov dptr, #0xc512     ; REG_NVME_QUEUE_INDEX
 *   49ec: movx a, @dptr
 *   49ed: mov 0x38, a           ; Save index to IDATA 0x38
 *   49ef: mov a, #0xff
 *   49f1: movx @dptr, a         ; Acknowledge by writing 0xFF
 *   49f2: mov dptr, #0x009f     ; Read sync reference
 *   49f5: movx a, @dptr
 *   49f6: mov r7, a
 *   49f7: mov dptr, #0x0517     ; Read queue counter
 *   49fa: movx a, @dptr
 *   49fb: inc a                 ; Increment
 *   49fc: movx @dptr, a         ; Store back
 *   49fd: xrl a, r7             ; Compare with reference
 *   49fe: jnz 0x4a36            ; If not equal, skip main processing
 *   4a00: ... (main processing loop)
 *   4a56: ret
 */
void nvme_queue_sync(void)
{
    __idata uint8_t *queue_idx = (__idata uint8_t *)0x38;
    uint8_t sync_ref, counter_val;
    __xdata uint8_t *ptr;

    /* Read queue index and acknowledge */
    *queue_idx = REG_NVME_QUEUE_INDEX;
    REG_NVME_QUEUE_INDEX = 0xFF;

    /* Read sync reference from 0x009F */
    sync_ref = *(__xdata uint8_t *)0x009F;

    /* Increment queue counter at 0x0517 */
    ptr = (__xdata uint8_t *)0x0517;
    (*ptr)++;
    counter_val = *ptr;

    /* If counter doesn't match reference, skip main processing */
    if (counter_val != sync_ref) {
        goto check_scsi;
    }

    /* Main processing loop - check init state and process */
    while (1) {
        sync_ref = *(__xdata uint8_t *)0x00C2;  /* G_INIT_STATE_00C2 */
        counter_val = *(__xdata uint8_t *)0x0517;

        if (counter_val != sync_ref) {
            /* Check G_INIT_STATE_00E5 */
            if (G_INIT_STATE_00E5 != 0) {
                /* Call helper with params: R3=0x03, R5=0x47, R7=0x0B */
                /* TODO: call helper_523c(0x03, 0x47, 0x0B) */
            }
            goto call_state_handler;
        }

        /* Check REG_CPU_LINK_CEF3 bit 3 */
        if (REG_CPU_LINK_CEF3 & 0x08) {
            /* Set CEF3 to 0x08 and call handler_2608 */
            REG_CPU_LINK_CEF3 = 0x08;
            /* TODO: call handler_2608() */
            continue;  /* Loop back */
        }

        /* Call helper 0x0395 and check result */
        /* TODO: result = call_0395() */
        /* if (result == 0) continue; else break; */
        break;
    }
    return;

call_state_handler:
    /* Call nvme_state_handler */
    nvme_state_handler();
    goto check_scsi;

check_scsi:
    /* Check G_SCSI_CTRL (0x0171) for pending operations */
    if (G_SCSI_CTRL > 0) {
        /* Decrement SCSI control */
        G_SCSI_CTRL--;

        /* Check G_USB_INIT_0B01 */
        if (G_USB_INIT_0B01 != 0) {
            /* Call helper with queue index */
            /* TODO: call helper_4eb3(0, *queue_idx) */
        } else {
            /* Call alternative helper */
            /* TODO: call helper_46f8(0, *queue_idx) */
        }
    }
}

/*
 * nvme_queue_process_pending - Process pending NVMe queue operations
 * Address: 0x3e81-0x3f2b (171 bytes)
 *
 * Processes pending queue operations. Reads queue status from 0xC516,
 * increments queue counters, and handles completion based on various
 * state flags.
 *
 * Key registers:
 * - 0xC516: REG_NVME_QUEUE_C516 - Queue status (bits 0-5: index)
 * - 0x009F: Queue reference base
 * - 0x00C2: Init state comparison
 * - 0x00E5: Secondary state flag
 * - 0xCEF3: REG_CPU_LINK_CEF3 - Link control
 * - 0xCEF2: REG_CPU_LINK_CEF2 - Link status
 * - 0x05AC: Buffer state flag
 *
 * Original disassembly:
 *   3e81: mov dptr, #0xc516     ; REG_NVME_QUEUE_C516
 *   3e84: movx a, @dptr
 *   3e85: anl a, #0x3f          ; Mask to 6 bits
 *   3e87: mov 0x38, a           ; Store to IDATA 0x38
 *   3e89: add a, #0x17          ; A = index + 0x17
 *   3e8b: lcall 0x3257          ; calc_dptr_0500_base
 *   3e8e: movx a, @dptr         ; Read queue counter
 *   3e8f: inc a                 ; Increment
 *   3e90: movx @dptr, a         ; Store back
 *   3e91: mov 0x39, a           ; Save new counter to IDATA 0x39
 *   3e93: mov a, #0x9f          ; Calculate 0x009F + index
 *   3e95: add a, 0x38
 *   3e97-3e9e: Read value and compare with counter
 *   ... (processing logic)
 *   3f2b: ret
 */
void nvme_queue_process_pending(void)
{
    __idata uint8_t *queue_idx = (__idata uint8_t *)0x38;
    __idata uint8_t *counter = (__idata uint8_t *)0x39;
    uint8_t status, ref_val;
    __xdata uint8_t *ptr;

    /* Read queue status and get index */
    status = REG_NVME_QUEUE_C516;
    *queue_idx = status & 0x3F;

    /* Increment counter at 0x0517 + index */
    ptr = nvme_calc_dptr_0500_base(0x17 + *queue_idx);
    (*ptr)++;
    *counter = *ptr;

    /* Calculate reference address: 0x009F + index */
    ptr = (__xdata uint8_t *)(0x009F + *queue_idx);
    ref_val = *ptr;

    /* Compare counter with reference */
    if (*counter != ref_val) {
        /* Not matched - jump to error handling */
        goto handle_mismatch;
    }

    /* Counter matches - check init state at 0x00C2 + index */
    ptr = (__xdata uint8_t *)(0x00C2 + *queue_idx);
    if (*ptr != *counter) {
        /* Check secondary state at 0x00E5 + index */
        ptr = (__xdata uint8_t *)(0x00E5 + *queue_idx);
        if (*ptr != 0) {
            /* Non-zero secondary state - call helper with params */
            /* TODO: call helper_523c(0x03, 0x47, 0x0B) */
        }
        goto done;
    }

    /* Check link control register bit 3 */
    if (!(REG_CPU_LINK_CEF3 & 0x08)) {
        /* Bit 3 not set - check buffer state at 0x05AC */
        ptr = (__xdata uint8_t *)0x05AC;
        if (*ptr == 0) {
            goto done;
        }

        /* Check REG_CPU_LINK_CEF2 bit 7 */
        if (REG_CPU_LINK_CEF2 & 0x80) {
            goto done;
        }
    }

    /* Set link control bit 3 and call handler */
    REG_CPU_LINK_CEF3 = 0x08;
    /* TODO: call handler_2608() */
    return;

handle_mismatch:
    /* Handle counter mismatch - typically retry or error */
    /* TODO: Additional error handling */

done:
    return;
}

/*
 * nvme_queue_helper_180d - NVMe queue initialization helper
 * Address: 0x180d-0x18xx
 *
 * Initializes NVMe queue state based on parameter.
 *
 * Parameters:
 *   r7: Queue enable flag (1=enable)
 *
 * Original disassembly:
 *   180d: mov dptr, #0x0a7d
 *   1810: mov a, r7
 *   1811: movx @dptr, a         ; Store r7 to 0x0A7D
 *   1812: xrl a, #0x01          ; Check if r7 == 1
 *   1814: jz 0x1819             ; If r7 == 1, continue
 *   1816: ljmp 0x19fa           ; Else jump to alternate path
 *   1819: ...                   ; Continue with queue setup
 */
static void nvme_queue_helper_180d(uint8_t enable)
{
    /* Store enable flag */
    *(__xdata uint8_t *)0x0A7D = enable;

    if (enable != 1) {
        /* Alternate path at 0x19FA - TODO */
        return;
    }

    /* Check if initialization needed */
    if (*(__xdata uint8_t *)0x000A == 0) {
        /* Increment counter at 0x07E8 */
        (*(__xdata uint8_t *)0x07E8)++;

        /* Check state at 0x0B41 */
        if (*(__xdata uint8_t *)0x0B41 != 0) {
            /* Call helper at 0x04DA with r7=1 */
            /* TODO: Implement helper_04da */
        }
    }

    /* Write queue index to 0xCE88 */
    *(__idata uint8_t *)0x38 = *(__xdata uint8_t *)0xC47A;
    REG_XFER_CTRL_CE88 = *(__idata uint8_t *)0x38;

    /* Wait for bit 0 of 0xCE89 */
    while (!(REG_XFER_READY & 0x01));

    /* Increment counter at 0x000A */
    (*(__xdata uint8_t *)0x000A)++;
}

/*
 * nvme_queue_helper - NVMe queue processing helper
 * Address: 0x1196-0x11a1 (12 bytes)
 *
 * Entry point for NVMe queue helper. Sets up queue state and
 * marks queue as active.
 *
 * Original disassembly:
 *   1196: mov r7, #0x01
 *   1198: lcall 0x180d          ; Call queue init helper
 *   119b: mov dptr, #0xc47a
 *   119e: mov a, #0xff
 *   11a0: movx @dptr, a         ; Set 0xC47A = 0xFF
 *   11a1: ret
 */
void nvme_queue_helper(void)
{
    /* Initialize queue with enable flag */
    nvme_queue_helper_180d(1);

    /* Mark queue as active */
    *(__xdata uint8_t *)0xC47A = 0xFF;
}
