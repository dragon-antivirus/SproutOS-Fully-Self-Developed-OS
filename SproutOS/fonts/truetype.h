#ifndef TRUETYPE_H
#define TRUETYPE_H
#include "kernel.h"

/* 运行时 TTF 中文渲染（stb_truetype）。
 *
 * 启动时用 ttf_init() 把 /YSHI.TTF（完整中文 TrueType）整文件读进内核堆，
 * 之后任意 Unicode 码点都能用 ttf_glyph() 栅格化成 16x16 单色位图，
 * 彻底取代离线的 cn_font_16x16 点阵烘焙（点阵已删除）。TTF 缺失或某字缺失时
 * 由 vga.c 画 16x16 空框兜底，不再回退到点阵。 */

void ttf_init(void);
int  ttf_ok(void);

/* 把码点 cp 栅格化为 16x16 单色位图（32 字节：16 行 × 2 字节，
 * 字节 0 的 MSB = 最左像素）。成功返回 1；若 TTF 未就绪或该字不在字体里，
 * 返回 0（调用方画空框兜底）。 */
int  ttf_glyph(uint32_t cp, uint8_t out[32]);

#endif
