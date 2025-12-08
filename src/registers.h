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

#define USB_SCSI_BUF_BASE       0x8000
#define USB_SCSI_BUF_SIZE       0x1000

#define USB_CTRL_BUF_BASE       0x9E00
#define USB_CTRL_BUF_SIZE       0x0200

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
// Core USB registers (0x9000-0x901F)
#define REG_USB_STATUS          XDATA_REG8(0x9000)
#define REG_PCIE_LINK_STATUS    XDATA_REG8(0xB480)  // PCIe link status (bit 0 = link up)
#define   USB_STATUS_ACTIVE       0x01  // Bit 0: USB active/pending
#define   USB_STATUS_INDICATOR    0x10  // Bit 4: USB status indicator
#define   USB_STATUS_CONNECTED    0x80  // Bit 7: USB ready/connected
#define REG_USB_CONTROL         XDATA_REG8(0x9001)
#define REG_USB_CONFIG          XDATA_REG8(0x9002)
#define   USB_CONFIG_MASK        0x0F  // Bits 0-3: USB configuration
#define REG_USB_EP0_STATUS      XDATA_REG8(0x9003)
#define REG_USB_EP0_LEN_L       XDATA_REG8(0x9004)
#define REG_USB_EP0_LEN_H       XDATA_REG8(0x9005)
#define REG_USB_EP0_CONFIG      XDATA_REG8(0x9006)
#define   USB_EP0_CONFIG_ENABLE   0x01  // Bit 0: EP0 config enable
#define REG_USB_SCSI_BUF_LEN    XDATA_REG16(0x9007)
#define REG_USB_SCSI_BUF_LEN_L  XDATA_REG8(0x9007)
#define REG_USB_SCSI_BUF_LEN_H  XDATA_REG8(0x9008)
#define REG_USB_MSC_CFG         XDATA_REG8(0x900B)
#define REG_USB_DATA_L          XDATA_REG8(0x9010)
#define REG_USB_DATA_H          XDATA_REG8(0x9011)
#define REG_USB_FIFO_L          XDATA_REG8(0x9012)
#define REG_USB_FIFO_H          XDATA_REG8(0x9013)
#define REG_USB_MODE_9018       XDATA_REG8(0x9018)
#define REG_USB_MODE_VAL_9019   XDATA_REG8(0x9019)
#define REG_USB_MSC_LENGTH      XDATA_REG8(0x901A)

// USB endpoint registers (0x905E-0x90FF)
#define REG_USB_EP_BUF_HI       XDATA_REG8(0x905B)  // USB endpoint buffer high byte
#define REG_USB_EP_BUF_LO       XDATA_REG8(0x905C)  // USB endpoint buffer low byte
#define REG_USB_EP_CTRL_905E    XDATA_REG8(0x905E)
#define REG_INT_FLAGS_EX0       XDATA_REG8(0x9091)
#define REG_USB_EP_CFG1         XDATA_REG8(0x9093)
#define REG_USB_EP_CFG2         XDATA_REG8(0x9094)
#define REG_USB_EP_READY        XDATA_REG8(0x9096)
#define REG_USB_STATUS_909E     XDATA_REG8(0x909E)
#define REG_USB_SIGNAL_90A1     XDATA_REG8(0x90A1)
#define REG_USB_SPEED           XDATA_REG8(0x90E0)
#define   USB_SPEED_MASK         0x03  // Bits 0-1: USB speed mode
#define REG_USB_MODE            XDATA_REG8(0x90E2)
#define REG_USB_EP_STATUS_90E3  XDATA_REG8(0x90E3)

