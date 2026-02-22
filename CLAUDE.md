**CRITICAL: ORIGINAL FIRMWARE (fw.bin) IS THE GOLDEN MASTER**
- The original firmware behavior is ALWAYS correct
- If our firmware does something different from original firmware, OUR FIRMWARE IS WRONG
- The emulator must be tested and working with the ORIGINAL firmware FIRST
- Never update the emulator to make our firmware work - only to match original firmware behavior
- When tracing or debugging, always verify against original firmware behavior

**CRITICAL: USB EMULATION MUST BE PURE DMA - ZERO PYTHON PROCESSING**
- The emulator must NOT parse, process, or understand USB control messages
- USB hardware DMAs in the request (setup packet) to MMIO registers
- Firmware reads setup packet, determines response, configures DMA source address registers
- USB hardware DMAs out the response from the address FIRMWARE configured
- ZERO addresses should be determined by Python code - all addresses come from firmware register writes
- The emulator just moves bytes between addresses the firmware specifies
- If something doesn't work, the fix is in the FIRMWARE, not adding logic to the emulator

We are reimplementing the firmware of the ASM2464PD chip in C in the src/ directory. The official firmware is in fw.bin.

We are trying to match each function in the original firmware to ours, giving good names to the functions and registers and structuring the src/ directory well.

Our firmware should build and the output should match fw.bin as close as possible. Build temporaries and artifacts should go in build/ The firmware we build should run on the device.

You can use radare on the fw.bin files to get the 8051 assembly, don't forget the 8051 architecture flag. Be aware 8051 only has a 64kb code size, so addresses above 0x8000 can be banked. There's an indirect jump/call to switch banks via DPX register (SFR 0x96).

Code address mapping:
- 0x0000-0x7FFF: Always maps to file offset 0x0000-0x7FFF (32KB shared)
- 0x8000-0xFF6B in bank 0: Maps to file offset 0x8000-0xFFFF
- 0x8000-0xFF6B in bank 1: Maps to file offset 0xFF6B + (addr - 0x8000)

fw.bin is `cat bank0.bin bank1.bin > fw.bin`
* bank0.bin is mapped at 0x0
* bank1.bin is mapped as 0x8000

Bank 1 file offsets: 0xFF6B-0x17F6A (mapped to code addresses 0x8000-0xFFFF)

BANK1 function addresses in comments should use the file offset for "actual addr", calculated as:
  file_offset = 0xFF6B + (code_addr - 0x8000)

ghidra.c is ghidra's attempt at C disassembly of the functions, you are welcome to reference it. Note: all the names in there may be wrong.

python/usb.py is tinygrad's library that talks to this chip. python/patch.py is a reflasher for this chip.

Every function you write should match one to one with a function in real firmware. Include the address range of the real function in a comment before our function.

Our firmware needs to have all the functionality of the real firmware, with all edge cases, state machines, and methods correctly implemented to match the behavior of the stock firmware.

Do not write things like `XDATA8(0xnnnn)`, instead define that as a register in include/registers.h with a good name with prefix "REG_" and use that register. Registers are addresses >= 0x6000, XDATA lower then this is global variables and shouldn't be in registers.h, put them in include/globals.h with the prefix "G_"

Do not use XDATA outside registers.h and globals.h! Don't use `*(__idata uint8_t *)0x16`, define that variable in globals. This is important.

Don't use extern void, instead include the correct header file.

Prioritize functions that you have already reversed the caller of.

Whenever you see a function or register with a name that includes the address in it, think about if you can give it a better name from context.

Registers and variables in general should not have aliases. Adding bit constants to registers.h similar to what's there is encouraged. You may not be the only one working in the repo. Don't do git checkout and make sure you read before you write.

All functions should exactly match the functions in the real firmware! It should match them one to one. This is the only way to ensure it is correct.

**CRITICAL: NO SHORTCUTS OR SIMPLIFIED IMPLEMENTATIONS**
- NEVER write comments like "simplified", "for now", "extensive register configuration" or skip functionality
- NEVER leave out register writes, helper function calls, or conditional logic from the original
- If the original firmware calls a helper function, you MUST implement and call that helper function
- If the original writes to 10 registers, your implementation MUST write to all 10 registers
- Every branch, every register write, every function call in the original must be replicated
- When in doubt, disassemble more of the original to understand the full behavior
- The goal is byte-for-byte behavioral equivalence, not "close enough"

