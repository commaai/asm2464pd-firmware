/*
 * ASM2464PD Firmware - SCSI/USB Mass Storage Functions
 *
 * Functions for USB Mass Storage protocol handling and SCSI command translation.
 * These functions handle CBW parsing, CSW generation, and buffer management.
 *
 * Address range: 0x4013-0x5765 (various functions)
 */

#include "../types.h"
#include "../sfr.h"
#include "../registers.h"
#include "../globals.h"
#include "../structs.h"

/* External functions */
extern uint8_t usb_read_transfer_params_hi(void);
extern uint8_t usb_read_transfer_params_lo(void);
extern uint16_t usb_read_transfer_params(void);
extern uint8_t protocol_compare_32bit(void);
extern void idata_load_dword(uint8_t addr);
extern void idata_store_dword(uint8_t addr);
extern uint8_t helper_0cab(uint8_t r0, uint8_t r1, uint8_t r6, uint8_t r7);
extern void helper_0c64(uint8_t a, uint8_t b);
extern uint8_t helper_313f(uint8_t param, uint8_t r0);
extern uint8_t helper_3298(void);
extern uint8_t helper_328a(void);
extern void flash_add_to_xdata16(uint8_t lo, uint8_t hi);
extern void nvme_io_request(uint8_t param1, uint8_t param2);
extern void usb_set_transfer_flag(void);
extern void usb_ep_config_bulk(void);
extern void usb_ep_config_int(void);
extern void power_check_status(uint8_t param);
extern void usb_parse_descriptor(uint8_t param1, uint8_t param2);
extern uint8_t usb_event_handler(void);
extern void usb_reset_interface(uint8_t param);
extern uint8_t usb_setup_endpoint(uint8_t param);
extern uint8_t reg_poll(uint8_t param);
extern void usb_set_done_flag(void);
extern void usb_set_transfer_active_flag(void);
extern void nvme_read_status(void);
extern void nvme_check_completion(uint16_t addr);
extern void dma_start_transfer(void);
extern void xdata_load_dword(void);
extern void handler_039a_buffer_dispatch(void);
extern uint8_t helper_1b0b(uint8_t param);
extern void helper_1b2e(uint8_t param);
extern void helper_1b30(uint8_t param);
extern void helper_1c13(uint8_t param);
extern void helper_166f(void);
extern void helper_15d4(void);
extern uint8_t helper_1646(void);
extern void usb_shift_right_3(uint8_t param);
extern void helper_15ef(uint8_t a, uint8_t b);
extern void helper_15f1(uint8_t param);
extern void usb_calc_addr_with_offset(void);
extern void helper_3f4a(void);
extern void interface_ready_check(uint8_t p1, uint8_t p2, uint8_t p3);
extern void dispatch_039f(uint8_t param);
extern void dispatch_04fd(void);
extern void dispatch_04ee(void);
extern void dispatch_04e9(void);
extern void dispatch_045d(void);
extern void dispatch_044e(void);
extern void dispatch_032c(void);
extern void dispatch_0340(void);
extern void handler_0327_usb_power_init(void);
extern void helper_3578(uint8_t param);
extern void helper_157d(void);
extern void protocol_dispatch(uint8_t param);
extern void transfer_func_1633(uint16_t param);
extern void helper_1579(void);
extern uint8_t usb_get_sys_status_offset(void);
extern void helper_3219(void);
extern void helper_3291(void);
extern void nvme_init_step(void);
extern void transfer_func_16b0(uint8_t param);
extern void helper_16e9(uint8_t param);
extern void helper_16eb(uint8_t param);
extern void nvme_util_advance_queue(void);
extern void handler_2608(void);
extern void nvme_util_clear_completion(void);
extern void nvme_util_check_command_ready(void);
extern void nvme_load_transfer_data(void);
extern void dma_setup_transfer(uint8_t p1, uint8_t p2, uint8_t p3);
extern void usb_copy_status_to_buffer(void);
extern void xdata_store_dword(uint8_t r0, uint16_t dptr, uint8_t r4, uint8_t r5, uint8_t r6, uint8_t r7);
extern void dispatch_0534(uint8_t p1, uint8_t p2, uint8_t p3);
extern void dispatch_0426(uint8_t param);

/* IDATA pointers for register-based operations */
#define IDATA_CMD_BUF     ((__idata uint8_t *)0x09)
#define IDATA_TRANSFER    ((__idata uint8_t *)0x6B)
#define IDATA_BUF_CTRL    ((__idata uint8_t *)0x6F)

/* Additional IDATA variables */
#define I_WORK_3B         (*((__idata uint8_t *)0x3B))