// USB link/status registers (0x9100-0x912F)
#define REG_USB_LINK_STATUS     XDATA_REG8(0x9100)
#define   USB_LINK_STATUS_MASK    0x03  // Bits 0-1: Link status
#define REG_USB_PERIPH_STATUS   XDATA_REG8(0x9101)
#define REG_USB_STATUS_0D       XDATA_REG8(0x910D)
#define REG_USB_STATUS_0E       XDATA_REG8(0x910E)
#define REG_USB_EP_STATUS       XDATA_REG8(0x9118)
#define REG_USB_BUFFER_ALT      XDATA_REG8(0x911B)
#define REG_USB_DATA_911D       XDATA_REG8(0x911D)
#define REG_USB_DATA_911E       XDATA_REG8(0x911E)
#define REG_USB_STATUS_1F       XDATA_REG8(0x911F)
// Note: 0x9120-0x9123 dual use: USB status AND CBW tag storage
#define REG_USB_STATUS_20       XDATA_REG8(0x9120)
#define REG_USB_STATUS_21       XDATA_REG8(0x9121)
#define REG_USB_STATUS_22       XDATA_REG8(0x9122)
#define REG_CBW_TAG_0           XDATA_REG8(0x9120)  // Dual-use
#define REG_CBW_TAG_1           XDATA_REG8(0x9121)  // Dual-use
#define REG_CBW_TAG_2           XDATA_REG8(0x9122)  // Dual-use
#define REG_CBW_TAG_3           XDATA_REG8(0x9123)

// USB PHY registers (0x91C0-0x91FF)
#define REG_USB_PHY_CTRL_91C0   XDATA_REG8(0x91C0)
#define REG_USB_PHY_CTRL_91C1   XDATA_REG8(0x91C1)
#define REG_USB_PHY_CTRL_91C3   XDATA_REG8(0x91C3)
#define REG_USB_EP_CTRL_91D0    XDATA_REG8(0x91D0)
#define REG_USB_PHY_CTRL_91D1   XDATA_REG8(0x91D1)

// USB control registers (0x9200-0x92BF)
#define REG_USB_CTRL_9201       XDATA_REG8(0x9201)
#define REG_USB_CTRL_920C       XDATA_REG8(0x920C)
#define REG_USB_PHY_CONFIG_9241 XDATA_REG8(0x9241)

// Power Management registers (0x92C0-0x92E0)
#define REG_POWER_ENABLE        XDATA_REG8(0x92C0)
#define   POWER_ENABLE_BIT        0x01  // Bit 0: Main power enable
#define   POWER_ENABLE_MAIN       0x80  // Bit 7: Main power on
#define REG_CLOCK_ENABLE        XDATA_REG8(0x92C1)
#define   CLOCK_ENABLE_BIT        0x01  // Bit 0: Clock enable
#define   CLOCK_ENABLE_BIT1       0x02  // Bit 1: Secondary clock
#define REG_POWER_STATUS        XDATA_REG8(0x92C2)
#define   POWER_STATUS_SUSPENDED  0x40  // Bit 6: Device suspended
#define REG_POWER_CTRL_92C4     XDATA_REG8(0x92C4)
#define REG_PHY_POWER           XDATA_REG8(0x92C5)
#define   PHY_POWER_ENABLE        0x04  // Bit 2: PHY power enable
#define REG_POWER_CTRL_92C6     XDATA_REG8(0x92C6)
#define REG_POWER_CTRL_92C7     XDATA_REG8(0x92C7)
#define REG_POWER_CTRL_92C8     XDATA_REG8(0x92C8)
#define REG_POWER_DOMAIN        XDATA_REG8(0x92E0)
#define   POWER_DOMAIN_BIT1       0x02  // Bit 1: Power domain control

// Buffer config registers (0x9300-0x93FF)
#define REG_BUF_CFG_9300        XDATA_REG8(0x9300)
#define REG_BUF_CFG_9301        XDATA_REG8(0x9301)
#define REG_BUF_CFG_9302        XDATA_REG8(0x9302)
#define REG_BUF_CFG_9303        XDATA_REG8(0x9303)
#define REG_BUF_CFG_9304        XDATA_REG8(0x9304)
#define REG_BUF_CFG_9305        XDATA_REG8(0x9305)

