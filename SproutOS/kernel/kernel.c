#include "kernel.h"
#include "truetype.h"   /* ttf_init：运行时加载中文 TTF（VFS 就绪后调用） */
#include "boot_logo_data.h"

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t reserved_fb;
} __attribute__((packed)) multiboot_info;

void launcher_open(int app_index);

/* 浏览器全屏独占标志（在 launcher_open(6) 置 1，主循环检测到后改走浏览器分支）。 */
int g_browser_open = 0;

/* 启动模式：0 = BIOS (Legacy), 1 = UEFI。
 * 在 kernel_main 中从 multiboot cmdline 解析 platform=${grub_platform} 得到。
 * GRUB 的 grub_platform 变量在 BIOS 下为 "pc"、在 UEFI 下为 "efi"。 */
int g_boot_mode = 0;
#define BOOT_BIOS 0
#define BOOT_UEFI 1

/* PCI fallback 路径下被选中的 framebuffer BAR 的实际字节数（探测大小）。
 * 用来在 got_fb 分支反算 fb_h（避免硬编码 768 与硬件真实 stride 不匹配）。 */
uint32_t g_last_fb_bar_size = 0;

/* SeedVM 全屏独占标志（在终端 `terra run` 置 1，主循环检测到后改走 seed 分支）。 */
seed_vm_t* g_seedvm = NULL;
int g_seedvm_active = 0;

/* =========================== UEFI GOP Fallback ===========================
 * 仅在 UEFI 路径下被调用（g_is_uefi=1）。当 GRUB 没把 framebuffer 填进
 * multiboot（bit12=0，多为 GOP 模式匹配失败导致），靠 PCI scan 找 VGA 设备
 * 读 BAR0 取 framebuffer 物理地址，硬编码 1024x768x32 作为显示尺寸。
 *
 * 不在 BIOS 下调用：BIOS 路径走 GRUB VBE 提供的 fb，没有 GOP fallback，
 * 错误时直接 halt，给出 grub.cfg 修改建议。
 *
 * 注意：PCI BAR size 是 GPU VRAM 分配（OVMF/virtio-vga 通常 8MB），远大于
 * 实际显示区，**绝对不能用** BAR_size / pitch 反算 height。
 * ========================================================================= */
