#include "kernel.h"
#include "pci.h"
#include "driver.h"
#include "net.h"

/* RTL8139 register offsets. */
#define RTL_MAC0    0x00
#define RTL_TXSTATUS0 0x10
#define RTL_TXSTART0  0x20
#define RTL_RBSTART   0x30
#define RTL_CR        0x37   /* command: RST=0x10, RE=0x08, TE=0x04 */
#define RTL_CAPR      0x38
#define RTL_IMR       0x3C
#define RTL_ISR       0x3E
#define RTL_TCR       0x40
#define RTL_RCR       0x44
#define RTL_CONFIG1   0x52

#define RTL_RXBUF_SIZE 8192

static int      rtl_io = 0;       /* 1 = port IO, 0 = memory mapped */
static uint32_t rtl_base = 0;     /* IO port or mapped virtual address */
static uint8_t* rtl_rx_buf = 0;
static uint32_t rtl_rx_off = 0;
static uint8_t* rtl_tx_buf = 0;
static uint8_t  rtl_irq = 0;
static uint8_t  rtl_mac[6];
static netdev_t rtl_nd;

/* Forward declarations (probe assigns these before they are defined). */
static int  rtl8139_send(netdev_t* dev, const uint8_t* dst, uint16_t et,
                         const uint8_t* payload, uint32_t len);
static void rtl8139_poll(netdev_t* dev);
static void rtl8139_irq_handler(regs* r);

/* 16-byte aligned kmalloc (kmalloc is only 8-byte aligned). */
static void* kalloc16(uint32_t size) {
    uint32_t p = (uint32_t)kmalloc(size + 15);
    if (!p) return 0;
    return (void*)((p + 15) & ~15u);
}

/* Unified register accessors (port IO or memory mapped). */
static uint8_t rtl_r8(int off) {
    if (rtl_io) return inb((uint16_t)(rtl_base + off));
    return *(volatile uint8_t*)(rtl_base + off);
}
static uint16_t rtl_r16(int off) {
    if (rtl_io) return inw((uint16_t)(rtl_base + off));
    return *(volatile uint16_t*)(rtl_base + off);
}
static uint32_t rtl_r32(int off) {
    if (rtl_io) return inl((uint16_t)(rtl_base + off));
    return *(volatile uint32_t*)(rtl_base + off);
}
static void rtl_w8(int off, uint8_t v) {
    if (rtl_io) outb((uint16_t)(rtl_base + off), v);
    else *(volatile uint8_t*)(rtl_base + off) = v;
}
static void rtl_w16(int off, uint16_t v) {
    if (rtl_io) outw((uint16_t)(rtl_base + off), v);
    else *(volatile uint16_t*)(rtl_base + off) = v;
}
static void rtl_w32(int off, uint32_t v) {
    if (rtl_io) outl((uint16_t)(rtl_base + off), v);
    else *(volatile uint32_t*)(rtl_base + off) = v;
}

