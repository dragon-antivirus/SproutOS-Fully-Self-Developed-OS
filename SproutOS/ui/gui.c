#include "kernel.h"
#include "vfs.h"
#include "net.h"
#include "ata.h"                /* ata_get_bdev: 硬盘扇区数 */

#define MAX_WINS 16

/* 编辑器“保存”按钮布局常量（gui_handle_input 与 draw_editor 共用） */
#define EDIT_BTN_X 10
#define EDIT_BTN_Y 25
#define EDIT_BTN_W 56
#define EDIT_BTN_H 18

/* 填充圆角矩形（用于桌面图标方块），定义在 gui_draw_desktop 之后，这里前向声明 */
static void fill_round_rect(int x, int y, int w, int h, int r, uint32_t c);
/* 1px 边框矩形（用于徽章等），定义在 gui_draw_desktop 之后，这里前向声明 */
static void outline(int x, int y, int w, int h, uint32_t c);

/* 开始菜单 / “开始”按钮布局常量（gui_handle_input 与 gui_draw_taskbar 共用，
 * 必须在所有使用它们的函数之前定义） */
#define MENU_ITEMS 7
#define MENU_ITEM_H 24
#define MENU_W 200
#define START_BTN_X 8
#define START_BTN_Y (fb.height - 24)
#define START_BTN_W 60
#define START_BTN_H 20

static window_t wins[MAX_WINS];
static int win_count = 0;
static int focus = -1;
static int drag_win = -1;

static int g_clicked = 0;
static int g_start_menu = 0;

static int files_mode = 0;
static vfs_dir_t files_dir;
static vfs_dir_t recycle_dir;        /* 回收站内容列表 */
static int recycle_need_refresh = 1;
static int recycle_scroll = 0;
static char g_recycle_name[13];      /* 回收站右键选中的文件名 */
static uint8_t files_buf[4096];
static uint32_t files_len = 0;
static int files_need_refresh = 1;
static int files_scroll = 0;

static const char* app_names[] = {
    "Files", "Editor", "Term", "About", "Paint", "Recycle", "Browser", 0
};

/* 桌面图标显示顺序（屏幕从上到下）：文件管理 / 回收站 / 终端 / 其他应用 / 浏览器。
 * 注意：这只是“显示顺序”，app_names[] 的索引顺序保持不变（Editor=1、Paint=4
 * 仍被 default_app_for / gui_open_file_in_app / launcher_open 等硬编码依赖）。
 * 浏览器索引为 6，启动走 launcher_open(6) 的全屏独占分支。 */
static const int g_desktop_order[] = { 0, 5, 2, 1, 3, 4, 6 };

/* 前向声明：编辑器/终端的初始化与按键处理（在 gui_new_window 里调用） */
static void editor_reset(void);
static void editor_save(void);
static void term_reset(void);
static void editor_on_key(char c);
static void term_on_key(char c);

/* ===== 画图（Paint）应用 =====
 * 256x192 画布 + 调色板 + 铅笔/橡皮 + 自定义保存文件名。 */
#define PAINT_W 256
#define PAINT_H 192
#define PAINT_OFFX 8       /* 画布可用区左边界（窗口内 x） */
#define PAINT_OFFY 70      /* 画布可用区上边界（标题栏+两排工具条之下） */
static uint32_t paint_canvas[PAINT_W * PAINT_H];
static int paint_pen_color = 0;     /* 当前画笔颜色（0 = 黑） */
static int paint_tool = 0;          /* 0 铅笔 / 1 橡皮 */
static char paint_fname[64];
static int paint_fname_edit = 0;
static int paint_drawing = 0;
static int paint_prev_x = 0, paint_prev_y = 0;
static int paint_status = 0;        /* 0 无 / 1 已保存 / 2 已修改 */

/* 文本编辑器：自定义保存文件名 */
static char editor_fname[64];
static int editor_fname_edit = 0;

/* 文件打开分发 / 右键上下文菜单 */
static int g_right_prev = 0;
static int g_right_clicked = 0;
static int g_ctx_active = 0;
static int g_ctx_x = 0, g_ctx_y = 0;
static char g_ctx_name[13];
static int g_suppress_reset = 0;    /* 打开具名文件时跳过默认 reset */

/* 剪贴板（复制/剪切的源文件名，cut=1 表示粘贴后删源；g_clip_dir 记录源目录首簇，支持跨目录粘贴） */
static char g_clip_name[64];
static int  g_clip_cut = 0;
static uint32_t g_clip_dir = 0;
/* 文件重命名编辑态：files_rename_edit=1 时键盘输入进入 files_rename_new；
 * files_rename_old 为原文件名（回车后 fat32_move 在 cwd 内改名）。 */
static int  files_rename_edit = 0;
static char files_rename_old[13];
static char files_rename_new[13];
/* 文件管理器状态提示（中文点阵索引字节串） */
static char g_files_status[16];

/* 右键菜单项模型：动态构造，支持 打开/复制/剪切/删除(红)/粘贴/新建文件夹/重命名 */
#define CTX_MAX 10
#define ACT_OPEN   0
#define ACT_COPY   1
#define ACT_CUT    2
#define ACT_DELETE 3
#define ACT_PASTE  4
#define ACT_WP     5   /* 设为桌面壁纸（图片） */
#define ACT_RESTORE 6  /* 回收站：还原 */
#define ACT_PURGE   7  /* 回收站：永久删除 */
#define ACT_NEWFOLDER 8  /* 新建文件夹 */
#define ACT_RENAME   9  /* 重命名 */
static int      g_ctx_n = 0;
static uint8_t  g_ctx_act[CTX_MAX];
static uint8_t  g_ctx_red[CTX_MAX];
static char     g_ctx_lbl[CTX_MAX][32];

/* 前向声明：画图 / 文件打开分发 */
static void paint_reset(void);
static void paint_open_named(const char* name);
static void paint_save(void);
static void paint_fname_on_key(char c);
static void paint_plot(int cx, int cy, uint32_t color);
static void paint_line(int x0, int y0, int x1, int y1, uint32_t color);
static void paint_display_rect(window_t* w, int* dx, int* dy, int* dw, int* dh);
static void draw_paint(int idx);
static void draw_recycle(int idx);
static void editor_open_named(const char* name);
static void editor_fname_on_key(char c);
static int default_app_for(const char* name);
void gui_open_file_in_app(int app_idx, const char* name);
static void ctx_add(uint8_t act, uint8_t red, const char* lbl);
static void set_status(const char* s);
static void build_paste_name(const char* src, char* dst);
static void files_new_folder(void);
static void files_rename_on_key(char c);

/* 画图调色板（用 0xRRGGBB 字面量，避免 rgb() 非常量初始化） */
static const uint32_t paint_palette[8] = {
    0x000000, 0xFFFFFF, 0xE53935, 0x22C55E,
    0x3B82F6, 0xF5C518, 0xEC4899, 0x00BCD4
};

/* 有界字符串拷贝：最多拷 n-1 个字符并补 '\0'（内核未提供 strncpy，避免隐式声明） */
static void fname_copy(char* dst, const char* src, int n) {
    int i = 0;
    while (i < n - 1 && src[i] != 0) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

void gui_draw_mouse(void) {
    int mx = mouse.x, my = mouse.y;
    int pts[14][2] = {
        {0,0},{0,15},{2,11},{5,17},{7,16},{4,11},{8,11}
    };
    uint32_t fg = rgb(255, 255, 255);
    uint32_t ol = rgb(0, 0, 0);
    for (int i = 1; i < 14; i++) {
        int x0 = mx + pts[i - 1][0];
        int y0 = my + pts[i - 1][1];
        int x1 = mx + pts[i][0];
        int y1 = my + pts[i][1];
        int dx = x1 - x0, dy = y1 - y0;
        int steps = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
        if (steps == 0) continue;
        for (int s = 0; s <= steps; s++) {
            int x = x0 + dx * s / steps;
            int y = y0 + dy * s / steps;
            fb_pixel(x, y, ol);
        }
    }
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++)
            if (i + j < 9) fb_pixel(mx + i, my + j, fg);
}

static int in_title(int idx) {
    return mouse.left && !wins[idx].dragging
        && mouse.x >= wins[idx].x && mouse.x <= wins[idx].x + wins[idx].w
        && mouse.y >= wins[idx].y && mouse.y <= wins[idx].y + 22;
}

int gui_new_window(const char* title, uint32_t color, int w, int h) {
    if (win_count >= MAX_WINS) { g_suppress_reset = 0; return -1; }
    int idx = win_count++;
    window_t* wn = &wins[idx];
    wn->active = 1;
    wn->x = 60 + (win_count * 24) % 200;
    wn->y = 60 + (win_count * 18) % 120;
    wn->w = w;
    wn->h = h;
    int tl = strlen(title);
    if (tl > 23) tl = 23;
    for (int i = 0; i < tl; i++) wn->title[i] = title[i];
    wn->title[tl] = 0;
    wn->title_color = color;
    wn->dragging = 0;
    wn->minimized = 0;
    wn->maximized = 0;
    wn->ox = wn->x; wn->oy = wn->y; wn->ow = wn->w; wn->oh = wn->h;
    if (!g_suppress_reset) {
        if (!strcmp(title, "Files")) {
            while (vfs_chdir_up() == 0) { }
            files_need_refresh = 1;
            files_mode = 0;
            files_scroll = 0;
            files_len = 0;
        } else if (!strcmp(title, "Editor")) {
            editor_reset();
        } else if (!strcmp(title, "Term")) {
            term_reset();
        } else if (!strcmp(title, "Paint")) {
            paint_reset();
        }
    }
    g_suppress_reset = 0;   /* 无论是否跳过，复位一次性标志 */
    focus = idx;
    return idx;
}

void gui_close_window(int idx) {
    if (idx < 0 || idx >= win_count) return;
    if (!strcmp(wins[idx].title, "Files")) g_ctx_active = 0;   /* 关文件管理器时收起右键菜单 */
    if (!strcmp(wins[idx].title, "Recycle")) g_ctx_active = 0; /* 关回收站时收起右键菜单 */
    for (int i = idx; i < win_count - 1; i++) wins[i] = wins[i + 1];
    win_count--;
    focus = win_count - 1;
}

int gui_get_focus(void) { return focus; }

/* 把一条菜单项加入当前菜单（最多 CTX_MAX 条）。lbl 为中文点阵索引字节串。 */
static void ctx_add(uint8_t act, uint8_t red, const char* lbl) {
    if (g_ctx_n >= CTX_MAX) return;
    g_ctx_act[g_ctx_n] = act;
    g_ctx_red[g_ctx_n] = red;
    int j = 0;
    while (lbl[j] && j < 31) { g_ctx_lbl[g_ctx_n][j] = lbl[j]; j++; }
    g_ctx_lbl[g_ctx_n][j] = 0;
    g_ctx_n++;
}

/* 设置文件管理器状态提示（中文点阵索引字节串）。 */
static void set_status(const char* s) {
    int i = 0;
    while (s[i] && i < (int)sizeof(g_files_status) - 1) { g_files_status[i] = s[i]; i++; }
    g_files_status[i] = 0;
}

/* 根据剪贴板源名构造粘贴目标名：若当前目录已存在同名文件，追加 _c1/_c2… 后缀。 */
static void build_paste_name(const char* src, char* dst) {
    vfs_dirent_t e;
    if (vfs_open(src, &e) != 0) { fname_copy(dst, src, 64); return; }  /* 不存在则直接用原名 */
    const char* dot = 0;
    for (const char* p = src; *p; p++) if (*p == '.') dot = p;
    int base_len = dot ? (int)(dot - src) : (int)strlen(src);
    for (int n = 1; n <= 99; n++) {
        int di = 0;
        for (int i = 0; i < base_len; i++) dst[di++] = src[i];
        dst[di++] = '_'; dst[di++] = 'c';
        if (n >= 10) dst[di++] = (char)('0' + n / 10);
        dst[di++] = (char)('0' + n % 10);
        if (dot) { for (const char* p = dot; *p; ) dst[di++] = *p++; }
        dst[di] = 0;
        vfs_dirent_t e2;
        if (vfs_open(dst, &e2) != 0) return;   /* 找到空闲名 */
    }
    fname_copy(dst, src, 64);   /* 极端情况兜底 */
}

/* 右键上下文菜单：命中文件管理器里的文件，或空白处（用于粘贴）则弹出。
 * 菜单内容：打开 / 复制 / 剪切 / 删除(红) / 粘贴（有剪贴板时）。 */
static int ctx_menu_open(void) {
    if (focus < 0 || focus >= win_count) return 0;
    const char* t = wins[focus].title;
    window_t* w = &wins[focus];
    g_ctx_n = 0;

    /* 回收站：右键列表项 → 还原 / 删除(红) */
    if (!strcmp(t, "Recycle")) {
        int list_top = w->y + 74, row_h = 18;
        if (mouse.x > w->x + 10 && mouse.x < w->x + w->w - 6 &&
            mouse.y > list_top && mouse.y < w->y + w->h - 4) {
            int i = (mouse.y - list_top) / row_h + recycle_scroll;
            if (i >= 0 && i < recycle_dir.count) {
                const char* src = recycle_dir.entries[i].name;
                int cn = 0;
                while (cn < (int)sizeof(g_recycle_name) - 1 && src[cn]) { g_recycle_name[cn] = src[cn]; cn++; }
                g_recycle_name[cn] = 0;
                ctx_add(ACT_RESTORE, 0, "还原");   /* 还原（还75 原76） */
                ctx_add(ACT_PURGE,   1, "删除");   /* 删除（红） */
            }
        }
        if (g_ctx_n == 0) { g_ctx_active = 0; return 0; }
        int mw = 150, mh = g_ctx_n * 24;
        g_ctx_x = mouse.x; g_ctx_y = mouse.y;
        if (g_ctx_x + mw > (int)fb.width)       g_ctx_x = (int)fb.width - mw;
        if (g_ctx_y + mh > (int)fb.height - 28) g_ctx_y = (int)fb.height - 28 - mh;
        g_ctx_active = 1;
        return 1;
    }

    /* 文件管理器：右键文件 → 打开/复制/剪切/壁纸/删除/重命名；空白处 → 新建文件夹/粘贴 */
    if (strcmp(t, "Files")) return 0;
    if (files_mode != 0) return 0;
    int list_top = w->y + 74, row_h = 18;
    int targeted = 0;
    if (mouse.x > w->x + 10 && mouse.x < w->x + w->w - 6 &&
        mouse.y > list_top && mouse.y < w->y + w->h - 4) {
        int i = (mouse.y - list_top) / row_h + files_scroll;
        if (i >= 0 && i < files_dir.count && !files_dir.entries[i].is_dir) {
            const char* src = files_dir.entries[i].name;
            int cn = 0;
            while (cn < (int)sizeof(g_ctx_name) - 1 && src[cn]) { g_ctx_name[cn] = src[cn]; cn++; }
            g_ctx_name[cn] = 0;
            targeted = 1;
        }
    }
    ctx_add(ACT_NEWFOLDER, 0, "新建文件夹");   /* 新建文件夹（始终可用，在 cwd 建目录） */
    if (targeted) {
        ctx_add(ACT_OPEN,   0, "打开");            /* 打开 */
        ctx_add(ACT_COPY,   0, "复制");            /* 复制 */
        ctx_add(ACT_CUT,    0, "剪切");            /* 剪切 */
        int n = (int)strlen(g_ctx_name);
        if (n >= 4 && (!strcmp(g_ctx_name + n - 4, ".bmp") ||
                       !strcmp(g_ctx_name + n - 4, ".BMP")))
            ctx_add(ACT_WP, 0, "设为桌面壁纸");  /* 设为桌面壁纸 */
        ctx_add(ACT_DELETE, 1, "删除");            /* 删除（红） */
        ctx_add(ACT_RENAME, 0, "重命名");            /* 重命名（重77 命78） */
    }
    if (g_clip_name[0] != 0) {
        ctx_add(ACT_PASTE,  0, "粘贴");            /* 粘贴 */
    }
    if (g_ctx_n == 0) { g_ctx_active = 0; return 0; }   /* 没有任何可选项则不弹 */
    int mw = 150, mh = g_ctx_n * 24;
    g_ctx_x = mouse.x; g_ctx_y = mouse.y;
    if (g_ctx_x + mw > (int)fb.width)       g_ctx_x = (int)fb.width - mw;
    if (g_ctx_y + mh > (int)fb.height - 28) g_ctx_y = (int)fb.height - 28 - mh;
    g_ctx_active = 1;
    return 1;
}

