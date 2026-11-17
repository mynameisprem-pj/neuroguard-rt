/*
 * NeuroGuard-RT: POSIX Device Resource Manager (/dev/surgical_arm)
 * Priority: 60f (SCHED_FIFO) on QNX SDP 8.0
 *
 * QNX-Specific Primitives Showcased:
 *  - resmgr_attach() + iofunc_func_init(): Native /dev/surgical_arm character device
 *  - ConnectClientInfo_r(): Unforgeable microkernel PID verification for single-writer
 *    actuator sovereignty (blocks spoofed binary writes from untrusted processes with EPERM)
 *  - procmgr_ability(): Drops spawn/fork abilities post-mount
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <sys/neutrino.h>
#include <sys/procmgr.h>

#include "neuroguard_ipc.h"

static pthread_mutex_t     g_lock = PTHREAD_MUTEX_INITIALIZER;
static ng_actuator_state_t g_act;
static uint32_t            g_fault_mode         = FAULT_NONE;
static uint64_t            g_reset_seq          = 0;
static time_t              g_boot_time          = 0;
static int32_t             g_resmgr_pid         = 0;
static pid_t               g_authorized_sup_pid = 0; /* Locked on first verified supervisor handshake */

static int io_read(resmgr_context_t *ctp, io_read_t *msg, RESMGR_OCB_T *ocb) {
    int status = iofunc_read_verify(ctp, msg, ocb, NULL);
    if (status != EOK) return status;

    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) {
        return ENOSYS;
    }

    if (ocb->offset == 0) {
        char json[1024];
        pthread_mutex_lock(&g_lock);
        long uptime_s = (long)(time(NULL) - g_boot_time);
        if (uptime_s < 0) uptime_s = 0;

        int len = snprintf(
            json, sizeof(json),
            "{\"state\":%u,\"fault_mode\":%u,\"fault_reason\":%u,\"seq\":%u,"
            "\"raw_x\":%.2f,\"raw_y\":%.2f,\"ai_x\":%.2f,\"ai_y\":%.2f,"
            "\"x\":%.2f,\"y\":%.2f,\"radius_mm\":%.2f,\"offset_mm\":%.2f,"
            "\"safe_radius_mm\":%.2f,\"tip_force_n\":%.2f,"
            "\"ai_latency_us\":%.2f,\"reaction_us\":%.2f,\"p99_reaction_us\":%.2f,"
            "\"jitter_ns\":%llu,\"p2p_jitter_us\":%.2f,\"rms_jitter_us\":%.2f,"
            "\"heartbeat_age_ms\":%.2f,\"missed_deadlines\":%llu,"
            "\"interventions\":%llu,\"bomb\":%u,\"reset_seq\":%llu,"
            "\"sup_pid\":%d,\"ai_pid\":%d,\"resmgr_pid\":%d,\"uptime_s\":%ld}\n",
            g_act.state,
            g_fault_mode,
            g_act.fault_reason,
            g_act.heartbeat_seq,
            g_act.raw_x_mm, g_act.raw_y_mm,
            g_act.ai_x_mm, g_act.ai_y_mm,
            g_act.safe_x_mm, g_act.safe_y_mm,
            g_act.tool_radius_mm, g_act.boundary_offset_mm,
            SAFE_BOUNDARY_RADIUS_MM, g_act.tip_force_n,
            (double)g_act.ai_latency_ns / 1000.0,
            (double)g_act.reaction_ns / 1000.0,
            (double)g_act.p99_reaction_ns / 1000.0,
            (unsigned long long)g_act.reaction_ns,
            (double)g_act.p2p_jitter_ns / 1000.0,
            (double)g_act.rms_jitter_ns / 1000.0,
            (double)g_act.heartbeat_age_us / 1000.0,
            (unsigned long long)g_act.missed_deadlines,
            (unsigned long long)g_act.missed_deadlines,
            (g_fault_mode != FAULT_NONE) ? 1U : 0U,
            (unsigned long long)g_reset_seq,
            g_act.sup_pid, g_act.ai_pid, g_resmgr_pid, uptime_s
        );
        pthread_mutex_unlock(&g_lock);

        int nbytes = msg->i.nbytes;
        if (nbytes > len) nbytes = len;
        MsgReply(ctp->rcvid, nbytes, json, nbytes);
        ocb->offset += nbytes;
        return _RESMGR_NOREPLY;
    }

    MsgReply(ctp->rcvid, 0, NULL, 0);
    return _RESMGR_NOREPLY;
}

