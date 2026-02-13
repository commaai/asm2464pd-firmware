#ifndef __REGISTERS_H__
#define __REGISTERS_H__

#include "types.h"

/*
 * ASM2464PD USB4/Thunderbolt NVMe Controller - Hardware Register Map
 *
 * All registers are memory-mapped in XDATA space.
 * Organized by functional block in address order.
 *
 * Address Space Layout:
 *   0x7000-0x7FFF  Flash buffer (4KB)
 *   0x8000-0x8FFF  USB/SCSI buffers
 *   0x9000-0x93FF  USB Interface
 *   0xA000-0xAFFF  NVMe I/O Queue
 *   0xB000-0xB1FF  NVMe Admin Queues
 *   0xB200-0xB4FF  PCIe Passthrough
 *   0xC000-0xC0FF  UART Controller
 *   0xC200-0xC2FF  Link/PHY Control
 *   0xC400-0xC5FF  NVMe Interface
 *   0xC600-0xC6FF  PHY Extended
 *   0xC800-0xC8FF  Interrupt / I2C / Flash / DMA
 *   0xCA00-0xCAFF  CPU Mode
 *   0xCC00-0xCCFF  Timer / CPU Control
 *   0xCE00-0xCEFF  SCSI DMA / Transfer Control
 *   0xD800-0xDFFF  USB Endpoint Buffer (see structs.h)
 *   0xE300-0xE3FF  PHY Completion / Debug
 *   0xE400-0xE4FF  Command Engine
 *   0xE600-0xE6FF  Debug/Interrupt
 *   0xE700-0xE7FF  System Status / Link Control
 *   0xEC00-0xECFF  NVMe Event
 *   0xEF00-0xEFFF  System Control
 *   0xF000-0xFFFF  NVMe Data Buffer
 */

//=============================================================================
// Helper Macros
//=============================================================================
#define XDATA_REG8(addr)   (*(__xdata uint8_t *)(addr))
#define XDATA_REG16(addr)  (*(__xdata uint16_t *)(addr))
#define XDATA_REG32(addr)  (*(__xdata uint32_t *)(addr))

//=============================================================================
// Memory Buffers
//=============================================================================
#define FLASH_BUFFER_BASE       0x7000
#define FLASH_BUFFER_SIZE       0x1000

// Flash buffer control registers (0x7041, 0x78AF-0x78B2)
#define REG_FLASH_BUF_CTRL_7041 XDATA_REG8(0x7041)  /* Flash buffer control */
#define   FLASH_BUF_CTRL_BIT6    0x40  // Bit 6: Buffer control enable
#define REG_FLASH_BUF_CFG_78AF  XDATA_REG8(0x78AF)  /* Flash buffer config 0 */
#define REG_FLASH_BUF_CFG_78B0  XDATA_REG8(0x78B0)  /* Flash buffer config 1 */
#define REG_FLASH_BUF_CFG_78B1  XDATA_REG8(0x78B1)  /* Flash buffer config 2 */
#define REG_FLASH_BUF_CFG_78B2  XDATA_REG8(0x78B2)  /* Flash buffer config 3 */
#define   FLASH_BUF_CFG_BIT6     0x40  // Bit 6: Buffer config enable

#define USB_SCSI_BUF_BASE       0x8000
#define USB_SCSI_BUF_SIZE       0x1000

// USB/SCSI buffer control registers (0x8005-0x800D)
#define REG_USB_BUF_COUNT_8005  XDATA_REG8(0x8005)  /* USB buffer count */
#define REG_USB_BUF_MAX_8006    XDATA_REG8(0x8006)  /* USB buffer max count */
#define REG_USB_BUF_CTRL_8008   XDATA_REG8(0x8008)  /* USB buffer control (power check: ==0x01) */
#define REG_USB_BUF_CTRL_8009   XDATA_REG8(0x8009)  /* USB buffer control (power check: ==0x08) */
#define REG_USB_BUF_CTRL_800A   XDATA_REG8(0x800A)  /* USB buffer control (power check: ==0x02) */
#define REG_USB_BUF_STATUS_800D XDATA_REG8(0x800D)  /* USB buffer status (mask 0x7F != 0 check) */

/*
 * USB Setup Packet Buffer (0x9E00-0x9E07)
 * Hardware writes the 8-byte USB setup packet here when received.
 * Firmware reads these registers in ISR at 0xA5EA-0xA604 to process request.
 *
 * Standard USB Setup Packet Format:
 *   Byte 0 (bmRequestType): Request characteristics
 *     Bit 7: Direction (0=Host-to-device, 1=Device-to-host)
 *     Bits 6-5: Type (0=Standard, 1=Class, 2=Vendor)
 *     Bits 4-0: Recipient (0=Device, 1=Interface, 2=Endpoint)
 *   Byte 1 (bRequest): Specific request code
 *     0x00=GET_STATUS, 0x01=CLEAR_FEATURE, 0x05=SET_ADDRESS
 *     0x06=GET_DESCRIPTOR, 0x09=SET_CONFIGURATION
 *   Bytes 2-3 (wValue): Request-specific value
 *   Bytes 4-5 (wIndex): Request-specific index
 *   Bytes 6-7 (wLength): Number of bytes to transfer
 */
#define USB_CTRL_BUF_BASE       0x9E00
#define USB_CTRL_BUF_SIZE       0x0200

// USB Setup Packet Registers
#define REG_USB_SETUP_TYPE      XDATA_REG8(0x9E00)  /* bmRequestType (direction/type/recipient) */
#define REG_USB_SETUP_REQUEST   XDATA_REG8(0x9E01)  /* bRequest (request code) */
#define REG_USB_SETUP_VALUE_L   XDATA_REG8(0x9E02)  /* wValue low byte (descriptor index) */
#define REG_USB_SETUP_VALUE_H   XDATA_REG8(0x9E03)  /* wValue high byte (descriptor type) */
#define REG_USB_SETUP_INDEX_L   XDATA_REG8(0x9E04)  /* wIndex low byte */
#define REG_USB_SETUP_INDEX_H   XDATA_REG8(0x9E05)  /* wIndex high byte */
#define REG_USB_SETUP_LENGTH_L  XDATA_REG8(0x9E06)  /* wLength low byte */
#define REG_USB_SETUP_LENGTH_H  XDATA_REG8(0x9E07)  /* wLength high byte */

// bmRequestType bit definitions
#define   USB_SETUP_DIR_HOST_TO_DEV  0x00  // Direction: Host to Device
#define   USB_SETUP_DIR_DEV_TO_HOST  0x80  // Direction: Device to Host
#define   USB_SETUP_TYPE_STANDARD    0x00  // Type: Standard request
#define   USB_SETUP_TYPE_CLASS       0x20  // Type: Class request
#define   USB_SETUP_TYPE_VENDOR      0x40  // Type: Vendor request
#define   USB_SETUP_RECIP_DEVICE     0x00  // Recipient: Device
#define   USB_SETUP_RECIP_INTERFACE  0x01  // Recipient: Interface
#define   USB_SETUP_RECIP_ENDPOINT   0x02  // Recipient: Endpoint

// Standard bRequest codes
#define   USB_REQ_GET_STATUS         0x00
#define   USB_REQ_CLEAR_FEATURE      0x01
#define   USB_REQ_SET_FEATURE        0x03
#define   USB_REQ_SET_ADDRESS        0x05
#define   USB_REQ_GET_DESCRIPTOR     0x06
#define   USB_REQ_SET_DESCRIPTOR     0x07
#define   USB_REQ_GET_CONFIGURATION  0x08
#define   USB_REQ_SET_CONFIGURATION  0x09

// Descriptor types (for wValue high byte in GET_DESCRIPTOR)
#define   USB_DESC_TYPE_DEVICE       0x01
#define   USB_DESC_TYPE_CONFIG       0x02
#define   USB_DESC_TYPE_STRING       0x03
#define   USB_DESC_TYPE_INTERFACE    0x04
#define   USB_DESC_TYPE_ENDPOINT     0x05
#define   USB_DESC_TYPE_BOS          0x0F  // Binary Object Store (USB 3.0)

// Additional USB control buffer registers
#define REG_USB_CTRL_BUF_9E16   XDATA_REG8(0x9E16)  /* USB control buffer descriptor 1 hi */
#define REG_USB_CTRL_BUF_9E17   XDATA_REG8(0x9E17)  /* USB control buffer descriptor 1 lo */
#define REG_USB_CTRL_BUF_9E1D   XDATA_REG8(0x9E1D)  /* USB control buffer descriptor 2 hi */
#define REG_USB_CTRL_BUF_9E1E   XDATA_REG8(0x9E1E)  /* USB control buffer descriptor 2 lo */

#define NVME_IOSQ_BASE          0xA000
#define NVME_IOSQ_SIZE          0x1000
#define NVME_IOSQ_DMA_ADDR      0x00820000

#define NVME_ASQ_BASE           0xB000
#define NVME_ASQ_SIZE           0x0100
#define NVME_ACQ_BASE           0xB100
#define NVME_ACQ_SIZE           0x0100

#define NVME_DATA_BUF_BASE      0xF000
#define NVME_DATA_BUF_SIZE      0x1000
#define NVME_DATA_BUF_DMA_ADDR  0x00200000

//=============================================================================
// USB Interface Registers (0x9000-0x93FF)
//=============================================================================
/*
 * USB Controller Overview:
 * The USB controller handles USB 2.0 and USB 3.0 (SuperSpeed) connections.
 *
 * USB State Machine (IDATA[0x6A]):
 *   0 = DISCONNECTED  - No USB connection
 *   1 = ATTACHED      - Cable connected
 *   2 = POWERED       - Bus powered
 *   3 = DEFAULT       - Default address assigned
 *   4 = ADDRESS       - Device address assigned
 *   5 = CONFIGURED    - Ready for vendor commands
 *
 * Key MMIO registers for USB:
 *   0x9000: Connection status (bit 7=connected, bit 0=active)
 *   0x9091: Control transfer phase (bit 0=setup, bit 1=data)
 *   0x9092: DMA trigger for descriptor transfers
 *   0x9101: Interrupt flags (bit 5 triggers command handler)
 *   0x9E00-0x9E07: USB setup packet buffer
 *   0xCE89: USB/DMA status (state machine control)
 */

// Core USB registers (0x9000-0x901F)
#define REG_USB_STATUS          XDATA_REG8(0x9000)
#define   USB_STATUS_ACTIVE       0x01  // Bit 0: USB active - SET for enumeration at ISR 0x0E68
#define   USB_STATUS_BIT2         0x04  // Bit 2: USB status flag
#define   USB_STATUS_INDICATOR    0x10  // Bit 4: USB status indicator
#define   USB_STATUS_CONNECTED    0x80  // Bit 7: USB cable connected
#define REG_USB_CONTROL         XDATA_REG8(0x9001)
#define REG_USB_CONFIG          XDATA_REG8(0x9002)
#define   USB_CONFIG_MASK        0x0F  // Bits 0-3: USB configuration value
#define   USB_CONFIG_BIT1        0x02  // Bit 1: Must be CLEAR to reach 0x9091 check at 0xCDF5
#define REG_USB_EP0_STATUS      XDATA_REG8(0x9003)
#define REG_USB_EP0_LEN_L       XDATA_REG8(0x9004)  /* EP0 transfer length low byte */
#define REG_USB_EP0_LEN_H       XDATA_REG8(0x9005)  /* EP0 transfer length high byte */
#define REG_USB_EP0_CONFIG      XDATA_REG8(0x9006)
#define   USB_EP0_CONFIG_ENABLE   0x01  // Bit 0: EP0 config enable
#define   USB_EP0_CONFIG_READY    0x80  // Bit 7: EP0 ready/valid
#define REG_USB_SCSI_BUF_LEN    XDATA_REG16(0x9007)
#define REG_USB_SCSI_BUF_LEN_L  XDATA_REG8(0x9007)
#define REG_USB_SCSI_BUF_LEN_H  XDATA_REG8(0x9008)
#define REG_USB_MSC_CFG         XDATA_REG8(0x900B)
#define REG_USB_DATA_L          XDATA_REG8(0x9010)
#define REG_USB_DATA_H          XDATA_REG8(0x9011)
#define REG_USB_FIFO_STATUS     XDATA_REG8(0x9012)  /* USB FIFO/status register */
#define   USB_FIFO_STATUS_READY   0x01  // Bit 0: USB ready/active
#define REG_USB_FIFO_H          XDATA_REG8(0x9013)
#define REG_USB_XCVR_MODE       XDATA_REG8(0x9018)
#define REG_USB_MODE_VAL_9019   XDATA_REG8(0x9019)
#define REG_USB_MSC_LENGTH      XDATA_REG8(0x901A)