/* 命中菜单项则处理并关闭；返回 1 表示已消费点击 */
static int ctx_menu_click(void) {
    int mw = 150, mh = g_ctx_n * 24;
    if (mouse.x < g_ctx_x || mouse.x > g_ctx_x + mw ||
        mouse.y < g_ctx_y || mouse.y > g_ctx_y + mh) {
        g_ctx_active = 0; return 0;   /* 点菜单外：关闭，未处理 */
    }
    int item = (mouse.y - g_ctx_y) / 24;
    if (item < 0 || item >= g_ctx_n) { g_ctx_active = 0; return 0; }
    uint8_t act = g_ctx_act[item];
    g_ctx_active = 0;
    if (act == ACT_OPEN) {
        int app = default_app_for(g_ctx_name);
        if (app == 1)      gui_open_file_in_app(1, g_ctx_name);
        else if (app == 4) gui_open_file_in_app(4, g_ctx_name);
        else {                         /* 其它文件：文本预览 */
            vfs_dirent_t e;
            if (vfs_open(g_ctx_name, &e) == 0) {
                uint32_t rl = 0;
                vfs_read(&e, files_buf, sizeof(files_buf) - 1, &rl);
                files_len = rl;
                if (files_len > (int)sizeof(files_buf) - 1) files_len = (int)sizeof(files_buf) - 1;
                files_buf[files_len] = 0;
                files_mode = 1;
            }
        }
        return 1;
    } else if (act == ACT_COPY) {
        fname_copy(g_clip_name, g_ctx_name, sizeof(g_clip_name));
        g_clip_dir = vfs_cwd_cluster();        /* 记录源目录，支持跨目录粘贴 */
        g_clip_cut = 0;
        set_status("已复制");            /* 已复制 */
        return 1;
    } else if (act == ACT_CUT) {
        fname_copy(g_clip_name, g_ctx_name, sizeof(g_clip_name));
        g_clip_dir = vfs_cwd_cluster();
        g_clip_cut = 1;
        set_status("已剪切");            /* 已剪切 */
        return 1;
    } else if (act == ACT_DELETE) {
        /* 删除 = 移入回收站（而非真删），文件可在“回收站”窗口还原/永久删除 */
        if (vfs_recycle(g_ctx_name) == 0) {
            files_need_refresh = 1;
            recycle_need_refresh = 1;
            set_status("已删除");        /* 已删除（已移入回收站） */
        } else {
            set_status("已删除");
        }
        return 1;
    } else if (act == ACT_PASTE) {
        char dst[64];
        build_paste_name(g_clip_name, dst);
        if (g_clip_dir < 2) g_clip_dir = vfs_cwd_cluster();
        if (vfs_copy_file(g_clip_name, g_clip_dir, dst, vfs_cwd_cluster()) == 0) {
            files_need_refresh = 1;
            set_status("已粘贴");        /* 已粘贴 */
            if (g_clip_cut) {
                vfs_unlink_in_dir(g_clip_dir, g_clip_name);
                g_clip_name[0] = 0; g_clip_cut = 0; g_clip_dir = 0;
            }
        } else {
            set_status("已删除");        /* 粘贴失败（串口的 [VFS] copy 会打印原因） */
        }
        return 1;
    } else if (act == ACT_NEWFOLDER) {
        files_new_folder();
        return 1;
    } else if (act == ACT_RENAME) {
        /* 进入重命名编辑态：键盘输入写入 files_rename_new，回车确认（cwd 内改名） */
        files_rename_edit = 1;
        fname_copy(files_rename_old, g_ctx_name, sizeof(files_rename_old));
        fname_copy(files_rename_new, g_ctx_name, sizeof(files_rename_new));
        set_status("重命名");                /* 重命名 */
        return 1;
    } else if (act == ACT_WP) {
        gui_set_wallpaper_from_file(g_ctx_name);
        return 1;
    } else if (act == ACT_RESTORE) {
        if (vfs_restore(g_recycle_name) == 0) {
            recycle_need_refresh = 1;
            set_status("已还原");        /* 已还原（已72 还75 原76） */
        }
        return 1;
    } else if (act == ACT_PURGE) {
        if (vfs_purge(g_recycle_name) == 0) {
            recycle_need_refresh = 1;
            set_status("已删除");        /* 已删除 */
        }
        return 1;
    }
    return 1;
}

void gui_handle_input(int clicked) {
    g_clicked = clicked;
    /* 右键边沿检测（mouse.right 当前状态，g_right_prev 上一帧） */
    g_right_clicked = (mouse.right && !g_right_prev);
    g_right_prev = mouse.right;

    /* 右键：在文件管理器里右键文件 → 弹菜单；否则关闭已开菜单 */
    if (g_right_clicked) {
        g_start_menu = 0;
        if (ctx_menu_open()) return;
        g_ctx_active = 0;
        return;
    }
    /* 上下文菜单激活时，左键处理菜单项；点外部则关闭 */
    if (g_ctx_active) {
        if (clicked) { ctx_menu_click(); return; }
    }

    if (clicked) {
        /* 1) 任务栏"开始"按钮：点击切换开始菜单 */
        {
            int sby = (int)START_BTN_Y;
            if (mouse.x >= START_BTN_X && mouse.x <= START_BTN_X + START_BTN_W &&
                mouse.y >= sby && mouse.y <= sby + (int)START_BTN_H) {
                g_start_menu = !g_start_menu;
                return;
            }
        }
        /* 2) 开始菜单打开时，点击菜单项或外部 */
        if (g_start_menu) {
            int mx0 = START_BTN_X;
            int my0 = START_BTN_Y - MENU_ITEMS * MENU_ITEM_H;
            int mx1 = mx0 + MENU_W;
            int my1 = my0 + MENU_ITEMS * MENU_ITEM_H;
            if (mouse.x >= mx0 && mouse.x < mx1 && mouse.y >= my0 && mouse.y < my1) {
                int item = (mouse.y - my0) / MENU_ITEM_H;
                if (item >= 0 && item < MENU_ITEMS) {
                    g_start_menu = 0;
                    if (item == 0) launcher_open(0);   /* 文件管理 */
                    else if (item == 1) launcher_open(1);  /* 编辑器   */
                    else if (item == 2) launcher_open(2);  /* 终端     */
                    else if (item == 3) launcher_open(3);  /* 关于     */
                    else if (item == 4) launcher_open(4);  /* 画图     */
                    else if (item == 5) launcher_open(6);  /* 浏览器   */
                    else                kernel_poweroff(); /* Power    */
                }
                return;   /* 菜单内的点击一律消费，不下传 */
            } else {
                g_start_menu = 0;  /* 点击菜单外部则关闭 */
                return;
            }
        }
        /* 3) 编辑器 Save 按钮（只对聚焦的编辑器窗口生效） */
        if (focus >= 0 && focus < win_count && !strcmp(wins[focus].title, "Editor")) {
            int sbx = wins[focus].x + EDIT_BTN_X;
            int sby = wins[focus].y + EDIT_BTN_Y;
            if (mouse.x >= sbx && mouse.x < sbx + EDIT_BTN_W &&
                mouse.y >= sby && mouse.y < sby + EDIT_BTN_H) {
                editor_save();
                return;
            }
        }
        /* 3.5) 编辑器文件名框 / 画图控件（聚焦窗口为对应应用时） */
        if (focus >= 0 && focus < win_count) {
            window_t* w = &wins[focus];
            if (!strcmp(w->title, "Editor")) {
                int fx = w->x + 78, fy = w->y + 28, fw = 150, fh = 16;
                if (mouse.x >= fx && mouse.x < fx + fw && mouse.y >= fy && mouse.y < fy + fh) {
                    editor_fname_edit = !editor_fname_edit;
                    return;
                }
            } else if (!strcmp(w->title, "Paint") && mouse.y > w->y + 22) {
                int px0 = w->x + 8, py0 = w->y + 26;     /* 文件名框（标题栏之下） */
                if (mouse.x >= px0 && mouse.x < px0 + 130 && mouse.y >= py0 && mouse.y < py0 + 16) {
                    paint_fname_edit = !paint_fname_edit; return;
                }
                int sbx = w->x + 145, sby = w->y + 26;   /* Save */
                if (mouse.x >= sbx && mouse.x < sbx + 50 && mouse.y >= sby && mouse.y < sby + 18) {
                    paint_save(); return;
                }
                int paly = w->y + 48;                    /* 调色板 */
                for (int i = 0; i < 8; i++) {
                    int pxx = w->x + 8 + i * 20;
                    if (mouse.x >= pxx && mouse.x < pxx + 16 && mouse.y >= paly && mouse.y < paly + 16) {
                        paint_pen_color = (int)paint_palette[i]; return;
                    }
                }
                int cbx = w->x + 168, cby = w->y + 48;   /* Clear */
                if (mouse.x >= cbx && mouse.x < cbx + 44 && mouse.y >= cby && mouse.y < cby + 16) {
                    for (int i = 0; i < PAINT_W * PAINT_H; i++) paint_canvas[i] = rgb(255, 255, 255);
                    paint_status = 2; return;
                }
                int ebx = w->x + 218, eby = w->y + 48;   /* 橡皮切换 */
                if (mouse.x >= ebx && mouse.x < ebx + 50 && mouse.y >= eby && mouse.y < eby + 16) {
                    paint_tool = (paint_tool == 1) ? 0 : 1; return;
                }
                /* 画布：用显示矩形反算缓冲区坐标（支持最大化缩放） */
                int dx, dy, dw, dh;
                paint_display_rect(w, &dx, &dy, &dw, &dh);
                int cx = (int)((mouse.x - dx) * PAINT_W / dw);
                int cy = (int)((mouse.y - dy) * PAINT_H / dh);
                if (cx >= 0 && cx < PAINT_W && cy >= 0 && cy < PAINT_H) {
                    uint32_t col = (paint_tool == 1) ? rgb(255, 255, 255) : (uint32_t)paint_pen_color;
                    paint_plot(cx, cy, col);
                    paint_drawing = 1; paint_prev_x = cx; paint_prev_y = cy;
                    return;
                }
            }
        }
        /* 4) 窗口标题栏三按钮 */
        for (int i = win_count - 1; i >= 0; i--) {
            if (wins[i].minimized) continue;
            window_t* w = &wins[i];
            int by0 = w->y + 3, by1 = w->y + 19;
            if (mouse.y < by0 || mouse.y > by1) continue;
            int rx = w->x + w->w;
            if (mouse.x >= rx - 20 && mouse.x <= rx - 4) {        /* 关闭 */
                gui_close_window(i);
                return;
            }
            if (mouse.x >= rx - 38 && mouse.x <= rx - 22) {       /* 最大化/还原 */
                gui_toggle_maximize(i);
                focus = i;
                return;
            }
            if (mouse.x >= rx - 56 && mouse.x <= rx - 40) {       /* 最小化 */
                wins[i].minimized = 1;
                if (focus == i) {
                    focus = -1;
                    for (int j = win_count - 1; j >= 0; j--)
                        if (!wins[j].minimized) { focus = j; break; }
                }
                return;
            }
        }
        /* 4) 窗口主体 / 标题拖动 */
        for (int i = win_count - 1; i >= 0; i--) {
            if (wins[i].minimized) continue;
            if (mouse.x >= wins[i].x && mouse.x <= wins[i].x + wins[i].w
                && mouse.y >= wins[i].y && mouse.y <= wins[i].y + wins[i].h) {
                focus = i;
                if (in_title(i)) {
                    if (wins[i].maximized) {
                        /* 拖动最大化窗口时先还原，再按当前鼠标位置重算拖拽偏移 */
                        gui_toggle_maximize(i);
                        wins[i].drag_dx = mouse.x - wins[i].x;
                        wins[i].drag_dy = mouse.y - wins[i].y;
                    }
                    wins[i].dragging = 1;
                    drag_win = i;
                }
                return;
            }
        }
    }
    /* 画图：按住左键在画布上连续绘制（拖动） */
    if (focus >= 0 && focus < win_count && !strcmp(wins[focus].title, "Paint") && mouse.left) {
        window_t* w = &wins[focus];
        int dx, dy, dw, dh;
        paint_display_rect(w, &dx, &dy, &dw, &dh);
        int cx = (int)((mouse.x - dx) * PAINT_W / dw);
        int cy = (int)((mouse.y - dy) * PAINT_H / dh);
        if (cx >= 0 && cx < PAINT_W && cy >= 0 && cy < PAINT_H) {
            uint32_t col = (paint_tool == 1) ? rgb(255, 255, 255) : (uint32_t)paint_pen_color;
            if (paint_drawing) paint_line(paint_prev_x, paint_prev_y, cx, cy, col);
            else { paint_plot(cx, cy, col); paint_drawing = 1; }
            paint_prev_x = cx; paint_prev_y = cy;
        }
    }
    if (mouse_released()) {
        if (drag_win >= 0) wins[drag_win].dragging = 0;
        drag_win = -1;
        paint_drawing = 0;
    }
    if (drag_win >= 0) {
        window_t* w = &wins[drag_win];
        w->x = mouse.x - w->drag_dx;
        w->y = mouse.y - w->drag_dy;
        if (w->x < -(w->w - 60)) w->x = -(w->w - 60);
        if (w->x > (int)fb.width - 60) w->x = (int)fb.width - 60;
        if (w->y < 0) w->y = 0;
        if (w->y > (int)fb.height - 28 - 22) w->y = (int)fb.height - 28 - 22;
    }
}

