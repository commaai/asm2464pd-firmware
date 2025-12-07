/*
 * ASM2464PD Firmware - Interrupt Controller Driver
 *
 * Handles interrupt status checking, acknowledgment, and dispatch.
 * The ASM2464PD uses a custom interrupt controller with status registers
 * for different interrupt sources.
 *
 *===========================================================================
 * INTERRUPT CONTROLLER ARCHITECTURE
 *===========================================================================
 *
 * Hardware Configuration:
 * - Custom interrupt controller (not standard 8051 interrupts)
 * - Multiple interrupt status registers for different domains
 * - Level-triggered interrupts with status polling
 *
 * Register Map (0xC800-0xC80F):
 * +-----------+----------------------------------------------------------+
 * | Address   | Description                                              |
 * +-----------+----------------------------------------------------------+
 * | 0xC801    | Interrupt control register                               |
 * | 0xC802    | USB master interrupt status                              |
 * |           |   bit 0: USB interrupt pending                           |
 * | 0xC805    | Auxiliary interrupt status                               |
 * | 0xC806    | System interrupt status                                  |
 * |           |   bit 0: System event interrupt                          |
 * |           |   bit 4: Timer/timeout interrupt                         |
 * |           |   bit 5: Link state change interrupt                     |
 * | 0xC809    | Interrupt control 2                                      |
 * | 0xC80A    | PCIe/NVMe interrupt status                               |
 * |           |   bit 4: NVMe command completion                         |
 * |           |   bit 5: PCIe link event                                 |
 * |           |   bit 6: NVMe queue interrupt                            |
 * +-----------+----------------------------------------------------------+
 *
 * Interrupt Dispatch Flow (from 0x44a3):
 * +----------------------------------------------------------------------+
 * |                    INTERRUPT DISPATCH                                |
 * +----------------------------------------------------------------------+
 * |  1. Check 0xC806 bit 0 -> call system event handler (0x0520)        |
 * |  2. Check 0xCC33 bit 2 -> call state handler (0x0390)               |
 * |  3. Check 0xC80A bit 6 -> call NVMe queue handler (0x052f)          |
 * |  4. Check event flags in 0x09F9                                     |
 * |  5. Check 0xC80A bit 5 -> call PCIe handler (0x061a)                |
 * |  6. Check 0xC80A bit 4 -> call NVMe handler (0x0593)                |
 * |  7. Check 0xC806 bit 4 -> call timer handler (0x0642)               |
 * +----------------------------------------------------------------------+
 *
 * Timer Interrupt Handler (0x450d-0x4531):
 * - Checks 0xC806 bit 4 and calls handler 0x0642
 * - Uses RETI to return from interrupt context
 *
 *===========================================================================
 * IMPLEMENTATION STATUS
 *===========================================================================
 * int_get_system_status       [DONE] Read system interrupt status
 * int_get_pcie_nvme_status    [DONE] Read PCIe/NVMe interrupt status
 * int_get_usb_status          [DONE] Read USB interrupt status
 * int_check_system_event      [DONE] Check system event bit
 * int_check_nvme_queue        [DONE] Check NVMe queue interrupt
 * int_check_pcie_event        [DONE] Check PCIe link event
 * int_check_nvme_complete     [DONE] Check NVMe command completion
 * int_check_timer             [DONE] Check timer interrupt
 *
 * Total: 8 functions implemented
 *===========================================================================
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"

/*
 * int_get_system_status - Read system interrupt status register
 * Based on: 0x44a3-0x44a6
 *
 * Reads the system interrupt status register (0xC806).
 *
 * Returns: Current value of system interrupt status
 *
 * Original disassembly:
 *   44a3: mov dptr, #0xc806   ; System interrupt status
 *   44a6: movx a, @dptr       ; Read status
 */
uint8_t int_get_system_status(void)
{
    return REG_INT_SYSTEM;
}

