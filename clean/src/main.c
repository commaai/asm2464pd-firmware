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
 *
 * USB 2.0 (9100=0x01):
 *   - No-data: 9091 0x01→0x08, send ZLP (9092=0x04), ack 0x08
 *   - Data IN status: 9002 toggle bit 1, 9092=0x02, 9091=0x02
 *
 * USB 3.0 (9100=0x02):
 *   - No-data: 9091 0x11→0x10, complete (9092=0x08), ack 0x10
 *   - Data IN status: 9092=0x08, 9091=0x10
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
    (void)REG_USB_CTRL_PHASE;
    REG_USB_CTRL_PHASE = 0x02;
    c = REG_USB_CONFIG;
    REG_USB_CONFIG = c & ~0x02;
    REG_USB_CTRL_PHASE = 0x04;
    (void)REG_USB_CTRL_PHASE;
    (void)REG_USB_CTRL_PHASE;
}

/* Complete status for no-data OUT requests */
static void send_zlp_ack(void) {
    if (is_usb3) {
        complete_usb3_status();
    } else {
        (void)REG_USB_CONFIG;
        (void)REG_USB_CTRL_PHASE;
        (void)REG_USB_CTRL_PHASE;
        (void)REG_USB_CTRL_PHASE;
        REG_USB_EP0_STATUS = 0x00;
        REG_USB_EP0_LEN_L = 0x00;
        REG_USB_DMA_TRIGGER = USB_DMA_SEND;
        REG_USB_CTRL_PHASE = 0x08;
    }
}

