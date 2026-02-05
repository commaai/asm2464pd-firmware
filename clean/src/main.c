/*
 * USB Enumeration Firmware for ASM2464PD
 * Init sequence extracted directly from trace/enumerate_min
 */

#include "types.h"
#include "registers.h"

__sfr __at(0xA8) IE;
__sfr __at(0x88) TCON;  /* Timer Control - controls INT0/INT1 edge/level triggering */
#define IE_EA   0x80
#define IE_EX1  0x04
#define IE_ET0  0x02
#define IE_EX0  0x01

/* TCON bits for external interrupts:
 * IT0 (bit 0) - INT0 type: 0=level, 1=edge triggered
 * IE0 (bit 1) - INT0 edge flag (set by hardware when edge detected)
 * IT1 (bit 2) - INT1 type: 0=level, 1=edge triggered
 * IE1 (bit 3) - INT1 edge flag (set by hardware when edge detected)
 */
#define TCON_IT0 0x01
#define TCON_IE0 0x02
#define TCON_IT1 0x04
#define TCON_IE1 0x08

void uart_putc(uint8_t ch) { REG_UART_THR = ch; }
void uart_puts(__code const char *str) { while (*str) uart_putc(*str++); }

static void uart_puthex(uint8_t val) {
    const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[val >> 4]);
    uart_putc(hex[val & 0x0F]);
}

/* USB peripheral status bits at 0x9101 */
#define USB_STATUS_SETUP    0x02  /* Setup packet received */
#define USB_STATUS_BUF      0x08  /* Buffer status (bit 3) - clear via 0x9301 */
#define USB_STATUS_LINK     0x10  /* Link status change */

/* USB standard requests */
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_REQ_SET_ADDRESS     0x05
#define USB_REQ_SET_CONFIG      0x09
#define USB_REQ_SET_SEL         0x30  /* USB 3.0: Set System Exit Latency */
#define USB_REQ_SET_ISOCH_DELAY 0x31  /* USB 3.0: Set Isochronous Delay */

/* USB Descriptor Types */
#define DESC_TYPE_DEVICE        0x01
#define DESC_TYPE_CONFIG        0x02
#define DESC_TYPE_STRING        0x03
#define DESC_TYPE_BOS           0x0F

/* Handle SET_ADDRESS request - from trace lines 5095-5146 */
static void handle_set_address(uint8_t addr) {
    uint8_t phase, tmp;
    (void)addr;
    
    /* 0x9091 = 0x01 already written in ISR */
    
    /* Wait for status phase (0x10) - SET_ADDRESS has no data phase */
    do {
        phase = XDATA_REG8(0x9091);
    } while (!(phase & 0x10));
    
    /* From trace: read 0x9090, write 0x01 */
    (void)XDATA_REG8(0x9090);
    XDATA_REG8(0x9090) = 0x01;
    XDATA_REG8(0x91D0) = 0x02;
    
    /* Read 0x9100 (link status) */
    (void)XDATA_REG8(0x9100);
    
    /* Read 0x92F8 twice */
    (void)XDATA_REG8(0x92F8);
    (void)XDATA_REG8(0x92F8);
    
    /* Read/write E716 - set to 0x01 only */
    tmp = XDATA_REG8(0xE716);
    XDATA_REG8(0xE716) = 0x01;
    
    /* Read 0x9002 */
    (void)XDATA_REG8(0x9002);
    
    /* Poll 0x9091 for status phase ready */
    do {
        phase = XDATA_REG8(0x9091);
    } while (!(phase & 0x10));
    
    /* Endpoint address setup - read/modify/write pattern from trace */
    tmp = XDATA_REG8(0x9206);
    XDATA_REG8(0x9206) = 0x03;
    tmp = XDATA_REG8(0x9207);
    XDATA_REG8(0x9207) = 0x03;
    tmp = XDATA_REG8(0x9206);
    XDATA_REG8(0x9206) = 0x07;
    tmp = XDATA_REG8(0x9207);
    XDATA_REG8(0x9207) = 0x07;
    
    /* Read 0x92F8 */
    (void)XDATA_REG8(0x92F8);
    
    /* More endpoint setup */
    tmp = XDATA_REG8(0x9206);
    XDATA_REG8(0x9206) = 0x07;
    tmp = XDATA_REG8(0x9207);
    XDATA_REG8(0x9207) = 0x07;
    
    /* Read 0x92F8 */
    (void)XDATA_REG8(0x92F8);
    
    /* Endpoint interval setup */
    XDATA_REG8(0x9208) = 0x00;
    XDATA_REG8(0x9209) = 0x0A;
    XDATA_REG8(0x920A) = 0x00;
    XDATA_REG8(0x920B) = 0x0A;
    
    /* Read/write 0x9202 */
    tmp = XDATA_REG8(0x9202);
    XDATA_REG8(0x9202) = tmp;
    
    /* Read/write 0x9220 */
    tmp = XDATA_REG8(0x9220);
    XDATA_REG8(0x9220) = 0x04;
    
    /* Complete - DMA complete, ack status */
    XDATA_REG8(0x9092) = 0x08;
    XDATA_REG8(0x9091) = 0x10;
    
    uart_puts("[A]\n");
}

/* Send descriptor data - common code for all descriptor types */
static void send_descriptor_data(uint8_t len) {
    /* Read 0x9100 */
    (void)XDATA_REG8(0x9100);
    
    /* Read 0x9002 */
    (void)XDATA_REG8(0x9002);
    
    /* Read 0x9091 three times */
    (void)XDATA_REG8(0x9091);
    (void)XDATA_REG8(0x9091);
    (void)XDATA_REG8(0x9091);
    
    /* Set length and trigger DMA */
    XDATA_REG8(0x9003) = 0x00;
    XDATA_REG8(0x9004) = len;
    XDATA_REG8(0x9092) = 0x04;
    
    /* Wait for DMA complete */
    while (XDATA_REG8(0x9092) != 0x00) { }
    
    /* Read status registers */
    (void)XDATA_REG8(0x9003);
    (void)XDATA_REG8(0x9004);
    (void)XDATA_REG8(0x9003);
    (void)XDATA_REG8(0x9004);
    
    /* Ack data phase */
    XDATA_REG8(0x9091) = 0x08;
    
    /* Wait for status phase ready */
    while (!(XDATA_REG8(0x9091) & 0x10)) { }
    
    /* Complete status phase */
    XDATA_REG8(0x9092) = 0x08;
    XDATA_REG8(0x9091) = 0x10;
}

