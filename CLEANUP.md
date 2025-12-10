Always keep in mind CLAUDE.md while working.

XDATA should not be used anywhere outside registers.h and globals.h unless you absolutely need to, like for a dynamic offset. Registers should be defined in the headers and used. Registers and globals should not have aliases, they should have one correct name used everywhere.

Add register bit defines as apppropriate. Using magic numbers in the code is frowned upon. Do not just define generic magic mumbers, only define bits if they are actually in the hardware register.

Functions and registers should not have names like helper_XXXX or handler_XXXX or reg_XXXX. Give them a name based on what the function or register does, but only if you are confident.

extern void should not be used to call functions. The proper header file should be included. If there's a mismatch, fix what is needed to match the real firmware.

Remove inline assembly and replace it with C code that does the same thing if possible.

Remove or merge duplicate functions that are at the same address. Remember, all functions should match the cooresponding function in the real firmware.

If functions are in main.c/utils.c that are actually specific to one driver/app they should be moved there.

Confirm the build still works.