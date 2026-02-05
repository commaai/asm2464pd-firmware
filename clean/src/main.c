/*
 * ASM2464PD USB Enumeration Firmware
 * Minimal firmware that enumerates as a USB 3.0 device
 */

#include "types.h"
#include "registers.h"

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

/*==========================================================================
 * USB Control Transfer Helpers
 *==========================================================================*/

/* Wait for status phase and complete (used for no-data requests) */
static void complete_no_data_request(void) {
    while (!(REG_USB_CTRL_PHASE & USB_CTRL_PHASE_STATUS)) { }
    REG_USB_DMA_TRIGGER = USB_DMA_STATUS_COMPLETE;
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_STATUS;
}

/* Send descriptor data and complete status phase */
static void send_descriptor(uint8_t len) {
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
    
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_DATA;
    while (!(REG_USB_CTRL_PHASE & USB_CTRL_PHASE_STATUS)) { }
    REG_USB_DMA_TRIGGER = USB_DMA_STATUS_COMPLETE;
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_STATUS;
}

/*==========================================================================
 * USB Request Handlers
 *==========================================================================*/

static void handle_set_address(void) {
    uint8_t tmp;
    
    while (!(REG_USB_CTRL_PHASE & USB_CTRL_PHASE_STATUS)) { }
    
    (void)REG_USB_INT_MASK_9090;
    REG_USB_INT_MASK_9090 = 0x01;
    REG_USB_EP_CTRL_91D0 = 0x02;
    (void)REG_USB_LINK_STATUS;
    (void)XDATA_REG8(0x92F8);
    (void)XDATA_REG8(0x92F8);
    tmp = XDATA_REG8(0xE716);
    XDATA_REG8(0xE716) = 0x01;
    (void)REG_USB_CONFIG;
    
    while (!(REG_USB_CTRL_PHASE & USB_CTRL_PHASE_STATUS)) { }
    
    /* Endpoint setup sequence */
    (void)XDATA_REG8(0x9206); XDATA_REG8(0x9206) = 0x03;
    (void)XDATA_REG8(0x9207); XDATA_REG8(0x9207) = 0x03;
    (void)XDATA_REG8(0x9206); XDATA_REG8(0x9206) = 0x07;
    (void)XDATA_REG8(0x9207); XDATA_REG8(0x9207) = 0x07;
    (void)XDATA_REG8(0x92F8);
    (void)XDATA_REG8(0x9206); XDATA_REG8(0x9206) = 0x07;
    (void)XDATA_REG8(0x9207); XDATA_REG8(0x9207) = 0x07;
    (void)XDATA_REG8(0x92F8);
    
    XDATA_REG8(0x9208) = 0x00; XDATA_REG8(0x9209) = 0x0A;
    XDATA_REG8(0x920A) = 0x00; XDATA_REG8(0x920B) = 0x0A;
    
    tmp = XDATA_REG8(0x9202); XDATA_REG8(0x9202) = tmp;
    tmp = XDATA_REG8(0x9220); XDATA_REG8(0x9220) = 0x04;
    
    REG_USB_DMA_TRIGGER = USB_DMA_STATUS_COMPLETE;
    REG_USB_CTRL_PHASE = USB_CTRL_PHASE_STATUS;
}

static void handle_get_descriptor(uint8_t type, uint8_t idx, uint8_t len) {
    uint8_t actual_len;
    __xdata uint8_t *buf = (__xdata uint8_t *)USB_CTRL_BUF_BASE;
    
    while (!(REG_USB_CTRL_PHASE & USB_CTRL_PHASE_DATA)) { }
    (void)REG_USB_LINK_STATUS;
    
    if (type == USB_DESC_TYPE_DEVICE) {
        /* 18-byte Device Descriptor */
        buf[0] = 0x12; buf[1] = 0x01;
        buf[2] = 0x20; buf[3] = 0x03;  /* USB 3.2 */
        buf[4] = 0x00; buf[5] = 0x00;
        buf[6] = 0x00; buf[7] = 0x09;  /* 512B max packet */
        buf[8] = 0x22; buf[9] = 0x11;  /* VID 0x1122 */
        buf[10] = 0x44; buf[11] = 0x33; /* PID 0x3344 */
        buf[12] = 0x01; buf[13] = 0x00;
        buf[14] = 0x01; buf[15] = 0x02;
        buf[16] = 0x03; buf[17] = 0x01;
        actual_len = (len < 18) ? len : 18;
        
    } else if (type == USB_DESC_TYPE_BOS) {
        /* 22-byte BOS Descriptor */
        buf[0] = 0x05; buf[1] = 0x0F;
        buf[2] = 0x16; buf[3] = 0x00;
        buf[4] = 0x02;
        /* USB 2.0 Extension */
        buf[5] = 0x07; buf[6] = 0x10;
        buf[7] = 0x02; buf[8] = 0x02;
        buf[9] = 0x00; buf[10] = 0x00;
        buf[11] = 0x00;
        /* SuperSpeed Capability */
        buf[12] = 0x0A; buf[13] = 0x10;
        buf[14] = 0x03; buf[15] = 0x00;
        buf[16] = 0x0E; buf[17] = 0x00;
        buf[18] = 0x03; buf[19] = 0x00;
        buf[20] = 0x00; buf[21] = 0x00;
        actual_len = (len < 22) ? len : 22;
        
    } else if (type == USB_DESC_TYPE_CONFIG) {
        /* 9-byte Config Descriptor (no interfaces) */
        buf[0] = 0x09; buf[1] = 0x02;
        buf[2] = 0x09; buf[3] = 0x00;
        buf[4] = 0x00; buf[5] = 0x01;
        buf[6] = 0x00; buf[7] = 0x80;
        buf[8] = 0x32;
        actual_len = (len < 9) ? len : 9;
        
    } else if (type == USB_DESC_TYPE_STRING) {
        if (idx == 0) {
            buf[0] = 0x04; buf[1] = 0x03;
            buf[2] = 0x09; buf[3] = 0x04;
            actual_len = (len < 4) ? len : 4;
        } else if (idx == 1) {
            buf[0] = 0x0A; buf[1] = 0x03;
            buf[2] = 't'; buf[3] = 0;
            buf[4] = 'i'; buf[5] = 0;
            buf[6] = 'n'; buf[7] = 0;
            buf[8] = 'y'; buf[9] = 0;
            actual_len = (len < 10) ? len : 10;
        } else if (idx == 2) {
            buf[0] = 0x08; buf[1] = 0x03;
            buf[2] = 'u'; buf[3] = 0;
            buf[4] = 's'; buf[5] = 0;
            buf[6] = 'b'; buf[7] = 0;
            actual_len = (len < 8) ? len : 8;
        } else {
            buf[0] = 0x08; buf[1] = 0x03;
            buf[2] = '0'; buf[3] = 0;
            buf[4] = '0'; buf[5] = 0;
            buf[6] = '1'; buf[7] = 0;
            actual_len = (len < 8) ? len : 8;
        }
    } else {
        return; /* Unknown descriptor */
    }
    
    send_descriptor(actual_len);
}

