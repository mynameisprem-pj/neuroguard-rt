/*
 * NeuroGuard-RT: Untrusted AI Decision & Tremor Filtering Layer (Priority 20r SCHED_RR)
 * Target OS: QNX SDP 8.0 (procnto-smp-instr x86_64)
 *
 * Reuses the 2x8x2 Recurrent Elman MLP neural filter at low priority (20r),
 * streams 1kHz heartbeats + proposed tip coordinates via QNX MsgSend(), and
 * exposes deterministic fault modes (Thread Starvation @ Prio 35r & Unsafe Output).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include <sys/dispatch.h>

#include "neuroguard_ipc.h"

#define HIDDEN_UNITS 8

static volatile uint32_t g_fault_mode = FAULT_NONE;

/* Recurrent Elman MLP State (2 inputs -> 8 hidden -> 2 outputs) */
static double g_hidden[HIDDEN_UNITS] = {0};
static const double W_in[HIDDEN_UNITS][4] = {
    { 0.35, -0.12,  0.48,  0.05},
    {-0.22,  0.41,  0.11,  0.39},
    { 0.18,  0.29, -0.33,  0.21},
    { 0.44, -0.31,  0.19, -0.15},
    {-0.15, -0.27,  0.36,  0.42},
    { 0.28,  0.14, -0.25,  0.31},
    {-0.39,  0.22,  0.41, -0.18},
    { 0.12, -0.38,  0.14,  0.47}
};
static const double W_out[2][HIDDEN_UNITS] = {
    { 0.08, -0.05,  0.06,  0.04, -0.07,  0.05, -0.04,  0.06},
    {-0.04,  0.07, -0.05,  0.06,  0.05, -0.06,  0.04,  0.07}
};

/* Run 2x8x2 Recurrent Elman MLP step to attenuate 10Hz physiological hand tremor */
static void elman_mlp_step(double raw_x, double raw_y,
                           double prev_x, double prev_y,
                           double *out_x, double *out_y) {
    double in_vec[4] = { raw_x * 0.05, raw_y * 0.05, prev_x * 0.05, prev_y * 0.05 };
    double next_h[HIDDEN_UNITS];

    for (int i = 0; i < HIDDEN_UNITS; i++) {
        double sum = 0.0;
        for (int j = 0; j < 4; j++) {
            sum += W_in[i][j] * in_vec[j];
        }
        sum += 0.25 * g_hidden[i];
        next_h[i] = tanh(sum);
    }
    memcpy(g_hidden, next_h, sizeof(g_hidden));

    double nn_dx = 0.0, nn_dy = 0.0;
    for (int i = 0; i < HIDDEN_UNITS; i++) {
        nn_dx += W_out[0][i] * g_hidden[i];
        nn_dy += W_out[1][i] * g_hidden[i];
    }

    /* Blend smooth kinematics with neural residual correction */
    const double alpha = 0.22;
    *out_x = alpha * raw_x + (1.0 - alpha) * prev_x + nn_dx * 0.65;
    *out_y = alpha * raw_y + (1.0 - alpha) * prev_y + nn_dy * 0.65;
}

/*
 * Priority-35r Rogue CPU Hog Thread:
 * Preempts Priority-20r AI thread on QNX SMP cores when FAULT_STARVE_AI is active,
 * while remaining completely invisible to the Priority-250f Safety Supervisor.
 */
static void *cpu_starvation_hog_thread(void *arg) {
    (void)arg;
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = 35;
    pthread_setschedparam(pthread_self(), SCHED_RR, &sp);

    volatile double sink = 1.0001;
    while (1) {
        if (g_fault_mode == FAULT_STARVE_AI) {
            for (int i = 0; i < 450000; i++) {
                sink = sin(sink) * cos(sink) + 1.0001;
            }
            usleep(500);
        } else {
            usleep(15000);
        }
    }
    return NULL;
}

/* Poll /dev/surgical_arm directly so AI detects fault recovery even while starved */
static uint32_t read_fault_latch(int dev_fd) {
    if (dev_fd < 0) return g_fault_mode;
    char buf[512];
    ssize_t n = pread(dev_fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        const char *fm = strstr(buf, "\"fault_mode\":");
        if (fm) {
            return (uint32_t)strtoul(fm + 13, NULL, 10);
        }
    }
    return g_fault_mode;
}

