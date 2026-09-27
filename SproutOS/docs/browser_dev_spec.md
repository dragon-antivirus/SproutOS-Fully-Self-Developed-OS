# SproutOS 浏览器 开发文档（Browser Development Spec）

> 读者：受委托为 SproutOS 实现网页浏览器的开发者。
> 本文档描述目标、约束、可复用接口、功能/搜索引擎需求与分阶段交付计划。所有 API 名称与约束均以来源代码（kernel.h / ui/gui.c / fs/vfs.h / drivers/pci）为准，请勿凭空假设。

---

## 1. 项目背景与目标（Goals）

**SproutOS** 是一个自研的 x86 32-bit 单体内核 + 帧缓冲 GUI 操作系统（不是 Linux，没有 POSIX、没有 libc、没有现成的网络栈）。当前已有：桌面（图标+任务栏+开始菜单）、窗口系统、文件管理器（FAT32 VFS）、文本编辑器、终端、画图、回收站占位窗口、英文点阵字模 + 中文 TTF 运行时渲染。

**本任务目标**：在 SproutOS 上实现一个**轻量级网页浏览器**应用，做到：

1. 能浏览**本地 HTML 文件**（从 FAT32 磁盘加载，Phase 1 即可验收，无需联网）。
2. （可选/Phase 2）能浏览**远程网页**（需自行实现网卡驱动 + TCP/IP + HTTP）。
3. 集成**搜索引擎**：主页有搜索框，可切换多个引擎，输入关键词跳转搜索结果页。
4. 与现有 GUI 框架**无缝集成**：桌面图标、开始菜单项、文件管理器里 `.html` 默认用浏览器打开。

**明确不在范围内（避免过度设计）**：JavaScript 执行引擎、完整 CSS 规范、音视频、插件、多进程沙箱、HTTPS/TLS（可作为 Phase 4 选项，极难）。

---

## 2. 运行环境与硬约束（Constraints）

- 架构：x86，Multiboot1 引导，32-bit，已开启分页（VMM 已初始化）。
- 运行模型：单内核（kernel mode），C（freestanding）+ 少量 NASM 汇编。
- 显示：帧缓冲 **1024×768×32bpp，双缓冲**；像素编码 `rgb(r,g,b) = (r<<16)|(g<<8)|b`（即 `0x00RRGGBB`）。
- 内存：已提供内核堆 `kmalloc/kfree`（已 init），大块可用 `pmm_alloc_page`；**没有** 用户态堆概念，一切都在内核地址空间。
- 字符串/内存运行时（见 `kernel.h`，**禁止依赖任何 libc**）：
  - 已有：`memset`、`memcpy`、`strlen`、`strcmp`、`strcpy`、`strcat`、`itoa`、`kprintf`。
  - **没有** `strncpy` / `sprintf` / `snprintf` / `strlen` 的安全版本——需要自己实现（参考 `ui/gui.c` 里现成的 `fname_copy(dst,src,n)` 有界拷贝 helper）。
- 不引入 C++（无 STL / 异常 / RTTI 支持）。

---

## 3. 开发语言与工具链（Language & Toolchain）

- **语言**：C（仅使用 `kernel.h` 暴露的运行时）；汇编用 NASM，**仅在**网卡 MMIO/端口访问、IDT 等不得已处使用。
- **工具链**：`i386-elf-gcc` + GNU Make（与现有 `Makefile` 完全一致）。**本机（Windows）没有 i386 工具链，统一在 Ubuntu VM 编译验证。**
- **构建 / 运行命令**（已在 Makefile 实现，直接复用）：
  - `make clean` —— 清产物
  - `make iso` —— 生成可启动 ISO（GRUB + 内核）
  - `make run` —— 起 QEMU 跑 ISO（会自动 `ensure-disk` 构建 disk.img）
  - `make disk` —— 单独构建 FAT32 磁盘镜像（含 `/wallpaper/background.bmp`）
  - `make dist` —— `iso` + `disk`
  - `make debug` —— 带 GDB 的 QEMU（如需断点调试）
  - 联网测试需给 QEMU 加网卡，例如：`-netdev user,id=n0 -device e1000,netdev=n0`（或 `rtl8139`）。
