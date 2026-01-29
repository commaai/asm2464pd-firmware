# ASM2464PD Firmware Makefile
#
# Builds the firmware for the ASM2464PD USB4/Thunderbolt NVMe controller
# using SDCC (Small Device C Compiler) for the 8051 architecture

# Compiler and tools
CC = sdcc
AS = sdas8051
LD = sdld
PACKIHX = packihx
OBJCOPY = sdobjcopy

# Compiler flags
CFLAGS = -mmcs51 --model-large --opt-code-size
CFLAGS += --std-c99
CFLAGS += -I$(SRC_DIR) -I$(SRC_DIR)/include
CFLAGS += --no-xinit-opt

# Memory model settings for ASM2464PD
# CODE: 0x0000 - 0xFFFF (64KB)
# XDATA: 0x0000 - 0xFFFF (64KB external RAM)
# IDATA: 0x00 - 0xFF (256 bytes internal RAM)
LDFLAGS = --code-loc 0x0000 --code-size 0x17F12
LDFLAGS += --xram-loc 0x0000 --xram-size 0x10000
LDFLAGS += --iram-size 0x100
LDFLAGS += --no-std-crt0

# Directories
SRC_DIR = src
DRIVERS_DIR = $(SRC_DIR)/drivers
APP_DIR = $(SRC_DIR)/app
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj

