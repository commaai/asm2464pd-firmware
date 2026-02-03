/*
 * USB Enumeration Firmware for ASM2464PD
 * 
 * Goal: Enumerate as ADD1:0001 "TinyBox"
 */

#include "types.h"
#include "registers.h"

__sfr __at(0xA8) IE;
__sfr __at(0x88) TCON;

/* USB Registers */
/* Setup packet is at 0x9104-0x910B according to original firmware disassembly */
#define USB_EP0_SETUP_0   XDATA8(0x9104)
#define USB_EP0_SETUP_1   XDATA8(0x9105)
#define USB_EP0_SETUP_2   XDATA8(0x9106)
#define USB_EP0_SETUP_3   XDATA8(0x9107)
#define USB_EP0_SETUP_4   XDATA8(0x9108)
#define USB_EP0_SETUP_5   XDATA8(0x9109)
#define USB_EP0_SETUP_6   XDATA8(0x910A)
#define USB_EP0_SETUP_7   XDATA8(0x910B)

#define USB_EP0_TX_LEN    XDATA8(0x9300)
#define USB_EP0_CTRL      XDATA8(0x9301)
#define USB_STATUS_9101   XDATA8(0x9101)
#define USB_STATUS_9091   XDATA8(0x9091)
#define USB_INT_C802      XDATA8(0xC802)

/* EP0 output buffer 
 * Original firmware at 0xb3ed writes descriptor data to 0x9E00.
 * This is an MMIO buffer that the USB hardware reads from.
 * The DMA config at 0x9310/0x9311 is for a different purpose. */
#define EP0_BUF_BASE      0x9E00

/* USB Request Types */
#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09

/* Descriptor Types */
#define USB_DESC_DEVICE           0x01
#define USB_DESC_CONFIGURATION    0x02
#define USB_DESC_STRING           0x03
#define USB_DESC_INTERFACE        0x04
#define USB_DESC_ENDPOINT         0x05
#define USB_DESC_BOS              0x0F

void uart_putc(uint8_t ch) {
    REG_UART_THR = ch;
}

void uart_puts(__code const char *str) {
    while (*str) uart_putc(*str++);
}

void uart_puthex(uint8_t val) {
    static __code const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[val >> 4]);
    uart_putc(hex[val & 0x0F]);
}

/* USB Device Descriptor - 18 bytes */
__code uint8_t usb_device_desc[] = {
    0x12,       /* bLength */
    0x01,       /* bDescriptorType = Device */
    0x00, 0x02, /* bcdUSB = 2.00 (for now, USB 2.0) */
    0x00,       /* bDeviceClass = 0 (defined by interface) */
    0x00,       /* bDeviceSubClass */
    0x00,       /* bDeviceProtocol */
    0x40,       /* bMaxPacketSize0 = 64 */
    0xD1, 0xAD, /* idVendor = 0xADD1 (little endian) */
    0x01, 0x00, /* idProduct = 0x0001 */
    0x00, 0x01, /* bcdDevice = 1.00 */
    0x01,       /* iManufacturer = string 1 */
    0x02,       /* iProduct = string 2 */
    0x03,       /* iSerialNumber = string 3 */
    0x01        /* bNumConfigurations = 1 */
};