static void fb_init_gop_fallback(const multiboot_info* mb,
                                 uint32_t* out_addr, uint32_t* out_pitch,
                                 uint32_t* out_w, uint32_t* out_h, uint32_t* out_bpp) {
    *out_addr = *out_pitch = *out_w = *out_h = *out_bpp = 0;
    serial_write("[M0/FB] GOP fallback: scanning PCI for VGA BARs (pick largest)...\n");
    static const uint8_t bar_offs[6] = {0x10, 0x14, 0x18, 0x1C, 0x20, 0x24};
    /* 挑所有 VGA 类(class 0x03)设备的所有非空、非 I/O BAR 中**最大的**那个。
     * LFB 永远最大，register mmio BAR 只有几 KB，ROM 也很小，
     * 用"取最大"代替固定的 4MB 阈值，避免 QEMU stdvga 3MB LFB 被误判。
     * 之前的硬过滤 4MB 加上"找到就停"导致 BIOS 路径上 stdvga 被跳过、
     * fb_addr 仍是 0，但 vga_init 仍被调到（显示花屏）。 */
    uint32_t best_addr = 0, best_size = 0;
    int best_b = -1, best_d = -1, best_i = -1;
    uint16_t best_ven = 0, best_dev = 0;
    for (int b = 0; b < 8; b++) {
        for (int d = 0; d < 32; d++) {
            uint32_t v = pci_config_read32(b, d, 0, 0);
            if ((v & 0xFFFF) == 0xFFFF) continue;  /* 不存在 */
            uint32_t c = pci_config_read32(b, d, 0, 8);
            uint8_t cls = (c >> 24) & 0xFF;
            if (cls != 0x03) continue;  /* 只要 VGA 类 */
            uint16_t ven = (uint16_t)(v & 0xFFFF);
            uint16_t dev = (uint16_t)((v >> 16) & 0xFFFF);
            serial_write("[M0/FB]   VGA ");
            serial_puti(b); serial_write(":");
            serial_puti(d); serial_write(".0 ven=0x");
            serial_puth(ven); serial_write(" dev=0x");
            serial_puth(dev); serial_write("\n");
            /* 启用 Memory Space (bit1) + I/O Space (bit0)：内存空间才能读 BAR，
             * I/O 空间才能让 VGA 响应 VBE DISPI 的 legacy I/O 端口 (0x1CE/0x1CF) */
            uint16_t cmd = pci_config_read16(b, d, 0, 4);
            if ((cmd & 0x3) != 0x3) pci_config_write16(b, d, 0, 4, cmd | 0x3);
            for (int i = 0; i < 6; i++) {
                uint32_t bar_orig = pci_config_read32(b, d, 0, bar_offs[i]);
                if (bar_orig == 0 || (bar_orig & 0x1)) continue;  /* 空 / I/O */
                uint32_t fb = bar_orig & 0xFFFFFFF0u;
                if (fb < 0x100000u) continue;  /* 1MB 以下忽略 */
                /* 探测大小：写全 1 → 读回 → 取反 + 1（标准 PCI BAR 探测法） */
                pci_config_write32(b, d, 0, bar_offs[i], 0xFFFFFFFFu);
                uint32_t size_probe = pci_config_read32(b, d, 0, bar_offs[i]);
                pci_config_write32(b, d, 0, bar_offs[i], bar_orig);  /* 还原 */
                uint32_t bar_size = (~(size_probe & 0xFFFFFFF0u)) + 1u;
                serial_write("[M0/FB]     BAR"); serial_puti(i);
                serial_write(" addr=0x"); serial_puth(fb);
                serial_write(" size=0x"); serial_puth(bar_size);
                if (bar_size > best_size) {
                    if (best_size) {
                        serial_write(" (new best, prev 0x");
                        serial_puth(best_size); serial_write(")");
                    } else {
                        serial_write(" (best)");
                    }
                    best_size = bar_size;
                    best_addr = fb;
                    best_b = b; best_d = d; best_i = i;
                    best_ven = ven; best_dev = dev;
                }
                serial_write("\n");
            }
        }
    }
    if (best_addr == 0) {
        serial_write("[M0/FB]   no VGA class 0x03 device with valid BAR found.\n");
        return;
    }
    *out_addr = best_addr;
    g_last_fb_bar_size = best_size;
    serial_write("[M0/FB] selected: ");
    serial_puti(best_b); serial_write(":");
    serial_puti(best_d); serial_write(".0 BAR");
    serial_puti(best_i);
    serial_write(" addr=0x"); serial_puth(best_addr);
    serial_write(" size=0x"); serial_puth(best_size);
    serial_write("\n");
    /* 默认分辨率：优先从 cmdline gfx=WxHxB（GRUB ${gfxmode} 透传）取真实尺寸；
     * 拿不到退回 1024x768x32。注意 BAR size 远大于实际显示区（VRAM 分配），
     * **不能**用来反算 height。 */
    uint32_t w = 1024, h = 768, bpp = 32;
    if (mb->flags & (1u << 2) && mb->cmdline) {
        const char* s = (const char*)mb->cmdline;
        for (; *s; s++) {
            if (s[0]=='g' && s[1]=='f' && s[2]=='x' && s[3]=='=') {
                const char* v = s + 4;
                unsigned long gw=0, gh=0, gb=0;
                while (*v>='0' && *v<='9') { gw = gw*10 + (*v-'0'); v++; }
                if (*v=='x') { v++; while (*v>='0' && *v<='9') { gh = gh*10 + (*v-'0'); v++; } }
                if (*v=='x') { v++; while (*v>='0' && *v<='9') { gb = gb*10 + (*v-'0'); v++; } }
                if (gw>=640 && gw<=4096 && gh>=480 && gh<=4096) {
                    w = (uint32_t)gw; h = (uint32_t)gh; bpp = (uint32_t)(gb==0?32:gb);
                }
                break;
            }
        }
    }
    *out_w = w; *out_h = h; *out_bpp = bpp;
    *out_pitch = w * (bpp / 8);
    serial_write("[M0/FB] fb size: w="); serial_puti(w);
    serial_write(" h="); serial_puti(h);
    serial_write(" bpp="); serial_puti(bpp);
    serial_write(" (BAR size only gives addr, not dimensions)\n");

    /* BIOS 路径：光拿到显存地址还不够，VGA 仍停在文本模式，必须显式切到图形
     * 模式，否则写 LFB 只是刷字符 RAM → 花屏。这里用 Bochs VBE DISPI 接口
     * （0x1CE/0x1CF）从保护模式直接下命令，无需实模式 BIOS。
     * 仅对 QEMU/Bochs 标准 VGA（ven=0x1234）调用；UEFI 走 GOP 已配好显示、
     * 且 virtio-vga 不实现 DISPI，故 g_boot_mode==BIOS 才进此分支。 */
    if (g_boot_mode == BOOT_BIOS && best_ven == 0x1234) {
        serial_write("[M0/FB] BIOS: GRUB left VGA in text mode; programming ");
        serial_puti(w); serial_write("x"); serial_puti(h);
        serial_write("x"); serial_puti(bpp);
        serial_write(" via Bochs DISPI (QEMU std VGA)...\n");
        int r = vga_vbe_set_mode((uint16_t)w, (uint16_t)h, (uint8_t)bpp);
        serial_write("[M0/FB] vga_vbe_set_mode -> ");
        serial_puti(r);
        serial_write(r ? " (UNSUPPORTED — hope GRUB set it)\n"
                        : " OK (LFB graphics mode active)\n");
    }
}

