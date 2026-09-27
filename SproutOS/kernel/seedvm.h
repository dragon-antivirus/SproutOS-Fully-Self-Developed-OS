/* kernel/seedvm.h — SeedVM 字节码解释器 & .seed 包格式
 *
 * .seed 是 SproutOS 的专属软件包格式。一个 .seed 文件包含：
 *   - 头部（魔数 SEED + 版本 + 元数据 + 段偏移）
 *   - 代码段（字节码）
 *   - 数据段（UTF-8 字符串池，\0 分隔）
 *
 * SeedVM 是栈式字节码解释器：
 *   - 256 深操作数栈（32 位整数 / 字符串指针）
 *   - 64 个局部变量槽
 *   - 帧制执行（每帧执行 N 条指令，避免阻塞主循环）
 *   - 全屏独占模式（类似浏览器），ESC 退出回桌面
 *
 * Terra 编译器(scripts/terrac.py)把 .terra 源码编译成 .seed 包。
 *
 * 里程碑 M5：包管理。
 * =========================================================================*/

#ifndef SEEDVM_H
#define SEEDVM_H

/* 类型说明：uint8_t/uint16_t/uint32_t/int32_t 由 kernel.h 顶部 typedef 提供
 * （freestanding 环境，无 libc）。本头文件经 kernel.h include，
 * 因此直接使用这些类型，不再 #include <stdint.h>。 */

/* ===== .seed 包格式 ======================================================*/

#define SEED_MAGIC      0x44454553   /* "SEED" 小端序: S=0x53 E=0x45 E=0x45 D=0x44 */
#define SEED_VERSION    1
#define SEED_NAME_LEN   32
#define SEED_AUTHOR_LEN 32
#define SEED_HDR_SIZE   88

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;            /* 0:  SEED_MAGIC                       */
    uint16_t version;          /* 4:  SEED_VERSION                     */
    uint16_t flags;            /* 6:  bit0=has_gui                     */
    char     name[SEED_NAME_LEN];     /* 8:  UTF-8 包名, \0 结尾        */
    char     author[SEED_AUTHOR_LEN]; /* 40: UTF-8 作者, \0 结尾        */
    uint32_t code_size;        /* 72: 代码段字节数                     */
    uint32_t data_size;       /* 76: 数据段字节数                     */
    uint32_t entry;           /* 80: 入口偏移（代码段内）              */
    uint32_t reserved;        /* 84: 保留，填 0                       */
} seed_header_t;
#pragma pack(pop)

/* ===== 操作码 ============================================================*/

/* 控制流 */
#define OP_NOP      0x00
#define OP_HALT     0x01
#define OP_JMP      0x02   /* +int16 偏移（从当前 IP 后算）              */
#define OP_JMPZ     0x03   /* pop cond; 若 0 则跳 +int16               */
#define OP_JMPNZ    0x04   /* pop cond; 若非 0 则跳 +int16             */

/* 栈操作 */
#define OP_PUSH8    0x10   /* +uint8 立即数                              */
#define OP_PUSH32   0x11   /* +uint32 立即数                             */
#define OP_PUSHSTR  0x12   /* +uint16 字符串池索引 → 压入字符串指针       */
#define OP_POP      0x13
#define OP_DUP      0x14

/* 算术 */
#define OP_ADD      0x20   /* pop a,b; push b+a                         */
#define OP_SUB      0x21   /* pop a,b; push b-a                         */
#define OP_MUL      0x22   /* pop a,b; push b*a                         */
#define OP_DIV      0x23   /* pop a,b; push b/a (整数除)                */
#define OP_MOD      0x24   /* pop a,b; push b%a                         */
#define OP_NEG      0x25   /* pop a; push -a                            */

/* 比较 → 压 1 或 0 */
#define OP_EQ       0x30
#define OP_NEQ      0x31
#define OP_LT       0x32
#define OP_GT       0x33
#define OP_LE       0x34
#define OP_GE       0x35

/* 逻辑 */
#define OP_AND      0x36   /* pop a,b; push (b && a) ? 1 : 0           */
#define OP_OR       0x37   /* pop a,b; push (b || a) ? 1 : 0           */
#define OP_NOT      0x38   /* pop a; push (!a) ? 1 : 0                 */

/* 局部变量 */
#define OP_LOAD     0x40   /* +uint8 变量索引 → 压入 local[idx]         */
#define OP_STORE    0x41   /* pop; local[idx] = pop                     */

/* 终端 I/O（在 SeedVM 全屏画布上渲染文本） */
#define OP_PRINT_STR 0x50  /* pop str_ptr; 在光标处渲染 UTF-8 文本      */
#define OP_PRINT_NUM 0x51  /* pop int; 转十进制文本渲染                 */
#define OP_PRINT_NL  0x52  /* 光标换行                                  */
#define OP_PRINT_CH  0x53  /* pop int; 作为字符渲染                      */
#define OP_PRINT    0x54  /* pop val; 运行时检测: 堆指针→字符串, 小整数→数字 */

/* 类型转换 */
#define OP_TOSTR    0x60   /* pop int; itoa 后 kmalloc 字符串; 压指针    */
#define OP_TOINT    0x61   /* pop str_ptr; atoi; 压 int                */

