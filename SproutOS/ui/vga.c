#include "kernel.h"
#include "truetype.h"

framebuffer fb;
#define FB_BACK_CAP (1024 * 768)
static uint32_t fb_back_mem[FB_BACK_CAP];
uint32_t* fb_back = 0;

void vga_init(uint32_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint32_t bpp) {
    serial_write("[vga] init start addr="); serial_puth(addr);
    serial_write(" pitch="); serial_puti(pitch);
    serial_write(" w="); serial_puti(w); serial_write(" h="); serial_puti(h);
    serial_write(" bpp="); serial_puti(bpp); serial_write("\n");
    fb.addr = (uint32_t*)addr;
    fb.pitch = pitch;
    fb.width = w;
    fb.height = h;
    fb.bpp = bpp;
    /* fb_back 用 fb.width 而非 fb.pitch 计算大小，避免 OVMF 等场景 stride
     * 大于 width*4 时 fb_back 越界。FB_BACK_CAP 也按 w*h 算兜底。 */
    uint32_t pixels = h * w;
    if (pixels <= FB_BACK_CAP) {
        fb_back = fb_back_mem;
    } else {
        fb_back = (uint32_t*)kmalloc(pixels * 4);
    }
    if (fb_back) {
        for (uint32_t i = 0; i < pixels; i++) fb_back[i] = 0;
    } else {
        fb_back = 0;
    }
    serial_write("[vga] backbuffer cleared fb_back="); serial_puth((uint32_t)fb_back);
    serial_write(" pixels="); serial_puti(pixels); serial_write("\n");
}

void fb_pixel(int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || (uint32_t)x >= fb.width || (uint32_t)y >= fb.height) return;
    uint32_t off = y * (fb.pitch / 4) + x;
    uint32_t* buf = fb_back ? fb_back : fb.addr;
    buf[off] = color;
}

void fb_clear(uint32_t color) {
    for (uint32_t y = 0; y < fb.height; y++)
        for (uint32_t x = 0; x < fb.width; x++)
            fb_pixel(x, y, color);
}

void fb_rect(int x, int y, int w, int h, uint32_t color) {
    for (int yy = y; yy < y + h; yy++)
        for (int xx = x; xx < x + w; xx++)
            fb_pixel(xx, yy, color);
}

void fb_char(int x, int y, int c, uint32_t fg, uint32_t bg) {
    if (c < 0 || c > 127) c = '?';
    for (int row = 0; row < 8; row++) {
        uint8_t bits = font8x8_basic[(int)c][row];
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col))
                fb_pixel(x + col, y + row, fg);
            else if (bg != (uint32_t)-1)
                fb_pixel(x + col, y + row, bg);
        }
    }
}

void fb_string(int x, int y, const char* s, uint32_t fg, uint32_t bg) {
    int cx = x;
    while (*s) {
        if (*s == '\n') { cx = x; y += 9; s++; continue; }
        fb_char(cx, y, *s, fg, bg);
        cx += 8;
        s++;
    }
}

/* 取得某个码点的 16x16 单色位图（32 字节）：
 * 用运行时 TTF 栅格化；失败（TTF 缺失或字体无此字）则画一个 16x16 空框，
 * 保证不越界、不崩。旧的点阵字库（cn_font_16x16）已删除，不再回退。 */
static void cn_bitmap(uint32_t cp, uint8_t out[32]) {
    for (int i = 0; i < 32; i++) out[i] = 0;
    if (ttf_glyph(cp, out)) return;
    /* TTF 不可用 / 缺字：空框兜底 */
    out[0]  = 0xFF; out[1]  = 0xFF;
    out[14] = 0xFF; out[15] = 0xFF;
    for (int r = 0; r < 16; r++) { out[r * 2] |= 0x80; out[r * 2 + 1] |= 0x01; }
}

