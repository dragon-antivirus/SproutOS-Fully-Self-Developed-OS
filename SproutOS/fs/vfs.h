#ifndef VFS_H
#define VFS_H

#include "kernel.h"
#include "ata.h"

typedef struct {
    char name[13];
    uint32_t size;
    uint32_t first_cluster;
    uint8_t is_dir;
} vfs_dirent_t;

typedef struct {
    vfs_dirent_t entries[48];
    int count;
} vfs_dir_t;

int vfs_mount(void);
int vfs_is_mounted(void);
int vfs_list(vfs_dir_t* out);
int vfs_open(const char* name, vfs_dirent_t* out);
int vfs_read(const vfs_dirent_t* e, uint8_t* buf, uint32_t len, uint32_t* out_len);
int vfs_write_file(const char* name, const uint8_t* data, uint32_t len);
int vfs_write_file_in_dir(uint32_t dir, const char* name, const uint8_t* data, uint32_t len);
uint32_t vfs_cwd_cluster(void);   /* 当前目录首簇（剪贴板记录源目录用） */
int vfs_chdir(const char* name);
int vfs_chdir_up(void);
const char* vfs_cwd_path(void);
int vfs_unlink(const char* name);
int vfs_unlink_in_dir(uint32_t dir, const char* name);
/* 复制：src_name 位于 src_dir，写到 dst_dir 下的 dst_name（支持跨目录复制） */
int vfs_copy_file(const char* src_name, uint32_t src_dir, const char* dst_name, uint32_t dst_dir);
int vfs_mkdir(const char* name);
int vfs_recycle(const char* name);     /* 把当前目录文件移入 RECYCLE 目录（删除的“软”实现） */
int vfs_restore(const char* name);    /* 从 RECYCLE 还原回当前目录 */
int vfs_purge(const char* name);      /* 在 RECYCLE 中永久删除 */
int vfs_list_recycle(vfs_dir_t* out); /* 列出回收站内容 */
void vfs_ensure_recycle(void);        /* 挂载后保证 RECYCLE 目录存在 */

#endif
