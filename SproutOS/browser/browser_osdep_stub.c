/*
 * browser_osdep_stub.c — 主机端 OS 依赖桩（仅用于脱离内核做语法/链接自检）
 *
 * ⚠️ 重要：本文件用于你在普通 PC 上用 gcc 验证 browser.c 能编译、链接、跑通主循环，
 *           **千万不要编入真实 SproutOS 内核镜像**（会与真实内核符号冲突）。
 *           真实内核直接编 browser.c + browser.h（接口按 OS_API_CONTRACT.md 映射）。
 *
 * 自检命令（需要本机有 gcc）：
 *     gcc -std=c99 -Wall -Wextra -c browser.c -o /tmp/browser.o        # 纯语法体检
 *     gcc -std=c99 -Wall -Wextra browser.c browser_osdep_stub.c -o /tmp/btest && /tmp/btest   # 链接+运行
 */

#include "browser.h"
#include <stdlib.h>
#include <string.h>

static uint32_t g_fb[1024*768];

uint32_t* fb_get_buffer(void){ return g_fb; }
int fb_width(void){ return 1024; }
int fb_height(void){ return 768; }

/* 跑满若干帧后自动退出，避免主机测试陷入死循环 */
void fb_present(void){ static int n=0; if(++n > 120) exit(0); }

void fb_fill_rect(int x,int y,int w,int h,uint32_t c){ (void)x;(void)y;(void)w;(void)h;(void)c; }
void fb_draw_rect(int x,int y,int w,int h,uint32_t c){ (void)x;(void)y;(void)w;(void)h;(void)c; }
void fb_draw_ascii(int x,int y,char c,uint32_t col){ (void)x;(void)y;(void)c;(void)col; }
void fb_draw_cn(int x,int y,const char* u,uint32_t col){ (void)x;(void)y;(void)u;(void)col; }
int fb_ascii_w(void){ return 8; }
int fb_cn_w(void){ return 16; }
int fb_line_h(void){ return 20; }
void fb_draw_rgba(int x,int y,int w,int h,const uint8_t* p){ (void)x;(void)y;(void)w;(void)h;(void)p; }

int mouse_x(void){ return 0; }
int mouse_y(void){ return 0; }
int mouse_down(void){ return 0; }
int kbd_poll(int* k){ *k=0; return 0; }

/* VFS 桩：一律返回“文件不存在”，用于验证容错路径不崩溃 */
int vfs_open(const char* p,int f){ (void)p;(void)f; return -1; }
int vfs_read(int fd,void* b,int l){ (void)fd;(void)b;(void)l; return 0; }
void vfs_close(int fd){ (void)fd; }

void* kmalloc(size_t n){ return malloc(n); }
void  kfree(void* p){ free(p); }
size_t kmalloc_used(void){ return 0; }
int kprintf(const char* f,...){ (void)f; return 0; }

int main(void){
    browser_start();
    return 0;
}
