#include "kernel.h"
#include "vfs.h"
#include "fat32.h"

static fat32_fs_t fs;
static uint8_t g_secbuf[512];
static uint32_t g_sec_lba = 0xFFFFFFFF;
static int g_sec_valid = 0;
static uint8_t g_work[65536];

/* ====== LFN（VFAT 长文件名）支持 ======
 * FAT32 LFN 条目：attr=0x0F，每条存最多 13 个 UTF-16LE 字符。
 * 一组 LFN 条目紧跟在对应的短名(8.3)条目之前，序号递减（最后一条 bit6=1）。
 * 我们只关心 ASCII 范围的字符（取 UTF-16LE 低字节），非 ASCII 用 '?' 替代。
 * 最大支持 255 字符的 LFN 名。 */
#define MAX_LFN 256

/* 从目录原始数据中解码 entry[idx] 对应的 LFN 长名。
 * entries/count: 整个目录的 32 字节条目数组；idx: 目标短名条目的索引。
 * 返回 LFN 长度（>0 表示成功），名字写入 lfn_out（以 \0 结尾）。 */
static int fat32_decode_lfn(const uint8_t* entries, uint32_t count,
                             uint32_t idx, char* lfn_out) {
    if (idx == 0) return 0;
    const uint8_t* prev = entries + (idx - 1) * 32;
    if (prev[11] != 0x0F) return 0;     /* 前一条不是 LFN */

    /* 向前扫描找到 LFN 链起始位置（序号从 1 递增到 N，N 的 bit6=1） */
    uint32_t lfn_start = idx;
    int expect_seq = 1;
    while (lfn_start > 0) {
        uint32_t p = lfn_start - 1;
        const uint8_t* e = entries + p * 32;
        if (e[11] != 0x0F) break;
        int seq = e[0] & 0x3F;
        if (seq != expect_seq) { expect_seq = 1; break; }  /* 序号不连续，重置 */
        lfn_start = p;
        expect_seq++;
        if (expect_seq > 20) break;    /* 20×13=260 > 255 上限 */
    }

    if (lfn_start == idx) return 0;

    /* 从 lfn_start 到 idx-1 按序号升序拼接字符 */
    int len = 0;
    for (uint32_t i = lfn_start; i < idx && len < MAX_LFN - 1; i++) {
        const uint8_t* e = entries + i * 32;
        /* 字符 1-5：偏移 1,3,5,7,9 */
        for (int o = 1; o <= 9 && len < MAX_LFN - 1; o += 2) {
            unsigned ch = e[o] | ((unsigned)e[o+1] << 8);
            if (!ch || ch == 0xFFFF) goto lfn_end;
            lfn_out[len++] = (ch < 128) ? (char)ch : '?';
        }
        /* 字符 6-11：偏移 14,16,18,20,22,24 */
        for (int o = 14; o <= 24 && len < MAX_LFN - 1; o += 2) {
            unsigned ch = e[o] | ((unsigned)e[o+1] << 8);
            if (!ch || ch == 0xFFFF) goto lfn_end;
            lfn_out[len++] = (ch < 128) ? (char)ch : '?';
        }
        /* 字符 12-13：偏移 28,30 */
        for (int o = 28; o <= 30 && len < MAX_LFN - 1; o += 2) {
            unsigned ch = e[o] | ((unsigned)e[o+1] << 8);
            if (!ch || ch == 0xFFFF) goto lfn_end;
            lfn_out[len++] = (ch < 128) ? (char)ch : '?';
        }
    }
lfn_end:
    lfn_out[len] = '\0';
    return len;
}

static int read_sec(uint32_t lba, void* out) {
    if (g_sec_valid && g_sec_lba == lba) {
        memcpy(out, g_secbuf, 512);
        return 0;
    }
    if (!fs.bdev || fs.bdev->read_sectors(lba, 1, g_secbuf) != 0) {
        g_sec_valid = 0;
        return -1;
    }
    g_sec_lba = lba;
    g_sec_valid = 1;
    memcpy(out, g_secbuf, 512);
    return 0;
}

