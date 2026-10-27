/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Charles Vestal
 * Copyright (C) 2026 the COSMOS contributors */
/* COSMOS's network side, built against the SDK's lwIP headers (as COSMOS PROBE's): one task fetches the weather at the
 * player's place (Open-Meteo: the cloud cover, the pressure and its last three hours, the place's offset from UTC) and
 * the Kp index (NOAA) over HTTPS, and reads the few numbers COSMOS needs; the servers' Date header gives the time. The connection and TLS are fm1-radio's
 * (net/radio_net.c), cut down: BearSSL's client, TLS 1.2 with ECDHE and AES-GCM. The server's certificate is NOT
 * checked (no roots, no clock): its key is used as sent. For weather numbers the worst a man in the middle can do is
 * change the weather. */
#include <stdint.h>
#include <string.h>
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "shim.h"
#include "plat.h"
#include "bearssl.h"

extern int snprintf(char *buf, unsigned long sz, const char *f, ...);
extern void *malloc(unsigned long n);
extern void free(void *p);
extern void os_time_dly(int ticks);
void br_fm1_yield(void) { os_time_dly(0); }        /* BearSSL's long computations let WiFi's tasks run (inner.h) */

typedef struct {
    int done;
    int32_t cloud, press10, trend10, utc_s, kp100, wcode;
    char date[40];
    char why[32];
} net_res_t;                                      /* app.h's */
void cosmos_entropy(uint8_t *p, unsigned n);

static net_res_t res;
static volatile int busy;
static int32_t at_lat100, at_lon100;

#include "remote.c"                              /* the phone's filter, over MQTT */

static void why_to(const char *s)
{
    unsigned i;
    for (i = 0; i + 1u < sizeof res.why && s[i]; i++)
        res.why[i] = s[i];
    res.why[i] = 0;
    shim_log(s);
}

/* ---- the connection: a socket and TLS over it */
typedef struct {
    const br_x509_class *vtable;                  /* no checks: the first certificate's key, as sent */
    br_x509_decoder_context dec;
    int depth;
} nov_t;
typedef struct {
    br_ssl_client_context cc;
    nov_t x509;
    br_sslio_context io;
} tls_t;
typedef struct {
    int sock;
    tls_t *tls;                                   /* from the heap, while the connection lasts */
    unsigned char *buf;                           /* its 16.7 KB record buffer, from the heap */
} conn_t;

static void nov_start_chain(const br_x509_class **c, const char *name) { (void)name; ((nov_t *)c)->depth = 0; }
static void nov_start_cert(const br_x509_class **c, uint32_t len)
{
    nov_t *x = (nov_t *)c;
    (void)len;
    if (x->depth == 0)
        br_x509_decoder_init(&x->dec, 0, 0);
}
static void nov_append(const br_x509_class **c, const unsigned char *b, size_t n)
{
    nov_t *x = (nov_t *)c;
    if (x->depth == 0)
        br_x509_decoder_push(&x->dec, b, n);
}
static void nov_end_cert(const br_x509_class **c) { ((nov_t *)c)->depth++; }
static unsigned nov_end_chain(const br_x509_class **c)
{
    nov_t *x = (nov_t *)c;
    return x->depth && br_x509_decoder_get_pkey(&x->dec) ? 0u : (unsigned)BR_ERR_X509_EMPTY_CHAIN;
}
static const br_x509_pkey *nov_get_pkey(const br_x509_class *const *c, unsigned *usages)
{
    if (usages)
        *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
    return br_x509_decoder_get_pkey(&((nov_t *)c)->dec);
}
static const br_x509_class NOV = {sizeof(nov_t), nov_start_chain, nov_start_cert, nov_append, nov_end_cert,
                                  nov_end_chain, nov_get_pkey};

static int sock_rd(void *ctx, unsigned char *b, size_t n)
{
    int r = recv(*(int *)ctx, b, n, 0);
    return r > 0 ? r : -1;
}
static int sock_wr(void *ctx, const unsigned char *b, size_t n)
{
    int r = send(*(int *)ctx, b, n, 0);
    return r > 0 ? r : -1;
}

static const uint16_t SUITES[] = {
    BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256, BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
    BR_TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384, BR_TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384};