/* Handle GET_DESCRIPTOR request */
static void handle_get_descriptor(uint8_t desc_type, uint8_t desc_idx, uint8_t len) {
    uint8_t actual_len;
    (void)desc_idx;
    
    /* Wait for data phase ready (0x9091 bit 3 set) */
    while (!(XDATA_REG8(0x9091) & 0x08)) { }
    
    /* Read 0x9100 */
    (void)XDATA_REG8(0x9100);
    
    if (desc_type == DESC_TYPE_DEVICE) {
        /* Write full 18-byte device descriptor */
        XDATA_REG8(0x9E00) = 0x12;  /* bLength = 18 */
        XDATA_REG8(0x9E01) = 0x01;  /* bDescriptorType = DEVICE */
        XDATA_REG8(0x9E02) = 0x20;  /* bcdUSB = 0x0320 (USB 3.2) */
        XDATA_REG8(0x9E03) = 0x03;
        XDATA_REG8(0x9E04) = 0x00;  /* bDeviceClass */
        XDATA_REG8(0x9E05) = 0x00;  /* bDeviceSubClass */
        XDATA_REG8(0x9E06) = 0x00;  /* bDeviceProtocol */
        XDATA_REG8(0x9E07) = 0x09;  /* bMaxPacketSize0 = 2^9 = 512 */
        XDATA_REG8(0x9E08) = 0x22;  /* idVendor = 0x1122 */
        XDATA_REG8(0x9E09) = 0x11;
        XDATA_REG8(0x9E0A) = 0x44;  /* idProduct = 0x3344 */
        XDATA_REG8(0x9E0B) = 0x33;
        XDATA_REG8(0x9E0C) = 0x01;  /* bcdDevice = 0x0001 */
        XDATA_REG8(0x9E0D) = 0x00;
        XDATA_REG8(0x9E0E) = 0x01;  /* iManufacturer = 1 */
        XDATA_REG8(0x9E0F) = 0x02;  /* iProduct = 2 */
        XDATA_REG8(0x9E10) = 0x03;  /* iSerialNumber = 3 */
        XDATA_REG8(0x9E11) = 0x01;  /* bNumConfigurations = 1 */
        
        actual_len = (len < 18) ? len : 18;
        send_descriptor_data(actual_len);
        uart_puts("[D]\n");
        
    } else if (desc_type == DESC_TYPE_BOS) {
        /* BOS descriptor - required for USB 3.0 */
        /* Total length = 22 bytes (5 header + 7 USB2 ext + 10 SS cap) */
        XDATA_REG8(0x9E00) = 0x05;  /* bLength = 5 */
        XDATA_REG8(0x9E01) = 0x0F;  /* bDescriptorType = BOS */
        XDATA_REG8(0x9E02) = 0x16;  /* wTotalLength = 22 */
        XDATA_REG8(0x9E03) = 0x00;
        XDATA_REG8(0x9E04) = 0x02;  /* bNumDeviceCaps = 2 */
        
        /* USB 2.0 Extension Capability (7 bytes) */
        XDATA_REG8(0x9E05) = 0x07;  /* bLength */
        XDATA_REG8(0x9E06) = 0x10;  /* bDescriptorType = DEVICE CAPABILITY */
        XDATA_REG8(0x9E07) = 0x02;  /* bDevCapabilityType = USB 2.0 EXTENSION */
        XDATA_REG8(0x9E08) = 0x02;  /* bmAttributes: LPM supported */
        XDATA_REG8(0x9E09) = 0x00;
        XDATA_REG8(0x9E0A) = 0x00;
        XDATA_REG8(0x9E0B) = 0x00;
        
        /* SuperSpeed USB Device Capability (10 bytes) */
        XDATA_REG8(0x9E0C) = 0x0A;  /* bLength */
        XDATA_REG8(0x9E0D) = 0x10;  /* bDescriptorType = DEVICE CAPABILITY */
        XDATA_REG8(0x9E0E) = 0x03;  /* bDevCapabilityType = SUPERSPEED_USB */
        XDATA_REG8(0x9E0F) = 0x00;  /* bmAttributes */
        XDATA_REG8(0x9E10) = 0x0E;  /* wSpeedsSupported: SS, HS, FS */
        XDATA_REG8(0x9E11) = 0x00;
        XDATA_REG8(0x9E12) = 0x03;  /* bFunctionalitySupport: FS */
        XDATA_REG8(0x9E13) = 0x00;  /* bU1DevExitLat */
        XDATA_REG8(0x9E14) = 0x00;  /* wU2DevExitLat */
        XDATA_REG8(0x9E15) = 0x00;
        
        actual_len = (len < 22) ? len : 22;
        send_descriptor_data(actual_len);
        uart_puts("[B]\n");
        
    } else if (desc_type == DESC_TYPE_CONFIG) {
        /* Configuration descriptor - minimal config with no interfaces/endpoints */
        /* Total length = 9 bytes (just config descriptor header) */
        XDATA_REG8(0x9E00) = 0x09;  /* bLength = 9 */
        XDATA_REG8(0x9E01) = 0x02;  /* bDescriptorType = CONFIGURATION */
        XDATA_REG8(0x9E02) = 0x09;  /* wTotalLength = 9 (just this descriptor) */
        XDATA_REG8(0x9E03) = 0x00;
        XDATA_REG8(0x9E04) = 0x00;  /* bNumInterfaces = 0 */
        XDATA_REG8(0x9E05) = 0x01;  /* bConfigurationValue = 1 */
        XDATA_REG8(0x9E06) = 0x00;  /* iConfiguration = 0 */
        XDATA_REG8(0x9E07) = 0x80;  /* bmAttributes = bus powered */
        XDATA_REG8(0x9E08) = 0x32;  /* bMaxPower = 100mA */
        
        actual_len = (len < 9) ? len : 9;
        send_descriptor_data(actual_len);
        uart_puts("[C]\n");
        
    } else if (desc_type == DESC_TYPE_STRING) {
        if (desc_idx == 0) {
            /* String descriptor 0: Language ID */
            XDATA_REG8(0x9E00) = 0x04;  /* bLength = 4 */
            XDATA_REG8(0x9E01) = 0x03;  /* bDescriptorType = STRING */
            XDATA_REG8(0x9E02) = 0x09;  /* English (US) */
            XDATA_REG8(0x9E03) = 0x04;
            actual_len = (len < 4) ? len : 4;
        } else if (desc_idx == 1) {
            /* String 1: Manufacturer = "tiny" */
            XDATA_REG8(0x9E00) = 0x0A;  /* bLength = 10 */
            XDATA_REG8(0x9E01) = 0x03;  /* bDescriptorType = STRING */
            XDATA_REG8(0x9E02) = 't'; XDATA_REG8(0x9E03) = 0x00;
            XDATA_REG8(0x9E04) = 'i'; XDATA_REG8(0x9E05) = 0x00;
            XDATA_REG8(0x9E06) = 'n'; XDATA_REG8(0x9E07) = 0x00;
            XDATA_REG8(0x9E08) = 'y'; XDATA_REG8(0x9E09) = 0x00;
            actual_len = (len < 10) ? len : 10;
        } else if (desc_idx == 2) {
            /* String 2: Product = "usb" */
            XDATA_REG8(0x9E00) = 0x08;  /* bLength = 8 */
            XDATA_REG8(0x9E01) = 0x03;  /* bDescriptorType = STRING */
            XDATA_REG8(0x9E02) = 'u'; XDATA_REG8(0x9E03) = 0x00;
            XDATA_REG8(0x9E04) = 's'; XDATA_REG8(0x9E05) = 0x00;
            XDATA_REG8(0x9E06) = 'b'; XDATA_REG8(0x9E07) = 0x00;
            actual_len = (len < 8) ? len : 8;
        } else if (desc_idx == 3) {
            /* String 3: Serial = "001" */
            XDATA_REG8(0x9E00) = 0x08;  /* bLength = 8 */
            XDATA_REG8(0x9E01) = 0x03;  /* bDescriptorType = STRING */
            XDATA_REG8(0x9E02) = '0'; XDATA_REG8(0x9E03) = 0x00;
            XDATA_REG8(0x9E04) = '0'; XDATA_REG8(0x9E05) = 0x00;
            XDATA_REG8(0x9E06) = '1'; XDATA_REG8(0x9E07) = 0x00;
            actual_len = (len < 8) ? len : 8;
        } else {
            /* Unknown string index - return empty */
            XDATA_REG8(0x9E00) = 0x02;
            XDATA_REG8(0x9E01) = 0x03;
            actual_len = 2;
        }
        send_descriptor_data(actual_len);
        uart_puts("[S]\n");
        
    } else {
        uart_puts("[D?]\n");
    }
}