int fat32_mount(block_device_t* bdev) {
    fs.bdev = bdev;
    uint8_t b[512];
    if (read_sec(0, b) != 0) return -1;
    int is_fat = (b[82] == 'F' && b[83] == 'A' && b[84] == 'T') ||
                 (b[54] == 'F' && b[55] == 'A' && b[56] == 'T');
    if (!is_fat) {
        serial_write("[M1] not a FAT volume (got non-FAT boot sector)\n");
        return -1;
    }
    uint16_t bps = (uint16_t)(b[11] | (b[12] << 8));
    uint8_t spc = b[13];
    uint16_t res = (uint16_t)(b[14] | (b[15] << 8));
    uint8_t nf = b[16];
    uint32_t fsz = (uint32_t)b[36] | ((uint32_t)b[37] << 8) |
                   ((uint32_t)b[38] << 16) | ((uint32_t)b[39] << 24);
    uint32_t root = (uint32_t)b[44] | ((uint32_t)b[45] << 8) |
                    ((uint32_t)b[46] << 16) | ((uint32_t)b[47] << 24);
    if (bps == 0 || spc == 0) return -1;
    fs.bytes_per_sector = bps;
    fs.sectors_per_cluster = spc;
    fs.reserved_sectors = res;
    fs.num_fats = nf;
    fs.fat_size = fsz;
    fs.root_cluster = root ? root : 2;
    fs.fat_start = res;
    fs.data_start = (uint32_t)res + (uint32_t)nf * fsz;
    fs.valid = 1;
    serial_write("[M1] FAT32 mounted: bps=");
    serial_puti(bps);
    serial_write(" spc=");
    serial_puti(spc);
    serial_write(" fat_sectors=");
    serial_puti(fsz);
    serial_write(" root_cluster=");
    serial_puti(root);
    serial_write("\n");
    return 0;
}

uint32_t fat32_root_cluster(void) { return fs.root_cluster; }

static uint32_t fat32_get_entry(uint32_t fat_sec, uint32_t bo) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        uint32_t o = bo + i;
        uint8_t b[512];
        if (read_sec(fat_sec + o / fs.bytes_per_sector, b) != 0) return 0x0FFFFFFF;
        v |= ((uint32_t)b[o % fs.bytes_per_sector]) << (8 * i);
    }
    return v & 0x0FFFFFFF;
}

static uint32_t fat32_next_cluster(uint32_t cluster) {
    uint32_t off = cluster * 4;
    uint32_t fat_sec = fs.fat_start + off / fs.bytes_per_sector;
    return fat32_get_entry(fat_sec, off % fs.bytes_per_sector);
}

static uint32_t fat32_read_chain(uint32_t start, uint32_t max_bytes, uint8_t* out) {
    uint32_t total = 0;
    uint32_t c = start;
    while (total < max_bytes) {
        if (c < 2 || c >= 0x0FFFFFF7) break;
        uint32_t rem = max_bytes - total;
        uint32_t cluster_bytes = fs.sectors_per_cluster * fs.bytes_per_sector;
        uint32_t copy = (cluster_bytes < rem) ? cluster_bytes : rem;
        uint32_t secs = (copy + fs.bytes_per_sector - 1) / fs.bytes_per_sector;
        uint32_t lba = fs.data_start + (c - 2) * fs.sectors_per_cluster;
        /* 逐扇区读取并只拷贝不超过 max_bytes 的字节，避免最后一扇区
         * 多读的 512 字节越界写坏相邻堆内存（曾导致 TTF 缓冲后堆块头被踩）。 */
        for (uint32_t si = 0; si < secs; si++) {
            if (read_sec(lba + si, g_secbuf) != 0) break;
            uint32_t off = si * fs.bytes_per_sector;
            uint32_t chunk = fs.bytes_per_sector;
            if (total + off + chunk > max_bytes) chunk = max_bytes - total - off;
            if (chunk > 0) memcpy(out + total + off, g_secbuf, chunk);
        }
        total += copy;
        if (copy < cluster_bytes) break;
        c = fat32_next_cluster(c);
    }
    return total;
}

