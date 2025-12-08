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
// IDATA Work Variables (0x00-0x7F)
//=============================================================================
/* These are IDATA (internal 8051 RAM) locations used as fast work variables.
 * With __at() absolute addressing, these can be defined in the header. */
/* Boot signature bytes (0x09-0x0C) - used in startup_0016 */
__idata __at(0x09) uint8_t I_BOOT_SIG_0;      /* Boot signature byte 0 */
__idata __at(0x0A) uint8_t I_BOOT_SIG_1;      /* Boot signature byte 1 */
__idata __at(0x0B) uint8_t I_BOOT_SIG_2;      /* Boot signature byte 2 */
__idata __at(0x0C) uint8_t I_BOOT_SIG_3;      /* Boot signature byte 3 */
__idata __at(0x0D) uint8_t I_QUEUE_IDX;       /* Queue index / endpoint offset */
__idata __at(0x11) uint8_t I_SCSI_TAG;        /* SCSI command tag */
__idata __at(0x12) uint8_t I_WORK_12;         /* Work variable 0x12 */
__idata __at(0x13) uint8_t I_WORK_13;         /* Work variable 0x13 */
__idata __at(0x14) uint8_t I_WORK_14;         /* Work variable 0x14 */
__idata __at(0x15) uint8_t I_WORK_15;         /* Work variable 0x15 */

/* IDATA pointers for SCSI command buffer (0x12-0x15) */
#define IDATA_SCSI_CMD_BUF    ((__idata uint8_t *)0x12)   /* SCSI cmd buffer pointer */
__idata __at(0x16) uint8_t I_CORE_STATE_L;    /* Core state low byte */
#define I_WORK_16 I_CORE_STATE_L              /* Alias for work variable 0x16 */
__idata __at(0x17) uint8_t I_CORE_STATE_H;    /* Core state high byte */
#define I_WORK_17 I_CORE_STATE_H              /* Alias for work variable 0x17 */
__idata __at(0x18) uint8_t I_WORK_18;         /* Work variable 0x18 */
__idata __at(0x19) uint8_t I_WORK_19;         /* Work variable 0x19 */
__idata __at(0x21) uint8_t I_LOG_INDEX;       /* Log index */
__idata __at(0x22) uint8_t I_WORK_22;         /* Work variable 0x22 - slot value */
__idata __at(0x23) uint8_t I_WORK_23;         /* Work variable 0x23 */
__idata __at(0x38) uint8_t I_WORK_38;         /* Work variable 0x38 */
__idata __at(0x39) uint8_t I_WORK_39;         /* Work variable 0x39 */
__idata __at(0x3A) uint8_t I_WORK_3A;         /* Work variable 0x3A */
__idata __at(0x3B) uint8_t I_WORK_3B;         /* Work variable 0x3B */
__idata __at(0x3C) uint8_t I_WORK_3C;         /* Work variable 0x3C */
__idata __at(0x3D) uint8_t I_WORK_3D;         /* Work variable 0x3D */
__idata __at(0x3E) uint8_t I_WORK_3E;         /* Work variable 0x3E */
__idata __at(0x3F) uint8_t I_WORK_3F;         /* Work variable 0x3F - transfer count */
__idata __at(0x40) uint8_t I_WORK_40;         /* Work variable 0x40 */
__idata __at(0x41) uint8_t I_WORK_41;         /* Work variable 0x41 */
__idata __at(0x42) uint8_t I_WORK_42;         /* Work variable 0x42 - tag status */
__idata __at(0x43) uint8_t I_WORK_43;         /* Work variable 0x43 - slot index */
__idata __at(0x44) uint8_t I_WORK_44;         /* Work variable 0x44 - multiplier */
__idata __at(0x45) uint8_t I_WORK_45;         /* Work variable 0x45 - chain index */
__idata __at(0x46) uint8_t I_WORK_46;         /* Work variable 0x46 - chain flag */
__idata __at(0x47) uint8_t I_WORK_47;         /* Work variable 0x47 - product cap */
__idata __at(0x51) uint8_t I_WORK_51;         /* Work variable 0x51 */
__idata __at(0x52) uint8_t I_WORK_52;         /* Work variable 0x52 */
__idata __at(0x53) uint8_t I_WORK_53;         /* Work variable 0x53 */
__idata __at(0x55) uint8_t I_WORK_55;         /* Work variable 0x55 */
__idata __at(0x63) uint8_t I_WORK_63;         /* Work variable 0x63 - EP config high byte */
__idata __at(0x64) uint8_t I_WORK_64;         /* Work variable 0x64 - EP config low byte */
__idata __at(0x65) uint8_t I_WORK_65;         /* Work variable 0x65 - EP mode */
__idata __at(0x6A) uint8_t I_STATE_6A;        /* State machine variable */
__idata __at(0x6B) uint8_t I_TRANSFER_6B;     /* Transfer pending byte 0 */
__idata __at(0x6C) uint8_t I_TRANSFER_6C;     /* Transfer pending byte 1 */
__idata __at(0x6D) uint8_t I_TRANSFER_6D;     /* Transfer pending byte 2 */
__idata __at(0x6E) uint8_t I_TRANSFER_6E;     /* Transfer pending byte 3 */
__idata __at(0x6F) uint8_t I_BUF_FLOW_CTRL;   /* Buffer flow control */
__idata __at(0x70) uint8_t I_BUF_THRESH_LO;   /* Buffer threshold low */
__idata __at(0x71) uint8_t I_BUF_THRESH_HI;   /* Buffer threshold high */
__idata __at(0x72) uint8_t I_BUF_CTRL_GLOBAL; /* Buffer control global */

