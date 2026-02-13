# ASM2464PD Firmware Enumeration Flow

Extracted from `trace/enumerate_robbe_calls` — 33K lines, 258K instructions, 400K cycles.

---

## USB Enumeration Summary

Complete USB enumeration sequence observed in the trace:

| # | ISR | Direction | Request |
|---|-----|-----------|---------|
| 1 | INT0 #2 (L13103) | OUT | `SET_ADDRESS(addr=72)` |
| 2 | INT0 #3 (L13175) | IN | `GET_DESCRIPTOR(DEVICE, len=8)` |
| 3 | INT0 #4 (L13307) | OUT | `SET_ISOCH_DELAY(delay=40)` |
| 4 | INT0 #5 (L13360) | IN | `GET_DESCRIPTOR(DEVICE, len=18)` |
| 5 | INT0 #6 (L13528) | IN | `GET_DESCRIPTOR(BOS, len=5)` |
| 6 | INT0 #7 (L13654) | IN | `GET_DESCRIPTOR(BOS, len=172)` |
| 7 | INT0 #8 (L14625) | IN | `GET_DESCRIPTOR(CONFIGURATION, len=9)` |
| 8 | INT0 #9 (L14763) | IN | `GET_DESCRIPTOR(CONFIGURATION, len=121)` |
| 9 | INT0 #10 (L15455) | IN | `GET_DESCRIPTOR(STRING, idx=3, len=255)` |
| 10 | INT0 #12 (L15674) | IN | `GET_DESCRIPTOR(STRING, idx=2, len=255)` |
| 11 | INT0 #13 (L15820) | IN | `GET_DESCRIPTOR(STRING, idx=1, len=255)` |
| 12 | INT0 #14 (L16008) | OUT | `SET_CONFIGURATION(config=1)` |
| 13 | INT0 #15 (L16097) | OUT | `SET_INTERFACE(alt=1, iface=0)` |
| 14 | INT0 #21 (L18720) | IN | `GET_DESCRIPTOR(BOS, len=5)` |
| 15 | INT0 #22 (L18858) | IN | `GET_DESCRIPTOR(BOS, len=172)` |

---

## Minimal USB Enumeration Flow (Register-Level)

This section describes the exact minimal register sequence needed to handle USB enumeration,
distilled from the trace. There are three transfer types: **no-data** (e.g. SET_ADDRESS),
**IN data** (e.g. GET_DESCRIPTOR), and **buffer init** (the very first INT0).

### ISR Entry Sequence (common to all SETUP packets)

Every INT0 ISR begins with this exact sequence:

```
1. Read 0xC802            → USB interrupt status (always 0x01)
2. Read 0x9101            → Peripheral status, check flags:
                             0x02 = SETUP packet ready
                             0x08 = Buffer event (first INT0 only)
                             0x22 = SETUP + some other bit
3. Read 0x9101 (x3 more)  → FW reads this register 4 times total
4. (call trampoline_ss_event — internal, no visible register ops)
5. Read 0x9091 (x2)       → Control phase state
6. (enter usb3_bit0_handler)
7. Read 0x9002             → USB config register
8. Write 0x9002 = same     → Write back same value (read-modify-write)
9. Read 0x9220             → Endpoint 0 control (always 0x00)
10. Write 0x9091 = 0x01    → Start setup phase
11. Read 0x9104-0x910B     → Read all 8 setup packet bytes
12. Read 0x9091             → Check what phase hardware is ready for
```

After step 12, the ISR branches based on the setup packet type.

### ISR Exit Sequence (common to all transfers)

After handling the request, every ISR ends with:

```
1. Read 0x9002             → USB config
2. Read 0x9091 (x4)        → Poll until status phase ready (0x10)
3. (call usb3_endpoint_setup)
4. Write 0x9092 = 0x08     → DMA complete signal
5. Write 0x9091 = 0x10     → Ack status phase
```

### Transfer Type A: No-Data OUT (SET_ADDRESS, SET_ISOCH_DELAY, SET_CONFIGURATION)

These have **no data phase** — go straight from setup to status.

After ISR entry, 0x9091 reads back `0x10` or `0x11` (status phase already ready):

```
ISR Entry (steps 1-12 above)
  → 0x9091 reads 0x11 (first time, both setup+status bits)
     or 0x9091 reads 0x10 (status ready)

--- Request-specific handling ---
  (SET_ADDRESS: writes 0x9090=0x48, 0x91D0=0x02, checks 0x9100, 0x92F8)
  (SET_ISOCH_DELAY: no register ops, just a stub)
  (SET_CONFIGURATION: writes D800-D803, 901A, 9006, 9094, 90E3, etc.)
--- End request-specific ---

ISR Exit (steps 1-5 above)
  Read 0x9091 x4 → all return 0x10
  Write 0x9092 = 0x08     ← DMA complete
  Write 0x9091 = 0x10     ← Ack status
```