static void fat32_parse_name(const uint8_t* ent, char* out) {
    int o = 0;
    for (int i = 0; i < 8; i++) {
        char ch = ent[i];
        if (ch == ' ' || ch == 0) break;
        out[o++] = ch;
    }
    int eo = 0;
    char ext[4];
    for (int i = 8; i < 11; i++) {
        char ch = ent[i];
        if (ch == ' ' || ch == 0) break;
        ext[eo++] = ch;
    }
    if (eo > 0) {
        out[o++] = '.';
        for (int i = 0; i < eo; i++) out[o++] = ext[i];
    }
    out[o] = 0;
}

int fat32_list_dir(uint32_t cluster, vfs_dir_t* out) {
    out->count = 0;
    if (!fs.valid) return -1;
    uint32_t n = fat32_read_chain(cluster, sizeof(g_work), g_work);
    uint32_t entries = n / 32;
    for (uint32_t i = 0; i < entries && out->count < 48; i++) {
        uint8_t* e = g_work + i * 32;
        if (e[0] == 0) break;
        if (e[0] == 0xE5) continue;
        uint8_t attr = e[11];
        if (attr & 0x0F) continue;       /* 跳过 LFN 条目本身 */
        vfs_dirent_t* d = &out->entries[out->count];

        /* 优先用 LFN 长名显示（更友好），回退到 8.3 短名 */
        char lfn[MAX_LFN];
        int has_lfn = fat32_decode_lfn(g_work, entries, i, lfn);
        if (has_lfn > 0) {
            int j;
            for (j = 0; lfn[j] && j < (int)sizeof(d->name)-1; j++) d->name[j] = lfn[j];
            d->name[j] = 0;
        } else {
            fat32_parse_name(e, d->name);
        }

        if (d->name[0] == '.') {                    /* 跳过 "." 与 ".." */
            if (d->name[1] == 0) continue;
            if (d->name[1] == '.' && d->name[2] == 0) continue;
        }
        d->size = (uint32_t)e[28] | ((uint32_t)e[29] << 8) |
                  ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);
        d->first_cluster = ((uint32_t)e[20] << 16) | ((uint32_t)e[21] << 24) |
                           (uint32_t)e[26] | ((uint32_t)e[27] << 8);
        d->is_dir = (attr & 0x10) ? 1 : 0;
        out->count++;
    }
    return 0;
}

/* FAT32 文件名比较本应大小写不敏感；这里做大小写无关比较，
 * 避免剪贴板名（可能小写）与目录项（8.3 大写）匹配失败。 */
static int fat32_name_eq(const char* a, const char* b) {
    int i = 0;
    while (a[i] && b[i]) {
        char ca = a[i], cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb) return 0;
        i++;
    }
    return a[i] == b[i];   /* 同时到结尾才算相等 */
}

int fat32_open_in_dir(uint32_t cluster, const char* name, vfs_dirent_t* out) {
    if (!fs.valid) return -1;
    uint32_t n = fat32_read_chain(cluster, sizeof(g_work), g_work);
    uint32_t entries = n / 32;

    for (uint32_t i = 0; i < entries; i++) {
        uint8_t* e = g_work + i * 32;
        if (e[0] == 0) break;           /* 目录结尾 */
        if (e[0] == 0xE5) continue;      /* 已删除 */
        uint8_t attr = e[11];
        if (attr & 0x0F) continue;       /* 跳过 LFN 条目本身 */

        /* ---- 先尝试 8.3 短名匹配 ---- */
        char short_name[13];
        fat32_parse_name(e, short_name);
        if (fat32_name_eq(short_name, name)) {
            out->first_cluster = ((uint32_t)e[20] << 16) | ((uint32_t)e[21] << 24) |
                                 (uint32_t)e[26] | ((uint32_t)e[27] << 8);
            out->size = (uint32_t)e[28] | ((uint32_t)e[29] << 8) |
                       ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);
            out->is_dir = (attr & 0x10) ? 1 : 0;
            /* 列表显示仍用短名 */
            { int j; for (j = 0; short_name[j]; j++) out->name[j] = short_name[j]; out->name[j]=0; }
            return 0;
        }

        /* ---- 再尝试 LFN 长名匹配 ---- */
        char lfn[MAX_LFN];
        if (fat32_decode_lfn(g_work, entries, i, lfn) > 0) {
            if (fat32_name_eq(lfn, name)) {
                out->first_cluster = ((uint32_t)e[20] << 16) | ((uint32_t)e[21] << 24) |
                                     (uint32_t)e[26] | ((uint32_t)e[27] << 8);
                out->size = (uint32_t)e[28] | ((uint32_t)e[29] << 8) |
                           ((uint32_t)e[30] << 16) | ((uint32_t)e[31] << 24);
                out->is_dir = (attr & 0x10) ? 1 : 0;
                /* 列表显示用长名（更友好） */
                { int j; for (j = 0; lfn[j] && j < (int)sizeof(out->name)-1; j++) out->name[j] = lfn[j]; out->name[j]=0; }
                return 0;
            }
        }
    }
    return -1;
}

