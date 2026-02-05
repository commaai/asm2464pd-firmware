# ASM2464PD Custom Firmware - USB Enumeration

## MISSION

**Make `clean/src/main.c` enumerate on real USB hardware.**

The device should appear as a USB device when plugged into a host computer.

## Build and Test

```bash
# Build AND flash (also shows UART output for ~15s)
cd /home/geohot/asm2464pd-firmware/clean && make flash

# Build only
cd /home/geohot/asm2464pd-firmware/clean && make
```

## Current Status

**Working:**
- Boot sequence completes (`[BOOT]` and `[GO]` printed)
- INT1 fires on power event, clears bit 6 of 0x92C2 which brings up USB link
- 0x92F7 transitions: 0x40 → 0x70 → 0x50 → 0x00 (link coming up)
- INT0 (USB) fires when host sends requests
- SET_ADDRESS (0x05) handled successfully
- GET_DESCRIPTOR (0x06) data phase works - descriptor sent to host

**Current Problem: ASYNC DMA CORRUPTION**

After GET_DESCRIPTOR status phase, writing `0x9092 = 0x08` triggers an **asynchronous DMA operation** that corrupts the setup buffer at 0x9104-0x910B:

Key findings from debugging:
1. **BEFORE** `0x9092=0x08`: buffer at 0x9104 shows `80 06` (correct GET_DESC)
2. **IMMEDIATELY AFTER** `0x9092=0x08`: buffer STILL shows `80 06` (correct!)
3. `0x9092` reads back as `0x00` immediately (operation "complete")
4. **AFTER ~1000 cycles delay**: buffer shows `00 31` (CORRUPTED!)
5. **NEXT ISR**: buffer consistently reads `00 31 28 00 00 00 00 00`

This proves:
- The corruption is **asynchronous** - happens after the write completes
- Some background DMA is running that overwrites 0x9104-0x910B
- The trace shows identical sequence working - so we're missing something

**Mystery values:** `0x31` = '1' (ASCII 49), `0x28` = '(' (ASCII 40)
- Not from our descriptor data
- Not from our code space
- May be from internal USB FIFO or wrong DMA source config

## Control Transfer State Machine (0x9091)

The USB control transfer uses register 0x9091 to track phases:
- `0x01` = Setup phase (write this to start)
- `0x08` = Data phase ready (poll for this before sending data)
- `0x10` = Status phase ready (poll for this before completing)

### SET_ADDRESS sequence (no data phase):
1. Write `0x9091 = 0x01` (start)
2. Poll until `0x9091 & 0x10` (status phase ready)
3. Do endpoint setup (0x9206-920B, 0x9220)
4. Write `0x9092 = 0x08` (DMA complete) ← **This does NOT corrupt!**
5. Write `0x9091 = 0x10` (ack status)

### GET_DESCRIPTOR sequence (has data phase):
1. Write `0x9091 = 0x01` (start)
2. Poll until `0x9091 & 0x08` (data phase ready)
3. Write descriptor to `0x9E00+`
4. Write `0x9003 = 0x00`, `0x9004 = length`
5. Write `0x9092 = 0x04` (DMA send)
6. Poll until `0x9092 == 0x00` (DMA complete)
7. Read `0x9003`, `0x9004` (verify transfer)
8. Write `0x9091 = 0x08` (ack data phase)
9. Poll until `0x9091 & 0x10` (status phase ready)
10. Write `0x9092 = 0x08` ← **THIS TRIGGERS ASYNC CORRUPTION!**
11. Write `0x9091 = 0x10` (ack status)

The difference: SET_ADDRESS skips steps 3-9 (no data phase), so there's no prior `0x9092=0x04` write. The `0x9092=0x08` after a data phase seems to trigger incorrect DMA.

## Key Register Reference

