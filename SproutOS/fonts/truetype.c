/* 运行时 TTF 中文渲染模块。
 * 依赖 stb_truetype.h（单头文件，MIT，freestanding 友好）。
 * 旧的中文点阵回退表（cn_fallback_map.h / cn_font_16x16）已删除，缺字由 vga.c 画空框。
 *
 * 关键点：stb_truetype 默认会 #include <math.h>/<stdlib.h>/<string.h>/<assert.h>，
 * 但那些外部依赖都包在 `#ifndef STBTT_*` 里；我们在包含实现之前用自己的
 * freestanding 实现（kmalloc/kfree + 自写 floor/ceil/sqrt/...）把它们全部重定义，
 * 从而完全不依赖宿主 C 运行库。 */

#include "kernel.h"

/* ---------------- freestanding 数学/内存 shim ---------------- */
static int stbtt_ifloor_impl(float x) { int i = (int)x; return (float)i > x ? i - 1 : i; }
static int stbtt_iceil_impl(float x)  { return -stbtt_ifloor_impl(-x); }
static float stbtt_fabs_impl(float x) { return x < 0 ? -x : x; }
static float stbtt_sqrt_impl(float x) {
    if (x <= 0.0f) return 0.0f;
    float g = x * 0.5f;
    for (int i = 0; i < 24; i++) {
        float g2 = 0.5f * (g + x / g);
        if (g2 == g) break;
        g = g2;
    }
    return g;
}
/* 以下三个仅被 SDF 路径引用（本系统不使用 SDF），仅保证可链接，行为无关紧要。 */
static float stbtt_pow_impl(float x, float y)  { (void)x; (void)y; return 0.0f; }
static float stbtt_fmod_impl(float x, float y) { (void)y; return x; }
static float stbtt_cos_impl(float x)           { (void)x; return 0.0f; }
static float stbtt_acos_impl(float x)          { (void)x; return 0.0f; }

#define STBTT_ifloor(x)   stbtt_ifloor_impl(x)
#define STBTT_iceil(x)    stbtt_iceil_impl(x)
#define STBTT_sqrt(x)     stbtt_sqrt_impl(x)
#define STBTT_pow(x,y)    stbtt_pow_impl(x,y)
#define STBTT_fmod(x,y)   stbtt_fmod_impl(x,y)
#define STBTT_cos(x)      stbtt_cos_impl(x)
#define STBTT_acos(x)     stbtt_acos_impl(x)
#define STBTT_fabs(x)     stbtt_fabs_impl(x)
#define STBTT_malloc(x,u) kmalloc((uint32_t)(x))
#define STBTT_free(x,u)   kfree(x)
#define STBTT_assert(x)
#define STBTT_strlen(x)   strlen(x)
#define STBTT_memcpy      memcpy
#define STBTT_memset      memset

/* stb_truetype.h 需要 size_t / NULL（通常来自 <stddef.h>），但本工程用
 * -ffreestanding -nostdlib 编译，不含该头。这里手动提供，与 freestanding 兼容；
 * 若其它头已定义则跳过，避免重定义。32 位下 size_t == unsigned int。 */
#ifndef NULL
#define NULL ((void*)0)
#endif
#ifndef __SIZE_T_DEFINED
#define __SIZE_T_DEFINED
typedef unsigned int size_t;
#endif

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"


/* ---------------- 状态 ---------------- */
static int           g_ready = 0;
static uint8_t*      g_font_buf = 0;   /* 字体文件缓冲，常驻（stb 只保存指针） */
static stbtt_fontinfo g_info;
static uint32_t      g_font_cksum = 0;  /* 加载时计算的简单校验和，用于检测堆 corruption */

/* 字形缓存：直接映射，命中即返回，避免重复栅格化。 */
#define TTF_CACHE_BITS 9
#define TTF_CACHE_N    (1u << TTF_CACHE_BITS)
static uint8_t  g_cache_v[TTF_CACHE_N];
static uint32_t g_cache_cp[TTF_CACHE_N];
static uint8_t  g_cache_dat[TTF_CACHE_N][32];

