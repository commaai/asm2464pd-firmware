/*
 * ASM2464PD USB Enumeration Firmware
 * Minimal firmware that enumerates as a USB 3.0 device
 */

#include "types.h"
#include "registers.h"

/* Override XDATA_REG macros to add volatile for proper MMIO access.
 * Without volatile, SDCC optimizes away reads whose value is discarded,
 * which breaks hardware handshake sequences that require register reads. */
#undef XDATA_REG8
#undef XDATA_REG16
#undef XDATA_REG32
#define XDATA_REG8(addr)   (*(volatile __xdata uint8_t *)(addr))
#define XDATA_REG16(addr)  (*(volatile __xdata uint16_t *)(addr))
#define XDATA_REG32(addr)  (*(volatile __xdata uint32_t *)(addr))

/* 8051 SFRs */
__sfr __at(0xA8) IE;
__sfr __at(0x88) TCON;

#define IE_EA   0x80
#define IE_EX1  0x04
#define IE_EX0  0x01

/*==========================================================================
 * UART - Debug Output
 *==========================================================================*/
static void uart_putc(uint8_t ch) { REG_UART_THR = ch; }
static void uart_puts(__code const char *s) { while (*s) uart_putc(*s++); }

static void uart_puthex(uint8_t val) {
    const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[val >> 4]);
    uart_putc(hex[val & 0x0F]);
}

static void memcpy_x(__xdata uint8_t *dst, const uint8_t *src, uint8_t n) {
    while (n--) *dst++ = *src++;
}

/*==========================================================================
 * Timer/MSC Reinit (from trace lines 4873-4901, 5000-5028)
 *==========================================================================*/
static void reinit_timers_and_msc(void) {
    uint8_t val;
    /* 92C8 read-writeback x2 */
    val = REG_POWER_CTRL_92C8; REG_POWER_CTRL_92C8 = val;
    val = REG_POWER_CTRL_92C8; REG_POWER_CTRL_92C8 = val;
    /* Timer2: stop, clear; Timer4: stop, clear */
    REG_TIMER2_CSR = 0x04; REG_TIMER2_CSR = 0x02;
    REG_TIMER4_CSR = 0x04; REG_TIMER4_CSR = 0x02;
    /* Timer2 config: read-writeback div, set thresholds */
    val = REG_TIMER2_DIV; REG_TIMER2_DIV = val;
    REG_TIMER2_THRESHOLD_LO = 0x00; REG_TIMER2_THRESHOLD_HI = 0x8B;
    /* Timer4 config */
    val = REG_TIMER4_DIV; REG_TIMER4_DIV = val;
    REG_TIMER4_THRESHOLD_LO = 0x00; REG_TIMER4_THRESHOLD_HI = 0xC7;
    /* MSC progressive init: 0→2→6→7→5→1→0 */
    val = REG_USB_MSC_CFG; REG_USB_MSC_CFG = val | 0x02;
    val = REG_USB_MSC_CFG; REG_USB_MSC_CFG = val | 0x04;
    val = REG_USB_MSC_CFG; REG_USB_MSC_CFG = val | 0x01;
    val = REG_USB_MSC_CFG; REG_USB_MSC_CFG = val & ~0x02;
    val = REG_USB_MSC_CFG; REG_USB_MSC_CFG = val & ~0x04;
    val = REG_USB_MSC_CFG; REG_USB_MSC_CFG = val & ~0x01;
    REG_USB_MSC_LENGTH = 0x0D;
}

/*==========================================================================
 * USB Request Handlers
 *==========================================================================*/

/* SET_ADDRESS: reference INT0 #2 register sequence
 * Read 9090, Write 9090=addr, Write 91D0=0x02,
 * Read 9100, Read 92F8 x2, Read 9002, Poll 9091 for 0x10,
 * Write 9092=0x08, Write 9091=0x10 */
static void handle_set_address(uint8_t addr) {
    (void)REG_USB_INT_MASK_9090;
    REG_USB_INT_MASK_9090 = addr;
    REG_USB_EP_CTRL_91D0 = 0x02;
    (void)REG_USB_LINK_STATUS;
    (void)XDATA_REG8(0x92F8);
    (void)XDATA_REG8(0x92F8);
    (void)REG_USB_CONFIG;
    while (!(REG_USB_CTRL_PHASE & USB_CTRL_PHASE_STATUS)) { }
    REG_USB_DMA_TRIGGER = USB_DMA_STATUS_COMPLETE;
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_STATUS;
}