/* IDATA pointers for SCSI register-based operations */
#define IDATA_CMD_BUF     ((__idata uint8_t *)0x09)   /* Command buffer pointer */
#define IDATA_TRANSFER    ((__idata uint8_t *)0x6B)   /* Transfer data pointer */
#define IDATA_BUF_CTRL    ((__idata uint8_t *)0x6F)   /* Buffer control pointer */

//=============================================================================
// System Work Area (0x0000-0x01FF)
//=============================================================================
#define G_SYSTEM_CTRL           XDATA_VAR8(0x0000)  /* System control byte */
#define G_IO_CMD_TYPE           XDATA_VAR8(0x0001)  /* I/O command type byte */
#define G_IO_CMD_STATE          XDATA_VAR8(0x0002)  /* I/O command state byte */
#define G_EP_STATUS_CTRL        XDATA_VAR8(0x0003)  /* Endpoint status control (checked by usb_ep_process) */
#define G_WORK_0006             XDATA_VAR8(0x0006)  /* Work variable 0x0006 */
#define G_WORK_0007             XDATA_VAR8(0x0007)  /* Work variable 0x0007 */
#define G_USB_CTRL_000A         XDATA_VAR8(0x000A)  /* USB control byte (increment counter) */
#define G_EP_CHECK_FLAG         G_USB_CTRL_000A     /* Alias: Endpoint check flag */
#define G_ENDPOINT_STATE_0051   XDATA_VAR8(0x0051)  /* Endpoint state storage */
#define G_SYS_FLAGS_0052        XDATA_VAR8(0x0052)  /* System flags 0x0052 */
#define G_USB_SETUP_RESULT      XDATA_VAR8(0x0053)  /* USB setup result storage */
#define G_BUFFER_LENGTH_HIGH    XDATA_VAR8(0x0054)  /* Buffer length high byte (for mode 4) */
#define G_NVME_QUEUE_READY      XDATA_VAR8(0x0055)  /* NVMe queue ready flag */
#define G_USB_ADDR_HI_0056      XDATA_VAR8(0x0056)  /* USB address high 0x0056 */
#define G_USB_ADDR_LO_0057      XDATA_VAR8(0x0057)  /* USB address low 0x0057 */
#define G_USB_WORK_009F         XDATA_VAR8(0x009F)  /* USB work array base */
#define G_INIT_STATE_00C2       XDATA_VAR8(0x00C2)  /* Initialization state flag */
#define G_INIT_STATE_00E5       XDATA_VAR8(0x00E5)  /* Initialization state flag 2 */
#define G_USB_INDEX_COUNTER     XDATA_VAR8(0x014E)  /* USB index counter (5-bit) */
#define G_SCSI_CTRL             XDATA_VAR8(0x0171)  /* SCSI control */
#define G_USB_WORK_01B4         XDATA_VAR8(0x01B4)  /* USB work variable 0x01B4 */
#define G_USB_WORK_01B6         XDATA_VAR8(0x01B6)  /* USB work variable 0x01B6 */

//=============================================================================
// DMA Work Area (0x0200-0x02FF)
//=============================================================================
#define G_DMA_MODE_SELECT       XDATA_VAR8(0x0203)  /* DMA mode select */
#define G_FLASH_READ_TRIGGER    XDATA_VAR8(0x0213)  /* Flash read trigger */
#define G_DMA_STATE_0214        XDATA_VAR8(0x0214)  /* DMA state/status */
#define G_DMA_PARAM1            XDATA_VAR8(0x020D)  /* DMA parameter 1 */
#define G_DMA_PARAM2            XDATA_VAR8(0x020E)  /* DMA parameter 2 */
#define G_DMA_WORK_0216         XDATA_VAR8(0x0216)  /* DMA work variable 0x0216 */
#define G_DMA_OFFSET            XDATA_VAR8(0x0217)  /* DMA offset storage */
#define G_BUF_ADDR_HI           XDATA_VAR8(0x0218)  /* Buffer address high */
#define G_BUF_ADDR_LO           XDATA_VAR8(0x0219)  /* Buffer address low */
#define G_BUF_BASE_HI           XDATA_VAR8(0x021A)  /* Buffer base address high */
#define G_BUF_BASE_LO           XDATA_VAR8(0x021B)  /* Buffer base address low */
#define G_BANK1_STATE_023F      XDATA_VAR8(0x023F)  /* Bank 1 state flag */

//=============================================================================
// System Status Work Area (0x0400-0x04FF)
//=============================================================================
#define G_LOG_COUNTER_044B      XDATA_VAR8(0x044B)  /* Log counter */
#define G_LOG_ACTIVE_044C       XDATA_VAR8(0x044C)  /* Log active flag */
#define G_LOG_INIT_044D         XDATA_VAR8(0x044D)  /* Log init flag */
#define G_REG_WAIT_BIT          XDATA_VAR8(0x045E)  /* Register wait bit */
#define G_SYS_STATUS_PRIMARY    XDATA_VAR8(0x0464)  /* Primary system status */
#define G_EP_INDEX_ALT          G_SYS_STATUS_PRIMARY  /* Alias for endpoint index */
#define G_SYS_STATUS_SECONDARY  XDATA_VAR8(0x0465)  /* Secondary system status */
#define G_EP_INDEX              G_SYS_STATUS_SECONDARY  /* Alias for endpoint index */
#define G_SYSTEM_CONFIG         XDATA_VAR8(0x0466)  /* System configuration */
#define G_SYSTEM_STATE          XDATA_VAR8(0x0467)  /* System state */
#define G_DATA_PORT             XDATA_VAR8(0x0468)  /* Data port */
#define G_INT_STATUS            XDATA_VAR8(0x0469)  /* Interrupt status */
#define G_SCSI_CMD_PARAM_0470   XDATA_VAR8(0x0470)  /* SCSI command parameter */
#define G_DMA_LOAD_PARAM1       XDATA_VAR8(0x0472)  /* DMA load parameter 1 */
#define G_DMA_LOAD_PARAM2       XDATA_VAR8(0x0473)  /* DMA load parameter 2 */
#define G_STATE_HELPER_41       XDATA_VAR8(0x0474)  /* State helper byte from R41 */
#define G_STATE_HELPER_42       XDATA_VAR8(0x0475)  /* State helper byte from R42 (masked) */
#define G_XFER_DIV_0476         XDATA_VAR8(0x0476)  /* Transfer division result */

