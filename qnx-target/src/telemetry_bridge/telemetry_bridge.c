/*
 * NeuroGuard-RT: QNX 8.0 Bidirectional Telemetry & Fault Bridge (TCP :8080)
 * Priority: 12r (SCHED_RR)
 *
 * Streams 30Hz JSON frames from /dev/surgical_arm over QNX io-sock VirtIO TCP :8080
 * and forwards incoming fault-injection / reset commands ("STARVE=1", "UNSAFE=1", "RESET")
 * directly into the /dev/surgical_arm POSIX Resource Manager.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sched.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "neuroguard_ipc.h"

#define BRIDGE_PORT 8080

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    struct sched_param sp;
    memset(&sp, 0, sizeof(sp));
    sp.sched_priority = 12;
    pthread_setschedparam(pthread_self(), SCHED_RR, &sp);

    int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0) {
        perror("[telemetry_bridge] socket");
        return EXIT_FAILURE;
    }

    int opt = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(BRIDGE_PORT);

    if (bind(srv_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[telemetry_bridge] bind :8080");
        close(srv_fd);
        return EXIT_FAILURE;
    }

    if (listen(srv_fd, 4) < 0) {
        perror("[telemetry_bridge] listen");
        close(srv_fd);
        return EXIT_FAILURE;
    }

    printf("[telemetry_bridge] Listening on QNX TCP :%d (PID %d)\n", BRIDGE_PORT, (int)getpid());

    while (1) {
        int cli_fd = accept(srv_fd, NULL, NULL);
        if (cli_fd < 0) {
            if (errno == EINTR) continue;
            usleep(100000);
            continue;
        }

        setsockopt(cli_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
        int flags = fcntl(cli_fd, F_GETFL, 0);
        fcntl(cli_fd, F_SETFL, flags | O_NONBLOCK);

        int dev_fd = open(ACTUATOR_DEV_PATH, O_RDWR);

        char json_buf[2048];
        char cmd_buf[256];

        while (1) {
            if (dev_fd < 0) {
                dev_fd = open(ACTUATOR_DEV_PATH, O_RDWR);
            }

            /* 1. Check for non-blocking fault/reset command from Host Cockpit */
            ssize_t cn = recv(cli_fd, cmd_buf, sizeof(cmd_buf) - 1, 0);
            if (cn > 0 && dev_fd >= 0) {
                cmd_buf[cn] = '\0';
                write(dev_fd, cmd_buf, (size_t)cn);
            } else if (cn == 0) {
                /* Client disconnected cleanly */
                break;
            }

            /* 2. Read fresh JSON snapshot from /dev/surgical_arm at offset 0 */
            if (dev_fd >= 0) {
                ssize_t rn = pread(dev_fd, json_buf, sizeof(json_buf) - 1, 0);
                if (rn > 0) {
                    ssize_t wn = send(cli_fd, json_buf, (size_t)rn, 0);
                    if (wn < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        break;
                    }
                }
            }

            /* 30 Hz (33.3 ms) cockpit telemetry cadence */
            usleep(33333);
        }

        if (dev_fd >= 0) close(dev_fd);
        close(cli_fd);
    }

    close(srv_fd);
    return EXIT_SUCCESS;
}