// USB endpoint registers (0x905A-0x90FF)
#define REG_USB_EP_CFG_905A     XDATA_REG8(0x905A)  /* USB endpoint config */
#define REG_USB_EP_BUF_HI       XDATA_REG8(0x905B)  /* DMA source address high (descriptor ROM) */
#define REG_USB_EP_BUF_LO       XDATA_REG8(0x905C)  /* DMA source address low (descriptor ROM) */
#define REG_USB_EP_CTRL_905D    XDATA_REG8(0x905D)  /* USB endpoint control 1 */
#define REG_USB_EP_MGMT         XDATA_REG8(0x905E)
#define REG_USB_EP_CTRL_905F    XDATA_REG8(0x905F)  /* USB endpoint control 2 */
#define   USB_EP_CTRL_905F_BIT3   0x08  // Bit 3: Endpoint enable flag
#define   USB_EP_CTRL_905F_BIT4   0x10  // Bit 4: Endpoint control flag
#define REG_USB_INT_MASK_9090   XDATA_REG8(0x9090)  /* USB interrupt mask */
#define   USB_INT_MASK_GLOBAL     0x80  // Bit 7: Global interrupt mask

/*
 * USB Control Transfer Phase Register (0x9091)
 * 
 * Controls USB control transfer state machine. Read to check current phase,
 * write to acknowledge/advance phase.
 *
 * Control Transfer Sequence (verified working):
 * 
 * 1. SETUP Phase:
 *    - Hardware sets bit 0 when SETUP packet received
 *    - Read 0x9002, write back; read 0x9220
 *    - Write 0x01 to acknowledge setup phase
 *    - Read setup packet from 0x9104-0x910B
 *
 * 2. DATA Phase (GET_DESCRIPTOR):
 *    - Poll until bit 3 (0x08) is set = data phase ready
 *    - Write descriptor to 0x9E00+ buffer
 *    - Set length: 0x9003=0, 0x9004=len
 *    - Write 0x9092=0x04 to trigger DMA send
 *    - Poll 0x9092 until 0 (DMA complete)
 *    - Write 0x08 to acknowledge data phase
 *
 * 3. STATUS Phase:
 *    - Poll until bit 4 (0x10) is set = status phase ready
 *    - Write 0x9092=0x08 to complete status
 *    - Write 0x10 to acknowledge status phase
 *
 * For no-data requests (SET_ADDRESS, SET_CONFIG):
 *    - Skip data phase, go directly to status phase
 */
#define REG_USB_CTRL_PHASE      XDATA_REG8(0x9091)
#define   USB_CTRL_PHASE_SETUP    0x01  // Bit 0: Setup packet received
#define   USB_CTRL_PHASE_DATA     0x08  // Bit 3: Data phase ready (poll for this)
#define   USB_CTRL_PHASE_STATUS   0x10  // Bit 4: Status phase ready (poll for this)

/*
 * USB EP0 DMA Control Register (0x9092)
 * 
 * Controls DMA transfers for EP0 control transfers.
 * Write to trigger operations, reads back 0 when complete.
 *
 * Values:
 *   0x04 = Start DMA send (IN transfer - device to host)
 *          Used for GET_DESCRIPTOR data phase.
 *          Set 0x9003=0, 0x9004=length first, then write 0x04.
 *          Poll until reads 0 for completion.
 *
 *   0x08 = Complete status phase
 *          Write after status phase ready (0x9091 & 0x10).
 *          For IN transfers: host sends ZLP, we ACK.
 *          For OUT transfers: we send ZLP.
 */
#define REG_USB_DMA_TRIGGER     XDATA_REG8(0x9092)
#define   USB_DMA_SEND            0x04  // Trigger DMA send (IN data phase)
#define   USB_DMA_STATUS_COMPLETE 0x08  // Complete status phase

/*
 * USB Endpoint Config 1 (0x9093)
 * Controls endpoint transfer mode and triggers bulk operations.
 *
 * ISR bit handlers (when bit is set):
 *   Bit 0: Calls cleanup (0x54b4 -> 0x3169, 0x320d)
 *   Bit 1: Bulk data complete - calls 0x32e4
 *   Bit 2: Calls cleanup (same as bit 0)
 *   Bit 3: State machine handler - calls 0x4d3e
 *
 * Values written by firmware:
 *   0x08: Bulk transfer mode (with 0x9094=0x02) - at 0x529c, 0x1cca
 *   0x02: Interrupt transfer mode (with 0x9094=0x10) - at 0x1cd5
 *   0x04: Event handler default action - at 0x1009
 *   0x02: Clear bit 1 after ISR - at 0x0fac
 */
#define REG_USB_EP_CFG1         XDATA_REG8(0x9093)
#define   USB_EP_CFG1_CLEANUP     0x01  // Bit 0: Cleanup pending
#define   USB_EP_CFG1_BULK_DONE   0x02  // Bit 1: Bulk transfer complete
#define   USB_EP_CFG1_CLEANUP2    0x04  // Bit 2: Cleanup pending (alt)
#define   USB_EP_CFG1_STATE_MACH  0x08  // Bit 3: State machine event
#define   USB_EP_CFG1_BULK_MODE   0x08  // Write: Configure bulk transfer mode
#define   USB_EP_CFG1_INT_MODE    0x02  // Write: Configure interrupt mode
#define REG_USB_EP_CFG2         XDATA_REG8(0x9094)
#define   USB_EP_CFG2_BULK_MODE   0x02  // Write with CFG1=0x08 for bulk mode
#define   USB_EP_CFG2_INT_MODE    0x10  // Write with CFG1=0x02 for int mode
#define REG_USB_EP_READY        XDATA_REG8(0x9096)
#define REG_USB_EP_CTRL_9097    XDATA_REG8(0x9097)  /* USB endpoint control */
#define REG_USB_EP_MODE_9098    XDATA_REG8(0x9098)  /* USB endpoint mode */
#define REG_USB_STATUS_909E     XDATA_REG8(0x909E)
#define REG_USB_CTRL_90A0       XDATA_REG8(0x90A0)  /* USB control 0x90A0 */
#define REG_USB_SIGNAL_90A1     XDATA_REG8(0x90A1)
#define REG_USB_SPEED           XDATA_REG8(0x90E0)
#define   USB_SPEED_MASK         0x03  // Bits 0-1: USB speed mode
#define REG_USB_MODE            XDATA_REG8(0x90E2)
#define REG_USB_EP_STATUS_90E3  XDATA_REG8(0x90E3)

/*
 * USB Link Status and Speed Registers (0x9100-0x912F)
 * These indicate current USB connection state and speed mode.
 */
#define REG_USB_LINK_STATUS     XDATA_REG8(0x9100)
#define   USB_LINK_STATUS_MASK    0x03  // Bits 0-1: USB speed mode
#define   USB_SPEED_FULL          0x00  // Full Speed (USB 1.x, 12 Mbps)
#define   USB_SPEED_HIGH          0x01  // High Speed (USB 2.0, 480 Mbps)
#define   USB_SPEED_SUPER         0x02  // SuperSpeed (USB 3.0, 5 Gbps)
#define   USB_SPEED_SUPER_PLUS    0x03  // SuperSpeed+ (USB 3.1+, 10+ Gbps)

/*
 * USB Interrupt/Event Status (0x9101)
 * Controls which USB handler path is taken in ISR.
 * Different bits trigger different code paths in the interrupt handler.
 */
#define REG_USB_PERIPH_STATUS   XDATA_REG8(0x9101)
#define   USB_PERIPH_EP0_ACTIVE   0x01  // Bit 0: EP0 control transfer active
#define   USB_PERIPH_DESC_REQ     0x02  // Bit 1: Descriptor request pending (triggers 0x033B)
#define   USB_PERIPH_BULK_REQ     0x08  // Bit 3: Bulk transfer request (vendor cmd path)
#define   USB_PERIPH_VENDOR_CMD   0x20  // Bit 5: Vendor command handler path
#define   USB_PERIPH_SUSPENDED    0x40  // Bit 6: Peripheral suspended / USB init

/*
 * USB Setup Packet Registers (0x9104-0x910B)
 * Hardware writes the 8-byte USB setup packet here when received.
 * Read at PC=0xA5F5 in ISR during control transfer setup phase.
 * Different from 0x9E00-0x9E07 which is the response buffer.
 *
 * Standard USB Setup Packet Format:
 *   Byte 0 (bmRequestType): Direction(1) | Type(2) | Recipient(5)
 *   Byte 1 (bRequest): Request code (GET_DESCRIPTOR=0x06, SET_ADDRESS=0x05, etc.)
 *   Bytes 2-3 (wValue): Request-specific (descriptor type/index for GET_DESCRIPTOR)
 *   Bytes 4-5 (wIndex): Request-specific (language ID for string descriptors)
 *   Bytes 6-7 (wLength): Number of bytes to transfer
 */
#define REG_USB_SETUP_BMREQ     XDATA_REG8(0x9104)  /* bmRequestType */
#define REG_USB_SETUP_BREQ      XDATA_REG8(0x9105)  /* bRequest */
#define REG_USB_SETUP_WVAL_L    XDATA_REG8(0x9106)  /* wValue low (descriptor index) */
#define REG_USB_SETUP_WVAL_H    XDATA_REG8(0x9107)  /* wValue high (descriptor type) */
#define REG_USB_SETUP_WIDX_L    XDATA_REG8(0x9108)  /* wIndex low */
#define REG_USB_SETUP_WIDX_H    XDATA_REG8(0x9109)  /* wIndex high */
#define REG_USB_SETUP_WLEN_L    XDATA_REG8(0x910A)  /* wLength low */
#define REG_USB_SETUP_WLEN_H    XDATA_REG8(0x910B)  /* wLength high */

#define REG_USB_PHY_STATUS_9105 XDATA_REG8(0x9105)  /* USB PHY status check (0xFF = active) - alias for SETUP_BREQ */
#define REG_USB_STAT_EXT_L      XDATA_REG8(0x910D)
#define REG_USB_STAT_EXT_H      XDATA_REG8(0x910E)
/* USB CDB (Command Descriptor Block) registers for vendor commands */
#define REG_USB_CDB_CMD         XDATA_REG8(0x910D)  /* CDB byte 0: Command type (alias for STAT_EXT_L) */
#define REG_USB_CDB_LEN         XDATA_REG8(0x910E)  /* CDB byte 1: Size/value (alias for STAT_EXT_H) */
#define REG_USB_CDB_ADDR_HI     XDATA_REG8(0x910F)  /* CDB byte 2: Address high byte */
#define REG_USB_CDB_ADDR_MID    XDATA_REG8(0x9110)  /* CDB byte 3: Address mid byte */
#define REG_USB_CDB_ADDR_LO     XDATA_REG8(0x9111)  /* CDB byte 4: Address low byte */
#define REG_USB_CDB_5           XDATA_REG8(0x9112)  /* CDB byte 5: Reserved */
#define REG_USB_EP_STATUS       XDATA_REG8(0x9118)
#define REG_USB_CBW_LEN_HI      XDATA_REG8(0x9119)  /* CBW length high byte */
#define REG_USB_CBW_LEN_LO      XDATA_REG8(0x911A)  /* CBW length low byte */
#define REG_USB_BUFFER_ALT      XDATA_REG8(0x911B)  /* CBW sig byte 0 / 'U' */
#define REG_USB_CBW_SIG1        XDATA_REG8(0x911C)  /* CBW sig byte 1 / 'S' */
#define REG_USB_CBW_SIG2        XDATA_REG8(0x911D)  /* CBW sig byte 2 / 'B' */
#define REG_USB_CBW_SIG3        XDATA_REG8(0x911E)  /* CBW sig byte 3 / 'C' */
#define REG_CBW_TAG_0           XDATA_REG8(0x911F)
#define REG_CBW_TAG_1           XDATA_REG8(0x9120)
#define REG_CBW_TAG_2           XDATA_REG8(0x9121)
#define REG_CBW_TAG_3           XDATA_REG8(0x9122)
#define REG_USB_CBW_XFER_LEN_0  XDATA_REG8(0x9123)  /* CBW transfer length byte 0 (LSB) */
#define REG_USB_CBW_XFER_LEN_1  XDATA_REG8(0x9124)  /* CBW transfer length byte 1 */
#define REG_USB_CBW_XFER_LEN_2  XDATA_REG8(0x9125)  /* CBW transfer length byte 2 */
#define REG_USB_CBW_XFER_LEN_3  XDATA_REG8(0x9126)  /* CBW transfer length byte 3 (MSB) */
#define REG_USB_CBW_FLAGS       XDATA_REG8(0x9127)  /* CBW flags (bit 7 = direction) */
#define   CBW_FLAGS_DIRECTION     0x80  // Bit 7: Data direction (1=IN, 0=OUT)
#define REG_USB_CBW_LUN         XDATA_REG8(0x9128)  /* CBW LUN (bits 0-3) */
#define   CBW_LUN_MASK            0x0F  // Bits 0-3: Logical Unit Number