//=============================================================================
// PCIe Passthrough Registers (0xB210-0xB4C8)
//=============================================================================
#define REG_PCIE_FMT_TYPE       XDATA_REG8(0xB210)
#define REG_PCIE_QUEUE_INDEX_LO XDATA_REG8(0xB80C)  // Queue index low
#define REG_PCIE_QUEUE_INDEX_HI XDATA_REG8(0xB80D)  // Queue index high
#define REG_PCIE_QUEUE_FLAGS_LO XDATA_REG8(0xB80E)  // Queue flags low
#define   PCIE_QUEUE_FLAG_VALID    0x01  // Bit 0: Queue entry valid
#define REG_PCIE_QUEUE_FLAGS_HI XDATA_REG8(0xB80F)  // Queue flags high
#define   PCIE_QUEUE_ID_MASK       0x0E  // Bits 1-3: Queue ID (shifted)
#define REG_PCIE_TLP_CTRL       XDATA_REG8(0xB213)
#define REG_PCIE_TLP_LENGTH     XDATA_REG8(0xB216)
#define REG_PCIE_BYTE_EN        XDATA_REG8(0xB217)
#define REG_PCIE_ADDR_0         XDATA_REG8(0xB218)
#define REG_PCIE_ADDR_1         XDATA_REG8(0xB219)
#define REG_PCIE_ADDR_2         XDATA_REG8(0xB21A)
#define REG_PCIE_ADDR_3         XDATA_REG8(0xB21B)
#define REG_PCIE_ADDR_HIGH      XDATA_REG8(0xB21C)
#define REG_PCIE_DATA           XDATA_REG8(0xB220)
#define REG_PCIE_TLP_CPL_HEADER XDATA_REG32(0xB224)
#define REG_PCIE_LINK_STATUS    XDATA_REG16(0xB22A)
#define REG_PCIE_CPL_STATUS     XDATA_REG8(0xB22B)
#define REG_PCIE_CPL_DATA       XDATA_REG8(0xB22C)
#define REG_PCIE_CPL_DATA_ALT   XDATA_REG8(0xB22D)
#define REG_PCIE_NVME_DOORBELL  XDATA_REG32(0xB250)
#define REG_PCIE_TRIGGER        XDATA_REG8(0xB254)
#define REG_PCIE_PM_ENTER       XDATA_REG8(0xB255)
#define REG_PCIE_COMPL_STATUS   XDATA_REG8(0xB284)
#define REG_PCIE_STATUS         XDATA_REG8(0xB296)
#define   PCIE_STATUS_ERROR       0x01  // Bit 0: Error flag
#define   PCIE_STATUS_COMPLETE    0x02  // Bit 1: Completion status
#define   PCIE_STATUS_BUSY        0x04  // Bit 2: Busy flag
#define REG_PCIE_CTRL_B402      XDATA_REG8(0xB402)
#define   PCIE_CTRL_B402_BIT0     0x01  // Bit 0: Control flag 0
#define   PCIE_CTRL_B402_BIT1     0x02  // Bit 1: Control flag 1
#define REG_PCIE_LANE_COUNT     XDATA_REG8(0xB424)
#define REG_PCIE_LINK_STATUS_ALT XDATA_REG16(0xB4AE)
#define REG_PCIE_LANE_MASK      XDATA_REG8(0xB4C8)

