#ifndef FAT32_H
#define FAT32_H

#include "kernel.h"
#include "vfs.h"

typedef struct {
    block_device_t* bdev;
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t num_fats;
    uint32_t fat_size;
    uint32_t root_cluster;
    uint32_t fat_start;
    uint32_t data_start;
    uint8_t valid;
} fat32_fs_t;

int fat32_mount(block_device_t* bdev);
uint32_t fat32_root_cluster(void);
int fat32_list_dir(uint32_t cluster, vfs_dir_t* out);
int fat32_open_in_dir(uint32_t cluster, const char* name, vfs_dirent_t* out);
int fat32_read_file(uint32_t first_cluster, uint8_t* buf, uint32_t len, uint32_t* out_len);
int fat32_write_file(uint32_t dir_cluster, const char* name, const uint8_t* data, uint32_t len);
int fat32_unlink(uint32_t dir_cluster, const char* name);
int fat32_mkdir(uint32_t dir_cluster, const char* name);
int fat32_move(uint32_t src_dir, const char* src_name,
               uint32_t dst_dir, const char* dst_name);

#endif