/* Additional registers */
#define REG_USB_STATUS_REG      XDATA_REG8(0x9012)
#define REG_SCSI_DMA_STATUS_L   XDATA_REG8(0xCE6E)
#define REG_SCSI_DMA_STATUS_H   XDATA_REG8(0xCE6F)
#define REG_BUFFER_CTRL_GLOBAL  XDATA_REG8(0xCE80)
#define REG_BUFFER_THRESHOLD_HIGH XDATA_REG8(0xCE81)
#define REG_BUFFER_THRESHOLD_LOW  XDATA_REG8(0xCE82)
#define REG_BUFFER_FLOW_CTRL    XDATA_REG8(0xCE83)
#define REG_UART_THR_RBR        XDATA_REG8(0xC000)
#define XDATA_REG8(addr)        (*(__xdata volatile uint8_t *)(addr))

/* Forward declarations */
static void scsi_setup_buffer_length(uint8_t hi, uint8_t lo);
static void scsi_set_usb_mode(uint8_t mode);
static void scsi_init_interface(void);
static void scsi_pcie_send_status(uint8_t param);
static void scsi_dispatch_0426(void);
static void scsi_cmd_process_4d92(void);
static void scsi_cmd_state_4c98(void);
static void scsi_cmd_clear_4c40(void);
static void helper_1580(uint8_t param);

/*
 * scsi_setup_transfer_result - Setup transfer result registers
 * Address: 0x4013-0x4054 (66 bytes)
 *
 * Prepares transfer parameters based on comparison result.
 * If compare succeeds: calculates new params from 0x31a5 result
 * If compare fails: stores zeros in IDATA[0x09]
 */
void scsi_setup_transfer_result(__xdata uint8_t *param)
{
    uint8_t hi, lo;
    uint8_t carry;

    /* Get transfer parameters and store result */
    *param = helper_3298();

    /* Read transfer params and compare */
    usb_read_transfer_params();
    carry = protocol_compare_32bit();

    if (carry) {
        /* Compare failed - store zeros to IDATA[0x09] */
        IDATA_CMD_BUF[0] = 0;
        IDATA_CMD_BUF[1] = 0;
        IDATA_CMD_BUF[2] = 0;
        IDATA_CMD_BUF[3] = 0;
    } else {
        /* Compare succeeded - load IDATA, recalculate */
        idata_load_dword(0x09);
        hi = usb_read_transfer_params_hi();
        lo = usb_read_transfer_params_lo();
        helper_0cab(0, 0, hi, lo);
        idata_store_dword(0x09);
    }
}

/*
 * scsi_process_transfer - Process SCSI transfer with counter management
 * Address: 0x4042-0x40d8 (from continuation of 4013)
 *
 * Manages transfer counters and initiates NVMe I/O requests.
 */
void scsi_process_transfer(uint8_t param_lo, uint8_t param_hi)
{
    uint8_t count_lo, count_hi;
    uint8_t mode;
    uint8_t transfer_hi, transfer_lo;

    flash_add_to_xdata16(param_lo, param_hi);

    /* Check if transfer count exceeds 16 */
    count_lo = G_XFER_COUNT_LO;
    if (count_lo >= 0x10) {
        /* Increment retry counter, reset count */
        G_XFER_RETRY_CNT++;
        G_XFER_COUNT_LO = 0;
        G_XFER_COUNT_HI = 0;
    }

    /* Call protocol handler with offset 9 */
    if (helper_313f(count_lo - 0x10, 9) == 0) {
        return;
    }

    /* Set mode based on G_XFER_MODE_0AF9 */
    mode = G_XFER_MODE_0AF9;
    if (mode == 1) {
        G_EP_DISPATCH_VAL3 = 0xF0;
    } else if (mode == 2) {
        G_EP_DISPATCH_VAL3 = 0xE8;
    } else {
        G_EP_DISPATCH_VAL3 = 0x80;
    }

    G_EP_DISPATCH_VAL4 = 0;

    /* Setup address */
    flash_add_to_xdata16(G_XFER_COUNT_LO, G_XFER_COUNT_HI);
    G_XFER_RETRY_CNT = G_XFER_RETRY_CNT | helper_3298();

    /* Transfer loop */
    transfer_hi = G_TRANSFER_PARAMS_HI;
    transfer_lo = G_TRANSFER_PARAMS_LO;

    while (1) {
        uint8_t cmp_hi = transfer_hi;
        uint8_t cmp_lo = transfer_lo;

        if (cmp_hi < count_hi || (cmp_hi == count_hi && cmp_lo < count_lo + 1)) {
            break;
        }

        nvme_io_request(G_EP_DISPATCH_VAL4, G_EP_DISPATCH_VAL3);
        count_lo++;
        if (count_lo == 0) {
            count_hi++;
        }
    }

    /* Setup buffer length */
    scsi_setup_buffer_length(transfer_hi - count_hi, transfer_lo - count_lo);
}