**SET_ADDRESS concrete example** (INT0 #2, trace line 13103):
```
Read  0xC802 = 0x01
Read  0x9101 = 0x02 (x4)
Read  0x9091 = 0x11 (x2)           ← setup + status already ready
Read  0x9002 = 0x20 → Write 0x9002 = 0x20
Read  0x9220 = 0x00
Write 0x9091 = 0x01                ← start setup phase
Read  0x9104..0x910B = 00 05 48 00 00 00 00 00  (SET_ADDRESS addr=0x48)
Read  0x9091 = 0x10                ← status phase ready
--- SET_ADDRESS specific ---
Write 0x9090 = 0x48                ← USB interrupt mask
Write 0x91D0 = 0x02                ← EP control
Read  0x9100 = 0x02                ← link status (USB3)
Read  0x92F8 = 0x00 (x2)
--- end specific ---
Read  0x9091 = 0x10 (x4)           ← confirm status ready
Write 0x9092 = 0x08                ← DMA complete
Write 0x9091 = 0x10                ← ack status phase
```

### Transfer Type B: IN Data (GET_DESCRIPTOR)

These have a **data phase** — firmware must send descriptor data to the host.

After ISR entry, 0x9091 reads back `0x08` (data phase ready):

```
ISR Entry (steps 1-12 above)
  → 0x9091 reads 0x01 (first time, only setup bit)
     then 0x9091 reads 0x08 after Write 0x9091=0x01 (data phase ready)

--- Build descriptor in 0x9E00+ buffer ---
  (call usb_descriptor_state_machine to populate 0x9E00..0x9Exx)
--- End descriptor build ---

--- Data phase ---
Read  0x9091 (x3)                  ← confirm 0x08 (data phase)
Write 0x9003 = 0x00                ← EP0 status = 0
Write 0x9004 = <length>            ← EP0 transfer length
Write 0x9092 = 0x04                ← DMA send (triggers data to host)
Read  0x9092 → 0x00                ← DMA send complete (immediate)
Read  0x9003 = 0x00                ← verify EP0 status
Read  0x9004 = <length>            ← verify transfer length
Read  0x9003, 0x9004               ← read again (FW does this twice)
Write 0x9091 = 0x08                ← ack data phase
--- End data phase ---

--- Status phase ---
Read  0x9091 → 0x10                ← status phase ready
Write 0x9092 = 0x08                ← DMA complete
Write 0x9091 = 0x10                ← ack status phase
```

**GET_DESCRIPTOR(DEVICE, len=8) concrete example** (INT0 #3, trace line 13175):
```
Read  0xC802 = 0x01
Read  0x9101 = 0x02 (x4)
Read  0x9091 = 0x01 (x2)           ← only setup bit, no status yet
Read  0x9002 = 0x20 → Write 0x9002 = 0x20
Read  0x9220 = 0x00
Write 0x9091 = 0x01                ← start setup phase
Read  0x9104..0x910B = 80 06 00 01 00 00 08 00  (GET_DESC DEVICE len=8)
Read  0x9091 = 0x08                ← data phase ready!
--- build descriptor ---
Write 0x9E00 = 0x12  (bLength=18)
Write 0x9E01 = 0x01  (bDescType=DEVICE)
Write 0x9E02..0x9E07               (rest of device descriptor header)
  FW then patches: 0x9E02=0x20, 0x9E03=0x03, 0x9E07=0x09 (USB3 speed fixup)
--- send data ---
Read  0x9091 = 0x08 (x3)           ← confirm data phase
Write 0x9003 = 0x00                ← EP0 status
Write 0x9004 = 0x08                ← length = 8 bytes
Write 0x9092 = 0x04                ← DMA SEND
Read  0x9092 = 0x00                ← send complete
Read  0x9003 = 0x00, 0x9004 = 0x08 ← verify (x2)
Write 0x9091 = 0x08                ← ack data phase
--- status ---
Read  0x9091 = 0x10                ← status phase ready
Write 0x9092 = 0x08                ← DMA complete
Write 0x9091 = 0x10                ← ack status phase
```

### Transfer Type C: First INT0 — Buffer/Link Init

The very first INT0 (before any SETUP packets) has `0x9101 = 0x08` (BUFFER flag, not SETUP).
This performs USB link initialization:

```
Read  0xC802 = 0x01
Read  0x9101 = 0x08 (x2)           ← BUFFER flag, not SETUP
Read  0x9301 = 0x80 (x2)           ← buffer config
Write 0x9301 = 0x80                ← write back
Read  0x92E0 = 0x00
Write 0x92E0 = 0x02                ← power domain enable
(call thunk_usb_state_reset)
(call usb_clear_pending_state)
Read  0xC6A8 → Write 0xC6A8       ← PHY config
Read  0x92C8 → Write 0x92C8 (x2)  ← power control
Write 0xCD31 = 0x04, 0x02          ← timer setup
(call usb_power_setup)
(call usb_config_cc22_cc24_cc25)
(call usb_cc1c_cc5c_timer_config)
--- MSC init ---
Write 0xD800-0xD803 = 55 53 42 53 ← "USBS" magic
Write 0x901A = 0x0D                ← MSC length
Write 0xC42C = 0x01                ← MSC control
Read  0xC42D → Write 0xC42D       ← MSC status clear
```

### Complete Enumeration Sequence (bird's-eye view)

```
BOOT (183K cycles)
  └─ Hardware init: PHY, timers, link training, memory config

INT1: Power Event
  └─ Read 0xC806=0x01 → print "[1 sec time out]" → link state update

INT0 #1: Buffer Init (0x9101=0x08)
  └─ USB power domain, timers, MSC init, "USBS" magic

INT0 #2: SET_ADDRESS(72)                    ← no data phase
  └─ Setup → 0x9090=0x48, 0x91D0=0x02 → Status → Done

INT0 #3: GET_DESCRIPTOR(DEVICE, len=8)      ← data phase (8 bytes)
  └─ Setup → Build desc → DMA send 8B → Status → Done

INT0 #4: SET_ISOCH_DELAY(40)                ← no data phase
  └─ Setup → (stub handler) → Status → Done

INT0 #5: GET_DESCRIPTOR(DEVICE, len=18)     ← data phase (18 bytes)
  └─ Setup → Build full desc → DMA send 18B → Status → Done

INT0 #6: GET_DESCRIPTOR(BOS, len=5)         ← data phase (5 bytes)
INT0 #7: GET_DESCRIPTOR(BOS, len=172)       ← data phase (172 bytes)
INT0 #8: GET_DESCRIPTOR(CONFIG, len=9)      ← data phase (9 bytes)
INT0 #9: GET_DESCRIPTOR(CONFIG, len=121)    ← data phase (121 bytes)
INT0 #10: GET_DESCRIPTOR(STRING idx=3)      ← data phase (multi-packet!)
  └─ Two DMA send/complete cycles (string split across 2 packets)
INT0 #12: GET_DESCRIPTOR(STRING idx=2)
INT0 #13: GET_DESCRIPTOR(STRING idx=1)

INT0 #14: SET_CONFIGURATION(1)              ← no data phase
  └─ Setup → D800="USBS", EP0 NAK, bulk EP clear → Status → Done

INT0 #15: SET_INTERFACE(alt=1, iface=0)     ← no data phase
  └─ Setup → Full endpoint/PHY reconfiguration → Status → Done

=== Device is now enumerated and configured ===

INT0 #16-20: Post-config command handling & disconnect checks
INT0 #21-22: Second BOS descriptor fetch (host re-requests after config)

MAIN LOOP: Polls link status, bulk transfers, PHY state
```

### Key Patterns

1. **0x9091 is the state machine** — Read it to know what phase HW is in, write it to ack that phase
2. **0x9092 = 0x04 sends data** — Write descriptor to 0x9E00+, set length in 0x9004, then trigger
3. **0x9092 = 0x08 signals transfer done** — Always written at the end of every transfer type
4. **0x9101 tells you what happened** — 0x02=SETUP packet, 0x08=buffer event
5. **Read 0x9002/Write 0x9002** — Always done before reading setup packet (config register latch?)
6. **Read 0x9220** — Always done before writing 0x9091=0x01 (endpoint 0 state check)
7. **Multi-read pattern** — FW reads 0x9101 x4 and 0x9091 x2 every time (polling/debounce?)

---

## Phase 1: Boot & Hardware Init

~183K cycles of hardware initialization before any interrupts fire.

### Init Call Sequence (grouped by timing)

**Block 1** (cycles 773, lines 15-15, 1 calls):
  - bank_select_reset()

**Block 2** (cycles 26371-183138, lines 17-11574, 4534 calls):
  - set_bit0() x6
  - thunk_usb_link_training_sequence()
  - usb_phy_cc3x_config_sequence()
  - get_e710_high_3bits()
  - write_byte_set_e717_bit0()
  - ... (275 more functions) ...
  - pcie_lane_mode_config() x42
  - get_e716_low2bits() x124
  - thunk_usb_phy_state_monitor() x41
  - usb_get_92f7_high_nibble() x82
  - usb_bulk_transfer_poll() x41

### Key Register Writes During Init

- `0xC801 = 0x10` REG_INT_ENABLE (L2211)
- `0xC800 = 0x04` REG_INT_STATUS_C800 (L2213)
- `0xC800 = 0x05` REG_INT_STATUS_C800 (L2220)
- `0xC801 = 0x50` REG_INT_ENABLE (L4584)
- `0x9091 = 0x1F` REG_USB_CTRL_PHASE (L6410)
- `0x9002 = 0xE0` REG_USB_CONFIG (L6416)
- `0xC801 = 0x50` REG_INT_ENABLE (L6666)
- `0xC809 = 0x08` REG_INT_CTRL (L6698)
- `0xC809 = 0x0A` REG_INT_CTRL (L6717)
- `0xC809 = 0x0A` REG_INT_CTRL (L8194)
- `0xC809 = 0x2A` REG_INT_CTRL (L9894)
- `0xC801 = 0x50` REG_INT_ENABLE (L9985)
- `0xC801 = 0x50` REG_INT_ENABLE (L9999)
- `0xC801 = 0x50` REG_INT_ENABLE (L10073)

---

## Phase 2: INT1 — Power/Link Event

### INT1 #1 (line 11577)

Reads:
  - `0xC806 = 0x01` REG_INT_SYSTEM

Functions called:
  - `thunk_usb_phy_state_monitor()`
  - `thunk_int1_pcie_power_event_handler()`
  - `uart_print()`
  - `get_memory()`
  - `usb3_link_state_update()`

---

## Phase 3: INT0 — USB Enumeration

Each INT0 handles one or more USB control transfer phases.

### INT0 #1 (line 12930)

*(No setup packet read — continuation of previous transfer)*

**Peripheral Status (0x9101):** 0x08
  Flags: BUFFER

**USB Config Writes:** 0x92E0=0x02, 0x92C8=0x24, 0x92C8=0x24

**Functions:**
  - `thunk_usb_phy_state_monitor()`
  - `thunk_usb_state_reset()`
  - `usb_clear_pending_state()`
  - `usb_set_c6a8_bit0()`
  - `usb_power_setup()`
  - `usb_config_cc22_cc24_cc25()`
  - `usb_cc1c_cc5c_timer_config()`
  - `usb_power_state_clear()`
  - ... and 5 more


### INT0 #2 (line 13103)

**Setup Packet:** `SET_ADDRESS(addr=72)` (OUT)
  - Raw: bmReq=0x00 bReq=0x05 wVal=0x0048 wIdx=0x0000 wLen=0

**Peripheral Status (0x9101):** 0x02
  Flags: SETUP

**Control Phase (0x9091):** Read 0x11 x2 → **Write** 0x01 → Read 0x10 x5 → **Write** 0x10

**DMA Trigger (0x9092):** **Write** 0x08 (DMA complete)

**Functions:** `trampoline_ss_event()`, `usb3_bit0_handler()`, `usb_dispatch_return()`, `usb_link_state_machine()`, `usb_speed_check_handler()`, `usb_get_speed_bank1()`, `usb3_endpoint_setup()`


### INT0 #3 (line 13175)

**Setup Packet:** `GET_DESCRIPTOR(DEVICE, len=8)` (IN)
  - Raw: bmReq=0x80 bReq=0x06 wVal=0x0100 wIdx=0x0000 wLen=8

**Peripheral Status (0x9101):** 0x02
  Flags: SETUP

**Control Phase (0x9091):** Read 0x01 x2 → **Write** 0x01 → Read 0x08 x4 → **Write** 0x08 → Read 0x10 → **Write** 0x10

**DMA Trigger (0x9092):** **Write** 0x04 (DMA send) → Read 0x00 (done) → **Write** 0x08 (DMA complete)

**EP0 (0x9003/9004):** Write 0x9003=0x00, Write 0x9004=0x08, Read 0x9003=0x00, Read 0x9004=0x08, Read 0x9003=0x00, Read 0x9004=0x08

**Descriptor Buffer (0x9E00-0x9E07):** `12 01 10 02 00 00 00 40 20 03 09`
  → DEVICE descriptor (18 bytes)

**Functions:**
  - `thunk_usb_phy_state_monitor()`
  - `trampoline_ss_event()`
  - `usb3_bit0_handler()`
  - `usb_dispatch_return()`
  - `usb_descriptor_state_machine()`
  - `clear_usb_remain_lo()`
  - `desc_write_byte_set_state()`
  - `desc_copy_to_ctrl_buffer()`
  - ... and 8 more


### INT0 #4 (line 13307)

**Setup Packet:** `SET_ISOCH_DELAY(delay=40)` (OUT)
  - Raw: bmReq=0x00 bReq=0x31 wVal=0x0028 wIdx=0x0000 wLen=0

**Peripheral Status (0x9101):** 0x02
  Flags: SETUP

**Control Phase (0x9091):** Read 0x01 x2 → **Write** 0x01 → Read 0x10 x5 → **Write** 0x10

**DMA Trigger (0x9092):** **Write** 0x08 (DMA complete)

**Functions:** `usb_bulk_transfer_poll()`, `trampoline_ss_event()`, `usb3_bit0_handler()`, `usb_dispatch_return()`, `empty_stub_e966()`, `usb3_endpoint_setup()`


### INT0 #5 (line 13360)

**Setup Packet:** `GET_DESCRIPTOR(DEVICE, len=18)` (IN)
  - Raw: bmReq=0x80 bReq=0x06 wVal=0x0100 wIdx=0x0000 wLen=18

**Peripheral Status (0x9101):** 0x02
  Flags: SETUP

**Control Phase (0x9091):** Read 0x01 x2 → **Write** 0x01 → Read 0x08 x4 → **Write** 0x08 → Read 0x10 → **Write** 0x10

**DMA Trigger (0x9092):** **Write** 0x04 (DMA send) → Read 0x00 (done) → **Write** 0x08 (DMA complete)

**EP0 (0x9003/9004):** Write 0x9003=0x00, Write 0x9004=0x12, Read 0x9003=0x00, Read 0x9004=0x12, Read 0x9003=0x00, Read 0x9004=0x12

**Descriptor Buffer (0x9E00-0x9E07):** `12 01 10 02 00 00 00 40 4C 17 63 24 01 00 02 03 01 01 20 03 09`
  → DEVICE descriptor (18 bytes)

**Functions:**
  - `trampoline_ss_event()`
  - `usb3_bit0_handler()`
  - `usb_dispatch_return()`
  - `usb_descriptor_state_machine()`
  - `clear_usb_remain_lo()`
  - `desc_write_byte_set_state()`
  - `desc_copy_to_ctrl_buffer()`
  - `usb_get_speed_bank1()`
  - ... and 7 more


### INT0 #6 (line 13528)

**Setup Packet:** `GET_DESCRIPTOR(BOS, len=5)` (IN)
  - Raw: bmReq=0x80 bReq=0x06 wVal=0x0F00 wIdx=0x0000 wLen=5

**Peripheral Status (0x9101):** 0x02
  Flags: SETUP

**Control Phase (0x9091):** Read 0x01 x2 → **Write** 0x01 → Read 0x08 x4 → **Write** 0x08 → Read 0x10 → **Write** 0x10

**DMA Trigger (0x9092):** **Write** 0x04 (DMA send) → Read 0x00 (done) → **Write** 0x08 (DMA complete)

**EP0 (0x9003/9004):** Write 0x9003=0x00, Write 0x9004=0x05, Read 0x9003=0x00, Read 0x9004=0x05, Read 0x9003=0x00, Read 0x9004=0x05

**Descriptor Buffer (0x9E00-0x9E70):** `05 0F AC 00 09 00 03 03 03 03 03 4C 17 05`
  → BOS descriptor (5 bytes)

**Functions:**
  - `thunk_usb_link_state_poll()`
  - `trampoline_ss_event()`
  - `usb3_bit0_handler()`
  - `usb_dispatch_return()`
  - `usb_descriptor_state_machine()`
  - `desc_init_state()`
  - `b1_usb_desc_offset_copy()`
  - `desc_copy_to_ctrl_buffer()`
  - ... and 10 more


### INT0 #7 (line 13654)

**Setup Packet:** `GET_DESCRIPTOR(BOS, len=172)` (IN)
  - Raw: bmReq=0x80 bReq=0x06 wVal=0x0F00 wIdx=0x0000 wLen=172

**Peripheral Status (0x9101):** 0x02
  Flags: SETUP

**Control Phase (0x9091):** Read 0x01 x2 → **Write** 0x01 → Read 0x08 x4 → **Write** 0x08 → Read 0x10 → **Write** 0x10

**DMA Trigger (0x9092):** **Write** 0x04 (DMA send) → Read 0x00 (done) → **Write** 0x08 (DMA complete)

**EP0 (0x9003/9004):** Write 0x9003=0x00, Write 0x9004=0xAC, Read 0x9003=0x00, Read 0x9004=0xAC, Read 0x9003=0x00, Read 0x9004=0xAC

**Descriptor Buffer (0x9E00-0x9E70):** `05 0F AC 00 09 07 10 02 1E F4 00 00 0A 10 03 00 0E 00 01 0A FF 07 14 10 04 00 31 30 30 30 30 30 30 30 00 00 00 00 00 00 00 00 12 10 06 00 54 40 00 00 00 00 01 00 00 00 10 03 10 02 18 10 08 00 06 00 64 00 64 00 00 00 DC 05 00 00 DC 05 00 00 FF FF FF FF 14 10 0A 00 01 00 00 00 00 11 00 00 30 40 0A 00 B0 40 0A 00 34 10 0D 10 02 00 00 80 0F 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 22 01 00 00 00 FF 00 11 87 80 00 12 08 10 0F 00 00 00 00 00 08 10 0F 01 00 00 00 00 00 03 03 03 03 03 4C 17 05`
  → BOS descriptor (5 bytes)

**Functions:**
  - `trampoline_ss_event()`
  - `usb3_bit0_handler()`
  - `usb_dispatch_return()`
  - `usb_descriptor_state_machine()`
  - `desc_init_state()`
  - `b1_usb_desc_offset_copy()`
  - `desc_copy_to_ctrl_buffer()`
  - `usb_get_speed_bank1()`
  - ... and 9 more


### INT0 #8 (line 14625)

**Setup Packet:** `GET_DESCRIPTOR(CONFIGURATION, len=9)` (IN)
  - Raw: bmReq=0x80 bReq=0x06 wVal=0x0200 wIdx=0x0000 wLen=9

**Peripheral Status (0x9101):** 0x02
  Flags: SETUP

**Control Phase (0x9091):** Read 0x01 x2 → **Write** 0x01 → Read 0x08 x4 → **Write** 0x08 → Read 0x10 → **Write** 0x10

**DMA Trigger (0x9092):** **Write** 0x04 (DMA send) → Read 0x00 (done) → **Write** 0x08 (DMA complete)

**EP0 (0x9003/9004):** Write 0x9003=0x00, Write 0x9004=0x09, Read 0x9003=0x00, Read 0x9004=0x09, Read 0x9003=0x00, Read 0x9004=0x09

**Descriptor Buffer (0x9E00-0x9E02):** `09 02 2C 00 01 01 00 C0 00 79`
  → CONFIGURATION descriptor (9 bytes)

**Functions:**
  - `thunk_usb_phy_state_monitor()`
  - `trampoline_ss_event()`
  - `usb3_bit0_handler()`
  - `usb_dispatch_return()`
  - `usb_descriptor_state_machine()`
  - `usb_get_speed_bank1()`
  - `b1_set_0ad7_clear_remain()`
  - `desc_write_byte_set_state()`
  - ... and 9 more


### INT0 #9 (line 14763)

**Setup Packet:** `GET_DESCRIPTOR(CONFIGURATION, len=121)` (IN)
  - Raw: bmReq=0x80 bReq=0x06 wVal=0x0200 wIdx=0x0000 wLen=121

**Peripheral Status (0x9101):** 0x02
  Flags: SETUP

**Control Phase (0x9091):** Read 0x01 x2 → **Write** 0x01 → Read 0x08 x4 → **Write** 0x08 → Read 0x10 → **Write** 0x10

**DMA Trigger (0x9092):** **Write** 0x04 (DMA send) → Read 0x00 (done) → **Write** 0x08 (DMA complete)

**EP0 (0x9003/9004):** Write 0x9003=0x00, Write 0x9004=0x79, Read 0x9003=0x00, Read 0x9004=0x79, Read 0x9003=0x00, Read 0x9004=0x79

**Descriptor Buffer (0x9E00-0x9E02):** `09 02 2C 00 01 01 00 C0 00 09 04 00 00 02 08 06 50 00 07 05 81 02 00 04 00 06 30 0F 00 00 00 07 05 02 02 00 04 00 06 30 0F 00 00 00 09 04 00 01 04 08 06 62 00 07 05 81 02 00 04 00 06 30 0F 05 00 00 04 24 03 00 07 05 02 02 00 04 00 06 30 0F 05 00 00 04 24 04 00 07 05 83 02 00 04 00 06 30 0F 05 00 00 04 24 02 00 07 05 04 02 00 04 00 06 30 00 00 00 00 04 24 01 00 79`
  → CONFIGURATION descriptor (9 bytes)

**Functions:**
  - `usb_bulk_transfer_poll()`
  - `trampoline_ss_event()`
  - `usb3_bit0_handler()`
  - `usb_dispatch_return()`
  - `usb_descriptor_state_machine()`
  - `usb_get_speed_bank1()`
  - `b1_set_0ad7_clear_remain()`
  - `desc_write_byte_set_state()`
  - ... and 9 more


### INT0 #10 (line 15455)

**Setup Packet:** `GET_DESCRIPTOR(STRING, idx=3, len=255)` (IN)
  - Raw: bmReq=0x80 bReq=0x06 wVal=0x0303 wIdx=0x0409 wLen=255

**Peripheral Status (0x9101):** 0x02
  Flags: SETUP

**Control Phase (0x9091):** Read 0x01 x2 → **Write** 0x01 → Read 0x08 x4 → **Write** 0x08 → Read 0x10 → **Write** 0x10 → Read 0x01 x2 → **Write** 0x01 → Read 0x08 x4 → **Write** 0x08 → Read 0x10 → **Write** 0x10

**DMA Trigger (0x9092):** **Write** 0x04 (DMA send) → Read 0x00 (done) → **Write** 0x08 (DMA complete) → **Write** 0x04 (DMA send) → Read 0x00 (done) → **Write** 0x08 (DMA complete)

**EP0 (0x9003/9004):** Write 0x9003=0x00, Write 0x9004=0x04, Read 0x9003=0x00, Read 0x9004=0x04, Read 0x9003=0x00, Read 0x9004=0x04, Write 0x9003=0x00, Write 0x9004=0x0E, Read 0x9003=0x00, Read 0x9004=0x0E, Read 0x9003=0x00, Read 0x9004=0x0E

**Descriptor Buffer (0x9E00-0x9E01):** `04 03 09 04 41 00 53 00 32 00 34 00 36 00 32 00 0E 03`
  → STRING descriptor (4 bytes)

**Functions:**
  - `trampoline_ss_event()`
  - `usb3_bit0_handler()`
  - `usb_dispatch_return()`
  - `usb_descriptor_state_machine()`
  - `desc_init_state()`
  - `desc_copy_to_ctrl_buffer()`
  - `usb_get_speed_bank1()`
  - `b1_add_0ae0_carry()`
  - ... and 11 more


### INT0 #12 (line 15674)

**Setup Packet:** `GET_DESCRIPTOR(STRING, idx=2, len=255)` (IN)
  - Raw: bmReq=0x80 bReq=0x06 wVal=0x0302 wIdx=0x0409 wLen=255

**Peripheral Status (0x9101):** 0x02
  Flags: SETUP

**Control Phase (0x9091):** Read 0x01 x2 → **Write** 0x01 → Read 0x08 x4 → **Write** 0x08 → Read 0x10 → **Write** 0x10

**DMA Trigger (0x9092):** **Write** 0x04 (DMA send) → Read 0x00 (done) → **Write** 0x08 (DMA complete)

**EP0 (0x9003/9004):** Write 0x9003=0x00, Write 0x9004=0x10, Read 0x9003=0x00, Read 0x9004=0x10, Read 0x9003=0x00, Read 0x9004=0x10

**Descriptor Buffer (0x9E02-0x9E01):** `41 00 73 00 6D 00 65 00 64 00 69 00 61 00 10 03`

**Functions:**
  - `trampoline_ss_event()`
  - `usb3_bit0_handler()`
  - `usb_dispatch_return()`
  - `usb_descriptor_state_machine()`
  - `usb_ctrl_buffer_init_loop()`
  - `get_desc_page_from_extmem()`
  - `get_usb_ctrl_buffer_page()`
  - `clear_usb_remain_lo()`
  - ... and 8 more


### INT0 #13 (line 15820)

**Setup Packet:** `GET_DESCRIPTOR(STRING, idx=1, len=255)` (IN)
  - Raw: bmReq=0x80 bReq=0x06 wVal=0x0301 wIdx=0x0409 wLen=255

**Peripheral Status (0x9101):** 0x02
  Flags: SETUP

**Control Phase (0x9091):** Read 0x01 x2 → **Write** 0x01 → Read 0x08 x4 → **Write** 0x08 → Read 0x10 → **Write** 0x10

**DMA Trigger (0x9092):** **Write** 0x04 (DMA send) → Read 0x00 (done) → **Write** 0x08 (DMA complete)

**EP0 (0x9003/9004):** Write 0x9003=0x00, Write 0x9004=0x1A, Read 0x9003=0x00, Read 0x9004=0x1A, Read 0x9003=0x00, Read 0x9004=0x1A

**Descriptor Buffer (0x9E02-0x9E01):** `30 00 30 00 30 00 30 00 30 00 30 00 30 00 30 00 30 00 30 00 30 00 30 00 1A 03`

**Functions:**
  - `thunk_usb_phy_state_monitor()`
  - `trampoline_ss_event()`
  - `usb3_bit0_handler()`
  - `usb_dispatch_return()`
  - `usb_descriptor_state_machine()`
  - `usb_ctrl_buffer_init_loop()`
  - `get_desc_page_from_extmem()`
  - `get_usb_ctrl_buffer_page()`
  - ... and 8 more


### INT0 #14 (line 16008)

**Setup Packet:** `SET_CONFIGURATION(config=1)` (OUT)
  - Raw: bmReq=0x00 bReq=0x09 wVal=0x0001 wIdx=0x0000 wLen=0

**Peripheral Status (0x9101):** 0x02
  Flags: SETUP

**Control Phase (0x9091):** Read 0x01 x2 → **Write** 0x01 → Read 0x10 x5 → **Write** 0x10

**DMA Trigger (0x9092):** **Write** 0x08 (DMA complete)

**USB Config Writes:** 0x924C=0x04

**Functions:**
  - `usb_bulk_transfer_poll()`
  - `trampoline_ss_event()`
  - `usb3_bit0_handler()`
  - `usb_dispatch_return()`
  - `usb_state_machine_handler()`
  - `write_4bytes_to_ptr_2()`
  - `usb_ep0_clear_and_nak()`
  - `usb_clear_bulk_int()`
  - ... and 4 more


### INT0 #15 (line 16097)

**Setup Packet:** `SET_INTERFACE(alt=1, iface=0)` (OUT)
  - Raw: bmReq=0x01 bReq=0x0B wVal=0x0001 wIdx=0x0000 wLen=0

**Peripheral Status (0x9101):** 0x02
  Flags: SETUP

**Control Phase (0x9091):** Read 0x01 x2 → **Write** 0x01 → Read 0x10 x5 → **Write** 0x10

**DMA Trigger (0x9092):** **Write** 0x08 (DMA complete)

**USB Config Writes:** 0x924C=0x04, 0x924C=0x05, 0x9200=0xF1, 0x9200=0xB1

**Functions:**
  - `trampoline_ss_event()`
  - `usb3_bit0_handler()`
  - `usb_dispatch_return()`
  - `usb_get_speed_bank1()`
  - `usb_reset_handler()`
  - `usb_phy_init()`
  - `usb_endpoint_state_clear()`
  - `usb_phy_c473_config()`
  - ... and 18 more


### INT0 #16 (line 18002)

*(No setup packet read — continuation of previous transfer)*

**USB Config Writes:** 0x924C=0x04

**Functions:**
  - `thunk_usb_phy_state_monitor()`
  - `usb_int0_cmd_handler()`
  - `usb_handle_command()`
  - `get_page_ff_by_param()`
  - `get_carry_negated()`
  - `get_carry_as_ff()`
  - `usb_copy_ceb2_ceb3_to_extmem()`
  - `usb_clear_slot_entry()`
  - ... and 23 more


### INT0 #17 (line 18171)

*(No setup packet read — continuation of previous transfer)*

**Peripheral Status (0x9101):** 0x22
  Flags: SETUP

**Functions:** `usb_bulk_transfer_poll()`, `usb_check_addr_disconnect()`, `usb_disconnect_handler()`


### INT0 #18 (line 18349)

*(No setup packet read — continuation of previous transfer)*

**Peripheral Status (0x9101):** 0x22
  Flags: SETUP

**Functions:** `usb_check_addr_disconnect()`, `usb_disconnect_handler()`


### INT0 #19 (line 18517)

*(No setup packet read — continuation of previous transfer)*

**Peripheral Status (0x9101):** 0x22
  Flags: SETUP

**Functions:** `thunk_usb_link_state_poll()`, `usb_check_addr_disconnect()`, `usb_disconnect_handler()`


### INT0 #20 (line 18687)

*(No setup packet read — continuation of previous transfer)*

**Peripheral Status (0x9101):** 0x22
  Flags: SETUP

**Functions:** `usb_check_addr_disconnect()`, `usb_disconnect_handler()`


### INT0 #21 (line 18720)

**Setup Packet:** `GET_DESCRIPTOR(BOS, len=5)` (IN)
  - Raw: bmReq=0x80 bReq=0x06 wVal=0x0F00 wIdx=0x0000 wLen=5

**Peripheral Status (0x9101):** 0x02
  Flags: SETUP

**Control Phase (0x9091):** Read 0x01 x2 → **Write** 0x01 → Read 0x08 x4 → **Write** 0x08 → Read 0x10 → **Write** 0x10

**DMA Trigger (0x9092):** **Write** 0x04 (DMA send) → Read 0x00 (done) → **Write** 0x08 (DMA complete)

**EP0 (0x9003/9004):** Write 0x9003=0x00, Write 0x9004=0x05, Read 0x9003=0x00, Read 0x9004=0x05, Read 0x9003=0x00, Read 0x9004=0x05

**Descriptor Buffer (0x9E00-0x9E70):** `05 0F AC 00 09 00 03 03 03 03 03 4C 17 05`
  → BOS descriptor (5 bytes)

**Functions:**
  - `thunk_usb_phy_state_monitor()`
  - `trampoline_ss_event()`
  - `usb3_bit0_handler()`
  - `usb_dispatch_return()`
  - `usb_descriptor_state_machine()`
  - `desc_init_state()`
  - `b1_usb_desc_offset_copy()`
  - `desc_copy_to_ctrl_buffer()`
  - ... and 10 more


### INT0 #22 (line 18858)

**Setup Packet:** `GET_DESCRIPTOR(BOS, len=172)` (IN)
  - Raw: bmReq=0x80 bReq=0x06 wVal=0x0F00 wIdx=0x0000 wLen=172

**Peripheral Status (0x9101):** 0x02
  Flags: SETUP

**Control Phase (0x9091):** Read 0x01 x2 → **Write** 0x01 → Read 0x08 x4 → **Write** 0x08 → Read 0x10 → **Write** 0x10

**DMA Trigger (0x9092):** **Write** 0x04 (DMA send) → Read 0x00 (done) → **Write** 0x08 (DMA complete)

**EP0 (0x9003/9004):** Write 0x9003=0x00, Write 0x9004=0xAC, Read 0x9003=0x00, Read 0x9004=0xAC, Read 0x9003=0x00, Read 0x9004=0xAC

**Descriptor Buffer (0x9E00-0x9E70):** `05 0F AC 00 09 07 10 02 1E F4 00 00 0A 10 03 00 0E 00 01 0A FF 07 14 10 04 00 31 30 30 30 30 30 30 30 00 00 00 00 00 00 00 00 12 10 06 00 54 40 00 00 00 00 01 00 00 00 10 03 10 02 18 10 08 00 06 00 64 00 64 00 00 00 DC 05 00 00 DC 05 00 00 FF FF FF FF 14 10 0A 00 01 00 00 00 00 11 00 00 30 40 0A 00 B0 40 0A 00 34 10 0D 10 02 00 00 80 0F 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 22 01 00 00 00 FF 00 11 87 80 00 12 08 10 0F 00 00 00 00 00 08 10 0F 01 00 00 00 00 00 03 03 03 03 03 4C 17 05`
  → BOS descriptor (5 bytes)

**Functions:**
  - `usb_bulk_transfer_poll()`
  - `trampoline_ss_event()`
  - `usb3_bit0_handler()`
  - `usb_dispatch_return()`
  - `usb_descriptor_state_machine()`
  - `desc_init_state()`
  - `b1_usb_desc_offset_copy()`
  - `desc_copy_to_ctrl_buffer()`
  - ... and 10 more


---

## Phase 4: Main Loop

Between interrupts, the firmware polls in a main loop:

- `usb_slot_handler_decrement_addr_count()` @ 0x0006
- `thunk_usb_connection_state_handler()` @ 0x0322
- `thunk_usb_phy_state_monitor()` @ 0x0359
- `thunk_usb_link_state_poll()` @ 0x0507
- `thunk_usb_link_training_wait_cc11()` @ 0x051B
- `thunk_usb_get_memory_2()` @ 0x057F
- `get_memory()` @ 0x0BC8
- `mult_add_with_carry()` @ 0x0DD1
- `read_addr+2()` @ 0x0DDD
- `write_3bytes_to_ptr_rev()` @ 0x0DE6
- `usb_int0_cmd_handler()` @ 0x1196
- `usb_handle_command()` @ 0x180D
- `read_addr_2_const7_dup()` @ 0x1B23
- `sub_if_gt_f0()` @ 0x1BC1
- `set_0203_020d_020e()` @ 0x1C4A
- `set_05a6_indexed()` @ 0x1C5D
- `calc_extmem_offset_5b4()` @ 0x1C90
- `usb_set_register_bit0()` @ 0x3133
- `usb_clear_slot_entry()` @ 0x3168
- `usb_load_slot_addr_offset()` @ 0x3189
- `get_page_90_or_8f()` @ 0x31C5
- `get_page_01_by_param()` @ 0x31D5
- `get_page_ff_by_param()` @ 0x31E0
- `get_carry_as_ff()` @ 0x31E2
- `usb_copy_ceb2_ceb3_to_extmem()` @ 0x31FB
- `calc_page_with_carry()` @ 0x3223
- `usb_set_flag_clear_c509_bit0()` @ 0x3249
- `get_carry_negated()` @ 0x325F
- `usb_read_addr_wrapper()` @ 0x329F
- `usb_process_request()` @ 0x3CB8
- `usb_state_mode_handler()` @ 0x3F4A
- `usb_state_dispatch()` @ 0x40D9
- `usb_bulk_transfer_poll()` @ 0x480C
- `usb_lun_config()` @ 0x4C98
- `usb_setup_state_transfer()` @ 0x5069
- `usb_slot_idx_handler()` @ 0x50DB
- `usb_set_transfer_params()` @ 0x523C
- `uart_print()` @ 0x538D
- `get_e716_low2bits()` @ 0x541F
- `b1_get_cc37_clear_bit2()` @ 0x984D
- `b1_set_ptr_ff()` @ 0x9A27
- `usb_phy_clear_c472_c43x()` @ 0xBB37
- `memset_bytes_of_uint32()` @ 0xBB47
- `b1_store_set_bit2()` @ 0xBB6D
- `b1_clear_ptr_bit3_c472_bit1()` @ 0xBB7E
- `b1_set_ptr_bit6()` @ 0xBBA8
- `b1_set_ptr_bit1()` @ 0xBBAF
- `pcie_lane_mode_config()` @ 0xC35B
- `b1_get_bit6()` @ 0xC3A8
- `usb_get_92f7_high_nibble()` @ 0xCB0F
- `usb_e7e3_link_mode_config()` @ 0xDD42
- `usb_phy_c473_config()` @ 0xE214
- `usb_link_training_start()` @ 0xE50D
- `usb_link_flags_dispatch()` @ 0xE7C1
- `usb_link_cc11_clear()` @ 0xE8EF
- `usb_get_memory_d61f()` @ 0xE9E7

---

## Appendix: Register Access Summary

### Top 30 Most Accessed Registers

| Register | Reads | Writes | Total | Name |
|----------|-------|--------|-------|------|
| 0xE716 | 2016 | 4 | 2020 | REG_LINK_STATUS_E716 |
| 0xC620 | 404 | 404 | 808 |  |
| 0xC655 | 404 | 404 | 808 | REG_PHY_CFG_C655 |
| 0x92F7 | 806 | 0 | 806 | REG_POWER_STATUS_92F7 |
| 0xC520 | 710 | 0 | 710 | REG_NVME_LINK_STATUS |
| 0x9000 | 421 | 3 | 424 | REG_USB_STATUS |
| 0xCD31 | 403 | 6 | 409 | REG_CPU_TIMER_CTRL_CD31 |
| 0xC8AD | 176 | 176 | 352 | REG_FLASH_MODE |
| 0xCEF3 | 349 | 1 | 350 | REG_CPU_LINK_CEF3 |
| 0xCC11 | 47 | 122 | 169 | REG_TIMER0_CSR |
| 0x9091 | 112 | 45 | 157 | REG_USB_CTRL_PHASE |
| 0xC8B6 | 40 | 40 | 80 | REG_DMA_CHAN_CTRL2 |
| 0xC8A9 | 36 | 36 | 72 | REG_FLASH_CSR |
| 0xC8AC | 36 | 36 | 72 | REG_FLASH_ADDR_LEN |
| 0x9101 | 70 | 0 | 70 | REG_USB_PERIPH_STATUS |
| 0xC42A | 34 | 34 | 68 | REG_NVME_DOORBELL |
| 0xCE89 | 61 | 0 | 61 | REG_USB_DMA_STATE |
| 0xCC10 | 30 | 30 | 60 | REG_TIMER0_DIV |
| 0xC473 | 28 | 28 | 56 | REG_NVME_LINK_PARAM |
| 0x9002 | 32 | 17 | 49 | REG_USB_CONFIG |
| 0xC802 | 44 | 0 | 44 | REG_INT_USB_STATUS |
| 0xCC37 | 22 | 22 | 44 | REG_CPU_CTRL_CC37 |
| 0x9092 | 12 | 28 | 40 | REG_USB_DMA_TRIGGER |
| 0xC8AF | 0 | 36 | 36 | REG_FLASH_BUF_OFFSET_HI |
| 0x9003 | 24 | 12 | 36 | REG_USB_EP0_STATUS |
| 0xC8AE | 0 | 36 | 36 | REG_FLASH_BUF_OFFSET_LO |
| 0xC8A4 | 0 | 36 | 36 | REG_FLASH_DATA_LEN_HI |
| 0xC8AA | 0 | 36 | 36 | REG_FLASH_CMD |
| 0x9004 | 24 | 12 | 36 | REG_USB_EP0_LEN_L |
| 0x900B | 18 | 18 | 36 | REG_USB_MSC_CFG |

---

## Appendix: Detailed USB Register Sequences

Exact register-level sequences for ISRs that handle setup packets or DMA.

### INT0 #2 — Register Sequence

```
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x11  REG_USB_CTRL_PHASE
Read  0x9091 = 0x11  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x00  REG_USB_SETUP_BMREQ
Read  0x9105 = 0x05  REG_USB_PHY_STATUS_9105
Read  0x9106 = 0x48  REG_USB_SETUP_WVAL_L
Read  0x9107 = 0x00  REG_USB_SETUP_WVAL_H
Read  0x9108 = 0x00  REG_USB_SETUP_WIDX_L
Read  0x9109 = 0x00  REG_USB_SETUP_WIDX_H
Read  0x910A = 0x00  REG_USB_SETUP_WLEN_L
Read  0x910B = 0x00  REG_USB_SETUP_WLEN_H
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
-> usb_dispatch_return()
-> usb_link_state_machine()
-> usb_speed_check_handler()
Read  0x9090 = 0x00  REG_USB_INT_MASK_9090
Write 0x9090 = 0x48  REG_USB_INT_MASK_9090
Write 0x91D0 = 0x02  REG_USB_EP_CTRL_91D0
-> usb_get_speed_bank1()
Read  0x9100 = 0x02  REG_USB_LINK_STATUS
Read  0x92F8 = 0x00  
Read  0x92F8 = 0x00  
Read  0x9002 = 0x20  REG_USB_CONFIG
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
-> usb3_endpoint_setup()
Write 0x9092 = 0x08  REG_USB_DMA_TRIGGER
Write 0x9091 = 0x10  REG_USB_CTRL_PHASE
```

### INT0 #3 — Register Sequence

```
-> thunk_usb_phy_state_monitor()
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x80  REG_USB_SETUP_BMREQ
Read  0x9105 = 0x06  REG_USB_PHY_STATUS_9105
Read  0x9106 = 0x00  REG_USB_SETUP_WVAL_L
Read  0x9107 = 0x01  REG_USB_SETUP_WVAL_H
Read  0x9108 = 0x00  REG_USB_SETUP_WIDX_L
Read  0x9109 = 0x00  REG_USB_SETUP_WIDX_H
Read  0x910A = 0x08  REG_USB_SETUP_WLEN_L
Read  0x910B = 0x00  REG_USB_SETUP_WLEN_H
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb_dispatch_return()
-> usb_descriptor_state_machine()
-> clear_usb_remain_lo()
-> desc_write_byte_set_state()
-> desc_copy_to_ctrl_buffer()
-> usb_get_speed_bank1()
Read  0x9100 = 0x02  REG_USB_LINK_STATUS
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E00 = 0x12  REG_USB_SETUP_TYPE
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E01 = 0x01  REG_USB_SETUP_REQUEST
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E02 = 0x10  REG_USB_SETUP_VALUE_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E03 = 0x02  REG_USB_SETUP_VALUE_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E04 = 0x00  REG_USB_SETUP_INDEX_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E05 = 0x00  REG_USB_SETUP_INDEX_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E06 = 0x00  REG_USB_SETUP_LENGTH_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E07 = 0x40  REG_USB_SETUP_LENGTH_H
-> usb_get_speed_bank1()
Read  0x9100 = 0x02  REG_USB_LINK_STATUS
Write 0x9E02 = 0x20  REG_USB_SETUP_VALUE_L
Write 0x9E03 = 0x03  REG_USB_SETUP_VALUE_H
Write 0x9E07 = 0x09  REG_USB_SETUP_LENGTH_H
Read  0x9002 = 0x20  REG_USB_CONFIG
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb3_bit3_handler()
-> usb_dispatch_return()
-> usb_transfer_data()
Write 0x9003 = 0x00  REG_USB_EP0_STATUS
Write 0x9004 = 0x08  REG_USB_EP0_LEN_L
Write 0x9092 = 0x04  REG_USB_DMA_TRIGGER
Read  0x9092 = 0x00  REG_USB_DMA_TRIGGER
Read  0x9003 = 0x00  REG_USB_EP0_STATUS
Read  0x9004 = 0x08  REG_USB_EP0_LEN_L
-> add_byte_with_carry_to_16bit()
Read  0x9003 = 0x00  REG_USB_EP0_STATUS
Read  0x9004 = 0x08  REG_USB_EP0_LEN_L
-> usb_idle_state_handler()
Write 0x9091 = 0x08  REG_USB_CTRL_PHASE
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
-> usb3_endpoint_setup()
Write 0x9092 = 0x08  REG_USB_DMA_TRIGGER
... (3 more register operations) ...
```

### INT0 #4 — Register Sequence

```
-> usb_bulk_transfer_poll()
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x00  REG_USB_SETUP_BMREQ
Read  0x9105 = 0x31  REG_USB_PHY_STATUS_9105
Read  0x9106 = 0x28  REG_USB_SETUP_WVAL_L
Read  0x9107 = 0x00  REG_USB_SETUP_WVAL_H
Read  0x9108 = 0x00  REG_USB_SETUP_WIDX_L
Read  0x9109 = 0x00  REG_USB_SETUP_WIDX_H
Read  0x910A = 0x00  REG_USB_SETUP_WLEN_L
Read  0x910B = 0x00  REG_USB_SETUP_WLEN_H
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
-> usb_dispatch_return()
-> empty_stub_e966()
Read  0x9002 = 0x20  REG_USB_CONFIG
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
-> usb3_endpoint_setup()
Write 0x9092 = 0x08  REG_USB_DMA_TRIGGER
Write 0x9091 = 0x10  REG_USB_CTRL_PHASE
```

### INT0 #5 — Register Sequence

```
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x80  REG_USB_SETUP_BMREQ
Read  0x9105 = 0x06  REG_USB_PHY_STATUS_9105
Read  0x9106 = 0x00  REG_USB_SETUP_WVAL_L
Read  0x9107 = 0x01  REG_USB_SETUP_WVAL_H
Read  0x9108 = 0x00  REG_USB_SETUP_WIDX_L
Read  0x9109 = 0x00  REG_USB_SETUP_WIDX_H
Read  0x910A = 0x12  REG_USB_SETUP_WLEN_L
Read  0x910B = 0x00  REG_USB_SETUP_WLEN_H
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb_dispatch_return()
-> usb_descriptor_state_machine()
-> clear_usb_remain_lo()
-> desc_write_byte_set_state()
-> desc_copy_to_ctrl_buffer()
-> usb_get_speed_bank1()
Read  0x9100 = 0x02  REG_USB_LINK_STATUS
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E00 = 0x12  REG_USB_SETUP_TYPE
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E01 = 0x01  REG_USB_SETUP_REQUEST
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E02 = 0x10  REG_USB_SETUP_VALUE_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E03 = 0x02  REG_USB_SETUP_VALUE_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E04 = 0x00  REG_USB_SETUP_INDEX_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E05 = 0x00  REG_USB_SETUP_INDEX_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E06 = 0x00  REG_USB_SETUP_LENGTH_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E07 = 0x40  REG_USB_SETUP_LENGTH_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E08 = 0x4C  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E09 = 0x17  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0A = 0x63  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0B = 0x24  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0C = 0x01  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0D = 0x00  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0E = 0x02  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0F = 0x03  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E10 = 0x01  
... (32 more register operations) ...
```

### INT0 #6 — Register Sequence

```
-> thunk_usb_link_state_poll()
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x80  REG_USB_SETUP_BMREQ
Read  0x9105 = 0x06  REG_USB_PHY_STATUS_9105
Read  0x9106 = 0x00  REG_USB_SETUP_WVAL_L
Read  0x9107 = 0x0F  REG_USB_SETUP_WVAL_H
Read  0x9108 = 0x00  REG_USB_SETUP_WIDX_L
Read  0x9109 = 0x00  REG_USB_SETUP_WIDX_H
Read  0x910A = 0x05  REG_USB_SETUP_WLEN_L
Read  0x910B = 0x00  REG_USB_SETUP_WLEN_H
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb_dispatch_return()
-> usb_descriptor_state_machine()
-> desc_init_state()
-> b1_usb_desc_offset_copy()
-> desc_copy_to_ctrl_buffer()
-> usb_get_speed_bank1()
Read  0x9100 = 0x02  REG_USB_LINK_STATUS
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E00 = 0x05  REG_USB_SETUP_TYPE
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E01 = 0x0F  REG_USB_SETUP_REQUEST
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E02 = 0xAC  REG_USB_SETUP_VALUE_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E03 = 0x00  REG_USB_SETUP_VALUE_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E04 = 0x09  REG_USB_SETUP_INDEX_L
-> desc_write_byte_at_offset()
Write 0x9E1A = 0x00  
-> desc_write_byte_indexed()
Write 0x9E1B = 0x03  
-> desc_write_byte_indexed()
Write 0x9E1C = 0x03  
-> desc_write_byte_indexed()
Write 0x9E1D = 0x03  REG_USB_CTRL_BUF_9E1D
-> desc_write_byte_indexed()
Write 0x9E1E = 0x03  REG_USB_CTRL_BUF_9E1E
-> desc_write_byte_indexed()
Write 0x9E1F = 0x03  
-> desc_write_byte_indexed()
Write 0x9E20 = 0x4C  
-> desc_write_byte_indexed()
Write 0x9E21 = 0x17  
-> desc_write_byte_at_offset()
Write 0x9E70 = 0x05  
Read  0x9002 = 0x20  REG_USB_CONFIG
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb3_bit3_handler()
-> usb_dispatch_return()
-> usb_transfer_data()
Write 0x9003 = 0x00  REG_USB_EP0_STATUS
Write 0x9004 = 0x05  REG_USB_EP0_LEN_L
Write 0x9092 = 0x04  REG_USB_DMA_TRIGGER
Read  0x9092 = 0x00  REG_USB_DMA_TRIGGER
Read  0x9003 = 0x00  REG_USB_EP0_STATUS
Read  0x9004 = 0x05  REG_USB_EP0_LEN_L
-> add_byte_with_carry_to_16bit()
Read  0x9003 = 0x00  REG_USB_EP0_STATUS
Read  0x9004 = 0x05  REG_USB_EP0_LEN_L
-> usb_idle_state_handler()
... (7 more register operations) ...
```

### INT0 #7 — Register Sequence

```
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x80  REG_USB_SETUP_BMREQ
Read  0x9105 = 0x06  REG_USB_PHY_STATUS_9105
Read  0x9106 = 0x00  REG_USB_SETUP_WVAL_L
Read  0x9107 = 0x0F  REG_USB_SETUP_WVAL_H
Read  0x9108 = 0x00  REG_USB_SETUP_WIDX_L
Read  0x9109 = 0x00  REG_USB_SETUP_WIDX_H
Read  0x910A = 0xAC  REG_USB_SETUP_WLEN_L
Read  0x910B = 0x00  REG_USB_SETUP_WLEN_H
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb_dispatch_return()
-> usb_descriptor_state_machine()
-> desc_init_state()
-> b1_usb_desc_offset_copy()
-> desc_copy_to_ctrl_buffer()
-> usb_get_speed_bank1()
Read  0x9100 = 0x02  REG_USB_LINK_STATUS
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E00 = 0x05  REG_USB_SETUP_TYPE
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E01 = 0x0F  REG_USB_SETUP_REQUEST
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E02 = 0xAC  REG_USB_SETUP_VALUE_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E03 = 0x00  REG_USB_SETUP_VALUE_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E04 = 0x09  REG_USB_SETUP_INDEX_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E05 = 0x07  REG_USB_SETUP_INDEX_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E06 = 0x10  REG_USB_SETUP_LENGTH_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E07 = 0x02  REG_USB_SETUP_LENGTH_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E08 = 0x1E  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E09 = 0xF4  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0A = 0x00  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0B = 0x00  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0C = 0x0A  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0D = 0x10  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0E = 0x03  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0F = 0x00  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E10 = 0x0E  
... (507 more register operations) ...
```

### INT0 #8 — Register Sequence

```
-> thunk_usb_phy_state_monitor()
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x80  REG_USB_SETUP_BMREQ
Read  0x9105 = 0x06  REG_USB_PHY_STATUS_9105
Read  0x9106 = 0x00  REG_USB_SETUP_WVAL_L
Read  0x9107 = 0x02  REG_USB_SETUP_WVAL_H
Read  0x9108 = 0x00  REG_USB_SETUP_WIDX_L
Read  0x9109 = 0x00  REG_USB_SETUP_WIDX_H
Read  0x910A = 0x09  REG_USB_SETUP_WLEN_L
Read  0x910B = 0x00  REG_USB_SETUP_WLEN_H
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb_dispatch_return()
-> usb_descriptor_state_machine()
-> usb_get_speed_bank1()
Read  0x9100 = 0x02  REG_USB_LINK_STATUS
-> b1_set_0ad7_clear_remain()
-> desc_write_byte_set_state()
-> desc_copy_to_ctrl_buffer()
-> usb_get_speed_bank1()
Read  0x9100 = 0x02  REG_USB_LINK_STATUS
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E00 = 0x09  REG_USB_SETUP_TYPE
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E01 = 0x02  REG_USB_SETUP_REQUEST
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E02 = 0x2C  REG_USB_SETUP_VALUE_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E03 = 0x00  REG_USB_SETUP_VALUE_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E04 = 0x01  REG_USB_SETUP_INDEX_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E05 = 0x01  REG_USB_SETUP_INDEX_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E06 = 0x00  REG_USB_SETUP_LENGTH_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E07 = 0xC0  REG_USB_SETUP_LENGTH_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E08 = 0x00  
Write 0x9E02 = 0x79  REG_USB_SETUP_VALUE_L
-> desc_get_e302_mode()
Read  0x9002 = 0x20  REG_USB_CONFIG
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb3_bit3_handler()
-> usb_dispatch_return()
-> usb_transfer_data()
Write 0x9003 = 0x00  REG_USB_EP0_STATUS
Write 0x9004 = 0x09  REG_USB_EP0_LEN_L
Write 0x9092 = 0x04  REG_USB_DMA_TRIGGER
Read  0x9092 = 0x00  REG_USB_DMA_TRIGGER
Read  0x9003 = 0x00  REG_USB_EP0_STATUS
Read  0x9004 = 0x09  REG_USB_EP0_LEN_L
-> add_byte_with_carry_to_16bit()
Read  0x9003 = 0x00  REG_USB_EP0_STATUS
Read  0x9004 = 0x09  REG_USB_EP0_LEN_L
-> usb_idle_state_handler()
Write 0x9091 = 0x08  REG_USB_CTRL_PHASE
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
... (5 more register operations) ...
```

### INT0 #9 — Register Sequence

```
-> usb_bulk_transfer_poll()
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x80  REG_USB_SETUP_BMREQ
Read  0x9105 = 0x06  REG_USB_PHY_STATUS_9105
Read  0x9106 = 0x00  REG_USB_SETUP_WVAL_L
Read  0x9107 = 0x02  REG_USB_SETUP_WVAL_H
Read  0x9108 = 0x00  REG_USB_SETUP_WIDX_L
Read  0x9109 = 0x00  REG_USB_SETUP_WIDX_H
Read  0x910A = 0x79  REG_USB_SETUP_WLEN_L
Read  0x910B = 0x00  REG_USB_SETUP_WLEN_H
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb_dispatch_return()
-> usb_descriptor_state_machine()
-> usb_get_speed_bank1()
Read  0x9100 = 0x02  REG_USB_LINK_STATUS
-> b1_set_0ad7_clear_remain()
-> desc_write_byte_set_state()
-> desc_copy_to_ctrl_buffer()
-> usb_get_speed_bank1()
Read  0x9100 = 0x02  REG_USB_LINK_STATUS
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E00 = 0x09  REG_USB_SETUP_TYPE
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E01 = 0x02  REG_USB_SETUP_REQUEST
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E02 = 0x2C  REG_USB_SETUP_VALUE_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E03 = 0x00  REG_USB_SETUP_VALUE_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E04 = 0x01  REG_USB_SETUP_INDEX_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E05 = 0x01  REG_USB_SETUP_INDEX_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E06 = 0x00  REG_USB_SETUP_LENGTH_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E07 = 0xC0  REG_USB_SETUP_LENGTH_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E08 = 0x00  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E09 = 0x09  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0A = 0x04  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0B = 0x00  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0C = 0x00  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0D = 0x02  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0E = 0x08  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0F = 0x06  
... (341 more register operations) ...
```

### INT0 #10 — Register Sequence

```
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x80  REG_USB_SETUP_BMREQ
Read  0x9105 = 0x06  REG_USB_PHY_STATUS_9105
Read  0x9106 = 0x00  REG_USB_SETUP_WVAL_L
Read  0x9107 = 0x03  REG_USB_SETUP_WVAL_H
Read  0x9108 = 0x00  REG_USB_SETUP_WIDX_L
Read  0x9109 = 0x00  REG_USB_SETUP_WIDX_H
Read  0x910A = 0xFF  REG_USB_SETUP_WLEN_L
Read  0x910B = 0x00  REG_USB_SETUP_WLEN_H
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb_dispatch_return()
-> usb_descriptor_state_machine()
-> usb_dispatch_return()
-> desc_init_state()
-> desc_copy_to_ctrl_buffer()
-> usb_get_speed_bank1()
Read  0x9100 = 0x02  REG_USB_LINK_STATUS
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E00 = 0x04  REG_USB_SETUP_TYPE
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E01 = 0x03  REG_USB_SETUP_REQUEST
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E02 = 0x09  REG_USB_SETUP_VALUE_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E03 = 0x04  REG_USB_SETUP_VALUE_H
Read  0x9002 = 0x20  REG_USB_CONFIG
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb3_bit3_handler()
-> usb_dispatch_return()
-> usb_transfer_data()
Write 0x9003 = 0x00  REG_USB_EP0_STATUS
Write 0x9004 = 0x04  REG_USB_EP0_LEN_L
Write 0x9092 = 0x04  REG_USB_DMA_TRIGGER
Read  0x9092 = 0x00  REG_USB_DMA_TRIGGER
Read  0x9003 = 0x00  REG_USB_EP0_STATUS
Read  0x9004 = 0x04  REG_USB_EP0_LEN_L
-> add_byte_with_carry_to_16bit()
Read  0x9003 = 0x00  REG_USB_EP0_STATUS
Read  0x9004 = 0x04  REG_USB_EP0_LEN_L
-> usb_idle_state_handler()
Write 0x9091 = 0x08  REG_USB_CTRL_PHASE
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
-> usb3_endpoint_setup()
Write 0x9092 = 0x08  REG_USB_DMA_TRIGGER
Write 0x9091 = 0x10  REG_USB_CTRL_PHASE
Read  0xC806 = 0x00  REG_INT_SYSTEM
Read  0xC802 = 0x01  REG_INT_USB_STATUS
-> thunk_usb_link_state_poll()
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x80  REG_USB_SETUP_BMREQ
... (74 more register operations) ...
```

### INT0 #12 — Register Sequence

```
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x80  REG_USB_SETUP_BMREQ
Read  0x9105 = 0x06  REG_USB_PHY_STATUS_9105
Read  0x9106 = 0x02  REG_USB_SETUP_WVAL_L
Read  0x9107 = 0x03  REG_USB_SETUP_WVAL_H
Read  0x9108 = 0x09  REG_USB_SETUP_WIDX_L
Read  0x9109 = 0x04  REG_USB_SETUP_WIDX_H
Read  0x910A = 0xFF  REG_USB_SETUP_WLEN_L
Read  0x910B = 0x00  REG_USB_SETUP_WLEN_H
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb_dispatch_return()
-> usb_descriptor_state_machine()
-> usb_dispatch_return()
-> usb_ctrl_buffer_init_loop()
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E02 = 0x41  REG_USB_SETUP_VALUE_L
-> get_usb_ctrl_buffer_page()
Write 0x9E03 = 0x00  REG_USB_SETUP_VALUE_H
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E04 = 0x73  REG_USB_SETUP_INDEX_L
-> get_usb_ctrl_buffer_page()
Write 0x9E05 = 0x00  REG_USB_SETUP_INDEX_H
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E06 = 0x6D  REG_USB_SETUP_LENGTH_L
-> get_usb_ctrl_buffer_page()
Write 0x9E07 = 0x00  REG_USB_SETUP_LENGTH_H
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E08 = 0x65  
-> get_usb_ctrl_buffer_page()
Write 0x9E09 = 0x00  
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E0A = 0x64  
-> get_usb_ctrl_buffer_page()
Write 0x9E0B = 0x00  
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E0C = 0x69  
-> get_usb_ctrl_buffer_page()
Write 0x9E0D = 0x00  
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E0E = 0x61  
-> get_usb_ctrl_buffer_page()
Write 0x9E0F = 0x00  
-> get_desc_page_from_extmem()
Write 0x9E00 = 0x10  REG_USB_SETUP_TYPE
Write 0x9E01 = 0x03  REG_USB_SETUP_REQUEST
Read  0x9E00 = 0x10  REG_USB_SETUP_TYPE
-> clear_usb_remain_lo()
-> desc_copy_to_ctrl_buffer()
-> usb_get_speed_bank1()
Read  0x9100 = 0x02  REG_USB_LINK_STATUS
Read  0x9002 = 0x20  REG_USB_CONFIG
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb3_bit3_handler()
-> usb_dispatch_return()
-> usb_transfer_data()
Write 0x9003 = 0x00  REG_USB_EP0_STATUS
Write 0x9004 = 0x10  REG_USB_EP0_LEN_L
Write 0x9092 = 0x04  REG_USB_DMA_TRIGGER
Read  0x9092 = 0x00  REG_USB_DMA_TRIGGER
... (14 more register operations) ...
```

### INT0 #13 — Register Sequence

```
-> thunk_usb_phy_state_monitor()
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x80  REG_USB_SETUP_BMREQ
Read  0x9105 = 0x06  REG_USB_PHY_STATUS_9105
Read  0x9106 = 0x01  REG_USB_SETUP_WVAL_L
Read  0x9107 = 0x03  REG_USB_SETUP_WVAL_H
Read  0x9108 = 0x09  REG_USB_SETUP_WIDX_L
Read  0x9109 = 0x04  REG_USB_SETUP_WIDX_H
Read  0x910A = 0xFF  REG_USB_SETUP_WLEN_L
Read  0x910B = 0x00  REG_USB_SETUP_WLEN_H
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb_dispatch_return()
-> usb_descriptor_state_machine()
-> usb_dispatch_return()
-> usb_ctrl_buffer_init_loop()
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E02 = 0x30  REG_USB_SETUP_VALUE_L
-> get_usb_ctrl_buffer_page()
Write 0x9E03 = 0x00  REG_USB_SETUP_VALUE_H
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E04 = 0x30  REG_USB_SETUP_INDEX_L
-> get_usb_ctrl_buffer_page()
Write 0x9E05 = 0x00  REG_USB_SETUP_INDEX_H
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E06 = 0x30  REG_USB_SETUP_LENGTH_L
-> get_usb_ctrl_buffer_page()
Write 0x9E07 = 0x00  REG_USB_SETUP_LENGTH_H
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E08 = 0x30  
-> get_usb_ctrl_buffer_page()
Write 0x9E09 = 0x00  
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E0A = 0x30  
-> get_usb_ctrl_buffer_page()
Write 0x9E0B = 0x00  
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E0C = 0x30  
-> get_usb_ctrl_buffer_page()
Write 0x9E0D = 0x00  
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E0E = 0x30  
-> get_usb_ctrl_buffer_page()
Write 0x9E0F = 0x00  
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E10 = 0x30  
-> get_usb_ctrl_buffer_page()
Write 0x9E11 = 0x00  
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E12 = 0x30  
-> get_usb_ctrl_buffer_page()
Write 0x9E13 = 0x00  
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E14 = 0x30  
-> get_usb_ctrl_buffer_page()
Write 0x9E15 = 0x00  
-> get_desc_page_from_extmem()
-> get_usb_ctrl_buffer_page()
Write 0x9E16 = 0x30  REG_USB_CTRL_BUF_9E16
... (39 more register operations) ...
```

### INT0 #14 — Register Sequence

```
-> usb_bulk_transfer_poll()
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x00  REG_USB_SETUP_BMREQ
Read  0x9105 = 0x09  REG_USB_PHY_STATUS_9105
Read  0x9106 = 0x01  REG_USB_SETUP_WVAL_L
Read  0x9107 = 0x00  REG_USB_SETUP_WVAL_H
Read  0x9108 = 0x00  REG_USB_SETUP_WIDX_L
Read  0x9109 = 0x00  REG_USB_SETUP_WIDX_H
Read  0x910A = 0x00  REG_USB_SETUP_WLEN_L
Read  0x910B = 0x00  REG_USB_SETUP_WLEN_H
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
-> usb_dispatch_return()
-> usb_state_machine_handler()
-> write_4bytes_to_ptr_2()
-> usb_ep0_clear_and_nak()
Write 0x901A = 0x0D  REG_USB_MSC_LENGTH
Read  0x9006 = 0x10  REG_USB_EP0_CONFIG
Write 0x9006 = 0x10  REG_USB_EP0_CONFIG
Read  0x9006 = 0x10  REG_USB_EP0_CONFIG
Write 0x9006 = 0x10  REG_USB_EP0_CONFIG
Write 0x9094 = 0x01  REG_USB_EP_CFG2
Write 0x9094 = 0x08  REG_USB_EP_CFG2
-> usb_clear_bulk_int()
Write 0x90E3 = 0x02  REG_USB_EP_STATUS_90E3
-> usb_bulk_ep_clear_ready()
Read  0x905F = 0x44  REG_USB_EP_CTRL_905F
Write 0x905F = 0x44  REG_USB_EP_CTRL_905F
Read  0x905D = 0x00  REG_USB_EP_CTRL_905D
Write 0x905D = 0x00  REG_USB_EP_CTRL_905D
Write 0x90E3 = 0x01  REG_USB_EP_STATUS_90E3
Write 0x90A0 = 0x01  REG_USB_CTRL_90A0
-> usb_get_9090_status()
Read  0x9090 = 0x48  REG_USB_INT_MASK_9090
Write 0x9090 = 0xC8  REG_USB_INT_MASK_9090
-> usb_disable_endpoint()
Read  0x9000 = 0x00  REG_USB_STATUS
Write 0x9000 = 0x00  REG_USB_STATUS
Read  0x924C = 0x04  REG_USB_CTRL_924C
Write 0x924C = 0x04  REG_USB_CTRL_924C
Read  0x9002 = 0x20  REG_USB_CONFIG
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
-> usb3_endpoint_setup()
Write 0x9092 = 0x08  REG_USB_DMA_TRIGGER
Write 0x9091 = 0x10  REG_USB_CTRL_PHASE
```

### INT0 #15 — Register Sequence

```
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x01  REG_USB_SETUP_BMREQ
Read  0x9105 = 0x0B  REG_USB_PHY_STATUS_9105
Read  0x9106 = 0x01  REG_USB_SETUP_WVAL_L
Read  0x9107 = 0x00  REG_USB_SETUP_WVAL_H
Read  0x9108 = 0x00  REG_USB_SETUP_WIDX_L
Read  0x9109 = 0x00  REG_USB_SETUP_WIDX_H
Read  0x910A = 0x00  REG_USB_SETUP_WLEN_L
Read  0x910B = 0x00  REG_USB_SETUP_WLEN_H
Read  0x9091 = 0x10  REG_USB_CTRL_PHASE
-> usb_dispatch_return()
-> usb_get_speed_bank1()
Read  0x9100 = 0x02  REG_USB_LINK_STATUS
-> usb_reset_handler()
Read  0x9090 = 0xC8  REG_USB_INT_MASK_9090
-> usb_phy_init()
-> usb_endpoint_state_clear()
-> usb_phy_c473_config()
-> b1_set_ptr_bit6()
-> b1_set_ptr_bit1()
-> b1_clear_ptr_bit3_c472_bit1()
-> memset_bytes_of_uint32()
-> b1_store_set_bit2()
-> usb_phy_clear_c472_c43x()
-> usb_mode_config()
-> memset_bytes_of_uint32()
-> memset_bytes_of_uint32()
-> memset_bytes_of_uint32()
Write 0x9096 = 0xFF  REG_USB_EP_READY
Write 0x9097 = 0xFF  REG_USB_EP_CTRL_9097
Write 0x9098 = 0xFF  REG_USB_EP_MODE_9098
Write 0x9099 = 0xFF  
-> memset_bytes_of_uint32()
Write 0x909A = 0xFF  
Write 0x909B = 0xFF  
Write 0x909C = 0xFF  
Write 0x909D = 0xFF  
Write 0x909E = 0x03  REG_USB_STATUS_909E
-> b1_fill_c438_c43b()
-> memset_bytes_of_uint32()
-> memset_bytes_of_uint32()
Write 0x9011 = 0x00  REG_USB_DATA_H
Write 0x9012 = 0x00  REG_USB_FIFO_STATUS
Write 0x9013 = 0x00  REG_USB_FIFO_H
Write 0x9014 = 0x00  
-> b1_fill_ptr_1_2_3()
Write 0x9015 = 0x00  
Write 0x9016 = 0x00  
Write 0x9017 = 0x00  
Write 0x9018 = 0x02  REG_USB_XCVR_MODE
Write 0x9010 = 0x00  REG_USB_DATA_L
-> b1_set_ptr_bit2()
-> b1_set_ptr_bit1()
-> b1_clear_ptr_bit3_c472_bit1()
-> memset_bytes_of_uint32()
-> usb_phy_clear_c472_c43x()
-> b1_set_ptr_bits1_2()
-> b1_set_ptr_bit3()
-> b1_set_ptr_bits1_2()
Read  0x900B = 0x00  REG_USB_MSC_CFG
Write 0x900B = 0x02  REG_USB_MSC_CFG
Read  0x900B = 0x02  REG_USB_MSC_CFG
Write 0x900B = 0x06  REG_USB_MSC_CFG
Read  0x900B = 0x06  REG_USB_MSC_CFG
Write 0x900B = 0x04  REG_USB_MSC_CFG
Read  0x900B = 0x04  REG_USB_MSC_CFG
Write 0x900B = 0x00  REG_USB_MSC_CFG
Read  0x9000 = 0x00  REG_USB_STATUS
... (43 more register operations) ...
```

### INT0 #21 — Register Sequence

```
-> thunk_usb_phy_state_monitor()
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x80  REG_USB_SETUP_BMREQ
Read  0x9105 = 0x06  REG_USB_PHY_STATUS_9105
Read  0x9106 = 0x00  REG_USB_SETUP_WVAL_L
Read  0x9107 = 0x0F  REG_USB_SETUP_WVAL_H
Read  0x9108 = 0x00  REG_USB_SETUP_WIDX_L
Read  0x9109 = 0x00  REG_USB_SETUP_WIDX_H
Read  0x910A = 0x05  REG_USB_SETUP_WLEN_L
Read  0x910B = 0x00  REG_USB_SETUP_WLEN_H
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb_dispatch_return()
-> usb_descriptor_state_machine()
-> desc_init_state()
-> b1_usb_desc_offset_copy()
-> desc_copy_to_ctrl_buffer()
-> usb_get_speed_bank1()
Read  0x9100 = 0x02  REG_USB_LINK_STATUS
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E00 = 0x05  REG_USB_SETUP_TYPE
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E01 = 0x0F  REG_USB_SETUP_REQUEST
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E02 = 0xAC  REG_USB_SETUP_VALUE_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E03 = 0x00  REG_USB_SETUP_VALUE_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E04 = 0x09  REG_USB_SETUP_INDEX_L
-> desc_write_byte_at_offset()
Write 0x9E1A = 0x00  
-> desc_write_byte_indexed()
Write 0x9E1B = 0x03  
-> desc_write_byte_indexed()
Write 0x9E1C = 0x03  
-> desc_write_byte_indexed()
Write 0x9E1D = 0x03  REG_USB_CTRL_BUF_9E1D
-> desc_write_byte_indexed()
Write 0x9E1E = 0x03  REG_USB_CTRL_BUF_9E1E
-> desc_write_byte_indexed()
Write 0x9E1F = 0x03  
-> desc_write_byte_indexed()
Write 0x9E20 = 0x4C  
-> desc_write_byte_indexed()
Write 0x9E21 = 0x17  
-> desc_write_byte_at_offset()
Write 0x9E70 = 0x05  
Read  0x9002 = 0x20  REG_USB_CONFIG
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb3_bit3_handler()
-> usb_dispatch_return()
-> usb_transfer_data()
Write 0x9003 = 0x00  REG_USB_EP0_STATUS
Write 0x9004 = 0x05  REG_USB_EP0_LEN_L
Write 0x9092 = 0x04  REG_USB_DMA_TRIGGER
Read  0x9092 = 0x00  REG_USB_DMA_TRIGGER
Read  0x9003 = 0x00  REG_USB_EP0_STATUS
Read  0x9004 = 0x05  REG_USB_EP0_LEN_L
-> add_byte_with_carry_to_16bit()
Read  0x9003 = 0x00  REG_USB_EP0_STATUS
Read  0x9004 = 0x05  REG_USB_EP0_LEN_L
-> usb_idle_state_handler()
... (7 more register operations) ...
```

### INT0 #22 — Register Sequence

```
-> usb_bulk_transfer_poll()
Read  0xC802 = 0x01  REG_INT_USB_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
Read  0x9101 = 0x02  REG_USB_PERIPH_STATUS
-> trampoline_ss_event()
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9091 = 0x01  REG_USB_CTRL_PHASE
-> usb3_bit0_handler()
Read  0x9002 = 0x20  REG_USB_CONFIG
Write 0x9002 = 0x20  REG_USB_CONFIG
Read  0x9220 = 0x00  
Write 0x9091 = 0x01  REG_USB_CTRL_PHASE
Read  0x9104 = 0x80  REG_USB_SETUP_BMREQ
Read  0x9105 = 0x06  REG_USB_PHY_STATUS_9105
Read  0x9106 = 0x00  REG_USB_SETUP_WVAL_L
Read  0x9107 = 0x0F  REG_USB_SETUP_WVAL_H
Read  0x9108 = 0x00  REG_USB_SETUP_WIDX_L
Read  0x9109 = 0x00  REG_USB_SETUP_WIDX_H
Read  0x910A = 0xAC  REG_USB_SETUP_WLEN_L
Read  0x910B = 0x00  REG_USB_SETUP_WLEN_H
Read  0x9091 = 0x08  REG_USB_CTRL_PHASE
-> usb_dispatch_return()
-> usb_descriptor_state_machine()
-> desc_init_state()
-> b1_usb_desc_offset_copy()
-> desc_copy_to_ctrl_buffer()
-> usb_get_speed_bank1()
Read  0x9100 = 0x02  REG_USB_LINK_STATUS
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E00 = 0x05  REG_USB_SETUP_TYPE
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E01 = 0x0F  REG_USB_SETUP_REQUEST
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E02 = 0xAC  REG_USB_SETUP_VALUE_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E03 = 0x00  REG_USB_SETUP_VALUE_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E04 = 0x09  REG_USB_SETUP_INDEX_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E05 = 0x07  REG_USB_SETUP_INDEX_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E06 = 0x10  REG_USB_SETUP_LENGTH_L
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E07 = 0x02  REG_USB_SETUP_LENGTH_H
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E08 = 0x1E  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E09 = 0xF4  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0A = 0x00  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0B = 0x00  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0C = 0x0A  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0D = 0x10  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0E = 0x03  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
Write 0x9E0F = 0x00  
-> b1_add_0ae0_carry()
-> b1_sub_62_dup()
... (508 more register operations) ...
```