**CRITICAL: NEVER REMOVE EXISTING CODE**
- Before modifying ANY function, READ the entire existing implementation first
- NEVER delete or "simplify" existing code - if code exists, it was added for a reason
- When adding new functionality, ADD to existing code, don't replace it
- If existing code seems wrong, VERIFY against the original firmware disassembly before changing
- Stubs (empty function bodies) are acceptable ONLY as placeholders until full implementation
- When implementing a stub, add the FULL implementation - don't partially implement
- Large functions (like ISRs) must include ALL sections from the original:
  - Disassemble the ENTIRE address range before implementing
  - Count the number of conditional branches, loops, and function calls
  - Verify your implementation has the same structure
- If you're unsure about existing code's purpose, trace it in the emulator first

Checking in and making sure it builds every once in a while is good. You can also see how far along you are by comparing the size of our compiled firmware bin to fw.bin

Before reverse engineering, check all the headers to see if the functions are already there.

All functions should be functionally the same as the ones in the real firmware and should be reconstructed from the real firmware. They should have headers like
```
/*
 * pcie_clear_address_regs - Clear address offset registers
 * Address: 0x9a9c-0x9aa2 (7 bytes)
 *
 * Clears IDATA locations 0x63 and 0x64 (address offset).
 *
 * Original disassembly:
 *   9a9c: clr a
 *   9a9d: mov r0, #0x63
 ...
```

For bank 1 it should look like
```
/*
 * pcie_addr_store - Store PCIe address with offset adjustment
 * Bank 1 Address: 0x839c-0x83b8 (29 bytes) [actual addr: 0x1039c]
 *
 * Calls e902 helper, loads current address from 0x05AF,
```
Update all functions that don't match this pattern.

Functions in the header file should have addresses
```
uint8_t pcie_get_link_speed(void);          /* 0x9a60-0x9a6b */
uint8_t pcie_get_link_speed_masked(void);   /* 0x9a30-0x9a3a */
```

## Emulator (emulate/ directory)

The emulator in emulate/ provides 8051 CPU emulation for testing and analyzing firmware behavior.

### CRITICAL: Emulator Updates for ORIGINAL Firmware Only

**NEVER update the emulator to make our firmware work.** The emulator must be updated ONLY to accurately emulate the behavior observed in the **original firmware (fw.bin)**.

- If a test fails for our firmware but passes for original firmware: Fix our firmware implementation
- If a test fails for original firmware: Investigate original firmware behavior and update emulator to match
- The emulator is the reference implementation of hardware behavior based on original firmware
- Our firmware must adapt to the emulator, not the other way around

### CRITICAL: Hardware Emulation Philosophy

The emulator MUST behave like real hardware. This means:
- **Only modify MMIO registers** (hardware state) - never directly modify RAM/XDATA/IDATA
- The firmware should naturally read MMIO registers and update its own RAM state
- If a test requires specific RAM values, the emulator must set MMIO registers that cause the firmware to write those values itself
- "Cheating" by directly writing RAM bypasses the firmware's state machines and leads to incorrect behavior

Example:
- BAD: `memory.xdata[0x05B1] = 0x04` (directly writing RAM)
- GOOD: Set MMIO registers that cause firmware to write 0x04 to 0x05B1 during normal processing

### CRITICAL: USB Descriptor Handling

**The emulator must NEVER search for USB descriptors in ROM or XDATA.**

The FIRMWARE is responsible for handling GET_DESCRIPTOR requests:
1. Firmware reads setup packet from MMIO (0x9E00-0x9E07)
2. Firmware looks up descriptor in its own code ROM
3. Firmware writes descriptor to USB transmit buffer via MMIO
4. USB hardware DMA sends the data to host

If you find yourself implementing `_find_descriptor_in_xdata()` or similar functions that search memory for USB descriptors, **you are doing something WRONG**. Instead, fix the MMIO emulation so the firmware's USB handler can complete successfully.

The emulator's job for USB:
- Provide correct MMIO register values for firmware to read
- Capture data that firmware writes to USB output registers
- Signal completion via status registers

### Debugging firmware execution:
- Add trace points and PC tracking features directly to the emulator (emulate/emu.py, emulate/hardware.py)
- Don't create temporary test scripts; add debugging helpers as emulator features
- Use `emu.setup_watch(addr, name)` to trace XDATA reads/writes at specific addresses
- Use `emu.trace_pcs.add(addr)` to trace when specific PC addresses are executed
- Enable `emu.hw.log_reads` and `emu.hw.log_writes` for MMIO debugging

### Key emulator files:
- emulate/emu.py: Main Emulator class with run(), reset(), load_firmware()
- emulate/cpu.py: 8051 CPU emulation
- emulate/memory.py: Memory system (code, xdata, idata, sfr)
- emulate/hardware.py: MMIO register emulation and USB/PCIe hardware simulation