void kernel_main(uint32_t magic, uint32_t mboot) {
    if (magic != 0x2BADB002) {
        serial_init_port(0x3F8);
        serial_write("Bad multiboot magic!\n");
        for (;;) { __asm__ volatile("cli; hlt"); }
    }

    serial_init_port(0x3F8);
    serial_write("SproutOS booting...\n");
    serial_write("[M0] edition: " OS_EDITION " (" OS_RELEASE ")\n");

    gdt_install();
    pic_remap();
    idt_install();
    timer_install();

    serial_write("[M0] pmm_init...\n");
    pmm_init(mboot);
    serial_write("[M0] vmm_init...\n");
    vmm_init();

    multiboot_info* mb = (multiboot_info*)mboot;

    /* 从 multiboot cmdline 解析启动方式（GRUB 传 platform=${grub_platform}）。
     * grub_platform 在 BIOS 下为 "pc"、UEFI 下为 "efi"。cmdline 形如 "platform=efi"。
     * flags bit 2 = cmdline 有效。mb->cmdline 是物理地址，低址恒等映射可直接转指针。 */
    if (mb->flags & (1u << 2) && mb->cmdline) {
        const char* cl = (const char*)mb->cmdline;
        while (*cl) {
            const char* p = cl, * key = "platform=efi";
            while (*p && *key && *p == *key) { p++; key++; }
            if (*key == 0) { g_boot_mode = 1; break; }
            cl++;
        }
    }
    serial_write("[BOOT] mode: ");
    serial_write(g_boot_mode ? "UEFI\n" : "BIOS\n");

    /* 注意：framebuffer 物理地址可能在高位 MMIO（>512MB）或低位，
     * 统一推迟到下方 fb_addr 已知后再调 vmm_map_region。 */

    serial_write("[M0] kmalloc_init...\n");
    kmalloc_init();

    serial_write("[M0] PMM free pages: ");
    serial_puti(pmm_free_pages());
    serial_write(" / ");
    serial_puti(pmm_total_pages());
    serial_write("\n");
    serial_write("[M0] -> vga_init\n");

    serial_write("[M0] fb info: bit12="); serial_puti((mb->flags >> 12) & 1u);
    serial_write(" addr=0x"); serial_puth((uint32_t)mb->framebuffer_addr);
    serial_write(" pitch="); serial_puti(mb->framebuffer_pitch);
    serial_write(" w="); serial_puti(mb->framebuffer_width);
    serial_write(" h="); serial_puti(mb->framebuffer_height);
    serial_write(" bpp="); serial_puti(mb->framebuffer_bpp);
    serial_write(" type="); serial_puti(mb->framebuffer_type);
    serial_write("\n");

    /* 帧分辨率与位深路径（**BIOS 与 UEFI 严格分离**，互不影响）：
     *
     *  ===================================================
     *   UEFI 路径（g_is_uefi=1）
     *  ===================================================
     *   - 优先：GRUB multiboot header 已填 framebuffer（bit12=1）且 bpp=32 → 直接用
     *   - 兜底：GRUB 没填（OVMF GOP 模式匹配失败）→ fb_init_gop_fallback()
     *           自己 PCI 扫 BAR + 硬编码 1024x768x32
     *
     *  ===================================================
     *   BIOS 路径（g_is_uefi=0，g_boot_mode==BOOT_BIOS）
     *  ===================================================
     *   - 只能用 GRUB VBE 已填的 framebuffer；GRUB 文本模式控制台
     *     (w=80 h=25 bpp=16) 视为 GRUB 切图形模式失败
     *   - 失败时打印明确诊断（含建议改 grub.cfg 的方式），halt
     */
    int g_is_uefi = (g_boot_mode == BOOT_UEFI);
    uint32_t fb_addr = 0, fb_pitch = 0, fb_w = 0, fb_h = 0, fb_bpp = 0;
    if (g_is_uefi) {
        /* ---- UEFI 路径 ---- */
        if ((mb->flags & (1u << 12)) && mb->framebuffer_addr &&
            mb->framebuffer_addr <= 0xFFFFFFFFULL &&
            mb->framebuffer_bpp == 32 && mb->framebuffer_type != 2) {
            fb_addr = (uint32_t)mb->framebuffer_addr;
            fb_pitch = mb->framebuffer_pitch;
            fb_w = mb->framebuffer_width;
            fb_h = mb->framebuffer_height;
            fb_bpp = mb->framebuffer_bpp;
            serial_write("[M0] UEFI fb from GRUB multiboot OK\n");
        } else {
            serial_write("[M0] UEFI: bit12=0 -> fb_init_gop_fallback\n");
            fb_init_gop_fallback(mb, &fb_addr, &fb_pitch, &fb_w, &fb_h, &fb_bpp);
        }
    } else {
        /* ---- BIOS 路径 ----
         * SeaBIOS/legacy GRUB：VESA VBE 设模式 + multiboot 头写 fb。
         * 检测到 文本控制台 mode（80x25x16bpp, type=2）算作 GRUB 没切到图模式。
         * 此时不再 halt，而是尝试 vga_bar fallback（PCI scan 拿 BAR）—
         * BIOS 路径下 PCI 设备是真实枚举到的，VGA BAR 同样可读。
         */
        int fb_ok = 0;
        int looks_like_text = (mb->framebuffer_type == 2) ||
                              (mb->framebuffer_width == 80 &&
                               mb->framebuffer_height == 25);
        if (!looks_like_text &&
            (mb->flags & (1u << 12)) && mb->framebuffer_addr &&
            mb->framebuffer_addr <= 0xFFFFFFFFULL &&
            mb->framebuffer_bpp == 32 &&
            mb->framebuffer_width >= 640 &&
            mb->framebuffer_height >= 480 &&
            mb->framebuffer_pitch >= mb->framebuffer_width * 4u) {
            fb_addr = (uint32_t)mb->framebuffer_addr;
            fb_pitch = mb->framebuffer_pitch;
            fb_w = mb->framebuffer_width;
            fb_h = mb->framebuffer_height;
            fb_bpp = mb->framebuffer_bpp;
            fb_ok = 1;
        }
        if (!fb_ok) {
            serial_write("\n[M0/BIOS] GRUB VBE didn't give a 32-bit RGB framebuffer.\n");
            serial_write("[M0/BIOS] Got: addr=0x"); serial_puth((uint32_t)mb->framebuffer_addr);
            serial_write(" pitch="); serial_puti(mb->framebuffer_pitch);
            serial_write(" w="); serial_puti(mb->framebuffer_width);
            serial_write(" h="); serial_puti(mb->framebuffer_height);
            serial_write(" bpp="); serial_puti(mb->framebuffer_bpp);
            serial_write(" type="); serial_puti(mb->framebuffer_type);
            serial_write("\n");
            if (looks_like_text) {
                serial_write("[M0/BIOS] 80x25 text console -> GRUB failed to set VBE graphics mode.\n");
            } else {
                serial_write("[M0/BIOS] GRUB VBE non-32bpp/RGB; kernel needs 32-bit RGB.\n");
            }
            /* 尝试走 PCI BAR fallback（vga_bar fallback，BIOS/UEFI 都可用）。
             * QEMU stdvga 1024x768x32 不一定在 VBE 表里，但 PCI BAR 总能拿到。 */
            serial_write("[M0/BIOS] trying vga_bar fallback (PCI scan)...\n");
            fb_init_gop_fallback(mb, &fb_addr, &fb_pitch, &fb_w, &fb_h, &fb_bpp);
            if (fb_addr && fb_w && fb_h && fb_bpp == 32) {
                fb_ok = 1;
                serial_write("[M0/BIOS] vga_bar fallback OK: ");
                serial_puti(fb_w); serial_write("x"); serial_puti(fb_h);
                serial_write("x"); serial_puti(fb_bpp); serial_write("\n");
            } else {
                serial_write("[M0/BIOS] vga_bar fallback failed. Try in grub.cfg:\n");
                serial_write("    set gfxmode=1024x768        (let GRUB pick available bpp)\n");
                serial_write("    set gfxpayload=1024x768x32   (request 32bpp for kernel)\n");
                serial_write("If GRUB still can't match, QEMU stdvga may need replacing via -vga.\n");
                for (;;) { __asm__ volatile("cli; hlt"); }
            }
        } else {
            serial_write("[M0/BIOS] fb from GRUB VBE: ");
            serial_puti(fb_w); serial_write("x"); serial_puti(fb_h);
            serial_write("x"); serial_puti(fb_bpp); serial_write("\n");
        }
    }
    if (fb_addr && fb_w && fb_h && fb_bpp == 32) {
        /* 把 framebuffer 物理区域映射进虚拟地址空间。NOCACHE 很重要：
         * framebuffer MMIO 不该走 CPU write-back cache，否则 CPU 写 fb.addr 后
         * CPU cache 可能延迟 flush，显卡扫描线读物理显存时看不到刚写入的内容——
         * 屏幕上就看到 OVMF/QEMU 默认的"花花绿绿测试图案"残留。
         * e1000 路径已用 PRESENT|WRITABLE|NOCACHE；这里之前漏了。 */
        uint32_t fb_size = fb_pitch * fb_h;
        serial_write("[M0] framebuffer phys=0x"); serial_puth(fb_addr);
        serial_write(" size=0x"); serial_puth(fb_size);
        serial_write(" ("); serial_puti(fb_w); serial_write("x"); serial_puti(fb_h);
        serial_write("x"); serial_puti(fb_bpp);
        serial_write(" pitch="); serial_puti(fb_pitch); serial_write(")\n");
        /* 高于 512MB 边界（恒等映射只覆盖 0~512MB）的物理地址必须显式映射，
         * 否则 fb_flush 写入会触发 #PF。低于 0x1000 是无意义地址，防御性 halt。 */
        if (fb_addr < 0x1000u) {
            serial_write("[M0] fb_addr too low, halt.\n");
            for (;;) { __asm__ volatile("cli; hlt"); }
        }
        if (fb_addr + fb_size > 0x20000000u) {
            serial_write("[M0] mapping framebuffer with NOCACHE...\n");
            vmm_map_region(fb_addr, fb_size,
                           PTE_PRESENT | PTE_WRITABLE | PTE_NOCACHE);
            serial_write("[M0] mapping done\n");
        }
        serial_write("[M0] -> vga_init @0x"); serial_puth(fb_addr);
        serial_write(" "); serial_puti(fb_w); serial_write("x");
        serial_puti(fb_h); serial_write("x"); serial_puti(fb_bpp); serial_write("\n");
        vga_init(fb_addr, fb_pitch, fb_w, fb_h, fb_bpp);
    } else {
        serial_write("[M0] No supported 32-bit RGB framebuffer available.\n");
        for (;;) { __asm__ volatile("cli; hlt"); }
    }

    serial_write("[M0] vga_init done\n");

    /* 打开中断，让 PIT 的 IRQ0 开始 tick，boot_animation 才能用 sleep_ticks 计时。
     * 之前 enable_int 放在 GUI 主循环之前，但 boot 动画需要它更早。 */
    enable_int();

    /* 开机动画：黑屏 → 逐列写出 S → 滑左 → 写出 prout → 光点分裂变蓝 OS →
     * 保持完整 "SproutOS"。整个过程 M1 还在跑（只打串口，不碰 framebuffer），
     * 所以画面在动画期间完全由 boot_animation 独占。 */
    boot_animation();

    serial_write("[M0] heap test: ");
    void* p1 = kmalloc(64);
    void* p2 = kmalloc(256);
    void* p3 = kmalloc(1024);
    serial_write(p1 && p2 && p3 ? "OK\n" : "FAIL\n");
    kfree(p2);
    void* p4 = kmalloc(128);
    serial_write(p4 ? "OK\n" : "FAIL\n");
    kfree(p1);
    kfree(p3);
    kfree(p4);
    serial_write("[M0] heap used: ");
    serial_puti(kmalloc_used());
    serial_write(" / ");
    serial_puti(kmalloc_total());
    serial_write("\n");
    serial_write("[M0] heap ok\n");

    serial_write("\n=== [M1] storage stack ===\n");
    serial_write("[M1] pci_init...\n"); pci_init(); serial_write("[M1] pci done\n");
    serial_write("[M1] ata_init...\n"); ata_init(); serial_write("[M1] ata done\n");
    serial_write("[M1] net_init...\n"); net_init(); serial_write("[M1] net done\n");
    serial_write("[M1] driver_run_all...\n"); driver_run_all(); serial_write("[M1] drivers done\n");
    serial_write("[M1] vfs_mount...\n"); vfs_mount(); serial_write("[M1] vfs done\n");
    serial_write("=== [M1] done ===\n\n");

    ttf_init();   /* 从 /YSHI.TTF 加载完整中文 TTF；失败则 GUI 自动回退点阵 */

    /* 堆诊断：打印最大空闲块（用于排查 kmalloc 失败） */
    {
        extern uint32_t kmalloc_free(void);
        serial_write("[HEAP] before wallpaper: free=");
        serial_puti(kmalloc_free());
        serial_write(" used=");
        serial_puti(kmalloc_used());
        serial_write("\n");
    }

    gui_load_wallpaper();

    /* 堆诊断：壁纸分配后再次检查 */
    {
        extern uint32_t kmalloc_free(void);
        serial_write("[HEAP] after wallpaper:  free=");
        serial_puti(kmalloc_free());
        serial_write(" used=");
        serial_puti(kmalloc_used());
        serial_write("\n");
    }

    kernel_selftest();

    serial_write("[GUI] mouse_install...\n");
    mouse_install();
    /* enable_int() 已在 vga_init 之后调用过了（boot_animation 需要它计时），
     * 这里不再重复调用。 */
    serial_write("[GUI] enter main loop\n");

    kprintf("Framebuffer %dx%d bpp=%d\n", fb.width, fb.height, fb.bpp);

    /* boot_animation 已经显示了完整 "SproutOS" 并保持了一段时间，
     * 直接进主循环。 */

    uint32_t next_frame = get_tick();

    for (;;) {
        if (get_tick() < next_frame) {
            __asm__ volatile("hlt");
            continue;
        }
        next_frame = get_tick() + 3;

        /* 浏览器全屏独占分支：直接驱动浏览器帧，跳过桌面/窗口/任务栏绘制。 */
        if (g_browser_open) {
            browser_frame();
            if (browser_done()) {
                g_browser_open = 0;
                mouse_clicked();   /* 消费可能残留的点击边沿，避免退出后误触桌面 */
            }
            sleep_ticks(1);
            continue;
        }

        /* SeedVM 全屏独占分支：执行 .seed 包字节码，ESC 退出回桌面。 */
        if (g_seedvm_active && g_seedvm) {
            seedvm_run_frame();
            sleep_ticks(1);
            continue;
        }

        int clicked = mouse_clicked();
        if (clicked) {
            int mx = mouse.x, my = mouse.y;
            if (my >= (int)fb.height - 28) {
                /* 任务栏左侧“开始”按钮由 ui/gui.c 的 gui_handle_input 处理 */
                /* 窗口按钮 x 位置需与 ui/gui.c::gui_draw_taskbar 保持一致（80 + i*80） */
                for (int i = 0; i < gui_window_count(); i++) {
                    int bx = 80 + i * 80;
                    int by = fb.height - 24;
                    if (mx >= bx && mx <= bx + 74 && my >= by && my <= by + 20) {
                        gui_taskbar_click(i);
                        break;
                    }
                }
            } else {
                int on_win = 0;
                for (int i = gui_window_count() - 1; i >= 0; i--) {
                    window_t* w = gui_window(i);
                    if (mx >= w->x && mx <= w->x + w->w && my >= w->y && my <= w->y + w->h) {
                        on_win = 1;
                        break;
                    }
                }
                if (!on_win) {
                    /* 桌面图标命中交给 gui_desktop_app_at()，其显示顺序由
                     * ui/gui.c 的 g_desktop_order 决定（与 gui_draw_desktop 一致）。 */
                    int ai = gui_desktop_app_at(mx, my);
                    if (ai >= 0) launcher_open(ai);
                }
            }
        }
        /* 键盘路由给聚焦窗口：编辑器(Ctrl+S 保存) 与终端(Ctrl+C/回车) 都靠它。
         * 每帧都处理，不依赖鼠标点击事件。 */
        if (kpeek()) {
            gui_handle_key(kgetc());
        }

        gui_handle_input(clicked);
        gui_draw_desktop();
        gui_draw_apps();
        gui_draw_taskbar();
        gui_draw_mouse();
        fb_flush();
        {
            static int first_frame = 1;
            if (first_frame) { first_frame = 0; serial_write("[GUI] frame 1 ok\n"); }
        }
        sleep_ticks(1);
    }
}