//=============================================================================
// Endpoint Configuration Work Area (0x0500-0x05FF)
//=============================================================================
#define G_EP_INIT_0517          XDATA_VAR8(0x0517)  /* Endpoint init state */
#define G_NVME_PARAM_053A       XDATA_VAR8(0x053A)  /* NVMe parameter storage */
#define G_NVME_STATE_053B       XDATA_VAR8(0x053B)  /* NVMe state flag */
#define G_SCSI_CMD_TYPE         XDATA_VAR8(0x053D)  /* SCSI command type */
#define G_SCSI_TRANSFER_FLAG    XDATA_VAR8(0x053E)  /* SCSI transfer flag */
#define G_SCSI_BUF_LEN_0        XDATA_VAR8(0x053F)  /* SCSI buffer length byte 0 */
#define G_SCSI_BUF_LEN_1        XDATA_VAR8(0x0540)  /* SCSI buffer length byte 1 */
#define G_SCSI_BUF_LEN_2        XDATA_VAR8(0x0541)  /* SCSI buffer length byte 2 */
#define G_SCSI_BUF_LEN_3        XDATA_VAR8(0x0542)  /* SCSI buffer length byte 3 */
#define G_SCSI_LBA_0            XDATA_VAR8(0x0543)  /* SCSI LBA byte 0 */
#define G_SCSI_LBA_1            XDATA_VAR8(0x0544)  /* SCSI LBA byte 1 */
#define G_SCSI_LBA_2            XDATA_VAR8(0x0545)  /* SCSI LBA byte 2 */
#define G_SCSI_LBA_3            XDATA_VAR8(0x0546)  /* SCSI LBA byte 3 */
#define G_SCSI_DEVICE_IDX       XDATA_VAR8(0x0547)  /* SCSI device index */
#define G_EP_CONFIG_BASE        XDATA_VAR8(0x054B)  /* EP config base */
#define G_EP_CONFIG_ARRAY       XDATA_VAR8(0x054E)  /* EP config array */
#define G_SCSI_MODE_FLAG        XDATA_VAR8(0x054F)  /* SCSI mode flag */
#define G_SCSI_STATUS_FLAG      XDATA_VAR8(0x0552)  /* SCSI status flag */
#define G_EP_QUEUE_CTRL         XDATA_VAR8(0x0564)  /* Endpoint queue control */
#define G_EP_QUEUE_STATUS       XDATA_VAR8(0x0565)  /* Endpoint queue status */
#define G_EP_QUEUE_PARAM        XDATA_VAR8(0x0566)  /* Endpoint queue parameter */
#define G_EP_QUEUE_IDATA        XDATA_VAR8(0x0567)  /* Endpoint queue IDATA copy */
#define G_BUF_OFFSET_HI         XDATA_VAR8(0x0568)  /* Buffer offset result high */
#define G_BUF_OFFSET_LO         XDATA_VAR8(0x0569)  /* Buffer offset result low */
#define G_EP_QUEUE_IDATA2       XDATA_VAR8(0x056A)  /* Endpoint queue IDATA byte 2 */
#define G_EP_QUEUE_IDATA3       XDATA_VAR8(0x056B)  /* Endpoint queue IDATA byte 3 */
#define G_LOG_PROCESS_STATE     XDATA_VAR8(0x0574)  /* Log processing state */
#define G_LOG_ENTRY_VALUE       XDATA_VAR8(0x0575)  /* Log entry value */
#define G_PCIE_TXN_COUNT_LO     XDATA_VAR8(0x05A6)  /* PCIe transaction count low */
#define G_PCIE_TXN_COUNT_HI     XDATA_VAR8(0x05A7)  /* PCIe transaction count high */
#define G_EP_CONFIG_05A8        XDATA_VAR8(0x05A8)  /* EP config 0x05A8 */
#define G_PCIE_DIRECTION        XDATA_VAR8(0x05AE)  /* PCIe direction (bit 0: 0=read, 1=write) */
#define G_PCIE_ADDR_0           XDATA_VAR8(0x05AF)  /* PCIe target address byte 0 */
#define G_PCIE_ADDR_1           XDATA_VAR8(0x05B0)  /* PCIe target address byte 1 */
#define G_PCIE_ADDR_2           XDATA_VAR8(0x05B1)  /* PCIe target address byte 2 */
#define G_PCIE_ADDR_3           XDATA_VAR8(0x05B2)  /* PCIe target address byte 3 */
#define G_EP_LOOKUP_TABLE       XDATA_VAR8(0x057A)  /* EP lookup table index */
#define G_EP_CONFIG_05F8        XDATA_VAR8(0x05F8)  /* EP config 0x05F8 */

