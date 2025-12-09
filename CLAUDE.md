We are reimplementing the firmware of the ASM2464PD chip in C in the src/ directory. The official firmware is in fw.bin.

We are trying to match each function in the original firmware to ours, giving good names to the functions and registers and structuring the src/ directory well.

Our firmware should build and the output should match fw.bin as close as possible. Build temporaries and artifacts should go in build/ The firmware we build should run on the device.

You can use radare on the fw.bin files to get the 8051 assembly, don't forget the 8051 architecture flag. Be aware 8051 only has a 64kb code size, so addresses after 0x10000 are in the second bank. There's an indirect jump/call to get to that bank, and I believe the second bank is mapped at 0x8000 (so 0x8000 is 0x10000 if you are in second bank)

0-0x8000 is always 0-0x8000
In bank 0:  0x8000-0x10000 is 0x8000-0x10000
In bank 1: 0x10000-0x18000 is 0x8000-0x10000

ghidra.c is ghidra's attempt at C disassembly of the functions, you are welcome to reference it. Note: all the names in there may be wrong.

python/usb.py is tinygrad's library that talks to this chip. python/patch.py is a reflasher for this chip.

Every function you write should match one to one with a function in real firmware. Include the address range of the real function in a comment before our function.

Our firmware needs to have all the functionality of the real firmware, with all edge cases, state machines, and methods correctly implemented to match the behavior of the stock firmware.

Do not write things like `XDATA8(0xnnnn)`, instead define that as a register in include/registers.h with a good name with prefix "REG_" and use that register. Registers are addresses >= 0x6000, XDATA lower then this is global variables and shouldn't be in registers.h, put them in include/globals.h with the prefix "G_"

Do not use XDATA outside registers.h and globals.h! Don't use `*(__idata uint8_t *)0x16`, define that variable in globals. This is important.

Prioritize functions that you have already reversed the caller of.

Whenever you see a function or register with a name that includes the address in it, think about if you can give it a better name from context.

Registers and variables in general should not have aliases. Adding bitfields to registers.h similar to what's there is encouraged.

You may not be the only one working in the repo. Don't do git checkout and make sure you read before you write.

Checking in and making sure it builds every once in a while is good. You can also see how far along you are by comparing the size of our compiled firmware bin to fw.bin
