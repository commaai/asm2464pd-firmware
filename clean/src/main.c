/*
 * ASM2464PD USB 3.0 Vendor-Class Firmware
 * Bulk IN/OUT via MSC engine, control transfers for enumeration + vendor cmds.
 */

#include "types.h"
#include "registers.h"
#include "globals.h"

__sfr __at(0xA8) IE;
__sfr __at(0x88) TCON;
__sfr __at(0x93) BANK_SEL;
#define IE_EA   0x80
#define IE_EX1  0x04
#define IE_ET0  0x02
#define IE_EX0  0x01

#define DESC_BUF ((__xdata uint8_t *)USB_CTRL_BUF_BASE)
#define EP_BUF(n) XDATA_REG8(0xD800 + (n))

static void desc_copy(__code const uint8_t *src, uint8_t len) {
    uint8_t i;
    for (i = 0; i < len; i++) DESC_BUF[i] = src[i];
}

void uart_putc(uint8_t ch) { REG_UART_THR = ch; }
void uart_puts(__code const char *str) { while (*str) uart_putc(*str++); }
static void uart_puthex(uint8_t val) {
    static __code const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[val >> 4]);
    uart_putc(hex[val & 0x0F]);
}

static volatile uint8_t is_usb3;
static volatile uint8_t need_bulk_init;
static volatile uint8_t need_cbw_process;
static uint8_t cbw_tag[4];
static volatile uint8_t bulk_out_state;
static uint16_t bulk_out_addr;
static uint8_t bulk_out_len;
static volatile uint8_t pd_power_ready_done;
static volatile uint8_t usb_configured;
static volatile uint8_t phy_unmask_pending;
static volatile uint16_t phy_unmask_counter;

static void poll_bulk_events(void);
static void phy_event_dispatcher(void);

/*=== USB Control Transfer Helpers ===*/

static void complete_usb3_status(void) {
    REG_USB_DMA_TRIGGER = USB_DMA_STATUS_COMPLETE;
    /* Stock firmware polls 0x9092 bit 2 until clear (0xb974) */
    while (REG_USB_DMA_TRIGGER & USB_DMA_STATUS_COMPLETE) { }
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_STAT_IN;
}

static void complete_usb20_status(void) {
    REG_USB_CONFIG |= USB_CTRL_PHASE_STAT_OUT;
    REG_USB_DMA_TRIGGER = USB_DMA_RECV;
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_STAT_OUT;
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_STAT_OUT;
    REG_USB_CONFIG &= ~USB_CTRL_PHASE_STAT_OUT;
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_DATA_OUT;
}

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

static void send_descriptor_data(uint8_t len) {
    REG_USB_EP0_STATUS = 0x00;
    REG_USB_EP0_LEN_L = len;
    REG_USB_DMA_TRIGGER = USB_DMA_SEND;
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_DATA_IN;
    if (is_usb3) complete_usb3_status();
}

/* Re-arm MSC engine to receive next CBW */
static void arm_msc(void) {
    EP_BUF(0x00) = 0x55; EP_BUF(0x01) = 0x53;
    EP_BUF(0x02) = 0x42; EP_BUF(0x03) = 0x53;
    REG_USB_MSC_LENGTH = 0x0D;
    REG_USB_MSC_CTRL = 0x01;
    REG_USB_MSC_STATUS &= ~0x01;
}

/*=== USB Request Handlers ===*/

static void handle_set_address(void) {
    uint8_t tmp;
    /* Stock firmware at BB19: 9090 = (9090 & 0x80) | r6
     * Preserves bit 7 (PHY event mask) while setting address bits.
     * r6 comes from XDATA[0x0AD0] — the USB device address/config value. */
    REG_USB_INT_MASK_9090 = (REG_USB_INT_MASK_9090 & 0x80) | 0x01;
    REG_USB_EP_CTRL_91D0 = 0x02;

    if (is_usb3) {
        REG_LINK_STATUS_E716 = 0x01;
        REG_USB_ADDR_CFG_A = 0x03; REG_USB_ADDR_CFG_B = 0x03;
        REG_USB_ADDR_CFG_A = 0x07; REG_USB_ADDR_CFG_B = 0x07;
        tmp = REG_USB_ADDR_CFG_A; REG_USB_ADDR_CFG_A = tmp;
        tmp = REG_USB_ADDR_CFG_B; REG_USB_ADDR_CFG_B = tmp;
        REG_USB_ADDR_PARAM_0 = 0x00; REG_USB_ADDR_PARAM_1 = 0x0A;
        REG_USB_ADDR_PARAM_2 = 0x00; REG_USB_ADDR_PARAM_3 = 0x0A;
        tmp = REG_USB_ADDR_CTRL; REG_USB_ADDR_CTRL = tmp;
        REG_USB_EP_CTRL_9220 = 0x04;
        complete_usb3_status();
    } else {
        send_zlp_ack();
    }
    uart_puts("[A]\n");
}

/* Descriptors */
static __code const uint8_t dev_desc[] = {
    0x12, 0x01, 0x20, 0x03, 0x00, 0x00, 0x00, 0x09,
    0xD1, 0xAD, 0x01, 0x00, 0x01, 0x00, 0x01, 0x02, 0x03, 0x01,
};
static __code const uint8_t cfg_desc[] = {
    0x09, 0x02, 0x2C, 0x00, 0x01, 0x01, 0x00, 0xC0, 0x00,
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0xFF, 0xFF, 0x00,
    0x07, 0x05, 0x81, 0x02, 0x00, 0x04, 0x00,
    0x06, 0x30, 0x00, 0x00, 0x00, 0x00,
    0x07, 0x05, 0x02, 0x02, 0x00, 0x04, 0x00,
    0x06, 0x30, 0x00, 0x00, 0x00, 0x00,
};
static __code const uint8_t bos_desc[] = {
    0x05, 0x0F, 0x16, 0x00, 0x02,
    0x07, 0x10, 0x02, 0x02, 0x00, 0x00, 0x00,
    0x0A, 0x10, 0x03, 0x00, 0x0E, 0x00, 0x03, 0x00, 0x00, 0x00,
};
static __code const uint8_t str0_desc[] = { 0x04, 0x03, 0x09, 0x04 };
static __code const uint8_t str1_desc[] = { 0x0A, 0x03, 't',0, 'i',0, 'n',0, 'y',0 };
static __code const uint8_t str2_desc[] = { 0x08, 0x03, 'u',0, 's',0, 'b',0 };
static __code const uint8_t str3_desc[] = { 0x08, 0x03, '0',0, '0',0, '1',0 };
static __code const uint8_t str_empty[] = { 0x02, 0x03 };

static void handle_get_descriptor(uint8_t desc_type, uint8_t desc_idx, uint8_t wlen) {
    __code const uint8_t *src;
    uint8_t desc_len;

    if (desc_type == USB_DESC_TYPE_DEVICE) {
        desc_copy(dev_desc, 18);
        if (!is_usb3) { DESC_BUF[2] = 0x10; DESC_BUF[3] = 0x02; DESC_BUF[7] = 0x40; }
        desc_len = 18;
    } else if (desc_type == USB_DESC_TYPE_CONFIG) {
        src = cfg_desc; desc_len = sizeof(cfg_desc); desc_copy(src, desc_len);
    } else if (desc_type == USB_DESC_TYPE_BOS) {
        src = bos_desc; desc_len = sizeof(bos_desc); desc_copy(src, desc_len);
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
}

/*=== SET_CONFIG ===*/
static void handle_set_config(void) {
    uint8_t t;
    REG_USB_EP_BUF_CTRL = 0x55; REG_USB_EP_BUF_SEL = 0x53;
    REG_USB_EP_BUF_DATA = 0x42; REG_USB_EP_BUF_PTR_LO = 0x53;
    REG_USB_MSC_LENGTH = 0x0D;
    t = REG_USB_EP0_CONFIG; REG_USB_EP0_CONFIG = t;
    t = REG_USB_EP0_CONFIG; REG_USB_EP0_CONFIG = t;
    REG_USB_EP_CFG2 = 0x01; REG_USB_EP_CFG2 = 0x08;
    REG_USB_EP_STATUS_90E3 = 0x02;
    t = REG_USB_EP_CTRL_905F; REG_USB_EP_CTRL_905F = t;
    t = REG_USB_EP_CTRL_905D; REG_USB_EP_CTRL_905D = t;
    REG_USB_EP_STATUS_90E3 = 0x01; REG_USB_CTRL_90A0 = 0x01;
    REG_USB_INT_MASK_9090 |= 0x80;
    t = REG_USB_STATUS; REG_USB_STATUS = t;
    t = REG_USB_CTRL_924C; REG_USB_CTRL_924C = t;
    send_zlp_ack();
    need_bulk_init = 1;
    uart_puts("[C]\n");
}

/*=== Bulk Init -- arms MSC engine for CBW reception ===*/
static void do_bulk_init(void) {
    uint16_t j;
    uint8_t t;

    /* Clear EP/NVMe/FIFO registers */
    REG_USB_EP_READY = 0xFF; REG_USB_EP_CTRL_9097 = 0xFF;
    REG_USB_EP_MODE_9098 = 0xFF; REG_USB_EP_MODE_9099 = 0xFF;
    REG_USB_EP_MODE_909A = 0xFF; REG_USB_EP_MODE_909B = 0xFF;
    REG_USB_EP_MODE_909C = 0xFF; REG_USB_EP_MODE_909D = 0xFF;
    REG_USB_STATUS_909E = 0x03;
    REG_USB_DATA_H = 0x00; REG_USB_FIFO_STATUS = 0x00;
    REG_USB_FIFO_H = 0x00; REG_USB_FIFO_4 = 0x00;
    REG_USB_FIFO_5 = 0x00; REG_USB_FIFO_6 = 0x00;
    REG_USB_FIFO_7 = 0x00;
    REG_USB_XCVR_MODE = 0x02; REG_USB_DATA_L = 0x00;

    /* MSC toggle */
    REG_USB_MSC_CFG |= 0x02;
    REG_USB_MSC_CFG |= 0x04;
    REG_USB_MSC_CFG &= ~0x02;
    REG_USB_MSC_CFG &= ~0x04;
    t = REG_USB_STATUS; REG_USB_STATUS = t;
    t = REG_USB_CTRL_924C; REG_USB_CTRL_924C = t;

    /* EP reconfig + activate */
    t = REG_USB_EP_CTRL_905F; REG_USB_EP_CTRL_905F = t;
    t = REG_USB_EP_CTRL_905D; REG_USB_EP_CTRL_905D = t;
    REG_USB_EP_STATUS_90E3 = 0x01; REG_USB_CTRL_90A0 = 0x01;
    REG_USB_STATUS = 0x01; REG_USB_CTRL_924C = 0x05;

    /* Clear endpoint buffer D800-DE5F */
    for (j = 0; j < 0x0660; j++) XDATA_REG8(0xD800 + j) = 0x00;
    REG_USB_EP_BUF_DE30 = 0x03; REG_USB_EP_BUF_DE36 = 0x00;

    /* 9200 toggle + MSC reset */
    REG_USB_CTRL_9200 |= 0x40;
    REG_USB_MSC_CFG |= 0x01;
    REG_USB_MSC_CFG &= ~0x01;
    REG_USB_CTRL_9200 &= ~0x40;

    /* Final EP config */
    t = REG_USB_EP0_CONFIG; REG_USB_EP0_CONFIG = t;
    t = REG_USB_EP0_CONFIG; REG_USB_EP0_CONFIG = t;
    REG_USB_EP_CFG2 = 0x01; REG_USB_EP_CFG2 = 0x08;
    REG_USB_EP_CTRL_905F |= 0x08;
    REG_USB_EP_STATUS_90E3 = 0x02; REG_USB_CTRL_90A0 = 0x01;

    /* Arm MSC for first CBW */
    REG_USB_STATUS = 0x00;
    t = REG_USB_CTRL_924C; REG_USB_CTRL_924C = t;
    REG_USB_MSC_CFG |= 0x02;
    REG_USB_MSC_CFG |= 0x04;
    REG_NVME_DOORBELL |= NVME_DOORBELL_BIT0;
    REG_USB_MSC_CFG |= 0x01;
    REG_NVME_DOORBELL |= NVME_DOORBELL_BIT1;
    REG_NVME_DOORBELL |= NVME_DOORBELL_BIT2;
    REG_NVME_DOORBELL |= NVME_DOORBELL_BIT3;
    REG_NVME_DOORBELL |= NVME_DOORBELL_BIT4;
    REG_USB_MSC_CFG &= ~0x02;
    REG_USB_MSC_CFG &= ~0x04;
    REG_NVME_DOORBELL &= ~NVME_DOORBELL_BIT0;
    REG_USB_MSC_CFG &= ~0x01;
    REG_NVME_DOORBELL &= ~NVME_DOORBELL_BIT1;
    REG_NVME_DOORBELL &= ~NVME_DOORBELL_BIT2;
    REG_NVME_DOORBELL &= ~NVME_DOORBELL_BIT3;
    REG_NVME_DOORBELL &= ~NVME_DOORBELL_BIT4;

    arm_msc();

    /* 9200 toggle (second pass) */
    REG_USB_CTRL_9200 |= 0x40;
    REG_USB_MSC_CFG |= 0x01;
    REG_USB_MSC_CFG &= ~0x01;
    REG_USB_CTRL_9200 &= ~0x40;

    /* EP reconfig (second pass) */
    t = REG_USB_EP0_CONFIG; REG_USB_EP0_CONFIG = t;
    t = REG_USB_EP0_CONFIG; REG_USB_EP0_CONFIG = t;
    REG_USB_EP_CFG2 = 0x01; REG_USB_EP_CFG2 = 0x08;
    t = REG_USB_EP_CTRL_905F; REG_USB_EP_CTRL_905F = t;
    REG_USB_EP_STATUS_90E3 = 0x02;

    usb_configured = 1;
    uart_puts("[rdy]\n");
}

/*=== Bulk Transfer Engine ===*/

static void send_csw(uint8_t status) {
    EP_BUF(0x0C) = status;
    EP_BUF(0x08) = 0x00; EP_BUF(0x09) = 0x00;
    EP_BUF(0x0A) = 0x00; EP_BUF(0x0B) = 0x00;
    REG_USB_BULK_DMA_TRIGGER = 0x01;
    while (!(REG_USB_PERIPH_STATUS & USB_PERIPH_EP_COMPLETE)) { }
    REG_USB_MSC_CTRL = 0x01;
    REG_USB_MSC_STATUS &= ~0x01;
}

static void sw_dma_bulk_in(uint16_t addr, uint8_t len) {
    uint8_t ah = (addr >> 8) & 0xFF;
    uint8_t al = addr & 0xFF;

    /* Clear stale EP_COMPLETE */
    if (REG_USB_PERIPH_STATUS & USB_PERIPH_EP_COMPLETE) {
        REG_USB_EP_STATUS_90E3 = 0x02; REG_USB_EP_READY = 0x01;
    }

    REG_USB_MSC_LENGTH = len;
    REG_DMA_CONFIG = DMA_CONFIG_SW_MODE;
    REG_USB_EP_BUF_HI = ah; REG_USB_EP_BUF_LO = al;
    EP_BUF(0x02) = ah; EP_BUF(0x03) = al;
    EP_BUF(0x04) = 0x00; EP_BUF(0x05) = 0x00;
    EP_BUF(0x06) = 0x00; EP_BUF(0x07) = 0x00;
    EP_BUF(0x0F) = 0x00; EP_BUF(0x00) = 0x03;

    REG_XFER_CTRL_C509 |= 0x01;
    REG_USB_EP_CFG_905A = USB_EP_CFG_BULK_IN;
    REG_USB_SW_DMA_TRIGGER = 0x01;
    REG_XFER_CTRL_C509 &= ~0x01;

    G_XFER_STATE_0AF4 = 0x40;
    REG_USB_BULK_DMA_TRIGGER = 0x01;

    /* Wait for EP_COMPLETE (9101 bit 5) */
    while (!(REG_USB_PERIPH_STATUS & USB_PERIPH_EP_COMPLETE)) { }
    REG_USB_EP_STATUS_90E3 = 0x02; REG_USB_EP_READY = 0x01;

    REG_DMA_CONFIG = DMA_CONFIG_DISABLE;
    REG_USB_MSC_LENGTH = 0x0D;
}

/*=== CBW Handler ===*/
static void handle_cbw(void) {
    uint8_t opcode;

    if (!(REG_USB_MODE & 0x01)) return;
    REG_USB_MODE = 0x01;

    /* CE88/CE89 DMA handshake */
    REG_BULK_DMA_HANDSHAKE = 0x00;
    while (!(REG_USB_DMA_STATE & USB_DMA_STATE_READY)) { }

    cbw_tag[0] = REG_CBW_TAG_0; cbw_tag[1] = REG_CBW_TAG_1;
    cbw_tag[2] = REG_CBW_TAG_2; cbw_tag[3] = REG_CBW_TAG_3;
    EP_BUF(0x04) = cbw_tag[0]; EP_BUF(0x05) = cbw_tag[1];
    EP_BUF(0x06) = cbw_tag[2]; EP_BUF(0x07) = cbw_tag[3];
    EP_BUF(0x0C) = 0x00;

    opcode = REG_USB_CBWCB_0;
    uart_puts("[CBW:");
    uart_puthex(opcode);
    uart_puts("]\n");

    if (opcode == 0xE5) {
        uint8_t val = REG_USB_CBWCB_1;
        uint16_t addr = ((uint16_t)REG_USB_CBWCB_3 << 8) | REG_USB_CBWCB_4;
        XDATA_REG8(addr) = val;
        send_csw(0x00);
    } else if (opcode == 0xE4) {
        uint8_t sz = REG_USB_CBWCB_1;
        uint16_t addr = ((uint16_t)REG_USB_CBWCB_3 << 8) | REG_USB_CBWCB_4;
        uint8_t i;
        for (i = 0; i < sz; i++) EP_BUF(i) = XDATA_REG8(addr + i);
        sw_dma_bulk_in(addr, sz);
        EP_BUF(0x00) = 0x55; EP_BUF(0x01) = 0x53;
        EP_BUF(0x02) = 0x42; EP_BUF(0x03) = 0x53;
        EP_BUF(0x04) = cbw_tag[0]; EP_BUF(0x05) = cbw_tag[1];
        EP_BUF(0x06) = cbw_tag[2]; EP_BUF(0x07) = cbw_tag[3];
        send_csw(0x00);
    } else if (opcode == 0xE6) {
        uint8_t len = REG_USB_CBWCB_1;
        uint16_t addr = ((uint16_t)REG_USB_CBWCB_3 << 8) | REG_USB_CBWCB_4;
        uint8_t i;
        if (len == 0) len = 64;
        for (i = 0; i < len; i++) EP_BUF(i) = XDATA_REG8(addr + i);
        sw_dma_bulk_in(addr, len);
        EP_BUF(0x00) = 0x55; EP_BUF(0x01) = 0x53;
        EP_BUF(0x02) = 0x42; EP_BUF(0x03) = 0x53;
        EP_BUF(0x04) = cbw_tag[0]; EP_BUF(0x05) = cbw_tag[1];
        EP_BUF(0x06) = cbw_tag[2]; EP_BUF(0x07) = cbw_tag[3];
        send_csw(0x00);
    } else if (opcode == 0xE7) {
        bulk_out_addr = ((uint16_t)REG_USB_CBWCB_3 << 8) | REG_USB_CBWCB_4;
        bulk_out_len = REG_USB_CBWCB_1;
        if (bulk_out_len == 0) bulk_out_len = 64;
        bulk_out_state = 1;
        return;
    } else if (opcode == 0xE8) {
        send_csw(0x00);
    } else if (opcode == 0xE9) {
        /* D92E-style PHY power enable sequence.
         * Triggered from host after USB is stable.
         * Returns CSW FIRST, then does the register writes. */
        uint8_t step = REG_USB_CBWCB_1;
        send_csw(0x00);

        /* After CSW is sent, do the register writes */
        if (step == 0) {
            /* Full D92E sequence with interrupts disabled */
            IE &= ~IE_EA;
            REG_POWER_STATUS = (REG_POWER_STATUS & ~0x40) | 0x40;
            REG_POWER_EVENT_92E1 = 0x10;
            REG_USB_STATUS = (REG_USB_STATUS & ~0x04) | 0x04;
            REG_USB_STATUS &= ~0x04;
            REG_USB_PHY_CTRL_91C0 |= 0x02;
            REG_USB_INT_MASK_9090 &= ~0x80;
            REG_BUF_CFG_9300 = 0x04;
            REG_USB_PHY_CTRL_91D1 = 0x02;
            REG_BUF_CFG_9301 = 0x40;
            REG_BUF_CFG_9301 = 0x80;
            REG_USB_PHY_CTRL_91D1 = 0x08;
            REG_USB_PHY_CTRL_91D1 = 0x01;
            IE |= IE_EA;
            uart_puts("[D92E done]\n");
        } else if (step == 1) {
            /* Just 92C2 |= 0x40 */
            IE &= ~IE_EA;
            REG_POWER_STATUS = (REG_POWER_STATUS & ~0x40) | 0x40;
            IE |= IE_EA;
            uart_puts("[92C2 set]\n");
        } else if (step == 2) {
            /* Just 9090 &= ~0x80 */
            IE &= ~IE_EA;
            REG_USB_INT_MASK_9090 &= ~0x80;
            IE |= IE_EA;
            uart_puts("[9090 clr]\n");
        } else if (step == 3) {
            /* Just unmask 9090 bit 7 — no 9000 pulse.
             * 9090 bit 7 masks PHY events (C80A bit 6).
             * Stock firmware clears via CC27 but the 9000 pulse kills USB3. */
            IE &= ~IE_EA;
            REG_USB_INT_MASK_9090 &= 0x7F;
            IE |= IE_EA;
            uart_puts("[9090 unmask]\n");
        } else if (step == 4) {
            /* Full CC27 + CA51 with interrupts disabled */
            IE &= ~IE_EA;
            REG_USB_INT_MASK_9090 &= 0x7F;
            { uint8_t s = REG_USB_LINK_STATUS;
            REG_USB_LINK_STATUS = (s & 0xFB) | 0x04;
            s = REG_USB_LINK_STATUS;
            REG_USB_LINK_STATUS = s & 0xFB; }
            REG_POWER_EVENT_92E1 = (REG_POWER_EVENT_92E1 & 0xBF) | 0x40;
            REG_POWER_STATUS &= 0xBF;
            IE |= IE_EA;
            uart_puts("[CA51 done]\n");
        }
        return;
    } else {
        send_csw(0x01);
    }
}

/*=== Link / 91D1 Handlers ===*/

static void handle_link_event(void) {
    uint8_t r9300 = REG_BUF_CFG_9300;
    if (r9300 & BUF_CFG_9300_SS_FAIL) {
        is_usb3 = 0;
        bulk_out_state = 0; need_cbw_process = 0; need_bulk_init = 0;
        uart_puts("[T]\n");
    } else if (r9300 & BUF_CFG_9300_SS_OK) {
        is_usb3 = 1;
        uart_puts("[3]\n");
    }
    REG_BUF_CFG_9300 = BUF_CFG_9300_SS_OK | BUF_CFG_9300_SS_FAIL | BUF_CFG_9300_SS_EVENT;
}

/*
 * 91D1 link training dispatch — keeps SS link alive.
 * Stock firmware ISR at 0x0f4a. Without this, link dies after 30-75s idle.
 */
static void handle_91d1_events(void) {
    uint8_t r91d1;

    if (!(REG_USB_PERIPH_STATUS & USB_PERIPH_BUS_RESET)) return;
    r91d1 = REG_USB_PHY_CTRL_91D1;

    /* bit 3: power management (U1/U2). Stock: 0x9b95 */
    if (r91d1 & USB_91D1_POWER_MGMT) {
        REG_USB_PHY_CTRL_91D1 = USB_91D1_POWER_MGMT;
        G_USB_TRANSFER_FLAG = 0;
        REG_TIMER_CTRL_CC3B &= ~TIMER_CTRL_LINK_POWER;
        G_TLP_BASE_LO = 0x01;
    }

    r91d1 = REG_USB_PHY_CTRL_91D1;

    /* bit 0: link training. Stock: 0xc465 -> bda4 state reset */
    if (r91d1 & USB_91D1_LINK_TRAIN) {
        REG_USB_PHY_CTRL_91D1 = USB_91D1_LINK_TRAIN;
        /* bda4: C6A8 |= 1, 92C8 &= ~3, CD31 reset */
        REG_PHY_CFG_C6A8 |= PHY_CFG_C6A8_ENABLE;
        REG_POWER_CTRL_92C8 &= ~POWER_CTRL_92C8_BIT0;
        REG_POWER_CTRL_92C8 &= ~POWER_CTRL_92C8_BIT1;
        REG_CPU_TIMER_CTRL_CD31 = CPU_TIMER_CD31_CLEAR;
        REG_CPU_TIMER_CTRL_CD31 = CPU_TIMER_CD31_START;
        if (!(REG_USB_PHY_CTRL_91C0 & USB_PHY_91C0_LINK_UP)) {
            REG_LINK_WIDTH_E710 = (REG_LINK_WIDTH_E710 & LINK_WIDTH_MASK) | LINK_RECOVERY_MODE;
            REG_TIMER_CTRL_CC3B &= ~TIMER_CTRL_LINK_POWER;
        }
        return;
    }

    /* bit 1: simple flag. Stock: 0xe6aa */
    if (r91d1 & USB_91D1_FLAG) {
        REG_USB_PHY_CTRL_91D1 = USB_91D1_FLAG;
        G_EP_DISPATCH_VAL3 = 0;
        G_USB_TRANSFER_FLAG = 1;
        return;
    }

    /* bit 2: link reset ack. Stock: 0xe682 */
    if (r91d1 & USB_91D1_LINK_RESET) {
        REG_PHY_CFG_C6A8 |= PHY_CFG_C6A8_ENABLE;
        G_USB_TRANSFER_FLAG = 0;
        G_SYS_FLAGS_07E8 = 0;
        REG_USB_PHY_CTRL_91D1 = USB_91D1_LINK_RESET;
    }
}

static void handle_usb_reset(void) {
    G_STATE_FLAG_0AF1 = 0x01;
    REG_USB_EP0_CONFIG |= 0x01;
    REG_USB_EP0_CONFIG |= 0x80;
    REG_USB_EP_READY = 0x01;
    bulk_out_state = 0; need_cbw_process = 0; need_bulk_init = 0;
    uart_puts("[R]\n");
}

/*=== Interrupt Handlers ===*/

static void poll_bulk_events(void) {
    uint8_t st = REG_USB_PERIPH_STATUS;
    if (st & USB_PERIPH_EP_COMPLETE) {
        REG_USB_EP_STATUS_90E3 = 0x02; REG_USB_EP_READY = 0x01;
    }
    if (st & USB_PERIPH_CBW_RECEIVED) need_cbw_process = 1;
}

void int0_isr(void) __interrupt(0) {
    uint8_t periph_status, phase;
    periph_status = REG_USB_PERIPH_STATUS;

    if (periph_status & USB_PERIPH_LINK_EVENT) handle_link_event();
    handle_91d1_events();

    if ((periph_status & USB_PERIPH_BUS_RESET) && !(periph_status & USB_PERIPH_CONTROL))
        handle_usb_reset();

    if (periph_status & USB_PERIPH_BULK_REQ) {
        uint8_t r9301 = REG_BUF_CFG_9301;
        if (r9301 & BUF_CFG_9301_BIT6)
            REG_BUF_CFG_9301 = BUF_CFG_9301_BIT6;
        else if (r9301 & BUF_CFG_9301_BIT7) {
            REG_BUF_CFG_9301 = BUF_CFG_9301_BIT7;
            REG_POWER_DOMAIN |= POWER_DOMAIN_BIT1;
        } else {
            uint8_t r9302 = REG_BUF_CFG_9302;
            if (r9302 & BUF_CFG_9302_BIT7) REG_BUF_CFG_9302 = BUF_CFG_9302_BIT7;
        }
    }

    if (!(periph_status & USB_PERIPH_CONTROL)) return;
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
        REG_USB_CTRL_PHASE = USB_CTRL_PHASE_SETUP;
        bmReq = REG_USB_SETUP_BMREQ; bReq = REG_USB_SETUP_BREQ;
        wValL = REG_USB_SETUP_WVAL_L; wValH = REG_USB_SETUP_WVAL_H;
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
            /* CLEAR_FEATURE(HALT) -- re-arm MSC */
            send_zlp_ack();
            arm_msc();
        } else if (bmReq == 0xC0 && bReq == 0xE4) {
            /* Vendor read XDATA via control */
            uint16_t addr = ((uint16_t)wValH << 8) | wValL;
            uint8_t vi;
            for (vi = 0; vi < wLenL; vi++) DESC_BUF[vi] = XDATA_REG8(addr + vi);
            send_descriptor_data(wLenL);
        } else if (bmReq == 0x40 && bReq == 0xE5) {
            /* Vendor write XDATA via control */
            uint16_t addr = ((uint16_t)wValH << 8) | wValL;
            XDATA_REG8(addr) = REG_USB_SETUP_WIDX_L;
            send_zlp_ack();
        } else if (bmReq == 0x40 && bReq == 0xE6) {
            /* Vendor write XDATA block via control */
            uint16_t addr = ((uint16_t)wValH << 8) | wValL;
            uint8_t vi;
            if (is_usb3) {
                for (vi = 0; vi < wLenL; vi++) XDATA_REG8(addr + vi) = DESC_BUF[vi];
            }
            send_zlp_ack();
        } else {
            send_zlp_ack();
        }
    }
}

