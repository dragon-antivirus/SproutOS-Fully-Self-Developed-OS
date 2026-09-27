
#include "kernel.h"
#include "browser_osdep.h"
#include "net.h"



uint32_t* fb_get_buffer(void){ return fb_back; }
int       fb_width(void){ return (int)fb.width; }
int       fb_height(void){ return (int)fb.height; }
void      fb_present(void){ fb_flush(); }


void fb_fill_rect(int x, int y, int w, int h, uint32_t color){
    fb_rect(x, y, w, h, color);
}


void fb_draw_rect(int x, int y, int w, int h, uint32_t color){
    if (w <= 0 || h <= 0) return;
    fb_rect(x, y, w, 1, color);
    fb_rect(x, y + h - 1, w, 1, color);
    fb_rect(x, y, 1, h, color);
    fb_rect(x + w - 1, y, 1, h, color);
}


void fb_draw_ascii(int x, int y, char c, uint32_t color){
    fb_char(x, y, (int)(unsigned char)c, color, (uint32_t)-1);
}

int fb_ascii_w(void){ return 8; }
int fb_cn_w(void){   return 16; }
int fb_line_h(void){ return 20; }



static int sym_fallback(unsigned int cp){
    switch (cp){
        case 0x2B05: return '<';   
        case 0x27A1: return '>';   
        case 0x27F3: return 'R';   
        case 0x2302: return 'H';   
        case 0x2715: return 'X';   
        case 0x25BC: return 'v';   
        default:     return -1;
    }
}


/* 浏览器中文/符号绘制：UTF-8 文本，逐码点绘制。
 * - ASCII（<0x80）走 8x8 字模，占 8px；
 * - 工具栏符号（← → ⟳ ⌂ ✕ ▼）若没有对应 CJK 字形，回退 ASCII 近似，占 16px 对齐；
 * - 其余 CJK 码点直接交给 fb_cn_char（运行时 TTF 栅格化，失败回退点阵/空框），占 16px。 */
void fb_draw_cn(int x, int y, const char* utf8, uint32_t color){
    if (!utf8 || !*utf8) return;
    int cx = x;
    while (*utf8){
        uint32_t cp = utf8_decode(&utf8);
        if (cp < 0x80){
            fb_draw_ascii(cx, y, (char)cp, color);
            cx += 8;
        } else {
            int fb = sym_fallback(cp);
            if (fb >= 0){
                fb_draw_ascii(cx, y, (char)fb, color);
                cx += 16;
            } else {
                fb_cn_char(cx, y, cp, color, (uint32_t)-1);
                cx += 16;
            }
        }
    }
}


