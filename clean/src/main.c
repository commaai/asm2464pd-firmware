/*
 * ASM2464PD USB 3.0 SuperSpeed Enumeration Firmware
 *
 * Minimal firmware that enumerates on USB 3.0 SuperSpeed.
 * Init sequence from trace/enumerate_min (stock firmware trace).
 * USB 3.0 control transfer handling from trace/flash_usb3.
 *
 * USB 3.0 vs 2.0 key differences:
 *   9100 = 0x02 (USB 3.0) vs 0x01 (USB 2.0)
 *   No-data requests: 9091 goes 0x11→0x10, complete with 9092=0x08, ack 0x10
 *   Data IN status: after data send, 9091→0x10, complete with 9092=0x08
 *   SET_ADDRESS: requires 9206/9207/9208-920B/9202/9220 register dance
 *   No ZLP needed for status phase on USB 3.0
 */

#include "types.h"
#include "registers.h"

__sfr __at(0xA8) IE;
__sfr __at(0x88) TCON;
#define IE_EA   0x80
#define IE_EX1  0x04
#define IE_ET0  0x02
#define IE_EX0  0x01

/* Write a byte to the descriptor response buffer at 0x9E00+offset */
#define DESC_BUF(n) XDATA_REG8(USB_CTRL_BUF_BASE + (n))

void uart_putc(uint8_t ch) { REG_UART_THR = ch; }
void uart_puts(__code const char *str) { while (*str) uart_putc(*str++); }

static void uart_puthex(uint8_t val) {
    const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[val >> 4]);
    uart_putc(hex[val & 0x0F]);
}

/* Global: 1 if USB 3.0 SuperSpeed, 0 if USB 2.0 */
static volatile uint8_t is_usb3;

/*==========================================================================
 * USB Control Transfer Helpers (USB 2.0 and USB 3.0)
 *==========================================================================*/

/* Complete USB 3.0 status phase */
static void complete_usb3_status(void) {
    while (!(REG_USB_CTRL_PHASE & 0x10)) { }
    REG_USB_DMA_TRIGGER = 0x08;
    REG_USB_CTRL_PHASE = 0x10;
}

/* Complete USB 2.0 status phase (for IN transfers with data) */
static void complete_usb20_status(void) {
    uint8_t c;
    c = REG_USB_CONFIG;
    REG_USB_CONFIG = c | 0x02;
    REG_USB_DMA_TRIGGER = 0x02;
    REG_USB_CTRL_PHASE = 0x02;
    REG_USB_CTRL_PHASE = 0x02;
    c = REG_USB_CONFIG;
    REG_USB_CONFIG = c & ~0x02;
    REG_USB_CTRL_PHASE = 0x04;
}

/* Complete status for no-data OUT requests */
static void send_zlp_ack(void) {
    if (is_usb3) {
        complete_usb3_status();
    } else {
        REG_USB_EP0_STATUS = 0x00;
        REG_USB_EP0_LEN_L = 0x00;
        REG_USB_DMA_TRIGGER = USB_DMA_SEND;
        REG_USB_CTRL_PHASE = 0x08;
    }
}

/* Send descriptor data via DMA, ack data phase, complete status */
static void send_descriptor_data(uint8_t len) {
    REG_USB_EP0_STATUS = 0x00;
    REG_USB_EP0_LEN_L = len;
    REG_USB_DMA_TRIGGER = USB_DMA_SEND;
    while (REG_USB_DMA_TRIGGER) { }
    REG_USB_CTRL_PHASE = 0x08;
    if (is_usb3) {
        complete_usb3_status();
    }
    /* USB 2.0: status handled by next ISR via complete_usb20_status */
}

/*==========================================================================
 * USB Request Handlers
 *==========================================================================*/

static void handle_set_address(void) {
    uint8_t tmp;
    REG_USB_INT_MASK_9090 = 0x01;
    REG_USB_EP_CTRL_91D0 = 0x02;

    if (is_usb3) {
        /* USB 3.0 SET_ADDRESS (from trace/flash_usb3 lines 3242-3279) */
        REG_LINK_STATUS_E716 = 0x01;

        while (!(REG_USB_CTRL_PHASE & 0x10)) { }

        /* Address programming registers */
        REG_USB_ADDR_CFG_A = 0x03;
        REG_USB_ADDR_CFG_B = 0x03;
        REG_USB_ADDR_CFG_A = 0x07;
        REG_USB_ADDR_CFG_B = 0x07;
        tmp = REG_USB_ADDR_CFG_A;
        REG_USB_ADDR_CFG_A = tmp;
        tmp = REG_USB_ADDR_CFG_B;
        REG_USB_ADDR_CFG_B = tmp;
        REG_USB_ADDR_PARAM_0 = 0x00;
        REG_USB_ADDR_PARAM_1 = 0x0A;
        REG_USB_ADDR_PARAM_2 = 0x00;
        REG_USB_ADDR_PARAM_3 = 0x0A;
        tmp = REG_USB_ADDR_CTRL;
        REG_USB_ADDR_CTRL = tmp;
        REG_USB_EP_CTRL_9220 = 0x04;

        REG_USB_DMA_TRIGGER = 0x08;
        REG_USB_CTRL_PHASE = 0x10;
    } else {
        send_zlp_ack();
    }
    uart_puts("[A]\n");
}