void timer0_isr(void) __interrupt(1) { }

/*
 * cc_interrupt_ack - Acknowledge CC/PD interrupt sources
 *
 * Stock firmware handler at 0xA79C checks CC23, CC81, CC91, CCD9, CCF9
 * bit 1 (status pending) and writes 0x02 to acknowledge each.
 * The full handler dispatches to PD state machine, VDM processing, etc.
 * For now we just ack all pending sources to prevent re-trigger.
 *
 * Stock ISR dispatch: INT1 (0x44D7) → C806 bit 0 → 0x0507 → 0xA79C
 */
static void cc_interrupt_ack(void) {
    /* CC23 bit 1: Timer3/CC event (stock: 0xA79C-0xA7AB) */
    if (REG_TIMER3_CSR & 0x02) {
        REG_TIMER3_CSR = 0x02;
    }

    /* CC81 bit 1: CC DMA/interrupt (stock: 0xA7AC-0xA7E0) */
    if (REG_CPU_INT_CTRL & 0x02) {
        REG_CPU_INT_CTRL = 0x02;
    }

    /* CC91 bit 1: PD contract event (stock: 0xA7E1-0xA871)
     *
     * Stock firmware reads 0x09F9 bit 6 (PD fully negotiated flag) and if set:
     *   92C2 |= 0x40 — enable power domain
     *   92E1 = 0x10  — trigger power event
     *   9090 &= ~0x80 — unmask PHY interrupts (enables C80A bit 6)
     *
     * We skip real PD negotiation but need these writes to enable PHY events.
     * Gate: only do it once, and only after USB is configured (is_usb3 set). */
    if (REG_CPU_DMA_INT & 0x02) {
        REG_CPU_DMA_INT = 0x02;
    }

    /* CCD9 bit 1: Transfer 2 DMA (stock: 0xA872-0xA87F) */
    if (REG_XFER2_DMA_STATUS & 0x02) {
        REG_XFER2_DMA_STATUS = 0x02;
    }

    /* CCF9 bit 1: CPU extended status (stock: 0xA880-0xA88D) */
    if (REG_CPU_EXT_STATUS & 0x02) {
        REG_CPU_EXT_STATUS = 0x02;
    }
}

void int1_isr(void) __interrupt(2) {
    /*
     * Stock firmware INT1 ISR at 0x44D7-0x4582:
     *   C806 bit 0 → lcall 0x0507 → 0xA79C (CC/PD interrupt handler)
     *   CC33 bit 2 → ack timer (write 0x04)
     *   C80A bit 6 → lcall 0x0516 → 0xAE89 (PHY event dispatcher)
     *   0x09F9 & 0x83 → additional C80A dispatches (bits 5, 4)
     */

    /* Power event: ack 92E1 and handle 92C2.
     * NOTE: The stock firmware does NOT have this in the ISR, but our firmware
     * needs the 92C2 write for USB to enumerate. Clear both bits 6+7 for USB,
     * but ONLY before USB is configured. After USB config, preserve bit 6
     * which is needed for PCIe PHY power domain. */
    { uint8_t pwr = REG_POWER_EVENT_92E1;
      if (pwr) {
        REG_POWER_EVENT_92E1 = pwr;
        if (!usb_configured) {
            REG_POWER_STATUS &= ~(POWER_STATUS_USB_PATH | 0x80);
        }
      }
    }

    /* CC/PD interrupt: C806 bit 0 → CC interrupt handler
     * Stock firmware: 0x44F4 checks C806 bit 0 → lcall 0x0507 → 0xA79C */
    if (REG_INT_SYSTEM & 0x01) {
        cc_interrupt_ack();
    }

    /* CC33 bit 2: timer ack (stock: 0x44FE-0x4508) */
    if (XDATA_REG8(0xCC33) & 0x04) {
        XDATA_REG8(0xCC33) = 0x04;
    }

    /* PHY event dispatch: C80A bit 6 → phy_event_dispatcher
     * Stock firmware: 0x450B checks C80A bit 6 → lcall 0x0516 → 0xAE89
     * Must gate on usb_configured: E40F events fire right after boot and
     * if dispatched during enumeration, the handler disrupts USB.
     * Silently ack events until USB is stable. */
    if (REG_INT_PCIE_NVME & 0x40) {
        if (usb_configured) {
            phy_event_dispatcher();
        } else {
            REG_PHY_EVENT_E40F = 0xFF;
            REG_PHY_INT_STATUS_E410 = 0xFF;
        }
    }
}

void timer1_isr(void) __interrupt(3) { }
void serial_isr(void) __interrupt(4) { }
void timer2_isr(void) __interrupt(5) { }

static void delay_short(void) {
    volatile uint16_t i;
    for (i = 0; i < 1000; i++) { }
}

static void delay_long(void) {
    volatile uint16_t i;
    for (i = 0; i < 10000; i++) { }
}

/*
 * xdata_ext_rmw - Read-modify-write XDATA with extended page via SFR 0x93
 * Address: 0x0A99-0x0AC8 (stock firmware helpers)
 *
 * The ASM2464PD uses SFR 0x93 (BANK_SEL) as an extended XDATA page register.
 * For page N (N>=2), actual SFR value = (N-1) & 0x7F.
 * After access, SFR 0x93 is reset to 0.
 *
 * Stock firmware at D9BD-D9DC uses this for PHY SerDes config:
 *   Page 2 (SFR 0x93=1), addr 0x7041: clear bit 6
 *   Page 2 (SFR 0x93=1), addr 0x1507: set bits 1,2
 */
/*
 * Extended XDATA page access via SFR 0x93 (BANK_SEL)
 *
 * WARNING: SDCC --model-large stores params/locals in XDATA.
 * BANK_SEL must be set ONLY around the actual movx instruction,
 * not around any other XDATA access (params, locals, etc).
 * Using __naked functions with inline asm to control this precisely.
 *
 * Stock firmware: 0x0A99 (ext_read), 0x0AB4 (ext_write)
 */
static uint8_t xdata_ext_read(uint16_t addr) __naked {
    (void)addr;
    __asm
        ; dptr already has addr (first param in model-large passed in dptr)
        ; Set page, read, clear page
        mov  0x93, #0x01
        movx a, @dptr
        mov  0x93, #0x00
        mov  dpl, a
        ret
    __endasm;
}

static void xdata_ext_write(uint16_t addr, uint8_t val) __naked {
    (void)addr; (void)val;
    __asm
        ; dptr has addr, val is in XDATA at _xdata_ext_write_PARM_2
        ; Must read val BEFORE setting BANK_SEL
        push dpl
        push dph
        mov  dptr, #_xdata_ext_write_PARM_2
        movx a, @dptr             ; a = val (read from page 0)
        pop  dph
        pop  dpl
        ; Now set page, write, clear page
        mov  0x93, #0x01
        movx @dptr, a
        mov  0x93, #0x00
        ret
    __endasm;
}


/*
 * phy_poll_registers - PHY poll register update
 * Address: 0xC0F9-0xC16B (115 bytes)
 *
 * Called every main loop iteration by phy_maintenance().
 * Updates PHY registers C655/C620/C65A based on poll mode,
 * and C655 bit 3/C623/C65A bit 3 for second-lane if training incomplete.
 *
 * Original disassembly:
 *   c0f9: mov dptr, #0x0b2f  ; read poll mode
 *   c0fc: movx a, @dptr
 *   c0fd: mov r7, a
 *   c0fe: cjne a, #0x02, c10f ; mode 2 -> special path
 *   ...
 */
static void phy_poll_registers(void) {
    uint8_t mode = G_PHY_POLL_MODE;
    uint8_t tmp;

    if (mode == 2) {
        /* Mode 2: C620 = (C620 & 0xE0) | 0x05 */
        tmp = REG_PHY_EXT_CTRL_C620;
        REG_PHY_EXT_CTRL_C620 = (tmp & 0xE0) | 0x05;
        /* Also set bit 3 of CC2A (keepalive) */
        tmp = REG_CPU_KEEPALIVE;
        REG_CPU_KEEPALIVE = (tmp & 0xF7) | 0x08;
    } else {
        /* Mode 0 or 1 */
        tmp = REG_PHY_CFG_C655;
        if (mode == 1) {
            REG_PHY_CFG_C655 = tmp & 0xFE;           /* Clear bit 0 */
        } else {
            REG_PHY_CFG_C655 = (tmp & 0xFE) | 0x01;  /* Set bit 0 */
        }

        tmp = REG_PHY_EXT_CTRL_C620;
        REG_PHY_EXT_CTRL_C620 = tmp & 0xE0;           /* Clear bits 0-4 */

        tmp = REG_PHY_CFG_C65A;
        REG_PHY_CFG_C65A = (tmp & 0xFE) | 0x01;       /* Set bit 0 */
    }

    /* If training not complete, also update second-lane registers */
    if (G_STATE_FLAG_0AE3 == 0) {
        uint8_t saved = G_PHY_LANE_POLL_MODE;

        if (saved == 2) {
            /* Saved mode 2: C623 = (C623 & 0xE0) | 0x05 */
            tmp = REG_PHY_EXT_CTRL_C623;
            REG_PHY_EXT_CTRL_C623 = (tmp & 0xE0) | 0x05;
        } else {
            /* Saved mode 0 or 1 */
            tmp = REG_PHY_CFG_C655;
            if (saved == 1) {
                REG_PHY_CFG_C655 = tmp & 0xF7;           /* Clear bit 3 */
            } else {
                REG_PHY_CFG_C655 = (tmp & 0xF7) | 0x08;  /* Set bit 3 */
            }

            tmp = REG_PHY_EXT_CTRL_C623;
            REG_PHY_EXT_CTRL_C623 = tmp & 0xE0;           /* Clear bits 0-4 */
        }

        /* Set bit 3 of C65A */
        tmp = REG_PHY_CFG_C65A;
        REG_PHY_CFG_C65A = (tmp & 0xF7) | 0x08;
    }
}

/*
 * phy_maintenance - Continuous PHY maintenance
 * Address: 0xC5A1-0xC608 (104 bytes)
 *
 * Called every main loop iteration. Determines PHY poll mode from CD31
 * status bits, checks training completion, and calls phy_poll_registers.
 *
 * Poll mode determination:
 *   - If G_USB_TRANSFER_FLAG (0x0B2E) nonzero: mode = 0
 *   - Else check CD31: bit0=1 AND bit1=0 -> mode=2, else mode=1
 *
 * Training check (0xD1C9):
 *   Returns 0-4 based on 92C2/91C0/9100/92F8 register state.
 *   If result > 1: save poll_mode to lane_poll_mode, clear poll_mode.
 *
 * Original disassembly:
 *   c5a1: clr EA
 *   c5a3: mov dptr, #0x09fa
 *   ...
 *   c603: lcall 0xc0f9
 *   c606: setb EA
 *   c608: ret
 */
static void phy_maintenance(void) {
    uint8_t cd31;

    IE &= ~IE_EA;   /* Disable interrupts (stock: clr EA at c5a1) */

    /* Determine PHY poll mode from CD31 status */
    if (G_USB_TRANSFER_FLAG != 0) {
        /* 0x0B2E nonzero -> mode 0 (at c5e4) */
        G_PHY_POLL_MODE = 0;
    } else {
        /* Read CD31 timer/PHY status (at c5c9) */
        cd31 = REG_CPU_TIMER_CTRL_CD31;
        if ((cd31 & 0x01) && !(cd31 & 0x02)) {
            /* bit0=1, bit1=0 -> mode 2 (at c5d4) */
            G_PHY_POLL_MODE = 2;
        } else {
            /* mode 1 (at c5dc) */
            G_PHY_POLL_MODE = 1;
        }
    }

    /* Check if training complete (at c5e9) */
    if (G_STATE_FLAG_0AE3 == 0) {
        /*
         * Training status check (0xD1C9)
         * Returns: 0=blocked, 1=not L0, 2=partial, 3=lanes partial, 4=fully trained
         * Check 92C2 bit 6 + 91C0 bit 1: if both set, return 0
         * Check 9100 & 0x03: if != 0x02, return 1
         * Check 92F8 bit 5: if clear, return 2
         * Check 92F8 bits 2-3: if zero, return 3
         * Check 92F8 bit 4: if clear, return 3, else return 4
         */
        uint8_t training_level = 1; /* default: not fully trained */
        if ((REG_POWER_STATUS & 0x40) && (REG_USB_PHY_CTRL_91C0 & 0x02)) {
            training_level = 0;
        } else if ((REG_USB_LINK_STATUS & 0x03) != 0x02) {
            training_level = 1;
        } else {
            uint8_t r92f8 = REG_POWER_STATUS_92F8;
            if (!(r92f8 & 0x20)) {
                training_level = 2;
            } else if (!(r92f8 & 0x0C)) {
                training_level = 3;
            } else if (r92f8 & 0x10) {
                training_level = 4;
            } else {
                training_level = 3;
            }
        }

        /* If training level > 1: save poll mode, clear it (at c5f8)
         * NOTE: This forces mode=0 which SETs C655 bit 0.
         * Mode 1 (CLEARs C655 bit 0) was what gave B450=0x01 previously. */
        if (training_level > 1) {
            G_PHY_LANE_POLL_MODE = G_PHY_POLL_MODE;
            G_PHY_POLL_MODE = 0;
        }
    }

    /* Call PHY poll register update (at c603) */
    phy_poll_registers();

    IE |= IE_EA;    /* Re-enable interrupts (stock: setb EA at c606) */
}