void launcher_open(int app_index) {
    /* 浏览器是“全屏独占”应用：不创建 window_t，直接初始化浏览器并切到浏览器分支。 */
    if (app_index == 6) {
        browser_init();
        g_browser_open = 1;
        return;
    }
    const char* nm = gui_app_names()[app_index];
    /* 颜色与桌面图标一致：绿(Files) 橙(Editor) 蓝(Term) 紫(About) 粉(Paint) 灰(Recycle) */
    uint32_t cols[] = { rgb(34,197,94), rgb(249,115,22), rgb(59,130,246), rgb(168,85,247), rgb(236,72,153), rgb(100,116,139) };
    int w = 360, h = 260;
    if (app_index == 0 || app_index == 1 || app_index == 2) { w = 420; h = 300; }
    if (app_index == 3) { w = 460; h = 380; }   /* 关于：容纳正文与系统参数 */
    if (app_index == 4) { w = 300; h = 280; }   /* 画图：256x192 画布 + 工具条 */
    if (app_index == 5) { w = 320; h = 240; }   /* 回收站 */
    gui_new_window(nm, cols[app_index], w, h);
}

/* 软关机：写 ACPI PM1a 控制端口（QEMU/PC 默认 0x604，Bochs 0xB004）触发 S5，
 * 失败则 cli;hlt 冻结。GUI 开始菜单“关机”项调用它。 */
