# Ghidra Python Script for AS2464 USB4/NVMe Firmware
# Imports function names, register labels, and global variables from reverse engineering work
#
# To use: Run in Ghidra's Script Manager on the loaded fw_bank0.bin or fw_bank1.bin
# @author reverse-asm2464 project
# @category ASMedia.AS2464

from ghidra.program.model.symbol import SourceType
from ghidra.program.model.address import AddressSet

def create_function_if_needed(addr, name):
    """Create function at address if it doesn't exist, then set the name"""
    func = getFunctionAt(addr)
    if func is None:
        createFunction(addr, name)
        func = getFunctionAt(addr)
    if func is not None:
        func.setName(name, SourceType.USER_DEFINED)
        print("Added function: {} at {}".format(name, addr))
        return True
    else:
        # Try to just set a label
        createLabel(addr, name, True)
        print("Added label: {} at {}".format(name, addr))
        return True
    return False

def create_label(addr, name):
    """Create a label at the given address"""
    try:
        createLabel(addr, name, True)
        print("Added label: {} at {}".format(name, addr))
        return True
    except:
        print("Failed to add label: {} at {}".format(name, addr))
        return False

def add_functions():
    """Add all known function names to CODE space"""

    # Function mappings: (address, name)
    # These are all verified addresses from our C reimplementation
    functions = [
        # =========================================================================
        # Core System / Banking Functions (0x0000-0x07FF)
        # =========================================================================
        (0x0000, "reset_vector"),
        (0x0300, "jump_bank_0"),
        (0x030a, "jump_bank_0_entry"),
        (0x0311, "jump_bank_1"),
        (0x0327, "handler_0327_usb_power_init"),
        (0x0390, "handler_0390"),
        (0x039a, "handler_039a_buffer_dispatch"),
        (0x0494, "handler_0494_event"),
        (0x0499, "timer0_poll_handler_0499"),
        (0x04b2, "handler_04b2_reserved"),
        (0x04d0, "handler_04d0_timer_link"),
        (0x04d5, "handler_04d5"),
        (0x0520, "handler_0520_system_int"),
        (0x0525, "handler_0525_flash_cmd"),
        (0x052f, "handler_052f_pcie_nvme_event"),
        (0x0570, "handler_0570_bank1_e911"),
        (0x0589, "handler_0589_phy_config"),
        (0x0593, "handler_0593_bank0_c105"),
        (0x0606, "handler_0606_error_state"),
        (0x0610, "handler_0610"),
        (0x061a, "handler_061a_bank1_a066"),
        (0x063d, "handler_063d"),
        (0x0642, "handler_0642_bank1_ef4e"),
        (0x0648, "init_data_table"),

        # =========================================================================
        # Flash / I2C Functions (0x0A00-0x0FFF)
        # =========================================================================
        (0x0be6, "write_xdata_reg"),
        (0x0c0f, "flash_div16"),
        (0x0c64, "flash_add_to_xdata16"),
        (0x0c7a, "flash_write_byte"),
        (0x0c87, "flash_write_idata"),
        (0x0c8f, "flash_write_r1_xdata"),
        (0x0d22, "reg_read_byte"),
        (0x0d33, "reg_read_word"),
        (0x0d46, "reg_read_dword"),
        (0x0d78, "idata_load_dword"),
        (0x0d84, "xdata_load_dword"),
        (0x0d90, "idata_load_dword_alt"),
        (0x0db9, "idata_store_dword"),
        (0x0dc5, "xdata_store_dword"),
        (0x0dd1, "mul_add_index"),
        (0x0ddd, "reg_wait_bit_set"),
        (0x0de6, "reg_wait_bit_clear"),
        (0x0def, "reg_poll"),

        # =========================================================================
        # External Interrupt 0 Handler (0x0E00-0x11FF)
        # =========================================================================
        (0x0e5b, "ext0_isr"),
        (0x0e96, "usb_ep_dispatch_loop"),
        (0x0f1c, "usb_endpoint_handler"),
        (0x0f2f, "usb_peripheral_handler"),
        (0x10e0, "usb_master_handler"),

        # =========================================================================
        # USB/NVMe Functions (0x1700-0x1DFF)
        # =========================================================================
        (0x173b, "dma_clear_dword"),
        (0x1743, "usb_get_sys_status_offset"),
        (0x1752, "usb_calc_addr_with_offset"),
        (0x175d, "dma_init_channel_b8"),
        (0x176b, "usb_calc_queue_addr"),
        (0x1779, "usb_calc_queue_addr_next"),
        (0x1787, "usb_set_done_flag"),
        (0x178e, "dma_calc_addr_with_carry"),
        (0x1795, "dma_clear_state_counters"),
        (0x179d, "usb_calc_indexed_addr"),
        (0x17a9, "dma_init_ep_queue"),
        (0x17b5, "scsi_get_tag_count_status"),
        (0x17c1, "usb_read_queue_status_masked"),
        (0x17cd, "usb_shift_right_3"),
        (0x17f3, "dma_shift_rrc2_mask"),
        (0x180d, "dma_store_to_0a7d"),
        (0x16f3, "dma_clear_status"),
        (0x16ff, "dma_reg_wait_bit"),
        (0x1709, "dma_set_scsi_param3"),
        (0x1713, "dma_set_scsi_param1"),
        (0x171d, "dma_load_transfer_params"),
        (0x172c, "dma_check_state_counter"),
        (0x1b7e, "usb_enable"),
        (0x1b88, "usb_calc_addr_009f"),
        (0x1b96, "usb_get_ep_config_indexed"),
        (0x1ba5, "usb_read_buf_addr_pair"),
        (0x1bae, "usb_get_idata_0x12_field"),
        (0x1bcb, "nvme_load_transfer_data"),
        (0x1bd7, "usb_setup_endpoint"),
        (0x1bde, "nvme_set_usb_mode_bit"),
        (0x1be8, "nvme_get_config_offset"),
        (0x1bf6, "nvme_calc_buffer_offset"),
        (0x1c0f, "nvme_calc_idata_offset"),
        (0x1c22, "nvme_check_scsi_ctrl"),
        (0x1c56, "nvme_get_dev_status_upper"),
        (0x1c6d, "nvme_subtract_idata_16"),
        (0x1c77, "nvme_get_cmd_param_upper"),
        (0x1c88, "nvme_calc_addr_01xx"),
        (0x1cae, "nvme_inc_circular_counter"),
        (0x1cb7, "nvme_calc_addr_012b"),
        (0x1cc1, "nvme_set_ep_queue_ctrl_84"),
        (0x1cd4, "nvme_clear_status_bit1"),
        (0x1cdc, "nvme_add_to_global_053a"),
        (0x1ce4, "nvme_calc_addr_04b7"),
        (0x1cfc, "usb_ep_config_bulk"),
        (0x1d07, "usb_ep_config_int"),
        (0x1d1d, "usb_set_transfer_flag"),
        (0x1d24, "nvme_get_data_ctrl_upper"),
        (0x1d2b, "nvme_set_data_ctrl_bit7"),
        (0x1d32, "nvme_store_idata_16"),
        (0x1d39, "usb_add_masked_counter"),
        (0x1d43, "usb_init_pcie_txn_state"),

        # =========================================================================
        # Main Loop Functions (0x2F00-0x33FF)
        # =========================================================================
        (0x2608, "handler_2608"),
        (0x2f80, "main_loop"),
        (0x3094, "timer1_check_and_ack"),
        (0x312a, "usb_set_transfer_active_flag"),
        (0x3147, "usb_copy_status_to_buffer"),
        (0x3168, "usb_clear_idata_indexed"),
        (0x3181, "usb_read_status_pair"),
        (0x31a5, "usb_read_transfer_params"),
        (0x3adb, "handler_3adb"),

        # =========================================================================
        # Main Entry / Init Functions (0x4300-0x44FF)
        # =========================================================================
        (0x431a, "main"),
        (0x4352, "process_init_table"),
        (0x4486, "timer0_isr"),

        # =========================================================================
        # DMA Functions (0x4A00-0x4BFF)
        # =========================================================================
        (0x4a57, "dma_config_channel"),
        (0x4a94, "dma_start_transfer"),
        (0x4be6, "handler_4be6"),
        (0x4fb6, "handler_4fb6"),

        # =========================================================================
        # USB Endpoint Processing (0x5200-0x55FF)
        # =========================================================================
        (0x523c, "dma_setup_transfer"),
        (0x5260, "dma_check_scsi_status"),
        (0x5284, "phy_config_link_params"),
        (0x52a7, "usb_ep_process"),
        (0x5305, "handler_5305"),
        (0x5409, "usb_ep_init_handler"),
        (0x5418, "reg_set_bit_0"),
        (0x5442, "usb_ep_handler"),

        # =========================================================================
        # Lookup Tables (0x5A00-0x5BFF)
        # =========================================================================
        (0x5a6a, "ep_index_table"),
        (0x5b6a, "ep_bit_mask_table"),
        (0x5b72, "ep_offset_table"),

        # =========================================================================
        # Timer Functions (0x95xx, 0xADxx)
        # =========================================================================
        (0x95b6, "handler_95b6"),
        (0x95c2, "timer0_csr_ack"),
        (0xad72, "timer0_setup"),
        (0xad95, "timer0_wait_done"),

        # =========================================================================
        # PCIe Functions (0x9900-0x9AFF)
        # =========================================================================
        (0x9902, "pcie_init"),
        (0x990c, "pcie_init_alt"),
        (0x999d, "pcie_clear_and_trigger"),
        (0x99eb, "pcie_get_completion_status"),
        (0x99f6, "pcie_set_idata_params"),
        (0x9a18, "pcie_setup_buffer_params"),
        (0x9a30, "pcie_set_byte_enables"),
        (0x9a53, "pcie_clear_reg_at_offset"),
        (0x9a60, "pcie_get_link_speed"),
        (0x9a74, "pcie_read_completion_data"),
        (0x9a7f, "pcie_wait_complete"),
        (0x9a8a, "pcie_inc_txn_counters"),
        (0x9a95, "pcie_write_status_complete"),
        (0x9a9c, "pcie_clear_address_regs"),
        (0x9aa9, "pcie_get_txn_count_hi"),

        # =========================================================================
        # Link/PHY Handlers (0xB000-0xB5FF)
        # =========================================================================
        (0xb031, "handler_b031"),
        (0xb10f, "flash_error_handler"),
        (0xb1a4, "flash_wait_and_poll"),
        (0xb1cb, "usb_power_init"),
        (0xb230, "error_state_handler"),
        (0xb4ba, "timer_tick_handler"),
        (0xb845, "flash_set_cmd"),
        (0xb85b, "flash_set_mode_bit4"),
        (0xb865, "flash_set_addr_md"),
        (0xb873, "flash_set_addr_hi"),
        (0xb888, "flash_set_data_len"),
        (0xb895, "flash_read_buffer_and_status"),
        (0xb8ae, "flash_set_mode_enable"),
        (0xb8c3, "handler_b8c3"),
        (0xbaa0, "flash_cmd_handler"),
        (0xbc8f, "handler_bc8f"),
        (0xbcb1, "handler_bcb1"),
        (0xbceb, "handler_bceb"),
        (0xbd2a, "handler_bd2a"),
        (0xbd49, "handler_bd49"),
        (0xbd5e, "handler_bd5e"),
        (0xbe36, "flash_run_transaction"),
        (0xbe70, "flash_poll_busy"),
        (0xbf8e, "handler_bf8e"),

        # =========================================================================
        # PCIe TLP Functions (0xC000-0xC2FF)
        # =========================================================================
        (0xc00d, "pcie_error_handler"),
        (0xc105, "handler_c105"),
        (0xc20c, "pcie_setup_memory_tlp"),
        (0xc245, "pcie_poll_and_read_completion"),

        # =========================================================================
        # Timer/Link Handlers (0xCE00-0xD0FF)
        # =========================================================================
        (0xce79, "timer_link_handler"),
        (0xcf28, "timer_config_handler"),
        (0xd07f, "handler_d07f"),
        (0xd0d3, "link_status_clear_handler"),
        (0xd676, "handler_d676"),
        (0xd810, "usb_buffer_handler"),
        (0xd894, "phy_register_config"),
        (0xd996, "handler_d996"),
        (0xdd42, "handler_dd42"),
        (0xde7e, "pcie_init_internal"),
        (0xdfdc, "handler_dfdc"),

        # =========================================================================
        # USB/Link Functions (0xE000-0xE4FF)
        # =========================================================================
        (0xe214, "handler_e214"),
        (0xe3d8, "handler_e3d8"),
        (0xe3f9, "flash_read_status"),
        (0xe4b4, "handler_e4b4"),
        (0xe529, "handler_e529"),
        (0xe56f, "event_state_handler"),
        (0xe6e7, "handler_e6e7"),
        (0xe6f0, "handler_e6f0"),
        (0xe80a, "delay_function"),
        (0xe8ef, "handler_e8ef"),
        (0xe90b, "handler_e90b"),

        # =========================================================================
        # Bank 1 Functions (0x8000+ = file 0x10000+)
        # =========================================================================
        (0x8000, "bank1_entry"),
        (0x83d6, "handler_83d6"),
        (0xa066, "error_handler_a066"),
        (0xabc9, "handler_abc9"),
        (0xadb0, "pcie_set_address"),
        (0xadc3, "pcie_setup_config_tlp"),
        (0xae87, "pcie_check_completion"),
        (0xaf5e, "debug_output_handler"),
        (0xe911, "error_handler_e911"),
        (0xe971, "reserved_stub"),
        (0xed02, "handler_ed02"),
        (0xee11, "handler_ee11"),
        (0xeef9, "handler_eef9"),
        (0xef4e, "error_handler_ef4e"),
    ]

    count = 0
    for addr_int, name in functions:
        try:
            addr = toAddr(addr_int)
            if create_function_if_needed(addr, name):
                count += 1
        except Exception as e:
            print("Error adding function {} at 0x{:04X}: {}".format(name, addr_int, e))

    return count

