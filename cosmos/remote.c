/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* The remote: the phone's filter over MQTT (3.1.1, QoS 0). A task of its own keeps a connection to a public
 * broker while the player wants it, subscribed to "cosmos-fm1/<code>"; the phone's page publishes "<open> <res>"
 * (0..1000 each) there over WebSocket. Plain TCP: the numbers are a filter's, and TLS would keep 21 KB of the heap
 * for as long as it lasts. Included by net.c (lwIP's sockets); tools/remote_host builds it on the computer's own */
enum { RM_OFF, RM_NO_WIFI, RM_CONNECTING, RM_LIVE, RM_RETRY };   /* app.h's */
#ifndef RM_HOST                                 /* the phone's page uses the same broker (web/site/remote) */
#define RM_HOST "broker.hivemq.com"
#define RM_PORT 1883
#endif
#ifndef RM_KEEP_S
#define RM_KEEP_S 60
#endif

static struct {
    volatile int want, running, state;           /* the player wants it; the task is on; RM_* */
    volatile int32_t open, res;                  /* the last message's, 0..1000 */
    volatile uint32_t got, at;                   /* messages so far; plat_ms of the last */
    char code[8];
    char why[24];
} rm;
static volatile int dns_busy;                    /* gethostbyname is lwIP's, one at a time (the fetch's too) */

static void rm_why(const char *s)
{
    unsigned i;
    for (i = 0; i + 1u < sizeof rm.why && s[i]; i++)
        rm.why[i] = s[i];
    rm.why[i] = 0;
    shim_log(s);
}
static int send_all(int sock, const uint8_t *b, int n)
{
    while (n > 0) {
        int r = send(sock, b, (size_t)n, 0);
        if (r <= 0)
            return -1;
        b += r;
        n -= r;
    }
    return 0;
}
static uint8_t *put16(uint8_t *d, unsigned v) { *d++ = (uint8_t)(v >> 8); *d++ = (uint8_t)v; return d; }
static uint8_t *putstr(uint8_t *d, const char *s)
{
    unsigned n = (unsigned)strlen(s);
    d = put16(d, n);
    memcpy(d, s, n);
    return d + n;
}
/* "<open> <res>": the numbers, 0..1000 */
static void rm_take(const uint8_t *p, int n)
{
    int32_t v[2] = {-1, 0}, k = 0, i;
    for (i = 0; i < n && k < 2; i++) {
        if (p[i] >= '0' && p[i] <= '9') {
            if (v[k] < 0)
                v[k] = 0;
            v[k] = v[k] * 10 + (p[i] - '0');
            if (v[k] > 1000)
                v[k] = 1000;
        } else if (v[k] >= 0) {
            k++;
            if (k < 2)
                v[k] = -1;
        }
    }
    if (v[0] < 0)
        return;
    rm.open = v[0];
    rm.res = v[1] < 0 ? 0 : v[1];
    rm.at = plat_ms();
    rm.got++;
}

static int rm_connect(void)
{
    struct hostent *he;
    struct sockaddr_in a;
    uint8_t pk[96], *q;
    char id[24], topic[32];
    int sock, ms;
    while (dns_busy)
        os_time_dly(1);
    dns_busy = 1;
    he = gethostbyname(RM_HOST);
    memset(&a, 0, sizeof a);
    if (he && he->h_addr_list && he->h_addr_list[0])
        memcpy(&a.sin_addr, he->h_addr_list[0], 4);
    dns_busy = 0;
    if (!a.sin_addr.s_addr) {
        rm_why("dns failed");
        return -1;
    }
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        rm_why("no socket");
        return -1;
    }
    a.sin_len = sizeof a;
    a.sin_family = AF_INET;
    a.sin_port = htons(RM_PORT);
    if (connect(sock, (struct sockaddr *)&a, sizeof a) < 0) {
        closesocket(sock);
        rm_why("connect failed");
        return -1;
    }
    ms = 250;                                     /* the loop comes round four times a second */
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &ms, sizeof ms);
    ms = 5000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &ms, sizeof ms);
    snprintf(id, sizeof id, "cosmos-%s-%u", rm.code, (unsigned)(plat_ms() & 0xFFFFu));
    q = pk + 2;                                   /* CONNECT: "MQTT" 4, a clean session, the keep-alive */
    q = putstr(q, "MQTT");
    *q++ = 4;
    *q++ = 0x02;
    q = put16(q, RM_KEEP_S);
    q = putstr(q, id);
    pk[0] = 0x10;
    pk[1] = (uint8_t)(q - pk - 2);
    if (send_all(sock, pk, (int)(q - pk))) {
        closesocket(sock);
        rm_why("send failed");
        return -1;
    }
    snprintf(topic, sizeof topic, "cosmos-fm1/%s", rm.code);
    q = pk + 2;                                   /* SUBSCRIBE: packet 1, the topic at QoS 0 */
    q = put16(q, 1);
    q = putstr(q, topic);
    *q++ = 0;
    pk[0] = 0x82;
    pk[1] = (uint8_t)(q - pk - 2);
    if (send_all(sock, pk, (int)(q - pk))) {
        closesocket(sock);
        rm_why("send failed");
        return -1;
    }
    return sock;
}