// USB PHY registers (0x91C0-0x91FF)
#define REG_USB_PHY_CTRL_91C0   XDATA_REG8(0x91C0)
#define   USB_PHY_CTRL_BIT1       0x02  // Bit 1: USB PHY ready/enable
#define REG_USB_PHY_CTRL_91C1   XDATA_REG8(0x91C1)
#define REG_USB_PHY_CTRL_91C3   XDATA_REG8(0x91C3)
#define REG_USB_EP_CTRL_91D0    XDATA_REG8(0x91D0)
#define REG_USB_PHY_CTRL_91D1   XDATA_REG8(0x91D1)
#define   USB_PHY_CTRL_BIT0      0x01  // Bit 0: PHY control flag 0
#define   USB_PHY_CTRL_BIT1      0x02  // Bit 1: PHY control flag 1
#define   USB_PHY_CTRL_BIT2      0x04  // Bit 2: PHY control flag 2
#define   USB_PHY_CTRL_BIT3      0x08  // Bit 3: PHY control flag 3

// USB control registers (0x9200-0x92BF)
#define REG_USB_CTRL_9200       XDATA_REG8(0x9200)  /* USB control base */
#define   USB_CTRL_9200_BIT6     0x40  // Bit 6: USB control enable flag
#define REG_USB_CTRL_9201       XDATA_REG8(0x9201)
#define   USB_CTRL_9201_BIT4      0x10  // Bit 4: USB control flag
#define REG_USB_CTRL_920C       XDATA_REG8(0x920C)
#define REG_USB_PHY_CONFIG_9241 XDATA_REG8(0x9241)
#define REG_USB_CTRL_924C       XDATA_REG8(0x924C)  // USB control (bit 0: endpoint ready)

/*
 * Power Management Registers (0x92C0-0x92E0)
 * Control power domains, clocks, and device power state.
 *
 * REG_POWER_STATUS (0x92C2) is particularly important for USB:
 *   Bit 6 controls ISR vs main loop execution paths.
 *   When CLEAR: ISR calls 0xBDA4 for descriptor init
 *   When SET: Main loop calls 0x0322 for transfer
 */
#define REG_POWER_ENABLE        XDATA_REG8(0x92C0)
#define   POWER_ENABLE_BIT        0x01  // Bit 0: Main power enable
#define   POWER_ENABLE_MAIN       0x80  // Bit 7: Main power on
#define REG_CLOCK_ENABLE        XDATA_REG8(0x92C1)
#define   CLOCK_ENABLE_BIT        0x01  // Bit 0: Clock enable
#define   CLOCK_ENABLE_BIT1       0x02  // Bit 1: Secondary clock
#define REG_POWER_STATUS        XDATA_REG8(0x92C2)
#define   POWER_STATUS_READY      0x02  // Bit 1: Power ready
#define   POWER_STATUS_USB_PATH   0x40  // Bit 6: Controls ISR/main loop USB path
#define REG_POWER_MISC_CTRL     XDATA_REG8(0x92C4)
#define REG_PHY_POWER           XDATA_REG8(0x92C5)
#define   PHY_POWER_ENABLE        0x04  // Bit 2: PHY power enable
#define REG_POWER_CTRL_92C6     XDATA_REG8(0x92C6)
#define REG_POWER_CTRL_92C7     XDATA_REG8(0x92C7)
#define REG_POWER_CTRL_92C8     XDATA_REG8(0x92C8)
#define REG_POWER_DOMAIN        XDATA_REG8(0x92E0)
#define   POWER_DOMAIN_BIT1       0x02  // Bit 1: Power domain control
#define REG_POWER_EVENT_92E1    XDATA_REG8(0x92E1)  // Power event register
#define REG_POWER_STATUS_92F7   XDATA_REG8(0x92F7)  // Power status (high nibble = state)

// Buffer config registers (0x9300-0x93FF)
#define REG_BUF_CFG_9300        XDATA_REG8(0x9300)
#define REG_BUF_CFG_9301        XDATA_REG8(0x9301)
#define   BUF_CFG_9301_BIT6      0x40  // Bit 6: Buffer config flag
#define   BUF_CFG_9301_BIT7      0x80  // Bit 7: Buffer config flag
#define REG_BUF_CFG_9302        XDATA_REG8(0x9302)
#define   BUF_CFG_9302_BIT7      0x80  // Bit 7: Buffer status flag
#define REG_BUF_CFG_9303        XDATA_REG8(0x9303)
#define REG_BUF_CFG_9304        XDATA_REG8(0x9304)
#define REG_BUF_CFG_9305        XDATA_REG8(0x9305)

//=============================================================================
// PCIe Passthrough Registers (0xB210-0xB8FF)
//=============================================================================

// PCIe extended register access (0x12xx banked -> 0xB2xx XDATA)
#define PCIE_EXT_REG(offset)  XDATA_REG8(0xB200 + (offset))

// PCIe TLP registers (0xB210-0xB284)
#define REG_PCIE_FMT_TYPE       XDATA_REG8(0xB210)
#define REG_PCIE_TLP_CTRL       XDATA_REG8(0xB213)
#define REG_PCIE_TLP_LENGTH     XDATA_REG8(0xB216)
#define REG_PCIE_BYTE_EN        XDATA_REG8(0xB217)
// REG_PCIE_B217 removed (alias)
#define REG_PCIE_ADDR_0         XDATA_REG8(0xB218)
#define REG_PCIE_ADDR_1         XDATA_REG8(0xB219)
#define REG_PCIE_ADDR_2         XDATA_REG8(0xB21A)
#define REG_PCIE_ADDR_3         XDATA_REG8(0xB21B)
#define REG_PCIE_ADDR_HIGH      XDATA_REG8(0xB21C)
#define REG_PCIE_DATA           XDATA_REG8(0xB220)
#define REG_PCIE_EXT_STATUS     XDATA_REG8(0xB223)   // PCIe extended status (bit 0 = ready)
#define REG_PCIE_TLP_CPL_HEADER XDATA_REG32(0xB224)
#define REG_PCIE_LINK_STATUS    XDATA_REG16(0xB22A)
#define REG_PCIE_CPL_STATUS     XDATA_REG8(0xB22B)
#define REG_PCIE_CPL_DATA       XDATA_REG8(0xB22C)
#define REG_PCIE_CPL_DATA_ALT   XDATA_REG8(0xB22D)

// PCIe Extended Link Registers (0xB234-0xB24E)
#define REG_PCIE_LINK_STATE_EXT XDATA_REG8(0xB234)   // Extended link state machine state
#define REG_PCIE_LINK_CFG       XDATA_REG8(0xB235)   // Link configuration (bits 6-7 kept on reset)
#define REG_PCIE_LINK_PARAM     XDATA_REG8(0xB236)   // Link parameter
#define REG_PCIE_LINK_STATUS_EXT XDATA_REG8(0xB237)  // Extended link status (bit 7 = active)
#define REG_PCIE_LINK_TRIGGER   XDATA_REG8(0xB238)   // Link trigger (bit 0 = busy)
#define   PCIE_LINK_TRIGGER_BUSY  0x01  // Bit 0: Link trigger busy
#define REG_PCIE_EXT_CFG_0      XDATA_REG8(0xB23C)   // Extended config 0
#define REG_PCIE_EXT_CFG_1      XDATA_REG8(0xB23D)   // Extended config 1
#define REG_PCIE_EXT_CFG_2      XDATA_REG8(0xB23E)   // Extended config 2
#define REG_PCIE_EXT_CFG_3      XDATA_REG8(0xB23F)   // Extended config 3
#define REG_PCIE_EXT_STATUS_RD  XDATA_REG8(0xB240)   // Extended status read
#define REG_PCIE_EXT_STATUS_RD1 XDATA_REG8(0xB241)   // Extended status read 1
#define REG_PCIE_EXT_STATUS_RD2 XDATA_REG8(0xB242)   // Extended status read 2
#define REG_PCIE_EXT_STATUS_RD3 XDATA_REG8(0xB243)   // Extended status read 3
#define REG_PCIE_EXT_STATUS_ALT XDATA_REG8(0xB24E)   // Extended status alternate

#define REG_PCIE_NVME_DOORBELL  XDATA_REG32(0xB250)
#define REG_PCIE_DOORBELL_CMD   XDATA_REG8(0xB251)   // Byte 1 of doorbell - command byte
#define REG_PCIE_TRIGGER        XDATA_REG8(0xB254)
#define REG_PCIE_PM_ENTER       XDATA_REG8(0xB255)
#define REG_PCIE_COMPL_STATUS   XDATA_REG8(0xB284)
#define REG_PCIE_POWER_B294     XDATA_REG8(0xB294)  /* PCIe power control */
// PCIe status registers (0xB296-0xB298)
#define REG_PCIE_STATUS         XDATA_REG8(0xB296)
#define   PCIE_STATUS_ERROR       0x01  // Bit 0: Error flag
#define   PCIE_STATUS_COMPLETE    0x02  // Bit 1: Completion status
#define   PCIE_STATUS_BUSY        0x04  // Bit 2: Busy flag
#define REG_PCIE_TUNNEL_CFG     XDATA_REG8(0xB298)  // TLP control (bit 4 = tunnel enable)
#define   PCIE_TLP_CTRL_TUNNEL    0x10  // Bit 4: Tunnel enable
#define REG_PCIE_CTRL_B2D5      XDATA_REG8(0xB2D5)  /* PCIe control */

// PCIe Tunnel Control (0xB401-0xB404)
#define REG_PCIE_TUNNEL_CTRL    XDATA_REG8(0xB401)  // PCIe tunnel control
#define   PCIE_TUNNEL_ENABLE      0x01  // Bit 0: Tunnel enable
#define REG_PCIE_CTRL_B402      XDATA_REG8(0xB402)
#define   PCIE_CTRL_B402_BIT0     0x01  // Bit 0: Control flag 0
#define   PCIE_CTRL_B402_BIT1     0x02  // Bit 1: Control flag 1
#define REG_PCIE_LINK_PARAM_B404 XDATA_REG8(0xB404) // PCIe link parameters
#define   PCIE_LINK_PARAM_MASK    0x0F  // Bits 0-3: Link parameters