//=============================================================================
// UART Controller (0xC000-0xC00F)
//=============================================================================
#define REG_UART_BASE           XDATA_REG8(0xC000)
#define REG_UART_THR            XDATA_REG8(0xC001)  // TX (WO)
#define REG_UART_RBR            XDATA_REG8(0xC001)  // RX (RO)
#define REG_UART_IER            XDATA_REG8(0xC002)
#define REG_UART_FCR            XDATA_REG8(0xC004)  // WO
#define REG_UART_IIR            XDATA_REG8(0xC004)  // RO
#define REG_UART_TFBF           XDATA_REG8(0xC006)
#define REG_UART_LCR            XDATA_REG8(0xC007)
#define REG_UART_MCR            XDATA_REG8(0xC008)
#define REG_UART_LSR            XDATA_REG8(0xC009)
#define REG_UART_MSR            XDATA_REG8(0xC00A)

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
#define REG_NVME_DATA_CTRL      XDATA_REG8(0xC414)
#define   NVME_DATA_CTRL_MASK     0xC0  // Bits 6-7: Data control mode
#define REG_NVME_DEV_STATUS     XDATA_REG8(0xC415)
#define   NVME_DEV_STATUS_MASK    0xC0  // Bits 6-7: Device status
#define REG_NVME_CMD            XDATA_REG8(0xC420)
#define REG_NVME_CMD_OPCODE     XDATA_REG8(0xC421)
#define REG_NVME_LBA_LOW        XDATA_REG8(0xC422)
#define REG_NVME_LBA_MID        XDATA_REG8(0xC423)
#define REG_NVME_LBA_HIGH       XDATA_REG8(0xC424)
#define REG_NVME_COUNT_LOW      XDATA_REG8(0xC425)
#define REG_NVME_COUNT_HIGH     XDATA_REG8(0xC426)
#define REG_NVME_ERROR          XDATA_REG8(0xC427)
#define REG_NVME_QUEUE_CFG      XDATA_REG8(0xC428)
#define REG_NVME_CMD_PARAM      XDATA_REG8(0xC429)
#define   NVME_CMD_PARAM_TYPE    0xE0  // Bits 5-7: Command parameter type
#define REG_NVME_DOORBELL       XDATA_REG8(0xC42A)
#define   NVME_DOORBELL_TRIGGER   0x01  // Bit 0: Doorbell trigger
#define   NVME_DOORBELL_MODE      0x08  // Bit 3: Doorbell mode
#define REG_NVME_CMD_FLAGS      XDATA_REG8(0xC42B)
// Note: 0xC42C-0xC42D are USB MSC registers, not NVMe
#define REG_USB_MSC_CTRL        XDATA_REG8(0xC42C)
#define REG_USB_MSC_STATUS      XDATA_REG8(0xC42D)
#define REG_NVME_CMD_PRP2       XDATA_REG8(0xC431)
#define REG_NVME_CMD_CDW10      XDATA_REG8(0xC435)
#define REG_NVME_CMD_CDW11      XDATA_REG8(0xC439)
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
#define REG_DMA_ENTRY           XDATA_REG16(0xC462)
#define REG_CMDQ_DIR_END        XDATA_REG16(0xC470)
#define REG_NVME_QUEUE_PTR_C471 XDATA_REG8(0xC471)
#define REG_NVME_BUF_CFG        XDATA_REG8(0xC508)  // NVMe buffer configuration
#define REG_NVME_QUEUE_INDEX    XDATA_REG8(0xC512)
#define REG_NVME_QUEUE_C516     XDATA_REG8(0xC516)
#define REG_NVME_QUEUE_TRIGGER  XDATA_REG8(0xC51A)
#define REG_NVME_QUEUE_STATUS   XDATA_REG8(0xC51E)
#define   NVME_QUEUE_STATUS_IDX   0x3F  // Bits 0-5: Queue index
#define REG_NVME_LINK_STATUS    XDATA_REG8(0xC520)

//=============================================================================
// PHY Extended Registers (0xC600-0xC6FF)
//=============================================================================
#define REG_PHY_EXT_2D          XDATA_REG8(0xC62D)
#define   PHY_EXT_LANE_MASK       0x07  // Bits 0-2: Lane configuration
#define REG_PHY_EXT_56          XDATA_REG8(0xC656)
#define   PHY_EXT_SIGNAL_CFG      0x20  // Bit 5: Signal config
#define REG_PHY_EXT_5B          XDATA_REG8(0xC65B)
#define   PHY_EXT_ENABLE          0x08  // Bit 3: PHY extended enable
#define   PHY_EXT_MODE            0x20  // Bit 5: PHY mode
#define REG_PHY_EXT_B3          XDATA_REG8(0xC6B3)
#define   PHY_EXT_LINK_READY      0x30  // Bits 4,5: Link ready status

//=============================================================================
// Interrupt Controller (0xC800-0xC80F)
//=============================================================================
#define REG_INT_CTRL_C801       XDATA_REG8(0xC801)
#define REG_INT_USB_MASTER      XDATA_REG8(0xC802)
#define REG_INT_AUX_C805        XDATA_REG8(0xC805)
#define REG_INT_SYSTEM          XDATA_REG8(0xC806)
#define   INT_SYSTEM_TIMER        0x10  // Bit 4: System timer event
#define REG_INT_CTRL_C809       XDATA_REG8(0xC809)
#define REG_INT_PCIE_NVME       XDATA_REG8(0xC80A)
#define   INT_PCIE_NVME_EVENTS    0x0F  // Bits 0-3: PCIe event flags
#define   INT_PCIE_NVME_TIMER     0x10  // Bit 4: Timer-related event
#define   INT_PCIE_NVME_EVENT     0x20  // Bit 5: Generic event flag
#define   INT_PCIE_NVME_STATUS    0x40  // Bit 6: PCIe/NVMe status

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
#define REG_FLASH_MODE          XDATA_REG8(0xC8AD)
#define REG_FLASH_BUF_OFFSET    XDATA_REG16(0xC8AE)

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