/* INT0 ISR - USB interrupt */
void int0_isr(void) __interrupt(0) {
    uint8_t periph_status, tmp, tmp9091;
    
    /* Trace: Read 0xC802 first */
    (void)REG_INT_USB_STATUS;
    
    /* Trace: Read 0x9101 FOUR times */
    periph_status = XDATA_REG8(0x9101);
    (void)XDATA_REG8(0x9101);
    (void)XDATA_REG8(0x9101);
    (void)XDATA_REG8(0x9101);
    
    /* Trace: Read 0x9091 TWICE - ALWAYS, before checking event type */
    tmp9091 = XDATA_REG8(0x9091);
    (void)XDATA_REG8(0x9091);
    
    /* Handle SETUP interrupt */
    if (periph_status & USB_STATUS_SETUP) {
        /* Check if this is status phase only (0x10 set, 0x01 not set) */
        if ((tmp9091 & 0x10) && !(tmp9091 & 0x01)) {
            /* Status phase - just clear it by writing 0x9091 = 0x10 */
            XDATA_REG8(0x9091) = 0x10;
            /* Don't process as setup packet */
        } else if (tmp9091 & 0x01) {
            /* Real setup phase */
            uart_puts("[I:");
            uart_puthex(periph_status);
            uart_puts("/91=");
            uart_puthex(tmp9091);
            uart_puts("]\n");
        /* Setup packet received */
        uint8_t bmReq, bReq, wValL, wValH, wIdxL, wIdxH, wLenL, wLenH;
        uint8_t tmp9002, tmp9220;
        
        /* Trace sequence:
         * 1. Read 0x9091 twice - DONE ABOVE
         * 2. Read 0x9002, write it back
         * 3. Read 0x9220
         * 4. Write 0x9091 = 0x01
         * 5. Read setup packet (ALL 8 bytes) */
        tmp9002 = XDATA_REG8(0x9002);
        XDATA_REG8(0x9002) = tmp9002;
        tmp9220 = XDATA_REG8(0x9220);
        XDATA_REG8(0x9091) = 0x01;
        
        /* Read ALL 8 bytes of setup packet ONCE */
        bmReq = XDATA_REG8(0x9104);
        bReq  = XDATA_REG8(0x9105);
        wValL = XDATA_REG8(0x9106);
        wValH = XDATA_REG8(0x9107);
        wIdxL = XDATA_REG8(0x9108);
        wIdxH = XDATA_REG8(0x9109);
        wLenL = XDATA_REG8(0x910A);
        wLenH = XDATA_REG8(0x910B);
        
        /* Debug - show ALL setup packet bytes */
        uart_puts("[S:");
        uart_puthex(bmReq);
        uart_puthex(bReq);
        uart_puthex(wValL);
        uart_puthex(wValH);
        uart_puthex(wIdxL);
        uart_puthex(wIdxH);
        uart_puthex(wLenL);
        uart_puthex(wLenH);
        uart_putc(']');
        
        /* Handle different requests */
        if (bmReq == 0x00 && bReq == USB_REQ_SET_ADDRESS) {
            handle_set_address(wValL);
        } else if (bmReq == 0x80 && bReq == USB_REQ_GET_DESCRIPTOR) {
            uint8_t wLenL = XDATA_REG8(0x910A);
            handle_get_descriptor(wValH, wValL, wLenL);
        } else if (bmReq == 0x00 && bReq == USB_REQ_SET_ISOCH_DELAY) {
            /* USB 3.0 SET_ISOCH_DELAY - no data phase, just ACK */
            /* wValue contains the delay in nanoseconds */
            uart_puts("[ISO:");
            uart_puthex(wValH);
            uart_puthex(wValL);
            uart_puts("]\n");
            
            /* Handle like SET_ADDRESS - wait for status phase and complete */
            {
                uint8_t phase;
                /* Wait for status phase ready */
                do { phase = XDATA_REG8(0x9091); } while (!(phase & 0x10));
                /* Complete */
                XDATA_REG8(0x9092) = 0x08;
                XDATA_REG8(0x9091) = 0x10;
            }
        } else if (bmReq == 0x00 && bReq == USB_REQ_SET_SEL) {
            /* USB 3.0 SET_SEL - has 6-byte data phase */
            uart_puts("[SEL]\n");
            /* TODO: receive 6 bytes of SEL data and ACK */
            /* For now, just try to complete status */
            {
                uint8_t phase;
                do { phase = XDATA_REG8(0x9091); } while (!(phase & 0x10));
                XDATA_REG8(0x9092) = 0x08;
                XDATA_REG8(0x9091) = 0x10;
            }
        } else if (bmReq == 0x00 && bReq == USB_REQ_SET_CONFIG) {
            /* SET_CONFIGURATION - no data phase, just ACK */
            uart_puts("[CFG:");
            uart_puthex(wValL);
            uart_puts("]\n");
            
            /* Handle like SET_ADDRESS - wait for status phase and complete */
            {
                uint8_t phase;
                do { phase = XDATA_REG8(0x9091); } while (!(phase & 0x10));
                XDATA_REG8(0x9092) = 0x08;
                XDATA_REG8(0x9091) = 0x10;
            }
        } else {
            uart_puts("[?:");
            uart_puthex(bmReq);
            uart_puthex(bReq);
            uart_puts("]\n");
        }
        }  /* end else if (tmp9091 & 0x01) - real setup phase */
    }  /* end if (periph_status & USB_STATUS_SETUP) */
    
    if (periph_status & USB_STATUS_BUF) {  /* Re-enabled */
        /* Buffer status event (bit 3) - from trace line 3925-3970 */
        uint8_t tmp900b;
        
        /* Read 0x9301 first */
        (void)XDATA_REG8(0x9301);
        
        /* Read/write E716 */
        tmp = XDATA_REG8(0xE716);
        XDATA_REG8(0xE716) = tmp;
        
        /* Read 92C2 */
        (void)XDATA_REG8(0x92C2);
        
        /* Read/write 92C8 twice */
        tmp = XDATA_REG8(0x92C8);
        XDATA_REG8(0x92C8) = tmp;
        tmp = XDATA_REG8(0x92C8);
        XDATA_REG8(0x92C8) = tmp;
        
        /* USB MSC config sequence from trace */
        tmp900b = XDATA_REG8(0x900B);
        XDATA_REG8(0x900B) = tmp900b | 0x02;  /* Set bit 1 */
        tmp900b = XDATA_REG8(0x900B);
        XDATA_REG8(0x900B) = tmp900b | 0x04;  /* Set bit 2 */
        tmp900b = XDATA_REG8(0x900B);
        XDATA_REG8(0x900B) = tmp900b | 0x01;  /* Set bit 0 */
        tmp900b = XDATA_REG8(0x900B);
        XDATA_REG8(0x900B) = tmp900b & ~0x02;  /* Clear bit 1 */
        tmp900b = XDATA_REG8(0x900B);
        XDATA_REG8(0x900B) = tmp900b & ~0x04;  /* Clear bit 2 */
        tmp900b = XDATA_REG8(0x900B);
        XDATA_REG8(0x900B) = tmp900b & ~0x01;  /* Clear bit 0 */
        
        /* Write 901A and 9301 to clear */
        XDATA_REG8(0x901A) = 0x0D;
        XDATA_REG8(0x9301) = 0x40;
    }
    
    if (periph_status & USB_STATUS_LINK) {
        /* Link status change (0x10) - handle like trace */
        uint8_t buf9300;
        
        /* Read/write 92C8 like in trace */
        tmp = XDATA_REG8(0x92C8);
        XDATA_REG8(0x92C8) = tmp;
        
        /* Check 0x9302 and 0x9300 - if bit 1 set, clear it */
        (void)XDATA_REG8(0x9302);
        (void)XDATA_REG8(0x9302);
        (void)XDATA_REG8(0x9302);
        buf9300 = XDATA_REG8(0x9300);
        if (buf9300 & 0x02) {
            XDATA_REG8(0x9300) = 0x08;
        }
        
        /* Clear link status (read-modify-write from trace) */
        tmp = XDATA_REG8(0xE716);
        XDATA_REG8(0xE716) = tmp;
    }
    
    /* Clear interrupt flags */
    (void)REG_INT_SYSTEM;
    (void)REG_INT_USB_STATUS;
}

