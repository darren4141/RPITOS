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

CFLAGS_BASE := -mcpu=cortex-a72 -marm -ffreestanding -nostdlib -O2 -Wall -g -pipe -mno-unaligned-access \
               $(patsubst %,-I%,$(wildcard source/drivers/*)) \
               $(patsubst %,-I%,$(wildcard source/kernel/*)) \
               $(patsubst %,-I%,$(wildcard source/boot/*)) \
               $(patsubst %,-I%,$(wildcard source/libraries/*)) \
               -Isource/telemetry

ifdef SAMPLE
# ─────────────────────────────────────────────────────────────────────────────
# Single-sample build:  make SAMPLE=<category>/<name>
# ─────────────────────────────────────────────────────────────────────────────

SAMPLE_NAME     := $(word 2,$(subst /, ,$(SAMPLE)))
SAMPLE_DIR      := source/samples/$(SAMPLE)
SAMPLE_OUT      := build/$(SAMPLE)
SAMPLE_OBJ      := build/$(SAMPLE)/o

# Per-sample declarations — expected variables:
#   SAMPLE_DRIVERS         — component names under source/drivers/<name>/
#   SAMPLE_KERNEL          — component names under source/kernel/<name>/
#   SAMPLE_BOOT_COMPONENTS — component names under source/boot/<name>/
#   SAMPLE_LIBS            — component names under source/libraries/<name>/
#   SAMPLE_TELEMETRY       — set to 1 to link source/telemetry/telemetry_frame.c + telemetry.c (RTOS app builds; gate the code itself with -DRTOS_TELEMETRY in SAMPLE_EXTRA_CFLAGS)
#   SAMPLE_TELEMETRY_BOOT  — set to 1 to link source/telemetry/telemetry_frame.c + telemetry_boot.c instead (bootstrap/bootloader — no RTOS, unlocked senders; mutually exclusive with SAMPLE_TELEMETRY)
#   SAMPLE_EXTRA_CFLAGS    — additional flags (optional, defaults to empty)
SAMPLE_EXTRA_CFLAGS :=
SAMPLE_TELEMETRY    :=
SAMPLE_TELEMETRY_BOOT :=
include $(SAMPLE_DIR)/config.mk

# -MMD -MP: per-object header dependency tracking, so header-only changes
# trigger rebuilds too — see Makefile.md.
CFLAGS  := $(CFLAGS_BASE) $(SAMPLE_EXTRA_CFLAGS) -I$(SAMPLE_DIR) -MMD -MP
LIBGCC  := $(shell $(ARMGNU)-gcc $(CFLAGS_BASE) -print-libgcc-file-name)
LINKER  := $(SAMPLE_DIR)/$(SAMPLE_NAME).ld

# startup.s priority: config.mk SAMPLE_STARTUP_OVERRIDE → per-sample startup.s → shared startup/startup.s
SAMPLE_STARTUP := $(or $(SAMPLE_STARTUP_OVERRIDE),$(wildcard $(SAMPLE_DIR)/startup.s),startup/startup.s)

SAMPLE_OBJECTS := \
  $(SAMPLE_OBJ)/startup.o \
  $(SAMPLE_OBJ)/main.o \
  $(patsubst %,$(SAMPLE_OBJ)/%.o,$(SAMPLE_DRIVERS)) \
  $(patsubst %,$(SAMPLE_OBJ)/%.o,$(SAMPLE_KERNEL)) \
  $(patsubst %,$(SAMPLE_OBJ)/%.o,$(SAMPLE_BOOT_COMPONENTS)) \
  $(patsubst %,$(SAMPLE_OBJ)/%.o,$(SAMPLE_LIBS))

ifeq ($(SAMPLE_TELEMETRY),1)
SAMPLE_OBJECTS += $(SAMPLE_OBJ)/telemetry_frame.o $(SAMPLE_OBJ)/telemetry.o
endif

ifeq ($(SAMPLE_TELEMETRY_BOOT),1)
SAMPLE_OBJECTS += $(SAMPLE_OBJ)/telemetry_frame.o $(SAMPLE_OBJ)/telemetry_boot.o
endif

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

# Generate one explicit rule per component (pattern rules don't support % twice
# in a single prerequisite path, e.g. source/drivers/%/%.c).
define COMPILE_RULE
$(SAMPLE_OBJ)/$(1).o: $(2)/$(1)/$(1).c | $(SAMPLE_OBJ)
	$(ARMGNU)-gcc $(CFLAGS) -c $$< -o $$@
endef

$(foreach d,$(SAMPLE_DRIVERS),       $(eval $(call COMPILE_RULE,$d,source/drivers)))
$(foreach d,$(SAMPLE_KERNEL),        $(eval $(call COMPILE_RULE,$d,source/kernel)))
$(foreach d,$(SAMPLE_BOOT_COMPONENTS),$(eval $(call COMPILE_RULE,$d,source/boot)))
$(foreach d,$(SAMPLE_LIBS),          $(eval $(call COMPILE_RULE,$d,source/libraries)))

# telemetry_*.c live flat under source/telemetry/ (not nested per-component like the libraries above)
$(SAMPLE_OBJ)/telemetry.o: source/telemetry/telemetry.c | $(SAMPLE_OBJ)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(SAMPLE_OBJ)/telemetry_frame.o: source/telemetry/telemetry_frame.c | $(SAMPLE_OBJ)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(SAMPLE_OBJ)/telemetry_boot.o: source/telemetry/telemetry_boot.c | $(SAMPLE_OBJ)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(SAMPLE_OUT):
	mkdir -p $@

# Pull in the .d files -MMD -MP generated on the previous build, if any exist yet
# (the leading '-' silences the "no such file" case on a clean tree) — this is what
# actually makes the header-dependency tracking above take effect.
-include $(SAMPLE_OBJECTS:.o=.d)

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
