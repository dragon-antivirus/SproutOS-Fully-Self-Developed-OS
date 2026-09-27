#ifndef PCI_H
#define PCI_H

#include "kernel.h"

typedef struct {
    uint8_t bus, dev, fn;
    uint16_t vendor;
    uint16_t device;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
} pci_device_t;

extern pci_device_t pci_devices[64];
extern int pci_device_count;

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);
uint8_t  pci_config_read8(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off);

/* Config-space writes (needed to enable bus master / memory space and to set BARs). */
void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint32_t val);
void pci_config_write16(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint16_t val);
void pci_config_write8 (uint8_t bus, uint8_t dev, uint8_t fn, uint8_t off, uint8_t val);

void pci_init(void);
const char* pci_class_name(uint8_t cls);

#endif