```
Interrupts:
0xC800 - Main interrupt status
0xC801 - Interrupt enable (set to 0x50)
0xC802 - USB interrupt status
0xC806 - System interrupt status
0xC809 - Interrupt control (set to 0x2A)

USB Control:
0x9091 - Control phase: 0x01=setup, 0x08=data, 0x10=status
0x9092 - DMA trigger: 0x04=send, 0x08=complete (ASYNC!)
0x9101 - Peripheral status: 0x02=SETUP pkt, 0x08=buffer, 0x10=link
0x9104-0x910B - Setup packet (bmReq, bReq, wVal, wIdx, wLen)
0x9E00+ - Response buffer (write descriptors here)
0x9003 - EP0 status
0x9004 - EP0 length

Power/Link:
0x92E1 - Power event (write back to clear)
0x92C2 - Power status (clear bit 6 on power event)
0x92F7 - Link status (0x00 = link up)
```

## Reference Files

- `clean/src/main.c` - Our firmware
- `trace/enumerate_min` - MMIO trace of original firmware enumerating (5228 lines)
- `fw.bin` - Original working firmware

## Trace Analysis Commands

```bash
# Find control phase handling
grep "0x9091" trace/enumerate_min | head -50

# Find GET_DESCRIPTOR sequence (around line 5162)
sed -n '5158,5230p' trace/enumerate_min

# Find SET_ADDRESS sequence (around line 5100)
sed -n '5090,5160p' trace/enumerate_min

# Check what happens after 0x9092=0x08 in trace
sed -n '5200,5230p' trace/enumerate_min
```

## What To Investigate Next

1. **Compare DMA config between SET_ADDRESS and GET_DESCRIPTOR** - Why does `0x9092=0x08` work for one but not the other?

2. **Look for DMA source/dest registers** - There might be registers we need to configure to point DMA to correct buffers

3. **Check if there's a DMA completion flag** - Maybe we need to wait for something other than `0x9092==0` before proceeding

4. **Look at 0x9E00 buffer relationship to 0x9104** - The DMA might be misconfigured to read from or write to wrong location

5. **Study the trace more carefully** for any register accesses between GET_DESC complete and next ISR that we're missing

6. **Check 0x9090 register** - SET_ADDRESS writes `0x9090 = 0x01` (USB INT mask), might affect DMA behavior

7. **Look for other DMA control registers** - Check 0xCC90-0xCCFF area for DMA configuration that might need resetting

---

## Debugging Session Notes (for future Claude sessions)

### How the Emulator/Trace Works
- The trace in `trace/enumerate_min` was captured from an **emulator running against REAL HARDWARE via USB proxy**
- This means the trace shows ACTUAL hardware register values, not simulated ones
- The emulator intercepts MMIO reads/writes and forwards them to real ASM2464PD hardware
- If the trace shows a sequence working, it WILL work on real hardware with exact same sequence

### What We Verified With Debug Output

**Test 1: Buffer before/after `0x9092=0x08`**
```c
uart_puts("[b:");  // BEFORE
uart_puthex(XDATA_REG8(0x9104));
uart_puthex(XDATA_REG8(0x9105));
XDATA_REG8(0x9092) = 0x08;  // THE TRIGGER
uart_puts("][a:");  // IMMEDIATELY AFTER  
uart_puthex(XDATA_REG8(0x9104));
uart_puthex(XDATA_REG8(0x9105));
```
Result: `[b:8006][a:8006]` - Buffer is STILL CORRECT immediately after write!

**Test 2: Buffer after delay**
```c
XDATA_REG8(0x9092) = 0x08;
{ uint16_t d; for(d = 0; d < 1000; d++) { } }  // ~1000 cycle delay
uart_puts("[d:");
uart_puthex(XDATA_REG8(0x9104));  
```
Result: `[d:0031]` - Buffer is CORRUPTED after delay!

