/*
 * dma.h - DMA Engine Driver
 *
 * DMA engine control for the ASM2464PD USB4/Thunderbolt to NVMe bridge.
 * Handles high-speed data transfers between USB, NVMe, and internal buffers
 * without CPU intervention.
 *
 * ARCHITECTURE
 *   USB Host <---> USB Buffer <---> DMA Engine <---> NVMe Buffer <---> NVMe SSD
 *                      |                |
 *                      v                v
 *                 XRAM Buffers    SCSI/Mass Storage
 *
 * TRANSFER MODES
 *   - USB to Buffer: Host writes (USB RX)
 *   - Buffer to USB: Host reads (USB TX)
 *   - Buffer to NVMe: SSD writes
 *   - NVMe to Buffer: SSD reads
 *
 * DMA ENGINE CORE REGISTERS (0xC8B0-0xC8DF)
 *   0xC8B0  DMA_MODE          DMA mode configuration
 *   0xC8B2  DMA_CHAN_AUX      Channel auxiliary config (2 bytes)
 *   0xC8B4-B5 Transfer count (16-bit)
 *   0xC8B6  DMA_CHAN_CTRL2    Channel control 2
 *                             Bit 0: Start/busy
 *                             Bit 1: Direction
 *                             Bit 2: Enable
 *                             Bit 7: Active
 *   0xC8B7  DMA_CHAN_STATUS2  Channel status 2
 *   0xC8B8  DMA_TRIGGER       Trigger register (poll bit 0)
 *   0xC8D4  DMA_CONFIG        Global DMA configuration
 *   0xC8D6  DMA_STATUS        DMA status
 *                             Bit 2: Done flag
 *                             Bit 3: Error flag
 *   0xC8D8  DMA_STATUS2       DMA status 2
 *
 * SCSI/MASS STORAGE DMA REGISTERS (0xCE40-0xCE6F)
 *   0xCE40  SCSI_DMA_PARAM0   SCSI parameter 0
 *   0xCE41  SCSI_DMA_PARAM1   SCSI parameter 1
 *   0xCE42  SCSI_DMA_PARAM2   SCSI parameter 2
 *   0xCE43  SCSI_DMA_PARAM3   SCSI parameter 3
 *   0xCE5C  SCSI_DMA_COMPL    Completion status
 *                             Bit 0: Mode 0 complete
 *                             Bit 1: Mode 0x10 complete
 *   0xCE66  SCSI_DMA_TAG_CNT  Tag count (5-bit, 0-31)
 *   0xCE67  SCSI_DMA_QUEUE    Queue status (4-bit, 0-15)
 *   0xCE6E  SCSI_DMA_CTRL     SCSI DMA control register
 *   0xCE96  SCSI_DMA_STATUS   DMA status/completion flags
 *
 * WORK AREA GLOBALS (0x0200-0x07FF)
 *   0x0203  G_DMA_MODE_SELECT    Current DMA mode
 *   0x020D  G_DMA_PARAM1         Transfer parameter 1
 *   0x020E  G_DMA_PARAM2         Transfer parameter 2
 *   0x021A-1B G_BUF_BASE         Buffer base address (16-bit)
 *   0x0472-73 G_DMA_LOAD_PARAM   Load parameters
 *   0x0564  G_EP_QUEUE_CTRL      Endpoint queue control
 *   0x0565  G_EP_QUEUE_STATUS    Endpoint queue status
 *   0x07E5  G_TRANSFER_ACTIVE    Transfer active flag
 *   0x0AA3-A4 G_STATE_COUNTER    16-bit state counter
 *
 * ADDRESS SPACES
 *   0x0000-0x00FF: Endpoint queue descriptors
 *   0x0100-0x01FF: Transfer work areas
 *   0x0400-0x04FF: DMA configuration tables
 *   0x0A00-0x0AFF: SCSI buffer management
 *
 * TRANSFER SEQUENCE
 *   1. Set transfer parameters in work area (G_DMA_MODE_SELECT, etc)
 *   2. Configure channel via dma_config_channel()
 *   3. Set buffer pointers and length
 *   4. Trigger transfer via DMA_TRIGGER (write 0x01)
 *   5. Poll DMA_TRIGGER bit 0 until clear
 *   6. Check DMA_STATUS for errors
 *   7. Clear status via dma_clear_status()
 */
#ifndef _DMA_H_
#define _DMA_H_

#include "../types.h"

/* DMA control */
void dma_clear_status(void);                    /* 0x1bcb-0x1bd4 */
void dma_set_scsi_param3(void);                 /* 0x16f3-0x16fe */
void dma_set_scsi_param1(void);                 /* 0x1709-0x1712 */
uint8_t dma_reg_wait_bit(__xdata uint8_t *ptr); /* 0x1713-0x171c */
void dma_load_transfer_params(void);            /* 0x16ff-0x1708 */

