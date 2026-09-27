#ifndef KERNEL_H
#define KERNEL_H

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef int int32_t;
typedef signed char int8_t;
typedef short int16_t;
/* i386 (ILP32) 下指针 32 位：uintptr_t 必须与工具链 <stdint.h> 定义一致
   （GCC -m32 下为 unsigned int）。若写成 unsigned long 会与 browser 链
   中 <stdint.h> 的 uintptr_t 冲突（类型不同、即使同宽也报错）。 */
#ifndef uintptr_t
typedef unsigned int uintptr_t;
#endif

/* freestanding 环境无 <stddef.h>：统一提供 NULL（truetype.c 的本地垫片与之互斥，不受影响） */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* === 系统版本身份（Sprout BIOS/UEFI 双启版） ===
 * 全系统唯一的版本真相来源；开机横幅、shell `ver`、关于对话框、VERSION 文件
 * 都从这里取，避免各处硬编码散落不一致。改版本只改这一处。 */
#define OS_NAME       "SproutOS"
#define OS_VERSION    "1.0"
#define OS_RELEASE    "2026.8 技术预览版"
#define OS_EDITION    "Sprout BIOS/UEFI双启版"
#define OS_FULLNAME   OS_NAME " " OS_VERSION "（" OS_EDITION "）"

typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp;
    uint32_t ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
} regs;

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t base_middle;
    uint8_t access;
    uint8_t granularity;
    uint8_t base_high;
} gdt_entry;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr;

typedef struct {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t always0;
    uint8_t flags;
    uint16_t base_hi;
} idt_entry;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr;

typedef struct {
    uint32_t* addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
} framebuffer;

typedef struct {
    int x, y, left, right, wheel;
} mouse_t;

typedef struct {
    int second, minute, hour, day, month, year;
} rtc_t;

typedef struct {
    int active;
    int x, y, w, h;
    char title[24];
    uint32_t title_color;
    int dragging;
    int drag_dx, drag_dy;
    int minimized;          /* 1 = 收进任务栏，不在桌面绘制 */
    int maximized;          /* 1 = 铺满工作区 */
    int ox, oy, ow, oh;     /* 最大化前的原始几何，用于还原 */
} window_t;

window_t* gui_window(int idx);
int gui_new_window(const char* title, uint32_t color, int w, int h);
void gui_close_window(int idx);
void gui_toggle_maximize(int idx);
void gui_taskbar_click(int idx);
int gui_get_focus(void);
void gui_handle_input(int clicked);
void gui_handle_key(char c);
int gui_window_count(void);
void gui_draw_desktop(void);
int gui_desktop_app_at(int mx, int my);
int gui_load_wallpaper(void);
int gui_set_wallpaper_from_file(const char* name);
void gui_open_file_in_app(int app_idx, const char* name);
void gui_draw_apps(void);
void gui_draw_taskbar(void);
void gui_draw_mouse(void);
const char** gui_app_names(void);
void launcher_open(int app_index);

/* 浏览器（全屏独占应用，不走 window_t 体系）。
 * launcher_open(6) 会调 browser_init() 并置 g_browser_open=1；主循环检测到
 * g_browser_open 后改走 browser_frame()，不再绘制桌面/窗口/任务栏。 */
void browser_init(void);
void browser_frame(void);
int  browser_done(void);
extern int g_browser_open;
extern int g_boot_mode;   /* 0 = BIOS, 1 = UEFI */
void boot_animation(void);
void kernel_poweroff(void);

/* ===== SeedVM 包管理 (M5) ===== */
#include "seedvm.h"
extern seed_vm_t* g_seedvm;
extern int g_seedvm_active;
void seedvm_run_frame(void);     /* 主循环每帧调用 */
void seedvm_exit(void);          /* 退出 seed 模式回桌面 */

extern framebuffer fb;
extern uint32_t* fb_back;
extern mouse_t mouse;
extern volatile uint32_t tick;

void outb(uint16_t port, uint8_t val);
uint8_t inb(uint16_t port);
void io_wait(void);
void outw(uint16_t port, uint16_t val);
uint16_t inw(uint16_t port);