static void handle_get_descriptor(uint8_t desc_type, uint8_t desc_idx, uint8_t wlen) {
    uint8_t actual_len;

    while (!(REG_USB_CTRL_PHASE & 0x08)) { }

    if (desc_type == 0x01) {
        /* Device descriptor */
        DESC_BUF(0x00) = 0x12;
        DESC_BUF(0x01) = 0x01;
        if (is_usb3) {
            DESC_BUF(0x02) = 0x20;      /* bcdUSB = 0x0320 */
            DESC_BUF(0x03) = 0x03;
        } else {
            DESC_BUF(0x02) = 0x10;      /* bcdUSB = 0x0210 */
            DESC_BUF(0x03) = 0x02;
        }
        DESC_BUF(0x04) = 0x00;
        DESC_BUF(0x05) = 0x00;
        DESC_BUF(0x06) = 0x00;
        DESC_BUF(0x07) = is_usb3 ? 0x09 : 0x40;  /* 512 or 64 */
        DESC_BUF(0x08) = 0xD1;          /* idVendor = 0xADD1 */
        DESC_BUF(0x09) = 0xAD;
        DESC_BUF(0x0A) = 0x01;          /* idProduct = 0x0001 */
        DESC_BUF(0x0B) = 0x00;
        DESC_BUF(0x0C) = 0x01;          /* bcdDevice = 0x0001 */
        DESC_BUF(0x0D) = 0x00;
        DESC_BUF(0x0E) = 0x01;
        DESC_BUF(0x0F) = 0x02;
        DESC_BUF(0x10) = 0x03;
        DESC_BUF(0x11) = 0x01;
        actual_len = (wlen < 18) ? wlen : 18;
        send_descriptor_data(actual_len);

    } else if (desc_type == 0x02) {
        /* Config + Interface + 2 Bulk EPs = 32 bytes */
        DESC_BUF(0x00) = 0x09;
        DESC_BUF(0x01) = 0x02;
        DESC_BUF(0x02) = 0x20;          /* wTotalLength = 32 */
        DESC_BUF(0x03) = 0x00;
        DESC_BUF(0x04) = 0x01;          /* bNumInterfaces */
        DESC_BUF(0x05) = 0x01;
        DESC_BUF(0x06) = 0x00;
        DESC_BUF(0x07) = 0xC0;          /* Self-powered */
        DESC_BUF(0x08) = 0x00;
        /* Interface */
        DESC_BUF(0x09) = 0x09;
        DESC_BUF(0x0A) = 0x04;
        DESC_BUF(0x0B) = 0x00;
        DESC_BUF(0x0C) = 0x00;
        DESC_BUF(0x0D) = 0x02;          /* 2 endpoints */
        DESC_BUF(0x0E) = 0xFF;          /* Vendor class */
        DESC_BUF(0x0F) = 0xFF;
        DESC_BUF(0x10) = 0xFF;
        DESC_BUF(0x11) = 0x00;
        /* Bulk IN EP1 */
        DESC_BUF(0x12) = 0x07;
        DESC_BUF(0x13) = 0x05;
        DESC_BUF(0x14) = 0x81;
        DESC_BUF(0x15) = 0x02;
        DESC_BUF(0x16) = 0x00;          /* wMaxPacketSize = 512 */
        DESC_BUF(0x17) = 0x02;
        DESC_BUF(0x18) = 0x00;
        /* Bulk OUT EP2 */
        DESC_BUF(0x19) = 0x07;
        DESC_BUF(0x1A) = 0x05;
        DESC_BUF(0x1B) = 0x02;
        DESC_BUF(0x1C) = 0x02;
        DESC_BUF(0x1D) = 0x00;          /* wMaxPacketSize = 512 */
        DESC_BUF(0x1E) = 0x02;
        DESC_BUF(0x1F) = 0x00;
        actual_len = (wlen < 32) ? wlen : 32;
        send_descriptor_data(actual_len);

    } else if (desc_type == 0x0F) {
        /* BOS: 5 + 7 (USB 2.0 ext) + 10 (SS cap) = 22 bytes */
        DESC_BUF(0x00) = 0x05;
        DESC_BUF(0x01) = 0x0F;
        DESC_BUF(0x02) = 0x16;          /* wTotalLength = 22 */
        DESC_BUF(0x03) = 0x00;
        DESC_BUF(0x04) = 0x02;          /* 2 capabilities */
        /* USB 2.0 Extension (7 bytes) */
        DESC_BUF(0x05) = 0x07;
        DESC_BUF(0x06) = 0x10;
        DESC_BUF(0x07) = 0x02;
        DESC_BUF(0x08) = 0x02;          /* LPM */
        DESC_BUF(0x09) = 0x00;
        DESC_BUF(0x0A) = 0x00;
        DESC_BUF(0x0B) = 0x00;
        /* SuperSpeed Device Capability (10 bytes) */
        DESC_BUF(0x0C) = 0x0A;
        DESC_BUF(0x0D) = 0x10;
        DESC_BUF(0x0E) = 0x03;          /* SUPERSPEED_USB */
        DESC_BUF(0x0F) = 0x00;
        DESC_BUF(0x10) = 0x0E;          /* SS|HS|FS */
        DESC_BUF(0x11) = 0x00;
        DESC_BUF(0x12) = 0x03;
        DESC_BUF(0x13) = 0x00;
        DESC_BUF(0x14) = 0x00;
        DESC_BUF(0x15) = 0x00;
        actual_len = (wlen < 22) ? wlen : 22;
        send_descriptor_data(actual_len);

    } else if (desc_type == 0x03) {
        /* String descriptors */
        if (desc_idx == 0) {
            DESC_BUF(0) = 0x04; DESC_BUF(1) = 0x03;
            DESC_BUF(2) = 0x09; DESC_BUF(3) = 0x04;
            actual_len = (wlen < 4) ? wlen : 4;
        } else if (desc_idx == 1) {
            DESC_BUF(0) = 0x0A; DESC_BUF(1) = 0x03;
            DESC_BUF(2) = 't'; DESC_BUF(3) = 0;
            DESC_BUF(4) = 'i'; DESC_BUF(5) = 0;
            DESC_BUF(6) = 'n'; DESC_BUF(7) = 0;
            DESC_BUF(8) = 'y'; DESC_BUF(9) = 0;
            actual_len = (wlen < 10) ? wlen : 10;
        } else if (desc_idx == 2) {
            DESC_BUF(0) = 0x08; DESC_BUF(1) = 0x03;
            DESC_BUF(2) = 'u'; DESC_BUF(3) = 0;
            DESC_BUF(4) = 's'; DESC_BUF(5) = 0;
            DESC_BUF(6) = 'b'; DESC_BUF(7) = 0;
            actual_len = (wlen < 8) ? wlen : 8;
        } else if (desc_idx == 3) {
            DESC_BUF(0) = 0x08; DESC_BUF(1) = 0x03;
            DESC_BUF(2) = '0'; DESC_BUF(3) = 0;
            DESC_BUF(4) = '0'; DESC_BUF(5) = 0;
            DESC_BUF(6) = '1'; DESC_BUF(7) = 0;
            actual_len = (wlen < 8) ? wlen : 8;
        } else {
            DESC_BUF(0) = 0x02; DESC_BUF(1) = 0x03;
            actual_len = 2;
        }
        send_descriptor_data(actual_len);
    } else {
        return;
    }

    uart_puthex(desc_type);
    uart_putc('\n');
}

