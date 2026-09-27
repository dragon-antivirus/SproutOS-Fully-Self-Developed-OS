/* kernel/seedvm.c — SeedVM 字节码解释器实现
 *
 * 栈式字节码 VM，帧制执行，全屏独占模式。
 * Terra 编译器(scripts/terrac.py)生成 .seed 包，本 VM 解释执行。
 *
 * 里程碑 M5：包管理。
 * =========================================================================*/

#include "kernel.h"    /* 先 kernel.h：提供整型 typedef（含 NULL/int16_t/uintptr_t）与 fb_*、kmalloc/kfree、string 函数声明 */
#include "seedvm.h"    /* 已被 kernel.h include（有 include guard），此处直接可用 */

/* ===== 颜色表 ===========================================================*/

const uint32_t seed_colors[SEED_COLOR_COUNT] = {
    0xFF000000,  /* 0=黑 */
    0xFFFFFFFF,  /* 1=白 */
    0xFFE53935,  /* 2=红 */
    0xFF4CAF50,  /* 3=绿 */
    0xFF2196F3,  /* 4=蓝 */
    0xFFFFEB3B,  /* 5=黄 */
    0xFF00BCD4,  /* 6=青 */
    0xFF9C27B0,  /* 7=紫 */
};

/* ===== 内部辅助 =========================================================*/

static uint8_t read_u8(seed_vm_t* vm) {
    if (vm->ip >= vm->code_size) { vm->error = 1; vm->halted = 1; return 0; }
    return vm->code[vm->ip++];
}

static uint16_t read_u16(seed_vm_t* vm) {
    uint16_t v = read_u8(vm);
    v |= (uint16_t)read_u8(vm) << 8;
    return v;
}

static int16_t read_s16(seed_vm_t* vm) {
    return (int16_t)read_u16(vm);
}

static uint32_t read_u32(seed_vm_t* vm) {
    uint32_t v = read_u8(vm);
    v |= (uint32_t)read_u8(vm) << 8;
    v |= (uint32_t)read_u8(vm) << 16;
    v |= (uint32_t)read_u8(vm) << 24;
    return v;
}

static void vm_push(seed_vm_t* vm, int32_t v) {
    if (vm->sp >= SEED_STACK_MAX - 1) {
        serial_write("[SEED] stack overflow\n");
        vm->error = 1; vm->halted = 1;
        return;
    }
    vm->stack[++vm->sp] = v;
}

static int32_t vm_pop(seed_vm_t* vm) {
    if (vm->sp < 0) {
        serial_write("[SEED] stack underflow\n");
        vm->error = 1; vm->halted = 1;
        return 0;
    }
    return vm->stack[vm->sp--];
}

/* 从数据池取字符串指针 */
static const char* data_string(seed_vm_t* vm, uint16_t idx) {
    if (idx >= vm->data_size) {
        serial_write("[SEED] bad string index\n");
        vm->error = 1;
        return "";
    }
    return (const char*)(vm->data + idx);
}

/* 计算字符串渲染宽度（ASCII=8px, 中文=16px） */
static int text_width(const char* s) {
    int w = 0;
    const unsigned char* p = (const unsigned char*)s;
    while (*p) {
        if ((*p & 0x80) == 0) { w += SEED_CHAR_W; p++; }
        else if ((*p & 0xE0) == 0xC0) { w += SEED_CN_CHAR_W; p += 2; }
        else if ((*p & 0xF0) == 0xE0) { w += SEED_CN_CHAR_W; p += 3; }
        else if ((*p & 0xF8) == 0xF0) { w += SEED_CN_CHAR_W; p += 4; }
        else p++;
    }
    return w;
}

/* 在光标处渲染字符串并推进光标 */
static void vm_print_str(seed_vm_t* vm, const char* s) {
    int px = vm->cursor_x;
    int py = vm->cursor_y;
    if (vm->terminal_mode) {
        px = vm->client_x + vm->cursor_x;
        py = vm->client_y + vm->cursor_y - vm->scroll_offset * SEED_LINE_H;
    }
    fb_cn_string(px, py, s, rgb(220, 220, 220), (uint32_t)-1);
    vm->cursor_x += text_width(s);
}

