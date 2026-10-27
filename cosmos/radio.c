/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* RADIO COSMOS: the FM-1s that tune in play one chord sequence at the same time, each with its own sky. A task of
 * its own, while RADIO is on, keeps the time (SNTP) and a connection to a public MQTT broker (3.1.1, QoS 0, plain
 * TCP: the messages are notes), under "cosmos-radio/v1/":
 *   seq/<slot % 16>  (retained) "<slot> <note> <note> <note>": the chord of a 10 s slot (slot = unix time / 10), up
 *                    to three MIDI notes; none: the sky's own. The web page (web/site/radio) writes them, up to ten
 *                    slots ahead; each FM-1 locks the next slot's when the current one begins
 *   here/<id>        (retained) "<lat> <lon>" while an FM-1 is tuned in; its will (and leaving) empties it
 *   web/<id>         the same for the web page's visitors
 * Each FM-1 counts the others and the visitors from these. Included by net.c (lwIP's sockets); tools/radio_host
 * builds it on the computer's own, against a local broker. */
enum { RADIO_OFF, RADIO_NO_WIFI, RADIO_CONNECTING, RADIO_LIVE, RADIO_RETRY };   /* app.h's */
#ifndef RD_HOST                                  /* the web page uses the same broker, over WebSocket */
#define RD_HOST "broker.hivemq.com"
#define RD_PORT 1883
#endif
#ifndef RD_NTP_HOST
#define RD_NTP_HOST "pool.ntp.org"
#define RD_NTP_PORT 123
#endif
#ifndef RD_KEEP_S
#define RD_KEEP_S 60
#endif
#define RD_TOPIC "cosmos-radio/v1/"
#define RD_SLOTS 16                              /* the slots kept: the sequence's ring */
#define RD_PEERS 48                              /* the FM-1s and the visitors counted, each */
#define RD_NTP_EVERY_MS (15u * 60u * 1000u)

static struct {
    volatile int want, running, state;           /* the player wants it; the task is on; RADIO_* */
    char id[8];                                  /* this FM-1's (from its chip) */
    char where[24];                              /* its place, "41.90 12.50" */
    /* the clock: at plat_ms c_ms0, the unix time was c_sec0 s and c_frac0 ms (synced: SNTP answered) */
    volatile int synced;
    volatile uint32_t c_ms0, c_sec0, c_frac0;
    uint32_t ntp_at;                             /* plat_ms of the last answer */
    /* the sequence as the broker has it */
    struct {
        uint32_t slot;
        uint8_t n, note[3];
    } seq[RD_SLOTS];
    uint32_t fm[RD_PEERS], web[RD_PEERS];        /* who is there: their ids, hashed (0: none) */
    char why[24];
} rd;
static volatile int dns_busy;                    /* gethostbyname is lwIP's, one at a time (the weather's fetch too) */

static void rd_why(const char *s)
{
    unsigned i;
    for (i = 0; i + 1u < sizeof rd.why && s[i]; i++)
        rd.why[i] = s[i];
    rd.why[i] = 0;
    shim_log(s);
}
static int rd_resolve(const char *host, struct sockaddr_in *a, int port)
{
    struct hostent *he;
    while (dns_busy)
        os_time_dly(1);
    dns_busy = 1;
    he = gethostbyname(host);
    memset(a, 0, sizeof *a);
    if (he && he->h_addr_list && he->h_addr_list[0])
        memcpy(&a->sin_addr, he->h_addr_list[0], 4);
    dns_busy = 0;
    a->sin_len = sizeof *a;
    a->sin_family = AF_INET;
    a->sin_port = htons((uint16_t)port);
    return a->sin_addr.s_addr ? 0 : -1;
}

/* ---- the time: SNTP (RFC 4330), three tries, the quickest answer kept */
static void rd_ntp(void)
{
    struct sockaddr_in a;
    int sock, k, ms = 1500;
    uint32_t best = 0xFFFFFFFFu;
    if (rd_resolve(RD_NTP_HOST, &a, RD_NTP_PORT)) {
        rd_why("ntp: dns failed");
        return;
    }
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &ms, sizeof ms);
    for (k = 0; k < 3; k++) {
        uint8_t p[48];
        uint32_t t1, t4, sec, frac;
        int r;
        memset(p, 0, sizeof p);
        p[0] = 0x23;                              /* no leap warning, version 4, a client */
        t1 = plat_ms();
        if (sendto(sock, p, sizeof p, 0, (struct sockaddr *)&a, sizeof a) != (int)sizeof p)
            continue;
        r = recv(sock, p, sizeof p, 0);
        t4 = plat_ms();
        if (r < 48 || (p[0] & 7u) != 4u)          /* a server's answer, whole */
            continue;
        sec = (uint32_t)p[40] << 24 | (uint32_t)p[41] << 16 | (uint32_t)p[42] << 8 | p[43];
        frac = (uint32_t)p[44] << 8 | p[45];      /* the second's fraction, its top 16 bits */
        if (!sec || t4 - t1 > 1500u || t4 - t1 >= best)
            continue;
        best = t4 - t1;
        rd.c_ms0 = t1 + (t4 - t1) / 2u;           /* the server's time halfway through the round trip */
        rd.c_sec0 = sec - 2208988800u;            /* from 1900 to 1970 */
        rd.c_frac0 = (frac * 1000u) >> 16;
        rd.synced = 1;
    }
    closesocket(sock);
    if (best != 0xFFFFFFFFu)
        rd.ntp_at = plat_ms();
    else
        rd_why("ntp: no answer");
}

