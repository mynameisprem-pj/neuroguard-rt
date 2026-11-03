#!/bin/bash
set -e
source $HOME/qnx800/qnxsdp-env.sh

IFS_FILE="/workspaces/neuroguard-rt/qnx-target/ifs/neuroguard-qnx800.ifs"
IMG_FILE="/workspaces/neuroguard-rt/qnx-target/ifs/neuroguard-qnx800.img"

# 1. Create a 32MB raw disk image
dd if=/dev/zero of="$IMG_FILE" bs=1M count=32 status=none

# 2. Create a single bootable FAT16/QNX partition using sfdisk
sfdisk "$IMG_FILE" << 'SFDISK_EOF' >/dev/null 2>&1
label: dos
unit: sectors
start=2048, type=4d, bootable
SFDISK_EOF

# 3. Extract the partition, initialize QNX filesystem with dinit, and embed the IFS as .boot
dd if="$IMG_FILE" of="${IMG_FILE}.part" bs=512 skip=2048 status=none
$QNX_HOST/usr/bin/dinit -h -f "$IFS_FILE" -B "$QNX_TARGET/x86_64/boot/sys/ipl-diskpc2-Stage1" "${IMG_FILE}.part" 2>/dev/null || \
  $QNX_HOST/usr/bin/dinit -h -f "$IFS_FILE" "${IMG_FILE}.part"

# 4. Write QNX IPL MBR loader + partition back into the raw disk image
dd if="$QNX_TARGET/x86_64/boot/sys/ipl-diskpc1" of="$IMG_FILE" bs=440 count=1 conv=notrunc status=none
dd if="${IMG_FILE}.part" of="$IMG_FILE" bs=512 seek=2048 conv=notrunc status=none
rm -f "${IMG_FILE}.part"

echo "[OK] Bootable QNX 8.0 Disk Image ready at: $IMG_FILE"
