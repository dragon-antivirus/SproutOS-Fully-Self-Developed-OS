# SproutOS 浏览器 — 内核集成接线说明（Makefile / 应用注册 / 数据目录）

本文件给出把 `kernel/browser/` 接入现有 SproutOS 内核的最小改动清单。
所有改动都在**现有内核工程内**完成，浏览器自身代码无需改动（除非要重命名 OS 接口，见 OS_API_CONTRACT.md）。

---

## A. 编译进内核镜像（规范 §9.6）

在现有内核 `Makefile` 的 `OBJS` 列表中加入新增目标文件（**必须用 Tab 缩进**）：

```makefile
OBJS = \
    kernel/main.o \
    kernel/gui.o \
    kernel/vfs.o \
    ...
    kernel/browser/browser.o \      # ← 新增
    kernel/browser/browser_osdep_stub.o   # ← 仅在你本地做语法自检时需要；真实内核不要编入（它只是桩）
```

> ⚠️ `browser_osdep_stub.c` 是本交付里用于**脱离内核做主机语法自检**的桩，**不要编入真实内核镜像**（会与真实内核符号冲突）。
> 真实内核直接编 `browser.c` + `browser.h`（头文件已在 `kernel/browser/`）。

浏览器源文件依赖头 `browser.h`，请确认 `Makefile` 的 include 路径能找到它（例如 `-Ikernel/browser` 或源文件用 `#include "browser.h"` 相对包含）。

---

## B. 应用注册（规范 §1.1、§4、§5.5）

在现有 `app_names[]` 注册表里增加一项（窗口标题保持英文）：

```c
const char* app_names[] = {
    "FileManager",
    "Editor",
    "Paint",
    "RecycleBin",
    "Browser",      // ← 新增，标题英文；桌面图标中文「浏览器」
    ...
};
```

并在应用启动分发处，当启动「浏览器」时调用：

```c
case APP_BROWSER:
    browser_start();   // 来自 kernel/browser/browser.h
    break;
```

桌面图标中文标注「浏览器」、开始菜单常驻入口，按你现有 GUI 模板（参照 Paint / RecycleBin）接入即可。
文件管理器 `.html` 后缀绑定：在文件管理器「打开」逻辑里，若扩展名为 `.html` 则启动 `APP_BROWSER` 并把路径传给浏览器
（可在 `browser_start()` 之后用 `load_url(path)` 钩子，或扩展 `browser_start` 增加 `const char* initial_path` 参数——按需微调）。

---

## C. 数据目录与空文件预创建（规范 §9.7、§5.4、P1-7）

在 `make disk` 构建 FAT32 镜像的步骤里，自动预创建下列目录与空文件（Phase 1 仅预创建，持久化读写在 Phase 3）：

```
/browser/                      # 浏览器资源目录（放 home.html / test_page.html / search_config.txt）
/browser/cache/                # 缓存目录（Phase 3 使用）
/browser/bookmarks.txt         # 书签（空文件）
/browser/history.log           # 历史（空文件）
/browser/search_config.txt     # 搜索引擎配置（写入默认引擎编号，如 0）
/downloads/                    # 下载目录（Phase 3 使用）
```

示例（`make disk` 阶段用 `mcopy` / `mkfs.fat` 或你现有 FAT 工具）：

```makefile
# 伪代码：在生成 fat.img 后拷入资源并建空文件
$(DD) if=/dev/zero of=$(DISK_IMG) bs=1M count=32
$(MKFS_FAT) -F 32 $(DISK_IMG)
$(MCOPY) -i $(DISK_IMG) kernel/browser/home.html ::/browser/home.html
$(MCOPY) -i $(DISK_IMG) kernel/browser/test_page.html ::/browser/test_page.html
$(MCOPY) -i $(DISK_IMG) kernel/browser/search_config.txt ::/browser/search_config.txt
$(MMKDIR) -i $(DISK_IMG) ::/browser/cache
$(MMKDIR) -i $(DISK_IMG) ::/downloads
$(TOUCH_EMPTY) -i $(DISK_IMG) ::/browser/bookmarks.txt
$(TOUCH_EMPTY) -i $(DISK_IMG) ::/browser/history.log
```

> 浏览器启动时会 `load_url("/browser/home.html")` 并尝试读 `/browser/search_config.txt`；
> 若文件不存在/为空，会自动回退到默认引擎（百度，编号 0），不会崩溃。

---

## D. 编译验证命令（沿用原版，规范 §3）

```bash
make clean && make iso     # 产出内核镜像
make run                   # QEMU 启动（e1000 / rtl8139 沿用）
# 调试：make debug / 串口日志 / kprintf / serial_*
```

预期：桌面出现「浏览器」图标 → 双击打开 → 进入 `/browser/home.html` →
可点选 6 个引擎、点击链接跳转、滚动长页、地址栏输入本地路径回车加载。

---

## E. 已知功能缺口（Phase 1 诚实声明，非 bug）

1. **按行居中/右对齐（text-align: center/right）**：已解析并存储到样式，但当前渲染器为即时模式，
   块级文本仅实现左对齐 + `margin` 缩进；**逐行居中/右对齐未实现**（需要按行缓冲二次测量，建议 Phase 1.x 补）。
2. **斜体 / 下划线视觉**：`<i>/<em>` 仅保留原样、`<u>` 未绘制下划线（依赖字体字形，当前框架无斜体/下划线字形）。
3. **网络搜索 / http 跳转 / PNG / 下载 / 缓存 / 书签历史持久化**：按规范属 Phase 2/3，P1 仅做 UI 与 `url_encode` 预生成，
   点击网络链接会提示「需联网后访问」。
4. **横向滚动交互**：滚动条已绘制且可点轨道跳转；更顺手的拖拽/滚轮需在输入层增加滚轮事件（当前契约无滚轮键）。

这些缺口不影响 Phase 1 验收项（本地渲染、链接、滚动、引擎 UI、数据文件预创建、回归不崩）。
