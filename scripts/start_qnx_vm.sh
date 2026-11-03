#!/bin/bash
set -e
cd /workspaces/neuroguard-rt/qnx-target/vm

# Kill any existing QEMU instance
if [ -f output/qemu.pid ]; then
    kill -9 $(cat output/qemu.pid) 2>/dev/null || true
    rm -f output/qemu.pid
fi

KVM_ARGS=""
if [ -w /dev/kvm ]; then
    KVM_ARGS="-enable-kvm -cpu host"
    echo "[QEMU] Hardware KVM acceleration enabled!"
else
    KVM_ARGS="-cpu max"
    echo "[QEMU] Running in software emulation mode"
fi

qemu-system-x86_64 \
  $KVM_ARGS \
  -smp 2 \
  -m 1024 \
  -drive file=output/disk-qemu.vmdk,if=ide,id=drv0 \
  -netdev user,id=net0,hostfwd=tcp::8080-:8080,hostfwd=tcp::2222-:22 \
  -device virtio-net-pci,netdev=net0 \
  -pidfile output/qemu.pid \
  -nographic \
  -serial mon:stdio