static int tls_open(conn_t *c, const char *host)
{
    br_ssl_engine_context *e;
    uint8_t seed[32];
    c->tls = malloc(sizeof *c->tls);
    c->buf = malloc(BR_SSL_BUFSIZE_MONO);
    if (!c->tls || !c->buf) {
        free(c->tls);
        free(c->buf);
        c->tls = 0;
        c->buf = 0;
        return -1;
    }
    e = &c->tls->cc.eng;
    br_ssl_client_zero(&c->tls->cc);
    br_ssl_engine_set_versions(e, BR_TLS12, BR_TLS12);
    br_ssl_engine_set_suites(e, SUITES, sizeof SUITES / sizeof SUITES[0]);
    br_ssl_engine_set_rsavrfy(e, &br_rsa_i31_pkcs1_vrfy);
    br_ssl_engine_set_ec(e, &br_ec_p256_m31);
    br_ssl_engine_set_ecdsa(e, &br_ecdsa_i31_vrfy_asn1);
    br_ssl_engine_set_hash(e, br_sha1_ID, &br_sha1_vtable);
    br_ssl_engine_set_hash(e, br_sha256_ID, &br_sha256_vtable);
    br_ssl_engine_set_hash(e, br_sha384_ID, &br_sha384_vtable);
    br_ssl_engine_set_prf_sha256(e, &br_tls12_sha256_prf);
    br_ssl_engine_set_prf_sha384(e, &br_tls12_sha384_prf);
    br_ssl_engine_set_gcm(e, &br_sslrec_in_gcm_vtable, &br_sslrec_out_gcm_vtable);
    br_ssl_engine_set_aes_ctr(e, &br_aes_small_ctr_vtable);
    br_ssl_engine_set_ghash(e, &br_ghash_ctmul32);
    c->tls->x509.vtable = &NOV;
    br_ssl_engine_set_x509(e, &c->tls->x509.vtable);
    br_ssl_engine_set_buffer(e, c->buf, BR_SSL_BUFSIZE_MONO, 0);
    cosmos_entropy(seed, sizeof seed);
    br_ssl_engine_inject_entropy(e, seed, sizeof seed);
    if (!br_ssl_client_reset(&c->tls->cc, host, 0)) {
        free(c->buf);
        free(c->tls);
        c->buf = 0;
        c->tls = 0;
        return -1;
    }
    br_sslio_init(&c->tls->io, e, sock_rd, &c->sock, sock_wr, &c->sock);
    return 0;
}

static void conn_close(conn_t *c)
{
    if (c->tls) {                                 /* no close_notify: the socket goes too */
        free(c->buf);
        free(c->tls);
        c->buf = 0;
        c->tls = 0;
    }
    if (c->sock >= 0)
        closesocket(c->sock);
    c->sock = -1;
}
static int conn_read(conn_t *c, void *b, unsigned n)   /* > 0 bytes, 0 the end, < 0 an error or a timeout */
{
    int r, err;
    r = br_sslio_read(&c->tls->io, b, n);
    if (r >= 0)
        return r;
    err = br_ssl_engine_last_error(&c->tls->cc.eng);
    if (err && err != BR_ERR_IO)
        snprintf(res.why, sizeof res.why, "tls error %d", err);
    return err == 0 ? 0 : -1;
}

static int ieq_prefix(const char *a, const char *p)    /* a starts with p, ignoring case */
{
    for (; *p; a++, p++) {
        char x = *a, y = *p;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y)
            return 0;
    }
    return 1;
}

/* GET https://host/path into body (max bytes; when it fills, the start goes and the last half stays: what COSMOS
 * reads is near the end). The body's length, or -1 (res.why says why). */
