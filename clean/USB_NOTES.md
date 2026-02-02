# USB Development Notes for Clean Firmware

## Current Status (2026-02-03): Setup Packet Detection Fixed, SET_ADDRESS Issue

### Progress Summary
1. **Setup packet detection FIXED** - Now using 0x9091 bit 0 to detect new setup packets
2. **GET_DESCRIPTOR works intermittently** - Host receives our 18-byte device descriptor
3. **SET_ADDRESS received** - We see SET_ADDRESS requests and respond with ZLP
4. **Device doesn't respond at new address** - Host times out after changing address

### Key Issue: Address Change Not Working

After SET_ADDRESS:
- We set `XDATA8(0x9006) = (addr & 0x7F) | 0x80`
- We send ZLP status
- Host tries to talk at new address
- Device doesn't respond

Possible causes:
1. Address register takes effect at wrong time
2. EP0 not re-armed after address change
3. Missing register writes that original firmware does
4. DMA/buffer configuration needs updating

### Working Detection Using 0x9091

```c
/* Check 0x9091 bit 0 for new setup packet */
if ((XDATA8(0x9091) & 0x01) && !(prev_9091 & 0x01)) {
    usb_handle_setup();
    XDATA8(0x909E) = 0x01;  /* Clear setup flag */
    XDATA8(0x90E3) = 0x02;  /* Re-arm for next packet */
    XDATA8(0x9091) = prev_9091 & ~0x01;  /* Clear 9091 bit 0 */
}
```

### EP0 Buffer Configuration

```c
EP0_BUF_BASE = 0x0160     /* Where we write descriptor data */
XDATA8(0x9310) = 0x60;    /* DMA low byte */
XDATA8(0x9311) = 0x01;    /* DMA high byte -> 0x0160 */
```

---

## Current Status (2026-02-03): Setup Packet Detection Fixed

### CRITICAL: Correct Setup Packet Detection

**DO NOT use packet content comparison for detection!**

The WRONG approach (causes missed/duplicate packets):
```c
// BAD - DO NOT USE THIS
if (cur[0] != last_setup[0] || ...) { is_new = 1; }
if (is_new && (cur[0] == 0x80 && cur[1] == 0x06)) { ... }
```

Problems with content comparison:
- Misses retried packets (same content as previous)
- May process garbage if buffer not cleared
- Filters by "looks like valid USB request" patterns = wrong

**The CORRECT approach uses the 0x909E hardware flag:**

From original firmware at 0x0ED3-0x0EEB:
```asm
mov dptr, #0x909E
movx a, @dptr
jb 0xe0.0, handle_setup  ; if bit 0 set, new setup packet
...
mov a, #0x01
movx @dptr, a            ; clear flag by writing 0x01
mov dptr, #0x90E3
inc a                    ; a = 0x02  
movx @dptr, a            ; write 0x02 to 0x90E3
```

Correct C code:
```c
if (XDATA8(0x909E) & 0x01) {
    usb_handle_setup();
    XDATA8(0x909E) = 0x01;  /* Clear flag */
    XDATA8(0x90E3) = 0x02;  /* Re-arm for next packet */
}
```

---

## Previous Status (2026-02-02): USB Interrupts Working, Enumeration Not Yet

**MAJOR PROGRESS:** USB interrupts are now firing! After extensive debugging:
- USB ISR fires with C802=0x41 (bits 0, 6)
- 9101 updates dynamically (0x01 → 0x03)
- Device appears on bus as 174c:2463 (bootloader VID/PID)

**Remaining Issue:** USB enumeration doesn't complete because:
- 9000=0x00 (USB active status not set)
- 9118=0x00 (USB enumeration state - causes ISR to exit early via table lookup)
- No setup packets arriving in 9E00 buffer
- Device disconnects after ~1 second

The key breakthrough was adding `91C0=0x00` (disable USB PHY) which triggers the 
interrupt controller to start generating USB-related interrupts.

## Key Findings

### Register State from Bootloader
When the bootloader hands off to firmware, these are the USB register values:
```
91C0 = 0x12  (bits 4, 1 set) - USB PHY control
91C1 = 0x3F
91C3 = 0x00
9241 = 0x00  - USB PHY config (NOT set by bootloader)
92C0 = 0x86  - Power control
92C2 = 0x47
9000 = 0x00  - USB status (no activity yet)
9006 = 0x10  - EP0 config
9301 = 0x00  - EP0 control
```