/* 在光标处渲染数字 */
static void vm_print_num(seed_vm_t* vm, int32_t n) {
    char buf[16];
    itoa(n, buf);
    vm_print_str(vm, buf);
}

/* 光标换行；终端模式下超出客户区高度则滚动 */
static void vm_print_nl(seed_vm_t* vm) {
    vm->cursor_x = 8;
    vm->cursor_y += SEED_LINE_H;
    if (vm->terminal_mode) {
        /* 计算当前行是否超出客户区底部 */
        int visible_y = vm->cursor_y - vm->scroll_offset * SEED_LINE_H;
        if (visible_y + SEED_LINE_H > vm->client_h) {
            vm->scroll_offset++;
            /* 重绘整个窗口内容（清客户区 + 重新渲染所有已输出行）
             * 简化实现：清客户区，光标回到第0行，由调用方决定是否重绘
             * 这里采用：滚动时清客户区，后续输出从顶部开始 */
            fb_rect(vm->client_x, vm->client_y, vm->client_w, vm->client_h, 0xFF0A0A12);
            vm->cursor_y = 8;
            vm->scroll_offset = 0;
        }
    } else {
        if (vm->cursor_y + SEED_LINE_H > (int)fb.height) {
            fb_clear(0xFF0A0A12);
            vm->cursor_x = 8;
            vm->cursor_y = 8;
        }
    }
}

/* ===== 终端窗口绘制 ====================================================*/

static void vm_draw_terminal_window(seed_vm_t* vm) {
    if (!vm->terminal_mode) return;
    fb_rect(vm->win_x + 3, vm->win_y + 3, vm->win_w, vm->win_h, 0x40000000);
    fb_rect(vm->win_x, vm->win_y, vm->win_w, vm->win_h, 0xFF1E1E2E);
    fb_rect(vm->win_x, vm->win_y, vm->win_w, 1, 0xFF4A4A6A);
    fb_rect(vm->win_x, vm->win_y + vm->win_h - 1, vm->win_w, 1, 0xFF4A4A6A);
    fb_rect(vm->win_x, vm->win_y, 1, vm->win_h, 0xFF4A4A6A);
    fb_rect(vm->win_x + vm->win_w - 1, vm->win_y, 1, vm->win_h, 0xFF4A4A6A);
    fb_rect(vm->win_x + 1, vm->win_y + 1, vm->win_w - 2, 26, 0xFF2D2D44);
    fb_rect(vm->win_x + 1, vm->win_y + 27, vm->win_w - 2, 1, 0xFF4A4A6A);
    char title[64];
    int tl = 0;
    const char* name = vm->name[0] ? vm->name : "Terminal";
    while (name[tl] && tl < 40) { title[tl] = name[tl]; tl++; }
    title[tl] = 0;
    fb_cn_string(vm->win_x + 10, vm->win_y + 6, title, rgb(200, 200, 230), (uint32_t)-1);
    fb_cn_string(vm->win_x + vm->win_w - 120, vm->win_y + 6,
                  "ESC 关闭", rgb(140, 140, 170), (uint32_t)-1);
    fb_rect(vm->client_x, vm->client_y, vm->client_w, vm->client_h, 0xFF0A0A12);
}

static void vm_refresh_titlebar(seed_vm_t* vm) {
    if (!vm->terminal_mode) return;
    fb_rect(vm->win_x + 1, vm->win_y + 1, vm->win_w - 2, 26, 0xFF2D2D44);
    fb_rect(vm->win_x + 1, vm->win_y + 27, vm->win_w - 2, 1, 0xFF4A4A6A);
    char title[64];
    int tl = 0;
    const char* name = vm->name[0] ? vm->name : "Terminal";
    while (name[tl] && tl < 40) { title[tl] = name[tl]; tl++; }
    title[tl] = 0;
    fb_cn_string(vm->win_x + 10, vm->win_y + 6, title, rgb(200, 200, 230), (uint32_t)-1);
    fb_cn_string(vm->win_x + vm->win_w - 120, vm->win_y + 6,
                  "ESC 关闭", rgb(140, 140, 170), (uint32_t)-1);
}

/* ===== 数学 / 字符串辅助（M5+ 新增） ================================*/

