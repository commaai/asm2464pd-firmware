/*
 * ASM2464PD Firmware - Queue Handler Functions (0xA000-0xBFFF)
 *
 * Function declarations for queue management and handler functions.
 */

#ifndef __QUEUE_HANDLERS_H__
#define __QUEUE_HANDLERS_H__

#include "types.h"

/*===========================================================================
 * Power State Helper Functions (0xaa00-0xaa35)
 *===========================================================================*/
uint8_t power_state_return_05(void);
uint8_t power_state_helper_aa02(void);
uint8_t power_state_helper_aa13(void);
uint8_t power_state_helper_aa1d(void);
uint8_t power_state_helper_aa26(void);

/*===========================================================================
 * PCIe Link State Functions (0xa2c2-0xa3da)
 *===========================================================================*/
void pcie_link_config_a2c2(void);
void pcie_set_state_a2df(uint8_t state);
void pcie_lane_write_cc_a2eb(void);
uint8_t pcie_read_link_state_a2ff(void);
void pcie_setup_lane_a308(uint8_t link_state);
void pcie_setup_lane_a310(uint8_t lane);
void pcie_lane_setup_a31c(uint8_t val);
uint8_t pcie_read_status_a334(void);
uint8_t pcie_read_status_a33d(uint8_t reg_offset);
void pcie_setup_all_lanes_a344(uint8_t val);
uint8_t pcie_get_status_a348(uint8_t val);
uint8_t pcie_get_status_a34f(void);
uint8_t pcie_modify_and_read_a358(void);
uint8_t pcie_modify_and_read_a35f(void);
void pcie_write_66_a365(void);
uint8_t pcie_get_status_a372(void);
void pcie_store_status_a37b(uint8_t status);
void pcie_setup_a38b(uint8_t source);
uint8_t pcie_check_int_source_a3c4(uint8_t source);

/*===========================================================================
 * NVMe/Command State Handler (0xaa36-0xab0c)
 *===========================================================================*/
void nvme_cmd_state_handler_aa36(void);

/*===========================================================================
 * System State Clear Function (0xbfc4)
 *===========================================================================*/
void system_state_clear_bfc4(void);

/*===========================================================================
 * USB Descriptor Buffer Helpers (0xa637-0xa660)
 *===========================================================================*/
void usb_descriptor_helper_a644(uint8_t adjustment, uint8_t offset);
void usb_descriptor_helper_a648(void);
void usb_descriptor_helper_a655(uint8_t offset, uint8_t value);

/*===========================================================================
 * Queue Index Helpers
 *===========================================================================*/
uint8_t queue_index_return_04(void);
uint8_t queue_index_return_05(void);

/*===========================================================================
 * High-Call-Count Queue Functions (0xaa09-0xaab5)
 *===========================================================================*/
uint8_t queue_helper_aa09(void);
uint8_t queue_helper_aa2b(void);
void queue_helper_aa42(void);
void queue_helper_aa4e(void);
void queue_helper_aa57(void);
void queue_helper_aa71(void);
void queue_helper_aa7d(void);
void queue_helper_aa7f(void);
void queue_helper_aa90(void);
void queue_helper_aaab(void);
void queue_helper_aaad(void);
void queue_helper_aab5(void);

/*===========================================================================
 * Buffer/DMA Support Functions (0xb6d4-0xbf9a)
 *===========================================================================*/
void buffer_helper_b6d4(void);
void buffer_helper_b6f0(void);
void buffer_helper_b6fa(void);
uint8_t queue_status_bc9f(void);
void queue_handler_bcfe(void);
void state_handler_bf9a(void);
void state_handler_bfb8(void);

/*===========================================================================
 * Queue Dispatch Functions (0xab27-0xabc8)
 *===========================================================================*/
void queue_dispatch_ab27(void);
void queue_dispatch_ab3a(void);
void queue_setup_abc9(void);

#endif /* __QUEUE_HANDLERS_H__ */