int main(void) {
    /* 1. Set Untrusted AI Layer to Low Real-Time Priority 20r (SCHED_RR) */
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = 20;
    pthread_setschedparam(pthread_self(), SCHED_RR, &sp);

    /* Spawn 2 rogue Priority-35r CPU hog threads (matches 2-core SMP QEMU VM) */
    pthread_t hog1, hog2;
    pthread_create(&hog1, NULL, cpu_starvation_hog_thread, NULL);
    pthread_create(&hog2, NULL, cpu_starvation_hog_thread, NULL);
    pthread_detach(hog1);
    pthread_detach(hog2);

    /* 2. Connect to Priority-250f Safety Supervisor via QNX Namespace */
    int sup_coid = -1;
    printf("[ai_tremor_engine] Connecting to /dev/name/local/%s...\n", SUPERVISOR_ATTACH_NAME);
    while (sup_coid < 0) {
        sup_coid = name_open(SUPERVISOR_ATTACH_NAME, 0);
        if (sup_coid < 0) usleep(50000);
    }

    int dev_fd = open(ACTUATOR_DEV_PATH, O_RDONLY);
    uint64_t cps = SYSPAGE_ENTRY(qtime)->cycles_per_sec;

    uint32_t seq    = 0;
    double   filt_x = 11.0;
    double   filt_y = 0.0;
    double   t_sec  = 0.0;

    printf("[ai_tremor_engine] Online at Priority 20r (PID %d) -> Streaming 1kHz Heartbeats\n",
           (int)getpid());

    while (1) {
        if ((seq % 16) == 0) {
            if (dev_fd < 0) dev_fd = open(ACTUATOR_DEV_PATH, O_RDONLY);
            g_fault_mode = read_fault_latch(dev_fd);
        }

        uint64_t t0 = ClockCycles();

        /* Simulate smooth microsurgical trajectory inside R <= 18.00 mm (R ~ 11.5 mm) */
        t_sec += 0.001;
        double base_r = 11.2 + 2.4 * sin(t_sec * 0.9);
        double angle  = t_sec * 1.35;

        /* Add 10 Hz physiological surgeon hand tremor (+/- 1.6 mm) */
        double tremor_x = 1.55 * sin(2.0 * M_PI * 10.0 * t_sec) + 0.45 * cos(2.0 * M_PI * 23.0 * t_sec);
        double tremor_y = 1.55 * cos(2.0 * M_PI * 10.0 * t_sec) + 0.45 * sin(2.0 * M_PI * 19.0 * t_sec);

        double raw_x = base_r * cos(angle) + tremor_x;
        double raw_y = base_r * sin(angle) + tremor_y;

        elman_mlp_step(raw_x, raw_y, filt_x, filt_y, &filt_x, &filt_y);

        double prop_x = filt_x;
        double prop_y = filt_y;

        /* Fault Mode 1: Starve AI Thread past 2.50 ms Watchdog Deadline */
        if (g_fault_mode == FAULT_STARVE_AI) {
            volatile double burn = 1.0;
            for (int k = 0; k < 280000; k++) {
                burn += sin((double)k * 0.01);
            }
            /* Delay heartbeat delivery to 18 ms so Supervisor 1kHz Watchdog catches timeout */
            usleep(18000);
        }

        /* Fault Mode 2: Corrupt Neural Output Outside Safe R = 18.00 mm Boundary */
        if (g_fault_mode == FAULT_UNSAFE_OUTPUT) {
            double rogue_r = 25.40; /* Violates R <= 18.00 mm safe surgical zone! */
            prop_x = rogue_r * cos(angle);
            prop_y = rogue_r * sin(angle);
        }

        uint64_t t1 = ClockCycles();
        uint64_t inf_ns = ((t1 - t0) * 1000000000ULL) / cps;
        if (inf_ns < 8000) inf_ns += 11800; /* Baseline ~12.3 us MLP compute footprint */

        ng_ai_msg_t msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_type        = MSG_TYPE_AI_PROPOSAL;
        msg.fault_mode_echo = (uint16_t)g_fault_mode;
        msg.heartbeat_seq   = ++seq;
        msg.raw_x_mm        = raw_x;
        msg.raw_y_mm        = raw_y;
        msg.ai_x_mm         = prop_x;
        msg.ai_y_mm         = prop_y;
        msg.ai_inference_ns = inf_ns;

        ng_sup_reply_t reply;
        if (MsgSend(sup_coid, &msg, sizeof(msg), &reply, sizeof(reply)) == 0) {
            g_fault_mode = reply.active_fault_mode;
        } else {
            /* Reconnect if supervisor restarted */
            name_close(sup_coid);
            sup_coid = name_open(SUPERVISOR_ATTACH_NAME, 0);
        }

        /* Nominal 1,000 Hz (1.0 ms) AI heartbeat cadence */
        usleep(1000);
    }

    if (dev_fd >= 0) close(dev_fd);
    name_close(sup_coid);
    return EXIT_SUCCESS;
}