int fat32_read_file(uint32_t first_cluster, uint8_t* buf, uint32_t len, uint32_t* out_len) {
    if (!fs.valid || first_cluster < 2) { *out_len = 0; return -1; }
    uint32_t n = fat32_read_chain(first_cluster, len, buf);
    *out_len = n;
    return 0;
}

/* ===================== 写支持（供 Editor 保存文件用） ===================== */

static int write_sec(uint32_t lba, const void* in) {
    if (!fs.bdev || fs.bdev->write_sectors(lba, 1, in) != 0) return -1;
    if (g_sec_valid && g_sec_lba == lba) memcpy(g_secbuf, in, 512);
    return 0;
}

/* 把一个 FAT 项写成 value（写入所有 FAT 副本，保证镜像一致）。 */
static int fat32_set_fat(uint32_t cluster, uint32_t value) {
    if (cluster < 2) return -1;
    uint32_t off = cluster * 4;
    uint32_t sector = off / fs.bytes_per_sector;
    uint32_t bo = off % fs.bytes_per_sector;
    for (uint32_t copy = 0; copy < fs.num_fats; copy++) {
        uint32_t fat_sec = fs.fat_start + (uint32_t)copy * fs.fat_size + sector;
        uint8_t b[512];
        if (read_sec(fat_sec, b) != 0) return -1;
        b[bo]     = value & 0xFF;
        b[bo + 1] = (value >> 8)  & 0xFF;
        b[bo + 2] = (value >> 16) & 0xFF;
        b[bo + 3] = (value >> 24) & 0xFF;
        if (write_sec(fat_sec, b) != 0) return -1;
    }
    return 0;
}

static uint32_t g_alloc_hint = 2;

/* 从 g_alloc_hint 往后找一个空闲簇（FAT 项为 0），找不到则回绕从 2 找。 */
static uint32_t fat32_alloc_cluster(void) {
    uint32_t maxc = fs.fat_size * (fs.bytes_per_sector / 4);
    for (uint32_t pass = 0; pass < 2; pass++) {
        uint32_t start = (pass == 0) ? g_alloc_hint : 2;
        uint32_t end   = (pass == 0) ? maxc : g_alloc_hint;
        for (uint32_t c = start; c < end && c < 0x0FFFFFF7; c++) {
            if (fat32_next_cluster(c) == 0) {
                g_alloc_hint = c + 1;
                fat32_set_fat(c, 0x0FFFFFFF);
                return c;
            }
        }
    }
    return 0;
}

/* 释放整条簇链（把每个 FAT 项清零）。 */
static void fat32_free_chain(uint32_t start) {
    uint32_t c = start;
    while (c >= 2 && c < 0x0FFFFFF7) {
        uint32_t nxt = fat32_next_cluster(c);
        fat32_set_fat(c, 0);
        c = nxt;
    }
}

/* 把数据写入一条新分配的簇链，返回首簇。 */
static int fat32_write_data(const uint8_t* buf, uint32_t len, uint32_t* out_first) {
    if (len == 0) { *out_first = 0; return 0; }
    uint32_t cluster_bytes = fs.sectors_per_cluster * fs.bytes_per_sector;
    uint32_t first = 0, prev = 0, written = 0;
    while (written < len) {
        uint32_t cl = fat32_alloc_cluster();
        if (cl == 0) { if (first) fat32_free_chain(first); return -1; }
        if (first == 0) first = cl; else fat32_set_fat(prev, cl);
        prev = cl;
        uint32_t copy = (len - written < cluster_bytes) ? (len - written) : cluster_bytes;
        uint32_t secs = (copy + fs.bytes_per_sector - 1) / fs.bytes_per_sector;
        uint32_t lba = fs.data_start + (cl - 2) * fs.sectors_per_cluster;
        if (fs.bdev->write_sectors(lba, secs, buf + written) != 0) {
            fat32_free_chain(first); return -1;
        }
        written += copy;
    }
    *out_first = first;
    return 0;
}

