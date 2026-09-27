#include "kernel.h"
#include "driver.h"
#include "pci.h"

static driver_t drivers[16];
static int driver_count = 0;

void driver_register(const char* name, uint16_t vendor, uint16_t device,
                     int (*probe)(uint8_t, uint8_t, uint8_t)) {
    if (driver_count >= 16) return;
    drivers[driver_count].name = name;
    drivers[driver_count].vendor = vendor;
    drivers[driver_count].device = device;
    drivers[driver_count].probe = probe;
    driver_count++;
    serial_write("[M1] driver registered: ");
    serial_write(name);
    serial_write("\n");
}

void driver_run_all(void) {
    serial_write("[M1] driver_run_all: matching ");
    serial_puti(pci_device_count);
    serial_write(" pci devices vs ");
    serial_puti(driver_count);
    serial_write(" drivers\n");
    for (int i = 0; i < pci_device_count; i++) {
        for (int j = 0; j < driver_count; j++) {
            if (drivers[j].vendor == pci_devices[i].vendor &&
                drivers[j].device == pci_devices[i].device) {
                serial_write("      probe ");
                serial_write(drivers[j].name);
                serial_write(" @ ");
                serial_puti(pci_devices[i].bus);
                serial_write(":");
                serial_puti(pci_devices[i].dev);
                serial_write(".");
                serial_puti(pci_devices[i].fn);
                serial_write("\n");
                if (drivers[j].probe)
                    drivers[j].probe(pci_devices[i].bus, pci_devices[i].dev, pci_devices[i].fn);
            }
        }
    }
}