// PCIe Tunnel Adapter Configuration (0xB410-0xB42B)
// These registers configure the USB4 PCIe tunnel adapter path
#define REG_TUNNEL_CFG_A_LO     XDATA_REG8(0xB410)  // Tunnel config A low (from 0x0A53)
#define REG_TUNNEL_CFG_A_HI     XDATA_REG8(0xB411)  // Tunnel config A high (from 0x0A52)
#define REG_TUNNEL_CREDITS      XDATA_REG8(0xB412)  // Tunnel credits (from 0x0A55)
#define REG_TUNNEL_CFG_MODE     XDATA_REG8(0xB413)  // Tunnel mode config (from 0x0A54)
#define REG_TUNNEL_CAP_0        XDATA_REG8(0xB415)  // Tunnel capability 0 (fixed 0x06)
#define REG_TUNNEL_CAP_1        XDATA_REG8(0xB416)  // Tunnel capability 1 (fixed 0x04)
#define REG_TUNNEL_CAP_2        XDATA_REG8(0xB417)  // Tunnel capability 2 (fixed 0x00)
#define REG_TUNNEL_PATH_CREDITS XDATA_REG8(0xB418)  // Tunnel path credits (from 0x0A55)
#define REG_TUNNEL_PATH_MODE    XDATA_REG8(0xB419)  // Tunnel path mode (from 0x0A54)
#define REG_TUNNEL_LINK_CFG_LO  XDATA_REG8(0xB41A)  // Tunnel link config low (from 0x0A53)
#define REG_TUNNEL_LINK_CFG_HI  XDATA_REG8(0xB41B)  // Tunnel link config high (from 0x0A52)
#define REG_TUNNEL_DATA_LO      XDATA_REG8(0xB420)  // Tunnel data register low
#define REG_TUNNEL_DATA_HI      XDATA_REG8(0xB421)  // Tunnel data register high
#define REG_TUNNEL_STATUS_0     XDATA_REG8(0xB422)  // Tunnel status byte 0
#define REG_TUNNEL_STATUS_1     XDATA_REG8(0xB423)  // Tunnel status byte 1

#define REG_PCIE_LANE_COUNT     XDATA_REG8(0xB424)
#define REG_TUNNEL_CAP2_0       XDATA_REG8(0xB425)  // Tunnel capability set 2 (fixed 0x06)
#define REG_TUNNEL_CAP2_1       XDATA_REG8(0xB426)  // Tunnel capability set 2 (fixed 0x04)
#define REG_TUNNEL_CAP2_2       XDATA_REG8(0xB427)  // Tunnel capability set 2 (fixed 0x00)
#define REG_TUNNEL_PATH2_CRED   XDATA_REG8(0xB428)  // Tunnel path 2 credits
#define REG_TUNNEL_PATH2_MODE   XDATA_REG8(0xB429)  // Tunnel path 2 mode
#define REG_TUNNEL_AUX_CFG_LO   XDATA_REG8(0xB42A)  // Tunnel auxiliary config low
#define REG_TUNNEL_AUX_CFG_HI   XDATA_REG8(0xB42B)  // Tunnel auxiliary config high

// Adapter Link State (0xB430-0xB4C8)
#define REG_TUNNEL_LINK_STATE   XDATA_REG8(0xB430)  // Tunnel link state (bit 0 = up)
#define REG_POWER_CTRL_B432     XDATA_REG8(0xB432)  // Power control for lanes
#define REG_PCIE_LINK_STATE     XDATA_REG8(0xB434)  // PCIe link state (low nibble = lane mask)
#define REG_POWER_CTRL_B455     XDATA_REG8(0xB455)  /* Power control */
#define REG_POWER_LANE_B404     REG_PCIE_LINK_PARAM_B404  // Alias for power lane config
#define   PCIE_LINK_STATE_MASK    0x0F  // Bits 0-3: PCIe link state/lane mask
#define REG_PCIE_LANE_CONFIG    XDATA_REG8(0xB436)  // PCIe lane configuration
#define   PCIE_LANE_CFG_LO_MASK   0x0F  // Bits 0-3: Low config
#define   PCIE_LANE_CFG_HI_MASK   0xF0  // Bits 4-7: High config

/*
 * PCIe Tunnel Link Control (0xB480-0xB482)
 * Controls USB4/Thunderbolt PCIe tunnel state.
 *
 * REG_TUNNEL_LINK_CTRL (0xB480) is critical for USB descriptor DMA:
 *   Bit 0 must be SET to prevent firmware at 0x20DA from clearing
 *   XDATA[0x0AF7] which would disable the descriptor DMA path.
 */
#define REG_TUNNEL_LINK_CTRL    XDATA_REG8(0xB480)  /* PCIe link state - must be SET for USB DMA */
#define   TUNNEL_LINK_UP          0x01  // Bit 0: PCIe tunnel link is up
#define   TUNNEL_LINK_ACTIVE      0x02  // Bit 1: Tunnel active
#define REG_TUNNEL_ADAPTER_MODE XDATA_REG8(0xB482)  /* Tunnel adapter mode */
#define   TUNNEL_MODE_MASK        0xF0  // Bits 4-7: Tunnel mode
#define   TUNNEL_MODE_ENABLED     0xF0  // High nibble 0xF0 = tunnel mode enabled

#define REG_PCIE_LINK_STATUS_ALT XDATA_REG16(0xB4AE)
#define REG_PCIE_LANE_MASK      XDATA_REG8(0xB4C8)

// PCIe Queue Registers (0xB80C-0xB80F)
#define REG_PCIE_QUEUE_INDEX_LO XDATA_REG8(0xB80C)  // Queue index low
#define REG_PCIE_QUEUE_INDEX_HI XDATA_REG8(0xB80D)  // Queue index high
#define REG_PCIE_QUEUE_FLAGS_LO XDATA_REG8(0xB80E)  // Queue flags low
#define   PCIE_QUEUE_FLAG_VALID    0x01  // Bit 0: Queue entry valid
#define REG_PCIE_QUEUE_FLAGS_HI XDATA_REG8(0xB80F)  // Queue flags high
#define   PCIE_QUEUE_ID_MASK       0x0E  // Bits 1-3: Queue ID (shifted)

//=============================================================================
// UART Controller (0xC000-0xC00F)
// Based on ASM1142 UART at 0xF100-0xF10A, adapted for ASM2464PD
// Default config: 921600 baud, 8O1 (set LCR=0x03 for 8N1)
//=============================================================================
#define REG_UART_RBR            XDATA_REG8(0xC000)  // Receive Buffer Register (RO)
#define REG_UART_THR            XDATA_REG8(0xC001)  // Transmit Holding Register (WO)
#define REG_UART_IER            XDATA_REG8(0xC002)  // Interrupt Enable Register
#define REG_UART_IIR            XDATA_REG8(0xC004)  // Interrupt Identification Register (RO)
#define REG_UART_FCR            XDATA_REG8(0xC004)  // FIFO Control Register (WO)
#define REG_UART_RFBR           XDATA_REG8(0xC005)  // RX FIFO Bytes Received (RO) - count of bytes in RX FIFO
#define REG_UART_TFBF           XDATA_REG8(0xC006)  // TX FIFO Bytes Free (RO)
#define REG_UART_LCR            XDATA_REG8(0xC007)  // Line Control Register
#define   LCR_DATA_BITS_MASK      0x03  // Bits 0-1: 0=5, 1=6, 2=7, 3=8 data bits
#define   LCR_STOP_BITS           0x04  // Bit 2: 0=1 stop, 1=2 stop bits
#define   LCR_PARITY_MASK         0x38  // Bits 3-5: Parity (XX0=None, 001=Odd, 011=Even)
#define   LCR_LOOPBACK            0x80  // Bit 7: Enable loopback mode
#define REG_UART_MCR            XDATA_REG8(0xC008)  // Modem Control Register
#define REG_UART_LSR            XDATA_REG8(0xC009)  // Line Status Register
#define   LSR_RX_FIFO_OVERFLOW    0x01  // Bit 0: RX FIFO overflow (RW1C)
#define   LSR_TX_EMPTY            0x20  // Bit 5: TX empty
#define REG_UART_MSR            XDATA_REG8(0xC00A)  // Modem Status Register
#define REG_UART_STATUS         XDATA_REG8(0xC00E)  // UART status (bits 0-2 = busy flags)

//=============================================================================
// Link/PHY Control Registers (0xC200-0xC2FF)
//=============================================================================
#define REG_LINK_CTRL           XDATA_REG8(0xC202)
#define REG_LINK_CONFIG         XDATA_REG8(0xC203)
#define REG_LINK_STATUS         XDATA_REG8(0xC204)
#define REG_PHY_CTRL            XDATA_REG8(0xC205)
#define REG_PHY_LINK_CTRL_C208  XDATA_REG8(0xC208)
#define REG_PHY_LINK_CONFIG_C20C XDATA_REG8(0xC20C)
#define REG_PHY_CONFIG          XDATA_REG8(0xC233)
#define   PHY_CONFIG_MODE_MASK    0x03  // Bits 0-1: PHY config mode
#define REG_PHY_STATUS          XDATA_REG8(0xC284)
#define REG_PHY_VENDOR_CTRL_C2E0 XDATA_REG8(0xC2E0)  /* PHY vendor control (bit 6/7 = read control) */
#define REG_PHY_VENDOR_CTRL_C2E2 XDATA_REG8(0xC2E2)  /* PHY vendor control 2 (bit 6/7 = read control) */

//=============================================================================
// Vendor/Debug Registers (0xC300-0xC3FF)
//=============================================================================
#define REG_VENDOR_CTRL_C343    XDATA_REG8(0xC343)  /* Vendor control (bit 6 = enable, bit 5 = mode) */
#define   VENDOR_CTRL_C343_BIT5   0x20              /* Bit 5: Vendor mode */
#define   VENDOR_CTRL_C343_BIT6   0x40              /* Bit 6: Vendor enable */
#define REG_VENDOR_CTRL_C360    XDATA_REG8(0xC360)  /* Vendor control (bit 6/7 = read control) */
#define REG_VENDOR_CTRL_C362    XDATA_REG8(0xC362)  /* Vendor control 2 (bit 6/7 = read control) */

//=============================================================================
// NVMe Interface Registers (0xC400-0xC5FF)
//=============================================================================
// NVMe DMA control (0xC4ED-0xC4EF)
#define REG_NVME_DMA_CTRL_ED    XDATA_REG8(0xC4ED)  // NVMe DMA control
#define REG_NVME_DMA_ADDR_LO    XDATA_REG8(0xC4EE)  // NVMe DMA address low
#define REG_NVME_DMA_ADDR_HI    XDATA_REG8(0xC4EF)  // NVMe DMA address high
#define REG_NVME_CTRL           XDATA_REG8(0xC400)
#define REG_NVME_STATUS         XDATA_REG8(0xC401)
#define REG_NVME_CTRL_STATUS    XDATA_REG8(0xC412)
#define   NVME_CTRL_STATUS_READY  0x02  // Bit 1: NVMe controller ready
#define REG_NVME_CONFIG         XDATA_REG8(0xC413)
#define   NVME_CONFIG_MASK_LO    0x3F  // Bits 0-5: Config value
#define   NVME_CONFIG_MASK_HI    0xC0  // Bits 6-7: Config mode
#define REG_NVME_DATA_CTRL      XDATA_REG8(0xC414)
#define   NVME_DATA_CTRL_MASK     0xC0  // Bits 6-7: Data control mode
#define   NVME_DATA_CTRL_BIT7     0x80  // Bit 7: Data control high bit
#define REG_NVME_DEV_STATUS     XDATA_REG8(0xC415)
#define   NVME_DEV_STATUS_MASK    0xC0  // Bits 6-7: Device status
// NVMe SCSI Command Buffer (0xC4C0-0xC4CA) - used for SCSI to NVMe translation
#define REG_NVME_SCSI_CMD_BUF_0 XDATA_REG8(0xC4C0)  // SCSI cmd buffer byte 0
#define REG_NVME_SCSI_CMD_BUF_1 XDATA_REG8(0xC4C1)  // SCSI cmd buffer byte 1
#define REG_NVME_SCSI_CMD_BUF_2 XDATA_REG8(0xC4C2)  // SCSI cmd buffer byte 2
#define REG_NVME_SCSI_CMD_BUF_3 XDATA_REG8(0xC4C3)  // SCSI cmd buffer byte 3
#define REG_NVME_SCSI_CMD_LEN_0 XDATA_REG8(0xC4C4)  // SCSI cmd length byte 0
#define REG_NVME_SCSI_CMD_LEN_1 XDATA_REG8(0xC4C5)  // SCSI cmd length byte 1
#define REG_NVME_SCSI_CMD_LEN_2 XDATA_REG8(0xC4C6)  // SCSI cmd length byte 2
#define REG_NVME_SCSI_CMD_LEN_3 XDATA_REG8(0xC4C7)  // SCSI cmd length byte 3
#define REG_NVME_SCSI_TAG       XDATA_REG8(0xC4C8)  // SCSI command tag
#define REG_NVME_SCSI_CTRL      XDATA_REG8(0xC4C9)  // SCSI control byte
#define REG_NVME_SCSI_DATA      XDATA_REG8(0xC4CA)  // SCSI data byte