void kernel_poweroff(void) {
    serial_write("[PWR] shutting down...\n");
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    __asm__ volatile("cli; hlt");
    for (;;) { __asm__ volatile("hlt"); }
}

/* ===== SeedVM 模式驱动 ====================================================
 * 主循环每帧调用：执行一批字节码 → 检测 ESC 退出 → 若已停机则显示提示。
 * 模式类似浏览器全屏独占，退出后回桌面。
 * =========================================================================*/
void seedvm_run_frame(void) {
    if (!g_seedvm) return;

    /* === 交互输入模式：OP_ASK 等待用户输入 === */
    if (g_seedvm->waiting_input) {
        while (kpeek()) {
            int c = kgetc();
            if (c == 27) {   /* ESC 退出 */
                seedvm_exit();
                return;
            }
            if (c == '\n' || c == '\r') {
                /* 输入完成 */
                g_seedvm->input_buf[g_seedvm->input_len] = 0;
                if (g_seedvm->ask_type == 1) {
                    /* 数字类型：atoi 解析 */
                    int val = 0;
                    int sign = 1;
                    int i = 0;
                    if (g_seedvm->input_buf[0] == '-') { sign = -1; i = 1; }
                    for (; g_seedvm->input_buf[i] >= '0' && g_seedvm->input_buf[i] <= '9'; i++)
                        val = val * 10 + (g_seedvm->input_buf[i] - '0');
                    g_seedvm->answer = val * sign;
                } else {
                    /* 文本类型：复制字符串 */
                    int len = g_seedvm->input_len;
                    char* s = kmalloc(len + 1);
                    if (s) {
                        for (int i = 0; i < len; i++) s[i] = g_seedvm->input_buf[i];
                        s[len] = 0;
                        g_seedvm->answer = (int32_t)(uintptr_t)s;
                    } else {
                        g_seedvm->answer = 0;
                    }
                }
                g_seedvm->has_answer = 1;
                g_seedvm->waiting_input = 0;
                /* 换行 */
                g_seedvm->cursor_x = 8;
                g_seedvm->cursor_y += 20;
                break;
            }
            if (c == '\b' || c == 127) {
                /* 退格：删除一个字符 */
                if (g_seedvm->input_len > 0) {
                    g_seedvm->input_len--;
                    /* 光标回退一个字符宽度，用空格覆盖 */
                    if (g_seedvm->input_len >= 0) {
                        char last = g_seedvm->input_buf[g_seedvm->input_len];
                        int cw = (last >= 0 && last < 128) ? 8 : 16;
                        g_seedvm->cursor_x -= cw;
                        /* 用空格覆盖（背景色） */
                        int px = g_seedvm->terminal_mode ? g_seedvm->client_x + g_seedvm->cursor_x : g_seedvm->cursor_x;
                        int py = g_seedvm->terminal_mode ? g_seedvm->client_y + g_seedvm->cursor_y : g_seedvm->cursor_y;
                        fb_rect(px, py, cw, 16, 0xFF0A0A12);
                    }
                }
                continue;
            }
            /* 可打印字符 */
            if (c >= 32 && c < 127 && g_seedvm->input_len < 255) {
                g_seedvm->input_buf[g_seedvm->input_len++] = (char)c;
                /* 回显 */
                char ch[2] = {(char)c, 0};
                int px = g_seedvm->terminal_mode ? g_seedvm->client_x + g_seedvm->cursor_x : g_seedvm->cursor_x;
                int py = g_seedvm->terminal_mode ? g_seedvm->client_y + g_seedvm->cursor_y : g_seedvm->cursor_y;
                fb_cn_string(px, py, ch, rgb(220, 220, 220), (uint32_t)-1);
                g_seedvm->cursor_x += 8;
            }
        }
        fb_flush();
        return;
    }

    /* === 正常执行模式 === */
    if (kpeek()) {
        int c = kgetc();
        if (c == 27) {   /* ESC */
            seedvm_exit();
            return;
        }
    }

    if (!seedvm_is_done(g_seedvm)) {
        seedvm_frame(g_seedvm);
    } else {
        /* VM 已停机，显示退出提示 */
        if (g_seedvm->terminal_mode) {
            int ty = g_seedvm->client_y + g_seedvm->client_h - 26;
            fb_rect(g_seedvm->client_x, ty, g_seedvm->client_w, 26, 0xFF1A1A2E);
            fb_cn_string(g_seedvm->client_x + 16, ty + 5,
                          "程序已结束  按 ESC 返回桌面",
                          rgb(148, 163, 184), (uint32_t)-1);
        } else {
            int y = fb.height - 30;
            fb_rect(0, y, fb.width, 30, 0xFF1A1A2E);
            fb_cn_string(16, y + 6, "程序已结束  按 ESC 返回桌面",
                          rgb(148, 163, 184), (uint32_t)-1);
        }
    }

    fb_flush();
}