//=============================================================================
// Timer Registers (0xCC10-0xCC24)
//=============================================================================
#define REG_TIMER0_DIV          XDATA_REG8(0xCC10)
#define REG_TIMER0_CSR          XDATA_REG8(0xCC11)
#define   TIMER_CSR_ENABLE        0x01  // Bit 0: Timer enable
#define   TIMER_CSR_EXPIRED       0x02  // Bit 1: Timer expired flag
#define   TIMER_CSR_CLEAR         0x04  // Bit 2: Clear interrupt
#define REG_TIMER0_THRESHOLD    XDATA_REG16(0xCC12)
#define REG_TIMER1_DIV          XDATA_REG8(0xCC16)
#define REG_TIMER1_CSR          XDATA_REG8(0xCC17)
#define REG_TIMER1_THRESHOLD    XDATA_REG16(0xCC18)
#define REG_TIMER2_DIV          XDATA_REG8(0xCC1C)
#define REG_TIMER2_CSR          XDATA_REG8(0xCC1D)
#define REG_TIMER2_THRESHOLD    XDATA_REG16(0xCC1E)
#define REG_TIMER3_DIV          XDATA_REG8(0xCC22)
#define REG_TIMER3_CSR          XDATA_REG8(0xCC23)
#define REG_TIMER3_IDLE_TIMEOUT XDATA_REG8(0xCC24)

//=============================================================================
// CPU Control Extended (0xCC30-0xCCFF)
//=============================================================================
#define REG_CPU_CTRL_CC30       XDATA_REG8(0xCC30)
#define REG_CPU_EXEC_CTRL       XDATA_REG8(0xCC31)
#define REG_CPU_EXEC_STATUS     XDATA_REG8(0xCC32)
#define   CPU_EXEC_STATUS_ACTIVE  0x01  // Bit 0: CPU execution active
#define REG_CPU_EXEC_STATUS_2   XDATA_REG8(0xCC33)
#define REG_CPU_CTRL_CC3A       XDATA_REG8(0xCC3A)
#define REG_CPU_CTRL_CC3B       XDATA_REG8(0xCC3B)
#define REG_CPU_CTRL_CC3D       XDATA_REG8(0xCC3D)
#define REG_CPU_CTRL_CC3E       XDATA_REG8(0xCC3E)
#define REG_CPU_CTRL_CC3F       XDATA_REG8(0xCC3F)
#define REG_CPU_STATUS_CC81     XDATA_REG8(0xCC81)
#define REG_CPU_STATUS_CC91     XDATA_REG8(0xCC91)
#define REG_CPU_STATUS_CC98     XDATA_REG8(0xCC98)
#define REG_CPU_DMA_CCD8        XDATA_REG8(0xCCD8)
#define REG_CPU_DMA_CCDA        XDATA_REG8(0xCCDA)
#define REG_CPU_DMA_CCDB        XDATA_REG8(0xCCDB)

