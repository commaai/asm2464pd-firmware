/*
 * ASM2464PD Firmware - SCSI/USB Mass Storage Functions
 *
 * Functions for USB Mass Storage protocol handling and SCSI command translation.
 * These functions handle CBW parsing, CSW generation, and buffer management.
 *
 * Address range: 0x4013-0x5765 (various functions)
 */

#include "types.h"
#include "sfr.h"
#include "registers.h"
#include "globals.h"
#include "structs.h"

/* External functions */
extern uint8_t usb_read_transfer_params_hi(void);
extern uint8_t usb_read_transfer_params_lo(void);
extern uint16_t usb_read_transfer_params(void);
extern uint8_t protocol_compare_32bit(void);
extern void idata_load_dword(uint8_t addr);
extern void idata_store_dword(uint8_t addr);
extern uint8_t helper_0cab(uint8_t r0, uint8_t r1, uint8_t r6, uint8_t r7);
extern void helper_0c64(uint8_t a, uint8_t b);
extern uint8_t helper_313f(uint8_t r0_val);
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
extern void handler_d916(void);               /* was: dispatch_039f */
extern void handler_e96c(void);               /* was: dispatch_04fd */
extern void handler_e6fc(void);               /* was: dispatch_04ee */
extern void dispatch_04e9(void);              /* 0x04e9 -> 0xE8E4 */
extern void pcie_tunnel_enable(void);         /* 0xC00D */
extern void handler_e91d(void);               /* was: dispatch_044e */
extern void phy_power_config_handler(void);   /* was: dispatch_032c */
extern void handler_bf8e(void);               /* was: dispatch_0340 */
extern void handler_0327_usb_power_init(void);
extern void helper_3578(uint8_t param);
extern void helper_157d(void);
extern void protocol_dispatch(uint8_t param);
extern void transfer_func_1633(uint16_t param);
extern void helper_1579(void);
extern uint8_t usb_get_sys_status_offset(void);
extern void nvme_call_and_signal_3219(void);
extern uint8_t queue_idx_get_3291(void);
extern void nvme_init_step(void);
extern void transfer_func_16b0(uint8_t param);
extern void helper_16e9(uint8_t param);
extern void helper_16eb(uint8_t param);
extern void nvme_util_advance_queue(void);
extern void dma_queue_state_handler(void);
extern void nvme_util_clear_completion(void);
extern void nvme_util_check_command_ready(void);
extern void nvme_load_transfer_data(void);
extern void dma_setup_transfer(uint8_t p1, uint8_t p2, uint8_t p3);
extern void usb_copy_status_to_buffer(void);
extern void xdata_store_dword(__xdata uint8_t *ptr, uint32_t val);
extern void handler_d6bc(void);               /* was: dispatch_0534 */
extern void dispatch_0426(void);              /* Bank 0 target 0xE762 */
extern void dispatch_041c(uint8_t param);     /* Bank 0 dispatch */
extern void dispatch_0453(void);              /* Bank 0 dispatch */
extern void transfer_func_173b(void);         /* Helper at 0x173b in usb.c */
extern void helper_312a(void);                /* Protocol helper at 0x312a */
extern void helper_31ce(void);                /* Protocol helper at 0x31ce */
extern void helper_1cf0(void);                /* Helper at 0x1cf0 */