/* Configuration Descriptor - total 32 bytes */
__code uint8_t usb_config_desc[] = {
    /* Configuration Descriptor */
    0x09,       /* bLength */
    0x02,       /* bDescriptorType = Configuration */
    0x20, 0x00, /* wTotalLength = 32 */
    0x01,       /* bNumInterfaces = 1 */
    0x01,       /* bConfigurationValue = 1 */
    0x00,       /* iConfiguration = 0 */
    0x80,       /* bmAttributes = Bus Powered */
    0xFA,       /* bMaxPower = 500mA */
    
    /* Interface Descriptor */
    0x09,       /* bLength */
    0x04,       /* bDescriptorType = Interface */
    0x00,       /* bInterfaceNumber = 0 */
    0x00,       /* bAlternateSetting = 0 */
    0x02,       /* bNumEndpoints = 2 */
    0x08,       /* bInterfaceClass = Mass Storage */
    0x06,       /* bInterfaceSubClass = SCSI */
    0x50,       /* bInterfaceProtocol = Bulk-Only */
    0x00,       /* iInterface = 0 */
    
    /* Endpoint 1 IN (Bulk) */
    0x07,       /* bLength */
    0x05,       /* bDescriptorType = Endpoint */
    0x81,       /* bEndpointAddress = 1 IN */
    0x02,       /* bmAttributes = Bulk */
    0x00, 0x02, /* wMaxPacketSize = 512 */
    0x00,       /* bInterval = 0 */
    
    /* Endpoint 2 OUT (Bulk) */
    0x07,       /* bLength */
    0x05,       /* bDescriptorType = Endpoint */
    0x02,       /* bEndpointAddress = 2 OUT */
    0x02,       /* bmAttributes = Bulk */
    0x00, 0x02, /* wMaxPacketSize = 512 */
    0x00        /* bInterval = 0 */
};

/* String Descriptor 0 - Language IDs */
__code uint8_t usb_string0[] = {
    0x04,       /* bLength */
    0x03,       /* bDescriptorType = String */
    0x09, 0x04  /* wLANGID = 0x0409 (English US) */
};

/* String Descriptor 1 - Manufacturer */
__code uint8_t usb_string1[] = {
    0x10,       /* bLength = 16 */
    0x03,       /* bDescriptorType = String */
    'T', 0, 'i', 0, 'n', 0, 'y', 0, 'B', 0, 'o', 0, 'x', 0
};

/* String Descriptor 2 - Product */
__code uint8_t usb_string2[] = {
    0x16,       /* bLength = 22 */
    0x03,       /* bDescriptorType = String */
    'T', 0, 'i', 0, 'n', 0, 'y', 0, 'E', 0, 'n', 0, 'c', 0, 'l', 0, 'o', 0, 's', 0
};

/* String Descriptor 3 - Serial */
__code uint8_t usb_string3[] = {
    0x1A,       /* bLength = 26 */
    0x03,       /* bDescriptorType = String */
    '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '0', 0, '1', 0
};

static uint8_t usb_address = 0;
static uint8_t usb_configured = 0;

void copy_to_ep0(__code uint8_t *src, uint8_t len) {
    uint8_t i;
    /* Try writing to BOTH possible buffer locations */
    /* 0x9E00 is MMIO USB buffer */
    /* 0x0160 is RAM buffer used by DMA */
    for (i = 0; i < len; i++) {
        XDATA8(0x9E00 + i) = src[i];  /* MMIO buffer */
        XDATA8(0x0160 + i) = src[i];  /* RAM buffer */
    }
    /* Debug: print first few bytes and verify readback */
    uart_putc('[');
    for (i = 0; i < 4 && i < len; i++) {
        uart_puthex(XDATA8(0x9E00 + i));
    }
    uart_putc('/');
    /* Also show 0x0160 buffer */
    for (i = 0; i < 4 && i < len; i++) {
        uart_puthex(XDATA8(0x0160 + i));
    }
    uart_putc(']');
}