//=============================================================================
// Transfer Work Area (0x0600-0x07FF)
//=============================================================================
#define G_DMA_WORK_05AC         XDATA_VAR8(0x05AC)  /* DMA work byte */
#define G_DMA_WORK_05AD         XDATA_VAR8(0x05AD)  /* DMA work byte */
#define G_MAX_LOG_ENTRIES       XDATA_VAR8(0x06E5)  /* Max error log entries */
#define G_STATE_FLAG_06E6       XDATA_VAR8(0x06E6)  /* Processing complete flag / error flag */
#define G_SCSI_STATUS_06CB      XDATA_VAR8(0x06CB)  /* SCSI status byte */
#define G_WORK_06E7             XDATA_VAR8(0x06E7)  /* Work variable 0x06E7 */
#define G_WORK_06E8             XDATA_VAR8(0x06E8)  /* Work variable 0x06E8 */
#define G_ERROR_CODE_06EA       XDATA_VAR8(0x06EA)  /* Error code */
#define G_WORK_06EB             XDATA_VAR8(0x06EB)  /* Work variable 0x06EB */
#define G_MISC_FLAG_06EC        XDATA_VAR8(0x06EC)  /* Miscellaneous flag */
#define G_FLASH_CMD_FLAG        XDATA_VAR8(0x07B8)  /* Flash command flag */
#define G_FLASH_CMD_TYPE        XDATA_VAR8(0x07BC)  /* Flash command type (1,2,3) */
#define G_FLASH_OP_COUNTER      XDATA_VAR8(0x07BD)  /* Flash operation counter */
#define G_SYS_FLAGS_BASE        XDATA_VAR8(0x07E4)  /* Flags base */
#define G_TRANSFER_ACTIVE       XDATA_VAR8(0x07E5)  /* Transfer active flag */
#define G_XFER_FLAG_07EA        XDATA_VAR8(0x07EA)  /* Transfer flag 0x07EA (set in SCSI DMA) */
#define G_SYS_FLAGS_07EB        XDATA_VAR8(0x07EB)  /* System flags 0x07EB */
#define G_SYS_FLAGS_07EC        XDATA_VAR8(0x07EC)  /* System flags 0x07EC */
#define G_SYS_FLAGS_07ED        XDATA_VAR8(0x07ED)  /* System flags 0x07ED */
#define G_SYS_FLAGS_07EE        XDATA_VAR8(0x07EE)  /* System flags 0x07EE */
#define G_SYS_FLAGS_07EF        XDATA_VAR8(0x07EF)  /* System flags 0x07EF */
#define G_SYS_FLAGS_07F0        XDATA_VAR8(0x07F0)  /* System flags 0x07F0 */
#define G_SYS_FLAGS_07F1        XDATA_VAR8(0x07F1)  /* System flags 0x07F1 */
#define G_SYS_FLAGS_07F2        XDATA_VAR8(0x07F2)  /* System flags 0x07F2 */
#define G_SYS_FLAGS_07F3        XDATA_VAR8(0x07F3)  /* System flags 0x07F3 */
#define G_SYS_FLAGS_07F4        XDATA_VAR8(0x07F4)  /* System flags 0x07F4 */
#define G_SYS_FLAGS_07F5        XDATA_VAR8(0x07F5)  /* System flags 0x07F5 */
#define G_SYS_FLAGS_07F6        XDATA_VAR8(0x07F6)  /* System flags 0x07F6 */
#define G_SYS_FLAGS_07E8        XDATA_VAR8(0x07E8)  /* System flags 0x07E8 */
#define G_TLP_STATE_07E9        XDATA_VAR8(0x07E9)  /* TLP state / queue status */
#define G_SYS_FLAGS_07F7        XDATA_VAR8(0x07F7)  /* System flags 0x07F7 */

//=============================================================================
// Flash Config Storage (0x0860-0x08FF)
// Loaded from flash buffer at 0x7076+ during config initialization
//=============================================================================
#define G_FLASH_CFG_086E        XDATA_VAR8(0x086E)  /* Flash config from 0x7076 */
#define G_FLASH_CFG_086F        XDATA_VAR8(0x086F)  /* Flash config from 0x7077 */
#define G_FLASH_CFG_0870        XDATA_VAR8(0x0870)  /* Flash config from 0x7078 */
#define G_FLASH_CFG_0871        XDATA_VAR8(0x0871)  /* Flash config from 0x7079 */

//=============================================================================
// Event/Loop State Work Area (0x0900-0x09FF)
//=============================================================================
#define G_EVENT_INIT_097A       XDATA_VAR8(0x097A)  /* Event init value */
#define G_LOOP_CHECK_098E       XDATA_VAR8(0x098E)  /* Loop check byte */
#define G_LOOP_STATE_0991       XDATA_VAR8(0x0991)  /* Loop state byte */
#define G_EVENT_CHECK_09EF      XDATA_VAR8(0x09EF)  /* Event check byte */
#define G_FLASH_MODE_1          XDATA_VAR8(0x09F4)  /* Flash mode config 1 */
#define G_FLASH_MODE_2          XDATA_VAR8(0x09F5)  /* Flash mode config 2 */
#define G_FLASH_MODE_3          XDATA_VAR8(0x09F6)  /* Flash mode config 3 */
#define G_FLASH_MODE_4          XDATA_VAR8(0x09F7)  /* Flash mode config 4 */
#define G_FLASH_MODE_5          XDATA_VAR8(0x09F8)  /* Flash mode config 5 */
#define G_EVENT_FLAGS           XDATA_VAR8(0x09F9)  /* Event flags */
#define   EVENT_FLAG_PENDING      0x01  // Bit 0: Event pending
#define   EVENT_FLAG_PROCESS      0x02  // Bit 1: Process event
#define   EVENT_FLAG_POWER        0x04  // Bit 2: Power event
#define   EVENT_FLAG_ACTIVE       0x80  // Bit 7: Events active
#define   EVENT_FLAGS_ANY         0x83  // Bits 0,1,7: Any event flag
#define G_EVENT_CTRL_09FA       XDATA_VAR8(0x09FA)  /* Event control */

