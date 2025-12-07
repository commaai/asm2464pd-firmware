#ifndef __GLOBALS_H__
#define __GLOBALS_H__

#include "types.h"

/*
 * ASM2464PD Firmware - Global Variables
 *
 * Global variables in XRAM work area (addresses < 0x6000).
 * These are NOT hardware registers - they are RAM locations used
 * by the firmware for state tracking and data transfer.
 *
 * Memory map (from reverse engineering):
 * 0x0000-0x5FFF: 24 kB XRAM (work area)
 * 0x6000-0x6FFF: 4 kB unused
 * 0x7000-0x7FFF: 4 kB XRAM (SPI flash buffer)
 */

//=============================================================================
// Helper Macro
//=============================================================================
#define XDATA_VAR8(addr)   (*(__xdata uint8_t *)(addr))

//=============================================================================
// System Work Area (0x0000-0x01FF)
//=============================================================================
#define G_SYSTEM_CTRL           XDATA_VAR8(0x0000)  /* System control byte */
#define G_EP_STATUS_CTRL        XDATA_VAR8(0x0003)  /* Endpoint status control (checked by usb_ep_process) */
#define G_EP_CHECK_FLAG         XDATA_VAR8(0x000A)  /* Endpoint check flag */
#define G_BUFFER_LENGTH_HIGH    XDATA_VAR8(0x0054)  /* Buffer length high byte (for mode 4) */
#define G_NVME_QUEUE_READY      XDATA_VAR8(0x0055)  /* NVMe queue ready flag */
#define G_USB_INDEX_COUNTER     XDATA_VAR8(0x014E)  /* USB index counter (5-bit) */
#define G_SCSI_CTRL             XDATA_VAR8(0x0171)  /* SCSI control */

//=============================================================================
// DMA Work Area (0x0200-0x02FF)
//=============================================================================
#define G_DMA_MODE_SELECT       XDATA_VAR8(0x0203)  /* DMA mode select */
#define G_DMA_PARAM1            XDATA_VAR8(0x020D)  /* DMA parameter 1 */
#define G_DMA_PARAM2            XDATA_VAR8(0x020E)  /* DMA parameter 2 */
#define G_BUF_ADDR_HI           XDATA_VAR8(0x0218)  /* Buffer address high */
#define G_BUF_ADDR_LO           XDATA_VAR8(0x0219)  /* Buffer address low */
#define G_BUF_BASE_HI           XDATA_VAR8(0x021A)  /* Buffer base address high */
#define G_BUF_BASE_LO           XDATA_VAR8(0x021B)  /* Buffer base address low */

//=============================================================================
// System Status Work Area (0x0400-0x04FF)
//=============================================================================
#define G_REG_WAIT_BIT          XDATA_VAR8(0x045E)  /* Register wait bit */
#define G_SYS_STATUS_PRIMARY    XDATA_VAR8(0x0464)  /* Primary system status */
#define G_SYS_STATUS_SECONDARY  XDATA_VAR8(0x0465)  /* Secondary system status */
#define G_SYSTEM_CONFIG         XDATA_VAR8(0x0466)  /* System configuration */
#define G_SYSTEM_STATE          XDATA_VAR8(0x0467)  /* System state */
#define G_DATA_PORT             XDATA_VAR8(0x0468)  /* Data port */
#define G_INT_STATUS            XDATA_VAR8(0x0469)  /* Interrupt status */
#define G_DMA_LOAD_PARAM1       XDATA_VAR8(0x0472)  /* DMA load parameter 1 */
#define G_DMA_LOAD_PARAM2       XDATA_VAR8(0x0473)  /* DMA load parameter 2 */
#define G_STATE_HELPER_41       XDATA_VAR8(0x0474)  /* State helper byte from R41 */
#define G_STATE_HELPER_42       XDATA_VAR8(0x0475)  /* State helper byte from R42 (masked) */

