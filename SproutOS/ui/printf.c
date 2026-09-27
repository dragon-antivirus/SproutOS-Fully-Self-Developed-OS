#include "kernel.h"
#include <stdarg.h>

static int con_x = 0;
static int con_y = 0;
static int con_enabled = 1;

void vga_scroll(void) {
    uint32_t* v = fb_back ? fb_back : fb.addr;
    uint32_t rowbytes = fb.pitch / 4;
    for (uint32_t y = 0; y < fb.height - 8; y++) {
        uint32_t* dst = v + y * rowbytes;
        uint32_t* src = v + (y + 8) * rowbytes;
        for (uint32_t x = 0; x < fb.width; x++)
            dst[x] = src[x];
    }
    for (uint32_t y = fb.height - 8; y < fb.height; y++)
        for (uint32_t x = 0; x < fb.width; x++)
            fb_pixel(x, y, rgb(15, 23, 42));
}

void kputc(char c) {
    serial_putc(c);
    if (!con_enabled) return;
    if (c == '\n') {
        con_x = 0;
        con_y += 9;
    } else if (c == '\r') {
        con_x = 0;
    } else {
        fb_char(con_x, con_y, c, rgb(200, 230, 255), (uint32_t)-1);
        con_x += 8;
        if (con_x >= (int)fb.width - 8) {
            con_x = 0;
            con_y += 9;
        }
    }
    if (con_y >= (int)fb.height - 8) {
        vga_scroll();
        con_y -= 8;
    }
}

void kputs(const char* s) {
    while (*s) kputc(*s++);
    kputc('\n');
}

static void putuint(uint32_t n, int base, int upper) {
    char buf[16];
    int i = 0;
    const char* dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (n == 0) { kputc('0'); return; }
    while (n > 0) { buf[i++] = dig[n % base]; n /= base; }
    while (i--) kputc(buf[i]);
}

int kprintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    while (*fmt) {
        if (*fmt == '%' && *(fmt + 1)) {
            fmt++;
            if (*fmt == 'd') {
                int v = va_arg(ap, int);
                if (v < 0) { kputc('-'); putuint((uint32_t)(-v), 10, 0); }
                else putuint((uint32_t)v, 10, 0);
            } else if (*fmt == 'u') {
                putuint(va_arg(ap, uint32_t), 10, 0);
            } else if (*fmt == 'x' || *fmt == 'X') {
                kputc('0'); kputc('x');
                putuint(va_arg(ap, uint32_t), 16, *fmt == 'X');
            } else if (*fmt == 's') {
                const char* s = va_arg(ap, const char*);
                while (*s) kputc(*s++);
            } else if (*fmt == 'c') {
                kputc((char)va_arg(ap, int));
            } else if (*fmt == '%') {
                kputc('%');
            } else {
                kputc('%');
                kputc(*fmt);
            }
        } else {
            kputc(*fmt);
        }
        fmt++;
    }
    va_end(ap);
    return 0;
}