/*
 * scsi_state_handler_40d9 - State machine handler
 * Address: 0x40d9-0x419c (196 bytes)
 *
 * Handles various command states (0x09, 0x0A, 0x01, 0x02, 0x03, 0x05, 0x08).
 */
void scsi_state_handler_40d9(void)
{
    uint8_t state = I_STATE_6A;
    uint8_t offset;
    uint8_t result;

    if (state == 0x09) {
        /* State 0x09: Setup complete flag */
        G_STATE_FLAG_06E6 = 1;
        offset = *(__idata uint8_t *)0x0D;
        result = helper_1b0b(offset + 0x71);

        if (result != 0) {
            /* Error path */
            helper_1b30(offset + 0x08);
            *(__xdata uint8_t *)0x06CB = 0xE0;
        } else {
            /* Success path */
            helper_1b2e(offset);
            *(__xdata uint8_t *)0x06CB = 0x60;
            helper_1c13(offset + 0x0C);
        }
        usb_set_transfer_flag();
        return;
    }

    if (state == 0x0A) {
        /* State 0x0A: Similar to 0x09 with different address */
        *(__xdata uint8_t *)0x07EA = 1;
        offset = *(__idata uint8_t *)0x0D;
        result = helper_1b0b(offset + 0x71);

        if (result != 0) {
            helper_1b30(offset + 0x08);
            *(__xdata uint8_t *)0x07EA = 0xF4;
        } else {
            helper_1b2e(offset);
            *(__xdata uint8_t *)0x07EA = 0x74;
            helper_1c13(offset + 0x0C);
        }
        usb_set_transfer_flag();
        return;
    }

    if (state == 0x01) {
        scsi_set_usb_mode(1);
        usb_ep_config_bulk();
        return;
    }

    if (state == 0x02) {
        scsi_set_usb_mode(0);
        usb_ep_config_int();
        return;
    }

    if (state == 0x03) {
        power_check_status(G_SYS_STATUS_PRIMARY + 0x56);
        return;
    }

    if (state == 0x08) {
        scsi_set_usb_mode(1);
        scsi_setup_buffer_length(0, 0);
        return;
    }

    if (state == 0x05) {
        if (G_SYS_FLAGS_0052 != 0) {
            usb_parse_descriptor(G_SYS_FLAGS_0052, 0);
            return;
        }
        usb_parse_descriptor(0, 0);
        if (G_EP_STATUS_CTRL != 0) {
            scsi_init_interface();
        }
    }
}

/*
 * scsi_action_handler_419d - Action handler
 * Address: 0x419d-0x425e (194 bytes)
 *
 * Handles USB event setup and interface reset.
 */
void scsi_action_handler_419d(uint8_t param)
{
    uint8_t event_result;
    uint8_t setup_result;

    G_ACTION_CODE_0A83 = param;

    event_result = usb_event_handler();
    usb_reset_interface(event_result + 0x06);

    I_WORK_3A = G_ACTION_CODE_0A83;
    I_WORK_3B = *(__xdata uint8_t *)(0x0A83 + 1);

    G_SYS_FLAGS_0052 |= 0x10;

    event_result = usb_event_handler();
    setup_result = usb_setup_endpoint(event_result + 0x04);
    *(__xdata uint8_t *)0x0053 = setup_result;
    G_BUFFER_LENGTH_HIGH = 0;

    reg_poll(setup_result);
    I_WORK_52 |= reg_poll(setup_result);

    scsi_process_transfer(0, 0);
}

/*
 * scsi_mode_setup_425f - Mode setup
 * Address: 0x425f-0x43d2 (372 bytes)
 *
 * Configures transfer mode and parameters.
 */
void scsi_mode_setup_425f(uint8_t param)
{
    uint8_t mode;

    *(__xdata uint8_t *)0x0A8E = param;
    G_XFER_MODE_0AF9 = param;
    G_XFER_COUNT_LO = 0;
    G_XFER_COUNT_HI = 0;
    G_XFER_RETRY_CNT = 0;

    mode = helper_328a();
    if (mode == 1) {
        G_TRANSFER_PARAMS_HI = 2;
        G_TRANSFER_PARAMS_LO = 0;
    } else if (mode == 2) {
        G_TRANSFER_PARAMS_HI = 4;
        G_TRANSFER_PARAMS_LO = 0;
    } else {
        G_TRANSFER_PARAMS_HI = 0;
        G_TRANSFER_PARAMS_LO = 0x40;
    }

    /* Load and compare dwords */
    idata_load_dword(0x09);
    idata_load_dword(0x6B);

    /* Store result */
    idata_store_dword(0x6F);
}