/* USB Descriptors */
const uint8_t desc_device[] = {
    0x12, 0x01,             /* bLength, bDescriptorType */
    0x20, 0x03,             /* bcdUSB = 3.2 */
    0x00, 0x00, 0x00,       /* bDeviceClass, bDeviceSubClass, bDeviceProtocol */
    0x09,                   /* bMaxPacketSize0 = 512 */
    0x22, 0x11,             /* idVendor = 0x1122 */
    0x44, 0x33,             /* idProduct = 0x3344 */
    0x01, 0x00,             /* bcdDevice */
    0x01, 0x02, 0x03,       /* iManufacturer, iProduct, iSerialNumber */
    0x01                    /* bNumConfigurations */
};

const uint8_t desc_bos[] = {
    0x05, 0x0F, 0x16, 0x00, 0x02,   /* BOS header: 22 bytes, 2 caps */
    0x07, 0x10, 0x02, 0x02, 0x00, 0x00, 0x00,  /* USB 2.0 Extension */
    0x0A, 0x10, 0x03, 0x00, 0x0E, 0x00, 0x03, 0x00, 0x00, 0x00  /* SuperSpeed */
};

const uint8_t desc_config[] = {
    0x09, 0x02,             /* bLength, bDescriptorType */
    0x09, 0x00,             /* wTotalLength = 9 */
    0x00, 0x01,             /* bNumInterfaces, bConfigurationValue */
    0x00, 0x80, 0x32        /* iConfiguration, bmAttributes, bMaxPower */
};

const uint8_t desc_string0[] = { 0x04, 0x03, 0x09, 0x04 };
const uint8_t desc_string1[] = { 0x0A, 0x03, 't', 0, 'i', 0, 'n', 0, 'y', 0 };
const uint8_t desc_string2[] = { 0x08, 0x03, 'u', 0, 's', 0, 'b', 0 };
const uint8_t desc_string3[] = { 0x08, 0x03, '0', 0, '0', 0, '1', 0 };

/* GET_DESCRIPTOR: trace lines 5176-5205 */
static void handle_get_descriptor(uint8_t type, uint8_t idx, uint8_t wLenL, uint8_t wLenH) {
    const uint8_t *src = 0;
    uint8_t desc_len = 0;
    uint16_t wLen;
    __xdata uint8_t *buf = (__xdata uint8_t *)USB_CTRL_BUF_BASE;

    /* Read link status (trace line 5177) */
    (void)REG_USB_LINK_STATUS;

    /* Select descriptor */
    if (type == USB_DESC_TYPE_DEVICE) {
        src = desc_device; desc_len = sizeof(desc_device);
    } else if (type == USB_DESC_TYPE_BOS) {
        src = desc_bos; desc_len = sizeof(desc_bos);
    } else if (type == USB_DESC_TYPE_CONFIG) {
        src = desc_config; desc_len = sizeof(desc_config);
    } else if (type == USB_DESC_TYPE_STRING) {
        if (idx == 0) { src = desc_string0; desc_len = sizeof(desc_string0); }
        else if (idx == 1) { src = desc_string1; desc_len = sizeof(desc_string1); }
        else if (idx == 2) { src = desc_string2; desc_len = sizeof(desc_string2); }
        else { src = desc_string3; desc_len = sizeof(desc_string3); }
    }

    if (!src) desc_len = 0;  /* Unknown type: send ZLP */
    wLen = ((uint16_t)wLenH << 8) | wLenL;
    if (src && wLen < desc_len) desc_len = (uint8_t)wLen;
    if (desc_len > 0) memcpy_x(buf, src, desc_len);

    /* Read link status again (trace line 5186) */
    (void)REG_USB_LINK_STATUS;
    /* Read config, then poll data phase ready (trace lines 5190-5193) */
    (void)REG_USB_CONFIG;
    (void)REG_USB_CTRL_PHASE;
    (void)REG_USB_CTRL_PHASE;
    (void)REG_USB_CTRL_PHASE;

    /* DMA send (trace lines 5194-5201) */
    REG_USB_EP0_STATUS = 0x00;
    REG_USB_EP0_LEN_L = desc_len;
    REG_USB_DMA_TRIGGER = USB_DMA_SEND;
    while (REG_USB_DMA_TRIGGER) { }
    /* Verify transfer (read status+length twice) */
    (void)REG_USB_EP0_STATUS; (void)REG_USB_EP0_LEN_L;
    (void)REG_USB_EP0_STATUS; (void)REG_USB_EP0_LEN_L;

    /* Ack data phase (trace line 5202) */
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_DATA;
    /* Wait for status phase (trace line 5203) */
    while (!(REG_USB_CTRL_PHASE & USB_CTRL_PHASE_STATUS)) { }
    /* Complete status (trace lines 5204-5205) */
    REG_USB_DMA_TRIGGER = USB_DMA_STATUS_COMPLETE;
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_STATUS;
}