/* 字符串 */
#define OP_CONCAT   0x70   /* pop a(str), pop b(str); 压入 kmalloc(b+a) */

/* GUI */
#define OP_G_RECT   0x80   /* pop color,h,w,y,x; 画填充矩形              */
#define OP_G_TEXT   0x81   /* pop color,str_ptr,y,x; 画文字              */
#define OP_G_CIRCLE 0x82   /* pop color,r,y,x; 画填充圆                  */
#define OP_G_CLEAR  0x83   /* 清屏黑色, 光标归零                         */
#define OP_G_FLUSH  0x84   /* 刷新帧缓冲到屏幕                          */
#define OP_G_WIDTH  0x85   /* 压入屏幕宽度                               */
#define OP_G_HEIGHT 0x86   /* 压入屏幕高度                               */
#define OP_G_PIXEL  0x87   /* pop color,y,x; 画点                        */
#define OP_G_LINE   0x88   /* pop color,y2,x2,y1,x1; 画线              */

/* 颜色常量 */
#define OP_PUSH_COLOR 0x90  /* +uint8 颜色索引(0-7)                       */

/* 交互 / 定时 */
#define OP_SLEEP    0x91   /* pop ms; 忙等 ms 毫秒                         */
#define OP_ASK      0xA0   /* +uint8 回答类型(0=文本,1=数字); pop 提示串; 打印并索取输入(内核无输入→存空值) */
#define OP_GETINPUT 0xA1   /* 压入最近一次 OP_ASK 的答案 */

/* 数学 / 字符串（M5+ 新增） */
#define OP_RAND     0xA2   /* pop max, pop min; push rand(min..max)        */
#define OP_ABS      0xA3   /* pop a; push abs(a)                            */
#define OP_SQRT     0xA4   /* pop a; push isqrt(a) (整数平方根)             */
#define OP_STRLEN   0xA5   /* pop str_ptr; push strlen(str)                 */
#define OP_UPPER    0xA6   /* pop str_ptr; push 大写后的新字符串             */
#define OP_LOWER    0xA7   /* pop str_ptr; push 小写后的新字符串             */

#define SEED_COLOR_COUNT 8
extern const uint32_t seed_colors[SEED_COLOR_COUNT];
/* 0=黑 1=白 2=红 3=绿 4=蓝 5=黄 6=青 7=紫 */

/* ===== VM 状态 ==========================================================*/

#define SEED_STACK_MAX  256
#define SEED_LOCALS_MAX 64
#define SEEDVM_IPS_PER_FRAME 2000   /* 每帧最多执行指令数 */
#define SEED_LINE_H 20              /* 文本行高 */
#define SEED_CHAR_W 8               /* ASCII 字宽 */
#define SEED_CN_CHAR_W 16           /* 中文字宽 */

typedef struct {
    /* 包数据 */
    uint8_t* code;
    uint32_t code_size;
    uint8_t* data;
    uint32_t data_size;

    /* 执行状态 */
    uint32_t ip;
    int32_t  stack[SEED_STACK_MAX];
    int      sp;               /* -1 = 空 */
    int32_t  locals[SEED_LOCALS_MAX];
    int      halted;
    int      error;

    /* 元数据 */
    char     name[SEED_NAME_LEN];
    char     author[SEED_AUTHOR_LEN];

    /* 文本光标（终端式输出） */
    int      cursor_x;
    int      cursor_y;

    /* 交互输入（OP_ASK / OP_GETINPUT） */
    int32_t  answer;        /* 最近一次 OP_ASK 得到的答案 */
    int      has_answer;    /* 是否已有答案 */
    int      ask_type;      /* 最近一次 OP_ASK 的类型 (0=文本,1=数字) */

    /* 键盘交互输入缓冲区 */
    int      waiting_input; /* 1=正在等待用户输入 (OP_ASK 阻塞) */
    char     input_buf[256];/* 用户输入缓冲区 */
    int      input_len;     /* 当前输入长度 */

    /* 终端窗口模式（非 GUI 程序使用，居中窗口带边框标题） */
    int      terminal_mode; /* 1=终端窗口模式, 0=全屏模式 */
    int      win_x, win_y;  /* 窗口左上角 */
    int      win_w, win_h;  /* 窗口宽高 */
    int      client_x, client_y; /* 客户区左上角（文本绘制起点） */
    int      client_w, client_h; /* 客户区宽高 */
    int      scroll_offset; /* 滚动偏移（行） */
} seed_vm_t;

/* ===== API ==============================================================*/

/* 加载 .seed 文件。成功返回指针，失败返回 NULL。 */
seed_vm_t* seedvm_load(const char* path);

/* 每帧执行一批指令。返回 1=已停机, 0=仍在运行。 */
int seedvm_frame(seed_vm_t* vm);

/* 是否已停机。 */
static inline int seedvm_is_done(seed_vm_t* vm) { return vm->halted; }

/* 清理 VM（释放 code/data/vm 本身）。 */
void seedvm_done(seed_vm_t* vm);

/* 从 .seed 文件头提取包名。成功返回 0，失败 -1。 */
int seedvm_get_info(const char* path, char* name, char* author, uint32_t* code_size, uint32_t* flags);

#endif /* SEEDVM_H */