/* 简单 LCG 伪随机数生成器 */
static uint32_t seedvm_rand_state = 0;
static int seedvm_rand_inited = 0;

static int seedvm_rand_range(int min, int max) {
    if (!seedvm_rand_inited) {
        /* 用栈地址 + 当前光标位置做种子，尽量避免每次启动相同 */
        seedvm_rand_state = (uint32_t)(uintptr_t)&seedvm_rand_state;
        seedvm_rand_state ^= (uint32_t)fb.width * 7919;
        seedvm_rand_state ^= (uint32_t)fb.height * 6271;
        seedvm_rand_inited = 1;
    }
    /* LCG: glibc 风格参数 */
    seedvm_rand_state = seedvm_rand_state * 1103515245u + 12345u;
    int range = max - min + 1;
    if (range <= 0) return min;
    /* 用高 16 位，分布更均匀 */
    return min + (int)((seedvm_rand_state >> 16) % (uint32_t)range);
}

/* 整数平方根（牛顿迭代） */
static int seedvm_isqrt(int n) {
    if (n <= 0) return 0;
    int x = n;
    int y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + n / x) / 2;
    }
    return x;
}

/* 字符串转大写（只处理 ASCII，UTF-8 中文不动），返回新分配字符串 */
static char* seedvm_str_upper(const char* s) {
    int len = strlen(s);
    char* r = kmalloc(len + 1);
    if (!r) return 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'a' && c <= 'z') r[i] = c - 32;
        else r[i] = s[i];
    }
    r[len] = 0;
    return r;
}

/* 字符串转小写 */
static char* seedvm_str_lower(const char* s) {
    int len = strlen(s);
    char* r = kmalloc(len + 1);
    if (!r) return 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'A' && c <= 'Z') r[i] = c + 32;
        else r[i] = s[i];
    }
    r[len] = 0;
    return r;
}

/* ===== 字节码执行 =======================================================*/