/* 画一个 16x16 码点（Unicode）。fg 为前景色，bg 为背景色（(uint32_t)-1 表示透明）。 */
void fb_cn_char(int x, int y, uint32_t cp, uint32_t fg, uint32_t bg) {
    uint8_t bm[32];
    cn_bitmap(cp, bm);
    for (int row = 0; row < 16; row++) {
        uint16_t bits = (uint16_t)(((uint16_t)bm[row * 2] << 8) | bm[row * 2 + 1]);
        for (int col = 0; col < 16; col++) {
            if (bits & (1 << (15 - col)))
                fb_pixel(x + col, y + row, fg);
            else if (bg != (uint32_t)-1)
                fb_pixel(x + col, y + row, bg);
        }
    }
}

/* 中文标签字符串：UTF-8 文本，逐码点绘制。
 * - 中文/CJK 码点（>=0x80）：占 16px，由 fb_cn_char 走 TTF 渲染（旧点阵回退已移除）；
 * - ASCII（<0x80）：占 8px，直接走 8x8 字模（不依赖 TTF，无字也能画）；
 *   （空格 0x20 与结束 0 仍占位但不画像素，保持对齐） */
void fb_cn_string(int x, int y, const char* s, uint32_t fg, uint32_t bg) {
    int cx = x;
    /* 诊断：首次调用时打印前 16 字节原始内容，确认 UTF-8 编码正确 */
    { static int _once = 0; if (!_once) { _once = 1;
      serial_write("[UTF8] fb_cn_string first call: \"");
      for (int _di = 0; s[_di] && _di < 32; _di++) {
          unsigned char _ch = (unsigned char)s[_di];
          if (_ch >= 0x20 && _ch < 0x7F) { char _tmp[2] = { (char)_ch, 0 }; serial_write(_tmp); }
          else { serial_write("\\x"); static const char* _hex="0123456789ABCDEF";
                 char _buf[4] = { _hex[(_ch>>4)&0xF], _hex[_ch&0xF], 0 }; serial_write(_buf); }
      }
      serial_write("\"\n");
    }}
    while (*s) {
        uint32_t cp = utf8_decode(&s);
        /* 诊断：前 3 个码点打印一次 */
        { static int _dc = 0; if (_dc < 3) {
            serial_write("[UTF8] cp"); serial_puti(++_dc);
            serial_write("=U+"); serial_puth(cp);
            serial_write("\n");
        }}
        if (cp < 0x80) {
            if (cp != 0x20 && cp != 0)
                fb_char(cx, y, (int)cp, fg, bg);
            cx += 8;
        } else {
            if (cp != 0)
                fb_cn_char(cx, y, cp, fg, bg);
            cx += 16;
        }
    }
}

uint32_t utf8_decode(const char** p) {
    const unsigned char* s = (const unsigned char*)*p;
    uint32_t cp; int n;
    if (s[0] < 0x80)            { cp = s[0];            n = 1; }
    else if ((s[0] & 0xE0) == 0xC0) { cp = s[0] & 0x1F; n = 2; }
    else if ((s[0] & 0xF0) == 0xE0) { cp = s[0] & 0x0F; n = 3; }
    else if ((s[0] & 0xF8) == 0xF0) { cp = s[0] & 0x07; n = 4; }
    else                       { cp = s[0];            n = 1; }
    for (int i = 1; i < n; i++) cp = (cp << 6) | (s[i] & 0x3F);
    *p = (const char*)(s + n);
    return cp;
}

int utf8_count(const char* s) {
    int n = 0;
    while (*s) { utf8_decode(&s); n++; }
    return n;
}

void fb_string_scale(int x, int y, const char* s, uint32_t fg, uint32_t bg, int scale) {
    int cx = x;
    while (*s) {
        if (*s == '\n') { cx = x; y += 9 * scale; s++; continue; }
        for (int row = 0; row < 8; row++) {
            uint8_t bits = font8x8_basic[(int)*s][row];
            for (int col = 0; col < 8; col++) {
                if (bits & (1 << col))
                    fb_rect(cx + col * scale, y + row * scale, scale, scale, fg);
                else if (bg != (uint32_t)-1)
                    fb_rect(cx + col * scale, y + row * scale, scale, scale, bg);
            }
        }
        cx += 8 * scale;
        s++;
    }
}

void fb_circle(int cx, int cy, int r, uint32_t color) {
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r)
                fb_pixel(cx + x, cy + y, color);
}

