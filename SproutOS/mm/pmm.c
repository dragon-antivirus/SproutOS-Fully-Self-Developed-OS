#include "kernel.h"
#include "pmm.h"

#define PMM_MAX_PAGES 131072
#define PAGE_SIZE 0x1000

static uint8_t pmm_bitmap[PMM_MAX_PAGES / 8];
static uint32_t pmm_total = 0;
static uint32_t pmm_used = 0;

extern uint8_t __kernel_start;
extern uint8_t __kernel_end;
extern uint8_t KHEAP_START;
extern uint8_t KHEAP_END;

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
} __attribute__((packed)) mbi_t;

static inline void bm_set(uint32_t bit) {
    pmm_bitmap[bit >> 3] |= (uint8_t)(1 << (bit & 7));
}

static inline void bm_clr(uint32_t bit) {
    pmm_bitmap[bit >> 3] &= (uint8_t)~(1 << (bit & 7));
}

static inline int bm_tst(uint32_t bit) {
    return pmm_bitmap[bit >> 3] & (1 << (bit & 7));
}

void pmm_mark_used(uint32_t base, uint32_t size) {
    if (size == 0) return;
    uint32_t s = base / PAGE_SIZE;
    uint32_t e = (base + size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (e > PMM_MAX_PAGES) e = PMM_MAX_PAGES;
    for (uint32_t i = s; i < e; i++) if (!bm_tst(i)) { bm_set(i); pmm_used++; }
}

void pmm_mark_free(uint32_t base, uint32_t size) {
    if (size == 0) return;
    uint32_t s = base / PAGE_SIZE;
    uint32_t e = (base + size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (e > PMM_MAX_PAGES) e = PMM_MAX_PAGES;
    for (uint32_t i = s; i < e; i++) if (bm_tst(i)) { bm_clr(i); pmm_used--; }
}

void pmm_init(uint32_t mboot_addr) {
    for (uint32_t i = 0; i < sizeof(pmm_bitmap); i++) pmm_bitmap[i] = 0xFF;
    pmm_total = 0;
    pmm_used = PMM_MAX_PAGES;

    mbi_t* mb = (mbi_t*)mboot_addr;

    if (mb->flags & (1u << 6)) {
        uint32_t m = mb->mmap_addr;
        uint32_t end = m + mb->mmap_length;
        while (m < end) {
            uint32_t sz = *(uint32_t*)m;
            uint64_t base = *(uint64_t*)(m + 4);
            uint64_t len = *(uint64_t*)(m + 12);
            uint32_t type = *(uint32_t*)(m + 20);
            if (type == 1 && (uint64_t)(uint32_t)base == base) {
                uint32_t lo = (uint32_t)base;
                uint32_t lz = (uint32_t)len;
                if (lo + lz < lo) lz = 0xFFFFFFFFu - lo;
                uint32_t s = lo / PAGE_SIZE;
                uint32_t e = (lo + lz + PAGE_SIZE - 1) / PAGE_SIZE;
                if (e > PMM_MAX_PAGES) e = PMM_MAX_PAGES;
                for (uint32_t i = s; i < e; i++) if (bm_tst(i)) { bm_clr(i); pmm_used--; }
            }
            m += sz + 4;
        }
    }

    pmm_mark_used(0, 0x100000);
    pmm_mark_used((uint32_t)&__kernel_start, (uint32_t)&__kernel_end - (uint32_t)&__kernel_start);
    pmm_mark_used((uint32_t)pmm_bitmap, sizeof(pmm_bitmap));
    /* 关键：必须保留 kmalloc 的堆区，否则 pmm 会把页目录/页表分配到这里，
       随后 kmalloc_init 写 block_header 时把这些页表清零 -> 内核踩踏自身页表 -> 随机 #PF 重启。
       （cr3=0x424000 正是 KHEAP_START，就是被踩的页目录。） */
    pmm_mark_used((uint32_t)&KHEAP_START, (uint32_t)&KHEAP_END - (uint32_t)&KHEAP_START);

    if (mb->flags & (1u << 12) && mb->framebuffer_addr && (uint64_t)(uint32_t)mb->framebuffer_addr == mb->framebuffer_addr) {
        uint32_t fb_addr = (uint32_t)mb->framebuffer_addr;
        uint32_t fb_size = mb->framebuffer_pitch * mb->framebuffer_height;
        pmm_mark_used(fb_addr, fb_size);
    }

    pmm_total = PMM_MAX_PAGES;
}

uint32_t pmm_alloc_page(void) {
    for (uint32_t i = 0; i < PMM_MAX_PAGES; i++) {
        if (!bm_tst(i)) {
            bm_set(i);
            pmm_used++;
            return i * PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free_page(uint32_t phys) {
    uint32_t bit = phys / PAGE_SIZE;
    if (bit >= PMM_MAX_PAGES) return;
    if (!bm_tst(bit)) return;
    bm_clr(bit);
    pmm_used--;
}

uint32_t pmm_total_pages(void) { return pmm_total; }
uint32_t pmm_free_pages(void) {
    return pmm_total >= pmm_used ? pmm_total - pmm_used : 0;
}
