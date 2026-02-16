/*
 * ASM2464PD USB 3.0 SuperSpeed Firmware with Bulk Transfers
 *
 * Init sequence from trace/enumerate_min (stock firmware trace).
 * USB 3.0 control transfer handling from trace/flash_usb3.
 * Bulk transfer engine from trace/pcie_enum.
 */

#include "types.h"
#include "registers.h"
#include "globals.h"

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
/* Global: set in ISR, cleared in main loop after heavy init */
static volatile uint8_t need_bulk_init;
/* Global: set in ISR when CBW received, cleared in main loop after processing */
static volatile uint8_t need_cbw_process;
/* Global: CBW tag bytes (saved before EP_BUF is overwritten by data) */
static uint8_t cbw_tag[4];
/* Global: deferred bulk OUT state (0=idle, 1=pending arm, 2=armed/waiting) */
static volatile uint8_t bulk_out_state;
static uint16_t bulk_out_addr;
static uint8_t bulk_out_len;

/* Forward declaration */
static void poll_bulk_events(void);

/* EP buffer accessor at 0xD800+ */
#define EP_BUF(n) XDATA_REG8(0xD800 + (n))

/*==========================================================================
 * USB Control Transfer Helpers (USB 2.0 and USB 3.0)
 *==========================================================================*/

/* Complete USB 3.0 status phase */
static void complete_usb3_status(void) {
    uint16_t t;
    for (t = 0; t < 50000; t++) {
        if (REG_USB_CTRL_PHASE & USB_CTRL_PHASE_STAT_IN) break;
    }
    REG_USB_DMA_TRIGGER = USB_DMA_STATUS_COMPLETE;
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_STAT_IN;
}

/* Complete USB 2.0 status phase (for IN transfers with data) */
static void complete_usb20_status(void) {
    uint8_t c;
    c = REG_USB_CONFIG;
    REG_USB_CONFIG = c | USB_CTRL_PHASE_STAT_OUT;
    REG_USB_DMA_TRIGGER = USB_DMA_RECV;
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_STAT_OUT;
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_STAT_OUT;
    c = REG_USB_CONFIG;
    REG_USB_CONFIG = c & ~USB_CTRL_PHASE_STAT_OUT;
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_DATA_OUT;
}

/* Complete status for no-data OUT requests */
static void send_zlp_ack(void) {
    if (is_usb3) {
        complete_usb3_status();
    } else {
        REG_USB_EP0_STATUS = 0x00;
        REG_USB_EP0_LEN_L = 0x00;
        REG_USB_DMA_TRIGGER = USB_DMA_SEND;
        REG_USB_CTRL_PHASE = USB_CTRL_PHASE_DATA_IN;
    }
}

/* Send descriptor data via DMA, ack data phase, complete status */
static void send_descriptor_data(uint8_t len) {
    REG_USB_EP0_STATUS = 0x00;
    REG_USB_EP0_LEN_L = len;
    REG_USB_DMA_TRIGGER = USB_DMA_SEND;
    while (REG_USB_DMA_TRIGGER) { }
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_DATA_IN;
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
        uint16_t t;
        REG_LINK_STATUS_E716 = 0x01;

        for (t = 0; t < 50000; t++) {
            if (REG_USB_CTRL_PHASE & USB_CTRL_PHASE_STAT_IN) break;
        }

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

        REG_USB_DMA_TRIGGER = USB_DMA_STATUS_COMPLETE;
        REG_USB_CTRL_PHASE = USB_CTRL_PHASE_STAT_IN;
    } else {
        send_zlp_ack();
    }
    uart_puts("[A]\n");
}

/* Device descriptor (18 bytes) -- bcdUSB/bMaxPacketSize patched per speed */
static __code const uint8_t dev_desc[] = {
    0x12, 0x01, 0x20, 0x03,     /* bLength, bDescType, bcdUSB=3.20 */
    0x00, 0x00, 0x00, 0x09,     /* class=0, subclass=0, proto=0, maxpkt=9 */
    0xD1, 0xAD, 0x01, 0x00,     /* idVendor=0xADD1, idProduct=0x0001 */
    0x01, 0x00, 0x01, 0x02,     /* bcdDevice=0x0001, iMfr=1, iProd=2 */
    0x03, 0x01,                 /* iSerial=3, bNumConfigurations=1 */
};