/*
 * timer_stop - Stop and reset hardware timer 0
 * Address: 0xE642-0xE64B (10 bytes)
 *
 * Stops timer by writing 0x04, then clears by writing 0x02 to CC11.
 *
 * Original disassembly:
 *   e642: mov dptr, #0xcc11
 *   e645: mov a, #0x04
 *   e647: movx @dptr, a       ; stop timer
 *   e648: mov a, #0x02
 *   e64a: movx @dptr, a       ; clear/reset timer
 *   e64b: ret
 */
static void timer_stop(void) {
    REG_TIMER0_CSR = TIMER_CSR_CLEAR;    /* 0x04 = stop */
    REG_TIMER0_CSR = TIMER_CSR_EXPIRED;  /* 0x02 = clear/ack */
}

/*
 * hw_timer_delay - Hardware timer-based delay
 * Address: 0xE581-0xE591 (17 bytes) + timer_setup at 0xE292-0xE2AD (28 bytes)
 *
 * Uses hardware timer 0 (CC10-CC13) for precise delays.
 * Stock firmware calls this via dispatch 0x0502.
 *
 * Parameters:
 *   prescaler: bits 0-2 of CC10 (timer clock divider)
 *   count_hi:  CC12 (timer count high byte)
 *   count_lo:  CC13 (timer count low byte)
 *
 * Original disassembly (timer_setup at 0xE292):
 *   e292: lcall 0xe642          ; timer_stop()
 *   e295: mov dptr, #0xcc10
 *   e298: movx a, @dptr         ; read CC10
 *   e299: anl a, #0xf8          ; clear bits 0-2
 *   e29b: orl a, r7             ; set prescaler bits
 *   e29c: movx @dptr, a         ; write CC10
 *   e29d-e2a6: write count to CC12:CC13
 *   e2a7: mov dptr, #0xcc11
 *   e2aa: mov a, #0x01
 *   e2ac: movx @dptr, a         ; start timer
 *
 * Original disassembly (hw_timer_delay at 0xE581):
 *   e581: lcall 0xe292          ; timer_setup
 *   e584: mov dptr, #0xcc11
 *   e587: movx a, @dptr         ; read CC11
 *   e588: jnb 0xe0.1, 0xe584   ; loop until bit 1 set (timer expired)
 *   e58b: mov dptr, #0xcc11
 *   e58e: mov a, #0x02
 *   e590: movx @dptr, a         ; ack timer (write 0x02)
 *   e591: ret
 */
static void hw_timer_delay(uint8_t prescaler, uint8_t count_hi, uint8_t count_lo) {
    /* timer_setup (0xE292): stop, configure, start */
    timer_stop();
    REG_TIMER0_DIV = (REG_TIMER0_DIV & 0xF8) | (prescaler & 0x07);
    REG_TIMER0_THRESHOLD_HI = count_hi;
    REG_TIMER0_THRESHOLD_LO = count_lo;
    REG_TIMER0_CSR = TIMER_CSR_ENABLE;  /* 0x01 = start timer */

    /* hw_timer_delay (0xE581): poll until expired */
    while (!(REG_TIMER0_CSR & TIMER_CSR_EXPIRED)) { }
    REG_TIMER0_CSR = TIMER_CSR_EXPIRED;  /* 0x02 = ack completion */
}

/*
 * phy_rst_rxpll - Reset RXPLL (receiver PLL) for PCIe link recovery
 * Bank 1 Address: 0xE989-0xE9BA (50 bytes) [actual addr: 0x168F4]
 *
 * Resets the downstream PCIe receiver PLL to allow re-lock.
 * Called during link speed changes, PHY recovery, and CDR timeout.
 *
 * Sequence:
 *   1. Set bit 2 of CC37 (RXPLL reset mode enable)
 *   2. Write 0xFF to C20E (assert RXPLL reset)
 *   3. hw_timer_delay(prescaler=1, count=0x0014)
 *   4. Write 0x00 to C20E (de-assert RXPLL reset)
 *   5. hw_timer_delay(prescaler=2, count=0x0028)
 *   6. Clear bit 2 of CC37 (exit reset mode)
 *
 * Original disassembly:
 *   e989: mov r3, #0xff         ; string bank
 *   e98b-e98f: lcall uart_puts("\r\n[RstRxpll...]")
 *   e992: lcall 0x9877          ; a = CC37 & 0xFB
 *   e995: orl a, #0x04          ; set bit 2
 *   e997: movx @dptr, a         ; CC37 |= 0x04
 *   e998: mov dptr, #0xc20e
 *   e99b: lcall 0x9a48          ; C20E = 0xFF, set delay params (R5=0x14,R4=0,R7=1)
 *   e99e: lcall 0x0502          ; hw_timer_delay(1, 0x00, 0x14)
 *   e9a1: mov dptr, #0xc20e
 *   e9a4: clr a
 *   e9a5: movx @dptr, a         ; C20E = 0x00
 *   e9a6: mov r5, #0x28
 *   e9a8: mov r4, a             ; R4 = 0
 *   e9a9: mov r7, #0x02
 *   e9ab: lcall 0x0502          ; hw_timer_delay(2, 0x00, 0x28)
 *   e9ae: lcall 0x9877          ; a = CC37 & 0xFB
 *   e9b1: movx @dptr, a         ; CC37 &= ~0x04
 *   e9b2-e9b8: ljmp uart_puts("[Done]")
 */
static void phy_rst_rxpll(void) {
    uart_puts("\r\n[RstRxpll...]");

    /* Enter RXPLL reset mode: CC37 |= 0x04 (set bit 2) */
    REG_CPU_CTRL_CC37 = (REG_CPU_CTRL_CC37 & ~CPU_CTRL_CC37_RXPLL_MODE) | CPU_CTRL_CC37_RXPLL_MODE;

    /* Assert RXPLL reset */
    REG_PHY_RXPLL_RESET = 0xFF;

    /* Wait: prescaler=1, count=0x0014 (20 ticks) */
    hw_timer_delay(0x01, 0x00, 0x14);

    /* De-assert RXPLL reset */
    REG_PHY_RXPLL_RESET = 0x00;

    /* Wait for PLL re-lock: prescaler=2, count=0x0028 (40 ticks) */
    hw_timer_delay(0x02, 0x00, 0x28);

    /* Exit RXPLL reset mode: CC37 &= ~0x04 (clear bit 2) */
    REG_CPU_CTRL_CC37 &= ~CPU_CTRL_CC37_RXPLL_MODE;

    uart_puts("[Done]");
}

/* Forward declarations for functions used before definition */
static void pcie_tunnel_adapter_config(void);

/*
 * phy_power_enable - PHY power-on and clock configuration for PCIe
 * Bank 1 Address: 0xD92E-0xD968 (59 bytes) [actual addr: 0x15899]
 *
 * Called by PD event handler to enable PHY power before link training.
 * Sequence:
 *   1. CC3D: 92C2 |= 0x40 (set bit 6 — PHY power enable)
 *   2. CC2D: 92E1 = 0x10, 9000 pulse bit 2 (PHY clock gate toggle)
 *   3. CC4C: 91C0 |= 0x02 (PHY interface enable)
 *   4. 9090 &= 0x7F (clear USB interference bit)
 *   5. Timer delay (if with_delay)
 *   6. 9300 = 0x04 (SerDes mode select)
 *   7. 91D1 sequence: 0x02 → 9301=0x40/0x80 → 91D1=0x08/0x01
 *
 * Original disassembly:
 *   d92e: lcall 0xcc3d           ; 92C2 |= 0x40, set dptr=92E1, a=0x10
 *   d931: lcall 0xcc2d           ; 92E1 = 0x10, 9000 pulse bit 2
 *   d934: lcall 0xcc4c           ; 91C0 |= 0x02
 *   d937: mov dptr, #0x9090
 *   d93a: movx a, @dptr
 *   d93b: anl a, #0x7f
 *   d93d: movx @dptr, a          ; 9090 &= 0x7F
 *   d93e: mov a, r7
 *   d93f: jz 0xd94a              ; skip delay if r7==0
 *   d941: hw_timer_delay(2, 0x57, 0x05)
 *   d94a: 9300 = 0x04
 *   d94f: 91D1 = 0x02
 *   d956: 9301 = 0x40
 *   d95c: 9301 = 0x80
 *   d95f: 91D1 = 0x08
 *   d964: 91D1 = 0x01
 *   d968: ret
 */
static void phy_power_enable(uint8_t with_delay) {
    /* CC3D sub: 92C2 |= 0x40 — PHY power enable */
    XDATA_REG8(0x92C2) = (XDATA_REG8(0x92C2) & 0xBF) | 0x40;

    /* CC2D sub: 92E1 = 0x10, then 9000 pulse bit 2 */
    XDATA_REG8(0x92E1) = 0x10;
    XDATA_REG8(0x9000) = (XDATA_REG8(0x9000) & 0xFB) | 0x04;
    XDATA_REG8(0x9000) &= 0xFB;

    /* CC4C sub: 91C0 |= 0x02 — PHY interface enable */
    REG_USB_PHY_CTRL_91C0 = (REG_USB_PHY_CTRL_91C0 & 0xFD) | 0x02;

    /* 9090 &= 0x7F — clear bit 7 */
    XDATA_REG8(0x9090) &= 0x7F;

    /* Optional delay (~600 ticks) */
    if (with_delay) {
        hw_timer_delay(0x02, 0x57, 0x05);
    }

    /* SerDes mode select */
    XDATA_REG8(0x9300) = 0x04;

    /* 91D1 PHY sequence */
    XDATA_REG8(0x91D1) = 0x02;
    XDATA_REG8(0x9301) = 0x40;
    XDATA_REG8(0x9301) = 0x80;
    XDATA_REG8(0x91D1) = 0x08;
    XDATA_REG8(0x91D1) = 0x01;
}

/*
 * pcie_full_link_setup - Complete PCIe link setup with ext page config
 * Bank 1 Address: 0xCC83-0xCCDC (90 bytes) [actual addr: 0x14BEE]
 *
 * Full CC83 function: configures CA06, reloads tunnel adapter,
 * writes ext page SerDes equalization, performs B4xx link-up dance,
 * then configures ext page downstream port settings.
 *
 * Original disassembly:
 *   cc83: CA06 &= 0xEF           ; clear bit 4
 *   cc8a: lcall C6D7             ; tunnel adapter reload from globals
 *   cc8d-cc9a: ext[4084]=0x22, ext[5084]=0x22  ; SerDes EQ
 *   cc9d: B401 |= 0x01           ; via 9941 helper
 *   cca3: B482 |= 0x01           ; via 9941 helper
 *   cca9: B482 = (B482 & 0x0F) | 0xF0
 *   ccaf: B401 &= 0xFE, B480 |= 0x01  ; via 993D helper
 *   ccb8: B430 &= 0xFE
 *   ccbf: B298 = (B298 & 0xEF) | 0x10
 *   ccc8: ext[6043] = 0x70       ; downstream port config
 *   ccd3: ext[2543] = (ext[2543] & 0x7F) | 0x80
 *   ccda: ljmp 0x0bbe            ; tail-call ext write
 */
static void pcie_full_link_setup(void) {
    /* CA06: clear bit 4 */
    XDATA_REG8(0xCA06) &= 0xEF;

    /* Reload tunnel adapter config (C6D7 equivalent) */
    pcie_tunnel_adapter_config();

    /* ext[0x4084] = 0x22 — SerDes equalization, lane 0 */
    xdata_ext_write(0x4084, 0x22);

    /* ext[0x5084] = 0x22 — SerDes equalization, lane 1 */
    xdata_ext_write(0x5084, 0x22);

    /* B401 |= 0x01 — link request */
    XDATA_REG8(0xB401) |= 0x01;

    /* B482 |= 0x01 — port enable */
    XDATA_REG8(0xB482) |= 0x01;

    /* B482 = (B482 & 0x0F) | 0xF0 — set high nibble */
    XDATA_REG8(0xB482) = (XDATA_REG8(0xB482) & 0x0F) | 0xF0;

    /* B401 &= 0xFE — clear request, then B480 |= 0x01 (via 993D) */
    XDATA_REG8(0xB401) &= 0xFE;
    XDATA_REG8(0xB480) |= 0x01;

    /* B430 &= 0xFE — clear lane lock */
    XDATA_REG8(0xB430) &= 0xFE;

    /* B298 = (B298 & 0xEF) | 0x10 — set bit 4 */
    XDATA_REG8(0xB298) = (XDATA_REG8(0xB298) & 0xEF) | 0x10;

    /* ext[0x6043] = 0x70 — downstream port config */
    xdata_ext_write(0x6043, 0x70);

    /* ext[0x2543] |= 0x80 — set bit 7 */
    { uint8_t tmp = xdata_ext_read(0x2543);
    xdata_ext_write(0x2543, (tmp & 0x7F) | 0x80); }
}

/*
 * ltssm_transition - LTSSM state machine manipulation for link training
 * Bank 1 Address: 0xCCDD-0xCD26 (74 bytes) [actual addr: 0x14C48]
 *
 * Steps through LTSSM states by manipulating CC3F bits 1,2,5,6
 * with hardware timer waits between each step. Also clears CC3D bit 7.
 *
 * Original disassembly:
 *   ccdd: mov dptr, #0xcc3f
 *   cce0: movx a, @dptr
 *   cce1: anl a, #0xdf          ; clear bit 5
 *   cce3: movx @dptr, a
 *   cce4: movx a, @dptr
 *   cce5: anl a, #0xbf          ; clear bit 6
 *   cce7: movx @dptr, a
 *   cce8: hw_timer_delay(0, 0x00, 0x09)
 *   ccf1: movx a, @dptr
 *   ccf5: anl a, #0xfd          ; clear bit 1
 *   ccf7: helper: write, delay(0, 0x00, 0xF9), re-read
 *   ccfa: anl a, #0xdf          ; clear bit 5
 *   ccfc: orl a, #0x20          ; set bit 5
 *   ccfe: movx @dptr, a
 *   ccff: hw_timer_delay(1, 0x01, 0x67)
 *   cd08: movx a, @dptr
 *   cd0c: anl a, #0xfb          ; clear bit 2
 *   cd0e: helper: write, delay(0, 0x00, 0xF9), re-read
 *   cd11: anl a, #0xbf          ; clear bit 6
 *   cd13: orl a, #0x40          ; set bit 6
 *   cd15: movx @dptr, a
 *   cd16: hw_timer_delay(0, 0x00, 0xF9)
 *   cd1f: mov dptr, #0xcc3d
 *   cd22: movx a, @dptr
 *   cd23: anl a, #0x7f          ; clear bit 7
 *   cd25: movx @dptr, a
 *   cd26: ret
 */
static void ltssm_transition(void) {
    uint8_t val;

    /* Phase 1: Clear LTSSM override bits 5,6 */
    val = REG_LTSSM_CTRL;
    REG_LTSSM_CTRL = val & ~LTSSM_CTRL_OVERRIDE_EN;   /* clear bit 5 */
    val = REG_LTSSM_CTRL;
    REG_LTSSM_CTRL = val & ~LTSSM_CTRL_FORCE_STATE;   /* clear bit 6 */

    /* Phase 2: Short delay (~9 ticks) */
    hw_timer_delay(0x00, 0x00, 0x09);

    /* Phase 3: Clear bit 1 (write trigger), delay, re-read */
    val = REG_LTSSM_CTRL;
    REG_LTSSM_CTRL = val & ~LTSSM_CTRL_WRITE_TRIG;    /* clear bit 1 */
    hw_timer_delay(0x00, 0x00, 0xF9);                  /* ~249 ticks */
    val = REG_LTSSM_CTRL;                              /* re-read after delay */

    /* Phase 4: Set bit 5 (LTSSM override enable) */
    val = (val & ~LTSSM_CTRL_OVERRIDE_EN) | LTSSM_CTRL_OVERRIDE_EN;
    REG_LTSSM_CTRL = val;

    /* Phase 5: Longer delay */
    hw_timer_delay(0x01, 0x01, 0x67);                  /* prescaler=1, count=0x0167 (~359 ticks) */

    /* Phase 6: Clear bit 2 (state change trigger), delay, re-read */
    val = REG_LTSSM_CTRL;
    REG_LTSSM_CTRL = val & ~LTSSM_CTRL_STATE_TRIG;    /* clear bit 2 */
    hw_timer_delay(0x00, 0x00, 0xF9);                  /* ~249 ticks */
    val = REG_LTSSM_CTRL;                              /* re-read after delay */

    /* Phase 7: Set bit 6 (force LTSSM state) */
    val = (val & ~LTSSM_CTRL_FORCE_STATE) | LTSSM_CTRL_FORCE_STATE;
    REG_LTSSM_CTRL = val;

    /* Phase 8: Delay */
    hw_timer_delay(0x00, 0x00, 0xF9);                  /* ~249 ticks */

    /* Phase 9: Clear CC3D bit 7 (force/lock bit) */
    REG_LTSSM_STATE &= ~LTSSM_STATE_FORCE;
}

/*
 * phy_rxpll_config - Configure RXPLL settings before reset
 * Bank 1 Address: 0xE957-0xE988 (50 bytes) [actual addr: 0x168C2]
 *
 * Called before RstRxpll in PHY maintenance/recovery paths.
 * Configures E760/E761 PHY PLL registers and triggers E763 events.
 * Also sets C808 bit 1 to enable RXPLL reconfiguration.
 *
 * Original disassembly:
 *   e957: mov dptr, #0xc808
 *   e95a: lcall 0xd59d          ; @dptr = (@dptr & 0xFD) | 0x02 (set bit 1)
 *   e95d: mov dptr, #0xe761
 *   e960: mov a, #0xff
 *   e962: movx @dptr, a         ; E761 = 0xFF
 *   e963: mov dptr, #0xe760
 *   e966: movx a, @dptr
 *   e967: anl a, #0xfb          ; clear bit 2
 *   e969: orl a, #0x04          ; set bit 2
 *   e96b: movx @dptr, a         ; E760 bit 2 toggled (clear+set = no-op on bit 2)
 *   e96c: inc dptr              ; E761
 *   e96d: movx a, @dptr
 *   e96e: anl a, #0xfb          ; clear bit 2
 *   e970: movx @dptr, a         ; E761 &= ~0x04
 *   e971: mov dptr, #0xe760
 *   e974: movx a, @dptr
 *   e975: anl a, #0xf7          ; clear bit 3
 *   e977: orl a, #0x08          ; set bit 3
 *   e979: movx @dptr, a         ; E760 bit 3 toggled
 *   e97a: inc dptr              ; E761
 *   e97b: movx a, @dptr
 *   e97c: anl a, #0xf7          ; clear bit 3
 *   e97e: movx @dptr, a         ; E761 &= ~0x08
 *   e97f: mov dptr, #0xe763
 *   e982: mov a, #0x04
 *   e984: movx @dptr, a         ; E763 = 0x04 (trigger)
 *   e985: mov a, #0x08
 *   e987: movx @dptr, a         ; E763 = 0x08 (trigger)
 *   e988: ret
 */
static void phy_rxpll_config(void) {
    uint8_t val;

    /* C808: set bit 1 (RXPLL config trigger) */
    REG_PHY_RXPLL_CFG_TRIG = (REG_PHY_RXPLL_CFG_TRIG & ~PHY_RXPLL_CFG_TRIG_BIT1) | PHY_RXPLL_CFG_TRIG_BIT1;

    /* E761 = 0xFF */
    REG_PHY_RXPLL_CFG_B = 0xFF;

    /* E760: toggle bit 2 (clear then set = ensures bit 2 is set) */
    val = REG_PHY_RXPLL_CFG_A;
    REG_PHY_RXPLL_CFG_A = (val & ~0x04) | 0x04;

    /* E761: clear bit 2 */
    val = REG_PHY_RXPLL_CFG_B;
    REG_PHY_RXPLL_CFG_B = val & ~0x04;

    /* E760: toggle bit 3 */
    val = REG_PHY_RXPLL_CFG_A;
    REG_PHY_RXPLL_CFG_A = (val & ~0x08) | 0x08;

    /* E761: clear bit 3 */
    val = REG_PHY_RXPLL_CFG_B;
    REG_PHY_RXPLL_CFG_B = val & ~0x08;

    /* Trigger PLL reconfiguration events */
    REG_PHY_RXPLL_TRIGGER = 0x04;
    REG_PHY_RXPLL_TRIGGER = 0x08;
}

/*
 * pd_state_init - Initialize PHY/PD command engine state
 * Address: 0xB806-0xB8A8 (163 bytes)
 *
 * Clears command engine globals needed for PHY event processing.
 * Stock firmware calls this during major reset, speed change, and CDR recovery.
 *
 * Original disassembly:
 *   b80f: clr a
 *   b810: mov dptr, #0x07b4    ; clear 07B4, 07B5
 *   ...
 *   b816: mov dptr, #0x07c0    ; clear 07C0, 07C1
 *   ...
 *   b830: mov dptr, #0x07ba    ; 07BA = 1
 *   ...
 *   b835: E400 bit 6 -> 07D2 = 0x10 or 0x01
 *   b85b: clear 07DB, 07DC, 07B6-07B9, 07C5, 07BB
 *   b887: lcall 0xe0d6, 0xe6cf
 *   b890: 07DD = 5, 07DE = 0, 07DF = 0
 *   b898: 07D7 = 1, 07D8 = 0x2C, 07D9 = 0, 07DA = 0x64
 */
