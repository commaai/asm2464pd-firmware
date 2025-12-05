#ifndef __REGISTERS_H__
#define __REGISTERS_H__

#include "types.h"

/*
 * AS2464 USB4/Thunderbolt NVMe Controller
 * Hardware Register Map
 *
 * All registers are memory-mapped and accessed via XDATA space
 * Based on firmware analysis and usb-to-pcie-re findings
 */

//=============================================================================
// Helper Macros
//=============================================================================
#define XDATA_REG8(addr)   (*(__xdata uint8_t *)(addr))
#define XDATA_REG16(addr)  (*(__xdata uint16_t *)(addr))
#define XDATA_REG32(addr)  (*(__xdata uint32_t *)(addr))

//=============================================================================
// UART Controller (0xC000-0xC00F)
// Dedicated UART, NOT standard 8051 UART (no SBUF/TI/RI)
// Hardware: 921600 baud 8N1 fixed, pins A21(RX)/B21(TX)
// Based on ASMedia USB host controller UART design
//=============================================================================
#define REG_UART_BASE           XDATA_REG8(0xC000)  // UART base
#define REG_UART_THR            XDATA_REG8(0xC001)  // Transmit Holding Register (WO)
#define REG_UART_RBR            XDATA_REG8(0xC001)  // Receive Buffer Register (RO)
#define REG_UART_IER            XDATA_REG8(0xC002)  // Interrupt Enable Register (RW)
#define REG_UART_FCR            XDATA_REG8(0xC004)  // FIFO Control Register (WO)
#define REG_UART_IIR            XDATA_REG8(0xC004)  // Interrupt Identification (RO)
#define REG_UART_TFBF           XDATA_REG8(0xC006)  // Transmit FIFO Buffer Full (RO)
#define REG_UART_LCR            XDATA_REG8(0xC007)  // Line Control Register (RW)
#define REG_UART_MCR            XDATA_REG8(0xC008)  // Modem Control Register (RW)
#define REG_UART_LSR            XDATA_REG8(0xC009)  // Line Status Register (RO)
#define REG_UART_MSR            XDATA_REG8(0xC00A)  // Modem Status Register (RO)

// UART configuration
#define UART_BAUD_RATE          921600
#define UART_DATA_BITS          8
#define UART_PARITY             0  // None
#define UART_STOP_BITS          1

//=============================================================================
// I2C Controller (0xC870-0xC87F)
// For external peripherals (LED controllers, etc)
//=============================================================================
#define REG_I2C_ADDR            XDATA_REG8(0xC870)  // Device address (RW)
#define REG_I2C_MODE            XDATA_REG8(0xC871)  // Mode/control (RW)
#define REG_I2C_LEN             XDATA_REG8(0xC873)  // Transfer length (RW)
#define REG_I2C_CSR             XDATA_REG8(0xC875)  // Control/status (RW)
#define REG_I2C_SRC             XDATA_REG32(0xC878) // Source address (RW, 4 bytes)
#define REG_I2C_DST             XDATA_REG32(0xC87C) // Destination address (RW, 4 bytes)
#define REG_I2C_CSR_ALT         XDATA_REG8(0xC87F)  // Control/status alt (RW)

//=============================================================================
// SPI Flash Controller (0xC89F-0xC8AE)
// Uses 4KB buffer at 0x7000
//=============================================================================
#define REG_FLASH_CON           XDATA_REG8(0xC89F)  // Flash control (RW)
#define REG_FLASH_ADDR_LO       XDATA_REG8(0xC8A1)  // Address low byte (RW)
#define REG_FLASH_ADDR_MD       XDATA_REG8(0xC8A2)  // Address middle byte (RW)
#define REG_FLASH_DATA_LEN      XDATA_REG8(0xC8A3)  // Data length (RW)
#define REG_FLASH_DIV           XDATA_REG8(0xC8A6)  // Clock divisor (RW)
#define REG_FLASH_CSR           XDATA_REG8(0xC8A9)  // Control/status (RW)
#define REG_FLASH_CMD           XDATA_REG8(0xC8AA)  // Command (WO)
#define REG_FLASH_ADDR_HI       XDATA_REG8(0xC8AB)  // Address high byte (RW)
#define REG_FLASH_ADDR_LEN      XDATA_REG8(0xC8AC)  // Address length (RW)
#define REG_FLASH_MODE          XDATA_REG8(0xC8AD)  // Mode (RW)
#define REG_FLASH_BUF_OFFSET    XDATA_REG16(0xC8AE) // Buffer offset (RW, 2 bytes)

#define FLASH_BUFFER_BASE       0x7000              // Flash buffer (4KB)
#define FLASH_BUFFER_SIZE       0x1000