/*==========================================================================
 * Interrupt Handlers
 *==========================================================================*/

static uint8_t link_reinit_count;

void int0_isr(void) __interrupt(0) {
    uint8_t s9101, phase, val;
    uint8_t bmReq, bReq, wValL, wValH, wLenL, wLenH;

    /* Entry: read USB interrupt status (trace: always first) */
    (void)REG_INT_USB_STATUS;

    /* Read peripheral status */
    s9101 = REG_USB_PERIPH_STATUS;

    if (s9101 & USB_PERIPH_DESC_REQ) {
        /*--- SETUP packet (9101 & 0x02) - trace lines 5091-5109 ---*/
        (void)REG_USB_PERIPH_STATUS;
        (void)REG_USB_PERIPH_STATUS;
        (void)REG_USB_PERIPH_STATUS;

        /* Read control phase twice (trace lines 5095-5096) */
        phase = REG_USB_CTRL_PHASE;
        phase = REG_USB_CTRL_PHASE;

        /* USB config handshake (trace lines 5097-5098) */
        val = REG_USB_CONFIG;
        REG_USB_CONFIG = val;

        /* Read endpoint control (trace line 5099) */
        (void)XDATA_REG8(0x9220);

        /* Start setup phase (trace line 5100) */
        REG_USB_CTRL_PHASE = USB_CTRL_PHASE_SETUP;

        /* Read 8-byte setup packet (trace lines 5101-5108) */
        bmReq = REG_USB_SETUP_BMREQ;
        bReq  = REG_USB_SETUP_BREQ;
        wValL = REG_USB_SETUP_WVAL_L;
        wValH = REG_USB_SETUP_WVAL_H;
        (void)REG_USB_SETUP_WIDX_L;
        (void)REG_USB_SETUP_WIDX_H;
        wLenL = REG_USB_SETUP_WLEN_L;
        wLenH = REG_USB_SETUP_WLEN_H;

        /* Read phase to determine state (trace line 5109/5176) */
        phase = REG_USB_CTRL_PHASE;

        if (bmReq == 0x00 && bReq == USB_REQ_SET_ADDRESS) {
            handle_set_address(wValL);
        } else if (bmReq == 0x80 && bReq == USB_REQ_GET_DESCRIPTOR) {
            if (phase & USB_CTRL_PHASE_DATA) {
                handle_get_descriptor(wValH, wValL, wLenL, wLenH);
            }
        } else {
            /* Status-only: SET_ISOCH_DELAY, SET_CONFIGURATION, SET_INTERFACE, etc.
             * Reference INT0 #4: read 9002, poll 9091, write 9092=0x08, 9091=0x10 */
            (void)REG_USB_CONFIG;
            while (!(REG_USB_CTRL_PHASE & USB_CTRL_PHASE_STATUS)) { }
            REG_USB_DMA_TRIGGER = USB_DMA_STATUS_COMPLETE;
            REG_USB_CTRL_PHASE = USB_CTRL_PHASE_STATUS;
        }

    } else if (s9101 & USB_PERIPH_BULK_REQ) {
        /*--- Buffer event (9101 & 0x08) - trace lines 4866-4901 ---*/
        (void)REG_USB_PERIPH_STATUS;

        val = REG_BUF_CFG_9301;
        if (val & 0x80) {
            (void)REG_BUF_CFG_9301;
            REG_BUF_CFG_9301 = 0x80;
            (void)REG_POWER_DOMAIN;
            REG_POWER_DOMAIN = 0x02;
            reinit_timers_and_msc();
        } else if (val & 0x40) {
            /* trace lines 4997-4999: E716 read-writeback, read 92C2 */
            {
                uint8_t e716 = XDATA_REG8(0xE716);
                XDATA_REG8(0xE716) = e716;
            }
            (void)REG_POWER_STATUS;
            reinit_timers_and_msc();
            REG_BUF_CFG_9301 = val;
        }

    } else if (s9101 & 0x10) {
        /*--- Link status event (9101 & 0x10) ---*/
        (void)REG_USB_PERIPH_STATUS;
        (void)REG_USB_PERIPH_STATUS;
        (void)REG_USB_PERIPH_STATUS;

        /* Read 9300, write back with bit 1 cleared (acknowledge event) */
        val = REG_BUF_CFG_9300;
        REG_BUF_CFG_9300 = val & ~0x02;

        /* E716 read-writeback */
        val = XDATA_REG8(0xE716);
        XDATA_REG8(0xE716) = val;

        /* Link reinit: E716 clear/restore + force power event + timer/MSC reinit.
         * Rate-limited: only first 2 invocations do the full reinit to avoid
         * destabilizing during interrupt storms. */
        if (link_reinit_count < 2) {
            link_reinit_count++;
            XDATA_REG8(0xE716) = 0x00;
            XDATA_REG8(0xE716) = 0x03;
            val = REG_POWER_STATUS;
            REG_POWER_STATUS = val | 0x40;
            reinit_timers_and_msc();
        }
    }

    /* Check/handle power event before exit (original does this inside ISR) */
    val = REG_POWER_STATUS;
    if (val & 0x40) {
        uint8_t e = REG_POWER_EVENT_92E1;
        REG_POWER_EVENT_92E1 = e | 0x40;
        REG_POWER_STATUS = val & ~0x40;
    }

    /* Exit: read system and USB interrupt status (trace lines 5147-5148) */
    (void)REG_INT_SYSTEM;
    (void)REG_INT_USB_STATUS;
}