/* one connection's life: its packets until it ends (or the player is done); 0 = the player is done */
static int rm_session(int sock)
{
    uint8_t buf[160];
    int have = 0, waiting_ping = 0;
    uint32_t last_tx = plat_ms(), last_rx = plat_ms();
    for (;;) {
        int r, err = 0;
        socklen_t el = sizeof err;
        uint32_t now;
        if (!rm.want || plat_wifi_state() != WIFI_CONNECTED)
            return rm.want;
        r = recv(sock, buf + have, sizeof buf - (unsigned)have, 0);
        now = plat_ms();
        if (r == 0) {
            rm_why("broker closed");
            return 1;
        }
        if (r < 0) {
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &el);
            if (err && err != EWOULDBLOCK && err != EAGAIN) {
                rm_why("connection lost");
                return 1;
            }
        } else {
            have += r;
            last_rx = now;
        }
        for (;;) {                                /* the whole packets in buf */
            uint32_t len = 0, mul = 1;
            int h = 1, whole = 0;
            while (h < have && h < 5) {           /* the remaining length: up to 4 bytes, 7 bits each */
                len += (uint32_t)(buf[h] & 127u) * mul;
                mul <<= 7;
                if (!(buf[h++] & 128u)) {
                    whole = 1;
                    break;
                }
            }
            if (!whole)
                break;                            /* the length not in yet */
            if (len + (uint32_t)h > sizeof buf) { /* too long for us: not a filter's */
                rm_why("packet too long");
                return 1;
            }
            if ((uint32_t)have < len + (uint32_t)h)
                break;
            switch (buf[0] >> 4) {
            case 2:                               /* CONNACK */
                if (len < 2 || buf[h + 1] != 0) {
                    rm_why("broker refused");
                    return 1;
                }
                rm.state = RM_LIVE;
                rm.why[0] = 0;
                break;
            case 3: {                             /* PUBLISH (QoS 0: no packet id) */
                unsigned tl = len >= 2 ? (unsigned)buf[h] << 8 | buf[h + 1] : 0;
                if (2u + tl <= len)
                    rm_take(buf + h + 2 + tl, (int)(len - 2u - tl));
                break;
            }
            case 13:                              /* PINGRESP */
                waiting_ping = 0;
                break;
            default:                              /* SUBACK and the rest */
                break;
            }
            memmove(buf, buf + h + len, (size_t)(have - h - (int)len));
            have -= h + (int)len;
        }
        if (now - last_tx >= RM_KEEP_S * 500u) {  /* PINGREQ at half the keep-alive */
            static const uint8_t PING[2] = {0xC0, 0};
            if (waiting_ping || send_all(sock, PING, 2)) {
                rm_why(waiting_ping ? "no answer" : "send failed");
                return 1;
            }
            waiting_ping = 1;
            last_tx = now;
        }
        if (now - last_rx > RM_KEEP_S * 1500u) {
            rm_why("silent broker");
            return 1;
        }
        if (r < 0)
            os_time_dly(0);
    }
}

static void rm_task(void *arg)
{
    (void)arg;
    while (rm.want) {
        int sock;
        if (plat_wifi_state() != WIFI_CONNECTED) {
            rm.state = RM_NO_WIFI;
            os_time_dly(50);                      /* half a second */
            continue;
        }
        rm.state = RM_CONNECTING;
        sock = rm_connect();
        if (sock >= 0) {
            int more = rm_session(sock);
            closesocket(sock);
            if (!more)
                break;
        }
        rm.state = RM_RETRY;
        {
            int k;
            for (k = 0; k < 10 && rm.want; k++)   /* five seconds, then again */
                os_time_dly(50);
        }
    }
    rm.state = RM_OFF;
    rm.running = 0;
}

/* the app's: on with the pairing code (5 characters), or off */
int net_remote(int on, const char *code)
{
    unsigned i;
    if (!on) {
        rm.want = 0;
        return 0;
    }
    for (i = 0; i + 1u < sizeof rm.code && code[i]; i++)
        rm.code[i] = code[i];
    rm.code[i] = 0;
    rm.want = 1;
    if (rm.running)
        return 0;
    rm.running = 1;
    rm.state = RM_CONNECTING;
    if (plat_net_task(rm_task, 0, "remote")) {
        rm.running = 0;
        rm.want = 0;
        rm.state = RM_OFF;
        rm_why("no task");
        return -1;
    }
    return 0;
}
int net_remote_state(void) { return rm.state; }
const char *net_remote_why(void) { return rm.why; }
/* the last message: its numbers (0..1000), how long ago (ms; ~0u none yet) */
uint32_t net_remote_get(int32_t *open, int32_t *res)
{
    if (open)
        *open = rm.open;
    if (res)
        *res = rm.res;
    return rm.got ? plat_ms() - rm.at : ~0u;
}
