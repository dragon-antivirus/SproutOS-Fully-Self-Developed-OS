// === SproutOS boot animation timing (手写维护，不再由脚本生成) ===
//
// 动画流程（每 tick = 10ms）：
//   1) PHASE_BLACK     黑屏停顿
//   2) PHASE_S_WRITE   大字 S 在屏幕正中央逐列写出（像写字一样从左到右）
//   3) PHASE_S_HOLD    写完后短暂保持
//   4) PHASE_SLIDE     S 向左滑动到最终 "SproutOS" 中 S 的位置
//   5) PHASE_PROUT     "prout" 五个字母同时逐列写出（紧贴 S 右侧）
//   6) PHASE_DOT       右侧出现一个白色光点
//   7) PHASE_SPLIT     光点分裂成两个并向左右移动 → 变成蓝色 O 和 S
//   8) PHASE_HOLD      完整 "SproutOS"（白 Sprout + 蓝 OS）保持
//
// 总时长 ≈ 2.3s

#pragma once

#define BOOT_PHASE_BLACK_TICKS     30   /* 300ms 黑屏 */
#define BOOT_PHASE_S_WRITE_TICKS  16   /* 160ms S 逐列写出（每列 2 ticks） */
#define BOOT_PHASE_S_HOLD_TICKS   10   /* 100ms S 保持 */
#define BOOT_PHASE_SLIDE_TICKS    20   /* 200ms S 向左滑动 */
#define BOOT_PHASE_PROUT_TICKS    40   /* 400ms prout 同时逐列写出 */
#define BOOT_PHASE_DOT_TICKS       8   /*  80ms 光点出现 */
#define BOOT_PHASE_SPLIT_TICKS    25   /* 250ms 光点分裂 + 变蓝 OS */
#define BOOT_PHASE_HOLD_TICKS     80   /* 800ms 完整保持 */