/* 把 QEMU/Bochs 标准 VGA 直接切到线性帧缓冲图形模式（BIOS 路径用）。见 drivers/vgavbe.c */
int vga_vbe_set_mode(uint16_t w, uint16_t h, uint8_t bpp);

void serial_init_port(uint16_t port);
void serial_putc(char c);
void serial_write(const char* s);
void serial_puti(int n);
void serial_puth(uint32_t n);

void gdt_install(void);
void idt_install(void);
void isr_handler(regs* r);
void register_interrupt_handler(uint8_t n, void (*handler)(regs*));
void enable_int();
void disable_int();

void pic_remap(void);

void timer_install(void);
void timer_tick(void);
void sleep_ticks(uint32_t t);
uint32_t get_tick(void);
uint64_t rdtsc(void);
void udelay(uint32_t us);

/* Networking (M4): registers NIC drivers; call once before driver_run_all(). */
void net_init(void);

void keyboard_handler(void);
char kgetc(void);
int kpeek(void);
int kgetc_ext(int* out);   /* 0xE0 扩展键（方向键等）队列，供浏览器 kbd_poll 使用 */

void mouse_install(void);
void mouse_handler(void);
int mouse_clicked(void);
int mouse_released(void);

void rtc_read(rtc_t* t);

void vga_init(uint32_t addr, uint32_t pitch, uint32_t w, uint32_t h, uint32_t bpp);
void fb_pixel(int x, int y, uint32_t color);
void fb_clear(uint32_t color);
void fb_rect(int x, int y, int w, int h, uint32_t color);
void fb_char(int x, int y, int c, uint32_t fg, uint32_t bg);
void fb_string(int x, int y, const char* s, uint32_t fg, uint32_t bg);
void fb_string_scale(int x, int y, const char* s, uint32_t fg, uint32_t bg, int scale);
void fb_circle(int cx, int cy, int r, uint32_t color);
void fb_ring(int cx, int cy, int r, uint32_t color, int thick);
void fb_line(int x0, int y0, int x1, int y1, uint32_t color);
void fb_polyline_partial(const int* pts, int n, int k, uint32_t color);
uint32_t rgb(uint8_t r, uint8_t g, uint8_t b);
void fb_flush(void);

void* memset(void* dst, int v, uint32_t n);
void* memcpy(void* dst, const void* src, uint32_t n);
int strlen(const char* s);
int strcmp(const char* a, const char* b);
char* strcpy(char* d, const char* s);
char* strcat(char* d, const char* s);
void itoa(int n, char* buf);

int kprintf(const char* fmt, ...);
void kputs(const char* s);
void kputc(char c);
void vga_scroll(void);

void kernel_main(uint32_t magic, uint32_t mboot);
void kernel_selftest(void);

void pmm_init(uint32_t mboot_addr);
uint32_t pmm_alloc_page(void);
void pmm_free_page(uint32_t phys);
uint32_t pmm_total_pages(void);
uint32_t pmm_free_pages(void);

void vmm_init(void);
void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags);
void vmm_unmap_page(uint32_t virt);
uint32_t vmm_virt_to_phys(uint32_t virt);

void kmalloc_init(void);
void* kmalloc(uint32_t size);
void kfree(void* ptr);
uint32_t kmalloc_used(void);
uint32_t kmalloc_total(void);

extern const uint8_t font8x8_basic[128][8];

void fb_cn_char(int x, int y, uint32_t cp, uint32_t fg, uint32_t bg);
void fb_cn_string(int x, int y, const char* s, uint32_t fg, uint32_t bg);

/* UTF-8 工具：utf8_decode 解码 *p 处的码点并前移指针；utf8_count 统计码点数。 */
uint32_t utf8_decode(const char** p);
int utf8_count(const char* s);

void outl(uint16_t port, uint32_t val);
uint32_t inl(uint16_t port);

#include "pmm.h"
#include "vmm.h"
#include "heap.h"
#include "pci.h"
#include "driver.h"
#include "ata.h"
#include "vfs.h"
#include "fat32.h"

#endif
