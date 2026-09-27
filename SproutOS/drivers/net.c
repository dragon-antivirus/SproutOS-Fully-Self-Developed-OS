#include "kernel.h"
#include "net.h"

/* ------------------------------------------------------------------ */
/* Global state                                                        */
/* ------------------------------------------------------------------ */

static netdev_t* g_dev = 0;
static uint8_t   g_mac[6] = {0};
/* QEMU user-mode networking assigns the guest 10.0.2.15 by default.   */
static uint32_t  g_ip = 0x0A00020F;
static int       g_ping_seq = 0;
static volatile int  g_ping_reply = 0;
static uint32_t  g_ping_reply_ip = 0;

/* Default gateway + netmask. QEMU user-mode networking uses gateway 10.0.2.2
   on a /24. On VMware NAT (or a real LAN) these differ — override at runtime
   with `net gw <ip>` / `net mask <ip>`. Routing MUST go through the gateway
   for any off-subnet destination, otherwise ARP fails and packets never send. */
static uint32_t g_gateway = 0x0A000202;   /* 10.0.2.2 */
static uint32_t g_netmask = 0xFFFFFF00;   /* 255.255.255.0 */

/* Small ARP cache (no expiration; enough for a demo). */
#define ARP_MAX 8
static uint32_t g_arp_ip[ARP_MAX];
static uint8_t  g_arp_mac[ARP_MAX][6];
static int      g_arp_n = 0;

/* ------------------------------------------------------------------ */
/* Tiny formatting helpers (avoid libc)                                */
/* ------------------------------------------------------------------ */

static void hex2(uint8_t v, char* o) {
    static const char* hx = "0123456789abcdef";
    o[0] = hx[(v >> 4) & 0xF];
    o[1] = hx[v & 0xF];
    o[2] = 0;
}

static void itoa3(uint32_t v, char* o) {
    /* v is 0..255; render decimal without leading zeros. */
    char t[4];
    int n = 0;
    if (v == 0) t[n++] = '0';
    while (v > 0 && n < 3) { t[n++] = (char)('0' + (v % 10)); v /= 10; }
    for (int i = 0; i < n; i++) o[i] = t[n - 1 - i];
    o[n] = 0;
}

/* Prefix match: p begins with kw and is followed by space/tab/EOL. */
static int cisp(const char* p, const char* kw) {
    while (*kw) { if (*p != *kw) return 0; p++; kw++; }
    return (*p == ' ' || *p == '\t' || *p == 0);
}

static int starts_with(const char* p, const char* kw) {
    while (*kw) { if (*p != *kw) return 0; p++; kw++; }
    return 1;
}

static void term_put_ip(uint32_t ip) {
    char d[4], buf[18];
    int n = 0;
    itoa3((ip >> 24) & 0xFF, d); for (int i = 0; d[i]; i++) buf[n++] = d[i]; buf[n++] = '.';
    itoa3((ip >> 16) & 0xFF, d); for (int i = 0; d[i]; i++) buf[n++] = d[i]; buf[n++] = '.';
    itoa3((ip >> 8)  & 0xFF, d); for (int i = 0; d[i]; i++) buf[n++] = d[i]; buf[n++] = '.';
    itoa3(ip & 0xFF,        d); for (int i = 0; d[i]; i++) buf[n++] = d[i];
    buf[n] = 0;
    term_puts(buf);
}

/* Forward declaration (defined below) so net_rx can use it. */
static void serial_put_ip(uint32_t ip);
static void net_rx_udp(const uint8_t* udp, uint32_t udp_len,
                       uint32_t src_ip, uint32_t dst_ip);
static void net_rx_tcp(const uint8_t* tcp, uint32_t tcp_len,
                       uint32_t src_ip);

/* Parse "a.b.c.d" into a uint32_t (wire order: a is most significant). */
static int parse_ip(const char* s, uint32_t* out) {
    uint32_t a[4];
    int n = 0;
    uint32_t cur = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            cur = cur * 10 + (uint32_t)(s[i] - '0');
        } else if (s[i] == '.') {
            if (n > 3) return -1;
            a[n++] = cur; cur = 0;
        } else if (s[i] == ' ' || s[i] == '\t') {
            break;
        } else {
            return -1;
        }
    }
    if (n > 3) return -1;
    a[n++] = cur;
    if (n != 4) return -1;
    *out = (a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3];
    return 0;
}

/* ------------------------------------------------------------------ */
/* Device registration / accessors                                     */
/* ------------------------------------------------------------------ */

void net_register(netdev_t* dev) {
    g_dev = dev;
    memcpy(g_mac, dev->mac, 6);
    serial_write("[NET] device up: ");
    serial_write(dev->name);
    serial_write(" mac=");
    for (int i = 0; i < 6; i++) {
        char b[3]; hex2(dev->mac[i], b);
        serial_write(b);
        if (i < 5) serial_write(":");
    }
    serial_write(" irq=");
    serial_puti((int)dev->irq);
    serial_write("\n");
}

int net_up(void) { return g_dev != 0; }
const uint8_t* net_mac(void) { return g_mac; }
const char* net_driver_name(void) { return g_dev ? g_dev->name : "none"; }

void net_poll(void) {
    if (g_dev && g_dev->poll) g_dev->poll(g_dev);
}

/* ------------------------------------------------------------------ */
/* ARP cache                                                           */
/* ------------------------------------------------------------------ */

static void arp_cache_add(uint32_t ip, const uint8_t* mac) {
    for (int i = 0; i < g_arp_n; i++)
        if (g_arp_ip[i] == ip) { memcpy(g_arp_mac[i], mac, 6); return; }
    if (g_arp_n < ARP_MAX) {
        g_arp_ip[g_arp_n] = ip;
        memcpy(g_arp_mac[g_arp_n], mac, 6);
        g_arp_n++;
    }
}

static int arp_cache_lookup(uint32_t ip, uint8_t* mac) {
    for (int i = 0; i < g_arp_n; i++)
        if (g_arp_ip[i] == ip) { memcpy(mac, g_arp_mac[i], 6); return 0; }
    return -1;
}

/* ------------------------------------------------------------------ */
/* RX dispatch (ARP / IPv4+ICMP)                                       */
/* ------------------------------------------------------------------ */