/* 圆环：填充 r-thick/2 与 r+thick/2 之间的环带（thick=1 时就是 1 像素的线） */
void fb_ring(int cx, int cy, int r, uint32_t color, int thick) {
    if (r < 0) return;
    int r_out = r + thick / 2;
    int r_in  = r - thick / 2;
    if (r_in < 0) r_in = 0;
    int r_out2 = r_out * r_out;
    int r_in2  = r_in  * r_in;
    for (int dy = -r_out; dy <= r_out; dy++) {
        int dy2 = dy * dy;
        for (int dx = -r_out; dx <= r_out; dx++) {
            int d2 = dx * dx + dy2;
            if (d2 <= r_out2 && d2 >= r_in2) {
                fb_pixel(cx + dx, cy + dy, color);
            }
        }
    }
}

/* Bresenham 直线 + 3x3 笔锋（让 logo 的白线在深色背景上更清晰） */
void fb_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1;
    int sy = dy < 0 ? -1 : 1;
    int x = x0, y = y0;
    int err = ax - ay;
    /* 走完所有点 */
    for (;;) {
        fb_rect(x - 1, y - 1, 3, 3, color);
        if (x == x1 && y == y1) break;
        int e2 = 2 * err;
        if (e2 > -ay) { err -= ay; x += sx; }
        if (e2 <  ax) { err += ax; y += sy; }
    }
}

/* 把 polyline 前 k 段画出来（k=0 不画；k>=n-1 画整条）。
 * 数据是 (x0, y0, x1, y1, ...) 的交错数组。 */
void fb_polyline_partial(const int* pts, int n, int k, uint32_t color) {
    if (k <= 0) return;
    if (k > n - 1) k = n - 1;
    for (int i = 0; i < k; i++) {
        int x0 = pts[i * 2],     y0 = pts[i * 2 + 1];
        int x1 = pts[i * 2 + 2], y1 = pts[i * 2 + 3];
        fb_line(x0, y0, x1, y1, color);
    }
}

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (r << 16) | (g << 8) | b;
}

void fb_flush(void) {
    if (!fb_back) return;
    /* 按 scanline (一行一行) 拷贝，绝不用单次大 memcpy：
     *   - src (fb_back) 按 fb.width 组织（无 padding）
     *   - dst (fb.addr 物理显存) 按 fb.pitch 步长（一行 fb.pitch 字节，
     *     其中前 fb.width*4 是有效数据，剩下的是硬件 padding）
     *   - 两种步长不同的情况下，单次 memcpy 会在每行尾部把下一行的开头几像素
     *     写到当前行末——视觉上就是"图像被水平多次复制"。
     *   - 即使两边步长一致（width*4 == pitch），按行写也等价于单次 memcpy，
     *     没有额外开销。 */
    uint32_t src_row_bytes = fb.width * 4;
    uint32_t dst_row_bytes = fb.pitch;
    const uint32_t* src = fb_back;
    uint8_t* dst = (uint8_t*)fb.addr;
    for (uint32_t y = 0; y < fb.height; y++) {
        memcpy(dst + (uintptr_t)y * dst_row_bytes,
               src + (uintptr_t)y * (src_row_bytes / 4),
               src_row_bytes);
    }
    /* 一次性诊断：首次 fb_flush 打印地址/尺寸，便于排查 stride 不匹配、写到
     * register MMIO、地址错误等问题，尤为 BIOS 路径下 VBE 模式报告的 pitch
     * 可能与物理显存实际 stride 不一致。 */
    static int _once = 0;
    if (!_once) {
        _once = 1;
        serial_write("[vga] first flush: addr=0x");
        serial_puth((uint32_t)fb.addr);
        serial_write(" src=0x");
        serial_puth((uint32_t)fb_back);
        serial_write(" w="); serial_puti(fb.width);
        serial_write(" h="); serial_puti(fb.height);
        serial_write(" pitch="); serial_puti(fb.pitch);
        serial_write(" src_row="); serial_puti(src_row_bytes);
        serial_write(" dst_row="); serial_puti(dst_row_bytes);
        serial_write("\n");
    }
}
