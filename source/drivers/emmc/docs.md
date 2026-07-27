# emmc

## Sector layout

eMMC sector layout is PHYSICAL LBA, and must match the on-disk MBR partition
table (verify with `sudo fdisk -lu /dev/sdX`). All firmware lives ABOVE the
FAT boot partition so raw dd/DFU writes can never corrupt the files the GPU
reads.

```
LBA 0        - 16383     : MBR + alignment gap
LBA 16384    - 1064959   : Partition 1 — FAT32 boot (512 MB). Holds
                            start4.elf, fixup4.dat, *.dtb, config.txt,
                            bootstrap.img. GPU reads these BY NAME.
                            *** Never write raw sectors in this range. ***
LBA 1064960  - end       : Partition 2 — repurposed as the raw firmware
                            region (was a leftover Linux partition).
```

Partition 2 layout (relative offsets from `EMMC_FW_BASE`):

```
+0      (1064960) - +511    : Bootloader slot A  (header @ base, binary @ +1)
+512    (1065472) - +1023   : Bootloader slot B  (reserved)
+1024   (1065984)          : Metadata           (active-slot + WDT state)
+1025   (1065985) - +2047   : reserved          (metadata growth)
+2048   (1067008) - +18431  : App slot A         (header @ base, binary @ +1)
+18432  (1083392) - +34815  : App slot B
```

App slots are A/B: DFU writes the inactive slot, then metadata flips the
active slot. The inactive slot doubles as the DFU staging area.

`EMMC_FW_BASE` is the start LBA of partition 2. If you ever repartition,
update it to match fdisk output — everything else in `emmc.h` is relative
to it.
