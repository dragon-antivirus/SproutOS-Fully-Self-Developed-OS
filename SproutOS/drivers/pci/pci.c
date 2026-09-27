#include "kernel.h"
#include "pci.h"
#include "driver.h"

pci_device_t pci_devices[64];
int pci_device_count = 0;

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) |
                    ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC);
    outl(PCI_CONFIG_ADDRESS, addr);
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    return (uint16_t)(pci_config_read32(bus, dev, fn, off) & 0xFFFF);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off) {
    return (uint8_t)(pci_config_read32(bus, dev, fn, off) & 0xFF);
}

void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t val) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) |
                    ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC);
    outl(PCI_CONFIG_ADDRESS, addr);
    outl(PCI_CONFIG_DATA, val);
}

void pci_config_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint16_t val) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) |
                    ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC);
    outl(PCI_CONFIG_ADDRESS, addr);
    outw(PCI_CONFIG_DATA, val);
}

void pci_config_write8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint8_t val) {
    uint32_t addr = 0x80000000u | ((uint32_t)bus << 16) |
                    ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (off & 0xFC);
    outl(PCI_CONFIG_ADDRESS, addr);
    outb(PCI_CONFIG_DATA, val);
}

const char* pci_class_name(uint8_t cls) {
    switch (cls) {
        case 0x00: return "legacy";
        case 0x01: return "storage";
        case 0x02: return "network";
        case 0x03: return "display";
        case 0x04: return "multimedia";
        case 0x06: return "bridge";
        case 0x0C: return "serial";
        default:    return "unknown";
    }
}

static void pci_add(uint8_t bus, uint8_t dev, uint8_t fn) {
    uint32_t v = pci_config_read32(bus, dev, fn, 0);
    uint16_t vendor = (uint16_t)(v & 0xFFFF);
    if (vendor == 0xFFFF) return;
    uint16_t device = (uint16_t)((v >> 16) & 0xFFFF);
    uint32_t c = pci_config_read32(bus, dev, fn, 8);
    uint8_t class_code = (uint8_t)((c >> 24) & 0xFF);
    uint8_t subclass = (uint8_t)((c >> 16) & 0xFF);
    uint8_t prog_if = (uint8_t)((c >> 8) & 0xFF);

    if (pci_device_count < 64) {
        pci_device_t* p = &pci_devices[pci_device_count++];
        p->bus = bus; p->dev = dev; p->fn = fn;
        p->vendor = vendor; p->device = device;
        p->class_code = class_code; p->subclass = subclass; p->prog_if = prog_if;
    }

    serial_write("      ");
    serial_puth(vendor); serial_write(":");
    serial_puth(device); serial_write("  cls=");
    serial_puti(class_code); serial_write("/"); serial_puti(subclass);
    serial_write(" ("); serial_write(pci_class_name(class_code));
    serial_write(") @ ");
    serial_puti(bus); serial_write(":");
    serial_puti(dev); serial_write("."); serial_puti(fn);
    serial_write("\n");
}

void pci_init(void) {
    serial_write("[M1] pci_init: scanning bus 0..7\n");
    for (uint8_t bus = 0; bus < 8; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint32_t v = pci_config_read32(bus, dev, 0, 0);
            if ((v & 0xFFFF) == 0xFFFF) continue;
            uint8_t hdr = pci_config_read8(bus, dev, 0, 0x0E);
            int multi = hdr & 0x80;
            pci_add(bus, dev, 0);
            if (multi) {
                for (uint8_t fn = 1; fn < 8; fn++) {
                    uint32_t fv = pci_config_read32(bus, dev, fn, 0);
                    if ((fv & 0xFFFF) != 0xFFFF) pci_add(bus, dev, fn);
                }
            }
        }
    }
    serial_write("[M1] pci_init: found ");
    serial_puti(pci_device_count);
    serial_write(" device(s)\n");
}