void usb_send_ep0(uint8_t len) {
    /*
     * USB EP0 IN transmission - EXACT sequence from 0xa5a6
     * 
     * This is the complete original firmware preparation for EP0 IN.
     */
    uint8_t tmp;
    uint16_t timeout;
    
    /* From 0xa5a6-0xa5af */
    XDATA8(0x07E1) = 0x00;     /* State = 0 */
    XDATA8(0x07E9) = 0x01;     /* Flag = 1 */
    
    /* From 0xa5b0-0xa5b6: Clear bit 1 in 0x9002 */
    tmp = XDATA8(0x9002);
    XDATA8(0x9002) = tmp & 0xFD;
    
    /* From 0xa5b7-0xa5cc: State-dependent section
     * Since we set 0x07E1=0 (state), we take the state==0 branch
     * which does 0x92C4 &= 0xFE and 0xCC17 = 0x04, 0x02 */
    tmp = XDATA8(0x92C4);
    XDATA8(0x92C4) = tmp & 0xFE;
    XDATA8(0xCC17) = 0x04;
    XDATA8(0xCC17) = 0x02;
    
    /* From 0xa5cd-0xa5e1 */
    XDATA8(0x07EB) = 0x00;
    
    /* 0x9220 bit 2 check - clear if set */
    tmp = XDATA8(0x9220);
    if (tmp & 0x04) {
        XDATA8(0x9220) = tmp & 0xFB;
    }
    
    XDATA8(0x0AD6) = 0x00;
    XDATA8(0x9091) = 0x01;     /* Arm for interrupt */
    
    /* Now set length and trigger (from 0xa581 and elsewhere) */
    XDATA8(0x9003) = 0x00;
    XDATA8(0x9004) = len;
    
    /* From 0xa513: trigger setup */
    tmp = XDATA8(0x905F);
    XDATA8(0x905F) = tmp & 0xFE;
    tmp = XDATA8(0x905D);
    XDATA8(0x905D) = tmp & 0xFE;
    XDATA8(0x90E3) = 0x01;
    XDATA8(0x90A0) = 0x01;
    
    /* From 0xa57a: final trigger */
    XDATA8(0x9092) = 0x01;
    
    uart_putc('T');
    uart_puthex(len);
    
    /* Wait for completion - check 0x9091 bits */
    for (timeout = 0; timeout < 10000; timeout++) {
        tmp = XDATA8(0x9091);
        if (tmp & 0x06) {  /* Check bits 1 or 2 */
            uart_putc('!');
            break;
        }
    }
}

void usb_send_zlp(void) {
    /* Send Zero Length Packet for status stage */
    usb_send_ep0(0);
    uart_putc('Z');
}

void usb_set_address(uint8_t addr) {
    /* Set USB device address in hardware after SET_ADDRESS status stage
     * 
     * From original firmware disassembly at 0x1cf9 (called from 0x0185):
     *   mov dptr, #0x9006
     *   movx a, @dptr
     *   anl a, #0x7f      ; mask to 7 bits (USB address is 7-bit)
     *   orl a, #0x80      ; set bit 7 (enable/valid bit)
     *   movx @dptr, a
     *
     * NOTE: Original firmware reads FIRST then modifies - the hardware may
     * auto-populate 0x9006 with the address from SET_ADDRESS packet.
     * Try both approaches.
     */
    uint8_t cur = XDATA8(0x9006);
    uart_putc('@');
    uart_puthex(cur);  /* Show what's already there */
    
    /* Write address with enable bit - try direct write */
    XDATA8(0x9006) = (addr & 0x7F) | 0x80;
    
    uart_putc('>');
    uart_puthex(XDATA8(0x9006));  /* Verify it was written */
}

void usb_stall_ep0(void) {
    USB_EP0_CTRL = 0x04;  /* STALL */
}

