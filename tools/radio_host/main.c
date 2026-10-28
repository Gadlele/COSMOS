/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* cosmos/radio.c on the computer's sockets, against a local broker and time server (RD_HOST, RD_PORT, RD_NTP_HOST,
 * RD_NTP_PORT): it prints the clock, who is there and the sequence as an FM-1 tuned in would have them. A test
 * harness, not part of the firmware.
 *   cc -O1 -DRD_HOST='"127.0.0.1"' -DRD_PORT=1883 -DRD_NTP_HOST='"127.0.0.1"' -DRD_NTP_PORT=12300 \
 *      -I../../cosmos main.c -lpthread -o radio_host
 *   ./radio_host ID seconds P          a player: a note every 400 ms
 *   ./radio_host ID seconds L [PLAYER] a listener (of PLAYER's id): each note heard, with how late it came
 *   (-DRD_KEEP_S=4: the pings and the watchdog, quickly; PLACE="lat lon" in the environment) */
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

static uint32_t day_ms(void)                     /* the radio clock's ms of the day */
{
    uint32_t sec, ms;
    return radio_clock(&sec, &ms) ? (sec % 86400u) * 1000u + ms : 0u;
}

int main(int argc, char **argv)
{
    uint32_t until, last = 0, last_note = 0;
    int last_state = -1, role, n = 0, listening = 0;
    if (argc < 4 || (argv[3][0] != 'P' && argv[3][0] != 'L')) {
        fprintf(stderr, "usage: radio_host ID seconds P|L [PLAYER]\n");
        return 2;
    }
    role = argv[3][0] == 'P' ? 1 : 2;
    until = plat_ms() + (uint32_t)atoi(argv[2]) * 1000u;
    if (radio_on(role, argv[1], getenv("PLACE") ? getenv("PLACE") : "41.90 12.50"))
        return 1;
    while ((int32_t)(plat_ms() - until) < 0) {
        uint32_t t, h, noted;
        uint8_t note;
        char id[6], place[14];
        int i;
        if (radio_state() != last_state)
            printf("state %d %s\n", last_state = radio_state(), radio_why()), fflush(stdout);
        if (role == 1 && radio_state() == RADIO_LIVE && plat_ms() - last_note >= 400u) {   /* a scale, round */
            static const uint8_t SC[8] = {60, 62, 64, 65, 67, 69, 71, 72};
            last_note = plat_ms();
            if (day_ms())
                radio_play(day_ms(), SC[n++ % 8]);
        }
        if (role == 2 && !listening && argc > 4)  /* the player asked for, once it is on air */
            for (i = 0; radio_player(i, &h, id, place, &noted); i++)
                if (!strcmp(id, argv[4])) {
                    radio_listen(h, id);
                    listening = 1;
                    printf("listening to %s at %s\n", id, place), fflush(stdout);
                }
        while (radio_heard(&t, &note)) {
            int32_t late = (int32_t)(day_ms() - t);
            printf("heard %u late %d ms\n", (unsigned)note, (int)late), fflush(stdout);
        }
        if (plat_ms() - last >= 5000u) {          /* every 5 s: the players on air, who listens to this one */
            last = plat_ms();
            printf("players:");
            for (i = 0; radio_player(i, &h, id, place, &noted); i++)
                printf(" %s(%s, note %u ms ago)", id, place, (unsigned)noted);
            printf("  listeners %d\n", radio_listeners()), fflush(stdout);
        }
        usleep(2000);
    }
    radio_on(0, 0, 0);
    usleep(600000);
    printf("state %d\n", radio_state());
    return 0;
}
