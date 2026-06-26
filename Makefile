###############################################################################
# Makefile — rpitos
# Bare-metal RTOS for Raspberry Pi CM4 (BCM2711, Cortex-A72, AArch32)
#
# Usage:
#   make                        — build all samples listed in samples.json
#   make SAMPLE=rtos/full_demo  — build one specific sample
#   make SAMPLE=boot/bootloader — build the bootloader
#   make clean                  — remove entire build/ tree
#   make SAMPLE=rtos/full_demo qemu — run a sample in QEMU
###############################################################################

ARMGNU ?= arm-none-eabi

CFLAGS_BASE := -mcpu=cortex-a72 -marm -ffreestanding -nostdlib -O2 -Wall -g -pipe \
               -Isource/drivers/inc -Isource/kernel/inc -Isource/boot/inc \
               -Isource/libraries/inc

ifdef SAMPLE
# ─────────────────────────────────────────────────────────────────────────────
# Single-sample build:  make SAMPLE=<category>/<name>
# ─────────────────────────────────────────────────────────────────────────────

SAMPLE_NAME     := $(word 2,$(subst /, ,$(SAMPLE)))
SAMPLE_DIR      := source/samples/$(SAMPLE)
SAMPLE_OUT      := build/$(SAMPLE)
SAMPLE_OBJ      := build/$(SAMPLE)/o

# Per-sample declarations — expected variables:
#   SAMPLE_DRIVERS         — source/drivers/src/<name>.c to compile
#   SAMPLE_KERNEL          — source/kernel/src/<name>.c to compile
#   SAMPLE_BOOT_COMPONENTS — source/boot/src/<name>.c to compile
#   SAMPLE_LIBS            — source/libraries/src/<name>.c to compile
#   SAMPLE_EXTRA_CFLAGS    — additional flags (optional, defaults to empty)
SAMPLE_EXTRA_CFLAGS :=
include $(SAMPLE_DIR)/config.mk

CFLAGS  := $(CFLAGS_BASE) $(SAMPLE_EXTRA_CFLAGS) -I$(SAMPLE_DIR)
LIBGCC  := $(shell $(ARMGNU)-gcc $(CFLAGS_BASE) -print-libgcc-file-name)
LINKER  := $(SAMPLE_DIR)/$(SAMPLE_NAME).ld

# startup.s: use per-sample override if present, otherwise fall back to shared
SAMPLE_STARTUP := $(or $(wildcard $(SAMPLE_DIR)/startup.s),startup/startup.s)

SAMPLE_C_SRCS := \
  $(SAMPLE_DIR)/main.c \
  $(patsubst %,source/drivers/src/%.c,  $(SAMPLE_DRIVERS)) \
  $(patsubst %,source/kernel/src/%.c,   $(SAMPLE_KERNEL)) \
  $(patsubst %,source/boot/src/%.c,     $(SAMPLE_BOOT_COMPONENTS)) \
  $(patsubst %,source/libraries/src/%.c,$(SAMPLE_LIBS))

SAMPLE_OBJECTS := \
  $(SAMPLE_OBJ)/startup.o \
  $(patsubst $(SAMPLE_DIR)/%.c,          $(SAMPLE_OBJ)/%.o, \
    $(filter $(SAMPLE_DIR)/%,            $(SAMPLE_C_SRCS))) \
  $(patsubst source/drivers/src/%.c,    $(SAMPLE_OBJ)/%.o, \
    $(filter source/drivers/src/%,      $(SAMPLE_C_SRCS))) \
  $(patsubst source/kernel/src/%.c,     $(SAMPLE_OBJ)/%.o, \
    $(filter source/kernel/src/%,       $(SAMPLE_C_SRCS))) \
  $(patsubst source/boot/src/%.c,       $(SAMPLE_OBJ)/%.o, \
    $(filter source/boot/src/%,         $(SAMPLE_C_SRCS))) \
  $(patsubst source/libraries/src/%.c,  $(SAMPLE_OBJ)/%.o, \
    $(filter source/libraries/src/%,    $(SAMPLE_C_SRCS)))

# ── Targets ──────────────────────────────────────────────────────────────────
.PHONY: all
all: $(SAMPLE_OUT)/$(SAMPLE_NAME).elf \
     $(SAMPLE_OUT)/$(SAMPLE_NAME).img \
     $(SAMPLE_OUT)/$(SAMPLE_NAME).hex \
     $(SAMPLE_OUT)/$(SAMPLE_NAME).list

$(SAMPLE_OUT)/$(SAMPLE_NAME).elf: $(SAMPLE_OBJECTS) $(LINKER)
	$(ARMGNU)-ld --no-undefined $(SAMPLE_OBJECTS) \
	  -Map $(SAMPLE_OUT)/$(SAMPLE_NAME).map \
	  -o $@ -T $(LINKER) $(LIBGCC)

$(SAMPLE_OUT)/$(SAMPLE_NAME).img: $(SAMPLE_OUT)/$(SAMPLE_NAME).elf
	$(ARMGNU)-objcopy $< -O binary $@

$(SAMPLE_OUT)/$(SAMPLE_NAME).hex: $(SAMPLE_OUT)/$(SAMPLE_NAME).elf
	$(ARMGNU)-objcopy $< -O ihex $@

$(SAMPLE_OUT)/$(SAMPLE_NAME).list: $(SAMPLE_OUT)/$(SAMPLE_NAME).elf
	$(ARMGNU)-objdump -d $< > $@

# ── Compile rules ─────────────────────────────────────────────────────────────
$(SAMPLE_OBJ)/startup.o: $(SAMPLE_STARTUP) | $(SAMPLE_OBJ)
	$(ARMGNU)-as $< -o $@

$(SAMPLE_OBJ)/%.o: $(SAMPLE_DIR)/%.c | $(SAMPLE_OBJ)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(SAMPLE_OBJ)/%.o: source/drivers/src/%.c | $(SAMPLE_OBJ)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(SAMPLE_OBJ)/%.o: source/kernel/src/%.c | $(SAMPLE_OBJ)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(SAMPLE_OBJ)/%.o: source/boot/src/%.c | $(SAMPLE_OBJ)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(SAMPLE_OBJ)/%.o: source/libraries/src/%.c | $(SAMPLE_OBJ)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(SAMPLE_OUT):
	mkdir -p $@

$(SAMPLE_OBJ):
	mkdir -p $@

# ── QEMU ─────────────────────────────────────────────────────────────────────
QEMU = qemu-system-arm

qemu: $(SAMPLE_OUT)/$(SAMPLE_NAME).elf
	$(QEMU) -M raspi2b -kernel $< -serial stdio -display none -monitor none \
	  -d int -D qemu.log

else
# ─────────────────────────────────────────────────────────────────────────────
# Default: build all valid samples from samples.json
# ─────────────────────────────────────────────────────────────────────────────

VALID_SAMPLES := $(shell grep -oE '"[a-z_]+/[a-z_]+"' samples.json | tr -d '"')

.PHONY: all clean $(VALID_SAMPLES)
all: $(VALID_SAMPLES)

$(VALID_SAMPLES):
	$(MAKE) SAMPLE=$@

clean:
	rm -rf build/

endif