- **调试手段**：
  - 所有日志走 `serial_write(const char* s)`（QEMU 串口，启动可见）；配套 `serial_puth` / `serial_puti`。
  - 可用 `kprintf(fmt, ...)` 做带格式的日志。
  - 新增自检可放进 `kernel/selftest.c` 的 `[SELFTEST]` 流程。

---

## 4. 必须复用的现有接口（Reuse — 不要另起炉灶）

### 4.1 窗口与 GUI（`ui/gui.c`，声明见 `kernel.h`）

| 接口 | 说明 |
|---|---|
| `int gui_new_window(const char* title, uint32_t color, int w, int h)` | 开窗口。**标题用英文**（如 `"Browser"`）；窗口内中文用 `fb_cn_string` 渲染 |
| `void gui_handle_key(char c)` | 键盘输入统一入口——地址栏输入接这里 |
| `void gui_draw_window(window_t* w)` | 画窗口边框/标题栏 |
| `gui_draw_one(idx)` | 按 `w->title` 字符串分发到各 app 的 `draw_xxx`；**新增 `else if (!strcmp(nm,"Browser")) draw_browser(idx);`** |
| `int gui_get_focus(void)` / `in_title(idx)` | 聚焦判断 / 标题栏命中 |
| `launcher_open(int app_index)` / `gui_app_names()` | 从桌面图标/开始菜单启动 app |

**集成步骤（严格仿照现有 Paint / Recycle）**：
1. 在 `ui/gui.c` 的 `app_names[]` 末尾加 `"Browser"`（成为新索引）。
2. `kernel/kernel.c` 的 `launcher_open`：`cols[]` 加一项颜色，`app_index==N` 给窗口尺寸（如 720×560）。
3. `gui_draw_one` 加 Browser 分发；实现 `static void draw_browser(int idx)`。
4. 桌面图标方块 + 中文标签（"浏览"或"浏览器"，字模索引见 §4.2）+ 开始菜单项，均随 `app_names[]` 自动生成或参考现有接法补齐。
5. 文件管理器 `default_app_for(name)` 加 `.html` → Browser 分支（参考现有 `.txt`→Editor、`.bmp`→Paint）。

### 4.2 绘制（`kernel.h` 声明）

- 像素/矩形/文本：`fb_pixel`、`fb_rect`、`fb_string`、`fb_string_scale`、`fb_char`、`fb_clear`、`fb_flush`、`fb_circle`、`fb_ring`、`fb_line`、`fb_polyline_partial`。
- 颜色：`uint32_t rgb(uint8_t r, uint8_t g, uint8_t b)`。
- **中文**：`void fb_cn_string(int x, int y, const char* utf8, uint32_t fg, uint32_t bg)`，传入**直接的中文 UTF-8 字符串**（如 `"浏览器"`），由 `fb_cn_char(x,y,cp,...)` 按 Unicode 码点渲染。渲染链路：`ttf_glyph(cp)`（运行时加载的 `YSHI.TTF` 用 stb_truetype 栅格化）→ 失败画 16×16 空框兜底（旧的点阵字库 `cn_font_16x16` 已删除，不再回退）。ASCII 子串（如 `"重命名:"` 的冒号）走 8×8 字模，不依赖 TTF。无需再手工维护字形索引。

### 4.3 文件系统（`fs/vfs.h`）

- `int vfs_open(const char* name, vfs_dirent_t* out)`
- `int vfs_read(const vfs_dirent_t* e, uint8_t* buf, uint32_t len, uint32_t* out_len)`
- `int vfs_write_file(const char* name, const uint8_t* data, uint32_t len)`
- `int vfs_chdir(const char* name)` / `int vfs_chdir_up(void)` / `int vfs_list(vfs_dir_t* out)`
- `int vfs_unlink(const char* name)` / `int vfs_copy_file(const char* src, const char* dst)`
- `vfs_dirent_t`（含 `name`、`size`、`first_cluster`、`is_dir` 等）；`vfs_dir_t` 最多 48 条。
- 用途：加载本地 `.html`、缓存、书签（可写 `/browser/bookmarks.txt` 之类）。

### 4.4 硬件 / 驱动框架（仅做联网时需要）

- **PCI 枚举已就绪**：`pci_init()`、`pci_devices[]`、`pci_config_read32/16/8(bus,dev,fn,off)`。
- **驱动注册接口**：`void driver_register(const char* name, uint16_t vendor, uint16_t device, int (*probe)(uint8_t bus, uint8_t dev, uint8_t fn))`——**直接注册一个网卡驱动**，按 vendor/device 匹配后 `probe` 拿到 bus/dev/fn。
  - QEMU `-device e1000`：vendor `0x8086`，device `0x100E`（或 `0x10D3`）。
  - QEMU `-device rtl8139`：vendor `0x10EC`，device `0x8139`。
