#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/neutrino.h>
#include <sys/dispatch.h>
#include "../../include/neuroguard_ipc.h"

int main(int argc, char **argv) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[TelemetryBridge] bind failed");
        return EXIT_FAILURE;
    }
    listen(server_fd, 4);
    printf("[TelemetryBridge] Listening on QNX TCP :8080\n");

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        /* Non-blocking socket to read incoming commands ("BOMB=1" / "BOMB=0") */
        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

        while (1) {
            char cmd[64] = {0};
            ssize_t n = recv(client_fd, cmd, sizeof(cmd) - 1, 0);
            if (n > 0) {
                int dev_fd = open(SURGICAL_ARM_DEV_PATH, O_WRONLY);
                if (dev_fd >= 0) {
                    write(dev_fd, cmd, n);
                    close(dev_fd);
                }
            }

            /* Read JSON directly from our POSIX Resource Manager /dev/surgical_arm */
            char json_buf[512] = {0};
            int dev_fd = open(SURGICAL_ARM_DEV_PATH, O_RDONLY);
            if (dev_fd >= 0) {
                ssize_t r = read(dev_fd, json_buf, sizeof(json_buf) - 1);
                close(dev_fd);
                if (r > 0) {
                    if (send(client_fd, json_buf, r, MSG_NOSIGNAL) < 0) {
                        break; /* Client disconnected */
                    }
                }
            }
            usleep(50000); /* 20Hz telemetry stream */
        }
        close(client_fd);
    }
    return EXIT_SUCCESS;
}