### USB vendor command testing:
- `emu.hw.inject_usb_command(cmd_type, xdata_addr, size=N)` injects E4/E5 commands
- The hardware emulation should set MMIO registers that trigger firmware's USB state machine
- Firmware reads USB CDB from MMIO registers (0x910D-0x9112) and processes naturally
- Check result at XDATA[0x8000] for E4 read responses

## Clean Firmware (clean/ directory)

The clean firmware in `clean/src/main.c` is a minimal from-scratch reimplementation for the
ASM2464PD USB4-to-NVMe bridge, targeting tinygrad GPU communication via USB.

### Build & Flash

```bash
# Build only:
cd clean && make

# Flash (handles wrapping + FTDI reset):
cd clean && rm -f build/firmware_wrapped.bin && make flash

# Flash without UART (keeps USB alive for testing):
cd /home/geohot/asm2464pd-firmware && python3 ftdi_debug.py -bn && sleep 8 && \
  python3 flash.py clean/build/firmware.bin && python3 ftdi_debug.py -rn

# Read UART (NOTE: resets device!):
python3 ftdi_debug.py -rt 15

# Read UART without resetting:
python3 -c "
from ftdi_debug import USBGPUDebug
import time, sys
with USBGPUDebug() as dbg:
    start = time.perf_counter()
    while time.perf_counter() - start < 5:
        sys.stdout.write(dbg.read())
        sys.stdout.flush()
        time.sleep(0.001)
"

# Flash stock firmware for comparison:
python3 ftdi_debug.py -bn && sleep 8 && python3 flash.py fw_tinygrad.bin && python3 ftdi_debug.py -rn

# Run tinygrad test:
cd /home/geohot/tinygrad && sudo PYTHONPATH=/home/geohot/tinygrad AMD_IFACE=usb AMD=1 \
  python3 test/test_tiny.py TestTiny.test_plus

# Read register via E4 control transfer (only works with clean firmware):
sudo python3 -c "
import sys, ctypes; sys.path.insert(0, '/home/geohot/tinygrad')
from tinygrad.runtime.autogen import libusb
from tinygrad.runtime.support.usb import USB3
usb = USB3(0xADD1, 0x0001, 0x81, 0x83, 0x02, 0x04, use_bot=True)
buf = (ctypes.c_uint8 * 4)()
rc = libusb.libusb_control_transfer(usb.handle, 0xC0, 0xE4, 0xC471, 0, buf, 1, 1000)
print(f'C471 = 0x{buf[0]:02X}')
"
```

### Current Status

1. **USB3 SuperSpeed enumeration** — WORKING. Device enumerates as `add1:0001`.
2. **PCIe link training** — WORKING. E762 bit 4 persists, B22B=0x04 (x4).
3. **CBW reception** — WORKING. 9101 bit 6 fires, CE88/CE89 DMA handshake completes.
4. **CSW send (bulk IN)** — BROKEN. C42C trigger consumed but no USB packet generated.

### CRITICAL: Stock Firmware CSW Works on USB3 with GPU

**Confirmed**: The stock firmware (fw_tinygrad.bin) successfully sends CSW via C42C=0x01
on USB3 even with a GPU (non-NVMe) connected. C471=0x00 (NVMe queue not busy) does NOT
block C42C on USB3 — the earlier assumption was wrong. The blocker is something else
in our MSC engine configuration.

### CRITICAL: Interface Descriptor Must Use Mass Storage Class

Stock firmware uses **class=0x08, subclass=0x06, protocol=0x50** (Mass Storage / SCSI / BOT).
Our clean firmware previously used class=0xFF (vendor-specific). The ASM2464PD MSC hardware
engine may require class 0x08 to enable C42C bulk IN routing.

**WARNING**: Using class 0x08 causes the OS mass storage driver to claim the device.
Tinygrad handles this via `libusb_detach_kernel_driver()`, but `libusb_open` may fail
if the kernel driver is mid-enumeration. Retry or delay may be needed.

### Stock Firmware MSC Engine Init (0xB1C5-0xB24F)

The stock firmware has a dedicated MSC engine initialization function at 0xB1C5
(called via banked jump at 0x0327). This runs during SET_INTERFACE/configuration
and sets critical registers BEFORE the first C42C=0x01:

