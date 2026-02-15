/*
 * ASM2464PD USB 3.0 SuperSpeed Firmware with Bulk Transfers
 *
 * Init sequence from trace/enumerate_min (stock firmware trace).
 * USB 3.0 control transfer handling from trace/flash_usb3.
 * Bulk transfer engine from trace/pcie_enum.
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
/* Global: set in ISR, cleared in main loop after heavy init */
static volatile uint8_t need_bulk_init;
/* Global: set in ISR when CBW received, cleared in main loop after processing */
static volatile uint8_t need_cbw_process;
/* Global: SCSI opcode from last CBW */
static volatile uint8_t cbw_opcode;
/* Global: CBW tag bytes (saved before EP_BUF is overwritten by data) */
static uint8_t cbw_tag[4];
/* Global: set by ISR when EP_COMPLETE fires, cleared by wait_ep_complete */
static volatile uint8_t ep_complete_flag;

/* Forward declaration */
static void poll_bulk_events(void);

/* EP buffer accessor at 0xD800+ */
#define EP_BUF(n) XDATA_REG8(0xD800 + (n))

/*==========================================================================
 * USB Control Transfer Helpers (USB 2.0 and USB 3.0)
 *==========================================================================*/

/* Complete USB 3.0 status phase */
static void complete_usb3_status(void) {
    while (!(REG_USB_CTRL_PHASE & USB_CTRL_PHASE_STAT_IN)) { }
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
        REG_LINK_STATUS_E716 = 0x01;

        while (!(REG_USB_CTRL_PHASE & USB_CTRL_PHASE_STAT_IN)) { }

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

/* Device descriptor (18 bytes) — bcdUSB/bMaxPacketSize patched per speed */
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

    while (!(REG_USB_CTRL_PHASE & USB_CTRL_PHASE_DATA_IN)) { }

    if (desc_type == USB_DESC_TYPE_DEVICE) {
        /* Device descriptor — patch bcdUSB and bMaxPacketSize for speed */
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
 * Bulk Endpoint Init — arms the MSC engine for CBW reception
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

    /* USBS signature + MSC trigger — arms receiver for CBW */
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
 * Bulk Transfer Engine — Runtime CSW/Data via 90A1 Path
 *
 * Stock firmware runtime CSW (from trace/e1_bulk_out.txt):
 *   1. D80C = status, D808-D80B = residue
 *   2. 90A1 = 0x01 (trigger send)
 *   3. Wait for 9101 bit 5 (EP complete) — confirms data sent
 *   4. Re-arm MSC engine (C42C=0x01)
 *==========================================================================*/

/*
 * wait_ep_complete - Wait for bulk EP send to complete, then ack
 *
 * Polls 9101 for bit 5 (EP_COMPLETE). When set, does the full
 * acknowledge sequence matching stock firmware (trace/e1_bulk_out.txt:592-597):
 *   1. Read 9000 (USB status check)
 *   2. Read 9096 (EP ready check)
 *   3. Write 90E3 = 0x02 (clear EP complete status)
 *   4. Write 9096 = 0x01 (re-arm bulk endpoint)
 */
static void wait_ep_complete(void) {
    uint16_t timeout;
    for (timeout = 60000; timeout; timeout--) {
        if (ep_complete_flag) {
            ep_complete_flag = 0;
            return;
        }
        /* Poll directly in case main loop polling hasn't run */
        poll_bulk_events();
    }
    uart_puts("!TO!");
}

/*
 * send_csw - Send CSW (Command Status Wrapper) via 90A1 path
 *
 * Writes CSW fields, triggers 90A1, waits for EP complete.
 * Then re-arms MSC engine for next CBW.
 */
static void send_csw(uint8_t status) {
    /* Set CSW status (stock: 0x52C7 → D80C) */
    EP_BUF(0x0C) = status;

    /* USB status check (stock: 0x342B → 9000) */
    (void)REG_USB_STATUS;

    /* Set residue = 0 (stock: 0x5434-0x5440 → D808-D80B) */
    EP_BUF(0x08) = 0x00;
    EP_BUF(0x09) = 0x00;
    EP_BUF(0x0A) = 0x00;
    EP_BUF(0x0B) = 0x00;

    /* Trigger CSW send: 90A1 (NVMe DMA path, no-op without NVMe) +
     * C42C (MSC engine sends D800+ data and arms for next CBW).
     * ISR handles EP_COMPLETE asynchronously — ack + re-arm. */
    REG_USB_BULK_DMA_TRIGGER = 0x01;
    REG_USB_MSC_CTRL = 0x01;
    {
        uint8_t st = REG_USB_MSC_STATUS;
        REG_USB_MSC_STATUS = st & ~0x01;
    }
}

/*
 * scsi_respond_data - Send data phase (13 bytes via C42C) then CSW.
 *
 * Since 90A1 doesn't work without NVMe and C42C always sends 13 bytes,
 * we use C42C for both data and CSW. The host must request exactly 13
 * bytes for the data phase. Data > 13 bytes is not supported.
 *
 * Data is already at D800+ (may have overwritten USBS sig and tag).
 * After sending data via C42C, we wait for EP_COMPLETE, restore the
 * CSW fields, and send CSW via another C42C.
 */
static void scsi_respond_data(uint8_t data_len) {
    (void)data_len;  /* C42C always sends 13 bytes regardless */

    /* Phase 1: Send data — data is already at D800+, trigger C42C */
    REG_USB_BULK_DMA_TRIGGER = 0x01;
    REG_USB_MSC_CTRL = 0x01;
    {
        uint8_t st = REG_USB_MSC_STATUS;
        REG_USB_MSC_STATUS = st & ~0x01;
    }

    /* Wait for data phase EP_COMPLETE — poll since ISR may not catch it.
     * The EP_COMPLETE from data send fires, poll_bulk_events handles ack+re-arm. */
    {
        uint16_t timeout;
        for (timeout = 60000; timeout; timeout--) {
            poll_bulk_events();
            if (ep_complete_flag) {
                ep_complete_flag = 0;
                break;
            }
        }
        if (!timeout) uart_puts("!TO1!");
    }

    /* Phase 2: Restore USBS signature and tag (overwritten by data) */
    EP_BUF(0x00) = 0x55;  /* 'U' */
    EP_BUF(0x01) = 0x53;  /* 'S' */
    EP_BUF(0x02) = 0x42;  /* 'B' */
    EP_BUF(0x03) = 0x53;  /* 'S' */
    EP_BUF(0x04) = cbw_tag[0];
    EP_BUF(0x05) = cbw_tag[1];
    EP_BUF(0x06) = cbw_tag[2];
    EP_BUF(0x07) = cbw_tag[3];

    /* Send CSW via MSC engine (send_csw triggers C42C again) */
    send_csw(0x00);
}



/*==========================================================================
 * SCSI Response Fillers
 *==========================================================================*/

static void fill_inquiry(void) {
    EP_BUF(0x00) = 0x00; EP_BUF(0x01) = 0x80;
    EP_BUF(0x02) = 0x04; EP_BUF(0x03) = 0x02;
    EP_BUF(0x04) = 0x1F;
    EP_BUF(0x05) = 0x00; EP_BUF(0x06) = 0x00; EP_BUF(0x07) = 0x00;
    EP_BUF(0x08) = 'T'; EP_BUF(0x09) = 'I'; EP_BUF(0x0A) = 'N'; EP_BUF(0x0B) = 'Y';
    EP_BUF(0x0C) = ' '; EP_BUF(0x0D) = ' '; EP_BUF(0x0E) = ' '; EP_BUF(0x0F) = ' ';
    EP_BUF(0x10) = 'A'; EP_BUF(0x11) = 'S'; EP_BUF(0x12) = 'M'; EP_BUF(0x13) = '2';
    EP_BUF(0x14) = '4'; EP_BUF(0x15) = '6'; EP_BUF(0x16) = '4'; EP_BUF(0x17) = 'P';
    EP_BUF(0x18) = 'D'; EP_BUF(0x19) = ' '; EP_BUF(0x1A) = ' '; EP_BUF(0x1B) = ' ';
    EP_BUF(0x1C) = ' '; EP_BUF(0x1D) = ' '; EP_BUF(0x1E) = ' '; EP_BUF(0x1F) = ' ';
    EP_BUF(0x20) = '0'; EP_BUF(0x21) = '0'; EP_BUF(0x22) = '0'; EP_BUF(0x23) = '1';
}

static void fill_sense_no_error(void) {
    EP_BUF(0x00) = 0x70; EP_BUF(0x01) = 0x00;
    EP_BUF(0x02) = 0x00; EP_BUF(0x03) = 0x00; EP_BUF(0x04) = 0x00;
    EP_BUF(0x05) = 0x00; EP_BUF(0x06) = 0x00; EP_BUF(0x07) = 0x0A;
    EP_BUF(0x08) = 0x00; EP_BUF(0x09) = 0x00; EP_BUF(0x0A) = 0x00;
    EP_BUF(0x0B) = 0x00; EP_BUF(0x0C) = 0x00; EP_BUF(0x0D) = 0x00;
    EP_BUF(0x0E) = 0x00; EP_BUF(0x0F) = 0x00;
    EP_BUF(0x10) = 0x00; EP_BUF(0x11) = 0x00;
}

static void fill_read_capacity(void) {
    EP_BUF(0x00) = 0x00; EP_BUF(0x01) = 0x00;
    EP_BUF(0x02) = 0x07; EP_BUF(0x03) = 0xFF;
    EP_BUF(0x04) = 0x00; EP_BUF(0x05) = 0x00;
    EP_BUF(0x06) = 0x02; EP_BUF(0x07) = 0x00;
}

static void fill_mode_sense(void) {
    EP_BUF(0x00) = 0x03; EP_BUF(0x01) = 0x00;
    EP_BUF(0x02) = 0x00; EP_BUF(0x03) = 0x00;
}

/*==========================================================================
 * CBW Handler — dispatches SCSI commands from bulk OUT
 *
 * Stock firmware flow (0x0FEB → 0x1023 → 0x3458 → epilogue → state machine):
 *   1. Check 90E2 bit 0 (must be set for CBW processing)
 *   2. Write 90E2 = 0x01 (acknowledge/re-arm)
 *   3. CE88/CE89 DMA handshake (0x3484-0x348C):
 *      - Write CE88 = 0x00 (init DMA state machine)
 *      - Poll CE89 bit 0 until set (hardware ready)
 *      - Check CE89 bit 1 for error
 *   4. Read CBW data from CE registers
 *   5. Set state machine state (IDATA[0x6A])
 *   6. Process SCSI command and send CSW via state machine
 *==========================================================================*/
static void handle_cbw(void) {
    uint8_t opcode, t;
    uint16_t timeout;

    /* Stock firmware (0x0FEE-0x0FF5): Check 90E2 bit 0 before proceeding.
     * If bit 0 not set, skip CBW processing (data not ready). */
    t = REG_USB_MODE;
    if (!(t & 0x01)) {
        uart_puts("[90E2=0]\n");
        return;
    }

    /* USB status check (stock: 0x0FFC reads 9000) */
    (void)REG_USB_STATUS;

    /* Ack and re-arm bulk mode (stock: 0x1023-0x1028) */
    REG_USB_MODE = 0x01;

    /* CE88/CE89 DMA handshake (stock: 0x3484-0x3498)
     * Write CE88=0x00 to init the DMA state machine,
     * poll CE89 bit 0 until set, then check for errors.
     * Stock firmware does this with a tight loop (no timeout). */
    REG_XFER_CTRL_CE88 = 0x00;
    for (timeout = 50000; timeout; timeout--) {
        t = REG_USB_DMA_STATE;
        if (t & 0x01) break;
    }

    /* Check for DMA errors (stock: 0x3493 checks CE89 bit 1, 0x349D checks CE86 bit 4)
     * NOTE: If DMA errors, just log and continue — the opcode register (912A)
     * may still be valid since the hardware already received the CBW. */
    t = REG_USB_DMA_STATE;
    if (t & 0x02) {
        uart_puts("[D1:");
        uart_puthex(t);
        uart_puts("]\n");
    }

    /* Copy CBW tag to CSW buffer AND save to globals.
     * (stock: 0x318A-0x31A6 copies 911F-9122 → D804-D807)
     * Globals are needed because data-in commands overwrite D804-D807. */
    cbw_tag[0] = REG_CBW_TAG_0;
    cbw_tag[1] = REG_CBW_TAG_1;
    cbw_tag[2] = REG_CBW_TAG_2;
    cbw_tag[3] = REG_CBW_TAG_3;
    EP_BUF(0x04) = cbw_tag[0];
    EP_BUF(0x05) = cbw_tag[1];
    EP_BUF(0x06) = cbw_tag[2];
    EP_BUF(0x07) = cbw_tag[3];

    /* Clear CSW status (stock: 0x4DF5 writes D80C=0x00) */
    EP_BUF(0x0C) = 0x00;

    /* Read SCSI opcode */
    opcode = REG_USB_CBWCB_0;

    /* C428 direction bit clearing (stock: 0x4D09-0x4D0C reads C428, writes back with &~0x03)
     * The stock trace shows C428=0x30, writes back 0x30 (bits 0-1 already clear).
     * This clears direction bits before command dispatch. */
    t = REG_NVME_QUEUE_CFG;
    REG_NVME_QUEUE_CFG = t & ~0x03;

    /* USB status check (stock: 0x4D18 reads 9000) */
    (void)REG_USB_STATUS;
    uart_puts("[CBW:");
    uart_puthex(opcode);
    uart_puts("]\n");

    if (opcode == 0x00) {
        /* TEST UNIT READY */
        send_csw(0x00);
    } else if (opcode == 0x12) {
        /* INQUIRY */
        REG_USB_STATUS = 0x01;
        fill_inquiry();
        scsi_respond_data(0x24);
        REG_USB_STATUS = 0x00;
    } else if (opcode == 0x03) {
        /* REQUEST SENSE */
        REG_USB_STATUS = 0x01;
        fill_sense_no_error();
        scsi_respond_data(0x12);
        REG_USB_STATUS = 0x00;
    } else if (opcode == 0x25) {
        /* READ CAPACITY(10) */
        REG_USB_STATUS = 0x01;
        fill_read_capacity();
        scsi_respond_data(0x08);
        REG_USB_STATUS = 0x00;
    } else if (opcode == 0x1A) {
        /* MODE SENSE(6) */
        REG_USB_STATUS = 0x01;
        fill_mode_sense();
        scsi_respond_data(0x04);
        REG_USB_STATUS = 0x00;
    } else if (opcode == 0x1E) {
        /* PREVENT ALLOW MEDIUM REMOVAL */
        REG_USB_STATUS = 0x01;
        send_csw(0x00);
        REG_USB_STATUS = 0x00;
    } else if (opcode == 0xE5) {
        /* VENDOR WRITE REGISTER */
        uint8_t val  = REG_USB_CBWCB_1;
        uint8_t ah   = REG_USB_CBWCB_3;
        uint8_t al   = REG_USB_CBWCB_4;
        uint16_t addr = ((uint16_t)ah << 8) | al;
        XDATA_REG8(addr) = val;
        REG_USB_STATUS = 0x01;
        send_csw(0x00);
        REG_USB_STATUS = 0x00;
    } else if (opcode == 0xE4) {
        /* VENDOR READ REGISTER — returns data in CSW residue field.
         * No data phase (90A1 requires NVMe, C42C only sends 13 bytes).
         * CDB[1] = size (1-4 bytes), CDB[3:4] = address.
         * CSW residue D808-D80B contains the register value(s).
         * Host sends CBW with data_transfer_length=0 and reads CSW. */
        uint8_t sz   = REG_USB_CBWCB_1;
        uint8_t ah   = REG_USB_CBWCB_3;
        uint8_t al   = REG_USB_CBWCB_4;
        uint16_t addr = ((uint16_t)ah << 8) | al;
        if (sz > 4) sz = 4;  /* Max 4 bytes fit in residue */
        /* Write register values to CSW residue field (D808-D80B) */
        EP_BUF(0x08) = (sz >= 1) ? XDATA_REG8(addr) : 0x00;
        EP_BUF(0x09) = (sz >= 2) ? XDATA_REG8(addr + 1) : 0x00;
        EP_BUF(0x0A) = (sz >= 3) ? XDATA_REG8(addr + 2) : 0x00;
        EP_BUF(0x0B) = (sz >= 4) ? XDATA_REG8(addr + 3) : 0x00;
        REG_USB_STATUS = 0x01;
        /* send_csw_raw: send CSW with residue already set */
        EP_BUF(0x0C) = 0x00;  /* status = success */
        (void)REG_USB_STATUS;
        /* Don't clear residue — it contains our data */
        REG_USB_BULK_DMA_TRIGGER = 0x01;
        REG_USB_MSC_CTRL = 0x01;
        {
            uint8_t st = REG_USB_MSC_STATUS;
            REG_USB_MSC_STATUS = st & ~0x01;
        }
        REG_USB_STATUS = 0x00;
    } else if (opcode == 0xE1 || opcode == 0xE3 || opcode == 0xE8) {
        /* VENDOR FLASH COMMANDS — accept and return success for now */
        REG_USB_STATUS = 0x01;
        send_csw(0x00);
        REG_USB_STATUS = 0x00;
    } else {
        /* Unknown — fail */
        REG_USB_STATUS = 0x01;
        send_csw(0x01);
        REG_USB_STATUS = 0x00;
    }
}

/*==========================================================================
 * Link Event Handler
 *==========================================================================*/
static void handle_link_event(void) {
    uint8_t r9300 = REG_BUF_CFG_9300;

    if (r9300 & BUF_CFG_9300_SS_FAIL) {
        /* USB 3.0 failed — fall back to USB 2.0 */
        REG_POWER_STATUS = REG_POWER_STATUS | POWER_STATUS_USB_PATH;
        REG_POWER_EVENT_92E1 = 0x10;
        REG_USB_STATUS = REG_USB_STATUS | 0x04;
        REG_USB_STATUS = REG_USB_STATUS & ~0x04;
        REG_PHY_LINK_CTRL = 0x00;
        REG_CPU_MODE = 0x00;
        REG_LINK_WIDTH_E710 = 0x1F;
        REG_USB_PHY_CTRL_91C0 = 0x10;
        is_usb3 = 0;
        uart_puts("[T]\n");
    } else if (r9300 & BUF_CFG_9300_SS_OK) {
        /* USB 3.0 link OK */
        REG_BUF_CFG_9300 = BUF_CFG_9300_SS_OK;
        is_usb3 = 1;
        uart_puts("[3]\n");
    }

    REG_BUF_CFG_9300 = BUF_CFG_9300_SS_FAIL;
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
    REG_POWER_STATUS = REG_POWER_STATUS & ~POWER_STATUS_USB_PATH;

    for (timeout = 10000; timeout; timeout--) {
        r91d1 = REG_USB_PHY_CTRL_91D1;
        if (r91d1 & USB_PHY_CTRL_BIT0) break;
    }
    if (r91d1 & USB_PHY_CTRL_BIT0)
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

    REG_BUF_CFG_9300 = BUF_CFG_9300_SS_FAIL;
    REG_POWER_STATUS = REG_POWER_STATUS | POWER_STATUS_USB_PATH;
    REG_POWER_EVENT_92E1 = 0x10;
    REG_TIMER_CTRL_CC3B = REG_TIMER_CTRL_CC3B | 0x02;

    /* Detect USB speed after reset */
    {
        uint8_t link = REG_USB_LINK_STATUS;
        is_usb3 = (link >= USB_SPEED_SUPER) ? 1 : 0;
        uart_puts("[R");
        uart_puthex(link);
        uart_puts("]\n");
    }
}

/*==========================================================================
 * Interrupt Handlers
 *==========================================================================*/

/*
 * poll_bulk_events - Check 9101 for bulk events (EP_COMPLETE, CBW_RECEIVED)
 * Called from main loop. Avoids edge-triggered interrupt issues where
 * multiple events on the same INT0 line miss edges.
 */
static void poll_bulk_events(void) {
    uint8_t st = REG_USB_PERIPH_STATUS;

    /* EP complete (bit 5 = 0x20): ack only.
     * 90E3/9096 ack re-enables bulk OUT for next CBW.
     * Do NOT re-arm with C42C here — it sends 13 bytes of stale data
     * which the host receives as an unsolicited BULK IN packet. */
    if (st & USB_PERIPH_EP_COMPLETE) {
        (void)REG_USB_STATUS;
        (void)REG_USB_EP_READY;
        REG_USB_EP_STATUS_90E3 = 0x02;
        REG_USB_EP_READY = 0x01;
        ep_complete_flag = 1;
    }

    /* CBW received (bit 6 = 0x40) */
    if (st & USB_PERIPH_CBW_RECEIVED) {
        need_cbw_process = 1;
    }
}

void int0_isr(void) __interrupt(0) {
    uint8_t periph_status, phase;

    periph_status = REG_USB_PERIPH_STATUS;

    /* Link event (bit 4 = 0x10): USB 3.0 link status change */
    if (periph_status & USB_PERIPH_LINK_EVENT) {
        handle_link_event();
    }

    /* Bus reset (bit 0 = 0x01) without control (bit 1 = 0x02) */
    if ((periph_status & USB_PERIPH_BUS_RESET) && !(periph_status & USB_PERIPH_CONTROL)) {
        handle_usb_reset();
    }

    /* Bulk request (bit 3 = 0x08): ack 9301/9302 bits */
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

    /* Control transfer (bit 1 = 0x02): EP0 setup packet */
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
        } else if (bmReq == 0xA1 && bReq == 0xFE) {
            /* GET_MAX_LUN — required for MSC */
            while (!(REG_USB_CTRL_PHASE & USB_CTRL_PHASE_DATA_IN)) { }
            DESC_BUF[0] = 0x00;
            send_descriptor_data(1);
        } else if (bmReq == 0x02 && bReq == 0x01) {
            /* CLEAR_FEATURE(HALT) on endpoint */
            send_zlp_ack();
        } else if (bmReq == 0x21 && bReq == 0xFF) {
            /* BULK_ONLY_RESET */
            send_zlp_ack();
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
    ep_complete_flag = 0;
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

    TCON = 0x05;  /* IT0=1 (edge-triggered INT0), IT1=1 (edge-triggered INT1) */
    IE = IE_EA | IE_EX0 | IE_EX1 | IE_ET0;

    while (1) {
        REG_CPU_KEEPALIVE = 0x0C;

        /* Poll 9101 for bulk events (avoids edge-triggered ISR issues) */
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
    }
}