static void exec_one(seed_vm_t* vm) {
    uint8_t op = read_u8(vm);
    int32_t a, b;
    const char* sa;
    const char* sb;
    char* concat_result;
    int len_a, len_b;

    switch (op) {
    /* --- 控制流 --- */
    case OP_NOP:
        break;
    case OP_HALT:
        vm->halted = 1;
        break;
    case OP_JMP: {
        int16_t off = read_s16(vm);
        /* off 是相对当前 IP 的偏移（已读 3 字节 = op + s16） */
        int32_t target = (int32_t)vm->ip + off;
        if (target < 0 || target >= (int32_t)vm->code_size) {
            serial_write("[SEED] jmp out of bounds\n");
            vm->error = 1; vm->halted = 1;
        } else {
            vm->ip = (uint32_t)target;
        }
        break;
    }
    case OP_JMPZ: {
        int16_t off = read_s16(vm);
        a = vm_pop(vm);
        if (a == 0) {
            int32_t target = (int32_t)vm->ip + off;
            if (target < 0 || target >= (int32_t)vm->code_size) {
                vm->error = 1; vm->halted = 1;
            } else {
                vm->ip = (uint32_t)target;
            }
        }
        break;
    }
    case OP_JMPNZ: {
        int16_t off = read_s16(vm);
        a = vm_pop(vm);
        if (a != 0) {
            int32_t target = (int32_t)vm->ip + off;
            if (target < 0 || target >= (int32_t)vm->code_size) {
                vm->error = 1; vm->halted = 1;
            } else {
                vm->ip = (uint32_t)target;
            }
        }
        break;
    }

    /* --- 栈操作 --- */
    case OP_PUSH8:
        vm_push(vm, (int32_t)read_u8(vm));   /* 零扩展：负数由编译器经 NEG 计算 */
        break;
    case OP_PUSH32:
        vm_push(vm, (int32_t)read_u32(vm));
        break;
    case OP_PUSHSTR: {
        uint16_t idx = read_u16(vm);
        vm_push(vm, (int32_t)(uintptr_t)data_string(vm, idx));
        break;
    }
    case OP_POP:
        vm_pop(vm);
        break;
    case OP_DUP:
        if (vm->sp >= 0) vm_push(vm, vm->stack[vm->sp]);
        break;

    /* --- 算术 --- */
    case OP_ADD:
        a = vm_pop(vm); b = vm_pop(vm); vm_push(vm, b + a); break;
    case OP_SUB:
        a = vm_pop(vm); b = vm_pop(vm); vm_push(vm, b - a); break;
    case OP_MUL:
        a = vm_pop(vm); b = vm_pop(vm); vm_push(vm, b * a); break;
    case OP_DIV:
        a = vm_pop(vm); b = vm_pop(vm);
        if (a == 0) { serial_write("[SEED] div by zero\n"); vm_push(vm, 0); }
        else vm_push(vm, b / a);
        break;
    case OP_MOD:
        a = vm_pop(vm); b = vm_pop(vm);
        if (a == 0) { vm_push(vm, 0); }
        else vm_push(vm, b % a);
        break;
    case OP_NEG:
        a = vm_pop(vm); vm_push(vm, -a); break;

    /* --- 比较 --- */
    case OP_EQ:  a = vm_pop(vm); b = vm_pop(vm); vm_push(vm, b == a); break;
    case OP_NEQ: a = vm_pop(vm); b = vm_pop(vm); vm_push(vm, b != a); break;
    case OP_LT:  a = vm_pop(vm); b = vm_pop(vm); vm_push(vm, b < a); break;
    case OP_GT:  a = vm_pop(vm); b = vm_pop(vm); vm_push(vm, b > a); break;
    case OP_LE:  a = vm_pop(vm); b = vm_pop(vm); vm_push(vm, b <= a); break;
    case OP_GE:  a = vm_pop(vm); b = vm_pop(vm); vm_push(vm, b >= a); break;

    /* --- 逻辑 --- */
    case OP_AND: a = vm_pop(vm); b = vm_pop(vm); vm_push(vm, (b && a) ? 1 : 0); break;
    case OP_OR:  a = vm_pop(vm); b = vm_pop(vm); vm_push(vm, (b || a) ? 1 : 0); break;
    case OP_NOT: a = vm_pop(vm); vm_push(vm, (!a) ? 1 : 0); break;

    /* --- 局部变量 --- */
    case OP_LOAD: {
        uint8_t idx = read_u8(vm);
        if (idx >= SEED_LOCALS_MAX) { vm->error = 1; break; }
        vm_push(vm, vm->locals[idx]);
        break;
    }
    case OP_STORE: {
        uint8_t idx = read_u8(vm);
        if (idx >= SEED_LOCALS_MAX) { vm->error = 1; vm_pop(vm); break; }
        vm->locals[idx] = vm_pop(vm);
        break;
    }

    /* --- 终端 I/O --- */
    case OP_PRINT_STR:
        a = vm_pop(vm);
        vm_print_str(vm, (const char*)(uintptr_t)a);
        break;
    case OP_PRINT_NUM:
        a = vm_pop(vm);
        vm_print_num(vm, a);
        break;
    case OP_PRINT_NL:
        vm_print_nl(vm);
        break;
    case OP_PRINT_CH:
        a = vm_pop(vm);
        {
            char ch[2] = { (char)a, 0 };
            vm_print_str(vm, ch);
        }
        break;
    case OP_PRINT: {
        /* 运行时类型检测：堆地址(>=0x1000000)视为字符串指针，否则整数 */
        a = vm_pop(vm);
        if ((uint32_t)a >= 0x1000000)
            vm_print_str(vm, (const char*)(uintptr_t)a);
        else
            vm_print_num(vm, a);
        break;
    }

    /* --- 类型转换 --- */
    case OP_TOSTR: {
        a = vm_pop(vm);
        char* s = kmalloc(16);
        if (s) { itoa(a, s); vm_push(vm, (int32_t)(uintptr_t)s); }
        else vm_push(vm, 0);
        break;
    }
    case OP_TOINT: {
        a = vm_pop(vm);
        sa = (const char*)(uintptr_t)a;
        int result = 0;
        int sign = 1;
        int i = 0;
        if (sa[0] == '-') { sign = -1; i = 1; }
        for (; sa[i] >= '0' && sa[i] <= '9'; i++)
            result = result * 10 + (sa[i] - '0');
        vm_push(vm, result * sign);
        break;
    }

    /* --- 字符串拼接 --- */
    case OP_CONCAT: {
        a = vm_pop(vm);  /* a = 后面的字符串 */
        b = vm_pop(vm);  /* b = 前面的字符串 */
        sa = (const char*)(uintptr_t)a;
        sb = (const char*)(uintptr_t)b;
        len_a = strlen(sa);
        len_b = strlen(sb);
        concat_result = kmalloc(len_a + len_b + 1);
        if (concat_result) {
            memcpy(concat_result, sb, len_b);
            memcpy(concat_result + len_b, sa, len_a);
            concat_result[len_a + len_b] = 0;
            vm_push(vm, (int32_t)(uintptr_t)concat_result);
        } else {
            vm_push(vm, 0);
        }
        break;
    }

    /* --- GUI --- */
    case OP_G_RECT: {
        uint32_t color = (uint32_t)vm_pop(vm);
        int h = vm_pop(vm); int w = vm_pop(vm);
        int y = vm_pop(vm); int x = vm_pop(vm);
        fb_rect(x, y, w, h, color);
        break;
    }
    case OP_G_TEXT: {
        uint32_t color = (uint32_t)vm_pop(vm);
        const char* str = (const char*)(uintptr_t)vm_pop(vm);
        int y = vm_pop(vm); int x = vm_pop(vm);
        fb_cn_string(x, y, str, color, (uint32_t)-1);
        break;
    }
    case OP_G_CIRCLE: {
        uint32_t color = (uint32_t)vm_pop(vm);
        int r = vm_pop(vm); int y = vm_pop(vm); int x = vm_pop(vm);
        fb_circle(x, y, r, color);
        break;
    }
    case OP_G_CLEAR:
        fb_clear(0xFF0A0A12);
        vm->cursor_x = 8;
        vm->cursor_y = 8;
        break;
    case OP_G_FLUSH:
        fb_flush();
        break;
    case OP_G_WIDTH:
        vm_push(vm, (int32_t)fb.width);
        break;
    case OP_G_HEIGHT:
        vm_push(vm, (int32_t)fb.height);
        break;
    case OP_G_PIXEL: {
        uint32_t color = (uint32_t)vm_pop(vm);
        int y = vm_pop(vm); int x = vm_pop(vm);
        fb_pixel(x, y, color);
        break;
    }
    case OP_G_LINE: {
        uint32_t color = (uint32_t)vm_pop(vm);
        int y2 = vm_pop(vm); int x2 = vm_pop(vm);
        int y1 = vm_pop(vm); int x1 = vm_pop(vm);
        fb_line(x1, y1, x2, y2, color);
        break;
    }

    /* --- 颜色常量 --- */
    case OP_PUSH_COLOR: {
        uint8_t idx = read_u8(vm);
        if (idx < SEED_COLOR_COUNT)
            vm_push(vm, (int32_t)seed_colors[idx]);
        else
            vm_push(vm, 0xFFFFFFFF);
        break;
    }

    /* --- 定时 / 交互 --- */
    case OP_SLEEP: {
        a = vm_pop(vm);
        if (a > 0 && a < 60000)
            udelay((uint32_t)a * 1000);   /* 忙等（帧内阻塞，见备注） */
        break;
    }
    case OP_ASK: {
        /* 字节流: OP_ASK <类型byte>；栈顶为提示串 */
        uint8_t typ = read_u8(vm);
        a = vm_pop(vm);
        vm_print_str(vm, (const char*)(uintptr_t)a);
        vm->ask_type = typ;
        vm->waiting_input = 1;
        vm->input_len = 0;
        vm->input_buf[0] = 0;
        vm->has_answer = 0;
        break;
    }
    case OP_GETINPUT:
        if (vm->has_answer)
            vm_push(vm, vm->answer);
        else
            vm_push(vm, 0);
        break;

    /* --- 数学 / 字符串（M5+ 新增） --- */
    case OP_RAND: {
        /* pop max, pop min; push rand(min..max) */
        int mx = vm_pop(vm);
        int mn = vm_pop(vm);
        vm_push(vm, seedvm_rand_range(mn, mx));
        break;
    }
    case OP_ABS: {
        a = vm_pop(vm);
        vm_push(vm, a < 0 ? -a : a);
        break;
    }
    case OP_SQRT: {
        a = vm_pop(vm);
        vm_push(vm, seedvm_isqrt(a));
        break;
    }
    case OP_STRLEN: {
        a = vm_pop(vm);
        sa = (const char*)(uintptr_t)a;
        vm_push(vm, (int32_t)strlen(sa));
        break;
    }
    case OP_UPPER: {
        a = vm_pop(vm);
        sa = (const char*)(uintptr_t)a;
        char* up = seedvm_str_upper(sa);
        vm_push(vm, (int32_t)(uintptr_t)up);
        break;
    }
    case OP_LOWER: {
        a = vm_pop(vm);
        sa = (const char*)(uintptr_t)a;
        char* lo = seedvm_str_lower(sa);
        vm_push(vm, (int32_t)(uintptr_t)lo);
        break;
    }

    default:
        serial_write("[SEED] unknown opcode: 0x");
        serial_puth(op);
        serial_write("\n");
        vm->error = 1; vm->halted = 1;
        break;
    }
}