//=============================================================================
// Timer Registers (0xCC10-0xCC24)
// Four hardware timers with divisor, CSR, and threshold
//=============================================================================
// Timer 0
#define REG_TIMER0_DIV          XDATA_REG8(0xCC10)  // Clock divisor (RW)
#define REG_TIMER0_CSR          XDATA_REG8(0xCC11)  // Control/status (RW)
#define REG_TIMER0_THRESHOLD    XDATA_REG16(0xCC12) // Threshold (RW, 2 bytes)

// Timer 1
#define REG_TIMER1_DIV          XDATA_REG8(0xCC16)  // Clock divisor (RW)
#define REG_TIMER1_CSR          XDATA_REG8(0xCC17)  // Control/status (RW)
#define REG_TIMER1_THRESHOLD    XDATA_REG16(0xCC18) // Threshold (RW, 2 bytes)

// Timer 2
#define REG_TIMER2_DIV          XDATA_REG8(0xCC1C)  // Clock divisor (RW)
#define REG_TIMER2_CSR          XDATA_REG8(0xCC1D)  // Control/status (RW)
#define REG_TIMER2_THRESHOLD    XDATA_REG16(0xCC1E) // Threshold (RW, 2 bytes)

// Timer 3 (with idle timeout)
#define REG_TIMER3_DIV          XDATA_REG8(0xCC22)  // Clock divisor (RW)
#define REG_TIMER3_CSR          XDATA_REG8(0xCC23)  // Control/status (RW)
#define REG_TIMER3_IDLE_TIMEOUT XDATA_REG8(0xCC24)  // Idle timeout (half-seconds, RW)

//=============================================================================
// CPU Control Registers
//=============================================================================
#define REG_CPU_MODE_NEXT       XDATA_REG8(0xCA06)  // CPU mode next (RW)
#define REG_CPU_EXEC_CTRL       XDATA_REG8(0xCC31)  // CPU execution control (RW)
#define REG_CPU_EXEC_STATUS     XDATA_REG8(0xCC32)  // CPU execution status (RW)
#define REG_CPU_EXEC_STATUS_2   XDATA_REG8(0xCC33)  // CPU execution status 2 (RW)

//=============================================================================
// Interrupt Controller Registers (0xC800-0xC80F)
//=============================================================================
#define REG_INT_USB_MASTER      XDATA_REG8(0xC802)  // USB master interrupt status (RW)
#define REG_INT_SYSTEM          XDATA_REG8(0xC806)  // System interrupt status (RW)
#define REG_INT_PCIE_NVME       XDATA_REG8(0xC80A)  // PCIe/NVMe interrupt status (RW)

//=============================================================================
// Event Flags (internal XDATA)
//=============================================================================
#define REG_EVENT_FLAGS         XDATA_REG8(0x09F9)  // Event flags (RW)

//=============================================================================
// NVMe Event Registers (0xEC00-0xEC0F)
//=============================================================================
#define REG_NVME_EVENT_ACK      XDATA_REG8(0xEC04)  // NVMe event acknowledge (WO)
#define REG_NVME_EVENT_STATUS   XDATA_REG8(0xEC06)  // NVMe event status (RO)

//=============================================================================
// System State Flags (internal RAM-like XDATA)
// Used for firmware state tracking
//=============================================================================
#define REG_STATE_FLAG_06E6     XDATA_REG8(0x06E6)  // Processing complete flag
#define REG_STATE_FLAG_0AE3     XDATA_REG8(0x0AE3)  // System state flag

//=============================================================================
// Power Management Registers (0x92C0-0x92C8)
// Identified via firmware analysis - power state control cluster
//=============================================================================
#define REG_POWER_CTRL_92C0     XDATA_REG8(0x92C0)  // Power control 0 (RW)
#define REG_POWER_CTRL_92C1     XDATA_REG8(0x92C1)  // Power control 1 / Clock config (RW)
#define REG_POWER_STATUS_92C2   XDATA_REG8(0x92C2)  // Power status (RW)
#define REG_POWER_CTRL_92C4     XDATA_REG8(0x92C4)  // Power control 4 - MAIN (RW)
#define REG_POWER_CTRL_92C5     XDATA_REG8(0x92C5)  // Power control 5 (RW)
#define REG_POWER_CTRL_92C6     XDATA_REG8(0x92C6)  // Power control 6 (RW)
#define REG_POWER_CTRL_92C8     XDATA_REG8(0x92C8)  // Power control 8 (RW)

// Primary power register aliases
#define REG_POWER_CTRL          REG_POWER_CTRL_92C4      // Main power control
#define REG_POWER_STATUS_CTRL   REG_POWER_STATUS_92C2    // Power status
#define REG_POWER_STATUS        REG_POWER_STATUS_92C2    // Alias
#define REG_CLOCK_CONFIG        REG_POWER_CTRL_92C1      // Clock configuration

