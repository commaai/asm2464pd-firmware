# Ghidra Python Script for AS2464 USB4/NVMe Firmware
# Imports function names and register labels from reverse engineering work
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
    # NOTE: These addresses are from ghidra.c which was generated from fw_bank0.bin
    # Use these addresses directly - they are correct for the fw_bank0.bin file
    functions = [
        # Verified functions from ghidra.c (FUN_CODE_xxxx)
        (0x0006, "func_util_0006"),
        (0x0016, "func_init_0016"),
        (0x01ea, "power_set_state"),
        (0x0206, "usb_parse_descriptor"),
        (0x0300, "nvme_util_get_status_flags"),
        (0x030a, "cmd_dispatch"),
        (0x0311, "nvme_util_get_error_flags"),
        (0x0390, "cmd_execute"),
        (0x041c, "power_check_status"),
        (0x0458, "protocol_dispatch"),
        (0x0499, "setup_data_structures"),
        (0x04da, "parse_descriptor"),
        (0x051b, "interface_ready_check"),
        (0x0520, "cmd_get_status"),
        (0x052f, "transfer_prepare"),
        (0x0570, "buffer_allocate"),
        (0x0593, "status_update"),
        (0x061a, "command_validate"),
        (0x0642, "completion_process"),

        # Flash/I2C (0x0Axx-0x0Dxx)
        (0x0ac1, "flash_func_0ac1"),
        (0x0adc, "flash_func_0adc"),
        (0x0b15, "flash_func_0b15"),
        (0x0bc8, "flash_func_0bc8"),
        (0x0be6, "phy_link_init"),
        (0x0bfd, "flash_func_0bfd"),
        (0x0c0f, "flash_func_0c0f"),
        (0x0c64, "flash_func_0c64"),
        (0x0c7a, "flash_func_0c7a"),
        (0x0d08, "flash_func_0d08"),
        (0x0d22, "reg_read_byte"),
        (0x0d33, "reg_read_word"),
        (0x0d46, "reg_read_dword"),
        (0x0d78, "reg_write_byte"),
        (0x0d84, "reg_modify_bits"),
        (0x0d90, "reg_set_bit"),
        (0x0db9, "reg_clear_bit"),
        (0x0dc5, "reg_test_bit"),
        (0x0ddd, "reg_wait_bit_set"),
        (0x0de6, "reg_wait_bit_clear"),
        (0x0def, "reg_poll"),

        # NVMe/SCSI (0x11xx-0x1Dxx)
        (0x1196, "nvme_submit_cmd"),
        (0x1567, "command_get_pending"),
        (0x1602, "transfer_func_1602"),
        (0x1607, "transfer_func_1607"),
        (0x161a, "transfer_func_161a"),
        (0x1633, "transfer_func_1633"),
        (0x16ae, "transfer_func_16ae"),
        (0x16b0, "transfer_func_16b0"),
        (0x16c3, "transfer_func_16c3"),
        (0x16cc, "transfer_func_16cc"),
        (0x16d2, "dma_complex_transfer"),
        (0x16f3, "transfer_func_16f3"),
        (0x16f6, "transfer_func_16f6"),
        (0x1709, "transfer_func_1709"),
        (0x1713, "transfer_func_1713"),
        (0x171d, "transfer_func_171d"),
        (0x173b, "command_abort"),
        (0x175d, "transfer_func_175d"),
        (0x1787, "transfer_func_1787"),
        (0x1795, "transfer_func_1795"),
        (0x17a9, "transfer_func_17a9"),
        (0x17d8, "transfer_func_17d8"),
        (0x17e3, "transfer_func_17e3"),
        (0x17ed, "transfer_func_17ed"),
        (0x17f3, "nvme_util_update_queue_ptr"),
        (0x180d, "nvme_command_handler"),

        # USB (0x1Axx-0x1Dxx)
        (0x1aad, "usb_func_1aad"),
        (0x1ad1, "usb_func_1ad1"),
        (0x1ad4, "usb_func_1ad4"),
        (0x1b14, "usb_func_1b14"),
        (0x1b20, "usb_func_1b20"),
        (0x1b23, "usb_event_handler"),
        (0x1b47, "usb_func_1b47"),
        (0x1b55, "usb_check_status"),
        (0x1b59, "usb_configure"),
        (0x1b60, "usb_func_1b60"),
        (0x1b7e, "usb_enable"),
        (0x1b9a, "ep_config_read"),
        (0x1bc3, "usb_reset_interface"),
        (0x1bcb, "usb_setup_endpoint"),
        (0x1bd7, "usb_start_transfer"),
        (0x1bde, "usb_stop_transfer"),
        (0x1be1, "usb_data_handler"),
        (0x1bf6, "usb_func_1bf6"),
        (0x1c77, "link_get_status"),
        (0x1cd4, "link_configure"),
        (0x1cdc, "link_set_speed"),
        (0x1cfc, "link_start_training"),
        (0x1d07, "link_check_training"),
        (0x1d24, "link_enable"),
        (0x1d2b, "link_disable"),

        # NVMe core (0x31xx-0x32xx)
        (0x312a, "nvme_reset_controller"),
        (0x3130, "nvme_init_step"),
        (0x3133, "nvme_check_completion"),
        (0x3147, "nvme_queue_setup"),
        (0x3189, "nvme_process_cmd"),
        (0x31ad, "nvme_io_request"),
        (0x31c5, "nvme_ring_doorbell"),
        (0x31ce, "nvme_read_status"),
        (0x31d8, "usb_get_descriptor_ptr"),
        (0x31e0, "nvme_build_cmd"),
        (0x31e2, "usb_get_descriptor_length"),
        (0x31fb, "usb_validate_descriptor"),
        (0x3223, "usb_get_endpoint_status"),
        (0x3226, "usb_get_endpoint_config"),
        (0x323b, "usb_set_endpoint_config"),
        (0x3249, "nvme_initialize"),
        (0x325f, "usb_convert_speed"),
        (0x32a5, "nvme_io_handler"),
        (0x38d4, "protocol_state_machine"),
        (0x3e81, "nvme_util_check_command_ready"),

        # DMA/Buffer (0x46xx-0x54xx)
        (0x46f8, "nvme_util_get_queue_depth"),
        (0x4784, "usb_setup_data_xfer"),
        (0x488f, "nvme_util_clear_completion"),
        (0x49e9, "nvme_util_get_phase_tag"),
        (0x4d44, "dma_transfer_handler"),
        (0x4e25, "usb_get_xfer_status"),
        (0x4ef5, "nvme_util_advance_queue"),
        (0x4ff2, "core_handler_4ff2"),
        (0x50db, "startup_init"),
        (0x523c, "usb_check_buffer_ready"),
        (0x544c, "dma_start_transfer"),

        # Bank1/PCIe (0x8xxx-0x9xxx)
        (0x8000, "bank1_entry"),
        (0x9902, "pcie_init"),
        (0x99af, "pcie_write_status"),
        (0x99e4, "pcie_read_status"),
        (0x9a0a, "pcie_trigger_transaction"),
        (0x9a30, "pcie_set_byte_enables"),
        (0x9a53, "pcie_check_link"),
        (0x9a7f, "pcie_wait_complete"),

        # High-level functions (0xAx-0xEx)
        (0xadb0, "pcie_set_address"),
        (0xae87, "pcie_check_completion"),
        (0xe529, "pcie_config_read"),
        (0xe545, "pcie_config_write"),
        (0xe57d, "pcie_memory_read"),
        (0xe5b1, "pcie_memory_write"),
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
    # These are in EXTMEM (XDATA) space
    registers = [
        # UART Controller (0xC000-0xC00F)
        (0xC000, "REG_UART_BASE"),
        (0xC001, "REG_UART_THR_RBR"),
        (0xC002, "REG_UART_IER"),
        (0xC004, "REG_UART_FCR_IIR"),
        (0xC006, "REG_UART_TFBF"),
        (0xC007, "REG_UART_LCR"),
        (0xC008, "REG_UART_MCR"),
        (0xC009, "REG_UART_LSR"),
        (0xC00A, "REG_UART_MSR"),

        # I2C Controller (0xC870-0xC87F)
        (0xC870, "REG_I2C_ADDR"),
        (0xC871, "REG_I2C_MODE"),
        (0xC873, "REG_I2C_LEN"),
        (0xC875, "REG_I2C_CSR"),
        (0xC878, "REG_I2C_SRC"),
        (0xC87C, "REG_I2C_DST"),
        (0xC87F, "REG_I2C_CSR_ALT"),

        # SPI Flash Controller (0xC89F-0xC8AE)
        (0xC89F, "REG_FLASH_CON"),
        (0xC8A1, "REG_FLASH_ADDR_LO"),
        (0xC8A2, "REG_FLASH_ADDR_MD"),
        (0xC8A3, "REG_FLASH_DATA_LEN"),
        (0xC8A6, "REG_FLASH_DIV"),
        (0xC8A9, "REG_FLASH_CSR"),
        (0xC8AA, "REG_FLASH_CMD"),
        (0xC8AB, "REG_FLASH_ADDR_HI"),
        (0xC8AC, "REG_FLASH_ADDR_LEN"),
        (0xC8AD, "REG_FLASH_MODE"),
        (0xC8AE, "REG_FLASH_BUF_OFFSET"),

        # Timer Registers (0xCC10-0xCC24)
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

        # CPU Control
        (0xCA06, "REG_CPU_MODE_NEXT"),
        (0xCC31, "REG_CPU_EXEC_CTRL"),

        # Power Management (0x92C0-0x92C8)
        (0x92C0, "REG_POWER_CTRL_92C0"),
        (0x92C1, "REG_POWER_CLOCK_CONFIG"),
        (0x92C2, "REG_POWER_STATUS"),
        (0x92C4, "REG_POWER_CTRL"),
        (0x92C5, "REG_POWER_CTRL_92C5"),
        (0x92C6, "REG_POWER_CTRL_92C6"),
        (0x92C8, "REG_POWER_CTRL_92C8"),

        # USB Interface (0x9000-0x90FF)
        (0x9000, "REG_USB_STATUS"),
        (0x9001, "REG_USB_CONTROL"),
        (0x9002, "REG_USB_CONFIG"),
        (0x9003, "REG_USB_EP0_STATUS"),
        (0x9004, "REG_USB_EP0_LEN_L"),
        (0x9005, "REG_USB_EP0_LEN_H"),
        (0x9006, "REG_USB_EP0_CONFIG"),
        (0x9007, "REG_USB_SCSI_BUF_LEN"),
        (0x9010, "REG_USB_DATA_L"),
        (0x9011, "REG_USB_DATA_H"),
        (0x9012, "REG_USB_FIFO_L"),
        (0x9013, "REG_USB_FIFO_H"),
        (0x9091, "REG_INT_FLAGS_EX0"),
        (0x911B, "REG_USB_BUFFER_ALT"),

        # Link/PHY Control (0xC200-0xC3FF)
        (0xC202, "REG_LINK_CTRL"),
        (0xC203, "REG_LINK_CONFIG"),
        (0xC204, "REG_LINK_STATUS"),
        (0xC205, "REG_PHY_CTRL"),
        (0xC233, "REG_PHY_CONFIG"),
        (0xC284, "REG_PHY_STATUS"),

        # NVMe Interface (0xC400-0xC5FF)
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

        # DMA Engine (0xC800-0xC9FF)
        (0xC8A1, "REG_DMA_CHANNEL_SEL"),
        (0xC8A2, "REG_DMA_CHANNEL_CTRL"),
        (0xC8A3, "REG_DMA_CHANNEL_STATUS"),
        (0xC8A4, "REG_DMA_LENGTH_LOW"),
        (0xC8A5, "REG_DMA_LENGTH_HIGH"),
        (0xC8A6, "REG_DMA_COUNT_LOW"),
        (0xC8A7, "REG_DMA_COUNT_HIGH"),
        (0xC8A8, "REG_DMA_PRIORITY"),
        (0xC8A9, "REG_DMA_BURST_SIZE"),
        (0xC8AA, "REG_DMA_SRC_L"),
        (0xC8AB, "REG_DMA_SRC_H"),
        (0xC8AC, "REG_DMA_DST_L"),
        (0xC8AD, "REG_DMA_DST_H"),
        (0xC8AE, "REG_DMA_LEN_L"),
        (0xC8AF, "REG_DMA_LEN_H"),
        (0xC8B0, "REG_DMA_MODE"),
        (0xC8D4, "REG_DMA_CONFIG"),
        (0xC8D6, "REG_DMA_STATUS"),

        # SCSI/Mass Storage DMA (0xCE40-0xCE6E)
        (0x0171, "REG_SCSI_CTRL"),
        (0xCE40, "REG_SCSI_DMA_PARAM0"),
        (0xCE41, "REG_SCSI_DMA_PARAM1"),
        (0xCE42, "REG_SCSI_DMA_PARAM2"),
        (0xCE43, "REG_SCSI_DMA_PARAM3"),
        (0xCE6E, "REG_SCSI_DMA_STATUS"),

        # Data Buffer (0xD800-0xD8FF)
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
        (0xD810, "REG_BUFFER_L"),
        (0xD811, "REG_BUFFER_H"),

        # PCIe Passthrough (0xB210-0xB4C8)
        (0xB210, "REG_PCIE_FMT_TYPE"),
        (0xB217, "REG_PCIE_BYTE_EN"),
        (0xB218, "REG_PCIE_ADDR_LOW"),
        (0xB21C, "REG_PCIE_ADDR_HIGH"),
        (0xB220, "REG_PCIE_DATA"),
        (0xB224, "REG_PCIE_TLP_CPL_HEADER"),
        (0xB22A, "REG_PCIE_LINK_STATUS"),
        (0xB250, "REG_PCIE_NVME_DOORBELL"),
        (0xB254, "REG_PCIE_TRIGGER"),
        (0xB255, "REG_PCIE_PM_ENTER"),
        (0xB284, "REG_PCIE_COMPL_STATUS"),
        (0xB296, "REG_PCIE_STATUS"),
        (0xB424, "REG_PCIE_LANE_COUNT"),
        (0xB4AE, "REG_PCIE_LINK_STATUS_ALT"),
        (0xB4C8, "REG_PCIE_LANE_MASK"),

        # Command Engine (0xE400-0xE4FF)
        (0xE40B, "REG_CMD_CONFIG"),
        (0xE422, "REG_CMD_OPCODE"),
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

        # System Registers
        (0x0000, "REG_SYSTEM_CTRL_0000"),
        (0x0464, "REG_SYS_STATUS_PRIMARY"),
        (0x0465, "REG_SYS_STATUS_SECONDARY"),
        (0x0466, "REG_SYSTEM_CONFIG"),
        (0x0467, "REG_SYSTEM_STATE"),
        (0x0468, "REG_DATA_PORT"),
        (0x0469, "REG_INT_STATUS"),
        (0x054B, "REG_EP_CONFIG_BASE"),
        (0x054E, "REG_EP_CONFIG_ARRAY"),
        (0x05A8, "REG_TRANSFER_SIZE"),
        (0x05B4, "REG_TRANSFER_FLAGS"),
        (0x05F8, "REG_TRANSFER_CTRL"),
        (0x07E4, "REG_SYS_FLAGS_BASE"),
        (0x07EC, "REG_SYS_FLAGS"),
        (0xEF4E, "REG_CRITICAL_CTRL"),

        # Memory Buffers
        (0x7000, "FLASH_BUFFER_BASE"),
        (0x8000, "USB_SCSI_BUF_BASE"),
        (0x9E00, "USB_CTRL_BUF_BASE"),
        (0xA000, "NVME_IOSQ_BASE"),
        (0xB000, "NVME_ASQ_BASE"),
        (0xB100, "NVME_ACQ_BASE"),
        (0xF000, "NVME_DATA_BUF_BASE"),
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

def run():
    """Main entry point"""
    print("=" * 60)
    print("AS2464 USB4/NVMe Firmware Symbol Import Script")
    print("Addresses verified against ghidra.c (from fw_bank0.bin)")
    print("=" * 60)

    print("\nAdding function names...")
    func_count = add_functions()
    print("Added {} functions".format(func_count))

    print("\nAdding register labels...")
    reg_count = add_registers()
    print("Added {} registers".format(reg_count))

    print("\n" + "=" * 60)
    print("Import complete!")
    print("Total symbols added: {}".format(func_count + reg_count))
    print("=" * 60)

# Run the script
run()
