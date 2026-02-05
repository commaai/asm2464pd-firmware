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