#define REG_NVME_CMD            XDATA_REG8(0xC420)
#define REG_NVME_CMD_OPCODE     XDATA_REG8(0xC421)
#define REG_NVME_LBA_LOW        XDATA_REG8(0xC422)
#define REG_NVME_LBA_MID        XDATA_REG8(0xC423)
#define REG_NVME_LBA_HIGH       XDATA_REG8(0xC424)
#define REG_NVME_COUNT_LOW      XDATA_REG8(0xC425)
#define REG_NVME_COUNT_HIGH     XDATA_REG8(0xC426)
#define REG_NVME_ERROR          XDATA_REG8(0xC427)
#define REG_NVME_QUEUE_CFG      XDATA_REG8(0xC428)
#define   NVME_QUEUE_CFG_MASK_LO  0x03  // Bits 0-1: Queue config low
#define   NVME_QUEUE_CFG_BIT3     0x08  // Bit 3: Queue config flag
#define REG_NVME_CMD_PARAM      XDATA_REG8(0xC429)
#define   NVME_CMD_PARAM_TYPE    0xE0  // Bits 5-7: Command parameter type
#define REG_NVME_DOORBELL       XDATA_REG8(0xC42A)
#define   NVME_DOORBELL_TRIGGER   0x01  // Bit 0: Doorbell trigger
#define   NVME_DOORBELL_MODE      0x08  // Bit 3: Doorbell mode
#define REG_NVME_CMD_FLAGS      XDATA_REG8(0xC42B)
// Note: 0xC42C-0xC42D are USB MSC registers, not NVMe
#define REG_USB_MSC_CTRL        XDATA_REG8(0xC42C)
#define REG_USB_MSC_STATUS      XDATA_REG8(0xC42D)
#define REG_NVME_CMD_PRP1       XDATA_REG8(0xC430)  // NVMe command PRP1
#define REG_NVME_CMD_PRP2       XDATA_REG8(0xC431)
#define REG_NVME_CMD_CDW10      XDATA_REG8(0xC435)
#define REG_NVME_INIT_CTRL      XDATA_REG8(0xC438)  // NVMe init control (set to 0xFF)
#define REG_NVME_CMD_CDW11      XDATA_REG8(0xC439)
#define REG_NVME_INT_MASK_A     XDATA_REG8(0xC43A)  /* NVMe/Interrupt mask A (init: 0xFF) */
#define REG_NVME_INT_MASK_B     XDATA_REG8(0xC43B)  /* NVMe/Interrupt mask B (init: 0xFF) */
#define REG_NVME_QUEUE_PTR      XDATA_REG8(0xC43D)
#define REG_NVME_QUEUE_DEPTH    XDATA_REG8(0xC43E)
#define REG_NVME_PHASE          XDATA_REG8(0xC43F)
#define REG_NVME_QUEUE_CTRL     XDATA_REG8(0xC440)
#define REG_NVME_SQ_HEAD        XDATA_REG8(0xC441)
#define REG_NVME_SQ_TAIL        XDATA_REG8(0xC442)
#define REG_NVME_CQ_HEAD        XDATA_REG8(0xC443)
#define REG_NVME_CQ_TAIL        XDATA_REG8(0xC444)
#define REG_NVME_CQ_STATUS      XDATA_REG8(0xC445)
#define REG_NVME_LBA_3          XDATA_REG8(0xC446)
#define REG_NVME_INIT_CTRL2     XDATA_REG8(0xC448)  // NVMe init control 2 (set to 0xFF)
#define REG_NVME_CMD_STATUS_50  XDATA_REG8(0xC450)  // NVMe command status
#define REG_NVME_QUEUE_STATUS_51 XDATA_REG8(0xC451) // NVMe queue status
#define   NVME_QUEUE_STATUS_51_MASK 0x1F  // Bits 0-4: Queue status index
#define REG_DMA_ENTRY           XDATA_REG16(0xC462)
#define REG_CMDQ_DIR_END        XDATA_REG16(0xC470)
#define REG_NVME_QUEUE_BUSY     XDATA_REG8(0xC471)  /* Queue busy status */
#define   NVME_QUEUE_BUSY_BIT     0x01              /* Bit 0: Queue busy */
#define REG_NVME_LINK_CTRL      XDATA_REG8(0xC472)  // NVMe link control
#define REG_NVME_LINK_PARAM     XDATA_REG8(0xC473)  // NVMe link parameter (bit 4)
#define REG_NVME_CMD_STATUS_C47A XDATA_REG8(0xC47A) // NVMe command status (used by usb_ep_loop)
#define REG_NVME_DMA_CTRL_C4E9  XDATA_REG8(0xC4E9)  // NVMe DMA control extended
#define REG_NVME_PARAM_C4EA     XDATA_REG8(0xC4EA)  // NVMe parameter storage
#define REG_NVME_PARAM_C4EB     XDATA_REG8(0xC4EB)  // NVMe parameter storage high
#define REG_NVME_BUF_CFG        XDATA_REG8(0xC508)  // NVMe buffer configuration
#define   NVME_BUF_CFG_MASK_LO   0x3F  // Bits 0-5: Buffer index
#define   NVME_BUF_CFG_MASK_HI   0xC0  // Bits 6-7: Buffer mode
#define REG_NVME_QUEUE_INDEX    XDATA_REG8(0xC512)
#define REG_NVME_QUEUE_PENDING  XDATA_REG8(0xC516)  /* Pending queue status */
#define   NVME_QUEUE_PENDING_IDX  0x3F              /* Bits 0-5: Queue index */
#define REG_NVME_QUEUE_TRIGGER  XDATA_REG8(0xC51A)
#define REG_NVME_QUEUE_STATUS   XDATA_REG8(0xC51E)
#define   NVME_QUEUE_STATUS_IDX   0x3F  // Bits 0-5: Queue index
#define REG_NVME_LINK_STATUS    XDATA_REG8(0xC520)
#define   NVME_LINK_STATUS_BIT1   0x02  // Bit 1: NVMe link status flag
#define   NVME_LINK_STATUS_BIT7   0x80  // Bit 7: NVMe link ready

//=============================================================================
// PHY Extended Registers (0xC600-0xC6FF)
//=============================================================================
#define REG_PHY_EXT_2D          XDATA_REG8(0xC62D)
#define   PHY_EXT_LANE_MASK       0x07  // Bits 0-2: Lane configuration
#define REG_PHY_CFG_C655        XDATA_REG8(0xC655)  /* PHY config (bit 3 set by flash_set_bit3) */
#define REG_PHY_EXT_56          XDATA_REG8(0xC656)
#define   PHY_EXT_SIGNAL_CFG      0x20  // Bit 5: Signal config
#define REG_PCIE_LANE_CTRL_C659 XDATA_REG8(0xC659)  /* PCIe lane control */
#define REG_PHY_CFG_C65A        XDATA_REG8(0xC65A)  /* PHY config (bit 3 set by flash_set_bit3) */
#define   PHY_CFG_C65A_BIT3       0x08  // Bit 3: PHY config flag
#define REG_PHY_EXT_5B          XDATA_REG8(0xC65B)
#define   PHY_EXT_ENABLE          0x08  // Bit 3: PHY extended enable
#define   PHY_EXT_MODE            0x20  // Bit 5: PHY mode
#define REG_PHY_EXT_B3          XDATA_REG8(0xC6B3)
#define   PHY_EXT_LINK_READY      0x30  // Bits 4,5: Link ready status
#define REG_PHY_LINK_CTRL_BD    XDATA_REG8(0xC6BD)  /* PHY link control (bit 0 = enable) */
#define REG_PHY_CFG_C6A8        XDATA_REG8(0xC6A8)  /* PHY config (bit 0 = enable) */
#define REG_PHY_VENDOR_CTRL_C6DB XDATA_REG8(0xC6DB) /* PHY vendor control (bit 2 = status) */
#define   PHY_VENDOR_CTRL_C6DB_BIT2 0x04            /* Bit 2: Vendor status flag */

//=============================================================================
// Interrupt Controller (0xC800-0xC80F)
//=============================================================================
#define REG_INT_STATUS_C800     XDATA_REG8(0xC800)  /* Interrupt status register */
#define   INT_STATUS_PCIE         0x04  // Bit 2: PCIe interrupt status
#define REG_INT_ENABLE          XDATA_REG8(0xC801)  /* Interrupt enable register */
#define   INT_ENABLE_GLOBAL       0x01  // Bit 0: Global interrupt enable
#define   INT_ENABLE_USB          0x02  // Bit 1: USB interrupt enable
#define   INT_ENABLE_PCIE         0x04  // Bit 2: PCIe interrupt enable
#define   INT_ENABLE_SYSTEM       0x10  // Bit 4: System interrupt enable
#define REG_INT_USB_STATUS      XDATA_REG8(0xC802)  /* USB interrupt status */
#define   INT_USB_MASTER          0x01  // Bit 0: USB master interrupt
#define   INT_USB_NVME_QUEUE      0x04  // Bit 2: NVMe queue processing
#define REG_INT_AUX_STATUS      XDATA_REG8(0xC805)  /* Auxiliary interrupt status */
#define   INT_AUX_ENABLE          0x02  // Bit 1: Auxiliary enable
#define   INT_AUX_STATUS          0x04  // Bit 2: Auxiliary status
#define REG_INT_SYSTEM          XDATA_REG8(0xC806)  /* System interrupt status */
#define   INT_SYSTEM_EVENT        0x01  // Bit 0: System event interrupt
#define   INT_SYSTEM_TIMER        0x10  // Bit 4: System timer event
#define   INT_SYSTEM_LINK         0x20  // Bit 5: Link state change
#define REG_INT_CTRL            XDATA_REG8(0xC809)  /* Interrupt control register */
#define REG_INT_PCIE_NVME       XDATA_REG8(0xC80A)  /* PCIe/NVMe interrupt status */
#define   INT_PCIE_NVME_EVENTS    0x0F  // Bits 0-3: PCIe event flags
#define   INT_PCIE_NVME_TIMER     0x10  // Bit 4: NVMe command completion
#define   INT_PCIE_NVME_EVENT     0x20  // Bit 5: PCIe link event
#define   INT_PCIE_NVME_STATUS    0x40  // Bit 6: NVMe queue interrupt

//=============================================================================
// I2C Controller (0xC870-0xC87F)
//=============================================================================
#define REG_I2C_ADDR            XDATA_REG8(0xC870)
#define REG_I2C_MODE            XDATA_REG8(0xC871)
#define REG_I2C_LEN             XDATA_REG8(0xC873)
#define REG_I2C_CSR             XDATA_REG8(0xC875)
#define REG_I2C_SRC             XDATA_REG32(0xC878)
#define REG_I2C_DST             XDATA_REG32(0xC87C)
#define REG_I2C_CSR_ALT         XDATA_REG8(0xC87F)

//=============================================================================
// Alternate Flash Controller (0xC880-0xC886)
//=============================================================================
#define REG_FLASH_CMD_ALT       XDATA_REG8(0xC880)  /* Alternate flash command */
#define REG_FLASH_CSR_ALT       XDATA_REG8(0xC881)  /* Alternate flash CSR */
#define REG_FLASH_ADDR_LO_ALT   XDATA_REG8(0xC882)  /* Alternate flash addr low */
#define REG_FLASH_ADDR_MD_ALT   XDATA_REG8(0xC883)  /* Alternate flash addr mid */
#define REG_FLASH_ADDR_HI_ALT   XDATA_REG8(0xC884)  /* Alternate flash addr high */
#define REG_FLASH_DATA_LEN_ALT  XDATA_REG8(0xC885)  /* Alternate flash data len */
#define REG_FLASH_DATA_HI_ALT   XDATA_REG8(0xC886)  /* Alternate flash data len hi */

