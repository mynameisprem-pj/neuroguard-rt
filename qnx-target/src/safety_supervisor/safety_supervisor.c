#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <errno.h>
#include <sched.h>
#include <pthread.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <sys/dispatch.h>
#include "../../include/neuroguard_ipc.h"

#define ARTERY_CENTER_X 50.0
#define ARTERY_CENTER_Y 50.0
#define ARTERY_SAFE_RADIUS 18.0
#define DEADLINE_WARNING_US 250
#define HARD_PREEMPT_US     800

static supervisor_reply_t current_telemetry;

/* Push latest state to our POSIX Resource Manager (/dev/surgical_arm) */
static void sync_to_resmgr(const supervisor_reply_t *state) {
    int fd = open(SURGICAL_ARM_DEV_PATH, O_WRONLY);
    if (fd >= 0) {
        write(fd, state, sizeof(supervisor_reply_t));
        close(fd);
    }
}

int main(int argc, char **argv) {
    name_attach_t *attach;
    kinematics_msg_t msg;
    int rcvid;

    /* 1. Lock into QNX Hard Real-Time Priority 250 (SCHED_FIFO) */
    struct sched_param param;
    param.sched_priority = 250;
    if (SchedSet(0, 0, SCHED_FIFO, &param) == -1) {
        perror("[SafetySupervisor] SchedSet failed (running at default prio)");
    }

    /* Request I/O privileges for deterministic hardware/timer access */
    ThreadCtl(_NTO_TCTL_IO_PRIV, 0);

    /* 2. Register QNX Namespace Attach Point for Synchronous IPC */
    if ((attach = name_attach(NULL, NEUROGUARD_ATTACH_POINT, 0)) == NULL) {
        perror("[SafetySupervisor] name_attach failed");
        return EXIT_FAILURE;
    }

    uint64_t cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    printf("[SafetySupervisor] Online at /dev/name/local/%s | Priority: 250 | CPS: %lu\n",
           NEUROGUARD_ATTACH_POINT, (unsigned long)cps);

    memset(&current_telemetry, 0, sizeof(current_telemetry));
    current_telemetry.safe_x = 20.0;
    current_telemetry.safe_y = 20.0;
    current_telemetry.state = STATE_NOMINAL_AI;

    uint64_t last_loop_cycles = ClockCycles();

    while (1) {
        rcvid = MsgReceive(attach->chid, &msg, sizeof(msg), NULL);
        uint64_t now_cycles = ClockCycles();

        if (rcvid == -1) {
            perror("MsgReceive");
            break;
        }

        /* Handle QNX Kernel / Client Pulses */
        if (rcvid == 0) {
            if (msg.hdr.code == PULSE_CODE_CPU_BOMB) {
                current_telemetry.cpu_bomb_active = msg.hdr.value.sival_int;
                printf("[SafetySupervisor] PULSE: CPU Bomb state set to %d\n",
                       current_telemetry.cpu_bomb_active);
            }
            continue;
        }

        /* Handle Telemetry Bridge Poll Request */
        if (msg.hdr.type == MSG_TYPE_TELEMETRY_REQ) {
            MsgReply(rcvid, EOK, &current_telemetry, sizeof(current_telemetry));
            continue;
        }

        /* Handle Synchronous Kinematics Frame from AI Tremor Engine */
        uint64_t ipc_transit_cycles = (now_cycles > msg.timestamp_cycles)
                                      ? (now_cycles - msg.timestamp_cycles) : 100;
        uint32_t jitter_ns = (uint32_t)((ipc_transit_cycles * 1000000000ULL) / cps);
        (void)last_loop_cycles;
        last_loop_cycles   = now_cycles;

        /* Calculate Euclidean distance to the critical artery boundary */
        double dx = msg.filtered_x - ARTERY_CENTER_X;
        double dy = msg.filtered_y - ARTERY_CENTER_Y;
        double dist = sqrt(dx * dx + dy * dy);

        /* Deterministic Safety Cage Evaluation */
        double target_x = msg.filtered_x;
        double target_y = msg.filtered_y;
        system_safety_state_t new_state = STATE_NOMINAL_AI;

        if (msg.ai_inference_us > HARD_PREEMPT_US || dist < ARTERY_SAFE_RADIUS) {
            /* HARD RTOS PREEMPTION: Clamp scalpel outside the artery boundary */
            new_state = STATE_SAFETY_OVERRIDE;
            current_telemetry.safety_interventions_count++;

            if (dist < ARTERY_SAFE_RADIUS && dist > 0.001) {
                double scale = ARTERY_SAFE_RADIUS / dist;
                target_x = ARTERY_CENTER_X + dx * scale;
                target_y = ARTERY_CENTER_Y + dy * scale;
            }
        } else if (msg.ai_inference_us > DEADLINE_WARNING_US) {
            new_state = STATE_DEADLINE_WARNING;
        }

        current_telemetry.supervisor_cycles = now_cycles;
        current_telemetry.safe_x = target_x;
        current_telemetry.safe_y = target_y;
        current_telemetry.distance_to_artery = dist;
        current_telemetry.scheduling_jitter_ns = jitter_ns;
        current_telemetry.loop_latency_us = msg.ai_inference_us;
        current_telemetry.state = new_state;

        /* Immediately unblock the caller via MsgReply */
        MsgReply(rcvid, EOK, &current_telemetry, sizeof(current_telemetry));

        /* Update /dev/surgical_arm asynchronously */
        sync_to_resmgr(&current_telemetry);
    }

    name_detach(attach, 0);
    return EXIT_SUCCESS;
}