def add_registers():
    """Add all known register labels to EXTMEM space"""

    # Register mappings: (address, name)
    # These are hardware registers (>= 0x6000)
    registers = [
        # =========================================================================
        # UART Controller (0xC000-0xC00F)
        # =========================================================================
        (0xC000, "REG_UART_BASE"),
        (0xC001, "REG_UART_THR_RBR"),
        (0xC002, "REG_UART_IER"),
        (0xC004, "REG_UART_FCR_IIR"),
        (0xC006, "REG_UART_TFBF"),
        (0xC007, "REG_UART_LCR"),
        (0xC008, "REG_UART_MCR"),
        (0xC009, "REG_UART_LSR"),
        (0xC00A, "REG_UART_MSR"),

        # =========================================================================
        # I2C Controller (0xC870-0xC87F)
        # =========================================================================
        (0xC870, "REG_I2C_ADDR"),
        (0xC871, "REG_I2C_MODE"),
        (0xC873, "REG_I2C_LEN"),
        (0xC875, "REG_I2C_CSR"),
        (0xC878, "REG_I2C_SRC"),
        (0xC87C, "REG_I2C_DST"),
        (0xC87F, "REG_I2C_CSR_ALT"),

        # =========================================================================
        # SPI Flash Controller (0xC89F-0xC8AF)
        # =========================================================================
        (0xC89F, "REG_FLASH_CON"),
        (0xC8A1, "REG_FLASH_ADDR_LO"),
        (0xC8A2, "REG_FLASH_ADDR_MD"),
        (0xC8A3, "REG_FLASH_DATA_LEN"),
        (0xC8A4, "REG_FLASH_DATA_LEN_HI"),
        (0xC8A6, "REG_FLASH_DIV"),
        (0xC8A9, "REG_FLASH_CSR"),
        (0xC8AA, "REG_FLASH_CMD"),
        (0xC8AB, "REG_FLASH_ADDR_HI"),
        (0xC8AC, "REG_FLASH_ADDR_LEN"),
        (0xC8AD, "REG_FLASH_MODE"),
        (0xC8AE, "REG_FLASH_BUF_OFFSET"),

        # =========================================================================
        # Interrupt Controller (0xC800-0xC80F)
        # =========================================================================
        (0xC801, "REG_INT_CTRL_C801"),
        (0xC802, "REG_INT_USB_MASTER"),
        (0xC806, "REG_INT_SYSTEM"),
        (0xC809, "REG_INT_CTRL_C809"),
        (0xC80A, "REG_INT_PCIE_NVME"),

        # =========================================================================
        # Timer Registers (0xCC10-0xCC24)
        # =========================================================================
        (0xCC10, "REG_TIMER0_DIV"),
        (0xCC11, "REG_TIMER0_CSR"),
        (0xCC12, "REG_TIMER0_THRESHOLD"),
        (0xCC16, "REG_TIMER1_DIV"),
        (0xCC17, "REG_TIMER1_CSR"),
        (0xCC18, "REG_TIMER1_THRESHOLD"),
        (0xCC1C, "REG_TIMER2_DIV"),
        (0xCC1D, "REG_TIMER2_CSR"),
        (0xCC1E, "REG_TIMER2_THRESHOLD"),
        (0xCC22, "REG_TIMER3_DIV"),
        (0xCC23, "REG_TIMER3_CSR"),
        (0xCC24, "REG_TIMER3_IDLE_TIMEOUT"),

        # =========================================================================
        # CPU Control Extended (0xCC30-0xCCFF)
        # =========================================================================
        (0xCC30, "REG_CPU_CTRL_CC30"),
        (0xCC31, "REG_CPU_EXEC_CTRL"),
        (0xCC32, "REG_CPU_EXEC_STATUS"),
        (0xCC33, "REG_CPU_EXEC_STATUS_2"),
        (0xCC3A, "REG_CPU_CTRL_CC3A"),
        (0xCC3B, "REG_CPU_CTRL_CC3B"),
        (0xCC3D, "REG_CPU_CTRL_CC3D"),
        (0xCC3E, "REG_CPU_CTRL_CC3E"),
        (0xCC3F, "REG_CPU_CTRL_CC3F"),
        (0xCC81, "REG_CPU_STATUS_CC81"),
        (0xCC91, "REG_CPU_STATUS_CC91"),
        (0xCC98, "REG_CPU_STATUS_CC98"),
        (0xCCD8, "REG_CPU_DMA_CCD8"),
        (0xCCDA, "REG_CPU_DMA_CCDA"),
        (0xCCDB, "REG_CPU_DMA_CCDB"),

        # =========================================================================
        # CPU Control (0xCA00-0xCAFF)
        # =========================================================================
        (0xCA06, "REG_CPU_MODE_NEXT"),
        (0xCA81, "REG_CA81"),

        # =========================================================================
        # CPU Link (0xCEF0-0xCEFF)
        # =========================================================================
        (0xCEF2, "REG_CPU_LINK_CEF2"),
        (0xCEF3, "REG_CPU_LINK_CEF3"),

        # =========================================================================
        # Power Management (0x92C0-0x92E8)
        # =========================================================================
        (0x92C0, "REG_POWER_CTRL_92C0"),
        (0x92C1, "REG_POWER_CTRL_92C1"),
        (0x92C2, "REG_POWER_STATUS_92C2"),
        (0x92C4, "REG_POWER_CTRL_92C4"),
        (0x92C5, "REG_POWER_CTRL_92C5"),
        (0x92C6, "REG_POWER_CTRL_92C6"),
        (0x92C8, "REG_POWER_CTRL_92C8"),
        (0x92E0, "REG_POWER_DOMAIN_92E0"),

        # =========================================================================
        # USB Interface (0x9000-0x91FF)
        # =========================================================================
        (0x9000, "REG_USB_STATUS"),
        (0x9001, "REG_USB_CONTROL"),
        (0x9002, "REG_USB_CONFIG"),
        (0x9003, "REG_USB_EP0_STATUS"),
        (0x9004, "REG_USB_EP0_LEN_L"),
        (0x9005, "REG_USB_EP0_LEN_H"),
        (0x9006, "REG_USB_EP0_CONFIG"),
        (0x9007, "REG_USB_SCSI_BUF_LEN_L"),
        (0x9008, "REG_USB_SCSI_BUF_LEN_H"),
        (0x9010, "REG_USB_DATA_L"),
        (0x9011, "REG_USB_DATA_H"),
        (0x9012, "REG_USB_FIFO_L"),
        (0x9013, "REG_USB_FIFO_H"),
        (0x905E, "REG_USB_EP_CTRL_905E"),
        (0x90E2, "REG_USB_MODE_90E2"),
        (0x9091, "REG_INT_FLAGS_EX0"),
        (0x9093, "REG_USB_EP_CFG1"),
        (0x9094, "REG_USB_EP_CFG2"),
        (0x9096, "REG_USB_EP_READY"),
        (0x9101, "REG_USB_PERIPH_STATUS"),
        (0x910D, "REG_USB_STATUS_0D"),
        (0x910E, "REG_USB_STATUS_0E"),
        (0x9118, "REG_USB_EP_STATUS"),
        (0x911B, "REG_USB_BUFFER_ALT"),
        (0x911F, "REG_USB_STATUS_1F"),
        (0x9120, "REG_USB_STATUS_20"),
        (0x9121, "REG_USB_STATUS_21"),
        (0x9122, "REG_USB_STATUS_22"),
        (0x91C0, "REG_USB_PHY_CTRL_91C0"),
        (0x91C1, "REG_USB_PHY_CTRL_91C1"),
        (0x91C3, "REG_USB_PHY_CTRL_91C3"),
        (0x91D1, "REG_USB_PHY_CTRL_91D1"),

        # =========================================================================
        # Buffer/DMA Configuration (0x9300-0x93FF)
        # =========================================================================
        (0x9300, "REG_BUF_CFG_9300"),
        (0x9301, "REG_BUF_CFG_9301"),
        (0x9302, "REG_BUF_CFG_9302"),
        (0x9303, "REG_BUF_CFG_9303"),
        (0x9304, "REG_BUF_CFG_9304"),
        (0x9305, "REG_BUF_CFG_9305"),

        # =========================================================================
        # Link/PHY Control (0xC200-0xC6FF)
        # =========================================================================
        (0xC202, "REG_LINK_CTRL"),
        (0xC203, "REG_LINK_CONFIG"),
        (0xC204, "REG_LINK_STATUS"),
        (0xC205, "REG_PHY_CTRL"),
        (0xC233, "REG_PHY_CONFIG"),
        (0xC284, "REG_PHY_STATUS"),
        (0xC62D, "REG_PHY_EXT_2D"),
        (0xC656, "REG_PHY_EXT_56"),
        (0xC65B, "REG_PHY_EXT_5B"),
        (0xC6B3, "REG_PHY_EXT_B3"),

        # =========================================================================
        # NVMe Interface (0xC400-0xC5FF)
        # =========================================================================
        (0xC400, "REG_NVME_CTRL"),
        (0xC401, "REG_NVME_STATUS"),
        (0xC412, "REG_NVME_CTRL_STATUS"),
        (0xC413, "REG_NVME_CONFIG"),
        (0xC414, "REG_NVME_DATA_CTRL"),
        (0xC415, "REG_NVME_DEV_STATUS"),
        (0xC420, "REG_NVME_CMD"),
        (0xC421, "REG_NVME_CMD_OPCODE"),
        (0xC422, "REG_NVME_LBA_0"),
        (0xC423, "REG_NVME_LBA_1"),
        (0xC424, "REG_NVME_LBA_2"),
        (0xC425, "REG_NVME_COUNT_LOW"),
        (0xC426, "REG_NVME_COUNT_HIGH"),
        (0xC427, "REG_NVME_ERROR"),
        (0xC428, "REG_NVME_QUEUE_CFG"),
        (0xC429, "REG_NVME_CMD_PARAM"),
        (0xC42A, "REG_NVME_DOORBELL"),
        (0xC42B, "REG_NVME_CMD_FLAGS"),
        (0xC42C, "REG_NVME_CMD_NSID"),
        (0xC42D, "REG_NVME_CMD_PRP1"),
        (0xC431, "REG_NVME_CMD_PRP2"),
        (0xC435, "REG_NVME_CMD_CDW10"),
        (0xC439, "REG_NVME_CMD_CDW11"),
        (0xC43D, "REG_NVME_QUEUE_PTR"),
        (0xC43E, "REG_NVME_QUEUE_DEPTH"),
        (0xC43F, "REG_NVME_PHASE"),
        (0xC440, "REG_NVME_QUEUE_CTRL"),
        (0xC441, "REG_NVME_SQ_HEAD"),
        (0xC442, "REG_NVME_SQ_TAIL"),
        (0xC443, "REG_NVME_CQ_HEAD"),
        (0xC444, "REG_NVME_CQ_TAIL"),
        (0xC445, "REG_NVME_CQ_STATUS"),
        (0xC446, "REG_NVME_LBA_3"),
        (0xC462, "REG_DMA_ENTRY"),
        (0xC470, "REG_CMDQ_DIR_END"),
        (0xC471, "REG_NVME_QUEUE_PTR_C471"),
        (0xC520, "REG_NVME_LINK_STATUS"),

        # =========================================================================
        # DMA Engine (0xC8B0-0xC8DF)
        # =========================================================================
        (0xC8B0, "REG_DMA_MODE"),
        (0xC8B2, "REG_DMA_CHAN_AUX"),
        (0xC8B3, "REG_DMA_CHAN_AUX1"),
        (0xC8B4, "REG_DMA_XFER_CNT_HI"),
        (0xC8B5, "REG_DMA_XFER_CNT_LO"),
        (0xC8B6, "REG_DMA_CHAN_CTRL2"),
        (0xC8B7, "REG_DMA_CHAN_STATUS2"),
        (0xC8B8, "REG_DMA_TRIGGER"),
        (0xC8D4, "REG_DMA_CONFIG"),
        (0xC8D6, "REG_DMA_STATUS"),
        (0xC8D8, "REG_DMA_STATUS2"),

        # =========================================================================
        # SCSI/Mass Storage DMA (0xCE40-0xCE6F)
        # =========================================================================
        (0xCE40, "REG_SCSI_DMA_PARAM0"),
        (0xCE41, "REG_SCSI_DMA_PARAM1"),
        (0xCE42, "REG_SCSI_DMA_PARAM2"),
        (0xCE43, "REG_SCSI_DMA_PARAM3"),
        (0xCE5C, "REG_SCSI_DMA_COMPL"),
        (0xCE66, "REG_SCSI_DMA_TAG_COUNT"),
        (0xCE67, "REG_SCSI_DMA_QUEUE_STAT"),
        (0xCE6E, "REG_SCSI_DMA_STATUS"),

        # =========================================================================
        # Data Buffer Registers (0xD800-0xD8FF)
        # =========================================================================
        (0xD800, "REG_BUFFER_CTRL"),
        (0xD801, "REG_BUFFER_SELECT"),
        (0xD802, "REG_BUFFER_DATA"),
        (0xD803, "REG_BUFFER_PTR_LOW"),
        (0xD804, "REG_BUFFER_PTR_HIGH"),
        (0xD805, "REG_BUFFER_LENGTH_LOW"),
        (0xD806, "REG_BUFFER_STATUS"),
        (0xD807, "REG_BUFFER_LENGTH_HIGH"),
        (0xD808, "REG_BUFFER_CTRL_GLOBAL"),
        (0xD809, "REG_BUFFER_THRESHOLD_HIGH"),
        (0xD80A, "REG_BUFFER_THRESHOLD_LOW"),
        (0xD80B, "REG_BUFFER_FLOW_CTRL"),
        (0xD80C, "REG_BUFFER_XFER_START"),
        (0xD810, "REG_BUFFER_L"),
        (0xD811, "REG_BUFFER_H"),

        # =========================================================================
        # PCIe Passthrough (0xB210-0xB4C8)
        # =========================================================================
        (0xB210, "REG_PCIE_FMT_TYPE"),
        (0xB213, "REG_PCIE_TLP_CTRL"),
        (0xB216, "REG_PCIE_TLP_LENGTH"),
        (0xB217, "REG_PCIE_BYTE_EN"),
        (0xB218, "REG_PCIE_ADDR_0"),
        (0xB219, "REG_PCIE_ADDR_1"),
        (0xB21A, "REG_PCIE_ADDR_2"),
        (0xB21B, "REG_PCIE_ADDR_3"),
        (0xB220, "REG_PCIE_DATA"),
        (0xB224, "REG_PCIE_TLP_CPL_HEADER"),
        (0xB22A, "REG_PCIE_LINK_STATUS"),
        (0xB22B, "REG_PCIE_CPL_STATUS"),
        (0xB22C, "REG_PCIE_CPL_DATA"),
        (0xB22D, "REG_PCIE_CPL_DATA_ALT"),
        (0xB250, "REG_PCIE_NVME_DOORBELL"),
        (0xB254, "REG_PCIE_TRIGGER"),
        (0xB255, "REG_PCIE_PM_ENTER"),
        (0xB284, "REG_PCIE_COMPL_STATUS"),
        (0xB296, "REG_PCIE_STATUS"),
        (0xB402, "REG_PCIE_CTRL_B402"),
        (0xB424, "REG_PCIE_LANE_COUNT"),
        (0xB4AE, "REG_PCIE_LINK_STATUS_ALT"),
        (0xB4C8, "REG_PCIE_LANE_MASK"),

        # =========================================================================
        # Command Engine (0xE400-0xE4FF)
        # =========================================================================
        (0xE40B, "REG_CMD_CONFIG"),
        (0xE40F, "REG_CMD_CTRL_E40F"),
        (0xE410, "REG_CMD_CTRL_E410"),
        (0xE422, "REG_CMD_PARAM"),
        (0xE423, "REG_CMD_STATUS"),
        (0xE424, "REG_CMD_ISSUE"),
        (0xE425, "REG_CMD_TAG"),
        (0xE426, "REG_CMD_LBA_0"),
        (0xE427, "REG_CMD_LBA_1"),
        (0xE428, "REG_CMD_LBA_2"),
        (0xE429, "REG_CMD_LBA_3"),
        (0xE42A, "REG_CMD_COUNT_LOW"),
        (0xE42B, "REG_CMD_COUNT_HIGH"),
        (0xE42C, "REG_CMD_LENGTH_LOW"),
        (0xE42D, "REG_CMD_LENGTH_HIGH"),
        (0xE42E, "REG_CMD_RESP_TAG"),
        (0xE42F, "REG_CMD_RESP_STATUS"),
        (0xE430, "REG_CMD_CTRL"),
        (0xE431, "REG_CMD_TIMEOUT"),
        (0xE432, "REG_CMD_PARAM_L"),
        (0xE433, "REG_CMD_PARAM_H"),

        # =========================================================================
        # System Status Registers (0xE700-0xE7FF)
        # =========================================================================
        (0xE324, "REG_LINK_CTRL_E324"),
        (0xE712, "REG_LINK_STATUS_E712"),
        (0xE760, "REG_SYS_CTRL_E760"),
        (0xE761, "REG_SYS_CTRL_E761"),
        (0xE763, "REG_SYS_CTRL_E763"),
        (0xE795, "REG_FLASH_READY_STATUS"),
        (0xE7E3, "REG_PHY_LINK_CTRL"),
        (0xE7FC, "REG_LINK_MODE_CTRL"),

        # =========================================================================
        # NVMe Event Registers (0xEC00-0xEC0F)
        # =========================================================================
        (0xEC04, "REG_NVME_EVENT_ACK"),
        (0xEC06, "REG_NVME_EVENT_STATUS"),

        # =========================================================================
        # System Registers
        # =========================================================================
        (0xEF4E, "REG_CRITICAL_CTRL"),
    ]

    count = 0
    for addr_int, name in registers:
        try:
            # Try EXTMEM space first
            addr = toAddr("EXTMEM:{:04X}".format(addr_int))
            if create_label(addr, name):
                count += 1
        except:
            try:
                # Fall back to direct address
                addr = toAddr(addr_int)
                if create_label(addr, name):
                    count += 1
            except Exception as e:
                print("Error adding register {} at 0x{:04X}: {}".format(name, addr_int, e))

    return count