/* INT1: trace lines 2000-2013 */
void int1_isr(void) __interrupt(2) {
    uint8_t val;
    (void)REG_INT_SYSTEM;
    (void)REG_TIMER3_CSR;
    val = REG_CPU_DMA_INT; REG_CPU_DMA_INT = val;
    (void)REG_XFER_DMA_CFG;
    (void)REG_XFER2_DMA_STATUS;
    (void)REG_CPU_EXT_STATUS;
    (void)REG_CPU_EXEC_STATUS_2;
    (void)REG_INT_PCIE_NVME;
    (void)REG_INT_PCIE_NVME;
    (void)REG_INT_PCIE_NVME;
    (void)REG_INT_PCIE_NVME;
    (void)REG_INT_SYSTEM;
}

void timer0_isr(void) __interrupt(1) { }
void timer1_isr(void) __interrupt(3) { }
void serial_isr(void) __interrupt(4) { }
void timer2_isr(void) __interrupt(5) { }

/*==========================================================================
 * Hardware Init Sequence (from trace)
 *==========================================================================*/

static void hw_init(void) {
    /* PCIe/DMA init */
    REG_CPU_EXEC_STATUS = 0x01; REG_CPU_MODE = 0x01;
    XDATA_REG8(0xE710) = 0x04; REG_CPU_EXEC_STATUS_2 = 0x04;
    REG_TIMER_CTRL_CC3B = 0x0C; XDATA_REG8(0xE717) = 0x01;
    REG_CPU_CTRL_CC3E = 0x00; REG_TIMER_CTRL_CC3B = 0x0C;
    REG_TIMER_CTRL_CC3B = 0x0C; XDATA_REG8(0xE716) = 0x03;
    REG_CPU_CTRL_CC3E = 0x00; REG_TIMER_CTRL_CC39 = 0x06;
    REG_TIMER_ENABLE_B = 0x14; REG_TIMER_ENABLE_A = 0x44;
    REG_CPU_CTRL_CC37 = 0x2C; XDATA_REG8(0xE780) = 0x00;
    XDATA_REG8(0xE716) = 0x00; XDATA_REG8(0xE716) = 0x03;
    
    /* Timer init sequence */
    { uint8_t i; for(i = 0; i < 4; i++) {
        REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
        REG_TIMER0_DIV = (i == 3) ? 0x13 : 0x12;
        REG_TIMER0_THRESHOLD_HI = 0x00;
        REG_TIMER0_THRESHOLD_LO = (i == 0) ? 0xC8 : (i == 1) ? 0x14 : 0x0A;
        REG_TIMER0_CSR = 0x01;
        if (i < 3) { REG_TIMER0_CSR = 0x02; }
    }}
    
    /* PHY config */
    XDATA_REG8(0xE7E3) = 0x00;
    XDATA_REG8(0xE764) = 0x14; XDATA_REG8(0xE764) = 0x14;
    XDATA_REG8(0xE764) = 0x14; XDATA_REG8(0xE764) = 0x14;
    XDATA_REG8(0xE76C) = 0x04; XDATA_REG8(0xE774) = 0x04;
    XDATA_REG8(0xE77C) = 0x04;
    
    /* More timer/clock setup */
    { uint8_t i; for(i = 0; i < 4; i++) {
        REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
        REG_TIMER0_DIV = 0x12; REG_TIMER0_THRESHOLD_HI = 0x00;
        REG_TIMER0_THRESHOLD_LO = 0xC7; REG_TIMER0_CSR = 0x01;
        REG_TIMER0_CSR = 0x02;
    }}
    
    /* Flash/interrupt config */
    REG_INT_AUX_STATUS = 0x02; REG_FLASH_DIV = 0x04;
    
    /* Flash status polling */
    { uint8_t i; for(i = 0; i < 20; i++) {
        REG_FLASH_MODE = 0x00; REG_FLASH_BUF_OFFSET_LO = 0x00;
        REG_FLASH_BUF_OFFSET_HI = 0x00; REG_FLASH_CMD = (i == 0) ? 0x06 : 0x05;
        REG_FLASH_ADDR_LEN = 0x04; REG_FLASH_ADDR_LO = 0x00;
        REG_FLASH_ADDR_MD = 0x00; REG_FLASH_ADDR_HI = 0x00;
        REG_FLASH_DATA_LEN = 0x00; REG_FLASH_DATA_LEN_HI = (i == 0) ? 0x00 : 0x01;
        REG_FLASH_CSR = 0x01;
    }}
    
    /* USB PHY and link init */
    REG_CPU_EXEC_STATUS_3 = 0x00; REG_INT_ENABLE = 0x10;
    REG_INT_STATUS_C800 = 0x04; REG_INT_STATUS_C800 = 0x05;
    REG_TIMER_CTRL_CC3B = 0x0D; REG_TIMER_CTRL_CC3B = 0x0F;
    REG_POWER_CTRL_92C6 = 0x05; REG_POWER_CTRL_92C7 = 0x00;
    REG_USB_CTRL_9201 = 0x0E; REG_USB_CTRL_9201 = 0x0C;
    REG_CLOCK_ENABLE = 0x82; REG_USB_CTRL_920C = 0x61;
    REG_USB_CTRL_920C = 0x60; REG_POWER_ENABLE = 0x87;
    REG_CLOCK_ENABLE = 0x83; REG_PHY_POWER = 0x2F;
    REG_USB_PHY_CONFIG_9241 = 0x10; REG_USB_PHY_CONFIG_9241 = 0xD0;
    XDATA_REG8(0xE741) = 0x5B; XDATA_REG8(0xE741) = 0x6B;
    XDATA_REG8(0xE742) = 0x1F; XDATA_REG8(0xE741) = 0xAB;
    XDATA_REG8(0xE742) = 0x17; XDATA_REG8(0xCC43) = 0x88;
    
    /* Buffer config */
    XDATA_REG8(0x9316) = 0x00; XDATA_REG8(0x9317) = 0x00;
    XDATA_REG8(0x931A) = 0x00; XDATA_REG8(0x931B) = 0x00;
    XDATA_REG8(0x9322) = 0x00; XDATA_REG8(0x9323) = 0x00;
    XDATA_REG8(0x9310) = 0x01; XDATA_REG8(0x9311) = 0x60;
    XDATA_REG8(0x9312) = 0x00; XDATA_REG8(0x9313) = 0xE3;
    XDATA_REG8(0x9314) = 0x01; XDATA_REG8(0x9315) = 0x60;
    XDATA_REG8(0x9318) = 0x01; XDATA_REG8(0x9319) = 0x60;
    XDATA_REG8(0x931C) = 0x00; XDATA_REG8(0x931D) = 0x03;
    XDATA_REG8(0x931E) = 0x00; XDATA_REG8(0x931F) = 0xE0;
    XDATA_REG8(0x9320) = 0x00; XDATA_REG8(0x9321) = 0xE3;
    
    /* USB endpoint and interrupt config */
    REG_CPU_EXEC_STATUS_3 = 0x00; REG_USB_EP_CTRL_905F = 0x44;
    XDATA_REG8(0xCC2A) = 0x04; XDATA_REG8(0xCC2C) = 0xC7;
    XDATA_REG8(0xCC2D) = 0xC7; REG_INT_ENABLE = 0x50;
    REG_CPU_EXEC_STATUS = 0x00; XDATA_REG8(0xC807) = 0x04;
    REG_POWER_CTRL_92C8 = 0x24; REG_POWER_CTRL_92C8 = 0x24;
    
    /* Timer 2/4 config */
    REG_TIMER2_CSR = 0x04; REG_TIMER2_CSR = 0x02;
    REG_TIMER4_CSR = 0x04; REG_TIMER4_CSR = 0x02;
    REG_TIMER2_DIV = 0x16; REG_TIMER2_THRESHOLD_LO = 0x00;
    REG_TIMER2_THRESHOLD_HI = 0x8B; REG_TIMER4_DIV = 0x54;
    REG_TIMER4_THRESHOLD_LO = 0x00; REG_TIMER4_THRESHOLD_HI = 0xC7;
    
    /* USB MSC/endpoint init */
    REG_USB_MSC_CFG = 0x07; REG_USB_MSC_CFG = 0x07;
    REG_USB_MSC_CFG = 0x07; REG_USB_MSC_CFG = 0x05;
    REG_USB_MSC_CFG = 0x01; REG_USB_MSC_CFG = 0x00;
    REG_USB_MSC_LENGTH = 0x0D; REG_POWER_ENABLE = 0x87;
    REG_USB_PHY_CTRL_91D1 = 0x0F; REG_BUF_CFG_9300 = 0x0C;
    REG_BUF_CFG_9301 = 0xC0; REG_BUF_CFG_9302 = 0xBF;
    REG_USB_CTRL_PHASE = 0x1F; REG_USB_EP_CFG1 = 0x0F;
    REG_USB_PHY_CTRL_91C1 = 0xF0; REG_BUF_CFG_9303 = 0x33;
    REG_BUF_CFG_9304 = 0x3F; REG_BUF_CFG_9305 = 0x40;
    REG_USB_CONFIG = 0xE0; REG_USB_EP0_LEN_H = 0xF0;
    REG_USB_MODE = 0x01; REG_USB_EP_MGMT = 0x00;
    
    /* Endpoint mask config */
    REG_USB_EP_READY = 0xFF; REG_USB_EP_CTRL_9097 = 0xFF;
    REG_USB_EP_MODE_9098 = 0xFF; XDATA_REG8(0x9099) = 0xFF;
    XDATA_REG8(0x909A) = 0xFF; XDATA_REG8(0x909B) = 0xFF;
    XDATA_REG8(0x909C) = 0xFF; XDATA_REG8(0x909D) = 0xFF;
    REG_USB_STATUS_909E = 0x03; REG_USB_DATA_H = 0xFF;
    REG_USB_FIFO_STATUS = 0xFF; REG_USB_FIFO_H = 0xFF;
    XDATA_REG8(0x9014) = 0xFF; XDATA_REG8(0x9015) = 0xFF;
    XDATA_REG8(0x9016) = 0xFF; XDATA_REG8(0x9017) = 0xFF;
    REG_USB_XCVR_MODE = 0x03; REG_USB_DATA_L = 0xFE;
    REG_USB_PHY_CTRL_91C3 = 0x00; REG_USB_PHY_CTRL_91C0 = 0x13;
    REG_USB_PHY_CTRL_91C0 = 0x12;
    
    /* Final timer/DMA config */
    REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
    REG_TIMER0_DIV = 0x14; REG_TIMER0_THRESHOLD_HI = 0x01;
    REG_TIMER0_THRESHOLD_LO = 0x8F; REG_TIMER0_CSR = 0x01;
    REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
    REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
    REG_TIMER0_DIV = 0x10; REG_TIMER0_THRESHOLD_HI = 0x00;
    REG_TIMER0_THRESHOLD_LO = 0x09; REG_TIMER0_CSR = 0x01;
    REG_TIMER0_CSR = 0x02; XDATA_REG8(0xC807) = 0x04;
    XDATA_REG8(0xC807) = 0x84; XDATA_REG8(0xE7FC) = 0xFF;
    
    /* DMA channel config */
    XDATA_REG8(0xCCD9) = 0x04; XDATA_REG8(0xCCD9) = 0x02;
    XDATA_REG8(0xCCD8) = 0x00; REG_INT_ENABLE = 0x50;
    XDATA_REG8(0xCCD8) = 0x04; XDATA_REG8(0xCCDA) = 0x00;
    XDATA_REG8(0xCCDB) = 0xC8; REG_INT_CTRL = 0x08;
    REG_INT_CTRL = 0x0A; REG_INT_CTRL = 0x0A;
    XDATA_REG8(0xCCF8) = 0x40; XDATA_REG8(0xCCF9) = 0x04;
    XDATA_REG8(0xCCF9) = 0x02;
    
    /* Timer interrupt setup */
    XDATA_REG8(0xCC88) = 0x10; XDATA_REG8(0xCC8A) = 0x00;
    XDATA_REG8(0xCC8B) = 0x0A; XDATA_REG8(0xCC89) = 0x01;
    XDATA_REG8(0xCC89) = 0x02; XDATA_REG8(0xCC88) = 0x10;
    XDATA_REG8(0xCC8A) = 0x00; XDATA_REG8(0xCC8B) = 0x3C;
    XDATA_REG8(0xCC89) = 0x01; XDATA_REG8(0xCC89) = 0x02;
    REG_INT_CTRL = 0x2A; REG_INT_ENABLE = 0x50;
    
    /* CPU control */
    XDATA_REG8(0xCC80) = 0x00; XDATA_REG8(0xCC80) = 0x03;
    XDATA_REG8(0xCC99) = 0x04; XDATA_REG8(0xCC99) = 0x02;
    REG_INT_ENABLE = 0x50; XDATA_REG8(0xCC98) = 0x00;
    XDATA_REG8(0xCC98) = 0x04;
    
    /* DMA control final */
    XDATA_REG8(0xCC82) = 0x18; XDATA_REG8(0xCC83) = 0x9C;
    XDATA_REG8(0xCC91) = 0x04; XDATA_REG8(0xCC91) = 0x02;
    REG_INT_ENABLE = 0x50; XDATA_REG8(0xCC90) = 0x00;
    XDATA_REG8(0xCC90) = 0x05; XDATA_REG8(0xCC92) = 0x00;
    XDATA_REG8(0xCC93) = 0xC8; XDATA_REG8(0xCC91) = 0x01;
}