- 端口 I/O：`outl(uint16_t port, uint32_t val)` / `inl(uint16_t port)`。
- 内存映射：`vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags)`（网卡 MMIO 空间需要映射进内核页表）。
- **中断**：需向内核维护者确认 IDT/PIC 框架是否就绪（是否有 `irq_register` 之类）。**稳妥起见，网卡收包先用轮询（polling）模式**，验证通过后再接中断。

### 4.5 调试（`kernel.h`）

- `serial_write(const char* s)`、`serial_puth(uint32_t)`、`serial_puti(int)`、`kprintf(const char* fmt, ...)`、`itoa(int, char* buf)`。

---

## 5. 功能需求（Requirements）

### 5.1 基本浏览
- **地址栏**：可输入 URL（`http://...` 或本地路径如 `/www/index.html`），回车加载。
- **导航按钮**：至少实现 后退 / 前进 / 刷新 / 主页。
- **内容渲染区**：显示网页文本与基本排版。
- **超链接**：点击 `<a>` 文本区域跳转。
- **状态栏**（窗口底部）：显示当前 URL / 加载中 / 出错信息（中文）。

### 5.2 本地文件浏览（Phase 1，无需网络）
- 文件管理器里 `.html` 默认用浏览器打开（§4.1 第 5 步的 `default_app_for` 分支）。
- 从 FAT32 读取并渲染本地 HTML。

### 5.3 渲染引擎（轻量，手写）
- **HTML 解析**：手写 tokenizer（状态机），**不要**引入大型第三方解析库（无 libc 适配成本高）。
- **支持的标签子集**：`a`、`p`、`h1`–`h3`、`br`、`hr`、`ul`/`ol`/`li`、`img`、`title`、`meta`、`b`/`i`（可选）、`input`、`form`（form 先只支持 GET）。
- **CSS 支持**：仅 inline style + 极少量属性（`color`、`font-size`、`text-align`）；**不实现**完整盒模型 / 浮动 / 绝对定位。
- **布局**：流式 + 简化行排版；中文走 16×16 字模、ASCII 走 8×8 字模；内容超出窗口需支持**上下滚动**（参考现有 `files_scroll` 模型或自写滚动条）。
- **图片**：支持 **BMP**（内核已有 BMP 编解码，`bmp_encode` / `gui_set_wallpaper_from_file` 可借鉴）；PNG 为可选项（需移植 tiny PNG decoder 如 `pngle`，注意 freestanding 适配）。

### 5.4 搜索引擎（详见第 6 节）。

### 5.5 可选项（明确可推迟）
历史记录、书签、多标签页、下载管理、HTTPS/TLS、JavaScript。

---

## 6. 搜索引擎要求（Search Engine）

- **默认主页**：居中搜索框 + 引擎切换下拉/按钮；回车后构造搜索 URL 并发起 GET。
- **至少内置 3 个可切换引擎**（中文用户优先百度/Bing）：

  | 引擎 | 搜索 URL 前缀 |
  |---|---|
  | 百度 | `https://www.baidu.com/s?wd=` |
  | Bing | `https://www.bing.com/search?q=` |
  | DuckDuckGo | `https://duckduckgo.com/?q=` |

- **URL 编码（必须自实现）**：查询串必须做 percent-encoding——按 **UTF-8** 字节拆成 `%XX`（中文是三字节）。内核无现成函数，需自己写 `url_encode`。**注意是 UTF-8，不是 GBK。**
- **默认引擎可配置**：编译期宏（如 `DEFAULT_SEARCH_ENGINE`）或配置文件 `/browser/config.txt`，方便切换。
- **隐私**：默认不在 URL 里塞多余 tracking / 客户端标识参数。
- **失败处理**：DNS/HTTP 失败时状态栏显示中文错误（如"无法连接"），**绝不能崩溃或 panic**。

---

## 7. 网络方案（若实现远程浏览）