window_t* gui_window(int idx) {
    return &wins[idx];
}

int gui_window_count(void) { return win_count; }

/* ===== 桌面壁纸加载器 =====
 * 只支持未压缩 24/32 位 BMP，尺寸必须正好 1024x768（与帧缓冲一致）。
 * 从 /wallpaper/background.bmp 读取；找不到或格式不符则回退纯色桌面。 */
#define WP_W 1024
#define WP_H 768

static uint32_t* g_wallpaper = 0;
static int g_wallpaper_ok = 0;

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8));
}

/* 程序化生成一张默认壁纸：斜向渐变 + 顶部柔光 + 中心 logo 占位圆。
 * 不依赖磁盘上的 BMP 文件；调色比旧的更"亮眼"，避免被误认为纯色回退。 */
static void gui_make_default_wallpaper(uint32_t* wp) {
    /* 斜向渐变：左上亮蓝 → 右下深紫 */
    uint8_t tl_r = 36,  tl_g = 60,  tl_b = 120;   /* 左上 */
    uint8_t br_r = 18,  br_g = 22,  br_b = 60;    /* 右下 */
    const int CX = WP_W / 2, CY = WP_H / 2;
    const int LOGO_R = 120;
    for (int32_t y = 0; y < WP_H; y++) {
        for (int32_t x = 0; x < WP_W; x++) {
            /* 斜向 t：x 走完 0..1 + y 走完 0..1，再 /2 */
            int t = ((x * 256 / WP_W) + (y * 256 / WP_H)) / 2;
            uint8_t r = (uint8_t)((tl_r * 256 + (int32_t)(br_r - tl_r) * t) / 256);
            uint8_t g = (uint8_t)((tl_g * 256 + (int32_t)(br_g - tl_g) * t) / 256);
            uint8_t b = (uint8_t)((tl_b * 256 + (int32_t)(br_b - tl_b) * t) / 256);
            /* 顶部柔光 (0..180 → 抬亮) */
            if (y < 180) {
                int boost = (180 - y) * 30 / 180;   /* 0..30 */
                r = (uint8_t)((int)r + boost > 255 ? 255 : r + boost);
                g = (uint8_t)((int)g + boost > 255 ? 255 : g + boost);
                b = (uint8_t)((int)b + boost > 255 ? 255 : b + boost);
            }
            /* 中心 logo 占位圆（比背景亮一档） */
            int dx = x - CX, dy = y - CY;
            if (dx*dx + dy*dy <= LOGO_R*LOGO_R) {
                r = (uint8_t)((int)r + 40 > 255 ? 255 : r + 40);
                g = (uint8_t)((int)g + 40 > 255 ? 255 : g + 40);
                b = (uint8_t)((int)b + 40 > 255 ? 255 : b + 40);
            }
            wp[(uint32_t)y * WP_W + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
}

/* 解码任意尺寸未压缩 24/32 位 BMP，最近邻缩放到 outW×outH，写入 out。
 * 返回 1 成功，0 失败（非 BMP / 压缩 / 不支持 bpp / 越界）。 */
static int bmp_decode_to(const uint8_t* raw, uint32_t rl, uint32_t* out, int outW, int outH) {
    if (rl < 54 || raw[0] != 'B' || raw[1] != 'M') return 0;
    uint32_t off  = rd32(raw + 10);
    int32_t  bw   = (int32_t)rd32(raw + 18);
    int32_t  bh   = (int32_t)rd32(raw + 22);
    uint16_t bpp  = rd16(raw + 28);
    uint32_t comp = rd32(raw + 30);
    if (comp != 0) return 0;
    if (bpp != 24 && bpp != 32) return 0;
    if (bw <= 0 || bh <= 0) return 0;
    int bytespp = bpp / 8;
    uint32_t rowbytes = ((uint32_t)bw * bytespp + 3u) / 4u * 4u;
    if (off + (uint32_t)bh * rowbytes > rl) return 0;
    for (int y = 0; y < outH; y++) {
        int sy = (y * bh) / outH;
        int32_t src_row = bh - 1 - sy;   /* 翻转（BMP 自底向上） */
        const uint8_t* sp = raw + off + (uint32_t)src_row * rowbytes;
        for (int x = 0; x < outW; x++) {
            int sx = (x * bw) / outW;
            const uint8_t* p = sp + sx * bytespp;
            out[(uint32_t)y * outW + x] = ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
        }
    }
    return 1;
}

/* 运行时把某个 VFS 上的 BMP 设为桌面壁纸（右键菜单调用）。
 * 解码后缩放到 1024x768 覆盖 g_wallpaper，下一帧自动生效。 */
int gui_set_wallpaper_from_file(const char* name) {
    if (!g_wallpaper_ok || !g_wallpaper) return 0;
    if (!vfs_is_mounted()) return 0;
    vfs_dirent_t e;
    if (vfs_open(name, &e) != 0) return 0;
    if (e.is_dir) return 0;
    uint8_t* raw = kmalloc(e.size);
    if (!raw) return 0;
    uint32_t rl = 0;
    vfs_read(&e, raw, e.size, &rl);
    int ok = bmp_decode_to(raw, rl, g_wallpaper, WP_W, WP_H);
    kfree(raw);
    serial_write(ok ? "[WP] wallpaper set from file: " : "[WP] wallpaper set failed: ");
    serial_write(name); serial_write("\n");
    return ok;
}

int gui_load_wallpaper(void) {
    if (g_wallpaper_ok) return 1;
    uint32_t* wp = kmalloc(WP_W * WP_H * 4);
    if (!wp) { serial_write("[WP] kmalloc wp failed\n"); return 0; }

    /* 优先尝试从 /wallpaper/bg.bmp 解码（用户自定义壁纸，8.3 短文件名）；
     * 任何失败都回退到程序化默认壁纸，保证壁纸一定生效。 */
    int loaded = 0;
    if (vfs_is_mounted()) {
        if (vfs_chdir("wallpaper") == 0) {
            vfs_dirent_t de;
            if (vfs_open("bg.bmp", &de) == 0) {
                uint8_t* raw = kmalloc(de.size);
                if (raw) {
                    uint32_t rl = 0;
                    vfs_read(&de, raw, de.size, &rl);
                    loaded = bmp_decode_to(raw, rl, wp, WP_W, WP_H);
                    kfree(raw);
                }
            }
            vfs_chdir_up();
        }
    }
    if (loaded) {
        serial_write("[WP] loaded 1024x768 wallpaper from /wallpaper/bg.bmp\n");
    } else {
        serial_write("[WP] no background.bmp -> using built-in default wallpaper\n");
        gui_make_default_wallpaper(wp);
    }
    g_wallpaper = wp;
    g_wallpaper_ok = 1;
    return 1;
}

static void gui_blit_wallpaper(void) {
    uint32_t* dst = fb_back ? fb_back : fb.addr;
    uint32_t rowpix = fb.pitch / 4;
    for (uint32_t y = 0; y < fb.height; y++)
        memcpy(dst + y * rowpix, g_wallpaper + y * WP_W, WP_W * 4);
}

void gui_draw_desktop(void) {
    if (g_wallpaper) {
        gui_blit_wallpaper();
    } else {
        fb_rect(0, 0, fb.width, fb.height, rgb(15, 23, 42));
        for (uint32_t i = 0; i < fb.width; i += 60)
            fb_rect(i, 0, 30, fb.height, rgb(20, 30, 55));
        for (uint32_t j = 0; j < fb.height; j += 60)
            fb_rect(0, j, fb.width, 30, rgb(20, 30, 55));
    }

    /* 桌面图标：模仿参考设计 —— 大号圆角彩色方块 + 方块内符号 + 下方白色中文标签。
     *   - 方块 64×64，圆角半径 12，竖向单列（起始 (24,24)，步长 90）
     *   - 内部符号用 2x 放大白字；下方标签为白色中文（带黑色阴影，保证在壁纸上清晰）
     *   - 颜色：文件管理=绿 / 编辑器=橙 / 终端=蓝 / 关于=紫
     *   - 命中范围与 kernel.c 保持一致：x 24..88，y 24+i*90 .. +84 */
    uint32_t cols[]   = { rgb(34,197,94), rgb(249,115,22), rgb(59,130,246), rgb(168,85,247), rgb(236,72,153), rgb(100,116,139), rgb(14,165,233) };
    const char* syms[]  = { "DIR", "TXT", ">_", "i", "IMG", "RB", "WWW" };
    /* 下方中文标签：直接写 UTF-8 中文，由 fb_cn_string 解码码点 → TTF/点阵渲染 */
    const char* cn_labels[] = {
        "文件管理",   /* 文件管理 */
        "编辑器",     /* 编辑器   */
        "终端",       /* 终端     */
        "关于",       /* 关于     */
        "画图",       /* 画图     */
        "回收站",     /* 回收站   */
        "浏览器",     /* 浏览器   */
    };
    int dn = (int)(sizeof(g_desktop_order) / sizeof(g_desktop_order[0]));
    for (int pos = 0; pos < dn; pos++) {
        int ai = g_desktop_order[pos];     /* app_names 索引 */
        int ax = 24;
        int ay = 24 + pos * 90;
        fill_round_rect(ax, ay, 64, 64, 12, cols[ai]);
        int scale = 2;
        int sw = (int)strlen(syms[ai]) * 8 * scale;
        fb_string_scale(ax + (64 - sw) / 2, ay + (64 - 8 * scale) / 2,
                        syms[ai], rgb(255, 255, 255), (uint32_t)-1, scale);
        /* 下方白色中文标签，居中（每字 16px），先画黑色阴影再画白色 */
        const char* cn = cn_labels[ai];
        int cnw = utf8_count(cn) * 16;
        int lx = ax + (64 - cnw) / 2;
        int ly = ay + 70;
        fb_cn_string(lx + 1, ly + 1, cn, rgb(0, 0, 0), (uint32_t)-1);
        fb_cn_string(lx, ly, cn, rgb(255, 255, 255), (uint32_t)-1);
    }

    /* 桌面右上角：启动方式徽章。
     * 暗底药丸 + 状态点(绿=UEFI / 琥珀=BIOS) + ASCII 标签 + 中文"启动"。
     * g_boot_mode 由 kernel.c 从 multiboot cmdline 解析得到。 */
    {
        int bw = 108, bh = 24;
        int bx = (int)fb.width - bw - 12;
        int by = 8;
        fill_round_rect(bx, by, bw, bh, bh / 2, rgb(15, 23, 42));
        outline(bx, by, bw, bh, rgb(71, 85, 105));
        /* 状态点：UEFI=绿, BIOS=琥珀 */
        uint32_t dot = g_boot_mode ? rgb(34, 197, 94) : rgb(251, 191, 36);
        fb_circle(bx + 14, by + bh / 2, 4, dot);
        /* ASCII 标签 */
        const char* mode = g_boot_mode ? "UEFI" : "BIOS";
        fb_string(bx + 26, by + (bh - 8) / 2, mode, rgb(226, 232, 240), (uint32_t)-1);
        /* 中文"启动" */
        fb_cn_string(bx + 26 + 4 * 8 + 6, by + (bh - 16) / 2, "启动",
                     rgb(148, 163, 184), (uint32_t)-1);
    }
}

/* 命中测试：返回 (mx,my) 命中的桌面图标对应的 app_names 索引；未命中返回 -1。
 * 几何必须与 gui_draw_desktop 的 g_desktop_order 完全一致。 */
int gui_desktop_app_at(int mx, int my) {
    int dn = (int)(sizeof(g_desktop_order) / sizeof(g_desktop_order[0]));
    for (int pos = 0; pos < dn; pos++) {
        int ay = 24 + pos * 90;
        if (mx >= 24 && mx <= 24 + 64 && my >= ay && my <= ay + 84)
            return g_desktop_order[pos];
    }
    return -1;
}

/* 画一个 1px 边框矩形（用于标题栏按钮图标，无填充） */
static void outline(int x, int y, int w, int h, uint32_t c) {
    fb_rect(x, y, w, 1, c);
    fb_rect(x, y + h - 1, w, 1, c);
    fb_rect(x, y, 1, h, c);
    fb_rect(x + w - 1, y, 1, h, c);
}

/* 填充圆角矩形（用于桌面图标方块）。r 为圆角半径。 */
static void fill_round_rect(int x, int y, int w, int h, int r, uint32_t c) {
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            int in = 1;
            if (xx < r && yy < r) {
                int dx = xx - r, dy = yy - r;
                if (dx * dx + dy * dy > r * r) in = 0;
            } else if (xx >= w - r && yy < r) {
                int dx = xx - (w - r), dy = yy - r;
                if (dx * dx + dy * dy > r * r) in = 0;
            } else if (xx < r && yy >= h - r) {
                int dx = xx - r, dy = yy - (h - r);
                if (dx * dx + dy * dy > r * r) in = 0;
            } else if (xx >= w - r && yy >= h - r) {
                int dx = xx - (w - r), dy = yy - (h - r);
                if (dx * dx + dy * dy > r * r) in = 0;
            }
            if (in) fb_pixel(x + xx, y + yy, c);
        }
    }
}

void gui_draw_window(window_t* wn) {
    uint32_t bg = (wn == &wins[focus] && focus >= 0) ? rgb(248, 250, 252) : rgb(226, 232, 240);
    fb_rect(wn->x, wn->y, wn->w, wn->h, bg);
    fb_rect(wn->x, wn->y, wn->w, 22, wn->title_color);
    /* 标题栏：ASCII 名查表得到中文（UTF-8），用 fb_cn_string 渲染。
     * 若查不到（自定义/未列名），回退 ASCII 渲染。 */
    const char* nm = wn->title;
    const char* cn_title = 0;
    if      (!strcmp(nm, "Files"))  cn_title = "文件管理";
    else if (!strcmp(nm, "Editor")) cn_title = "编辑器";
    else if (!strcmp(nm, "Term"))   cn_title = "终端";
    else if (!strcmp(nm, "About"))  cn_title = "关于";
    else if (!strcmp(nm, "Paint"))  cn_title = "画图";
    if (cn_title) fb_cn_string(wn->x + 8, wn->y + 3, cn_title, rgb(255, 255, 255), (uint32_t)-1);
    else          fb_string(wn->x + 8, wn->y + 7, wn->title, rgb(255, 255, 255), (uint32_t)-1);

    /* 标题栏右侧三个按钮：最小化(_) / 最大化(▢) / 关闭(X)，各 16x16，间距 2px */
    int my0 = wn->y + 3;
    int rx = wn->x + wn->w;
    /* 最小化 _ */
    int bx_min = rx - 56;
    fb_rect(bx_min, my0, 16, 16, rgb(226, 232, 240));
    fb_rect(bx_min + 3, my0 + 12, 10, 2, rgb(15, 23, 42));
    /* 最大化 / 还原 ▢ */
    int bx_max = rx - 38;
    fb_rect(bx_max, my0, 16, 16, rgb(226, 232, 240));
    if (wn->maximized) {
        /* 还原图标：两个叠加的方框 */
        outline(bx_max + 4, my0 + 5, 8, 8, rgb(15, 23, 42));
        outline(bx_max + 2, my0 + 3, 8, 8, rgb(15, 23, 42));
    } else {
        fb_rect(bx_max + 3, my0 + 3, 10, 10, rgb(241, 245, 249));
        outline(bx_max + 3, my0 + 3, 10, 10, rgb(15, 23, 42));
    }
    /* 关闭 X */
    int bx_cls = rx - 20;
    fb_rect(bx_cls, my0, 16, 16, rgb(239, 68, 68));
    fb_char(bx_cls + 4, my0 + 2, 'X', rgb(255, 255, 255), (uint32_t)-1);

    fb_rect(wn->x, wn->y, wn->w, 1, rgb(15, 23, 42));
    fb_rect(wn->x, wn->y + wn->h - 1, wn->w, 1, rgb(100, 116, 139));
    fb_rect(wn->x, wn->y, 1, wn->h, rgb(100, 116, 139));
    fb_rect(wn->x + wn->w - 1, wn->y, 1, wn->h, rgb(100, 116, 139));
}

/* ===== 开始菜单（点"开始"按钮弹出） =====
 * 菜单 5 项：文件管理/编辑器/终端/关于/Power（关机）。每个 24px 高。
 * 布局常量见文件顶部的 MENU_ 与 START_BTN_ 系列宏定义。 */

/* 把 ASCII 窗口名翻译为中文菜单标签（UTF-8 中文）。
 * 注意：必须写入结尾的 0，否则 fb_cn_string 会越界读到栈上脏数据，
 * 在中文标签后面多画出一串乱码（表现为“位置往后串了一个”）。 */
static int menu_label_for(const char* title, char* out) {
    const char* cn = 0;
    if      (!strcmp(title, "Files"))  cn = "文件管理";
    else if (!strcmp(title, "Editor")) cn = "编辑器";
    else if (!strcmp(title, "Term"))   cn = "终端";
    else if (!strcmp(title, "About"))  cn = "关于";
    else if (!strcmp(title, "Paint"))  cn = "画图";
    else if (!strcmp(title, "Browser"))cn = "浏览器";
    else return 0;
    int n = 0;
    while (cn[n]) { out[n] = cn[n]; n++; }   /* 拷贝 UTF-8 字节（含多字节中文） */
    out[n] = 0;   /* 关键：字符串结尾，避免越界 */
    return n;
}

void gui_draw_taskbar(void) {
    int ty = fb.height - 28;
    fb_rect(0, ty, fb.width, 28, rgb(30, 41, 59));
    fb_rect(0, ty, fb.width, 1, rgb(71, 85, 105));

    /* "开始" 按钮：宽 60、高 20。字模索引 27=开, 30=始。 */
    uint32_t btn_col = g_start_menu ? rgb(34, 197, 94) : rgb(51, 65, 85);
    fb_rect(START_BTN_X, START_BTN_Y, START_BTN_W, START_BTN_H, btn_col);
    outline(START_BTN_X, START_BTN_Y, START_BTN_W, START_BTN_H, rgb(15, 23, 42));
    const char* start_label = "开始";  /* 直接 UTF-8 中文 */
    fb_cn_string(START_BTN_X + 12, START_BTN_Y + 2, start_label, rgb(255, 255, 255), (uint32_t)-1);

    /* 任务栏右侧放时间 */
    rtc_t t;
    rtc_read(&t);
    char clk[16];
    clk[0] = '0' + t.hour / 10; clk[1] = '0' + t.hour % 10;
    clk[2] = ':';
    clk[3] = '0' + t.minute / 10; clk[4] = '0' + t.minute % 10;
    clk[5] = ':';
    clk[6] = '0' + t.second / 10; clk[7] = '0' + t.second % 10;
    clk[8] = 0;
    fb_string(fb.width - 70, ty + 9, clk, rgb(226, 232, 240), (uint32_t)-1);

    /* 窗口按钮：每个 74 宽，从 x=80 开始；中文标题 */
    for (int i = 0; i < win_count; i++) {
        int bx = 80 + i * 80;
        fb_rect(bx, ty + 4, 74, 20, (i == focus) ? rgb(34, 197, 94) : rgb(51, 65, 85));
        char cn_t[32]; int cn_n = menu_label_for(wins[i].title, cn_t);
        if (cn_n > 0) fb_cn_string(bx + 8, ty + 7, cn_t, rgb(255, 255, 255), (uint32_t)-1);
        else          fb_string(bx + 8, ty + 9, wins[i].title, rgb(255, 255, 255), (uint32_t)-1);
    }

    /* 开始菜单（最上层覆盖） */
    if (g_start_menu) {
        int mx = START_BTN_X;
        int my = START_BTN_Y - MENU_ITEMS * MENU_ITEM_H;
        /* 阴影 */
        fb_rect(mx + 3, my + 3, MENU_W, MENU_ITEMS * MENU_ITEM_H, rgb(15, 23, 42));
        /* 面板 */
        fb_rect(mx, my, MENU_W, MENU_ITEMS * MENU_ITEM_H, rgb(248, 250, 252));
        outline(mx, my, MENU_W, MENU_ITEMS * MENU_ITEM_H, rgb(15, 23, 42));
        /* 7 项：文件管理 / 编辑器 / 终端 / 关于 / 画图 / 浏览器 / Power */
        uint32_t cols[MENU_ITEMS] = { rgb(34,197,94), rgb(249,115,22), rgb(59,130,246), rgb(168,85,247), rgb(236,72,153), rgb(14,165,233), rgb(239,68,68) };
        const char* labels_asc[MENU_ITEMS] = { "Files", "Editor", "Term", "About", "Paint", "Browser", "Power" };
        for (int i = 0; i < MENU_ITEMS; i++) {
            int ry = my + i * MENU_ITEM_H;
            /* 悬停高亮（鼠标位置在项内时） */
            if (mouse.x >= mx && mouse.x < mx + MENU_W &&
                mouse.y >= ry && mouse.y < ry + MENU_ITEM_H) {
                fb_rect(mx + 1, ry + 1, MENU_W - 2, MENU_ITEM_H - 2, rgb(226, 232, 240));
            }
            /* 小色块当图标 */
            fb_rect(mx + 8, ry + 4, 16, 16, cols[i]);
            /* 文字（前四项中文，第五项英文 "Power"） */
            char cn_lab[32]; int cn_n = menu_label_for(labels_asc[i], cn_lab);
            if (cn_n > 0) {
                fb_cn_string(mx + 32, ry + 4, cn_lab, rgb(15, 23, 42), (uint32_t)-1);
            } else {
                fb_string(mx + 32, ry + 7, "Power", rgb(15, 23, 42), (uint32_t)-1);
            }
        }
    }

    /* 文件管理器右键上下文菜单（最上层绘制，动态项数；删除项为红色） */
    if (g_ctx_active) {
        int mw = 150, mh = g_ctx_n * 24;
        fb_rect(g_ctx_x + 2, g_ctx_y + 2, mw, mh, rgb(15, 23, 42));
        fb_rect(g_ctx_x, g_ctx_y, mw, mh, rgb(248, 250, 252));
        outline(g_ctx_x, g_ctx_y, mw, mh, rgb(15, 23, 42));
        for (int k = 0; k < g_ctx_n; k++) {
            int iy = g_ctx_y + k * 24;
            int hover = (mouse.x >= g_ctx_x && mouse.x < g_ctx_x + mw &&
                         mouse.y >= iy && mouse.y < iy + 24);
            if (hover) fb_rect(g_ctx_x + 1, iy + 1, mw - 2, 22, rgb(226, 232, 240));
            uint32_t col = g_ctx_red[k] ? rgb(220, 38, 38) : rgb(15, 23, 42);
            fb_cn_string(g_ctx_x + 12, iy + 4, g_ctx_lbl[k], col, (uint32_t)-1);
        }
    }
}

/* ===================== 终端（M3） =====================
 * 基于键盘驱动 + VFS 的真 shell：输入行 + 滚动回显 + 命令解析。
 * 滚动缓冲用行环形缓冲（TERM_ROWS 行，每行 TERM_COLS 列）。 */
#define TERM_ROWS 256
#define TERM_COLS 120
static char term_lines[TERM_ROWS][TERM_COLS];
static int  term_count = 0;
static int  term_head = 0;     /* 下一个写入位置（环形） */
static char term_input[128];
static int  term_input_len = 0;

/* 命令前缀判断：p 以 kw 开头且后接空格/制表/结尾。避免依赖 strncmp。 */
static int cmd_is(const char* p, const char* kw) {
    while (*kw) { if (*p != *kw) return 0; p++; kw++; }
    return (*p == ' ' || *p == '\t' || *p == 0);
}

void term_puts(const char* s) {
    char buf[TERM_COLS];
    int j = 0;  /* 字节位置 */
    int w = 0;  /* 显示宽度（ASCII=1，中文=2） */

    for (int i = 0; s[i]; ) {
        /* 解析 UTF-8 字符，计算字节数和显示宽度 */
        int char_len = 1;
        int char_width = 1;
        unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            char_len = 1; char_width = 1;
        } else if ((c & 0xE0) == 0xC0) {
            char_len = 2; char_width = 2;
        } else if ((c & 0xF0) == 0xE0) {
            char_len = 3; char_width = 2;
        } else if ((c & 0xF8) == 0xF0) {
            char_len = 4; char_width = 2;
        } else {
            char_len = 1; char_width = 1;
        }

        if (s[i] == '\n') {
            buf[j] = 0;
            memcpy(term_lines[term_head], buf, j + 1);
            term_head = (term_head + 1) % TERM_ROWS;
            if (term_count < TERM_ROWS) term_count++;
            j = 0; w = 0; i++;
        } else if (w + char_width <= TERM_COLS - 1 && j + char_len < TERM_COLS - 1) {
            /* 字符能放进当前行 */
            for (int k = 0; k < char_len; k++) buf[j++] = s[i + k];
            w += char_width;
            i += char_len;
        } else {
            /* 换行，不递增 i，下一轮处理这个字符 */
            buf[j] = 0;
            memcpy(term_lines[term_head], buf, j + 1);
            term_head = (term_head + 1) % TERM_ROWS;
            if (term_count < TERM_ROWS) term_count++;
            j = 0; w = 0;
        }
    }
    if (j > 0) {
        buf[j] = 0;
        memcpy(term_lines[term_head], buf, j + 1);
        term_head = (term_head + 1) % TERM_ROWS;
        if (term_count < TERM_ROWS) term_count++;
    }
}