/*==========================================================================
 * Link Event Handler
 *==========================================================================*/
static void handle_link_event(void) {
    uint8_t r9300 = REG_BUF_CFG_9300;

    if (r9300 & 0x04) {
        /* USB 3.0 failed — fall back to USB 2.0 */
        REG_POWER_STATUS = REG_POWER_STATUS | 0x40;
        REG_POWER_EVENT_92E1 = 0x10;
        REG_USB_STATUS = REG_USB_STATUS | 0x04;
        REG_USB_STATUS = REG_USB_STATUS & ~0x04;
        REG_PHY_LINK_CTRL = 0x00;
        REG_CPU_MODE = 0x00;
        REG_LINK_WIDTH_E710 = 0x1F;
        REG_USB_PHY_CTRL_91C0 = 0x10;
        is_usb3 = 0;
        uart_puts("[T]\n");
    } else if (r9300 & 0x08) {
        /* USB 3.0 link OK */
        REG_BUF_CFG_9300 = 0x08;
        is_usb3 = 1;
        uart_puts("[3]\n");
    }

    REG_BUF_CFG_9300 = 0x04;
}

/*==========================================================================
 * USB Bus Reset Handler (from trace/enumerate_usb_20)
 *==========================================================================*/
static void handle_usb_reset(void) {
    uint8_t r91d1;
    uint16_t timeout;

    r91d1 = REG_USB_PHY_CTRL_91D1;
    REG_USB_PHY_CTRL_91D1 = r91d1;

    REG_TIMER_CTRL_CC3B = REG_TIMER_CTRL_CC3B & ~0x02;
    REG_CLOCK_CTRL_92CF = 0x00;
    REG_CLOCK_CTRL_92CF = 0x04;
    REG_CLOCK_ENABLE = REG_CLOCK_ENABLE | 0x10;
    REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
    REG_TIMER0_DIV = 0x10;
    REG_TIMER0_THRESHOLD_HI = 0x00; REG_TIMER0_THRESHOLD_LO = 0x0A;
    REG_TIMER0_CSR = 0x01;
    REG_TIMER0_CSR = 0x02;
    REG_CLOCK_ENABLE = REG_CLOCK_ENABLE & ~0x10;
    REG_CLOCK_CTRL_92CF = 0x07;
    REG_CLOCK_CTRL_92CF = 0x03;
    REG_PHY_LINK_CTRL = 0x00;
    REG_POWER_EVENT_92E1 = 0x40;
    REG_POWER_STATUS = REG_POWER_STATUS & ~0x40;

    for (timeout = 10000; timeout; timeout--) {
        r91d1 = REG_USB_PHY_CTRL_91D1;
        if (r91d1 & 0x01) break;
    }
    if (r91d1 & 0x01)
        REG_USB_PHY_CTRL_91D1 = r91d1;

    REG_CPU_TIMER_CTRL_CD31 = 0x04; REG_CPU_TIMER_CTRL_CD31 = 0x02;
    REG_TIMER2_CSR = 0x04; REG_TIMER2_CSR = 0x02;
    REG_TIMER4_CSR = 0x04; REG_TIMER4_CSR = 0x02;
    REG_TIMER2_THRESHOLD_LO = 0x00; REG_TIMER2_THRESHOLD_HI = 0x8B;
    REG_TIMER4_THRESHOLD_LO = 0x00; REG_TIMER4_THRESHOLD_HI = 0xC7;

    /* MSC + NVMe doorbell dance */
    REG_USB_MSC_CFG = REG_USB_MSC_CFG | 0x02;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG | 0x04;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | 0x01;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG | 0x01;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | 0x02;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | 0x04;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | 0x08;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | 0x10;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG & ~0x02;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG & ~0x04;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~0x01;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG & ~0x01;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~0x02;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~0x04;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~0x08;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~0x10;

    REG_USB_EP_BUF_CTRL = 0x55;
    REG_USB_EP_BUF_SEL  = 0x53;
    REG_USB_EP_BUF_DATA = 0x42;
    REG_USB_EP_BUF_PTR_LO = 0x53;
    REG_USB_MSC_LENGTH = 0x0D;
    REG_USB_MSC_CTRL = 0x01;
    REG_USB_MSC_STATUS = REG_USB_MSC_STATUS;

    /* NVMe init under doorbell bit 5 */
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | 0x20;
    REG_NVME_INIT_CTRL2 = 0xFF;
    REG_NVME_INIT_CTRL2_1 = 0xFF;
    REG_NVME_INIT_CTRL2_2 = 0xFF;
    REG_NVME_INIT_CTRL2_3 = 0xFF;
    REG_NVME_INIT_CTRL = 0xFF;
    REG_NVME_CMD_CDW11 = 0xFF;
    REG_NVME_INT_MASK_A = 0xFF;
    REG_NVME_INT_MASK_B = 0xFF;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~0x20;

    REG_BUF_CFG_9300 = 0x04;
    REG_POWER_STATUS = REG_POWER_STATUS | 0x40;
    REG_POWER_EVENT_92E1 = 0x10;
    REG_TIMER_CTRL_CC3B = REG_TIMER_CTRL_CC3B | 0x02;

    /* Detect USB speed after reset */
    {
        uint8_t link = REG_USB_LINK_STATUS;
        is_usb3 = (link >= 0x02) ? 1 : 0;
        uart_puts("[R");
        uart_puthex(link);
        uart_puts("]\n");
    }
}