static void pd_state_init(void) {
    uint8_t val;

    /* Clear command engine state (stock: 0xB80F-0xB82F) */
    G_PD_STATE_07B4 = 0;
    G_PD_STATE_07B5 = 0;
    G_CMD_ADDR_LO = 0;       /* 07C0: command slot counter */
    G_CMD_SLOT_C1 = 0;       /* 07C1: command slot value */
    G_CMD_STATUS = 0;        /* 07C4 */
    G_CMD_WORK_C2 = 0;       /* 07C2 */
    G_CMD_ADDR_HI = 0;       /* 07BF */
    G_PD_STATE_07BE = 0;
    G_PD_STATE_07E0 = 0;

    /* Set init flag (stock: 0xB830) */
    G_PD_INIT_07BA = 1;

    /* E400 bit 6 determines PD mode (stock: 0xB835-0xB84D) */
    val = REG_CMD_CTRL_E400;
    if (val & 0x40) {
        G_PD_MODE_07D2 = 0x10;
    } else {
        G_PD_MODE_07D2 = 0x01;
    }

    /* If 07DB was 0, set 07C7=2 (stock: 0xB851-0xB859) */
    if (G_PD_COUNTER_07DB == 0) {
        G_CMD_WORK_C7 = 0x02;
    }

    /* Clear remaining state (stock: 0xB85A-0xB886) */
    G_PD_COUNTER_07DB = 0;
    G_PD_COUNTER_07DC = 0;
    G_PD_FLAG_07B6 = 0;
    G_CMD_ENGINE_SLOT = 0;   /* 07B7 */
    G_CMD_WORK_C5 = 0;       /* 07C5 */
    G_CMD_PENDING_07BB = 0;
    G_VENDOR_CTRL_07B9 = 0;
    G_FLASH_CMD_FLAG = 0;    /* 07B8 */

    /* Command parameters (stock: 0xB88D-0xB8A8) */
    G_CMD_LBA_3 = 0x05;     /* 07DD */
    G_CMD_FLAG_07DE = 0;
    G_PCIE_COMPLETE_07DF = 0;
}

/*
 * phy_clear_events - Clear all PHY events and disable event generation
 * Address: 0x947C-0x94AB (48 bytes)
 *
 * Writes 0xFF to E40F and E410 (W1C clear all), clears E40B bits 1-3,
 * configures CC88 timer, and writes CC89=1 to trigger reset.
 *
 * Original disassembly:
 *   947c: mov dptr, #0xe40f
 *   947f: mov a, #0xff
 *   9481: movx @dptr, a         ; E40F = 0xFF
 *   9482: inc dptr
 *   9483: movx @dptr, a         ; E410 = 0xFF
 *   9484: E40B: clear bits 1,2,3
 *   9493: CC88 = (CC88 & 0xF8) | 0x02
 *   949c: CC8A = 0x00, CC8B = 0xC7
 *   94a5: CC89 = 0x01
 */
static void phy_clear_events(void) {
    REG_PHY_EVENT_E40F = 0xFF;
    REG_PHY_INT_STATUS_E410 = 0xFF;

    REG_CMD_CONFIG &= ~0x02;   /* E40B: clear bit 1 */
    REG_CMD_CONFIG &= ~0x04;   /* E40B: clear bit 2 */
    REG_CMD_CONFIG &= ~0x08;   /* E40B: clear bit 3 */

    REG_XFER_DMA_CTRL = (REG_XFER_DMA_CTRL & 0xF8) | 0x02;  /* CC88 */
    REG_XFER_DMA_ADDR_LO = 0x00;                              /* CC8A */
    REG_XFER_DMA_ADDR_HI = 0xC7;                              /* CC8B */
    REG_XFER_DMA_CMD = 0x01;                                  /* CC89 */
}

/*
 * phy_enable_events - Enable PHY event generation
 * Address: 0x94EA-0x9505 (28 bytes)
 *
 * Sets CC89=2 (enable event timer), then sets E40B bits 1,2,3
 * to enable link change, speed change, and CDR events.
 *
 * Original disassembly:
 *   94ea: mov dptr, #0xcc89
 *   94ed: mov a, #0x02
 *   94ef: movx @dptr, a         ; CC89 = 2
 *   94f0: E40B: set bit 1, then bit 2, then bit 3
 */
static void phy_enable_events(void) {
    REG_XFER_DMA_CMD = 0x02;                                  /* CC89 = 2 */

    REG_CMD_CONFIG = (REG_CMD_CONFIG & ~0x02) | 0x02;  /* E40B: set bit 1 */
    REG_CMD_CONFIG = (REG_CMD_CONFIG & ~0x04) | 0x04;  /* E40B: set bit 2 */
    REG_CMD_CONFIG = (REG_CMD_CONFIG & ~0x08) | 0x08;  /* E40B: set bit 3 */
}

/*
 * phy_poll_cmd_ready - Check if PHY command engine is idle
 * Address: 0xDE5A-0xDE86 (45 bytes)
 *
 * Returns 0 if command engine ready, 1 if busy.
 * Checks E402 bits 1,2,3 and E41C bit 0.
 *
 * Original disassembly:
 *   de5a: E402 bit 1 -> return 1
 *   de64: E41C bit 0 -> return 1
 *   de6b: E402 bit 2 -> return 1
 *   de77: E402 bit 3 -> return 1
 *   de84: return 0
 */
static uint8_t phy_poll_cmd_ready(void) {
    uint8_t val;

    val = REG_CMD_STATUS_E402;
    if (val & 0x02) return 1;

    if (REG_CMD_BUSY_STATUS & CMD_BUSY_STATUS_BUSY) return 1;

    val = REG_CMD_STATUS_E402;
    if (val & 0x04) return 1;

    val = REG_CMD_STATUS_E402;
    if (val & 0x08) return 1;

    return 0;
}

/*
 * phy_command_submit - Submit PHY command and wait for completion
 * Address: 0xDFD6-0xDFFD (40 bytes)
 *
 * Polls until idle, writes G_CMD_SLOT_C1 to E403, triggers via E41C,
 * polls completion, advances slot counter.
 *
 * Original disassembly:
 *   dfd6: lcall 0xde5a         ; poll until ready
 *   dfdc: E403 = G_CMD_SLOT_C1
 *   dfe4: E41C |= 0x01         ; trigger
 *   dfe7: poll E41C bit 0 until clear
 *   dfee: G_CMD_ADDR_LO = (G_CMD_ADDR_LO + 1) & 0x07
 *   dff6: G_PD_STATE_07B4 = 0
 */
static void phy_command_submit(void) {
    /* Poll until command engine idle */
    while (phy_poll_cmd_ready()) { }

    /* Write command slot value to E403 */
    REG_CMD_CTRL_E403 = G_CMD_SLOT_C1;

    /* Trigger: set E41C bit 0 */
    REG_CMD_BUSY_STATUS = (REG_CMD_BUSY_STATUS & ~CMD_BUSY_STATUS_BUSY) | CMD_BUSY_STATUS_BUSY;

    /* Poll until E41C bit 0 clears (command complete) */
    while (REG_CMD_BUSY_STATUS & CMD_BUSY_STATUS_BUSY) { }

    /* Advance slot counter (mod 8) */
    G_CMD_ADDR_LO = (G_CMD_ADDR_LO + 1) & 0x07;

    /* Clear state */
    G_PD_STATE_07B4 = 0;
}

/*
 * phy_event_link_training - Handle link training event (E410 bit 6)
 * Bank 1 Address: 0xE1BE-0xE1DD (32 bytes) [actual addr: 0x16129]
 *
 * Each E410 bit 6 event advances the PCIe link training state machine
 * by one step. Decrements slot counter, merges into E421, submits command.
 *
 * Original disassembly:
 *   e1be: mov dptr, #0x07c0    ; G_CMD_ADDR_LO
 *   e1c1: movx a, @dptr
 *   e1c2: dec a
 *   e1c3: anl a, #0x07         ; wrap to 0-7
 *   e1c5: movx @dptr, a
 *   e1c6: read E421, save in r7
 *   e1cf: lcall 0x9643         ; a = 2 * G_CMD_ADDR_LO
 *   e1d4: orl a, r7            ; merge
 *   e1d9: write back to E421
 *   e1da: lcall 0xdfd6         ; phy_command_submit
 */
static void phy_event_link_training(void) {
    uint8_t slot, cmd_mode, slot_bits;

    /* Decrement and wrap command slot counter */
    slot = G_CMD_ADDR_LO;
    slot = (slot - 1) & 0x07;
    G_CMD_ADDR_LO = slot;

    /* Read command mode register */
    cmd_mode = REG_CMD_MODE_E421;

    /* Compute doubled slot index (0x9643: a = XDATA[07C0]; add a, acc) */
    slot_bits = G_CMD_ADDR_LO;
    slot_bits = slot_bits + slot_bits;

    /* Merge and write back */
    REG_CMD_MODE_E421 = cmd_mode | slot_bits;

    /* Submit command */
    phy_command_submit();
}

/*
 * phy_event_cdr_recovery - Handle CDR recovery event (E410 bit 5)
 * Bank 1 Address: 0xE5DF-0xE5EB (13 bytes) [actual addr: 0x1654A]
 *
 * Dispatches to full (0xDA52) or quick (0xBCA1) recovery based on
 * G_PD_COUNTER_07DB.
 *
 * Full recovery (DA52): clear events, poll CC89, re-enable, submit cmd.
 * Quick recovery (BCA1): check E302 lane count, partial reset E403-E405.
 *
 * Original disassembly:
 *   e5df: mov dptr, #0x07db
 *   e5e2: movx a, @dptr
 *   e5e3: jnz $e5e8           ; quick path if nonzero
 *   e5e5: ljmp 0xda52          ; full recovery
 *   e5e8: lcall 0xbca1         ; quick recovery
 */
static void phy_event_cdr_recovery(void) {
    if (G_PD_COUNTER_07DB == 0) {
        /* Full CDR recovery (stock: 0xDA52) */
        uart_puts("[CDR:F]");
        G_PD_COUNTER_07DB = 1;
        pd_state_init();
        phy_clear_events();
        while (!(REG_XFER_DMA_CMD & 0x02)) { }  /* poll CC89 bit 1 */
        phy_enable_events();
        G_PD_COUNTER_07DB = 1;
    } else {
        /* Quick CDR recovery (stock: 0xBCA1) */
        uint8_t lane_count = (REG_PHY_MODE_E302 & 0x30) >> 4;
        uart_puts("[CDR:Q]");
        if (lane_count != 3) {
            pd_state_init();
            phy_clear_events();
            while (!(REG_XFER_DMA_CMD & 0x02)) { }  /* poll CC89 bit 1 */
            phy_enable_events();

            REG_CMD_CTRL_E403 = 0x00;
            REG_CMD_CFG_E404 = 0x40;
            REG_CMD_CFG_E405 = (REG_CMD_CFG_E405 & 0xF8) | 0x05;
            REG_CMD_STATUS_E402 = (REG_CMD_STATUS_E402 & 0x1F) | 0x20;

            while (phy_poll_cmd_ready()) { }
            REG_CMD_BUSY_STATUS = (REG_CMD_BUSY_STATUS & ~CMD_BUSY_STATUS_BUSY) | CMD_BUSY_STATUS_BUSY;
            while (REG_CMD_BUSY_STATUS & CMD_BUSY_STATUS_BUSY) { }

            G_PD_COUNTER_07DC = 1;
        }
    }
}

/*
 * phy_event_major_handler - Handle E40F major events
 *
 * Simplified handler for E40F bit 7 (major reset), bit 0 (link change),
 * and bit 5 (speed change). All three paths clear events, poll CC89,
 * and re-enable events.
 *
 * Stock firmware addresses:
 *   bit 7: 0xDD9C (phy_event_major_reset)
 *   bit 0: 0x83D6 (phy_event_link_change)
 *   bit 5: 0xE19E (phy_event_speed_change)
 */
static void phy_event_major_handler(uint8_t event_bit) {
    uart_puts("[E40F:");
    uart_puthex(event_bit);
    uart_puts("]\n");

    /* NOTE: Stock firmware does pd_state_init + phy_clear_events + phy_enable_events
     * here, but this was causing USB resets. For now, just set the counter
     * and let the event system naturally re-arm. The key insight is that
     * these events ARE firing (CC controller enabled them), but the full
     * reset sequence was too disruptive during USB operation. */
    G_PD_COUNTER_07DB = 1;
}

/*
 * phy_event_dispatcher - PHY/PCIe event dispatcher
 * Address: 0xAE89-0xAF5B (211 bytes)
 *
 * Called from main loop when C80A bit 6 is set. Reads E40F and E410,
 * dispatches to handlers based on individual bits (priority order).
 * Also checks E314 and E661 for completion events.
 *
 * Gating condition (from caller at 0x9332):
 *   C80A bit 6 must be set (INT_PCIE_NVME_STATUS)
 *
 * Priority: E40F bit 7 > 0 > 5, then E410 bit 0 > 3 > 4 > 5 > 6 > 7
 *
 * Original disassembly:
 *   ae89-ae9a: UART debug output (CR/LF, debug prefix)
 *   ae9b: read E40F, display hex
 *   aea9: read E410, display hex
 *   aeb7: E40F bit 7 -> phy_event_major_reset (0xDD9C)
 *   aec6: E40F bit 0 -> ack, phy_event_link_change (0x83D6)
 *   aed5: E40F bit 5 -> ack, phy_event_speed_change (0xE19E)
 *   aee4: E410 bit 0 -> ack only
 *   aef0: E410 bit 3 -> ack only
 *   aefc: E410 bit 4 -> ack only
 *   af08: E410 bit 5 -> ack, phy_event_cdr_recovery (0xE5DF)
 *   af17: E410 bit 6 -> ack, phy_event_link_training (0xE1BE)
 *   af26: E410 bit 7 -> ack only
 *   af30: E314 bits 0,1,2 -> ack; E661 bit 7 -> ack
 */
static void phy_event_dispatcher(void) {
    uint8_t e40f, e410;

    /* Gate check: C80A bit 6 (stock: 0x9357)
     * NOTE: On stock firmware this is set by PCIe hardware when link active.
     * We skip the gate check since our link isn't trained yet — we need
     * events to CAUSE training, not wait for training to enable events. */

    /* Read event registers */
    e40f = REG_PHY_EVENT_E40F;
    e410 = REG_PHY_INT_STATUS_E410;

    /* Skip if no events pending */
    if (!e40f && !e410) return;

    /* === E40F dispatch (higher priority) === */

    /* E40F bit 7: Major PHY event (stock: -> 0xDD9C) */
    if (REG_PHY_EVENT_E40F & PHY_EVENT_MAJOR) {
        phy_event_major_handler(0x80);
        REG_PHY_INT_STATUS_E410 = PHY_INT_MAJOR_ERROR;  /* ack E410 bit 7 */
        goto end_checks;
    }

    /* E40F bit 0: Link state change (stock: -> 0x83D6) */
    if (REG_PHY_EVENT_E40F & PHY_EVENT_LINK_CHANGE) {
        REG_PHY_EVENT_E40F = PHY_EVENT_LINK_CHANGE;     /* W1C ack */
        phy_event_major_handler(0x01);
        goto end_checks;
    }

    /* E40F bit 5: Speed change (stock: -> 0xE19E) */
    if (REG_PHY_EVENT_E40F & PHY_EVENT_SPEED_CHANGE) {
        REG_PHY_EVENT_E40F = PHY_EVENT_SPEED_CHANGE;    /* W1C ack */
        phy_event_major_handler(0x20);
        goto end_checks;
    }

    /* === E410 dispatch (lower priority) === */

    /* E410 bit 0: Minor event (ack only) */
    if (REG_PHY_INT_STATUS_E410 & PHY_INT_MINOR_EVENT) {
        REG_PHY_INT_STATUS_E410 = PHY_INT_MINOR_EVENT;
        goto end_checks;
    }

    /* E410 bit 3: CDR timeout (ack only) */
    if (REG_PHY_INT_STATUS_E410 & PHY_INT_CDR_TIMEOUT) {
        REG_PHY_INT_STATUS_E410 = PHY_INT_CDR_TIMEOUT;
        goto end_checks;
    }

    /* E410 bit 4: PLL event (ack only) */
    if (REG_PHY_INT_STATUS_E410 & PHY_INT_PLL_EVENT) {
        REG_PHY_INT_STATUS_E410 = PHY_INT_PLL_EVENT;
        goto end_checks;
    }

    /* E410 bit 5: CDR recovery needed (stock: -> 0xE5DF) */
    if (REG_PHY_INT_STATUS_E410 & PHY_INT_CDR_RECOVERY) {
        REG_PHY_INT_STATUS_E410 = PHY_INT_CDR_RECOVERY;
        phy_event_cdr_recovery();
        goto end_checks;
    }

    /* E410 bit 6: Link training event (stock: -> 0xE1BE) */
    if (REG_PHY_INT_STATUS_E410 & PHY_INT_LINK_TRAINING) {
        REG_PHY_INT_STATUS_E410 = PHY_INT_LINK_TRAINING;
        phy_event_link_training();
        goto end_checks;
    }

    /* E410 bit 7: Major error (ack only) */
    if (REG_PHY_INT_STATUS_E410 & PHY_INT_MAJOR_ERROR) {
        REG_PHY_INT_STATUS_E410 = PHY_INT_MAJOR_ERROR;
    }

end_checks:
    /* Final: ack E314 and E661 pending events (stock: 0xAF30-0xAF5B) */
    if (REG_DEBUG_STATUS_E314 & 0x01) { REG_DEBUG_STATUS_E314 = 0x01; return; }
    if (REG_DEBUG_STATUS_E314 & 0x02) { REG_DEBUG_STATUS_E314 = 0x02; return; }
    if (REG_DEBUG_STATUS_E314 & 0x04) { REG_DEBUG_STATUS_E314 = 0x04; return; }
    if (REG_DEBUG_INT_E661 & DEBUG_INT_E661_FLAG) {
        REG_DEBUG_INT_E661 = DEBUG_INT_E661_FLAG;
    }
}

/*
 * cc_analog_config - Configure CC pin analog front-end for USB-C detection
 * Address: 0xD7FD-0xD83A (62 bytes)
 *
 * Configures E401, E406, E407, E408 registers for CC1/CC2 pin detection.
 * Sets voltage references, bias currents, and termination resistors.
 * Only performs configuration if E40B bit 7 reads back as set (CC hardware present).
 *
 * Original disassembly:
 *   d7fd: mov dptr, #0xe40b
 *   d800: movx a, @dptr
 *   d801: anl a, #0x7f         ; clear bit 7
 *   d803: orl a, #0x80         ; set bit 7
 *   d805: movx @dptr, a        ; E40B |= 0x80
 *   d806: movx a, @dptr        ; re-read
 *   d807: anl a, #0x80         ; isolate bit 7
 *   d80c: jz 0xd83a            ; skip if bit 7 clear (no CC hardware)
 *   d80e-d81c: E401 config (bits 0-2 = 0x04, bits 3-7 = 0xB0)
 *   d81d-d82b: E406 config (bits 0-3 = 0x06, bits 4-7 = 0xA0)
 *   d82c-d832: E407 = (E407 & 0xE0) | 0x15
 *   d833-d839: E408 = (E408 & 0xE0) | 0x1C
 *   d83a: ret
 */
static void cc_analog_config(void) {
    uint8_t val;

    /* E40B: set bit 7 (enable CC analog) */
    val = REG_CMD_CONFIG;
    REG_CMD_CONFIG = (val & 0x7F) | 0x80;

    /* Re-read: check if bit 7 stuck (CC hardware present) */
    val = REG_CMD_CONFIG;
    if (!(val & 0x80)) return;  /* No CC hardware, skip */

    /* E401: CC1 detect mode + voltage reference */
    val = XDATA_REG8(0xE401);
    XDATA_REG8(0xE401) = (val & 0xF8) | 0x04;   /* bits 0-2 = 4 */
    val = XDATA_REG8(0xE401);
    XDATA_REG8(0xE401) = (val & 0x07) | 0xB0;   /* bits 3-7 = 0xB0 */

    /* E406: CC2 detect mode + voltage reference */
    val = XDATA_REG8(0xE406);
    XDATA_REG8(0xE406) = (val & 0xF0) | 0x06;   /* bits 0-3 = 6 */
    val = XDATA_REG8(0xE406);
    XDATA_REG8(0xE406) = (val & 0x0F) | 0xA0;   /* bits 4-7 = 0xA0 */

    /* E407: CC bias current */
    val = XDATA_REG8(0xE407);
    XDATA_REG8(0xE407) = (val & 0xE0) | 0x15;

    /* E408: CC termination resistor */
    val = XDATA_REG8(0xE408);
    XDATA_REG8(0xE408) = (val & 0xE0) | 0x1C;
}

