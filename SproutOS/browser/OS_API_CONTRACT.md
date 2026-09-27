# SproutOS 浏览器 — OS 接口契约（OS API Contract）

本文件集中说明 `browser.c` 依赖的**内核必须提供**的符号，以及它们的语义。
所有外部依赖只出现在 `browser.h` 的「OS DEPENDENCY CONTRACT」段，接入时**只需改这一处**把声明映射到你真实内核的 API。

> 约定：本模块为 freestanding C，**不调用任何 libc**。像素统一用 0xRRGGBB（注释中说明），
> BMP 走 RGBA8888。帧缓冲假定 1024×768、32bpp、双缓冲（与规范 §2 一致）。

---

## 1. 帧缓冲 / 绘制

| 符号 | 语义 |
|------|------|
| `uint32_t* fb_get_buffer(void)` | 返回后台缓冲基址（32bpp）。浏览器默认通过下面的绘制接口输出，不直接写缓冲，故可选。 |
| `int fb_width(void)` | 帧缓冲宽（规范 1024）。 |
| `int fb_height(void)` | 帧缓冲高（规范 768）。 |
| `void fb_present(void)` | 将后台缓冲刷到屏幕（双缓冲交换/拷贝）。主循环每帧调用一次。 |
| `void fb_fill_rect(int x,int y,int w,int h,uint32_t color)` | 填充实心矩形，color=0xRRGGBB。 |
| `void fb_draw_rect(int x,int y,int w,int h,uint32_t color)` | 仅画边框矩形。 |
| `void fb_draw_ascii(int x,int y,char c,uint32_t color)` | 在 (x,y) 画一个 ASCII 字符（左上角对齐）。 |
| `void fb_draw_cn(int x,int y,const char* utf8,uint32_t color)` | 在 (x,y) 画一个 CJK（或任意 UTF-8 单字）字形，utf8 指向该字的多字节序列（如"浏"=0xE6xB6x8F）。需遵循 `fb_cn_string` 字体索引规范（规范 §9.2）。 |
| `int fb_ascii_w(void)` | ASCII 字形前进宽度（px）。 |
| `int fb_cn_w(void)` | CJK 字形前进宽度（px）。 |
| `int fb_line_h(void)` | 默认行高（px），用作 `line-height` 未设时的回退值。 |
| `void fb_draw_rgba(int x,int y,int w,int h,const uint8_t* rgba)` | 绘制原始 RGBA8888 像素块（用于 BMP）。rgba 长度 = w*h*4，行优先、左上角起。 |

> 若你的 GUI 接口的矩形/字符 API 名不同，只需在 `browser.h` 把这些 `extern` 改名或加 `#define` 别名，
> 例如 `#define fb_fill_rect gui_fill_rect`。

---

## 2. 输入（非阻塞轮询）

| 符号 | 语义 |
|------|------|
| `int mouse_x(void)` | 当前鼠标 X（屏幕坐标）。 |
| `int mouse_y(void)` | 当前鼠标 Y。 |
| `int mouse_down(void)` | 左键按住返回 1，否则 0。 |
| `int kbd_poll(int* out_key)` | **非阻塞**取出一个按键事件：有则返回 1 并把键值写入 `*out_key`；无返回 0。`*out_key` 为可打印 ASCII（32..126）或下列宏：<br>`KEY_BACK(8)` `KEY_TAB(9)` `KEY_ENTER(13)` `KEY_ESC(27)` `KEY_LEFT(0x100)` `KEY_RIGHT(0x101)` `KEY_UP(0x102)` `KEY_DOWN(0x103)`。主循环会用 `while(kbd_poll(&k))` 排空队列。 |

> 如果你的内核是**事件派发**模型（窗口管理器回调），可改为直接调用 `browser_on_mouse(x,y,down)` / `browser_on_key(key)`，
> 而不要调用 `browser_start()` 的自循环。两个钩子已在 `browser.h` 导出。

---

## 3. VFS

| 符号 | 语义 |
|------|------|
| `int vfs_open(const char* path,int flags)` | 打开文件，成功返回 fd（≥0），失败 -1。`flags` 用 `VFS_RD(0)` 只读。 |
| `int vfs_read(int fd,void* buf,int len)` | 读取最多 len 字节，返回实际读取数（0 表示 EOF）。 |
| `void vfs_close(int fd)` | 关闭 fd。 |

浏览器用它们实现 `read_file()`（带增长缓冲、4MB 上限保护），读取本地 `.html` 与 `.bmp`。

---

## 4. 内核内存

| 符号 | 语义 |
|------|------|
| `void* kmalloc(size_t n)` | 内核堆分配，返回 0 填充？不保证清零，需自行初始化（本模块已用 `m_memset` 清零节点）。 |
| `void kfree(void* p)` | 释放。 |
| `size_t kmalloc_used(void)` | 当前已分配字节数，用于内存泄漏监控（规范 §6.4），主循环注释示例已用。 |

> 若 `kmalloc` 带 `__attribute__((warn_unused_result))`，本模块对所有返回值均已判空，不会触发警告。

---

## 5. 可选

| 符号 | 语义 |
|------|------|
| `void kprintf(const char* fmt, ...)` | 串口/日志输出，可选。不提供可给空实现。 |

---

## 6. 浏览器对外公开符号

| 符号 | 说明 |
|------|------|
| `void browser_start(void)` | 内核在用户启动「浏览器」应用时调用。进入自循环事件处理，窗口关闭后返回。 |
| `void browser_on_mouse(int x,int y,int down)` | 事件派发模型下的鼠标钩子（down:1 按下 / 0 抬起）。 |
| `void browser_on_key(int key)` | 事件派发模型下的键盘钩子。 |

> 窗口标题请保持英文 `Browser`（桌面图标中文标注「浏览器」，规范 §9.2）。