void usb_handle_setup(void) {
    uint8_t bmRequestType = USB_EP0_SETUP_0;
    uint8_t bRequest = USB_EP0_SETUP_1;
    uint16_t wValue = USB_EP0_SETUP_2 | (USB_EP0_SETUP_3 << 8);
    uint16_t wIndex = USB_EP0_SETUP_4 | (USB_EP0_SETUP_5 << 8);
    uint16_t wLength = USB_EP0_SETUP_6 | (USB_EP0_SETUP_7 << 8);
    uint8_t desc_type = wValue >> 8;
    uint8_t desc_index = wValue & 0xFF;
    uint8_t len;
    
    uart_putc('S');
    uart_puthex(bmRequestType);
    uart_puthex(bRequest);
    uart_putc(' ');
    
    /* Standard Device Requests (bmRequestType & 0x60 == 0x00) */
    if ((bmRequestType & 0x60) == 0x00) {
        switch (bRequest) {
        case USB_REQ_GET_DESCRIPTOR:
            uart_putc('D');
            uart_puthex(desc_type);
            switch (desc_type) {
            case USB_DESC_DEVICE:
                len = sizeof(usb_device_desc);
                if (wLength < len) len = wLength;
                copy_to_ep0(usb_device_desc, len);
                usb_send_ep0(len);
                uart_putc('d');
                return;
                
            case USB_DESC_CONFIGURATION:
                len = sizeof(usb_config_desc);
                if (wLength < len) len = wLength;
                copy_to_ep0(usb_config_desc, len);
                usb_send_ep0(len);
                uart_putc('c');
                return;
                
            case USB_DESC_STRING:
                uart_putc('s');
                uart_puthex(desc_index);
                switch (desc_index) {
                case 0:
                    len = sizeof(usb_string0);
                    if (wLength < len) len = wLength;
                    copy_to_ep0(usb_string0, len);
                    usb_send_ep0(len);
                    return;
                case 1:
                    len = sizeof(usb_string1);
                    if (wLength < len) len = wLength;
                    copy_to_ep0(usb_string1, len);
                    usb_send_ep0(len);
                    return;
                case 2:
                    len = sizeof(usb_string2);
                    if (wLength < len) len = wLength;
                    copy_to_ep0(usb_string2, len);
                    usb_send_ep0(len);
                    return;
                case 3:
                    len = sizeof(usb_string3);
                    if (wLength < len) len = wLength;
                    copy_to_ep0(usb_string3, len);
                    usb_send_ep0(len);
                    return;
                }
                break;
            }
            break;
            
        case USB_REQ_SET_ADDRESS:
            uart_putc('A');
            uart_puthex(wValue & 0x7F);
            usb_address = wValue & 0x7F;
            /* Per USB spec, address takes effect AFTER status stage ZLP is ACK'd.
             * 
             * From original firmware 0x0185 SET_ADDRESS handler:
             * 1. Send ZLP for status stage
             * 2. Set address register AFTER ZLP completes
             * 
             * The key might be that we need to set address BEFORE sending ZLP
             * so hardware uses new address for the ACK. Try that.
             */
            
            /* Set address first - hardware might apply it after status stage */
            usb_set_address(usb_address);
            
            /* Send ZLP for status stage */
            usb_send_zlp();
            uart_putc('!');
            
            /* Re-arm EP0 */
            XDATA8(0x9301) = 0xC0;
            return;
            
        case USB_REQ_SET_CONFIGURATION:
            uart_putc('C');
            uart_puthex(wValue & 0xFF);
            usb_configured = wValue & 0xFF;
            usb_send_zlp();
            return;
            
        case USB_REQ_GET_CONFIGURATION:
            XDATA8(EP0_BUF_BASE) = usb_configured;
            usb_send_ep0(1);
            return;
            
        case USB_REQ_GET_STATUS:
            uart_putc('?');
            XDATA8(EP0_BUF_BASE) = 0x00;
            XDATA8(EP0_BUF_BASE + 1) = 0x00;
            usb_send_ep0(2);
            return;
        }
    }
    
    /* Unknown request - STALL */
    uart_putc('!');
    usb_stall_ep0();
}

void usb_isr_handler(void) {
    uint8_t c802 = USB_INT_C802;
    uint8_t s9101 = USB_STATUS_9101;
    uint8_t s9091 = USB_STATUS_9091;
    
    uart_putc('U');
    uart_puthex(c802);
    uart_putc('/');
    uart_puthex(s9091);
    uart_putc(' ');
    
    /* Check for setup packet */
    if (s9091 & 0x01) {
        usb_handle_setup();
        /* Clear setup received and re-arm EP0 */
        USB_STATUS_9091 = s9091 & ~0x01;
    }
    
    /* Re-arm EP0 for next packet */
    USB_EP0_CTRL = 0xC0;
}

void timer0_isr_handler(void) {}
void int1_isr_handler(void) { uart_putc('I'); }

