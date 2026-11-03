#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>
#include "../../include/neuroguard_ipc.h"

static resmgr_connect_funcs_t connect_funcs;
static resmgr_io_funcs_t      io_funcs;
static iofunc_attr_t          attr;

static supervisor_reply_t latest_state = {
    .raw_x = 74.0, .raw_y = 50.0,
    .ai_x = 74.0,  .ai_y = 50.0,
    .safe_x = 74.0, .safe_y = 50.0,
    .distance_to_artery = 24.0,
    .scheduling_jitter_ns = 1180,
    .loop_latency_us = 42,
    .state = STATE_NOMINAL_AI,
    .safety_interventions_count = 0,
    .cpu_bomb_active = 0,
    .manual_override_active = 0,
    .manual_target_x = 74.0,
    .manual_target_y = 50.0
};

static volatile int sticky_bomb_flag = 0;
static volatile int sticky_reset_flag = 0;
static volatile int manual_active = 0;
static volatile double manual_x = 74.0;
static volatile double manual_y = 50.0;
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

int io_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *ocb) {
    int status;
    if ((status = iofunc_read_verify(ctp, msg, ocb, NULL)) != EOK)
        return status;
    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE)
        return ENOSYS;

    char buf[512];
    pthread_mutex_lock(&state_mutex);
    int len = snprintf(buf, sizeof(buf),
        "{\"x\":%.2f,\"y\":%.2f,\"raw_x\":%.2f,\"raw_y\":%.2f,\"ai_x\":%.2f,\"ai_y\":%.2f,"
        "\"artery_dist\":%.2f,\"jitter_ns\":%u,\"latency_us\":%u,\"state\":%d,"
        "\"interventions\":%u,\"bomb\":%d,\"reset\":%d,\"man\":%d,\"mx\":%.2f,\"my\":%.2f}\n",
        latest_state.safe_x, latest_state.safe_y,
        latest_state.raw_x, latest_state.raw_y,
        latest_state.ai_x, latest_state.ai_y,
        latest_state.distance_to_artery, latest_state.scheduling_jitter_ns,
        latest_state.loop_latency_us, latest_state.state,
        latest_state.safety_interventions_count, sticky_bomb_flag,
        sticky_reset_flag, manual_active, manual_x, manual_y);
    sticky_reset_flag = 0;
    pthread_mutex_unlock(&state_mutex);

    if (ocb->offset >= len) {
        _IO_SET_READ_NBYTES(ctp, 0);
        return _RESMGR_NPARTS(0);
    }

    int nbytes = min(msg->i.nbytes, len - ocb->offset);
    MsgReply(ctp->rcvid, nbytes, buf + ocb->offset, nbytes);
    ocb->offset += nbytes;
    return _RESMGR_NOREPLY;
}

int io_write(resmgr_context_t *ctp, io_write_t *msg, RESMGR_OCB_T *ocb) {
    int status;
    if ((status = iofunc_write_verify(ctp, msg, ocb, NULL)) != EOK)
        return status;

    _IO_SET_WRITE_NBYTES(ctp, msg->i.nbytes);

    if (msg->i.nbytes == sizeof(supervisor_reply_t)) {
        pthread_mutex_lock(&state_mutex);
        resmgr_msgread(ctp, &latest_state, sizeof(supervisor_reply_t), sizeof(msg->i));
        latest_state.cpu_bomb_active = sticky_bomb_flag;
        pthread_mutex_unlock(&state_mutex);
    } else {
        char cmd[128] = {0};
        int bytes = (msg->i.nbytes < 127) ? msg->i.nbytes : 127;
        resmgr_msgread(ctp, cmd, bytes, sizeof(msg->i));
        pthread_mutex_lock(&state_mutex);
        if (strstr(cmd, "BOMB=1") != NULL) {
            sticky_bomb_flag = 1;
        } else if (strstr(cmd, "BOMB=0") != NULL) {
            sticky_bomb_flag = 0;
        } else if (strstr(cmd, "RESET") != NULL) {
            sticky_bomb_flag = 0;
            sticky_reset_flag = 1;
            latest_state.safety_interventions_count = 0;
        } else if (strncmp(cmd, "POS=", 4) == 0) {
            double px = 50.0, py = 50.0;
            if (sscanf(cmd + 4, "%lf,%lf", &px, &py) == 2) {
                manual_active = 1;
                manual_x = px;
                manual_y = py;
            }
        } else if (strstr(cmd, "AUTO") != NULL) {
            manual_active = 0;
        }
        pthread_mutex_unlock(&state_mutex);
    }
    return _RESMGR_NPARTS(0);
}

int main(int argc, char **argv) {
    dispatch_t           *dpp;
    resmgr_attr_t        resmgr_attr;
    dispatch_context_t   *ctp;

    int chid = ChannelCreate(_NTO_CHF_FIXED_PRIORITY | _NTO_CHF_DISCONNECT);
    if ((dpp = dispatch_create_channel(chid, DISPATCH_FLAG_NOLOCK)) == NULL) {
        perror("dispatch_create_channel");
        return EXIT_FAILURE;
    }

    struct sched_param param = { .sched_priority = 60 };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);

    memset(&resmgr_attr, 0, sizeof(resmgr_attr));
    resmgr_attr.nparts_max = 1;
    resmgr_attr.msg_max_size = 2048;

    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &connect_funcs,
                     _RESMGR_IO_NFUNCS, &io_funcs);
    io_funcs.read  = io_read;
    io_funcs.write = io_write;

    iofunc_attr_init(&attr, S_IFNAM | 0666, 0, 0);

    if (resmgr_attach(dpp, &resmgr_attr, SURGICAL_ARM_DEV_PATH, _FTYPE_ANY, 0,
                      &connect_funcs, &io_funcs, &attr) == -1) {
        perror("resmgr_attach");
        return EXIT_FAILURE;
    }

    printf("[NeuroGuard] QNX 8.0 Resource Manager active at %s (SCHED_FIFO Prio 60)\n",
           SURGICAL_ARM_DEV_PATH);

    ctp = dispatch_context_alloc(dpp);
    while (1) {
        if ((ctp = dispatch_block(ctp)) == NULL)
            return EXIT_FAILURE;
        dispatch_handler(ctp);
    }
    return EXIT_SUCCESS;
}