/* 把文件名转成 11 字节 8.3 格式（大写、空格填充）。 */
static void make_83(const char* name, uint8_t* dest) {
    for (int i = 0; i < 11; i++) dest[i] = ' ';
    const char* dot = 0;
    for (const char* p = name; *p; p++) if (*p == '.') dot = p;
    int baselen = dot ? (int)(dot - name) : (int)strlen(name);
    int ni = 0;
    for (int i = 0; i < baselen && i < 8; i++) {
        char ch = name[i];
        if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
        dest[ni++] = ch;
    }
    if (dot) {
        const char* ext = dot + 1;
        int ei = 0;
        for (int i = 0; ext[i] && ei < 3; i++) {
            char ch = ext[i];
            if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
            dest[8 + ei++] = ch;
        }
    }
}

/* 在目录 dir_cluster 下创建或覆盖名为 name 的文件，写入 data/len。
 * 返回 0 成功，-1 失败（目录满/无空闲簇/未挂载）。 */
int fat32_write_file(uint32_t dir_cluster, const char* name, const uint8_t* data, uint32_t len) {
    if (!fs.valid || dir_cluster < 2) return -1;

    uint32_t n = fat32_read_chain(dir_cluster, sizeof(g_work), g_work);
    uint32_t entries = n / 32;

    uint8_t name83[11]; make_83(name, name83);

    int slot = -1, found = 0;
    for (uint32_t i = 0; i < entries; i++) {
        uint8_t* e = g_work + i * 32;
        if (e[0] == 0) { slot = (int)i; found = 0; break; }      /* 已到目录结尾 */
        if (e[0] == 0xE5) { if (slot < 0) slot = (int)i; continue; } /* 已删除槽 */
        if ((e[11] & 0x0F) == 0) {
            int match = 1;
            for (int k = 0; k < 11; k++) if (e[k] != name83[k]) { match = 0; break; }
            if (match) { slot = (int)i; found = 1; break; }
        }
    }
    if (slot < 0) return -1;   /* 目录满，拒绝写入（避免越界分配新目录簇） */

    if (found) {
        uint32_t old_first = ((uint32_t)g_work[slot*32+20] << 16) |
                             ((uint32_t)g_work[slot*32+21] << 24) |
                             (uint32_t)g_work[slot*32+26] |
                             ((uint32_t)g_work[slot*32+27] << 8);
        if (old_first >= 2) fat32_free_chain(old_first);
    }

    uint32_t first = 0;
    if (fat32_write_data(data, len, &first) != 0) return -1;

    uint8_t* e = g_work + slot * 32;
    for (int i = 0; i < 32; i++) e[i] = 0;
    memcpy(e, name83, 11);
    e[11] = 0x20;                 /* 归档位（普通文件） */
    e[14] = 0; e[15] = 0;         /* 时间 */
    e[16] = 0x02; e[17] = 0x5D;   /* 日期 2026-08-02 ((2026-1980)<<9)|(8<<5)|2 */
    e[18] = 0; e[19] = 0;
    e[20] = (first >> 16) & 0xFF; e[21] = (first >> 24) & 0xFF;
    e[26] = first & 0xFF;         e[27] = (first >> 8) & 0xFF;
    e[28] = len & 0xFF; e[29] = (len >> 8) & 0xFF; e[30] = (len >> 16) & 0xFF; e[31] = (len >> 24) & 0xFF;

    /* 把目录改动写回磁盘（按簇链逐簇回写，正确支持多簇目录）。 */
    uint32_t c = dir_cluster, offset = 0;
    uint32_t need_bytes = (uint32_t)(slot + 1) * 32;
    while (c >= 2 && c < 0x0FFFFFF7 && offset < need_bytes) {
        uint32_t lba = fs.data_start + (c - 2) * fs.sectors_per_cluster;
        uint32_t cluster_bytes = fs.sectors_per_cluster * fs.bytes_per_sector;
        uint32_t copy = (offset + cluster_bytes < need_bytes) ? cluster_bytes : (need_bytes - offset);
        uint32_t secs = (copy + 511) / 512;
        if (fs.bdev->write_sectors(lba, secs, g_work + offset) != 0) return -1;
        offset += cluster_bytes;
        c = fat32_next_cluster(c);
    }
    serial_write("[FAT32] wrote file ");
    serial_write(name);
    serial_write(" first=");
    serial_puti(first);
    serial_write(" size=");
    serial_puti(len);
    serial_write("\n");
    return 0;
}

