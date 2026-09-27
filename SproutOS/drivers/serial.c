#include "kernel.h"

static int serial_init = 0;

void serial_init_port(uint16_t port) {
    outb(port + 1, 0x00);
    outb(port + 3, 0x80);
    outb(port + 0, 0x03);
    outb(port + 1, 0x00);
    outb(port + 3, 0x03);
    outb(port + 2, 0xC7);
    outb(port + 4, 0x0B);
    serial_init = 1;
}

void serial_putc(char c) {
    if (!serial_init) serial_init_port(0x3F8);
    while ((inb(0x3F8 + 5) & 0x20) == 0);
    outb(0x3F8, c);
}

void serial_write(const char* s) {
    while (*s) serial_putc(*s++);
}

void serial_puti(int n) {
    char buf[16];
    int i = 0;
    if (n == 0) { serial_putc('0'); return; }
    if (n < 0) { serial_putc('-'); n = -n; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i--) serial_putc(buf[i]);
}

void serial_puth(uint32_t n) {
    serial_write("0x");
    char hex[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4)
        serial_putc(hex[(n >> i) & 0xF]);
}