static int https_get(const char *host, const char *path, char *body, int max)
{
    conn_t c = {-1, 0, 0};
    char req[256], line[160];
    struct hostent *he;
    struct sockaddr_in a;
    int n, status = 0, len = 0;
    while (dns_busy)
        os_time_dly(1);
    dns_busy = 1;
    he = gethostbyname(host);
    memset(&a, 0, sizeof a);
    if (he && he->h_addr_list && he->h_addr_list[0])
        memcpy(&a.sin_addr, he->h_addr_list[0], 4);
    dns_busy = 0;
    if (!a.sin_addr.s_addr) {
        why_to("dns failed");
        return -1;
    }
    c.sock = socket(AF_INET, SOCK_STREAM, 0);
    if (c.sock < 0) {
        why_to("no socket");
        return -1;
    }
    a.sin_len = sizeof a;
    a.sin_family = AF_INET;
    a.sin_port = htons(443);
    if (connect(c.sock, (struct sockaddr *)&a, sizeof a) < 0) {
        conn_close(&c);
        why_to("connect failed");
        return -1;
    }
    {   /* a silent server ends the recv after 8 s (lwIP's NONSTANDARD option: an int, ms) */
        int ms = 8000;
        setsockopt(c.sock, SOL_SOCKET, SO_RCVTIMEO, &ms, sizeof ms);
    }
    if (tls_open(&c, host)) {
        conn_close(&c);
        why_to("tls: no memory");
        return -1;
    }
    n = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: COSMOS-FM1\r\n"
                 "Accept: application/json\r\nConnection: close\r\n\r\n", path, host);
    if (br_sslio_write_all(&c.tls->io, req, (size_t)n) < 0 || br_sslio_flush(&c.tls->io) < 0) {
        snprintf(res.why, sizeof res.why, "tls error %d", br_ssl_engine_last_error(&c.tls->cc.eng));
        conn_close(&c);
        return -1;
    }
    for (;;) {                                    /* the headers, a line at a time */
        unsigned k = 0;
        char ch = 0;
        while (k + 1u < sizeof line) {
            if (conn_read(&c, &ch, 1) != 1) {
                conn_close(&c);
                if (!res.why[0])
                    why_to("closed in headers");
                return -1;
            }
            if (ch == '\n')
                break;
            if (ch != '\r')
                line[k++] = ch;
        }
        if (ch != '\n')                           /* too long: the rest of the line goes */
            while (conn_read(&c, &ch, 1) == 1 && ch != '\n')
                ;
        line[k] = 0;
        if (!status) {                            /* "HTTP/1.x 200 OK" */
            const char *p = line;
            while (*p && *p != ' ')
                p++;
            while (*p == ' ')
                p++;
            while (*p >= '0' && *p <= '9')
                status = status * 10 + (*p++ - '0');
            continue;
        }
        if (!k)
            break;                                /* the blank line: the body follows */
        if (ieq_prefix(line, "date:")) {
            const char *v = line + 5;
            unsigned i;
            while (*v == ' ')
                v++;
            for (i = 0; i + 1u < sizeof res.date && v[i]; i++)
                res.date[i] = v[i];
            res.date[i] = 0;
        }
    }
    if (status != 200) {
        conn_close(&c);
        snprintf(res.why, sizeof res.why, "http status %d", status);
        return -1;
    }
    for (;;) {
        if (len == max) {                         /* full: keep the last half */
            memmove(body, body + max / 2, (size_t)(max - max / 2));
            len = max - max / 2;
        }
        n = conn_read(&c, body + len, (unsigned)(max - len));
        if (n <= 0)
            break;
        len += n;
    }
    conn_close(&c);
    return n < 0 && !len ? -1 : len;
}

/* ---- the numbers: "key":12.34 after from (or anywhere), the last one when last */
static const char *find(const char *s, int n, const char *w, int last)
{
    int i, k, wl = (int)strlen(w);
    const char *hit = 0;
    for (i = 0; i + wl <= n; i++) {
        for (k = 0; k < wl && s[i + k] == w[k]; k++)
            ;
        if (k == wl) {
            hit = s + i + wl;
            if (!last)
                return hit;
        }
    }
    return hit;
}
static int32_t num100(const char *p)              /* "1012.3" -> 101230; -1 none */
{
    int32_t v = 0, neg = 0, d = 0;
    if (!p)
        return -1;
    while (*p == ' ' || *p == '"')
        p++;
    if (*p == '-')
        neg = 1, p++;
    if (*p < '0' || *p > '9')
        return -1;
    while (*p >= '0' && *p <= '9')
        v = v * 10 + (*p++ - '0');
    v *= 100;
    if (*p == '.')
        for (p++; *p >= '0' && *p <= '9'; p++)
            if (d < 2)
                v += (*p - '0') * (d++ ? 1 : 10);
    return neg ? -v : v;
}