/*==========================================================================
 * Main
 *==========================================================================*/

void main(void) {
    uint8_t val;

    IE = 0;
    REG_UART_LCR &= 0xF7;
    uart_puts("\n[BOOT]\n");

    hw_init();

    uart_puts("[GO]\n");

    TCON = 0;  /* Level-triggered interrupts */
    IE = IE_EA | IE_EX0 | IE_EX1;

    while (1) {
        /* Timer control: read, OR in bit 3 (trace: read 0x04, write 0x0C) */
        val = XDATA_REG8(0xCC2A);
        XDATA_REG8(0xCC2A) = val | 0x08;

        /* USB DMA state (original main loop reads this) */
        (void)XDATA_REG8(0xCE89);

        /* Check power status, handle power event (trace: 92C2) */
        val = REG_POWER_STATUS;
        if (val & 0x40) {
            /* Read 92E1 first, then write back (trace: read 0x60, write 0x60) */
            { uint8_t e = REG_POWER_EVENT_92E1; REG_POWER_EVENT_92E1 = e; }
            REG_POWER_STATUS = val & ~0x40;
        }

        /* CPU timer control (original main loop reads this) */
        (void)XDATA_REG8(0xCD31);

        /* PHY config: read-writeback (original main loop does this) */
        val = XDATA_REG8(0xC655);
        XDATA_REG8(0xC655) = val;
        val = XDATA_REG8(0xC620);
        XDATA_REG8(0xC620) = val;

        /* Read power status twice (trace: 92F7 x2) */
        (void)REG_POWER_STATUS_92F7;
        (void)REG_POWER_STATUS_92F7;

        /* Read USB status (trace: 9000) */
        (void)REG_USB_STATUS;

        /* High-frequency registers from reference (top accessed):
         * E716 (2020 accesses), C520 (710), CEF3 (350) */
        (void)XDATA_REG8(0xE716);
        (void)XDATA_REG8(0xC520);
        (void)XDATA_REG8(0xCEF3);

        /* Reset link reinit rate limiter so ISR can reinit again */
        link_reinit_count = 0;
    }
}