void seedvm_exit(void) {
    if (g_seedvm) {
        seedvm_done(g_seedvm);
        g_seedvm = NULL;
    }
    g_seedvm_active = 0;
    fb_clear(0);
    mouse_clicked();  /* 消费残留点击 */
    serial_write("[SEED] exited to desktop\n");
}

/* ===== 开机动画 ===========================================================
 * 新版 SproutOS 字形开机动画（2026-08-20 重写）。
 * 动画流程（每 tick = 10ms，详见 ui/boot_logo_data.h）：
 *   1) PHASE_BLACK     黑屏停顿
 *   2) PHASE_S_WRITE   大字 S 在屏幕正中央逐列写出（像写字一样从左到右）
 *   3) PHASE_S_HOLD    S 写完后短暂保持
 *   4) PHASE_SLIDE     S 向左滑动到最终位置
 *   5) PHASE_PROUT     "prout" 五个字母同时逐列写出
 *   6) PHASE_DOT       右侧出现一个白色光点
 *   7) PHASE_SPLIT     光点分裂成两个 → 变成蓝色 O 和 S
 *   8) PHASE_HOLD      完整 "SproutOS" 保持
 *
 * 布局：scale=10（单字 80×80），"SproutOS" 8 字 = 640 像素宽，
 * 居中起点 x=(1024-640)/2=192, y=(768-80)/2=344。
 * S 单独显示时居中 x=(1024-80)/2=472。
 * 色彩：Sprout 白色，OS 蓝色 rgb(59,130,246)。
 * =========================================================================*/