```
92C0 |= 0x80     Power enable bit 7
91D1 = 0x0F      Clear all link events
9300 = 0x0C      Buffer config
9301 = 0xC0      Buffer config
9302 = 0xBF      Buffer config
9091 = 0x1F      Control phase (may be volatile — doesn't stick after init)
9093 = 0x0F      EP status (may be volatile)
91C1 = 0xF0      PHY control
9303 = 0x33      Buffer descriptor (retains value)
9304 = 0x3F      Buffer descriptor (retains value)
9305 = 0x40      Buffer descriptor (retains value)
9002 = 0xE0      USB config
9005 = 0xF0      EP0 length high (retains value)
90E2 = 0x01      MSC gate register — CRITICAL for C42C
905E &= ~0x01    EP control
C42C = 0x01      Initial MSC engine arm
C42D &= ~0x01    Clear MSC status
```

Then post-C42C:
```
call 0xCF3D(R7=0)   NVMe PRP/queue register init (C430, C440, 9096-9098, C448, 9011, 9018)
call 0xDF5E          NVMe link init (C428 &= ~0x08, C473 bits 6+5)
91C3 &= ~0x20        PHY control
91C0 toggle bit 0    PHY reset
call 0x54BB          Clears G_PCIE_ENUM_DONE (0x0AF7 = 0x00)
call 0xE292(R5=0x8F, R4=0x01, R7=0x04)  Endpoint config
```

**NOTE**: Many of these registers (9091, 9093, 9300-9302, 91D1, 90E2) are volatile —
they don't retain values after hardware processing. They may need to be set at the
right time relative to the C42C trigger and hardware state machine.

### Stock Firmware ISR CBW Flow (0x0E33)

The stock ISR uses a **two-pass mechanism** for CBW processing:

**Pass 1** (CBW just arrived):
1. C802 bit 0 check (interrupt gate)
2. 9101 bit 5 (EP_COMPLETE) — from initial arm_msc C42C=0x01
   - If 9000 bit 0 NOT set → 9096 path → sets 90E2=0x01 → EXIT
3. 9101 bit 6 (CBW_RECEIVED) — if 90E2=0, EXIT immediately

**Pass 2** (90E2 now set from pass 1):
1. 9101 bit 6 (CBW_RECEIVED) + 90E2=0x01 → proceed
2. Check 9000 bit 0:
   - SET + C47B==0: call 0x17DB(R7=2) DMA setup, set 90E2=0x01, EXIT
   - SET + C47B!=0: check C471 → call 0x116E
   - NOT SET: set 90E2=0x01, call 0x3458 (CBW process → scsi_csw_build)

**Pass 2 (9000 NOT set)** is the TUR path: calls 0x3458 which does:
1. CE88/CE89 DMA handshake
2. Check G_PCIE_ENUM_DONE (0x0AF7) == 0x01
3. Call 0x4CE7 (CBW parse → eventually scsi_csw_build at 0x494D)
4. scsi_csw_build: doorbell dance + "USBS" to D800 + C42C=0x01 + C42D clear
5. Jump to 0xC16C (post-CSW cleanup: clear 0x000A/B/0052, C42A LINK_GATE toggle)

### Key Differences Between Stock and Clean Firmware

| Aspect | Stock | Clean |
|--------|-------|-------|
| Interface class | 0x08 (Mass Storage) | 0xFF (vendor) → changed to 0x08 |
| MSC engine init | Full 0xB1C5 sequence | Missing most registers |
| ISR CBW handling | Two-pass with 90E2 gate | Direct single-pass |
| 0x0AF7 (PCIE_ENUM_DONE) | Set to 0x01 after PCIe init | Never set |
| Post-CSW cleanup | 0xC16C: clear vars + C42A LINK_GATE | Partial |
| arm_msc after CSW | NOT done (re-arm via cleanup) | Was done immediately (double C42C) |

### C42C Write Locations in Stock Firmware

1. **0x49B5** — scsi_csw_build: CSW send trigger (doorbell dance + USBS + C42C=0x01)
2. **0xB21C** — MSC engine init (0xB1C5): Initial MSC arm during bulk setup
3. **0x1150** — ISR Timer0 exit: C42C acknowledge (read bit 0, if set → call 0x47D5 → write 0x01)

### Next Steps for CSW Fix

1. **Add full MSC engine init from 0xB1C5** — but be careful about register volatility;
   some registers (9091, 9300-9302) may need to be written at specific points in the
   hardware state machine, not just during init.
2. **Investigate timing** — C42C may need to be written while specific hardware state
   bits are active (e.g., 90E2=0x01 must be set immediately before C42C, not earlier).
3. **Consider implementing the two-pass ISR mechanism** — the 90E2 gate may be
   essential for hardware to set up the bulk IN DMA path before CSW is triggered.
4. **Set G_PCIE_ENUM_DONE (0x0AF7) = 0x01** — stock firmware requires this for
   CBW processing at 0x34A3; our firmware skips this check but it may affect
   hardware state.
