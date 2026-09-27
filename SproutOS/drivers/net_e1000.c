#include "kernel.h"
#include "pci.h"
#include "driver.h"
#include "net.h"

/* e1000 register offsets (byte offsets; access via mmio[off/4]). */
#define E1000_CTRL  0x0000
#define E1000_STATUS 0x0008
#define E1000_ICR   0x00C0
#define E1000_RCTL  0x0100
#define E1000_TCTL  0x0400
#define E1000_RDBAL 0x2800
#define E1000_RDBAH 0x2804
#define E1000_RDLEN 0x2808
#define E1000_RDH   0x2810
#define E1000_RDT   0x2818
#define E1000_TDBAL 0x3800
#define E1000_TDBAH 0x3804
#define E1000_TDLEN 0x3808
#define E1000_TDH   0x3810
#define E1000_TDT   0x3818
#define E1000_RAL   0x5400
#define E1000_RAH   0x5404

#define E1000_TX_N   16
#define E1000_RX_N   64          /* 增大 RX 环：更多在途缓冲，避免服务器被流控卡住 */
#define E1000_RXBUF  2048

/* Transmit descriptor (16 bytes). */
typedef struct {
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc;

/* Receive descriptor (16 bytes). */
typedef struct {
    uint32_t addr_lo;
    uint32_t addr_hi;
    uint16_t length;
    uint16_t csum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc;

static volatile uint32_t* e1000_mmio = 0;
static volatile e1000_tx_desc* e1000_tx = 0;
static uint8_t*           e1000_tx_buf = 0;
static volatile e1000_rx_desc* e1000_rx = 0;
static uint8_t*           e1000_rx_buf[E1000_RX_N];
static int                e1000_tx_head = 0;
static int                e1000_rx_cur = 0;
static uint8_t            e1000_irq = 0;

/* Forward declarations (probe assigns these before they are defined). */
static int  e1000_send(netdev_t* dev, const uint8_t* dst, uint16_t et,
                       const uint8_t* payload, uint32_t len);
static void e1000_poll(netdev_t* dev);
static void e1000_irq_handler(regs* r);

/* 16-byte aligned kmalloc (kmalloc is only 8-byte aligned). */
static void* kalloc16(uint32_t size) {
    uint32_t p = (uint32_t)kmalloc(size + 15);
    if (!p) return 0;
    return (void*)((p + 15) & ~15u);
}

static int e1000_probe(uint8_t bus, uint8_t dev, uint8_t fn) {
    uint32_t bar0 = pci_config_read32(bus, dev, fn, 0x10);
    if (bar0 & 0x1) {
        serial_write("[E1000] IO BAR not supported; skipping\n");
        return -1;
    }
    uint32_t phys = bar0 & 0xFFFFFFF0u;
    /* Map the MMIO BAR (high address, uncached). Identity map covers only
       the lower 512 MB, so the NIC's register window must be mapped. The
       e1000 register space spans past 0x5400, so map 64 KB. */
    for (uint32_t pg = 0; pg < 0x10000; pg += 0x1000)
        vmm_map_page(phys + pg, phys + pg, 0x01 | 0x02 | 0x10);  /* PRESENT|WRITABLE|NOCACHE */
    e1000_mmio = (volatile uint32_t*)(uint32_t)phys;

    /* Enable memory space + bus master in the PCI command register. */
    uint16_t cmd = pci_config_read16(bus, dev, fn, 0x04);
    cmd |= 0x02 | 0x04;
    pci_config_write16(bus, dev, fn, 0x04, cmd);

    /* Software reset (CTRL.SWR, bit 26) and wait for it to clear. */
    e1000_mmio[E1000_CTRL / 4] |= (1u << 26);
    for (int i = 0; i < 100; i++) {
        if (!(e1000_mmio[E1000_CTRL / 4] & (1u << 26))) break;
        udelay(10000);
    }

    /* Read the MAC from RAL/RAH (QEMU programmes it there). */
    uint32_t ral = e1000_mmio[E1000_RAL / 4];
    uint32_t rah = e1000_mmio[E1000_RAH / 4];
    uint8_t mac[6];
    mac[0] = (uint8_t)(ral & 0xFF);
    mac[1] = (uint8_t)((ral >> 8) & 0xFF);
    mac[2] = (uint8_t)((ral >> 16) & 0xFF);
    mac[3] = (uint8_t)((ral >> 24) & 0xFF);
    mac[4] = (uint8_t)(rah & 0xFF);
    mac[5] = (uint8_t)((rah >> 8) & 0xFF);

    e1000_irq = pci_config_read8(bus, dev, fn, 0x3C);

    /* Allocate rings + buffers. */
    e1000_tx = (volatile e1000_tx_desc*)kalloc16(sizeof(e1000_tx_desc) * E1000_TX_N);
    e1000_tx_buf = (uint8_t*)kalloc16(2048);
    e1000_rx = (volatile e1000_rx_desc*)kalloc16(sizeof(e1000_rx_desc) * E1000_RX_N);
    if (!e1000_tx || !e1000_tx_buf || !e1000_rx) {
        serial_write("[E1000] out of memory\n");
        return -1;
    }
    for (int i = 0; i < E1000_RX_N; i++) {
        e1000_rx_buf[i] = (uint8_t*)kalloc16(E1000_RXBUF);
        if (!e1000_rx_buf[i]) { serial_write("[E1000] rx buf oom\n"); return -1; }
    }

    /* Initialise the transmit ring (all descriptors point at one buffer;
       we wait for DD before reusing, so this is safe). */
    for (int i = 0; i < E1000_TX_N; i++) {
        e1000_tx[i].addr_lo = (uint32_t)e1000_tx_buf;
        e1000_tx[i].addr_hi = 0;
        e1000_tx[i].length = 0;
        e1000_tx[i].cso = 0;
        e1000_tx[i].cmd = 0;
        e1000_tx[i].status = 0;
        e1000_tx[i].css = 0;
        e1000_tx[i].special = 0;
    }
    e1000_mmio[E1000_TDBAL / 4] = (uint32_t)e1000_tx;
    e1000_mmio[E1000_TDBAH / 4] = 0;
    e1000_mmio[E1000_TDLEN / 4] = (uint32_t)(sizeof(e1000_tx_desc) * E1000_TX_N);
    e1000_mmio[E1000_TDH / 4] = 0;
    e1000_mmio[E1000_TDT / 4] = 0;
    e1000_mmio[E1000_TCTL / 4] =
        (1u << 1) | (1u << 3) | (0x0Fu << 4) | (0x40u << 12);  /* EN|PSP|CT|COLD */

    /* Initialise the receive ring. */
    for (int i = 0; i < E1000_RX_N; i++) {
        e1000_rx[i].addr_lo = (uint32_t)e1000_rx_buf[i];
        e1000_rx[i].addr_hi = 0;
        e1000_rx[i].length = 0;
        e1000_rx[i].csum = 0;
        e1000_rx[i].status = 0;
        e1000_rx[i].errors = 0;
        e1000_rx[i].special = 0;
    }
    e1000_mmio[E1000_RDBAL / 4] = (uint32_t)e1000_rx;
    e1000_mmio[E1000_RDBAH / 4] = 0;
    e1000_mmio[E1000_RDLEN / 4] = (uint32_t)(sizeof(e1000_rx_desc) * E1000_RX_N);
    e1000_mmio[E1000_RDH / 4] = 0;
    e1000_mmio[E1000_RDT / 4] = E1000_RX_N - 1;
    e1000_mmio[E1000_RCTL / 4] =
        (1u << 1) | (1u << 15) | (1u << 26);   /* EN|BAM|SECRC (strip CRC) */

    /* Register the device. */
    static netdev_t nd;
    nd.name = "e1000";
    memcpy(nd.mac, mac, 6);
    nd.irq = e1000_irq;
    nd.send = e1000_send;
    nd.poll = e1000_poll;
    nd.drv = 0;
    net_register(&nd);

    /* Defensive IRQ hook (RX is polled, IMS left at 0 so no interrupts fire). */
    if (e1000_irq) {
        register_interrupt_handler((uint8_t)(0x20 + e1000_irq), e1000_irq_handler);
    }
    return 0;
}

static int e1000_send(netdev_t* dev, const uint8_t* dst, uint16_t et,
                      const uint8_t* payload, uint32_t len) {
    if (!e1000_mmio) return -1;
    if (len > 2048 - 14) len = 2048 - 14;

    memcpy(e1000_tx_buf, dst, 6);
    memcpy(e1000_tx_buf + 6, dev->mac, 6);
    e1000_tx_buf[12] = (uint8_t)((et >> 8) & 0xFF);
    e1000_tx_buf[13] = (uint8_t)(et & 0xFF);
    memcpy(e1000_tx_buf + 14, payload, len);
    uint32_t tlen = 14 + len;

    int i = e1000_tx_head;
    e1000_tx[i].addr_lo = (uint32_t)e1000_tx_buf;
    e1000_tx[i].addr_hi = 0;
    e1000_tx[i].length = (uint16_t)tlen;
    e1000_tx[i].cso = 0;
    e1000_tx[i].cmd = 0x0B;     /* EOP | IFCS | RS */
    e1000_tx[i].status = 0;
    e1000_mmio[E1000_TDT / 4] = (uint32_t)((i + 1) % E1000_TX_N);

    /* Wait for descriptor done (DD). */
    int tries = 0;
    while (!(e1000_tx[i].status & 0x01)) {
        tries++;
        if (tries > 2000000) break;
    }
    e1000_tx_head = (i + 1) % E1000_TX_N;
    return 0;
}

static void e1000_poll(netdev_t* dev) {
    (void)dev;
    if (!e1000_mmio) return;
    uint16_t rdt = (uint16_t)(E1000_RX_N - 1);
    int cur = e1000_rx_cur;
    while (e1000_rx[cur].status & 0x01) {
        uint16_t len = e1000_rx[cur].length;
        if (len > E1000_RXBUF) len = E1000_RXBUF;
        net_rx(e1000_rx_buf[cur], len);
        e1000_rx[cur].status = 0;
        e1000_rx[cur].length = 0;
        rdt = (uint16_t)cur;                 /* hand this descriptor back to HW */
        e1000_mmio[E1000_RDT / 4] = rdt;
        cur = (cur + 1) % E1000_RX_N;
    }
    e1000_rx_cur = cur;
}

static void e1000_irq_handler(regs* r) {
    (void)r;
    if (!e1000_mmio) return;
    uint32_t icr = e1000_mmio[E1000_ICR / 4];   /* read clears */
    (void)icr;
    net_poll();
}

void e1000_register(void) {
    /* Generic Intel 8254x Gigabit register set — covers QEMU and VMware. */
    driver_register("e1000", 0x8086, 0x100E, e1000_probe);   /* 82540EM (QEMU default) */
    driver_register("e1000", 0x8086, 0x100F, e1000_probe);   /* 82545EM (VMware "E1000" default) */
    driver_register("e1000", 0x8086, 0x1010, e1000_probe);   /* 82546EB */
    driver_register("e1000", 0x8086, 0x1011, e1000_probe);   /* 82545EM */
    driver_register("e1000", 0x8086, 0x1029, e1000_probe);   /* 82559 (older VMware) */
    driver_register("e1000", 0x8086, 0x107C, e1000_probe);   /* 82541PI */
    driver_register("e1000", 0x8086, 0x10D3, e1000_probe);   /* 82574L (e1000e / VMware "E1000E") */
}
