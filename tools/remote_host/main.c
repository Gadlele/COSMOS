/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* cosmos/remote.c on the computer's sockets, against a broker (a local mosquitto: RM_HOST, RM_PORT): it prints
 * each message's numbers as the FM-1 would take them. A test harness, not part of the firmware.
 *   cc -O1 -DRM_HOST='"127.0.0.1"' -DRM_PORT=1883 -I../../cosmos main.c -lpthread -o remote_host
 *   ./remote_host CODE [seconds]   (-DRM_KEEP_S=4: the pings and the watchdog, quickly) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define closesocket close
#define sin_len sin_zero[0]                       /* lwIP's field; BSD's has it, Linux's not: somewhere harmless */
enum { WIFI_CONNECTED = 5 };
static int opt_ms(int s, int lvl, int name, const void *v, socklen_t n)   /* lwIP's timeouts are an int of ms */
{
    if (lvl == SOL_SOCKET && (name == SO_RCVTIMEO || name == SO_SNDTIMEO) && n == sizeof(int)) {
        struct timeval t = {*(const int *)v / 1000, *(const int *)v % 1000 * 1000};
        return setsockopt(s, lvl, name, &t, sizeof t);
    }
    return setsockopt(s, lvl, name, v, n);
}
#define setsockopt opt_ms
static int wifi = WIFI_CONNECTED;
static uint32_t plat_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint32_t)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
static void os_time_dly(int ticks) { usleep(ticks ? ticks * 10000 : 1000); }
static int plat_wifi_state(void) { return wifi; }
static void shim_log(const char *s) { fprintf(stderr, "[remote] %s\n", s); }
static void *run(void *a)
{
    void (**fn)(void *) = a;
    (*fn)(0);
    return 0;
}
static int plat_net_task(void (*fn)(void *), void *arg, const char *name)
{
    static void (*f)(void *);
    pthread_t t;
    (void)arg, (void)name;
    f = fn;
    return pthread_create(&t, 0, run, &f) ? -1 : (pthread_detach(t), 0);
}
#include "remote.c"

int main(int argc, char **argv)
{
    uint32_t until, seen = 0;
    int last_state = -1;
    if (argc < 2) {
        fprintf(stderr, "usage: remote_host CODE [seconds]\n");
        return 2;
    }
    until = plat_ms() + (argc > 2 ? (uint32_t)atoi(argv[2]) : 20u) * 1000u;
    if (net_remote(1, argv[1]))
        return 1;
    while ((int32_t)(plat_ms() - until) < 0) {
        int32_t o, r;
        uint32_t age = net_remote_get(&o, &r);
        if (net_remote_state() != last_state)
            printf("state %d %s\n", last_state = net_remote_state(), net_remote_why()), fflush(stdout);
        if (rm.got != seen) {
            seen = rm.got;
            printf("open %d res %d age %u\n", (int)o, (int)r, (unsigned)age), fflush(stdout);
        }
        usleep(5000);
    }
    net_remote(0, 0);
    usleep(400000);
    printf("messages %u state %d\n", (unsigned)rm.got, net_remote_state());
    return 0;
}