**Test 3: Check 0x9091 on ISR entry**
```c
tmp9091 = XDATA_REG8(0x9091);
uart_puts("[91=");
uart_puthex(tmp9091);
```
Results:
- SET_ADDRESS entry: `[91=11]` (both setup and status bits)
- First GET_DESC entry: `[91=01]` (setup phase)
- First CORRUPT entry: `[91=01]` (looks like valid new setup!)
- Subsequent CORRUPT: `[91=10]` (stuck in status phase)

### The Async DMA Theory

The `0x9092=0x08` write triggers some background DMA that:
1. Completes "instantly" (register reads back as 0x00)
2. But actually runs asynchronously in hardware
3. Eventually overwrites 0x9104-0x910B with garbage
4. The garbage `00 31 28 00 00 00 00 00` is consistent

The fact that SET_ADDRESS's `0x9092=0x08` doesn't corrupt suggests:
- The DMA behavior depends on prior state
- GET_DESCRIPTOR's data phase (`0x9092=0x04`) may configure something that persists
- Or the DMA source/dest is set by some register we're not handling

### ISR Entry Sequence (from trace)

The trace shows this EXACT sequence on ISR entry for SETUP packets:
```
1. Read 0xC802 (USB interrupt status)
2. Read 0x9101 FOUR times (peripheral status - checks for SETUP bit 0x02)
3. Read 0x9091 TWICE (control phase)
4. Read 0x9002, write same value back (USB config)
5. Read 0x9220 (endpoint control)
6. Write 0x9091 = 0x01 (start setup phase)
7. Read 0x9104-0x910B (setup packet - ALL 8 bytes in sequence)
```

Our code now matches this sequence, but corruption still happens.

### Trace Line References

- **Lines 5091-5146**: SET_ADDRESS handling (works)
- **Lines 5158-5210**: GET_DESCRIPTOR handling (works in trace)
- **Lines 5213-5228**: Next ISR entry (trace ends before setup packet read)

Key difference: trace ends at PC=0xA5E8 before it reads the setup packet for the SECOND GET_DESCRIPTOR. So we can't see if the trace would have the same corruption issue.

### Things That DON'T Cause Corruption

1. UART output during ISR (disabled it, same result)
2. Reading 0x9091 at different times (tried various orders)
3. Polling 0x9092 after write (it's already 0x00)
4. Order of 0x9091 reads in ISR entry
5. Clearing 0x9003/0x9004 (EP0 length) before `0x9092=0x08`
6. Polling for data phase ready before writing descriptor

### Possible Root Causes To Explore

1. **DMA source address register** - Maybe 0x9003/0x9004 or another register sets where DMA reads FROM, and after data phase it's pointing to wrong location

2. **Buffer bank switching** - The chip might have multiple setup packet buffers, and `0x9092=0x08` switches to wrong one

3. **Missing register access** - The trace might do something between GET_DESC complete and next ISR that resets DMA state

4. **Timing-sensitive operation** - Real hardware might need delays we're not providing

5. **Interrupt edge vs level** - We use level-triggered (TCON=0), trace might use edge-triggered

### USB Enumeration Flow (what should happen)

1. Host sends SET_ADDRESS with address (e.g., 0x01)
2. Device ACKs, stores address
3. Host sends GET_DESCRIPTOR (device) with wLength=8 (first 8 bytes only)
4. Device sends first 8 bytes of device descriptor
5. Host sends GET_DESCRIPTOR (device) with wLength=18 (full descriptor)
6. Device sends full 18-byte descriptor
7. Host may request config descriptor, string descriptors, etc.

We fail at step 5 - the second GET_DESCRIPTOR has garbage in setup buffer.

### Code Structure

```
main.c:
├── uart_putc/puts/puthex - Debug output
├── handle_set_address() - Lines ~50-127
├── handle_get_descriptor() - Lines ~129-210
├── int0_isr() - USB interrupt handler, lines ~213-354
├── int1_isr() - Power interrupt handler, lines ~356-396
└── main() - Init sequence + main loop, lines ~398-910
```

The init sequence (lines 405-859) is copied directly from the trace and should be correct.
