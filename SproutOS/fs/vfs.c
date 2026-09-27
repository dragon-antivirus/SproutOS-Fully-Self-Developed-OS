#include "kernel.h"
#include "vfs.h"
#include "fat32.h"

static uint32_t cwd_cluster = 2;
static char cwd_path[128] = "/";
static uint32_t cwd_stack[8];
static int cwd_depth = 0;
static int mounted = 0;

int vfs_mount(void) {
    block_device_t* b = ata_get_bdev(0);
    if (!b) {
        serial_write("[M1] vfs_mount: no block device\n");
        return -1;
    }
    if (fat32_mount(b) != 0) {
        serial_write("[M1] vfs_mount: fat32 mount failed\n");
        return -1;
    }
    cwd_cluster = fat32_root_cluster();
    cwd_path[0] = '/';
    cwd_path[1] = 0;
    cwd_depth = 0;
    mounted = 1;
    vfs_ensure_recycle();   /* 保证 RECYCLE 目录存在（删除文件会移入此处） */
    serial_write("[M1] vfs_mount: OK (FAT32 at /)\n");
    return 0;
}

int vfs_is_mounted(void) { return mounted; }

int vfs_list(vfs_dir_t* out) {
    if (!mounted) return -1;
    return fat32_list_dir(cwd_cluster, out);
}

int vfs_open(const char* name, vfs_dirent_t* out) {
    if (!mounted) return -1;
    return fat32_open_in_dir(cwd_cluster, name, out);
}

int vfs_read(const vfs_dirent_t* e, uint8_t* buf, uint32_t len, uint32_t* out_len) {
    if (!mounted) return -1;
    return fat32_read_file(e->first_cluster, buf, len, out_len);
}

int vfs_write_file(const char* name, const uint8_t* data, uint32_t len) {
    if (!mounted) return -1;
    return fat32_write_file(cwd_cluster, name, data, len);
}

int vfs_write_file_in_dir(uint32_t dir, const char* name, const uint8_t* data, uint32_t len) {
    if (!mounted) return -1;
    return fat32_write_file(dir, name, data, len);
}

uint32_t vfs_cwd_cluster(void) { return (mounted ? cwd_cluster : 0); }

int vfs_chdir(const char* name) {
    if (!mounted) return -1;
    vfs_dirent_t e;
    if (fat32_open_in_dir(cwd_cluster, name, &e) != 0) return -1;
    if (!e.is_dir) return -1;
    if (cwd_depth >= 8) return -1;
    cwd_stack[cwd_depth++] = cwd_cluster;
    cwd_cluster = e.first_cluster;
    int l = strlen(cwd_path);
    if (l > 0 && cwd_path[l - 1] != '/') cwd_path[l++] = '/';
    const char* n = name;
    while (*n && l < 127) cwd_path[l++] = *n++;
    cwd_path[l] = 0;
    return 0;
}

int vfs_chdir_up(void) {
    if (!mounted || cwd_depth <= 0) return -1;
    cwd_cluster = cwd_stack[--cwd_depth];
    int l = strlen(cwd_path);
    int i = l - 1;
    while (i > 0 && cwd_path[i] != '/') i--;
    if (i > 0) cwd_path[i] = 0;
    else { cwd_path[0] = '/'; cwd_path[1] = 0; }
    return 0;
}

const char* vfs_cwd_path(void) { return cwd_path; }

/* 删除当前目录下的文件（释放簇链 + 标记目录项空闲）。 */
int vfs_unlink(const char* name) {
    if (!mounted) return -1;
    return fat32_unlink(cwd_cluster, name);
}

int vfs_unlink_in_dir(uint32_t dir, const char* name) {
    if (!mounted) return -1;
    return fat32_unlink(dir, name);
}

/* 把 src_dir 下的 src_name 复制到 dst_dir 下的 dst_name（供右键"复制/剪切后粘贴"）。
 * 静态缓冲上限 256KB，超出部分截断；本系统的文件均较小，足够用。
 * 关键修复：剪贴板记录的是"源目录+文件名"，所以复制/粘贴支持跨目录
 * （之前只按当前 cwd 找源文件，从子目录复制后回到根目录粘贴会因找不到文件而静默失败）。 */
