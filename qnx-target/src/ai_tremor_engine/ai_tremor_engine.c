#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <sys/dispatch.h>
#include "../../include/neuroguard_ipc.h"

static volatile int bomb_enabled = 0;

/* Rogue CPU Starvation Thread (simulates runaway non-critical load / AI hang) */
void *cpu_bomb_worker(void *arg) {
    struct sched_param param = { .sched_priority = 25 };
    pthread_setschedparam(pthread_self(), SCHED_RR, &param);

    while (1) {
        if (bomb_enabled) {
            /* Burn CPU cycles to starve Priority-15 AI inference */
            volatile double sink = 0.0;
            for (int i = 0; i < 450000; i++) {
                sink += sin((double)i) * cos((double)i);
            }
        } else {
            usleep(10000);
        }
    }
    return NULL;
}

/* Lightweight C Neural Tremor Filter (2-layer recurrent MLP approximation) */
static void run_ai_tremor_mlp(double raw_x, double raw_y,
                              double *out_x, double *out_y,
                              int starve_flag) {
    static double hidden_x = 50.0, hidden_y = 50.0;
    const double alpha = 0.28; /* Learned recurrent smoothing weight */

    /* Matrix-vector hidden layer pass */
    int iterations = starve_flag ? 180000 : 1200;
    volatile double activation = 0.0;
    for (int i = 0; i < iterations; i++) {
        activation += tanh((double)i * 0.0001);
    }

    hidden_x = alpha * raw_x + (1.0 - alpha) * hidden_x;
    hidden_y = alpha * raw_y + (1.0 - alpha) * hidden_y;

    /* If AI is starved/hanging, simulate stale trajectory drifting into artery */
    if (starve_flag) {
        *out_x = 50.0 + (raw_x - 50.0) * 0.25;
        *out_y = 50.0 + (raw_y - 50.0) * 0.25;
    } else {
        *out_x = hidden_x;
        *out_y = hidden_y;
    }
}

/* Check if user/dashboard enabled BOMB via /dev/surgical_arm */
static void poll_bomb_flag(int coid) {
    int fd = open(SURGICAL_ARM_DEV_PATH, O_RDONLY);
    if (fd >= 0) {
        char buf[512] = {0};
        if (read(fd, buf, sizeof(buf) - 1) > 0) {
            char *p = strstr(buf, "\"bomb\":");
            if (p) {
                int new_bomb = atoi(p + 7);
                if (new_bomb != bomb_enabled) {
                    bomb_enabled = new_bomb;
                    MsgSendPulse(coid, -1, PULSE_CODE_CPU_BOMB, bomb_enabled);
                }
            }
        }
        close(fd);
    }
}

int main(int argc, char **argv) {
    int coid;
    /* Wait for Safety Supervisor attach point */
    while ((coid = name_open(NEUROGUARD_ATTACH_POINT, 0)) == -1) {
        usleep(100000);
    }

    /* Run AI Engine at normal Priority 15 */
    struct sched_param param = { .sched_priority = 15 };
    SchedSet(0, 0, SCHED_RR, &param);

    pthread_t bomb_tid;
    pthread_create(&bomb_tid, NULL, cpu_bomb_worker, NULL);

    uint64_t cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    double angle = 0.0;
    int tick = 0;

    printf("[AITremorEngine] Connected to Safety Supervisor | Priority: 15\n");

    while (1) {
        if ((tick++ % 10) == 0) {
            poll_bomb_flag(coid);
        }

        /* Simulate surgeon guiding scalpel in an orbit around (50,50) + 10Hz tremor */
        angle += 0.04;
        double tremor_x = 3.5 * sin(angle * 12.0);
        double tremor_y = 3.5 * cos(angle * 11.0);
        double raw_x = 50.0 + 22.0 * cos(angle) + tremor_x;
        double raw_y = 50.0 + 22.0 * sin(angle) + tremor_y;

        uint64_t t0 = ClockCycles();
        double filt_x, filt_y;
        run_ai_tremor_mlp(raw_x, raw_y, &filt_x, &filt_y, bomb_enabled);
        uint64_t t1 = ClockCycles();

        kinematics_msg_t msg;
        supervisor_reply_t reply;
        memset(&msg, 0, sizeof(msg));
        msg.hdr.type = MSG_TYPE_KINEMATICS;
        msg.timestamp_cycles = ClockCycles();
        msg.raw_x = raw_x;
        msg.raw_y = raw_y;
        msg.filtered_x = filt_x;
        msg.filtered_y = filt_y;
        msg.ai_inference_us = (uint32_t)(((t1 - t0) * 1000000ULL) / cps);

        /* Synchronous QNX IPC to Priority-250 Safety Supervisor */
        MsgSend(coid, &msg, sizeof(msg), &reply, sizeof(reply));

        usleep(16000); /* ~60Hz control loop */
    }

    name_close(coid);
    return EXIT_SUCCESS;
}