/*
 * pd_cc_controller_init - Initialize CC controller for USB-C PD detection
 * Address: 0xB02F-0xB0FD (207 bytes)
 *
 * Full CC controller initialization sequence that enables CC pin detection.
 * Must be called before reading E302 bits[5:4] for CC state.
 * Configures the E400-E413 command engine for CC detection, sets up
 * the CC analog front-end, and polls E302 bits[7:6] until CC is detected.
 *
 * Register writes (in order):
 *   E40B |= 0x40 (set bit 6)
 *   E40A = 0x0F
 *   E413: clear bits 0,1
 *   E400: clear bit 7
 *   CC88 &= 0xF8, CC8A = 0 (clear CC DMA)
 *   CC8B = 0x0A, CC89 = 0x01 (trigger), poll CC89 bit 1, CC89 = 0x02 (ack)
 *   E40B |= 0x01 (set bit 0)
 *   CC88 &= 0xF8, CC8A = 0 (clear again)
 *   CC8B = 0x3C, CC89 = 0x01 (trigger), poll CC89 bit 1, CC89 = 0x02 (ack)
 *   Poll E402 bit 3 until clear
 *   E409: clear bit 0, set bit 6
 *   E420 = 0x40, E409 = (E409 & 0xF1) | 0x06
 *   E400 |= 0x40 (set bit 6)
 *   E411 = 0xA1, E412 = 0x79
 *   E400 = (E400 & 0xC3) | 0x3C (set bits 2-5)
 *   E409: clear bit 7
 *   C809 |= 0x20 (CC interrupt enable)
 *   cc_analog_config() — E401, E406, E407, E408
 *   E40E = 0x8A
 *   Poll E302 bits[7:6] until nonzero
 *   E400 |= 0x80 (enable CC)
 *   E40B: clear bit 0
 *   E66A: clear bit 4
 *   E40D = 0x28
 *   E413 = (E413 & 0x8F) | 0x60 (set bits 5,6)
 *
 * Original disassembly:
 *   b02f: mov dptr, #0xe40b
 *   b032: lcall 0x967c         ; E40B |= 0x40
 *   ...
 *   b0fd: ret
 */
static void pd_cc_controller_init(void) {
    uint8_t val;

    /* E40B: set bit 6 */
    val = REG_CMD_CONFIG;
    REG_CMD_CONFIG = (val & ~0x40) | 0x40;

    /* E40A = 0x0F */
    REG_CMD_CFG_E40A = 0x0F;

    /* E413: clear bits 0,1 */
    val = XDATA_REG8(0xE413);
    XDATA_REG8(0xE413) = val & 0xFE;
    val = XDATA_REG8(0xE413);
    XDATA_REG8(0xE413) = val & 0xFD;

    /* E400: clear bit 7 (disable CC before reconfiguring) */
    val = REG_CMD_CTRL_E400;
    REG_CMD_CTRL_E400 = val & 0x7F;

    /* CC DMA clear: CC88 &= 0xF8, CC8A = 0 */
    REG_XFER_DMA_CTRL = REG_XFER_DMA_CTRL & 0xF8;
    REG_XFER_DMA_ADDR_LO = 0x00;

    /* Write 0x0A to CC8B (inc dptr from CC8A), trigger CC89=1, poll bit 1, ack */
    REG_XFER_DMA_ADDR_HI = 0x0A;   /* CC8B = 0x0A */
    REG_XFER_DMA_CMD = 0x01;       /* CC89 = 1 (trigger) */
    while (!(REG_XFER_DMA_CMD & 0x02)) { }  /* poll CC89 bit 1 */
    REG_XFER_DMA_CMD = 0x02;       /* CC89 = 2 (ack) */

    /* E40B: set bit 0 */
    val = REG_CMD_CONFIG;
    REG_CMD_CONFIG = (val & 0xFE) | 0x01;

    /* CC DMA clear again */
    REG_XFER_DMA_CTRL = REG_XFER_DMA_CTRL & 0xF8;
    REG_XFER_DMA_ADDR_LO = 0x00;

    /* Write 0x3C to CC8B, trigger, poll, ack */
    REG_XFER_DMA_ADDR_HI = 0x3C;   /* CC8B = 0x3C */
    REG_XFER_DMA_CMD = 0x01;       /* CC89 = 1 (trigger) */
    while (!(REG_XFER_DMA_CMD & 0x02)) { }  /* poll CC89 bit 1 */
    REG_XFER_DMA_CMD = 0x02;       /* CC89 = 2 (ack) */

    /* Poll E402 bit 3 until clear */
    while (REG_CMD_STATUS_E402 & 0x08) { }

    /* E409: clear bit 0, then set bit 6 */
    val = XDATA_REG8(0xE409);
    XDATA_REG8(0xE409) = val & 0xFE;
    val = XDATA_REG8(0xE409);
    XDATA_REG8(0xE409) = (val & ~0x40) | 0x40;

    /* E420 = 0x40, E409 = (E409 & 0xF1) | 0x06 */
    XDATA_REG8(0xE420) = 0x40;
    val = XDATA_REG8(0xE409);
    XDATA_REG8(0xE409) = (val & 0xF1) | 0x06;

    /* E400: set bit 6 */
    val = REG_CMD_CTRL_E400;
    REG_CMD_CTRL_E400 = (val & ~0x40) | 0x40;

    /* E411 = 0xA1, E412 = 0x79 — CC comparison thresholds */
    XDATA_REG8(0xE411) = 0xA1;
    XDATA_REG8(0xE412) = 0x79;

    /* E400: set bits 2-5 (CC mode selection) */
    val = REG_CMD_CTRL_E400;
    REG_CMD_CTRL_E400 = (val & 0xC3) | 0x3C;

    /* E409: clear bit 7 */
    val = XDATA_REG8(0xE409);
    XDATA_REG8(0xE409) = val & 0x7F;

    /* C809: set bit 5 (CC interrupt enable)
     * Enables CC/PD interrupt on INT1 (checked via C806 bit 0).
     * int1_isr must ack CC23/CC81/CC91/CCD9/CCF9 to prevent re-trigger. */
    REG_INT_CTRL = (REG_INT_CTRL & ~0x20) | 0x20;

    /* Configure CC analog front-end: E401, E406, E407, E408 */
    cc_analog_config();

    /* E40E = 0x8A (CC detection timing) */
    XDATA_REG8(0xE40E) = 0x8A;

    /* Poll E302 bits[7:6] until nonzero (CC detected)
     * Stock firmware does an infinite poll here (no timeout).
     * We use a very long timeout to match. */
    uart_puts("[CC poll]\n");
    while (1) {
        val = REG_PHY_MODE_E302;
        if ((val & 0xC0) != 0) break;
    }
    uart_puts("[E302=");
    uart_puthex(val);
    uart_puts("]\n");

    /* E400: set bit 7 (enable CC) */
    val = REG_CMD_CTRL_E400;
    REG_CMD_CTRL_E400 = (val & 0x7F) | 0x80;

    /* E40B: clear bit 0 */
    val = REG_CMD_CONFIG;
    REG_CMD_CONFIG = val & 0xFE;

    /* E66A: clear bit 4 */
    REG_PD_CTRL_E66A = REG_PD_CTRL_E66A & ~PD_CTRL_E66A_BIT4;

    /* E40D = 0x28 */
    REG_CMD_CFG_E40D = 0x28;

    /* E413: set bits 5,6 */
    val = XDATA_REG8(0xE413);
    XDATA_REG8(0xE413) = (val & 0x8F) | 0x60;
}

/*
 * pd_cc_clear_registers - Clear PD command parameter area E420-E43F
 * Address: 0xE4A0-0xE4B3 (20 bytes)
 *
 * Zeros 32 bytes from 0xE420 to 0xE43F to reset the command parameter area
 * before setting up a new PD command. Called from Drive_HardRst sequence.
 *
 * Original disassembly:
 *   e4a0: clr a
 *   e4a1: mov r7, a            ; R7 = 0
 *   e4a2: mov a, #0x20
 *   e4a4: add a, r7            ; A = 0x20 + R7
 *   e4a5: mov 0x82, a          ; DPL = A
 *   e4a7: clr a
 *   e4a8: addc a, #0xe4        ; DPH = 0xE4
 *   e4aa: mov 0x83, a
 *   e4ac: clr a
 *   e4ad: movx @dptr, a        ; XDATA[0xE420+R7] = 0
 *   e4ae: inc r7
 *   e4af: mov a, r7
 *   e4b0: cjne a, #0x20, 0xe4a2
 *   e4b3: ret
 */
static void pd_cc_clear_registers(void) {
    uint8_t i;
    for (i = 0; i < 0x20; i++) {
        XDATA_REG8(0xE420 + i) = 0x00;
    }
}

/*
 * pd_cc_state_check - Check CC state and drive hard reset if needed
 * Address: 0xBCA1-0xBD10 (112 bytes)
 *
 * Reads E302 bits [5:4] for CC state:
 *   - If CC state == 3 (CC pins open): skip hard reset (device not connected)
 *   - Otherwise: perform full PD CC hard reset sequence
 *
 * The hard reset sequence:
 *   1. Clear command parameter area (E420-E43F)
 *   2. Re-init PD state variables
 *   3. Clear PHY events, wait for CC89 ready, enable events
 *   4. Set E403=0x00, E404=0x40, E405=(E405&0xF8)|0x05 — CC hard reset signal config
 *   5. Set E402 bit 5 — trigger CC signaling
 *   6. Wait for command engine idle, trigger E41C, wait completion
 *   7. Set G_PD_COUNTER_07DC = 1
 *
 * Original disassembly:
 *   bca1: mov dptr, #0xe302
 *   bca4: movx a, @dptr
 *   bca5: anl a, #0x30         ; mask bits 4-5
 *   bca7: mov r7, a
 *   bca8: swap a
 *   bca9: anl a, #0x0f         ; CC state = bits[5:4] as 0-3
 *   bcab: xrl a, #0x03         ; check if == 3
 *   bcad: jz 0xbd01            ; if CC_state==3, skip (CCOpen)
 *   bcbe: lcall 0xe4a0         ; pd_cc_clear_registers
 *   bcc1: lcall 0xb806         ; pd_state_init
 *   bcc4: lcall 0x947c         ; phy_clear_events
 *   bcc7: poll CC89 bit 1
 *   bcce: lcall 0x94ea         ; phy_enable_events
 *   bcd1: E403 = 0x00
 *   bcd7: E404 = 0x40
 *   bcdb: E405 = (E405 & 0xF8) | 0x05
 *   bce1: E402 = (E402 & 0x1F) | 0x20
 *   bcea: poll phy_poll_cmd_ready until idle
 *   bcf0: lcall 0x9558 (E41C |= 0x01 trigger)
 *   bcf3: poll E41C bit 0 until clear
 *   bcfa: G_PD_COUNTER_07DC = 1
 *   bd00: ret
 */
static void pd_cc_state_check(void) {
    uint8_t cc_raw, cc_state;

    /* Read CC state from E302 bits [5:4] */
    cc_raw = REG_PHY_MODE_E302 & 0x30;
    cc_state = (cc_raw >> 4) & 0x0F;

    uart_puts("[CC=");
    uart_puthex(cc_state);

    if (cc_state == 3) {
        /* CC pins open — no device connected, skip hard reset */
        uart_puts(":open]\n");
        return;
    }

    /* CC connected — perform PD hard reset */
    uart_puts(":HardRst]\n");

    /* Step 1: Clear command parameter area (0xE4A0) */
    pd_cc_clear_registers();

    /* Step 2: Re-init PD state (0xB806) */
    pd_state_init();

    /* Step 3: Clear events, wait ready, enable events */
    phy_clear_events();
    while (!(REG_XFER_DMA_CMD & 0x02)) { }  /* poll CC89 bit 1 */
    phy_enable_events();

    /* Step 4: Configure CC hard reset signal (0xBCD1-0xBCE9) */
    REG_CMD_CTRL_E403 = 0x00;
    REG_CMD_CFG_E404 = 0x40;
    REG_CMD_CFG_E405 = (REG_CMD_CFG_E405 & 0xF8) | 0x05;

    /* Step 5: Trigger CC signaling — set E402 bit 5 */
    REG_CMD_STATUS_E402 = (REG_CMD_STATUS_E402 & 0x1F) | 0x20;

    /* Step 6: Wait for command engine idle, then trigger */
    while (phy_poll_cmd_ready()) { }

    /* Trigger: E41C |= 0x01 (stock: cmd_start_trigger at 0x9558) */
    REG_CMD_BUSY_STATUS = (REG_CMD_BUSY_STATUS & ~CMD_BUSY_STATUS_BUSY) | CMD_BUSY_STATUS_BUSY;

    /* Wait for E41C bit 0 to clear (command complete) */
    while (REG_CMD_BUSY_STATUS & CMD_BUSY_STATUS_BUSY) { }

    /* Step 7: Set done flag */
    G_PD_COUNTER_07DC = 1;

    uart_puts("[HardRst done]\n");
}

/*
 * pcie_serdes_lane_enable - Per-lane SerDes PHY bit 7 configuration
 * Address: 0xD530-0xD573 (68 bytes)
 *
 * For each lane (bits 0-3 of lane_mask), sets or clears bit 7 of
 * page2 register (0x78+lane)*256 + 0xAF. Bit 7 enables the lane
 * for PCIe link training.
 *
 * Original disassembly:
 *   d530: lcall 0xcaee        ; read page2:0x78AF
 *   d533: lcall 0xcaf7        ; a = a & 0x7F, r6 = result, load r7 for bit test
 *   d536: jnb acc.0, d53b     ; if lane 0 enabled → r5 = 1
 *   ...                       ; repeat for lanes 1, 2, 3
 *   d571: ljmp 0x0bbe         ; write page2:0x7BAF
 */
static void pcie_serdes_lane_enable(uint8_t lane_mask) {
    uint8_t i, val;
    static __code const uint8_t lane_pages[4] = {0x78, 0x79, 0x7A, 0x7B};

    for (i = 0; i < 4; i++) {
        uint16_t addr = ((uint16_t)lane_pages[i] << 8) | 0xAF;
        val = xdata_ext_read(addr);
        if (lane_mask & (1 << i))
            val = (val & 0x7F) | 0x80;  /* set bit 7 (enable lane) */
        else
            val = val & 0x7F;           /* clear bit 7 (disable lane) */
        xdata_ext_write(addr, val);
    }
}

/*
 * pcie_progressive_lane_enable - Progressive lane enable with PHY config
 * Address: 0xBEA0-0xBF1B (BEA0 loop) + 0xC7A4-0xC808 (C7A4 wrapper)
 *
 * BEA0: Enables lanes one at a time, calling D530 (per-lane PHY config)
 *       and a hardware timer delay between each step.
 * C7A4: Wrapper that saves/restores B402 bit 1, calls BEA0, configures B436.
 *
 * BEA0 original disassembly:
 *   bea0: XDATA[0x0aa9] = r7        ; target mask
 *         XDATA[0x0aac] = 0x01      ; shift register
 *         XDATA[0x0aab] = B434 & 0x0F ; current lane state
 *         XDATA[0x0aaa] = 0          ; iteration counter
 *   beba: (loop) merge shift into state, write B434, call D530, delay(E581), shift<<=1, iter++
 *         if iter < 4: loop
 *
 * C7A4 original disassembly:
 *   c7a4: [0x0aa7] = r7; [0x0aa8] = B402 & 0x02; B402 &= 0xFD
 *   c7c8: lcall BEA0
 *   c7e8: B436 = (B436 & 0xF0) | (r7 & 0x0E)
 *         B436 = (B436 & 0x0F) | ((~B404 & 0x0F) << 4)
 */
static void pcie_progressive_lane_enable(uint8_t target_mask) {
    uint8_t saved_b402_bit1;
    uint8_t current_mask, shift, iter;

    /* C7A4: save B402 bit 1, then clear it */
    saved_b402_bit1 = REG_PCIE_CTRL_B402 & 0x02;
    REG_PCIE_CTRL_B402 &= 0xFD;

    /* BEA0: progressive lane enable loop */
    current_mask = REG_PCIE_LINK_STATE & 0x0F;
    shift = 0x01;

    for (iter = 0; iter < 4; iter++) {
        if (current_mask == target_mask) break;

        /* Add next lane: merge shift bit into current mask */
        current_mask = (current_mask | shift) & target_mask;

        /* Write new lane mask to B434 (preserve high nibble) */
        REG_PCIE_LINK_STATE = (REG_PCIE_LINK_STATE & 0xF0) | current_mask;

        /* D530: configure PHY bit 7 for each enabled lane */
        pcie_serdes_lane_enable(current_mask);

        /* E581: hardware timer delay (prescaler=2, count=0x00C7 = 199 ticks) */
        hw_timer_delay(0x02, 0x00, 0xC7);

        /* Shift to next lane */
        shift += shift;  /* shift <<= 1 */
    }

    /* C7A4: restore B402 bit 1 if it was set */
    if (saved_b402_bit1)
        REG_PCIE_CTRL_B402 |= 0x02;

    /* C7A4: configure B436 with final lane state */
    REG_PCIE_LANE_CONFIG = (REG_PCIE_LANE_CONFIG & 0xF0) | (target_mask & 0x0E);
    { uint8_t inv = ((REG_PCIE_LINK_PARAM_B404 & 0x0F) ^ 0x0F) << 4;
    REG_PCIE_LANE_CONFIG = (REG_PCIE_LANE_CONFIG & 0x0F) | (inv & 0xF0); }
}

/*
 * pcie_phy_e764_config - Configure E764 PHY timer control
 * Address: 0xE2E6-0xE2FF (26 bytes)
 *
 * Sequential RMW operations on E764: clear bits 0,1,3; set bit 2.
 * Re-reads between writes ensure hardware register ordering.
 *
 * Original disassembly:
 *   e2e6: mov a, r7             ; r7 = lane mask (0x0F)
 *   e2e7: jnb 0xe0.0, 0xe2ff   ; skip if bit 0 not set
 *   e2ea: mov dptr, #0xe764
 *   e2ed: movx a, @dptr         ; read E764
 *   e2ee: anl a, #0xfd          ; clear bit 1
 *   e2f0: movx @dptr, a
 *   e2f1: movx a, @dptr         ; re-read
 *   e2f2: anl a, #0xfe          ; clear bit 0
 *   e2f4: movx @dptr, a
 *   e2f5: movx a, @dptr         ; re-read
 *   e2f6: anl a, #0xf7          ; clear bit 3
 *   e2f8: movx @dptr, a
 *   e2f9: movx a, @dptr         ; re-read
 *   e2fa: anl a, #0xfb          ; clear bit 2
 *   e2fc: orl a, #0x04          ; set bit 2
 *   e2fe: movx @dptr, a
 *   e2ff: ret
 */
static void pcie_phy_e764_config(void) {
    uint8_t val;
    val = REG_PHY_TIMER_CTRL_E764; val &= 0xFD; REG_PHY_TIMER_CTRL_E764 = val;
    val = REG_PHY_TIMER_CTRL_E764; val &= 0xFE; REG_PHY_TIMER_CTRL_E764 = val;
    val = REG_PHY_TIMER_CTRL_E764; val &= 0xF7; REG_PHY_TIMER_CTRL_E764 = val;
    val = REG_PHY_TIMER_CTRL_E764; val = (val & 0xFB) | 0x04; REG_PHY_TIMER_CTRL_E764 = val;
}

/*
 * pcie_link_width_config - Configure PCIe link width and per-lane SerDes
 * Address: 0xD45E-0xD4A3 (70 bytes)
 *
 * Sets B432 link width, B404 lane param, and per-lane E76C/E774/E77C bit 4.
 * When lane_param=1, only lane 0 is primary (bits 1,2,3 of param are 0),
 * so E76C/E774/E77C bit 4 are all cleared.
 *
 * Original disassembly:
 *   d45e: B432 = (B432 & 0xF8) | 0x07
 *   d467: B404 = (B404 & 0xF0) | r7
 *   d472: if r7 != 1: return
 *   d474-d4a2: per-lane E76C/E774/E77C bit 4 based on r7 bits 1-3
 *   d4a3: ret
 */
static void pcie_link_width_config(uint8_t lane_param) {
    REG_POWER_CTRL_B432 = (REG_POWER_CTRL_B432 & 0xF8) | 0x07;
    REG_PCIE_LINK_PARAM_B404 = (REG_PCIE_LINK_PARAM_B404 & 0xF0) | lane_param;

    if (lane_param != 0x01) return;

    /* Per-lane SerDes bit 4 config based on lane_param bits 1-3.
     * With lane_param=1, bits 1,2,3 are all 0 → clear bit 4 on all. */
    { uint8_t val;
    val = REG_SYS_CTRL_E76C; val = (val & 0xEF) | ((lane_param & 0x02) ? 0x10 : 0x00); REG_SYS_CTRL_E76C = val;
    val = REG_SYS_CTRL_E774; val = (val & 0xEF) | ((lane_param & 0x04) ? 0x10 : 0x00); REG_SYS_CTRL_E774 = val;
    val = REG_SYS_CTRL_E77C; val = (val & 0xEF) | ((lane_param & 0x08) ? 0x10 : 0x00); REG_SYS_CTRL_E77C = val;
    }
}

/*
 * pcie_serdes_full_config - Full SerDes lane configuration
 * Address: 0xE049-0xE06A (34 bytes)
 *
 * Calls D530(0x0F) to set bit 7 on all 4 lanes, then sets bit 6 on all 4 lanes.
 * Net effect: bits 6 and 7 of page2:0x78AF-0x7BAF are all set.
 *
 * Original disassembly:
 *   e049: mov r7, #0x0f; lcall D530  ; enable all lanes bit 7
 *   e04e: lcall caee                 ; read page2:0x78AF
 *   e051-e06a: (a & 0xBF) | 0x40 → write each lane, auto-increment
 */
static void pcie_serdes_full_config(void) {
    uint8_t i, val;
    static __code const uint8_t lane_pages[4] = {0x78, 0x79, 0x7A, 0x7B};

    /* D530: set bit 7 on all 4 lanes */
    pcie_serdes_lane_enable(0x0F);

    /* Set bit 6 on all 4 lanes */
    for (i = 0; i < 4; i++) {
        uint16_t addr = ((uint16_t)lane_pages[i] << 8) | 0xAF;
        val = xdata_ext_read(addr);
        val = (val & 0xBF) | 0x40;  /* clear bit 6, then set bit 6 (match stock) */
        xdata_ext_write(addr, val);
    }
}