static int rtl8139_probe(uint8_t bus, uint8_t dev, uint8_t fn) {
    uint32_t bar0 = pci_config_read32(bus, dev, fn, 0x10);
    if (bar0 & 0x1) {
        rtl_io = 1;
        rtl_base = bar0 & ~3u;
    } else {
        uint32_t phys = bar0 & ~0xFu;
        vmm_map_page(phys, phys, 0x01 | 0x02 | 0x10);   /* PRESENT|WRITABLE|NOCACHE */
        rtl_io = 0;
        rtl_base = phys;
    }

    /* Enable IO/memory space + bus master. */
    uint16_t cmd = pci_config_read16(bus, dev, fn, 0x04);
    cmd |= 0x01 | 0x04;       /* IO/MMIO space + bus master */
    pci_config_write16(bus, dev, fn, 0x04, cmd);

    /* Power on (CONFIG1 bit 4 = 0). */
    rtl_w8(RTL_CONFIG1, (uint8_t)(rtl_r8(RTL_CONFIG1) & (uint8_t)~0x10));

    /* Software reset; wait for it to clear. */
    rtl_w8(RTL_CR, 0x10);
    for (int i = 0; i < 100; i++) {
        if (!(rtl_r8(RTL_CR) & 0x10)) break;
        udelay(10000);
    }

    /* Read the MAC. */
    for (int i = 0; i < 6; i++) rtl_mac[i] = rtl_r8(RTL_MAC0 + i);
    rtl_irq = pci_config_read8(bus, dev, fn, 0x3C);

    /* Buffers. */
    rtl_rx_buf = (uint8_t*)kalloc16(RTL_RXBUF_SIZE + 16);
    rtl_tx_buf = (uint8_t*)kalloc16(2048);
    if (!rtl_rx_buf || !rtl_tx_buf) {
        serial_write("[RTL8139] out of memory\n");
        return -1;
    }
    rtl_rx_off = 0;

    /* Receive buffer + config. */
    rtl_w32(RTL_RBSTART, (uint32_t)rtl_rx_buf);
    rtl_w32(RTL_RCR, 0x0F | (1u << 7) | (0u << 10));  /* accept all/bcast + WRAP + 8K */
    /* Transmit config: max DMA burst 1024, default inter-frame gap. */
    rtl_w32(RTL_TCR, 0x03000700);
    /* Enable receiver + transmitter. */
    rtl_w8(RTL_CR, 0x0C);

    /* Register the device. */
    rtl_nd.name = "rtl8139";
    memcpy(rtl_nd.mac, rtl_mac, 6);
    rtl_nd.irq = rtl_irq;
    rtl_nd.send = rtl8139_send;
    rtl_nd.poll = rtl8139_poll;
    rtl_nd.drv = 0;
    net_register(&rtl_nd);

    if (rtl_irq) {
        register_interrupt_handler((uint8_t)(0x20 + rtl_irq), rtl8139_irq_handler);
    }
    return 0;
}

static int rtl8139_send(netdev_t* dev, const uint8_t* dst, uint16_t et,
                        const uint8_t* payload, uint32_t len) {
    if (len > 1792) len = 1792;
    memcpy(rtl_tx_buf, dst, 6);
    memcpy(rtl_tx_buf + 6, dev->mac, 6);
    rtl_tx_buf[12] = (uint8_t)((et >> 8) & 0xFF);
    rtl_tx_buf[13] = (uint8_t)(et & 0xFF);
    memcpy(rtl_tx_buf + 14, payload, len);
    uint32_t tlen = 14 + len;

    rtl_w32(RTL_TXSTART0, (uint32_t)rtl_tx_buf);
    rtl_w32(RTL_TXSTATUS0, tlen);          /* writing length starts transmit */

    int tries = 0;
    while (!(rtl_r32(RTL_TXSTATUS0) & ((1u << 15) | (1u << 14)))) {  /* TOK | TABT */
        tries++;
        if (tries > 2000000) break;
    }
    return 0;
}

static void rtl8139_poll(netdev_t* dev) {
    (void)dev;
    for (;;) {
        uint16_t* p = (uint16_t*)(rtl_rx_buf + rtl_rx_off);
        uint16_t status = p[0];
        if (!(status & 0x0001)) break;            /* ROK not set -> no packet */
        uint16_t len = p[1];                      /* frame len incl CRC, excl 4-byte header */
        if (len > 4) {
            uint32_t flen = (uint32_t)len - 4;    /* drop CRC */
            if (flen > 1514) flen = 1514;
            net_rx((uint8_t*)(p + 2), flen);
        }
        /* Advance past this frame (header + payload), 4-byte aligned. */
        rtl_rx_off += (uint32_t)len + 4;
        rtl_rx_off = (rtl_rx_off + 3) & ~3u;
        if (rtl_rx_off >= RTL_RXBUF_SIZE) rtl_rx_off = 0;
        /* Tell the chip it can reuse memory up to here (CAPR = off-16, aligned). */
        uint32_t capr = (rtl_rx_off >= 16) ? (rtl_rx_off - 16)
                                           : (RTL_RXBUF_SIZE - 16 + rtl_rx_off);
        rtl_w16(RTL_CAPR, (uint16_t)(capr & 0xFFF8));
    }
}

static void rtl8139_irq_handler(regs* r) {
    (void)r;
    uint16_t isr = rtl_r16(RTL_ISR);   /* read clears */
    (void)isr;
    net_poll();
}

void rtl8139_register(void) {
    driver_register("rtl8139", 0x10EC, 0x8139, rtl8139_probe);
}