static void term_reset(void) {
    term_count = 0;
    term_head = 0;
    term_input_len = 0;
    term_puts("SproutOS shell. Type 'help' for commands.\n");
}

/* ===== Planter 包管理器辅助函数 ===== */
static void term_to_root(void) {
    while (vfs_chdir_up() == 0) {}
}
/* 进入 root 下的子目录 sub（NULL/空串表示仅回 root）。返回是否成功进入子目录。 */
static int term_chdir_abs(const char* sub) {
    term_to_root();
    if (sub && sub[0]) return vfs_chdir(sub) == 0;
    return 1;
}
/* 取 root 下某目录的首簇（用于跨目录复制/删除）；不存在返回 0；不改变 cwd。 */
static uint32_t planter_dir_cluster(const char* name) {
    term_to_root();
    if (vfs_chdir(name) != 0) { term_to_root(); return 0; }
    uint32_t c = vfs_cwd_cluster();
    term_to_root();
    return c;
}
/* 包名 -> 8.3 的 .SEED 文件名（温室种子，无扩展名补 .SEED）。返回 0 成功，-1 名字过长。 */
static int planter_norm_name(const char* fn, char* out, int outsz) {
    int has_dot = 0, fl = 0;
    for (; fn[fl] && fl < outsz - 6; fl++) {
        if (fn[fl] == '.') has_dot = 1;
        out[fl] = fn[fl];
    }
    out[fl] = 0;
    if (!has_dot) {
        if (fl + 5 < outsz) { strcat(out, ".SEED"); return 0; }
        return -1;
    }
    return 0;
}
/* 包名 -> 8.3 的 .SPT 文件名（APPS 可执行文件，3字符扩展名避免长文件名问题）。 */
static int planter_norm_spt(const char* fn, char* out, int outsz) {
    int has_dot = 0, fl = 0;
    for (; fn[fl] && fl < outsz - 5; fl++) {
        if (fn[fl] == '.') has_dot = 1;
        out[fl] = fn[fl];
    }
    out[fl] = 0;
    if (!has_dot) {
        if (fl + 4 < outsz) { strcat(out, ".SPT"); return 0; }
        return -1;
    }
    return 0;
}
/* 是否 .SEED 文件（温室种子） */
static int is_seedfile(const char* nm) {
    int l = strlen(nm);
    return l >= 5 && nm[l-5]=='.' && nm[l-4]=='S' && nm[l-3]=='E' && nm[l-2]=='E' && nm[l-1]=='D';
}
/* 是否 .SPT 文件（APPS 可执行文件） */
static int is_sptfile(const char* nm) {
    int l = strlen(nm);
    return l >= 4 && nm[l-4]=='.' && nm[l-3]=='S' && nm[l-2]=='P' && nm[l-1]=='T';
}
/* 大小写不敏感子串匹配 */
static int contains_ci(const char* hay, const char* nd) {
    int hlen = strlen(hay), nlen = strlen(nd);
    if (nlen == 0) return 1;
    for (int i = 0; i + nlen <= hlen; i++) {
        int ok = 1;
        for (int j = 0; j < nlen; j++) {
            char a = hay[i+j], b = nd[j];
            if (a >= 'a' && a <= 'z') a -= 32;
            if (b >= 'a' && b <= 'z') b -= 32;
            if (a != b) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}
static void term_exec(const char* cmd) {
    int i = 0;
    while (cmd[i] == ' ' || cmd[i] == '\t') i++;
    const char* p = cmd + i;
    if (p[0] == 0) { term_puts("\n"); return; }

    if (cmd_is(p, "help")) {
        term_puts("Commands:\n");
        term_puts("  ls            list current directory\n");
        term_puts("  cat <file>    print file contents\n");
        term_puts("  cd <dir>      change directory\n");
        term_puts("  pwd           print working directory\n");
        term_puts("  echo <text>   print text\n");
        term_puts("  date          show RTC date/time\n");
        term_puts("  clear         clear screen\n");
        term_puts("  ver           show version\n");
        term_puts("  shutdown      power off the system\n");
        term_puts("  poweroff      same as shutdown\n");
        term_puts("  net           network status / arp resolve\n");
        term_puts("  ping <ip>     send ICMP echo request\n");
        term_puts("  terra run <pkg>  run a .seed package (ESC exit)\n");
        term_puts("  planter          package manager help\n");
        term_puts("  plant/weed/water/rain/garden/seek/sniff/prune\n");
        term_puts("\n");
    } else if (cmd_is(p, "ls")) {
        if (!vfs_is_mounted()) { term_puts("no disk mounted\n"); return; }
        vfs_dir_t d;
        if (vfs_list(&d) != 0) { term_puts("ls failed\n"); return; }
        for (int k = 0; k < d.count; k++) {
            term_puts(d.entries[k].name);
            term_puts(d.entries[k].is_dir ? "/\n" : "\n");
        }
        term_puts("\n");
    } else if (cmd_is(p, "pwd")) {
        term_puts(vfs_cwd_path());
        term_puts("\n");
    } else if (cmd_is(p, "ver")) {
        term_puts(OS_FULLNAME "\n");
    } else if (cmd_is(p, "clear")) {
        term_count = 0; term_head = 0; term_input_len = 0; term_input[0] = 0;
    } else if (cmd_is(p, "shutdown") || cmd_is(p, "poweroff")) {
        term_puts("正在关机...\n");
        /* QEMU ACPI 关机端口（i440fx） */
        uint16_t pwroff = 0x2000;
        __asm__ volatile("outw %0, %1" : : "a"(pwroff), "Nd"(0x604));
        /* 备用：旧版 QEMU/Bochs 关机端口 */
        __asm__ volatile("outw %0, %1" : : "a"(pwroff), "Nd"(0xB004));
        /* 如果上面都没生效，halt CPU */
        term_puts("可以安全关闭电源了。\n");
        while (1) { __asm__ volatile("hlt"); }
    } else if (cmd_is(p, "date")) {
        rtc_t t; rtc_read(&t);
        char b[40];
        int n = 0;
        b[n++] = '0' + t.year/1000; b[n++] = '0' + (t.year/100)%10;
        b[n++] = '0' + (t.year/10)%10; b[n++] = '0' + t.year%10;
        b[n++] = '-'; b[n++] = '0' + t.month/10; b[n++] = '0' + t.month%10;
        b[n++] = '-'; b[n++] = '0' + t.day/10; b[n++] = '0' + t.day%10;
        b[n++] = ' ';
        b[n++] = '0' + t.hour/10; b[n++] = '0' + t.hour%10;
        b[n++] = ':';
        b[n++] = '0' + t.minute/10; b[n++] = '0' + t.minute%10;
        b[n++] = '\n'; b[n] = 0;
        term_puts(b);
    } else if (cmd_is(p, "echo")) {
        const char* a = p + 4;
        while (*a == ' ') a++;
        term_puts(a);
        term_puts("\n");
    } else if (cmd_is(p, "net")) {
        net_cmd(p + 3);
    } else if (cmd_is(p, "ping")) {
        net_ping(p + 4);
    } else if (cmd_is(p, "cd")) {
        const char* a = p + 2;
        while (*a == ' ') a++;
        if (a[0] == 0 || !strcmp(a, "/") || !strcmp(a, ".")) {
            while (vfs_chdir_up() == 0) {}
        } else if (!strcmp(a, "..")) {
            vfs_chdir_up();
        } else if (vfs_chdir(a) != 0) {
            term_puts("cd: no such directory\n");
        }
        term_puts("\n");
    } else if (cmd_is(p, "cat")) {
        const char* a = p + 3;
        while (*a == ' ') a++;
        if (a[0] == 0) { term_puts("cat: missing file\n"); return; }
        vfs_dirent_t e;
        if (vfs_open(a, &e) != 0) { term_puts("cat: no such file\n"); return; }
        if (e.is_dir) { term_puts("cat: is a directory\n"); return; }
        static uint8_t cbuf[4096];
        uint32_t rl = 0;
        vfs_read(&e, cbuf, sizeof(cbuf) - 1, &rl);
        cbuf[rl] = 0;
        term_puts((char*)cbuf);
        if (rl > 0 && cbuf[rl - 1] != '\n') term_puts("\n");
        } else if (cmd_is(p, "terra")) {
        const char* a = p + 5;
        while (*a == ' ' || *a == '\t') a++;
        if (a[0] == 0 || !strcmp(a, "help")) {
            term_puts("Terra runtime (.seed packages):\n");
            term_puts("  terra run <pkg>   run a package (ESC to exit)\n");
            term_puts("  terra help        this help\n\n");
        } else if (cmd_is(a, "run")) {
            const char* fn = a + 3;
            while (*fn == ' ' || *fn == '\t') fn++;
            if (*fn == 0) { term_puts("terra run: missing package name\n"); return; }
            char seed_name[16], spt_name[16];
            if (planter_norm_name(fn, seed_name, sizeof(seed_name)) != 0) {
                term_puts("terra: name too long\n"); return;
            }
            if (planter_norm_spt(fn, spt_name, sizeof(spt_name)) != 0) {
                term_puts("terra: name too long\n"); return;
            }
            g_seedvm = NULL;
            /* 优先从 APPS 加载 .SPT（已安装的可执行文件） */
            if (term_chdir_abs("APPS")) { g_seedvm = seedvm_load(spt_name); term_to_root(); }
            /* 找不到则从 GREENHOU 加载 .SEED（温室种子，直接运行） */
            if (!g_seedvm && term_chdir_abs("GREENHOU")) { g_seedvm = seedvm_load(seed_name); term_to_root(); }
            if (!g_seedvm) {
                term_puts("terra: package not found: ");
                term_puts(fn); term_puts("\n");
                return;
            }
            g_seedvm_active = 1;
        } else {
            term_puts("terra: unknown subcommand. Type 'terra help'\n");
        }
    } else if (cmd_is(p, "planter")) {
        term_puts("Planter 包管理器 (The Greenhouse / 温室):\n");
        term_puts("  terra run <pkg>  运行包 (ESC 退出)\n");
        term_puts("  plant <pkg>      从温室播种(安装)到花园\n");
        term_puts("  weed <pkg>       除草(卸载)\n");
        term_puts("  water <pkg>      浇水(更新单个)\n");
        term_puts("  rain             下雨(全量更新)\n");
        term_puts("  garden           查看花园(已安装)\n");
        term_puts("  seek <kw>        在温室找种子\n");
        term_puts("  sniff <pkg>      查看种子信息\n");
        term_puts("  prune            修剪缓存/依赖\n");
        term_puts("  grind <src>      研磨(宿主机编译, 见提示)\n\n");
    } else if (cmd_is(p, "plant")) {
        const char* fn = p + 5;
        while (*fn == ' ' || *fn == '\t') fn++;
        if (*fn == 0) { term_puts("plant: missing package name\n"); return; }
        char seed_name[16], spt_name[16];
        if (planter_norm_name(fn, seed_name, sizeof(seed_name)) != 0) { term_puts("plant: name too long\n"); return; }
        if (planter_norm_spt(fn, spt_name, sizeof(spt_name)) != 0) { term_puts("plant: name too long\n"); return; }
        uint32_t gh = planter_dir_cluster("GREENHOU");
        uint32_t ap = planter_dir_cluster("APPS");
        if (!gh || !ap) { term_puts("plant: 温室或花园目录缺失\n"); return; }
        vfs_unlink_in_dir(ap, spt_name);
        if (vfs_copy_file(seed_name, gh, spt_name, ap) != 0) {
            term_puts("plant: 温室里没有这个种子: ");
            term_puts(fn); term_puts("\n");
            return;
        }
        term_puts("已播种(安装): "); term_puts(fn); term_puts("  (.SEED -> .SPT)\n");
    } else if (cmd_is(p, "weed")) {
        const char* fn = p + 4;
        while (*fn == ' ' || *fn == '\t') fn++;
        if (*fn == 0) { term_puts("weed: missing package name\n"); return; }
        char spt_name[16];
        if (planter_norm_spt(fn, spt_name, sizeof(spt_name)) != 0) { term_puts("weed: name too long\n"); return; }
        uint32_t ap = planter_dir_cluster("APPS");
        if (!ap) { term_puts("weed: 花园目录缺失\n"); return; }
        if (vfs_unlink_in_dir(ap, spt_name) != 0) {
            term_puts("weed: 花园里没有这个: ");
            term_puts(fn); term_puts("\n");
            return;
        }
        term_puts("已除草(卸载): "); term_puts(fn); term_puts("\n");
    } else if (cmd_is(p, "water")) {
        const char* fn = p + 5;
        while (*fn == ' ' || *fn == '\t') fn++;
        if (*fn == 0) { term_puts("water: missing package name\n"); return; }
        char seed_name[16], spt_name[16];
        if (planter_norm_name(fn, seed_name, sizeof(seed_name)) != 0) { term_puts("water: name too long\n"); return; }
        if (planter_norm_spt(fn, spt_name, sizeof(spt_name)) != 0) { term_puts("water: name too long\n"); return; }
        uint32_t gh = planter_dir_cluster("GREENHOU");
        uint32_t ap = planter_dir_cluster("APPS");
        if (!gh || !ap) { term_puts("water: 温室或花园目录缺失\n"); return; }
        vfs_unlink_in_dir(ap, spt_name);
        if (vfs_copy_file(seed_name, gh, spt_name, ap) != 0) {
            term_puts("water: 温室里没有这个种子: ");
            term_puts(fn); term_puts("\n");
            return;
        }
        term_puts("已浇水(更新): "); term_puts(fn); term_puts("  (.SEED -> .SPT)\n");
    } else if (cmd_is(p, "rain")) {
        uint32_t gh = planter_dir_cluster("GREENHOU");
        uint32_t ap = planter_dir_cluster("APPS");
        if (!gh || !ap) { term_puts("rain: 温室或花园目录缺失\n"); return; }
        term_chdir_abs("APPS");
        vfs_dir_t d; int n = 0;
        if (vfs_list(&d) == 0) {
            for (int k = 0; k < d.count; k++) {
                const char* nm = d.entries[k].name;
                if (!is_sptfile(nm)) continue;
                /* 把 .SPT 文件名转成 .SEED（替换扩展名） */
                char seed_name[16];
                int nl = strlen(nm);
                if (nl > 4) {
                    memcpy(seed_name, nm, nl - 4);
                    seed_name[nl - 4] = 0;
                    strcat(seed_name, ".SEED");
                } else {
                    strcpy(seed_name, nm);
                }
                vfs_unlink_in_dir(ap, nm);
                if (vfs_copy_file(seed_name, gh, nm, ap) == 0) n++;
            }
        }
        term_to_root();
        if (n == 0) term_puts("rain: 花园是空的，先 plant 一些种子\n");
        else { term_puts("rain: 已给 "); char b[8]; itoa(n, b); term_puts(b); term_puts(" 个包浇水(全量更新)\n"); }
    } else if (cmd_is(p, "garden")) {
        uint32_t ap = planter_dir_cluster("APPS");
        if (!ap) { term_puts("garden: 花园目录缺失\n"); return; }
        term_chdir_abs("APPS");
        vfs_dir_t d; int found = 0;
        if (vfs_list(&d) == 0) {
            for (int k = 0; k < d.count; k++) {
                const char* nm = d.entries[k].name;
                if (!is_sptfile(nm)) continue;
                term_puts("  "); term_puts(nm);
                term_puts("  ("); char b[16]; itoa(d.entries[k].size, b); term_puts(b); term_puts(" bytes)\n");
                found++;
            }
        }
        term_to_root();
        if (!found) term_puts("花园是空的。从温室播种: plant <名>\n");
        term_puts("\n");
    } else if (cmd_is(p, "seek")) {
        const char* fn = p + 4;
        while (*fn == ' ' || *fn == '\t') fn++;
        if (*fn == 0) { term_puts("seek: missing keyword\n"); return; }
        uint32_t gh = planter_dir_cluster("GREENHOU");
        if (!gh) { term_puts("seek: 温室目录缺失\n"); return; }
        term_chdir_abs("GREENHOU");
        vfs_dir_t d; int found = 0;
        if (vfs_list(&d) == 0) {
            for (int k = 0; k < d.count; k++) {
                const char* nm = d.entries[k].name;
                if (!is_seedfile(nm)) continue;
                if (contains_ci(nm, fn)) { term_puts("  "); term_puts(nm); term_puts("\n"); found++; }
            }
        }
        term_to_root();
        if (!found) { term_puts("seek: 温室里没有匹配 '"); term_puts(fn); term_puts("' 的种子\n"); }
        term_puts("\n");
    } else if (cmd_is(p, "prune")) {
        uint32_t cc = planter_dir_cluster("CACHE");
        if (!cc) { term_puts("prune: 缓存目录缺失\n"); return; }
        term_chdir_abs("CACHE");
        vfs_dir_t d; int n = 0;
        if (vfs_list(&d) == 0) {
            for (int k = 0; k < d.count; k++) {
                if (d.entries[k].is_dir) continue;
                if (vfs_unlink_in_dir(cc, d.entries[k].name) == 0) n++;
            }
        }
        term_to_root();
        term_puts("prune: 修剪了 "); char b[8]; itoa(n, b); term_puts(b); term_puts(" 项缓存/依赖\n");
    } else if (cmd_is(p, "sniff")) {
        const char* fn = p + 5;
        while (*fn == ' ' || *fn == '\t') fn++;
        if (*fn == 0) { term_puts("sniff: missing package name\n"); return; }
        char seed_name[16], spt_name[16];
        if (planter_norm_name(fn, seed_name, sizeof(seed_name)) != 0) { term_puts("sniff: name too long\n"); return; }
        if (planter_norm_spt(fn, spt_name, sizeof(spt_name)) != 0) { term_puts("sniff: name too long\n"); return; }
        char pkg_name[32], pkg_author[32]; uint32_t code_size, flags; int ok = 0;
        if (term_chdir_abs("GREENHOU")) { ok = seedvm_get_info(seed_name, pkg_name, pkg_author, &code_size, &flags) == 0; term_to_root(); }
        if (!ok && term_chdir_abs("APPS")) { ok = seedvm_get_info(spt_name, pkg_name, pkg_author, &code_size, &flags) == 0; term_to_root(); }
        if (!ok) { term_puts("sniff: 找不到种子: "); term_puts(fn); term_puts("\n"); return; }
        term_puts("  品种(Name):   "); term_puts(pkg_name); term_puts("\n");
        term_puts("  作者(Author): "); term_puts(pkg_author); term_puts("\n");
        term_puts("  代码(Code):   "); char b[16]; itoa(code_size, b); term_puts(b); term_puts(" bytes\n");
        term_puts("  图形(GUI):    "); term_puts(flags & 1 ? "yes" : "no"); term_puts("\n\n");
    } else if (cmd_is(p, "grind")) {
        const char* fn = p + 5;
        while (*fn == ' ' || *fn == '\t') fn++;
        term_puts("grind(研磨) 在宿主机执行，SproutOS 内不编译。\n");
        term_puts("  用 Terra 源码 (.terra) 生成 .seed 包：\n");
        term_puts("    python3 scripts/terrac.py <源文件.terra> -o <包名>.seed -n <包名> -a <作者>\n");
        if (*fn != 0) {
            term_puts("  示例: python3 scripts/terrac.py "); term_puts(fn); term_puts(" -o ");
            term_puts(fn); term_puts(".seed\n");
        }
        term_puts("\n");
    }
else {
        term_puts("unknown command: ");
        term_puts(p);
        term_puts("\n");
    }
}

static void term_on_key(char c) {
    if (c == '\n' || c == '\r') {
        term_puts("SproutOS> ");
        term_puts(term_input);
        term_puts("\n");
        term_exec(term_input);
        term_input_len = 0;
        term_input[0] = 0;   /* 清空缓冲区，防止旧字符残留 */
        return;
    }
    if (c == 0x03) {  /* Ctrl+C：放弃当前输入行 */
        term_puts("^C\n");
        term_input_len = 0;
        term_input[0] = 0;
        return;
    }
    if (c == '\b') {
        if (term_input_len > 0) {
            term_input[--term_input_len] = 0;
        }
        return;
    }
    if (c >= 0x20 && c < 0x7F) {
        if (term_input_len < (int)sizeof(term_input) - 1) {
            term_input[term_input_len++] = c;
            term_input[term_input_len] = 0;  /* 补0，防止旧字符残留 */
        }
    }
}

static void draw_term(int idx) {
    window_t* w = &wins[idx];
    int x0 = w->x + 6, y0 = w->y + 26;
    int bw = w->w - 12, bh = w->h - 32;
    fb_rect(x0, y0, bw, bh, rgb(15, 23, 42));   /* 深色终端背景 */
    int rows = (bh - 8) / 14;
    if (rows < 1) rows = 1;
    int input_row = rows - 1;
    if (input_row < 0) input_row = 0;

    int oldest = (term_head - term_count + TERM_ROWS) % TERM_ROWS;
    int row = 0;
    for (int k = 0; k < term_count; k++) {
        int li = (oldest + k) % TERM_ROWS;
        const char* line = term_lines[li];
        if (row >= input_row) break;
        /* 用 fb_cn_string 绘制，支持中文（UTF-8） */
        fb_cn_string(x0 + 4, y0 + 4 + row * 14, line, rgb(34, 197, 94), (uint32_t)-1);
        row++;
    }

    char prompt[256];
    int pl = 0;
    const char* pr = "SproutOS> ";
    while (*pr && pl < 240) prompt[pl++] = *pr++;
    for (int i = 0; i < term_input_len && pl < 240; i++) prompt[pl++] = term_input[i];
    prompt[pl] = 0;
    fb_cn_string(x0 + 4, y0 + 4 + input_row * 14, prompt, rgb(34, 197, 94), (uint32_t)-1);

    /* 计算光标 x 位置：ASCII 8px，中文 16px */
    int cursor_px = 0;
    for (int i = 0; prompt[i]; ) {
        unsigned char c = (unsigned char)prompt[i];
        if (c < 0x80) { cursor_px += 8; i++; }
        else if ((c & 0xE0) == 0xC0) { cursor_px += 16; i += 2; }
        else if ((c & 0xF0) == 0xE0) { cursor_px += 16; i += 3; }
        else if ((c & 0xF8) == 0xF0) { cursor_px += 16; i += 4; }
        else { cursor_px += 8; i++; }
    }

    /* 闪烁光标：在输入行末尾绘制下划线，每约0.5秒切换一次 */
    static int cursor_tick = 0;
    cursor_tick++;
    if (cursor_tick >= 30) cursor_tick = 0;  /* 约30fps，30帧≈1秒，半亮半灭 */
    if (cursor_tick < 15) {
        int cursor_x = x0 + 4 + cursor_px;
        int cursor_y = y0 + 4 + input_row * 14 + 12;  /* 下划线在字符底部 */
        if (cursor_x + 8 <= x0 + bw - 4) {
            fb_rect(cursor_x, cursor_y, 8, 2, rgb(34, 197, 94));
        }
    }
}

/* 在当前目录创建一个新文件夹：自动命名 folder1 / folder2 …（避免重名），
 * 通过 vfs_mkdir 落到磁盘（8.3 名，ASCII，因为本 FS 不支持中文目录名）。 */
static void files_new_folder(void) {
    char name[13];
    for (int n = 1; n <= 999; n++) {
        int di = 0;
        const char* base = "folder";
        for (int i = 0; base[i]; i++) name[di++] = base[i];
        if (n >= 100) name[di++] = (char)('0' + n / 100);
        if (n >= 10)  name[di++] = (char)('0' + (n / 10) % 10);
        name[di++] = (char)('0' + n % 10);
        name[di] = 0;
        vfs_dirent_t e;
        if (vfs_open(name, &e) != 0) break;   /* 找到空闲名 */
    }
    if (vfs_mkdir(name) == 0) {
        files_need_refresh = 1;
        files_scroll = 0;
        set_status("已新建");   /* 已新建 */
    }
}

static void draw_files(int idx) {
    window_t* w = &wins[idx];
    int content_x = w->x + 12;

    if (!vfs_is_mounted()) {
        fb_string(content_x, w->y + 34, "No disk mounted (run: make disk)", rgb(220, 38, 38), (uint32_t)-1);
        return;
    }

    fb_string(content_x, w->y + 34, vfs_cwd_path(), rgb(15, 23, 42), (uint32_t)-1);

    /* 操作状态提示（复制/剪切/粘贴/删除后显示，中文点阵） */
    if (g_files_status[0] != 0)
        fb_cn_string(w->x + w->w - 110, w->y + 34, g_files_status, rgb(22, 163, 74), (uint32_t)-1);

    if (files_mode == 0) {
        int bx = w->x + 12, by = w->y + 50, bw = 34, bh = 18;
        fb_rect(bx, by, bw, bh, rgb(203, 213, 225));
        fb_string(bx + 8, by + 5, "..", rgb(15, 23, 42), (uint32_t)-1);

        /* 新建文件夹按钮（中文标签：新建文件夹 = 新46 建73 文2 件3 夹74） */
        int nbx = w->x + 52, nby = w->y + 50, nbw = 96, nbh = 18;
        fb_rect(nbx, nby, nbw, nbh, rgb(203, 213, 225));
        fb_cn_string(nbx + 8, nby + 1, "新建文件夹", rgb(15, 23, 42), (uint32_t)-1);

        /* 重命名输入框（右键文件→重命名 后显示；键盘输入，回车确认/Esc 取消） */
        if (files_rename_edit) {
            int ix = w->x + 12, iw = w->w - 24;
            int ry = w->y + 70;
            fb_rect(ix, ry, iw, 18, rgb(255, 255, 255));
            outline(ix, ry, iw, 18, rgb(37, 99, 235));
            fb_cn_string(ix + 4, ry + 2, "重命名:", rgb(37, 99, 235), (uint32_t)-1);  /* 重命名: */
            int tx = ix + 4 + 3 * 16;
            fb_string(tx, ry + 2, files_rename_new, rgb(15, 23, 42), (uint32_t)-1);
            fb_rect(tx + 8 * (int)strlen(files_rename_new), ry + 2, 1, 12, rgb(15, 23, 42)); /* 光标 */
        }

        if (files_need_refresh) {
            vfs_list(&files_dir);
            files_need_refresh = 0;
            if (files_scroll > files_dir.count) files_scroll = 0;
        }

        int list_top = w->y + 74;
        int row_h = 18;
        for (int i = 0; i < files_dir.count; i++) {
            int ry = list_top + (i - files_scroll) * row_h;
            if (ry < list_top) continue;
            if (ry + row_h > w->y + w->h - 4) break;
            uint32_t col = files_dir.entries[i].is_dir ? rgb(249, 115, 22) : rgb(59, 130, 246);
            fb_rect(w->x + 14, ry + 3, 12, 12, col);
            fb_string(w->x + 32, ry + 4, files_dir.entries[i].name, rgb(15, 23, 42), (uint32_t)-1);
            if (!files_dir.entries[i].is_dir) {
                char sz[16];
                itoa(files_dir.entries[i].size, sz);
                fb_string(w->x + w->w - 70, ry + 4, sz, rgb(100, 116, 139), (uint32_t)-1);
            }
        }

        if (g_clicked) {
            if (mouse.x >= bx && mouse.x <= bx + bw && mouse.y >= by && mouse.y <= by + bh) {
                vfs_chdir_up();
                files_need_refresh = 1;
                files_scroll = 0;
            } else if (mouse.x >= nbx && mouse.x <= nbx + nbw && mouse.y >= nby && mouse.y <= nby + nbh) {
                files_new_folder();
            } else if (mouse.x > w->x + 10 && mouse.x < w->x + w->w - 6 &&
                       mouse.y > list_top && mouse.y < w->y + w->h - 4) {
                int i = (mouse.y - list_top) / row_h + files_scroll;
                if (i >= 0 && i < files_dir.count) {
                    if (files_dir.entries[i].is_dir) {
                        vfs_chdir(files_dir.entries[i].name);
                        files_need_refresh = 1;
                        files_scroll = 0;
                    } else {
                        const char* nm = files_dir.entries[i].name;
                        int app = default_app_for(nm);
                        if (app == 1) {            /* .txt -> 文本编辑器 */
                            gui_open_file_in_app(1, nm);
                        } else if (app == 4) {     /* .bmp -> 画图（图片预览+绘制） */
                            gui_open_file_in_app(4, nm);
                        } else {                   /* 其它：原样文本预览 */
                            vfs_dirent_t e = files_dir.entries[i];
                            uint32_t rl = 0;
                            vfs_read(&e, files_buf, sizeof(files_buf) - 1, &rl);
                            files_len = rl;
                            if (files_len > sizeof(files_buf) - 1) files_len = sizeof(files_buf) - 1;
                            files_buf[files_len] = 0;
                            files_mode = 1;
                        }
                    }
                }
            }
        }
    } else {
        fb_rect(w->x + 12, w->y + 50, 60, 16, rgb(203, 213, 225));
        fb_string(w->x + 16, w->y + 52, "< back", rgb(15, 23, 42), (uint32_t)-1);

        int ty = w->y + 74;
        int cx = w->x + 12;
        for (uint32_t i = 0; i < files_len; i++) {
            char c = files_buf[i];
            if (c == '\r') continue;
            if (c == '\n' || cx >= w->x + w->w - 12) {
                ty += 14;
                cx = w->x + 12;
                if (ty > w->y + w->h - 16) break;
                if (c == '\n') continue;
            }
            fb_char(cx, ty, c, rgb(15, 23, 42), (uint32_t)-1);
            cx += 8;
        }

        if (g_clicked) {
            files_mode = 0;
            files_need_refresh = 1;
        }
    }
}

/* ===================== 文本编辑器（M2） =====================
 * 自包含：打开时尝试从 VFS 读 editor.txt；编辑后 Ctrl+S 写回 VFS。
 * 支持：可打印字符插入、Backspace、Enter（换行）。光标固定在末尾（追加式编辑）。 */
#define EDIT_BUF 16384
static char editor_buf[EDIT_BUF];
static uint32_t editor_len = 0;
static uint32_t editor_scroll = 0;
static int editor_status = 0;   /* 0 无 / 1 已保存 / 2 已修改 */

static void editor_reset(void) {
    editor_len = 0;
    editor_scroll = 0;
    editor_status = 0;
    editor_fname_edit = 0;
    fname_copy(editor_fname, "editor.txt", sizeof(editor_fname));
    vfs_dirent_t e;
    if (vfs_open(editor_fname, &e) == 0) {
        uint32_t rl = 0;
        vfs_read(&e, (uint8_t*)editor_buf, EDIT_BUF - 1, &rl);
        if (rl > EDIT_BUF - 1) rl = EDIT_BUF - 1;
        editor_len = rl;
        editor_buf[editor_len] = 0;
    }
}

/* 打开一个具名文件（从文件管理器双击 .txt 时调用）：先设自定义文件名，再载入内容。 */
static void editor_open_named(const char* name) {
    editor_len = 0;
    editor_scroll = 0;
    editor_status = 0;
    editor_fname_edit = 0;
    fname_copy(editor_fname, name, sizeof(editor_fname));
    vfs_dirent_t e;
    if (vfs_open(editor_fname, &e) == 0) {
        uint32_t rl = 0;
        vfs_read(&e, (uint8_t*)editor_buf, EDIT_BUF - 1, &rl);
        if (rl > EDIT_BUF - 1) rl = EDIT_BUF - 1;
        editor_len = rl;
        editor_buf[editor_len] = 0;
    }
}

/* 保存到 FAT32，被 Ctrl+S 和 Save 按钮共用；文件名用 editor_fname（可自定义） */
static void editor_save(void) {
    int r = vfs_write_file(editor_fname, (uint8_t*)editor_buf, editor_len);
    editor_status = (r == 0) ? 1 : 2;
}

/* 编辑文件名输入框：可打印字符、Backspace、Enter/Esc（确认并退出编辑） */
static void editor_fname_on_key(char c) {
    if (c == '\n' || c == '\r' || c == 0x1B) { editor_fname_edit = 0; return; }
    int n = (int)strlen(editor_fname);
    if (c == '\b') {
        if (n > 0) editor_fname[n - 1] = 0;
        return;
    }
    if (c >= 0x20 && c < 0x7F && n < (int)sizeof(editor_fname) - 1) {
        editor_fname[n] = c;
        editor_fname[n + 1] = 0;
    }
}

static void editor_on_key(char c) {
    if (c == 0x13) {   /* Ctrl+S 保存 */
        editor_save();
        return;
    }
    if (c == '\b') {
        if (editor_len > 0) { editor_len--; editor_buf[editor_len] = 0; editor_status = 2; }
        return;
    }
    if (c == '\n' || c == '\r') {
        if (editor_len < EDIT_BUF - 1) { editor_buf[editor_len++] = '\n'; editor_buf[editor_len] = 0; editor_status = 2; }
        return;
    }
    if (c >= 0x20 && c < 0x7F) {
        if (editor_len < EDIT_BUF - 1) { editor_buf[editor_len++] = c; editor_buf[editor_len] = 0; editor_status = 2; }
    }
}

/* 文件管理器重命名输入框：可打印字符、Backspace、Enter 确认（cwd 内改名）、Esc 取消 */
static void files_rename_on_key(char c) {
    if (c == 0x1B) { files_rename_edit = 0; set_status("已删除"); return; }  /* Esc 取消 */
    if (c == '\n' || c == '\r') {                                            /* Enter 确认改名 */
        if (files_rename_new[0] != 0 && strcmp(files_rename_new, files_rename_old) != 0) {
            if (fat32_move(vfs_cwd_cluster(), files_rename_old,
                           vfs_cwd_cluster(), files_rename_new) == 0) {
                files_need_refresh = 1;
                set_status("重命名");        /* 重命名（已改名） */
            } else {
                set_status("已删除");    /* 改名失败 */
            }
        }
        files_rename_edit = 0;
        return;
    }
    int n = (int)strlen(files_rename_new);
    if (c == '\b') {
        if (n > 0) files_rename_new[n - 1] = 0;
        return;
    }
    if (c >= 0x20 && c < 0x7F && n < (int)sizeof(files_rename_new) - 1) {
        files_rename_new[n] = c;
        files_rename_new[n + 1] = 0;
    }
}

static void draw_editor(int idx) {
    window_t* w = &wins[idx];
    int cols = (w->w - 20) / 8;
    int rows = (w->h - 44) / 14;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    /* 顶部工具条：可见的 Save 按钮（点击或 Ctrl+S 都触发保存） */
    int bx = w->x + EDIT_BTN_X, by = w->y + EDIT_BTN_Y;
    fb_rect(bx, by, EDIT_BTN_W, EDIT_BTN_H, rgb(34, 197, 94));
    outline(bx, by, EDIT_BTN_W, EDIT_BTN_H, rgb(15, 23, 42));
    fb_string(bx + 16, by + 5, "Save", rgb(255, 255, 255), (uint32_t)-1);

    /* 文件名输入框（点击进入编辑；键入自定义保存名） + 状态指示 */
    int fx = bx + EDIT_BTN_W + 12, fy = by + 3, fw = 150, fh = 16;
    uint32_t fld_col = editor_fname_edit ? rgb(255, 255, 255) : rgb(226, 232, 240);
    fb_rect(fx, fy, fw, fh, fld_col);
    outline(fx, fy, fw, fh, rgb(15, 23, 42));
    fb_string(fx + 4, fy + 3, editor_fname, rgb(15, 23, 42), (uint32_t)-1);
    if (editor_fname_edit) fb_rect(fx + 4 + 8 * (int)strlen(editor_fname), fy + 2, 1, 12, rgb(15, 23, 42));
    const char* st = (editor_status == 1) ? "[saved]" : (editor_status == 2) ? "[modified]" : "";
    fb_string(w->x + w->w - 70, by + 5, st, rgb(34, 197, 94), (uint32_t)-1);

    /* 计算逻辑行数并自动滚到末尾（追加式光标在结尾） */
    uint32_t p = 0; int total_lines = 0;
    while (p < editor_len) {
        int cl = 0;
        while (p < editor_len && cl < cols && editor_buf[p] != '\n') { p++; cl++; }
        if (p < editor_len && editor_buf[p] == '\n') p++;
        total_lines++;
    }
    editor_scroll = (total_lines > rows) ? (uint32_t)(total_lines - rows) : 0;

    int yy = w->y + 50;
    p = 0; int line = 0;
    while (p < editor_len) {
        char linebuf[256]; int ll = 0;
        while (p < editor_len && ll < cols && editor_buf[p] != '\n') linebuf[ll++] = editor_buf[p++];
        if (p < editor_len && editor_buf[p] == '\n') p++;
        linebuf[ll] = 0;
        if (line >= (int)editor_scroll) {
            fb_string(w->x + 10, yy, linebuf, rgb(15, 23, 42), (uint32_t)-1);
            yy += 14;
        }
        line++;
        if (yy > w->y + w->h - 8) break;
    }
    if (editor_len == 0 && editor_status == 0) {
        fb_string(w->x + 10, w->y + 50, "(empty - type, then Ctrl+S to save)", rgb(148, 163, 184), (uint32_t)-1);
    }
}

/* ===================== 画图（Paint） =====================
 * 256x192 画布 + 预览 + 绘制；保存为 24 位 BMP，文件名可自定义。
 * 笔/橡皮在画布上拖动绘制；颜色取自调色板；Clear 清空画布。 */

/* 计算画布在窗口内的显示矩形：在标题栏+两排工具条之下的可用区里，按 256x192
 * 等比缩放并居中。最大化窗口时可用区更大 → 画布显示得更大（"同步分辨率"）。 */
static void paint_display_rect(window_t* w, int* dx, int* dy, int* dw, int* dh) {
    int avail_x = w->x + PAINT_OFFX;
    int avail_y = w->y + PAINT_OFFY;
    int avail_w = w->w - PAINT_OFFX - 8;
    int avail_h = w->h - PAINT_OFFY - 8;
    if (avail_w < 8) avail_w = 8;
    if (avail_h < 8) avail_h = 8;
    if (avail_w * PAINT_H <= avail_h * PAINT_W) {
        *dw = avail_w;
        *dh = avail_w * PAINT_H / PAINT_W;
    } else {
        *dh = avail_h;
        *dw = avail_h * PAINT_W / PAINT_H;
    }
    if (*dw < 1) *dw = 1;
    if (*dh < 1) *dh = 1;
    *dx = avail_x + (avail_w - *dw) / 2;
    *dy = avail_y + (avail_h - *dh) / 2;
}

/* 在画布 (cx,cy) 处画一个像素（裁剪到画布范围） */
static void paint_plot(int cx, int cy, uint32_t color) {
    if (cx < 0 || cy < 0 || cx >= PAINT_W || cy >= PAINT_H) return;
    paint_canvas[(uint32_t)cy * PAINT_W + cx] = color;
}

/* Bresenham 直线，画进画布（用于拖动画笔的连续笔迹） */
static void paint_line(int x0, int y0, int x1, int y1, uint32_t color) {
    int dx = x1 - x0, dy = y1 - y0;
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    int sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    int x = x0, y = y0, err = ax - ay;
    for (;;) {
        paint_plot(x, y, color);
        if (x == x1 && y == y1) break;
        int e2 = 2 * err;
        if (e2 > -ay) { err -= ay; x += sx; }
        if (e2 <  ax) { err += ax; y += sy; }
    }
}

/* 把 outW×outH 的 32 位画布编码成 24 位 BMP，返回 kmalloc 的缓冲，*out_size 为字节数。 */
static uint8_t* bmp_encode(const uint32_t* pix, int W, int H, uint32_t* out_size) {
    uint32_t rowbytes = ((uint32_t)W * 3 + 3u) / 4u * 4u;
    uint32_t pixels = (uint32_t)H * rowbytes;
    uint32_t filesize = 54 + pixels;
    uint8_t* buf = kmalloc(filesize);
    if (!buf) return 0;
    buf[0] = 'B'; buf[1] = 'M';
    buf[2] = filesize & 0xFF; buf[3] = (filesize >> 8) & 0xFF;
    buf[4] = (filesize >> 16) & 0xFF; buf[5] = (filesize >> 24) & 0xFF;
    buf[6] = 0; buf[7] = 0; buf[8] = 0; buf[9] = 0;
    buf[10] = 54; buf[11] = 0; buf[12] = 0; buf[13] = 0;
    buf[14] = 40; buf[15] = 0; buf[16] = 0; buf[17] = 0;
    buf[18] = W & 0xFF; buf[19] = (W >> 8) & 0xFF; buf[20] = (W >> 16) & 0xFF; buf[21] = (W >> 24) & 0xFF;
    buf[22] = H & 0xFF; buf[23] = (H >> 8) & 0xFF; buf[24] = (H >> 16) & 0xFF; buf[25] = (H >> 24) & 0xFF;
    buf[26] = 1; buf[27] = 0;
    buf[28] = 24; buf[29] = 0;
    buf[30] = 0; buf[31] = 0; buf[32] = 0; buf[33] = 0;
    buf[34] = pixels & 0xFF; buf[35] = (pixels >> 8) & 0xFF; buf[36] = (pixels >> 16) & 0xFF; buf[37] = (pixels >> 24) & 0xFF;
    buf[38] = 0x0B; buf[39] = 0x0B; buf[40] = 0; buf[41] = 0;
    buf[42] = 0x0B; buf[43] = 0x0B; buf[44] = 0; buf[45] = 0;
    buf[46] = 0; buf[47] = 0; buf[48] = 0; buf[49] = 0; buf[50] = 0; buf[51] = 0; buf[52] = 0; buf[53] = 0;
    uint32_t dst = 54;
    for (int y = 0; y < H; y++) {
        int src_row = H - 1 - y;   /* 翻转（BMP 自底向上） */
        const uint32_t* sp = pix + (uint32_t)src_row * W;
        for (int x = 0; x < W; x++) {
            uint32_t c = sp[x];
            buf[dst++] = (uint8_t)(c & 0xFF);
            buf[dst++] = (uint8_t)((c >> 8) & 0xFF);
            buf[dst++] = (uint8_t)((c >> 16) & 0xFF);
        }
        uint32_t pad = rowbytes - (uint32_t)W * 3;
        for (uint32_t k = 0; k < pad; k++) buf[dst++] = 0;
    }
    *out_size = filesize;
    return buf;
}

static void paint_reset(void) {
    for (int i = 0; i < PAINT_W * PAINT_H; i++) paint_canvas[i] = rgb(255, 255, 255);
    paint_pen_color = 0;     /* 黑 */
    paint_tool = 0;          /* 铅笔 */
    paint_fname_edit = 0;
    paint_status = 0;
    fname_copy(paint_fname, "untitled.bmp", sizeof(paint_fname));
}

/* 打开一个具名 BMP（从文件管理器双击图片时调用）：解码到画布（缩放 256x192）。 */
static void paint_open_named(const char* name) {
    paint_reset();
    fname_copy(paint_fname, name, sizeof(paint_fname));
    if (!vfs_is_mounted()) return;
    vfs_dirent_t e;
    if (vfs_open(name, &e) != 0) return;
    uint8_t* raw = kmalloc(e.size);
    if (!raw) return;
    uint32_t rl = 0;
    vfs_read(&e, raw, e.size, &rl);
    paint_status = bmp_decode_to(raw, rl, paint_canvas, PAINT_W, PAINT_H) ? 0 : 2;
    kfree(raw);
}

static void paint_save(void) {
    uint32_t sz = 0;
    uint8_t* data = bmp_encode(paint_canvas, PAINT_W, PAINT_H, &sz);
    if (!data) { paint_status = 2; return; }
    int r = vfs_write_file(paint_fname, data, sz);
    kfree(data);
    paint_status = (r == 0) ? 1 : 2;
}

static void paint_fname_on_key(char c) {
    if (c == '\n' || c == '\r' || c == 0x1B) { paint_fname_edit = 0; return; }
    int n = (int)strlen(paint_fname);
    if (c == '\b') {
        if (n > 0) paint_fname[n - 1] = 0;
        return;
    }
    if (c >= 0x20 && c < 0x7F && n < (int)sizeof(paint_fname) - 1) {
        paint_fname[n] = c;
        paint_fname[n + 1] = 0;
    }
}

static void draw_paint(int idx) {
    window_t* w = &wins[idx];

    /* 工具条：文件名输入框 + Save 按钮（位于标题栏下方，避免与标题栏按钮重叠） */
    int fx = w->x + 8, fy = w->y + 26, fw = 130, fh = 16;
    uint32_t fld_col = paint_fname_edit ? rgb(255, 255, 255) : rgb(226, 232, 240);
    fb_rect(fx, fy, fw, fh, fld_col);
    outline(fx, fy, fw, fh, rgb(15, 23, 42));
    fb_string(fx + 4, fy + 3, paint_fname, rgb(15, 23, 42), (uint32_t)-1);
    if (paint_fname_edit) fb_rect(fx + 4 + 8 * (int)strlen(paint_fname), fy + 2, 1, 12, rgb(15, 23, 42));

    int sbx = w->x + 145, sby = w->y + 26;
    fb_rect(sbx, sby, 50, 18, rgb(34, 197, 94));
    outline(sbx, sby, 50, 18, rgb(15, 23, 42));
    fb_string(sbx + 12, sby + 5, "Save", rgb(255, 255, 255), (uint32_t)-1);

    /* 调色板（8 色）+ 当前色指示框（第二排，标题栏之下） */
    int py = w->y + 48;
    for (int i = 0; i < 8; i++) {
        int px = w->x + 8 + i * 20;
        fb_rect(px, py, 16, 16, paint_palette[i]);
        outline(px, py, 16, 16, rgb(15, 23, 42));
        if (paint_palette[i] == (uint32_t)paint_pen_color)
            outline(px - 1, py - 1, 18, 18, rgb(250, 204, 21));
    }

    /* Clear 按钮 + Eraser 切换 */
    int cbx = w->x + 168, cby = w->y + 48;
    fb_rect(cbx, cby, 44, 16, rgb(203, 213, 225));
    outline(cbx, cby, 44, 16, rgb(15, 23, 42));
    fb_string(cbx + 6, cby + 3, "Clear", rgb(15, 23, 42), (uint32_t)-1);

    int ebx = cbx + 50, eby = w->y + 48;
    uint32_t et_col = (paint_tool == 1) ? rgb(250, 204, 21) : rgb(203, 213, 225);
    fb_rect(ebx, eby, 50, 16, et_col);
    outline(ebx, eby, 50, 16, rgb(15, 23, 42));
    fb_string(ebx + 6, eby + 3, "Erase", rgb(15, 23, 42), (uint32_t)-1);

    /* 画布：按窗口内容区等比缩放显示（最大化即"同步分辨率"，内部缓冲固定 256x192）。
     * 用 paint_display_rect 计算显示矩形，最近邻缩放逐像素绘制。 */
    int dx, dy, dw, dh;
    paint_display_rect(w, &dx, &dy, &dw, &dh);
    for (int yy = 0; yy < dh; yy++)
        for (int xx = 0; xx < dw; xx++) {
            int bx = xx * PAINT_W / dw;
            int by = yy * PAINT_H / dh;
            fb_pixel(dx + xx, dy + yy, paint_canvas[(uint32_t)by * PAINT_W + bx]);
        }
    outline(dx, dy, dw, dh, rgb(15, 23, 42));

    /* 状态指示 */
    const char* st = (paint_status == 1) ? "[saved]" : (paint_status == 2) ? "[modified]" : "";
    fb_string(w->x + w->w - 70, w->y + 8, st, rgb(34, 197, 94), (uint32_t)-1);
}

static int default_app_for(const char* name) {
    int n = (int)strlen(name);
    const char* ext = 0;
    for (int i = n - 1; i >= 0; i--) if (name[i] == '.') { ext = name + i; break; }
    if (ext) {
        if (!strcmp(ext, ".txt") || !strcmp(ext, ".TXT")) return 1;   /* Editor */
        if (!strcmp(ext, ".bmp") || !strcmp(ext, ".BMP")) return 4;   /* Paint  */
    }
    return -1;
}

/* 从文件管理器以默认应用打开具名文件；g_suppress_reset 避免 gui_new_window 清掉已载入内容。 */
void gui_open_file_in_app(int app_idx, const char* name) {
    g_suppress_reset = 1;
    launcher_open(app_idx);
    if (app_idx == 1)      editor_open_named(name);
    else if (app_idx == 4) paint_open_named(name);
}

void gui_handle_key(char c) {
    if (focus < 0 || focus >= win_count) return;
    const char* nm = wins[focus].title;
    if (!strcmp(nm, "Files")) {
        if (files_rename_edit) { files_rename_on_key(c); return; }  /* 重命名输入优先 */
    } else if (!strcmp(nm, "Editor")) {
        if (editor_fname_edit) editor_fname_on_key(c);
        else editor_on_key(c);
    } else if (!strcmp(nm, "Term")) {
        term_on_key(c);
    } else if (!strcmp(nm, "Paint")) {
        if (paint_fname_edit) paint_fname_on_key(c);
    }
}

void gui_toggle_maximize(int idx) {
    if (idx < 0 || idx >= win_count) return;
    window_t* w = &wins[idx];
    if (w->maximized) {
        w->x = w->ox; w->y = w->oy; w->w = w->ow; w->h = w->oh;
        w->maximized = 0;
    } else {
        w->ox = w->x; w->oy = w->y; w->ow = w->w; w->oh = w->h;
        w->x = 0; w->y = 0;
        w->w = (int)fb.width;
        w->h = (int)fb.height - 28;   /* 工作区 = 屏幕减去底部任务栏 */
        w->maximized = 1;
    }
}

void gui_taskbar_click(int idx) {
    if (idx < 0 || idx >= win_count) return;
    if (wins[idx].minimized) {
        wins[idx].minimized = 0;
        focus = idx;
    } else if (idx == focus) {
        wins[idx].minimized = 1;
        focus = -1;
        for (int j = win_count - 1; j >= 0; j--)
            if (!wins[j].minimized) { focus = j; break; }
    } else {
        focus = idx;
    }
}

/* ===================== 关于（About） =====================
 * 展示系统介绍与开发/发布信息。正文直接用 UTF-8 中文，由 fb_cn_string 渲染。
 * 启动方式 / 内存 / 硬盘 / CPU 均取真实运行时值。 */

/* 计算一段混合中英文字符串的像素宽度（CJK 16px，ASCII 8px），用于居中/排版。 */
static int text_px_width(const char* s) {
    int w = 0;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        if (*p < 0x80) { w += 8; p++; }
        else if ((*p & 0xE0) == 0xC0) { w += 16; p += 2; }
        else if ((*p & 0xF0) == 0xE0) { w += 16; p += 3; }
        else if ((*p & 0xF8) == 0xF0) { w += 16; p += 4; }
        else { w += 16; p++; }
    }
    return w;
}

/* 通过 CPUID 读取 CPU 型号字符串（leaf 0x80000002..0x80000004）。 */
static const char* detect_cpu_brand(void) {
    static char brand[64];
    uint32_t a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000));
    if (a < 0x80000004) return "Unknown CPU";
    uint32_t regs[12];
    __asm__ volatile("cpuid" : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3]) : "a"(0x80000002));
    __asm__ volatile("cpuid" : "=a"(regs[4]), "=b"(regs[5]), "=c"(regs[6]), "=d"(regs[7]) : "a"(0x80000003));
    __asm__ volatile("cpuid" : "=a"(regs[8]), "=b"(regs[9]), "=c"(regs[10]), "=d"(regs[11]) : "a"(0x80000004));
    char* p = brand;
    for (int i = 0; i < 12; i++) {
        uint32_t v = regs[i];
        *p++ = (char)(v & 0xFF); *p++ = (char)((v >> 8) & 0xFF);
        *p++ = (char)((v >> 16) & 0xFF); *p++ = (char)((v >> 24) & 0xFF);
    }
    brand[48] = 0;
    const char* s = brand;
    while (*s == ' ') s++;
    return s;
}