### Original Firmware USB Init Sequence
From emulator tracing, the original firmware writes these registers in order:
```c
// Phase 1: Clock/power setup
XDATA8(0x92C6) = 0x05;
XDATA8(0x92C7) = 0x00;
XDATA8(0x9201) = 0x00;
XDATA8(0x92C1) = 0x03;
XDATA8(0x920C) = 0x00;
XDATA8(0x92C0) = 0x81;  // Power enable
XDATA8(0x92C5) = 0x04;
XDATA8(0x9241) = 0x10;
XDATA8(0x9241) = 0xD0;  // USB PHY config - bits 7,6,4

// Phase 2: DMA buffer setup (0x93xx range)
// Sets up buffer addresses at 0x9310-0x9321

// Phase 3: More config
XDATA8(0x905F) = 0x00;
XDATA8(0x92C8) = 0x00;
XDATA8(0x900B) = 0x02/0x06/0x07/0x05/0x01/0x00;  // Sequence
XDATA8(0x901A) = 0x0D;

// Phase 4: USB endpoint config
XDATA8(0x92C0) = 0x81;
XDATA8(0x91D1) = 0x0F;
XDATA8(0x9300) = 0x0C;
XDATA8(0x9301) = 0xC0;  // EP0 arm
XDATA8(0x9302) = 0xBF;
XDATA8(0x9091) = 0x1F;
XDATA8(0x9093) = 0x0F;
XDATA8(0x91C1) = 0xF0;
// ... more endpoint config ...

// Phase 5: Interrupt masks (0x9096-0x909E, 0x9010-0x9018)
// All set to 0xFF or 0x03

// Phase 6: Final USB PHY enable
XDATA8(0x91C3) = 0x00;
XDATA8(0x91C0) = 0x03;  // Set bits 0,1
XDATA8(0x91C0) = 0x02;  // Clear bit 0, keep bit 1
```

### Key USB Control Registers

| Register | Purpose | Bootloader | Original FW |
|----------|---------|------------|-------------|
| 0x91C0 | USB PHY control | 0x12 | 0x03→0x02 |
| 0x91C1 | USB PHY control | 0x3F | 0xF0 |
| 0x9241 | USB PHY config | 0x00 | 0x10→0xD0 |
| 0x92C0 | Power enable | 0x86 | 0x81 |
| 0x9301 | EP0 control | 0x00 | 0xC0 (arm) |