// Power control bits
#define PWR_CORE_EN             0x01
#define PWR_PHY_EN              0x02
#define PWR_USB_EN              0x04
#define PWR_NVME_EN             0x08
#define PWR_DMA_EN              0x10
#define PWR_PLL_EN              0x20
#define PWR_CLK_EN              0x40

// Power status bits
#define PWR_ST_OFF              0x00
#define PWR_ST_INIT             0x40
#define PWR_ST_READY            0x80
#define PWR_ST_ERROR            0xC0

// Power state constants
#define POWER_ACTIVE            0x03
#define POWER_LOW               0x02
#define POWER_SUSPEND           0x01
#define POWER_RESUME            0x04

//=============================================================================
// USB Interface Registers (0x9000-0x90FF)
//=============================================================================
#define REG_USB_STATUS          XDATA_REG8(0x9000)  // Status (RW)
#define REG_USB_PERIPH_STATUS   XDATA_REG8(0x9101)  // Peripheral status (RW)
#define REG_USB_CONTROL         XDATA_REG8(0x9001)  // Control (RW)
#define REG_USB_CONFIG          XDATA_REG8(0x9002)  // Configuration (RW)
#define REG_USB_EP0_STATUS      XDATA_REG8(0x9003)  // EP0 status (RW)
#define REG_USB_EP0_LEN_L       XDATA_REG8(0x9004)  // EP0 length low (RW)
#define REG_USB_EP0_LEN_H       XDATA_REG8(0x9005)  // EP0 length high (RW)
#define REG_USB_EP0_CONFIG      XDATA_REG8(0x9006)  // EP0 config (RW)
#define REG_USB_SCSI_BUF_LEN    XDATA_REG16(0x9007) // SCSI buffer length (RW, 2 bytes)
#define REG_USB_DATA_L          XDATA_REG8(0x9010)  // Data low (RW)
#define REG_USB_DATA_H          XDATA_REG8(0x9011)  // Data high (RW)
#define REG_USB_FIFO_L          XDATA_REG8(0x9012)  // FIFO low (RW)
#define REG_USB_FIFO_H          XDATA_REG8(0x9013)  // FIFO high (RW)
#define REG_INT_FLAGS_EX0       XDATA_REG8(0x9091)  // Interrupt flags external (RW)
#define REG_USB_BUFFER_ALT      XDATA_REG8(0x911B)  // USB buffer alt (RW)

// USB status bits
#define USB_CONNECTED           0x80
#define USB_READY               0x80  // Alias for connected/ready state
#define USB_LINK_ACTIVE         0x40
#define USB_TRANSFER_DONE       0x20
#define USB_SPEED_MASK          0x30
#define USB_SUSPENDED           0x08
#define USB_RESET_DET           0x04
#define USB_ERROR               0x02
#define USB_INT_PEND            0x01
#define USB_ACTIVITY            0x01

// USB speed values
#define USB_SPEED_DISC          0x00
#define USB_SPEED_USB2          0x10
#define USB_SPEED_USB3          0x20
#define USB_SPEED_USB4          0x30

// USB speed detailed values
#define USB_SPEED_UNKNOWN       0x00
#define USB_SPEED_LOW           0x01
#define USB_SPEED_FULL          0x02
#define USB_SPEED_HIGH          0x03
#define USB_SPEED_SUPER         0x04

// USB control bits
#define USB_ENABLE              0x80
#define USB_INT_EN              0x40
#define USB_CLR_ERR             0x20
#define USB_RECONNECT           0x10
#define USB_TX_READY            0x04
#define USB_RX_READY            0x08
#define USB_CONNECT             0x10
#define USB_DISCONNECT          0x20

//=============================================================================
// Link/PHY Control Registers (0xC200-0xC3FF)
//=============================================================================
#define REG_LINK_CTRL           XDATA_REG8(0xC202)  // Link control (RW)
#define REG_LINK_CONFIG         XDATA_REG8(0xC203)  // Link configuration (RW)
#define REG_LINK_STATUS         XDATA_REG8(0xC204)  // Link status (RO)
#define REG_PHY_CTRL            XDATA_REG8(0xC205)  // PHY control (RW)
#define REG_PHY_CONFIG          XDATA_REG8(0xC233)  // PHY configuration (RW)
#define REG_PHY_STATUS          XDATA_REG8(0xC284)  // PHY status (RW)

// Link control bits
#define LINK_ENABLE             0x80
#define LINK_RESET              0x40
#define LINK_MODE_MASK          0x30
#define LINK_AUTO_NEG           0x08
#define LINK_TRAIN_EN           0x04
#define LINK_PHY_EN             0x02
#define LINK_CLR_ERR            0x01