/* Config + Interface + 2 Bulk EPs + 2 SS Companion (44 bytes) */
static __code const uint8_t cfg_desc[] = {
    /* Configuration */
    0x09, 0x02, 0x2C, 0x00,     /* wTotalLength=44, bNumInterfaces=1 */
    0x01, 0x01, 0x00, 0xC0,     /* bConfigValue=1, iConfig=0, self-powered */
    0x00,                       /* bMaxPower=0 */
    /* Interface */
    0x09, 0x04, 0x00, 0x00,     /* bInterfaceNumber=0, bAltSetting=0 */
    0x02, 0xFF, 0xFF, 0xFF,     /* bNumEP=2, vendor class FF/FF/FF */
    0x00,                       /* iInterface=0 */
    /* Bulk IN EP1 */
    0x07, 0x05, 0x81, 0x02,     /* EP1 IN, bulk */
    0x00, 0x04, 0x00,           /* wMaxPacketSize=1024, bInterval=0 */
    /* SS Endpoint Companion for EP1 IN */
    0x06, 0x30, 0x00, 0x00,     /* bMaxBurst=0, bmAttributes=0 */
    0x00, 0x00,                 /* wBytesPerInterval=0 */
    /* Bulk OUT EP2 */
    0x07, 0x05, 0x02, 0x02,     /* EP2 OUT, bulk */
    0x00, 0x04, 0x00,           /* wMaxPacketSize=1024, bInterval=0 */
    /* SS Endpoint Companion for EP2 OUT */
    0x06, 0x30, 0x00, 0x00,     /* bMaxBurst=0, bmAttributes=0 */
    0x00, 0x00,                 /* wBytesPerInterval=0 */
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
    uint16_t t;

    for (t = 0; t < 50000; t++) {
        if (REG_USB_CTRL_PHASE & USB_CTRL_PHASE_DATA_IN) break;
    }
    if (t == 50000) return;  /* Timeout — host cancelled */

    if (desc_type == USB_DESC_TYPE_DEVICE) {
        /* Device descriptor -- patch bcdUSB and bMaxPacketSize for speed */
        desc_copy(dev_desc, 18);
        if (!is_usb3) {
            DESC_BUF[2] = 0x10; DESC_BUF[3] = 0x02;  /* bcdUSB = 0x0210 */
            DESC_BUF[7] = 0x40;                        /* bMaxPacketSize0 = 64 */
        }
        desc_len = 18;
    } else if (desc_type == USB_DESC_TYPE_CONFIG) {
        src = cfg_desc; desc_len = sizeof(cfg_desc);
        desc_copy(src, desc_len);
    } else if (desc_type == USB_DESC_TYPE_BOS) {
        src = bos_desc; desc_len = sizeof(bos_desc);
        desc_copy(src, desc_len);
    } else if (desc_type == USB_DESC_TYPE_STRING) {
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
 * SET_CONFIG Handler (trace lines 5249-5280)
 *==========================================================================*/
static void handle_set_config(void) {
    uint8_t t;

    /* USBS signature at EP buffer */
    REG_USB_EP_BUF_CTRL = 0x55;
    REG_USB_EP_BUF_SEL  = 0x53;
    REG_USB_EP_BUF_DATA = 0x42;
    REG_USB_EP_BUF_PTR_LO = 0x53;
    REG_USB_MSC_LENGTH = 0x0D;

    /* EP reconfig */
    t = REG_USB_EP0_CONFIG; REG_USB_EP0_CONFIG = t;
    t = REG_USB_EP0_CONFIG; REG_USB_EP0_CONFIG = t;
    REG_USB_EP_CFG2 = 0x01;
    REG_USB_EP_CFG2 = 0x08;
    REG_USB_EP_STATUS_90E3 = 0x02;
    t = REG_USB_EP_CTRL_905F; REG_USB_EP_CTRL_905F = t;
    t = REG_USB_EP_CTRL_905D; REG_USB_EP_CTRL_905D = t;
    REG_USB_EP_STATUS_90E3 = 0x01;
    REG_USB_CTRL_90A0 = 0x01;
    t = REG_USB_INT_MASK_9090; REG_USB_INT_MASK_9090 = t | 0x80;
    t = REG_USB_STATUS; REG_USB_STATUS = t;
    t = REG_USB_CTRL_924C; REG_USB_CTRL_924C = t;

    send_zlp_ack();

    /* Defer heavy bulk init to main loop */
    need_bulk_init = 1;
    uart_puts("[C]\n");
}

/*==========================================================================
 * Bulk Endpoint Init -- arms the MSC engine for CBW reception
 * Deferred from ISR via need_bulk_init flag.
 * From trace/pcie_enum lines 5304-10262.
 *==========================================================================*/
static void do_bulk_init(void) {
    uint16_t j;
    uint8_t t;

    /* Phase 1: NVMe link init under doorbell bit 5 */
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | NVME_DOORBELL_LINK_GATE;
    t = REG_NVME_QUEUE_CFG; REG_NVME_QUEUE_CFG = t;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_LINK_CTRL; REG_NVME_LINK_CTRL = t;
    REG_NVME_INIT_CTRL2 = 0xFF;
    REG_NVME_INIT_CTRL2_1 = 0xFF;
    REG_NVME_INIT_CTRL2_2 = 0xFF;
    REG_NVME_INIT_CTRL2_3 = 0xFF;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_LINK_CTRL; REG_NVME_LINK_CTRL = t;
    REG_NVME_INIT_CTRL = 0xFF;
    REG_NVME_CMD_CDW11 = 0xFF;
    REG_NVME_INT_MASK_A = 0xFF;
    REG_NVME_INT_MASK_B = 0xFF;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~NVME_DOORBELL_LINK_GATE;

    /* Phase 2: PRP/Queue/EP clears */
    REG_NVME_CMD_PRP1 = 0xFF; REG_NVME_CMD_PRP2 = 0xFF;
    REG_NVME_CMD_PRP3 = 0xFF; REG_NVME_CMD_PRP4 = 0xFF;
    REG_NVME_QUEUE_CTRL = 0xFF; REG_NVME_SQ_HEAD = 0xFF;
    REG_NVME_SQ_TAIL = 0xFF; REG_NVME_CQ_HEAD = 0xFF;
    REG_USB_EP_READY = 0xFF; REG_USB_EP_CTRL_9097 = 0xFF;
    REG_USB_EP_MODE_9098 = 0xFF; REG_USB_EP_MODE_9099 = 0xFF;
    REG_USB_EP_MODE_909A = 0xFF; REG_USB_EP_MODE_909B = 0xFF;
    REG_USB_EP_MODE_909C = 0xFF; REG_USB_EP_MODE_909D = 0xFF;
    REG_USB_STATUS_909E = 0x03;
    REG_NVME_INIT_CTRL = 0x00; REG_NVME_CMD_CDW11 = 0x00;
    REG_NVME_INT_MASK_A = 0x00; REG_NVME_INT_MASK_B = 0x00;
    REG_NVME_INIT_CTRL2 = 0x00;
    REG_NVME_INIT_CTRL2_1 = 0x00; REG_NVME_INIT_CTRL2_2 = 0x00;
    REG_NVME_INIT_CTRL2_3 = 0x00;
    REG_USB_DATA_H = 0x00; REG_USB_FIFO_STATUS = 0x00;
    REG_USB_FIFO_H = 0x00; REG_USB_FIFO_4 = 0x00;
    REG_USB_FIFO_5 = 0x00; REG_USB_FIFO_6 = 0x00;
    REG_USB_FIFO_7 = 0x00;
    REG_USB_XCVR_MODE = 0x02; REG_USB_DATA_L = 0x00;

    /* Phase 3: NVMe init second pass with C428 |= 0x20 */
    t = REG_NVME_QUEUE_CFG; REG_NVME_QUEUE_CFG = t;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_QUEUE_CFG; REG_NVME_QUEUE_CFG = t | 0x20;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_LINK_CTRL; REG_NVME_LINK_CTRL = t;
    REG_NVME_INIT_CTRL2 = 0xFF;
    REG_NVME_INIT_CTRL2_1 = 0xFF; REG_NVME_INIT_CTRL2_2 = 0xFF;
    REG_NVME_INIT_CTRL2_3 = 0xFF;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_LINK_CTRL; REG_NVME_LINK_CTRL = t;
    REG_NVME_INIT_CTRL = 0xFF; REG_NVME_CMD_CDW11 = 0xFF;
    REG_NVME_INT_MASK_A = 0xFF; REG_NVME_INT_MASK_B = 0xFF;

    /* Phase 4: Doorbell dance + arm NVMe queue */
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | NVME_DOORBELL_BIT1;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | NVME_DOORBELL_BIT2;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | NVME_DOORBELL_BIT3;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~NVME_DOORBELL_BIT1;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~NVME_DOORBELL_BIT2;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~NVME_DOORBELL_BIT3;
    REG_NVME_QUEUE_BUSY = 0x01;
    t = REG_NVME_LINK_CTRL; REG_NVME_LINK_CTRL = 0x00;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | NVME_DOORBELL_BIT4;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~NVME_DOORBELL_BIT4;

    /* Phase 5: MSC toggle + USB status */
    REG_USB_MSC_CFG = REG_USB_MSC_CFG | 0x02;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG | 0x04;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG & ~0x02;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG & ~0x04;
    t = REG_USB_STATUS; REG_USB_STATUS = t;
    t = REG_USB_CTRL_924C; REG_USB_CTRL_924C = t;

    /* Phase 6: EP reconfig + USB activate */
    t = REG_USB_EP_CTRL_905F; REG_USB_EP_CTRL_905F = t;
    t = REG_USB_EP_CTRL_905D; REG_USB_EP_CTRL_905D = t;
    REG_USB_EP_STATUS_90E3 = 0x01;
    REG_USB_CTRL_90A0 = 0x01;
    REG_USB_STATUS = 0x01;
    REG_USB_CTRL_924C = 0x05;

    /* Phase 7: Clear endpoint buffer D800-DE5F */
    for (j = 0; j < 0x0660; j++)
        XDATA_REG8(0xD800 + j) = 0x00;

    /* Phase 8: Post-buffer config */
    REG_USB_EP_BUF_DE30 = 0x03;
    REG_USB_EP_BUF_DE36 = 0x00;

    /* Phase 9: 9200 toggle + MSC bit 0 */
    t = REG_USB_CTRL_9200; REG_USB_CTRL_9200 = t | 0x40;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG | 0x01;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG & ~0x01;
    t = REG_USB_CTRL_9200; REG_USB_CTRL_9200 = t & ~0x40;

    /* Phase 10: Final EP config */
    t = REG_USB_EP0_CONFIG; REG_USB_EP0_CONFIG = t;
    t = REG_USB_EP0_CONFIG; REG_USB_EP0_CONFIG = t;
    REG_USB_EP_CFG2 = 0x01;
    REG_USB_EP_CFG2 = 0x08;
    t = REG_USB_EP_CTRL_905F; REG_USB_EP_CTRL_905F = t | 0x08;
    REG_USB_EP_STATUS_90E3 = 0x02;
    REG_USB_CTRL_90A0 = 0x01;

    /* Phase 11: Arms MSC engine for first CBW reception */
    REG_USB_STATUS = 0x00;
    t = REG_USB_CTRL_924C; REG_USB_CTRL_924C = t;
    /* Ramp up MSC + NVMe doorbells */
    REG_USB_MSC_CFG = REG_USB_MSC_CFG | 0x02;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG | 0x04;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | NVME_DOORBELL_BIT0;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG | 0x01;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | NVME_DOORBELL_BIT1;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | NVME_DOORBELL_BIT2;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | NVME_DOORBELL_BIT3;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | NVME_DOORBELL_BIT4;
    /* Ramp down */
    REG_USB_MSC_CFG = REG_USB_MSC_CFG & ~0x02;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG & ~0x04;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~NVME_DOORBELL_BIT0;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG & ~0x01;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~NVME_DOORBELL_BIT1;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~NVME_DOORBELL_BIT2;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~NVME_DOORBELL_BIT3;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~NVME_DOORBELL_BIT4;

    /* USBS signature + MSC trigger -- arms receiver for CBW */
    EP_BUF(0x00) = 0x55; EP_BUF(0x01) = 0x53;
    EP_BUF(0x02) = 0x42; EP_BUF(0x03) = 0x53;
    REG_USB_MSC_LENGTH = 0x0D;
    REG_USB_MSC_CTRL = 0x01;
    t = REG_USB_MSC_STATUS; REG_USB_MSC_STATUS = t & ~0x01;

    /* NVMe link init under doorbell bit 5 (second pass) */
    REG_NVME_DOORBELL = REG_NVME_DOORBELL | NVME_DOORBELL_LINK_GATE;
    t = REG_NVME_QUEUE_CFG; REG_NVME_QUEUE_CFG = t;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_LINK_CTRL; REG_NVME_LINK_CTRL = t;
    REG_NVME_INIT_CTRL2 = 0xFF;
    REG_NVME_INIT_CTRL2_1 = 0xFF;
    REG_NVME_INIT_CTRL2_2 = 0xFF;
    REG_NVME_INIT_CTRL2_3 = 0xFF;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_LINK_PARAM; REG_NVME_LINK_PARAM = t;
    t = REG_NVME_LINK_CTRL; REG_NVME_LINK_CTRL = t;
    REG_NVME_INIT_CTRL = 0xFF;
    REG_NVME_CMD_CDW11 = 0xFF;
    REG_NVME_INT_MASK_A = 0xFF;
    REG_NVME_INT_MASK_B = 0xFF;
    REG_NVME_DOORBELL = REG_NVME_DOORBELL & ~NVME_DOORBELL_LINK_GATE;

    /* 9200 toggle + MSC bit 0 (second pass) */
    t = REG_USB_CTRL_9200; REG_USB_CTRL_9200 = t | 0x40;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG | 0x01;
    REG_USB_MSC_CFG = REG_USB_MSC_CFG & ~0x01;
    t = REG_USB_CTRL_9200; REG_USB_CTRL_9200 = t & ~0x40;

    /* EP reconfig (second pass) */
    t = REG_USB_EP0_CONFIG; REG_USB_EP0_CONFIG = t;
    t = REG_USB_EP0_CONFIG; REG_USB_EP0_CONFIG = t;
    REG_USB_EP_CFG2 = 0x01;
    REG_USB_EP_CFG2 = 0x08;
    t = REG_USB_EP_CTRL_905F; REG_USB_EP_CTRL_905F = t;
    REG_USB_EP_STATUS_90E3 = 0x02;

    uart_puts("[rdy]\n");
}

/*==========================================================================
 * Bulk Transfer Engine
 *
 * CSW send: 90A1=0x01 triggers DMA, C42C=0x01 re-arms MSC for next CBW.
 * SW DMA bulk IN: C8D4=0xA0, 905B:905C=source, 90E1=0x01, 90A1=0x01.
 * Bulk OUT data: hardware auto-DMA's to flash buffer at 0x7000.
 *==========================================================================*/

/* send_csw - Send CSW (Command Status Wrapper) and re-arm MSC engine */
static void send_csw(uint8_t status) {
    EP_BUF(0x0C) = status;
    (void)REG_USB_STATUS;
    EP_BUF(0x08) = 0x00;
    EP_BUF(0x09) = 0x00;
    EP_BUF(0x0A) = 0x00;
    EP_BUF(0x0B) = 0x00;
    REG_USB_BULK_DMA_TRIGGER = 0x01;
    REG_USB_MSC_CTRL = 0x01;
    {
        uint8_t st = REG_USB_MSC_STATUS;
        REG_USB_MSC_STATUS = st & ~0x01;
    }
}

/*
 * sw_dma_bulk_in - Send data from XDATA to host via software DMA
 *
 * Uses C8D4=0xA0 (SW DMA mode), 905B:905C for source address,
 * 90E1 for DMA setup, 90A1 for send trigger.
 * Waits for EP_COMPLETE (9101 bit 5) before returning.
 */
static void sw_dma_bulk_in(uint16_t addr, uint8_t len) {
    uint8_t ah = (addr >> 8) & 0xFF;
    uint8_t al = addr & 0xFF;

    /* Clear stale EP_COMPLETE */
    {
        uint8_t st = REG_USB_PERIPH_STATUS;
        if (st & 0x20) {
            REG_USB_EP_STATUS_90E3 = 0x02;
            REG_USB_EP_READY = 0x01;
        }
    }

    REG_USB_MSC_LENGTH = len;
    REG_DMA_CONFIG = DMA_CONFIG_SW_MODE;       /* C8D4 = 0xA0 */

    REG_USB_EP_BUF_HI = ah;                    /* 905B */
    REG_USB_EP_BUF_LO = al;                    /* 905C */
    EP_BUF(0x02) = ah;                         /* D802 */
    EP_BUF(0x03) = al;                         /* D803 */
    EP_BUF(0x04) = 0x00;
    EP_BUF(0x05) = 0x00;
    EP_BUF(0x06) = 0x00;
    EP_BUF(0x07) = 0x00;
    EP_BUF(0x0F) = 0x00;
    EP_BUF(0x00) = 0x03;                       /* Direction = IN */

    REG_XFER_CTRL_C509 |= 0x01;

    REG_USB_EP_CFG_905A = USB_EP_CFG_BULK_IN;  /* 905A = 0x10 */
    REG_USB_SW_DMA_TRIGGER = 0x01;             /* 90E1 = 0x01 */

    REG_XFER_CTRL_C509 &= ~0x01;

    G_XFER_STATE_0AF4 = 0x40;
    REG_USB_BULK_DMA_TRIGGER = 0x01;           /* 90A1 = actual send */

    /* Wait for EP_COMPLETE (9101 bit 5) */
    {
        uint16_t t16;
        for (t16 = 0; t16 < 60000; t16++) {
            uint8_t st = REG_USB_PERIPH_STATUS;
            if (st & 0x20) {
                (void)REG_USB_STATUS;
                (void)REG_USB_EP_READY;
                REG_USB_EP_STATUS_90E3 = 0x02;
                REG_USB_EP_READY = 0x01;
                break;
            }
        }
    }

    REG_DMA_CONFIG = DMA_CONFIG_DISABLE;       /* C8D4 = 0x00 */
    REG_USB_MSC_LENGTH = 0x0D;                 /* Restore for CSW */
}

/*==========================================================================
 * CBW Handler -- dispatches vendor commands from bulk OUT
 *==========================================================================*/
static void handle_cbw(void) {
    uint8_t opcode, t;
    uint16_t timeout;

    /* Check 90E2 bit 0 before proceeding (stock: 0x0FEE) */
    t = REG_USB_MODE;
    if (!(t & 0x01))
        return;

    (void)REG_USB_STATUS;
    REG_USB_MODE = 0x01;

    /* CE88/CE89 DMA handshake (stock: 0x3484-0x3498) */
    REG_BULK_DMA_HANDSHAKE = 0x00;
    for (timeout = 50000; timeout; timeout--) {
        if (REG_USB_DMA_STATE & USB_DMA_STATE_READY) break;
    }

    /* Copy CBW tag to CSW buffer and save to globals */
    cbw_tag[0] = REG_CBW_TAG_0;
    cbw_tag[1] = REG_CBW_TAG_1;
    cbw_tag[2] = REG_CBW_TAG_2;
    cbw_tag[3] = REG_CBW_TAG_3;
    EP_BUF(0x04) = cbw_tag[0];
    EP_BUF(0x05) = cbw_tag[1];
    EP_BUF(0x06) = cbw_tag[2];
    EP_BUF(0x07) = cbw_tag[3];
    EP_BUF(0x0C) = 0x00;

    opcode = REG_USB_CBWCB_0;

    /* Clear C428 direction bits (stock: 0x4D09) */
    t = REG_NVME_QUEUE_CFG;
    REG_NVME_QUEUE_CFG = t & ~0x03;

    (void)REG_USB_STATUS;
    uart_puts("[CBW:");
    uart_puthex(opcode);
    uart_puts("]\n");

    if (opcode == 0xE5) {
        /* VENDOR WRITE REGISTER: CDB[1]=value, CDB[3:4]=address */
        uint8_t val  = REG_USB_CBWCB_1;
        uint8_t ah   = REG_USB_CBWCB_3;
        uint8_t al   = REG_USB_CBWCB_4;
        uint16_t addr = ((uint16_t)ah << 8) | al;
        XDATA_REG8(addr) = val;
        send_csw(0x00);
    } else if (opcode == 0xE4) {
        /* VENDOR READ REGISTER: CDB[1]=size, CDB[3:4]=address.
         * Returns data in CSW residue field (D808-D80B). */
        uint8_t sz   = REG_USB_CBWCB_1;
        uint8_t ah   = REG_USB_CBWCB_3;
        uint8_t al   = REG_USB_CBWCB_4;
        uint16_t addr = ((uint16_t)ah << 8) | al;
        if (sz > 4) sz = 4;
        EP_BUF(0x08) = (sz >= 1) ? XDATA_REG8(addr) : 0x00;
        EP_BUF(0x09) = (sz >= 2) ? XDATA_REG8(addr + 1) : 0x00;
        EP_BUF(0x0A) = (sz >= 3) ? XDATA_REG8(addr + 2) : 0x00;
        EP_BUF(0x0B) = (sz >= 4) ? XDATA_REG8(addr + 3) : 0x00;
        EP_BUF(0x0C) = 0x00;
        (void)REG_USB_STATUS;
        /* Send CSW with residue intact (don't clear D808-D80B) */
        REG_USB_BULK_DMA_TRIGGER = 0x01;
        REG_USB_MSC_CTRL = 0x01;
        {
            uint8_t st = REG_USB_MSC_STATUS;
            REG_USB_MSC_STATUS = st & ~0x01;
        }
    } else if (opcode == 0xE6) {
        /* VENDOR BULK IN: CDB[1]=length, CDB[3:4]=source address.
         * Sends data from XDATA to host via SW DMA. */
        uint8_t len  = REG_USB_CBWCB_1;
        uint8_t ah   = REG_USB_CBWCB_3;
        uint8_t al   = REG_USB_CBWCB_4;
        uint16_t addr = ((uint16_t)ah << 8) | al;
        if (len == 0) len = 64;

        /* Pre-fill EP buffer with source data */
        {
            uint8_t i;
            for (i = 0; i < len; i++)
                EP_BUF(i) = XDATA_REG8(addr + i);
        }

        sw_dma_bulk_in(addr, len);

        /* Restore USBS signature and tag for CSW */
        EP_BUF(0x00) = 0x55; EP_BUF(0x01) = 0x53;
        EP_BUF(0x02) = 0x42; EP_BUF(0x03) = 0x53;
        EP_BUF(0x04) = cbw_tag[0]; EP_BUF(0x05) = cbw_tag[1];
        EP_BUF(0x06) = cbw_tag[2]; EP_BUF(0x07) = cbw_tag[3];
        send_csw(0x00);
    } else if (opcode == 0xE7) {
        /* VENDOR BULK OUT: CDB[1]=length, CDB[3:4]=dest address.
         * Receives data from host. Deferred to main loop state machine:
         * state 1 arms endpoint, state 2 waits for BULK_DATA, copies
         * from flash buffer (0x7000) to target, then sends CSW. */
        uint8_t len  = REG_USB_CBWCB_1;
        uint8_t ah   = REG_USB_CBWCB_3;
        uint8_t al   = REG_USB_CBWCB_4;
        bulk_out_addr = ((uint16_t)ah << 8) | al;
        bulk_out_len = (len == 0) ? 64 : len;
        bulk_out_state = 1;
        return;  /* Return to main loop for deferred processing */
    } else if (opcode == 0xE8) {
        /* VENDOR NO-DATA */
        send_csw(0x00);
    } else {
        /* Unknown -- fail */
        send_csw(0x01);
    }
}

/*==========================================================================
 * Link Event Handler
 *==========================================================================*/
static void handle_link_event(void) {
    uint8_t r9300 = REG_BUF_CFG_9300;

    if (r9300 & BUF_CFG_9300_SS_FAIL) {
        /* USB 3.0 link lost -- mark as not USB3.
         * Run the same reset sequence as 91D1 bit 0 handler to
         * allow the link to retrain. */
        is_usb3 = 0;

        /* C6A8 |= 0x01: set link ready for retrain (from bda4) */
        {
            uint8_t t = REG_PHY_CFG_C6A8;
            REG_PHY_CFG_C6A8 = (t & 0xFE) | 0x01;
        }

        /* 92C8 &= ~0x03: clear clock bits for re-init */
        {
            uint8_t t = REG_POWER_CTRL_92C8;
            REG_POWER_CTRL_92C8 = t & ~0x01;
            t = REG_POWER_CTRL_92C8;
            REG_POWER_CTRL_92C8 = t & ~0x02;
        }

        /* CD31 reset sequence */
        REG_CPU_TIMER_CTRL_CD31 = 0x04;
        REG_CPU_TIMER_CTRL_CD31 = 0x02;

        /* E710 link mode config */
        {
            uint8_t t = REG_LINK_WIDTH_E710;
            REG_LINK_WIDTH_E710 = (t & 0xE0) | 0x04;
        }

        /* CC3B: clear link status bit */
        {
            uint8_t t = REG_TIMER_CTRL_CC3B;
            REG_TIMER_CTRL_CC3B = t & ~0x02;
        }

        /* Reset software state for re-enumeration */
        bulk_out_state = 0;
        need_cbw_process = 0;
        need_bulk_init = 0;

        uart_puts("[T]\n");
    } else if (r9300 & BUF_CFG_9300_SS_OK) {
        /* USB 3.0 link OK */
        is_usb3 = 1;
        uart_puts("[3]\n");
    }

    /* Clear all pending link event bits including bit 3 (write-1-to-clear)
     * Stock firmware writes 0x0C to 9300 during init (hw_init), which clears
     * bits 2 and 3.  The ISR at 0x10b2 also checks bit 3 after 91D1 dispatch. */
    REG_BUF_CFG_9300 = BUF_CFG_9300_SS_OK | BUF_CFG_9300_SS_FAIL | BUF_CFG_9300_SS_EVENT;
}

/*==========================================================================
 * 91D1 Link Training Handlers
 *
 * Stock firmware ISR at 0x0f4a reads 91D1 and dispatches on individual bits.
 * Each handler is called via bank-switch trampoline at 0x0300 (DPX=0).
 * These handlers configure PHY/power/clock registers to maintain the SS link.
 * Without them, the SS link degrades and dies after 30-75 seconds.
 *==========================================================================*/

/*
 * handle_91d1_bit3 - Power management handler (U1/U2 state transitions)
 * Address: 0x9b95-0x9cdb
 *
 * Called when 91D1 bit 3 fires (SS power state change event).
 * Clears CC3B bit 1, configures 92CF clock recovery, toggles 92C1 bit 4,
 * polls E712 for completion, then restores.
 *
 * Simplified: we do the critical MMIO writes but skip the deep state
 * machine (0x0ACC checks, 0xE712 polling, 0xCA51 calls) since our
 * firmware doesn't use U1/U2 power states.
 */
static void handle_91d1_bit3(void) {
    uint8_t t;

    /* Clear link event counter (stock: 0x9b95-0x9b99) */
    G_USB_TRANSFER_FLAG = 0;

    /* CC3B &= ~0x02: clear SS link status bit (stock: 0x9b9a-0x9ba0) */
    t = REG_TIMER_CTRL_CC3B;
    REG_TIMER_CTRL_CC3B = t & ~0x02;

    /* Set link event processed flag (stock: 0x9c14-0x9c19) */
    G_TLP_BASE_LO = 0x01;
}

/*
 * handle_91d1_bit0 - Link training/recovery handler
 * Address: 0xc465-0xc4ce
 *
 * Called when 91D1 bit 0 fires (link training event).
 * Calls the full state reset (bda4), checks 91C0 bit 1 for link status.
 * If link is down, polls 92FB and may force link retrain via 9300.
 *
 * We implement the critical state reset from 0xbda4:
 * - Clear XDATA state variables
 * - C6A8 |= 0x01 (link ready)
 * - 92C8 &= ~0x03 (clear clock bits)
 * - CD31 = 0x04, then 0x02 (reset sequence)
 */
static void handle_91d1_bit0(void) {
    uint8_t t;

    /* bda4 state reset: clear critical XDATA state variables */
    G_SYS_FLAGS_07ED = 0;
    G_SYS_FLAGS_07EE = 0;
    G_EP_DISPATCH_OFFSET = 0;  /* 0x0AF5 */
    G_SYS_FLAGS_07EB = 0;
    G_STATE_FLAG_0AF1 = 0;
    G_LINK_POWER_STATE_0ACA = 0;
    G_USB_CTRL_STATE_07E1 = 0x05;  /* Ready state */
    G_USB_TRANSFER_FLAG = 0;  /* 0x0B2E */
    G_TLP_MASK_0ACB = 0;
    G_CMD_WORK_E3 = 0;  /* 0x07E3 */
    G_TRANSFER_ACTIVE = 0;  /* 0x07E5 */
    G_SYS_FLAGS_07E8 = 0;
    G_TLP_STATE_07E9 = 0;
    G_LINK_EVENT_0B2D = 0;
    G_XFER_FLAG_07EA = 0;
    G_TRANSFER_BUSY_0B3B = 0;

    /* C6A8 |= 0x01: set link ready (stock: bda4 calls cc56) */
    t = REG_PHY_CFG_C6A8;
    REG_PHY_CFG_C6A8 = (t & 0xFE) | 0x01;

    /* 92C8 &= ~0x01, &= ~0x02: clear clock enable bits (stock: 0xbe00-0xbe0a) */
    t = REG_POWER_CTRL_92C8;
    REG_POWER_CTRL_92C8 = t & ~0x01;
    t = REG_POWER_CTRL_92C8;
    REG_POWER_CTRL_92C8 = t & ~0x02;

    /* CD31 = 0x04, then 0x02: hardware reset sequence (stock: 0xbe0b-0xbe13) */
    REG_CPU_TIMER_CTRL_CD31 = 0x04;
    REG_CPU_TIMER_CTRL_CD31 = 0x02;

    /* Check 91C0 bit 1: if link is up, we're done (stock: 0xc468-0xc46c) */
    t = REG_USB_PHY_CTRL_91C0;
    if (t & 0x02) {
        return;  /* Link is up, recovery complete */
    }

    /* Link is down: configure E710 and clear CC3B bit 1 (stock: 0xc4bb-0xc4ca) */
    t = REG_LINK_WIDTH_E710;
    REG_LINK_WIDTH_E710 = (t & 0xE0) | 0x04;

    t = REG_TIMER_CTRL_CC3B;
    REG_TIMER_CTRL_CC3B = t & ~0x02;
}

/*
 * handle_91d1_bit1 - Simple flag handler
 * Address: 0xe6aa-0xe6ac -> ljmp 0xc324
 *
 * Sets R7=0, jumps to common handler at 0xc324 which stores the
 * event type and sets the link event flag (0x0B2E = 1).
 */
static void handle_91d1_bit1(void) {
    G_EP_DISPATCH_VAL3 = 0;  /* 0x0A7D = 0 (event type) */
    G_USB_TRANSFER_FLAG = 1;  /* 0x0B2E = 1 (link event flag) */
}

/*
 * handle_91d1_bit2 - Link reset acknowledgment
 * Address: 0xe682-0xe688
 *
 * Calls cc56 (C6A8 |= 0x01) and cc79 (0x0B2E=0, 0x07E8=0).
 */
static void handle_91d1_bit2(void) {
    uint8_t t;
    /* cc56: C6A8 = (old & 0xFE) | 0x01 */
    t = REG_PHY_CFG_C6A8;
    REG_PHY_CFG_C6A8 = (t & 0xFE) | 0x01;
    /* cc79: clear state */
    G_USB_TRANSFER_FLAG = 0;  /* 0x0B2E */
    G_SYS_FLAGS_07E8 = 0;
}

/*
 * handle_91d1_events - Dispatch 91D1 link training events
 *
 * Stock firmware at 0x0f4a-0x0f8e: checks 9101 bit 0 first,
 * then reads 91D1 and dispatches on individual bits.
 * Each bit is acknowledged by writing its mask back to 91D1 (write-1-to-clear).
 * Handler is called AFTER ack for bits 3,0,1 and BEFORE ack for bit 2.
 *
 * Priority order: bit 3 > bit 0 > bit 1 > bit 2
 */
static void handle_91d1_events(void) {
    uint8_t r91d1;

    /* Check 9101 bit 0: SS link interrupt pending (stock: 0x0f4a-0x0f4e) */
    if (!(REG_USB_PERIPH_STATUS & 0x01))
        return;

    r91d1 = REG_USB_PHY_CTRL_91D1;

    /* 91D1 bit 3: power management (stock: 0x0f55-0x0f5b) */
    if (r91d1 & 0x08) {
        REG_USB_PHY_CTRL_91D1 = 0x08;  /* ack bit 3 */
        handle_91d1_bit3();
    }

    /* Re-read 91D1 for remaining bits (stock re-reads at 0x0f5e) */
    r91d1 = REG_USB_PHY_CTRL_91D1;

    /* 91D1 bit 0: link training (stock: 0x0f62-0x0f68) */
    if (r91d1 & 0x01) {
        REG_USB_PHY_CTRL_91D1 = 0x01;  /* ack bit 0 */
        handle_91d1_bit0();
        return;  /* stock jumps to 0x10b8 after bit 0 handler */
    }

    /* 91D1 bit 1: simple flag (stock: 0x0f72-0x0f78) */
    if (r91d1 & 0x02) {
        REG_USB_PHY_CTRL_91D1 = 0x02;  /* ack bit 1 */
        handle_91d1_bit1();
        return;
    }

    /* 91D1 bit 2: link reset ack (stock: 0x0f82-0x0f8b) */
    if (r91d1 & 0x04) {
        handle_91d1_bit2();  /* handler BEFORE ack for bit 2 */
        REG_USB_PHY_CTRL_91D1 = 0x04;  /* ack bit 2 */
    }
}

/*==========================================================================
 * USB Bus Reset Handler
 *
 * Stock firmware ISR at 0x0e33 handles bus reset via two paths:
 *
 * Path 1 (0x0ef4): When 9101 bit 5 set + 9000 bit 0 clear
 *   - Checks 9096 bit 0 (EP_READY reset flag)
 *   - Calls 0x5333 which (for normal state) does:
 *     - Write 0x01 to XDATA[0x0AF1] (state flag)
 *     - Set bit 0 of 0x9006 (EP0 enable)
 *     - Set bit 7 of 0x9006 (EP0 ready)
 *   - Acks by writing 0x01 to 0x9096
 *
 * Path 2 (0x0f4a): When 9101 bit 0 set (link event)
 *   - Reads 91D1 and dispatches on individual bits
 *   - Each bit handler calls a bank-switched function
 *   - Acks each bit by writing it back to 91D1
 *
 * Our firmware handles both paths here. The previous implementation
 * was traced from a USB 2.0 enumeration and incorrectly forced
 * USB 2.0 fallback (REG_BUF_CFG_9300 = SS_FAIL), broke the PHY,
 * and reset timers/MSC/NVMe — none of which the stock firmware does
 * on a normal bus reset.
 *==========================================================================*/
static void handle_usb_reset(void) {
    /* EP0 reset sequence (stock firmware 0x5333 → 0x3169 + 0x320d):
     * 0x3169: write 0x01 to 0x0AF1, then 0x9006 = (0x9006 & 0xFE) | 0x01
     * 0x320d: 0x9006 = (0x9006 & 0x7F) | 0x80 */
    G_STATE_FLAG_0AF1 = 0x01;
    REG_USB_EP0_CONFIG = (REG_USB_EP0_CONFIG & 0xFE) | 0x01;
    REG_USB_EP0_CONFIG = (REG_USB_EP0_CONFIG & 0x7F) | 0x80;

    /* Ack the EP ready reset flag (stock firmware 0x0f01-0x100f) */
    REG_USB_EP_READY = 0x01;

    /* Reset software state so re-enumeration works */
    bulk_out_state = 0;
    need_cbw_process = 0;
    need_bulk_init = 0;

    uart_puts("[R]\n");
}

/*==========================================================================
 * Interrupt Handlers
 *==========================================================================*/

/*
 * poll_bulk_events - Check 9101 for bulk events (EP_COMPLETE, CBW_RECEIVED)
 * Called from main loop to avoid edge-triggered interrupt issues.
 */
static void poll_bulk_events(void) {
    uint8_t st = REG_USB_PERIPH_STATUS;

    /* EP complete (bit 5): ack only, do NOT re-arm with C42C here */
    if (st & USB_PERIPH_EP_COMPLETE) {
        (void)REG_USB_STATUS;
        (void)REG_USB_EP_READY;
        REG_USB_EP_STATUS_90E3 = 0x02;
        REG_USB_EP_READY = 0x01;
    }

    /* CBW received (bit 6) */
    if (st & USB_PERIPH_CBW_RECEIVED) {
        need_cbw_process = 1;
    }
}

void int0_isr(void) __interrupt(0) {
    uint8_t periph_status, phase;

    periph_status = REG_USB_PERIPH_STATUS;

    /* Link event (bit 4): USB 3.0 link status change */
    if (periph_status & USB_PERIPH_LINK_EVENT) {
        handle_link_event();
    }

    /* 91D1 link training dispatch (stock: 0x0f4a-0x0f8e)
     * Triggered when 9101 bit 0 is set (SS link interrupt).
     * Must be called to handle U1/U2 power transitions and
     * link recovery events that keep the SS link alive. */
    handle_91d1_events();

    /* Bus reset (bit 0) without control (bit 1) */
    if ((periph_status & USB_PERIPH_BUS_RESET) && !(periph_status & USB_PERIPH_CONTROL)) {
        handle_usb_reset();
    }

    /* Bulk request (bit 3): ack 9301/9302 bits */
    if (periph_status & USB_PERIPH_BULK_REQ) {
        uint8_t r9301 = REG_BUF_CFG_9301;
        if (r9301 & BUF_CFG_9301_BIT6)
            REG_BUF_CFG_9301 = BUF_CFG_9301_BIT6;
        else if (r9301 & BUF_CFG_9301_BIT7) {
            REG_BUF_CFG_9301 = BUF_CFG_9301_BIT7;
            REG_POWER_DOMAIN = (REG_POWER_DOMAIN & ~POWER_DOMAIN_BIT1) | POWER_DOMAIN_BIT1;
        } else {
            uint8_t r9302 = REG_BUF_CFG_9302;
            if (r9302 & BUF_CFG_9302_BIT7)
                REG_BUF_CFG_9302 = BUF_CFG_9302_BIT7;
        }
    }

    /* Control transfer (bit 1): EP0 setup packet */
    if (!(periph_status & USB_PERIPH_CONTROL))
        return;

    phase = REG_USB_CTRL_PHASE;

    if (phase == USB_CTRL_PHASE_DATA_OUT || phase == 0x00) {
        REG_USB_CTRL_PHASE = USB_CTRL_PHASE_DATA_OUT;
        return;
    }

    if ((phase & USB_CTRL_PHASE_STAT_OUT) && !(phase & USB_CTRL_PHASE_SETUP)) {
        complete_usb20_status();
    } else if ((phase & USB_CTRL_PHASE_STAT_IN) && !(phase & USB_CTRL_PHASE_SETUP)) {
        REG_USB_DMA_TRIGGER = USB_DMA_STATUS_COMPLETE;
        REG_USB_CTRL_PHASE = USB_CTRL_PHASE_STAT_IN;
    } else if (phase & USB_CTRL_PHASE_SETUP) {
        uint8_t bmReq, bReq, wValL, wValH, wLenL;

        REG_USB_CONFIG = REG_USB_CONFIG;
        REG_USB_CTRL_PHASE = USB_CTRL_PHASE_SETUP;

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
            handle_set_config();
        } else if (bmReq == 0x01 && bReq == USB_REQ_SET_INTERFACE) {
            need_bulk_init = 1;
            send_zlp_ack();
            uart_puts("[I]\n");
        } else if (bmReq == 0x02 && bReq == 0x01) {
            /* CLEAR_FEATURE(HALT) on endpoint -- re-arm MSC for CBW */
            send_zlp_ack();
            EP_BUF(0x00) = 0x55; EP_BUF(0x01) = 0x53;
            EP_BUF(0x02) = 0x42; EP_BUF(0x03) = 0x53;
            REG_USB_MSC_LENGTH = 0x0D;
            REG_USB_MSC_CTRL = 0x01;
            {
                uint8_t ct = REG_USB_MSC_STATUS;
                REG_USB_MSC_STATUS = ct & ~0x01;
            }
        } else if (bmReq == 0xC0 && bReq == 0xE4) {
            /* VENDOR READ XDATA via control transfer */
            uint16_t addr = ((uint16_t)wValH << 8) | wValL;
            uint8_t len = REG_USB_SETUP_WLEN_L;
            uint8_t vi;
            uint16_t tw;
            for (tw = 0; tw < 50000; tw++) {
                if (REG_USB_CTRL_PHASE & USB_CTRL_PHASE_DATA_IN) break;
            }
            if (tw < 50000) {
                for (vi = 0; vi < len; vi++)
                    DESC_BUF[vi] = XDATA_REG8(addr + vi);
                send_descriptor_data(len);
            }
        } else if (bmReq == 0x40 && bReq == 0xE5) {
            /* VENDOR WRITE XDATA via control transfer */
            uint16_t addr = ((uint16_t)wValH << 8) | wValL;
            uint8_t val = REG_USB_SETUP_WIDX_L;
            XDATA_REG8(addr) = val;
            send_zlp_ack();
        } else if (bmReq == 0x40 && bReq == 0xE6) {
            /* VENDOR WRITE XDATA BLOCK via control transfer */
            uint16_t addr = ((uint16_t)wValH << 8) | wValL;
            uint8_t len = REG_USB_SETUP_WLEN_L;
            uint8_t vi;
            if (is_usb3) {
                for (vi = 0; vi < len; vi++)
                    XDATA_REG8(addr + vi) = DESC_BUF[vi];
                send_zlp_ack();
            } else {
                send_zlp_ack();
            }
        } else if (bmReq == 0x00 && (bReq == USB_REQ_SET_SEL || bReq == USB_REQ_SET_ISOCH_DELAY)) {
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
        REG_POWER_STATUS = REG_POWER_STATUS & ~(POWER_STATUS_USB_PATH | 0x80);
    }
}

void timer1_isr(void) __interrupt(3) { }
void serial_isr(void) __interrupt(4) { }
void timer2_isr(void) __interrupt(5) { }

/*==========================================================================
 * Hardware Init (from trace/enumerate_min -- stock firmware)
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
    REG_INT_AUX_STATUS = 0x02;
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
    need_bulk_init = 0;
    bulk_out_state = 0;
    REG_UART_LCR &= 0xF7;
    uart_puts("\n[BOOT]\n");

    hw_init();

    /* Detect initial USB speed (0x02=SS, 0x03=SS+ both mean USB 3.0) */
    {
        uint8_t link = REG_USB_LINK_STATUS;
        is_usb3 = (link >= USB_SPEED_SUPER) ? 1 : 0;
        uart_puts("[link=");
        uart_puthex(link);
        uart_puts("]\n");
    }

    uart_puts("[GO]\n");

    TCON = 0x04;  /* IT0=0 (level-triggered INT0), IT1=1 (edge-triggered INT1) */
    IE = IE_EA | IE_EX0 | IE_EX1 | IE_ET0;

    while (1) {
        REG_CPU_KEEPALIVE = 0x0C;

        /* Poll 9101 for bulk events */
        poll_bulk_events();

        /* Deferred bulk init (too heavy for ISR) */
        if (need_bulk_init) {
            need_bulk_init = 0;
            do_bulk_init();
        }

        /* Handle CBW in main loop context */
        if (need_cbw_process) {
            need_cbw_process = 0;
            handle_cbw();
        }

        /* Deferred bulk OUT state machine:
         * State 1: arm endpoint for data reception.
         * State 2: wait for BULK_DATA, copy from flash buffer (0x7000)
         *          to target address, then send CSW. */
        if (bulk_out_state == 1) {
            REG_USB_EP_CFG1 = USB_EP_CFG1_ARM_OUT;  /* 9093 = 0x02 */
            REG_USB_EP_CFG2 = USB_EP_CFG2_ARM_OUT;  /* 9094 = 0x10 */
            bulk_out_state = 2;
        } else if (bulk_out_state == 2) {
            uint8_t st = REG_USB_PERIPH_STATUS;
            if (st & USB_PERIPH_BULK_DATA) {
                uint8_t t;

                /* Ack 9093 (stock: 0x0FAC) */
                (void)REG_USB_EP_CFG1;
                REG_USB_EP_CFG1 = USB_EP_CFG1_ARM_OUT;

                /* C805 DMA config (stock: 0x32BF) */
                t = REG_INT_AUX_STATUS;
                REG_INT_AUX_STATUS = (t & 0xF9) | 0x02;

                /* CE88/CE89 handshake: triggers DMA of bulk OUT data
                 * to flash buffer at 0x7000. Poll CE89 bit 0 for ready. */
                REG_BULK_DMA_HANDSHAKE = 0x00;
                {
                    uint16_t t16;
                    for (t16 = 50000; t16; t16--) {
                        if (REG_USB_DMA_STATE & USB_DMA_STATE_READY) break;
                    }
                }

                /* Wait for DMA transfer byte count (CE55) to become nonzero,
                 * indicating USB-to-flash-buffer DMA is complete.
                 * Stock firmware reads CE55 at 0x34B6 after CE88/CE89 handshake. */
                {
                    uint16_t t16;
                    for (t16 = 50000; t16; t16--) {
                        if (REG_SCSI_DMA_XFER_CNT) break;
                    }
                }

                /* Copy received data from flash buffer (0x7000) to target */
                {
                    uint8_t ci;
                    for (ci = 0; ci < bulk_out_len; ci++)
                        XDATA_REG8(bulk_out_addr + ci) = XDATA_REG8(0x7000 + ci);
                }

                /* Restore USBS signature and tag for CSW */
                EP_BUF(0x00) = 0x55; EP_BUF(0x01) = 0x53;
                EP_BUF(0x02) = 0x42; EP_BUF(0x03) = 0x53;
                EP_BUF(0x04) = cbw_tag[0]; EP_BUF(0x05) = cbw_tag[1];
                EP_BUF(0x06) = cbw_tag[2]; EP_BUF(0x07) = cbw_tag[3];

                send_csw(0x00);
                bulk_out_state = 0;
            }
        }
    }
}
