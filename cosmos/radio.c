/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 the COSMOS contributors */
/* RADIO COSMOS: players in pairs, and listeners. An FM-1 tuned in is a PLAYER or a LISTENER. The players pair up,
 * two by two, at random: one plays the PAD, the other the BELLS (a coin both toss alike, from their ids), each hearing
 * the other's notes at once; one alone plays the bells, waiting. A LISTENER hears one pair (or one alone), chosen on
 * its RADIO page. Every FM-1 plays what it hears in its own sky's sounds. A task of its own, while tuned in, keeps the
 * time (SNTP) and a connection to a public MQTT broker (3.1.1, QoS 0, plain TCP: the messages are notes), under
 * "cosmos-radio/v3/":
 *   here/<id>   (retained) "<lat> <lon> P [<id>]" a player and its partner (the pair: each names the other),
 *               "<lat> <lon> L [<id>]" a listener and whom it hears; its will (and leaving) empties it
 *   play/<id>   a player's keys: "<t> <k><note> ..." (t: ms of the day, UTC; k: b a bell, p a pad note held, o let
 *               go; MIDI notes): the partner plays them at once, the listeners 600 ms after they were played
 * Included by net.c (lwIP's sockets); tools/radio_host builds it on the computer's own, against a local broker. In
 * the browser emulator (RD_WEB, from app.c) the same radio talks through the page instead: the worklet takes what
 * goes out (rdw_poll, web.c's web_radio_poll) and the page's MQTT over WebSocket brings what comes
 * (cosmos/web/emu/app.js). */
#ifndef RD_WEB
enum { RADIO_OFF, RADIO_NO_WIFI, RADIO_CONNECTING, RADIO_LIVE, RADIO_RETRY };   /* app.h's */
#endif
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
#define RD_TOPIC "cosmos-radio/v3/"
#define RD_PEERS 24                              /* the FM-1s known at once */
#define RD_EV 32                                 /* the notes waiting, each way */
#define RD_NTP_EVERY_MS (15u * 60u * 1000u)

static struct {
    volatile int want, running, state;           /* the player wants it; the task is on; RADIO_* */
    char id[8];                                  /* this FM-1's (from its chip) */
    char where[24];                              /* its place, "41.90 12.50" */
    /* the clock: at plat_ms c_ms0, the unix time was c_sec0 s and c_frac0 ms (synced: SNTP answered) */
    volatile int synced;
    volatile uint32_t c_ms0, c_sec0, c_frac0;
    uint32_t ntp_at;                             /* plat_ms of the last answer */
    struct {                                     /* who is tuned in (h 0: a free slot) */
        uint32_t h, target, noted;               /* its id hashed; a listener's player or a player's partner, hashed;
                                                  * plat_ms of its last note */
        char id[6], place[14];                   /* its id; its place, "41.90 12.50" */
        uint8_t role;                            /* 1 a player, 2 a listener */
    } peer[RD_PEERS];
    uint32_t me;                                 /* this FM-1's id, hashed */
    volatile int role;                           /* this one's: 1 a player, 2 a listener */
    volatile uint32_t target;                    /* a listener's player, hashed (0 none) */
    char target_id[6];
    volatile int pres_dirty;                     /* the presence to send again (the role or the player changed) */
    volatile uint32_t partner;                   /* a player's partner, hashed (0 none): asked, or the pair made */
    char partner_id[6];
    uint32_t asked_at, next_ask, seed;           /* plat_ms the partner was asked; the next try; the dice */
    struct {
        uint32_t t;
        uint8_t kind, note;                      /* 'b', 'p', 'o'; a MIDI note */
    } in[RD_EV], out[RD_EV];                     /* the notes: heard, to go (a player's) */
    volatile uint32_t in_w, in_r, out_w, out_r;
    char why[24];
} rd;
static void rd_why(const char *s)
{
    unsigned i;
    for (i = 0; i + 1u < sizeof rd.why && s[i]; i++)
        rd.why[i] = s[i];
    rd.why[i] = 0;
#ifndef RD_WEB
    shim_log(s);
#endif
}
#ifndef RD_WEB
static volatile int dns_busy;                    /* gethostbyname is lwIP's, one at a time (the weather's fetch too) */
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
/* this FM-1's presence: retained, its place, its role (and a listener's player); empty: gone */
static int rd_presence(int sock, int here)
{
    uint8_t pk[96], *q = pk + 2;
    char t[40];
    snprintf(t, sizeof t, RD_TOPIC "here/%s", rd.id);
    q = putstr(q, t);
    if (here)
        q += snprintf((char *)q, 40, rd.role == 1 ? (rd.partner ? "%s P %s" : "%s P") : rd.target ? "%s L %s" : "%s L",
                      rd.where, rd.role == 1 ? rd.partner_id : rd.target_id);
    rd.pres_dirty = 0;
    return send_pk(sock, pk, 0x31, q);           /* PUBLISH, retained, QoS 0 */
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
    ms = 40;                                      /* the loop comes round 25 times a second: the notes */
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
    q = pk + 2;                                   /* SUBSCRIBE: packet 1, two filters at QoS 0 */
    q = put16(q, 1);
    q = putstr(q, RD_TOPIC "here/+");
    *q++ = 0;
    q = putstr(q, RD_TOPIC "play/+");
    *q++ = 0;
    if (send_pk(sock, pk, 0x82, q) || rd_presence(sock, 1)) {
        closesocket(sock);
        rd_why("send failed");
        return -1;
    }
    return sock;
}