//=============================================================================
// SCSI/Mass Storage DMA (0xCE40-0xCE97)
//=============================================================================
#define REG_SCSI_DMA_PARAM0     XDATA_REG8(0xCE40)
#define REG_SCSI_DMA_PARAM1     XDATA_REG8(0xCE41)
#define REG_SCSI_DMA_PARAM2     XDATA_REG8(0xCE42)
#define REG_SCSI_DMA_PARAM3     XDATA_REG8(0xCE43)
#define REG_SCSI_DMA_PARAM4     XDATA_REG8(0xCE44)
#define REG_SCSI_DMA_PARAM5     XDATA_REG8(0xCE45)
#define REG_SCSI_DMA_COMPL      XDATA_REG8(0xCE5C)
#define REG_SCSI_TRANSFER_CTRL  XDATA_REG8(0xCE70)
#define REG_SCSI_TRANSFER_MODE  XDATA_REG8(0xCE72)
#define REG_SCSI_BUF_CTRL0      XDATA_REG8(0xCE73)
#define REG_SCSI_BUF_CTRL1      XDATA_REG8(0xCE74)
#define REG_SCSI_BUF_LEN_LO     XDATA_REG8(0xCE75)
#define REG_SCSI_BUF_ADDR0      XDATA_REG8(0xCE76)
#define REG_SCSI_BUF_ADDR1      XDATA_REG8(0xCE77)
#define REG_SCSI_BUF_ADDR2      XDATA_REG8(0xCE78)
#define REG_SCSI_BUF_ADDR3      XDATA_REG8(0xCE79)
#define REG_SCSI_CMD_LIMIT_LO   XDATA_REG8(0xCE80)
#define REG_SCSI_CMD_LIMIT_HI   XDATA_REG8(0xCE81)
#define REG_SCSI_CMD_MODE       XDATA_REG8(0xCE82)
#define REG_SCSI_CMD_FLAGS      XDATA_REG8(0xCE83)
#define   SCSI_DMA_COMPL_MODE0    0x01  // Bit 0: Mode 0 complete
#define   SCSI_DMA_COMPL_MODE10   0x02  // Bit 1: Mode 0x10 complete
#define REG_XFER_CTRL_CE65      XDATA_REG8(0xCE65)
#define REG_SCSI_DMA_TAG_COUNT  XDATA_REG8(0xCE66)
#define   SCSI_DMA_TAG_MASK       0x1F  // Bits 0-4: Tag count (0-31)
#define REG_SCSI_DMA_QUEUE_STAT XDATA_REG8(0xCE67)
#define   SCSI_DMA_QUEUE_MASK     0x0F  // Bits 0-3: Queue status (0-15)
#define REG_SCSI_DMA_STATUS     XDATA_REG16(0xCE6E)
#define REG_XFER_STATUS_CE86    XDATA_REG8(0xCE86)
#define REG_XFER_CTRL_CE88      XDATA_REG8(0xCE88)
#define REG_XFER_READY          XDATA_REG8(0xCE89)
#define   XFER_READY_BIT          0x01  // Bit 0: Transfer ready
#define   XFER_READY_DONE         0x02  // Bit 1: Transfer done
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

// Note: Full struct access at 0xD800 - see structs.h

//=============================================================================
// PHY Completion / Debug (0xE300-0xE3FF)
//=============================================================================
#define REG_DEBUG_STATUS_E314   XDATA_REG8(0xE314)
#define REG_PHY_COMPLETION_E318 XDATA_REG8(0xE318)
#define REG_LINK_CTRL_E324      XDATA_REG8(0xE324)

//=============================================================================
// Command Engine (0xE400-0xE4FF)
//=============================================================================
#define REG_CMD_STATUS_E402     XDATA_REG8(0xE402)
#define REG_CMD_CTRL_E403       XDATA_REG8(0xE403)
#define REG_CMD_CONFIG          XDATA_REG8(0xE40B)
#define REG_CMD_CTRL_E40F       XDATA_REG8(0xE40F)
#define REG_CMD_CTRL_E410       XDATA_REG8(0xE410)
#define REG_CMD_BUSY_STATUS     XDATA_REG8(0xE41C)
#define REG_CMD_TRIGGER         XDATA_REG8(0xE420)
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

//=============================================================================
// Debug/Interrupt (0xE600-0xE6FF)
//=============================================================================
#define REG_DEBUG_INT_E661      XDATA_REG8(0xE661)

//=============================================================================
// System Status / Link Control (0xE700-0xE7FF)
//=============================================================================
#define REG_LINK_STATUS_E712    XDATA_REG8(0xE712)
#define REG_LINK_STATUS_E716    XDATA_REG8(0xE716)
#define   LINK_STATUS_E716_MASK  0x03  // Bits 0-1: Link status
#define REG_SYS_CTRL_E760       XDATA_REG8(0xE760)
#define REG_SYS_CTRL_E761       XDATA_REG8(0xE761)
#define REG_SYS_CTRL_E763       XDATA_REG8(0xE763)
#define REG_FLASH_READY_STATUS  XDATA_REG8(0xE795)
#define REG_PHY_LINK_CTRL       XDATA_REG8(0xE7E3)
#define   PHY_LINK_CTRL_BIT6      0x40  // Bit 6: PHY link control flag
#define   PHY_LINK_CTRL_BIT7      0x80  // Bit 7: PHY link ready
#define REG_LINK_MODE_CTRL      XDATA_REG8(0xE7FC)

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
// Timeouts (milliseconds)
//=============================================================================
#define TIMEOUT_NVME            5000
#define TIMEOUT_DMA             10000

#endif /* __REGISTERS_H__ */
