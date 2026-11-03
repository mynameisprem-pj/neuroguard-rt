#ifndef NEUROGUARD_IPC_H
#define NEUROGUARD_IPC_H

#include <stdint.h>
#include <sys/neutrino.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>

#define NEUROGUARD_ATTACH_POINT "neuroguard_supervisor"
#define SURGICAL_ARM_DEV_PATH   "/dev/surgical_arm"

/* Custom QNX Pulse & Message Codes */
#define PULSE_CODE_HEARTBEAT    (_PULSE_CODE_MINAVAIL + 1)
#define PULSE_CODE_CPU_BOMB     (_PULSE_CODE_MINAVAIL + 2)
#define MSG_TYPE_KINEMATICS     (_IO_MAX + 10)
#define MSG_TYPE_TELEMETRY_REQ  (_IO_MAX + 11)

/* System Operating States */
typedef enum {
    STATE_NOMINAL_AI = 0,       /* AI tremor filter active & within deadline */
    STATE_DEADLINE_WARNING = 1, /* Jitter > 250us detected */
    STATE_SAFETY_OVERRIDE = 2   /* Priority-250 Hard RTOS Preemption Active */
} system_safety_state_t;

/* Synchronous IPC Payload sent from AI Filter -> Safety Supervisor */
typedef struct {
    struct _pulse hdr;
    uint64_t timestamp_cycles;
    double raw_x;
    double raw_y;
    double filtered_x;
    double filtered_y;
    uint32_t ai_inference_us;   /* Execution time of AI step in microseconds */
} kinematics_msg_t;

/* Reply Payload from Safety Supervisor -> AI Filter & Telemetry Bridge */
typedef struct {
    uint64_t supervisor_cycles;
    double safe_x;
    double safe_y;
    double distance_to_artery;
    uint32_t scheduling_jitter_ns; /* Measured via QNX ClockCycles() */
    uint32_t loop_latency_us;
    system_safety_state_t state;
    uint32_t safety_interventions_count;
    int cpu_bomb_active;
} supervisor_reply_t;

#endif /* NEUROGUARD_IPC_H */