// Link modes
#define LINK_MODE_USB3          0x00
#define LINK_MODE_USB4G2        0x10
#define LINK_MODE_USB4G3        0x20
#define LINK_MODE_TB            0x30

// Link status bits
#define LINK_TRAINED            0x80
#define LINK_PHY_RDY            0x40
#define LINK_ERROR              0x20
#define LINK_SPEED_MASK         0x0F

// PHY status bits
#define PHY_READY               0x80
#define PHY_PLL_LOCK            0x40
#define PHY_RX_DETECT           0x20
#define PHY_TX_EN               0x10
#define PHY_LANE_MASK           0x0F
#define PHY_TRAIN_START         0x01
#define PHY_TRAIN_DONE          0x80
#define PHY_LINK_UP             0x40
#define PHY_LINK_DOWN           0x00

//=============================================================================
// PHY Extended Registers (0xC600-0xC6FF)
// Used in phy_config_link_params (0x5284) and handler_4fb6
//=============================================================================
#define REG_PHY_EXT_2D          XDATA_REG8(0xC62D)  // PHY extended config 0x2D
#define REG_PHY_EXT_56          XDATA_REG8(0xC656)  // PHY extended config 0x56
#define REG_PHY_EXT_5B          XDATA_REG8(0xC65B)  // PHY extended config 0x5B
#define REG_PHY_EXT_B3          XDATA_REG8(0xC6B3)  // PHY status (polled in handler_4fb6)

//=============================================================================
// NVMe Interface Registers (0xC400-0xC5FF)
//=============================================================================
#define REG_NVME_CTRL           XDATA_REG8(0xC400)  // Control (RW)
#define REG_NVME_STATUS         XDATA_REG8(0xC401)  // Status (RO)
#define REG_NVME_CTRL_STATUS    XDATA_REG8(0xC412)  // Control/status (RW)
#define REG_NVME_CONFIG         XDATA_REG8(0xC413)  // Configuration (RW)
#define REG_NVME_DATA_CTRL      XDATA_REG8(0xC414)  // Data control (RW)
#define REG_NVME_DEV_STATUS     XDATA_REG8(0xC415)  // Device status (RO)
#define REG_NVME_CMD            XDATA_REG8(0xC420)  // Command register (WO)
#define REG_NVME_CMD_REG        XDATA_REG8(0xC420)  // Alias
#define REG_NVME_CMD_OPCODE     XDATA_REG8(0xC421)  // Command opcode (WO)
#define REG_NVME_LBA_LOW        XDATA_REG8(0xC422)  // LBA low (WO)
#define REG_NVME_LBA_MID        XDATA_REG8(0xC423)  // LBA mid (WO)
#define REG_NVME_LBA_HIGH       XDATA_REG8(0xC424)  // LBA high (WO)
#define REG_NVME_LBA_0          XDATA_REG8(0xC422)  // LBA byte 0 (alias)
#define REG_NVME_LBA_1          XDATA_REG8(0xC423)  // LBA byte 1 (alias)
#define REG_NVME_LBA_2          XDATA_REG8(0xC424)  // LBA byte 2 (alias)
#define REG_NVME_LBA_3          XDATA_REG8(0xC446)  // LBA byte 3
#define REG_NVME_COUNT_LOW      XDATA_REG8(0xC425)  // Count low (WO)
#define REG_NVME_COUNT_HIGH     XDATA_REG8(0xC426)  // Count high (WO)
#define REG_NVME_COUNT          XDATA_REG8(0xC425)  // Count (alias for COUNT_LOW)
#define REG_NVME_ERROR          XDATA_REG8(0xC427)  // Error code (RO)
#define REG_NVME_QUEUE_CFG      XDATA_REG8(0xC428)  // Queue config (RW)
#define REG_NVME_CMD_PARAM      XDATA_REG8(0xC429)  // Command parameter (WO)
#define REG_NVME_DOORBELL       XDATA_REG8(0xC42A)  // Doorbell/status (RW)
#define REG_NVME_CMD_FLAGS      XDATA_REG8(0xC42B)  // Command flags (WO)
#define REG_NVME_CMD_NSID       XDATA_REG8(0xC42C)  // Namespace ID (WO)
#define REG_NVME_CMD_PRP1       XDATA_REG8(0xC42D)  // PRP1 pointer (WO)
#define REG_NVME_CMD_PRP2       XDATA_REG8(0xC431)  // PRP2 pointer (WO)
#define REG_NVME_CMD_CDW10      XDATA_REG8(0xC435)  // Command DWord 10 (WO)
#define REG_NVME_CMD_CDW11      XDATA_REG8(0xC439)  // Command DWord 11 (WO)
#define REG_NVME_QUEUE_PTR      XDATA_REG8(0xC43D)  // Queue pointer (RW)
#define REG_NVME_QUEUE_DEPTH    XDATA_REG8(0xC43E)  // Queue depth (RW)
#define REG_NVME_PHASE          XDATA_REG8(0xC43F)  // Phase bit (RW)
#define REG_NVME_QUEUE_CTRL     XDATA_REG8(0xC440)  // Queue control (RW)
#define REG_NVME_SQ_HEAD        XDATA_REG8(0xC441)  // Submission queue head (RW)
#define REG_NVME_SQ_TAIL        XDATA_REG8(0xC442)  // Submission queue tail (RW)
#define REG_NVME_CQ_HEAD        XDATA_REG8(0xC443)  // Completion queue head (RW)
#define REG_NVME_CQ_TAIL        XDATA_REG8(0xC444)  // Completion queue tail (RW)
#define REG_NVME_CQ_STATUS      XDATA_REG8(0xC445)  // Completion status (RO)
#define REG_DMA_ENTRY           XDATA_REG16(0xC462) // DMA entry point (RW, 2 bytes)
#define REG_CMDQ_DIR_END        XDATA_REG16(0xC470) // Command queue dir end (RW, 2 bytes)