/* 把字节数格式化为“xxx MB”或“x.x GB”。 */
static void fmt_bytes(uint64_t bytes, char* out) {
    /* 仅用移位与乘法，避免 64 位除法（freestanding 链接不带 libgcc）。
     * 1024*1024*1024 = 2^30，余数取十分位：rem*10>>30 ≈ rem/(2^30/10)。 */
    if (bytes >= (1ULL << 30)) {
        uint64_t gb = bytes >> 30;
        uint64_t frac = ((bytes & ((1ULL << 30) - 1)) * 10) >> 30; /* 0..9 */
        int n = 0;
        if (gb > 9999) gb = 9999; /* 防止 itoa 越界 */
        itoa((int)gb, out);
        while (out[n]) n++;
        out[n++] = '.';
        out[n++] = (char)('0' + (int)frac);
        out[n++] = ' '; out[n++] = 'G'; out[n++] = 'B'; out[n] = 0;
    } else {
        int mb = (int)(bytes >> 20);
        itoa(mb, out);
        int n = 0; while (out[n]) n++;
        out[n++] = ' '; out[n++] = 'M'; out[n++] = 'B'; out[n] = 0;
    }
}

static void draw_about(int idx) {
    window_t* w = &wins[idx];
    int cx = w->x + w->w / 2;
    int y = w->y + 34;
    uint32_t ink = rgb(15, 23, 42);
    uint32_t sub = rgb(100, 116, 139);

    /* 顶部分隔线 */
    fb_rect(w->x + 12, y, w->w - 24, 1, sub);
    y += 14;

    /* 标题（关于 SproutOS），居中 */
    const char* title = "关于 SproutOS";
    fb_cn_string(cx - text_px_width(title) / 2, y, title, rgb(22, 163, 74), (uint32_t)-1);
    y += 24;

    /* 分隔线 */
    fb_rect(w->x + 12, y, w->w - 24, 1, sub);
    y += 14;

    /* 正文：逐行绘制，左对齐（窗口宽度足够容纳一行中文） */
    const char* lines[] = {
        "本系统作者：竹影清风",
        "SproutOS（嫩芽操作系统）是一个从零自研的、",
        "教学与趣味导向的 x86 操作系统。",
        "它拥有图形桌面、中文界面、文件管理、文本编辑、",
        "画图、网络浏览，以及一整套「中文编程语言 +",
        "字节码虚拟机 + 包管理器」的软件生态。",
        "整个系统围绕一个温柔的隐喻展开：",
        "代码像种子一样种在「大地(Terra)」上，",
        "再用「播种机(Planter)」种进系统，长成应用。",
        0
    };
    for (int i = 0; lines[i]; i++) {
        fb_cn_string(w->x + 16, y, lines[i], ink, (uint32_t)-1);
        y += 20;
    }

    /* 系统参数 */
    y += 6;
    fb_cn_string(w->x + 16, y, "系统参数：", rgb(22, 163, 74), (uint32_t)-1);
    y += 20;

    /* 启动方式（真实） */
    const char* mode = g_boot_mode ? "UEFI" : "BIOS";
    fb_cn_string(w->x + 32, y, "启动方式：", ink, (uint32_t)-1);
    fb_string(w->x + 32 + text_px_width("启动方式："), y, mode, ink, (uint32_t)-1);
    y += 20;

    /* 系统版本（固定） */
    fb_cn_string(w->x + 32, y, "系统版本：" OS_RELEASE, ink, (uint32_t)-1);
    y += 20;

    /* 版本代号（固定：BIOS/UEFI 双启版） */
    fb_cn_string(w->x + 32, y, "版本代号：" OS_EDITION, ink, (uint32_t)-1);
    y += 20;

    /* CPU 型号（真实） */
    fb_cn_string(w->x + 32, y, "系统CPU型号：", ink, (uint32_t)-1);
    fb_string(w->x + 32 + text_px_width("系统CPU型号："), y, detect_cpu_brand(), ink, (uint32_t)-1);
    y += 20;

    /* 内存大小（真实：PMM 检测到的可用页数 × 4KB） */
    char mem[32];
    fmt_bytes((uint64_t)pmm_total_pages() * 4096ULL, mem);
    fb_cn_string(w->x + 32, y, "系统内存大小：", ink, (uint32_t)-1);
    fb_string(w->x + 32 + text_px_width("系统内存大小："), y, mem, ink, (uint32_t)-1);
    y += 20;

    /* 硬盘大小（真实：ATA 磁盘总扇区 × 512） */
    char disk[32];
    block_device_t* bdev = ata_get_bdev(0);
    if (bdev)
        fmt_bytes((uint64_t)bdev->total_sectors * 512ULL, disk);
    else {
        const char* u = "未知";
        int n = 0;
        while (u[n]) { disk[n] = u[n]; n++; }
        disk[n] = 0;
    }
    fb_cn_string(w->x + 32, y, "系统硬盘大小：", ink, (uint32_t)-1);
    fb_string(w->x + 32 + text_px_width("系统硬盘大小："), y, disk, ink, (uint32_t)-1);
}