void timer0_isr(void) __interrupt(1) { }

/* INT1 ISR - System/Power interrupt */
void int1_isr(void) __interrupt(2) {
    uint8_t tmp;
    uart_puts("[I1:");
    
    /* Read system interrupt status */
    tmp = REG_INT_SYSTEM;  /* 0xC806 */
    uart_puthex(tmp);
    
    /* Clear DMA interrupt (from trace) */
    tmp = XDATA_REG8(0xCC91);
    XDATA_REG8(0xCC91) = tmp;
    
    /* Read various status registers */
    (void)XDATA_REG8(0xCC99);
    (void)XDATA_REG8(0xCCD9);
    (void)XDATA_REG8(0xCCF9);
    (void)XDATA_REG8(0xCC33);
    
    /* Read PCIe/NVMe status (4x per trace) */
    (void)XDATA_REG8(0xC80A);
    (void)XDATA_REG8(0xC80A);
    (void)XDATA_REG8(0xC80A);
    (void)XDATA_REG8(0xC80A);
    
    /* Handle power events (critical for USB to work!)
     * From trace: read 0x92E1, write it back; read 0x92C2, clear bit 6 */
    tmp = XDATA_REG8(0x92E1);
    if (tmp) {
        uart_putc('/');
        uart_puthex(tmp);
        XDATA_REG8(0x92E1) = tmp;  /* Clear/ack power event */
        
        /* Clear pending power status bits */
        tmp = XDATA_REG8(0x92C2);
        XDATA_REG8(0x92C2) = tmp & 0x3F;  /* Clear bits 6-7 */
    }
    
    /* Final read to clear */
    (void)REG_INT_SYSTEM;
    uart_puts("]\n");
}