/* 删除目录项 name：标记 0xE5 + 释放数据簇链 + 回写目录。 */
int fat32_unlink(uint32_t dir_cluster, const char* name) {
    if (!fs.valid || dir_cluster < 2) return -1;
    uint32_t n = fat32_read_chain(dir_cluster, sizeof(g_work), g_work);
    uint32_t entries = n / 32;
    uint8_t name83[11]; make_83(name, name83);
    int slot = -1;
    for (uint32_t i = 0; i < entries; i++) {
        uint8_t* e = g_work + i * 32;
        if (e[0] == 0 || e[0] == 0xE5) continue;
        if ((e[11] & 0x0F) == 0) {
            int match = 1;
            for (int k = 0; k < 11; k++) if (e[k] != name83[k]) { match = 0; break; }
            if (match) { slot = (int)i; break; }
        }
    }
    if (slot < 0) return -1;
    uint32_t first = ((uint32_t)g_work[slot*32+20] << 16) |
                     ((uint32_t)g_work[slot*32+21] << 24) |
                     (uint32_t)g_work[slot*32+26] |
                     ((uint32_t)g_work[slot*32+27] << 8);
    if (first >= 2) fat32_free_chain(first);
    g_work[slot * 32] = 0xE5;   /* 标记为空闲（已删除） */
    /* 把目录改动写回磁盘（与 fat32_write_file 同一套逐簇回写逻辑） */
    uint32_t c = dir_cluster, offset = 0;
    uint32_t need_bytes = (uint32_t)(slot + 1) * 32;
    while (c >= 2 && c < 0x0FFFFFF7 && offset < need_bytes) {
        uint32_t lba = fs.data_start + (c - 2) * fs.sectors_per_cluster;
        uint32_t cluster_bytes = fs.sectors_per_cluster * fs.bytes_per_sector;
        uint32_t copy = (offset + cluster_bytes < need_bytes) ? cluster_bytes : (need_bytes - offset);
        uint32_t secs = (copy + 511) / 512;
        if (fs.bdev->write_sectors(lba, secs, g_work + offset) != 0) return -1;
        offset += cluster_bytes;
        c = fat32_next_cluster(c);
    }
    serial_write("[FAT32] unlinked ");
    serial_write(name);
    serial_write("\n");
    return 0;
}

/* 在目录 dir_cluster 下创建子目录 name（8.3）。分配一个数据簇作新目录的首簇，
 * 并初始化 "." 与 ".." 两项，写回父目录。返回 0 成功，-1 失败。 */