/*
 * pcie_tunnel_adapter_config - Configure tunnel adapter registers
 * Address: D9A4 area - tunnel config sub-sequence
 *
 * Writes VID/PID-like config to B41x/B42x tunnel registers.
 * Called twice by stock firmware: once before and once after link up attempt.
 */
static void pcie_tunnel_adapter_config(void) {
    /* Path A: VID=0x1B21, credits=0x2464 */
    REG_TUNNEL_CFG_A_LO   = 0x1B;
    REG_TUNNEL_CFG_A_HI   = 0x21;
    REG_TUNNEL_DATA_LO    = 0x1B;
    REG_TUNNEL_DATA_HI    = 0x21;
    REG_TUNNEL_CREDITS     = 0x24;
    REG_TUNNEL_CFG_MODE    = 0x64;
    REG_TUNNEL_STATUS_0    = 0x24;
    REG_TUNNEL_STATUS_1    = 0x64;
    REG_TUNNEL_CAP_0       = 0x06;
    REG_TUNNEL_CAP_1       = 0x04;
    REG_TUNNEL_CAP_2       = 0x00;
    REG_TUNNEL_CAP2_0      = 0x06;
    REG_TUNNEL_CAP2_1      = 0x04;
    REG_TUNNEL_CAP2_2      = 0x00;

    /* Path B: link config */
    REG_TUNNEL_LINK_CFG_LO = 0x1B;
    REG_TUNNEL_LINK_CFG_HI = 0x21;
    REG_TUNNEL_AUX_CFG_LO  = 0x1B;
    REG_TUNNEL_AUX_CFG_HI  = 0x21;
    REG_TUNNEL_PATH_CREDITS = 0x24;
    REG_TUNNEL_PATH_MODE    = 0x64;
    REG_TUNNEL_PATH2_CRED   = 0x24;
    REG_TUNNEL_PATH2_MODE   = 0x64;
}

/*
 * pcie_link_up_attempt - Perform one link-up sequence
 * From stock firmware proxy trace on real hardware (cycles 86088-86142 / 86940-86994)
 *
 * Sequence: B401=1, B482=F1, B482=F1, B401=0, B480=1, B430=4, B298=0x11
 */
/*
 * pcie_link_up_attempt - Perform one link-up sequence
 * Stock firmware CC83 at 0xCC9B-0xCCC0:
 *   B401 |= 0x01, B482 |= 0x01, B482 = (B482 & 0x0F) | 0xF0,
 *   B401 &= 0xFE (via helper 993E), B480 |= 0x01, B430 &= 0xFE,
 *   B298 = (B298 & 0xEF) | 0x10
 */
static void pcie_link_up_attempt(void) {
    XDATA_REG8(0xB401) |= 0x01;
    XDATA_REG8(0xB482) |= 0x01;
    XDATA_REG8(0xB482) = (XDATA_REG8(0xB482) & 0x0F) | 0xF0;
    XDATA_REG8(0xB401) &= 0xFE;
    XDATA_REG8(0xB480) |= 0x01;
    XDATA_REG8(0xB430) &= 0xFE;
    XDATA_REG8(0xB298) = (XDATA_REG8(0xB298) & 0xEF) | 0x10;
}

/*
 * pcie_init - PCIe downstream link initialization
 *
 * Exact sequence captured from stock firmware emulator trace.
 * Phases:
 *   1. Clear/configure base registers (E7E3, B402, B432, B404)
 *   2. Progressive lane enable (B434: 0x01→0x03→0x07→0x0F with delays)
 *   3. Lane config (B436)
 *   4. PHY config (C65B, C656, C62D)
 *   5. PHY init (CD31, CD30-CD33, C655, C620, C65A)
 *   6. Tunnel adapter config (B41x/B42x)
 *   7. Link up attempt #1 (B401/B482/B480)
 *   8. DMA engine config (B264-B281, CEEFx)
 *   9. Link up attempt #2
 */
static void pcie_init(void) {
    uart_puts("[PCIe init]\n");

    /* Phase 1: Clear/configure base registers
     * Stock firmware (emulator trace cycles 27096-27175):
     *   E7E3=0x00, B402=0x00, C659=0x00, B432=0x07, B404=0x01, B402=0x00
     * NOTE: B402=0x00 in stock, NOT 0x01. Bit 0 must stay clear. */
    REG_PHY_LINK_CTRL = 0x00;             /* E7E3 = 0x00 */
    REG_PCIE_LANE_CTRL_C659 = 0x00;       /* C659 = 0x00 */
    REG_POWER_CTRL_B432 = 0x07;           /* B432 = 0x07 */
    REG_PCIE_LINK_PARAM_B404 = 0x01;      /* B404 = 0x01 */

    /* Phase 2: Progressive lane enable with delays */
    REG_PCIE_LINK_STATE = 0x01;
    delay_short();
    REG_PCIE_LINK_STATE = 0x03;
    delay_short();
    REG_PCIE_LINK_STATE = 0x07;
    delay_short();
    REG_PCIE_LINK_STATE = 0x0F;
    delay_short();

    /* Phase 3: Lane configuration */
    REG_PCIE_LANE_CONFIG = 0x0E;
    REG_PCIE_LANE_CONFIG = 0xEE;

    /* Phase 4: PHY configuration */
    REG_PHY_EXT_5B = 0x28;
    REG_PHY_EXT_56 = 0x00;
    REG_PHY_EXT_5B = 0x28;
    REG_PHY_EXT_2D = 0x07;

    delay_long();
    delay_long();
    delay_long();

    /* Phase 5: PHY init — timer + DMA sequence
     * Stock trace (cycles ~65-74k): CD31, CD30=0x05, CD33=0xC7,
     * CC2A=0x04, CC2C/CC2D=0xC7, C655=0x09, C620=0x00, C65A=0x01 */
    REG_CPU_TIMER_CTRL_CD31 = 0x04;
    REG_CPU_TIMER_CTRL_CD31 = 0x02;
    REG_PHY_DMA_CMD_CD30 = 0x05;          /* Stock: 0x05 (not 0x15) */
    REG_PHY_DMA_ADDR_LO = 0x00;
    REG_PHY_DMA_ADDR_HI = 0xC7;
    REG_CPU_KEEPALIVE = 0x04;              /* CC2A — must be before link-up */
    REG_CPU_KEEPALIVE_CC2C = 0xC7;         /* CC2C — keepalive timer */
    REG_CPU_KEEPALIVE_CC2D = 0xC7;         /* CC2D — keepalive timer */
    REG_PHY_CFG_C655 = 0x09;              /* Stock: 0x09 (bits 0 and 3 set) */
    REG_PHY_EXT_CTRL_C620 = 0x00;
    REG_PHY_CFG_C65A = 0x01;              /* C65A — PHY config before link-up */

    /* Phase 6: Tunnel adapter config */
    pcie_tunnel_adapter_config();

    /* Phase 7: First link-up attempt */
    /* Skip link-up if already trained — link-up resets LTSSM to Detect */
    if (REG_PCIE_LTSSM_STATE < 0x10) {
        pcie_link_up_attempt();
    }

    /* Phase 8: DMA engine config */
    REG_PCIE_DMA_SIZE_A = 0x08;
    REG_PCIE_DMA_SIZE_B = 0x00;
    REG_PCIE_DMA_SIZE_C = 0x08;
    REG_PCIE_DMA_SIZE_D = 0x08;
    REG_PCIE_DMA_BUF_A = 0x08;
    REG_PCIE_DMA_BUF_B = 0x20;
    REG_PCIE_DMA_BUF_C = 0x08;
    REG_PCIE_DMA_BUF_D = 0x28;
    REG_PCIE_DMA_CFG_50 = 0x00;
    REG_PCIE_DMA_CFG_51 = 0x00;
    REG_CPU_LINK_CEF3 = 0x08;
    REG_CPU_LINK_CEF2 = 0x80;
    /* Stock firmware writes these as RMW with global-derived values.
     * Emulator shows both end up as 0x00. Preserve hardware defaults. */
    XDATA_REG8(0xCEF0) &= 0xFE;          /* CEF0: clear bit 0 only */
    XDATA_REG8(0xCEEF) &= 0xFE;          /* CEEF: clear bit 0 only */
    XDATA_REG8(0xC807) = 0x04;            /* INT_DMA_CTRL (stock: cycle 74900) */
    REG_PCIE_DMA_CTRL_B281 = 0x10;
    REG_PHY_CFG_C6A8 = 0x01;
    XDATA_REG8(0x92C8) = (XDATA_REG8(0x92C8) & 0xFC);  /* Stock: clear low bits */
    REG_CPU_TIMER_CTRL_CD31 = 0x04;
    REG_CPU_TIMER_CTRL_CD31 = 0x02;

    /* Timer 1 (CC1C-CC1F) and Timer 2 (CC5C-CC5F) config
     * Stock firmware at cycles 75126-75175 */
    XDATA_REG8(0xCC1D) = 0x04;  /* stop timer 1 */
    XDATA_REG8(0xCC1D) = 0x02;  /* clear timer 1 */
    XDATA_REG8(0xCC5D) = 0x04;  /* stop timer 2 */
    XDATA_REG8(0xCC5D) = 0x02;  /* clear timer 2 */
    XDATA_REG8(0xCC1C) = 0x06;  /* timer 1 prescaler */
    XDATA_REG8(0xCC1E) = 0x00;  /* timer 1 count hi */
    XDATA_REG8(0xCC1F) = 0x8B;  /* timer 1 count lo */
    XDATA_REG8(0xCC5C) = 0x04;  /* timer 2 prescaler */
    XDATA_REG8(0xCC5E) = 0x00;  /* timer 2 count hi */
    XDATA_REG8(0xCC5F) = 0xC7;  /* timer 2 count lo */

    /* Phase 9: Second link-up with reconfigured adapter */
    if (REG_PCIE_LTSSM_STATE < 0x10) {
        REG_PCIE_TUNNEL_CTRL = 0x01;
        REG_PCIE_TUNNEL_CTRL = 0x00;
        pcie_tunnel_adapter_config();
        pcie_link_up_attempt();
    }

    /* Phase 9b: Bridge port register configuration.
     * B298 bit 4 (tunnel enable) is needed for TLP completions to return data.
     * B430 bit 0 clear for lane config.
     * NOTE: These are set AFTER USB enumeration (in handle_cbw) to avoid
     * interfering with host xHCI controller during early boot. */

    /* Final: set link active + clean up (stock: cycles 75650-75798) */
    REG_PCIE_LANE_CTRL_C659 &= 0xFE;     /* C659: clear bit 0 */
    /* B402 skip — see Phase 1 comment */
    REG_PCIE_LANE_CONFIG = 0xEE;
    REG_PCIE_LANE_CONFIG = 0xEE;

    /* NOTE: CE3D, C233 SerDes reset, D9A4 signal detect, E3AF CDR config
     * are NOT in the stock emulator init path. They only appear in the
     * D92E event handler on real hardware. Skipping them for now to match
     * stock init ordering. They may be added later if needed. */

    /* B1C5 pre-CF3D: MSC engine init trigger (stock: B219-B223)
     * NOTE: C42C=0x01, C42D&=0xFE — stock does this before CF3D. */
    REG_USB_MSC_CTRL = 0x01;
    REG_USB_MSC_STATUS &= 0xFE;

    /* CF3D(0) equivalent: PHY/interrupt mask config */
    { uint8_t ff_idx;
    for (ff_idx = 0; ff_idx < 4; ff_idx++) XDATA_REG8(0xC430 + ff_idx) = 0xFF;
    for (ff_idx = 0; ff_idx < 4; ff_idx++) XDATA_REG8(0xC440 + ff_idx) = 0xFF;
    for (ff_idx = 0; ff_idx < 8; ff_idx++) XDATA_REG8(0x9096 + ff_idx) = 0xFF;
    XDATA_REG8(0x909E) = 0x03;
    for (ff_idx = 0; ff_idx < 4; ff_idx++) XDATA_REG8(0xC438 + ff_idx) = 0xFF;
    for (ff_idx = 0; ff_idx < 4; ff_idx++) XDATA_REG8(0xC448 + ff_idx) = 0xFF;
    for (ff_idx = 0; ff_idx < 8; ff_idx++) XDATA_REG8(0x9011 + ff_idx) = 0xFF;
    XDATA_REG8(0x9018) = 0x03;
    XDATA_REG8(0x9010) = 0xFE;
    }

    /* DF5E equivalent: NVMe/PCIe link prep */
    XDATA_REG8(0xC428) &= 0xF7;
    XDATA_REG8(0xC473) = (XDATA_REG8(0xC473) & 0xBF) | 0x40;
    XDATA_REG8(0xC473) = (XDATA_REG8(0xC473) & 0xFD) | 0x02;
    XDATA_REG8(0xC473) &= 0xF7;
    XDATA_REG8(0xC472) &= 0xFD;
    { uint8_t fi;
    for (fi = 0; fi < 4; fi++) XDATA_REG8(0xC448 + fi) = 0xFF;
    }
    XDATA_REG8(0xC473) = (XDATA_REG8(0xC473) & 0xDF) | 0x20;
    XDATA_REG8(0xC473) = (XDATA_REG8(0xC473) & 0xFB) | 0x04;
    XDATA_REG8(0xC473) &= 0xEF;
    XDATA_REG8(0xC472) &= 0xFB;
    { uint8_t fi;
    for (fi = 0; fi < 4; fi++) XDATA_REG8(0xC438 + fi) = 0xFF;
    }

    /*
     * B1C5 post-CF3D/DF5E sequence: PHY init completion
     * Stock firmware address: 0xB22C-0xB25F
     *
     * After CF3D(0) and DF5E, the stock firmware does:
     *   1. 91C3 &= 0xDF (clear bit 5 — PHY control)
     *   2. 91C0 pulse bit 0 (set then clear — PHY reset pulse)
     *   3. Clear 0x0AF7 (PCIe enum done flag)
     *   4. Start timer (prescaler=4, count=0x018F)
     *   5. Poll E318 bit 4 (PHY completion) with timer timeout
     *   6. Stop timer
     *   7. Read 91C0 bits 3-4, set 0x09FA=4, write 0x0AE1
     *
     * Original disassembly:
     *   b22c: mov dptr, #0x91c3
     *   b22f: movx a, @dptr
     *   b230: anl a, #0xdf       ; 91C3 &= 0xDF
     *   b232: movx @dptr, a
     *   b233: mov dptr, #0x91c0
     *   b236: movx a, @dptr
     *   b237: anl a, #0xfe
     *   b239: orl a, #0x01       ; 91C0 |= 0x01
     *   b23b: movx @dptr, a
     *   b23c: movx a, @dptr
     *   b23d: anl a, #0xfe       ; 91C0 &= 0xFE
     *   b23f: movx @dptr, a
     *   b240: lcall 0x54bb       ; 0x0AF7 = 0
     *   b243: mov r5, #0x8f      ; timer count lo
     *   b245: mov r4, #0x01      ; timer count hi
     *   b247: mov r7, #0x04      ; prescaler
     *   b249: lcall 0xe292       ; timer_setup(4, 0x01, 0x8F)
     *   b24c: mov dptr, #0xe318
     *   b24f: movx a, @dptr      ; read E318
     *   b250: anl a, #0x10       ; check bit 4
     *   b252: mov r7, a
     *   b253: swap a
     *   b254: anl a, #0x0f
     *   b256: jnz 0xb25f         ; if bit 4 set, done
     *   b258: mov dptr, #0xcc11
     *   b25b: movx a, @dptr
     *   b25c: jnb 0xe0.1, 0xb24c ; if timer not expired, keep polling
     *   b25f: lcall 0xe642       ; timer_stop
     */
    REG_USB_PHY_CTRL_91C3 &= 0xDF;         /* clear bit 5 */
    REG_USB_PHY_CTRL_91C0 |= 0x01;         /* set bit 0 */
    REG_USB_PHY_CTRL_91C0 &= 0xFE;         /* clear bit 0 (pulse) */
    G_XFER_CTRL_0AF7 = 0;                  /* clear PCIe enum done */

    /* Start timer and poll E318 bit 4 for PHY completion */
    timer_stop();
    REG_TIMER0_DIV = (REG_TIMER0_DIV & 0xF8) | 0x04;  /* prescaler = 4 */
    REG_TIMER0_THRESHOLD_HI = 0x01;
    REG_TIMER0_THRESHOLD_LO = 0x8F;
    REG_TIMER0_CSR = TIMER_CSR_ENABLE;     /* start timer */

    uart_puts("[E318 poll]\n");
    while (!(REG_PHY_COMPLETION_E318 & 0x10)) {
        if (REG_TIMER0_CSR & TIMER_CSR_EXPIRED) break;  /* timeout */
    }
    timer_stop();

    /* Post-completion: read 91C0 bits 3-4, set globals (stock: B262-B285) */
    { uint8_t phy_status = (REG_USB_PHY_CTRL_91C0 & 0x18) >> 3;
    uart_puts("[91C0 phy="); uart_puthex(phy_status); uart_puts("]\n");
    G_EVENT_CTRL_09FA = 0x04;
    if (phy_status == 0x02) {
        G_TLP_BASE_LO = (G_EVENT_CTRL_09FA == 0x04) ? 0x01 : 0x02;
    } else {
        G_TLP_BASE_LO = 0x02;
    }
    }

    /* Post-E318 timer setup (stock: cycle 94486-94535) */
    timer_stop();
    REG_TIMER0_DIV = (REG_TIMER0_DIV & 0xF8) | 0x00;  /* prescaler = 0 */
    REG_TIMER0_THRESHOLD_HI = 0x00;
    REG_TIMER0_THRESHOLD_LO = 0x09;
    REG_TIMER0_CSR = TIMER_CSR_ENABLE;
    while (!(REG_TIMER0_CSR & TIMER_CSR_EXPIRED)) { }
    REG_TIMER0_CSR = TIMER_CSR_EXPIRED;

    /* C807: 0x04 then 0x84 (stock: cycles 94580-94586) */
    XDATA_REG8(0xC807) = 0x04;
    XDATA_REG8(0xC807) = 0x84;

    uart_puts("[B450="); uart_puthex(REG_PCIE_LTSSM_STATE); uart_puts("]\n");
    uart_puts("[PCIe done]\n");
}

/* pcie_link_train removed — all init now in pcie_init() pre-USB */

/*
 * phy_link_ctrl_init - PHY link controller initialization
 * Bank 1 Address: 0xDAC8-0xDB34 (109 bytes) [actual addr: 0x15A33]
 *
 * Configures PHY link controller registers and extended page registers
 * for PCIe link training. Called via trampoline 0x0561 → bank1 0xEEB5
 * which calls DAC8 then falls through to serdes_phy_init (0x8E31).
 *
 * Sets C21B bits 7:6, C202 bit 3 (link ctrl enable), configures
 * extended page registers 0x1262/0x28ED/0x28CE/0x281C/0x281D,
 * clears C20B bit 7, and configures C22F.
 *
 * Original disassembly:
 *   dac8: mov dptr, #0xc21b
 *   dacb: movx a, @dptr
 *   dacc: anl a, #0x3f
 *   dace: orl a, #0xc0
 *   dad0: movx @dptr, a
 *   dad1: mov dptr, #0xc202
 *   dad4: movx a, @dptr
 *   dad5: anl a, #0xf7
 *   dad7: orl a, #0x08
 *   dad9: movx @dptr, a
 *   dada: mov r3, #0x02       ; ext page mode
 *   dadc: mov r2, #0x12
 *   dade: mov r1, #0x62
 *   dae0: lcall 0x0ba0        ; read ext[0x1262]
 *   dae3: anl a, #0xef        ; clear bit 4
 *   dae5: lcall 0x0bbe        ; write ext[0x1262]
 *   dae8: mov r2, #0x28
 *   daea: mov r1, #0xed
 *   daec: lcall 0x0ba0        ; read ext[0x28ED]
 *   daef: anl a, #0xbf
 *   daf1: orl a, #0x40        ; set bit 6
 *   daf3: lcall 0x0bbe        ; write ext[0x28ED]
 *   daf6: mov r1, #0xce
 *   daf8: lcall 0x0ba0        ; read ext[0x28CE]
 *   dafb: anl a, #0xfe        ; clear bit 0
 *   dafd: lcall 0x0bbe        ; write ext[0x28CE]
 *   db00: mov r1, #0x1c
 *   db02: lcall 0x0ba0        ; read ext[0x281C]
 *   db05: orl a, #0x80        ; set bit 7
 *   db07: lcall 0x0bbe        ; write ext[0x281C]
 *   db0a: lcall 0x0ba0        ; read ext[0x281C] again
 *   db0d: orl a, #0x40        ; set bit 6
 *   db0f: lcall 0x0bbe        ; write ext[0x281C]
 *   db12: lcall 0x0ba0        ; read ext[0x281C] again
 *   db15: orl a, #0x02        ; set bit 1
 *   db17: lcall 0x0bbe        ; write ext[0x281C]
 *   db1a: mov dptr, #0xc20b
 *   db1d: movx a, @dptr
 *   db1e: anl a, #0x7f        ; clear bit 7
 *   db20: movx @dptr, a
 *   db21: inc r1              ; r1 = 0x1d
 *   db22: lcall 0x0ba0        ; read ext[0x281D]
 *   db25: anl a, #0xfe        ; clear bit 0
 *   db27: lcall 0x0bbe        ; write ext[0x281D]
 *   db2a: mov dptr, #0xc22f
 *   db2d: lcall 0xc3c9        ; C22F = (C22F & 0xFB) | 0x04
 *   db30: movx a, @dptr
 *   db31: anl a, #0xbf        ; C22F &= 0xBF
 *   db33: movx @dptr, a
 *   db34: ret
 */