/* ---- MQTT: the packets this side sends */
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
/* a packet: its type byte, then the body pk + 2 .. q (short: one byte of length) */
static int send_pk(int sock, uint8_t *pk, uint8_t type, const uint8_t *q)
{
    pk[0] = type;
    pk[1] = (uint8_t)(q - pk - 2);
    return send_all(sock, pk, (int)(q - pk));
}
/* this FM-1's presence: retained, its place; empty: gone */
static int rd_presence(int sock, int here)
{
    uint8_t pk[80], *q = pk + 2;
    char t[40];
    unsigned n;
    snprintf(t, sizeof t, RD_TOPIC "here/%s", rd.id);
    q = putstr(q, t);
    n = here ? (unsigned)strlen(rd.where) : 0u;
    memcpy(q, rd.where, n);
    return send_pk(sock, pk, 0x31, q + n);       /* PUBLISH, retained, QoS 0 */
}

static int rd_connect(void)
{
    struct sockaddr_in a;
    uint8_t pk[128], *q;
    char t[40];
    int sock, ms;
    if (rd_resolve(RD_HOST, &a, RD_PORT)) {
        rd_why("dns failed");
        return -1;
    }
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        rd_why("no socket");
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&a, sizeof a) < 0) {
        closesocket(sock);
        rd_why("connect failed");
        return -1;
    }
    ms = 250;                                     /* the loop comes round four times a second */
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &ms, sizeof ms);
    ms = 5000;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &ms, sizeof ms);
    q = pk + 2;                                   /* CONNECT: a clean session, a will that empties the presence */
    q = putstr(q, "MQTT");
    *q++ = 4;
    *q++ = 0x02 | 0x04 | 0x20;                    /* clean, will, will retained (QoS 0) */
    q = put16(q, RD_KEEP_S);
    snprintf(t, sizeof t, "cosmos-%s-%u", rd.id, (unsigned)(plat_ms() & 0xFFFFu));
    q = putstr(q, t);
    snprintf(t, sizeof t, RD_TOPIC "here/%s", rd.id);
    q = putstr(q, t);
    q = put16(q, 0);                              /* the will's message: none */
    if (send_pk(sock, pk, 0x10, q)) {
        closesocket(sock);
        rd_why("send failed");
        return -1;
    }
    q = pk + 2;                                   /* SUBSCRIBE: packet 1, three filters at QoS 0 */
    q = put16(q, 1);
    q = putstr(q, RD_TOPIC "seq/+");
    *q++ = 0;
    q = putstr(q, RD_TOPIC "here/+");
    *q++ = 0;
    q = putstr(q, RD_TOPIC "web/+");
    *q++ = 0;
    if (send_pk(sock, pk, 0x82, q) || rd_presence(sock, 1)) {
        closesocket(sock);
        rd_why("send failed");
        return -1;
    }
    return sock;
}

