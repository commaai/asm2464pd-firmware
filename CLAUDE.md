We are reimplementing the firmware of the ASM2464PD chip in C in the src/ directory. The official firmware is in fw.bin.

We are trying to match each function in the original firmware to ours, giving good names to the functions and registers and structuring the src/ directory well.

Our firmware should build and the output should match fw.bin as close as possible. Build temporaries and artifacts should go in build/ The firmware we build should run on the device.

You can use radare on the fw.bin files to get the 8051 assembly.

registers.h is some reverse engineer registers. They may be wrong.

ghidra.c is ghidra's attempt at C disassembly of the functions, you are welcome to reference it. Note: all the names in there may be wrong.

usb-to-pcie-re is an attempt to reverse engineer this chip. Read usb-to-pcie-re/ASM2x6x/doc/Notes.md

usb.py is tinygrad's library that talks to this chip.

Every function you write should match one to one with a function in real firmware. Include the address range of the real function in a comment before our function.

Our firmware needs to have all the functionality of the real firmware, with all edge cases, state machines, and methods correctly implemented to match the behavior of the stock firmware.

Do not write things like `XDATA8(0xC8B6)`, instead define that as a register in registers.h with a good name with prefix "REG_" and use that register. Registers are addresses >= 0x6000, XDATA lower then this is global variables and shouldn't be in registers.h, put them in globals.h with the prefix "G_"

Prioritize functions that you have already reversed the caller of.