static void phy_link_ctrl_init(void) {
    uint8_t tmp;

    /* C21B: set bits 7:6 */
    REG_PHY_LINK_CTRL_C21B = (REG_PHY_LINK_CTRL_C21B & 0x3F) | 0xC0;

    /* C202: set bit 3 — link controller enable */
    REG_LINK_CTRL = (REG_LINK_CTRL & 0xF7) | LINK_CTRL_BIT3;

    /* ext[0x1262]: clear bit 4 */
    tmp = xdata_ext_read(0x1262);
    xdata_ext_write(0x1262, tmp & 0xEF);

    /* ext[0x28ED]: set bit 6 */
    tmp = xdata_ext_read(0x28ED);
    xdata_ext_write(0x28ED, (tmp & 0xBF) | 0x40);

    /* ext[0x28CE]: clear bit 0 */
    tmp = xdata_ext_read(0x28CE);
    xdata_ext_write(0x28CE, tmp & 0xFE);

    /* ext[0x281C]: set bits 7, 6, 1 (three sequential RMW ops) */
    tmp = xdata_ext_read(0x281C);
    tmp |= 0x80;
    xdata_ext_write(0x281C, tmp);
    tmp = xdata_ext_read(0x281C);
    tmp |= 0x40;
    xdata_ext_write(0x281C, tmp);
    tmp = xdata_ext_read(0x281C);
    tmp |= 0x02;
    xdata_ext_write(0x281C, tmp);

    /* C20B: clear bit 7 */
    REG_PHY_LINK_CTRL_C20B &= 0x7F;

    /* ext[0x281D]: clear bit 0 */
    tmp = xdata_ext_read(0x281D);
    xdata_ext_write(0x281D, tmp & 0xFE);

    /* C22F: set bit 2 (via helper c3c9 pattern) then clear bit 6 */
    REG_PHY_SERDES_C22F = (REG_PHY_SERDES_C22F & 0xFB) | 0x04;
    REG_PHY_SERDES_C22F &= 0xBF;
}

/*
 * serdes_phy_init - SerDes PHY lane initialization
 * Bank 1 Address: 0x8E31-0x9271 (1089 bytes) [actual addr: 0x10D9C]
 *
 * Configures SerDes PHY registers for all lanes including:
 *   - PHY PLL reference clock (E741/E742)
 *   - CPU clock config (CC43)
 *   - Lane equalization, CDR, and signal integrity for lanes 0-3
 *   - Buffer descriptor config for PCIe DMA (93xx registers)
 *
 * Called from init chain after USB PHY config (9241) and before
 * buffer descriptor setup. Stock firmware calls this via bank switch
 * trampoline from the hw_init chain at ~cycle 40900.
 *
 * All register writes are read-modify-write to preserve other bits.
 */
static void serdes_phy_init(void) {
    uint8_t tmp;

    /* === E741/E742 PHY PLL reference clock config === */
    /* E741 = (E741 & 0xF8) | 0x03  — set bits [2:0] = 011 */
    REG_PHY_PLL_CTRL = (REG_PHY_PLL_CTRL & 0xF8) | 0x03;
    /* E741 = (E741 & 0xC7) | 0x28  — set bits [5:3] = 101 */
    REG_PHY_PLL_CTRL = (REG_PHY_PLL_CTRL & 0xC7) | 0x28;
    /* E742 = (E742 & 0xFC) | 0x03  — set bits [1:0] = 11 */
    REG_PHY_PLL_CFG = (REG_PHY_PLL_CFG & 0xFC) | 0x03;
    /* E741 = (E741 & 0x3F) | 0x80  — set bit 7, clear bit 6 */
    REG_PHY_PLL_CTRL = (REG_PHY_PLL_CTRL & 0x3F) | 0x80;
    /* E742 = E742 & 0xF7  — clear bit 3 */
    REG_PHY_PLL_CFG &= 0xF7;

    /* === CC43 CPU clock config === */
    /* CC43 = (CC43 & 0x1F) | 0x80  — set bit 7, preserve bits [4:0] */
    REG_CPU_CLK_CFG = (REG_CPU_CLK_CFG & 0x1F) | 0x80;

    /* === C21F: set bit 2 === */
    XDATA_REG8(0xC21F) = (XDATA_REG8(0xC21F) & 0xFB) | 0x04;

    /* === Extended register write: XDATA[0x2849] = 0xA0 === */
    xdata_ext_write(0x2849, 0xA0);

    /* === Compute equalization parameters from globals ===
     * G_SERDES_EQ_MULT (0x0746) * 0x20, OR'd with G_SERDES_EQ_PARAM (0x0736)
     * Stock values: 0x0746=5, 0x0736=3 → result = (5*32)|3 = 0xA3
     * Written to IDATA[0x6A-0x6D] for later use by equalization code */
    {
        uint16_t eq_val;
        uint8_t eq_lo, eq_hi;
        eq_val = (uint16_t)G_SERDES_EQ_MULT * 0x20;
        eq_lo = (uint8_t)eq_val | G_SERDES_EQ_PARAM;
        eq_hi = (uint8_t)(eq_val >> 8);
        I_USB_STATE = eq_hi;        /* IDATA[0x6A] = high byte */
        I_TRANSFER_6B = eq_lo;      /* IDATA[0x6B] = low byte */
        I_TRANSFER_6C = eq_hi;      /* IDATA[0x6C] = high byte (copy) */
        I_TRANSFER_6D = eq_lo;      /* IDATA[0x6D] = low byte (copy) */
    }

    /* === Lane 0-1 (C2xx) SerDes config === */

    /* C2A8: set bit 6 */
    XDATA_REG8(0xC2A8) = (XDATA_REG8(0xC2A8) & 0x3F) | 0x40;
    /* C2C5: set bits [6:4]=0x70 */
    XDATA_REG8(0xC2C5) = (XDATA_REG8(0xC2C5) & 0x8F) | 0x70;
    /* C2A1: bits [6:5]=11 */
    XDATA_REG8(0xC2A1) = (XDATA_REG8(0xC2A1) & 0x9F) | 0x60;
    /* C28C, C29C, C2AC: set bit 0 */
    XDATA_REG8(0xC28C) = (XDATA_REG8(0xC28C) & 0xFE) | 0x01;
    XDATA_REG8(0xC29C) = (XDATA_REG8(0xC29C) & 0xFE) | 0x01;
    XDATA_REG8(0xC2AC) = (XDATA_REG8(0xC2AC) & 0xFE) | 0x01;
    /* C2BC: clear bit 0 */
    XDATA_REG8(0xC2BC) &= 0xFE;
    /* C28C: clear bit 1 */
    XDATA_REG8(0xC28C) &= 0xFD;
    /* C29C, C2AC: set bit 1 */
    XDATA_REG8(0xC29C) = (XDATA_REG8(0xC29C) & 0xFD) | 0x02;
    XDATA_REG8(0xC2AC) = (XDATA_REG8(0xC2AC) & 0xFD) | 0x02;
    /* C2BC: clear bit 1 */
    XDATA_REG8(0xC2BC) &= 0xFD;
    /* C2C3: bits [5:2]=0111 */
    XDATA_REG8(0xC2C3) = (XDATA_REG8(0xC2C3) & 0xC3) | 0x1C;
    /* C2C9: keep bit 7, set bits 6,0 */
    XDATA_REG8(0xC2C9) = (XDATA_REG8(0xC2C9) & 0x80) | 0x41;
    /* C2A5: high nibble=0xE0, C2A6: high nibble=0x70 */
    XDATA_REG8(0xC2A5) = (XDATA_REG8(0xC2A5) & 0x0F) | 0xE0;
    XDATA_REG8(0xC2A6) = (XDATA_REG8(0xC2A6) & 0x0F) | 0x70;
    /* C2CA: clear bits 0-3 sequentially */
    XDATA_REG8(0xC2CA) &= 0xFE;
    XDATA_REG8(0xC2CA) &= 0xFD;
    XDATA_REG8(0xC2CA) &= 0xFB;
    XDATA_REG8(0xC2CA) &= 0xF7;
    /* C287: bits [7:5]=111 */
    XDATA_REG8(0xC287) = (XDATA_REG8(0xC287) & 0x1F) | 0xE0;
    /* C294: set bits [6:4]=0x70 */
    XDATA_REG8(0xC294) = (XDATA_REG8(0xC294) & 0x8F) | 0x70;
    /* C2A2: bits [6:5]=11 */
    XDATA_REG8(0xC2A2) = (XDATA_REG8(0xC2A2) & 0x1F) | 0x60;
    /* C2C5: bits [3:0]=0x0B */
    XDATA_REG8(0xC2C5) = (XDATA_REG8(0xC2C5) & 0xF0) | 0x0B;
    /* C293: bits [3:2]=01 */
    XDATA_REG8(0xC293) = (XDATA_REG8(0xC293) & 0xF3) | 0x04;
    /* C2CE: bits [1:0]=10 */
    XDATA_REG8(0xC2CE) = (XDATA_REG8(0xC2CE) & 0xFC) | 0x02;
    /* C2CE: bits [4:2]=101 */
    XDATA_REG8(0xC2CE) = (XDATA_REG8(0xC2CE) & 0xE3) | 0x14;
    /* C328: set bit 6 (via helper that writes C2CE then switches to C328) */
    XDATA_REG8(0xC328) = (XDATA_REG8(0xC328) & 0x3F) | 0x40;

    /* === Lane 2-3 (C3xx) SerDes config === */

    /* C345: set bits [6:4]=0x70 */
    XDATA_REG8(0xC345) = (XDATA_REG8(0xC345) & 0x8F) | 0x70;
    /* C321: bits [6:5]=11 */
    XDATA_REG8(0xC321) = (XDATA_REG8(0xC321) & 0x9F) | 0x60;
    /* C30C, C31C, C32C: set bit 0 */
    XDATA_REG8(0xC30C) = (XDATA_REG8(0xC30C) & 0xFE) | 0x01;
    XDATA_REG8(0xC31C) = (XDATA_REG8(0xC31C) & 0xFE) | 0x01;
    XDATA_REG8(0xC32C) = (XDATA_REG8(0xC32C) & 0xFE) | 0x01;
    /* C33C: clear bit 0 */
    XDATA_REG8(0xC33C) &= 0xFE;
    /* C30C: clear bit 1 */
    XDATA_REG8(0xC30C) &= 0xFD;
    /* C31C, C32C: set bit 1 */
    XDATA_REG8(0xC31C) = (XDATA_REG8(0xC31C) & 0xFD) | 0x02;
    XDATA_REG8(0xC32C) = (XDATA_REG8(0xC32C) & 0xFD) | 0x02;
    /* C33C: clear bit 1 */
    XDATA_REG8(0xC33C) &= 0xFD;
    /* C343: bits [5:2]=0111 */
    XDATA_REG8(0xC343) = (XDATA_REG8(0xC343) & 0xC3) | 0x1C;
    /* C349: keep bit 7, set bits 6,0 */
    XDATA_REG8(0xC349) = (XDATA_REG8(0xC349) & 0x80) | 0x41;
    /* C325: high nibble=0xE0, C326: high nibble=0x70 */
    XDATA_REG8(0xC325) = (XDATA_REG8(0xC325) & 0x0F) | 0xE0;
    XDATA_REG8(0xC326) = (XDATA_REG8(0xC326) & 0x0F) | 0x70;
    /* C34A: clear bits 0-3 sequentially */
    XDATA_REG8(0xC34A) &= 0xFE;
    XDATA_REG8(0xC34A) &= 0xFD;
    XDATA_REG8(0xC34A) &= 0xFB;
    XDATA_REG8(0xC34A) &= 0xF7;
    /* C307: bits [7:5]=111 */
    XDATA_REG8(0xC307) = (XDATA_REG8(0xC307) & 0x1F) | 0xE0;
    /* C314: set bits [6:4]=0x70 */
    XDATA_REG8(0xC314) = (XDATA_REG8(0xC314) & 0x8F) | 0x70;
    /* C322: bits [6:5]=11 */
    XDATA_REG8(0xC322) = (XDATA_REG8(0xC322) & 0x1F) | 0x60;
    /* C345: bits [3:0]=0x0B */
    XDATA_REG8(0xC345) = (XDATA_REG8(0xC345) & 0xF0) | 0x0B;
    /* C313: bits [3:2]=01 */
    XDATA_REG8(0xC313) = (XDATA_REG8(0xC313) & 0xF3) | 0x04;
    /* C34E: bits [1:0]=10 */
    XDATA_REG8(0xC34E) = (XDATA_REG8(0xC34E) & 0xFC) | 0x02;
    /* C34E: bits [4:2]=010, 100 -> actually (& 0xE3) | 0x14 */
    XDATA_REG8(0xC34E) = (XDATA_REG8(0xC34E) & 0xE3) | 0x14;

    /* === C21D: bits [7:6]=10 === */
    XDATA_REG8(0xC21D) = (XDATA_REG8(0xC21D) & 0x3F) | 0x80;

    /* === Clear SerDes status registers === */
    XDATA_REG8(0x9316) = 0x00;
    XDATA_REG8(0x9317) = 0x00;
    XDATA_REG8(0x931A) = 0x00;
    XDATA_REG8(0x931B) = 0x00;
    XDATA_REG8(0x9322) = 0x00;
    XDATA_REG8(0x9323) = 0x00;

    /* === Lane 0-1 additional equalizer/driver config === */

    /* C290: clear bits [6:5] */
    XDATA_REG8(0xC290) &= 0x9F;
    /* C2A0: clear bits [6:5] */
    XDATA_REG8(0xC2A0) &= 0x9F;
    /* C282: set bits [4:0]=0x0A */
    XDATA_REG8(0xC282) = (XDATA_REG8(0xC282) & 0xE0) | 0x0A;
    /* C292: bits [4:0]=0x09 */
    XDATA_REG8(0xC292) = (XDATA_REG8(0xC292) & 0xE0) | 0x09;
    /* C2A2: set bits [4:0]=0x0A */
    XDATA_REG8(0xC2A2) = (XDATA_REG8(0xC2A2) & 0xE0) | 0x0A;
    /* C290: set bits [4:0]=0x03 */
    XDATA_REG8(0xC290) = (XDATA_REG8(0xC290) & 0xE0) | 0x03;
    /* C2A0: set bits [4:0]=0x03 */
    XDATA_REG8(0xC2A0) = (XDATA_REG8(0xC2A0) & 0xE0) | 0x03;
    /* C291: set bits [4:0]=0x08 */
    XDATA_REG8(0xC291) = (XDATA_REG8(0xC291) & 0xE0) | 0x08;
    /* C2A1: set bits [4:0]=0x08 */
    XDATA_REG8(0xC2A1) = (XDATA_REG8(0xC2A1) & 0xE0) | 0x08;
    /* C2DB: bits [4:0]=0x1B */
    XDATA_REG8(0xC2DB) = (XDATA_REG8(0xC2DB) & 0xE0) | 0x1B;
    /* C284: bits [3:0]=0x05 */
    XDATA_REG8(0xC284) = (XDATA_REG8(0xC284) & 0xF0) | 0x05;
    /* C294: bits [3:0]=0x07 */
    XDATA_REG8(0xC294) = (XDATA_REG8(0xC294) & 0xF0) | 0x07;
    /* C285: bits [3:0]=0x0F */
    XDATA_REG8(0xC285) = (XDATA_REG8(0xC285) & 0xF0) | 0x0F;
    /* C295: bits [3:0]=0x0C */
    XDATA_REG8(0xC295) = (XDATA_REG8(0xC295) & 0xF0) | 0x0C;
    /* C2A5: bits [3:0]=0x0F */
    XDATA_REG8(0xC2A5) = (XDATA_REG8(0xC2A5) & 0xF0) | 0x0F;
    /* C285: bits [7:4]=0x60, C286: bits [3:0]=0x07 */
    XDATA_REG8(0xC285) = (XDATA_REG8(0xC285) & 0x0F) | 0x60;
    XDATA_REG8(0xC286) = (XDATA_REG8(0xC286) & 0xF0) | 0x07;
    /* C296: bits [3:0]=0x0F */
    XDATA_REG8(0xC296) = (XDATA_REG8(0xC296) & 0xF0) | 0x0F;
    /* C2A7: set bits [4:0]=0x11 */
    XDATA_REG8(0xC2A7) = (XDATA_REG8(0xC2A7) & 0xE0) | 0x11;
    /* C28B: bits [5:0]=0x0A */
    XDATA_REG8(0xC28B) = (XDATA_REG8(0xC28B) & 0xC0) | 0x0A;
    /* C284: bits [6:4]=010 */
    XDATA_REG8(0xC284) = (XDATA_REG8(0xC284) & 0x8F) | 0x40;
    /* C2A4: bits [6:4]=000 */
    XDATA_REG8(0xC2A4) &= 0x8F;
    /* C289: bits [7:4]=0x90 */
    XDATA_REG8(0xC289) = (XDATA_REG8(0xC289) & 0x0F) | 0x90;
    /* C299: set bits [7:4]=0x80 */
    XDATA_REG8(0xC299) = (XDATA_REG8(0xC299) & 0x0F) | 0x80;
    /* C2A9: set bits [7:4]=0x80 */
    XDATA_REG8(0xC2A9) = (XDATA_REG8(0xC2A9) & 0x0F) | 0x80;
    /* C282: bits [7:5]=101 */
    XDATA_REG8(0xC282) = (XDATA_REG8(0xC282) & 0x1F) | 0xA0;
    /* C292: bits [7:5]=001 */
    XDATA_REG8(0xC292) = (XDATA_REG8(0xC292) & 0x1F) | 0x20;
    /* C2C6: bits [3:0]=0x0D */
    XDATA_REG8(0xC2C6) = (XDATA_REG8(0xC2C6) & 0xF0) | 0x0D;
    /* C2CC: bits [3:1]=111 (0x0E mask) */
    XDATA_REG8(0xC2CC) = (XDATA_REG8(0xC2CC) & 0xF1) | 0x0E;
    /* C2CD: = 0x00 */
    XDATA_REG8(0xC2CD) = 0x00;

    /* === Lane 2-3 additional equalizer/driver config === */

    /* C310: clear bits [6:5] */
    XDATA_REG8(0xC310) &= 0x9F;
    /* C320: clear bits [6:5] */
    XDATA_REG8(0xC320) &= 0x9F;
    /* C302: set bits [4:0]=0x0A */
    XDATA_REG8(0xC302) = (XDATA_REG8(0xC302) & 0xE0) | 0x0A;
    /* C312: bits [4:0]=0x09 */
    XDATA_REG8(0xC312) = (XDATA_REG8(0xC312) & 0xE0) | 0x09;
    /* C322: set bits [4:0]=0x0A */
    XDATA_REG8(0xC322) = (XDATA_REG8(0xC322) & 0xE0) | 0x0A;
    /* C310: set bits [4:0]=0x03 */
    XDATA_REG8(0xC310) = (XDATA_REG8(0xC310) & 0xE0) | 0x03;
    /* C320: set bits [4:0]=0x03 */
    XDATA_REG8(0xC320) = (XDATA_REG8(0xC320) & 0xE0) | 0x03;
    /* C311: set bits [4:0]=0x08 */
    XDATA_REG8(0xC311) = (XDATA_REG8(0xC311) & 0xE0) | 0x08;
    /* C321: set bits [4:0]=0x08 */
    XDATA_REG8(0xC321) = (XDATA_REG8(0xC321) & 0xE0) | 0x08;
    /* C35B: bits [4:0]=0x1B */
    XDATA_REG8(0xC35B) = (XDATA_REG8(0xC35B) & 0xE0) | 0x1B;
    /* C304: bits [3:0]=0x05 */
    XDATA_REG8(0xC304) = (XDATA_REG8(0xC304) & 0xF0) | 0x05;
    /* C314: bits [3:0]=0x07 */
    XDATA_REG8(0xC314) = (XDATA_REG8(0xC314) & 0xF0) | 0x07;
    /* C305: bits [3:0]=0x0F */
    XDATA_REG8(0xC305) = (XDATA_REG8(0xC305) & 0xF0) | 0x0F;
    /* C315: bits [3:0]=0x0C */
    XDATA_REG8(0xC315) = (XDATA_REG8(0xC315) & 0xF0) | 0x0C;
    /* C325: bits [3:0]=0x0F */
    XDATA_REG8(0xC325) = (XDATA_REG8(0xC325) & 0xF0) | 0x0F;
    /* C305: bits [7:4]=0x60, C306: bits [3:0]=0x07 */
    XDATA_REG8(0xC305) = (XDATA_REG8(0xC305) & 0x0F) | 0x60;
    XDATA_REG8(0xC306) = (XDATA_REG8(0xC306) & 0xF0) | 0x07;
    /* C316: bits [3:0]=0x0F */
    XDATA_REG8(0xC316) = (XDATA_REG8(0xC316) & 0xF0) | 0x0F;
    /* C327: set bits [4:0]=0x11 */
    XDATA_REG8(0xC327) = (XDATA_REG8(0xC327) & 0xE0) | 0x11;
    /* C30B: bits [5:0]=0x0A */
    XDATA_REG8(0xC30B) = (XDATA_REG8(0xC30B) & 0xC0) | 0x0A;
    /* C304: bits [6:4]=010 */
    XDATA_REG8(0xC304) = (XDATA_REG8(0xC304) & 0x8F) | 0x40;
    /* C324: bits [6:4]=000 */
    XDATA_REG8(0xC324) &= 0x8F;
    /* C309: bits [7:4]=0x90 */
    XDATA_REG8(0xC309) = (XDATA_REG8(0xC309) & 0x0F) | 0x90;
    /* C319: set bits [7:4]=0x80 */
    XDATA_REG8(0xC319) = (XDATA_REG8(0xC319) & 0x0F) | 0x80;
    /* C329: set bits [7:4]=0x80 */
    XDATA_REG8(0xC329) = (XDATA_REG8(0xC329) & 0x0F) | 0x80;
    /* C302: bits [7:5]=101 */
    XDATA_REG8(0xC302) = (XDATA_REG8(0xC302) & 0x1F) | 0xA0;
    /* C312: bits [7:5]=001 */
    XDATA_REG8(0xC312) = (XDATA_REG8(0xC312) & 0x1F) | 0x20;
    /* C346: bits [3:0]=0x0D */
    XDATA_REG8(0xC346) = (XDATA_REG8(0xC346) & 0xF0) | 0x0D;
    /* C34C: bits [3:1]=111 (0x0E mask) */
    XDATA_REG8(0xC34C) = (XDATA_REG8(0xC34C) & 0xF1) | 0x0E;
    /* C34D: = 0x00 */
    XDATA_REG8(0xC34D) = 0x00;

    /* === Buffer descriptor config for PCIe SerDes DMA (93xx) === */
    XDATA_REG8(0x9310) = 0x01;
    XDATA_REG8(0x9311) = 0x60;
    XDATA_REG8(0x9312) = 0x00;
    XDATA_REG8(0x9313) = 0xE3;
    XDATA_REG8(0x9314) = 0x01;
    XDATA_REG8(0x9315) = 0x60;
    XDATA_REG8(0x9318) = 0x01;
    XDATA_REG8(0x9319) = 0x60;
    XDATA_REG8(0x931C) = 0x00;
    XDATA_REG8(0x931D) = 0x03;
    XDATA_REG8(0x931E) = 0x00;
    XDATA_REG8(0x931F) = 0xE0;
    XDATA_REG8(0x9320) = 0x00;
    XDATA_REG8(0x9321) = 0xE3;

    /* === Additional lane config === */

    /* C2A3: bits [3:2]=01 */
    XDATA_REG8(0xC2A3) = (XDATA_REG8(0xC2A3) & 0xF3) | 0x04;
    /* C323: bits [3:2]=01 */
    XDATA_REG8(0xC323) = (XDATA_REG8(0xC323) & 0xF3) | 0x04;
    /* C297: bits [7:5]=010 */
    XDATA_REG8(0xC297) = (XDATA_REG8(0xC297) & 0x1F) | 0x40;
    /* C29A: bits [3:0]=0x0E */
    XDATA_REG8(0xC29A) = (XDATA_REG8(0xC29A) & 0xF0) | 0x0E;
    /* C2A7: bits [7:5]=010 */
    XDATA_REG8(0xC2A7) = (XDATA_REG8(0xC2A7) & 0x1F) | 0x40;
    /* C2AB: bits [5:0]=0x00 */
    XDATA_REG8(0xC2AB) &= 0xC0;
    /* C317: bits [7:5]=010 */
    XDATA_REG8(0xC317) = (XDATA_REG8(0xC317) & 0x1F) | 0x40;
    /* C31A: bits [3:0]=0x0E */
    XDATA_REG8(0xC31A) = (XDATA_REG8(0xC31A) & 0xF0) | 0x0E;
    /* C327: bits [7:5]=010 */
    XDATA_REG8(0xC327) = (XDATA_REG8(0xC327) & 0x1F) | 0x40;
    /* C32B: bits [5:0]=0x00 */
    XDATA_REG8(0xC32B) &= 0xC0;

    /* === Final equalization tune === */

    /* C2AA: bits [3:0]=0x0D */
    XDATA_REG8(0xC2AA) = (XDATA_REG8(0xC2AA) & 0xF0) | 0x0D;
    /* C297: bits [4:0]=0x10 */
    XDATA_REG8(0xC297) = (XDATA_REG8(0xC297) & 0xE0) | 0x10;
    /* C293: bits [1:0]=01 */
    XDATA_REG8(0xC293) = (XDATA_REG8(0xC293) & 0xFC) | 0x01;
    /* C283: bits [3:2]=01 */
    XDATA_REG8(0xC283) = (XDATA_REG8(0xC283) & 0xF3) | 0x04;
    /* C2A6: bits [3:0]=0x0B */
    XDATA_REG8(0xC2A6) = (XDATA_REG8(0xC2A6) & 0xF0) | 0x0B;
    /* C2A4: bits [3:0]=0x07 */
    XDATA_REG8(0xC2A4) = (XDATA_REG8(0xC2A4) & 0xF0) | 0x07;
    /* C2A3: bits [1:0]=10 */
    XDATA_REG8(0xC2A3) = (XDATA_REG8(0xC2A3) & 0xFC) | 0x02;
    /* C29B: bits [5:0]=0x00 */
    XDATA_REG8(0xC29B) &= 0xC0;

    /* C32A: bits [3:0]=0x0D */
    XDATA_REG8(0xC32A) = (XDATA_REG8(0xC32A) & 0xF0) | 0x0D;
    /* C317: bits [4:0]=0x10 */
    XDATA_REG8(0xC317) = (XDATA_REG8(0xC317) & 0xE0) | 0x10;
    /* C313: bits [1:0]=01 */
    XDATA_REG8(0xC313) = (XDATA_REG8(0xC313) & 0xFC) | 0x01;
    /* C303: bits [3:2]=01 */
    XDATA_REG8(0xC303) = (XDATA_REG8(0xC303) & 0xF3) | 0x04;
    /* C326: bits [3:0]=0x0B */
    XDATA_REG8(0xC326) = (XDATA_REG8(0xC326) & 0xF0) | 0x0B;
    /* C324: bits [3:0]=0x07 */
    XDATA_REG8(0xC324) = (XDATA_REG8(0xC324) & 0xF0) | 0x07;
    /* C323: bits [1:0]=10 */
    XDATA_REG8(0xC323) = (XDATA_REG8(0xC323) & 0xFC) | 0x02;
    /* C31B: bits [5:0]=0x00 */
    XDATA_REG8(0xC31B) &= 0xC0;

    /* === Conditional: if C8FF >= 5, apply extra config === */
    tmp = XDATA_REG8(0xC8FF);
    if (tmp >= 0x05) {
        /* C294: bits [3:0]=0x06 */
        XDATA_REG8(0xC294) = (XDATA_REG8(0xC294) & 0xF0) | 0x06;
        /* C297: set bits [4:0]=0x11 */
        XDATA_REG8(0xC297) = (XDATA_REG8(0xC297) & 0xE0) | 0x11;
        /* C314: bits [3:0]=0x06 */
        XDATA_REG8(0xC314) = (XDATA_REG8(0xC314) & 0xF0) | 0x06;
        /* C317: set bits [4:0]=0x11 */
        XDATA_REG8(0xC317) = (XDATA_REG8(0xC317) & 0xE0) | 0x11;
    }
}

