/*
 * USB Descriptors for ASM2464PD
 *
 * These descriptors are stored in code ROM and DMA'd to the USB buffer
 * when the host requests them via GET_DESCRIPTOR.
 *
 * Original firmware descriptor locations:
 *   Device descriptor: 0x0627 (18 bytes)
 *   Config descriptor: 0x0639
 *   String descriptors: 0x063B onwards
 */

#include "include/types.h"

/*
 * USB Device Descriptor (18 bytes)
 * Matches original ASM2464PD firmware
 */
__code const uint8_t usb_device_descriptor[18] = {
    0x12,        /* bLength */
    0x01,        /* bDescriptorType (Device) */
    0x10, 0x02,  /* bcdUSB (2.10) */
    0x00,        /* bDeviceClass (defined at interface level) */
    0x00,        /* bDeviceSubClass */
    0x00,        /* bDeviceProtocol */
    0x40,        /* bMaxPacketSize0 (64 bytes) */
    0x4C, 0x17,  /* idVendor (0x174C = ASMedia) */
    0x62, 0x24,  /* idProduct (0x2462) */
    0x01, 0x00,  /* bcdDevice (1.00) */
    0x02,        /* iManufacturer (string index 2) */
    0x03,        /* iProduct (string index 3) */
    0x01,        /* iSerialNumber (string index 1) */
    0x01         /* bNumConfigurations */
};

/*
 * USB Configuration Descriptor (32 bytes total)
 * Includes interface and endpoint descriptors
 */
__code const uint8_t usb_config_descriptor[32] = {
    /* Configuration Descriptor */
    0x09,        /* bLength */
    0x02,        /* bDescriptorType (Configuration) */
    0x20, 0x00,  /* wTotalLength (32 bytes) */
    0x01,        /* bNumInterfaces */
    0x01,        /* bConfigurationValue */
    0x00,        /* iConfiguration */
    0x80,        /* bmAttributes (bus powered) */
    0xFA,        /* bMaxPower (500mA) */

    /* Interface Descriptor */
    0x09,        /* bLength */
    0x04,        /* bDescriptorType (Interface) */
    0x00,        /* bInterfaceNumber */
    0x00,        /* bAlternateSetting */
    0x02,        /* bNumEndpoints */
    0x08,        /* bInterfaceClass (Mass Storage) */
    0x06,        /* bInterfaceSubClass (SCSI) */
    0x50,        /* bInterfaceProtocol (Bulk-Only) */
    0x00,        /* iInterface */

    /* Endpoint Descriptor (Bulk IN) */
    0x07,        /* bLength */
    0x05,        /* bDescriptorType (Endpoint) */
    0x81,        /* bEndpointAddress (EP1 IN) */
    0x02,        /* bmAttributes (Bulk) */
    0x00, 0x02,  /* wMaxPacketSize (512) */
    0x00,        /* bInterval */

    /* Endpoint Descriptor (Bulk OUT) */
    0x07,        /* bLength */
    0x05,        /* bDescriptorType (Endpoint) */
    0x02,        /* bEndpointAddress (EP2 OUT) */
    0x02,        /* bmAttributes (Bulk) */
    0x00, 0x02,  /* wMaxPacketSize (512) */
    0x00         /* bInterval */
};

/*
 * String Descriptor 0 - Language ID
 */
__code const uint8_t usb_string_descriptor_0[4] = {
    0x04,        /* bLength */
    0x03,        /* bDescriptorType (String) */
    0x09, 0x04   /* Language ID (0x0409 = US English) */
};

/*
 * String Descriptor 1 - Serial Number "v00000000000"
 */
__code const uint8_t usb_string_descriptor_1[26] = {
    0x1A,        /* bLength (26 bytes) */
    0x03,        /* bDescriptorType (String) */
    'v', 0x00,
    '0', 0x00,
    '0', 0x00,
    '0', 0x00,
    '0', 0x00,
    '0', 0x00,
    '0', 0x00,
    '0', 0x00,
    '0', 0x00,
    '0', 0x00,
    '0', 0x00,
    '0', 0x00
};

/*
 * String Descriptor 2 - Manufacturer "Asmedia"
 */
__code const uint8_t usb_string_descriptor_2[16] = {
    0x10,        /* bLength (16 bytes) */
    0x03,        /* bDescriptorType (String) */
    'A', 0x00,
    's', 0x00,
    'm', 0x00,
    'e', 0x00,
    'd', 0x00,
    'i', 0x00,
    'a', 0x00
};

/*
 * String Descriptor 3 - Product "ASM2464PD"
 */
__code const uint8_t usb_string_descriptor_3[20] = {
    0x14,        /* bLength (20 bytes) */
    0x03,        /* bDescriptorType (String) */
    'A', 0x00,
    'S', 0x00,
    'M', 0x00,
    '2', 0x00,
    '4', 0x00,
    '6', 0x00,
    '4', 0x00,
    'P', 0x00,
    'D', 0x00
};

/*
 * BOS Descriptor (Binary Object Store)
 * Required for USB 3.0 devices to advertise SuperSpeed capabilities
 */
__code const uint8_t usb_bos_descriptor[22] = {
    /* BOS Descriptor Header */
    0x05,        /* bLength (5 bytes) */
    0x0F,        /* bDescriptorType (BOS) */
    0x16, 0x00,  /* wTotalLength (22 bytes) */
    0x02,        /* bNumDeviceCaps (2 capability descriptors) */

    /* USB 2.0 Extension Descriptor */
    0x07,        /* bLength (7 bytes) */
    0x10,        /* bDescriptorType (Device Capability) */
    0x02,        /* bDevCapabilityType (USB 2.0 Extension) */
    0x02, 0x00, 0x00, 0x00,  /* bmAttributes (LPM supported) */

    /* SuperSpeed USB Device Capability */
    0x0A,        /* bLength (10 bytes) */
    0x10,        /* bDescriptorType (Device Capability) */
    0x03,        /* bDevCapabilityType (SuperSpeed USB) */
    0x00,        /* bmAttributes */
    0x0E, 0x00,  /* wSpeedsSupported (FS, HS, SS) */
    0x01,        /* bFunctionalitySupport (Full Speed minimum) */
    0x0A,        /* bU1DevExitLat (10us) */
    0xFF, 0x07   /* wU2DevExitLat (2047us) */
};

/*
 * Get descriptor pointer and length by type and index
 * Returns pointer to descriptor in code ROM, sets *length
 */
__code const uint8_t* usb_get_descriptor(uint8_t type, uint8_t index, uint8_t *length)
{
    switch (type) {
        case 0x01:  /* Device descriptor */
            *length = sizeof(usb_device_descriptor);
            return usb_device_descriptor;

        case 0x02:  /* Configuration descriptor */
            *length = sizeof(usb_config_descriptor);
            return usb_config_descriptor;

        case 0x03:  /* String descriptor */
            switch (index) {
                case 0:
                    *length = sizeof(usb_string_descriptor_0);
                    return usb_string_descriptor_0;
                case 1:
                    *length = sizeof(usb_string_descriptor_1);
                    return usb_string_descriptor_1;
                case 2:
                    *length = sizeof(usb_string_descriptor_2);
                    return usb_string_descriptor_2;
                case 3:
                    *length = sizeof(usb_string_descriptor_3);
                    return usb_string_descriptor_3;
                default:
                    *length = 0;
                    return (void*)0;
            }

        case 0x0F:  /* BOS descriptor */
            *length = sizeof(usb_bos_descriptor);
            return usb_bos_descriptor;

        default:
            *length = 0;
            return (void*)0;
    }
}