- **推荐 QEMU user-mode networking**：`-netdev user,id=n0 -device e1000,netdev=n0`。Guest 内 DHCP 拿 `10.0.2.15`，网关 `10.0.2.2`，DNS `10.0.2.3`，可直连外网。
- **协议栈自实现**（项目暂无 lwIP；如需移植须先征得维护者同意），建议分阶段：
  - A. 裸以太网帧收发（网卡驱动 + 收发环形缓冲区）
  - B. ARP + IPv4（校验和软件计算）+ ICMP（可选 ping）
  - C. UDP + DNS（A 记录查询，域名→IP）
  - D. TCP（三次握手、序列号、滑动窗口/重传、状态机；先做 client）
  - E. HTTP/1.1 GET（处理 200 / 301 / 302 重定向、Content-Length、分块传输编码 `Transfer-Encoding: chunked`、`Connection: close`）
  - F.（可选）HTTPS（TLS 1.2+，极难，建议移植 mbedTLS 或明确后置）
- **收包先用轮询**，确认 IDT 后再改中断，降低风险。
- 所有网络缓冲用 `kmalloc`，务必配对 `kfree`，可用 `kmalloc_used()` 观察泄漏。

---

## 8. 分阶段交付（Phased Plan）

- **Phase 1（必做，验收核心）**：本地 HTML 浏览器。集成进 GUI（桌面图标 / 开始菜单 / `.html` 文件关联），渲染引擎（标签子集 + 简化排版），地址栏可输入本地路径，链接可点，内容可滚动。**无需网络，可独立验收。**
- **Phase 2**：联网。网卡驱动（e1000 / rtl8139）+ 协议栈（ARP/IP/UDP/DNS/TCP/HTTP），能打开真实 `http://` 站点。
- **Phase 3**：搜索引擎集成 + 图片显示 + 历史 / 书签。
- **Phase 4（可选 / 范围外）**：HTTPS/TLS、JavaScript、CSS 增强、多标签。

---

## 9. 编译与验收（Build & Acceptance）

- 必须能在 Ubuntu VM 干净跑通：`make clean && make iso && make run`（联网用例另加 QEMU 网卡参数）。
- 新增源文件**必须加入 Makefile 的 `OBJS`**（用真 tab，不是空格）；新增网卡驱动放 `drivers/` 下并 `driver_register`。
- **验收用例**：
  1. Phase 1：双击桌面"浏览器"图标开窗口；地址栏输入 `/test.html`（提前放进 disk.img）回车渲染；点链接跳转；滚动内容。
  2. Phase 2：地址栏输入 `http://example.com` 能显示页面（需 QEMU 联网参数）。
  3. 搜索引擎：主页输入"操作系统"→ 跳转百度/Bing 结果页。
  4. 回归：文件管理 / 画图 / 回收站 / 右键菜单不被破坏。
- 串口日志无内核 panic；`kmalloc` 无明显泄漏。

---

## 10. 坑位与约定（Gotchas）

1. **freestanding，禁止 libc**：`strncpy`/`sprintf` 等一律自实现（参考 `fname_copy`）。
2. **中文渲染**：运行时 TTF（`fonts/SproutOS-YShiNewHeiGGJRegular.ttf` + `stb_truetype`）栅格化，失败由 GUI 画 16×16 空框兜底（旧的点阵字库 `cn_font_16x16` 已删除）。GUI/浏览器一律写真实 UTF-8 中文，不再手编码模索引字节。
3. **窗口标题用英文，窗口内中文用 `fb_cn_string`**（与 Files/Editor/Paint/Recycle 一致）。
4. **不要引入大依赖**；保持单内核可启动、可编译。
5. **Makefile 配方用真 tab**；新增文件进 `OBJS`。
6. 网络部分**先轮询后中断**；**先本地后联网**，确保 Phase 1 可独立验收。
7. 与 GUI 集成**严格仿照现有 app**（Paint/Recycle）：`app_names[]` + `launcher_open` 尺寸 + `gui_draw_one` 分发 + `draw_xxx` 函数 + 桌面/开始菜单图标。
8. 任何新增全局状态/窗口字段，注意与现有 `window_t`（x,y,w,h,maximized,ox,oy,ow,oh,dragging,...）字段命名一致，避免冲突。

---

*文档依据 SproutOS 当前代码状态（kernel.h、ui/gui.c、fs/vfs.h、drivers/pci）编写。实现前请先 `make run` 跑通现有系统，读懂 Paint / Recycle 两个 app 的接入方式作为模板。*
