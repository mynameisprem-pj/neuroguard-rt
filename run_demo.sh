#!/bin/bash
set -e
source $HOME/qnx800/qnxsdp-env.sh

echo "========================================================"
echo "  NeuroGuard-RT : QNX 8.0 Grand Prize Demo Launcher     "
echo "========================================================"

# 1. Compile QNX 8.0 Binaries
echo "[1/4] Compiling QNX 8.0 C Binaries with qcc (-Vgcc_ntox86_64)..."
cd /workspaces/neuroguard-rt/qnx-target
make

# 2. Rebuild QNX 8.0 QEMU Target Image
echo "[2/4] Building QNX 8.0 QEMU Virtual Machine Image (mkqnximage)..."
cd /workspaces/neuroguard-rt/qnx-target/vm
mkqnximage --type=qemu --arch=x86_64 --ssh-ident=$HOME/.ssh/id_ed25519.pub --build >/dev/null

# 3. Stop any previous instances
pkill -9 -f "qemu-system-x86_64" 2>/dev/null || true
pkill -9 -f "bridge_server.py" 2>/dev/null || true
rm -f /workspaces/neuroguard-rt/qnx-target/vm/output/qemu.pid

# 4. Launch QNX 8.0 VM in Background Daemon Mode
echo "[3/4] Starting QNX 8.0 Microkernel VM in background (TCP :8080 forwarded)..."
KVM_ARGS="-cpu max"
if [ -w /dev/kvm ]; then
    KVM_ARGS="-enable-kvm -cpu host"
fi

nohup qemu-system-x86_64 \
  $KVM_ARGS \
  -smp 2 \
  -m 1024 \
  -drive file=/workspaces/neuroguard-rt/qnx-target/vm/output/disk-qemu.vmdk,if=ide,id=drv0 \
  -netdev user,id=net0,hostfwd=tcp::8080-:8080,hostfwd=tcp::2222-:22 \
  -device virtio-net-pci,netdev=net0 \
  -pidfile /workspaces/neuroguard-rt/qnx-target/vm/output/qemu.pid \
  -nographic \
  -serial file:/workspaces/neuroguard-rt/qnx-target/vm/output/qnx_console.log \
  >/dev/null 2>&1 &

# 5. Launch Surgical Cockpit Web Server in Background
echo "[4/4] Starting Surgical Cockpit Bridge Server on http://localhost:3000 ..."
nohup python3 /workspaces/neuroguard-rt/dashboard/bridge_server.py \
  >/workspaces/neuroguard-rt/dashboard/cockpit.log 2>&1 &

echo ""
echo "✅ NeuroGuard-RT is running in background daemon mode!"
echo "   - Web Cockpit UI : http://localhost:3000 (Open Port 3000 in Codespaces)"
echo "   - QNX Boot Log   : tail -f /workspaces/neuroguard-rt/qnx-target/vm/output/qnx_console.log"
echo "   - Safe to close terminals!"