// NVMe control/status bits
#define NVME_ENABLE             0x80
#define NVME_RESET              0x40
#define NVME_SHUTDOWN           0x20
#define NVME_ERROR              0x10
#define NVME_STATE_MASK         0x0F
#define NVME_INT_EN             0x08
#define NVME_CLR_ERR            0x04
#define NVME_START              0x01
#define NVME_COMPLETE           0x80

// NVMe states
#define NVME_ST_INIT            0x01
#define NVME_ST_READY           0x02
#define NVME_ST_ACTIVE          0x04
#define NVME_ST_ERROR           0x08

// NVMe status bits
#define NVME_READY              0x80
#define NVME_CMD_DONE           0x40
#define NVME_INT_PEND           0x20
#define NVME_CQ_READY           0x10

// NVMe device status bits
#define NVME_DEV_PRESENT        0x80
#define NVME_DEV_READY          0x40
#define NVME_DEV_TYPE_MASK      0x30

// NVMe doorbell bits
#define NVME_QUEUE_AVAIL        0x80
#define NVME_COMPL_AVAIL        0x40
#define NVME_CMD_READY          0x20
#define NVME_CLR_CQ             0x10
#define NVME_QUEUE_ADVANCE      0x08
#define NVME_FLUSH              0x04

//=============================================================================
// DMA Engine Registers (0xC800-0xC9FF)
//=============================================================================
#define REG_DMA_CHANNEL_SEL     XDATA_REG8(0xC8A1)  // Channel select (RW)
#define REG_DMA_CHANNEL_CTRL    XDATA_REG8(0xC8A2)  // Channel control (RW)
#define REG_DMA_CHANNEL_STATUS  XDATA_REG8(0xC8A3)  // Channel status (RW)
#define REG_DMA_LENGTH_LOW      XDATA_REG8(0xC8A4)  // Length low (RW)
#define REG_DMA_LENGTH_HIGH     XDATA_REG8(0xC8A5)  // Length high (RW)
#define REG_DMA_COUNT_LOW       XDATA_REG8(0xC8A6)  // Count low (RW)
#define REG_DMA_COUNT_HIGH      XDATA_REG8(0xC8A7)  // Count high (RW)
#define REG_DMA_PRIORITY        XDATA_REG8(0xC8A8)  // Priority (RW)
#define REG_DMA_BURST_SIZE      XDATA_REG8(0xC8A9)  // Burst size (RW)
#define REG_DMA_SRC_L           XDATA_REG8(0xC8AA)  // Source low (RW)
#define REG_DMA_SRC_H           XDATA_REG8(0xC8AB)  // Source high (RW)
#define REG_DMA_DST_L           XDATA_REG8(0xC8AC)  // Destination low (RW)
#define REG_DMA_DST_H           XDATA_REG8(0xC8AD)  // Destination high (RW)
#define REG_DMA_CTRL            XDATA_REG8(0xC8AD)  // DMA control (RW)
#define REG_DMA_LEN_L           XDATA_REG8(0xC8AE)  // Length low (RW)
#define REG_DMA_LEN_H           XDATA_REG8(0xC8AF)  // Length high (RW)
#define REG_DMA_MODE            XDATA_REG8(0xC8B0)  // Mode (RW)
#define REG_DMA_CONFIG          XDATA_REG8(0xC8D4)  // Configuration (WO)
#define REG_DMA_STATUS          XDATA_REG8(0xC8D6)  // Status (RW)

