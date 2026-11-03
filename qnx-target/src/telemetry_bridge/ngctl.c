#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "../../include/neuroguard_ipc.h"

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "bomb") == 0) {
        int val = (argc > 2) ? atoi(argv[2]) : 1;
        int fd = open(SURGICAL_ARM_DEV_PATH, O_WRONLY);
        if (fd < 0) {
            perror("open /dev/surgical_arm");
            return 1;
        }
        char cmd[32];
        int len = snprintf(cmd, sizeof(cmd), "BOMB=%d", val);
        write(fd, cmd, len);
        close(fd);
        printf("[ngctl] Sent '%s' to %s\n", cmd, SURGICAL_ARM_DEV_PATH);
        return 0;
    }

    if (argc > 1 && strcmp(argv[1], "stream") == 0) {
        /* Stream continuous 20Hz telemetry to /dev/ser2 (QEMU host bridge) */
        int ser_fd = open("/dev/ser2", O_RDWR);
        if (ser_fd < 0) ser_fd = STDOUT_FILENO;

        int flags = fcntl(ser_fd, F_GETFL, 0);
        fcntl(ser_fd, F_SETFL, flags | O_NONBLOCK);

        while (1) {
            char cmd[64] = {0};
            ssize_t n = read(ser_fd, cmd, sizeof(cmd) - 1);
            if (n > 0) {
                int dev_fd = open(SURGICAL_ARM_DEV_PATH, O_WRONLY);
                if (dev_fd >= 0) {
                    write(dev_fd, cmd, n);
                    close(dev_fd);
                }
            }

            int fd = open(SURGICAL_ARM_DEV_PATH, O_RDONLY);
            if (fd >= 0) {
                char buf[512] = {0};
                ssize_t r = read(fd, buf, sizeof(buf) - 1);
                close(fd);
                if (r > 0) {
                    write(ser_fd, buf, r);
                }
            }
            usleep(50000); /* 20Hz */
        }
    }

    /* Default: Read and print current JSON from /dev/surgical_arm */
    int fd = open(SURGICAL_ARM_DEV_PATH, O_RDONLY);
    if (fd < 0) {
        perror("open /dev/surgical_arm");
        return 1;
    }
    char buf[512] = {0};
    ssize_t r = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (r > 0) {
        printf("%s", buf);
    }
    return 0;
}