/* ---- what comes */
static uint32_t rd_hash(const uint8_t *s, int n)  /* FNV-1a, never 0 */
{
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < n; i++)
        h = (h ^ s[i]) * 16777619u;
    return h ? h : 1u;
}
static void rd_peer(uint32_t *list, uint32_t h, int here)
{
    int i, free_i = -1;
    for (i = 0; i < RD_PEERS; i++) {
        if (list[i] == h) {
            if (!here)
                list[i] = 0;
            return;
        }
        if (!list[i] && free_i < 0)
            free_i = i;
    }
    if (here && free_i >= 0)
        list[free_i] = h;
}
static int starts(const uint8_t *s, int n, const char *p)
{
    int i;
    for (i = 0; p[i]; i++)
        if (i >= n || s[i] != (uint8_t)p[i])
            return 0;
    return i;
}
/* a PUBLISH: its topic and its message */
static void rd_take(const uint8_t *tp, int tn, const uint8_t *m, int mn)
{
    int k;
    if ((k = starts(tp, tn, RD_TOPIC "seq/"))) {  /* "<slot> <note> <note> <note>" */
        uint32_t v[4] = {0, 0, 0, 0};
        int n = 0, in = 0, i;
        for (i = 0; i < mn && n < 4; i++) {
            if (m[i] >= '0' && m[i] <= '9') {
                v[n] = v[n] * 10u + (uint32_t)(m[i] - '0');
                in = 1;
            } else if (in) {
                n++;
                in = 0;
            }
        }
        n += in && n < 4;
        if (n >= 1) {
            int s = (int)(v[0] % RD_SLOTS), j, c = 0;
            rd.seq[s].slot = v[0];
            for (j = 1; j < n; j++)
                if (v[j] >= 24u && v[j] <= 108u)  /* C1 .. C8 */
                    rd.seq[s].note[c++] = (uint8_t)v[j];
            rd.seq[s].n = (uint8_t)c;
        }
    } else if ((k = starts(tp, tn, RD_TOPIC "here/"))) {
        rd_peer(rd.fm, rd_hash(tp + k, tn - k), mn > 0);
    } else if ((k = starts(tp, tn, RD_TOPIC "web/"))) {
        rd_peer(rd.web, rd_hash(tp + k, tn - k), mn > 0);
    }
}