// DMA control bits
#define DMA_ENABLE              0x80
#define DMA_DIR_N2U             0x40  // NVMe to USB
#define DMA_ACTIVE              0x20
#define DMA_ERROR               0x10
#define DMA_CHAN_MASK           0x0F
#define DMA_START               0x01
#define DMA_USB_TO_BUF          0x02
#define DMA_BUF_TO_USB          0x04
#define DMA_RESET               0x80
#define DMA_CLR_ERR             0x40
#define DMA_ABORT               0x20

// DMA configuration
#define DMA_BURST_MASK          0xC0
#define DMA_PRIO_MASK           0x30
#define DMA_MODE_MASK           0x0F
#define DMA_MODE_BURST          0x01
#define DMA_MODE_INCREMENT      0x02

// DMA status bits
#define DMA_COMPLETE            0x80
#define DMA_IN_PROGRESS         0x40
#define DMA_ERR_MASK            0x30
#define DMA_ERROR_FLAG          0x01
#define DMA_DONE_FLAG           0x02
#define DMA_INT_EN              0x04
#define DMA_CH_EN               0x80
#define DMA_CH_USB_SRC          0x00
#define DMA_CH_NVME_SRC         0x40
#define DMA_CH_START            0x01
#define DMA_CH_ABORT            0x02
#define DMA_CH_CLR_ERR          0x04
#define DMA_CH_ERROR            0x80
#define DMA_CH_DONE             0x40
#define DMA_CH_BUSY             0x20

//=============================================================================
// SCSI/Mass Storage DMA Registers (0xCE40-0xCE6E)
//=============================================================================
#define REG_SCSI_CTRL           XDATA_REG8(0x0171)  // SCSI control (RW)
#define REG_SCSI_DMA_PARAM0     XDATA_REG8(0xCE40)  // DMA parameter 0 (RW)
#define REG_SCSI_DMA_PARAM1     XDATA_REG8(0xCE41)  // DMA parameter 1 (RW)
#define REG_SCSI_DMA_PARAM2     XDATA_REG8(0xCE42)  // DMA parameter 2 (RW)
#define REG_SCSI_DMA_PARAM3     XDATA_REG8(0xCE43)  // DMA parameter 3 (RW)
#define REG_SCSI_DMA_STATUS     XDATA_REG16(0xCE6E) // DMA status (RW, 2 bytes)

//=============================================================================
// Data Buffer Registers (0xD800-0xD8FF)
//=============================================================================
#define REG_BUFFER_CTRL         XDATA_REG8(0xD800)  // Buffer control (WO)
#define REG_BUFFER_SELECT       XDATA_REG8(0xD801)  // Buffer select (RW)
#define REG_BUFFER_DATA         XDATA_REG8(0xD802)  // Buffer data/pointer (WO)
#define REG_BUFFER_PTR_LOW      XDATA_REG8(0xD803)  // Pointer low (RW)
#define REG_BUFFER_PTR_HIGH     XDATA_REG8(0xD804)  // Pointer high (RW)
#define REG_BUFFER_LENGTH_LOW   XDATA_REG8(0xD805)  // Length low (RW)
#define REG_BUFFER_STATUS       XDATA_REG8(0xD806)  // Status (RW)
#define REG_BUFFER_LENGTH_HIGH  XDATA_REG8(0xD807)  // Length high (RW)
#define REG_BUFFER_CTRL_GLOBAL  XDATA_REG8(0xD808)  // Control global (RW)
#define REG_BUFFER_THRESHOLD_HIGH XDATA_REG8(0xD809) // Threshold high (RW)
#define REG_BUFFER_THRESHOLD_LOW  XDATA_REG8(0xD80A) // Threshold low (RW)
#define REG_BUFFER_FLOW_CTRL    XDATA_REG8(0xD80B)  // Flow control (RW)
#define REG_BUFFER_L            XDATA_REG8(0xD810)  // Buffer low (RW)
#define REG_BUFFER_H            XDATA_REG8(0xD811)  // Buffer high (RW)

// Buffer status bits
#define BUF_FULL                0x80
#define BUF_EMPTY               0x40
#define BUF_LEVEL_MASK          0x3F
#define BUF_FLUSH               0x80
#define BUF_BUSY                0x40
#define BUF_ALLOCATED           0x01
#define BUF_CLEAR               0x02
#define BUF_WRITE_EN            0x04
#define BUF_READ_EN             0x08
#define BUF_DMA_EN              0x10
#define BUF_OVERFLOW            0x20
#define BUF_RESET_ALL           0x80
#define BUF_DIRECTION_READ      0x00  // Read direction (NVMe -> Buffer -> USB)
#define BUF_DIRECTION_WRITE     0x04  // Write direction (USB -> Buffer -> NVMe)
#define BUF_CLR_OVERFLOW        0x20
#define BUF_FLOW_PAUSE          0x01