### Register Writability
Some registers are write-only (don't read back written values):
- **Writable (verified):** 0x91C0, 0x91C1, 0x92C0, 0x9241
- **Write-only (read as 0x00):** 0x91D1, 0x9300, 0x9301, 0x9091, 0x9000

### Interrupt Handling
Original firmware uses interrupts for USB:
- INT0 (External Interrupt 0) at vector 0x0003 → handler at 0x0E33
- INT1 (External Interrupt 1) at vector 0x0013 → handler at 0x44D7

The ISR at 0x0E33:
1. Saves context (ACC, B, DPTR, PSW, R0-R7)
2. Reads 0xC802 to check interrupt source
3. Reads 0x9101 for USB status
4. Reads 0x9000 for USB connection status
5. Dispatches to appropriate handler

Our ISR setup (in crt0.s):
```
IE = 0x85   (EA=1, EX0=1, EX1=1)
TCON = 0x05 (IT0=1, IT1=1 - edge triggered)
```

**Problem:** We're not seeing any 'U' characters in UART output, meaning our USB ISR
is never firing. This could be because:
1. The interrupt line isn't being asserted by hardware
2. We need additional enable bits set
3. The interrupt is level-triggered, not edge-triggered
4. Something else is preventing interrupt delivery

### 8051 SFR Behavior (Important!)
Standard 8051 timer registers DON'T work as expected:
- Writing `IE = 0x82` (EA + ET0) reads back as `0x80` (only EA sticks)
- Writing `TCON = 0x10` (TR0) reads back as `0x00` (doesn't stick)
- Timer 0 interrupts NEVER fire

However, external interrupt bits DO work:
- Writing `IE = 0x85` (EA + EX0 + EX1) reads back as `0x85` (works!)
- Writing `TCON = 0x05` (IT0 + IT1) reads back as `0x05` (works!)

This means the chip uses standard 8051 external interrupts but has custom timer/peripheral handling.

### Boot Sequence Discovery
The bootloader behavior differs based on reset type:
- **Bootloader reset (-b flag)**: Bootloader enumerates as 174c:2463, waits for flash commands
- **Normal reset (-r flag)**: Bootloader jumps DIRECTLY to firmware without enumerating

This means our firmware must initialize USB completely from scratch - we can't rely on
any bootloader USB state.

### Bootloader USB Register State
When bootloader hands off to firmware (normal reset), these are the register values:
```
91C0 = 0x12  (bits 4, 1 set)
92C0 = 0x86  (NOT 0x81 like original firmware)
9241 = 0x00
9000 = 0x00  (no USB activity)
C802 = 0x00
```

Note: 92C0=0x86 from bootloader vs 92C0=0x81 from original firmware suggests power/clock config differs.

### KEY DISCOVERY: 92C0 Controls USB Visibility
Writing `XDATA8(0x92C0) = 0x81` (instead of bootloader's 0x86) makes the USB device
appear on the bus! The device enumerates as the bootloader's VID/PID (174c:2463),
then the host sends a USB reset, and after ~1-2 seconds it disconnects because we
don't respond to enumeration requests.

The difference: 0x86 vs 0x81 is bit 2 (0x04) and bit 0 (0x01):
- 0x86 = 1000 0110 (bits 7,2,1)
- 0x81 = 1000 0001 (bits 7,0)

Bit 0 of 0x92C0 appears to enable USB PHY/connection.

### Interrupt System - CONFIRMED WORKING
External interrupts (INT0, INT1) work correctly:
- Writing `TCON |= 0x02` (IE0 flag) triggers INT0 ISR → 'U' printed
- Writing `TCON |= 0x08` (IE1 flag) triggers INT1 ISR → 'I' printed
- `IE = 0x85, TCON = 0x05` properly enables edge-triggered external interrupts

Standard 8051 timer interrupts do NOT work (ET0, TR0 bits don't stick).

USB interrupts are NOT firing even when device appears on bus - suggests MMIO
interrupt enable registers need to be configured.

### Current Issue: Inconsistent USB Appearance
The device appears on USB bus inconsistently:
- Sometimes 92C0=0x81 alone makes device visible (enumerates as 174c:2463)
- Other times the device doesn't appear at all
- When it appears: host sends USB reset, device disconnects after ~1 sec (no response)
- C802, 9000, 9101 all read as 0x00 even when device is on bus

The inconsistency might be due to:
- Timing sensitivity in the USB PHY initialization
- Missing prerequisite register configuration
- Host-side USB hub/timing variations

### ISR Status Registers (from original firmware 0x0E33)
```
0xC802: Primary interrupt status (bit 0 checked first)
0x9101: USB peripheral status (bit 5 = command pending)
0x9000: USB connection status (bit 0 = active)
```

Original firmware checks these in order and dispatches to different handlers
based on which bits are set.

## What Needs to Happen

For USB to work, our firmware must:
1. **Initialize USB hardware** - Set up PHY, endpoints, buffers
2. **Enable interrupts** - Configure IE, TCON properly
3. **Handle USB bus reset** - Respond to host reset signal
4. **Handle enumeration** - Respond to SET_ADDRESS, GET_DESCRIPTOR
5. **Provide descriptors** - Device, config, string descriptors in ROM

The device appearing briefly (as bootloader VID/PID 174c:2463) proves the USB PHY
and D+ pullup are working. The disconnect happens because we don't respond to
the USB control requests that follow the bus reset.

## Next Steps

1. **Verify interrupts work** - Try timer interrupt to confirm 8051 interrupts function
2. **Check interrupt enable registers** - There may be MMIO registers that gate USB interrupts
3. **Implement USB polling** - As fallback, poll USB status registers in main loop
4. **Add minimal descriptor handling** - At minimum respond to GET_DEVICE_DESCRIPTOR

## Useful Commands

```bash
# Build and flash
cd clean && make clean && make && make flash

# Monitor USB and UART
sudo dmesg -C && python3 ftdi_debug.py -r -t 5 & sleep 6 && sudo dmesg

# Check USB devices
lsusb | grep -iE "174c|add1"

# Flash original firmware to verify hardware works
python3 ftdi_debug.py -bn && python3 flash.py fw.bin && python3 ftdi_debug.py -rn
```

## Emulator Tracing

```python
# Trace USB register writes
from emulate.emu import Emulator
emu = Emulator()
emu.load_firmware('fw.bin')
emu.hw.log_writes = True  # Enable write logging
for i in range(100000):
    emu.step()
```

## Session Notes (2026-02-02 Session 2) - CURRENT

### MAJOR BREAKTHROUGH: USB Interrupts Now Working!

After adding these register configurations:
1. Interrupt channel 3 configuration (C8AA=0x03, C8AC=0x03, C8A4=0x80)
2. C8Bx register configuration (interrupt controller block)
3. C809 |= 0x20 (set bit 5, from cycle 157049 in original firmware)
4. CC91=0x01, E7E3=0x00 (pre-USB PHY disable)
5. **91C0=0x00 (disable USB PHY to trigger re-init) - CRITICAL!**

The USB ISR now fires! We see:
```
U41/01/00/00  →  C802=0x41, 9101=0x01, 9000=0x00, 9091=0x00
C802=41 9101=01 ... 9105=06 9107=01 9101=03
```

### ISR Flow Analysis (0x0E33 → 0x10B8)

The original firmware ISR checks these conditions in order:
1. C802 bit 0 → jump to 0x10B8 (we hit this! ✓)
2. At 0x10B8: Check C806 bit 5 → handle DMA path (we have C806=0x00 ✗)
3. At 0x10E5: Check C802 bit 2 → USB enumeration via C471 (we have bit 6 not bit 2 ✗)
4. Eventually: Check 9000 bit 0 → USB handling at 0x0E6E (we have 9000=0x00 ✗)

### Critical Discovery: 9118 Table Lookup

At 0x0E71, the ISR reads 9118 and uses it as index into table at 0x5AC9:
```
Table[0x5AC9]: 08 00 01 00 02 00 01 00 ...
              idx0=08, idx1=00, idx2=01, ...
```

If table[9118] >= 8, the ISR exits early (at 0x0E82: jnc 0x0ED3).
- Our 9118=0x00 → table[0]=0x08 → exits early!
- Emulator sets 9118=0x01 → table[1]=0x00 → continues USB handling

### Why 9000 and 9118 Stay at Zero

The emulator artificially injects at cycle 200001:
- 9000=0x81, C802=0x05, 9101=0x61, 9118=0x01

On real hardware, these must be set by the USB controller when:
1. USB link is established
2. Host sends bus reset
3. Enumeration begins

With 91C0=0x00, the PHY is disabled and 9100=0x00 (link down).
The USB controller may need the link UP to update 9000/9118.

### Current Experiment: PHY Toggle

Instead of leaving 91C0=0x00, trying to toggle it:
```c
XDATA8(0x91C0) = 0x00;  // Disable PHY (triggers interrupt mechanism)
delay(100);
XDATA8(0x91C0) = 0x03;  // Re-enable
XDATA8(0x91C0) = 0x02;  // Final state
```

This might allow the PHY to come back up while keeping the interrupt path active.

### Registers That Need to Update for Enumeration

| Register | Current | Needed | Purpose |
|----------|---------|--------|---------|
| 9000 | 0x00 | 0x81 | USB active status |
| 9118 | 0x00 | 0x01+ | USB enumeration state (table index) |
| C802 | 0x41 | 0x05 | Interrupt status (need bit 2) |
| C806 | 0x00 | 0x20+ | System status (bit 5 for DMA path) |
| 9E00 | 0x00 | setup packet | First byte of 8-byte setup packet |

### Added Basic USB Handler

Added code to check 9091 bit 0 for setup packet indication and parse 9E00 buffer.
Also added device descriptor response for GET_DESCRIPTOR.
Currently 9091=0x00 so no setup packets are being indicated.

## Session Notes (2026-02-02 continued)

### E7xx and CCxx Registers - Safe to Use
Previously thought E7xx/CCxx broke USB, but after careful testing:
- E710, E716, E717, E780, E7E3, E324 can be written safely
- CC10-CC13, CC30-CC3E can be written safely
- The earlier issue may have been order/timing related

### USB Device Appears But Status Registers Don't Update
The fundamental problem remains:
- USB device appears on bus (enumerates as 174c:2463, bootloader VID/PID)
- Host sends USB bus reset
- 9100=0x03 shows USB link is active
- BUT 9000, 9101, C802 all remain 0x00
- No setup packets appear in 9E00 buffer
- INT0 is never asserted by hardware

This means the USB controller isn't generating status updates or interrupts.

### Possible Causes
1. USB controller is in a "pass-through" mode that doesn't generate status
2. There's a master enable bit we haven't found
3. The emulator's USB model doesn't match real hardware behavior
4. Some register needs to be READ before it generates events

### Registers Verified Working (read back correctly)
- 91C0=02, 9100=03, 92C0=81, 9241=D0, IE=85, TCON=05

### Registers that are Write-Only (read as 0x00)
- 9096-909E (interrupt enable masks)
- 9300-9305 (endpoint config)
- 90E2 (USB init trigger)

### C004 Breaks UART
Writing 0x02 to C004 corrupts UART output. Skip this register.

### BREAKTHROUGH: C8Ax Registers Enable Interrupt Controller
Adding C8Ax registers (C8A1-C8AF, C8AA, C8AB, C8AC, C8AD, C8AE, C8AF) caused C802 to start showing values!
- Before C8Ax: C802=0x00 always
- After C8Ax: C802=0x40 (bit 6 set)

This proves the C8Ax registers configure the interrupt controller that generates C802 status bits.

### ISR Dispatch Flow (0x0E33)
The original firmware ISR checks:
1. C802 bit 0 → if set, dispatch to USB handler at 0x10B8
2. 9101 bit 5 → if set, dispatch to 0x0F07 (USB command pending)
3. 9000 bit 0 → if set, continue USB handling at 0x0E6E

C802=0x40 (bit 6) we're seeing is a different interrupt source (maybe PD/PCIe).
For USB, we need C802 bit 0 (0x01) or bit 2 (0x04) to be set.

### Interrupt System Verified Working
Manual trigger of INT0 via `TCON |= 0x02` correctly fires the ISR and prints 'U'.
The issue is that USB hardware events don't assert INT0 or update status registers.

## Session Notes (2026-02-02)

### Write-Only Registers Confirmed
The interrupt enable registers 0x9096-0x909E are **write-only**:
- Writing 0xFF to 0x9096 succeeds but reading returns 0x00
- This is normal hardware behavior - writes work, just can't verify

### Register State After Our Init
After running usb_enable_minimal():
```
91C0=02  (was 0x12 from bootloader) - PHY enabled
91C1=3F  (unchanged)
91C3=00
92C0=81  (was 0x86) - USB power enabled
92C1=82
92C2=47
9241=D0  (was 0x00) - PHY config set!
9000=00  - Still no USB activity
9101=00  - Still no peripheral status
C802=00  - Still no interrupts
```

### Problem: Device Not Appearing on USB
Even with:
- 92C0=0x81 (USB power)
- 9241=0xD0 (PHY config)
- 91C0=0x02 (PHY enable)
- Interrupt masks set

The device does NOT appear on USB bus (no lsusb entry, no dmesg).

### Hypothesis: Need Full Endpoint Configuration
The USB PHY might not pull D+ high (signaling device presence) until:
1. Endpoints are configured (0x9300-0x9305)
2. EP0 is armed for reception (0x9301=0xC0)
3. DMA buffers are set up (0x9310-0x9321)

Original firmware does ALL of this before USB becomes visible.

### Session Update (2026-02-02 continued)

#### USB Device Now Appears on Bus
With proper init sequence, the device now appears briefly on USB:
- Device enumerates as `174c:2463` (bootloader VID/PID from ROM)
- Host sends USB bus reset
- Device disconnects after ~1 second (we don't respond)

This confirms USB PHY is properly initialized.

#### Critical Problem: Status Registers Stay at Zero
Even when device is on USB bus and host is sending reset/enumeration:
- `0x9000` = 0x00 (should indicate USB activity)
- `0x9101` = 0x00 (should have interrupt flags)
- `0xC802` = 0x00 (should have system interrupt status)
- `0x9118` = 0x00 (USB status register)
- `0x9096` = 0x00 (read back as zero)

The interrupt line (INT0) is never asserted because no status is being set.

#### Working Register Scan (from real hardware)
```
Scan90: (USB endpoint config)
9002=3F 9005=FF 9006=10 900B=07 
9010=FE 9011=FF 9012=FF 9013=FF 9014=FF 9015=FF 9016=FF 9017=FF 9018=03 9019=01
905E=01 905F=44 9061=01 

Scan91: (USB link/PHY)
9100=03 (USB link status - bits 0,1 set!)
910C=AA 911B=55 911C=53 911D=42 911E=43 911F=05 ("USBC" signature from bootloader)
91C0=02 91C1=30 91C2=C0 

Scan92: (USB power/PHY config)
9200=B1 9202=21 9241=D0 
92C0=81 92C1=03 92C2=47 92C3=08 92C4=10 92C5=04 92C6=05

Scan93: (Endpoint/buffer config)
9303=33 9304=3F 9305=40 (our writes worked)
9310=02 9312=02 9314=02 9318=02 931D=10 931E=02 9320=02 9321=10
```

Note: `9100=0x03` shows USB link is established, but 9000/9101 stay at 0.

#### Key Difference: Emulator vs Real Hardware
In the emulator, at cycle 200001 a simulated USB plug-in event:
- Sets `0x9000 = 0x81`
- Sets `0xC802 = 0x05`
- Sets `0x9101 = 0x61`
- Triggers EX0 interrupt

On real hardware, these registers never become non-zero despite USB activity.

#### Possible Causes
1. Missing hardware enable bit that gates status register updates
2. Status is edge-triggered and we're missing the window
3. DMA buffer configuration is incorrect/missing
4. Some other initialization step is required

#### Setup Packet Buffer (0x9E00-0x9E07)
Polling 0x9E00 shows constant 0x55 (bootloader garbage, not real setup packets).
No USB setup packets are being DMA'd to this buffer despite USB activity on the bus.

#### Root Cause Hypothesis
The USB controller has a gating mechanism that prevents:
1. Status registers from being updated
2. Setup packets from being DMA'd to the buffer
3. INT0 from being asserted

This gate is enabled by some register we haven't found yet.

#### Next Steps to Try
1. Trace ALL register writes in original firmware init (not just USB-related)
2. Look for registers in 0xB4xx, 0xCAxx, 0xCDxx ranges
3. Check if there's an enable bit in a register we're already writing
4. Compare register-by-register state between original firmware and ours

## Summary of Progress (Session 2)

### What's Working
- USB device appears on bus (174c:2463)
- USB ISR fires (C802 bit 0 triggers INT0)
- 9101 updates dynamically (USB peripheral status)
- Interrupt controller configured (C8Ax, C8Bx registers)
- 8051 external interrupts working (IE=0x85, TCON=0x05)

### What's NOT Working
- 9000=0x00 (USB active status never set)
- 9118=0x00 (causes early ISR exit via table lookup)
- C802 bit 2 not set (needed for USB enumeration path)
- No setup packets in 9E00 buffer
- Device disconnects after ~1 second

### Key Insight
The emulator artificially injects USB state (9000, 9118, C802) at cycle 200001.
On real hardware, the USB controller must set these based on actual bus events.
Something is preventing the USB controller from updating these status registers.

### Possible Root Causes
1. PHY disabled (91C0=0x00) prevents status updates
2. Missing register that enables USB status generation
3. DMA/buffer configuration issue
4. Timing - status set briefly and we miss it

### Files Modified
- `clean/src/main.c` - Full USB init sequence with interrupt handling
- `clean/src/crt0.s` - ISR vectors for INT0/INT1

## Session Notes (2026-02-02 Session 3)

### CRITICAL FINDING: Emulator vs Real Hardware Register Values

The emulator's register values DO NOT match real hardware! This is a major discovery.

Created `read_regs.py` tool to read registers from running firmware over USB using E4 vendor commands.

#### Register Comparison: Original Firmware (Real HW) vs Emulator Trace

| Register | Original FW (Real HW) | Emulator Trace | Notes |
|----------|----------------------|----------------|-------|
| 91C0 | 0x12 | 0x00 → 0x02 | **DIFFERENT** - Real HW has PHY in different state |
| 91C1 | 0x30 | 0xF0 | **DIFFERENT** |
| 92C0 | 0x87 | 0x81 | **DIFFERENT** - Bit 2 and bit 0 differ |
| 92C1 | 0x83 | 0x03 | **DIFFERENT** |
| 92C2 | 0x07 | 0x40 | **DIFFERENT** |
| 92C5 | 0x2F | 0x04 | **DIFFERENT** |
| 92C8 | 0x24 | 0x00 | **DIFFERENT** |
| C800 | 0x05 | 0x04/0x05 | Close |
| C801 | 0x10 | 0x50 | **DIFFERENT** |
| C807 | 0x04 | 0x84 | **DIFFERENT** |
| C809 | 0x00 | 0x2A | **DIFFERENT** |
| CA60 | 0x56 | 0x0E | **DIFFERENT** |
| 9118 | 0x00 | 0x01 (injected) | Emulator injects, HW stays 0x00 when idle |
| 9000 | 0x00 | 0x81 (injected) | Emulator injects, HW stays 0x00 when idle |
| C802 | 0x00 | 0x05 (injected) | Status registers are 0 when no events pending |

**Key insight**: Status registers (9000, 9118, C802) are 0x00 in idle state on BOTH original and our firmware. They only become non-zero during active USB events.

#### Bootloader State (from normal reset)

When bootloader jumps to firmware (no bootloader USB enumeration):
```
91C0=0x12  (PHY control - bits 4,1 set)
91C1=0x3F
92C0=0x86  (USB power)
92C1=0x82
92C2=0x47
9100=0x03  (USB link active!)
9000=0x00
C800=0x00  (interrupt controller NOT configured)
C801=0x00
C802=0x00
C807=0x00
C809=0x00
```

#### Device Appearance Pattern

Observed via `dmesg`:
1. Device appears as 174c:2463 (bootloader VID/PID) during reset
2. Host sees "ASMedia AS2462" USB Mass Storage device
3. Host sends USB reset
4. Bootloader jumps to our firmware
5. Our firmware doesn't respond to USB control transfers
6. Device disconnects after ~1 second (host timeout)

This confirms: **The problem is that our firmware doesn't handle USB enumeration**, not that it breaks something. The bootloader successfully starts enumeration, but our firmware doesn't continue it.

### read_regs.py Tool

Created tool to read XDATA registers from running firmware over USB:
- Uses E4 vendor command via SCSI BOT protocol
- Works with both ADD1:0001 and 174C:2464 devices
- Location: `/home/light/fun/asm2464pd-firmware/read_regs.py`

Usage:
```bash
python3 read_regs.py              # Dump all key registers
python3 read_regs.py 0x91C0 0x92C0  # Read specific addresses
```

### What Needs to Happen for Enumeration

1. **Firmware must respond to USB control transfers** - GET_DESCRIPTOR, SET_ADDRESS, etc.
2. **USB interrupts must be enabled** - C802 should generate INT0 when USB events occur
3. **EP0 must be armed** - Ready to receive setup packets
4. **Status registers update during events** - 9000, 9118 become non-zero during active transfers

### Next Steps

1. Compare original firmware's ISR handler more closely - what does it do when USB event arrives?
2. Check if EP0 arming (0x9301=0xC0) needs to be done continuously
3. The emulator's USB injection model doesn't match real hardware - need to update emulator
4. Consider polling approach: check 9E00 for setup packets in main loop instead of relying on interrupts

### Emulator TODO

The emulator should be updated to NOT inject artificial USB state. Instead:
- MMIO reads at 9000, 9118, C802 should return 0x00 until actual USB events
- USB events should be triggered by host interaction, not by cycle count
- This will make emulator behavior match real hardware

## Summary: What We've Done So Far

### Project Goal
Create minimal "clean" USB firmware for ASM2464PD that successfully enumerates as a USB device with VID:PID ADD1:0001.

### Working Baseline (Committed)
The committed `clean/src/main.c` (~104 lines, 850 bytes) achieves:
- **Device appears on USB bus** - Host sees device and attempts enumeration
- **Host sends GET_DESCRIPTOR** - We get `error -71` (EPROTO) in dmesg
- This means USB PHY is working, the device IS visible, enumeration IS starting

The minimal working init sequence:
```c
// Interrupt controller config
XDATA8(0xC800) = 0x05;  XDATA8(0xC801) = 0x10;
XDATA8(0xC805) = 0x02;  XDATA8(0xC807) = 0x04;
XDATA8(0xC8A6) = 0x04;  XDATA8(0xC8AA) = 0x03;
XDATA8(0xC8AC) = 0x07;  XDATA8(0xC8A1) = 0x80;
XDATA8(0xC8A4) = 0x80;  XDATA8(0xC8A9) = 0x01;
XDATA8(0xC8B2) = 0xBC;  XDATA8(0xC8B3) = 0x80;
XDATA8(0xC8B4) = 0xFF;  XDATA8(0xC8B5) = 0xFF;
XDATA8(0xC8B6) = 0x14;

// KEY: Disable PHY triggers USB visibility
XDATA8(0x91C0) = 0x00;

// Enable 8051 interrupts
TCON = 0x05;  IE = 0x85;
```

### Stashed Work (git stash@{0})
A more complete version with:
- Full USB device/config/string descriptors (ADD1:0001 "TinyBox")
- USB request handler for GET_DESCRIPTOR, SET_ADDRESS, SET_CONFIGURATION
- Polling for setup packets via 9091 bit 0 and 9E00 buffer
- ISR handler that reads C802, 9101, 9091

**Problem with stashed version**: It was detecting false "SETUP" packets because 9E00 contained 0x55 garbage from bootloader, and 9091 bit 0 was spuriously set.

### Key Discoveries

1. **91C0=0x00 triggers USB visibility** - Writing 0 to the PHY control register paradoxically makes device appear on bus

2. **USB ISR fires with C802=0x41** - We see interrupt status bits 0 and 6

3. **9101 updates dynamically** - Goes from 0x01 to 0x03 showing USB peripheral activity

4. **Error -71 means progress** - EPROTO = host IS talking to device, we're just not responding correctly

5. **EP0 response mechanism unclear**:
   - 0x9300 = EP0 TX length
   - 0x9301 = EP0 control (0xC0 arms for OUT, 0x01 possibly for IN?)
   - 0x9E00-0x9E07 = Setup packet buffer
   - EP0 data buffer location: probably 0x0160 (from DMA setup in original)

### What Needs to Happen for Enumeration

1. **Detect setup packet arrival** - via 9091 bit 0 OR polling 9E00 changes
2. **Parse setup packet** - Read 8 bytes from 9E00-9E07
3. **Respond to GET_DESCRIPTOR (Device)** - Write 18-byte descriptor to EP0 buffer, send
4. **Respond to SET_ADDRESS** - Send ZLP, then apply new address
5. **Respond to GET_DESCRIPTOR (Config)** - Full config descriptor
6. **Respond to SET_CONFIGURATION** - Send ZLP, device is enumerated

### USB Enumeration Flow
```
Host                                Device
  |-- GET_DESCRIPTOR (Device) 64b --> |  (setup: 80 06 00 01 00 00 40 00)
  |<-- 18-byte Device Descriptor ---- |  <-- WE FAIL HERE
  |-- SET_ADDRESS (XX) -------------> |  (setup: 00 05 XX 00 00 00 00 00)
  |<-- ZLP (status) ----------------- |
  |-- GET_DESCRIPTOR (Device) ------> |  (at new address)
  |<-- 18-byte Device Descriptor ---- |
  |-- GET_DESCRIPTOR (Config) ------> |
  |<-- Config Descriptor ------------ |
  |-- SET_CONFIGURATION (1) --------> |
  |<-- ZLP (status) ----------------- |
  ENUMERATION COMPLETE
```

### Next Steps

1. **Start from working baseline** (committed version that gets error -71)
2. **Add setup packet detection** - poll 9E00 for non-0x55 values indicating real setup packet
3. **Add GET_DESCRIPTOR response** - copy device descriptor to EP0 buffer, trigger send
4. **Find correct EP0 TX trigger** - experiment with 9301 values (0x01, 0x02, etc.)
5. **Verify EP0 buffer location** - may need to trace original firmware more closely

### Useful Commands
```bash
# Build and flash
cd /home/light/fun/asm2464pd-firmware/clean
make clean && make && make flash

# Monitor UART and USB together
cd /home/light/fun/asm2464pd-firmware
sudo dmesg -C && python3 ftdi_debug.py -r -t 5 & sleep 6 && sudo dmesg | tail -20

# Check for USB enumeration attempts
sudo dmesg | grep -iE "usb|error"

# Restore stashed work
git stash pop
```

## Session 5 Findings (2026-02-02)

### CRITICAL: Setup Packet Location

**Setup packets are at 0x9104-0x910B, NOT 0x9E00!**

From disassembly at 0xA5EA-0xA604:
- Firmware reads 8 bytes from 0x9104+offset (loop with r7)
- Copies them to RAM at 0x0ACE+offset
- 0x9E00 is the EP0 OUTPUT buffer where firmware writes response data

### USB Register Mapping (Corrected)

| Register | Purpose | Direction |
|----------|---------|-----------|
| 0x9104-0x910B | Setup packet (8 bytes) | HW → FW (read) |
| 0x9E00+ | EP0 data buffer | FW → HW (write) |
| 0x9300 | EP0 TX length | FW → HW (write) |
| 0x9301 | EP0 control | FW → HW (write) |
| 0x9091 | USB status flags | HW → FW (read) |
| 0x9092 | DMA trigger | FW → HW (write, 0x04=trigger) |

### Working: Setup Packet Reception

Our firmware now correctly receives and parses setup packets:
```
[SETUP@9104] 8006000100004000 S8006 D01Td
              ^^^^^^^^^^^^^^^^
              80 = bmRequestType (device-to-host)
              06 = bRequest (GET_DESCRIPTOR)
              00 01 = wValue (type=1=Device, index=0)
              00 00 = wIndex
              40 00 = wLength (64 bytes)
```

### Not Working: Response Transmission

We write descriptor to 0x9E00 and try to trigger DMA via 0x9092, but host still gets error -71.

Possible issues:
1. DMA source address not configured (0x9310-0x9311?)
2. Missing USB state machine step
3. Wrong trigger mechanism

### Original Firmware Descriptor Flow (from disassembly at 0xB343)

1. Call 0xA581 (some setup)
2. Write 0x02 to unknown location via DPTR
3. Write to 0x9E00 (clear/init)
4. Call 0xA563 (check link speed?)
5. Complex state machine based on descriptor type/speed
6. Eventually writes descriptor data to 0x9E00
7. Returns status code in R7 (0x03=success, 0x05=error)

The original firmware has a VERY complex USB state machine. It checks many status registers and has multiple code paths based on USB speed (USB2/USB3).

### Next Steps

1. **Trace original firmware** more carefully to understand DMA setup
2. **Check 0x9310-0x9311** for DMA source address configuration  
3. **Look at 0xA581 helper** function to understand setup phase
4. **Compare register state** between original and our firmware after descriptor handling

## Session Notes (2026-02-03) - MAJOR PROGRESS!

### GET_DESCRIPTOR Now Working!

After adding the proper EP0 transmission sequence based on original firmware disassembly at 0xa5a6:

```c
void usb_send_ep0(uint8_t len) {
    uint8_t tmp;
    
    XDATA8(0x07E1) = 0x00;
    XDATA8(0x07E9) = 0x01;
    
    tmp = XDATA8(0x9002);
    XDATA8(0x9002) = tmp & 0xFD;
    
    XDATA8(0x07EB) = 0x00;
    XDATA8(0x0AD6) = 0x00;
    XDATA8(0x9091) = 0x01;
    
    XDATA8(0x9004) = len;
    XDATA8(0x9003) = 0x00;
    XDATA8(0x90E3) = 0x02;
    XDATA8(0x9092) = 0x01;
}
```

The host now successfully receives our device descriptor! We see SET_ADDRESS commands:
```
[SETUP] 0005050000000000 S0005 A05
```

This means:
- bmRequestType = 0x00 (Host to Device, Standard, Device)
- bRequest = 0x05 (SET_ADDRESS)
- wValue = 0x0005 (address 5)

### New Error: Device Not Responding at Address

dmesg now shows:
```
usb 5-1.2: Device not responding to setup address.
usb 5-1.2: device not accepting address 122, error -71
```

This is PROGRESS! The error changed from "device descriptor read/64, error -71" to "Device not responding to setup address".

**What this means:**
1. GET_DESCRIPTOR succeeded - host got our device descriptor
2. SET_ADDRESS was sent by host
3. We sent ZLP status response (using same sequence as data)
4. Host acknowledged the SET_ADDRESS
5. Host now tries to communicate at the NEW address (122/123)
6. Device is NOT responding at the new address

**Root cause:** We're not correctly setting the USB device address in hardware.

### Key Discovery: USB Address Register

We tried `XDATA8(0x9118) = addr;` but 0x9118 might not be the USB address register.

**Need to find the correct address register.** Options to investigate:
- 0x9118 - USB status/state, maybe not address
- 0x9006 - EP0 config, maybe has address field
- 0x9301 - EP0 control
- Some register in 0x91xx or 0x92xx range

### USB Enumeration Flow Progress

```
Host                              Device              Status
 |-- GET_DESCRIPTOR (Device) -->  |                   
 |<-- 18-byte Device Descriptor - |                   ✓ WORKING!
 |-- SET_ADDRESS (addr=N) ------> |
 |<-- ZLP ----------------------- |                   ✓ ZLP sent
 |-- (now talks to addr N) -----> |
 |<-- ??? ----------------------- |                   ✗ FAILING - wrong address
```

### Next Steps for SET_ADDRESS

1. **Find USB address register** in original firmware disassembly
2. **Search for SET_ADDRESS handler** in original firmware
3. **Trace what registers change** after SET_ADDRESS in emulator
4. Per USB spec, address change takes effect AFTER status stage ZLP
5. May need to wait for some status bit before setting address

### Important: 0x07xx Addresses

The original firmware writes to 0x07E1, 0x07E9, 0x07EB etc. These might be:
- IDATA accessed via movx (unlikely for 8051)
- Shadow registers mapped to XDATA
- Internal USB controller state
- Need to verify these addresses are correct for our context

## Session Notes (2026-02-03 continued) - SET_ADDRESS Debugging

### Current Status: EP0 IN Not Transmitting

We're stuck at SET_ADDRESS. The sequence is:
1. GET_DESCRIPTOR received (8006 packets work)
2. Descriptor data copied to buffer (`[12010002]`)
3. Triggers written (0x9003, 0x9004, 0x90E3, 0x9092)
4. Host eventually receives descriptor (intermittent)
5. SET_ADDRESS received (0005 packets)
6. Address register (0x9006) updated correctly: `@10>85` (0x10 -> 0x85)
7. ZLP send attempted
8. ZLP completion TIMES OUT (0x9091 bit doesn't change)
9. Device doesn't respond at new address

### Key Findings

**Buffer Addresses:**
- 0x9E00: Allows receiving correct setup packets, gets to SET_ADDRESS
- 0x0160: Causes corrupted packets or immediate failures
- DMA (0x9310/0x9311) configured to 0x0160

**Address Register (0x9006):**
- Initial value: 0x10 (from bootloader)
- After SET_ADDRESS: 0x85 (address 5 + enable bit)
- Write DOES take effect

**ZLP Completion:**
- Polling 0x9091 bit 0 for status change -> TIMEOUT
- Tried both 0x1cca values (0x9093=0x08, 0x9094=0x02) and 0x1cd5 values (0x9093=0x02, 0x9094=0x10)
- Neither produces a completed transfer

### Theory

EP0 IN transfers aren't actually being transmitted by hardware. The DMA or trigger mechanism is wrong:
1. We write data to buffer
2. We set length in 0x9004
3. We trigger via 0x90E3=0x02, 0x9092=0x01
4. But hardware never actually sends the data

Possible causes:
- Wrong DMA source address configuration
- Missing EP0 IN enable bit
- Wrong trigger sequence
- Need to poll/wait for ready before triggering

### What Works (Partially)

GET_DESCRIPTOR sometimes succeeds - host receives our descriptor and sends SET_ADDRESS.
This proves the basic mechanism CAN work, but something's intermittent or we're missing a step.

### Next Steps

1. Trace original firmware's complete EP0 IN flow with emulator
2. Look for EP0 IN ready/enable bits we might be missing
3. Check if there's a status that needs to be polled before trigger
4. Compare register state between working and failing cases