#endif /* RD_WEB */

/* ---- what comes */
static uint32_t rd_hash(const uint8_t *s, int n)  /* FNV-1a, never 0 */
{
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < n; i++)
        h = (h ^ s[i]) * 16777619u;
    return h ? h : 1u;
}
static int rd_find(uint32_t h, int add)         /* a peer's slot (add: a free one if none); -1 */
{
    int i, free_i = -1;
    for (i = 0; i < RD_PEERS; i++) {
        if (rd.peer[i].h == h)
            return i;
        if (!rd.peer[i].h && free_i < 0)
            free_i = i;
    }
    if (add && free_i >= 0) {
        memset(&rd.peer[free_i], 0, sizeof rd.peer[free_i]);
        rd.peer[free_i].h = h;
    }
    return add ? free_i : -1;
}
static int starts(const uint8_t *s, int n, const char *p)
{
    int i;
    for (i = 0; p[i]; i++)
        if (i >= n || s[i] != (uint8_t)p[i])
            return 0;
    return i;
}
/* whether peer i's notes are this one's to play: a player's partner (the pair made); a listener's player, or that
 * player's partner */
static int rd_mated(int i)                       /* peer i, a player, paired: its partner's slot (else -1) */
{
    int j;
    if (rd.peer[i].role != 1 || !rd.peer[i].target)
        return -1;
    j = rd_find(rd.peer[i].target, 0);
    return j >= 0 && rd.peer[j].role == 1 && rd.peer[j].target == rd.peer[i].h ? j : -1;
}
static int rd_hears(int i)
{
    if (rd.role == 1)
        return rd.partner && rd.peer[i].h == rd.partner && rd.peer[i].target == rd.me;
    if (rd.role != 2 || !rd.target)
        return 0;
    if (rd.peer[i].h == rd.target)
        return 1;
    {
        int j = rd_mated(i);
        return j >= 0 && rd.peer[j].h == rd.target;
    }
}
/* a PUBLISH: its topic and its message */
static void rd_take(const uint8_t *tp, int tn, const uint8_t *m, int mn)
{
    int k;
    if ((k = starts(tp, tn, RD_TOPIC "here/"))) {   /* "<lat> <lon> P", "<lat> <lon> L [<id>]"; empty: gone */
        uint32_t h = rd_hash(tp + k, tn - k);
        int i = rd_find(h, mn > 0), j, f, w;
        if (i < 0)
            return;
        if (!mn) {
            rd.peer[i].h = 0;
            return;
        }
        for (j = 0; j < 5 && k + j < tn; j++)
            rd.peer[i].id[j] = (char)tp[k + j];
        rd.peer[i].id[j] = 0;
        {   /* its words: lat, lon (its place), its role, a listener's player */
            int ws[4] = {0, 0, 0, 0}, we[4] = {0, 0, 0, 0}, n = 0, p = 0;
            for (j = 0; j <= mn && n < 4; j++)
                if (j == mn || m[j] == ' ') {
                    if (j > p) {
                        ws[n] = p;
                        we[n++] = j;
                    }
                    p = j + 1;
                }
            for (j = f = 0; j < 2 && j < n; j++) {
                for (w = ws[j]; w < we[j] && f < 13; w++)
                    rd.peer[i].place[f++] = (char)m[w];
                if (j == 0 && f < 13)
                    rd.peer[i].place[f++] = ' ';
            }
            rd.peer[i].place[f] = 0;
            rd.peer[i].role = n > 2 && m[ws[2]] == 'P' ? 1u : 2u;
            rd.peer[i].target = n > 3 ? rd_hash(m + ws[3], we[3] - ws[3]) : 0u;
        }
    } else if ((k = starts(tp, tn, RD_TOPIC "play/"))) {   /* a player's notes: its star flashes; heard if ours */
        uint32_t h = rd_hash(tp + k, tn - k), v = 0, t = 0;
        int i = rd_find(h, 0), j, in = 0, odd = 0;
        uint8_t kind = 'b';
        if (i < 0)
            return;
        rd.peer[i].noted = plat_ms();
        if (h == rd.me || !rd_hears(i))
            return;
        for (j = 0; j <= mn; j++) {
            if (j < mn && odd && !in && (m[j] == 'b' || m[j] == 'p' || m[j] == 'o')) {
                kind = m[j];
            } else if (j < mn && m[j] >= '0' && m[j] <= '9') {
                v = v * 10u + (uint32_t)(m[j] - '0');
                in = 1;
            } else if (in) {
                if (!odd)
                    t = v;
                else if (v >= 24u && v <= 108u && rd.in_w - rd.in_r < RD_EV) {
                    rd.in[rd.in_w % RD_EV].t = t;
                    rd.in[rd.in_w % RD_EV].kind = kind;
                    rd.in[rd.in_w % RD_EV].note = (uint8_t)v;
                    rd.in_w++;
                }
                odd ^= 1;
                v = 0;
                in = 0;
                kind = 'b';
            }
        }
    }
}

