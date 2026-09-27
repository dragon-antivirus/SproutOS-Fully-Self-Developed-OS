#include "kernel.h"
#include "ata.h"
#include "driver.h"

#define ATA_DATA      0
#define ATA_SECT_CNT  2
#define ATA_LBA_LOW   3
#define ATA_LBA_MID   4
#define ATA_LBA_HIGH  5
#define ATA_DRIVE     6
#define ATA_CMD       7
#define ATA_STATUS    7

#define CMD_READ  0x20
#define CMD_WRITE 0x30
#define CMD_IDENT 0xEC

static block_device_t ata_dev[2];
static int ata_ready = 0;
static int ata_registered = 0;

static uint16_t base_of(int dev) { return dev ? 0x170 : 0x1F0; }

static int ata_wait_not_bsy(uint16_t base) {
    uint8_t s;
    int tries = 0;
    do {
        s = inb(base + ATA_STATUS);
        if (++tries > 2000000) return -1;
    } while (s & 0x80);
    return (s & 0x01) ? -1 : 0;
}

static int ata_wait_drq(uint16_t base) {
    uint8_t s;
    int tries = 0;
    do {
        s = inb(base + ATA_STATUS);
        if (s & 0x01) return -1;
        if (++tries > 2000000) return -1;
    } while ((s & 0x88) != 0x08);
    return 0;
}

static int ata_rw(int dev, uint32_t lba, uint32_t count, void* buf, int write) {
    if (count == 0) return 0;
    uint16_t base = base_of(dev);
    if (ata_wait_not_bsy(base) != 0) return -1;
    outb(base + ATA_DRIVE, 0xE0 | ((lba >> 24) & 0x0F));
    outb(base + ATA_SECT_CNT, (uint8_t)count);
    outb(base + ATA_LBA_LOW,  (uint8_t)(lba & 0xFF));
    outb(base + ATA_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    outb(base + ATA_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));
    outb(base + ATA_CMD, (uint8_t)(write ? CMD_WRITE : CMD_READ));

    uint16_t* p = (uint16_t*)buf;
    for (uint32_t i = 0; i < count; i++) {
        if (ata_wait_drq(base) != 0) return -1;
        for (int j = 0; j < 256; j++) {
            if (write) outw(base + ATA_DATA, *p++);
            else *p++ = inw(base + ATA_DATA);
        }
    }
    return 0;
}

static int ata_read0(uint32_t lba, uint32_t count, void* buf) { return ata_rw(0, lba, count, buf, 0); }
static int ata_read1(uint32_t lba, uint32_t count, void* buf) { return ata_rw(1, lba, count, buf, 0); }
static int ata_write0(uint32_t lba, uint32_t count, const void* buf) { return ata_rw(0, lba, count, (void*)buf, 1); }
static int ata_write1(uint32_t lba, uint32_t count, const void* buf) { return ata_rw(1, lba, count, (void*)buf, 1); }

static int ata_probe_disk(int dev) {
    uint16_t base = base_of(dev);
    if (ata_wait_not_bsy(base) != 0) return -1;
    outb(base + ATA_DRIVE, 0xE0);
    outb(base + ATA_SECT_CNT, 0);
    outb(base + ATA_LBA_LOW, 0);
    outb(base + ATA_LBA_MID, 0);
    outb(base + ATA_LBA_HIGH, 0);
    outb(base + ATA_CMD, CMD_IDENT);
    uint8_t s = inb(base + ATA_STATUS);
    if (s == 0) return -1;
    if (ata_wait_drq(base) != 0) return -1;
    uint16_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = inw(base + ATA_DATA);
    uint32_t sectors = (uint32_t)buf[60] | ((uint32_t)buf[61] << 16);
    if (sectors == 0) {
        uint64_t s48 = (uint64_t)buf[100] | ((uint64_t)buf[101] << 16) |
                       ((uint64_t)buf[102] << 32) | ((uint64_t)buf[103] << 48);
        sectors = (uint32_t)(s48 & 0xFFFFFFFF);
    }
    ata_dev[dev].total_sectors = sectors;
    ata_dev[dev].read_sectors = dev ? ata_read1 : ata_read0;
    ata_dev[dev].write_sectors = dev ? ata_write1 : ata_write0;
    ata_dev[dev].name = dev ? "ata1" : "ata0";
    return 0;
}

int ata_register_driver(void) {
    if (ata_registered) return 0;
    ata_registered = 1;
    driver_register("ata-ide", 0x8086, 0x7010, 0);
    driver_register("ata-ide", 0x8086, 0x7111, 0);
    return 0;
}

int ata_init(void) {
    if (ata_ready) return 0;
    ata_register_driver();
    serial_write("[M1] ata_init: probing IDE primary/secondary\n");
    for (int dev = 0; dev < 2; dev++) {
        if (ata_probe_disk(dev) == 0) {
            ata_ready = 1;
            serial_write("      disk ");
            serial_write(ata_dev[dev].name);
            serial_write(" sectors=");
            serial_puti(ata_dev[dev].total_sectors);
            serial_write("\n");
        }
    }
    if (!ata_ready) serial_write("      no IDE disk found\n");
    return ata_ready ? 0 : -1;
}

int ata_is_ready(void) { return ata_ready; }

block_device_t* ata_get_bdev(int idx) {
    if (idx < 0 || idx >= 2) return 0;
    if (ata_dev[idx].total_sectors == 0) return 0;
    return &ata_dev[idx];
}
