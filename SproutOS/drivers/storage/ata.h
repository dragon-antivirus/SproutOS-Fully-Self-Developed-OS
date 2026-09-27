#ifndef ATA_H
#define ATA_H

#include "kernel.h"

typedef struct block_device {
    const char* name;
    uint32_t total_sectors;
    int (*read_sectors)(uint32_t lba, uint32_t count, void* buf);
    int (*write_sectors)(uint32_t lba, uint32_t count, const void* buf);
} block_device_t;

block_device_t* ata_get_bdev(int idx);
int ata_init(void);
int ata_is_ready(void);
int ata_register_driver(void);

#endif