void net_rx(const uint8_t* frame, uint32_t len) {
    if (len < 14) return;
    uint16_t et = ((uint16_t)frame[12] << 8) | frame[13];

    if (et == ETH_TYPE_ARP) {
        if (len < 14 + 28) return;
        const uint8_t* a = frame + 14;       /* ARP packet starts here */
        uint16_t op = ((uint16_t)a[6] << 8) | a[7];
        if (op != 2) return;                 /* only handle replies */
        /* layout: HTYPE(2) PTYPE(2) HLEN(1) PLEN(1) OPER(2)
           SENDER_MAC(6)@8  SENDER_IP(4)@14  TARGET_MAC(6)  TARGET_IP(4) */
        uint32_t sip = ((uint32_t)a[14] << 24) | ((uint32_t)a[15] << 16) |
                       ((uint32_t)a[16] << 8) | a[17];
        arp_cache_add(sip, a + 8);
        serial_write("[NET] arp reply ");
        serial_put_ip(sip);
        serial_write(" -> ");
        for (int i = 0; i < 6; i++) {
            char b[3]; hex2(a[8 + i], b); serial_write(b);
            if (i < 5) serial_write(":");
        }
        serial_write("\n");
    } else if (et == ETH_TYPE_IP) {
        if (len < 14 + 20) return;
        const uint8_t* ip = frame + 14;
        uint8_t ihl = (uint8_t)((ip[0] & 0x0F) * 4);
        if (ihl < 20) return;
        uint8_t proto = ip[9];
        uint32_t src = ((uint32_t)ip[12] << 24) | ((uint32_t)ip[13] << 16) |
                       ((uint32_t)ip[14] << 8) | ip[15];
        if (proto == IP_PROTO_ICMP) {
            const uint8_t* icmp = ip + ihl;
            if (icmp[0] == 0) {              /* echo reply */
                g_ping_reply = 1;
                g_ping_reply_ip = src;
                serial_write("[NET] icmp echo reply from ");
                serial_put_ip(src);
                serial_write("\n");
            }
        } else if (proto == IP_PROTO_UDP) {
            net_rx_udp(ip + ihl, len - 14 - (uint32_t)ihl, src,
                       ((uint32_t)ip[12] << 24) | ((uint32_t)ip[13] << 16) |
                       ((uint32_t)ip[14] << 8) | ip[15]);
        } else if (proto == IP_PROTO_TCP) {
            /* 关键修复：必须用 IP 头的 total length 推算 TCP 段长（权威值，
             * 不含以太网 CRC/尾部），不能用以太网帧长度 len-14-ihl。
             * 否则网卡若把 4 字节 CRC 计入 len，纯 ACK 包（含 TCP options）
             * 会被算成 payload_len=6，在 ESTABLISHED 里因 seq==rcv_nxt 被误当
             * 数据消费，错误推进 rcv_nxt，导致后续真实数据段 seq 不匹配被丢弃
             * （本项目 HTTP 响应长期 6 字节、多段传输卡死的根因）。 */
            uint16_t ip_total = (uint16_t)(((uint16_t)ip[2] << 8) | ip[3]);
            uint32_t tcp_len = (ip_total > ihl) ? (uint32_t)(ip_total - ihl) : 0;
            if (tcp_len >= 20) net_rx_tcp(ip + ihl, tcp_len, src);
        }
    }
}

static void serial_put_ip(uint32_t ip) {
    char d[4];
    itoa3((ip >> 24) & 0xFF, d); serial_write(d); serial_write(".");
    itoa3((ip >> 16) & 0xFF, d); serial_write(d); serial_write(".");
    itoa3((ip >> 8)  & 0xFF, d); serial_write(d); serial_write(".");
    itoa3(ip & 0xFF,        d); serial_write(d);
}

/* ------------------------------------------------------------------ */
/* ARP resolve (blocking)                                              */
/* ------------------------------------------------------------------ */

