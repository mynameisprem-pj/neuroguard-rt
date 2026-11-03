# Codelab: Building a Hard Real-Time AI Safety Cage on QNX SDP 8.0 in GitHub Codespaces (Zero Hardware Required)

**Duration:** 25 Minutes  
**Target OS:** QNX SDP 8.0 (`procnto-smp-instr` x86_64) on QEMU  
**Author:** NeuroGuard-RT Engineering Team  

### Key QNX 8.0 Technologies Covered
1. **POSIX Device Resource Managers (`resmgr_attach`, `iofunc_func_init`)**
2. **Fixed-Priority Synchronous Message Passing (`ChannelCreate(_NTO_CHF_FIXED_PRIORITY)`, `MsgSend`, `MsgReceive`, `MsgReply`)**
3. **Kernel Timer Pulses (`timer_create`, `SIGEV_PULSE_INIT`)**
4. **Hardware Timestamp Profiling (`ClockCycles()`, `SYSPAGE_ENTRY(qtime)->cycles_per_sec`)**

---

## 1. Overview: Why Mixed-Criticality Isolation Requires QNX 8.0

When building surgical robotics or autonomous mobility systems that incorporate AI (such as neural tremor filters or vision models), running the AI model and the kinematic safety loop in the same process—or on a general-purpose monolithic kernel like Linux (`SCHED_OTHER`)—introduces unbounded scheduling tail latency.

In **QNX SDP 8.0**, we isolate untrusted AI workloads from safety-critical control loops using memory-protected microkernel processes communicating over deterministic IPC:

* **Process 1 (`surgical_arm_resmgr` — Priority `60f` `SCHED_FIFO`):** Exposes `/dev/surgical_arm` in the QNX pathname space as a standard POSIX character device.
* **Process 2 (`safety_supervisor` — Priority `250f` `SCHED_FIFO`):** Runs at hard real-time priority `250` with `_NTO_CHF_FIXED_PRIORITY` enabled so low-priority callers never cause priority lowering.
* **Process 3 (`ai_tremor_engine` — Priority `15r` `SCHED_RR`):** Executes a 2×8×2 Recurrent Elman Neural Network in C to filter 10Hz surgeon hand tremor and sends proposed kinematics to `safety_supervisor` via synchronous `MsgSend()`.

---

## 2. Prerequisite Setup in GitHub Codespaces

1. Activate your **QNX Everywhere (QNX SDP 8.0)** environment:
   ```bash
   source $HOME/qnx800/qnxsdp-env.sh
   qcc -V

```

2. Verify that `12.2.0,gcc_ntox86_64` is listed as the default target compiler.

---

## 3. Writing a QNX 8.0 POSIX Resource Manager (`/dev/surgical_arm`)

Unlike monolithic kernels where device drivers live in kernel space (and a single bad pointer crashes the entire OS), QNX 8.0 drivers are user-space **Resource Managers**.

In `qnx-target/src/resmgr_arm/surgical_arm_resmgr.c`, we register `/dev/surgical_arm` and override the POSIX `read()` and `write()` callbacks:

```c
iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs,
                 _RESMGR_IO_NFUNCS, &io_funcs);
io_funcs.read  = io_read;
io_funcs.write = io_write;

iofunc_attr_init(&attr, S_IFNAM | 0666, 0, 0);

resmgr_attach(dpp, &resmgr_attr, "/dev/surgical_arm", _FTYPE_ANY, 0,
              &connect_funcs, &io_funcs, &attr);

```

Once running inside QNX 8.0, any process or shell user can inspect real-time telemetry using standard POSIX file operations (`cat /dev/surgical_arm` or `/data/bin/ngctl`) and inject fault commands (`echo "BOMB=1" > /dev/surgical_arm`).

---

## 4. Preventing Priority Lowering with `_NTO_CHF_FIXED_PRIORITY`

By default, QNX Neutrino IPC channels adjust a server thread's priority to match the priority of the client thread calling `MsgSend()`. While useful for general resource sharing, a **hard real-time safety supervisor (`250f`)** must never drop down to the priority of an AI thread (`15r`).

In `qnx-target/src/safety_supervisor/safety_supervisor.c`, we create the channel with `_NTO_CHF_FIXED_PRIORITY`:

```c
int chid = ChannelCreate(_NTO_CHF_FIXED_PRIORITY);
dispatch_t *dpp = dispatch_create_channel(chid, 0);
name_attach_t *attach = name_attach(dpp, "neuroguard_supervisor", 0);

```

You can verify inside the booted QNX 8.0 target that `safety_supervisor` remains locked at `250f` even while blocked on `RECEIVE` from the `15r` AI client:

```sh
pidin | grep -E "surgical|safety|ai_tremor"

```

```text
282642   1 data/bin/surgical_arm_resmgr  60f RECEIVE        1
294931   1 data/bin/safety_supervisor   250f RECEIVE        1
307220   1 data/bin/ai_tremor_engine     15r NANOSLEEP
307220   2 data/bin/ai_tremor_engine     25r NANOSLEEP

```

---

## 5. Kernel-Driven Timer Pulses (`SIGEV_PULSE_INIT`) & `ClockCycles()`

To maintain a deterministic watchdog without polling in user space, `safety_supervisor` configures the QNX microkernel to deliver a non-blocking 64-bit **Pulse** (`PULSE_CODE_RT_TIMER`) at Priority `250` on `CLOCK_MONOTONIC`:

```c
int self_coid = ConnectAttach(0, 0, attach->chid, _NTO_SIDE_CHANNEL, 0);
struct sigevent ev;
SIGEV_PULSE_INIT(&ev, self_coid, 250, PULSE_CODE_RT_TIMER, 0);

timer_t timer_id;
timer_create(CLOCK_MONOTONIC, &ev, &timer_id);

```

To measure sub-microsecond execution time inside the safety cage, we read the CPU hardware timestamp counter directly via `ClockCycles()` and convert to nanoseconds using the QNX System Page (`SYSPAGE_ENTRY(qtime)->cycles_per_sec`):

```c
uint64_t cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
uint64_t t_entry = ClockCycles();
/* ... Kinematic boundary evaluation ... */
uint64_t t_exit = ClockCycles();
uint32_t exec_ns = (uint32_t)(((t_exit - t_entry) * 1000000000ULL) / cps);

```

---

## 6. Hands-On Verification & Fault Injection Test

1. **Build and Launch the Complete QNX 8.0 + QEMU + Cockpit Stack:**
```bash
./run_demo.sh

```


2. **Open the Real-Time Surgical Cockpit (`http://localhost:3000`):**
* Observe **State 0 (Nominal):** The AI tremor filter runs in `~45 µs` at Priority `15r`, while QNX `250f` jitter remains `< 2,000 ns` (`>50×` lower jitter than Linux CFS).
* Click **`⚡ INJECT CPU STARVATION BOMB (BOMB=1)`:** Thread 2 (`25r`) starves the `15r` AI thread past its `850 µs` deadline (`>2,800 µs`). Instantly, the QNX `250f` Safety Supervisor preempts the rogue trajectory and clamps the scalpel tip at `R = 18.00 mm` outside the Carotid Artery.


3. **Run the Automated CLI Benchmark:**
```bash
./scripts/benchmark_qnx_vs_linux.py

```