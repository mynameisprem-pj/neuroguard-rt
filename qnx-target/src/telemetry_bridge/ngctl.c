/*
 * ngctl: Command-Line Control & Telemetry Inspector for /dev/surgical_arm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "neuroguard_ipc.h"

int main(int argc, char **argv) {
    int fd = open(ACTUATOR_DEV_PATH, O_RDWR);
    if (fd < 0) {
        perror("open(/dev/surgical_arm)");
        return 1;
    }

    if (argc >= 2) {
        const char *cmd = NULL;
        if (strcmp(argv[1], "starve") == 0 || (strcmp(argv[1], "bomb") == 0 && argc >= 3 && strcmp(argv[2], "1") == 0)) {
            cmd = "STARVE=1\n";
        } else if (strcmp(argv[1], "unsafe") == 0) {
            cmd = "UNSAFE=1\n";
        } else if (strcmp(argv[1], "reset") == 0 || (strcmp(argv[1], "bomb") == 0 && argc >= 3 && strcmp(argv[2], "0") == 0)) {
            cmd = "RESET\n";
        }
        if (cmd) {
            write(fd, cmd, strlen(cmd));
            usleep(30000);
        }
    }

    char buf[1024];
    ssize_t n = pread(fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        fputs(buf, stdout);
    }
    close(fd);
    return 0;
}