void main(void) {
    uint16_t loop = 0;
    uint8_t prev_9e00 = 0xFF;
    
    /* UART init */
    REG_UART_LCR &= 0x7F;
    REG_UART_LCR &= ~(1 << 3);
    REG_UART_FCR |= (1 << 1);
    
    uart_puts("\n=== USB ENUM v6 ===\n");
    
    /* This version was getting SET_ADDRESS/descriptor errors - device WAS visible */
    
    /* Configure interrupt controller first */
    XDATA8(0xC800) = 0x05;
    XDATA8(0xC801) = 0x10;
    XDATA8(0xC805) = 0x02;
    XDATA8(0xC807) = 0x04;
    XDATA8(0xC809) = 0x20;  /* Bit 5 from original firmware */
    
    /* C8Ax channel config */
    XDATA8(0xC8A1) = 0x80;
    XDATA8(0xC8A4) = 0x80;
    XDATA8(0xC8A6) = 0x04;
    XDATA8(0xC8AA) = 0x03;
    XDATA8(0xC8AC) = 0x07;
    XDATA8(0xC8A9) = 0x01;
    
    /* C8Bx config */
    XDATA8(0xC8B2) = 0xBC;
    XDATA8(0xC8B3) = 0x80;
    XDATA8(0xC8B4) = 0xFF;
    XDATA8(0xC8B5) = 0xFF;
    XDATA8(0xC8B6) = 0x14;
    
    /* USB endpoint config - COMPLETE sequence from original firmware */
    
    /* DMA buffer config - exact values from original firmware trace */
    XDATA8(0x9310) = 0x01;  /* DMA config */
    XDATA8(0x9311) = 0x60;
    XDATA8(0x9312) = 0x00;
    XDATA8(0x9313) = 0xE3;
    XDATA8(0x9314) = 0x01;
    XDATA8(0x9315) = 0x60;
    XDATA8(0x9318) = 0x01;
    XDATA8(0x9319) = 0x60;
    XDATA8(0x931C) = 0x00;
    XDATA8(0x931D) = 0x03;
    XDATA8(0x931E) = 0x00;
    XDATA8(0x931F) = 0xE0;
    XDATA8(0x9320) = 0x00;
    XDATA8(0x9321) = 0xE3;
    
    XDATA8(0x905F) = 0x00;
    XDATA8(0x92C8) = 0x00;
    
    /* 0x900B sequence */
    XDATA8(0x900B) = 0x02;
    XDATA8(0x900B) = 0x06;
    XDATA8(0x900B) = 0x07;
    XDATA8(0x900B) = 0x05;
    XDATA8(0x900B) = 0x01;
    XDATA8(0x900B) = 0x00;
    
    XDATA8(0x901A) = 0x0D;
    XDATA8(0x9010) = 0xFE;
    
    /* USB power */
    XDATA8(0x92C0) = 0x81;
    
    /* CRITICAL registers we were missing! */
    XDATA8(0x91D1) = 0x0F;
    XDATA8(0x9300) = 0x0C;
    XDATA8(0x9301) = 0xC0;  /* Arm EP0 */
    XDATA8(0x9302) = 0xBF;
    
    /* USB interrupt enables */
    XDATA8(0x9091) = 0x1F;
    XDATA8(0x9093) = 0x0F;
    
    /* USB PHY config */
    XDATA8(0x91C1) = 0xF0;
    
    /* More endpoint config */
    XDATA8(0x9303) = 0x33;
    XDATA8(0x9304) = 0x3F;
    XDATA8(0x9305) = 0x40;
    
    /* CRITICAL registers from original firmware! */
    XDATA8(0x9002) = 0xE0;
    XDATA8(0x9005) = 0xF0;
    XDATA8(0x90E2) = 0x01;
    XDATA8(0x905E) = 0x00;
    
    /* Interrupt enable masks */
    XDATA8(0x9096) = 0xFF;
    XDATA8(0x9097) = 0xFF;
    XDATA8(0x9098) = 0xFF;
    XDATA8(0x9099) = 0xFF;
    XDATA8(0x909A) = 0xFF;
    XDATA8(0x909B) = 0xFF;
    XDATA8(0x909C) = 0xFF;
    XDATA8(0x909D) = 0xFF;
    XDATA8(0x909E) = 0x03;
    
    /* From 0xa5a6 - USB subsystem control (when state=0) */
    {
        uint8_t tmp = XDATA8(0x92C4);
        XDATA8(0x92C4) = tmp & 0xFE;  /* Clear bit 0 */
    }
    XDATA8(0xCC17) = 0x04;
    XDATA8(0xCC17) = 0x02;
    
    XDATA8(0x9241) = 0xD0;  /* PHY config */
    
    /* USB PHY: 91C0=0x00 makes device visible on bus
     * Note: 91C0=0x02 gives 9100=0x03 but device is NOT visible */
    XDATA8(0x91C3) = 0x00;
    XDATA8(0x91C0) = 0x00;
    
    uart_puts("Init done, 9100=");
    uart_puthex(XDATA8(0x9100));
    uart_puts(" C802=");
    uart_puthex(XDATA8(0xC802));
    uart_putc('\n');
    
    /* Enable 8051 external interrupts */
    TCON = 0x05;
    IE = 0x85;
    
    uart_puts("Polling...\n");
    
    /* Clear any garbage in setup buffer */
    prev_9e00 = XDATA8(0x9E00);
    
    /* Main loop - poll for USB setup packets
     * 
     * Try multiple detection methods since hardware behavior is unclear:
     * 1. 0x909E bit 0 - setup packet received flag (from original FW at 0x0ED3)
     * 2. 0x9091 bit 0 - USB status flag (we see this change to 0x01)
     * 
     * IMPORTANT: DO NOT use packet content comparison for detection!
     */
    while (1) {
        static uint8_t prev_9091, prev_c802, prev_909e;
        uint8_t s9091, c802, s909e;
        uint8_t setup_pending = 0;
        
        /* Method 1: Check 0x909E bit 0 (original firmware method) */
        s909e = XDATA8(0x909E);
        if (s909e & 0x01) {
            setup_pending = 1;
            uart_putc('E');  /* 909E triggered */
        }
        
        /* Method 2: Check 0x9091 bit 0 (we see this flag) */
        s9091 = XDATA8(0x9091);
        if ((s9091 & 0x01) && !(prev_9091 & 0x01)) {
            setup_pending = 1;
            uart_putc('I');  /* 9091 triggered */
        }
        
        if (setup_pending) {
            uart_puts("\n[SETUP] ");
            uart_puthex(XDATA8(0x9104));
            uart_puthex(XDATA8(0x9105));
            uart_puthex(XDATA8(0x9106));
            uart_puthex(XDATA8(0x9107));
            uart_puthex(XDATA8(0x9108));
            uart_puthex(XDATA8(0x9109));
            uart_puthex(XDATA8(0x910A));
            uart_puthex(XDATA8(0x910B));
            uart_putc(' ');
            
            usb_handle_setup();
            
            /* Clear flags and re-arm EP0 */
            XDATA8(0x909E) = 0x01;  /* Clear setup received flag */
            XDATA8(0x90E3) = 0x02;  /* Trigger completion */
            XDATA8(0x9091) = s9091 & ~0x01;  /* Clear 9091 bit 0 */
            XDATA8(0x9301) = 0xC0;  /* Re-arm EP0 for next packet */
        }
        
        /* Monitor changes in status registers */
        c802 = XDATA8(0xC802);
        if (c802 != prev_c802) {
            prev_c802 = c802;
            uart_puts("C="); uart_puthex(c802); uart_putc(' ');
        }
        
        if (s9091 != prev_9091) {
            prev_9091 = s9091;
            uart_puts("S="); uart_puthex(s9091); uart_putc(' ');
        }
        
        if (s909e != prev_909e) {
            prev_909e = s909e;
            uart_puts("R="); uart_puthex(s909e); uart_putc(' ');
        }
        
        /* Handle INT0/INT1 flags manually */
        if (TCON & 0x02) { uart_putc('0'); TCON &= ~0x02; }
        if (TCON & 0x08) { uart_putc('1'); TCON &= ~0x08; }
        
        if (++loop == 0) uart_putc('.');
    }
}