//=============================================================================
// PCIe Tunnel Adapter Config (0x0A52-0x0A55)
//=============================================================================
#define G_PCIE_ADAPTER_CFG_LO   XDATA_VAR8(0x0A52)  /* Adapter config low (link config high) */
#define G_PCIE_ADAPTER_CFG_HI   XDATA_VAR8(0x0A53)  /* Adapter config high (link config low) */
#define G_PCIE_ADAPTER_MODE     XDATA_VAR8(0x0A54)  /* Adapter mode config */
#define G_PCIE_ADAPTER_AUX      XDATA_VAR8(0x0A55)  /* Adapter auxiliary config */
#define G_FLASH_CONFIG_VALID    XDATA_VAR8(0x0A56)  /* Flash config valid flag */
#define G_CMD_CTRL_PARAM        XDATA_VAR8(0x0A57)  /* Command control parameter for E430 */
#define G_CMD_TIMEOUT_PARAM     XDATA_VAR8(0x0A58)  /* Command timeout parameter for E431 */

//=============================================================================
// Endpoint Dispatch Work Area (0x0A00-0x0BFF)
//=============================================================================
#define G_LOOP_STATE            XDATA_VAR8(0x0A59)  /* Main loop state flag */
#define G_LOOP_STATE_0A5A       XDATA_VAR8(0x0A5A)  /* Main loop state secondary */
#define G_ACTION_CODE_0A83      XDATA_VAR8(0x0A83)  /* Action code storage for state_action_dispatch */
#define   ACTION_CODE_EXTENDED    0x02  // Bit 1: Extended mode flag
#define G_ACTION_PARAM_0A84     XDATA_VAR8(0x0A84)  /* Action parameter (byte after action code) */
#define G_DMA_PARAM_0A8D        XDATA_VAR8(0x0A8D)  /* DMA parameter storage */
#define G_DMA_MODE_0A8E         XDATA_VAR8(0x0A8E)  /* DMA mode storage */
#define G_EP_DISPATCH_VAL1      XDATA_VAR8(0x0A7B)  /* Endpoint dispatch value 1 */
#define G_EP_DISPATCH_VAL2      XDATA_VAR8(0x0A7C)  /* Endpoint dispatch value 2 */
#define G_EP_DISPATCH_VAL3      XDATA_VAR8(0x0A7D)  /* Endpoint dispatch value 3 */
#define G_USB_EP_MODE           G_EP_DISPATCH_VAL3  /* Alias: USB endpoint mode flag */
#define G_EP_DISPATCH_VAL4      XDATA_VAR8(0x0A7E)  /* Endpoint dispatch value 4 */
#define G_STATE_COUNTER_HI      XDATA_VAR8(0x0AA3)  /* State counter high */
#define G_STATE_COUNTER_LO      XDATA_VAR8(0x0AA4)  /* State counter low */
#define G_STATE_COUNTER_0AA5    XDATA_VAR8(0x0AA5)  /* State counter byte 2 */
#define G_LOG_PROCESSED_INDEX   XDATA_VAR8(0x0AA1)  /* Current processed log index */
#define G_STATE_PARAM_0AA2      XDATA_VAR8(0x0AA2)  /* State machine parameter */
#define G_STATE_RESULT_0AA3     XDATA_VAR8(0x0AA3)  /* State machine result */
#define G_STATE_WORK_0A84       XDATA_VAR8(0x0A84)  /* State work variable */
/* TLP handler state variables (also used for flash operations) */
#define G_TLP_COUNT_HI          XDATA_VAR8(0x0AA8)  /* TLP transfer count high byte */
#define G_TLP_COUNT_LO          XDATA_VAR8(0x0AA9)  /* TLP transfer count low byte */
#define G_TLP_STATUS            XDATA_VAR8(0x0AAA)  /* TLP status / pending count */
/* Legacy names for flash compatibility */
#define G_FLASH_ERROR_0         G_TLP_COUNT_HI      /* Alias: Flash error flag 0 */
#define G_FLASH_ERROR_1         G_TLP_COUNT_LO      /* Alias: Flash error flag 1 */
#define G_FLASH_RESET_0AAA      G_TLP_STATUS        /* Alias: Flash reset flag */
#define G_STATE_HELPER_0AAB     XDATA_VAR8(0x0AAB)  /* State helper variable */
#define G_STATE_COUNTER_0AAC    XDATA_VAR8(0x0AAC)  /* State counter/index */
#define G_FLASH_ADDR_0          XDATA_VAR8(0x0AAD)  /* Flash address byte 0 (low) */
#define G_FLASH_ADDR_1          XDATA_VAR8(0x0AAE)  /* Flash address byte 1 */
#define G_FLASH_ADDR_2          XDATA_VAR8(0x0AAF)  /* Flash address byte 2 */
#define G_FLASH_ADDR_3          XDATA_VAR8(0x0AB0)  /* Flash address byte 3 (high) */
#define G_FLASH_LEN_LO          XDATA_VAR8(0x0AB1)  /* Flash data length low */
#define G_FLASH_LEN_HI          XDATA_VAR8(0x0AB2)  /* Flash data length high */
#define G_STATE_0AB6            XDATA_VAR8(0x0AB6)  /* State control 0x0AB6 */
#define G_SYSTEM_STATE_0AE2     XDATA_VAR8(0x0AE2)  /* System state */
#define G_STATE_FLAG_0AE3       XDATA_VAR8(0x0AE3)  /* System state flag */
#define G_PHY_LANE_CFG_0AE4     XDATA_VAR8(0x0AE4)  /* PHY lane configuration */
#define G_TLP_INIT_FLAG_0AE5    XDATA_VAR8(0x0AE5)  /* TLP init complete flag */
#define G_LINK_SPEED_MODE_0AE6  XDATA_VAR8(0x0AE6)  /* Link speed mode */
#define G_LINK_CFG_BIT_0AE7     XDATA_VAR8(0x0AE7)  /* Link config bit (from 0x707D bit 3) */
#define G_STATE_0AE8            XDATA_VAR8(0x0AE8)  /* State control 0x0AE8 */
#define G_STATE_0AE9            XDATA_VAR8(0x0AE9)  /* State control 0x0AE9 */
#define G_FLASH_CFG_0AEA        XDATA_VAR8(0x0AEA)  /* Flash config 0x0AEA (from 0x707D bit 0) */
#define G_LINK_CFG_0AEB         XDATA_VAR8(0x0AEB)  /* Link config 0x0AEB */
#define G_PHY_CFG_0AEC          XDATA_VAR8(0x0AEC)  /* PHY config 0x0AEC */
#define G_PHY_CFG_0AED          XDATA_VAR8(0x0AED)  /* PHY config 0x0AED */
#define G_STATE_CHECK_0AEE      XDATA_VAR8(0x0AEE)  /* State check byte */
#define G_LINK_CFG_0AEF         XDATA_VAR8(0x0AEF)  /* Link config 0x0AEF */
#define G_FLASH_CFG_0AF0        XDATA_VAR8(0x0AF0)  /* Flash config 0x0AF0 */
#define G_STATE_FLAG_0AF1       XDATA_VAR8(0x0AF1)  /* State flag */
#define   STATE_FLAG_INIT         0x02  // Bit 1: Init state flag
#define   STATE_FLAG_PHY_READY    0x20  // Bit 5: PHY link ready
#define G_TRANSFER_FLAG_0AF2    XDATA_VAR8(0x0AF2)  /* Transfer flag 0x0AF2 */
#define G_EP_DISPATCH_OFFSET    XDATA_VAR8(0x0AF5)  /* Endpoint dispatch offset */
#define G_XFER_STATE_0AF6       XDATA_VAR8(0x0AF6)  /* Transfer state 0x0AF6 */
#define G_XFER_CTRL_0AF7        XDATA_VAR8(0x0AF7)  /* Transfer control 0x0AF7 */
#define G_POWER_INIT_FLAG       XDATA_VAR8(0x0AF8)  /* Power init flag (set to 0 in usb_power_init) */
#define G_TRANSFER_FLAG_0AF8    G_POWER_INIT_FLAG   /* Alias: Transfer flag for USB loop */
#define G_XFER_MODE_0AF9        XDATA_VAR8(0x0AF9)  /* Transfer mode/state: 1=mode1, 2=mode2 */
#define G_TRANSFER_PARAMS_HI    XDATA_VAR8(0x0AFA)  /* Transfer params high byte */
#define G_TRANSFER_PARAMS_LO    XDATA_VAR8(0x0AFB)  /* Transfer params low byte */
#define G_XFER_COUNT_LO         XDATA_VAR8(0x0AFC)  /* Transfer counter low byte */
#define G_XFER_COUNT_HI         XDATA_VAR8(0x0AFD)  /* Transfer counter high byte */
#define G_XFER_RETRY_CNT        XDATA_VAR8(0x0AFE)  /* Transfer retry counter */
#define G_USB_PARAM_0B00        XDATA_VAR8(0x0B00)  /* USB parameter storage */
#define G_USB_INIT_0B01         XDATA_VAR8(0x0B01)  /* USB init state flag */
#define G_TLP_PENDING_0B21      XDATA_VAR8(0x0B21)  /* TLP pending count / DMA control */
#define G_USB_TRANSFER_FLAG     XDATA_VAR8(0x0B2E)  /* USB transfer flag */
#define G_INTERFACE_READY_0B2F  XDATA_VAR8(0x0B2F)  /* Interface ready flag */
#define G_STATE_0B39            XDATA_VAR8(0x0B39)  /* State control 0x0B39 */
#define G_TRANSFER_BUSY_0B3B    XDATA_VAR8(0x0B3B)  /* Transfer busy flag */
#define G_STATE_CTRL_0B3C       XDATA_VAR8(0x0B3C)  /* State control 0x0B3C */
#define G_USB_STATE_0B41        XDATA_VAR8(0x0B41)  /* USB state check */
#define G_BUFFER_STATE_0AA6     XDATA_VAR8(0x0AA6)  /* Buffer state flags */
#define G_BUFFER_STATE_0AA7     XDATA_VAR8(0x0AA7)  /* Buffer state control */
#define G_STATE_CTRL_0B3E       XDATA_VAR8(0x0B3E)  /* State control 0x0B3E */
#define G_STATE_CTRL_0B3F       XDATA_VAR8(0x0B3F)  /* State control 0x0B3F */
#define G_DMA_ENDPOINT_0578     XDATA_VAR8(0x0578)  /* DMA endpoint control */
#define G_XFER_STATE_0AF3       XDATA_VAR8(0x0AF3)  /* Transfer state / direction (bit 7) */
#define G_XFER_LUN_0AF4         XDATA_VAR8(0x0AF4)  /* Transfer LUN (bits 0-3) */

