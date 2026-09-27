

#ifndef SPROUTOS_BROWSER_H
#define SPROUTOS_BROWSER_H

#include <stddef.h>
#include <stdint.h>




extern uint32_t* fb_get_buffer(void);   
extern int       fb_width(void);        
extern int       fb_height(void);       
extern void      fb_present(void);      
extern void      fb_fill_rect(int x, int y, int w, int h, uint32_t color);
extern void      fb_draw_rect(int x, int y, int w, int h, uint32_t color); 
extern void      fb_draw_ascii(int x, int y, char c, uint32_t color);


extern void      fb_draw_cn(int x, int y, const char* utf8, uint32_t color);
extern int       fb_ascii_w(void);      
extern int       fb_cn_w(void);         
extern int       fb_line_h(void);       

extern void      fb_draw_rgba(int x, int y, int w, int h, const uint8_t* rgba);


#define KEY_NONE    0
#define KEY_BACK    8
#define KEY_TAB     9
#define KEY_ENTER   13
#define KEY_ESC     27
#define KEY_LEFT    0x100
#define KEY_RIGHT   0x101
#define KEY_UP      0x102
#define KEY_DOWN    0x103

extern int  mouse_x(void);          
extern int  mouse_y(void);          
extern int  mouse_down(void);       


extern int  kbd_poll(int* out_key);


#define VFS_RD 0
extern int  vfs_open(const char* path, int flags); 
extern int  vfs_read(int fd, void* buf, int len);   
extern void vfs_close(int fd);


extern void* kmalloc(size_t n);
extern void  kfree(void* p);
extern size_t kmalloc_used(void);    


extern int kprintf(const char* fmt, ...);




void browser_start(void);



void browser_on_mouse(int x, int y, int down);
void browser_on_key(int key);

#endif 