/* ---- the pairs: each free player asks one free player at random (naming it as its partner); one asked takes the
 * one who asked; both naming each other, the pair is made. Unanswered in 4 s, or the partner gone: free again */
static uint32_t rd_rand(void)
{
    rd.seed = rd.seed * 1664525u + 1013904223u + plat_ms();
    return rd.seed >> 8;
}
static void rd_set_partner(int i)                /* i -1: none */
{
    rd.partner = i < 0 ? 0u : rd.peer[i].h;
    if (i >= 0)
        memcpy(rd.partner_id, rd.peer[i].id, 6);
    else
        rd.partner_id[0] = 0;
    rd.asked_at = plat_ms();
    rd.pres_dirty = 1;
}
static void rd_pair(void)
{
    uint32_t now = plat_ms();
    int k, i = rd.partner ? rd_find(rd.partner, 0) : -1, n = 0;
    if (rd.role != 1) {
        if (rd.partner)
            rd_set_partner(-1);
        return;
    }
    if (rd.partner) {
        if (i < 0 || rd.peer[i].role != 1 || (rd.peer[i].target && rd.peer[i].target != rd.me)) {
            rd_set_partner(-1);                   /* gone, or another's */
            rd.next_ask = now + 500u + rd_rand() % 1500u;
        } else if (rd.peer[i].target == rd.me) {
            rd.asked_at = now;                    /* the pair: held */
        } else if (now - rd.asked_at > 4000u) {
            rd_set_partner(-1);                   /* no answer */
            rd.next_ask = now + 500u + rd_rand() % 2500u;
        }
        if (rd.partner && rd.peer[i].target == rd.me)
            return;
    }
    for (k = 0; k < RD_PEERS; k++)                /* one asking me: taken at once */
        if (rd.peer[k].h && rd.peer[k].h != rd.me && rd.peer[k].role == 1 && rd.peer[k].target == rd.me) {
            if (rd.partner != rd.peer[k].h)
                rd_set_partner(k);
            return;
        }
    if (rd.partner || (int32_t)(now - rd.next_ask) < 0)
        return;
    for (k = 0; k < RD_PEERS; k++)                /* the free players: one at random */
        n += rd.peer[k].h && rd.peer[k].h != rd.me && rd.peer[k].role == 1 && !rd.peer[k].target;
    if (!n)
        return;
    n = (int)(rd_rand() % (uint32_t)n);
    for (k = 0; k < RD_PEERS; k++)
        if (rd.peer[k].h && rd.peer[k].h != rd.me && rd.peer[k].role == 1 && !rd.peer[k].target && !n--) {
            rd_set_partner(k);
            return;
        }
}

