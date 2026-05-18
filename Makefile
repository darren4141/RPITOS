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
         -Idrivers/inc -Iinc

# Source directories
ASM_SRCS := $(wildcard startup/*.s)
C_SRCS   := $(wildcard source/*.c) $(wildcard drivers/src/*.c)

# Object files — all land flat in build/
OBJECTS := $(patsubst startup/%.s,   $(BUILD)%.o, $(ASM_SRCS)) \
           $(patsubst source/%.c,    $(BUILD)%.o, $(filter source/%, $(C_SRCS))) \
           $(patsubst drivers/src/%.c, $(BUILD)%.o, $(filter drivers/src/%, $(C_SRCS)))

# Rules
all: $(TARGET) $(LIST)

rebuild: clean all

$(LIST): $(BUILD)output.elf
	$(ARMGNU)-objdump -d $(BUILD)output.elf > $(LIST)

$(TARGET): $(BUILD)output.elf
	$(ARMGNU)-objcopy $(BUILD)output.elf -O binary $(TARGET)

$(BUILD)output.elf: $(OBJECTS) $(LINKER)
	$(ARMGNU)-ld --no-undefined $(OBJECTS) -Map $(MAP) -o $(BUILD)output.elf -T $(LINKER)

$(BUILD)%.o: startup/%.s | $(BUILD)
	$(ARMGNU)-as $< -o $@

$(BUILD)%.o: source/%.c | $(BUILD)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(BUILD)%.o: drivers/src/%.c | $(BUILD)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	-rm -rf $(BUILD)
	-rm -f $(TARGET)
	-rm -f $(LIST)
	-rm -f $(MAP)
