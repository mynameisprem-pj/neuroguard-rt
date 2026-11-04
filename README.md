📘 **[Read the Step-by-Step QNX 8.0 Developer Codelab & Tutorial (`CODELAB.md`)](./CODELAB.md)**

# NeuroGuard-RT: Deterministic QNX 8.0 Safety Cage for AI-Assisted Robotic Microsurgery

[![Target OS](https://img.shields.io/badge/Target_RTOS-QNX_SDP_8.0_(procnto--smp--instr)-0096D6?style=for-the-badge)](https://blackberry.qnx.com/)
[![Category](https://img.shields.io/badge/Category-Robotics_%26_Medical-10B981?style=for-the-badge)](#)
[![Architecture](https://img.shields.io/badge/IPC-Synchronous_MsgSend_%2B_POSIX_ResMgr-8B5CF6?style=for-the-badge)](#)
[![License](https://img.shields.io/badge/License-MIT-F59E0B?style=for-the-badge)](LICENSE)

> **What happens when a surgical AI model hallucinates, experiences priority inversion, or stalls mid-incision?**  
> **NeuroGuard-RT** wraps an untrusted C-based Neural Tremor Compensation Engine inside a **hard real-time QNX 8.0 microkernel safety supervisor (`SCHED_FIFO`, Priority `250f`)**. Even under a catastrophic CPU starvation attack where AI inference latency spikes by >3,000%, QNX 8.0 preempts the rogue AI in **~1.5 microseconds** and mathematically clamps the robotic scalpel outside the 18.00mm Carotid Artery exclusion zone with zero deadline misses.

---

## Live QNX 8.0 Surgical Cockpit Telemetry

| State 0: Nominal AI Tremor Filtering (`BOMB=0`) | State 2: Hard RTOS Preemption Active (`BOMB=1`) |
| :---: | :---: |
| ![Nominal State](docs/assets/cockpit-nominal.png) | ![Override State](docs/assets/cockpit-override.png) |
| *AI MLP runs in `~45 µs` at Priority `15r`; QNX `250f` supervisor tracks with `1.59 µs` IPC jitter (`54.3×` lower jitter than Linux CFS).* | *CPU starvation stalls AI thread (`>2,800 µs`); QNX `250f` supervisor preempts in `< 2 µs` and clamps scalpel at `R = 18.00 mm`.* |

---

## 1. Problem Statement & Why QNX 8.0 is Required (Potential Impact)

Modern surgical robots (cranial, spinal, and vascular microsurgery platforms) increasingly rely on deep learning models to filter out surgeon physiological hand tremor (`8–12 Hz`) and smooth tool kinematics. However:

1. **AI Inference is Non-Deterministic:** Neural networks suffer from unpredictable tail latencies, memory allocation stalls, and CPU contention.
2. **General-Purpose OS Kernels (Linux CFS) Cannot Guarantee Microsecond Preemption:** On standard monolithic kernels, a runaway background thread or priority inversion can delay safety-critical kinematics loops by over 85–1,600 microseconds—enough for a robotic scalpel to lacerate a carotid artery.
3. **The QNX 8.0 Microkernel Solution:** By isolating each subsystem into independent memory-protected QNX processes communicating over **fixed-priority synchronous message passing (`_NTO_CHF_FIXED_PRIORITY`)**, NeuroGuard-RT allows medical device makers to deploy cutting-edge AI filters while maintaining **IEC 62304 Class C / ISO 13485 hard real-time determinism**.

---

## 2. QNX 8.0 System Architecture (Technological Implementation)

NeuroGuard-RT is compiled natively with `qcc -Vgcc_ntox86_64` for the **QNX 8.0 (`procnto-smp-instr`)** microkernel running inside an x86_64 QEMU virtual target with live hardware-timer telemetry bridged to a host Surgical Cockpit UI.

```text
+--------------------------------------------------------------------------------------------------+
|                        QNX SDP 8.0 RTOS TARGET (procnto-smp-instr x86_64)                        |
|                                                                                                  |
|  +-------------------------------+         Synchronous IPC          +-------------------------+  |
|  | /data/bin/ai_tremor_engine    | ---- MsgSend() / MsgReply() ---> | /data/bin/              |  |
|  | - Recurrent MLP Filter (15r)  |      (/dev/name/local/           | safety_supervisor       |  |
|  | - CPU Bomb Injector (25r)     |       neuroguard_supervisor)     | - Priority: 250f (FIFO) |  |
|  +-------------------------------+                                  | - _NTO_CHF_FIXED_PRIO   |  |
|                 ^                                                   | - ClockCycles() Jitter  |  |
|                 | Poll / Pulses (PULSE_CODE_CPU_BOMB)               | - R=18.0mm Artery Clamp |  |
|                 v                                                   +------------+------------+  |
|  +-------------------------------+                                               |               |
|  | /data/bin/surgical_arm_resmgr | <--------- POSIX write() State Sync ----------+               |
|  | - POSIX Resource Manager      |                                                               |
|  | - Path: /dev/surgical_arm     | ---------> POSIX read() JSON Telemetry ------> +-----------+  |
|  | - Priority: 60f (SCHED_FIFO)  |                                                | telemetry |  |
|  +-------------------------------+ <--------- io_write("BOMB=1" / "BOMB=0") ----- | _bridge   |  |
|                                                                                   | TCP :8080 |  |
+-----------------------------------------------------------------------------------+-----+-----+--+
                                                                                          |
                                                                    QEMU VirtIO Network (`io-sock`)
                                                                                          |
                                                                                          v
+--------------------------------------------------------------------------------------------------+
|                        HOST SURGICAL COCKPIT & OSCILLOSCOPE (:3000)                              |
|  - Real-time 100x100mm Carotid Artery Workspace Canvas (Raw Tremor vs. AI vs. QNX Safe Scalpel)  |
|  - Dual Microsecond Scheduling Oscilloscopes (QNX 250f Jitter vs. AI Starvation Latency)         |
|  - Interactive Fault Injection Deck (`BOMB=1`, `BOMB=0`, Manual Cursor Boundary Stress Test)     |
+--------------------------------------------------------------------------------------------------+

```

### Key QNX 8.0 Native Primitives Used

* **POSIX Device Resource Manager (`resmgr_attach`, `iofunc_func_init`):** `surgical_arm_resmgr` registers `/dev/surgical_arm` in the QNX pathname space (`60f SCHED_FIFO`). Any process or operator in the QNX shell can inspect live kinematics via `/data/bin/ngctl` (or `cat /dev/surgical_arm`) and inject faults via POSIX `write()`.
* **Fixed-Priority Synchronous IPC (`ChannelCreate(_NTO_CHF_FIXED_PRIORITY)`):** `safety_supervisor` runs at hard real-time **`SCHED_FIFO` Priority `250f**`, ensuring low-priority AI client messages (`15r`) never degrade the supervisor's scheduling priority.
* **Sub-Microsecond Hardware Cycle Profiling (`ClockCycles()` & `SYSPAGE_ENTRY(qtime)->cycles_per_sec`):** Every kinematics frame is profiled using the CPU's hardware timestamp counter via the QNX system page.
* **Non-Blocking QNX Kernel Pulses (`MsgSendPulse`):** Fault-injection state transitions (`PULSE_CODE_CPU_BOMB`) and counter resets (`PULSE_CODE_HEARTBEAT`) are delivered asynchronously via 64-bit QNX pulses without blocking the safety loop.

---

## 3. Empirical Benchmark: QNX 8.0 (`SCHED_FIFO`) vs. Linux Host (`SCHED_OTHER`)

Produced using `./scripts/benchmark_qnx_vs_linux.py` against the live QNX 8.0 QEMU target:

| Metric | Nominal Load (`BOMB=0`) | CPU Starvation Attack (`BOMB=1`) | Safety Requirement |
| --- | --- | --- | --- |
| **QNX 8.0 `250f` Supervisor Jitter (p99)** | `1.84 µs` (`1,840 ns`) | `1.85 µs` (`1,850 ns`) | `< 10.0 µs` (**PASS**) |
| **Linux CFS Host Jitter (p99)** | `86.28 µs` (`86,278 ns`) | `1,670.40 µs` (`1.67 ms`) | `< 10.0 µs` (**FAIL**) |
| **AI Tremor MLP Inference Latency (p99)** | `61.0 µs` | `3,840.0 µs` (*Starved*) | Preempted @ `850 µs` |
| **Minimum Carotid Artery Clearance** | `22.50 mm` | `18.00 mm` (*Hard Clamped*) | `>= 18.00 mm` (**PASS**) |

---

## 4. Repository Structure

```text
neuroguard-rt/
├── .devcontainer/
│   └── devcontainer.json          # One-click GitHub Codespaces environment (QEMU + Python + C++)
├── docs/
│   └── assets/                    # Live QNX 8.0 Surgical Cockpit telemetry screenshots
├── qnx-target/
│   ├── Makefile                   # Builds all 5 QNX 8.0 x86_64 binaries via qcc -Vgcc_ntox86_64
│   ├── include/
│   │   └── neuroguard_ipc.h       # Shared QNX IPC structs, pulse codes, and /dev/surgical_arm contract
│   ├── src/
│   │   ├── resmgr_arm/            # POSIX Resource Manager (/dev/surgical_arm, Priority 60f)
│   │   ├── safety_supervisor/     # Hard Real-Time Safety Cage (Priority 250f, SCHED_FIFO)
│   │   ├── ai_tremor_engine/      # Recurrent MLP Tremor Filter (15r) + CPU Bomb Thread (25r)
│   │   └── telemetry_bridge/      # QNX io-sock TCP :8080 bridge + `ngctl` CLI inspector
│   └── ifs/
│       └── neuroguard.build       # Standalone QNX 8.0 mkifs OS image buildfile
├── dashboard/
│   ├── bridge_server.py           # Host TCP-to-HTTP telemetry proxy & Linux jitter profiler (:3000)
│   └── index.html                 # Real-Time Surgical Cockpit UI & Dual Microsecond Oscilloscopes
├── scripts/
│   ├── start_qnx_vm.sh            # Launches QNX 8.0 QEMU target with VirtIO networking (:8080)
│   └── benchmark_qnx_vs_linux.py  # Automated CLI benchmark comparing QNX 8.0 vs. Linux jitter
└── run_demo.sh                    # One-command build, QNX VM boot, and Cockpit UI launcher

```

---

## 5. Quick Start (One-Command Build & Run in GitHub Codespaces)

### 1. Launch the Complete Demo (QNX 8.0 VM + Surgical Cockpit)

```bash
source $HOME/qnx800/qnxsdp-env.sh
./run_demo.sh

```

Open **`http://localhost:3000`** (or click **Port 3000** in GitHub Codespaces).

### 2. Run the Automated Empirical Benchmark

```bash
./scripts/benchmark_qnx_vs_linux.py

```

### 3. Inspect Processes & Resource Manager Inside the QNX 8.0 Shell

```bash
pidin | grep -E "surgical|safety|ai_tremor|telemetry"
/data/bin/ngctl
/data/bin/ngctl bomb 1
/data/bin/ngctl
/data/bin/ngctl bomb 0

```

## Evidence of QNX 8.0 Use (Reviewer Verification Index)

* **QNX Version & Target:** QNX SDP 8.0 (`com.qnx.qnx800`, microkernel `procnto-smp-instr`) running on `x86_64` QEMU (`mkqnximage --type=qemu --arch=x86_64`).
* **Toolchain & Build System:** Compiled natively via `qcc -Vgcc_ntox86_64` (GCC 12.2.0) in [`qnx-target/Makefile`](qnx-target/Makefile) and packaged via [`qnx-target/ifs/neuroguard.build`](qnx-target/ifs/neuroguard.build).
* **QNX-Specific C Source Files & Kernel APIs Used:**
  1. [`qnx-target/src/resmgr_arm/surgical_arm_resmgr.c`](qnx-target/src/resmgr_arm/surgical_arm_resmgr.c): Implements a native QNX POSIX Device Resource Manager at `/dev/surgical_arm` using `ChannelCreate(_NTO_CHF_FIXED_PRIORITY | _NTO_CHF_DISCONNECT)`, `dispatch_create_channel()`, `iofunc_func_init()`, `iofunc_attr_init()`, `resmgr_attach()`, `resmgr_msgread()`, and `MsgReply()`.
  2. [`qnx-target/src/safety_supervisor/safety_supervisor.c`](qnx-target/src/safety_supervisor/safety_supervisor.c): Implements a hard real-time (`SCHED_FIFO` Priority `250f`) safety supervisor using `name_attach()`, `ChannelCreate(_NTO_CHF_FIXED_PRIORITY)`, `ThreadCtl(_NTO_TCTL_IO_PRIV, 0)`, `ConnectAttach()`, `SIGEV_PULSE_INIT()`, `timer_create(CLOCK_MONOTONIC)`, `MsgReceive()`, `MsgReply()`, `ClockCycles()`, and `SYSPAGE_ENTRY(qtime)->cycles_per_sec`.
  3. [`qnx-target/src/ai_tremor_engine/ai_tremor_engine.c`](qnx-target/src/ai_tremor_engine/ai_tremor_engine.c): Connects to the QNX namespace via `name_open()`, executes synchronous microkernel IPC via `MsgSend()`, and delivers asynchronous 64-bit kernel pulses via `MsgSendPulse()`.
  4. [`qnx-target/src/telemetry_bridge/ngctl.c`](qnx-target/src/telemetry_bridge/ngctl.c) & [`telemetry_bridge.c`](qnx-target/src/telemetry_bridge/telemetry_bridge.c): Native QNX target CLI and `io-sock` TCP `:8080` telemetry streamer.
* **Verified QNX 8.0 Target Output (`pidin` & `uname -a`):**
  * `uname -a`: `QNX noname 8.0.0 2026/06/05-16:26:03EDT x86pc x86_64`
  * `pidin`: Shows `safety_supervisor` at `250f RECEIVE`, `surgical_arm_resmgr` at `60f RECEIVE`, and `ai_tremor_engine` at `15r NANOSLEEP` / `25r NANOSLEEP`.
