###############################################################################
#	Makefile — rpitos
#	Bare-metal RTOS for Raspberry Pi CM4 (BCM2711, Cortex-A72, AArch32)
###############################################################################

ARMGNU ?= arm-none-eabi

BUILD   = build/
TARGET  = kernel7l.img
LIST    = kernel.list
MAP     = kernel.map
LINKER  = kernel.ld

# GCC flags for bare-metal Cortex-A72 AArch32
CFLAGS = -mcpu=cortex-a72 -marm -ffreestanding -nostdlib -O2 -Wall \
         -Idrivers/inc -Iinc -Ilibraries/inc -Ikernel/inc

# Source directories
ASM_SRCS := $(wildcard startup/*.s)
C_SRCS   := $(wildcard source/*.c) $(wildcard drivers/src/*.c) $(wildcard libraries/src/*.c) $(wildcard kernel/src/*.c)

# Object files — all land flat in build/
OBJECTS := $(patsubst startup/%.s,   $(BUILD)%.o, $(ASM_SRCS)) \
           $(patsubst source/%.c,    $(BUILD)%.o, $(filter source/%, $(C_SRCS))) \
           $(patsubst drivers/src/%.c, $(BUILD)%.o, $(filter drivers/src/%, $(C_SRCS))) \
           $(patsubst libraries/src/%.c, $(BUILD)%.o, $(filter libraries/src/%, $(C_SRCS))) \
           $(patsubst kernel/src/%.c, $(BUILD)%.o, $(filter kernel/src/%, $(C_SRCS)))

# Rules
all: $(TARGET) $(LIST)

rebuild: clean all

$(LIST): $(BUILD)output.elf
	$(ARMGNU)-objdump -d $(BUILD)output.elf > $(LIST)

$(TARGET): $(BUILD)output.elf
	$(ARMGNU)-objcopy $(BUILD)output.elf -O binary $(TARGET)

LIBGCC := $(shell $(ARMGNU)-gcc $(CFLAGS) -print-libgcc-file-name)

$(BUILD)output.elf: $(OBJECTS) $(LINKER)
	$(ARMGNU)-ld --no-undefined $(OBJECTS) -Map $(MAP) -o $(BUILD)output.elf -T $(LINKER) $(LIBGCC)

$(BUILD)%.o: startup/%.s | $(BUILD)
	$(ARMGNU)-as $< -o $@

$(BUILD)%.o: source/%.c | $(BUILD)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(BUILD)%.o: drivers/src/%.c | $(BUILD)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(BUILD)%.o: libraries/src/%.c | $(BUILD)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(BUILD)%.o: kernel/src/%.c | $(BUILD)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	-rm -rf $(BUILD)
	-rm -f $(TARGET)
	-rm -f $(LIST)
	-rm -f $(MAP)

# ── QEMU ─────────────────────────────────────────────────────────────────────
# Requires: qemu-system-arm in PATH
#   Windows:  winget install QEMU.QEMU   (then re-open terminal)
#   macOS:    brew install qemu
#   Linux:    sudo apt install qemu-system-arm
#
# -M raspi4b         : Raspberry Pi 4B machine (closest QEMU model to CM4)
# -kernel            : raw binary image (kernel7l.img)
# -serial stdio      : UART0 (PL011 @ 0xFE201000) → your terminal
# -nographic         : no GUI window, everything in terminal
# -d int             : log every CPU exception/interrupt taken
# -D qemu.log        : write that log to qemu.log (open separately to read)
#
# Exit QEMU: Ctrl+A, then X
QEMU = qemu-system-arm

qemu: $(BUILD)output.elf
	$(QEMU) -M raspi2b -kernel $(BUILD)output.elf -serial stdio -display none -monitor none -d int -D qemu.log
