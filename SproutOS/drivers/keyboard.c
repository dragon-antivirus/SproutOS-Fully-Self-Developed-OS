#include "kernel.h"

static char keybuf[256];
static int keybuf_head = 0, keybuf_tail = 0;

/* Extended keys (arrows / Home / End / PgUp / PgDn / Del, sent with a 0xE0
 * prefix) are queued here as 16+ pseudo-codes that match the browser's
 * KEY_LEFT/KEY_RIGHT/KEY_UP/KEY_DOWN/... macros, so kbd_poll can deliver them. */
static int extbuf[32];
static int ext_head = 0, ext_tail = 0;
static int e0_pending = 0;

/* PS/2 Set-1 scancode → ASCII。数组下标 = scancode。
 * 每个 Row 的最后一个元素就是该 Row 在数组中的位置 + 1（即下个 Row 的起点）。
 * Row 1: sc 0x00..0x1c   (29 项, 含 Esc/数字/字母上半/Enter)
 * Row 2: sc 0x1d..0x39   (29 项, 含 Ctrl/A行/反斜杠/字母下半/Space)
 * Row 3: sc 0x3a..0x7f   (70 项, F1..F12 等修饰键, 全 0)
 *
 * 历史 bug：原版漏掉 sc 0x2b (\)，导致 z/x/c/v/b/n/m 一整行向右偏移 1 位
 * (按 z 出 x、按 b 出 n 等)。已在 Row 2 第 14 位插入 '\\'/'|' 修复。 */
static const char shift_map[128] = {
    /* sc 0x00..0x1c (29) */ 0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 8, 9, 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 13,
    /* sc 0x1d..0x39 (29) */ 0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', 34, '~', 0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, 0, 0, 32,
    /* sc 0x3a..0x7f (70, all 0) */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char normal_map[128] = {
    /* sc 0x00..0x1c (29) */ 0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 8, 9, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 13,
    /* sc 0x1d..0x39 (29) */ 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', 39, '`', 0, 92, 'z', 'x', 'c', 'v', 'b', 'n', 'm', 44, '.', '/', 0, 0, 0, 32,
    /* sc 0x3a..0x7f (70, all 0) */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static int shift = 0;
static int ctrl = 0;

void keyboard_handler(void) {
    uint8_t scan = inb(0x60);
    if (scan == 0xE0) { e0_pending = 1; return; }
    if (scan == 0x2A || scan == 0x36) { shift = 1; return; }
    if (scan == 0xAA || scan == 0xB6) { shift = 0; return; }
    if (scan == 0x1D) { ctrl = 1; return; }   /* Ctrl 按下 */
    if (scan == 0x9D) { ctrl = 0; return; }   /* Ctrl 释放 */
    int release = (scan & 0x80);
    if (e0_pending) {
        e0_pending = 0;
        if (release) return;
        int code = 0;
        switch (scan) {
            case 0x4B: code = 0x100; break;  /* ← left  */
            case 0x4D: code = 0x101; break;  /* → right */
            case 0x48: code = 0x102; break;  /* ↑ up    */
            case 0x50: code = 0x103; break;  /* ↓ down  */
            case 0x47: code = 0x104; break;  /* Home    */
            case 0x4F: code = 0x105; break;  /* End     */
            case 0x49: code = 0x106; break;  /* PgUp    */
            case 0x51: code = 0x107; break;  /* PgDn    */
            case 0x53: code = 0x7F;  break;  /* Del     */
            default: break;
        }
        if (code) {
            extbuf[ext_tail] = code;
            ext_tail = (ext_tail + 1) % 32;
        }
        return;
    }
    if (release) return;
    char c = (shift ? shift_map : normal_map)[scan];
    if (c) {
        /* Ctrl+字母 → 传统 ASCII 控制码（Ctrl+S = 0x13，Ctrl+C = 0x03），
         * 这样编辑器/终端无需额外状态就能识别组合键。 */
        if (ctrl && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) {
            c = (c | 0x20) - 'a' + 1;
        }
        keybuf[keybuf_tail] = c;
        keybuf_tail = (keybuf_tail + 1) % 256;
    }
}

char kgetc(void) {
    while (keybuf_head == keybuf_tail)
        __asm__ volatile("hlt");
    char c = keybuf[keybuf_head];
    keybuf_head = (keybuf_head + 1) % 256;
    return c;
}

int kpeek(void) {
    return keybuf_head != keybuf_tail;
}

/* Drain an extended (0xE0-prefixed) key as a 16+ pseudo-code. Returns 1 and
 * writes the code to *out, or 0 if the extended queue is empty. The browser's
 * kbd_poll polls this before the ASCII queue. */
int kgetc_ext(int* out) {
    if (ext_head == ext_tail) return 0;
    *out = extbuf[ext_head];
    ext_head = (ext_head + 1) % 32;
    return 1;
}