//=============================================================================
// SPI Flash Controller (0xC89F-0xC8AE)
//=============================================================================
#define REG_FLASH_CON           XDATA_REG8(0xC89F)
#define REG_FLASH_ADDR_LO       XDATA_REG8(0xC8A1)
#define REG_FLASH_ADDR_MD       XDATA_REG8(0xC8A2)
#define REG_FLASH_DATA_LEN      XDATA_REG8(0xC8A3)
#define REG_FLASH_DATA_LEN_HI   XDATA_REG8(0xC8A4)
#define REG_FLASH_DIV           XDATA_REG8(0xC8A6)
#define REG_FLASH_CSR           XDATA_REG8(0xC8A9)
#define   FLASH_CSR_BUSY          0x01  // Bit 0: Flash controller busy
#define REG_FLASH_CMD           XDATA_REG8(0xC8AA)
#define REG_FLASH_ADDR_HI       XDATA_REG8(0xC8AB)
#define REG_FLASH_ADDR_LEN      XDATA_REG8(0xC8AC)
#define   FLASH_ADDR_LEN_MASK     0xFC  // Bits 2-7: Address length (upper bits)
#define REG_FLASH_MODE          XDATA_REG8(0xC8AD)
#define   FLASH_MODE_ENABLE       0x01  // Bit 0: Flash mode enable
#define REG_FLASH_BUF_OFFSET    XDATA_REG16(0xC8AE)
#define REG_FLASH_BUF_OFFSET_LO XDATA_REG8(0xC8AE)  /* Flash buffer offset low byte */
#define REG_FLASH_BUF_OFFSET_HI XDATA_REG8(0xC8AF)  /* Flash buffer offset high byte */

//=============================================================================
// DMA Engine Registers (0xC8B0-0xC8D9)
//=============================================================================
#define REG_DMA_MODE            XDATA_REG8(0xC8B0)
#define REG_DMA_CHAN_AUX        XDATA_REG8(0xC8B2)
#define REG_DMA_CHAN_AUX1       XDATA_REG8(0xC8B3)
#define REG_DMA_XFER_CNT_HI     XDATA_REG8(0xC8B4)
#define REG_DMA_XFER_CNT_LO     XDATA_REG8(0xC8B5)
#define REG_DMA_CHAN_CTRL2      XDATA_REG8(0xC8B6)
#define   DMA_CHAN_CTRL2_START    0x01  // Bit 0: Start/busy
#define   DMA_CHAN_CTRL2_DIR      0x02  // Bit 1: Direction
#define   DMA_CHAN_CTRL2_ENABLE   0x04  // Bit 2: Enable
#define   DMA_CHAN_CTRL2_ACTIVE   0x80  // Bit 7: Active
#define REG_DMA_CHAN_STATUS2    XDATA_REG8(0xC8B7)
#define REG_DMA_TRIGGER         XDATA_REG8(0xC8B8)
#define   DMA_TRIGGER_START       0x01  // Bit 0: Trigger transfer
#define REG_DMA_CONFIG          XDATA_REG8(0xC8D4)
#define REG_DMA_QUEUE_IDX       XDATA_REG8(0xC8D5)
#define REG_DMA_STATUS          XDATA_REG8(0xC8D6)
#define   DMA_STATUS_TRIGGER      0x01  // Bit 0: Status trigger
#define   DMA_STATUS_DONE         0x04  // Bit 2: Done flag
#define   DMA_STATUS_ERROR        0x08  // Bit 3: Error flag
#define REG_DMA_CTRL            XDATA_REG8(0xC8D7)
#define REG_DMA_STATUS2         XDATA_REG8(0xC8D8)
#define   DMA_STATUS2_TRIGGER     0x01  // Bit 0: Status 2 trigger
#define REG_DMA_STATUS3         XDATA_REG8(0xC8D9)
#define   DMA_STATUS3_UPPER      0xF8  // Bits 3-7: Status upper bits

//=============================================================================
// CPU Mode/Control (0xCA00-0xCAFF)
//=============================================================================
#define REG_CPU_MODE_NEXT       XDATA_REG8(0xCA06)
#define REG_CPU_CTRL_CA2E       XDATA_REG8(0xCA2E)  /* CPU control */
#define REG_CPU_CTRL_CA60       XDATA_REG8(0xCA60)  /* CPU control CA60 */
#define REG_CPU_CTRL_CA70       XDATA_REG8(0xCA70)  /* CPU control */
#define REG_CPU_CTRL_CA81       XDATA_REG8(0xCA81)  /* CPU control CA81 - PCIe init */

//=============================================================================
// Timer Registers (0xCC10-0xCC24)
//=============================================================================
#define REG_TIMER0_DIV          XDATA_REG8(0xCC10)
#define REG_TIMER0_CSR          XDATA_REG8(0xCC11)
#define   TIMER_CSR_ENABLE        0x01  // Bit 0: Timer enable
#define   TIMER_CSR_EXPIRED       0x02  // Bit 1: Timer expired flag
#define   TIMER_CSR_CLEAR         0x04  // Bit 2: Clear interrupt
#define REG_TIMER0_THRESHOLD    XDATA_REG16(0xCC12)
#define REG_TIMER0_THRESHOLD_HI XDATA_REG8(0xCC12)  /* Timer 0 threshold high byte */
#define REG_TIMER0_THRESHOLD_LO XDATA_REG8(0xCC13)  /* Timer 0 threshold low byte */
#define REG_TIMER1_DIV          XDATA_REG8(0xCC16)
#define REG_TIMER1_CSR          XDATA_REG8(0xCC17)
#define REG_TIMER1_THRESHOLD    XDATA_REG16(0xCC18)
#define REG_TIMER2_DIV          XDATA_REG8(0xCC1C)
#define REG_TIMER2_CSR          XDATA_REG8(0xCC1D)
#define REG_TIMER2_THRESHOLD    XDATA_REG16(0xCC1E)
#define REG_TIMER2_THRESHOLD_LO XDATA_REG8(0xCC1E)  /* Timer 2 threshold low */
#define REG_TIMER2_THRESHOLD_HI XDATA_REG8(0xCC1F)  /* Timer 2 threshold high */
#define REG_TIMER3_DIV          XDATA_REG8(0xCC22)
#define REG_TIMER3_CSR          XDATA_REG8(0xCC23)
#define REG_TIMER3_IDLE_TIMEOUT XDATA_REG8(0xCC24)

//=============================================================================
// CPU Control Extended (0xCC30-0xCCFF)
//=============================================================================
#define REG_CPU_MODE            XDATA_REG8(0xCC30)  /* CPU mode control */
#define   CPU_MODE_NORMAL         0x00  // Normal operation
#define   CPU_MODE_RESET          0x01  // Reset mode
#define REG_CPU_EXEC_CTRL       XDATA_REG8(0xCC31)  /* CPU execution control */
#define   CPU_EXEC_ENABLE         0x01  // Bit 0: Execution enable
#define REG_CPU_EXEC_STATUS     XDATA_REG8(0xCC32)  /* CPU execution status */
#define   CPU_EXEC_STATUS_ACTIVE  0x01  // Bit 0: CPU execution active
#define REG_CPU_EXEC_STATUS_2   XDATA_REG8(0xCC33)  /* CPU execution status 2 */
#define   CPU_EXEC_STATUS_2_INT   0x04  // Bit 2: Interrupt pending
#define REG_CPU_EXEC_CTRL_2     XDATA_REG8(0xCC34)  /* CPU execution control 2 */
#define REG_CPU_EXEC_STATUS_3   XDATA_REG8(0xCC35)  /* CPU execution status 3 */
#define   CPU_EXEC_STATUS_3_BIT0  0x01  // Bit 0: Exec active flag
#define   CPU_EXEC_STATUS_3_BIT2  0x04  // Bit 2: Exec status flag
#define REG_CPU_CTRL_CC36       XDATA_REG8(0xCC36)  /* CPU control */
#define REG_CPU_CTRL_CC37       XDATA_REG8(0xCC37)  /* CPU control */
// Timer enable/disable control registers
#define REG_TIMER_ENABLE_A      XDATA_REG8(0xCC38)  /* Timer enable control A */
#define   TIMER_ENABLE_A_BIT      0x02              /* Bit 1: Timer enable */
#define REG_TIMER_CTRL_CC39     XDATA_REG8(0xCC39)  /* Timer control */
#define REG_TIMER_ENABLE_B      XDATA_REG8(0xCC3A)  /* Timer enable control B */
#define   TIMER_ENABLE_B_BIT      0x02              /* Bit 1: Timer enable */
#define   TIMER_ENABLE_B_BITS56   0x60              /* Bits 5-6: Timer extended mode */
#define REG_TIMER_CTRL_CC3B     XDATA_REG8(0xCC3B)  /* Timer control */
#define   TIMER_CTRL_ENABLE       0x01              /* Bit 0: Timer active */
#define   TIMER_CTRL_START        0x02              /* Bit 1: Timer start */
#define REG_CPU_CTRL_CC3D       XDATA_REG8(0xCC3D)
#define REG_CPU_CTRL_CC3E       XDATA_REG8(0xCC3E)
#define REG_CPU_CTRL_CC3F       XDATA_REG8(0xCC3F)

// Timer 4 Registers (0xCC5C-0xCC5F)
#define REG_TIMER4_DIV          XDATA_REG8(0xCC5C)  /* Timer 4 divisor */
#define REG_TIMER4_CSR          XDATA_REG8(0xCC5D)  /* Timer 4 control/status */
#define REG_TIMER4_THRESHOLD_LO XDATA_REG8(0xCC5E)  /* Timer 4 threshold low */
#define REG_TIMER4_THRESHOLD_HI XDATA_REG8(0xCC5F)  /* Timer 4 threshold high */

// CPU control registers (0xCC80-0xCC83)
#define REG_CPU_CTRL_CC80       XDATA_REG8(0xCC80)  /* CPU control 0xCC80 */
#define   CPU_CTRL_CC80_ENABLE   0x03  // Bits 0-1: CPU control enable mask
#define REG_CPU_INT_CTRL        XDATA_REG8(0xCC81)
#define   CPU_INT_CTRL_ENABLE    0x01  // Bit 0: Enable/start interrupt
#define   CPU_INT_CTRL_ACK       0x02  // Bit 1: Acknowledge interrupt
#define   CPU_INT_CTRL_TRIGGER   0x04  // Bit 2: Trigger interrupt
#define REG_CPU_CTRL_CC82       XDATA_REG8(0xCC82)  /* CPU control 0xCC82 */
#define REG_CPU_CTRL_CC83       XDATA_REG8(0xCC83)  /* CPU control 0xCC83 */

// Transfer DMA controller - for internal memory block transfers
#define REG_XFER_DMA_CTRL       XDATA_REG8(0xCC88)  /* Transfer DMA control */
#define REG_XFER_DMA_CMD        XDATA_REG8(0xCC89)  /* Transfer DMA command/status */
#define   XFER_DMA_CMD_START     0x01  // Bit 0: Start transfer
#define   XFER_DMA_CMD_DONE      0x02  // Bit 1: Transfer complete
#define   XFER_DMA_CMD_MODE      0x30  // Bits 4-5: Transfer mode (0x31 = mode 1)
#define REG_XFER_DMA_ADDR_LO    XDATA_REG8(0xCC8A)  /* Transfer DMA address low */
#define REG_XFER_DMA_ADDR_HI    XDATA_REG8(0xCC8B)  /* Transfer DMA address high */