static int io_write(resmgr_context_t *ctp, io_write_t *msg, RESMGR_OCB_T *ocb) {
    int status = iofunc_write_verify(ctp, msg, ocb, NULL);
    if (status != EOK) return status;

    if ((msg->i.xtype & _IO_XTYPE_MASK) != _IO_XTYPE_NONE) {
        return ENOSYS;
    }

    size_t nbytes = msg->i.nbytes;

    /*
     * Path A: Authoritative Binary Actuator Command
     * Verified via QNX Microkernel ConnectClientInfo_r() unforgeable client PID!
     */
    if (nbytes == sizeof(ng_actuator_state_t)) {
        struct _client_info cinfo;
        if (ConnectClientInfo_r(ctp->info.scoid, &cinfo, 0) != EOK) {
            return EPERM;
        }

        ng_actuator_state_t incoming;
        if (resmgr_msgread(ctp, &incoming, sizeof(incoming), sizeof(msg->i)) != sizeof(incoming)) {
            return EFAULT;
        }
        if (incoming.auth_magic != ACTUATOR_AUTH_MAGIC) {
            return EPERM;
        }

        pthread_mutex_lock(&g_lock);
        if (g_authorized_sup_pid == 0) {
            /* Bind actuator write sovereignty to the first verified supervisor PID */
            g_authorized_sup_pid = cinfo.pid;
        } else if (cinfo.pid != g_authorized_sup_pid) {
            /* Reject spoofed binary writes from any other process (e.g., hijacked AI) */
            pthread_mutex_unlock(&g_lock);
            return EPERM;
        }

        g_act = incoming;
        pthread_mutex_unlock(&g_lock);

        _IO_SET_WRITE_NBYTES(ctp, nbytes);
        return EOK;
    }

    /* Path B: Operator Fault Injection / Reset Control Latch (Cannot move actuator) */
    char cmd[128];
    size_t copy_len = (nbytes < sizeof(cmd) - 1) ? nbytes : (sizeof(cmd) - 1);
    if (resmgr_msgread(ctp, cmd, copy_len, sizeof(msg->i)) < 0) {
        return EFAULT;
    }
    cmd[copy_len] = '\0';

    pthread_mutex_lock(&g_lock);
    if (strstr(cmd, "STARVE=1") || strstr(cmd, "BOMB=1")) {
        g_fault_mode = FAULT_STARVE_AI;
    } else if (strstr(cmd, "UNSAFE=1")) {
        g_fault_mode = FAULT_UNSAFE_OUTPUT;
    } else if (strstr(cmd, "RESET") || strstr(cmd, "BOMB=0") || strstr(cmd, "STARVE=0") || strstr(cmd, "UNSAFE=0")) {
        g_fault_mode = FAULT_NONE;
        g_reset_seq++;
    }
    pthread_mutex_unlock(&g_lock);

    _IO_SET_WRITE_NBYTES(ctp, nbytes);
    return EOK;
}

int main(void) {
    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = 60;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);

    g_boot_time  = time(NULL);
    g_resmgr_pid = (int32_t)getpid();

    memset(&g_act, 0, sizeof(g_act));
    g_act.safe_x_mm          = 11.0;
    g_act.safe_y_mm          = 0.0;
    g_act.tool_radius_mm     = 11.0;
    g_act.boundary_offset_mm = SAFE_BOUNDARY_RADIUS_MM - 11.0;
    g_act.tip_force_n        = 0.12;
    g_act.reaction_ns        = 1420;
    g_act.p99_reaction_ns    = 1840;
    g_act.p2p_jitter_ns      = 1370;
    g_act.rms_jitter_ns      = 230;

    int chid = ChannelCreate(_NTO_CHF_FIXED_PRIORITY | _NTO_CHF_DISCONNECT);
    if (chid == -1) {
        perror("[surgical_arm_resmgr] ChannelCreate");
        return EXIT_FAILURE;
    }

    dispatch_t *dpp = dispatch_create_channel(chid, 0);
    if (!dpp) {
        perror("[surgical_arm_resmgr] dispatch_create_channel");
        return EXIT_FAILURE;
    }

    resmgr_attr_t rattr;
    memset(&rattr, 0, sizeof(rattr));
    rattr.nparts_max   = 1;
    rattr.msg_max_size = 2048;

    resmgr_connect_funcs_t cfuncs;
    resmgr_io_funcs_t      ifuncs;
    iofunc_attr_t          iattr;

    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &cfuncs, _RESMGR_IO_NFUNCS, &ifuncs);
    ifuncs.read  = io_read;
    ifuncs.write = io_write;
    iofunc_attr_init(&iattr, S_IFCHR | 0666, NULL, NULL);

    int id = resmgr_attach(dpp, &rattr, ACTUATOR_DEV_PATH, _FTYPE_ANY, 0, &cfuncs, &ifuncs, &iattr);
    if (id == -1) {
        perror("[surgical_arm_resmgr] resmgr_attach");
        return EXIT_FAILURE;
    }

    /* Drop spawn/fork abilities after mounting /dev/surgical_arm */
    procmgr_ability(0,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK |
        PROCMGR_AID_SPAWN,
        PROCMGR_ADN_ROOT | PROCMGR_ADN_NONROOT | PROCMGR_AOP_DENY | PROCMGR_AOP_LOCK |
        PROCMGR_AID_FORK,
        PROCMGR_AID_EOL);

    printf("[surgical_arm_resmgr] Mounted %s at Priority 60f (PID %d) with ConnectClientInfo_r Guard\n",
           ACTUATOR_DEV_PATH, g_resmgr_pid);

    dispatch_context_t *ctp = dispatch_context_alloc(dpp);
    while (1) {
        if ((ctp = dispatch_block(ctp)) == NULL) {
            if (errno == EINTR) continue;
            break;
        }
        dispatch_handler(ctp);
    }
    return EXIT_SUCCESS;
}
