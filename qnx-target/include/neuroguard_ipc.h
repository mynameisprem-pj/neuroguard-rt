#ifndef NEUROGUARD_IPC_H
#define NEUROGUARD_IPC_H

#include <stdint.h>
#include <sys/neutrino.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>

/* QNX Namespace & POSIX Device Paths */
#define SUPERVISOR_ATTACH_NAME  "neuroguard_supervisor"
#define ACTUATOR_DEV_PATH       "/dev/surgical_arm"

/* Hard Real-Time Geometry & Timing Thresholds */
#define SAFE_BOUNDARY_RADIUS_MM 18.00   /* Glowing Cyan Safe Surgical Zone (R <= 18.00 mm) */
#define AI_HEARTBEAT_TIMEOUT_US 2500    /* 2.50 ms max allowed gap between AI heartbeats   */
#define AI_INFERENCE_LIMIT_US   850     /* 850 us max per-frame neural inference budget    */
#define WATCHDOG_PERIOD_NS      1000000L /* 1.00 ms (1,000 Hz) hardware watchdog pulse     */

/* Custom QNX Kernel Pulse Codes (_PULSE_CODE_MINAVAIL .. _PULSE_CODE_MAXAVAIL) */
#define PULSE_CODE_WATCHDOG     (_PULSE_CODE_MINAVAIL + 1)
#define PULSE_CODE_FAULT_CTRL   (_PULSE_CODE_MINAVAIL + 2)

/* IPC Message Types */
#define MSG_TYPE_AI_PROPOSAL    0x4E47  /* 'NG' synchronous AI heartbeat + trajectory msg  */
#define ACTUATOR_AUTH_MAGIC     0x53414645UL /* 'SAFE' - Exclusive supervisor write token  */

/* System Safety States */
typedef enum {
    STATE_NORMAL       = 0,  /* Nominal AI tremor filtering inside R <= 18.00 mm   */
    STATE_FAULT        = 1,  /* Transient fault trip                               */
    STATE_SAFE_CLAMPED = 2   /* Tip locked to R = 18.00 mm boundary until RESET    */
} safety_state_t;

/* Fault Injection Modes (Controlled from Cockpit UI or ngctl) */
typedef enum {
    FAULT_NONE          = 0,
    FAULT_STARVE_AI     = 1, /* High-priority CPU hog starves AI heartbeats        */
    FAULT_UNSAFE_OUTPUT = 2  /* Forces AI to command unsafe R > 24.0 mm excursion  */
} fault_mode_t;

/* Fault Reason Flags */
typedef enum {
    REASON_NONE            = 0,
    REASON_HEARTBEAT_MISS  = 1,
    REASON_BOUNDARY_BREACH = 2,
    REASON_LATENCY_SPIKE   = 3
} fault_reason_t;

/*
 * Synchronous QNX IPC Message: Untrusted AI Layer (Prio 20r) -> Safety Supervisor (Prio 250f)
 * Must begin with `struct _pulse` so a single QNX channel handles both pulses and messages.
 */
typedef struct {
    struct _pulse hdr;
    uint16_t      msg_type;
    uint16_t      fault_mode_echo;
    uint32_t      heartbeat_seq;
    double        raw_x_mm;          /* Unfiltered surgeon input with 10Hz tremor (mm) */
    double        raw_y_mm;
    double        ai_x_mm;           /* AI proposed end-effector X (-25..+25 mm)       */
    double        ai_y_mm;           /* AI proposed end-effector Y (-25..+25 mm)       */
    uint64_t      ai_inference_ns;   /* Measured AI compute latency (nanoseconds)      */
} ng_ai_msg_t;

/* Synchronous Reply: Safety Supervisor (Prio 250f) -> Untrusted AI Layer */
typedef struct {
    uint32_t      status;            /* safety_state_t                                 */
    uint32_t      active_fault_mode; /* Current fault_mode_t from /dev/surgical_arm    */
    double        clamped_x_mm;      /* Authoritative safe X position                  */
    double        clamped_y_mm;      /* Authoritative safe Y position                  */
    uint64_t      reaction_ns;       /* Supervisor decision reaction time (ns)         */
} ng_sup_reply_t;

/*
 * Exclusive Actuator State Payload: Safety Supervisor (250f) -> /dev/surgical_arm (60f)
 * Only binaries presenting `auth_magic == ACTUATOR_AUTH_MAGIC` can update physical state.
 */
typedef struct {
    uint32_t      auth_magic;        /* Must equal ACTUATOR_AUTH_MAGIC                 */
    uint32_t      state;             /* safety_state_t                                 */
    uint32_t      fault_reason;      /* fault_reason_t                                 */
    uint32_t      heartbeat_seq;
    double        raw_x_mm;
    double        raw_y_mm;
    double        ai_x_mm;
    double        ai_y_mm;
    double        safe_x_mm;         /* Physical scalpel tip X (mm, center = 0,0)      */
    double        safe_y_mm;         /* Physical scalpel tip Y (mm, center = 0,0)      */
    double        tool_radius_mm;    /* Distance from (0,0) center (<= 18.00 mm)       */
    double        boundary_offset_mm;/* 18.00 - tool_radius_mm (0.00 when clamped)     */
    double        tip_force_n;       /* Simulated tissue contact force (Newtons)       */
    uint64_t      ai_latency_ns;     /* Latest AI inference time (ns)                  */
    uint64_t      reaction_ns;       /* Latest Supervisor reaction time (ns)           */
    uint64_t      p99_reaction_ns;   /* Rolling p99 Supervisor reaction time (ns)      */
    uint64_t      p2p_jitter_ns;     /* Peak-to-Peak watchdog jitter (ns)              */
    uint64_t      rms_jitter_ns;     /* RMS watchdog jitter (ns)                       */
    uint64_t      heartbeat_age_us;  /* Microseconds since last valid AI heartbeat     */
    uint64_t      missed_deadlines;  /* Total missed deadlines / fault trips           */
    uint64_t      reset_ack_seq;     /* Tracks operator RESET counter                  */
    int32_t       sup_pid;           /* Live QNX PID of safety_supervisor              */
    int32_t       ai_pid;            /* Live QNX PID of ai_tremor_engine               */
} ng_actuator_state_t;

#endif /* NEUROGUARD_IPC_H */