#define REG_CPU_DMA_CTRL_CC90   XDATA_REG8(0xCC90)  /* CPU DMA control */
#define REG_CPU_DMA_INT         XDATA_REG8(0xCC91)  /* CPU DMA interrupt status */
#define   CPU_DMA_INT_ACK        0x02  // Bit 1: Acknowledge DMA interrupt
#define   CPU_DMA_INT_TRIGGER    0x04  // Bit 2: Trigger DMA
#define REG_CPU_DMA_DATA_LO     XDATA_REG8(0xCC92)  /* CPU DMA data low */
#define REG_CPU_DMA_DATA_HI     XDATA_REG8(0xCC93)  /* CPU DMA data high */
#define REG_CPU_DMA_READY       XDATA_REG8(0xCC98)  /* CPU DMA ready status */
#define   CPU_DMA_READY_BIT2     0x04              /* Bit 2: DMA ready flag */
#define REG_XFER_DMA_CFG        XDATA_REG8(0xCC99)  /* Transfer DMA config */
#define   XFER_DMA_CFG_ACK       0x02  // Bit 1: Acknowledge config
#define   XFER_DMA_CFG_ENABLE    0x04  // Bit 2: Config enable
#define REG_XFER_DMA_DATA_LO    XDATA_REG8(0xCC9A)  /* Transfer DMA data low */
#define REG_XFER_DMA_DATA_HI    XDATA_REG8(0xCC9B)  /* Transfer DMA data high */
// Secondary transfer DMA controller
#define REG_XFER2_DMA_CTRL      XDATA_REG8(0xCCD8)  /* Transfer 2 DMA control */
#define REG_XFER2_DMA_STATUS    XDATA_REG8(0xCCD9)  /* Transfer 2 DMA status */
#define   XFER2_DMA_STATUS_ACK   0x02  // Bit 1: Acknowledge status
#define REG_TIMER5_CSR          XDATA_REG8(0xCCB9)  /* Timer 5 control/status (alternate) */
#define REG_XFER2_DMA_ADDR_LO   XDATA_REG8(0xCCDA)  /* Transfer 2 DMA address low */
#define REG_XFER2_DMA_ADDR_HI   XDATA_REG8(0xCCDB)  /* Transfer 2 DMA address high */
#define REG_CPU_EXT_CTRL        XDATA_REG8(0xCCF8)  /* CPU extended control */
#define REG_CPU_EXT_STATUS      XDATA_REG8(0xCCF9)  /* CPU extended status */
#define   CPU_EXT_STATUS_ACK     0x02  // Bit 1: Acknowledge extended status

//=============================================================================
// CPU Extended Control (0xCD00-0xCD3F)
//=============================================================================
#define REG_CPU_TIMER_CTRL_CD31 XDATA_REG8(0xCD31)  /* CPU timer control */

//=============================================================================
// SCSI DMA Control (0xCE00-0xCE3F)
//=============================================================================
#define REG_SCSI_DMA_CTRL       XDATA_REG8(0xCE00)  // SCSI DMA control register
#define REG_SCSI_DMA_PARAM      XDATA_REG8(0xCE01)  // SCSI DMA parameter register
#define REG_SCSI_DMA_CFG_CE36   XDATA_REG8(0xCE36)  // SCSI DMA config 0xCE36
#define REG_SCSI_DMA_TAG_CE3A   XDATA_REG8(0xCE3A)  // SCSI DMA tag storage

//=============================================================================
// SCSI/Mass Storage DMA (0xCE40-0xCE97)
//=============================================================================
#define REG_SCSI_DMA_PARAM0     XDATA_REG8(0xCE40)
#define REG_SCSI_DMA_PARAM1     XDATA_REG8(0xCE41)
#define REG_SCSI_DMA_PARAM2     XDATA_REG8(0xCE42)
#define REG_SCSI_DMA_PARAM3     XDATA_REG8(0xCE43)
#define REG_SCSI_DMA_PARAM4     XDATA_REG8(0xCE44)
#define REG_SCSI_DMA_PARAM5     XDATA_REG8(0xCE45)
#define REG_SCSI_TAG_IDX        XDATA_REG8(0xCE51)   /* SCSI tag index */
#define REG_SCSI_TAG_VALUE      XDATA_REG8(0xCE55)   /* SCSI tag value */
#define REG_SCSI_DMA_COMPL      XDATA_REG8(0xCE5C)
#define REG_SCSI_DMA_MASK       XDATA_REG8(0xCE5D)  /* SCSI DMA mask register */
#define REG_SCSI_DMA_QUEUE      XDATA_REG8(0xCE5F)  /* SCSI DMA queue control */
#define REG_SCSI_TRANSFER_CTRL  XDATA_REG8(0xCE70)
#define REG_SCSI_TRANSFER_MODE  XDATA_REG8(0xCE72)
#define REG_SCSI_BUF_CTRL0      XDATA_REG8(0xCE73)
#define REG_SCSI_BUF_CTRL1      XDATA_REG8(0xCE74)
#define REG_SCSI_BUF_LEN_LO     XDATA_REG8(0xCE75)
#define REG_SCSI_BUF_ADDR0      XDATA_REG8(0xCE76)
#define REG_SCSI_BUF_ADDR1      XDATA_REG8(0xCE77)
#define REG_SCSI_BUF_ADDR2      XDATA_REG8(0xCE78)
#define REG_SCSI_BUF_ADDR3      XDATA_REG8(0xCE79)
#define REG_SCSI_BUF_CTRL       XDATA_REG8(0xCE80)  /* SCSI buffer control global */
#define REG_SCSI_BUF_THRESH_HI  XDATA_REG8(0xCE81)  /* SCSI buffer threshold high */
#define REG_SCSI_BUF_THRESH_LO  XDATA_REG8(0xCE82)  /* SCSI buffer threshold low */
#define REG_SCSI_BUF_FLOW       XDATA_REG8(0xCE83)  /* SCSI buffer flow control */
#define   SCSI_DMA_COMPL_MODE0    0x01  // Bit 0: Mode 0 complete
#define   SCSI_DMA_COMPL_MODE10   0x02  // Bit 1: Mode 0x10 complete
#define REG_XFER_STATUS_CE60    XDATA_REG8(0xCE60)  // Transfer status CE60
#define   XFER_STATUS_BIT6        0x40  // Bit 6: Status flag
#define REG_XFER_CTRL_CE65      XDATA_REG8(0xCE65)
#define REG_SCSI_DMA_TAG_COUNT  XDATA_REG8(0xCE66)
#define   SCSI_DMA_TAG_MASK       0x1F  // Bits 0-4: Tag count (0-31)
#define REG_SCSI_DMA_QUEUE_STAT XDATA_REG8(0xCE67)
#define   SCSI_DMA_QUEUE_MASK     0x0F  // Bits 0-3: Queue status (0-15)
#define REG_XFER_STATUS_CE6C    XDATA_REG8(0xCE6C)  // Transfer status CE6C (bit 7: ready)
#define REG_SCSI_DMA_STATUS     XDATA_REG16(0xCE6E)
#define REG_SCSI_DMA_STATUS_L   XDATA_REG8(0xCE6E)   /* SCSI DMA status low byte */
#define REG_SCSI_DMA_STATUS_H   XDATA_REG8(0xCE6F)   /* SCSI DMA status high byte */
/*
 * USB/DMA State Machine Control (0xCE86-0xCE89)
 * These registers control USB enumeration and command state transitions.
 *
 * REG_USB_DMA_STATE (0xCE89) is the key state machine control register:
 *   Bit 0: Must be SET to exit initial wait loop (0x348C)
 *   Bit 1: Checked at 0x3493 for successful enumeration path
 *   Bit 2: Controls state 3→4→5 transitions (0x3588)
 */
#define REG_XFER_STATUS_CE86    XDATA_REG8(0xCE86)  /* Transfer status (bit 4 checked at 0x349D) */
#define REG_XFER_CTRL_CE88      XDATA_REG8(0xCE88)  /* DMA trigger - write resets state for new transfer */
#define REG_USB_DMA_STATE       XDATA_REG8(0xCE89)  /* USB/DMA state machine control */
#define   USB_DMA_STATE_READY     0x01  // Bit 0: Exit wait loop, ready for next phase
#define   USB_DMA_STATE_SUCCESS   0x02  // Bit 1: Enumeration/transfer successful
#define   USB_DMA_STATE_COMPLETE  0x04  // Bit 2: State machine complete
#define REG_XFER_CTRL_CE8A      XDATA_REG8(0xCE8A)   /* Transfer control CE8A */
#define REG_XFER_MODE_CE95      XDATA_REG8(0xCE95)
#define REG_SCSI_DMA_CMD_REG    XDATA_REG8(0xCE96)
#define REG_SCSI_DMA_RESP_REG   XDATA_REG8(0xCE97)

//=============================================================================
// USB Descriptor Validation (0xCEB0-0xCEB3)
//=============================================================================
#define REG_USB_DESC_VAL_CEB2   XDATA_REG8(0xCEB2)
#define REG_USB_DESC_VAL_CEB3   XDATA_REG8(0xCEB3)

//=============================================================================
// CPU Link Control (0xCEF0-0xCEFF)
//=============================================================================
#define REG_CPU_LINK_CEF2       XDATA_REG8(0xCEF2)
#define   CPU_LINK_CEF2_READY     0x80  // Bit 7: Link ready
#define REG_CPU_LINK_CEF3       XDATA_REG8(0xCEF3)
#define   CPU_LINK_CEF3_ACTIVE    0x08  // Bit 3: Link active

// USB Endpoint Buffer (0xD800-0xD80F)
// These can be accessed as CSW or as control registers depending on context
#define REG_USB_EP_BUF_CTRL     XDATA_REG8(0xD800)  // Buffer control/mode/sig0
#define REG_USB_EP_BUF_SEL      XDATA_REG8(0xD801)  // Buffer select/sig1
#define REG_USB_EP_BUF_DATA     XDATA_REG8(0xD802)  // Buffer data/sig2
#define REG_USB_EP_BUF_PTR_LO   XDATA_REG8(0xD803)  // Pointer low/sig3
#define REG_USB_EP_BUF_PTR_HI   XDATA_REG8(0xD804)  // Pointer high/tag0
#define REG_USB_EP_BUF_LEN_LO   XDATA_REG8(0xD805)  // Length low/tag1
#define REG_USB_EP_BUF_STATUS   XDATA_REG8(0xD806)  // Status/tag2
#define REG_USB_EP_BUF_LEN_HI   XDATA_REG8(0xD807)  // Length high/tag3
#define REG_USB_EP_RESIDUE0     XDATA_REG8(0xD808)  // Residue byte 0
#define REG_USB_EP_RESIDUE1     XDATA_REG8(0xD809)  // Residue byte 1
#define REG_USB_EP_RESIDUE2     XDATA_REG8(0xD80A)  // Residue byte 2
#define REG_USB_EP_RESIDUE3     XDATA_REG8(0xD80B)  // Residue byte 3
#define REG_USB_EP_CSW_STATUS   XDATA_REG8(0xD80C)  // CSW status
#define REG_USB_EP_CTRL_0D      XDATA_REG8(0xD80D)  // Control 0D
#define REG_USB_EP_CTRL_0E      XDATA_REG8(0xD80E)  // Control 0E
#define REG_USB_EP_CTRL_0F      XDATA_REG8(0xD80F)  // Control 0F
#define REG_USB_EP_CTRL_10      XDATA_REG8(0xD810)  // Control 10
// USB Endpoint buffers at 0xDE30, 0xDE36 (extended region)
#define REG_USB_EP_BUF_DE30     XDATA_REG8(0xDE30)  // Endpoint buffer extended control
#define REG_USB_EP_BUF_DE36     XDATA_REG8(0xDE36)  // Endpoint buffer extended config

// Note: Full struct access at 0xD800 - see structs.h

//=============================================================================
// PHY Completion / Debug (0xE300-0xE3FF)
//=============================================================================
#define REG_PHY_MODE_E302       XDATA_REG8(0xE302)  /* PHY mode (bits 4-5 = lane config) */
#define REG_DEBUG_STATUS_E314   XDATA_REG8(0xE314)
#define REG_PHY_COMPLETION_E318 XDATA_REG8(0xE318)
#define REG_LINK_CTRL_E324      XDATA_REG8(0xE324)
#define   LINK_CTRL_E324_BIT2     0x04  // Bit 2: Link control flag