/* the last number of a JSON array that p starts in ("[1013.1,1012.8,1012.5]"), as num100; -1 none */
static int32_t last100(const char *p, const char *end)
{
    const char *q = 0;
    if (!p)
        return -1;
    for (; p < end && *p != ']'; p++)
        if (*p == ',')
            q = p + 1;
    return q ? num100(q) : -1;
}
/* "41.90", "-3.70": hundredths as a decimal */
static char *dec2(char *d, int32_t v)
{
    if (v < 0) {
        *d++ = '-';
        v = -v;
    }
    d += snprintf(d, 12, "%d.%02d", (int)(v / 100), (int)(v % 100));
    return d;
}

#define BODY_MAX 6144

static void fetch_task(void *arg)
{
    char *body = malloc(BODY_MAX + 1), path[200], *q;
    int n;
    (void)arg;
    if (!body) {
        why_to("no memory for the body");
        busy = 0;
        return;
    }
    /* the weather at the player's place: cloud cover %, pressure at sea level hPa now and over the last 3 hours,
     * the place's offset from UTC (summer time with it) */
    q = path + snprintf(path, sizeof path, "/v1/forecast?latitude=");
    q = dec2(q, at_lat100);
    q += snprintf(q, 12, "&longitude=");
    q = dec2(q, at_lon100);
    snprintf(q, (unsigned long)(path + sizeof path - q),
             "&current=cloud_cover,pressure_msl,weather_code&hourly=pressure_msl&past_hours=3&forecast_hours=1"
             "&timezone=auto");
    n = https_get("api.open-meteo.com", path, body, BODY_MAX);
    if (n > 0) {
        const char *cur, *hr, *off;
        int rest;
        body[n] = 0;
        off = find(body, n, "\"utc_offset_seconds\":", 0);
        if (off) {
            int32_t v = num100(off[0] == '-' ? off + 1 : off);
            if (v >= 0)
                res.utc_s = (off[0] == '-' ? -v : v) / 100;
        }
        cur = find(body, n, "\"current\":{", 0);  /* not "current_units": the numbers, not their units */
        rest = cur ? n - (int)(cur - body) : 0;
        if (cur) {
            int32_t p = num100(find(cur, rest, "\"pressure_msl\":", 0));
            res.cloud = num100(find(cur, rest, "\"cloud_cover\":", 0));
            if (res.cloud >= 0)
                res.cloud /= 100;
            res.press10 = p >= 0 ? p / 10 : -1;
            res.wcode = num100(find(cur, rest, "\"weather_code\":", 0));
            if (res.wcode >= 0)
                res.wcode /= 100;
            res.done++;
        } else {
            why_to("open-meteo: no data");
        }
        hr = find(body, n, "\"hourly\":{", 0);
        if (hr) {                                 /* the pressure 3 hours ago and now: its trend */
            const char *a = find(hr, n - (int)(hr - body), "\"pressure_msl\":[", 0);
            int32_t first = a ? num100(a) : -1, last = last100(a, body + n);
            if (first >= 0 && last >= 0)
                res.trend10 = (last - first) / 10;
        }
    }
    /* the planetary Kp index, every 3 hours over the last week: the last one */
    n = https_get("services.swpc.noaa.gov", "/products/noaa-planetary-k-index.json", body, BODY_MAX);
    if (n > 0) {
        res.kp100 = num100(find(body, n, "\"Kp\":", 1));
        if (res.kp100 >= 0)
            res.done++;
        else
            why_to("noaa: no kp");
    }
    free(body);
    busy = 0;
}

int net_fetch(int32_t lat100, int32_t lon100)
{
    if (busy)
        return -1;
    memset(&res, 0, sizeof res);
    res.cloud = res.press10 = res.kp100 = res.wcode = -1;
    res.trend10 = 0;
    res.utc_s = -999999;                          /* app.h NET_NONE_S: none */
    at_lat100 = lat100;
    at_lon100 = lon100;
    busy = 1;
    if (plat_net_task(fetch_task, 0, "fetch")) {
        busy = 0;
        why_to("no task");
        return -1;
    }
    return 0;
}
int net_busy(void) { return busy; }
const net_res_t *net_result(void) { return &res; }