//=============================================================================
// Endpoint Configuration Work Area (0x0500-0x05FF)
//=============================================================================
#define G_NVME_PARAM_053A       XDATA_VAR8(0x053A)  /* NVMe parameter storage */
#define G_EP_CONFIG_BASE        XDATA_VAR8(0x054B)  /* EP config base */
#define G_EP_CONFIG_ARRAY       XDATA_VAR8(0x054E)  /* EP config array */
#define G_EP_QUEUE_CTRL         XDATA_VAR8(0x0564)  /* Endpoint queue control */
#define G_EP_QUEUE_STATUS       XDATA_VAR8(0x0565)  /* Endpoint queue status */
#define G_EP_QUEUE_PARAM        XDATA_VAR8(0x0566)  /* Endpoint queue parameter */
#define G_EP_QUEUE_IDATA        XDATA_VAR8(0x0567)  /* Endpoint queue IDATA copy */
#define G_BUF_OFFSET_HI         XDATA_VAR8(0x0568)  /* Buffer offset result high */
#define G_LOG_PROCESS_STATE     XDATA_VAR8(0x0574)  /* Log processing state */
#define G_LOG_ENTRY_VALUE       XDATA_VAR8(0x0575)  /* Log entry value */
#define G_BUF_OFFSET_LO         XDATA_VAR8(0x0569)  /* Buffer offset result low */
#define G_PCIE_TXN_COUNT_LO     XDATA_VAR8(0x05A6)  /* PCIe transaction count low */
#define G_PCIE_TXN_COUNT_HI     XDATA_VAR8(0x05A7)  /* PCIe transaction count high */
#define G_EP_CONFIG_05A8        XDATA_VAR8(0x05A8)  /* EP config 0x05A8 */
#define G_PCIE_DIRECTION        XDATA_VAR8(0x05AE)  /* PCIe direction (bit 0: 0=read, 1=write) */
#define G_PCIE_ADDR_0           XDATA_VAR8(0x05AF)  /* PCIe target address byte 0 */
#define G_PCIE_ADDR_1           XDATA_VAR8(0x05B0)  /* PCIe target address byte 1 */
#define G_PCIE_ADDR_2           XDATA_VAR8(0x05B1)  /* PCIe target address byte 2 */
#define G_PCIE_ADDR_3           XDATA_VAR8(0x05B2)  /* PCIe target address byte 3 */
#define G_EP_CONFIG_05F8        XDATA_VAR8(0x05F8)  /* EP config 0x05F8 */

//=============================================================================
// Transfer Work Area (0x0600-0x07FF)
//=============================================================================
#define G_MAX_LOG_ENTRIES       XDATA_VAR8(0x06E5)  /* Max error log entries */
#define G_STATE_FLAG_06E6       XDATA_VAR8(0x06E6)  /* Processing complete flag / error flag */
#define G_ERROR_CODE_06EA       XDATA_VAR8(0x06EA)  /* Error code */
#define G_MISC_FLAG_06EC        XDATA_VAR8(0x06EC)  /* Miscellaneous flag */
#define G_FLASH_CMD_FLAG        XDATA_VAR8(0x07B8)  /* Flash command flag */
#define G_FLASH_CMD_TYPE        XDATA_VAR8(0x07BC)  /* Flash command type (1,2,3) */
#define G_FLASH_OP_COUNTER      XDATA_VAR8(0x07BD)  /* Flash operation counter */
#define G_SYS_FLAGS_BASE        XDATA_VAR8(0x07E4)  /* Flags base */
#define G_TRANSFER_ACTIVE       XDATA_VAR8(0x07E5)  /* Transfer active flag */
#define G_SYS_FLAGS_07EC        XDATA_VAR8(0x07EC)  /* System flags 0x07EC */
#define G_SYS_FLAGS_07ED        XDATA_VAR8(0x07ED)  /* System flags 0x07ED */
#define G_SYS_FLAGS_07EE        XDATA_VAR8(0x07EE)  /* System flags 0x07EE */
#define G_SYS_FLAGS_07EF        XDATA_VAR8(0x07EF)  /* System flags 0x07EF */

//=============================================================================
// Event/Loop State Work Area (0x0900-0x09FF)
//=============================================================================
#define G_EVENT_INIT_097A       XDATA_VAR8(0x097A)  /* Event init value */
#define G_LOOP_CHECK_098E       XDATA_VAR8(0x098E)  /* Loop check byte */
#define G_LOOP_STATE_0991       XDATA_VAR8(0x0991)  /* Loop state byte */
#define G_EVENT_CHECK_09EF      XDATA_VAR8(0x09EF)  /* Event check byte */
#define G_EVENT_FLAGS           XDATA_VAR8(0x09F9)  /* Event flags */
#define G_EVENT_CTRL_09FA       XDATA_VAR8(0x09FA)  /* Event control */

