###############################################################################
#	Makefile — rpitos
#	Bare-metal RTOS for Raspberry Pi CM4 (BCM2711, Cortex-A72, AArch32)
###############################################################################

ARMGNU ?= arm-none-eabi

BUILD   = build/
TARGET  = kernel7l.img
ELF     = kernel7l.elf
HEX     = kernel7l.hex
LIST    = kernel.list
MAP     = kernel.map
LINKER  = kernel.ld

# GCC flags for bare-metal Cortex-A72 AArch32
CFLAGS = -mcpu=cortex-a72 -marm -ffreestanding -nostdlib -O2 -Wall -g \
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
all: $(TARGET) $(ELF) $(HEX) $(LIST)

rebuild: clean all

$(LIST): $(BUILD)output.elf
	$(ARMGNU)-objdump -d $(BUILD)output.elf > $(LIST)

$(TARGET): $(BUILD)output.elf
	$(ARMGNU)-objcopy $(BUILD)output.elf -O binary $(TARGET)

$(ELF): $(BUILD)output.elf
	cp $(BUILD)output.elf $(ELF)

$(HEX): $(BUILD)output.elf
	$(ARMGNU)-objcopy $(BUILD)output.elf -O ihex $(HEX)

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
	-rm -f $(TARGET) $(ELF) $(HEX) $(BOOT_TARGET) $(BOOT_ELF) $(BOOT_HEX)
	-rm -f $(LIST)
	-rm -f $(MAP)

# ── Bootloader ────────────────────────────────────────────────────────────────
BOOT_BUILD  = build/boot/
BOOT_TARGET = boot7l.img
BOOT_ELF    = boot7l.elf
BOOT_LINKER = boot.ld

BOOT_CFLAGS = -mcpu=cortex-a72 -marm -ffreestanding -nostdlib -O2 -Wall -g \
              -Iboot/inc -Idrivers/inc -Ikernel/inc -Ilibraries/inc \
              -DUART_MINIMAL

BOOT_C_SRCS := boot/app/main.c \
               $(wildcard boot/src/*.c) \
               drivers/src/gpio.c \
               drivers/src/jtag.c \
               drivers/src/crc.c \
               drivers/src/emmc.c \
               drivers/src/uart.c \
               kernel/src/boot_flags.c \
               kernel/src/dfu_trigger.c

BOOT_ASM_SRCS := boot/startup.s

BOOT_OBJECTS := $(patsubst boot/%.s,           $(BOOT_BUILD)%.o, $(BOOT_ASM_SRCS)) \
                $(patsubst boot/app/%.c,       $(BOOT_BUILD)%.o, $(filter boot/app/%, $(BOOT_C_SRCS))) \
                $(patsubst boot/src/%.c,       $(BOOT_BUILD)%.o, $(filter boot/src/%, $(BOOT_C_SRCS))) \
                $(patsubst drivers/src/%.c,    $(BOOT_BUILD)%.o, $(filter drivers/src/%, $(BOOT_C_SRCS))) \
                $(patsubst libraries/src/%.c,  $(BOOT_BUILD)%.o, $(filter libraries/src/%, $(BOOT_C_SRCS))) \
                $(patsubst kernel/src/%.c,     $(BOOT_BUILD)%.o, $(filter kernel/src/%, $(BOOT_C_SRCS)))

BOOT_HEX = boot7l.hex

boot: $(BOOT_TARGET) $(BOOT_ELF) $(BOOT_HEX)

$(BOOT_HEX): $(BOOT_BUILD)boot.elf
	$(ARMGNU)-objcopy $(BOOT_BUILD)boot.elf -O ihex $(BOOT_HEX)

$(BOOT_TARGET): $(BOOT_BUILD)boot.elf
	$(ARMGNU)-objcopy $(BOOT_BUILD)boot.elf -O binary $(BOOT_TARGET)

$(BOOT_ELF): $(BOOT_BUILD)boot.elf
	cp $(BOOT_BUILD)boot.elf $(BOOT_ELF)

$(BOOT_BUILD)boot.elf: $(BOOT_OBJECTS) $(BOOT_LINKER) | $(BOOT_BUILD)
	$(ARMGNU)-ld --no-undefined $(BOOT_OBJECTS) -Map $(BOOT_BUILD)boot.map \
	    -o $(BOOT_BUILD)boot.elf -T $(BOOT_LINKER) $(LIBGCC)

$(BOOT_BUILD)%.o: boot/%.s | $(BOOT_BUILD)
	$(ARMGNU)-as $< -o $@

$(BOOT_BUILD)%.o: boot/app/%.c | $(BOOT_BUILD)
	$(ARMGNU)-gcc $(BOOT_CFLAGS) -c $< -o $@

$(BOOT_BUILD)%.o: boot/src/%.c | $(BOOT_BUILD)
	$(ARMGNU)-gcc $(BOOT_CFLAGS) -c $< -o $@

$(BOOT_BUILD)%.o: drivers/src/%.c | $(BOOT_BUILD)
	$(ARMGNU)-gcc $(BOOT_CFLAGS) -c $< -o $@

$(BOOT_BUILD)%.o: libraries/src/%.c | $(BOOT_BUILD)
	$(ARMGNU)-gcc $(BOOT_CFLAGS) -c $< -o $@

$(BOOT_BUILD)%.o: kernel/src/%.c | $(BOOT_BUILD)
	$(ARMGNU)-gcc $(BOOT_CFLAGS) -c $< -o $@

$(BOOT_BUILD):
	mkdir -p $(BOOT_BUILD)

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