def add_globals():
    """Add all known global variable labels to EXTMEM space"""

    # Global variable mappings: (address, name)
    # These are RAM locations (< 0x6000)
    globals_list = [
        # =========================================================================
        # System Work Area (0x0000-0x01FF)
        # =========================================================================
        (0x0000, "G_SYSTEM_CTRL"),
        (0x000A, "G_EP_CHECK_FLAG"),
        (0x014E, "G_USB_INDEX_COUNTER"),
        (0x0171, "G_SCSI_CTRL"),

        # =========================================================================
        # DMA Work Area (0x0200-0x02FF)
        # =========================================================================
        (0x0203, "G_DMA_MODE_SELECT"),
        (0x020D, "G_DMA_PARAM1"),
        (0x020E, "G_DMA_PARAM2"),
        (0x0218, "G_BUF_ADDR_HI"),
        (0x0219, "G_BUF_ADDR_LO"),
        (0x021A, "G_BUF_BASE_HI"),
        (0x021B, "G_BUF_BASE_LO"),

        # =========================================================================
        # System Status Work Area (0x0400-0x04FF)
        # =========================================================================
        (0x045E, "G_REG_WAIT_BIT"),
        (0x0464, "G_SYS_STATUS_PRIMARY"),
        (0x0465, "G_SYS_STATUS_SECONDARY"),
        (0x0466, "G_SYSTEM_CONFIG"),
        (0x0467, "G_SYSTEM_STATE"),
        (0x0468, "G_DATA_PORT"),
        (0x0469, "G_INT_STATUS"),
        (0x0472, "G_DMA_LOAD_PARAM1"),
        (0x0473, "G_DMA_LOAD_PARAM2"),
        (0x0474, "G_STATE_HELPER_41"),
        (0x0475, "G_STATE_HELPER_42"),

        # =========================================================================
        # Endpoint Configuration Work Area (0x0500-0x05FF)
        # =========================================================================
        (0x053A, "G_NVME_PARAM_053A"),
        (0x054B, "G_EP_CONFIG_BASE"),
        (0x054E, "G_EP_CONFIG_ARRAY"),
        (0x0564, "G_EP_QUEUE_CTRL"),
        (0x0565, "G_EP_QUEUE_STATUS"),
        (0x0568, "G_BUF_OFFSET_HI"),
        (0x0569, "G_BUF_OFFSET_LO"),
        (0x0574, "G_LOG_PROCESS_STATE"),
        (0x0575, "G_LOG_ENTRY_VALUE"),
        (0x05A6, "G_PCIE_TXN_COUNT_LO"),
        (0x05A7, "G_PCIE_TXN_COUNT_HI"),
        (0x05A8, "G_EP_CONFIG_05A8"),
        (0x05AE, "G_PCIE_DIRECTION"),
        (0x05AF, "G_PCIE_ADDR_0"),
        (0x05B0, "G_PCIE_ADDR_1"),
        (0x05B1, "G_PCIE_ADDR_2"),
        (0x05B2, "G_PCIE_ADDR_3"),
        (0x05D3, "G_EP_CONFIG_MULT_BASE"),
        (0x05F8, "G_EP_CONFIG_05F8"),

        # =========================================================================
        # Transfer Work Area (0x0600-0x07FF)
        # =========================================================================
        (0x06E5, "G_MAX_LOG_ENTRIES"),
        (0x06E6, "G_STATE_FLAG_06E6"),
        (0x06EA, "G_ERROR_CODE_06EA"),
        (0x06EC, "G_MISC_FLAG_06EC"),
        (0x07B8, "G_FLASH_CMD_FLAG"),
        (0x07BC, "G_FLASH_CMD_TYPE"),
        (0x07BD, "G_FLASH_OP_COUNTER"),
        (0x07E4, "G_SYS_FLAGS_BASE"),
        (0x07E5, "G_TRANSFER_ACTIVE"),
        (0x07EC, "G_SYS_FLAGS_07EC"),
        (0x07ED, "G_SYS_FLAGS_07ED"),
        (0x07EE, "G_SYS_FLAGS_07EE"),
        (0x07EF, "G_SYS_FLAGS_07EF"),

        # =========================================================================
        # Event/Loop State Work Area (0x0900-0x09FF)
        # =========================================================================
        (0x097A, "G_EVENT_INIT_097A"),
        (0x098E, "G_LOOP_CHECK_098E"),
        (0x0991, "G_LOOP_STATE_0991"),
        (0x09EF, "G_EVENT_CHECK_09EF"),
        (0x09F9, "G_EVENT_FLAGS"),
        (0x09FA, "G_EVENT_CTRL_09FA"),

        # =========================================================================
        # Endpoint Dispatch Work Area (0x0A00-0x0BFF)
        # =========================================================================
        (0x0A59, "G_LOOP_STATE"),
        (0x0A7B, "G_EP_DISPATCH_VAL1"),
        (0x0A7C, "G_EP_DISPATCH_VAL2"),
        (0x0A7D, "G_EP_DISPATCH_VAL3"),
        (0x0AA1, "G_LOG_PROCESSED_INDEX"),
        (0x0AA3, "G_STATE_COUNTER_HI"),
        (0x0AA4, "G_STATE_COUNTER_LO"),
        (0x0AA8, "G_FLASH_ERROR_0"),
        (0x0AA9, "G_FLASH_ERROR_1"),
        (0x0AAA, "G_FLASH_RESET_0AAA"),
        (0x0AAD, "G_FLASH_ADDR_0"),
        (0x0AAE, "G_FLASH_ADDR_1"),
        (0x0AAF, "G_FLASH_ADDR_2"),
        (0x0AB0, "G_FLASH_ADDR_3"),
        (0x0AB1, "G_FLASH_LEN_LO"),
        (0x0AB2, "G_FLASH_LEN_HI"),
        (0x0AE2, "G_SYSTEM_STATE_0AE2"),
        (0x0AE3, "G_STATE_FLAG_0AE3"),
        (0x0AEE, "G_STATE_CHECK_0AEE"),
        (0x0AF1, "G_STATE_FLAG_0AF1"),
        (0x0AF2, "G_TRANSFER_FLAG_0AF2"),
        (0x0AF5, "G_EP_DISPATCH_OFFSET"),
        (0x0AFA, "G_TRANSFER_PARAMS_HI"),
        (0x0AFB, "G_TRANSFER_PARAMS_LO"),
        (0x0B00, "G_USB_PARAM_0B00"),
        (0x0B2E, "G_USB_TRANSFER_FLAG"),
        (0x0B41, "G_USB_STATE_0B41"),

        # =========================================================================
        # Memory Buffers
        # =========================================================================
        (0x7000, "FLASH_BUFFER_BASE"),
        (0x8000, "USB_SCSI_BUF_BASE"),
        (0x9E00, "USB_CTRL_BUF_BASE"),
        (0xA000, "NVME_IOSQ_BASE"),
        (0xB000, "NVME_ASQ_BASE"),
        (0xB100, "NVME_ACQ_BASE"),
        (0xF000, "NVME_DATA_BUF_BASE"),
    ]

    count = 0
    for addr_int, name in globals_list:
        try:
            # Try EXTMEM space first
            addr = toAddr("EXTMEM:{:04X}".format(addr_int))
            if create_label(addr, name):
                count += 1
        except:
            try:
                # Fall back to direct address
                addr = toAddr(addr_int)
                if create_label(addr, name):
                    count += 1
            except Exception as e:
                print("Error adding global {} at 0x{:04X}: {}".format(name, addr_int, e))

    return count

def run():
    """Main entry point"""
    print("=" * 60)
    print("AS2464 USB4/NVMe Firmware Symbol Import Script")
    print("Updated with all function, register, and global names")
    print("from the C reimplementation project")
    print("=" * 60)

    print("\nAdding function names...")
    func_count = add_functions()
    print("Added {} functions".format(func_count))

    print("\nAdding register labels...")
    reg_count = add_registers()
    print("Added {} registers".format(reg_count))

    print("\nAdding global variable labels...")
    glob_count = add_globals()
    print("Added {} globals".format(glob_count))

    print("\n" + "=" * 60)
    print("Import complete!")
    print("Total symbols added: {}".format(func_count + reg_count + glob_count))
    print("=" * 60)

# Run the script
run()
