# NeuroGuard-RT: QNX 8.0 Hard Real-Time Surgical AI Safety Cage

> 🔗 **Quick Links:**  
> 📘 **[25-Minute QNX 8.0 Developer Codelab (`CODELAB.md`)](CODELAB.md)** &nbsp;|&nbsp; 🚀 **[One-Command QEMU Launcher (`run_demo.sh`)](run_demo.sh)** &nbsp;|&nbsp; 📊 **[Reaction-Time Profiler (`scripts/benchmark_qnx_vs_linux.py`)](scripts/benchmark_qnx_vs_linux.py)**

**NeuroGuard-RT** is a mixed-criticality surgical robotics safety architecture built natively for **QNX SDP 8.0 (`procnto-smp-instr` x86_64)**. It wraps an untrusted, starvable AI tremor-filtering layer (`Priority 20r SCHED_RR`) inside a deterministic **Priority-250f (`SCHED_FIFO`) QNX Microkernel Safety Supervisor** that monitors `1,000 Hz` heartbeats and clamps the surgical actuator inside a glowing **`R = 18.00 mm` Safe Surgical Envelope** in **under 2 microseconds**.

---

## System Architecture (Pure QNX Message-Passing & Pulses)

```text
+----------------------------------------------------------------------------------------------------+
|                         QNX SDP 8.0 MICROKERNEL (procnto-smp-instr x86_64)                         |
|                                                                                                    |
|  +----------------------------------+   MsgSend() [1kHz Heartbeat +  +--------------------------+  |
|  | 1. UNTRUSTED AI LAYER            |    Proposed Tip (x,y) mm]      | 2. SAFETY SUPERVISOR     |  |
|  | /data/bin/ai_tremor_engine       | -----------------------------> | /data/bin/               |  |
|  | • Priority: 20r (SCHED_RR)       |                                | safety_supervisor        |  |
|  | • 2x8x2 Recurrent Elman MLP      |   1kHz SIGEV_PULSE Watchdog    | • Priority: 250f (FIFO)  |  |
|  | • Starvation Hog (Prio 35r)      |             +----------------> | • _NTO_CHF_FIXED_PRIORITY|  |
|  | • Unsafe Excursion Injector      |             |                  | • Latest + p99 < 2.0 us  |  |
|  +----------------------------------+             |                  | • Clamps R <= 18.00 mm   |  |
|                   ^                               |                  +-------------+------------+  |
|                   | Polls Fault Latch             | CLOCK_MONOTONIC                | Exclusive     |
|                   v                               +--------------------------------+ Write Token   |
|  +----------------------------------+                                              | ('SAFE')      |
|  | 3. ACTUATOR RESOURCE MANAGER     | <--------------------------------------------+               |
|  | /data/bin/surgical_arm_resmgr    |                                                              |
|  | • Path: /dev/surgical_arm        | ---------> POSIX read() 30Hz JSON ---------> +------------+  |
|  | • Priority: 60f (SCHED_FIFO)     | <--------- POSIX write() STARVE/UNSAFE/RESET | telemetry  |  |
|  +----------------------------------+                                              | _bridge    |  |
+------------------------------------------------------------------------------------+-----+------+--+
                                                                                           | TCP :8080
                                                                                           v
                                                   QNX Surgical Robotics Cockpit (http://localhost:3000)

```

### Core Safety Guarantees

1. **Fixed-Priority IPC (`_NTO_CHF_FIXED_PRIORITY`):** Prevents Priority-20 AI client messages from ever lowering the Priority-250f Safety Supervisor.
2. **Autonomous 1,000 Hz Hardware Watchdog (`SIGEV_PULSE_INIT`):** Fires kernel pulses every `1.0 ms` (`CLOCK_MONOTONIC`). If the AI layer is starved past `2.50 ms`, the Supervisor independently locks the scalpel to `R = 18.00 mm` (`SAFE CLAMPED`) even if the AI thread is completely frozen.
3. **Single-Writer Actuator Sovereignty:** `/dev/surgical_arm` rejects binary position writes with `EPERM` unless authenticated by `safety_supervisor` (`ACTUATOR_AUTH_MAGIC`).

---

## Build & Run Instructions

### Target & Toolchain Specifications