/*
 * scsi_dma_handler_43d3 - DMA control handler
 * Address: 0x43d3-0x4468 (150 bytes)
 *
 * Handles DMA transfer initiation based on mode flags.
 */
void scsi_dma_handler_43d3(uint8_t param)
{
    uint8_t status;
    uint8_t event_result;

    *(__xdata uint8_t *)0x0A8D = param;

    /* Check bit 0 */
    if ((param & 0x01) != 0) {
        helper_3f4a();
        if (param != 0) {
            *(__xdata uint8_t *)0x0214 = param;
            return;
        }
    }

    status = REG_USB_STATUS_REG;
    if ((status & 0x01) == 0) {
        return;
    }

    param = *(__xdata uint8_t *)0x0A8D;

    /* Check bit 1 - setup endpoint */
    if ((param >> 1) & 0x01) {
        event_result = usb_event_handler();
        usb_setup_endpoint(event_result + 0x13);
        IDATA_TRANSFER[0] = 0;
        IDATA_TRANSFER[1] = 0;
        IDATA_TRANSFER[2] = 0;
        IDATA_TRANSFER[3] = 0;
        return;
    }

    /* Check bit 2 - reset interface type 1 */
    if ((param >> 2) & 0x01) {
        event_result = usb_event_handler();
        usb_reset_interface(event_result + 0x16);
        IDATA_TRANSFER[0] = 0;
        IDATA_TRANSFER[1] = 0;
        IDATA_TRANSFER[2] = 0;
        IDATA_TRANSFER[3] = 0;
        return;
    }

    /* Check bit 3 - reset interface type 2 */
    if ((param >> 3) & 0x01) {
        event_result = usb_event_handler();
        usb_reset_interface(event_result + 0x15);
        xdata_load_dword();
        return;
    }

    /* Check bit 4 - reset interface type 3 */
    if ((param >> 4) & 0x01) {
        event_result = usb_event_handler();
        usb_reset_interface(event_result + 0x19);
        xdata_load_dword();
        return;
    }

    /* Check bit 5 - DMA check mode 1 */
    param = *(__xdata uint8_t *)0x0A8D;
    if ((param >> 5) & 0x01) {
        IDATA_TRANSFER[0] = 0;
        IDATA_TRANSFER[1] = 0;
        IDATA_TRANSFER[2] = 0;
        IDATA_TRANSFER[3] = 0;
        if (reg_poll(0) == 0) {
            *(__xdata uint8_t *)0x0214 = 5;
            return;
        }
    }

    /* Check bit 6 - DMA start */
    param = *(__xdata uint8_t *)0x0A8D;
    if ((param >> 6) & 0x01) {
        IDATA_TRANSFER[0] = 0;
        IDATA_TRANSFER[1] = 0;
        IDATA_TRANSFER[2] = 0x40;
        IDATA_TRANSFER[3] = 0;
        dma_start_transfer();
        *(__xdata uint8_t *)0x0214 = 5;
    }
}

/*
 * scsi_dma_start_4469 - Start DMA transfer
 * Address: 0x4469-0x4531 (201 bytes)
 *
 * Initiates DMA transfer with parameters.
 */
void scsi_dma_start_4469(uint8_t param)
{
    IDATA_TRANSFER[0] = param;
    IDATA_TRANSFER[1] = param;
    IDATA_TRANSFER[2] = 0x40;
    IDATA_TRANSFER[3] = 0;

    dma_start_transfer();
    *(__xdata uint8_t *)0x0214 = 5;
}

/*
 * scsi_init_interface - Initialize interface
 * Address: 0x4532-0x45cf (158 bytes)
 *
 * Initializes USB/SCSI interface based on flags.
 */
static void scsi_init_interface(void)
{
    uint8_t flags;

    I_WORK_3A = G_EP_STATUS_CTRL;
    flags = I_WORK_3A;

    /* Bit 7: Main interface */
    if ((flags & 0x80) != 0) {
        interface_ready_check(0, 0x13, 5);
        dispatch_039f(0);
        *(__xdata uint8_t *)0x0B2F = 1;
        dispatch_04fd();
    }

    /* Bit 4: Secondary interface */
    if ((flags >> 4) & 0x01) {
        interface_ready_check(1, 0x8F, 5);
    }

    /* Bit 3: Protocol init */
    if ((flags >> 3) & 0x01) {
        helper_3578(0x81);
    }

    /* Bit 1: Endpoint init */
    if ((flags >> 1) & 0x01) {
        dispatch_04ee();
    }

    /* Update CPU mode */
    REG_CPU_MODE_NEXT = (REG_CPU_MODE_NEXT & 0xFE) | (((flags >> 5) & 0x01) == 0);

    /* Bit 6: Check completion and loop */
    if ((flags >> 6) & 0x01) {
        nvme_check_completion(0xCC31);
        while (1) {
            /* Infinite loop - system reset required */
        }
    }

    /* Bit 2: Buffer setup */
    if ((flags >> 2) & 0x01) {
        REG_BUF_CFG_9300 = 4;
        REG_USB_PHY_CTRL_91D1 = 2;
        REG_BUF_CFG_9301 = 0x40;
        REG_BUF_CFG_9301 = 0x80;
        REG_USB_PHY_CTRL_91D1 = 8;
        REG_USB_PHY_CTRL_91D1 = 1;
        *(__xdata uint8_t *)0x01B6 = 0;
        nvme_check_completion(0xCC30);
        G_STATE_FLAG_06E6 = 1;
        dispatch_032c();
        dispatch_0340();
        handler_0327_usb_power_init();
    }
}