/* ===== 帧制执行 =========================================================*/

int seedvm_frame(seed_vm_t* vm) {
    if (!vm || vm->halted) return 1;
    if (vm->waiting_input) return 0; /* 等待用户输入，暂停执行 */

    int budget = SEEDVM_IPS_PER_FRAME;
    while (budget-- > 0 && !vm->halted && !vm->error) {
        exec_one(vm);
    }

    if (vm->error) {
        serial_write("[SEED] VM error at ip=");
        serial_puti(vm->ip);
        serial_write("\n");
        vm->halted = 1;
    }

    /* 终端模式下每帧刷新标题栏，确保不被意外覆盖 */
    if (vm->terminal_mode && !vm->halted) {
        vm_refresh_titlebar(vm);
    }

    return vm->halted;
}

/* ===== 加载 / 清理 =====================================================*/

seed_vm_t* seedvm_load(const char* path) {
    vfs_dirent_t de;

    if (vfs_open(path, &de) != 0) {
        serial_write("[SEED] file not found: ");
        serial_write(path);
        serial_write("\n");
        return NULL;
    }

    if (de.size < SEED_HDR_SIZE) {
        serial_write("[SEED] file too small\n");
        return NULL;
    }

    /* 读取整个文件 */
    uint8_t* buf = kmalloc(de.size);
    if (!buf) {
        serial_write("[SEED] kmalloc failed for file\n");
        return NULL;
    }

    uint32_t out_len = 0;
    if (vfs_read(&de, buf, de.size, &out_len) != 0) {
        serial_write("[SEED] vfs_read failed\n");
        kfree(buf);
        return NULL;
    }

    /* DEBUG: 打印文件头前 88 字节的十六进制 */
    {
        serial_write("[SEED] DEBUG: first_cluster=");
        serial_puti(de.first_cluster);
        serial_write(" size=");
        serial_puti(de.size);
        serial_write(" out_len=");
        serial_puti(out_len);
        serial_write("\n");
        serial_write("[SEED] DEBUG: hdr hex:\n");
        for (uint32_t i = 0; i < 88 && i < out_len; i++) {
            char hex[4];
            hex[0] = "0123456789ABCDEF"[buf[i] >> 4];
            hex[1] = "0123456789ABCDEF"[buf[i] & 0xF];
            hex[2] = ' ';
            hex[3] = 0;
            serial_write(hex);
            if ((i + 1) % 16 == 0) serial_write("\n");
        }
        serial_write("\n");
    }

    /* 校验头 */
    seed_header_t* hdr = (seed_header_t*)buf;
    if (hdr->magic != SEED_MAGIC) {
        serial_write("[SEED] bad magic\n");
        kfree(buf);
        return NULL;
    }
    if (hdr->version != SEED_VERSION) {
        serial_write("[SEED] unsupported version\n");
        kfree(buf);
        return NULL;
    }

    /* 分配 VM */
    seed_vm_t* vm = (seed_vm_t*)kmalloc(sizeof(seed_vm_t));
    if (!vm) {
        serial_write("[SEED] kmalloc failed for vm\n");
        kfree(buf);
        return NULL;
    }
    memset(vm, 0, sizeof(*vm));

    /* 分配代码和数据缓冲 */
    vm->code = (uint8_t*)kmalloc(hdr->code_size ? hdr->code_size : 1);
    vm->data = (uint8_t*)kmalloc(hdr->data_size ? hdr->data_size : 1);
    if (!vm->code || (!vm->data && hdr->data_size > 0)) {
        serial_write("[SEED] kmalloc failed for code/data\n");
        if (vm->code) kfree(vm->code);
        if (vm->data) kfree(vm->data);
        kfree(vm);
        kfree(buf);
        return NULL;
    }

    /* 拷贝代码和数据 */
    if (hdr->code_size > 0)
        memcpy(vm->code, buf + SEED_HDR_SIZE, hdr->code_size);
    if (hdr->data_size > 0)
        memcpy(vm->data, buf + SEED_HDR_SIZE + hdr->code_size, hdr->data_size);

    vm->code_size = hdr->code_size;
    vm->data_size = hdr->data_size;
    vm->ip = hdr->entry;
    vm->sp = -1;
    vm->halted = 0;
    vm->error = 0;
    vm->cursor_x = 8;
    vm->cursor_y = 8;

    memcpy(vm->name, hdr->name, SEED_NAME_LEN);
    vm->name[SEED_NAME_LEN - 1] = 0;
    memcpy(vm->author, hdr->author, SEED_AUTHOR_LEN);
    vm->author[SEED_AUTHOR_LEN - 1] = 0;

    kfree(buf);

    /* 初始化屏幕 */
    fb_clear(0xFF0A0A12);

    /* 终端窗口模式：非 GUI 程序使用居中终端窗口，GUI 程序全屏 */
    vm->terminal_mode = (hdr->flags & 1) ? 0 : 1;
    vm->scroll_offset = 0;
    if (vm->terminal_mode) {
        vm->win_w = fb.width * 80 / 100;
        vm->win_h = fb.height * 72 / 100;
        if (vm->win_w < 400) vm->win_w = 400;
        if (vm->win_h < 300) vm->win_h = 300;
        vm->win_x = (fb.width - vm->win_w) / 2;
        vm->win_y = (fb.height - vm->win_h) / 2;
        /* 客户区：窗口内去掉边框(1px)和标题栏(28px) */
        vm->client_x = vm->win_x + 1;
        vm->client_y = vm->win_y + 28;
        vm->client_w = vm->win_w - 2;
        vm->client_h = vm->win_h - 29;
        /* 光标相对于客户区左上角 */
        vm->cursor_x = 8;
        vm->cursor_y = 8;
        vm_draw_terminal_window(vm);
    }

    serial_write("[SEED] loaded: ");
    serial_write(vm->name);
    serial_write(" (code=");
    serial_puti(hdr->code_size);
    serial_write(" data=");
    serial_puti(hdr->data_size);
    serial_write(")\n");

    return vm;
}