/* 辅助：在 (x,y) 以 scale 放大显示字符 ch 的前 cols 列（0..8）。
 * 逐列从左到右刷出——这就是"写字"效果。bit 0 = 最左列。 */
static void fb_char_progressive(int x, int y, char ch, int cols, uint32_t fg, int scale) {
    if ((uint8_t)ch > 127) return;   /* 无符号视图：负数/非 ASCII 一并排除，无 -Wtype-limits 警告 */
    const uint8_t* glyph = font8x8_basic[(int)ch];
    if (cols < 0) cols = 0;
    if (cols > 8) cols = 8;
    for (int col = 0; col < cols; col++) {
        for (int row = 0; row < 8; row++) {
            if (glyph[row] & (1 << col)) {
                fb_rect(x + col * scale, y + row * scale, scale, scale, fg);
            }
        }
    }
}

/* 辅助：以逐列渐进方式显示字符串 s 的前 total_cols 列。
 * 多个字符同时从左到右刷出（每字 8 列）。 */
static void fb_string_write(int x, int y, const char* s, int total_cols,
                             uint32_t fg, int scale) {
    int cx = x;
    int drawn = 0;
    while (*s && drawn < total_cols) {
        int cols_this = total_cols - drawn;
        if (cols_this > 8) cols_this = 8;
        fb_char_progressive(cx, y, *s, cols_this, fg, scale);
        cx += 8 * scale;
        drawn += 8;
        s++;
    }
}

