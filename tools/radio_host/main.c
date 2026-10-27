/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* cosmos/radio.c on the computer's sockets, against a local broker and time server (RD_HOST, RD_PORT, RD_NTP_HOST,
 * RD_NTP_PORT): it prints the clock, who is there and the sequence as an FM-1 tuned in would have them. A test
 * harness, not part of the firmware.
 *   cc -O1 -DRD_HOST='"127.0.0.1"' -DRD_PORT=1883 -DRD_NTP_HOST='"127.0.0.1"' -DRD_NTP_PORT=12300 \
 *      -I../../cosmos main.c -lpthread -o radio_host
 *   ./radio_host ID [seconds]   (-DRD_KEEP_S=4: the pings and the watchdog, quickly) */
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
#include <stdarg.h>

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
#include "radio.c"

int main(int argc, char **argv)
{
    uint32_t until, last = 0;
    int last_state = -1;
    if (argc < 2) {
        fprintf(stderr, "usage: radio_host ID [seconds]\n");
        return 2;
    }
    until = plat_ms() + (argc > 2 ? (uint32_t)atoi(argv[2]) : 20u) * 1000u;
    if (radio_on(1, argv[1], "41.90 12.50"))
        return 1;
    while ((int32_t)(plat_ms() - until) < 0) {
        uint32_t s = 0, ms = 0;
        if (radio_state() != last_state)
            printf("state %d %s\n", last_state = radio_state(), radio_why()), fflush(stdout);
        if (plat_ms() - last >= 1000u) {          /* each second: the clock, who is there, the slots around now */
            int f, w, k;
            last = plat_ms();
            if (radio_clock(&s, &ms)) {
                struct timespec t;
                clock_gettime(CLOCK_REALTIME, &t);
                printf("clock %u.%03u (system %+ld ms)", (unsigned)s, (unsigned)ms,
                       (long)((int64_t)s * 1000 + ms - ((int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000)));
            } else {
                printf("clock none");
            }
            radio_counts(&f, &w);
            printf("  fm1 %d web %d  slots", f, w);
            for (k = 0; k < 4 && s; k++) {
                uint8_t n[3];
                int c = radio_slot(s / 10u + (uint32_t)k, n), j;
                printf(" [");
                for (j = 0; j < c; j++)
                    printf(j ? " %d" : "%d", n[j]);
                printf("]");
            }
            printf("\n");
            fflush(stdout);
        }
        usleep(5000);
    }
    radio_on(0, 0, 0);
    usleep(600000);
    printf("state %d\n", radio_state());
    return 0;
}