void timer1_isr(void) __interrupt(3) { }
void serial_isr(void) __interrupt(4) { }
void timer2_isr(void) __interrupt(5) { }

void main(void)
{
    IE = 0;
    REG_UART_LCR &= 0xF7;
    uart_puts("\n[BOOT]\n");

    /* === Init sequence from trace/enumerate_min === */
    XDATA_REG8(0xCC32) = 0x01;
    XDATA_REG8(0xCC30) = 0x01;
    XDATA_REG8(0xE710) = 0x04;
    XDATA_REG8(0xCC33) = 0x04;
    XDATA_REG8(0xCC3B) = 0x0C;
    XDATA_REG8(0xE717) = 0x01;
    XDATA_REG8(0xCC3E) = 0x00;
    XDATA_REG8(0xCC3B) = 0x0C;
    XDATA_REG8(0xCC3B) = 0x0C;
    XDATA_REG8(0xE716) = 0x03;
    XDATA_REG8(0xCC3E) = 0x00;
    XDATA_REG8(0xCC39) = 0x06;
    XDATA_REG8(0xCC3A) = 0x14;
    XDATA_REG8(0xCC38) = 0x44;
    XDATA_REG8(0xCC37) = 0x2C;
    XDATA_REG8(0xE780) = 0x00;
    XDATA_REG8(0xE716) = 0x00;
    XDATA_REG8(0xE716) = 0x03;
    XDATA_REG8(0xCC11) = 0x04;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x12;
    XDATA_REG8(0xCC12) = 0x00;
    XDATA_REG8(0xCC13) = 0xC8;
    XDATA_REG8(0xCC11) = 0x01;
    XDATA_REG8(0xCC11) = 0x04;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC37) = 0x28;
    XDATA_REG8(0xCC11) = 0x04;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x12;
    XDATA_REG8(0xCC12) = 0x00;
    XDATA_REG8(0xCC13) = 0x14;
    XDATA_REG8(0xCC11) = 0x01;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC11) = 0x04;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x13;
    XDATA_REG8(0xCC12) = 0x00;
    XDATA_REG8(0xCC13) = 0x0A;
    XDATA_REG8(0xCC11) = 0x01;
    XDATA_REG8(0xCC11) = 0x04;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xE7E3) = 0x00;
    XDATA_REG8(0xE764) = 0x14;
    XDATA_REG8(0xE764) = 0x14;
    XDATA_REG8(0xE764) = 0x14;
    XDATA_REG8(0xE764) = 0x14;
    XDATA_REG8(0xE76C) = 0x04;
    XDATA_REG8(0xE774) = 0x04;
    XDATA_REG8(0xE77C) = 0x04;
    XDATA_REG8(0xCC11) = 0x04;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x12;
    XDATA_REG8(0xCC12) = 0x00;
    XDATA_REG8(0xCC13) = 0xC7;
    XDATA_REG8(0xCC11) = 0x01;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC11) = 0x04;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x12;
    XDATA_REG8(0xCC12) = 0x00;
    XDATA_REG8(0xCC13) = 0xC7;
    XDATA_REG8(0xCC11) = 0x01;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC11) = 0x04;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x12;
    XDATA_REG8(0xCC12) = 0x00;
    XDATA_REG8(0xCC13) = 0xC7;
    XDATA_REG8(0xCC11) = 0x01;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC11) = 0x04;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x12;
    XDATA_REG8(0xCC12) = 0x00;
    XDATA_REG8(0xCC13) = 0xC7;
    XDATA_REG8(0xCC11) = 0x01;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xC805) = 0x02;
    XDATA_REG8(0xC8A6) = 0x04;
    XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AE) = 0x00;
    XDATA_REG8(0xC8AF) = 0x00;
    XDATA_REG8(0xC8AA) = 0x06;
    XDATA_REG8(0xC8AC) = 0x04;
    XDATA_REG8(0xC8A1) = 0x00;
    XDATA_REG8(0xC8A2) = 0x00;
    XDATA_REG8(0xC8AB) = 0x00;
    XDATA_REG8(0xC8A3) = 0x00;
    XDATA_REG8(0xC8A4) = 0x00;
    XDATA_REG8(0xC8A9) = 0x01;
    XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AE) = 0x00;
    XDATA_REG8(0xC8AF) = 0x00;
    XDATA_REG8(0xC8AA) = 0x05;
    XDATA_REG8(0xC8AC) = 0x04;
    XDATA_REG8(0xC8A1) = 0x00;
    XDATA_REG8(0xC8A2) = 0x00;
    XDATA_REG8(0xC8AB) = 0x00;
    XDATA_REG8(0xC8A3) = 0x00;
    XDATA_REG8(0xC8A4) = 0x01;
    XDATA_REG8(0xC8A9) = 0x01;
    XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AD) = 0x01;
    XDATA_REG8(0xC8AE) = 0x00;
    XDATA_REG8(0xC8AF) = 0x00;
    XDATA_REG8(0xC8AA) = 0x01;
    XDATA_REG8(0xC8AC) = 0x04;
    XDATA_REG8(0xC8A3) = 0x00;
    XDATA_REG8(0xC8A4) = 0x01;
    XDATA_REG8(0xC8A9) = 0x01;
    XDATA_REG8(0xCC11) = 0x04;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x15;
    XDATA_REG8(0xCC12) = 0x00;
    XDATA_REG8(0xCC13) = 0x59;
    XDATA_REG8(0xCC11) = 0x01;
    /* Flash status polling x19 */
    { uint8_t i; for(i=0; i<19; i++) {
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AE) = 0x00; XDATA_REG8(0xC8AF) = 0x00;
        XDATA_REG8(0xC8AA) = 0x05; XDATA_REG8(0xC8AC) = 0x04;
        XDATA_REG8(0xC8A1) = 0x00; XDATA_REG8(0xC8A2) = 0x00; XDATA_REG8(0xC8AB) = 0x00;
        XDATA_REG8(0xC8A3) = 0x00; XDATA_REG8(0xC8A4) = 0x01; XDATA_REG8(0xC8A9) = 0x01;
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
    }}
    XDATA_REG8(0xCC35) = 0x00;
    XDATA_REG8(0xC801) = 0x10;
    XDATA_REG8(0xC800) = 0x04;
    XDATA_REG8(0xC800) = 0x05;
    XDATA_REG8(0xCC3B) = 0x0D;
    XDATA_REG8(0xCC3B) = 0x0F;
    XDATA_REG8(0x92C6) = 0x05;
    XDATA_REG8(0x92C7) = 0x00;
    XDATA_REG8(0x9201) = 0x0E;
    XDATA_REG8(0x9201) = 0x0C;
    XDATA_REG8(0x92C1) = 0x82;
    XDATA_REG8(0x920C) = 0x61;
    XDATA_REG8(0x920C) = 0x60;
    XDATA_REG8(0x92C0) = 0x87;
    XDATA_REG8(0x92C1) = 0x83;
    XDATA_REG8(0x92C5) = 0x2F;
    XDATA_REG8(0x9241) = 0x10;
    XDATA_REG8(0x9241) = 0xD0;
    XDATA_REG8(0xE741) = 0x5B;
    XDATA_REG8(0xE741) = 0x6B;
    XDATA_REG8(0xE742) = 0x1F;
    XDATA_REG8(0xE741) = 0xAB;
    XDATA_REG8(0xE742) = 0x17;
    XDATA_REG8(0xCC43) = 0x88;
    XDATA_REG8(0x9316) = 0x00;
    XDATA_REG8(0x9317) = 0x00;
    XDATA_REG8(0x931A) = 0x00;
    XDATA_REG8(0x931B) = 0x00;
    XDATA_REG8(0x9322) = 0x00;
    XDATA_REG8(0x9323) = 0x00;
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
    /* Flash reads bank0 x3 */
    { uint8_t i; for(i=0; i<3; i++) {
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AE) = 0x00; XDATA_REG8(0xC8AF) = 0x00;
        XDATA_REG8(0xC8AA) = 0x03; XDATA_REG8(0xC8AC) = 0x07;
        XDATA_REG8(0xC8A1) = 0x00; XDATA_REG8(0xC8A2) = 0x00; XDATA_REG8(0xC8AB) = 0x00;
        XDATA_REG8(0xC8A3) = 0x00; XDATA_REG8(0xC8A4) = 0x80; XDATA_REG8(0xC8A9) = 0x01;
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
    }}
    /* Flash reads bank2 x3 */
    { uint8_t i; for(i=0; i<3; i++) {
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AE) = 0x00; XDATA_REG8(0xC8AF) = 0x00;
        XDATA_REG8(0xC8AA) = 0x03; XDATA_REG8(0xC8AC) = 0x07;
        XDATA_REG8(0xC8A1) = 0x00; XDATA_REG8(0xC8A2) = 0x00; XDATA_REG8(0xC8AB) = 0x02;
        XDATA_REG8(0xC8A3) = 0x00; XDATA_REG8(0xC8A4) = 0x80; XDATA_REG8(0xC8A9) = 0x01;
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
    }}
    XDATA_REG8(0xCC35) = 0x00;
    XDATA_REG8(0x905F) = 0x44;
    /* Flash reads bank0 addr 0x80 x3 */
    { uint8_t i; for(i=0; i<3; i++) {
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AE) = 0x00; XDATA_REG8(0xC8AF) = 0x00;
        XDATA_REG8(0xC8AA) = 0x03; XDATA_REG8(0xC8AC) = 0x07;
        XDATA_REG8(0xC8A1) = 0x80; XDATA_REG8(0xC8A2) = 0x00; XDATA_REG8(0xC8AB) = 0x00;
        XDATA_REG8(0xC8A3) = 0x00; XDATA_REG8(0xC8A4) = 0x80; XDATA_REG8(0xC8A9) = 0x01;
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
    }}
    /* Flash reads bank2 addr 0x80 x3 */
    { uint8_t i; for(i=0; i<3; i++) {
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AE) = 0x00; XDATA_REG8(0xC8AF) = 0x00;
        XDATA_REG8(0xC8AA) = 0x03; XDATA_REG8(0xC8AC) = 0x07;
        XDATA_REG8(0xC8A1) = 0x80; XDATA_REG8(0xC8A2) = 0x00; XDATA_REG8(0xC8AB) = 0x02;
        XDATA_REG8(0xC8A3) = 0x00; XDATA_REG8(0xC8A4) = 0x80; XDATA_REG8(0xC8A9) = 0x01;
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
        XDATA_REG8(0xC8AD) = 0x00; XDATA_REG8(0xC8AD) = 0x00;
    }}
    XDATA_REG8(0xCC2A) = 0x04;
    XDATA_REG8(0xCC2C) = 0xC7;
    XDATA_REG8(0xCC2D) = 0xC7;
    XDATA_REG8(0xC801) = 0x50;
    XDATA_REG8(0xCC32) = 0x00;
    XDATA_REG8(0xC807) = 0x04;
    XDATA_REG8(0x92C8) = 0x24;
    XDATA_REG8(0x92C8) = 0x24;
    XDATA_REG8(0xCC1D) = 0x04;
    XDATA_REG8(0xCC1D) = 0x02;
    XDATA_REG8(0xCC5D) = 0x04;
    XDATA_REG8(0xCC5D) = 0x02;
    XDATA_REG8(0xCC1C) = 0x16;
    XDATA_REG8(0xCC1E) = 0x00;
    XDATA_REG8(0xCC1F) = 0x8B;
    XDATA_REG8(0xCC5C) = 0x54;
    XDATA_REG8(0xCC5E) = 0x00;
    XDATA_REG8(0xCC5F) = 0xC7;
    XDATA_REG8(0xC8D8) = 0x00;
    XDATA_REG8(0xC8D8) = 0x00;
    XDATA_REG8(0xC8D8) = 0x00;
    XDATA_REG8(0xC8D7) = 0x00;
    XDATA_REG8(0xC8D6) = 0x00;
    XDATA_REG8(0xC8D6) = 0x00;
    XDATA_REG8(0xC8D6) = 0x00;
    XDATA_REG8(0xC8D5) = 0x00;
    XDATA_REG8(0xC8D8) = 0x02;
    XDATA_REG8(0xC8B7) = 0x00;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x94;
    XDATA_REG8(0xC8B2) = 0xA0;
    XDATA_REG8(0xC8B3) = 0x00;
    XDATA_REG8(0xC8B4) = 0x0F;
    XDATA_REG8(0xC8B5) = 0xFF;
    XDATA_REG8(0xC8B8) = 0x01;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8D8) = 0x02;
    XDATA_REG8(0xC8B7) = 0x00;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x94;
    XDATA_REG8(0xC8B2) = 0xB0;
    XDATA_REG8(0xC8B3) = 0x00;
    XDATA_REG8(0xC8B4) = 0x01;
    XDATA_REG8(0xC8B5) = 0xFF;
    XDATA_REG8(0xC8B8) = 0x01;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8D8) = 0x00;
    XDATA_REG8(0xC8B7) = 0x00;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x94;
    XDATA_REG8(0xC8B2) = 0xA0;
    XDATA_REG8(0xC8B3) = 0x00;
    XDATA_REG8(0xC8B4) = 0x0F;
    XDATA_REG8(0xC8B5) = 0xFF;
    XDATA_REG8(0xC8B8) = 0x01;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8D8) = 0x00;
    XDATA_REG8(0xC8B7) = 0x00;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x94;
    XDATA_REG8(0xC8B2) = 0xB0;
    XDATA_REG8(0xC8B3) = 0x00;
    XDATA_REG8(0xC8B4) = 0x01;
    XDATA_REG8(0xC8B5) = 0xFF;
    XDATA_REG8(0xC8B8) = 0x01;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8D6) = 0x02;
    XDATA_REG8(0xC8B7) = 0x00;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x94;
    XDATA_REG8(0xC8B2) = 0xB8;
    XDATA_REG8(0xC8B3) = 0x00;
    XDATA_REG8(0xC8B4) = 0x03;
    XDATA_REG8(0xC8B5) = 0xFF;
    XDATA_REG8(0xC8B8) = 0x01;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8D6) = 0x02;
    XDATA_REG8(0xC8B7) = 0x00;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x94;
    XDATA_REG8(0xC8B2) = 0xBC;
    XDATA_REG8(0xC8B3) = 0x00;
    XDATA_REG8(0xC8B4) = 0x00;
    XDATA_REG8(0xC8B5) = 0x7F;
    XDATA_REG8(0xC8B8) = 0x01;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8D6) = 0x00;
    XDATA_REG8(0xC8B7) = 0x00;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x94;
    XDATA_REG8(0xC8B2) = 0xB8;
    XDATA_REG8(0xC8B3) = 0x00;
    XDATA_REG8(0xC8B4) = 0x03;
    XDATA_REG8(0xC8B5) = 0xFF;
    XDATA_REG8(0xC8B8) = 0x01;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8D6) = 0x00;
    XDATA_REG8(0xC8B7) = 0x00;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0xC8B6) = 0x94;
    XDATA_REG8(0xC8B2) = 0xBC;
    XDATA_REG8(0xC8B3) = 0x00;
    XDATA_REG8(0xC8B4) = 0x00;
    XDATA_REG8(0xC8B5) = 0x7F;
    XDATA_REG8(0xC8B8) = 0x01;
    XDATA_REG8(0xC8B6) = 0x14;
    XDATA_REG8(0x900B) = 0x07;
    XDATA_REG8(0x900B) = 0x07;
    XDATA_REG8(0x900B) = 0x07;
    XDATA_REG8(0x900B) = 0x05;
    XDATA_REG8(0x900B) = 0x01;
    XDATA_REG8(0x900B) = 0x00;
    XDATA_REG8(0x901A) = 0x0D;
    XDATA_REG8(0x92C0) = 0x87;
    XDATA_REG8(0x91D1) = 0x0F;
    XDATA_REG8(0x9300) = 0x0C;
    XDATA_REG8(0x9301) = 0xC0;
    XDATA_REG8(0x9302) = 0xBF;
    XDATA_REG8(0x9091) = 0x1F;
    XDATA_REG8(0x9093) = 0x0F;
    XDATA_REG8(0x91C1) = 0xF0;
    XDATA_REG8(0x9303) = 0x33;
    XDATA_REG8(0x9304) = 0x3F;
    XDATA_REG8(0x9305) = 0x40;
    XDATA_REG8(0x9002) = 0xE0;
    XDATA_REG8(0x9005) = 0xF0;
    XDATA_REG8(0x90E2) = 0x01;
    XDATA_REG8(0x905E) = 0x00;
    XDATA_REG8(0x9096) = 0xFF;
    XDATA_REG8(0x9097) = 0xFF;
    XDATA_REG8(0x9098) = 0xFF;
    XDATA_REG8(0x9099) = 0xFF;
    XDATA_REG8(0x909A) = 0xFF;
    XDATA_REG8(0x909B) = 0xFF;
    XDATA_REG8(0x909C) = 0xFF;
    XDATA_REG8(0x909D) = 0xFF;
    XDATA_REG8(0x909E) = 0x03;
    XDATA_REG8(0x9011) = 0xFF;
    XDATA_REG8(0x9012) = 0xFF;
    XDATA_REG8(0x9013) = 0xFF;
    XDATA_REG8(0x9014) = 0xFF;
    XDATA_REG8(0x9015) = 0xFF;
    XDATA_REG8(0x9016) = 0xFF;
    XDATA_REG8(0x9017) = 0xFF;
    XDATA_REG8(0x9018) = 0x03;
    XDATA_REG8(0x9010) = 0xFE;
    XDATA_REG8(0x91C3) = 0x00;
    XDATA_REG8(0x91C0) = 0x13;
    XDATA_REG8(0x91C0) = 0x12;
    XDATA_REG8(0xCC11) = 0x04;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x14;
    XDATA_REG8(0xCC12) = 0x01;
    XDATA_REG8(0xCC13) = 0x8F;
    XDATA_REG8(0xCC11) = 0x01;
    XDATA_REG8(0xCC11) = 0x04;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC11) = 0x04;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xCC10) = 0x10;
    XDATA_REG8(0xCC12) = 0x00;
    XDATA_REG8(0xCC13) = 0x09;
    XDATA_REG8(0xCC11) = 0x01;
    XDATA_REG8(0xCC11) = 0x02;
    XDATA_REG8(0xC807) = 0x04;
    XDATA_REG8(0xC807) = 0x84;
    XDATA_REG8(0xE7FC) = 0xFF;
    XDATA_REG8(0xCCD9) = 0x04;
    XDATA_REG8(0xCCD9) = 0x02;
    XDATA_REG8(0xCCD8) = 0x00;
    XDATA_REG8(0xC801) = 0x50;
    XDATA_REG8(0xCCD8) = 0x04;
    XDATA_REG8(0xCCDA) = 0x00;
    XDATA_REG8(0xCCDB) = 0xC8;
    XDATA_REG8(0xC809) = 0x08;
    XDATA_REG8(0xC809) = 0x0A;
    XDATA_REG8(0xC809) = 0x0A;
    XDATA_REG8(0xCCF8) = 0x40;
    XDATA_REG8(0xCCF9) = 0x04;
    XDATA_REG8(0xCCF9) = 0x02;
    XDATA_REG8(0xCC88) = 0x10;
    XDATA_REG8(0xCC8A) = 0x00;
    XDATA_REG8(0xCC8B) = 0x0A;
    XDATA_REG8(0xCC89) = 0x01;
    XDATA_REG8(0xCC89) = 0x02;
    XDATA_REG8(0xCC88) = 0x10;
    XDATA_REG8(0xCC8A) = 0x00;
    XDATA_REG8(0xCC8B) = 0x3C;
    XDATA_REG8(0xCC89) = 0x01;
    XDATA_REG8(0xCC89) = 0x02;
    XDATA_REG8(0xC809) = 0x2A;
    XDATA_REG8(0xC801) = 0x50;
    XDATA_REG8(0xCC80) = 0x00;
    XDATA_REG8(0xCC80) = 0x03;
    XDATA_REG8(0xCC99) = 0x04;
    XDATA_REG8(0xCC99) = 0x02;
    XDATA_REG8(0xC801) = 0x50;
    XDATA_REG8(0xCC98) = 0x00;
    XDATA_REG8(0xCC98) = 0x04;
    XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AE) = 0x00;
    XDATA_REG8(0xC8AF) = 0x00;
    XDATA_REG8(0xC8AA) = 0x03;
    XDATA_REG8(0xC8AC) = 0x07;
    XDATA_REG8(0xC8A1) = 0x00;
    XDATA_REG8(0xC8A2) = 0x80;
    XDATA_REG8(0xC8AB) = 0x01;
    XDATA_REG8(0xC8A3) = 0x00;
    XDATA_REG8(0xC8A4) = 0x04;
    XDATA_REG8(0xC8A9) = 0x01;
    XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xC8AD) = 0x00;
    XDATA_REG8(0xCC82) = 0x18;
    XDATA_REG8(0xCC83) = 0x9C;
    XDATA_REG8(0xCC91) = 0x04;
    XDATA_REG8(0xCC91) = 0x02;
    XDATA_REG8(0xC801) = 0x50;
    XDATA_REG8(0xCC90) = 0x00;
    XDATA_REG8(0xCC90) = 0x05;
    XDATA_REG8(0xCC92) = 0x00;
    XDATA_REG8(0xCC93) = 0xC8;
    XDATA_REG8(0xCC91) = 0x01;

    uart_puts("[GO]\n");
    
    /* Set TCON for LEVEL-triggered interrupts
     * The USB interrupt line stays asserted while there are pending events,
     * so we need level-triggered mode to keep servicing them */
    TCON = 0;  /* Level triggered for both INT0 and INT1 */
    
    IE = IE_EA | IE_EX0 | IE_EX1 | IE_ET0;

    /* Main loop with debug output */
    {
        uint16_t cnt = 0;
        uint8_t last_9000 = 0, last_9101 = 0;
        while (1) {
            uint8_t cur_9000, cur_9101;
            
            XDATA_REG8(0xCC2A) = 0x0C;
            (void)XDATA_REG8(0x92F7);
            cur_9000 = XDATA_REG8(0x9000);
            cur_9101 = XDATA_REG8(0x9101);
            
            /* Print when USB status changes */
            if (cur_9000 != last_9000 || cur_9101 != last_9101) {
                uart_puts("U:");
                uart_puthex(cur_9000);
                uart_putc('/');
                uart_puthex(cur_9101);
                uart_putc('\n');
                last_9000 = cur_9000;
                last_9101 = cur_9101;
            }
            
            /* Print status every ~65536 loops */
            if (++cnt == 0) {
                uart_puts("S:");
                uart_puthex(XDATA_REG8(0xC800));  /* Main int status */
                uart_putc('/');
                uart_puthex(XDATA_REG8(0xC802));  /* USB int status */
                uart_putc('/');
                uart_puthex(XDATA_REG8(0x92F7));  /* Power status */
                uart_putc('/');
                uart_puthex(XDATA_REG8(0x92C2));  /* Power status 2 */
                uart_putc('/');
                uart_puthex(cur_9101);            /* USB periph 0x9101 */
                uart_putc('\n');
            }
        }
    }
}
