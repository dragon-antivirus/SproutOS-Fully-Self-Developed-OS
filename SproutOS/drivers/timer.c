#include "kernel.h"

volatile uint32_t tick = 0;

/* Calibrated CPU frequency in MHz (used by udelay). Fallback 1000 if
   calibration is unavailable. */
static uint32_t g_cpu_mhz = 1000;

uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* Busy-wait for approximately `us` microseconds using the TSC. */
void udelay(uint32_t us) {
    uint64_t end = rdtsc() + (uint64_t)g_cpu_mhz * us;
    while (rdtsc() < end) __asm__ volatile("pause" ::: "memory");
}

void timer_install(void) {
    uint32_t divisor = 1193180 / 100;
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);

    /* Calibrate TSC against the PIT *without* relying on timer IRQs (which are
       not enabled yet at this point in boot). Temporarily switch channel 0 to
       one-shot mode 0, count down a known interval by polling its current
       value, then restore the 100 Hz periodic mode. */
    outb(0x43, 0x30);            /* ch0, mode 0 (one-shot), lobyte/hibyte */
    outb(0x40, 0xFF);
    outb(0x40, 0xFF);            /* count = 0xFFFF (~54.9 ms max) */
    uint64_t t0 = rdtsc();
    /* ~1 ms @ 1.19318 MHz PIT = 1193 ticks; wait until count drops that much. */
    const uint16_t target = 0xFFFF - 1193;   /* = 0xFB56 */
    uint16_t c;
    do {
        outb(0x43, 0x00);        /* latch counter 0 */
        c = (uint16_t)(inb(0x40) | (inb(0x40) << 8));
    } while (c > target);

    uint64_t t1 = rdtsc();
    /* Restore 100 Hz periodic mode 2. */
    outb(0x43, 0x36);
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);

    uint64_t dt = t1 - t0;       /* TSC ticks in ~1 ms */
    /* Truncate to 32 bits before dividing so the compiler emits a native
       32-bit `div` instead of the libgcc __udivdi3 helper. */
    g_cpu_mhz = (uint32_t)dt / 1000;
    if (g_cpu_mhz == 0 || g_cpu_mhz > 100000) g_cpu_mhz = 1000;
    serial_write("[TIMER] cpu ~");
    serial_puti(g_cpu_mhz);
    serial_write(" MHz\n");
}

void timer_tick(void) {
    tick++;
}

void sleep_ticks(uint32_t t) {
    uint32_t target = tick + t;
    while (tick < target) __asm__ volatile("hlt");
}

uint32_t get_tick(void) {
    return tick;
}