#ifndef RD_WEB
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
        if (rd.state == RADIO_LIVE)
            rd_pair();
        if (rd.pres_dirty && rd.state == RADIO_LIVE && rd_presence(sock, 1)) {
            rd_why("send failed");
            return 1;
        }
        if (rd.out_r != rd.out_w) {               /* a player's notes, as they come */
            uint8_t pk[128], *q2 = pk + 2;
            char t[40];
            int c = 0;
            snprintf(t, sizeof t, RD_TOPIC "play/%s", rd.id);
            q2 = putstr(q2, t);
            while (rd.out_r != rd.out_w && c < 6) {
                q2 += snprintf((char *)q2, 20, "%s%u %c%u", c ? " " : "", (unsigned)rd.out[rd.out_r % RD_EV].t,
                               rd.out[rd.out_r % RD_EV].kind, (unsigned)rd.out[rd.out_r % RD_EV].note);
                rd.out_r++;
                c++;
            }
            if (send_pk(sock, pk, 0x30, q2)) {
                rd_why("send failed");
                return 1;
            }
            last_tx = now;
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
        memset(rd.peer, 0, sizeof rd.peer);       /* the broker tells who is there again */
        rd.partner = 0;
        rd.partner_id[0] = 0;
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
/* tuned in as a player (1) or a listener (2), with this FM-1's id and place; 0: off */
int radio_on(int role, const char *id, const char *where)
{
    unsigned i;
    if (!role) {
        rd.want = 0;
        return 0;
    }
    if (role != rd.role)
        rd.pres_dirty = 1;
    rd.role = role;
    for (i = 0; i + 1u < sizeof rd.id && id[i]; i++)
        rd.id[i] = id[i];
    rd.id[i] = 0;
    for (i = 0; i + 1u < sizeof rd.where && where[i]; i++)
        rd.where[i] = where[i];
    rd.where[i] = 0;
    rd.me = rd_hash((const uint8_t *)rd.id, (int)strlen(rd.id));
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
#else /* RD_WEB */
/* ---- the browser's: what goes out, as lines for the page ("C <id>" connect, its will emptying here/<id>; "D"
 * disconnect; "P <retain> <topic> <message>"), taken by the worklet each few ms (rdw_poll: web.c exports these as
 * web_radio_*); what comes, a message at a time (rdw_in); the connection's state and the clock, the page's */
static char rd_out[2048];
static unsigned rd_out_n;
static uint8_t rd_in[512];
static void rd_line(const char *a, const char *b, const char *c)   /* a line out: its words (b, c: or 0) */
{
    char l[192];
    int n = snprintf(l, sizeof l, "%s%s%s%s%s\n", a, b ? " " : "", b ? b : "", c ? " " : "", c ? c : "");
    if (n > 0 && n < (int)sizeof l && rd_out_n + (unsigned)n < sizeof rd_out) {
        memcpy(rd_out + rd_out_n, l, (size_t)n);
        rd_out_n += (unsigned)n;
        rd_out[rd_out_n] = 0;
    }
}
static void rd_presence_web(int here)
{
    char t[40], m[64] = "";
    snprintf(t, sizeof t, RD_TOPIC "here/%s", rd.id);
    if (here)
        snprintf(m, sizeof m, rd.role == 1 ? (rd.partner ? "%s P %s" : "%s P") : rd.target ? "%s L %s" : "%s L",
                 rd.where, rd.role == 1 ? rd.partner_id : rd.target_id);
    rd.pres_dirty = 0;
    rd_line("P 1", t, m);
}
int radio_on(int role, const char *id, const char *where)
{
    unsigned i;
    if (!role) {
        if (rd.want) {                            /* leaving: the presence emptied, then goodbye */
            if (rd.state == RADIO_LIVE)
                rd_presence_web(0);
            rd_line("D", 0, 0);
        }
        rd.want = 0;
        rd.state = RADIO_OFF;
        return 0;
    }
    if (role != rd.role)
        rd.pres_dirty = 1;
    rd.role = role;
    for (i = 0; i + 1u < sizeof rd.id && id[i]; i++)
        rd.id[i] = id[i];
    rd.id[i] = 0;
    for (i = 0; i + 1u < sizeof rd.where && where[i]; i++)
        rd.where[i] = where[i];
    rd.where[i] = 0;
    rd.me = rd_hash((const uint8_t *)rd.id, (int)strlen(rd.id));
    if (!rd.want) {
        rd.want = 1;
        rd.state = RADIO_CONNECTING;
        rd_line("C", rd.id, 0);
    }
    return 0;
}
/* the worklet, every few ms: the pairs, the presence, the notes; the lines out (their length; 0 none) */
int rdw_poll(void)
{
    if (rd.want && rd.state == RADIO_LIVE) {
        rd_pair();
        if (rd.pres_dirty)
            rd_presence_web(1);
        while (rd.out_r != rd.out_w) {
            char t[40], m[160];
            int c = 0, n = 0;
            snprintf(t, sizeof t, RD_TOPIC "play/%s", rd.id);
            while (rd.out_r != rd.out_w && c < 6) {
                n += snprintf(m + n, sizeof m - (unsigned)n, "%s%u %c%u", c ? " " : "", (unsigned)rd.out[rd.out_r % RD_EV].t,
                              rd.out[rd.out_r % RD_EV].kind, (unsigned)rd.out[rd.out_r % RD_EV].note);
                rd.out_r++;
                c++;
            }
            rd_line("P 0", t, m);
        }
    }
    return (int)rd_out_n;
}
char *rdw_out(void) { return rd_out; }
void rdw_taken(void) { rd_out_n = 0; rd_out[0] = 0; }
/* the page's connection: 1 on air (the broker tells who is there again), 0 lost (the page tries again) */
void rdw_state(int live)
{
    if (!rd.want)
        return;
    memset(rd.peer, 0, sizeof rd.peer);
    rd.partner = 0;
    rd.partner_id[0] = 0;
    rd.state = live ? RADIO_LIVE : RADIO_RETRY;
    rd_why(live ? "" : "reconnecting");
    rd.pres_dirty = 1;
}
uint8_t *rdw_in_buf(void) { return rd_in; }
/* a message come: its topic (tn bytes) then its message (mn bytes), in rdw_in_buf */
void rdw_in(int tn, int mn)
{
    if (rd.want && tn > 0 && mn >= 0 && tn + mn <= (int)sizeof rd_in)
        rd_take(rd_in, tn, rd_in + tn, mn);
}
/* the time now (the page's, from its site's clock) */
void rdw_clock(uint32_t sec, uint32_t ms)
{
    rd.c_ms0 = plat_ms();
    rd.c_sec0 = sec;
    rd.c_frac0 = ms;
    rd.synced = 1;
}
#endif /* RD_WEB */
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
/* the players tuned in (this one left out), one at a time: i from 0; 0 = none at i. h its id hashed, id (6 B), place
 * (14 B), noted_ms how long since its last note (~0u never), mate its partner hashed (0: none, or not paired yet) */
int radio_player(int i, uint32_t *h, char *id, char *place, uint32_t *noted_ms, uint32_t *mate)
{
    int k, n = 0;
    for (k = 0; k < RD_PEERS; k++) {
        int j;
        if (!rd.peer[k].h || rd.peer[k].role != 1 || rd.peer[k].h == rd.me)
            continue;
        if (n++ != i)
            continue;
        *h = rd.peer[k].h;
        memcpy(id, rd.peer[k].id, 6);
        memcpy(place, rd.peer[k].place, 14);
        *noted_ms = rd.peer[k].noted ? plat_ms() - rd.peer[k].noted : ~0u;
        j = rd_mated(k);
        *mate = j >= 0 ? rd.peer[j].h : 0u;
        return 1;
    }
    return 0;
}
/* a player's part: 0 alone (the bells, waiting), 1 the PAD, 2 the BELLS, with its partner's id (6 B) */
int radio_jam(char *partner_id)
{
    uint32_t p = rd.partner;
    int i = p ? rd_find(p, 0) : -1, coin;
    char a[20];
    if (rd.role != 1 || i < 0 || rd.peer[i].target != rd.me)
        return 0;
    memcpy(partner_id, rd.partner_id, 6);
    {   /* the coin: the two ids hashed, the lower first (both toss alike); heads, the lower plays the pad */
        int lo = strcmp(rd.id, rd.partner_id) < 0;
        snprintf(a, sizeof a, "%s%s", lo ? rd.id : rd.partner_id, lo ? rd.partner_id : rd.id);
        coin = (int)(rd_hash((const uint8_t *)a, (int)strlen(a)) >> 7 & 1u);
        return coin == lo ? 1 : 2;
    }
}
/* a listener's player (h 0: none); its id goes out with the presence */
void radio_listen(uint32_t h, const char *id)
{
    unsigned i;
    for (i = 0; i < 5u && id && id[i]; i++)
        rd.target_id[i] = id[i];
    rd.target_id[i] = 0;
    rd.target = h;
    rd.in_r = rd.in_w;                            /* the last one's notes, not heard */
    rd.pres_dirty = 1;
}
/* how many listen to this player's pair */
int radio_listeners(void)
{
    int k, n = 0;
    uint32_t mate = rd.partner;
    int i = mate ? rd_find(mate, 0) : -1;
    if (i < 0 || rd.peer[i].target != rd.me)
        mate = 0;
    for (k = 0; k < RD_PEERS; k++)
        n += rd.peer[k].h && rd.peer[k].role == 2 && rd.peer[k].target &&
             (rd.peer[k].target == rd.me || rd.peer[k].target == mate);
    return n;
}
/* a player's key: kind 'b' a bell, 'p' a pad note held, 'o' let go; at t (ms of the day), to its partner and whoever
 * listens */
void radio_play(uint32_t t, uint8_t kind, uint8_t note)
{
    if (rd.out_w - rd.out_r >= RD_EV)
        return;
    rd.out[rd.out_w % RD_EV].t = t;
    rd.out[rd.out_w % RD_EV].kind = kind;
    rd.out[rd.out_w % RD_EV].note = note;
    rd.out_w++;
}
/* the next note heard (the partner's, or the pair listened to): 1 = one (t its time, ms of the day; kind as above) */
int radio_heard(uint32_t *t, uint8_t *kind, uint8_t *note)
{
    if (rd.in_r == rd.in_w)
        return 0;
    *t = rd.in[rd.in_r % RD_EV].t;
    *kind = rd.in[rd.in_r % RD_EV].kind;
    *note = rd.in[rd.in_r % RD_EV].note;
    rd.in_r++;
    return 1;
}