int fat32_mkdir(uint32_t dir_cluster, const char* name) {
    if (!fs.valid || dir_cluster < 2) return -1;
    uint8_t name83[11]; make_83(name, name83);

    /* 已存在同名则跳过（避免重复创建） */
    vfs_dirent_t existing;
    if (fat32_open_in_dir(dir_cluster, name, &existing) == 0) return 0;

    uint32_t first = fat32_alloc_cluster();   /* 分配一个新簇作新目录首簇（标记为 EOC） */
    if (first < 2) return -1;

    /* 把新目录首簇清零（fat32_write_data 已写过，但保险起见再清一次，避免残留 ".." 链） */
    {
        uint32_t cluster_bytes = fs.sectors_per_cluster * fs.bytes_per_sector;
        uint8_t* zero = (uint8_t*)g_work + (sizeof(g_work) - cluster_bytes);
        for (uint32_t i = 0; i < cluster_bytes; i++) zero[i] = 0;
        uint32_t lba = fs.data_start + (first - 2) * fs.sectors_per_cluster;
        uint32_t secs = (cluster_bytes + 511) / 512;
        if (fs.bdev->write_sectors(lba, secs, zero) != 0) return -1;
    }

    /* 写 "." 与 ".." 两项（各 32 字节，属性 0x10 目录） */
    {
        uint8_t dot[32]; for (int i = 0; i < 32; i++) dot[i] = 0;
        dot[0] = '.'; dot[11] = 0x10;
        dot[20] = (first >> 16) & 0xFF; dot[21] = (first >> 24) & 0xFF;
        dot[26] = first & 0xFF; dot[27] = (first >> 8) & 0xFF;
        uint8_t dotdot[32]; for (int i = 0; i < 32; i++) dotdot[i] = 0;
        dotdot[0] = '.'; dotdot[1] = '.'; dotdot[11] = 0x10;
        uint32_t parent = (dir_cluster == fs.root_cluster) ? 0 : dir_cluster;
        dotdot[20] = (parent >> 16) & 0xFF; dotdot[21] = (parent >> 24) & 0xFF;
        dotdot[26] = parent & 0xFF; dotdot[27] = (parent >> 8) & 0xFF;
        uint8_t blk[64]; memcpy(blk, dot, 32); memcpy(blk + 32, dotdot, 32);
        uint32_t lba = fs.data_start + (first - 2) * fs.sectors_per_cluster;
        if (fs.bdev->write_sectors(lba, 1, blk) != 0) return -1;
    }

    /* 在父目录里新建目录项 */
    uint32_t n = fat32_read_chain(dir_cluster, sizeof(g_work), g_work);
    uint32_t entries = n / 32;
    int slot = -1;
    for (uint32_t i = 0; i < entries; i++) {
        uint8_t* e = g_work + i * 32;
        if (e[0] == 0) { slot = (int)i; break; }
        if (e[0] == 0xE5) { if (slot < 0) slot = (int)i; }
    }
    if (slot < 0) { fat32_free_chain(first); return -1; }

    uint8_t* e = g_work + slot * 32;
    for (int i = 0; i < 32; i++) e[i] = 0;
    memcpy(e, name83, 11);
    e[11] = 0x10;                 /* 目录属性 */
    e[14] = 0; e[15] = 0;
    e[16] = 0x02; e[17] = 0x5D;   /* 日期 2026-08-02 */
    e[18] = 0; e[19] = 0;
    e[20] = (first >> 16) & 0xFF; e[21] = (first >> 24) & 0xFF;
    e[26] = first & 0xFF;         e[27] = (first >> 8) & 0xFF;
    e[28] = 0; e[29] = 0; e[30] = 0; e[31] = 0;

    uint32_t c = dir_cluster, offset = 0;
    uint32_t need_bytes = (uint32_t)(slot + 1) * 32;
    while (c >= 2 && c < 0x0FFFFFF7 && offset < need_bytes) {
        uint32_t lba = fs.data_start + (c - 2) * fs.sectors_per_cluster;
        uint32_t cluster_bytes = fs.sectors_per_cluster * fs.bytes_per_sector;
        uint32_t copy = (offset + cluster_bytes < need_bytes) ? cluster_bytes : (need_bytes - offset);
        uint32_t secs = (copy + 511) / 512;
        if (fs.bdev->write_sectors(lba, secs, g_work + offset) != 0) return -1;
        offset += cluster_bytes;
        c = fat32_next_cluster(c);
    }
    serial_write("[FAT32] mkdir ");
    serial_write(name);
    serial_write("\n");
    return 0;
}

/* 把 src_dir 下的 src_name 移动（重命名/跨目录）到 dst_dir 下的 dst_name。
 * 仅搬运目录项，不动数据簇，效率高。返回 0 成功，-1 失败。 */