/*
 * scsi_buffer_handler_45d0 - Buffer management handler
 * Address: 0x45d0-0x466a (155 bytes)
 *
 * Manages SCSI buffer operations and threshold configuration.
 */
void scsi_buffer_handler_45d0(void)
{
    uint8_t val;
    uint8_t mode;

    *(__xdata uint8_t *)0x044D = 0;
    helper_166f();

    val = *(__xdata uint8_t *)0x044D;
    if (val == 1) {
        usb_calc_addr_with_offset();
        *(__xdata uint8_t *)0xCE6F = *(__xdata uint8_t *)0x044D;
        return;
    }

    usb_calc_addr_with_offset();
    val = *(__xdata uint8_t *)0x044D;
    helper_15d4();

    if (*(__xdata uint8_t *)0x044D > 1) {
        val = *(__xdata uint8_t *)0x0578;
        mode = helper_1646();
    }

    usb_shift_right_3(val);

    if (mode < 3) {
        *(__xdata uint8_t *)0xCE6F = val;
        *(__xdata uint8_t *)0xCE6F = val + 1;
        return;
    }

    if (mode < 5) {
        uint8_t bit = (val >> 2) & 0x01;
        helper_15ef(0, 0);
        *(__xdata uint8_t *)0x0578 = *(__xdata uint8_t *)0x0578 & (bit ? 0x0F : 0xF0);
        return;
    }

    if (mode < 9) {
        helper_15f1(0x40);
        *(__xdata uint8_t *)0x0578 = 0;
        return;
    }

    if (mode < 17) {
        helper_15ef(mode - 17, 0);
        *(__xdata uint8_t *)0x0578 = 0;
        helper_15f1(0x3F);
        *(__xdata uint8_t *)0x0578 = 0;
        return;
    }

    helper_15ef(mode - 17, 0);
    *(__xdata uint8_t *)0x0578 = 0;
    helper_15f1(0x3F);
    *(__xdata uint8_t *)0x0578 = 0;
    helper_15f1(0x3E);
    *(__xdata uint8_t *)0x0578 = 0;
    helper_15f1(0x3D);
    *(__xdata uint8_t *)0x0578 = 0;
}

/*
 * scsi_transfer_check_466b - Check transfer state
 * Address: 0x466b-0x480b (417 bytes)
 *
 * Checks system flags and initiates appropriate transfer operations.
 */
void scsi_transfer_check_466b(void)
{
    uint8_t status;
    uint8_t val;

    if (G_SYS_FLAGS_07EF != 0) {
        return;
    }

    if (G_TRANSFER_BUSY_0B3B != 0) {
        return;
    }

    status = REG_PHY_EXT_56;
    if (((status >> 5) & 0x01) != 1) {
        dispatch_04e9();
        return;
    }

    G_PCIE_TXN_COUNT_LO = usb_get_sys_status_offset();
    helper_157d();

    val = *(__xdata uint8_t *)0x0A8E;
    if (val == 0x10) {
        return;
    }

    val = *(__xdata uint8_t *)0x0A8E;

    if (val == 0x80) {
        transfer_func_1633(0xB480);
        protocol_dispatch(G_PCIE_TXN_COUNT_LO);
        scsi_pcie_send_status(0);
        helper_1579();
        *(__xdata uint8_t *)0x05A6 = 3;
        interface_ready_check(0, 199, 3);

        if (G_ERROR_CODE_06EA == 0xFE) {
            return;
        }

        scsi_dispatch_0426();
        helper_1579();
        *(__xdata uint8_t *)0x05A6 = 5;
        return;
    }

    if (val == 0x81 || val == 0x0F) {
        usb_set_done_flag();
        dispatch_045d();
    }
}

/*
 * scsi_queue_handler_480c - Queue management handler
 * Address: 0x480c-0x4903 (248 bytes)
 *
 * Handles NVMe queue operations and completion processing.
 */