/* Forward declarations */
static void scsi_setup_buffer_length(uint8_t hi, uint8_t lo);
static void scsi_set_usb_mode(uint8_t mode);
static void scsi_init_interface(void);
static void scsi_pcie_send_status(uint8_t param);
static void scsi_dispatch_reset(void);
static void scsi_cmd_process(void);
static void scsi_cmd_state_machine(void);
static void scsi_cmd_clear(void);
static void pcie_setup_transaction(uint8_t param);
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
    if (helper_313f(9) == 0) {
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
 * scsi_state_dispatch - State machine dispatcher
 * Address: 0x40d9-0x419c (196 bytes)
 *
 * Handles various command states (0x09, 0x0A, 0x01, 0x02, 0x03, 0x05, 0x08).
 */
void scsi_state_dispatch(void)
{
    uint8_t state = I_STATE_6A;
    uint8_t offset;
    uint8_t result;

    if (state == 0x09) {
        /* State 0x09: Setup complete flag */
        G_STATE_FLAG_06E6 = 1;
        offset = I_QUEUE_IDX;
        result = helper_1b0b(offset + 0x71);

        if (result != 0) {
            /* Error path */
            helper_1b30(offset + 0x08);
            G_SCSI_STATUS_06CB = 0xE0;
        } else {
            /* Success path */
            helper_1b2e(offset);
            G_SCSI_STATUS_06CB = 0x60;
            helper_1c13(offset + 0x0C);
        }
        usb_set_transfer_flag();
        return;
    }

    if (state == 0x0A) {
        /* State 0x0A: Similar to 0x09 with different address */
        G_XFER_FLAG_07EA = 1;
        offset = I_QUEUE_IDX;
        result = helper_1b0b(offset + 0x71);

        if (result != 0) {
            helper_1b30(offset + 0x08);
            G_XFER_FLAG_07EA = 0xF4;
        } else {
            helper_1b2e(offset);
            G_XFER_FLAG_07EA = 0x74;
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
 * scsi_setup_action - Setup action and configure USB events
 * Address: 0x419d-0x425e (194 bytes)
 *
 * Handles USB event setup and interface reset.
 */
void scsi_setup_action(uint8_t param)
{
    uint8_t event_result;
    uint8_t setup_result;

    G_ACTION_CODE_0A83 = param;

    event_result = usb_event_handler();
    usb_reset_interface(event_result + 0x06);

    I_WORK_3A = G_ACTION_CODE_0A83;
    I_WORK_3B = G_ACTION_PARAM_0A84;

    G_SYS_FLAGS_0052 |= 0x10;

    event_result = usb_event_handler();
    setup_result = usb_setup_endpoint(event_result + 0x04);
    G_USB_SETUP_RESULT = setup_result;
    G_BUFFER_LENGTH_HIGH = 0;

    reg_poll(setup_result);
    I_WORK_52 |= reg_poll(setup_result);

    scsi_process_transfer(0, 0);
}

/*
 * scsi_init_transfer_mode - Initialize transfer mode
 * Address: 0x425f-0x43d2 (372 bytes)
 *
 * Configures transfer mode and parameters.
 */
void scsi_init_transfer_mode(uint8_t param)
{
    uint8_t mode;

    G_DMA_MODE_0A8E = param;
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
 * scsi_dma_dispatch - DMA control dispatcher
 * Address: 0x43d3-0x4468 (150 bytes)
 *
 * Handles DMA transfer initiation based on mode flags.
 */
void scsi_dma_dispatch(uint8_t param)
{
    uint8_t status;
    uint8_t event_result;

    G_DMA_PARAM_0A8D = param;

    /* Check bit 0 */
    if ((param & 0x01) != 0) {
        helper_3f4a();
        if (param != 0) {
            G_DMA_STATE_0214 = param;
            return;
        }
    }

    status = REG_USB_FIFO_STATUS;
    if ((status & USB_FIFO_STATUS_READY) == 0) {
        return;
    }

    param = G_DMA_PARAM_0A8D;

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
    param = G_DMA_PARAM_0A8D;
    if ((param >> 5) & 0x01) {
        IDATA_TRANSFER[0] = 0;
        IDATA_TRANSFER[1] = 0;
        IDATA_TRANSFER[2] = 0;
        IDATA_TRANSFER[3] = 0;
        if (reg_poll(0) == 0) {
            G_DMA_STATE_0214 = 5;
            return;
        }
    }

    /* Check bit 6 - DMA start */
    param = G_DMA_PARAM_0A8D;
    if ((param >> 6) & 0x01) {
        IDATA_TRANSFER[0] = 0;
        IDATA_TRANSFER[1] = 0;
        IDATA_TRANSFER[2] = 0x40;
        IDATA_TRANSFER[3] = 0;
        dma_start_transfer();
        G_DMA_STATE_0214 = 5;
    }
}

/*
 * scsi_dma_start_with_param - Start DMA transfer with parameter
 * Address: 0x4469-0x4531 (201 bytes)
 *
 * Initiates DMA transfer with parameters.
 */
void scsi_dma_start_with_param(uint8_t param)
{
    IDATA_TRANSFER[0] = param;
    IDATA_TRANSFER[1] = param;
    IDATA_TRANSFER[2] = 0x40;
    IDATA_TRANSFER[3] = 0;

    dma_start_transfer();
    G_DMA_STATE_0214 = 5;
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
        handler_d916();  /* was: dispatch_039f */
        G_INTERFACE_READY_0B2F = 1;
        handler_e96c();  /* was: dispatch_04fd */
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
        handler_e6fc();  /* was: dispatch_04ee */
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
        G_USB_WORK_01B6 = 0;
        nvme_check_completion(0xCC30);
        G_STATE_FLAG_06E6 = 1;
        phy_power_config_handler();  /* was: dispatch_032c */
        handler_bf8e();  /* was: dispatch_0340 */
        handler_0327_usb_power_init();
    }
}

/*
 * scsi_buffer_threshold_config - Configure buffer thresholds
 * Address: 0x45d0-0x466a (155 bytes)
 *
 * Manages SCSI buffer operations and threshold configuration.
 */
void scsi_buffer_threshold_config(void)
{
    uint8_t val;
    uint8_t mode;

    G_LOG_INIT_044D = 0;
    helper_166f();

    val = G_LOG_INIT_044D;
    if (val == 1) {
        usb_calc_addr_with_offset();
        REG_SCSI_DMA_STATUS_H = G_LOG_INIT_044D;
        return;
    }

    usb_calc_addr_with_offset();
    val = G_LOG_INIT_044D;
    helper_15d4();

    if (G_LOG_INIT_044D > 1) {
        val = G_DMA_ENDPOINT_0578;
        mode = helper_1646();
    }

    usb_shift_right_3(val);

    if (mode < 3) {
        REG_SCSI_DMA_STATUS_H = val;
        REG_SCSI_DMA_STATUS_H = val + 1;
        return;
    }

    if (mode < 5) {
        uint8_t bit = (val >> 2) & 0x01;
        helper_15ef(0, 0);
        G_DMA_ENDPOINT_0578 = G_DMA_ENDPOINT_0578 & (bit ? 0x0F : 0xF0);
        return;
    }

    if (mode < 9) {
        helper_15f1(0x40);
        G_DMA_ENDPOINT_0578 = 0;
        return;
    }

    if (mode < 17) {
        helper_15ef(mode - 17, 0);
        G_DMA_ENDPOINT_0578 = 0;
        helper_15f1(0x3F);
        G_DMA_ENDPOINT_0578 = 0;
        return;
    }

    helper_15ef(mode - 17, 0);
    G_DMA_ENDPOINT_0578 = 0;
    helper_15f1(0x3F);
    G_DMA_ENDPOINT_0578 = 0;
    helper_15f1(0x3E);
    G_DMA_ENDPOINT_0578 = 0;
    helper_15f1(0x3D);
    G_DMA_ENDPOINT_0578 = 0;
}

/*
 * scsi_transfer_dispatch - Dispatch transfer operations
 * Address: 0x466b-0x480b (417 bytes)
 *
 * Checks system flags and initiates appropriate transfer operations.
 */
void scsi_transfer_dispatch(void)
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
        dispatch_04e9();  /* 0x04e9 -> 0xE8E4 */
        return;
    }

    G_PCIE_TXN_COUNT_LO = usb_get_sys_status_offset();
    helper_157d();

    val = G_DMA_MODE_0A8E;
    if (val == 0x10) {
        return;
    }

    val = G_DMA_MODE_0A8E;

    if (val == 0x80) {
        transfer_func_1633(0xB480);
        protocol_dispatch(G_PCIE_TXN_COUNT_LO);
        scsi_pcie_send_status(0);
        helper_1579();
        G_PCIE_TXN_COUNT_LO = 3;
        interface_ready_check(0, 199, 3);

        if (G_ERROR_CODE_06EA == 0xFE) {
            return;
        }

        scsi_dispatch_reset();
        helper_1579();
        G_PCIE_TXN_COUNT_LO = 5;
        return;
    }

    if (val == 0x81 || val == 0x0F) {
        usb_set_done_flag();
        pcie_tunnel_enable();  /* 0xC00D */
    }
}

/*
 * scsi_nvme_queue_process - Process NVMe queue and completions
 * Address: 0x480c-0x4903 (248 bytes)
 *
 * Handles NVMe queue operations and completion processing.
 */
void scsi_nvme_queue_process(void)
{
    uint8_t status;

    status = REG_LINK_STATUS_E716;
    if ((status & LINK_STATUS_E716_MASK) == 0) {
        return;
    }

    status = REG_USB_FIFO_STATUS;
    if ((status & USB_FIFO_STATUS_READY) == 0) {
        /* USB not ready */
        status = REG_XFER_READY;
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
                dma_queue_state_handler();
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
 * scsi_csw_build - Build Command Status Wrapper
 * Address: 0x4904-0x4976 (115 bytes)
 *
 * Generates Command Status Wrapper response.
 */
void scsi_csw_build(void)
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
 * scsi_csw_send - Send CSW with status
 * Address: 0x4977-0x4b24 (430 bytes)
 *
 * Sends Command Status Wrapper with specified status.
 */
void scsi_csw_send(uint8_t param_hi, uint8_t param_lo)
{
    uint8_t status;

    /* Check SCSI control state */
    status = G_SCSI_CTRL;
    if (status != 0) {
        G_SCSI_CTRL = status - 1;
    }

    /* Generate and send CSW */
    scsi_csw_build();
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

    status = REG_USB_FIFO_STATUS;
    if ((status & USB_FIFO_STATUS_READY) == 0) {
        return;
    }

    speed = helper_328a();
    if (speed != 1) {
        return;
    }

    if (mode != 0) {
        REG_USB_EP_CTRL_91D0 = 0x08;
    } else {
        REG_USB_EP_CTRL_91D0 = 0x10;
    }
}

/*
 * scsi_dma_set_mode - Set DMA transfer mode
 * Address: 0x533d-0x5358 (28 bytes)
 *
 * Handles SCSI DMA status updates.
 */
void scsi_dma_set_mode(uint8_t param)
{
    REG_XFER_MODE_CE95 = param >> 1;

    if (REG_XFER_CTRL_CE65 == 0) {
        return;
    }

    REG_SCSI_DMA_STATUS_L = param;
    REG_SCSI_DMA_STATUS_H = param + 1;
}

/*
 * scsi_sys_status_update - Update system status
 * Address: 0x5359-0x5372 (26 bytes)
 *
 * Updates primary system status with parameter.
 */
void scsi_sys_status_update(uint8_t param)
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
 * scsi_csw_write_residue - Write residue to CSW buffer
 * Address: 0x53c0-0x53d3 (20 bytes)
 *
 * Writes residue value from IDATA to CSW buffer registers.
 */
void scsi_csw_write_residue(void)
{
    REG_SCSI_BUF_CTRL = I_BUF_CTRL_GLOBAL;
    REG_SCSI_BUF_THRESH_HI = I_BUF_THRESH_HI;
    REG_SCSI_BUF_THRESH_LO = I_BUF_THRESH_LO;
    REG_SCSI_BUF_FLOW = I_BUF_FLOW_CTRL;
}

/*
 * scsi_pcie_send_status - Send PCIe status
 * Address: 0x519e-0x51c6 (41 bytes)
 *
 * Sends status over PCIe with configuration.
 */
static void scsi_pcie_send_status(uint8_t param)
{
    I_WORK_65 = 3;
    pcie_setup_transaction(G_PCIE_TXN_COUNT_LO);

    /* Store status */
    xdata_store_dword(&REG_PCIE_DATA, (uint32_t)(param | 0x08) << 24);
    handler_e91d();  /* was: dispatch_044e */
}

/*
 * scsi_cbw_validate - Validate CBW signature
 * Address: 0x51ef-0x51f8 (10 bytes)
 *
 * Validates 'USBC' signature in Command Block Wrapper.
 */
uint8_t scsi_cbw_validate(void)
{
    uint8_t len_hi = REG_USB_CBW_LEN_HI;
    uint8_t len_lo = REG_USB_CBW_LEN_LO;

    /* Check length is 0x1F (31 bytes for CBW) */
    if (len_lo != 0x1F || len_hi != 0x00) {
        return 0;
    }

    /* Validate 'USBC' signature */
    if (REG_USB_BUFFER_ALT != 'U') return 0;
    if (REG_USB_CBW_SIG1 != 'S') return 0;
    if (REG_USB_CBW_SIG2 != 'B') return 0;
    if (REG_USB_CBW_SIG3 != 'C') return 0;

    return 1;
}

/*
 * uart_print_hex_byte - Output hex byte to UART
 * Address: 0x51c7-0x51e5 (31 bytes)
 *
 * Outputs a byte as two hex digits to UART.
 */
void uart_print_hex_byte(uint8_t val)
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
 * scsi_dispatch_reset - Dispatch reset handler
 * Address: inline helper
 */
static void scsi_dispatch_reset(void)
{
    /* Parameter 0x14 passed via R7 in original code */
    dispatch_0426();  /* Bank 0 target 0xE762 */
}

/*
 * scsi_transfer_start - Start SCSI transfer
 * Address: 0x5069-0x50fe (150 bytes)
 *
 * Manages transfer state and DMA operations.
 */
void scsi_transfer_start(uint8_t param)
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

    if (G_LOG_COUNTER_044B == 1 && G_WORK_0006 != 0) {
        dma_setup_transfer(0, 0x3A, 2);
    }

    nvme_load_transfer_data();
}

/*
 * scsi_cbw_parse - Parse CBW fields
 * Address: 0x5112-0x5156 (69 bytes)
 *
 * Copies CBW fields to internal work variables.
 */
void scsi_cbw_parse(void)
{
    usb_copy_status_to_buffer();

    /* Copy CBW transfer length to IDATA (big-endian to little-endian) */
    I_TRANSFER_6B = REG_USB_CBW_XFER_LEN_3;
    I_TRANSFER_6C = REG_USB_CBW_XFER_LEN_2;
    I_TRANSFER_6D = REG_USB_CBW_XFER_LEN_1;
    I_TRANSFER_6E = REG_USB_CBW_XFER_LEN_0;

    /* Extract direction and LUN */
    G_XFER_STATE_0AF3 = REG_USB_CBW_FLAGS & CBW_FLAGS_DIRECTION;
    G_XFER_LUN_0AF4 = REG_USB_CBW_LUN & CBW_LUN_MASK;

    /* Process command */
    scsi_cmd_process();
}

/*
 * scsi_cmd_process - Process SCSI command
 * Address: 0x4d92-0x4e6c (219 bytes)
 *
 * Main SCSI command processing function.
 */
static void scsi_cmd_process(void)
{
    /* Command processing logic */
    scsi_cmd_state_machine();
}

/*
 * scsi_cmd_state_machine - Command state machine
 * Address: 0x4c98-0x4d91 (250 bytes)
 *
 * State machine for SCSI command execution.
 */
static void scsi_cmd_state_machine(void)
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
 * scsi_check_link_status - Check link status
 * Address: 0x541f-0x5425 (7 bytes)
 *
 * Returns bits 0-1 of link status register.
 */
uint8_t scsi_check_link_status(void)
{
    return REG_LINK_STATUS_E716 & LINK_STATUS_E716_MASK;
}

/*
 * scsi_flash_ready_check - Check flash ready status
 * Address: 0x5305-0x5320 (28 bytes)
 *
 * Handles flash ready status checking.
 */
void scsi_flash_ready_check(void)
{
    uint8_t status1, status2, status3;

    scsi_cmd_clear();

    status1 = REG_FLASH_READY_STATUS;
    status2 = REG_FLASH_READY_STATUS;
    status3 = REG_FLASH_READY_STATUS;

    /* Note: handler_d6bc is a bank switch stub; actual params may be passed via globals */
    (void)status1; (void)status2; (void)status3;
    handler_d6bc();  /* was: dispatch_0534 */

    G_SYS_FLAGS_07F6 = 1;
}

/*
 * scsi_cmd_clear - Clear command state
 * Address: 0x4c40-0x4c97 (88 bytes)
 *
 * Clears SCSI command state and buffers.
 */
static void scsi_cmd_clear(void)
{
    /* Clear command state */
}

/*
 * scsi_dma_check_mask - Check DMA completion by mask
 * Address: 0x5373-0x5397 (37 bytes)
 *
 * Checks if transfer is complete based on mask.
 */
void scsi_dma_check_mask(uint8_t param)
{
    uint8_t status;
    static __code const uint8_t mask_table[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

    status = REG_SCSI_DMA_MASK;
    if ((status & mask_table[param]) != 0) {
        usb_shift_right_3(param);
        /* Additional processing */
    }
}

/*
 * scsi_queue_dispatch - Queue dispatch handler
 * Address: 0x52c7-0x5304 (62 bytes)
 *
 * Dispatches queue operations based on mask.
 */
void scsi_queue_dispatch(uint8_t param)
{
    uint8_t status;
    static __code const uint8_t mask_table[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};

    status = REG_SCSI_DMA_QUEUE;
    if ((status & mask_table[param]) != 0) {
        transfer_func_16b0(param);
        REG_SCSI_DMA_QUEUE = param + 2;
        REG_SCSI_DMA_QUEUE = param + 3;
    }
}

/*
 * pcie_setup_transaction - Setup PCIe transaction
 * Address: 0x1580
 */
static void pcie_setup_transaction(uint8_t param)
{
    (void)param;  /* Stub - actual implementation pending */
}

/* External functions for new implementations */
extern void usb_read_buf_addr_pair(void);
extern void usb_data_handler(void);
extern void usb_configure(void);
extern void nvme_get_config_offset(void);
extern void nvme_calc_addr_01xx(uint8_t param);
extern uint8_t helper_165e(uint8_t param);
extern void helper_1660(uint8_t param1, uint8_t param2);
extern void helper_1659(uint8_t param);
extern void helper_0412(uint8_t param);
extern void helper_1677(void);
extern void usb_calc_queue_addr(uint8_t param);
extern void usb_calc_queue_addr_next(uint8_t param);
extern uint8_t helper_313d(void);
extern void nvme_ep_config_init_3267(void);
extern void helper_3181(void);
extern void helper_3279(void);
extern void helper_1ce4(void);
extern uint8_t helper_1c22(void);
extern uint8_t helper_3bcd(uint8_t param);
extern void helper_523c(uint8_t r3, uint8_t r5, uint8_t r7);
extern void helper_544c(void);

/*
 * nvme_scsi_cmd_buffer_setup - Setup NVMe SCSI command buffer
 * Address: 0x4f37-0x4f76 (64 bytes)
 *
 * Transfers SCSI command parameters to NVMe SCSI translation registers.
 * Loads IDATA[0x12-0x15] dword to C4C0-C4C3, stores tag, clears length,
 * then calls data handler and restores.
 */
void nvme_scsi_cmd_buffer_setup(void)
{
    /* Load IDATA dword to NVMe SCSI command buffer registers */
    idata_load_dword(0x12);
    REG_NVME_SCSI_CMD_BUF_0 = IDATA_SCSI_CMD_BUF[0];
    REG_NVME_SCSI_CMD_BUF_1 = IDATA_SCSI_CMD_BUF[1];
    REG_NVME_SCSI_CMD_BUF_2 = IDATA_SCSI_CMD_BUF[2];
    REG_NVME_SCSI_CMD_BUF_3 = IDATA_SCSI_CMD_BUF[3];

    /* Store SCSI tag */
    REG_NVME_SCSI_TAG = I_SCSI_TAG;

    /* Read buffer address pair */
    usb_read_buf_addr_pair();

    /* Clear command length (R4=R5=0) */
    REG_NVME_SCSI_CMD_LEN_0 = 0;
    REG_NVME_SCSI_CMD_LEN_1 = 0;
    REG_NVME_SCSI_CMD_LEN_2 = 0;
    REG_NVME_SCSI_CMD_LEN_3 = 0;

    /* Clear control byte */
    REG_NVME_SCSI_CTRL = 0;

    /* Call data handler with DPTR at C4CA */
    usb_data_handler();

    /* Load result back from C4C0 to IDATA[0x12] */
    IDATA_SCSI_CMD_BUF[0] = REG_NVME_SCSI_CMD_BUF_0;
    IDATA_SCSI_CMD_BUF[1] = REG_NVME_SCSI_CMD_BUF_1;
    IDATA_SCSI_CMD_BUF[2] = REG_NVME_SCSI_CMD_BUF_2;
    IDATA_SCSI_CMD_BUF[3] = REG_NVME_SCSI_CMD_BUF_3;
    idata_store_dword(0x12);

    /* Read final result from tag register */
    /* R4=R5=R6=0, R7 = C4C8 value */
}

/*
 * scsi_read_slot_table - Read from SCSI slot table
 * Address: 0x5043-0x504e (12 bytes)
 *
 * Reads a byte from the slot status table at 0x0108 + offset.
 * Returns the value at that address.
 */
uint8_t scsi_read_slot_table(uint8_t offset)
{
    __xdata uint8_t *addr = (__xdata uint8_t *)(0x0108 + offset);
    return *addr;
}

/*
 * scsi_clear_slot_entry - Clear slot entry and setup pointer
 * Address: 0x502e-0x5042 (21 bytes)
 *
 * Stores 0xFF to slot table at 0x0100 + param1, then returns
 * DPTR setup to 0x0517 + R7 (for subsequent data access).
 */
void scsi_clear_slot_entry(uint8_t slot_offset, uint8_t data_offset)
{
    __xdata uint8_t *slot_addr = (__xdata uint8_t *)(0x0100 + slot_offset);

    /* Mark slot as free (0xFF) */
    *slot_addr = 0xFF;

    /* Setup pointer to data at 0x0517 + offset */
    /* Note: In original code this sets DPTR which is used by caller */
    (void)data_offset;  /* Used for DPTR calculation in caller */
}

/*
 * scsi_transfer_check - Check and process SCSI transfer status
 * Address: 0x4ddc-0x4e24 (73 bytes)
 *
 * Polls USB status register and processes transfer completion.
 * Handles register 0x9093 bit 1 and 0x9101 bit 1 for transfer state.
 */
void scsi_transfer_check(void)
{
    uint8_t status;

    /* Check initial condition */
    if (helper_313d() == 0) {
        return;
    }

    nvme_ep_config_init_3267();

    /* Poll for transfer completion */
    while (1) {
        status = REG_USB_EP_CFG1;  /* 0x9093 */
        if (status & 0x02) {
            /* Bit 1 set - process completion */
            REG_USB_EP_CFG1 = 0x02;

            /* Load IDATA dword from 0x6B */
            idata_load_dword(0x6B);

            /* Push R6, R7 */
            helper_3181();
            helper_3279();

            /* Calculate using helper */
            helper_0cab(0, 0, 0, 0);

            /* Store back to IDATA 0x6B */
            idata_store_dword(0x6B);
            return;
        }

        /* Check handler dispatch result */
        handler_039a_buffer_dispatch();
        /* Original code returns on certain condition - simplified */
        return;
    }
}

/*
 * scsi_dma_dispatch_helper - DMA dispatch helper
 * Address: 0x4abf-0x4b24 (102 bytes)
 *
 * Handles DMA dispatch with endpoint check and state management.
 */
uint8_t scsi_dma_dispatch_helper(void)
{
    uint8_t status;

    helper_1b2e(0);
    status = REG_XFER_READY;  /* Check some status via 0x1bd5 */
    I_WORK_3C = status & 0x01;

    /* Call DMA dispatch with param 0x22 */
    scsi_dma_dispatch(0x22);
    /* Check dispatch result via global status */
    if (G_DMA_STATE_0214 != 0) {
        return G_DMA_STATE_0214;
    }

    /* Check work flag */
    if (I_WORK_3C != 0) {
        helper_544c();
        helper_1b2e(0);  /* usb_write_byte_1bcb equivalent */
        return 5;
    }

    /* Check transfer active */
    if (G_TRANSFER_ACTIVE == 0) {
        /* Call 0x1c4a handler */
    }

    /* Setup loop through slots - copy from 0x0201 area to 0x8000 area */
    for (I_WORK_3B = 0; I_WORK_3B < 8; I_WORK_3B++) {
        __xdata uint8_t *slot_addr = (__xdata uint8_t *)(0x0201 + I_WORK_3B);
        __xdata uint8_t *dest_addr = (__xdata uint8_t *)(0x8000 + I_WORK_3B);
        *dest_addr = *slot_addr;
    }

    return 0;
}

/*
 * scsi_endpoint_queue_process - Process USB endpoint queue
 * Address: 0x4b8b-0x4be5 (91 bytes)
 *
 * Main endpoint queue processing function. Handles status updates
 * and calls CSW send when appropriate.
 */
void scsi_endpoint_queue_process(void)
{
    uint8_t status;
    uint8_t r6_val;

    /* Get primary system status */
    status = G_SYS_STATUS_PRIMARY;
    helper_165e(status);
    I_WORK_53 = G_SYS_STATUS_PRIMARY;

    /* Calculate next index: (status + 1) & 0x03 */
    r6_val = (I_WORK_53 + 1) & 0x03;

    /* Setup endpoint parameters */
    helper_1660(status + 0x4E, r6_val);
    helper_1659(r6_val);
    helper_0412(G_SYS_STATUS_PRIMARY);

    /* Check if status is 0 */
    if (G_SYS_STATUS_PRIMARY == 0) {
        helper_1677();
        G_SYS_STATUS_PRIMARY = 0;
    }

    /* Main processing loop */
    while (1) {
        uint8_t csw_param;

        status = G_SYS_STATUS_PRIMARY;
        csw_param = (status != 0) ? 4 : 1;

        scsi_csw_send(0, csw_param);

        if (status != 0) {
            break;
        }

        /* Check primary status again */
        if (G_SYS_STATUS_PRIMARY != 0) {
            continue;
        }

        helper_1677();
        if (G_SYS_STATUS_PRIMARY != 0) {
            usb_calc_queue_addr(I_WORK_53);
            usb_calc_queue_addr_next(I_WORK_53);
            return;
        }
    }
}

/*
 * scsi_state_handler - State-based command handler
 * Address: 0x4d44-0x4d91 (78 bytes)
 *
 * Dispatches handling based on I_STATE_6A value.
 * Handles states 1, 8, and default.
 */
void scsi_state_handler(void)
{
    uint8_t state;
    uint8_t usb_status;

    state = I_STATE_6A;

    /* State 1: Call 0x4013 - setup transfer */
    if (state == 1) {
        scsi_setup_transfer_result(0);
        /* Original code checks R7 result, but we just return */
        return;
    }

    /* State 8 (0x08): Check I/O command state */
    if (state == 0x08) {
        /* Check G_IO_CMD_STATE (0x0001) */
        if (G_IO_CMD_STATE == 0) {
            /* Fall through to USB check */
        } else if (G_IO_CMD_STATE == 3) {
            /* Call 0x3130 */
        }
    }

    /* Check USB status bit 0 */
    usb_status = REG_USB_STATUS;
    if (usb_status & 0x01) {
        queue_idx_get_3291();
        handler_039a_buffer_dispatch();  /* via 0x0206 */
    } else {
        nvme_call_and_signal_3219();
    }

    I_STATE_6A = 5;
}

/*
 * scsi_queue_scan_handler - Scan and process queue entries
 * Address: 0x4ef5-0x4f36 (66 bytes)
 *
 * Scans through the queue looking for matching entries and
 * processes them.
 */
void scsi_queue_scan_handler(void)
{
    uint8_t idx;
    uint8_t limit;

    /* Check initial condition */
    if (helper_1c22()) {
        return;
    }

    I_WORK_23 = 0;

    while (1) {
        limit = G_NVME_STATE_053B;

        /* Check if we've scanned all entries */
        if (I_WORK_23 >= limit) {
            return;
        }

        /* Get entry at current index via 0x1ce4 */
        helper_1ce4();
        I_WORK_22 = G_USB_INDEX_COUNTER;  /* Read slot value */

        /* Check if entry matches current USB index */
        if (G_USB_INDEX_COUNTER == I_WORK_22) {
            /* Clear entry via 0x1ce4 */
            helper_1ce4();

            /* Decrement SCSI control counter */
            G_SCSI_CTRL--;

            /* Check G_USB_INIT_0B01 and dispatch */
            if (G_USB_INIT_0B01 != 0) {
                /* Call 0x4eb3 - NVMe command handler */
            } else {
                /* Call 0x46f8 */
            }
            return;
        }

        I_WORK_23++;
    }
}

/*
 * scsi_core_process - Core SCSI data handler
 * Address: 0x5008-0x502d (38 bytes)
 *
 * Handles core SCSI data path operations.
 */
void scsi_core_process(void)
{
    uint8_t val;

    /* Decrement R3 (implicit in calling convention) */
    helper_1b2e(0);  /* usb_read_buffer */

    /* Add 0x11 and call 0x1bc3 */
    /* This reads/writes some USB buffer data */

    /* Load from 0xC4xx and process */
    xdata_load_dword();  /* 0x0d84 */

    /* Setup R0=0x0E, read buffer 0x1b20 */
    /* Add 0x15 and call 0x1b14 */
    /* Add 0x1B and call 0x1bc3 */

    /* Read result and store to IDATA */
    val = REG_NVME_SCSI_CMD_BUF_0;  /* Example register read */
    I_CORE_STATE_L = val;
    I_CORE_STATE_H = REG_NVME_SCSI_CMD_BUF_1;
}

/*
 * scsi_transfer_start_alt - Alternative transfer start handler
 * Address: 0x50a2-0x50da (57 bytes)
 *
 * Handles transfer start with flag checking and DMA setup.
 */
uint8_t scsi_transfer_start_alt(void)
{
    uint8_t status;

    helper_3f4a();
    I_WORK_3B = 0;  /* R7 parameter */

    if (I_WORK_3B != 0) {
        return 0;
    }

    /* Call 0x1b23, 0x1bd5 */
    helper_1b2e(0);
    status = REG_XFER_READY;
    I_WORK_3C = status & 0x02;

    if (I_WORK_3C != 0) {
        helper_544c();
    } else {
        /* Check G_XFER_FLAG_07EA */
        if (G_XFER_FLAG_07EA == 1) {
            /* Call 0x3bcd with R7=0 */
            if (helper_3bcd(0) != 0) {
                /* Call 0x523c with R3=0, R5=0x44, R7=4 */
                helper_523c(0, 0x44, 4);
            }
        }
    }

    /* Call 0x1bcb and return 5 */
    return 5;
}

/* External helper declarations */
extern void helper_3f4a(void);
extern void helper_3147(void);

/*
 * scsi_transfer_check_5069 - Transfer check and setup handler
 * Address: 0x5069-0x50a1 (57 bytes)
 *
 * Clears transfer control, calls check function, and optionally
 * sets up DMA parameters based on state.
 */
uint8_t scsi_transfer_check_5069(uint8_t param)
{
    /* Clear transfer control flag */
    G_XFER_CTRL_0AF7 = 0;

    /* Call check function */
    helper_3f4a();
    I_WORK_3B = param;  /* R7 from caller */

    if (I_WORK_3B != 0) {
        /* If transfer active, set flag */
        if (G_TRANSFER_ACTIVE != 0) {
            G_XFER_CTRL_0AF7 = 1;
        }
        return I_WORK_3B;
    }

    /* Check log counter state */
    if (G_LOG_COUNTER_044B == 1) {
        if (G_WORK_0006 != 0) {
            /* Setup DMA: R3=0, R5=0x3A, R7=2 */
            helper_523c(0, 0x3A, 2);
        }
    }

    /* Final cleanup call */
    helper_1b2e(0);  /* 0x1bcb equivalent */
    return 5;
}

/*
 * scsi_tag_setup_50ff - Setup SCSI tag entry
 * Address: 0x50ff-0x5111 (19 bytes)
 *
 * Writes tag data to offset 0x2F + R6, checks queue index
 * and conditionally updates it.
 */
void scsi_tag_setup_50ff(uint8_t tag_offset, uint8_t tag_value)
{
    /* Calculate address and call helper to setup tag */
    /* Original: mov a, #0x2f; add a, r6; lcall 0x325f */
    /* Store R5 (tag_value) to calculated address */

    /* Check if current queue index matches R7 */
    if (I_QUEUE_IDX == tag_offset) {
        /* Update queue index with R6 value */
        I_QUEUE_IDX = tag_value;
    }
}

/*
 * scsi_nvme_completion_read - Read NVMe completion data
 * Address: 0x5112-0x5144 (51 bytes)
 *
 * Reads NVMe completion queue registers (0x9123-0x9128) and
 * stores data to IDATA transfer buffer and global state.
 */
void scsi_nvme_completion_read(void)
{
    /* Call status copy function */
    helper_3147();

    /* Read NVMe completion data from 0x9123-0x9128 */
    /* Store to IDATA[0x6B-0x6E] in reverse order */
    I_TRANSFER_6B = REG_USB_CBW_XFER_LEN_3;     /* 0x9126 */
    I_TRANSFER_6C = REG_USB_CBW_XFER_LEN_2;     /* 0x9125 */
    I_TRANSFER_6D = REG_USB_CBW_XFER_LEN_1;     /* 0x9124 */
    I_TRANSFER_6E = REG_USB_CBW_XFER_LEN_0;     /* 0x9123 */

    /* Extract direction flag (bit 7 of 0x9127) */
    G_XFER_STATE_0AF3 = REG_USB_CBW_FLAGS & CBW_FLAGS_DIRECTION;  /* 0x9127 */

    /* Extract LUN (lower 4 bits of 0x9128) */
    G_XFER_LUN_0AF4 = REG_USB_CBW_LUN & CBW_LUN_MASK;   /* 0x9128 */

    /* Jump to state handler */
    /* Original: ljmp 0x4d92 - we just return and let caller handle */
}

/*
 * scsi_uart_print_hex - Print byte as hex to UART
 * Address: 0x51c7-0x51e5 (31 bytes)
 *
 * Debug function: outputs a byte value as two hex digits to UART.
 */
void scsi_uart_print_hex(uint8_t value)
{
    uint8_t hi_nibble = value >> 4;
    uint8_t lo_nibble = value & 0x0F;
    uint8_t base;

    /* Output high nibble */
    base = (hi_nibble < 10) ? '0' : ('A' - 10);
    REG_UART_THR_RBR = base + hi_nibble;

    /* Output low nibble */
    base = (lo_nibble < 10) ? '0' : ('A' - 10);
    REG_UART_THR_RBR = base + lo_nibble;
}

/*
 * scsi_uart_print_digit - Print single digit to UART
 * Address: 0x51e6-0x51ee (9 bytes)
 *
 * Debug function: outputs a digit (0-9) to UART.
 */
void scsi_uart_print_digit(uint8_t digit)
{
    REG_UART_THR_RBR = '0' + digit;
}

/*
 * scsi_decrement_pending - Decrement pending counter
 * Address: 0x53a7-0x53bf (25 bytes)
 *
 * Decrements the endpoint check counter if > 0, otherwise
 * clears it and calls cleanup handler.
 */
void scsi_decrement_pending(void)
{
    /* Call setup function first */
    /* helper_50db(); - 0x50db */

    /* Check G_EP_CHECK_FLAG (0x000A) */
    if (G_EP_CHECK_FLAG > 1) {
        /* Decrement counter */
        G_EP_CHECK_FLAG--;
    } else {
        /* Counter reached 0 or 1, clear and call cleanup */
        G_EP_CHECK_FLAG = 0;
        /* Call cleanup handler at 0x5409 */
    }
}

/*
 * scsi_state_dispatch_52b1 - State dispatch handler continuation
 * Address: 0x52b1-0x52c6 (22 bytes)
 *
 * Part of state machine - stores mode and optionally calls DMA setup.
 */
void scsi_state_dispatch_52b1(void)
{
    /* Store mode value 2 to current DPTR */
    /* Check G_EP_STATUS_CTRL (0x0003) */
    if (G_EP_STATUS_CTRL != 0) {
        /* Call DMA mode setup */
        scsi_dma_mode_setup();
    }
    /* Jump to cleanup at 0x5409 */
}

/*
 * scsi_queue_check_52c7 - Queue status check with mask
 * Address: 0x52c7-0x52e5 (31 bytes)
 *
 * Checks queue status using lookup table and processes if bit set.
 * Returns 1 if processed, 0 otherwise.
 */
uint8_t scsi_queue_check_52c7(uint8_t index)
{
    uint8_t mask;
    uint8_t status;

    /* Lookup mask from code table at 0x5B7A */
    /* Original: movc a, @a+dptr with dptr=0x5b7a */
    static __code const uint8_t mask_table_5b7a[] = {
        0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
    };
    mask = (index < 8) ? mask_table_5b7a[index] : 0;

    /* Read status from CE5F and check against mask */
    status = REG_SCSI_DMA_QUEUE;  /* 0xCE5F approximation */
    if ((status & mask) != 0) {
        /* Call transfer helper */
        transfer_func_16b0(index);
        /* Store to DPTR: index+2, index+3 */
        return 1;
    }

    return 0;
}

/*
 * scsi_slot_config_46f8 - Slot configuration handler
 * Address: 0x46f8-0x4783 (140 bytes)
 *
 * Configures slot based on endpoint dispatch values.
 * Sets up transfer parameters and calls various helpers.
 */
extern void helper_1b77(void);
extern uint8_t helper_1c1b(void);
extern void helper_1ba5(void);
extern void helper_1c6d(void);
extern void helper_1cc1(void);
extern void helper_1b47(void);
extern void helper_1c77(void);
extern void helper_1d39(uint8_t param);
extern void helper_1d43(void);
extern void helper_1aad(uint8_t param);
extern void helper_1cae(void);
extern void helper_1c56(void);
extern void helper_041c(uint8_t param);

void scsi_slot_config_46f8(uint8_t r7_val, uint8_t r5_val)
{
    uint8_t mode;
    uint8_t tmp;

    /* Store parameters to endpoint dispatch values */
    G_EP_DISPATCH_VAL3 = r7_val;  /* 0x0A7D */
    G_EP_DISPATCH_VAL4 = r5_val;  /* 0x0A7E */

    /* Call helper sequence */
    helper_1b77();
    if (helper_1c1b()) {
        /* Carry set - setup additional parameters */
        helper_1ba5();
        /* Store values via indirect addressing @R0 */
        I_WORK_18 = G_WORK_0006;
        I_WORK_19 = G_WORK_0007;
        helper_1c6d();
    }
    helper_1cc1();

    /* Store endpoint dispatch value to queue status */
    G_EP_QUEUE_STATUS = G_EP_DISPATCH_VAL3;  /* 0x0565 = 0x0A7D */

    /* Set mode based on dispatch value 4 */
    mode = 0x02;
    if (G_EP_DISPATCH_VAL4 == 0x01) {
        mode = 0x01;
    }
    helper_1aad(mode);
    helper_1d43();

    /* Clear bit 7 of REG_NVME_DATA_CTRL (0xC414) */
    REG_NVME_DATA_CTRL &= ~NVME_DATA_CTRL_BIT7;

    /* Check SCSI control */
    tmp = G_SCSI_CTRL;  /* 0x0171 */
    if (tmp > 0) {
        /* Get DMA work value and add to dispatch value */
        tmp = G_DMA_WORK_0216 + G_EP_DISPATCH_VAL3;
        helper_1c56();
        G_EP_DISPATCH_VAL3 |= tmp;
    } else {
        helper_1b47();
    }

    /* Continue with address calculation */
    tmp = G_EP_DISPATCH_VAL3;
    helper_1c77();
    G_EP_DISPATCH_VAL3 |= tmp;

    /* Check SCSI control again for NVMe setup */
    if (G_SCSI_CTRL > 0) {
        helper_1d39(G_DMA_WORK_0216);
        nvme_scsi_cmd_buffer_setup();
    }

    helper_1cae();

    /* Final check for dispatch value 4 */
    if (G_EP_DISPATCH_VAL4 == 0) {
        helper_041c(G_USB_PARAM_0B00);
    }
}

/*
 * scsi_state_switch_4784 - State-based command switch
 * Address: 0x4784-0x480b (136 bytes)
 *
 * Handles state machine transitions based on I_STATE_6A value.
 * Processes different command types (0x03, 0x04, 0x05).
 */
extern void helper_3133(void);
extern void helper_3130(void);
extern void helper_312a(void);
extern void helper_31ce(void);
extern void helper_0206(void);

void scsi_state_switch_4784(void)
{
    uint8_t state;
    uint8_t cmd_type;

    state = I_STATE_6A;

    /* Check state value (add 0xFD = check if state == 3) */
    if (state == 0x03) {
        /* State 3: Check command type */
        cmd_type = G_IO_CMD_TYPE;
        if (cmd_type == 0x03 || cmd_type == 0x00) {
            scsi_csw_build();
            return;
        }
    } else if (state == 0x04) {
        /* State 4: Check for command types 0x07 or 0x00 */
        cmd_type = G_IO_CMD_TYPE;
        if (cmd_type == 0x07 || cmd_type == 0x00) {
            scsi_csw_build();
            return;
        }
    } else if (state == 0x05) {
        /* State 5: Multiple checks */
        cmd_type = G_IO_CMD_TYPE;
        if (cmd_type == 0x03 || cmd_type == 0x00) {
            if (cmd_type == 0x03) {
                if ((REG_USB_STATUS & USB_STATUS_ACTIVE) == 0) {
                    helper_3130();
                }
            }
            goto check_usb_status;
        }
    }

    /* Default path - call protocol handlers */
    helper_312a();
    helper_31ce();

check_usb_status:
    /* Check USB control register bit 0 */
    if (REG_USB_STATUS & USB_STATUS_ACTIVE) {
        queue_idx_get_3291();
        helper_0206();
    } else {
        nvme_call_and_signal_3219();
    }

    /* Set state to 5 */
    I_STATE_6A = 0x05;
}

/*
 * scsi_csw_build_ext_488f - Extended CSW build handler
 * Address: 0x488f-0x4903 (117 bytes)
 *
 * Extended CSW building with additional status checks.
 */
extern void handler_2608(void);  /* dma.c - 0x2608 */

void scsi_csw_build_ext_488f(void)
{
    uint8_t tmp;

    /* Check REG_NVME_LINK_STATUS (0xC520) bit 1 */
    if (REG_NVME_LINK_STATUS & NVME_LINK_STATUS_BIT1) {
        scsi_csw_build_ext_488f();  /* Recursive call at 0x488f */
    }

    /* Check REG_NVME_LINK_STATUS bit 7 */
    if (REG_NVME_LINK_STATUS & NVME_LINK_STATUS_BIT7) {
        tmp = REG_NVME_LINK_STATUS;
        REG_NVME_LINK_STATUS = tmp | 0x20;
    }

    /* Check for error condition */
    if (G_NVME_QUEUE_READY != 0) {
        return;
    }

    /* Check REG_CPU_LINK_CEF3 bit 3 */
    if (REG_CPU_LINK_CEF3 & CPU_LINK_CEF3_ACTIVE) {
        REG_CPU_LINK_CEF3 = 0x08;  /* Clear bit by writing 1 */
        handler_2608();
    }

    /* Check bit 1 again */
    if (REG_NVME_LINK_STATUS & NVME_LINK_STATUS_BIT1) {
        scsi_csw_build_ext_488f();
    }

    /* Check bit 7 */
    if (REG_NVME_LINK_STATUS & NVME_LINK_STATUS_BIT7) {
        tmp = REG_NVME_LINK_STATUS;
        REG_NVME_LINK_STATUS = tmp | 0x20;
    }
}

/*
 * scsi_nvme_setup_49e9 - NVMe command setup
 * Address: 0x49e9-0x4a56 (110 bytes)
 *
 * Sets up NVMe command from SCSI parameters.
 */
extern void helper_1bd7(uint8_t param);
extern void helper_15a0(void);

void scsi_nvme_setup_49e9(uint8_t param)
{
    uint8_t status;
    uint8_t val;
    (void)param;

    /* Get system status and check */
    status = G_SYS_STATUS_PRIMARY;

    /* Setup base addresses based on status */
    if (status == 0x01) {
        G_BUF_BASE_HI = 0xA8;
    } else {
        G_BUF_BASE_HI = 0xA0;
    }
    G_BUF_BASE_LO = 0x00;

    /* Call helper for status secondary */
    helper_1bd7(G_SYS_STATUS_SECONDARY);

    /* Store DMA work value */
    G_DMA_WORK_0216 = G_SYS_STATUS_SECONDARY;

    /* Calculate buffer address */
    helper_15a0();
    G_BUF_ADDR_HI = G_EP_CONFIG_BASE;
    G_BUF_ADDR_LO = G_EP_CONFIG_ARRAY;

    /* Get offset value */
    val = helper_1b0b(G_SYS_STATUS_SECONDARY);
    G_DMA_OFFSET = val;
}

/*
 * scsi_dma_config_4a57 - DMA configuration handler
 * Address: 0x4a57-0x4abe (104 bytes)
 *
 * Configures DMA based on transfer parameters.
 */
void scsi_dma_config_4a57(void)
{
    uint8_t val;

    /* Check REG_XFER_CTRL_CE8A */
    val = REG_XFER_CTRL_CE8A;
    REG_XFER_CTRL_CE8A = val & 0xFB;  /* Clear bit 2 */

    /* Get USB parameter and combine with CE01 */
    val = G_USB_PARAM_0B00;
    REG_SCSI_DMA_PARAM = val | (REG_SCSI_DMA_PARAM & 0xC0);

    /* Set up CE3A from NVMe param */
    val = G_NVME_PARAM_053A;
    REG_SCSI_DMA_TAG_CE3A = val;

    /* Write command and wait for completion */
    REG_SCSI_DMA_CTRL = 0x03;
    while (REG_SCSI_DMA_CTRL != 0) {
        /* Wait for command completion */
    }

    /* Increment circular counter */
    nvme_util_advance_queue();

    /* Check result and call appropriate handler */
    if (G_SYS_STATUS_PRIMARY == 0) {
        power_check_status(0);
    }

    /* Get EP config and check */
    usb_get_sys_status_offset();
    val = G_SYS_STATUS_SECONDARY;
    if (val == 0) {
        /* Add to global NVMe param */
        G_NVME_PARAM_053A++;
    }
}

/*
 * scsi_queue_setup_4b25 - Queue setup handler
 * Address: 0x4b25-0x4b5e (58 bytes)
 *
 * Sets up queue with DMA parameters.
 */
extern void usb_configure(void);
extern void nvme_calc_addr_01xx(uint8_t param);
extern void nvme_get_config_offset(void);

void scsi_queue_setup_4b25(uint8_t param)
{
    uint8_t val;
    uint8_t status;

    /* Get slot index and mask */
    val = REG_NVME_QUEUE_STATUS;  /* 0xC51E */
    I_WORK_3B = val & 0x3F;

    /* Update CE88 with slot value */
    val = REG_XFER_CTRL_CE88;
    REG_XFER_CTRL_CE88 = (val & 0xC0) | I_WORK_3B;

    /* Wait for CE89 bit 0 */
    while ((REG_XFER_READY & 0x01) == 0) {
        /* Wait */
    }

    /* Get system status */
    status = G_SYS_STATUS_PRIMARY;
    helper_1b0b(0);
    I_WORK_3C = G_SYS_STATUS_PRIMARY;

    /* Calculate address offset */
    nvme_calc_addr_01xx(param + 0x94);

    /* Set CE3A with param */
    val = REG_SCSI_DMA_TAG_CE3A;
    REG_SCSI_DMA_TAG_CE3A = val | param;

    /* Set CE01 based on status */
    val = 0x00;
    if (status == 0x01) {
        val = 0x40;
    }
    REG_SCSI_DMA_PARAM = I_WORK_3C | val;

    /* Configure and get offset */
    usb_configure();
    I_WORK_3C = (I_WORK_3C + 1) & 0x1F;
    power_check_status(I_WORK_3C);
    nvme_get_config_offset();
    REG_SCSI_DMA_CTRL = I_WORK_3C;
}

/*
 * scsi_dma_init_4be6 - DMA initialization
 * Address: 0x4be6-0x4c3f (90 bytes)
 *
 * Initializes DMA registers and control bits.
 */
extern void reg_set_bit_0(uint16_t addr);
extern void helper_03a4(void);
extern void helper_0584(void);
extern void helper_048f(void);

void scsi_dma_init_4be6(void)
{
    uint8_t val;

    /* Set up fixed values at 0x07F0-0x07F5 */
    G_SYS_FLAGS_07F0 = 0x24;
    G_SYS_FLAGS_07F1 = 0x04;
    G_SYS_FLAGS_07F2 = 0x17;
    G_SYS_FLAGS_07F3 = 0x85;
    G_SYS_FLAGS_07F4 = 0x00;
    G_SYS_FLAGS_07F5 = 0x00;

    /* Clear bit 0 of CC35 */
    val = REG_CPU_EXEC_STATUS_3;  /* 0xCC35 */
    REG_CPU_EXEC_STATUS_3 = val & 0xFE;

    /* Modify C801 */
    val = REG_INT_ENABLE;  /* 0xC801 */
    REG_INT_ENABLE = (val & 0xEF) | 0x10;

    /* Modify C800 */
    val = REG_INT_STATUS_C800;
    REG_INT_STATUS_C800 = (val & 0xFB) | 0x04;

    /* Modify CA60 */
    val = REG_CPU_CTRL_CA60;
    REG_CPU_CTRL_CA60 = (val & 0xF8) | 0x06;
    val = REG_CPU_CTRL_CA60;
    REG_CPU_CTRL_CA60 = (val & 0xF7) | 0x08;

    /* Set bit 0 of C800 */
    reg_set_bit_0(0xC800);

    /* Set bit 0 and modify CC3B */
    reg_set_bit_0(0xCC3B);
    val = REG_TIMER_CTRL_CC3B;  /* 0xCC3B */
    REG_TIMER_CTRL_CC3B = (val & 0xFD) | 0x02;

    /* Call initialization helpers */
    helper_03a4();
    helper_0584();
    helper_048f();
}

/*
 * scsi_buffer_setup_4e25 - Buffer setup handler
 * Address: 0x4e25-0x4e6c (72 bytes)
 *
 * Sets up buffer addresses for transfer.
 */
void scsi_buffer_setup_4e25(void)
{
    uint8_t status;
    uint8_t val;

    status = G_SYS_STATUS_PRIMARY;

    /* Set buffer base address based on status */
    if (status == 0x01) {
        G_BUF_BASE_HI = 0xA8;
    } else {
        G_BUF_BASE_HI = 0xA0;
    }
    G_BUF_BASE_LO = 0x00;

    /* Get EP config */
    val = G_SYS_STATUS_SECONDARY;
    helper_1bd7(val);
    G_DMA_WORK_0216 = val;

    /* Calculate buffer address using multiply-add */
    helper_15a0();
    G_BUF_ADDR_HI = G_EP_CONFIG_BASE;
    G_BUF_ADDR_LO = G_EP_CONFIG_ARRAY;

    /* Get DMA offset */
    val = helper_1b0b(G_SYS_STATUS_SECONDARY);
    G_DMA_OFFSET = val;
}

/*
 * scsi_ep_config_4e6d - Endpoint configuration
 * Address: 0x4e6d-0x4ef4 (136 bytes)
 *
 * Configures endpoint for SCSI transfer.
 */
extern uint8_t ep_config_read(uint8_t param);
extern void mul_add_index(uint8_t param1, uint8_t param2);
extern uint8_t helper_1b9d(uint8_t p1, uint16_t p2);

void scsi_ep_config_4e6d(void)
{
    uint8_t status;
    uint8_t val;

    status = G_SYS_STATUS_PRIMARY;

    /* Set buffer base based on status */
    if (status == 0x01) {
        G_BUF_BASE_HI = 0xA8;
    } else {
        G_BUF_BASE_HI = 0xA0;
    }
    G_BUF_BASE_LO = 0x00;

    /* Read endpoint config */
    val = G_SYS_STATUS_SECONDARY;
    val = ep_config_read(val);
    G_DMA_WORK_0216 = val;

    /* Calculate address with multiply-add */
    mul_add_index(G_SYS_STATUS_SECONDARY, 0x14);
    G_BUF_ADDR_HI = G_EP_CONFIG_BASE;
    G_BUF_ADDR_LO = G_EP_CONFIG_ARRAY;

    /* Get offset from helper */
    val = helper_1b9d(G_SYS_STATUS_SECONDARY, 0x054F);
    G_DMA_OFFSET = val;
}

/*
 * scsi_transfer_helper_4f77 - Transfer helper function
 * Address: 0x4f77-0x4fb5 (63 bytes)
 *
 * Handles transfer with parameter checking.
 */
void scsi_transfer_helper_4f77(uint8_t param)
{
    uint8_t val;

    /* Clear bit 2 of CE8A */
    val = REG_XFER_CTRL_CE8A;
    REG_XFER_CTRL_CE8A = val & 0xFB;

    /* Combine USB param with CE01 */
    val = G_USB_PARAM_0B00;
    REG_SCSI_DMA_PARAM = val | (REG_SCSI_DMA_PARAM & 0xC0);

    /* Set CE3A */
    val = G_NVME_PARAM_053A;
    REG_SCSI_DMA_TAG_CE3A = val | param;

    /* Start command */
    REG_SCSI_DMA_CTRL = 0x03;
    while (REG_SCSI_DMA_CTRL != 0) {
        /* Wait */
    }

    /* Advance queue */
    nvme_util_advance_queue();

    /* Check status and handle */
    if (G_SYS_STATUS_PRIMARY == 0) {
        power_check_status(0);
    }

    /* Get EP config */
    usb_get_sys_status_offset();
    if (G_SYS_STATUS_SECONDARY == 0) {
        G_NVME_PARAM_053A++;
    }
}

/*
 * scsi_queue_handler_4fb6 - Queue processing handler
 * Address: 0x4fb6-0x4ff1 (60 bytes)
 *
 * Processes queue with DMA initialization.
 */
void scsi_queue_handler_4fb6(void)
{
    /* Initialize DMA */
    scsi_dma_init_4be6();
}

/*
 * scsi_core_handler_4ff2 - Core command handler
 * Address: 0x4ff2-0x5007 (22 bytes)
 *
 * Core handler that dispatches to NVMe functions.
 */
void scsi_core_handler_4ff2(uint8_t flag, uint8_t param)
{
    (void)param;
    if (flag) {
        scsi_ep_config_4e6d();
    }
}

/*
 * scsi_addr_calc_5038 - Calculate XDATA address 0x0517 + offset
 * Address: 0x5038-0x5042 (11 bytes)
 *
 * Sets up DPTR to point to XDATA address 0x0517 + param
 * Returns the high byte of the address (0x05 + carry)
 */
uint8_t scsi_addr_calc_5038(uint8_t param)
{
    /* mov a, #0x17; add a, r7; mov dpl, a; clr a; addc a, #0x05; mov dph, a */
    uint16_t addr = 0x0517 + param;
    (void)addr;  /* Address would be used by caller via DPTR */
    return (uint8_t)(addr >> 8);
}

/*
 * scsi_xdata_read_5043 - Read from XDATA[0x0108 + offset]
 * Address: 0x5043-0x504e (12 bytes)
 *
 * Reads a byte from XDATA address 0x0108 + param (with carry)
 */
uint8_t scsi_xdata_read_5043(uint8_t param)
{
    /* mov a, #0x08; add a, r7 -> falls through to 5046 which reads @dptr */
    __xdata uint8_t *ptr = (__xdata uint8_t *)(0x0108 + param);
    return *ptr;
}

/*
 * scsi_xdata_read_5046 - Read from calculated XDATA address
 * Address: 0x5046-0x504e (9 bytes)
 *
 * Reads from XDATA[0x0100 | low_byte]
 */
uint8_t scsi_xdata_read_5046(uint8_t low_addr)
{
    __xdata uint8_t *ptr = (__xdata uint8_t *)(0x0100 | low_addr);
    return *ptr;
}

/*
 * scsi_xdata_setup_504f - Setup DPTR from XDATA[0x0a84] + 0x0c
 * Address: 0x504f-0x505c (14 bytes)
 *
 * Reads XDATA[0x0a84], adds 0x0c, returns whether overflow occurred
 */
uint8_t scsi_xdata_setup_504f(void)
{
    /* mov dptr, #0x0a84; movx a, @dptr; add a, #0x0c */
    uint8_t val = XDATA8(0x0a84);
    uint8_t sum = val + 0x0c;
    /* Returns 0xFF if overflow, 0x00 if no overflow */
    return (sum < val) ? 0xFF : 0x00;
}

/*
 * scsi_addr_adjust_5058 - Adjust address with carry
 * Address: 0x5058-0x505c (5 bytes)
 *
 * Subtracts carry from param
 */
uint8_t scsi_addr_adjust_5058(uint8_t param, uint8_t carry)
{
    return param - carry;
}

/*
 * scsi_xdata_read_505d - Read from XDATA[param + 0xc2]
 * Address: 0x505d-0x5068 (12 bytes)
 *
 * Reads from low XDATA address param + 0xc2
 */
uint8_t scsi_xdata_read_505d(uint8_t param)
{
    /* add a, #0xc2 -> falls through to read */
    __xdata uint8_t *ptr = (__xdata uint8_t *)(param + 0xc2);
    if ((uint16_t)(param + 0xc2) >= 0x100) {
        /* With carry, use 0x01XX */
        ptr = (__xdata uint8_t *)(0x0100 | ((param + 0xc2) & 0xFF));
    }
    return *ptr;
}

/*
 * scsi_xdata_read_5061 - Read from low XDATA with carry-based high byte
 * Address: 0x5061-0x5068 (8 bytes)
 */
uint8_t scsi_xdata_read_5061(uint8_t low_addr, uint8_t carry)
{
    __xdata uint8_t *ptr;
    if (carry) {
        ptr = (__xdata uint8_t *)(0xFF00 | low_addr);
    } else {
        ptr = (__xdata uint8_t *)low_addr;
    }
    return *ptr;
}

/*
 * scsi_usbc_signature_check_51f9 - Check for "USBC" signature at DPTR
 * Address: 0x51f9-0x5215 (29 bytes)
 *
 * Checks if current DPTR points to USB CBW signature "USBC" (0x55534243)
 * Returns 1 if signature matches, 0 otherwise
 */
uint8_t scsi_usbc_signature_check_51f9(__xdata uint8_t *ptr)
{
    /* Check for USBC signature at ptr */
    /* Byte 0 should be 0 (already checked by caller) */
    if (ptr[0] != 0) return 0;

    /* Byte 1 should be 0x55 ('U') */
    if (ptr[1] != 0x55) return 0;

    /* Byte 2 should be 0x53 ('S') */
    if (ptr[2] != 0x53) return 0;

    /* Byte 3 should be 0x42 ('B') */
    if (ptr[3] != 0x42) return 0;

    /* Byte 4 should be 0x43 ('C') */
    if (ptr[4] != 0x43) return 0;

    return 1;
}

/*
 * scsi_reg_write_5398 - Write value to register C001
 * Address: 0x5398-0x53a3 (12 bytes)
 *
 * Part of a loop that writes R7 to REG_C001 and increments R1:R2
 */
void scsi_reg_write_5398(uint8_t val)
{
    REG_UART_THR = val;
}

/*
 * scsi_init_slot_53d4 - Initialize slot register
 * Address: 0x53d4-0x53e5 (18 bytes)
 *
 * Writes 0xFF to REG_C51A, increments G_012B and masks with 0x1F
 */
void scsi_init_slot_53d4(void)
{
    uint8_t val;

    /* Write 0xFF to REG_C51A */
    REG_NVME_LINK_STATUS = 0xFF;

    /* Read, increment, mask and write back */
    val = XDATA8(0x012B);
    val = (val + 1) & 0x1F;
    XDATA8(0x012B) = val;

    /* Jump to dispatch_041c - represented as call */
    dispatch_041c(val);
}

/*
 * scsi_dispatch_5426 - Dispatch handler
 * Address: 0x5426-0x5454 (47 bytes)
 *
 * Calls dispatch_0426 with param 0x14, then helper_173b with DPTR=0xB220
 */
void scsi_dispatch_5426(void)
{
    dispatch_0426();
    transfer_func_173b();
    dispatch_0453();
}

/*
 * scsi_helper_5455 - Call protocol helpers
 * Address: 0x5455-0x545b (7 bytes)
 *
 * Calls helper_312a and helper_31ce in sequence
 */
void scsi_helper_5455(void)
{
    helper_312a();
    helper_31ce();
}

/*
 * scsi_clear_mode_545c - Clear transfer mode flag
 * Address: 0x545c-0x5461 (6 bytes)
 *
 * Clears XDATA[0x0af8] to 0
 */
void scsi_clear_mode_545c(void)
{
    G_POWER_INIT_FLAG = 0;  /* Clear XDATA[0x0af8] */
}

/*
 * scsi_helper_5462 - Call helper 1cf0
 * Address: 0x5462-0x5465 (4 bytes)
 *
 * Simple wrapper for helper_1cf0
 */
void scsi_helper_5462(void)
{
    helper_1cf0();
}

/*
 * scsi_loop_process_573b - Loop-based data processing
 * Address: 0x573b-0x5764 (42 bytes)
 *
 * Complex loop function with djnz instructions. Called from jump tables.
 * Ghidra shows complex parameter list but decompilation is unreliable.
 * TODO: Needs detailed reverse engineering if called directly.
 */
void scsi_loop_process_573b(void)
{
    /* Stub - complex loop function, needs detailed RE */
}

/*
 * Note: The following addresses are DATA tables, not code:
 * - 0x5157-0x519d: Jump/data table (NOPs and control flow)
 * - 0x53a4: Part of loop at 0x538d (not standalone function)
 * - 0x54fc-0x5621: Data table/jump table
 * - 0x5622-0x5930: Large data tables
 */
