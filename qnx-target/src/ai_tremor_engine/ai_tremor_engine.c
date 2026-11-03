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

void *cpu_bomb_worker(void *arg) {
    struct sched_param param = { .sched_priority = 25 };
    pthread_setschedparam(pthread_self(), SCHED_RR, &param);

    while (1) {
        if (bomb_enabled) {
            volatile double sink = 0.0;
            for (int i = 0; i < 350000; i++) {
                sink += sin((double)i) * cos((double)i);
            }
            usleep(1000);
        } else {
            usleep(20000);
        }
    }
    return NULL;
}

static void run_ai_tremor_mlp(double raw_x, double raw_y,
                              double *out_x, double *out_y,
                              int starve_flag) {
    static double hidden_x = 74.0, hidden_y = 50.0;
    const double alpha = 0.38;

    int iterations = starve_flag ? 95000 : 120;
    volatile double activation = 0.0;
    for (int i = 0; i < iterations; i++) {
        activation += tanh((double)i * 0.0001);
    }

    hidden_x = alpha * raw_x + (1.0 - alpha) * hidden_x;
    hidden_y = alpha * raw_y + (1.0 - alpha) * hidden_y;

    if (starve_flag) {
        /* When starved, AI trajectory collapses deep inside the 18mm carotid artery zone */
        *out_x = 50.0 + (raw_x - 50.0) * 0.32;
        *out_y = 50.0 + (raw_y - 50.0) * 0.32;
    } else {
        /* Nominal AI tremor-filtered orbit safely outside R=18mm (at R=23.5mm) */
        double dx = hidden_x - 50.0;
        double dy = hidden_y - 50.0;
        double r = sqrt(dx * dx + dy * dy);
        if (r < 21.5 && r > 0.01) {
            *out_x = 50.0 + dx * (22.5 / r);
            *out_y = 50.0 + dy * (22.5 / r);
        } else {
            *out_x = hidden_x;
            *out_y = hidden_y;
        }
    }
}

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
            char *intr = strstr(buf, "\"interventions\":0");
            if (intr && !bomb_enabled) {
                MsgSendPulse(coid, -1, PULSE_CODE_HEARTBEAT, 0);
            }
        }
        close(fd);
    }
}

int main(int argc, char **argv) {
    int coid;
    while ((coid = name_open(NEUROGUARD_ATTACH_POINT, 0)) == -1) {
        usleep(100000);
    }

    struct sched_param param = { .sched_priority = 15 };
    pthread_setschedparam(pthread_self(), SCHED_RR, &param);

    pthread_t bomb_tid;
    pthread_create(&bomb_tid, NULL, cpu_bomb_worker, NULL);

    uint64_t cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;
    double angle = 0.0;
    int tick = 0;

    printf("[AITremorEngine] Connected to Safety Supervisor | Priority: 15\n");

    while (1) {
        if ((tick++ % 4) == 0) {
            poll_bomb_flag(coid);
        }

        angle += 0.045;
        double tremor_x = 2.6 * sin(angle * 11.0);
        double tremor_y = 2.6 * cos(angle * 13.0);
        double raw_x = 50.0 + 25.0 * cos(angle) + tremor_x;
        double raw_y = 50.0 + 25.0 * sin(angle) + tremor_y;

        uint64_t t0 = ClockCycles();
        double filt_x, filt_y;
        run_ai_tremor_mlp(raw_x, raw_y, &filt_x, &filt_y, bomb_enabled);
        uint64_t t1 = ClockCycles();

        uint32_t inf_us = (uint32_t)(((t1 - t0) * 1000000ULL) / cps);
        if (!bomb_enabled) {
            inf_us = 44 + (inf_us % 19);
        } else if (inf_us < 1800) {
            inf_us = 2850 + (inf_us % 1400);
        }

        kinematics_msg_t msg;
        supervisor_reply_t reply;
        memset(&msg, 0, sizeof(msg));
        msg.hdr.type = MSG_TYPE_KINEMATICS;
        msg.timestamp_cycles = ClockCycles();
        msg.raw_x = raw_x;
        msg.raw_y = raw_y;
        msg.filtered_x = filt_x;
        msg.filtered_y = filt_y;
        msg.ai_inference_us = inf_us;

        MsgSend(coid, &msg, sizeof(msg), &reply, sizeof(reply));
        usleep(16000);
    }

    name_close(coid);
    return EXIT_SUCCESS;
}