* **QNX Version:** QNX Software Development Platform (SDP) 8.0 (`com.qnx.qnx800`, microkernel `procnto-smp-instr`) via QNX Everywhere
* **Target Platform:** `x86_64` QEMU Virtual Machine (`mkqnximage --type=qemu --arch=x86_64`, 2 SMP cores, VirtIO `io-sock` networking)
* **Toolchain:** `qcc -Vgcc_ntox86_64` (GCC 12.2.0)

### Option A: Full QNX 8.0 Cross-Compile + QEMU Target + Cockpit (WSL2 / Linux / Codespaces)

```bash
# 1. Activate QNX SDP 8.0 environment
source $HOME/qnx800/qnxsdp-env.sh

# 2. Cross-compile all 5 QNX C binaries, build bootable QNX 8.0 image,
#    boot QEMU x86_64 target (:8080), and launch Cockpit UI (:3000)
./run_demo.sh

# 3. Open http://localhost:3000 in your browser, then run the reaction-time logger:
python3 scripts/benchmark_qnx_vs_linux.py

```

#### Direct Verification Inside QNX 8.0 Shell

```sh
uname -a
pidin | grep -E "surgical|safety|ai_tremor|telemetry"
/data/bin/ngctl           # Inspect live JSON telemetry from /dev/surgical_arm
/data/bin/ngctl starve    # Inject Priority-35r AI CPU starvation
/data/bin/ngctl reset     # Clear sticky safety latch
/data/bin/ngctl unsafe    # Force rogue R=25.4mm AI output excursion

```

### Option B: Instant Host UI & Twin Preview (Windows / macOS / Linux — Zero Dependencies)

To preview the **QNX Surgical Robotics Cockpit** and run the fault-injection workflow on a host laptop without QEMU booted:

```bash
python dashboard/bridge_server.py
# Open http://localhost:3000 (Keys: [S] Starve AI, [U] Unsafe AI, [R] Reset Cage)

```

---

## Evidence of QNX 8.0 Use (Reviewer File Index)

* [`qnx-target/include/neuroguard_ipc.h`](https://www.google.com/search?q=qnx-target/include/neuroguard_ipc.h): QNX `struct _pulse` message contract, `PULSE_CODE_WATCHDOG`, safety states, and exclusive actuator struct.
* [`qnx-target/src/safety_supervisor/safety_supervisor.c`](https://www.google.com/search?q=qnx-target/src/safety_supervisor/safety_supervisor.c): Priority `250f` (`SCHED_FIFO`), `ChannelCreate(_NTO_CHF_FIXED_PRIORITY | _NTO_CHF_DISCONNECT)`, `name_attach()`, `SIGEV_PULSE_INIT()`, `timer_create(CLOCK_MONOTONIC)`, `MsgReceive()`, `MsgReply()`, `ClockCycles()`, and `SYSPAGE_ENTRY(qtime)->cycles_per_sec`.
* [`qnx-target/src/resmgr_arm/surgical_arm_resmgr.c`](https://www.google.com/search?q=qnx-target/src/resmgr_arm/surgical_arm_resmgr.c): Priority `60f` POSIX Device Resource Manager (`resmgr_attach`, `iofunc_func_init`, `resmgr_msgread`) mounted at `/dev/surgical_arm`.
* [`qnx-target/src/ai_tremor_engine/ai_tremor_engine.c`](https://www.google.com/search?q=qnx-target/src/ai_tremor_engine/ai_tremor_engine.c): Priority `20r` Recurrent Elman MLP filter, `name_open()`, `MsgSend()`, and Priority `35r` starvation threads.
* [`qnx-target/src/telemetry_bridge/telemetry_bridge.c`](https://www.google.com/search?q=qnx-target/src/telemetry_bridge/telemetry_bridge.c) & [`ngctl.c`](https://www.google.com/search?q=qnx-target/src/telemetry_bridge/ngctl.c): Native QNX `io-sock` TCP `:8080` telemetry streamer and target CLI controller.
* [`qnx-target/Makefile`](https://www.google.com/search?q=qnx-target/Makefile) & [`qnx-target/ifs/neuroguard.build`](https://www.google.com/search?q=qnx-target/ifs/neuroguard.build): `qcc` cross-compilation rules and QNX 8.0 IFS buildfile.

---

## License

Released under the [MIT License].