//=============================================================================
// State Machine Work Area (0x0A80-0x0ABF)
//=============================================================================
#define G_STATE_WORK_0A85       XDATA_VAR8(0x0A85)  /* State machine temp storage */
#define G_STATE_WORK_0A86       XDATA_VAR8(0x0A86)  /* State machine counter */
#define G_STATE_WORK_0B3D       XDATA_VAR8(0x0B3D)  /* State machine flag 0B3D */
#define G_STATE_WORK_0B3E       XDATA_VAR8(0x0B3E)  /* State machine flag 0B3E */
#define G_STATE_WORK_002D       XDATA_VAR8(0x002D)  /* System work byte 0x2D */

//=============================================================================
// Flash Buffer Area Control (0x7000-0x7FFF)
//=============================================================================
#define G_FLASH_BUF_BASE        XDATA_VAR8(0x7000)  /* Flash buffer base */
#define G_FLASH_BUF_7004        XDATA_VAR8(0x7004)  /* Flash buffer config start */
#define G_FLASH_BUF_702C        XDATA_VAR8(0x702C)  /* Flash buffer serial start */
#define G_FLASH_BUF_705C        XDATA_VAR8(0x705C)  /* Flash buffer data 1 */
#define G_FLASH_BUF_705D        XDATA_VAR8(0x705D)  /* Flash buffer data 2 */
#define G_FLASH_BUF_705E        XDATA_VAR8(0x705E)  /* Flash buffer data 3 */
#define G_FLASH_BUF_705F        XDATA_VAR8(0x705F)  /* Flash buffer data 4 */
#define G_FLASH_BUF_7076        XDATA_VAR8(0x7076)  /* Flash config source byte 0 */
#define G_FLASH_BUF_7077        XDATA_VAR8(0x7077)  /* Flash config source byte 1 */
#define G_FLASH_BUF_7078        XDATA_VAR8(0x7078)  /* Flash config source byte 2 */
#define G_FLASH_BUF_7079        XDATA_VAR8(0x7079)  /* Flash config source byte 3 */
#define G_FLASH_BUF_707A        XDATA_VAR8(0x707A)  /* Flash config source byte 4 */
#define G_FLASH_BUF_707B        XDATA_VAR8(0x707B)  /* Flash buffer byte 0x707B */
#define G_FLASH_BUF_707C        XDATA_VAR8(0x707C)  /* Flash buffer byte 0x707C */
#define G_FLASH_BUF_707D        XDATA_VAR8(0x707D)  /* Flash buffer byte 0x707D */
#define G_FLASH_BUF_707E        XDATA_VAR8(0x707E)  /* Flash header marker */
#define G_FLASH_BUF_707F        XDATA_VAR8(0x707F)  /* Flash checksum */