/*
 * int_get_pcie_nvme_status - Read PCIe/NVMe interrupt status register
 * Based on: 0x44ba-0x44bd
 *
 * Reads the PCIe/NVMe interrupt status register (0xC80A).
 *
 * Returns: Current value of PCIe/NVMe interrupt status
 *
 * Original disassembly:
 *   44ba: mov dptr, #0xc80a   ; PCIe/NVMe interrupt status
 *   44bd: movx a, @dptr       ; Read status
 */
uint8_t int_get_pcie_nvme_status(void)
{
    return REG_INT_PCIE_NVME;
}

/*
 * int_get_usb_status - Read USB master interrupt status register
 * Based on: 0x0e78-0x0e7b
 *
 * Reads the USB master interrupt status register (0xC802).
 *
 * Returns: Current value of USB interrupt status
 *
 * Original disassembly:
 *   0e78: mov dptr, #0xc802   ; USB master interrupt status
 *   0e7b: movx a, @dptr       ; Read status
 */
uint8_t int_get_usb_status(void)
{
    return REG_INT_USB_MASTER;
}

/*
 * int_check_system_event - Check if system event interrupt is pending
 * Based on: 0x44a7-0x44aa
 *
 * Checks bit 0 of system interrupt status register.
 * If set, calls the system event handler.
 *
 * Returns: Non-zero if system event interrupt pending
 *
 * Original disassembly:
 *   44a7: jnb 0xe0.0, 0x44ad  ; Check bit 0
 *   44aa: lcall 0x0520        ; Call system event handler
 */
uint8_t int_check_system_event(void)
{
    return (REG_INT_SYSTEM & 0x01);
}

/*
 * int_check_nvme_queue - Check if NVMe queue interrupt is pending
 * Based on: 0x44be-0x44c1
 *
 * Checks bit 6 of PCIe/NVMe interrupt status register.
 * This indicates an NVMe queue event.
 *
 * Returns: Non-zero if NVMe queue interrupt pending
 *
 * Original disassembly:
 *   44be: jnb 0xe0.6, 0x44c4  ; Check bit 6
 *   44c1: lcall 0x052f        ; Call NVMe queue handler
 */
uint8_t int_check_nvme_queue(void)
{
    return (REG_INT_PCIE_NVME & 0x40);
}

/*
 * int_check_pcie_event - Check if PCIe link event interrupt is pending
 * Based on: 0x44d0-0x44d3
 *
 * Checks bit 5 of PCIe/NVMe interrupt status register.
 * This indicates a PCIe link state change.
 *
 * Returns: Non-zero if PCIe event interrupt pending
 *
 * Original disassembly:
 *   44d0: jnb 0xe0.5, 0x44d6  ; Check bit 5
 *   44d3: lcall 0x061a        ; Call PCIe handler
 */
uint8_t int_check_pcie_event(void)
{
    return (REG_INT_PCIE_NVME & 0x20);
}

/*
 * int_check_nvme_complete - Check if NVMe command completion interrupt
 * Based on: 0x44da-0x44dd
 *
 * Checks bit 4 of PCIe/NVMe interrupt status register.
 * This indicates NVMe command completion.
 *
 * Returns: Non-zero if NVMe completion interrupt pending
 *
 * Original disassembly:
 *   44da: jnb 0xe0.4, 0x44e0  ; Check bit 4
 *   44dd: lcall 0x0593        ; Call NVMe handler
 */
uint8_t int_check_nvme_complete(void)
{
    return (REG_INT_PCIE_NVME & 0x10);
}

/*
 * int_check_timer - Check if timer interrupt is pending
 * Based on: 0x4511-0x4514
 *
 * Checks bit 4 of system interrupt status register.
 * This indicates a timer event.
 *
 * Returns: Non-zero if timer interrupt pending
 *
 * Original disassembly:
 *   450d: mov dptr, #0xc806   ; System interrupt status
 *   4510: movx a, @dptr       ; Read status
 *   4511: jnb 0xe0.4, 0x4517  ; Check bit 4
 *   4514: lcall 0x0642        ; Call timer handler
 */
uint8_t int_check_timer(void)
{
    return (REG_INT_SYSTEM & 0x10);
}
