#include "kernel.h"

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    io_wait();
    return inb(0x71);
}

static int bcd_to_bin(uint8_t b) {
    return (b & 0x0F) + ((b >> 4) * 10);
}

void rtc_read(rtc_t* t) {
    uint8_t s, m, h, d, mo, y, st;
    do {
        s = cmos_read(0x00);
        m = cmos_read(0x02);
        h = cmos_read(0x04);
        d = cmos_read(0x07);
        mo = cmos_read(0x08);
        y = cmos_read(0x09);
        st = cmos_read(0x0B);
    } while (s != cmos_read(0x00));

    if (!(st & 0x04)) {
        s = bcd_to_bin(s);
        m = bcd_to_bin(m);
        h = bcd_to_bin(h);
        d = bcd_to_bin(d);
        mo = bcd_to_bin(mo);
        y = bcd_to_bin(y);
    }
    if (!(st & 0x02)) {
        if (h == 0) h = 12;
        else if (h > 12) h -= 12;
    }
    t->second = s;
    t->minute = m;
    t->hour = h;
    t->day = d;
    t->month = mo;
    t->year = 2000 + y;
}