//=============================================================================
// Work Variables 0x0A5x-0x0A9x
//=============================================================================
#define G_EP_CFG_FLAG_0A5B      XDATA_VAR8(0x0A5B)  /* EP config flag (set to 1 by 0x99c7) */
#define G_NIBBLE_SWAP_0A5B      G_EP_CFG_FLAG_0A5B  /* Alias for nibble_swap_helper */
#define G_EP_CFG_0A5C           XDATA_VAR8(0x0A5C)  /* EP config value */
#define G_NIBBLE_SWAP_0A5C      G_EP_CFG_0A5C       /* Alias for nibble_swap_helper */
#define G_EP_CFG_0A5D           XDATA_VAR8(0x0A5D)  /* EP config value */
#define G_EP_CFG_0A5E           XDATA_VAR8(0x0A5E)  /* EP config value (cleared by 0x9741) */
#define G_EP_CFG_0A5F           XDATA_VAR8(0x0A5F)  /* EP config value (cleared by 0x9741) */
#define G_EP_CFG_0A60           XDATA_VAR8(0x0A60)  /* EP config value (cleared by 0x9741) */
#define G_LANE_STATE_0A9D       XDATA_VAR8(0x0A9D)  /* Lane state value */
#define G_TLP_MASK_0ACB         XDATA_VAR8(0x0ACB)  /* TLP mask value */
#define G_TLP_BLOCK_SIZE_0ACC   XDATA_VAR8(0x0ACC)  /* TLP block size (double = 2x) */
#define G_TLP_STATE_0ACF        XDATA_VAR8(0x0ACF)  /* TLP state (low 5 bits) */
#define G_TLP_CMD_STATE_0AD0    XDATA_VAR8(0x0AD0)  /* TLP command state */
#define G_LINK_STATE_0AD1       XDATA_VAR8(0x0AD1)  /* Link state counter */
#define G_USB_DESC_FLAG_0ACD    XDATA_VAR8(0x0ACD)  /* USB descriptor flag */
#define G_USB_DESC_MODE_0ACE    XDATA_VAR8(0x0ACE)  /* USB descriptor mode */
#define G_LINK_STATE_0AD2       XDATA_VAR8(0x0AD2)  /* Link state flag */
#define G_TLP_MODE_0AD3         XDATA_VAR8(0x0AD3)  /* TLP mode flag */
#define G_TLP_ADDR_OFFSET_HI    XDATA_VAR8(0x0AD5)  /* TLP address offset high */
#define G_TLP_ADDR_OFFSET_LO    XDATA_VAR8(0x0AD6)  /* TLP address offset low */
#define G_TLP_COUNT_0AD7        XDATA_VAR8(0x0AD7)  /* TLP iteration count */
#define G_TLP_TIMEOUT_HI        XDATA_VAR8(0x0AD8)  /* TLP timeout counter high */
#define G_TLP_TIMEOUT_LO        XDATA_VAR8(0x0AD9)  /* TLP timeout counter low */
#define G_TLP_TRANSFER_HI       XDATA_VAR8(0x0ADA)  /* TLP transfer address high */
#define G_TLP_TRANSFER_LO       XDATA_VAR8(0x0ADB)  /* TLP transfer address low */
#define G_TLP_COMPUTED_HI       XDATA_VAR8(0x0ADC)  /* TLP computed value high */
#define G_TLP_COMPUTED_LO       XDATA_VAR8(0x0ADD)  /* TLP computed value low */
#define G_TLP_LIMIT_HI          XDATA_VAR8(0x0ADE)  /* TLP limit/max high */
#define G_TLP_LIMIT_LO          XDATA_VAR8(0x0ADF)  /* TLP limit/max low */
#define G_TLP_BASE_HI           XDATA_VAR8(0x0AE0)  /* TLP buffer base high */
#define G_TLP_BASE_LO           XDATA_VAR8(0x0AE1)  /* TLP buffer base low */

//=============================================================================
// Timer/Init Control 0x0B40
//=============================================================================
#define G_TIMER_INIT_0B40       XDATA_VAR8(0x0B40)  /* Timer init control */
#define G_PCIE_CTRL_SAVE_0B44   XDATA_VAR8(0x0B44)  /* PCIe control saved state */