void scsi_queue_handler_480c(void)
{
    uint8_t status;

    status = *(__xdata uint8_t *)0xE716;
    if ((status & 0x03) == 0) {
        return;
    }

    status = REG_USB_STATUS_REG;
    if ((status & 0x01) == 0) {
        /* USB not ready */
        status = *(__xdata uint8_t *)0xCE89;
        if ((status >> 2) & 0x01) {
            nvme_util_advance_queue();
        }
        return;
    }

    /* USB ready - process completions */
    while (1) {
        if (G_NVME_QUEUE_READY == 0) {
            status = REG_CPU_LINK_CEF3;
            if ((status >> 3) & 0x01) {
                REG_CPU_LINK_CEF3 = 8;
                handler_2608();
            }

            status = REG_NVME_LINK_STATUS;
            if ((status >> 1) & 0x01) {
                nvme_util_clear_completion();
            }

            status = REG_NVME_LINK_STATUS;
            if ((status & 0x01) != 0) {
                nvme_util_check_command_ready();
            }
        }
        /* Loop continues based on queue state */
        break;
    }
}

/*
 * scsi_csw_handler_4904 - CSW generation
 * Address: 0x4904-0x4976 (115 bytes)
 *
 * Generates Command Status Wrapper response.
 */
void scsi_csw_handler_4904(void)
{
    /* Build and send CSW */
    /* CSW signature 'USBS' */
    USB_CSW->sig0 = 0x55;  /* 'U' */
    USB_CSW->sig1 = 0x53;  /* 'S' */
    USB_CSW->sig2 = 0x42;  /* 'B' */
    USB_CSW->sig3 = 0x53;  /* 'S' */

    /* Copy tag from CBW */
    USB_CSW->tag0 = REG_CBW_TAG_0;
    USB_CSW->tag1 = REG_CBW_TAG_1;
    USB_CSW->tag2 = REG_CBW_TAG_2;
    USB_CSW->tag3 = REG_CBW_TAG_3;

    /* Residue from IDATA[0x6F-0x72] */
    USB_CSW->residue0 = IDATA_BUF_CTRL[0];
    USB_CSW->residue1 = IDATA_BUF_CTRL[1];
    USB_CSW->residue2 = IDATA_BUF_CTRL[2];
    USB_CSW->residue3 = IDATA_BUF_CTRL[3];

    /* Status byte - success */
    USB_CSW->status = 0;

    /* Set packet length (13 bytes) and trigger */
    REG_USB_MSC_LENGTH = 13;
    REG_USB_MSC_CTRL = 0x01;

    /* Clear status bit */
    REG_USB_MSC_STATUS = REG_USB_MSC_STATUS & 0xFE;
}

/*
 * scsi_send_csw_4977 - Send CSW with status
 * Address: 0x4977-0x4b24 (430 bytes)
 *
 * Sends Command Status Wrapper with specified status.
 */
void scsi_send_csw_4977(uint8_t param_hi, uint8_t param_lo)
{
    uint8_t status;

    /* Check SCSI control state */
    status = G_SCSI_CTRL;
    if (status != 0) {
        G_SCSI_CTRL = status - 1;
    }

    /* Generate and send CSW */
    scsi_csw_handler_4904();
}

/*
 * scsi_setup_buffer_length - Setup SCSI buffer length registers
 * Address: 0x5216-0x523b (38 bytes)
 *
 * Configures buffer length registers for SCSI transfer.
 */
static void scsi_setup_buffer_length(uint8_t hi, uint8_t lo)
{
    uint8_t carry;

    usb_read_transfer_params();
    carry = protocol_compare_32bit();

    if (carry) {
        /* Compare failed - use IDATA values */
        idata_load_dword(0x09);
        lo = IDATA_CMD_BUF[2];
        hi = IDATA_CMD_BUF[3];
    } else {
        /* Compare succeeded - use transfer params */
        lo = usb_read_transfer_params_lo();
    }

    REG_USB_SCSI_BUF_LEN_L = hi;
    REG_USB_SCSI_BUF_LEN_H = lo;
    REG_USB_EP_CFG1 = 0x08;
    REG_USB_EP_CFG2 = 0x02;
}

/*
 * scsi_set_usb_mode - Set USB transfer mode
 * Address: 0x5321-0x533c (28 bytes)
 *
 * Configures USB transfer mode based on status.
 */
static void scsi_set_usb_mode(uint8_t mode)
{
    uint8_t status;
    uint8_t speed;

    status = REG_USB_STATUS_REG;
    if ((status & 0x01) == 0) {
        return;
    }

    speed = helper_328a();
    if (speed != 1) {
        return;
    }

    if (mode != 0) {
        *(__xdata uint8_t *)0x91D0 = 0x08;
    } else {
        *(__xdata uint8_t *)0x91D0 = 0x10;
    }
}