//=============================================================================
// PCIe Passthrough Registers (0xB210-0xB4C8)
// Transaction Layer Packet (TLP) operations
//=============================================================================
#define REG_PCIE_FMT_TYPE       XDATA_REG8(0xB210)  // Format/type (RW)
#define REG_PCIE_BYTE_EN        XDATA_REG8(0xB217)  // Byte enables (RW)
#define REG_PCIE_ADDR_LOW       XDATA_REG8(0xB218)  // Address low (RW, 4 bytes)
#define REG_PCIE_ADDR_HIGH      XDATA_REG8(0xB21C)  // Address high (RW, 4 bytes)
#define REG_PCIE_DATA           XDATA_REG8(0xB220)  // Data register (RW, 4 bytes)
#define REG_PCIE_TLP_CPL_HEADER XDATA_REG32(0xB224) // TLP completion header (RW, 4 bytes)
#define REG_PCIE_LINK_STATUS    XDATA_REG16(0xB22A) // Link status (RO, 2 bytes)
#define REG_PCIE_NVME_DOORBELL  XDATA_REG32(0xB250) // NVMe doorbell SQT/CQH (RW, 4 bytes)
#define REG_PCIE_TRIGGER        XDATA_REG8(0xB254)  // Trigger/start (WO)
#define REG_PCIE_PM_ENTER       XDATA_REG8(0xB255)  // PM enter (WO)
#define REG_PCIE_COMPL_STATUS   XDATA_REG8(0xB284)  // Completion status (RO)
#define REG_PCIE_STATUS         XDATA_REG8(0xB296)  // Status (RW)
#define REG_PCIE_LANE_COUNT     XDATA_REG8(0xB424)  // Lane count (RO)
#define REG_PCIE_LINK_STATUS_ALT XDATA_REG16(0xB4AE) // Link status alt (RO, 2 bytes)
#define REG_PCIE_LANE_MASK      XDATA_REG8(0xB4C8)  // Lane mask (RW)

// PCIe status bits
#define PCIE_STATUS_ERROR       0x01
#define PCIE_STATUS_COMPLETE    0x02
#define PCIE_STATUS_BUSY        0x04

// PCIe format/type codes
#define PCIE_FMT_MEM_READ       0x20
#define PCIE_FMT_MEM_WRITE      0x60
#define PCIE_FMT_CFG_READ_0     0x04
#define PCIE_FMT_CFG_WRITE_0    0x44
#define PCIE_FMT_CFG_READ_1     0x05
#define PCIE_FMT_CFG_WRITE_1    0x45

//=============================================================================
// Command Engine Registers (0xE400-0xE4FF)
//=============================================================================
#define REG_CMD_CONFIG          XDATA_REG8(0xE40B)  // Config (RW)
#define REG_CMD_PARAM           XDATA_REG8(0xE422)  // Parameter (RW)
#define REG_CMD_OPCODE          XDATA_REG8(0xE422)  // Alias
#define REG_CMD_STATUS          XDATA_REG8(0xE423)  // Status (RW)
#define REG_CMD_ISSUE           XDATA_REG8(0xE424)  // Issue strobe (WO)
#define REG_CMD_TAG             XDATA_REG8(0xE425)  // Tag (RW)
#define REG_CMD_LBA_0           XDATA_REG8(0xE426)  // LBA byte 0 (RW)
#define REG_CMD_LBA_1           XDATA_REG8(0xE427)  // LBA byte 1 (RW)
#define REG_CMD_LBA_2           XDATA_REG8(0xE428)  // LBA byte 2 (RW)
#define REG_CMD_LBA_3           XDATA_REG8(0xE429)  // LBA byte 3 (RW)
#define REG_CMD_COUNT_LOW       XDATA_REG8(0xE42A)  // Count low (RW)
#define REG_CMD_COUNT_HIGH      XDATA_REG8(0xE42B)  // Count high (RW)
#define REG_CMD_LENGTH_LOW      XDATA_REG8(0xE42C)  // Length low (RW)
#define REG_CMD_LENGTH_HIGH     XDATA_REG8(0xE42D)  // Length high (RW)
#define REG_CMD_RESP_TAG        XDATA_REG8(0xE42E)  // Response tag (RW)
#define REG_CMD_RESP_STATUS     XDATA_REG8(0xE42F)  // Response status (RW)
#define REG_CMD_CTRL            XDATA_REG8(0xE430)  // Control (RW)
#define REG_CMD_TIMEOUT         XDATA_REG8(0xE431)  // Timeout (RW)
#define REG_CMD_PARAM_L         XDATA_REG8(0xE432)  // Parameter low (RW)
#define REG_CMD_PARAM_H         XDATA_REG8(0xE433)  // Parameter high (RW)