int net_arp_resolve(uint32_t ip, uint8_t* out_mac) {
    if (!g_dev) return -1;
    if (arp_cache_lookup(ip, out_mac) == 0) { serial_write("[ARP] cache hit\n"); return 0; }

    serial_write("[ARP] query ");
    serial_put_ip(ip);

    /* Build an ARP request (28-byte body after the ethernet header). */
    static uint8_t req[28];
    req[0] = 0x00; req[1] = 0x01;   /* htype = ethernet */
    req[2] = 0x08; req[3] = 0x00;   /* ptype = IPv4 */
    req[4] = 0x06;                  /* hlen */
    req[5] = 0x04;                  /* plen */
    req[6] = 0x00; req[7] = 0x01;   /* oper = request */
    memcpy(req + 8, g_mac, 6);      /* sender mac */
    req[14] = (uint8_t)((g_ip >> 24) & 0xFF);
    req[15] = (uint8_t)((g_ip >> 16) & 0xFF);
    req[16] = (uint8_t)((g_ip >> 8) & 0xFF);
    req[17] = (uint8_t)(g_ip & 0xFF);
    memset(req + 18, 0, 6);         /* target mac (unknown) */
    req[24] = (uint8_t)((ip >> 24) & 0xFF);
    req[25] = (uint8_t)((ip >> 16) & 0xFF);
    req[26] = (uint8_t)((ip >> 8) & 0xFF);
    req[27] = (uint8_t)(ip & 0xFF);

    static uint8_t bc[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    g_dev->send(g_dev, bc, ETH_TYPE_ARP, req, 28);
    serial_write(" sent\n");

    for (int t = 0; t < 100; t++) {
        net_poll();
        if (arp_cache_lookup(ip, out_mac) == 0) {
            serial_write("[ARP] ok\n");
            return 0;
        }
        udelay(10000);             /* ~10 ms */
    }
    serial_write("[ARP] timeout\n");
    return -1;
}

/* Resolve the MAC of the *next hop* for dst_ip. If dst is on the local
   subnet we ARP it directly; otherwise we ARP the default gateway. The frame
   is always sent to the next-hop's MAC, while the IP header keeps dst_ip.
   This is what makes off-subnet traffic (i.e. the real Internet) work. */
static int net_route_mac(uint32_t dst_ip, uint8_t* mac) {
    uint32_t next_hop = ((dst_ip & g_netmask) == (g_ip & g_netmask))
                        ? dst_ip : g_gateway;
    return net_arp_resolve(next_hop, mac);
}

/* ------------------------------------------------------------------ */
/* IPv4 send                                                           */
/* ------------------------------------------------------------------ */

int net_send_ip(uint32_t dst_ip, uint8_t proto, const uint8_t* data, uint32_t len) {
    if (!g_dev) return -1;
    uint8_t dst_mac[6];
    if (net_route_mac(dst_ip, dst_mac) != 0) return -1;

    static uint8_t pkt[14 + 60 + 1500];   /* eth + IP header + payload */
    memcpy(pkt, dst_mac, 6);
    memcpy(pkt + 6, g_mac, 6);
    pkt[12] = (uint8_t)((ETH_TYPE_IP >> 8) & 0xFF);
    pkt[13] = (uint8_t)(ETH_TYPE_IP & 0xFF);

    uint8_t* ip = pkt + 14;
    ip[0] = 0x45;                  /* version 4, IHL 5 */
    ip[1] = 0;                     /* DSCP/ECN */
    uint32_t total = 20 + len;
    ip[2] = (uint8_t)((total >> 8) & 0xFF);
    ip[3] = (uint8_t)(total & 0xFF);
    static uint16_t g_id = 0;
    g_id++;
    ip[4] = (uint8_t)((g_id >> 8) & 0xFF);
    ip[5] = (uint8_t)(g_id & 0xFF);
    ip[6] = 0; ip[7] = 0;          /* flags/fragment */
    ip[8] = 64;                    /* TTL */
    ip[9] = proto;
    ip[10] = 0; ip[11] = 0;        /* checksum (fill below) */
    ip[12] = (uint8_t)((g_ip >> 24) & 0xFF);
    ip[13] = (uint8_t)((g_ip >> 16) & 0xFF);
    ip[14] = (uint8_t)((g_ip >> 8) & 0xFF);
    ip[15] = (uint8_t)(g_ip & 0xFF);
    ip[16] = (uint8_t)((dst_ip >> 24) & 0xFF);
    ip[17] = (uint8_t)((dst_ip >> 16) & 0xFF);
    ip[18] = (uint8_t)((dst_ip >> 8) & 0xFF);
    ip[19] = (uint8_t)(dst_ip & 0xFF);
    memcpy(ip + 20, data, len);

    /* IPv4 header checksum (ones' complement over the 20-byte header). */
    uint32_t sum = 0;
    for (int i = 0; i < 10; i++) {
        uint16_t w = (uint16_t)(((uint16_t)ip[i * 2] << 8) | ip[i * 2 + 1]);
        sum += w;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    uint16_t ck = (uint16_t)(~sum);
    ip[10] = (uint8_t)((ck >> 8) & 0xFF);
    ip[11] = (uint8_t)(ck & 0xFF);

    return g_dev->send(g_dev, dst_mac, ETH_TYPE_IP, ip, total);
}

/* ------------------------------------------------------------------ */
/* Terminal commands                                                   */
/* ------------------------------------------------------------------ */

void net_cmd(const char* arg) {
    while (*arg == ' ' || *arg == '\t') arg++;
    if (arg[0] == 0 || cisp(arg, "status") || cisp(arg, "info")) {
        if (!net_up()) { term_puts("net: no device detected\n"); return; }
        term_puts("net: driver="); term_puts(net_driver_name()); term_puts("\n");
        term_puts("     mac=");
        char b[3];
        for (int i = 0; i < 6; i++) { hex2(net_mac()[i], b); term_puts(b); if (i < 5) term_puts(":"); }
        term_puts("\n     ip=");
        term_put_ip(g_ip);
        term_puts("\n     mask=");
        term_put_ip(g_netmask);
        term_puts("\n     gw =");
        term_put_ip(g_gateway);
        term_puts("\n");
        return;
    }
    if (cisp(arg, "gw")) {
        const char* a = arg + 2;
        while (*a == ' ' || *a == '\t') a++;
        uint32_t ip;
        if (parse_ip(a, &ip) == 0) {
            g_gateway = ip;
            term_puts("net: gateway set to "); term_put_ip(g_gateway); term_puts("\n");
        } else {
            term_puts("net: bad ip address\n");
        }
        return;
    }
    if (cisp(arg, "mask")) {
        const char* a = arg + 4;
        while (*a == ' ' || *a == '\t') a++;
        uint32_t ip;
        if (parse_ip(a, &ip) == 0) {
            g_netmask = ip;
            term_puts("net: netmask set to "); term_put_ip(g_netmask); term_puts("\n");
        } else {
            term_puts("net: bad ip address\n");
        }
        return;
    }
    if (cisp(arg, "ip")) {
        const char* a = arg + 2;
        while (*a == ' ' || *a == '\t') a++;
        uint32_t ip;
        if (parse_ip(a, &ip) == 0) {
            g_ip = ip;
            term_puts("net: ip set to "); term_put_ip(g_ip); term_puts("\n");
        } else {
            term_puts("net: bad ip address\n");
        }
        return;
    }
    if (cisp(arg, "arp")) {
        const char* a = arg + 3;
        while (*a == ' ' || *a == '\t') a++;
        uint32_t ip;
        if (parse_ip(a, &ip) != 0) { term_puts("net: bad ip address\n"); return; }
        uint8_t mac[6];
        term_puts("net: arp resolve "); term_put_ip(ip);
        if (net_arp_resolve(ip, mac) == 0) {
            term_puts(" -> ");
            char b[3];
            for (int i = 0; i < 6; i++) { hex2(mac[i], b); term_puts(b); if (i < 5) term_puts(":"); }
            term_puts("\n");
        } else {
            term_puts(": no reply (timeout)\n");
        }
        return;
    }
    if (cisp(arg, "ping")) { net_ping(arg + 4); return; }
    term_puts("net: usage: net [status|ip <a.b.c.d>|gw <a.b.c.d>|mask <a.b.c.d>|arp <a.b.c.d>]\n");
}

void net_ping(const char* arg) {
    while (*arg == ' ' || *arg == '\t') arg++;
    uint32_t ip;
    if (parse_ip(arg, &ip) != 0) { term_puts("ping: bad ip address\n"); return; }
    if (!net_up()) { term_puts("ping: no device detected\n"); return; }

    static uint8_t icmp[64];
    memset(icmp, 0, sizeof(icmp));
    icmp[0] = 8;                          /* echo request */
    icmp[1] = 0;                          /* code */
    uint16_t id = 0x1234;
    uint16_t seq = (uint16_t)(++g_ping_seq);
    icmp[4] = (uint8_t)((id >> 8) & 0xFF);
    icmp[5] = (uint8_t)(id & 0xFF);
    icmp[6] = (uint8_t)((seq >> 8) & 0xFF);
    icmp[7] = (uint8_t)(seq & 0xFF);
    const char* msg = "sproutos";
    for (int i = 0; msg[i]; i++) icmp[8 + i] = (uint8_t)msg[i];
    uint32_t ilen = 8 + (uint32_t)strlen(msg);

    /* ICMP checksum over the message. */
    uint32_t sum = 0;
    for (uint32_t i = 0; i < ilen; i += 2) {
        uint16_t w = (uint16_t)(((uint16_t)icmp[i] << 8) | icmp[i + 1]);
        sum += w;
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    uint16_t ck = (uint16_t)(~sum);
    icmp[2] = (uint8_t)((ck >> 8) & 0xFF);
    icmp[3] = (uint8_t)(ck & 0xFF);

    g_ping_reply = 0;
    uint32_t t0 = get_tick();
    if (net_send_ip(ip, IP_PROTO_ICMP, icmp, ilen) != 0) {
        term_puts("ping: send failed (device/arp error)\n");
        return;
    }
    term_puts("PING "); term_put_ip(ip); term_puts(": 56 data bytes\n");

    int got = 0;
    for (int i = 0; i < 200; i++) {       /* ~2 s */
        net_poll();
        if (g_ping_reply) { got = 1; break; }
        udelay(10000);
    }
    uint32_t dt = get_tick() - t0;
    if (got) {
        term_puts("64 bytes from "); term_put_ip(g_ping_reply_ip);
        term_puts(": icmp_seq=");
        char s[4]; itoa3(seq, s); term_puts(s);
        term_puts(" time="); itoa3(dt * 10, s); term_puts(s); term_puts("ms\n");
    } else {
        term_puts("ping: timeout (no reply)\n");
        term_puts("  note: QEMU -netdev user does NOT forward ICMP.\n");
        term_puts("  use -netdev tap, or a second VM as peer, to get replies.\n");
    }
}

/* ------------------------------------------------------------------ */
/* UDP send                                                             */
/* ------------------------------------------------------------------ */

int net_udp_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
                 const uint8_t* data, uint32_t len) {
    if (!g_dev) return -1;
    uint8_t pkt[8 + 1500];
    uint16_t ulen = 8 + (uint16_t)len;
    pkt[0] = (uint8_t)((src_port >> 8) & 0xFF);
    pkt[1] = (uint8_t)(src_port & 0xFF);
    pkt[2] = (uint8_t)((dst_port >> 8) & 0xFF);
    pkt[3] = (uint8_t)(dst_port & 0xFF);
    pkt[4] = (uint8_t)((ulen >> 8) & 0xFF);
    pkt[5] = (uint8_t)(ulen & 0xFF);
    pkt[6] = 0;
    pkt[7] = 0;
    if (len > 0) memcpy(pkt + 8, data, len);
    return net_send_ip(dst_ip, IP_PROTO_UDP, pkt, ulen);
}

/* ------------------------------------------------------------------ */
/* DNS client                                                           */
/* ------------------------------------------------------------------ */

#define DNS_PORT      53
#define DNS_TIMEOUT_MS 5000

static uint32_t g_dns_server = 0x0A000203; /* default: QEMU slirp 10.0.2.3 */
static uint8_t  dns_rx_buf[512];
static int      dns_rx_len = 0;
static volatile int dns_rx_ready = 0;
static int dns_resolve_one(const char* hostname, uint32_t* out_ip);

static void net_rx_udp(const uint8_t* udp, uint32_t udp_len,
                       uint32_t src_ip, uint32_t dst_ip) {
    if (udp_len < 8) return;
    uint16_t dst_port = ((uint16_t)udp[2] << 8) | udp[3];
    (void)dst_ip;
    (void)src_ip;
    if (dst_port != 12345) return;
    dns_rx_len = (int)(udp_len - 8);
    if (dns_rx_len > (int)sizeof(dns_rx_buf)) dns_rx_len = (int)sizeof(dns_rx_buf);
    memcpy(dns_rx_buf, udp + 8, (uint32_t)dns_rx_len);
    dns_rx_ready = 1;
}

int dns_resolve(const char* hostname, uint32_t* out_ip) {
    if (!g_dev || !hostname || !*hostname) return -1;

    /* Try up to 2 DNS servers: QEMU slirp (10.0.2.3) then Google (8.8.8.8) */
    uint32_t servers[2] = { 0x0A000203, 0x08080808 };
    int ns;
    for (ns = 0; ns < 2; ns++) {
        g_dns_server = servers[ns];
        int ret = dns_resolve_one(hostname, out_ip);
        if (ret == 0) return 0;
        /* If first server failed, try next */
        if (ns == 0) serial_write("[DNS] fallback to 8.8.8.8...\n");
    }
    return -1;
}

static int dns_resolve_one(const char* hostname, uint32_t* out_ip) {
    serial_write("[DNS] resolve: ");
    serial_write(hostname);

    uint8_t qbuf[256];
    int qlen = 0;

    qbuf[qlen++] = 0x12; qbuf[qlen++] = 0x34;
    qbuf[qlen++] = 0x01; qbuf[qlen++] = 0x00;
    qbuf[qlen++] = 0x00; qbuf[qlen++] = 0x01;
    qbuf[qlen++] = 0x00; qbuf[qlen++] = 0x00;
    qbuf[qlen++] = 0x00; qbuf[qlen++] = 0x00;
    qbuf[qlen++] = 0x00; qbuf[qlen++] = 0x00;

    const char* p = hostname;
    while (*p) {
        const char* dot = p;
        while (*dot && *dot != '.') dot++;
        int label_len = (int)(dot - p);
        if (label_len > 63 || label_len == 0) return -1;
        qbuf[qlen++] = (uint8_t)label_len;
        int i;
        for (i = 0; i < label_len; i++) qbuf[qlen++] = (uint8_t)p[i];
        if (*dot == '.') p = dot + 1;
        else p = dot;
    }
    qbuf[qlen++] = 0x00;
    qbuf[qlen++] = 0x00; qbuf[qlen++] = 0x01;
    qbuf[qlen++] = 0x00; qbuf[qlen++] = 0x01;

    dns_rx_ready = 0;
    if (net_udp_send(g_dns_server, DNS_PORT, 12345, qbuf, (uint32_t)qlen) != 0) {
        serial_write(" -> send failed\n");
        return -1;
    }

    int i;
    for (i = 0; i < DNS_TIMEOUT_MS / 10; i++) {
        net_poll();
        if (dns_rx_ready) break;
        udelay(10000);
    }
    if (!dns_rx_ready) {
        serial_write(" -> timeout\n");
        return -1;
    }
    dns_rx_ready = 0;

    if (dns_rx_len < 12) { serial_write(" -> response too short\n"); return -1; }

    /* Diagnostic: dump first 20 bytes of response */
    serial_write(" -> rx[");
    int di;
    for (di = 0; di < 20 && di < dns_rx_len; di++) {
        char hb[3]; hex2(dns_rx_buf[di], hb); serial_write(hb);
    }
    serial_write("]");

    uint8_t* r = dns_rx_buf;
    int qr = (r[2] >> 7) & 1;
    int rcode = r[3] & 0xF;
    int ancount = ((int)r[6] << 8) | r[7];

    if (!qr) { serial_write(" not a response\n"); return -1; }
    if (rcode != 0) {
        serial_write(" rcode=");
        char c[4]; itoa3(rcode, c); serial_write(c);
        serial_write("\n");
        return -1;
    }
    if (ancount == 0) { serial_write(" no answers\n"); return -1; }

    int pos = 12;
    int qdcount = ((int)r[4] << 8) | r[5];
    for (i = 0; i < qdcount && pos < dns_rx_len; i++) {
        while (pos < dns_rx_len && r[pos] != 0) {
            int lbl = r[pos];
            if (lbl >= 0xC0) { pos += 2; break; }
            pos += 1 + lbl;
            if (pos >= dns_rx_len) break;
        }
        if (pos < dns_rx_len) pos += 5;
    }

    for (i = 0; i < ancount && pos < dns_rx_len; i++) {
        if (pos + 16 > dns_rx_len) break;

        if ((r[pos] & 0xC0) == 0xC0) pos += 2;
        else {
            while (pos < dns_rx_len && r[pos] != 0) {
                int lbl = r[pos];
                if (lbl >= 0xC0) { pos += 2; break; }
                pos += 1 + lbl;
                if (pos >= dns_rx_len) break;
            }
            if (pos < dns_rx_len) pos++;
        }

        int atype = ((int)r[pos] << 8) | r[pos + 1]; pos += 2;
        int aclass = ((int)r[pos] << 8) | r[pos + 1]; pos += 2;
        uint32_t ttl_val = ((uint32_t)r[pos] << 24) | ((uint32_t)r[pos + 1] << 16) |
                           ((uint32_t)r[pos + 2] << 8) | r[pos + 3]; pos += 4;
        (void)ttl_val;
        int rdlen = ((int)r[pos] << 8) | r[pos + 1]; pos += 2;

        if (aclass == 1 && atype == 1 && rdlen == 4 && pos + 4 <= dns_rx_len) {
            *out_ip = ((uint32_t)r[pos] << 24) | ((uint32_t)r[pos + 1] << 16) |
                      ((uint32_t)r[pos + 2] << 8) | r[pos + 3];
            serial_write(" -> ");
            serial_put_ip(*out_ip);
            serial_write("\n");
            return 0;
        }
        pos += rdlen;
    }

    serial_write(" no A record\n");
    return -1;
}

/* ------------------------------------------------------------------ */
/* Minimal TCP (single blocking connection)                             */
/* ------------------------------------------------------------------ */

enum tcp_state { TCP_CLOSED, TCP_SYN_SENT, TCP_ESTABLISHED,
                 TCP_FIN_WAIT1, TCP_FIN_WAIT2 };

static enum tcp_state g_tcp_state = TCP_CLOSED;
static uint32_t       g_tcp_remote_ip = 0;
static uint16_t       g_tcp_remote_port = 0;
static uint16_t       g_tcp_local_port = 0;
static uint32_t       g_tcp_snd_nxt = 0;
static uint32_t       g_tcp_snd_una = 0;
static uint32_t       g_tcp_rcv_nxt = 0;
static uint16_t       g_tcp_iss = 0;
static uint32_t       g_tcp_irss = 0;

#define TCP_RX_BUF_SIZE (512*1024)
static uint8_t   g_tcp_rx_buf[TCP_RX_BUF_SIZE];
static uint32_t  g_tcp_rx_head = 0;
static uint32_t  g_tcp_rx_tail = 0;
static volatile int g_tcp_got_data = 0;
static volatile int g_tcp_got_fin = 0;
static volatile int g_tcp_got_rst = 0;    /* 收到 RST 标志 */
/* 诊断：TCP 数据段接收计数 */
static volatile uint32_t g_tcp_rx_segs = 0;
static volatile uint32_t g_tcp_rx_bytes = 0;
static volatile int g_tcp_got_synack = 0;
static volatile int g_tcp_got_finack = 0;

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                             const uint8_t* tcp_hdr, uint32_t tcp_len) {
    uint32_t sum = 0;
    sum += (uint16_t)(src_ip >> 16); sum += (uint16_t)(src_ip & 0xFFFF);
    sum += (uint16_t)(dst_ip >> 16); sum += (uint16_t)(dst_ip & 0xFFFF);
    sum += 6;
    sum += (uint16_t)(tcp_len >> 16); sum += (uint16_t)(tcp_len & 0xFFFF);
    uint32_t i;
    for (i = 0; i + 1 < tcp_len; i += 2) {
        sum += (uint16_t)(((uint16_t)tcp_hdr[i] << 8) | tcp_hdr[i + 1]);
    }
    if (tcp_len % 2) sum += (uint16_t)(tcp_hdr[tcp_len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

static int tcp_tx(uint32_t seq, uint32_t ack, uint8_t flags,
                  const uint8_t* data, uint32_t dlen) {
    if (!g_dev) return -1;
    uint8_t pkt[20 + 1480];
    uint32_t hdr_len = 20;
    uint32_t total = hdr_len + dlen;
    memset(pkt, 0, 20);

    pkt[0]  = (g_tcp_local_port >> 8) & 0xFF;
    pkt[1]  = g_tcp_local_port & 0xFF;
    pkt[2]  = (g_tcp_remote_port >> 8) & 0xFF;
    pkt[3]  = g_tcp_remote_port & 0xFF;
    pkt[4]  = (seq >> 24) & 0xFF; pkt[5]  = (seq >> 16) & 0xFF;
    pkt[6]  = (seq >> 8) & 0xFF;  pkt[7]  = seq & 0xFF;
    pkt[8]  = (ack >> 24) & 0xFF;  pkt[9]  = (ack >> 16) & 0xFF;
    pkt[10] = (ack >> 8) & 0xFF;   pkt[11] = ack & 0xFF;
    pkt[12] = 0x50;
    pkt[13] = flags;
    pkt[14] = 0xFF;
    pkt[15] = 0xFF;
    if (dlen > 0) memcpy(pkt + 20, data, dlen);

    uint16_t ck = tcp_checksum(g_ip, g_tcp_remote_ip, pkt, total);
    pkt[16] = (ck >> 8) & 0xFF;
    pkt[17] = ck & 0xFF;

    return net_send_ip(g_tcp_remote_ip, IP_PROTO_TCP, pkt, total);
}

/* 发送一个纯 ACK 段（携带当前 rcv_nxt 作为确认号）。
 * 这是 TCP 正确性的关键：接收方必须持续 ACK，否则发送方在发完初始窗口后
 * 会因等不到 ACK 而停滞（本项目此前 HTTP 响应被截断到 6 字节的根因）。 */
static void tcp_send_ack(void) {
    if (g_tcp_state == TCP_CLOSED) return;
    tcp_tx(g_tcp_snd_nxt, g_tcp_rcv_nxt, 0x10, 0, 0);
}

static void net_rx_tcp(const uint8_t* tcp, uint32_t tcp_len,
                       uint32_t src_ip) {
    if (tcp_len < 20) return;
    uint16_t sport = ((uint16_t)tcp[0] << 8) | tcp[1];
    uint16_t dport = ((uint16_t)tcp[2] << 8) | tcp[3];
    if (dport != g_tcp_local_port) return;
    if (src_ip != g_tcp_remote_ip || sport != g_tcp_remote_port) return;

    uint32_t seq = ((uint32_t)tcp[4] << 24) | ((uint32_t)tcp[5] << 16) |
                   ((uint32_t)tcp[6] << 8) | tcp[7];
    uint32_t ack = ((uint32_t)tcp[8] << 24) | ((uint32_t)tcp[9] << 16) |
                   ((uint32_t)tcp[10] << 8) | tcp[11];
    uint8_t flags = tcp[13];
    uint32_t doff = ((uint32_t)tcp[12] >> 4) * 4;
    if (doff < 20 || doff > tcp_len) return;
    uint32_t payload_len = tcp_len - doff;
    const uint8_t* payload = tcp + doff;

    /* ---- 诊断：记录每个收到的 TCP 段（无论状态） ---- */
    {
        serial_write("[TCP] <");
        /* flags: SYN=0x02 ACK=0x10 FIN=0x01 RST=0x04 PSH=0x08 */
        if (flags & 0x02) serial_write("S");
        if (flags & 0x04) { serial_write("R"); g_tcp_got_rst = 1; }
        if (flags & 0x08) serial_write("P");
        if (flags & 0x10) serial_write("A");
        if (flags & 0x01) serial_write("F");
        serial_write(" seq=");
        { char nb[12]; int ni=0; uint32_t v=seq;
          if(v==0)nb[ni++]='0';
          while(v>0){nb[ni++]='0'+(char)(v%10);v/=10;}
          int si,ei=ni-1;while(si<ei){char t=nb[si];nb[si]=nb[ei];nb[ei]=t;si++;ei--;}
          nb[ni]=0; serial_write(nb);
        }
        serial_write(" dlen=");
        { char nb[12]; int ni=0; uint32_t v=payload_len;
          if(v==0)nb[ni++]='0';
          while(v>0){nb[ni++]='0'+(char)(v%10);v/=10;}
          int si,ei=ni-1;while(si<ei){char t=nb[si];nb[si]=nb[ei];nb[ei]=t;si++;ei--;}
          nb[ni]=0; serial_write(nb);
        }
        serial_write("\n");
    }

    /* 若收到 RST，立即标记并打印 */
    if ((flags & 0x04) && g_tcp_state != TCP_CLOSED) {
        serial_write("[TCP] !!! RST RECEIVED !!!\n");
        g_tcp_state = TCP_CLOSED;
        return;
    }

    switch (g_tcp_state) {
    case TCP_SYN_SENT:
        if ((flags & 0x12) == 0x12) {
            g_tcp_snd_una = ack;
            g_tcp_snd_nxt = ack;
            g_tcp_irss = seq;
            g_tcp_rcv_nxt = seq + 1;
            g_tcp_got_synack = 1;
        }
        break;
    case TCP_ESTABLISHED:
        /* payload_len 现已由 IP total length 正确推导：纯 ACK（仅 TCP options，
         * 无应用数据）的 payload_len=0，不会进入此分支；带数据的段
         * payload_len>0 且 seq==rcv_nxt 时入 RX 缓冲区。无需 PSH/FIN 门控，
         * 否则会丢弃中间无 PSH 的数据段导致多段传输卡死。 */
        if (payload_len > 0) {
            if (seq == g_tcp_rcv_nxt) {
                uint32_t space = TCP_RX_BUF_SIZE - g_tcp_rx_tail;
                if (space > 0) {
                    uint32_t copy = payload_len;
                    if (copy > space) copy = space;
                    memcpy(g_tcp_rx_buf + g_tcp_rx_tail, payload, copy);
                    g_tcp_rx_tail += copy;
                    g_tcp_rcv_nxt += copy;
                    g_tcp_got_data = 1;
                    g_tcp_rx_segs++;
                    g_tcp_rx_bytes += copy;
                }
            }
            /* 收到数据（无论是否恰好对齐 rcv_nxt）都回 ACK 确认，避免对端停滞。 */
            tcp_send_ack();
        }
        if (flags & 0x01) {
            g_tcp_got_fin = 1;
            if (seq + 1 > g_tcp_rcv_nxt)
                g_tcp_rcv_nxt = seq + 1;
            tcp_send_ack();
        }
        break;
    case TCP_FIN_WAIT1:
        if ((flags & 0x11) == 0x11) {
            g_tcp_got_finack = 1;
        } else if ((flags & 0x10) && ack >= g_tcp_snd_nxt) {
            g_tcp_state = TCP_FIN_WAIT2;
        }
        break;
    case TCP_FIN_WAIT2:
        if ((flags & 0x11) == 0x11) {
            g_tcp_got_finack = 1;
        }
        break;
    default: break;
    }

    if ((flags & 0x10) && g_tcp_state != TCP_CLOSED) {
        if (ack > g_tcp_snd_una) g_tcp_snd_una = ack;
    }
}

int net_tcp_connect(uint32_t dst_ip, uint16_t dst_port) {
    if (!g_dev) return -1;
    g_tcp_state = TCP_CLOSED;
    g_tcp_remote_ip = dst_ip;
    g_tcp_remote_port = dst_port;
    g_tcp_local_port = 0xC000 | (uint16_t)(get_tick() & 0x3FFF);
    static uint16_t iss_seq = 0x1000;
    g_tcp_iss = iss_seq;
    iss_seq += 0x1000;
    g_tcp_snd_nxt = g_tcp_iss;
    g_tcp_snd_una = g_tcp_iss;
    g_tcp_rcv_nxt = 0;
    g_tcp_irss = 0;
    g_tcp_rx_head = 0;
    g_tcp_rx_tail = 0;
    g_tcp_got_data = 0;
    g_tcp_got_fin = 0;
    g_tcp_got_synack = 0;
    g_tcp_got_finack = 0;

    g_tcp_state = TCP_SYN_SENT;
    serial_write("[TCP] SYN to ");
    serial_put_ip(dst_ip);
    serial_write(":");
    char p[6]; itoa3(dst_port, p); serial_write(p);
    serial_write("\n");

    if (tcp_tx(g_tcp_iss, 0, 0x02, 0, 0) != 0) {
        serial_write("[TCP] tx failed\n");
        g_tcp_state = TCP_CLOSED;
        return -1;
    }
    serial_write("[TCP] SYN sent, waiting...\n");

    int i;
    for (i = 0; i < 300; i++) {
        net_poll();
        if (g_tcp_got_synack) break;
        udelay(10000);
    }
    if (!g_tcp_got_synack) {
        serial_write("[TCP] SYN-ACK timeout after ");
        char c[8]; itoa3(i, c); serial_write(c);
        serial_write(" iterations\n");
        g_tcp_state = TCP_CLOSED;
        return -1;
    }
    g_tcp_got_synack = 0;

    if (tcp_tx(g_tcp_snd_nxt, g_tcp_rcv_nxt, 0x10, 0, 0) != 0) {
        g_tcp_state = TCP_CLOSED;
        return -1;
    }
    g_tcp_state = TCP_ESTABLISHED;
    serial_write("[TCP] established\n");
    return 0;
}

int net_tcp_send(const uint8_t* data, uint32_t len) {
    if (g_tcp_state != TCP_ESTABLISHED || !data || len == 0) return -1;
    if (tcp_tx(g_tcp_snd_nxt, g_tcp_rcv_nxt, 0x18, data, len) != 0) return -1;
    g_tcp_snd_nxt += len;

    int i;
    for (i = 0; i < 200; i++) {
        net_poll();
        if (g_tcp_snd_una >= g_tcp_snd_nxt) break;
        udelay(5000);
    }
    return 0;
}

int net_tcp_recv(uint8_t* buf, uint32_t cap, int timeout_ms) {
    if (!buf || cap == 0) return -1;
    int ticks = timeout_ms / 1;   /* 每 1ms 轮询一次，降低每段等待延迟 */
    if (ticks < 1) ticks = 1;
    int i;
    for (i = 0; i <= ticks; i++) {
        if (g_tcp_rx_tail > g_tcp_rx_head) {
            uint32_t avail = g_tcp_rx_tail - g_tcp_rx_head;
            if (avail > cap) avail = cap;
            memcpy(buf, g_tcp_rx_buf + g_tcp_rx_head, avail);
            g_tcp_rx_head += avail;
            /* 全部消费完则把游标归零，避免线性缓冲单调增长（响应可大于缓冲）。 */
            if (g_tcp_rx_head == g_tcp_rx_tail) { g_tcp_rx_head = 0; g_tcp_rx_tail = 0; }
            return (int)avail;
        }
        if (g_tcp_got_fin && g_tcp_rx_tail == g_tcp_rx_head) return 0;
        if (g_tcp_got_rst) return -1;   /* RST：连接被重置 */
        if (i < ticks) { net_poll(); udelay(1000); }  /* 1ms 密集轮询 */
    }
    return -1;
}

void net_tcp_close(void) {
    if (g_tcp_state == TCP_CLOSED) return;
    if (g_tcp_state == TCP_ESTABLISHED) {
        g_tcp_state = TCP_FIN_WAIT1;
        tcp_tx(g_tcp_snd_nxt, g_tcp_rcv_nxt, 0x11, 0, 0);
        g_tcp_snd_nxt++;

        int i;
        for (i = 0; i < 200; i++) {
            net_poll();
            if (g_tcp_got_finack) break;
            udelay(10000);
        }
        if (g_tcp_got_finack) {
            tcp_tx(g_tcp_snd_nxt, g_tcp_rcv_nxt, 0x10, 0, 0);
        }
    }
    g_tcp_state = TCP_CLOSED;
    serial_write("[TCP] closed\n");
}

/* ------------------------------------------------------------------ */
/* HTTP/1.0 client                                                      */
/* ------------------------------------------------------------------ */

#define HTTP_MAX_RESP (512*1024)

int http_get(const char* host, const char* path,
             uint8_t** out_body, uint32_t* out_body_len) {
    if (!host || !path || !out_body || !out_body_len) return -1;
    *out_body = 0;
    *out_body_len = 0;

    serial_write("[HTTP] GET http://");
    serial_write(host);
    serial_write(path);
    serial_write("\n");

    uint32_t ip;
    if (dns_resolve(host, &ip) != 0) return -1;

    if (net_tcp_connect(ip, 80) != 0) return -1;

    char req[1024];
    int reqlen = 0;
    const char* p = "GET ";
    while (*p) req[reqlen++] = *p++;
    p = path;
    while (*p && reqlen < 900) req[reqlen++] = *p++;
    p = " HTTP/1.0\r\nHost: ";
    while (*p && reqlen < 950) req[reqlen++] = *p++;
    p = host;
    while (*p && reqlen < 980) req[reqlen++] = *p++;
    p = "\r\nUser-Agent: SproutOS/1.0\r\nConnection: close\r\n\r\n";
    while (*p && reqlen < 1010) req[reqlen++] = *p++;
    req[reqlen] = 0;

    if (net_tcp_send((const uint8_t*)req, (uint32_t)reqlen) != 0) {
        net_tcp_close(); return -1;
    }

    uint8_t* resp = kmalloc(HTTP_MAX_RESP);
    if (!resp) { net_tcp_close(); return -1; }
    uint32_t rlen = 0;
    int max_wait = 30000;
    int empty_rounds = 0;   /* 连续空轮次计数 */
    while (rlen < HTTP_MAX_RESP) {
        int got = net_tcp_recv(resp + rlen, HTTP_MAX_RESP - rlen, max_wait);
        if (got > 0) {
            rlen += (uint32_t)got;
            max_wait = 2000;
            empty_rounds = 0;
        } else if (got == 0) {
            /* FIN 收到且数据消费完：再等一小会儿看是否有延迟包 */
            empty_rounds++;
            if (empty_rounds > 3) break;   /* 多等 3 轮（~6ms）后真退出 */
            net_poll(); udelay(2000);
        } else {
            break;
        }
    }
    net_tcp_close();

    if (rlen == 0) { kfree(resp); return -1; }

    /* ---- 诊断：打印原始响应的关键信息 ---- */
    serial_write("[HTTP] total_rlen=");
    { char nb[12]; int ni=0; uint32_t rl=rlen;
      if(rl==0)nb[ni++]='0';
      while(rl>0){nb[ni++]='0'+(char)(rl%10);rl/=10;}
      int si,ei=ni-1;while(si<ei){char t=nb[si];nb[si]=nb[ei];nb[ei]=t;si++;ei--;}
      nb[ni]=0; serial_write(nb);
    }
    serial_write(" status=");
    /* 打印前 200 字节的可见部分（非可打字符用 . 替代） */
    { int di=0; uint32_t show = (rlen<200)?rlen:200U;
      while(di<(int)show){
          unsigned char c=resp[di];
          if(c>=32&&c<127){uint8_t tmp[2];tmp[0]=c;tmp[1]=0;serial_write((char*)tmp);}
          else{serial_write(".");}
          di++;
      }
      serial_write("\n");
    }
    /* Hex dump 前 32 字节（最关键：直接看到 6 字节到底是什么） */
    { int di=0; uint32_t hshow = (rlen<32)?rlen:32U;
      serial_write("[HTTP] hex(");
      { char nb[12]; int ni=0; uint32_t v=hshow;
        if(v==0)nb[ni++]='0';
        while(v>0){nb[ni++]='0'+(char)(v%10);v/=10;}
        int si,ei=ni-1;while(si<ei){char t=nb[si];nb[si]=nb[ei];nb[ei]=t;si++;ei--;}
        nb[ni]=0; serial_write(nb);
      }
      serial_write(")=");
      while(di<(int)hshow){
          unsigned char c=resp[di];
          { char hb[3]; int hi=0;
            hb[hi++] = "0123456789ABCDEF"[(c>>4)&0xF];
            hb[hi++] = "0123456789ABCDEF"[c&0xF];
            hb[hi]=0; serial_write(hb);
          }
          di++;
      }
      serial_write("\n");
    }
    /* 打印 TCP 接收统计 */
    serial_write("[TCP] rx_segs=");
    { char nb[12]; int ni=0; uint32_t v=g_tcp_rx_segs;
      if(v==0)nb[ni++]='0';
      while(v>0){nb[ni++]='0'+(char)(v%10);v/=10;}
      int si,ei=ni-1;while(si<ei){char t=nb[si];nb[si]=nb[ei];nb[ei]=t;si++;ei--;}
      nb[ni]=0; serial_write(nb);
    }
    serial_write(" rx_bytes=");
    { char nb[12]; int ni=0; uint32_t v=g_tcp_rx_bytes;
      if(v==0)nb[ni++]='0';
      while(v>0){nb[ni++]='0'+(char)(v%10);v/=10;}
      int si,ei=ni-1;while(si<ei){char t=nb[si];nb[si]=nb[ei];nb[ei]=t;si++;ei--;}
      nb[ni]=0; serial_write(nb);
    }
    serial_write("\n");

    uint32_t body_off = 0;
    uint32_t body_len = rlen;
    int status_code = 0;

    const char* rsp = (const char*)resp;
    if (rlen >= 12 && rsp[0] == 'H' && rsp[1] == 'T' && rsp[2] == 'T') {
        const char* eol = rsp;
        int i;
        for (i = 0; i < (int)rlen - 4 && !(eol[0]=='\r'&&eol[1]=='\n'&&eol[2]=='\r'&&eol[3]=='\n'); i++, eol++);
        if (eol[0]=='\r'&&eol[1]=='\n'&&eol[2]=='\r'&&eol[3]=='\n') {
            const char* sp = rsp + 9;
            while (*sp == ' ') sp++;
            status_code = 0;
            while (*sp >= '0' && *sp <= '9') status_code = status_code * 10 + (*sp++ - '0');

            if (status_code == 301 || status_code == 302 || status_code == 303 ||
                status_code == 307 || status_code == 308) {
                const char* loc = 0;
                const char* hline = rsp;
                while (hline < eol) {
                    const char* next = hline;
                    while (next < eol && !(*next=='\r'&&*(next+1)=='\n')) next++;
                    if (starts_with(hline, "Location:") == 0) {
                        loc = hline + 9;
                        while (*loc == ' ') loc++;
                        int llen = (int)(next - loc);
                        if (llen > 0 && llen < 512) {
                            char redir_url[512];
                            int ci;
                            for (ci = 0; ci < llen; ci++) redir_url[ci] = loc[ci];
                            redir_url[ci] = 0;

                            /* 仅跟随 HTTP 重定向；HTTPS 需要 TLS，本 OS 不支持。
                             * 跳过 HTTPS 重定向，直接使用 302 响应 body（通常含
                             * "Redirecting..." HTML），避免返回 -1 导致浏览器白屏。 */
                            if (starts_with(redir_url, "http://") == 0) {
                                char rh[256], rp[256];
                                rh[0]=0; rp[0]=0;
                                const char* ru = redir_url + 7;
                                const char* slash = ru;
                                while (*slash && *slash != '/') slash++;
                                if (*slash) {
                                    int hl = (int)(slash - ru);
                                    if (hl < 255) {
                                        int di = 0;
                                        for (di = 0; di < hl; di++) rh[di] = ru[di];
                                        rh[hl] = 0;
                                        const char* pp = slash + 1;
                                        while (*pp) { rp[di++] = *pp++; }
                                        rp[di] = 0;
                                    }
                                }
                                kfree(resp);
                                if (rh[0]) return http_get(rh, rp[0]?rp:"/", out_body, out_body_len);
                                return -1;
                            }
                            /* https:// 或其他 scheme：不跟随，fall through 用 body */
                            serial_write("[HTTP] note: skipping https redirect, using response body\n");
                        }
                    }
                    hline = next + 2;
                }
            }

            const char* cl = rsp;
            while (cl < eol) {
                const char* nxt = cl;
                while (nxt < eol && !(*nxt=='\r'&&*(nxt+1)=='\n')) nxt++;
                if (starts_with(cl, "Content-Length:") == 0) {
                    const char* v = cl + 15;
                    while (*v == ' ') v++;
                    uint32_t clen = 0;
                    while (*v >= '0' && *v <= '9') clen = clen * 10 + (uint32_t)(*v++ - '0');
                    body_len = clen;
                }
                if (starts_with(cl, "Transfer-Encoding:") == 0) {
                    body_len = rlen;
                }
                cl = nxt + 2;
            }
            body_off = (uint32_t)((eol + 4) - rsp);
            if (body_off > rlen) body_off = rlen;
            if (body_len > rlen - body_off) body_len = rlen - body_off;
        }
    }

    if (body_len == 0 || body_off >= rlen) {
        kfree(resp); return -1;
    }

    uint8_t* body = kmalloc(body_len + 1);
    if (!body) { kfree(resp); return -1; }
    memcpy(body, resp + body_off, body_len);
    body[body_len] = 0;
    kfree(resp);

    *out_body = body;
    *out_body_len = body_len;
    serial_write("[HTTP] ok ");
    char nb[12]; int ni=0; uint32_t bl=body_len;
    if (bl == 0) nb[ni++] = '0';
    while (bl > 0) { nb[ni++] = '0' + (char)(bl % 10); bl /= 10; }
    int si,ei=ni-1; while(si<ei){char t=nb[si];nb[si]=nb[ei];nb[ei]=t;si++;ei--;}
    nb[ni]=0; serial_write(nb);
    serial_write(" bytes\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Driver registration (called from net_init, before driver_run_all)   */
/* ------------------------------------------------------------------ */

void net_init(void) {
    serial_write("[NET] net_init: registering NIC drivers\n");
    e1000_register();
    rtl8139_register();
}