/* DMA channel configuration */
void dma_config_channel(uint8_t channel, uint8_t r4_param);     /* 0x171d-0x172b */
void dma_setup_transfer(uint8_t r7_mode, uint8_t r5_param, uint8_t r3_param);  /* 0x4a57-0x4a93 */
void dma_init_channel_b8(void);                 /* 0x523c-0x525f */
void dma_init_channel_with_config(uint8_t config);              /* 0x5260-0x5283 */
void dma_config_channel_0x10(void);             /* 0x1795-0x179c */

/* DMA status */
uint8_t dma_check_scsi_status(uint8_t mode);    /* 0x17a9-0x17b4 */
void dma_clear_state_counters(void);            /* 0x17b5-0x17c0 */
void dma_init_ep_queue(void);                   /* 0x172c-0x173a */
uint8_t scsi_get_tag_count_status(void);        /* 0x173b-0x1742 */
uint8_t dma_check_state_counter(void);          /* 0x17c1-0x17cc */
uint8_t scsi_get_queue_status(void);            /* 0x17cd-0x17d7 */
uint8_t dma_shift_and_check(uint8_t val);       /* 0x4a94-0x4abe */

/* DMA transfer */
void dma_start_transfer(uint8_t aux0, uint8_t aux1, uint8_t count_hi, uint8_t count_lo);  /* 0x1787-0x178d */
void dma_set_error_flag(void);                  /* 0x1743-0x1751 */
void dma_setup_usb_rx(uint16_t len);            /* 0x1752-0x175c */
void dma_setup_usb_tx(uint16_t len);            /* 0x175d-0x176a */
void dma_wait_complete(void);                   /* 0x176b-0x1778 */

/* DMA address calculation */
uint8_t dma_get_config_offset_05a8(void);       /* 0x1779-0x1786 */
__xdata uint8_t *dma_calc_offset_0059(uint8_t offset);          /* 0x17f3-0x17fc */
__xdata uint8_t *dma_calc_addr_0478(uint8_t index);             /* 0x178e-0x1794 */
__xdata uint8_t *dma_calc_addr_0479(uint8_t index);             /* 0x179d-0x17a8 */
__xdata uint8_t *dma_calc_addr_00c2(void);      /* 0x180d-0x1819 */
__xdata uint8_t *dma_calc_ep_config_ptr(void);  /* 0x1602-0x1619 */
__xdata uint8_t *dma_calc_addr_046x(uint8_t offset);            /* 0x161a-0x1639 */
__xdata uint8_t *dma_calc_addr_0466(uint8_t offset);            /* 0x163a-0x1645 */
__xdata uint8_t *dma_calc_addr_0456(uint8_t offset);            /* 0x1646-0x1657 */
uint16_t dma_calc_addr_002c(uint8_t offset, uint8_t high);      /* 0x16ae-0x16b6 */

/* DMA SCSI operations */
uint8_t dma_shift_rrc2_mask(uint8_t val);       /* 0x16b7-0x16c2 */
void dma_store_to_0a7d(uint8_t val);            /* 0x16de-0x16e8 */
void dma_calc_scsi_index(void);                 /* 0x16e9-0x16f2 */
uint8_t dma_write_to_scsi_ce96(void);           /* 0x17d8-0x17e2 */
void dma_write_to_scsi_ce6e(void);              /* 0x17e3-0x17ec */
void dma_write_idata_to_dptr(__xdata uint8_t *ptr);             /* 0x17ed-0x17f2 */
void dma_read_0461(void);                       /* 0x17fd-0x1803 */
void dma_store_and_dispatch(uint8_t val);       /* 0x180d-0x181d */
void dma_clear_dword(__xdata uint8_t *ptr);     /* 0x173b-0x1742 */

/* Transfer functions */
uint16_t transfer_set_dptr_0464_offset(void);   /* 0x1659-0x1667 */
uint16_t transfer_calc_work43_offset(__xdata uint8_t *dptr);    /* 0x1668-0x1676 */
uint16_t transfer_calc_work53_offset(void);     /* 0x1677-0x1686 */
uint16_t transfer_get_ep_queue_addr(void);      /* 0x1687-0x1695 */
uint16_t transfer_calc_work55_offset(void);     /* 0x1696-0x16a1 */
void transfer_func_16b0(uint8_t param);         /* 0x16b0-0x16b6 */
void transfer_func_1633(uint16_t addr);         /* 0x1633-0x1639 */

/* DMA handlers */
void dma_interrupt_handler(void);               /* 0x2608-0x2809 */
void dma_transfer_handler(uint8_t param);       /* 0xce23-0xce76 */
void transfer_continuation_d996(void);          /* 0xd996-0xda8e */
void dma_poll_complete(void);                   /* 0xceab-0xcece */
void dma_buffer_store_result_e68f(void);        /* 0xe68f-0xe6fb (Bank 1) */
void dma_poll_link_ready(void);                 /* 0xe6fc-0xe725 (Bank 1) */

#endif /* _DMA_H_ */