// Command status bits
#define CMD_COMPLETE            0x80
#define CMD_IN_PROGRESS         0x40
#define CMD_ERROR               0x20
#define CMD_RESULT_MASK         0x1F
#define CMD_PENDING             0x01
#define CMD_BUSY                0x02
#define CMD_ABORT               0x04
#define CMD_ENABLE              0x80
#define CMD_RESET               0x01
#define CMD_START               0x01
#define CMD_STOP                0x02
#define CMD_ABORTED             0x08
#define CMD_RESP_SUCCESS        0x00
#define CMD_RESP_ERROR          0x01
#define CMD_RESP_READY          0x01
#define CMD_TIMEOUT_FLAG        0x80

//=============================================================================
// System Registers
//=============================================================================
#define REG_SYSTEM_CTRL_0000    XDATA_REG8(0x0000)  // System control (RW)
#define REG_SYS_STATUS_PRIMARY  XDATA_REG8(0x0464)  // Primary status (RW)
#define REG_SYS_STATUS_SECONDARY XDATA_REG8(0x0465) // Secondary status (R)
#define REG_SYSTEM_CONFIG       XDATA_REG8(0x0466)  // Configuration (RW)
#define REG_SYSTEM_STATE        XDATA_REG8(0x0467)  // State (RW)
#define REG_DATA_PORT           XDATA_REG8(0x0468)  // Data port (RW)
#define REG_INT_STATUS          XDATA_REG8(0x0469)  // Interrupt status (RW)
#define REG_EP_CONFIG_BASE      XDATA_REG8(0x054B)  // EP config base (RW)
#define REG_EP_CONFIG_ARRAY     XDATA_REG8(0x054E)  // EP config array (indexed *20, RW)
#define REG_TRANSFER_SIZE       XDATA_REG32(0x05A8) // Transfer size (RW, 4 bytes)
#define REG_TRANSFER_FLAGS      XDATA_REG8(0x05B4)  // Transfer flags (RW)
#define REG_TRANSFER_CTRL       XDATA_REG8(0x05F8)  // Transfer control (RW)
#define REG_SYS_FLAGS_BASE      XDATA_REG8(0x07E4)  // Flags base (RW)
#define REG_SYS_FLAGS           XDATA_REG32(0x07EC) // System flags (RW, 4 bytes)
#define REG_CRITICAL_CTRL       XDATA_REG8(0xEF4E)  // Critical control (RW)

// System register aliases
#define REG_SYSTEM_STATUS       REG_SYS_STATUS_PRIMARY
#define REG_SYSTEM_CTRL         REG_CRITICAL_CTRL

// System status bits
#define SYS_READY               0x80
#define SYS_ERROR               0x40
#define SYS_BUSY                0x20
#define SYS_SOFT_RESET          0x20  // Alias for SYS_BUSY
#define SYS_LINK_UP             0x10
#define SYS_NVME_READY          0x08
#define SYS_USB_READY           0x04
#define SYS_ENABLE              0x04  // Alias for SYS_USB_READY
#define SYS_INIT                0x02
#define SYS_RESET               0x01

// Interrupt status bits
#define INT_TIMER               0x01
#define INT_DMA                 0x02
#define INT_NVME                0x04
#define INT_USB                 0x08

//=============================================================================
// Memory Buffers (NVMe Queues, USB/SCSI Buffers)
//=============================================================================
// NVMe Admin Queues
#define NVME_ASQ_BASE           0xB000  // Admin Submission Queue (512 bytes)
#define NVME_ASQ_SIZE           0x0100
#define NVME_ACQ_BASE           0xB100  // Admin Completion Queue (512 bytes)
#define NVME_ACQ_SIZE           0x0100

// NVMe I/O Queue (maps to PCIe DMA 0x00820000)
#define NVME_IOSQ_BASE          0xA000  // I/O Submission Queue (4 KB)
#define NVME_IOSQ_SIZE          0x1000
#define NVME_IOSQ_DMA_ADDR      0x00820000

// NVMe Data Buffer (maps to PCIe DMA 0x00200000)
#define NVME_DATA_BUF_BASE      0xF000  // Data buffer (4 KB)
#define NVME_DATA_BUF_SIZE      0x1000
#define NVME_DATA_BUF_DMA_ADDR  0x00200000

// USB/SCSI Buffers
#define USB_CTRL_BUF_BASE       0x9E00  // Control transfer buffer (512 bytes)
#define USB_CTRL_BUF_SIZE       0x0200
#define USB_SCSI_BUF_BASE       0x8000  // SCSI buffers (4 KB)
#define USB_SCSI_BUF_SIZE       0x1000

//=============================================================================
// Timeouts (milliseconds)
//=============================================================================
#define TIMEOUT_NVME            5000
#define TIMEOUT_DMA             10000

#endif /* __REGISTERS_H__ */