void seedvm_done(seed_vm_t* vm) {
    if (!vm) return;
    if (vm->code) kfree(vm->code);
    if (vm->data) kfree(vm->data);
    kfree(vm);
    serial_write("[SEED] vm freed\n");
}

int seedvm_get_info(const char* path, char* name, char* author,
                    uint32_t* code_size, uint32_t* flags) {
    vfs_dirent_t de;
    if (vfs_open(path, &de) != 0) return -1;
    if (de.size < SEED_HDR_SIZE) return -1;

    uint8_t buf[SEED_HDR_SIZE];
    uint32_t out_len;
    if (vfs_read(&de, buf, SEED_HDR_SIZE, &out_len) != 0) return -1;
    if (out_len < SEED_HDR_SIZE) return -1;

    seed_header_t* hdr = (seed_header_t*)buf;
    if (hdr->magic != SEED_MAGIC) return -1;

    if (name) { memcpy(name, hdr->name, SEED_NAME_LEN); name[SEED_NAME_LEN - 1] = 0; }
    if (author) { memcpy(author, hdr->author, SEED_AUTHOR_LEN); author[SEED_AUTHOR_LEN - 1] = 0; }
    if (code_size) *code_size = hdr->code_size;
    if (flags) *flags = hdr->flags;

    return 0;
}