/* Send descriptor data via DMA, ack data phase, complete status */
static void send_descriptor_data(uint8_t len) {
    (void)REG_USB_LINK_STATUS;
    (void)REG_USB_CONFIG;
    (void)REG_USB_CTRL_PHASE;
    (void)REG_USB_CTRL_PHASE;
    (void)REG_USB_CTRL_PHASE;
    REG_USB_EP0_STATUS = 0x00;
    REG_USB_EP0_LEN_L = len;
    REG_USB_DMA_TRIGGER = USB_DMA_SEND;
    while (REG_USB_DMA_TRIGGER) { }
    (void)REG_USB_EP0_STATUS;
    (void)REG_USB_EP0_LEN_L;
    (void)REG_USB_EP0_STATUS;
    (void)REG_USB_EP0_LEN_L;
    REG_USB_CTRL_PHASE = 0x08;
    (void)REG_USB_CTRL_PHASE;
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
    { uint8_t t = REG_USB_INT_MASK_9090; REG_USB_INT_MASK_9090 = 0x01; }
    REG_USB_EP_CTRL_91D0 = 0x02;
    (void)REG_USB_LINK_STATUS;
    (void)REG_POWER_STATUS_92F8;

    if (is_usb3) {
        /* USB 3.0 SET_ADDRESS (from trace/flash_usb3 lines 3242-3279) */
        (void)REG_POWER_STATUS_92F8;
        (void)XDATA_REG8(0xC6DB);
        tmp = XDATA_REG8(0xE716);
        XDATA_REG8(0xE716) = 0x01;

        (void)REG_USB_CONFIG;
        while (!(REG_USB_CTRL_PHASE & 0x10)) { }

        /* Address programming registers */
        tmp = XDATA_REG8(0x9206);
        XDATA_REG8(0x9206) = 0x03;
        tmp = XDATA_REG8(0x9207);
        XDATA_REG8(0x9207) = 0x03;
        tmp = XDATA_REG8(0x9206);
        XDATA_REG8(0x9206) = 0x07;
        tmp = XDATA_REG8(0x9207);
        XDATA_REG8(0x9207) = 0x07;
        (void)REG_POWER_STATUS_92F8;
        tmp = XDATA_REG8(0x9206);
        XDATA_REG8(0x9206) = tmp;
        tmp = XDATA_REG8(0x9207);
        XDATA_REG8(0x9207) = tmp;
        (void)REG_POWER_STATUS_92F8;
        XDATA_REG8(0x9208) = 0x00;
        XDATA_REG8(0x9209) = 0x0A;
        XDATA_REG8(0x920A) = 0x00;
        XDATA_REG8(0x920B) = 0x0A;
        tmp = XDATA_REG8(0x9202);
        XDATA_REG8(0x9202) = tmp;
        (void)REG_USB_EP_CTRL_9220;
        XDATA_REG8(0x9220) = 0x04;

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
    (void)REG_USB_LINK_STATUS;

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
    uint8_t r9300;

    (void)REG_BUF_CFG_9302; (void)REG_BUF_CFG_9302;
    (void)REG_BUF_CFG_9302; (void)REG_BUF_CFG_9302;
    (void)REG_BUF_CFG_9302; (void)REG_BUF_CFG_9302;
    r9300 = REG_BUF_CFG_9300;

    if (r9300 & 0x04) {
        /* USB 3.0 failed — fall back to USB 2.0 */
        { uint8_t t = REG_LINK_STATUS_E716; REG_LINK_STATUS_E716 = t; }
        { uint8_t t = REG_POWER_STATUS; REG_POWER_STATUS = t | 0x40; }
        REG_POWER_EVENT_92E1 = 0x10;
        { uint8_t t = REG_USB_INT_MASK_9090; REG_USB_INT_MASK_9090 = t; }
        { uint8_t t = REG_USB_STATUS; REG_USB_STATUS = t | 0x04; }
        { uint8_t t = REG_USB_STATUS; REG_USB_STATUS = t & ~0x04; }
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

    { uint8_t t = REG_TIMER_CTRL_CC3B; REG_TIMER_CTRL_CC3B = t & ~0x02; }
    (void)REG_POWER_STATUS;
    (void)REG_POWER_STATUS_92F8;
    REG_CLOCK_CTRL_92CF = 0x00;
    (void)REG_CLOCK_CTRL_92CF;
    REG_CLOCK_CTRL_92CF = 0x04;
    { uint8_t t = REG_CLOCK_ENABLE; REG_CLOCK_ENABLE = t | 0x10; }
    REG_TIMER0_CSR = 0x04; REG_TIMER0_CSR = 0x02;
    REG_TIMER0_DIV = 0x10;
    REG_TIMER0_THRESHOLD_HI = 0x00; REG_TIMER0_THRESHOLD_LO = 0x0A;
    REG_TIMER0_CSR = 0x01;
    (void)REG_TIMER0_CSR; REG_TIMER0_CSR = 0x02;
    (void)REG_USB_EP0_COMPLETE; (void)REG_USB_EP0_COMPLETE;
    { uint8_t t = REG_CLOCK_ENABLE; REG_CLOCK_ENABLE = t & ~0x10; }
    REG_CLOCK_CTRL_92CF = 0x07;
    (void)REG_CLOCK_CTRL_92CF;
    REG_CLOCK_CTRL_92CF = 0x03;
    REG_PHY_LINK_CTRL = 0x00;
    (void)REG_POWER_STATUS_92F8;
    REG_POWER_EVENT_92E1 = 0x40;
    { uint8_t t = REG_POWER_STATUS; REG_POWER_STATUS = t & ~0x40; }

    for (timeout = 10000; timeout; timeout--) {
        (void)REG_POWER_POLL_92FB;
        r91d1 = REG_USB_PHY_CTRL_91D1;
        (void)REG_PHY_POLL_E750;
        if (r91d1 & 0x01) break;
    }
    if (r91d1 & 0x01)
        REG_USB_PHY_CTRL_91D1 = r91d1;

    { uint8_t t = REG_PHY_CFG_C6A8; REG_PHY_CFG_C6A8 = t; }
    { uint8_t t = REG_POWER_CTRL_92C8; REG_POWER_CTRL_92C8 = t; }
    { uint8_t t = REG_POWER_CTRL_92C8; REG_POWER_CTRL_92C8 = t; }
    REG_CPU_TIMER_CTRL_CD31 = 0x04; REG_CPU_TIMER_CTRL_CD31 = 0x02;
    REG_TIMER2_CSR = 0x04; REG_TIMER2_CSR = 0x02;
    REG_TIMER4_CSR = 0x04; REG_TIMER4_CSR = 0x02;
    { uint8_t t = REG_TIMER2_DIV; REG_TIMER2_DIV = t; }
    REG_TIMER2_THRESHOLD_LO = 0x00; REG_TIMER2_THRESHOLD_HI = 0x8B;
    { uint8_t t = REG_TIMER4_DIV; REG_TIMER4_DIV = t; }
    REG_TIMER4_THRESHOLD_LO = 0x00; REG_TIMER4_THRESHOLD_HI = 0xC7;

    /* MSC + NVMe doorbell dance */
    { uint8_t t = REG_USB_MSC_CFG; REG_USB_MSC_CFG = t | 0x02; }
    { uint8_t t = REG_USB_MSC_CFG; REG_USB_MSC_CFG = t | 0x04; }
    { uint8_t t = REG_NVME_DOORBELL; REG_NVME_DOORBELL = t | 0x01; }
    { uint8_t t = REG_USB_MSC_CFG; REG_USB_MSC_CFG = t | 0x01; }
    { uint8_t t = REG_NVME_DOORBELL; REG_NVME_DOORBELL = t | 0x02; }
    { uint8_t t = REG_NVME_DOORBELL; REG_NVME_DOORBELL = t | 0x04; }
    { uint8_t t = REG_NVME_DOORBELL; REG_NVME_DOORBELL = t | 0x08; }
    { uint8_t t = REG_NVME_DOORBELL; REG_NVME_DOORBELL = t | 0x10; }
    { uint8_t t = REG_USB_MSC_CFG; REG_USB_MSC_CFG = t & ~0x02; }
    { uint8_t t = REG_USB_MSC_CFG; REG_USB_MSC_CFG = t & ~0x04; }
    { uint8_t t = REG_NVME_DOORBELL; REG_NVME_DOORBELL = t & ~0x01; }
    { uint8_t t = REG_USB_MSC_CFG; REG_USB_MSC_CFG = t & ~0x01; }
    { uint8_t t = REG_NVME_DOORBELL; REG_NVME_DOORBELL = t & ~0x02; }
    { uint8_t t = REG_NVME_DOORBELL; REG_NVME_DOORBELL = t & ~0x04; }
    { uint8_t t = REG_NVME_DOORBELL; REG_NVME_DOORBELL = t & ~0x08; }
    { uint8_t t = REG_NVME_DOORBELL; REG_NVME_DOORBELL = t & ~0x10; }

    REG_USB_EP_BUF_CTRL = 0x55;
    REG_USB_EP_BUF_SEL  = 0x53;
    REG_USB_EP_BUF_DATA = 0x42;
    REG_USB_EP_BUF_PTR_LO = 0x53;
    REG_USB_MSC_LENGTH = 0x0D;
    REG_USB_MSC_CTRL = 0x01;
    { uint8_t t = REG_USB_MSC_STATUS; REG_USB_MSC_STATUS = t; }

    /* NVMe init under doorbell bit 5 */
    { uint8_t t = REG_NVME_DOORBELL; REG_NVME_DOORBELL = t | 0x20; }
    { uint8_t t = REG_NVME_QUEUE_CFG; REG_NVME_QUEUE_CFG = t; }
    { uint8_t t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t; }
    { uint8_t t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t; }
    { uint8_t t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t; }
    { uint8_t t = REG_NVME_LINK_CTRL; REG_NVME_LINK_CTRL = t; }
    REG_NVME_INIT_CTRL2 = 0xFF;
    XDATA_REG8(0xC449) = 0xFF;
    XDATA_REG8(0xC44A) = 0xFF;
    XDATA_REG8(0xC44B) = 0xFF;
    { uint8_t t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t; }
    { uint8_t t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t; }
    { uint8_t t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t; }
    { uint8_t t = REG_NVME_LINK_CTRL; REG_NVME_LINK_CTRL = t; }
    REG_NVME_INIT_CTRL = 0xFF;
    REG_NVME_CMD_CDW11 = 0xFF;
    REG_NVME_INT_MASK_A = 0xFF;
    REG_NVME_INT_MASK_B = 0xFF;
    { uint8_t t = REG_NVME_DOORBELL; REG_NVME_DOORBELL = t & ~0x20; }

    (void)REG_USB_PHY_CTRL_91C0;
    (void)REG_POWER_POLL_92FB;
    REG_BUF_CFG_9300 = 0x04;
    { uint8_t t = REG_POWER_STATUS; REG_POWER_STATUS = t | 0x40; }
    REG_POWER_EVENT_92E1 = 0x10;
    { uint8_t t = REG_TIMER_CTRL_CC3B; REG_TIMER_CTRL_CC3B = t | 0x02; }

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

    (void)REG_INT_USB_STATUS;

    periph_status = REG_USB_PERIPH_STATUS;
    (void)REG_USB_PERIPH_STATUS;
    (void)REG_USB_PERIPH_STATUS;
    (void)REG_USB_PERIPH_STATUS;

    if (periph_status & 0x10) {
        handle_link_event();
        (void)REG_INT_SYSTEM;
        (void)REG_INT_USB_STATUS;
        return;
    }

    if ((periph_status & 0x01) && !(periph_status & 0x02)) {
        handle_usb_reset();
        return;
    }

    if (!(periph_status & 0x02)) {
        (void)REG_INT_SYSTEM;
        (void)REG_INT_USB_STATUS;
        return;
    }

    phase = REG_USB_CTRL_PHASE;
    (void)REG_USB_CTRL_PHASE;

    if (phase == 0x04 || phase == 0x00) {
        REG_USB_CTRL_PHASE = 0x04;
        (void)REG_INT_SYSTEM;
        (void)REG_INT_USB_STATUS;
        return;
    }

    if ((phase & 0x02) && !(phase & 0x01)) {
        /* USB 2.0: status phase after data IN */
        (void)REG_USB_CONFIG;
        (void)REG_USB_CTRL_PHASE;
        complete_usb20_status();
    } else if ((phase & 0x10) && !(phase & 0x01)) {
        /* USB 3.0: stale status phase */
        REG_USB_DMA_TRIGGER = 0x08;
        REG_USB_CTRL_PHASE = 0x10;
    } else if (phase & 0x01) {
        uint8_t bmReq, bReq, wValL, wValH, wLenL;

        { uint8_t t = REG_USB_CONFIG; REG_USB_CONFIG = t; }
        (void)REG_USB_EP_CTRL_9220;
        REG_USB_CTRL_PHASE = 0x01;

        bmReq = REG_USB_SETUP_BMREQ;
        bReq  = REG_USB_SETUP_BREQ;
        wValL = REG_USB_SETUP_WVAL_L;
        wValH = REG_USB_SETUP_WVAL_H;
        (void)REG_USB_SETUP_WIDX_L;
        (void)REG_USB_SETUP_WIDX_H;
        wLenL = REG_USB_SETUP_WLEN_L;
        (void)REG_USB_SETUP_WLEN_H;

        (void)REG_USB_CTRL_PHASE;

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

    (void)REG_INT_SYSTEM;
    (void)REG_INT_USB_STATUS;
}

void timer0_isr(void) __interrupt(1) { }

void int1_isr(void) __interrupt(2) {
    uint8_t tmp;

    (void)REG_INT_SYSTEM;
    { uint8_t t = REG_CPU_DMA_INT; REG_CPU_DMA_INT = t; }
    (void)REG_XFER_DMA_CFG;
    (void)REG_XFER2_DMA_STATUS;
    (void)REG_CPU_EXT_STATUS;
    (void)REG_CPU_EXEC_STATUS_2;
    (void)REG_INT_PCIE_NVME; (void)REG_INT_PCIE_NVME;
    (void)REG_INT_PCIE_NVME; (void)REG_INT_PCIE_NVME;

    tmp = REG_POWER_EVENT_92E1;
    if (tmp) {
        REG_POWER_EVENT_92E1 = tmp;
        { uint8_t t = REG_POWER_STATUS; REG_POWER_STATUS = t & 0x3F; }
    }

    (void)REG_INT_SYSTEM;
}

void timer1_isr(void) __interrupt(3) { }
void serial_isr(void) __interrupt(4) { }
void timer2_isr(void) __interrupt(5) { }

/*==========================================================================
 * Hardware Init (from trace/enumerate_min — stock firmware)
 *==========================================================================*/
static void hw_init(void) {
    uint8_t i;

    XDATA_REG8(0xCC32) = 0x01; XDATA_REG8(0xCC30) = 0x01;
    XDATA_REG8(0xE710) = 0x04; XDATA_REG8(0xCC33) = 0x04;
    XDATA_REG8(0xCC3B) = 0x0C; XDATA_REG8(0xE717) = 0x01;
    XDATA_REG8(0xCC3E) = 0x00; XDATA_REG8(0xCC3B) = 0x0C;
    XDATA_REG8(0xCC3B) = 0x0C; XDATA_REG8(0xE716) = 0x03;
    XDATA_REG8(0xCC3E) = 0x00; XDATA_REG8(0xCC39) = 0x06;
    XDATA_REG8(0xCC3A) = 0x14; XDATA_REG8(0xCC38) = 0x44;
    XDATA_REG8(0xCC37) = 0x2C; XDATA_REG8(0xE780) = 0x00;
    XDATA_REG8(0xE716) = 0x00; XDATA_REG8(0xE716) = 0x03;
    /* Timer0 init */
    XDATA_REG8(0xCC11) = 0x04; XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x12; XDATA_REG8(0xCC12) = 0x00;
    XDATA_REG8(0xCC13) = 0xC8; XDATA_REG8(0xCC11) = 0x01;
    XDATA_REG8(0xCC11) = 0x04; XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC37) = 0x28;
    XDATA_REG8(0xCC11) = 0x04; XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x12; XDATA_REG8(0xCC12) = 0x00;
    XDATA_REG8(0xCC13) = 0x14; XDATA_REG8(0xCC11) = 0x01;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC11) = 0x04; XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x13; XDATA_REG8(0xCC12) = 0x00;
    XDATA_REG8(0xCC13) = 0x0A; XDATA_REG8(0xCC11) = 0x01;
    XDATA_REG8(0xCC11) = 0x04; XDATA_REG8(0xCC11) = 0x02;
    /* PHY init */
    XDATA_REG8(0xE7E3) = 0x00;
    XDATA_REG8(0xE764) = 0x14; XDATA_REG8(0xE764) = 0x14;
    XDATA_REG8(0xE764) = 0x14; XDATA_REG8(0xE764) = 0x14;
    XDATA_REG8(0xE76C) = 0x04; XDATA_REG8(0xE774) = 0x04;
    XDATA_REG8(0xE77C) = 0x04;
    /* Timer0 threshold x4 */
    for (i = 0; i < 4; i++) {
        XDATA_REG8(0xCC11) = 0x04; XDATA_REG8(0xCC11) = 0x02;
        XDATA_REG8(0xCC10) = 0x12; XDATA_REG8(0xCC12) = 0x00;
        XDATA_REG8(0xCC13) = 0xC7; XDATA_REG8(0xCC11) = 0x01;
        XDATA_REG8(0xCC11) = 0x02;
    }
    /* Flash controller init */
    XDATA_REG8(0xC805) = 0x02; XDATA_REG8(0xC8A6) = 0x04;
    XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AE) = 0x00;
    XDATA_REG8(0xC8AF) = 0x00; XDATA_REG8(0xC8AA) = 0x06;
    XDATA_REG8(0xC8AC) = 0x04; XDATA_REG8(0xC8A1) = 0x00;
    XDATA_REG8(0xC8A2) = 0x00; XDATA_REG8(0xC8AB) = 0x00;
    XDATA_REG8(0xC8A3) = 0x00; XDATA_REG8(0xC8A4) = 0x00;
    XDATA_REG8(0xC8A9) = 0x01;
    for (i = 0; i < 5; i++) XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AE) = 0x00; XDATA_REG8(0xC8AF) = 0x00;
    XDATA_REG8(0xC8AA) = 0x05; XDATA_REG8(0xC8AC) = 0x04;
    XDATA_REG8(0xC8A1) = 0x00; XDATA_REG8(0xC8A2) = 0x00;
    XDATA_REG8(0xC8AB) = 0x00; XDATA_REG8(0xC8A3) = 0x00;
    XDATA_REG8(0xC8A4) = 0x01; XDATA_REG8(0xC8A9) = 0x01;
    for (i = 0; i < 4; i++) XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AD) = 0x01;
    XDATA_REG8(0xC8AE) = 0x00; XDATA_REG8(0xC8AF) = 0x00;
    XDATA_REG8(0xC8AA) = 0x01; XDATA_REG8(0xC8AC) = 0x04;
    XDATA_REG8(0xC8A3) = 0x00; XDATA_REG8(0xC8A4) = 0x01;
    XDATA_REG8(0xC8A9) = 0x01;
    XDATA_REG8(0xCC11) = 0x04; XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x15; XDATA_REG8(0xCC12) = 0x00;
    XDATA_REG8(0xCC13) = 0x59; XDATA_REG8(0xCC11) = 0x01;
    /* Flash status polling x19 */
    for (i = 0; i < 19; i++) {
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AE) = 0x00;
        XDATA_REG8(0xC8AF) = 0x00; XDATA_REG8(0xC8AA) = 0x05;
        XDATA_REG8(0xC8AC) = 0x04; XDATA_REG8(0xC8A1) = 0x00;
        XDATA_REG8(0xC8A2) = 0x00; XDATA_REG8(0xC8AB) = 0x00;
        XDATA_REG8(0xC8A3) = 0x00; XDATA_REG8(0xC8A4) = 0x01;
        XDATA_REG8(0xC8A9) = 0x01;
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
    }
    XDATA_REG8(0xCC35) = 0x00;
    XDATA_REG8(0xC801) = 0x10;
    XDATA_REG8(0xC800) = 0x04; XDATA_REG8(0xC800) = 0x05;
    XDATA_REG8(0xCC3B) = 0x0D; XDATA_REG8(0xCC3B) = 0x0F;
    /* Power/clock init */
    XDATA_REG8(0x92C6) = 0x05; XDATA_REG8(0x92C7) = 0x00;
    XDATA_REG8(0x9201) = 0x0E; XDATA_REG8(0x9201) = 0x0C;
    XDATA_REG8(0x92C1) = 0x82; XDATA_REG8(0x920C) = 0x61;
    XDATA_REG8(0x920C) = 0x60; XDATA_REG8(0x92C0) = 0x87;
    XDATA_REG8(0x92C1) = 0x83; XDATA_REG8(0x92C5) = 0x2F;
    XDATA_REG8(0x9241) = 0x10; XDATA_REG8(0x9241) = 0xD0;
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
    /* Flash reads (bank0 x3, bank2 x3, bank0+0x80 x3, bank2+0x80 x3) */
    for (i = 0; i < 12; i++) {
        uint8_t bank = (i / 3) & 1 ? 0x02 : 0x00;
        uint8_t addr = (i / 6) ? 0x80 : 0x00;
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AE) = 0x00;
        XDATA_REG8(0xC8AF) = 0x00; XDATA_REG8(0xC8AA) = 0x03;
        XDATA_REG8(0xC8AC) = 0x07; XDATA_REG8(0xC8A1) = addr;
        XDATA_REG8(0xC8A2) = 0x00; XDATA_REG8(0xC8AB) = bank;
        XDATA_REG8(0xC8A3) = 0x00; XDATA_REG8(0xC8A4) = 0x80;
        XDATA_REG8(0xC8A9) = 0x01;
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
    }
    XDATA_REG8(0xCC35) = 0x00; XDATA_REG8(0x905F) = 0x44;
    XDATA_REG8(0xCC2A) = 0x04;
    XDATA_REG8(0xCC2C) = 0xC7; XDATA_REG8(0xCC2D) = 0xC7;
    XDATA_REG8(0xC801) = 0x50; XDATA_REG8(0xCC32) = 0x00;
    XDATA_REG8(0xC807) = 0x04;
    XDATA_REG8(0x92C8) = 0x24; XDATA_REG8(0x92C8) = 0x24;
    /* Timer2/4 init */
    XDATA_REG8(0xCC1D) = 0x04; XDATA_REG8(0xCC1D) = 0x02;
    XDATA_REG8(0xCC5D) = 0x04; XDATA_REG8(0xCC5D) = 0x02;
    XDATA_REG8(0xCC1C) = 0x16; XDATA_REG8(0xCC1E) = 0x00;
    XDATA_REG8(0xCC1F) = 0x8B; XDATA_REG8(0xCC5C) = 0x54;
    XDATA_REG8(0xCC5E) = 0x00; XDATA_REG8(0xCC5F) = 0xC7;
    /* DMA init */
    XDATA_REG8(0xC8D8) = 0x00; XDATA_REG8(0xC8D8) = 0x00;
    XDATA_REG8(0xC8D8) = 0x00; XDATA_REG8(0xC8D7) = 0x00;
    XDATA_REG8(0xC8D6) = 0x00; XDATA_REG8(0xC8D6) = 0x00;
    XDATA_REG8(0xC8D6) = 0x00; XDATA_REG8(0xC8D5) = 0x00;
    /* DMA channel config x8 */
    { static __code const uint8_t dma_cfg[][4] = {
        {0x02, 0xA0, 0x0F, 0xFF}, {0x02, 0xB0, 0x01, 0xFF},
        {0x00, 0xA0, 0x0F, 0xFF}, {0x00, 0xB0, 0x01, 0xFF},
        {0x02, 0xB8, 0x03, 0xFF}, {0x02, 0xBC, 0x00, 0x7F},
        {0x00, 0xB8, 0x03, 0xFF}, {0x00, 0xBC, 0x00, 0x7F},
    };
    for (i = 0; i < 8; i++) {
        if (i < 4) XDATA_REG8(0xC8D8) = dma_cfg[i][0];
        else       XDATA_REG8(0xC8D6) = dma_cfg[i][0];
        XDATA_REG8(0xC8B7) = 0x00;
        XDATA_REG8(0xC8B6) = 0x14; XDATA_REG8(0xC8B6) = 0x14;
        XDATA_REG8(0xC8B6) = 0x14; XDATA_REG8(0xC8B6) = 0x94;
        XDATA_REG8(0xC8B2) = dma_cfg[i][1];
        XDATA_REG8(0xC8B3) = 0x00;
        XDATA_REG8(0xC8B4) = dma_cfg[i][2];
        XDATA_REG8(0xC8B5) = dma_cfg[i][3];
        XDATA_REG8(0xC8B8) = 0x01;
        XDATA_REG8(0xC8B6) = 0x14;
    }}
    /* MSC init */
    XDATA_REG8(0x900B) = 0x07; XDATA_REG8(0x900B) = 0x07;
    XDATA_REG8(0x900B) = 0x07; XDATA_REG8(0x900B) = 0x05;
    XDATA_REG8(0x900B) = 0x01; XDATA_REG8(0x900B) = 0x00;
    XDATA_REG8(0x901A) = 0x0D;
    /* USB controller init */
    XDATA_REG8(0x92C0) = 0x87; XDATA_REG8(0x91D1) = 0x0F;
    XDATA_REG8(0x9300) = 0x0C; XDATA_REG8(0x9301) = 0xC0;
    XDATA_REG8(0x9302) = 0xBF; XDATA_REG8(0x9091) = 0x1F;
    XDATA_REG8(0x9093) = 0x0F; XDATA_REG8(0x91C1) = 0xF0;
    XDATA_REG8(0x9303) = 0x33; XDATA_REG8(0x9304) = 0x3F;
    XDATA_REG8(0x9305) = 0x40; XDATA_REG8(0x9002) = 0xE0;
    XDATA_REG8(0x9005) = 0xF0; XDATA_REG8(0x90E2) = 0x01;
    XDATA_REG8(0x905E) = 0x00;
    /* Endpoint ready masks */
    XDATA_REG8(0x9096) = 0xFF; XDATA_REG8(0x9097) = 0xFF;
    XDATA_REG8(0x9098) = 0xFF; XDATA_REG8(0x9099) = 0xFF;
    XDATA_REG8(0x909A) = 0xFF; XDATA_REG8(0x909B) = 0xFF;
    XDATA_REG8(0x909C) = 0xFF; XDATA_REG8(0x909D) = 0xFF;
    XDATA_REG8(0x909E) = 0x03;
    XDATA_REG8(0x9011) = 0xFF; XDATA_REG8(0x9012) = 0xFF;
    XDATA_REG8(0x9013) = 0xFF; XDATA_REG8(0x9014) = 0xFF;
    XDATA_REG8(0x9015) = 0xFF; XDATA_REG8(0x9016) = 0xFF;
    XDATA_REG8(0x9017) = 0xFF; XDATA_REG8(0x9018) = 0x03;
    XDATA_REG8(0x9010) = 0xFE;
    /* USB PHY init */
    XDATA_REG8(0x91C3) = 0x00;
    XDATA_REG8(0x91C0) = 0x13; XDATA_REG8(0x91C0) = 0x12;
    /* Timer/DMA final init */
    XDATA_REG8(0xCC11) = 0x04; XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x14; XDATA_REG8(0xCC12) = 0x01;
    XDATA_REG8(0xCC13) = 0x8F; XDATA_REG8(0xCC11) = 0x01;
    XDATA_REG8(0xCC11) = 0x04; XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC11) = 0x04; XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x10; XDATA_REG8(0xCC12) = 0x00;
    XDATA_REG8(0xCC13) = 0x09; XDATA_REG8(0xCC11) = 0x01;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xC807) = 0x04; XDATA_REG8(0xC807) = 0x84;
    XDATA_REG8(0xE7FC) = 0xFF;
    XDATA_REG8(0xCCD9) = 0x04; XDATA_REG8(0xCCD9) = 0x02;
    XDATA_REG8(0xCCD8) = 0x00; XDATA_REG8(0xC801) = 0x50;
    XDATA_REG8(0xCCD8) = 0x04;
    XDATA_REG8(0xCCDA) = 0x00; XDATA_REG8(0xCCDB) = 0xC8;
    XDATA_REG8(0xC809) = 0x08; XDATA_REG8(0xC809) = 0x0A;
    XDATA_REG8(0xC809) = 0x0A;
    XDATA_REG8(0xCCF8) = 0x40;
    XDATA_REG8(0xCCF9) = 0x04; XDATA_REG8(0xCCF9) = 0x02;
    XDATA_REG8(0xCC88) = 0x10; XDATA_REG8(0xCC8A) = 0x00;
    XDATA_REG8(0xCC8B) = 0x0A; XDATA_REG8(0xCC89) = 0x01;
    XDATA_REG8(0xCC89) = 0x02;
    XDATA_REG8(0xCC88) = 0x10; XDATA_REG8(0xCC8A) = 0x00;
    XDATA_REG8(0xCC8B) = 0x3C; XDATA_REG8(0xCC89) = 0x01;
    XDATA_REG8(0xCC89) = 0x02;
    /* Interrupt controller */
    XDATA_REG8(0xC809) = 0x2A; XDATA_REG8(0xC801) = 0x50;
    XDATA_REG8(0xCC80) = 0x00; XDATA_REG8(0xCC80) = 0x03;
    XDATA_REG8(0xCC99) = 0x04; XDATA_REG8(0xCC99) = 0x02;
    XDATA_REG8(0xC801) = 0x50;
    XDATA_REG8(0xCC98) = 0x00; XDATA_REG8(0xCC98) = 0x04;
    /* Final flash read */
    XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AE) = 0x00;
    XDATA_REG8(0xC8AF) = 0x00; XDATA_REG8(0xC8AA) = 0x03;
    XDATA_REG8(0xC8AC) = 0x07; XDATA_REG8(0xC8A1) = 0x00;
    XDATA_REG8(0xC8A2) = 0x80; XDATA_REG8(0xC8AB) = 0x01;
    XDATA_REG8(0xC8A3) = 0x00; XDATA_REG8(0xC8A4) = 0x04;
    XDATA_REG8(0xC8A9) = 0x01;
    XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xCC82) = 0x18; XDATA_REG8(0xCC83) = 0x9C;
    XDATA_REG8(0xCC91) = 0x04; XDATA_REG8(0xCC91) = 0x02;
    XDATA_REG8(0xC801) = 0x50;
    XDATA_REG8(0xCC90) = 0x00; XDATA_REG8(0xCC90) = 0x05;
    XDATA_REG8(0xCC92) = 0x00; XDATA_REG8(0xCC93) = 0xC8;
    XDATA_REG8(0xCC91) = 0x01;
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
        (void)REG_POWER_STATUS_92F7;
    }
}