static uint8_t g_copy_buf[1 << 18];
int vfs_copy_file(const char* src_name, uint32_t src_dir, const char* dst_name, uint32_t dst_dir) {
    if (!mounted) return -1;
    if (src_dir < 2 || dst_dir < 2) return -1;
    vfs_dirent_t e;
    if (fat32_open_in_dir(src_dir, src_name, &e) != 0) {
        serial_write("[VFS] copy: source not found in src_dir\n");
        return -1;
    }
    uint32_t len = e.size;
    if (len > sizeof(g_copy_buf)) len = sizeof(g_copy_buf);
    uint32_t rl = 0;
    if (vfs_read(&e, g_copy_buf, len, &rl) != 0) {
        serial_write("[VFS] copy: read failed\n");
        return -1;
    }
    if (rl == 0) { serial_write("[VFS] copy: 0 bytes read\n"); return -1; }
    if (fat32_write_file(dst_dir, dst_name, g_copy_buf, rl) != 0) {
        serial_write("[VFS] copy: write failed\n");
        return -1;
    }
    serial_write("[VFS] copied ");
    serial_write(src_name);
    serial_write(" -> ");
    serial_write(dst_name);
    serial_write("\n");
    return 0;
}

/* 在 dir_cluster 下找一个不与已有项冲突的名字：基于 src 构造 base_c1/_c2…。
 * 结果写入 dst。返回 0。 */
static void vfs_unique_in_dir(uint32_t dir_cluster, const char* src, char* dst) {
    vfs_dirent_t e;
    if (fat32_open_in_dir(dir_cluster, src, &e) != 0) {   /* 原名可用 */
        int di = 0; while (src[di] && di < 63) { dst[di] = src[di]; di++; } dst[di] = 0;
        return;
    }
    const char* dot = 0;
    for (const char* p = src; *p; p++) if (*p == '.') dot = p;
    int base_len = dot ? (int)(dot - src) : (int)strlen(src);
    for (int n = 1; n <= 99; n++) {
        int di = 0;
        for (int i = 0; i < base_len && di < 60; i++) dst[di++] = src[i];
        dst[di++] = '_'; dst[di++] = 'c';
        if (n >= 10) dst[di++] = (char)('0' + n / 10);
        dst[di++] = (char)('0' + n % 10);
        if (dot) { for (const char* p = dot; *p && di < 63; ) dst[di++] = *p++; }
        dst[di] = 0;
        vfs_dirent_t e2;
        if (fat32_open_in_dir(dir_cluster, dst, &e2) != 0) return;  /* 找到空闲名 */
    }
    int di = 0; while (src[di] && di < 63) { dst[di] = src[di]; di++; } dst[di] = 0;
}

int vfs_mkdir(const char* name) {
    if (!mounted) return -1;
    return fat32_mkdir(cwd_cluster, name);
}

/* 回收站目录名（8.3，必须 ASCII，因为本 FS 不支持长文件名/中文目录名） */
#define RECYCLE_NAME "RECYCLE"

void vfs_ensure_recycle(void) {
    if (!mounted) return;
    vfs_dirent_t e;
    if (fat32_open_in_dir(fat32_root_cluster(), RECYCLE_NAME, &e) != 0)
        fat32_mkdir(fat32_root_cluster(), RECYCLE_NAME);
}

static uint32_t recycle_cluster(void) {
    vfs_dirent_t e;
    if (fat32_open_in_dir(fat32_root_cluster(), RECYCLE_NAME, &e) != 0) return 0;
    return e.first_cluster;
}

int vfs_recycle(const char* name) {
    if (!mounted) return -1;
    uint32_t rc = recycle_cluster();
    if (rc < 2) return -1;
    char dst[64];
    vfs_unique_in_dir(rc, name, dst);
    return fat32_move(cwd_cluster, name, rc, dst);
}

int vfs_restore(const char* name) {
    if (!mounted) return -1;
    uint32_t rc = recycle_cluster();
    if (rc < 2) return -1;
    char dst[64];
    vfs_unique_in_dir(cwd_cluster, name, dst);
    return fat32_move(rc, name, cwd_cluster, dst);
}

int vfs_purge(const char* name) {
    if (!mounted) return -1;
    uint32_t rc = recycle_cluster();
    if (rc < 2) return -1;
    return fat32_unlink(rc, name);
}

int vfs_list_recycle(vfs_dir_t* out) {
    out->count = 0;
    if (!mounted) return -1;
    uint32_t rc = recycle_cluster();
    if (rc < 2) return -1;
    return fat32_list_dir(rc, out);
}