/* one connection's life: its packets until it ends (or the player is done); 0 = the player is done */
static int rd_session(int sock)
{
    uint8_t buf[192];
    int have = 0, waiting_ping = 0;
    uint32_t last_tx = plat_ms(), last_rx = plat_ms();
    for (;;) {
        int r, err = 0;
        socklen_t el = sizeof err;
        uint32_t now;
        if (!rd.want || plat_wifi_state() != WIFI_CONNECTED)
            return rd.want;
        if (!rd.synced || plat_ms() - rd.ntp_at > RD_NTP_EVERY_MS) {
            rd_ntp();                             /* the clock again (MQTT waits a moment) */
            if (!rd.synced)
                rd.ntp_at = plat_ms() - RD_NTP_EVERY_MS + 30000u;   /* none: again in 30 s */
        }
        r = recv(sock, buf + have, sizeof buf - (unsigned)have, 0);
        now = plat_ms();
        if (r == 0) {
            rd_why("broker closed");
            return 1;
        }
        if (r < 0) {
            getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &el);
            if (err && err != EWOULDBLOCK && err != EAGAIN) {
                rd_why("connection lost");
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
            if (len + (uint32_t)h > sizeof buf) { /* too long for us: not ours */
                rd_why("packet too long");
                return 1;
            }
            if ((uint32_t)have < len + (uint32_t)h)
                break;
            switch (buf[0] >> 4) {
            case 2:                               /* CONNACK */
                if (len < 2 || buf[h + 1] != 0) {
                    rd_why("broker refused");
                    return 1;
                }
                rd.state = RADIO_LIVE;
                rd.why[0] = 0;
                break;
            case 3: {                             /* PUBLISH (QoS 0: no packet id) */
                unsigned tl = len >= 2 ? (unsigned)buf[h] << 8 | buf[h + 1] : 0;
                if (2u + tl <= len)
                    rd_take(buf + h + 2, (int)tl, buf + h + 2 + tl, (int)(len - 2u - tl));
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
        if (now - last_tx >= RD_KEEP_S * 500u) {  /* PINGREQ at half the keep-alive */
            static const uint8_t PING[2] = {0xC0, 0};
            if (waiting_ping || send_all(sock, PING, 2)) {
                rd_why(waiting_ping ? "no answer" : "send failed");
                return 1;
            }
            waiting_ping = 1;
            last_tx = now;
        }
        if (now - last_rx > RD_KEEP_S * 1500u) {
            rd_why("silent broker");
            return 1;
        }
        if (r < 0)
            os_time_dly(0);
    }
}

static void rd_task(void *arg)
{
    (void)arg;
    while (rd.want) {
        int sock;
        if (plat_wifi_state() != WIFI_CONNECTED) {
            rd.state = RADIO_NO_WIFI;
            os_time_dly(50);                      /* half a second */
            continue;
        }
        rd.state = RADIO_CONNECTING;
        memset(rd.fm, 0, sizeof rd.fm);           /* the broker tells who is there again */
        memset(rd.web, 0, sizeof rd.web);
        sock = rd_connect();
        if (sock >= 0) {
            int more = rd_session(sock);
            if (!more) {                          /* leaving: the presence emptied, then goodbye */
                static const uint8_t BYE[2] = {0xE0, 0};
                rd_presence(sock, 0);
                send_all(sock, BYE, 2);
            }
            closesocket(sock);
            if (!more)
                break;
        }
        rd.state = RADIO_RETRY;
        {
            int k;
            for (k = 0; k < 10 && rd.want; k++)   /* five seconds, then again */
                os_time_dly(50);
        }
    }
    rd.state = RADIO_OFF;
    rd.running = 0;
}

/* ---- the app's */
int radio_on(int on, const char *id, const char *where)
{
    unsigned i;
    if (!on) {
        rd.want = 0;
        return 0;
    }
    for (i = 0; i + 1u < sizeof rd.id && id[i]; i++)
        rd.id[i] = id[i];
    rd.id[i] = 0;
    for (i = 0; i + 1u < sizeof rd.where && where[i]; i++)
        rd.where[i] = where[i];
    rd.where[i] = 0;
    rd.want = 1;
    if (rd.running)
        return 0;
    rd.running = 1;
    rd.state = RADIO_CONNECTING;
    if (plat_net_task(rd_task, 0, "radio")) {
        rd.running = 0;
        rd.want = 0;
        rd.state = RADIO_OFF;
        rd_why("no task");
        return -1;
    }
    return 0;
}
int radio_state(void) { return rd.state; }
const char *radio_why(void) { return rd.why; }
/* the time now, to the millisecond: 1 = synced */
int radio_clock(uint32_t *sec, uint32_t *ms)
{
    uint32_t tot;
    if (!rd.synced)
        return 0;
    tot = rd.c_frac0 + (plat_ms() - rd.c_ms0);
    *sec = rd.c_sec0 + tot / 1000u;
    *ms = tot % 1000u;
    return 1;
}
/* a slot's chord as the broker has it now: its notes (up to 3); 0 none (the sky's) */
int radio_slot(uint32_t slot, uint8_t *notes)
{
    int s = (int)(slot % RD_SLOTS), i;
    if (rd.seq[s].slot != slot)
        return 0;
    for (i = 0; i < rd.seq[s].n; i++)
        notes[i] = rd.seq[s].note[i];
    return rd.seq[s].n;
}
void radio_counts(int *fm1, int *web)
{
    int i, a = 0, b = 0;
    for (i = 0; i < RD_PEERS; i++) {
        a += rd.fm[i] != 0;
        b += rd.web[i] != 0;
    }
    *fm1 = a;
    *web = b;
}