# Source files
CRT0_SRC = $(SRC_DIR)/crt0.s
MAIN_SRCS = $(SRC_DIR)/main.c $(SRC_DIR)/utils.c $(SRC_DIR)/interrupt.c
DRIVER_SRCS = $(wildcard $(DRIVERS_DIR)/*.c)
APP_SRCS = $(wildcard $(APP_DIR)/*.c)
SRCS = $(MAIN_SRCS) $(DRIVER_SRCS) $(APP_SRCS)

# Object files - crt0 must be linked first!
CRT0_OBJ = $(OBJ_DIR)/crt0.rel
MAIN_OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.rel,$(MAIN_SRCS))
DRIVER_OBJS = $(patsubst $(DRIVERS_DIR)/%.c,$(OBJ_DIR)/drivers/%.rel,$(DRIVER_SRCS))
APP_OBJS = $(patsubst $(APP_DIR)/%.c,$(OBJ_DIR)/app/%.rel,$(APP_SRCS))
OBJS = $(CRT0_OBJ) $(MAIN_OBJS) $(DRIVER_OBJS) $(APP_OBJS)

# Output files
TARGET = $(BUILD_DIR)/firmware
HEX = $(TARGET).ihx
BIN = $(TARGET).bin

# Original firmware for comparison
ORIGINAL_FW = fw.bin

.PHONY: all clean compare disasm hex

all: $(BIN)

# Create directories
$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)
	mkdir -p $(OBJ_DIR)/drivers
	mkdir -p $(OBJ_DIR)/app

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Assemble startup code
$(CRT0_OBJ): $(CRT0_SRC) | $(OBJ_DIR)
	$(AS) -l -o -s $<
	mv $(SRC_DIR)/crt0.rel $(CRT0_OBJ)
	-mv $(SRC_DIR)/crt0.lst $(OBJ_DIR)/ 2>/dev/null || true
	-mv $(SRC_DIR)/crt0.sym $(OBJ_DIR)/ 2>/dev/null || true

# Compile main source files
$(OBJ_DIR)/%.rel: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile driver source files
$(OBJ_DIR)/drivers/%.rel: $(DRIVERS_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile app source files
$(OBJ_DIR)/app/%.rel: $(APP_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Link object files to Intel HEX
$(HEX): $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@

# Convert Intel HEX to binary
$(BIN): $(HEX)
	@echo "Converting HEX to BIN..."
	@if command -v objcopy > /dev/null 2>&1; then \
		objcopy -I ihex -O binary $< $@; \
	elif command -v sdobjcopy > /dev/null 2>&1; then \
		sdobjcopy -I ihex -O binary $< $@; \
	else \
		python3 -c "import sys; \
data = bytearray(0x18000); \
for line in open('$<'): \
    if line[0] != ':': continue; \
    n = int(line[1:3], 16); \
    addr = int(line[3:7], 16); \
    t = int(line[7:9], 16); \
    if t == 0: \
        for i in range(n): \
            data[addr+i] = int(line[9+i*2:11+i*2], 16); \
open('$@', 'wb').write(bytes(data[:sum(1 for b in data if b)]))"; \
	fi
	@echo "Built: $@"

# Add ASM2464 firmware wrapper (body_len + body + magic + checksum + crc)
$(BUILD_DIR)/firmware_wrapped.bin: $(BIN)
	@echo "Wrapping firmware with ASM2464 header..."
	@python3 -c "\
import zlib; \
body = open('$<', 'rb').read(); \
body_len = len(body).to_bytes(4, 'little'); \
checksum = bytes([sum(body) & 0xFF]); \
crc = zlib.crc32(body).to_bytes(4, 'little'); \
magic = bytes([0xA5]); \
open('$@', 'wb').write(body_len + body + magic + checksum + crc)"
	@echo "Built: $@"

# Create wrapped firmware for flashing
wrapped: $(BUILD_DIR)/firmware_wrapped.bin

# Compare with original firmware
compare: $(BIN)
	@echo "Comparing with original firmware..."
	@if [ -f $(ORIGINAL_FW) ]; then \
		echo "Original size: $$(stat -c %s $(ORIGINAL_FW)) bytes"; \
		echo "Built size: $$(stat -c %s $(BIN)) bytes"; \
		echo "Diff (first 256 bytes):"; \
		xxd $(ORIGINAL_FW) | head -16 > /tmp/orig.hex; \
		xxd $(BIN) | head -16 > /tmp/built.hex; \
		diff /tmp/orig.hex /tmp/built.hex || true; \
	else \
		echo "Original firmware $(ORIGINAL_FW) not found"; \
	fi

# Disassemble the built firmware
disasm: $(BIN)
	@echo "Disassembling built firmware..."
	@r2 -a 8051 -q -c 'aaa; pdf @ 0' $(BIN) 2>/dev/null || \
		echo "radare2 not available for disassembly"

# Show hex dump of built firmware
hex: $(BIN)
	xxd $(BIN) | head -64

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
	rm -f $(SRC_DIR)/*.asm $(SRC_DIR)/*.lst $(SRC_DIR)/*.sym
	rm -f $(SRC_DIR)/*.rel $(SRC_DIR)/*.rst $(SRC_DIR)/*.map
	rm -f $(DRIVERS_DIR)/*.asm $(DRIVERS_DIR)/*.lst $(DRIVERS_DIR)/*.sym
	rm -f $(DRIVERS_DIR)/*.rel $(DRIVERS_DIR)/*.rst $(DRIVERS_DIR)/*.map
	rm -f $(APP_DIR)/*.asm $(APP_DIR)/*.lst $(APP_DIR)/*.sym
	rm -f $(APP_DIR)/*.rel $(APP_DIR)/*.rst $(APP_DIR)/*.map
	rm -f *.lk *.mem *.map

# Show help
help:
	@echo "ASM2464PD Firmware Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build firmware binary (default)"
	@echo "  wrapped  - Build firmware with ASM2464 header for flashing"
	@echo "  compare  - Compare built firmware with original"
	@echo "  disasm   - Disassemble built firmware"
	@echo "  hex      - Show hex dump of built firmware"
	@echo "  clean    - Remove build artifacts"
	@echo "  help     - Show this help message"
	@echo ""
	@echo "Requirements:"
	@echo "  - SDCC (Small Device C Compiler)"
	@echo "  - Python 3 (for firmware wrapping)"
	@echo "  - radare2 (optional, for disassembly)"

# Debug: show variables
debug:
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "SRCS: $(SRCS)"
	@echo "OBJS: $(OBJS)"

flash: $(BUILD_DIR)/firmware_wrapped.bin
	@echo "Flashing firmware to device..."
	@python3 flash.py $<