void ttf_init(void) {
    g_ready = 0;
    vfs_dirent_t e;
    if (vfs_open("YSHI.TTF", &e) != 0) {
        serial_write("[ttf] /YSHI.TTF not found -> fall back to baked bitmap font\n");
        return;
    }
    uint32_t cap = e.size ? e.size : 1;
    uint8_t* buf = kmalloc(cap);
    if (!buf) { serial_write("[ttf] kmalloc failed for font buffer\n"); return; }
    uint32_t rl = 0;
    if (vfs_read(&e, buf, cap, &rl) != 0 || rl < 32) {
        serial_write("[ttf] failed to read font file\n");
        kfree(buf);
        return;
    }
    if (stbtt_InitFont(&g_info, (const unsigned char*)buf, 0) == 0) {
        serial_write("[ttf] stbtt_InitFont failed\n");
        kfree(buf);
        return;
    }
    g_font_buf = buf;   /* 永久保留，stb 内部持有该指针 */
    g_ready = 1;

    /* 计算简单校验和（XOR + 加法混合），用于后续检测堆是否破坏了字体数据 */
    {   uint32_t ck = 0;
        for (uint32_t ci = 0; ci < rl && ci < 65536; ci += 4) {
            ck ^= ((uint32_t)buf[ci]) | ((uint32_t)buf[ci+1]<<8) |
                  ((uint32_t)buf[ci+2]<<16) | ((uint32_t)buf[ci+3]<<24);
            ck = ck * 31 + ci;
        }
        g_font_cksum = ck;
        serial_write(" cksum=0x"); serial_puth(ck);
    }
    serial_puti(rl);
    serial_write(" bytes, glyph cache = ");
    serial_puti(TTF_CACHE_N);

    /* 诊断：立即测试一个常见汉字（U+6587 '文'）的 glyph 查找，
     * 确认 cmap 可用。若 gi=0 说明字体 cmap 有问题或数据损坏。
     * 同时打印 font info 关键字段和原始数据校验。 */
    {
        /* 校验 TTF 头部 magic 和关键 offset */
        uint32_t sfnt = (buf[0]<<24)|(buf[1]<<16)|(buf[2]<<8)|buf[3];
        uint16_t nt  = (buf[4]<<8)|buf[5];   /* numTables */
        serial_write(", sfnt=0x"); serial_puth(sfnt);
        serial_write(" tables="); serial_puti(nt);

        /* stb 解析出的关键字段（struct 无 ascent/descent 直接字段） */
        serial_write(" fontstart="); serial_puti(g_info.fontstart);
        serial_write(" ng="); serial_puti(g_info.numGlyphs);
        serial_write(" cmap="); serial_puti(g_info.index_map);
        serial_write(" loca="); serial_puti(g_info.loca);
        serial_write(" glyf="); serial_puti(g_info.glyf);
        serial_write(" hhea="); serial_puti(g_info.hhea);
        serial_write(" hmtx="); serial_puti(g_info.hmtx);
        serial_write(" ilocfmt="); serial_puti(g_info.indexToLocFormat);

        /* VMetrics 需单独 API 取 */
        int vasc = 0, vdesc = 0, vlg = 0;
        stbtt_GetFontVMetrics(&g_info, &vasc, &vdesc, &vlg);
        serial_write(" asc="); serial_puti(vasc);
        serial_write(" desc="); serial_puti(vdesc);

        int test_gi = stbtt_FindGlyphIndex(&g_info, 0x6587);  /* '文' */
        serial_write(" test_gi="); serial_puti(test_gi);
        if (test_gi > 0) {
            float ts = stbtt_ScaleForPixelHeight(&g_info, 16.0f);
            int tw, th, tx, ty;
            unsigned char* tbmp = stbtt_GetGlyphBitmap(&g_info, ts, ts, test_gi, &tw, &th, &tx, &ty);
            serial_write(" scale="); serial_puti((int)(ts*100));
            serial_write("/100 bmp="); serial_puti(tw);
            serial_write("x"); serial_puti(th);
            if (tbmp) { STBTT_free(tbmp, 0); serial_write(" OK"); }
            else serial_write(" NULL");
        }
        serial_write("\n");
    }
}

int ttf_ok(void) { return g_ready; }

int ttf_glyph(uint32_t cp, uint8_t out[32]) {
    if (!g_ready) return 0;

    /* 首次调用时验证字体数据是否被堆 corruption 破坏 */
    { static int _ck_done = 0; if (!_ck_done) { _ck_done = 1;
        uint32_t ck = 0;
        uint8_t* f = g_font_buf;
        for (uint32_t ci = 0; ci < 65536; ci += 4) {
            ck ^= ((uint32_t)f[ci]) | ((uint32_t)f[ci+1]<<8) |
                  ((uint32_t)f[ci+2]<<16) | ((uint32_t)f[ci+3]<<24);
            ck = ck * 31 + ci;
        }
        if (ck != g_font_cksum)
            serial_write("[ttf] !!! FONT DATA CORRUPTED !!! expected_ck=0x... got=0x...\n");
        else
            serial_write("[ttf] font data integrity OK (cksum match)\n");
    }}

    uint32_t key = cp & (TTF_CACHE_N - 1);
    if (g_cache_v[key] && g_cache_cp[key] == cp) {
        memcpy(out, g_cache_dat[key], 32);
        return 1;
    }

    float scale = stbtt_ScaleForPixelHeight(&g_info, 16.0f);
    int gi = stbtt_FindGlyphIndex(&g_info, (int)cp);
    if (gi == 0) {
        /* 仅首次失败时输出诊断，避免刷屏 */
        static int diag_done = 0;
        if (!diag_done) {
            diag_done = 1;
            serial_write("[ttf] glyph NOT FOUND for cp=0x");
            serial_puth(cp);
            serial_write(" (gi=0); scale(x1e4)=");
            /* scale 是 ~0.0156 的小浮点，乘 1e4 取整打印，避免显示成 0 */
            serial_puti((int)(scale * 10000));
            serial_write("; g_ready=");
            serial_puti(g_ready);
            serial_write(" data@");
            serial_puth((uint32_t)g_info.data);
            serial_write("\n");
        }
        return 0;   /* 字体里没有该字 -> 调用方画空框 */
    }

    int w, h, xoff, yoff;
    unsigned char* bmp = stbtt_GetGlyphBitmap(&g_info, scale, scale, gi,
                                              &w, &h, &xoff, &yoff);
    if (!bmp) return 0;

    for (int i = 0; i < 32; i++) out[i] = 0;
    int dx = (16 - w) / 2;   /* 把字形包围盒居中到 16x16 单元格 */
    int dy = (16 - h) / 2;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            if (bmp[row * w + col] > 128) {
                int X = dx + col;
                int Y = dy + row;
                if (X >= 0 && X < 16 && Y >= 0 && Y < 16) {
                    int bit = 15 - X;                 /* MSB = 最左像素 */
                    if (bit >= 8) out[Y * 2]     |= (uint8_t)(1u << (bit - 8));
                    else          out[Y * 2 + 1] |= (uint8_t)(1u << bit);
                }
            }
        }
    }
    STBTT_free(bmp, 0);

    g_cache_v[key] = 1;
    g_cache_cp[key] = cp;
    memcpy(g_cache_dat[key], out, 32);
    return 1;
}
