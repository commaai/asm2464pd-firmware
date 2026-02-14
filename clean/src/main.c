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

/* Descriptor response buffer at 0x9E00 */
#define DESC_BUF ((__xdata uint8_t *)USB_CTRL_BUF_BASE)

static void desc_copy(__code const uint8_t *src, uint8_t len) {
    uint8_t i;
    for (i = 0; i < len; i++)
        DESC_BUF[i] = src[i];
}

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

/* Device descriptor (18 bytes) — bcdUSB/bMaxPacketSize patched per speed */
static __code const uint8_t dev_desc[] = {
    0x12, 0x01, 0x20, 0x03,     /* bLength, bDescType, bcdUSB=3.20 */
    0x00, 0x00, 0x00, 0x09,     /* class=0, subclass=0, proto=0, maxpkt=9 */
    0xD1, 0xAD, 0x01, 0x00,     /* idVendor=0xADD1, idProduct=0x0001 */
    0x01, 0x00, 0x01, 0x02,     /* bcdDevice=0x0001, iMfr=1, iProd=2 */
    0x03, 0x01,                 /* iSerial=3, bNumConfigurations=1 */
};

/* Config + Interface + 2 Bulk EPs (32 bytes) */
static __code const uint8_t cfg_desc[] = {
    /* Configuration */
    0x09, 0x02, 0x20, 0x00,     /* wTotalLength=32, bNumInterfaces=1 */
    0x01, 0x01, 0x00, 0xC0,     /* bConfigValue=1, iConfig=0, self-powered */
    0x00,                       /* bMaxPower=0 */
    /* Interface */
    0x09, 0x04, 0x00, 0x00,     /* bInterfaceNumber=0, bAltSetting=0 */
    0x02, 0xFF, 0xFF, 0xFF,     /* bNumEP=2, vendor class FF/FF/FF */
    0x00,                       /* iInterface=0 */
    /* Bulk IN EP1 */
    0x07, 0x05, 0x81, 0x02,     /* EP1 IN, bulk */
    0x00, 0x02, 0x00,           /* wMaxPacketSize=512, bInterval=0 */
    /* Bulk OUT EP2 */
    0x07, 0x05, 0x02, 0x02,     /* EP2 OUT, bulk */
    0x00, 0x02, 0x00,           /* wMaxPacketSize=512, bInterval=0 */
};

/* BOS: USB 2.0 Extension + SuperSpeed capability (22 bytes) */
static __code const uint8_t bos_desc[] = {
    0x05, 0x0F, 0x16, 0x00, 0x02,   /* BOS header, wTotalLength=22, 2 caps */
    /* USB 2.0 Extension (7 bytes) */
    0x07, 0x10, 0x02, 0x02, 0x00, 0x00, 0x00,
    /* SuperSpeed Device Capability (10 bytes) */
    0x0A, 0x10, 0x03, 0x00, 0x0E, 0x00, 0x03, 0x00, 0x00, 0x00,
};

/* String descriptor 0: language ID */
static __code const uint8_t str0_desc[] = { 0x04, 0x03, 0x09, 0x04 };
/* String descriptor 1: "tiny" */
static __code const uint8_t str1_desc[] = { 0x0A, 0x03, 't',0, 'i',0, 'n',0, 'y',0 };
/* String descriptor 2: "usb" */
static __code const uint8_t str2_desc[] = { 0x08, 0x03, 'u',0, 's',0, 'b',0 };
/* String descriptor 3: "001" */
static __code const uint8_t str3_desc[] = { 0x08, 0x03, '0',0, '0',0, '1',0 };
/* Empty string */
static __code const uint8_t str_empty[] = { 0x02, 0x03 };

static void handle_get_descriptor(uint8_t desc_type, uint8_t desc_idx, uint8_t wlen) {
    __code const uint8_t *src;
    uint8_t desc_len;

    while (!(REG_USB_CTRL_PHASE & 0x08)) { }

    if (desc_type == 0x01) {
        /* Device descriptor — patch bcdUSB and bMaxPacketSize for speed */
        desc_copy(dev_desc, 18);
        if (!is_usb3) {
            DESC_BUF[2] = 0x10; DESC_BUF[3] = 0x02;  /* bcdUSB = 0x0210 */
            DESC_BUF[7] = 0x40;                        /* bMaxPacketSize0 = 64 */
        }
        desc_len = 18;
    } else if (desc_type == 0x02) {
        src = cfg_desc; desc_len = sizeof(cfg_desc);
        desc_copy(src, desc_len);
    } else if (desc_type == 0x0F) {
        src = bos_desc; desc_len = sizeof(bos_desc);
        desc_copy(src, desc_len);
    } else if (desc_type == 0x03) {
        if (desc_idx == 0)      { src = str0_desc; desc_len = sizeof(str0_desc); }
        else if (desc_idx == 1) { src = str1_desc; desc_len = sizeof(str1_desc); }
        else if (desc_idx == 2) { src = str2_desc; desc_len = sizeof(str2_desc); }
        else if (desc_idx == 3) { src = str3_desc; desc_len = sizeof(str3_desc); }
        else                    { src = str_empty; desc_len = sizeof(str_empty); }
        desc_copy(src, desc_len);
    } else {
        return;
    }

    send_descriptor_data(wlen < desc_len ? wlen : desc_len);
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

    if (periph_status & 0x08) {
        /* Bulk request — acknowledge pending bits in 9301/9302 (stock firmware 0x0f0e-0x0f47) */
        uint8_t r9301 = REG_BUF_CFG_9301;
        if (r9301 & 0x40)
            REG_BUF_CFG_9301 = 0x40;
        else if (r9301 & 0x80) {
            REG_BUF_CFG_9301 = 0x80;
            REG_POWER_DOMAIN = (REG_POWER_DOMAIN & 0xFD) | 0x02;
        } else {
            uint8_t r9302 = REG_BUF_CFG_9302;
            if (r9302 & 0x80)
                REG_BUF_CFG_9302 = 0x80;
        }
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