/* 回收站：当前为占位窗口（内核暂无独立回收存储），显示“回收站为空”。
 *   标题栏为 "Recycle"（英文，遵循窗口标题约定）；窗口内居中显示中文标题与空提示。 */
static void draw_recycle(int idx) {
    window_t* w = &wins[idx];
    int content_x = w->x + 12;

    if (!vfs_is_mounted()) {
        fb_string(content_x, w->y + 34, "No disk mounted", rgb(220, 38, 38), (uint32_t)-1);
        return;
    }

    fb_string(content_x, w->y + 34, "/RECYCLE", rgb(15, 23, 42), (uint32_t)-1);

    /* 操作状态提示（还原/删除后显示，中文点阵，与文件管理器共用 g_files_status） */
    if (g_files_status[0] != 0)
        fb_cn_string(w->x + w->w - 110, w->y + 34, g_files_status, rgb(22, 163, 74), (uint32_t)-1);

    if (recycle_need_refresh) {
        vfs_list_recycle(&recycle_dir);
        recycle_need_refresh = 0;
        if (recycle_scroll > recycle_dir.count) recycle_scroll = 0;
    }

    int list_top = w->y + 74;
    int row_h = 18;
    if (recycle_dir.count == 0) {
        static const char empty[] = "回收站为空";   /* 回收站为空 */
        int el = utf8_count(empty) * 16;
        fb_cn_string(w->x + w->w / 2 - el / 2, list_top + 10, empty, rgb(100, 116, 139), (uint32_t)-1);
        return;
    }
    for (int i = 0; i < recycle_dir.count; i++) {
        int ry = list_top + (i - recycle_scroll) * row_h;
        if (ry < list_top) continue;
        if (ry + row_h > w->y + w->h - 4) break;
        uint32_t col = recycle_dir.entries[i].is_dir ? rgb(249, 115, 22) : rgb(59, 130, 246);
        fb_rect(w->x + 14, ry + 3, 12, 12, col);
        fb_string(w->x + 32, ry + 4, recycle_dir.entries[i].name, rgb(15, 23, 42), (uint32_t)-1);
        if (!recycle_dir.entries[i].is_dir) {
            char sz[16];
            itoa(recycle_dir.entries[i].size, sz);
            fb_string(w->x + w->w - 70, ry + 4, sz, rgb(100, 116, 139), (uint32_t)-1);
        }
    }
}

static void gui_draw_one(int idx) {
    window_t* w = &wins[idx];
    const char* nm = w->title;
    gui_draw_window(w);
    if (!strcmp(nm, "Term")) draw_term(idx);
    else if (!strcmp(nm, "Files")) draw_files(idx);
    else if (!strcmp(nm, "Editor")) draw_editor(idx);
    else if (!strcmp(nm, "About")) draw_about(idx);
    else if (!strcmp(nm, "Paint")) draw_paint(idx);
    else if (!strcmp(nm, "Recycle")) draw_recycle(idx);
    else fb_string(w->x + 12, w->y + 40, nm, rgb(15, 23, 42), (uint32_t)-1);
}

void gui_draw_apps(void) {
    /* 最小化窗口不绘制；聚焦窗口最后绘制（置顶） */
    for (int i = 0; i < win_count; i++) {
        if (wins[i].minimized) continue;
        if (i == focus) continue;
        gui_draw_one(i);
    }
    if (focus >= 0 && !wins[focus].minimized)
        gui_draw_one(focus);
}

const char** gui_app_names(void) { return app_names; }