//=============================================================================
// Endpoint Dispatch Work Area (0x0A00-0x0BFF)
//=============================================================================
#define G_LOOP_STATE            XDATA_VAR8(0x0A59)  /* Main loop state flag */
#define G_EP_DISPATCH_VAL1      XDATA_VAR8(0x0A7B)  /* Endpoint dispatch value 1 */
#define G_EP_DISPATCH_VAL2      XDATA_VAR8(0x0A7C)  /* Endpoint dispatch value 2 */
#define G_EP_DISPATCH_VAL3      XDATA_VAR8(0x0A7D)  /* Endpoint dispatch value 3 */
#define G_STATE_COUNTER_HI      XDATA_VAR8(0x0AA3)  /* State counter high */
#define G_STATE_COUNTER_LO      XDATA_VAR8(0x0AA4)  /* State counter low */
#define G_LOG_PROCESSED_INDEX   XDATA_VAR8(0x0AA1)  /* Current processed log index */
#define G_FLASH_ERROR_0         XDATA_VAR8(0x0AA8)  /* Flash error flag 0 */
#define G_FLASH_ERROR_1         XDATA_VAR8(0x0AA9)  /* Flash error flag 1 */
#define G_FLASH_RESET_0AAA      XDATA_VAR8(0x0AAA)  /* Flash reset flag */
#define G_FLASH_ADDR_0          XDATA_VAR8(0x0AAD)  /* Flash address byte 0 (low) */
#define G_FLASH_ADDR_1          XDATA_VAR8(0x0AAE)  /* Flash address byte 1 */
#define G_FLASH_ADDR_2          XDATA_VAR8(0x0AAF)  /* Flash address byte 2 */
#define G_FLASH_ADDR_3          XDATA_VAR8(0x0AB0)  /* Flash address byte 3 (high) */
#define G_FLASH_LEN_LO          XDATA_VAR8(0x0AB1)  /* Flash data length low */
#define G_FLASH_LEN_HI          XDATA_VAR8(0x0AB2)  /* Flash data length high */
#define G_SYSTEM_STATE_0AE2     XDATA_VAR8(0x0AE2)  /* System state */
#define G_STATE_FLAG_0AE3       XDATA_VAR8(0x0AE3)  /* System state flag */
#define G_STATE_CHECK_0AEE      XDATA_VAR8(0x0AEE)  /* State check byte */
#define G_STATE_FLAG_0AF1       XDATA_VAR8(0x0AF1)  /* State flag */
#define G_TRANSFER_FLAG_0AF2    XDATA_VAR8(0x0AF2)  /* Transfer flag 0x0AF2 */
#define G_EP_DISPATCH_OFFSET    XDATA_VAR8(0x0AF5)  /* Endpoint dispatch offset */
#define G_TRANSFER_PARAMS_HI    XDATA_VAR8(0x0AFA)  /* Transfer params high byte */
#define G_TRANSFER_PARAMS_LO    XDATA_VAR8(0x0AFB)  /* Transfer params low byte */
#define G_USB_PARAM_0B00        XDATA_VAR8(0x0B00)  /* USB parameter storage */
#define G_USB_TRANSFER_FLAG     XDATA_VAR8(0x0B2E)  /* USB transfer flag */
#define G_USB_STATE_0B41        XDATA_VAR8(0x0B41)  /* USB state check */

//=============================================================================
// USB/SCSI Buffer Area Control (0xD800-0xDFFF)
// Note: This is XRAM buffer area, not true MMIO registers
//=============================================================================
#define G_BUF_XFER_START        XDATA_VAR8(0xD80C)  /* Buffer transfer start */

//=============================================================================
// Command Engine Work Area (0x07B0-0x07FF)
//=============================================================================
#define G_CMD_SLOT_INDEX        XDATA_VAR8(0x07B7)  /* Command slot index (3-bit) */
#define G_CMD_OP_COUNTER        XDATA_VAR8(0x07BD)  /* Command operation counter */
#define G_CMD_STATE             XDATA_VAR8(0x07C3)  /* Command state (3-bit) */
#define G_CMD_STATUS            XDATA_VAR8(0x07C4)  /* Command status byte */
#define G_CMD_MODE              XDATA_VAR8(0x07CA)  /* Command mode (1=mode1, 2=mode2, 3=mode3) */
#define G_CMD_PARAM_0           XDATA_VAR8(0x07D3)  /* Command parameter 0 */
#define G_CMD_PARAM_1           XDATA_VAR8(0x07D4)  /* Command parameter 1 */
#define G_CMD_LBA_0             XDATA_VAR8(0x07DA)  /* Command LBA byte 0 (low) */
#define G_CMD_LBA_1             XDATA_VAR8(0x07DB)  /* Command LBA byte 1 */
#define G_CMD_LBA_2             XDATA_VAR8(0x07DC)  /* Command LBA byte 2 */
#define G_CMD_LBA_3             XDATA_VAR8(0x07DD)  /* Command LBA byte 3 (high) */

#endif /* __GLOBALS_H__ */