/*==========================================================================
 * Interrupt Handlers
 *==========================================================================*/

void int0_isr(void) __interrupt(0) {
    uint8_t periph_status, phase;

    periph_status = REG_USB_PERIPH_STATUS;

    if (periph_status & 0x10) {
        handle_link_event();
        return;
    }

    if ((periph_status & 0x01) && !(periph_status & 0x02)) {
        handle_usb_reset();
        return;
    }

    if (!(periph_status & 0x02))
        return;

    phase = REG_USB_CTRL_PHASE;

    if (phase == 0x04 || phase == 0x00) {
        REG_USB_CTRL_PHASE = 0x04;
        return;
    }

    if ((phase & 0x02) && !(phase & 0x01)) {
        /* USB 2.0: status phase after data IN */
        complete_usb20_status();
    } else if ((phase & 0x10) && !(phase & 0x01)) {
        /* USB 3.0: stale status phase */
        REG_USB_DMA_TRIGGER = 0x08;
        REG_USB_CTRL_PHASE = 0x10;
    } else if (phase & 0x01) {
        uint8_t bmReq, bReq, wValL, wValH, wLenL;

        REG_USB_CONFIG = REG_USB_CONFIG;
        REG_USB_CTRL_PHASE = 0x01;

        bmReq = REG_USB_SETUP_BMREQ;
        bReq  = REG_USB_SETUP_BREQ;
        wValL = REG_USB_SETUP_WVAL_L;
        wValH = REG_USB_SETUP_WVAL_H;
        wLenL = REG_USB_SETUP_WLEN_L;

        if (bmReq == 0x00 && bReq == USB_REQ_SET_ADDRESS) {
            handle_set_address();
        } else if (bmReq == 0x80 && bReq == USB_REQ_GET_DESCRIPTOR) {
            handle_get_descriptor(wValH, wValL, wLenL);
        } else if (bmReq == 0x00 && bReq == USB_REQ_SET_CONFIGURATION) {
            send_zlp_ack();
            uart_puts("[C]\n");
        } else if (bmReq == 0x01 && bReq == 0x0B) {
            /* SET_INTERFACE */
            send_zlp_ack();
            uart_puts("[I]\n");
        } else if (bmReq == 0x00 && (bReq == 0x30 || bReq == 0x31)) {
            /* SET_SEL / SET_ISOCH_DELAY (USB 3.0) */
            send_zlp_ack();
        } else {
            send_zlp_ack();
        }
    }
}