//=============================================================================
// USB/SCSI Buffer Area Control (0xD800-0xDFFF)
// Note: This is XRAM buffer area, not true MMIO registers
//=============================================================================
#define G_BUF_XFER_START        XDATA_VAR8(0xD80C)  /* Buffer transfer start */

//=============================================================================
// Command Engine Work Area (0x07B0-0x07FF)
//=============================================================================
#define G_CMD_SLOT_INDEX        XDATA_VAR8(0x07B7)  /* Command slot index (3-bit) */
#define G_CMD_PENDING_07BB      XDATA_VAR8(0x07BB)  /* Command pending flag */
#define G_CMD_STATE_07BC        XDATA_VAR8(0x07BC)  /* Command state (0-3) */
#define G_CMD_OP_COUNTER        XDATA_VAR8(0x07BD)  /* Command operation counter */
#define G_CMD_ADDR_HI           XDATA_VAR8(0x07BF)  /* Computed slot address high */
#define G_CMD_ADDR_LO           XDATA_VAR8(0x07C0)  /* Computed slot address low */
#define G_CMD_SLOT_C1           XDATA_VAR8(0x07C1)  /* Slot index for address calc */
#define G_CMD_WORK_C2           XDATA_VAR8(0x07C2)  /* Command work byte */
#define G_CMD_STATE             XDATA_VAR8(0x07C3)  /* Command state (3-bit) */
#define G_CMD_STATUS            XDATA_VAR8(0x07C4)  /* Command status byte */
#define G_CMD_WORK_C5           XDATA_VAR8(0x07C5)  /* Command work byte */
#define G_CMD_WORK_C7           XDATA_VAR8(0x07C7)  /* Command work byte */
#define G_CMD_MODE              XDATA_VAR8(0x07CA)  /* Command mode (1=mode1, 2=mode2, 3=mode3) */
#define G_CMD_PARAM_0           XDATA_VAR8(0x07D3)  /* Command parameter 0 */
#define G_CMD_PARAM_1           XDATA_VAR8(0x07D4)  /* Command parameter 1 */
#define G_CMD_PARAM_2           XDATA_VAR8(0x07D5)  /* Command parameter 2 (slot count) */
#define G_CMD_LBA_0             XDATA_VAR8(0x07DA)  /* Command LBA byte 0 (low) */
#define G_CMD_LBA_1             XDATA_VAR8(0x07DB)  /* Command LBA byte 1 */
#define G_CMD_LBA_2             XDATA_VAR8(0x07DC)  /* Command LBA byte 2 */
#define G_CMD_LBA_3             XDATA_VAR8(0x07DD)  /* Command LBA byte 3 (high) */
#define G_CMD_FLAG_07DE         XDATA_VAR8(0x07DE)  /* Command flag 0x07DE */
#define G_PCIE_COMPLETE_07DF    XDATA_VAR8(0x07DF)  /* PCIe link complete flag */
#define G_CMD_WORK_E3           XDATA_VAR8(0x07E3)  /* Command work byte */
#define G_CMD_DEBUG_FF          XDATA_VAR8(0x07FF)  /* Debug marker byte */

//=============================================================================
// PCIe Interrupt Handler Work Area
//=============================================================================
#define G_PCIE_LANE_STATE_0A9E  XDATA_VAR8(0x0A9E)  /* PCIe lane state */
#define G_PCIE_STATUS_0B35      XDATA_VAR8(0x0B35)  /* PCIe status work byte */
#define G_PCIE_STATUS_0B36      XDATA_VAR8(0x0B36)  /* PCIe status work byte */
#define G_PCIE_STATUS_0B37      XDATA_VAR8(0x0B37)  /* PCIe status work byte */
#define G_PCIE_STATUS_0B13      XDATA_VAR8(0x0B13)  /* PCIe status work */
#define G_PCIE_STATUS_0B14      XDATA_VAR8(0x0B14)  /* PCIe status work */
#define G_PCIE_STATUS_0B15      XDATA_VAR8(0x0B15)  /* PCIe status work */
#define G_PCIE_STATUS_0B16      XDATA_VAR8(0x0B16)  /* PCIe status work */
#define G_PCIE_STATUS_0B17      XDATA_VAR8(0x0B17)  /* PCIe status work */
#define G_PCIE_STATUS_0B18      XDATA_VAR8(0x0B18)  /* PCIe status work */
#define G_PCIE_STATUS_0B19      XDATA_VAR8(0x0B19)  /* PCIe status flag */
#define G_PCIE_STATUS_0B1A      XDATA_VAR8(0x0B1A)  /* PCIe status work */
#define G_DMA_WORK_0B1D         XDATA_VAR8(0x0B1D)  /* DMA work byte (r4) */
#define G_DMA_WORK_0B1E         XDATA_VAR8(0x0B1E)  /* DMA work byte (r5) */
#define G_DMA_WORK_0B1F         XDATA_VAR8(0x0B1F)  /* DMA work byte (r6) */
#define G_DMA_WORK_0B20         XDATA_VAR8(0x0B20)  /* DMA work byte (r7) */
#define G_DMA_WORK_0B21         XDATA_VAR8(0x0B21)  /* DMA work byte */
#define G_DMA_WORK_0B24         XDATA_VAR8(0x0B24)  /* DMA work byte */
#define G_DMA_WORK_0B25         XDATA_VAR8(0x0B25)  /* DMA work byte */

//=============================================================================
// Power State Machine Work Area (0x0A60-0x0A6F)
//=============================================================================
#define G_POWER_STATE_MAX_0A61  XDATA_VAR8(0x0A61)  /* Power state iteration max */
#define G_POWER_STATE_IDX_0A62  XDATA_VAR8(0x0A62)  /* Power state iteration index */

#endif /* __GLOBALS_H__ */
