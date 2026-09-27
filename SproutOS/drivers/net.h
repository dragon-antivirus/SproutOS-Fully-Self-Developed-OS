#ifndef NET_H
#define NET_H

#include "kernel.h"

/* EtherTypes */
#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IP  0x0800

/* IP protocol numbers */
#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP  17
#define IP_PROTO_TCP  6

/* A detected/active network device. Drivers fill this in and call
   net_register() once the NIC is brought up. */
typedef struct netdev netdev_t;

struct netdev {
    const char* name;          /* driver name, e.g. "e1000" */
    uint8_t mac[6];
    uint32_t irq;              /* PCI interrupt line (informational) */
    /* Send one ethernet frame. dst_mac = 6-byte destination, et = ethertype,
       payload/len = L3 packet. Returns 0 on success. */
    int (*send)(netdev_t* dev, const uint8_t* dst_mac, uint16_t et,
                const uint8_t* payload, uint32_t len);
    /* Poll for received frames and feed them to net_rx(). */
    void (*poll)(netdev_t* dev);
    void* drv;                 /* driver-private state (mmio base, rings) */
};

/* Called once at boot, before driver_run_all(). Registers NIC drivers. */
void net_init(void);

/* Terminal output helper (defined in ui/gui.c, shared with the net stack). */
void term_puts(const char* s);

/* Called by a driver probe on successful bring-up. Sets the active device. */
void net_register(netdev_t* dev);

/* Drivers call this with each received ethernet frame (14-byte header +
   payload). Parses ARP / IPv4+ICMP. */
void net_rx(const uint8_t* frame, uint32_t len);

/* Poll the active device for incoming frames (safe to call often). */
void net_poll(void);

/* Send an IPv4 packet to dst_ip (wire order) with given proto + payload.
   Resolves ARP as needed. Returns 0 on success, -1 on failure. */
int net_send_ip(uint32_t dst_ip, uint8_t proto, const uint8_t* data, uint32_t len);

/* Resolve dst_ip to a MAC via ARP (blocking poll up to ~1s). 0 on success. */
int net_arp_resolve(uint32_t dst_ip, uint8_t* out_mac);

/* Accessors */
int            net_up(void);
const uint8_t* net_mac(void);
const char*    net_driver_name(void);

/* Terminal commands (dispatched from ui/gui.c term_exec). */
void net_cmd(const char* arg);    /* arg: text after "net " (may be "") */
void net_ping(const char* arg);   /* arg: text after "ping " */

/* Driver registration entry points (implemented in net_e1000.c / net_rtl8139.c). */
void e1000_register(void);
void rtl8139_register(void);

/* ------------------------------------------------------------------ */
/* UDP                                                                  */
/* ------------------------------------------------------------------ */

/* Send a UDP datagram. Returns 0 on success. */
int net_udp_send(uint32_t dst_ip, uint16_t dst_port, uint16_t src_port,
                 const uint8_t* data, uint32_t len);

/* ------------------------------------------------------------------ */
/* DNS client                                                           */
/* ------------------------------------------------------------------ */

/* Resolve hostname via DNS (queries 10.0.2.3:53, QEMU user-mode DNS).
   Returns 0 on success, writes IPv4 into out_ip (wire order). */
int dns_resolve(const char* hostname, uint32_t* out_ip);

/* ------------------------------------------------------------------ */
/* Minimal TCP (single blocking connection)                             */
/* ------------------------------------------------------------------ */

/* Connect to dst_ip:dst_port (blocking, SYN/SYN-ACK/ACK). 0 = ok. */
int net_tcp_connect(uint32_t dst_ip, uint16_t dst_port);

/* Send data on the established TCP connection. 0 = ok. */
int net_tcp_send(const uint8_t* data, uint32_t len);

/* Receive data (blocks up to timeout_ms with polling).
   Returns bytes received, or -1 on error/close. */
int net_tcp_recv(uint8_t* buf, uint32_t cap, int timeout_ms);

/* Gracefully close the TCP connection. */
void net_tcp_close(void);

/* ------------------------------------------------------------------ */
/* HTTP/1.0 client                                                      */
/* ------------------------------------------------------------------ */

/* HTTP GET request. Resolves host via DNS, connects, sends request,
   reads response. Returns 0 on success; *out_body is kmalloc'd buffer
   (caller must kfree), *out_body_len set.
   Supports HTTP/1.0 non-pipelined, follows redirects (max 3). */
int http_get(const char* host, const char* path,
             uint8_t** out_body, uint32_t* out_body_len);

#endif