void timer0_isr(void) __interrupt(1) { }

void int1_isr(void) __interrupt(2) {
    uint8_t tmp;

    REG_CPU_DMA_INT = REG_CPU_DMA_INT;

    tmp = REG_POWER_EVENT_92E1;
    if (tmp) {
        REG_POWER_EVENT_92E1 = tmp;
        REG_POWER_STATUS = REG_POWER_STATUS & 0x3F;
    }
}

void timer1_isr(void) __interrupt(3) { }
void serial_isr(void) __interrupt(4) { }
void timer2_isr(void) __interrupt(5) { }

/*==========================================================================
 * Hardware Init (from trace/enumerate_min — stock firmware)
 *==========================================================================*/
static void hw_init(void) {
    uint8_t i;

    REG_CPU_EXEC_STATUS = 0x01; REG_CPU_MODE = 0x01;
    REG_LINK_WIDTH_E710 = 0x04; REG_CPU_EXEC_STATUS_2 = 0x04;
    REG_TIMER_CTRL_CC3B = 0x0C; REG_LINK_CTRL_E717 = 0x01;
    REG_CPU_CTRL_CC3E = 0x00; REG_TIMER_CTRL_CC3B = 0x0C;
    REG_TIMER_CTRL_CC3B = 0x0C; REG_LINK_STATUS_E716 = 0x03;
    REG_CPU_CTRL_CC3E = 0x00; REG_TIMER_CTRL_CC39 = 0x06;
    REG_TIMER_ENABLE_B = 0x14; REG_TIMER_ENABLE_A = 0x44;
    REG_CPU_CTRL_CC37 = 0x2C; REG_SYS_CTRL_E780 = 0x00;
    REG_LINK_STATUS_E716 = 0x00; REG_LINK_STATUS_E716 = 0x03;
    /* Timer0 init */
    REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
    REG_TIMER0_DIV = 0x12; REG_TIMER0_THRESHOLD_HI = 0x00;
    REG_TIMER0_THRESHOLD_LO = 0xC8; REG_TIMER0_CSR = 0x01;
    REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
    REG_CPU_CTRL_CC37 = 0x28;
    REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
    REG_TIMER0_DIV = 0x12; REG_TIMER0_THRESHOLD_HI = 0x00;
    REG_TIMER0_THRESHOLD_LO = 0x14; REG_TIMER0_CSR = 0x01;
    REG_TIMER0_CSR = 0x02;
    REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
    REG_TIMER0_DIV = 0x13; REG_TIMER0_THRESHOLD_HI = 0x00;
    REG_TIMER0_THRESHOLD_LO = 0x0A; REG_TIMER0_CSR = 0x01;
    REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
    /* PHY init */
    REG_PHY_LINK_CTRL = 0x00;
    REG_PHY_TIMER_CTRL_E764 = 0x14; REG_PHY_TIMER_CTRL_E764 = 0x14;
    REG_PHY_TIMER_CTRL_E764 = 0x14; REG_PHY_TIMER_CTRL_E764 = 0x14;
    REG_SYS_CTRL_E76C = 0x04; REG_SYS_CTRL_E774 = 0x04;
    REG_SYS_CTRL_E77C = 0x04;
    /* Timer0 threshold x4 */
    for (i = 0; i < 4; i++) {
        REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
        REG_TIMER0_DIV = 0x12; REG_TIMER0_THRESHOLD_HI = 0x00;
        REG_TIMER0_THRESHOLD_LO = 0xC7; REG_TIMER0_CSR = 0x01;
        REG_TIMER0_CSR = 0x02;
    }
    /* Flash controller init */
    REG_INT_AUX_STATUS = 0x02; REG_FLASH_DIV = 0x04;
    REG_FLASH_MODE = 0x00; REG_FLASH_BUF_OFFSET_LO = 0x00;
    REG_FLASH_BUF_OFFSET_HI = 0x00; REG_FLASH_CMD = 0x06;
    REG_FLASH_ADDR_LEN = 0x04; REG_FLASH_ADDR_LO = 0x00;
    REG_FLASH_ADDR_MD = 0x00; REG_FLASH_ADDR_HI = 0x00;
    REG_FLASH_DATA_LEN = 0x00; REG_FLASH_DATA_LEN_HI = 0x00;
    REG_FLASH_CSR = 0x01;
    for (i = 0; i < 5; i++) REG_FLASH_MODE = 0x00;
    REG_FLASH_BUF_OFFSET_LO = 0x00; REG_FLASH_BUF_OFFSET_HI = 0x00;
    REG_FLASH_CMD = 0x05; REG_FLASH_ADDR_LEN = 0x04;
    REG_FLASH_ADDR_LO = 0x00; REG_FLASH_ADDR_MD = 0x00;
    REG_FLASH_ADDR_HI = 0x00; REG_FLASH_DATA_LEN = 0x00;
    REG_FLASH_DATA_LEN_HI = 0x01; REG_FLASH_CSR = 0x01;
    for (i = 0; i < 4; i++) REG_FLASH_MODE = 0x00;
    REG_FLASH_MODE = 0x01;
    REG_FLASH_BUF_OFFSET_LO = 0x00; REG_FLASH_BUF_OFFSET_HI = 0x00;
    REG_FLASH_CMD = 0x01; REG_FLASH_ADDR_LEN = 0x04;
    REG_FLASH_DATA_LEN = 0x00; REG_FLASH_DATA_LEN_HI = 0x01;
    REG_FLASH_CSR = 0x01;
    REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
    REG_TIMER0_DIV = 0x15; REG_TIMER0_THRESHOLD_HI = 0x00;
    REG_TIMER0_THRESHOLD_LO = 0x59; REG_TIMER0_CSR = 0x01;
    /* Flash status polling x19 */
    for (i = 0; i < 19; i++) {
        REG_FLASH_MODE = 0x00; REG_FLASH_BUF_OFFSET_LO = 0x00;
        REG_FLASH_BUF_OFFSET_HI = 0x00; REG_FLASH_CMD = 0x05;
        REG_FLASH_ADDR_LEN = 0x04; REG_FLASH_ADDR_LO = 0x00;
        REG_FLASH_ADDR_MD = 0x00; REG_FLASH_ADDR_HI = 0x00;
        REG_FLASH_DATA_LEN = 0x00; REG_FLASH_DATA_LEN_HI = 0x01;
        REG_FLASH_CSR = 0x01;
        REG_FLASH_MODE = 0x00; REG_FLASH_MODE = 0x00;
        REG_FLASH_MODE = 0x00; REG_FLASH_MODE = 0x00;
    }
    REG_CPU_EXEC_STATUS_3 = 0x00;
    REG_INT_ENABLE = 0x10;
    REG_INT_STATUS_C800 = 0x04; REG_INT_STATUS_C800 = 0x05;
    REG_TIMER_CTRL_CC3B = 0x0D; REG_TIMER_CTRL_CC3B = 0x0F;
    /* Power/clock init */
    REG_POWER_CTRL_92C6 = 0x05; REG_POWER_CTRL_92C7 = 0x00;
    REG_USB_CTRL_9201 = 0x0E; REG_USB_CTRL_9201 = 0x0C;
    REG_CLOCK_ENABLE = 0x82; REG_USB_CTRL_920C = 0x61;
    REG_USB_CTRL_920C = 0x60; REG_POWER_ENABLE = 0x87;
    REG_CLOCK_ENABLE = 0x83; REG_PHY_POWER = 0x2F;
    REG_USB_PHY_CONFIG_9241 = 0x10; REG_USB_PHY_CONFIG_9241 = 0xD0;
    REG_PHY_PLL_CTRL = 0x5B; REG_PHY_PLL_CTRL = 0x6B;
    REG_PHY_PLL_CFG = 0x1F; REG_PHY_PLL_CTRL = 0xAB;
    REG_PHY_PLL_CFG = 0x17; REG_CPU_CLK_CFG = 0x88;
    /* Buffer config */
    REG_BUF_DESC_STAT0_HI = 0x00; REG_BUF_DESC_STAT0_LO = 0x00;
    REG_BUF_DESC_STAT1_HI = 0x00; REG_BUF_DESC_STAT1_LO = 0x00;
    REG_BUF_DESC_STAT2_HI = 0x00; REG_BUF_DESC_STAT2_LO = 0x00;
    REG_BUF_DESC_BASE0_HI = 0x01; REG_BUF_DESC_BASE0_LO = 0x60;
    REG_BUF_DESC_SIZE0_HI = 0x00; REG_BUF_DESC_SIZE0_LO = 0xE3;
    REG_BUF_DESC_BASE1_HI = 0x01; REG_BUF_DESC_BASE1_LO = 0x60;
    REG_BUF_DESC_BASE2_HI = 0x01; REG_BUF_DESC_BASE2_LO = 0x60;
    REG_BUF_DESC_CFG0_HI = 0x00; REG_BUF_DESC_CFG0_LO = 0x03;
    REG_BUF_DESC_CFG1_HI = 0x00; REG_BUF_DESC_CFG1_LO = 0xE0;
    REG_BUF_DESC_CFG2_HI = 0x00; REG_BUF_DESC_CFG2_LO = 0xE3;
    /* Flash reads (bank0 x3, bank2 x3, bank0+0x80 x3, bank2+0x80 x3) */
    for (i = 0; i < 12; i++) {
        uint8_t bank = (i / 3) & 1 ? 0x02 : 0x00;
        uint8_t addr = (i / 6) ? 0x80 : 0x00;
        REG_FLASH_MODE = 0x00; REG_FLASH_BUF_OFFSET_LO = 0x00;
        REG_FLASH_BUF_OFFSET_HI = 0x00; REG_FLASH_CMD = 0x03;
        REG_FLASH_ADDR_LEN = 0x07; REG_FLASH_ADDR_LO = addr;
        REG_FLASH_ADDR_MD = 0x00; REG_FLASH_ADDR_HI = bank;
        REG_FLASH_DATA_LEN = 0x00; REG_FLASH_DATA_LEN_HI = 0x80;
        REG_FLASH_CSR = 0x01;
        REG_FLASH_MODE = 0x00; REG_FLASH_MODE = 0x00;
        REG_FLASH_MODE = 0x00; REG_FLASH_MODE = 0x00;
    }
    REG_CPU_EXEC_STATUS_3 = 0x00; REG_USB_EP_CTRL_905F = 0x44;
    REG_CPU_KEEPALIVE = 0x04;
    REG_CPU_KEEPALIVE_CC2C = 0xC7; REG_CPU_KEEPALIVE_CC2D = 0xC7;
    REG_INT_ENABLE = 0x50; REG_CPU_EXEC_STATUS = 0x00;
    REG_INT_DMA_CTRL = 0x04;
    REG_POWER_CTRL_92C8 = 0x24; REG_POWER_CTRL_92C8 = 0x24;
    /* Timer2/4 init */
    REG_TIMER2_CSR = 0x04; REG_TIMER2_CSR = 0x02;
    REG_TIMER4_CSR = 0x04; REG_TIMER4_CSR = 0x02;
    REG_TIMER2_DIV = 0x16; REG_TIMER2_THRESHOLD_LO = 0x00;
    REG_TIMER2_THRESHOLD_HI = 0x8B; REG_TIMER4_DIV = 0x54;
    REG_TIMER4_THRESHOLD_LO = 0x00; REG_TIMER4_THRESHOLD_HI = 0xC7;
    /* DMA init */
    REG_DMA_STATUS2 = 0x00; REG_DMA_STATUS2 = 0x00;
    REG_DMA_STATUS2 = 0x00; REG_DMA_CTRL = 0x00;
    REG_DMA_STATUS = 0x00; REG_DMA_STATUS = 0x00;
    REG_DMA_STATUS = 0x00; REG_DMA_QUEUE_IDX = 0x00;
    /* DMA channel config x8 */
    { static __code const uint8_t dma_cfg[][4] = {
        {0x02, 0xA0, 0x0F, 0xFF}, {0x02, 0xB0, 0x01, 0xFF},
        {0x00, 0xA0, 0x0F, 0xFF}, {0x00, 0xB0, 0x01, 0xFF},
        {0x02, 0xB8, 0x03, 0xFF}, {0x02, 0xBC, 0x00, 0x7F},
        {0x00, 0xB8, 0x03, 0xFF}, {0x00, 0xBC, 0x00, 0x7F},
    };
    for (i = 0; i < 8; i++) {
        if (i < 4) REG_DMA_STATUS2 = dma_cfg[i][0];
        else       REG_DMA_STATUS = dma_cfg[i][0];
        REG_DMA_CHAN_STATUS2 = 0x00;
        REG_DMA_CHAN_CTRL2 = 0x14; REG_DMA_CHAN_CTRL2 = 0x14;
        REG_DMA_CHAN_CTRL2 = 0x14; REG_DMA_CHAN_CTRL2 = 0x94;
        REG_DMA_CHAN_AUX = dma_cfg[i][1];
        REG_DMA_CHAN_AUX1 = 0x00;
        REG_DMA_XFER_CNT_HI = dma_cfg[i][2];
        REG_DMA_XFER_CNT_LO = dma_cfg[i][3];
        REG_DMA_TRIGGER = 0x01;
        REG_DMA_CHAN_CTRL2 = 0x14;
    }}
    /* MSC init */
    REG_USB_MSC_CFG = 0x07; REG_USB_MSC_CFG = 0x07;
    REG_USB_MSC_CFG = 0x07; REG_USB_MSC_CFG = 0x05;
    REG_USB_MSC_CFG = 0x01; REG_USB_MSC_CFG = 0x00;
    REG_USB_MSC_LENGTH = 0x0D;
    /* USB controller init */
    REG_POWER_ENABLE = 0x87; REG_USB_PHY_CTRL_91D1 = 0x0F;
    REG_BUF_CFG_9300 = 0x0C; REG_BUF_CFG_9301 = 0xC0;
    REG_BUF_CFG_9302 = 0xBF; REG_USB_CTRL_PHASE = 0x1F;
    REG_USB_EP_CFG1 = 0x0F; REG_USB_PHY_CTRL_91C1 = 0xF0;
    REG_BUF_CFG_9303 = 0x33; REG_BUF_CFG_9304 = 0x3F;
    REG_BUF_CFG_9305 = 0x40; REG_USB_CONFIG = 0xE0;
    REG_USB_EP0_LEN_H = 0xF0; REG_USB_MODE = 0x01;
    REG_USB_EP_MGMT = 0x00;
    /* Endpoint ready masks */
    REG_USB_EP_READY = 0xFF; REG_USB_EP_CTRL_9097 = 0xFF;
    REG_USB_EP_MODE_9098 = 0xFF; REG_USB_EP_MODE_9099 = 0xFF;
    REG_USB_EP_MODE_909A = 0xFF; REG_USB_EP_MODE_909B = 0xFF;
    REG_USB_EP_MODE_909C = 0xFF; REG_USB_EP_MODE_909D = 0xFF;
    REG_USB_STATUS_909E = 0x03;
    REG_USB_DATA_H = 0xFF; REG_USB_FIFO_STATUS = 0xFF;
    REG_USB_FIFO_H = 0xFF; REG_USB_FIFO_4 = 0xFF;
    REG_USB_FIFO_5 = 0xFF; REG_USB_FIFO_6 = 0xFF;
    REG_USB_FIFO_7 = 0xFF; REG_USB_XCVR_MODE = 0x03;
    REG_USB_DATA_L = 0xFE;
    /* USB PHY init */
    REG_USB_PHY_CTRL_91C3 = 0x00;
    REG_USB_PHY_CTRL_91C0 = 0x13; REG_USB_PHY_CTRL_91C0 = 0x12;
    /* Timer/DMA final init */
    REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
    REG_TIMER0_DIV = 0x14; REG_TIMER0_THRESHOLD_HI = 0x01;
    REG_TIMER0_THRESHOLD_LO = 0x8F; REG_TIMER0_CSR = 0x01;
    REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
    REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
    REG_TIMER0_DIV = 0x10; REG_TIMER0_THRESHOLD_HI = 0x00;
    REG_TIMER0_THRESHOLD_LO = 0x09; REG_TIMER0_CSR = 0x01;
    REG_TIMER0_CSR = 0x02;
    REG_INT_DMA_CTRL = 0x04; REG_INT_DMA_CTRL = 0x84;
    REG_LINK_MODE_CTRL = 0xFF;
    REG_XFER2_DMA_STATUS = 0x04; REG_XFER2_DMA_STATUS = 0x02;
    REG_XFER2_DMA_CTRL = 0x00; REG_INT_ENABLE = 0x50;
    REG_XFER2_DMA_CTRL = 0x04;
    REG_XFER2_DMA_ADDR_LO = 0x00; REG_XFER2_DMA_ADDR_HI = 0xC8;
    REG_INT_CTRL = 0x08; REG_INT_CTRL = 0x0A;
    REG_INT_CTRL = 0x0A;
    REG_CPU_EXT_CTRL = 0x40;
    REG_CPU_EXT_STATUS = 0x04; REG_CPU_EXT_STATUS = 0x02;
    REG_XFER_DMA_CTRL = 0x10; REG_XFER_DMA_ADDR_LO = 0x00;
    REG_XFER_DMA_ADDR_HI = 0x0A; REG_XFER_DMA_CMD = 0x01;
    REG_XFER_DMA_CMD = 0x02;
    REG_XFER_DMA_CTRL = 0x10; REG_XFER_DMA_ADDR_LO = 0x00;
    REG_XFER_DMA_ADDR_HI = 0x3C; REG_XFER_DMA_CMD = 0x01;
    REG_XFER_DMA_CMD = 0x02;
    /* Interrupt controller */
    REG_INT_CTRL = 0x2A; REG_INT_ENABLE = 0x50;
    REG_CPU_CTRL_CC80 = 0x00; REG_CPU_CTRL_CC80 = 0x03;
    REG_XFER_DMA_CFG = 0x04; REG_XFER_DMA_CFG = 0x02;
    REG_INT_ENABLE = 0x50;
    REG_CPU_DMA_READY = 0x00; REG_CPU_DMA_READY = 0x04;
    /* Final flash read */
    REG_FLASH_MODE = 0x00; REG_FLASH_BUF_OFFSET_LO = 0x00;
    REG_FLASH_BUF_OFFSET_HI = 0x00; REG_FLASH_CMD = 0x03;
    REG_FLASH_ADDR_LEN = 0x07; REG_FLASH_ADDR_LO = 0x00;
    REG_FLASH_ADDR_MD = 0x80; REG_FLASH_ADDR_HI = 0x01;
    REG_FLASH_DATA_LEN = 0x00; REG_FLASH_DATA_LEN_HI = 0x04;
    REG_FLASH_CSR = 0x01;
    REG_FLASH_MODE = 0x00; REG_FLASH_MODE = 0x00;
    REG_FLASH_MODE = 0x00; REG_FLASH_MODE = 0x00;
    REG_CPU_CTRL_CC82 = 0x18; REG_CPU_CTRL_CC83 = 0x9C;
    REG_CPU_DMA_INT = 0x04; REG_CPU_DMA_INT = 0x02;
    REG_INT_ENABLE = 0x50;
    REG_CPU_DMA_CTRL_CC90 = 0x00; REG_CPU_DMA_CTRL_CC90 = 0x05;
    REG_CPU_DMA_DATA_LO = 0x00; REG_CPU_DMA_DATA_HI = 0xC8;
    REG_CPU_DMA_INT = 0x01;
}

void main(void)
{
    IE = 0;
    is_usb3 = 0;
    REG_UART_LCR &= 0xF7;
    uart_puts("\n[BOOT]\n");

    hw_init();

    /* Detect initial USB speed (0x02=SS, 0x03=SS+ both mean USB 3.0) */
    {
        uint8_t link = REG_USB_LINK_STATUS;
        is_usb3 = (link >= 0x02) ? 1 : 0;
        uart_puts("[link=");
        uart_puthex(link);
        uart_puts("]\n");
    }

    uart_puts("[GO]\n");

    TCON = 0;
    IE = IE_EA | IE_EX0 | IE_EX1 | IE_ET0;

    while (1) {
        REG_CPU_KEEPALIVE = 0x0C;
    }
}