void boot_animation(void) {
    uint32_t white = rgb(255, 255, 255);
    uint32_t blue  = rgb(59, 130, 246);    /* Tailwind blue-500 */
    uint32_t glow  = rgb(96, 165, 250);    /* blue-400 */

    const int scale = 10;
    const int cell  = 8 * scale;            /* 80 单字宽高 */
    const int final_x = ((int)fb.width  - 8 * cell) / 2;  /* 192 */
    const int char_y  = ((int)fb.height - cell) / 2;      /* 344 */
    const int s_alone_x = ((int)fb.width - cell) / 2;     /* 472 */

    serial_write("[BOOT] animation start\n");

    /* === 阶段 1: PHASE_BLACK 黑屏停顿 === */
    fb_clear(0);
    fb_flush();
    sleep_ticks(BOOT_PHASE_BLACK_TICKS);

    /* === 阶段 2: PHASE_S_WRITE 大字 S 逐列写出 === */
    for (int i = 1; i <= BOOT_PHASE_S_WRITE_TICKS; i++) {
        int cols = (i * 8) / BOOT_PHASE_S_WRITE_TICKS;
        if (cols > 8) cols = 8;
        fb_clear(0);
        fb_char_progressive(s_alone_x, char_y, 'S', cols, white, scale);
        fb_flush();
        sleep_ticks(1);
    }

    /* === 阶段 3: PHASE_S_HOLD S 保持 === */
    sleep_ticks(BOOT_PHASE_S_HOLD_TICKS);

    /* === 阶段 4: PHASE_SLIDE S 向左滑动到最终位置 === */
    {
        int slide = s_alone_x - final_x;  /* 280 */
        for (int i = 1; i <= BOOT_PHASE_SLIDE_TICKS; i++) {
            int x = s_alone_x - (slide * i) / BOOT_PHASE_SLIDE_TICKS;
            fb_clear(0);
            fb_char_progressive(x, char_y, 'S', 8, white, scale);
            fb_flush();
            sleep_ticks(1);
        }
    }

    /* === 阶段 5: PHASE_PROUT "prout" 同时逐列写出 === */
    {
        int prout_x = final_x + cell;     /* S 右边紧贴 */
        int prout_cols = 5 * 8;            /* 40 列 */
        for (int i = 1; i <= BOOT_PHASE_PROUT_TICKS; i++) {
            int cols = (i * prout_cols) / BOOT_PHASE_PROUT_TICKS;
            if (cols > prout_cols) cols = prout_cols;
            fb_clear(0);
            /* S 已在最终位置 */
            fb_char_progressive(final_x, char_y, 'S', 8, white, scale);
            /* prout 前 cols 列 */
            fb_string_write(prout_x, char_y, "prout", cols, white, scale);
            fb_flush();
            sleep_ticks(1);
        }
    }

    /* === 阶段 6: PHASE_DOT 右侧光点出现 === */
    {
        int dot_cx = final_x + 6 * cell + cell / 2;  /* 752, OS 区域中心 */
        int dot_cy = char_y + cell / 2;               /* 384 */
        for (int i = 1; i <= BOOT_PHASE_DOT_TICKS; i++) {
            int r = (i * 30) / BOOT_PHASE_DOT_TICKS;
            if (r < 1) r = 1;
            fb_clear(0);
            fb_string_scale(final_x, char_y, "Sprout", white, (uint32_t)-1, scale);
            fb_circle(dot_cx, dot_cy, r, white);
            fb_flush();
            sleep_ticks(1);
        }
    }

    /* === 阶段 7: PHASE_SPLIT 光点分裂 + 变蓝 OS === */
    {
        int dot_cx = final_x + 6 * cell + cell / 2;  /* 752 */
        int dot_cy = char_y + cell / 2;               /* 384 */
        int o_cx   = final_x + 6 * cell + 4 * scale; /* 712, O 字中心 */
        int s_cx   = final_x + 7 * cell + 4 * scale;  /* 792, S 字中心 */
        for (int i = 1; i <= BOOT_PHASE_SPLIT_TICKS; i++) {
            int p = (i * 100) / BOOT_PHASE_SPLIT_TICKS;  /* 进度 0..100 */
            fb_clear(0);
            fb_string_scale(final_x, char_y, "Sprout", white, (uint32_t)-1, scale);

            /* 两个光点从中心向左右拉开 */
            int lx = dot_cx - (dot_cx - o_cx) * p / 100;
            int rx = dot_cx + (s_cx - dot_cx) * p / 100;
            int rad = 30 - p / 4;
            if (rad < 6) rad = 6;

            /* 光点颜色：白→蓝渐变 */
            uint32_t gc;
            if (p < 50) {
                int t = p * 2;  /* 0..100 */
                int cr = 255 - (255 - 96)  * t / 100;
                int cg = 255 - (255 - 165) * t / 100;
                int cb = 255 - (255 - 250) * t / 100;
                gc = rgb(cr, cg, cb);
            } else {
                gc = glow;
            }
            fb_circle(lx, dot_cy, rad, gc);
            fb_circle(rx, dot_cy, rad, gc);

            /* 后半段：蓝色 O 和 S 逐渐放大出现 */
            if (p > 50) {
                int sub = 1 + (p - 50) * 7 / 50;  /* 1..8 */
                if (sub > scale) sub = scale;
                fb_string_scale(o_cx - sub * 4, dot_cy - sub * 4,
                                "O", blue, (uint32_t)-1, sub);
                fb_string_scale(s_cx - sub * 4, dot_cy - sub * 4,
                                "S", blue, (uint32_t)-1, sub);
            }
            fb_flush();
            sleep_ticks(1);
        }
    }

    /* === 阶段 8: PHASE_HOLD 完整 SproutOS === */
    fb_clear(0);
    fb_string_scale(final_x, char_y, "Sprout", white, (uint32_t)-1, scale);
    fb_string_scale(final_x + 6 * cell, char_y, "O", blue, (uint32_t)-1, scale);
    fb_string_scale(final_x + 7 * cell, char_y, "S", blue, (uint32_t)-1, scale);
    fb_flush();
    sleep_ticks(BOOT_PHASE_HOLD_TICKS);

    serial_write("[BOOT] animation done, clearing framebuffer for desktop\n");
    /* Boot animation finishes with a static "SproutOS" composition. Hand a clean
     * black framebuffer over to the desktop / wallpaper stage so no leftover
     * glyphs leak into the GUI. This was the very first symptom users saw under
     * the GOP fallback that reported fb_h=2048: the previous animation frame
     * stayed visible at the bottom of the screen. */
    fb_clear(0);
    fb_flush();
}