//=============================================================================
// Command Engine (0xE400-0xE4FF)
//=============================================================================
#define REG_CMD_CTRL_E400       XDATA_REG8(0xE400)  /* Command control (bit 7 = enable, bit 6 = busy) */
#define   CMD_CTRL_E400_BIT6      0x40  // Bit 6: Command busy flag
#define   CMD_CTRL_E400_BIT7      0x80  // Bit 7: Command enable
#define REG_CMD_STATUS_E402     XDATA_REG8(0xE402)  /* Command status (bit 3 = poll status) */
#define REG_CMD_CTRL_E403       XDATA_REG8(0xE403)
#define REG_CMD_CFG_E404        XDATA_REG8(0xE404)
#define REG_CMD_CFG_E405        XDATA_REG8(0xE405)
#define REG_CMD_CTRL_E409       XDATA_REG8(0xE409)  /* Command control (bit 0,7 = flags) */
#define REG_CMD_CFG_E40A        XDATA_REG8(0xE40A)  /* Command config - write 0x0F */
#define REG_CMD_CONFIG          XDATA_REG8(0xE40B)  /* Command config (bit 0 = flag) */
#define REG_CMD_CFG_E40D        XDATA_REG8(0xE40D)  /* Command config - write 0x28 */
#define REG_CMD_CFG_E40E        XDATA_REG8(0xE40E)  /* Command config - write 0x8A */
#define REG_CMD_CTRL_E40F       XDATA_REG8(0xE40F)
#define REG_CMD_CTRL_E410       XDATA_REG8(0xE410)
#define REG_CMD_CFG_E411        XDATA_REG8(0xE411)  /* Command config - write 0xA1 */
#define REG_CMD_CFG_E412        XDATA_REG8(0xE412)  /* Command config - write 0x79 */
#define REG_CMD_CFG_E413        XDATA_REG8(0xE413)  /* Command config (bits 0,1,4,5,6 = flags) */
#define REG_CMD_BUSY_STATUS     XDATA_REG8(0xE41C)
#define   CMD_BUSY_STATUS_BUSY    0x01  // Bit 0: Command engine busy
#define REG_CMD_TRIGGER         XDATA_REG8(0xE420)
#define REG_CMD_MODE_E421       XDATA_REG8(0xE421)
#define REG_CMD_PARAM           XDATA_REG8(0xE422)
#define REG_CMD_STATUS          XDATA_REG8(0xE423)
#define REG_CMD_ISSUE           XDATA_REG8(0xE424)
#define REG_CMD_TAG             XDATA_REG8(0xE425)
#define REG_CMD_LBA_0           XDATA_REG8(0xE426)
#define REG_CMD_LBA_1           XDATA_REG8(0xE427)
#define REG_CMD_LBA_2           XDATA_REG8(0xE428)
#define REG_CMD_LBA_3           XDATA_REG8(0xE429)
#define REG_CMD_COUNT_LOW       XDATA_REG8(0xE42A)
#define REG_CMD_COUNT_HIGH      XDATA_REG8(0xE42B)
#define REG_CMD_LENGTH_LOW      XDATA_REG8(0xE42C)
#define REG_CMD_LENGTH_HIGH     XDATA_REG8(0xE42D)
#define REG_CMD_RESP_TAG        XDATA_REG8(0xE42E)
#define REG_CMD_RESP_STATUS     XDATA_REG8(0xE42F)
#define REG_CMD_CTRL            XDATA_REG8(0xE430)
#define REG_CMD_TIMEOUT         XDATA_REG8(0xE431)
#define REG_CMD_PARAM_L         XDATA_REG8(0xE432)
#define REG_CMD_PARAM_H         XDATA_REG8(0xE433)
#define REG_CMD_EXT_PARAM_0     XDATA_REG8(0xE434)
#define REG_CMD_EXT_PARAM_1     XDATA_REG8(0xE435)

//=============================================================================
// Timer/CPU Control (0xCC00-0xCCFF)
//=============================================================================
#define REG_USB_STATUS_CC89     XDATA_REG8(0xCC89)  /* USB status - poll bit 1 for ready */
#define   USB_STATUS_CC89_BIT1    0x02  // Bit 1: USB ready flag

//=============================================================================
// Debug/Interrupt (0xE600-0xE6FF)
//=============================================================================
#define REG_DEBUG_INT_E62F      XDATA_REG8(0xE62F)  // Debug interrupt 0x62F
#define REG_DEBUG_INT_E65F      XDATA_REG8(0xE65F)  // Debug interrupt 0x65F
#define REG_DEBUG_INT_E661      XDATA_REG8(0xE661)
#define   DEBUG_INT_E661_FLAG     0x80  // Bit 7: Debug interrupt flag
#define REG_PD_CTRL_E66A        XDATA_REG8(0xE66A)  /* PD control - clear bit 4 */
#define   PD_CTRL_E66A_BIT4       0x10  // Bit 4: PD control flag

//=============================================================================
// System Status / Link Control (0xE700-0xE7FF)
//=============================================================================
#define REG_LINK_WIDTH_E710     XDATA_REG8(0xE710)  /* Link width status (bits 5-7) */
#define   LINK_WIDTH_MASK         0xE0  // Bits 5-7: Link width
#define   LINK_WIDTH_LANES_MASK   0x1F  // Bits 0-4: Lane configuration

/*
 * USB EP0 Transfer Complete Status (0xE712)
 * The main loop at 0xCDC6-0xCDD9 polls this register waiting for
 * bits 0 or 1 to be SET to exit the polling loop and process USB events.
 * Without these bits, firmware never reaches USB dispatch at 0xCDE7.
 */
#define REG_USB_EP0_COMPLETE    XDATA_REG8(0xE712)
#define   USB_EP0_COMPLETE_BIT0   0x01  // Bit 0: EP0 transfer complete
#define   USB_EP0_COMPLETE_BIT1   0x02  // Bit 1: EP0 status phase complete

#define REG_LINK_STATUS_E716    XDATA_REG8(0xE716)
#define   LINK_STATUS_E716_MASK  0x03  // Bits 0-1: Link status
#define REG_LINK_CTRL_E717      XDATA_REG8(0xE717)  /* Link control (bit 0 = enable) */
#define REG_SYS_CTRL_E760       XDATA_REG8(0xE760)
#define REG_SYS_CTRL_E761       XDATA_REG8(0xE761)
#define REG_SYS_CTRL_E763       XDATA_REG8(0xE763)
#define REG_PHY_TIMER_CTRL_E764 XDATA_REG8(0xE764)  /* PHY timer control */
#define REG_SYS_CTRL_E765       XDATA_REG8(0xE765)  /* System control E765 */
#define REG_SYS_CTRL_E76C       XDATA_REG8(0xE76C)  /* System control */
#define REG_SYS_CTRL_E774       XDATA_REG8(0xE774)  /* System control */
#define REG_SYS_CTRL_E77C       XDATA_REG8(0xE77C)  /* System control */
#define REG_SYS_CTRL_E780       XDATA_REG8(0xE780)  /* System control */
#define REG_FLASH_READY_STATUS  XDATA_REG8(0xE795)
#define REG_PHY_LINK_CTRL       XDATA_REG8(0xE7E3)
#define   PHY_LINK_CTRL_BIT6      0x40  // Bit 6: PHY link control flag
#define   PHY_LINK_CTRL_BIT7      0x80  // Bit 7: PHY link ready
#define REG_PHY_LINK_TRIGGER    XDATA_REG8(0xE7FA)  /* PHY link trigger/config */
#define REG_LINK_MODE_CTRL      XDATA_REG8(0xE7FC)
#define   LINK_MODE_CTRL_MASK     0x03  // Bits 0-1: Link mode control

//=============================================================================
// System Control Extended (0xEA00-0xEAFF)
//=============================================================================
#define REG_SYS_CTRL_EA90       XDATA_REG8(0xEA90)  /* System control EA90 */

//=============================================================================
// NVMe Event (0xEC00-0xEC0F)
//=============================================================================
#define REG_NVME_EVENT_ACK      XDATA_REG8(0xEC04)
#define REG_NVME_EVENT_STATUS   XDATA_REG8(0xEC06)
#define   NVME_EVENT_PENDING      0x01  // Bit 0: NVMe event pending

//=============================================================================
// System Control (0xEF00-0xEFFF)
//=============================================================================
#define REG_CRITICAL_CTRL       XDATA_REG8(0xEF4E)

//=============================================================================
// PCIe TLP Format/Type Codes (for REG_PCIE_FMT_TYPE)
//=============================================================================
#define PCIE_FMT_MEM_READ       0x00
#define PCIE_FMT_MEM_WRITE      0x40
#define PCIE_FMT_CFG_READ_0     0x04
#define PCIE_FMT_CFG_WRITE_0    0x44
#define PCIE_FMT_CFG_READ_1     0x05
#define PCIE_FMT_CFG_WRITE_1    0x45

//=============================================================================
// Bank-Selected Registers (0x0xxx-0x2xxx)
// These are accessed via bank switching or as part of extended memory access
//=============================================================================
#define REG_BANK_0200           XDATA_REG8(0x0200)  /* Bank register at 0x0200 */
#define REG_BANK_1200           XDATA_REG8(0x1200)  /* Bank register at 0x1200 */
#define REG_BANK_1235           XDATA_REG8(0x1235)  /* Bank register at 0x1235 */
#define REG_BANK_1407           XDATA_REG8(0x1407)  /* Bank register at 0x1407 */
#define REG_BANK_1504           XDATA_REG8(0x1504)  /* Bank register at 0x1504 */
#define REG_BANK_1507           XDATA_REG8(0x1507)  /* Bank register at 0x1507 */
#define REG_BANK_1603           XDATA_REG8(0x1603)  /* Bank register at 0x1603 */
#define REG_BANK_2269           XDATA_REG8(0x2269)  /* Bank register at 0x2269 */

//=============================================================================
// Additional registers used by enumeration firmware
//=============================================================================
#define REG_USB_DATA_9014       XDATA_REG8(0x9014)
#define REG_USB_DATA_9015       XDATA_REG8(0x9015)
#define REG_USB_DATA_9016       XDATA_REG8(0x9016)
#define REG_USB_DATA_9017       XDATA_REG8(0x9017)
#define REG_USB_EP_MASK_9099    XDATA_REG8(0x9099)
#define REG_USB_EP_MASK_909A    XDATA_REG8(0x909A)
#define REG_USB_EP_MASK_909B    XDATA_REG8(0x909B)
#define REG_USB_EP_MASK_909C    XDATA_REG8(0x909C)
#define REG_USB_EP_MASK_909D    XDATA_REG8(0x909D)
#define REG_USB_EP_CTRL_9220    XDATA_REG8(0x9220)
#define REG_USB_TRAINING_92F8   XDATA_REG8(0x92F8)
#define REG_BUF_CFG_9310        XDATA_REG8(0x9310)
#define REG_BUF_CFG_9311        XDATA_REG8(0x9311)
#define REG_BUF_CFG_9312        XDATA_REG8(0x9312)
#define REG_BUF_CFG_9313        XDATA_REG8(0x9313)
#define REG_BUF_CFG_9314        XDATA_REG8(0x9314)
#define REG_BUF_CFG_9315        XDATA_REG8(0x9315)
#define REG_BUF_CFG_9316        XDATA_REG8(0x9316)
#define REG_BUF_CFG_9317        XDATA_REG8(0x9317)
#define REG_BUF_CFG_9318        XDATA_REG8(0x9318)
#define REG_BUF_CFG_9319        XDATA_REG8(0x9319)
#define REG_BUF_CFG_931A        XDATA_REG8(0x931A)
#define REG_BUF_CFG_931B        XDATA_REG8(0x931B)
#define REG_BUF_CFG_931C        XDATA_REG8(0x931C)
#define REG_BUF_CFG_931D        XDATA_REG8(0x931D)
#define REG_BUF_CFG_931E        XDATA_REG8(0x931E)
#define REG_BUF_CFG_931F        XDATA_REG8(0x931F)
#define REG_BUF_CFG_9320        XDATA_REG8(0x9320)
#define REG_BUF_CFG_9321        XDATA_REG8(0x9321)
#define REG_BUF_CFG_9322        XDATA_REG8(0x9322)
#define REG_BUF_CFG_9323        XDATA_REG8(0x9323)
#define REG_PHY_LINK_CFG_C620   XDATA_REG8(0xC620)
#define REG_INT_AUX_C807        XDATA_REG8(0xC807)
#define REG_TIMER_CTRL_CC2A     XDATA_REG8(0xCC2A)
#define REG_TIMER_CFG_CC2C      XDATA_REG8(0xCC2C)
#define REG_TIMER_CFG_CC2D      XDATA_REG8(0xCC2D)
#define REG_CPU_CTRL_CC43       XDATA_REG8(0xCC43)
#define REG_PHY_CFG_E741        XDATA_REG8(0xE741)
#define REG_PHY_CFG_E742        XDATA_REG8(0xE742)

//=============================================================================
// Timeouts (milliseconds)
//=============================================================================
#define TIMEOUT_NVME            5000
#define TIMEOUT_DMA             10000

#endif /* __REGISTERS_H__ */
