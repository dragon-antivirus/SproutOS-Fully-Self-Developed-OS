#include "kernel.h"

mouse_t mouse = {0, 0, 0, 0, 0};
static uint8_t mouse_cycle = 0;
static uint8_t mouse_packet[3];
static int prev_left = 0;
static int click_pending = 0;

static int mouse_packet_aligned(uint8_t value) {
    return (value & 0x08) != 0;
}

void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        while (timeout-- && (inb(0x64) & 1) == 0);
    } else {
        while (timeout-- && (inb(0x64) & 2) != 0);
    }
}

void mouse_write(uint8_t data) {
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, data);
}

uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(0x60);
}

void mouse_install(void) {
    mouse_wait(1);
    outb(0x64, 0xA8);
    mouse_wait(1);
    outb(0x64, 0x20);
    uint8_t status = inb(0x60) | 2;
    mouse_wait(1);
    outb(0x64, 0x60);
    mouse_wait(1);
    outb(0x60, status);
    mouse_wait(1);
    outb(0x64, 0xD4);
    mouse_wait(1);
    outb(0x60, 0xF4);
    mouse_read();

    mouse_cycle = 0;
    prev_left = 0;
    click_pending = 0;
    mouse.x = fb.width / 2;
    mouse.y = fb.height / 2;
    mouse.left = 0;
    mouse.right = 0;
    mouse.wheel = 0;
}

void mouse_handler(void) {
    uint8_t value = inb(0x60);

    if (mouse_cycle == 0) {
        if (!mouse_packet_aligned(value)) return;
        mouse_packet[0] = value;
        mouse_cycle = 1;
        return;
    }

    mouse_packet[mouse_cycle++] = value;
    if (mouse_cycle < 3) return;
    mouse_cycle = 0;
    if (mouse_packet[0] & 0x80 || mouse_packet[0] & 0x40) return;

    int dx = (int)(int8_t)mouse_packet[1];
    int dy = (int)(int8_t)mouse_packet[2];
    dy = -dy;

    mouse.x += dx;
    mouse.y += dy;
    if (mouse.x < 0) mouse.x = 0;
    if (mouse.y < 0) mouse.y = 0;
    if (mouse.x >= (int)fb.width) mouse.x = fb.width - 1;
    if (mouse.y >= (int)fb.height) mouse.y = fb.height - 1;

    prev_left = mouse.left;
    mouse.left = (mouse_packet[0] & 0x01) ? 1 : 0;
    mouse.right = (mouse_packet[0] & 0x02) ? 1 : 0;
    if (mouse.left && !prev_left) click_pending = 1;
    if (!mouse.left) click_pending = 0;
}

int mouse_clicked(void) {
    if (click_pending) {
        click_pending = 0;
        return 1;
    }
    return 0;
}

int mouse_released(void) {
    return !mouse.left && prev_left;
}