/*=== Hardware Init (from stock firmware trace) ===*/
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
    REG_CPU_CTRL_CC37 = 0x28;
    REG_PHY_LINK_CTRL = 0x00;
    REG_PHY_TIMER_CTRL_E764 = 0x14; REG_PHY_TIMER_CTRL_E764 = 0x14;
    REG_PHY_TIMER_CTRL_E764 = 0x14; REG_PHY_TIMER_CTRL_E764 = 0x14;
    REG_SYS_CTRL_E76C = 0x04; REG_SYS_CTRL_E774 = 0x04;
    REG_SYS_CTRL_E77C = 0x04;
    REG_INT_AUX_STATUS = 0x02; REG_CPU_EXEC_STATUS_3 = 0x00;
    REG_INT_ENABLE = 0x10;
    REG_INT_STATUS_C800 = 0x04; REG_INT_STATUS_C800 = 0x05;
    REG_TIMER_CTRL_CC3B = 0x0D; REG_TIMER_CTRL_CC3B = 0x0F;
    REG_POWER_CTRL_92C6 = 0x05; REG_POWER_CTRL_92C7 = 0x00;
    REG_USB_CTRL_9201 = 0x0E; REG_USB_CTRL_9201 = 0x0C;
    REG_CLOCK_ENABLE = 0x82; REG_USB_CTRL_920C = 0x61;
    REG_USB_CTRL_920C = 0x60;
    /* Stock firmware (0xC8F7-0xC906): C20C/C208 PHY link config before power-on */
    XDATA_REG8(0xC20C) = (XDATA_REG8(0xC20C) & 0xBF) | 0x40;  /* C20C: set bit 6 */
    XDATA_REG8(0xC208) &= 0xEF;                                  /* C208: clear bit 4 */
    REG_POWER_ENABLE = 0x87;
    REG_CLOCK_ENABLE = 0x83; REG_PHY_POWER = 0x2F;
    REG_USB_PHY_CONFIG_9241 = 0x10; REG_USB_PHY_CONFIG_9241 = 0xD0;
    /* E741/E742/CC43 now handled by serdes_phy_init() called after hw_init */
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
    REG_DMA_STATUS2 = 0x00; REG_DMA_STATUS2 = 0x00;
    REG_DMA_STATUS2 = 0x00; REG_DMA_CTRL = 0x00;
    REG_DMA_STATUS = 0x00; REG_DMA_STATUS = 0x00;
    REG_DMA_STATUS = 0x00; REG_DMA_QUEUE_IDX = 0x00;
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
    REG_USB_MSC_CFG = 0x07; REG_USB_MSC_CFG = 0x07;
    REG_USB_MSC_CFG = 0x07; REG_USB_MSC_CFG = 0x05;
    REG_USB_MSC_CFG = 0x01; REG_USB_MSC_CFG = 0x00;
    REG_USB_MSC_LENGTH = 0x0D;
    REG_POWER_ENABLE = 0x87; REG_USB_PHY_CTRL_91D1 = USB_91D1_ALL;
    REG_BUF_CFG_9300 = 0x0C; REG_BUF_CFG_9301 = 0xC0;
    REG_BUF_CFG_9302 = 0xBF; REG_USB_CTRL_PHASE = 0x1F;
    REG_USB_EP_CFG1 = 0x0F; REG_USB_PHY_CTRL_91C1 = 0xF0;
    REG_BUF_CFG_9303 = 0x33; REG_BUF_CFG_9304 = 0x3F;
    REG_BUF_CFG_9305 = 0x40; REG_USB_CONFIG = 0xE0;
    REG_USB_EP0_LEN_H = 0xF0; REG_USB_MODE = 0x01;
    REG_USB_EP_MGMT = 0x00;
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
    REG_USB_PHY_CTRL_91C3 = 0x00;
    REG_USB_PHY_CTRL_91C0 = 0x13; REG_USB_PHY_CTRL_91C0 = 0x12;
    REG_INT_DMA_CTRL = 0x04; REG_INT_DMA_CTRL = 0x84;
    REG_LINK_MODE_CTRL = 0xFF;
    REG_XFER2_DMA_STATUS = 0x04; REG_XFER2_DMA_STATUS = 0x02;
    REG_XFER2_DMA_CTRL = 0x00; REG_INT_ENABLE = 0x50;
    REG_XFER2_DMA_CTRL = 0x04;
    REG_XFER2_DMA_ADDR_LO = 0x00; REG_XFER2_DMA_ADDR_HI = 0xC8;
    REG_INT_CTRL = 0x08; REG_INT_CTRL = 0x0A; REG_INT_CTRL = 0x0A;
    REG_CPU_EXT_CTRL = 0x40;
    REG_CPU_EXT_STATUS = 0x04; REG_CPU_EXT_STATUS = 0x02;
    REG_XFER_DMA_CTRL = 0x10; REG_XFER_DMA_ADDR_LO = 0x00;
    REG_XFER_DMA_ADDR_HI = 0x0A; REG_XFER_DMA_CMD = 0x01;
    REG_XFER_DMA_CMD = 0x02;
    REG_XFER_DMA_CTRL = 0x10; REG_XFER_DMA_ADDR_LO = 0x00;
    REG_XFER_DMA_ADDR_HI = 0x3C; REG_XFER_DMA_CMD = 0x01;
    REG_XFER_DMA_CMD = 0x02;
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

    /* Stock firmware at 0x925F calls 0xC020 which writes:
     *   E764 (already done above), then CA81 |= 0x01
     * CA81 controls PCIe PHY clock enable — must be set before pcie_init */
    REG_CPU_CTRL_CA81 |= 0x01;
}

void main(void) {
    IE = 0;
    is_usb3 = 0; need_bulk_init = 0; bulk_out_state = 0;
    pd_power_ready_done = 0; phy_unmask_pending = 0; phy_unmask_counter = 0;
    usb_configured = 0;

    /* Initialize XDATA globals — stock firmware's flash_phy_calib_load_new (0x8FCF)
     * sets these defaults even when flash calibration fails (no valid marker).
     * G_STATE_FLAG_0AE3=1 is CRITICAL: prevents phy_poll_registers from
     * writing C655 bit 3 / C623 / C65A bit 3 which interferes with link training. */
    G_STATE_FLAG_0AE3 = 1;           /* Stock: set to 1 at 0x8FDD */
    G_SYSTEM_STATE_0AE2 = 1;         /* Stock: set to 1 at 0x8FD9 */
    G_TLP_INIT_FLAG_0AE5 = 1;        /* Stock: set to 1 at 0x8FE7 */
    G_USB_TRANSFER_FLAG = 0;
    G_PHY_POLL_MODE = 0;
    G_PHY_LANE_POLL_MODE = 0;
    G_LINK_EVENT_0B2D = 0;
    G_STATE_0AE8 = 0x0F;             /* Stock: set to 0x0F at 0x8FED */
    G_FLASH_CFG_0AF0 = 0x00;         /* Stock: set to 0 when 0AE5=1 at 0x91DD */
    G_SERDES_EQ_PARAM = 0x03;        /* Stock: set from init data table at 0x4403 */
    G_SERDES_EQ_MULT = 0x05;         /* Stock: set from init data table at 0x4403 */

    /* Post-calibration register writes from flash_phy_calib_load_new (0x8FCF).
     * Stock firmware does these after setting globals, before pcie_init:
     *   CC35 &= ~0x04  (clear bit 2 of CPU exec status 3)
     *   C65A &= ~0x08  (clear bit 3 of PHY config)
     *   905F &= ~0x10  (clear bit 4 of USB EP control) */
    REG_CPU_EXEC_STATUS_3 &= ~0x04;
    REG_PHY_CFG_C65A &= ~PHY_CFG_C65A_BIT3;
    REG_USB_EP_CTRL_905F &= ~USB_EP_CTRL_905F_BIT4;

    REG_UART_LCR &= 0xF7;
    uart_puts("\n[BOOT]\n");

    /* Ack any pending PHY events from previous boot.
     * Only do W1C acks — don't modify config registers that affect USB3 link. */
    XDATA_REG8(0xE40F) = 0xFF;           /* W1C ack all E40F events */
    XDATA_REG8(0xE410) = 0xFF;           /* W1C ack all E410 events */

    /*
     * PD CC initialization and hard reset — BEFORE hw_init.
     * Stock firmware UART trace shows this order:
     *   [InternalPD_StateInit][CC_state=00][Drive_HardRst]
     *   → PD negotiation (Source_Cap → Accept → PS_RDY)
     *   → USB enumeration
     *   → [RstRxpll...][Done][CDRV ok]
     *
     * The hard reset must happen before USB comes up because it
     * causes the host to restart PD negotiation on the CC pins.
     * If done after USB enumeration, the host resets the USB connection.
     */
    /* CC init BEFORE hw_init — enables CC pin detection for USB-C.
     * Previous USB2 fallback was caused by aggressive boot-time suppression
     * (92C2/E40B/C80A writes), not CC init itself. */
    uart_puts("[CC init]\n");
    pd_cc_controller_init();
    XDATA_REG8(0xE40F) = 0xFF;
    XDATA_REG8(0xE410) = 0xFF;

    hw_init();
    phy_link_ctrl_init();
    serdes_phy_init();
    pcie_init();

    /* Pre-USB link training poll:
     * D92E + CC83/CCDD + RstRxpll already done in pcie_init().
     * 9090 bit 7 was cleared in pcie_init (D92E sequence).
     * Just poll for link training events before USB enumeration. */
    uart_puts("[LT pre-USB]\n");
    /* Ensure 9090 bit 7 is still clear for PHY events */
    REG_USB_INT_MASK_9090 &= 0x7F;

    /* Dump key registers to understand PHY state */
    uart_puts("[9090="); uart_puthex(REG_USB_INT_MASK_9090);
    uart_puts("][C80A="); uart_puthex(REG_INT_PCIE_NVME);
    uart_puts("][C809="); uart_puthex(REG_INT_CTRL);
    uart_puts("][E40B="); uart_puthex(REG_CMD_CONFIG);
    uart_puts("][E400="); uart_puthex(REG_CMD_CTRL_E400);
    uart_puts("]\n");

    { uint16_t lt_timeout;
      for (lt_timeout = 0; lt_timeout < 50000; lt_timeout++) {
          uint8_t e40f = REG_PHY_EVENT_E40F;
          uint8_t e410 = REG_PHY_INT_STATUS_E410;
          uint8_t ltssm = REG_PCIE_LTSSM_STATE;
          if (e40f || e410) {
              uart_puts("[E:");
              uart_puthex(e40f);
              uart_putc('/');
              uart_puthex(e410);
              uart_puts(" B=");
              uart_puthex(ltssm);
              uart_puts("]\n");
              /* Dispatch PHY events */
              phy_event_dispatcher();
          }
          if (ltssm >= 0x10) {  /* Polling or beyond */
              uart_puts("[TRAINED B450=");
              uart_puthex(ltssm);
              uart_puts("]\n");
              break;
          }
          REG_CPU_KEEPALIVE = 0x0C;
      }
      uart_puts("[LT done B450=");
      uart_puthex(REG_PCIE_LTSSM_STATE);
      uart_puts("][E40F=");
      uart_puthex(REG_PHY_EVENT_E40F);
      uart_puts("][E410=");
      uart_puthex(REG_PHY_INT_STATUS_E410);
      uart_puts("]\n");
    }
    REG_USB_INT_MASK_9090 |= 0x80;  /* re-set bit 7 for USB */

    /* Suppress any remaining PHY events */
    XDATA_REG8(0xE40F) = 0xFF;
    XDATA_REG8(0xE410) = 0xFF;

    /* TLP engine verified: CfgRd0 returns 24641B21 (ASM2464 bridge) with B298 bit 4.
     * B213/B216 not needed here — set by individual config read functions. */
    uart_puts("[B298="); uart_puthex(XDATA_REG8(0xB298)); uart_puts("]\n");

    uint8_t link = REG_USB_LINK_STATUS;
    is_usb3 = (link >= USB_SPEED_SUPER) ? 1 : 0;
    uart_puts("[link="); uart_puthex(link); uart_puts("]\n");

    uart_puts("[GO]\n");
    TCON = 0x04;  /* IT0=0 (level-triggered INT0) */
    IE = IE_EA | IE_EX0 | IE_EX1 | IE_ET0;

    while (1) {
        REG_CPU_KEEPALIVE = 0x0C;
        poll_bulk_events();

        if (need_bulk_init) { need_bulk_init = 0; do_bulk_init(); }
        if (need_cbw_process) { need_cbw_process = 0; handle_cbw(); }

        if (usb_configured && !pd_power_ready_done) {
            pd_power_ready_done = 1;
            /* Enable PCIe TLP bridge — must be after USB enumeration to avoid
             * interfering with host xHCI during early boot. B298 bit 4 enables
             * the tunnel so TLP completions return data. */
            XDATA_REG8(0xB430) &= 0xFE;
            XDATA_REG8(0xB298) = (XDATA_REG8(0xB298) & 0xEF) | 0x10;
            uart_puts("[rdy B450=");
            uart_puthex(REG_PCIE_LTSSM_STATE);
            uart_puts("]\n");
        }

        /* Continuous PHY maintenance — stock firmware calls this every iteration
         * via lcall 0x04f3 -> 0xC5A1 (phy_maintenance). */
        phy_maintenance();

        /* PHY event dispatch */
        phy_event_dispatcher();

        if (bulk_out_state == 1) {
            REG_USB_EP_CFG1 = USB_EP_CFG1_ARM_OUT;
            REG_USB_EP_CFG2 = USB_EP_CFG2_ARM_OUT;
            bulk_out_state = 2;
        } else if (bulk_out_state == 2) {
            uint8_t st = REG_USB_PERIPH_STATUS;
            if (st & USB_PERIPH_BULK_DATA) {
                REG_USB_EP_CFG1 = USB_EP_CFG1_ARM_OUT;
                REG_INT_AUX_STATUS = (REG_INT_AUX_STATUS & 0xF9) | 0x02;
                REG_BULK_DMA_HANDSHAKE = 0x00;
                while (!(REG_USB_DMA_STATE & USB_DMA_STATE_READY)) { }
                { uint8_t ci;
                  for (ci = 0; ci < bulk_out_len; ci++)
                      XDATA_REG8(bulk_out_addr + ci) = XDATA_REG8(0x7000 + ci); }
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