int fat32_move(uint32_t src_dir, const char* src_name,
               uint32_t dst_dir, const char* dst_name) {
    if (!fs.valid || src_dir < 2 || dst_dir < 2) return -1;
    uint8_t sname83[11]; make_83(src_name, sname83);
    uint8_t dname83[11]; make_83(dst_name, dname83);

    /* 1) 在源目录找到源项，暂存其 32 字节，并标记 0xE5 */
    uint32_t sn = fat32_read_chain(src_dir, sizeof(g_work), g_work);
    uint32_t sentries = sn / 32;
    int sslot = -1;
    uint8_t saved[32];
    for (uint32_t i = 0; i < sentries; i++) {
        uint8_t* e = g_work + i * 32;
        if (e[0] == 0 || e[0] == 0xE5) continue;
        if ((e[11] & 0x0F) == 0) {
            int match = 1;
            for (int k = 0; k < 11; k++) if (e[k] != sname83[k]) { match = 0; break; }
            if (match) { sslot = (int)i; memcpy(saved, e, 32); break; }
        }
    }
    if (sslot < 0) return -1;
    g_work[sslot * 32] = 0xE5;
    {
        uint32_t c = src_dir, offset = 0;
        uint32_t need_bytes = (uint32_t)(sslot + 1) * 32;
        while (c >= 2 && c < 0x0FFFFFF7 && offset < need_bytes) {
            uint32_t lba = fs.data_start + (c - 2) * fs.sectors_per_cluster;
            uint32_t cluster_bytes = fs.sectors_per_cluster * fs.bytes_per_sector;
            uint32_t copy = (offset + cluster_bytes < need_bytes) ? cluster_bytes : (need_bytes - offset);
            uint32_t secs = (copy + 511) / 512;
            if (fs.bdev->write_sectors(lba, secs, g_work + offset) != 0) return -1;
            offset += cluster_bytes;
            c = fat32_next_cluster(c);
        }
    }

    /* 2) 在目标目录找空槽，写入暂存项（改名） */
    uint32_t dn = fat32_read_chain(dst_dir, sizeof(g_work), g_work);
    uint32_t dentries = dn / 32;
    int dslot = -1, dfound = 0;
    for (uint32_t i = 0; i < dentries; i++) {
        uint8_t* e = g_work + i * 32;
        if (e[0] == 0) { dslot = (int)i; dfound = 0; break; }
        if (e[0] == 0xE5) { if (dslot < 0) dslot = (int)i; continue; }
        if ((e[11] & 0x0F) == 0) {
            int match = 1;
            for (int k = 0; k < 11; k++) if (e[k] != dname83[k]) { match = 0; break; }
            if (match) { dslot = (int)i; dfound = 1; break; }
        }
    }
    if (dslot < 0) return -1;
    if (dfound) {
        uint32_t old_first = ((uint32_t)g_work[dslot*32+20] << 16) |
                             ((uint32_t)g_work[dslot*32+21] << 24) |
                             (uint32_t)g_work[dslot*32+26] |
                             ((uint32_t)g_work[dslot*32+27] << 8);
        if (old_first >= 2) fat32_free_chain(old_first);
    }
    uint8_t* e = g_work + dslot * 32;
    memcpy(e, saved, 32);
    memcpy(e, dname83, 11);   /* 用新名覆盖 8.3 名 */
    {
        uint32_t c = dst_dir, offset = 0;
        uint32_t need_bytes = (uint32_t)(dslot + 1) * 32;
        while (c >= 2 && c < 0x0FFFFFF7 && offset < need_bytes) {
            uint32_t lba = fs.data_start + (c - 2) * fs.sectors_per_cluster;
            uint32_t cluster_bytes = fs.sectors_per_cluster * fs.bytes_per_sector;
            uint32_t copy = (offset + cluster_bytes < need_bytes) ? cluster_bytes : (need_bytes - offset);
            uint32_t secs = (copy + 511) / 512;
            if (fs.bdev->write_sectors(lba, secs, g_work + offset) != 0) return -1;
            offset += cluster_bytes;
            c = fat32_next_cluster(c);
        }
    }
    serial_write("[FAT32] moved ");
    serial_write(src_name);
    serial_write(" -> ");
    serial_write(dst_name);
    serial_write("\n");
    return 0;
}
