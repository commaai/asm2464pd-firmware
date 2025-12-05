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
#define G_EP_CHECK_FLAG         XDATA_VAR8(0x000A)  /* Endpoint check flag */
#define G_SCSI_CTRL             XDATA_VAR8(0x0171)  /* SCSI control */

//=============================================================================
// DMA Work Area (0x0200-0x02FF)
//=============================================================================
#define G_DMA_MODE_SELECT       XDATA_VAR8(0x0203)  /* DMA mode select */
#define G_DMA_PARAM1            XDATA_VAR8(0x020D)  /* DMA parameter 1 */
#define G_DMA_PARAM2            XDATA_VAR8(0x020E)  /* DMA parameter 2 */
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

//=============================================================================
// Endpoint Configuration Work Area (0x0500-0x05FF)
//=============================================================================
#define G_EP_CONFIG_BASE        XDATA_VAR8(0x054B)  /* EP config base */
#define G_EP_CONFIG_ARRAY       XDATA_VAR8(0x054E)  /* EP config array */
#define G_EP_QUEUE_CTRL         XDATA_VAR8(0x0564)  /* Endpoint queue control */
#define G_EP_QUEUE_STATUS       XDATA_VAR8(0x0565)  /* Endpoint queue status */
#define G_BUF_OFFSET_HI         XDATA_VAR8(0x0568)  /* Buffer offset result high */
#define G_BUF_OFFSET_LO         XDATA_VAR8(0x0569)  /* Buffer offset result low */

//=============================================================================
// Transfer Work Area (0x0600-0x07FF)
//=============================================================================
#define G_STATE_FLAG_06E6       XDATA_VAR8(0x06E6)  /* Processing complete flag */
#define G_SYS_FLAGS_BASE        XDATA_VAR8(0x07E4)  /* Flags base */
#define G_TRANSFER_ACTIVE       XDATA_VAR8(0x07E5)  /* Transfer active flag */

//=============================================================================
// Event/Loop State Work Area (0x0900-0x09FF)
//=============================================================================
#define G_EVENT_FLAGS           XDATA_VAR8(0x09F9)  /* Event flags */

//=============================================================================
// Endpoint Dispatch Work Area (0x0A00-0x0BFF)
//=============================================================================
#define G_LOOP_STATE            XDATA_VAR8(0x0A59)  /* Main loop state flag */
#define G_EP_DISPATCH_VAL1      XDATA_VAR8(0x0A7B)  /* Endpoint dispatch value 1 */
#define G_EP_DISPATCH_VAL2      XDATA_VAR8(0x0A7C)  /* Endpoint dispatch value 2 */
#define G_STATE_COUNTER_HI      XDATA_VAR8(0x0AA3)  /* State counter high */
#define G_STATE_COUNTER_LO      XDATA_VAR8(0x0AA4)  /* State counter low */
#define G_SYSTEM_STATE_0AE2     XDATA_VAR8(0x0AE2)  /* System state */
#define G_STATE_FLAG_0AE3       XDATA_VAR8(0x0AE3)  /* System state flag */
#define G_EP_DISPATCH_OFFSET    XDATA_VAR8(0x0AF5)  /* Endpoint dispatch offset */
#define G_USB_TRANSFER_FLAG     XDATA_VAR8(0x0B2E)  /* USB transfer flag */

//=============================================================================
// USB/SCSI Buffer Area Control (0xD800-0xDFFF)
// Note: This is XRAM buffer area, not true MMIO registers
//=============================================================================
#define G_BUF_XFER_START        XDATA_VAR8(0xD80C)  /* Buffer transfer start */

#endif /* __GLOBALS_H__ */
