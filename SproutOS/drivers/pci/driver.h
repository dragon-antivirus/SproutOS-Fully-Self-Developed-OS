#ifndef DRIVER_H
#define DRIVER_H

#include "kernel.h"

typedef struct driver {
    const char* name;
    uint16_t vendor;
    uint16_t device;
    int (*probe)(uint8_t bus, uint8_t dev, uint8_t fn);
} driver_t;

void driver_register(const char* name, uint16_t vendor, uint16_t device,
                     int (*probe)(uint8_t, uint8_t, uint8_t));
void driver_run_all(void);

#endif
