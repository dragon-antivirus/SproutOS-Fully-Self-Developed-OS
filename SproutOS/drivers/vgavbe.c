/* drivers/vgavbe.c — 从 32 位保护模式把 QEMU/Bochs 标准 VGA 切到线性帧缓冲图形模式。
 *
 * 为什么需要它：GRUB 在 BIOS 下偶尔无法替我们把 VGA 切到 VBE 图形模式
 * （multiboot framebuffer_type==2 = 文本控制台）。光靠 PCI 扫 BAR 拿到显存地址
 * 还不够——VGA 硬件仍停在文本模式，往 LFB 写像素只会刷到字符 RAM，屏幕表现为花屏。
 *
 * 解决：QEMU "standard VGA"（PCI ven=0x1234 / dev=0x1111，与 Bochs VGA 同款）
 * 实现 Bochs VBE DISPI 接口，用两个 16-bit I/O 端口即可从保护模式直接设模式，
 * 不需要切回实模式调 int 0x10：
 *     INDEX 端口 0x1CE，DATA 端口 0x1CF
 *   关键寄存器索引：1=XRES 2=YRES 3=BPP 4=ENABLE 6=VIRT_WIDTH 7=VIRT_HEIGHT
 *   ENABLE 写 (1<<0)|(1<<1) = 使能 + LFB，触发 QEMU 按 XRES/YRES/BPP 重配显示。
 *
 * 注意：仅对 QEMU/Bochs 标准 VGA 调用（上层已按 vendor 0x1234 校验）。virtio-vga
 * （UEFI 路径）不实现该接口，且 GOP 已经配好显示，故绝不在 UEFI 下调用。
 */
#include "kernel.h"

#define VGA_DISPI_INDEX  0x1CE
#define VGA_DISPI_DATA   0x1CF
#define DI_XRES    1
#define DI_YRES    2
#define DI_BPP     3
#define DI_ENABLE  4
#define DI_VWIDTH  6
#define DI_VHEIGHT 7
#define DI_ENABLE_ON  0x01
#define DI_ENABLE_LFB 0x02

int vga_vbe_set_mode(uint16_t w, uint16_t h, uint8_t bpp) {
    /* 诊断：读 DISPI ID（应为 0xB0Cx），仅用于日志，不据此 return。 */
    outw(VGA_DISPI_INDEX, 0);
    uint16_t id = inw(VGA_DISPI_DATA);
    serial_write("[VBE] DISPI id=0x");
    serial_puth(id);
    serial_write("\n");

    /* 1) 先关闭 VBE（清掉旧模式，避免残留） */
    outw(VGA_DISPI_INDEX, DI_ENABLE);
    outw(VGA_DISPI_DATA, 0);

    /* 2) 设分辨率 / 位深 / 虚拟分辨率（VIRT 取与可见分辨率一致） */
    outw(VGA_DISPI_INDEX, DI_XRES);
    outw(VGA_DISPI_DATA, w);
    outw(VGA_DISPI_INDEX, DI_YRES);
    outw(VGA_DISPI_DATA, h);
    outw(VGA_DISPI_INDEX, DI_BPP);
    outw(VGA_DISPI_DATA, bpp);
    outw(VGA_DISPI_INDEX, DI_VWIDTH);
    outw(VGA_DISPI_DATA, w);
    outw(VGA_DISPI_INDEX, DI_VHEIGHT);
    outw(VGA_DISPI_DATA, h);

    /* 3) 使能 + LFB（不置 NOCLEARMEM，让 QEMU 清一次 VRAM，避免旧文本残影）。
     *    写 ENABLE 会触发 QEMU vbe_set_mode 按 XRES/YRES/BPP 重配显示控制器。 */
    outw(VGA_DISPI_INDEX, DI_ENABLE);
    outw(VGA_DISPI_DATA, DI_ENABLE_ON | DI_ENABLE_LFB);
    return 0;
}