void fb_draw_rgba(int x, int y, int w, int h, const uint8_t* rgba){
    if (!rgba || w <= 0 || h <= 0) return;
    uint32_t rowpix = fb.pitch / 4;
    uint32_t* buf = fb_back ? fb_back : fb.addr;
    for (int j = 0; j < h; j++){
        int py = y + j;
        if (py < 0 || py >= (int)fb.height) continue;
        for (int i = 0; i < w; i++){
            int px = x + i;
            if (px < 0 || px >= (int)fb.width) continue;
            int si = (j * w + i) * 4;
            uint8_t r = rgba[si], g = rgba[si+1], b = rgba[si+2], a = rgba[si+3];
            if (a == 0) continue;
            buf[(uint32_t)py * rowpix + (uint32_t)px] =
                ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
}



int mouse_x(void){ return mouse.x; }
int mouse_y(void){ return mouse.y; }
int mouse_down(void){ return mouse.left; }



int kbd_poll(int* out_key){
    int k;
    if (kgetc_ext(&k)){ *out_key = k; return 1; }
    if (kpeek()){ *out_key = (int)(unsigned char)kgetc(); return 1; }
    return 0;
}



#define BFD_MAX 8
typedef struct { int used; uint8_t* data; uint32_t size; uint32_t off; } bfd_t;
static bfd_t g_bfd[BFD_MAX];

static int browser_path_resolve(const char* path, uint32_t* out_cluster, uint32_t* out_size){
    uint32_t cur = fat32_root_cluster();
    const char* p = path;
    while (*p == '/') p++;
    if (*p == 0) return -1;
    serial_write("[BFS] resolve: root_cl=");
    serial_puti((int)cur);
    serial_write(" path=");
    serial_write(p);
    serial_write("\n");
    int step = 0;
    while (*p){
        const char* slash = p;
        while (*slash && *slash != '/') slash++;
        int len = (int)(slash - p);
        if (len == 0){ p = slash + 1; continue; }
        char nm[13];
        if (len > 12) len = 12;
        int i; for (i = 0; i < len; i++) nm[i] = p[i];
        nm[i] = 0;
        vfs_dirent_t e;
        step++;
        serial_write("[BFS] step ");
        serial_puti(step);
        serial_write(" lookup \"");
        serial_write(nm);
        serial_write("\" in dir cl=");
        serial_puti((int)cur);
        if (fat32_open_in_dir(cur, nm, &e) != 0) {
            serial_write(" -> NOT FOUND!\n");
            return -1;
        }
        serial_write(" -> OK");
        if (e.is_dir) { serial_write(" [DIR] cl="); serial_puti((int)e.first_cluster); }
        else { serial_write(" [FILE] size="); serial_puti((int)e.size); }
        serial_write("\n");
        if (*slash == '/'){
            if (!e.is_dir) return -1;
            cur = e.first_cluster;
            p = slash + 1;
        } else {
            *out_cluster = e.first_cluster;
            *out_size   = e.size;
            return 0;
        }
    }
    return -1;
}

int browser_vfs_open(const char* path, int flags){
    (void)flags;
    uint32_t cluster, size;
    serial_write("[BFS] open: ");
    serial_write(path);
    if (browser_path_resolve(path, &cluster, &size) != 0) {
        serial_write(" -> RESOLVE FAIL\n");
        return -1;
    }
    serial_write(" -> cl=");
    serial_puti((int)cluster);
    serial_write(" size=");
    serial_puti((int)size);
    int fd = -1;
    for (int i = 0; i < BFD_MAX; i++){ if (!g_bfd[i].used){ fd = i; break; } }
    if (fd < 0) { serial_write(" NO_FREE_FD\n"); return -1; }
    uint32_t cap = size ? size : 1;
    uint8_t* buf = kmalloc(cap);
    if (!buf) { serial_write(" KMALLOC_FAIL\n"); return -1; }
    uint32_t rl = 0;
    fat32_read_file(cluster, buf, cap, &rl);
    serial_write(" read=");
    serial_puti((int)rl);
    serial_write("/");
    serial_puti((int)cap);
    serial_write("\n");
    g_bfd[fd].used = 1;
    g_bfd[fd].data = buf;
    g_bfd[fd].size = rl;
    g_bfd[fd].off  = 0;
    return fd + 1;
}

int browser_vfs_read(int fd, void* buf, int len){
    fd -= 1;
    if (fd < 0 || fd >= BFD_MAX || !g_bfd[fd].used) return -1;
    bfd_t* b = &g_bfd[fd];
    if (b->off >= b->size) return 0;
    uint32_t n = (uint32_t)len;
    if (n > b->size - b->off) n = b->size - b->off;
    if (n > 0) memcpy(buf, b->data + b->off, n);
    b->off += n;
    return (int)n;
}

void browser_vfs_close(int fd){
    fd -= 1;
    if (fd < 0 || fd >= BFD_MAX || !g_bfd[fd].used) return;
    kfree(g_bfd[fd].data);
    g_bfd[fd].used = 0;
    g_bfd[fd].data = 0;
    g_bfd[fd].size = 0;
    g_bfd[fd].off  = 0;
}


int net_http_get(const char* url, uint8_t** out_buf, int* out_len){
    *out_buf = 0; *out_len = 0;
    if (!url || url[0]!='h'||url[1]!='t'||url[2]!='t'||url[3]!='p'||url[4]!=':'||url[5]!='/'||url[6]!='/') return -1;
    const char* p = url + 7;
    if (url[7] == 's' && url[8] == '/') p += 2;  /* skip "s" in https:// */
    const char* host = p;
    while (*p && *p != '/' && *p != ':') p++;
    int hlen = (int)(p - host);
    if (hlen <= 0 || hlen > 253) return -1;
    char hs[256];
    int i; for (i = 0; i < hlen; i++) hs[i] = host[i]; hs[hlen] = 0;
    const char* path = "/";
    if (*p == ':'){
        p++;
        while (*p && *p >= '0' && *p <= '9') p++;
        if (*p == '/') path = p++; else if (*p) path = "/";
    } else if (*p == '/'){
        path = p;
    } else {
        path = "/";
    }
    uint32_t blen = 0;
    uint8_t* body = 0;
    if (http_get(hs, path, &body, &blen) != 0) return -1;
    *out_buf = body;
    *out_len = (int)blen;
    return 0;
}