/*
 * scsi_dma_status_533d - DMA status handler
 * Address: 0x533d-0x5358 (28 bytes)
 *
 * Handles SCSI DMA status updates.
 */
void scsi_dma_status_533d(uint8_t param)
{
    *(__xdata uint8_t *)0xCE95 = param >> 1;

    if (*(__xdata uint8_t *)0xCE65 == 0) {
        return;
    }

    REG_SCSI_DMA_STATUS_L = param;
    REG_SCSI_DMA_STATUS_H = param + 1;
}

/*
 * scsi_status_update_5359 - Update system status
 * Address: 0x5359-0x5372 (26 bytes)
 *
 * Updates primary system status with parameter.
 */
void scsi_status_update_5359(uint8_t param)
{
    uint8_t status;

    status = G_SYS_STATUS_PRIMARY;
    helper_16e9(status);
    I_WORK_51 = G_SYS_STATUS_PRIMARY;

    status = (I_WORK_51 + param) & 0x1F;
    helper_16eb(status + 0x56);
    G_SYS_STATUS_PRIMARY = status;
}

/*
 * scsi_write_residue_53c0 - Write residue to CSW buffer
 * Address: 0x53c0-0x53d3 (20 bytes)
 *
 * Writes residue value from IDATA to CSW buffer.
 */
void scsi_write_residue_53c0(void)
{
    REG_BUFFER_CTRL_GLOBAL = I_BUF_CTRL_GLOBAL;
    REG_BUFFER_THRESHOLD_HIGH = I_BUF_THRESH_HI;
    REG_BUFFER_THRESHOLD_LOW = I_BUF_THRESH_LO;
    REG_BUFFER_FLOW_CTRL = I_BUF_FLOW_CTRL;
}

/*
 * scsi_pcie_send_status - Send PCIe status
 * Address: 0x519e-0x51c6 (41 bytes)
 *
 * Sends status over PCIe with configuration.
 */
static void scsi_pcie_send_status(uint8_t param)
{
    *(__idata uint8_t *)0x65 = 3;
    helper_1580(G_PCIE_TXN_COUNT_LO);

    /* Store status */
    xdata_store_dword(0, 0xB220, 0, 0, 0, param | 0x08);
    dispatch_044e();
}

/*
 * scsi_cbw_validate_51ef - Validate CBW signature
 * Address: 0x51ef-0x51f8 (10 bytes)
 *
 * Validates 'USBC' signature in Command Block Wrapper.
 */
uint8_t scsi_cbw_validate_51ef(void)
{
    uint8_t len_hi = *(__xdata uint8_t *)0x9119;
    uint8_t len_lo = *(__xdata uint8_t *)0x911A;

    /* Check length is 0x1F (31 bytes for CBW) */
    if (len_lo != 0x1F || len_hi != 0x00) {
        return 0;
    }

    /* Validate 'USBC' signature */
    if (REG_USB_BUFFER_ALT != 'U') return 0;
    if (*(__xdata uint8_t *)0x911C != 'S') return 0;
    if (*(__xdata uint8_t *)0x911D != 'B') return 0;
    if (*(__xdata uint8_t *)0x911E != 'C') return 0;

    return 1;
}

/*
 * scsi_uart_hex_51c7 - Output hex byte to UART
 * Address: 0x51c7-0x51e5 (31 bytes)
 *
 * Outputs a byte as two hex digits to UART.
 */
void scsi_uart_hex_51c7(uint8_t val)
{
    uint8_t hi = val >> 4;
    uint8_t lo = val & 0x0F;
    uint8_t ch;

    /* Output high nibble */
    ch = (hi < 10) ? '0' : '7';
    REG_UART_THR_RBR = ch + hi;

    /* Output low nibble */
    ch = (lo < 10) ? '0' : '7';
    REG_UART_THR_RBR = ch + lo;
}

/*
 * scsi_dispatch_0426 - Dispatch to handler 0x0426
 * Address: inline helper
 */
static void scsi_dispatch_0426(void)
{
    dispatch_0426(0x14);
}

/*
 * scsi_transfer_handler_5069 - Transfer control handler
 * Address: 0x5069-0x50fe (150 bytes)
 *
 * Manages transfer state and DMA operations.
 */
void scsi_transfer_handler_5069(uint8_t param)
{
    G_XFER_CTRL_0AF7 = 0;
    helper_3f4a();
    I_WORK_3B = param;

    if (param != 0) {
        if (G_TRANSFER_ACTIVE != 0) {
            G_XFER_CTRL_0AF7 = 1;
        }
        return;
    }

    if (*(__xdata uint8_t *)0x044B == 1 && *(__xdata uint8_t *)0x0006 != 0) {
        dma_setup_transfer(0, 0x3A, 2);
    }

    nvme_load_transfer_data();
}

