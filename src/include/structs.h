#ifndef __STRUCTS_H__
#define __STRUCTS_H__

#include "types.h"

/*
 * ASM2464PD Firmware - Memory-mapped Structure Definitions
 *
 * These structures represent different views of shared memory regions.
 * The same physical memory can be interpreted differently depending on context.
 */

//=============================================================================
// USB Endpoint Buffer at 0xD800
// This memory region is used for USB packet data. Different packet types
// use different interpretations of the same memory.
//=============================================================================

/* USB Mass Storage Command Status Wrapper (CSW) - 13 bytes
 * Used when sending CSW response packets to the host.
 * See USB Mass Storage Class Bulk-Only Transport spec.
 * Note: Individual bytes instead of arrays to avoid SDCC optimizer issues.
 */
struct usb_csw {
    uint8_t sig0;           /* 0xD800: Signature byte 0 'U' */
    uint8_t sig1;           /* 0xD801: Signature byte 1 'S' */
    uint8_t sig2;           /* 0xD802: Signature byte 2 'B' */
    uint8_t sig3;           /* 0xD803: Signature byte 3 'S' */
    uint8_t tag0;           /* 0xD804: Tag byte 0 (LSB) */
    uint8_t tag1;           /* 0xD805: Tag byte 1 */
    uint8_t tag2;           /* 0xD806: Tag byte 2 */
    uint8_t tag3;           /* 0xD807: Tag byte 3 (MSB) */
    uint8_t residue0;       /* 0xD808: Data residue byte 0 (LSB) */
    uint8_t residue1;       /* 0xD809: Data residue byte 1 */
    uint8_t residue2;       /* 0xD80A: Data residue byte 2 */
    uint8_t residue3;       /* 0xD80B: Data residue byte 3 (MSB) */
    uint8_t status;         /* 0xD80C: Status (0=pass, 1=fail, 2=phase error) */
};

/* USB Buffer Control view - 13 bytes
 * Used for non-CSW packet transfers and buffer management.
 */
struct usb_buf_ctrl {
    uint8_t ctrl;           /* 0xD800: Buffer control/mode */
    uint8_t select;         /* 0xD801: Buffer select */
    uint8_t data;           /* 0xD802: Buffer data/pointer */
    uint8_t ptr_low;        /* 0xD803: Pointer low */
    uint8_t ptr_high;       /* 0xD804: Pointer high */
    uint8_t length_low;     /* 0xD805: Length low */
    uint8_t status;         /* 0xD806: Status */
    uint8_t length_high;    /* 0xD807: Length high */
    uint8_t ctrl_global;    /* 0xD808: Control global */
    uint8_t threshold_high; /* 0xD809: Threshold high */
    uint8_t threshold_low;  /* 0xD80A: Threshold low */
    uint8_t flow_ctrl;      /* 0xD80B: Flow control */
    uint8_t xfer_start;     /* 0xD80C: Transfer start */
};

/* Pointers to the USB endpoint buffer structures */
#define USB_CSW         ((volatile __xdata struct usb_csw *)0xD800)
#define USB_BUF_CTRL    ((volatile __xdata struct usb_buf_ctrl *)0xD800)

/* CSW signature bytes */
#define USB_CSW_SIGNATURE_0     0x55  /* 'U' */
#define USB_CSW_SIGNATURE_1     0x53  /* 'S' */
#define USB_CSW_SIGNATURE_2     0x42  /* 'B' */
#define USB_CSW_SIGNATURE_3     0x53  /* 'S' */

/* CSW status values */
#define USB_CSW_STATUS_PASS         0x00
#define USB_CSW_STATUS_FAIL         0x01
#define USB_CSW_STATUS_PHASE_ERROR  0x02

/* CSW length */
#define USB_CSW_LENGTH  13

#endif /* __STRUCTS_H__ */