/*==========================================================================
 * Interrupt Handlers
 *==========================================================================*/

void int0_isr(void) __interrupt(0) {
    uint8_t status, phase;
    uint8_t bmReq, bReq, wValL, wValH, wLenL;
    
    (void)REG_INT_USB_STATUS;
    
    status = REG_USB_PERIPH_STATUS;
    (void)REG_USB_PERIPH_STATUS;
    (void)REG_USB_PERIPH_STATUS;
    (void)REG_USB_PERIPH_STATUS;
    
    phase = REG_USB_CTRL_PHASE;
    (void)REG_USB_CTRL_PHASE;
    
    if (status & USB_PERIPH_DESC_REQ) {
        if ((phase & USB_CTRL_PHASE_STATUS) && !(phase & USB_CTRL_PHASE_SETUP)) {
            REG_USB_CTRL_PHASE = USB_CTRL_PHASE_STATUS;
        } else if (phase & USB_CTRL_PHASE_SETUP) {
            uint8_t tmp = REG_USB_CONFIG;
            REG_USB_CONFIG = tmp;
            (void)XDATA_REG8(0x9220);
            REG_USB_CTRL_PHASE = USB_CTRL_PHASE_SETUP;
            
            bmReq = REG_USB_SETUP_BMREQ;
            bReq  = REG_USB_SETUP_BREQ;
            wValL = REG_USB_SETUP_WVAL_L;
            wValH = REG_USB_SETUP_WVAL_H;
            (void)REG_USB_SETUP_WIDX_L;
            (void)REG_USB_SETUP_WIDX_H;
            wLenL = REG_USB_SETUP_WLEN_L;
            (void)REG_USB_SETUP_WLEN_H;
            
            /* Debug: show USB request */
            uart_puts("U:");
            uart_puthex(bReq);
            uart_putc('\n');
            
            if (bmReq == 0x00 && bReq == USB_REQ_SET_ADDRESS) {
                handle_set_address();
            } else if (bmReq == 0x80 && bReq == USB_REQ_GET_DESCRIPTOR) {
                handle_get_descriptor(wValH, wValL, wLenL);
            } else if (bmReq == 0x00 && (bReq == USB_REQ_SET_CONFIGURATION || bReq == 0x31)) {
                complete_no_data_request();
            }
        }
    }
    
    (void)REG_INT_SYSTEM;
    (void)REG_INT_USB_STATUS;
}

void int1_isr(void) __interrupt(2) {
    uint8_t sys = REG_INT_SYSTEM;  /* Read clears interrupt source */
    
    uart_puts("I1:");
    uart_puthex(sys);
    uart_putc('\n');
    
    /* Trace reads these registers to handle INT1 */
    (void)XDATA_REG8(0xCC23);  /* Timer3 CSR */
    (void)XDATA_REG8(0xCC91);  /* DMA int */
    XDATA_REG8(0xCC91) = XDATA_REG8(0xCC91);
    (void)XDATA_REG8(0xCC99);
    (void)XDATA_REG8(0xCCD9);
    (void)XDATA_REG8(0xCCF9);
    (void)REG_CPU_EXEC_STATUS_2;
    (void)REG_INT_PCIE_NVME;
    (void)REG_INT_PCIE_NVME;
    (void)REG_INT_PCIE_NVME;
    (void)REG_INT_PCIE_NVME;
    (void)REG_INT_SYSTEM;  /* Should be 0 now */
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
    uint8_t event;
    
    IE = 0;
    REG_UART_LCR &= 0xF7;
    uart_puts("\n[BOOT]\n");
    
    hw_init();
    
    uart_puts("[GO]\n");
    
    TCON = 0;  /* Level-triggered interrupts */
    IE = IE_EA | IE_EX0 | IE_EX1;
    
    while (1) {
        /* Handle power events in main loop (per trace) */
        event = REG_POWER_EVENT_92E1;
        if (event) {
            REG_POWER_EVENT_92E1 = 0x60;
            REG_POWER_STATUS = REG_POWER_STATUS & 0x3F;
        }
        
        XDATA_REG8(0xCC2A) = 0x0C;
        (void)REG_POWER_STATUS_92F7;
    }
}
