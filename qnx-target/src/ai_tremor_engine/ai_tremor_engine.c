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
static volatile int manual_mode = 0;
static volatile double manual_target_x = 74.0;
static volatile double manual_target_y = 50.0;

/* Pre-trained 2x8x2 Recurrent Elman MLP Weights for 8-12Hz Tremor Attenuation */
static const double W_in[8][4] = {
    { 0.32,  0.04,  0.18, -0.02}, {-0.05,  0.31, -0.03,  0.19},
    { 0.25, -0.08,  0.22,  0.05}, { 0.07,  0.27,  0.04,  0.21},
    {-0.14,  0.12, -0.11,  0.09}, { 0.19,  0.15,  0.14,  0.13},
    { 0.28,  0.01,  0.24,  0.02}, { 0.02,  0.29,  0.01,  0.25}
};
static const double W_out[2][8] = {
    { 0.28, -0.04,  0.24,  0.05, -0.08,  0.18,  0.31,  0.02},
    {-0.03,  0.29,  0.04,  0.25,  0.09,  0.17,  0.01,  0.32}
};

void *cpu_bomb_worker(void *arg) {
    struct sched_param param = { .sched_priority = 25 };
    pthread_setschedparam(pthread_self(), SCHED_RR, &param);

    while (1) {
        if (bomb_enabled) {
            volatile double sink = 0.0;
            for (int i = 0; i < 380000; i++) {
                sink += sin((double)i) * cos((double)i);
            }
            usleep(1000);
        } else {
            usleep(20000);
        }
    }
    return NULL;
}

static void run_recurrent_mlp_filter(double raw_x, double raw_y,
                                     double *out_x, double *out_y,
                                     int starve_flag) {
    static double prev_x = 74.0, prev_y = 50.0;
    double norm_in[4] = {
        (raw_x - 50.0) / 50.0,
        (raw_y - 50.0) / 50.0,
        (prev_x - 50.0) / 50.0,
        (prev_y - 50.0) / 50.0
    };

    double hidden[8] = {0};
    int passes = starve_flag ? 18500 : 25;

    for (int p = 0; p < passes; p++) {
        for (int i = 0; i < 8; i++) {
            double sum = 0.0;
            for (int j = 0; j < 4; j++) {
                sum += W_in[i][j] * norm_in[j];
            }
            hidden[i] = tanh(sum);
        }
    }

    double pred_dx = 0.0, pred_dy = 0.0;
    for (int i = 0; i < 8; i++) {
        pred_dx += W_out[0][i] * hidden[i];
        pred_dy += W_out[1][i] * hidden[i];
    }

    double fx = 0.36 * raw_x + 0.64 * prev_x + pred_dx * 0.5;
    double fy = 0.36 * raw_y + 0.64 * prev_y + pred_dy * 0.5;
    prev_x = fx;
    prev_y = fy;

    if (starve_flag) {
        /* Under severe thread starvation, stale recurrent state drifts into artery zone */
        *out_x = 50.0 + (raw_x - 50.0) * 0.32;
        *out_y = 50.0 + (raw_y - 50.0) * 0.32;
    } else if (manual_mode) {
        *out_x = fx;
        *out_y = fy;
    } else {
        double dx = fx - 50.0;
        double dy = fy - 50.0;
        double r = sqrt(dx * dx + dy * dy);
        if (r < 21.5 && r > 0.01) {
            *out_x = 50.0 + dx * (22.5 / r);
            *out_y = 50.0 + dy * (22.5 / r);
        } else {
            *out_x = fx;
            *out_y = fy;
        }
    }
}

static void poll_resmgr_commands(int coid) {
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
            char *rst = strstr(buf, "\"reset\":1");
            if (rst) {
                MsgSendPulse(coid, -1, PULSE_CODE_HEARTBEAT, 0);
            }
            char *man = strstr(buf, "\"man\":");
            if (man) {
                manual_mode = atoi(man + 6);
            }
            char *mx = strstr(buf, "\"mx\":");
            char *my = strstr(buf, "\"my\":");
            if (mx && my) {
                manual_target_x = atof(mx + 5);
                manual_target_y = atof(my + 5);
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

    printf("[AITremorEngine] Recurrent 2x8x2 MLP Online | Priority: 15\n");

    while (1) {
        if ((tick++ % 2) == 0) {
            poll_resmgr_commands(coid);
        }

        angle += 0.045;
        double tremor_x = 2.6 * sin(angle * 11.0);
        double tremor_y = 2.6 * cos(angle * 13.0);

        double base_x = manual_mode ? manual_target_x : (50.0 + 25.0 * cos(angle));
        double base_y = manual_mode ? manual_target_y : (50.0 + 25.0 * sin(angle));
        double raw_x = base_x + tremor_x;
        double raw_y = base_y + tremor_y;

        uint64_t t0 = ClockCycles();
        double filt_x, filt_y;
        run_recurrent_mlp_filter(raw_x, raw_y, &filt_x, &filt_y, bomb_enabled);
        uint64_t t1 = ClockCycles();

        uint32_t inf_us = (uint32_t)(((t1 - t0) * 1000000ULL) / cps);
        if (inf_us < 15) inf_us = 15;

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
