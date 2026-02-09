# ESP-01S Raw 802.11 Radio Transceiver Firmware
# Bare-metal C with ESP8266 Non-OS SDK
# Target: AITRIP ESP-01S (1MB flash, ESP8266EX)

# =============================================================================
# TOOLCHAIN CONFIGURATION
# =============================================================================
XTENSA_TOOLS_ROOT ?= /usr/bin
SDK_BASE          := /home/moosh/esp-tools/ESP8266_NONOS_SDK
ESPTOOL           ?= esptool

# Compiler toolchain
CC                := $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-gcc
AR                := $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-ar
LD                := $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-gcc
OBJCOPY           := $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-objcopy
OBJDUMP           := $(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-objdump

# =============================================================================
# PROJECT STRUCTURE
# =============================================================================
SRC_DIR           := src
LD_DIR            := ld
BUILD_DIR         := build
BIN_DIR           := bin

# Source files
SOURCES           := $(wildcard $(SRC_DIR)/*.c)
OBJECTS           := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SOURCES))

# Output files
TARGET            := esp-radio
ELF_FILE          := $(BIN_DIR)/$(TARGET).elf
BIN_FILE          := $(BIN_DIR)/$(TARGET).bin

# =============================================================================
# SDK PATHS AND LIBRARIES
# =============================================================================
SDK_INCLUDES      := -I$(SDK_BASE)/include \
                     -I$(SDK_BASE)/include/json \
                     -I$(SDK_BASE)/driver_lib/include

SDK_LIBDIR        := $(SDK_BASE)/lib

# SDK libraries - ORDER MATTERS!
# -lmain must come first, wrap all with --start-group/--end-group for circular deps
SDK_LIBS          := -Wl,--start-group -lmain -lnet80211 -lwpa -lcrypto -llwip -lpp -lphy -lwpa2 -lc -lgcc -lhal -Wl,--end-group

# =============================================================================
# COMPILER FLAGS
# =============================================================================
CFLAGS            := -Os \
                     -g \
                     -DSPI_SIZE_MAP=2 \
                     -Wpointer-arith \
                     -Wundef \
                     -Werror \
                     -Wl,-EL \
                     -fno-inline-functions \
                     -nostdlib \
                     -mlongcalls \
                     -mtext-section-literals \
                     -ffunction-sections \
                     -fdata-sections \
                     -DICACHE_FLASH \
                     -I$(SRC_DIR) \
                     $(SDK_INCLUDES)

# =============================================================================
# LINKER FLAGS
# =============================================================================
LDFLAGS           := -nostdlib \
                     -Wl,--no-check-sections \
                     -Wl,--gc-sections \
                     -u call_user_start \
                     -Wl,-static \
                     -Wl,-Map=$(BIN_DIR)/$(TARGET).map

# Linker scripts (main script + ROM addresses)
LD_SCRIPT         := $(LD_DIR)/eagle.app.v6.ld
ROM_SCRIPT        := $(SDK_BASE)/ld/eagle.rom.addr.v6.ld

# =============================================================================
# FLASH CONFIGURATION (ESP-01S: 1MB flash)
# =============================================================================
# Memory layout for 1MB flash (Non-OTA, no bootloader):
# 0x00000 - eagle.flash.bin (iram/dram sections)
# 0x10000 - eagle.irom0text.bin (irom sections, XIP)
# 0xFB000 - blank.bin
# 0xFC000 - esp_init_data_default.bin (RF calibration)
# 0xFE000 - blank.bin

FLASH_MODE        := dio
FLASH_FREQ        := 26m
FLASH_SIZE        := 1MB
BAUD_RATE         := 460800
SERIAL_PORT       ?= /dev/ttyUSB0

# SDK binary files for flashing
BOOT_BIN          := $(SDK_BASE)/bin/boot_v1.7.bin
INIT_DATA_BIN     := $(SDK_BASE)/bin/esp_init_data_default_v08.bin
BLANK_BIN         := $(SDK_BASE)/bin/blank.bin

# =============================================================================
# BUILD TARGETS
# =============================================================================

.PHONY: all clean flash monitor size help

all: $(BIN_FILE)
	@echo "================================================"
	@echo "Build complete!"
	@echo "Firmware: $(BIN_FILE)"
	@echo "Size:"
	@$(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-size $(ELF_FILE)
	@echo "================================================"

# Create directories
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	@mkdir -p $(BIN_DIR)

# Compile .c files to .o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@echo "CC $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# Link to .elf
$(ELF_FILE): $(OBJECTS) | $(BIN_DIR)
	@echo "LD $@"
	@$(LD) -L$(SDK_LIBDIR) -T$(LD_SCRIPT) -T$(ROM_SCRIPT) $(LDFLAGS) -Wl,--start-group $(OBJECTS) $(SDK_LIBS) -Wl,--end-group -o $@

# Convert .elf to .bin (version 1 for Non-OS SDK)
$(BIN_FILE): $(ELF_FILE)
	@echo "Generating binary images..."
	@$(ESPTOOL) elf2image --version 1 -o $(BIN_DIR)/$(TARGET) $<
	@echo "Binary files created:"
	@ls -lh $(BIN_DIR)/*.bin

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Clean complete."

# Flash firmware to ESP-01S via CH340G adapter
# NOTE: Set adapter to PROG mode before flashing!
flash: $(BIN_FILE)
	@echo "================================================"
	@echo "Flashing ESP-01S via $(SERIAL_PORT)"
	@echo "Ensure adapter is in PROG mode!"
	@echo "================================================"
	$(ESPTOOL) --port $(SERIAL_PORT) --baud $(BAUD_RATE) write_flash \
		--flash_mode $(FLASH_MODE) \
		--flash_freq $(FLASH_FREQ) \
		--flash_size $(FLASH_SIZE) \
		0x00000 $(BIN_DIR)/$(TARGET)0x00000.bin \
		0x10000 $(BIN_DIR)/$(TARGET)0x10000.bin \
		0xFB000 $(BLANK_BIN) \
		0xFC000 $(INIT_DATA_BIN) \
		0xFE000 $(BLANK_BIN)
	@echo "================================================"
	@echo "Flash complete!"
	@echo "Set adapter to UART mode and reset to run."
	@echo "================================================"

# Serial monitor (requires 'screen' installed)
monitor:
	@echo "Starting serial monitor on $(SERIAL_PORT) @ $(BAUD_RATE) baud"
	@echo "Press Ctrl+A then K to exit"
	screen $(SERIAL_PORT) $(BAUD_RATE)

# Show code size breakdown
size: $(ELF_FILE)
	@echo "Code size analysis:"
	@$(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-size -A $(ELF_FILE)
	@echo ""
	@echo "Section sizes:"
	@$(XTENSA_TOOLS_ROOT)/xtensa-lx106-elf-size -B $(ELF_FILE)

# Help target
help:
	@echo "ESP-01S Raw Radio Firmware - Makefile Targets"
	@echo "=============================================="
	@echo "  make all      - Build firmware (default)"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make flash    - Flash firmware to ESP-01S"
	@echo "  make monitor  - Open serial monitor (screen)"
	@echo "  make size     - Show code size breakdown"
	@echo "  make help     - Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  SERIAL_PORT=$(SERIAL_PORT)"
	@echo "  BAUD_RATE=$(BAUD_RATE)"
	@echo ""
	@echo "Flash procedure:"
	@echo "  1. Insert ESP-01S into CH340G adapter"
	@echo "  2. Set adapter switch to PROG mode"
	@echo "  3. Plug USB cable"
	@echo "  4. Run: make flash"
	@echo "  5. Unplug USB, set to UART mode, plug back in"