/*
 * scsi_copy_cbw_data_5112 - Copy CBW data to work area
 * Address: 0x5112-0x5156 (69 bytes)
 *
 * Copies CBW fields to internal work variables.
 */
void scsi_copy_cbw_data_5112(void)
{
    usb_copy_status_to_buffer();

    /* Copy CBW transfer length to IDATA */
    I_TRANSFER_6B = *(__xdata uint8_t *)0x9126;
    I_TRANSFER_6C = *(__xdata uint8_t *)0x9125;
    I_TRANSFER_6D = *(__xdata uint8_t *)0x9124;
    I_TRANSFER_6E = *(__xdata uint8_t *)0x9123;

    /* Extract direction and LUN */
    *(__xdata uint8_t *)0x0AF3 = *(__xdata uint8_t *)0x9127 & 0x80;
    *(__xdata uint8_t *)0x0AF4 = *(__xdata uint8_t *)0x9128 & 0x0F;

    /* Process command */
    scsi_cmd_process_4d92();
}

/*
 * scsi_cmd_process_4d92 - Process SCSI command
 * Address: 0x4d92-0x4e6c (219 bytes)
 *
 * Main SCSI command processing function.
 */
static void scsi_cmd_process_4d92(void)
{
    /* Command processing logic */
    scsi_cmd_state_4c98();
}

/*
 * scsi_cmd_state_4c98 - Command state machine
 * Address: 0x4c98-0x4d91 (250 bytes)
 *
 * State machine for SCSI command execution.
 */
static void scsi_cmd_state_4c98(void)
{
    /* State machine implementation */
}

/*
 * scsi_ep_init_handler - Endpoint initialization
 * Address: 0x53e6-0x541e (57 bytes)
 *
 * Initializes USB endpoints and resets state.
 */
void scsi_ep_init_handler(void)
{
    G_USB_TRANSFER_FLAG = 0;
    I_STATE_6A = 0;
    G_STATE_FLAG_06E6 = 0;
    handler_039a_buffer_dispatch();
}

/*
 * scsi_check_e716_541f - Check transfer status register
 * Address: 0x541f-0x5425 (7 bytes)
 *
 * Returns bits 0-1 of transfer status.
 */
uint8_t scsi_check_e716_541f(void)
{
    return *(__xdata uint8_t *)0xE716 & 0x03;
}

/*
 * scsi_handler_5305 - Flash ready handler
 * Address: 0x5305-0x5320 (28 bytes)
 *
 * Handles flash ready status checking.
 */
void scsi_handler_5305(void)
{
    uint8_t status1, status2, status3;

    scsi_cmd_clear_4c40();

    status1 = REG_FLASH_READY_STATUS;
    status2 = REG_FLASH_READY_STATUS;
    status3 = REG_FLASH_READY_STATUS;

    dispatch_0534(status3 & 0x20, status2 & 0x02, status1 & 0x01);

    G_SYS_FLAGS_07F6 = 1;
}

/*
 * scsi_cmd_clear_4c40 - Clear command state
 * Address: 0x4c40-0x4c97 (88 bytes)
 *
 * Clears SCSI command state and buffers.
 */
static void scsi_cmd_clear_4c40(void)
{
    /* Clear command state */
}

/*
 * scsi_transfer_check_5373 - Check transfer completion
 * Address: 0x5373-0x5397 (37 bytes)
 *
 * Checks if transfer is complete based on mask.
 */
void scsi_transfer_check_5373(uint8_t param)
{
    uint8_t status;
    static __code const uint8_t mask_table[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

    status = *(__xdata uint8_t *)0xCE5D;
    if ((status & mask_table[param]) != 0) {
        usb_shift_right_3(param);
        /* Additional processing */
    }
}

/*
 * scsi_queue_dispatch_52c7 - Queue dispatch handler
 * Address: 0x52c7-0x5304 (62 bytes)
 *
 * Dispatches queue operations based on mask.
 */
void scsi_queue_dispatch_52c7(uint8_t param)
{
    uint8_t status;
    static __code const uint8_t mask_table[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

    status = *(__xdata uint8_t *)0xCE5F;
    if ((status & mask_table[param]) != 0) {
        transfer_func_16b0(param);
        *(__xdata uint8_t *)0xCE5F = param + 2;
        *(__xdata uint8_t *)0xCE5F = param + 3;
    }
}

/*
 * helper_1580 - Helper function stub
 * Address: 0x1580
 */
static void helper_1580(uint8_t param)
{
    (void)param;  /* Stub - actual implementation pending */
}
