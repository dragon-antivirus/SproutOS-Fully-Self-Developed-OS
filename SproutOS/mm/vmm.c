#include "kernel.h"
#include "vmm.h"
#include "pmm.h"

#define PAGE_SIZE      0x1000
#define PDE_INDEX(v)   ((v) >> 22)
#define PTE_INDEX(v)   (((v) >> 12) & 0x3FF)
#define PDE_COUNT      1024

static uint32_t* current_pd = 0;

/* 返回 pdi 对应的页表虚拟地址。
   启用分页后绝不能再用 pdi*0x400000 当页表基址——
   那只是 4GB 平坦页表空间的线性下标，页表真实的物理基址是 PDE[pdi] 里存的值。
   访问它靠恒等映射（页表页都落在 0-512MB 内）。这是个隐藏极深的坑：
   对 fb_phys=0xFD000000，pdi=1012，1012*0x400000 恰好 == 0xFD000000，
   于是 vmm_map_page 会试图把 PTE 写到帧缓冲自己的地址上 -> #PF 三重故障。 */
static inline uint32_t* vmm_pt_of(uint32_t pdi) {
    uint32_t pde = current_pd[pdi];
    return (uint32_t*)(pde & 0xFFFFF000u);
}

void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pdi = PDE_INDEX(virt);
    uint32_t pti = PTE_INDEX(virt);
    if (pdi >= PDE_COUNT) return;
    volatile uint32_t* pt = (volatile uint32_t*)vmm_pt_of(pdi);
    pt[pti] = (phys & 0xFFFFF000u) | (flags & 0xFFFu);
}

void vmm_map_region(uint32_t virt, uint32_t size, uint32_t flags) {
    uint32_t start = virt & 0xFFFFF000u;
    uint32_t end = (virt + size + 0xFFFu) & 0xFFFFF000u;
    for (uint32_t a = start; a < end; a += 0x1000u) {
        vmm_map_page(a, a, flags);
    }
}

void vmm_unmap_page(uint32_t virt) {
    uint32_t pdi = PDE_INDEX(virt);
    uint32_t pti = PTE_INDEX(virt);
    if (pdi >= PDE_COUNT) return;
    volatile uint32_t* pt = (volatile uint32_t*)vmm_pt_of(pdi);
    pt[pti] = 0;
}

uint32_t vmm_virt_to_phys(uint32_t virt) {
    uint32_t pdi = PDE_INDEX(virt);
    uint32_t pti = PTE_INDEX(virt);
    if (pdi >= PDE_COUNT) return 0;
    volatile uint32_t* pt = (volatile uint32_t*)vmm_pt_of(pdi);
    uint32_t pte_val = pt[pti];
    if (!(pte_val & PTE_PRESENT)) return 0;
    return (pte_val & 0xFFFFF000u) | (virt & 0xFFFu);
}

void vmm_init(void) {
    /* 1. 分配 page directory 自身 */
    uint32_t pd_phys = pmm_alloc_page();
    if (!pd_phys) {
        serial_write("VMM: out of memory (page dir)\n");
        for (;;) { __asm__ volatile("cli; hlt"); }
    }

    /* 2. 分配 1024 个 page table 物理页（覆盖整个 4GB 地址空间）。
          启用分页后再 map 帧缓冲(高地址)时，这些预建页表已就位，
          不会再出现"分配页表页 -> 该页未映射 -> 连环 fault"的隐患。 */
    uint32_t pts_phys[PDE_COUNT];
    for (int i = 0; i < PDE_COUNT; i++) {
        pts_phys[i] = pmm_alloc_page();
        if (!pts_phys[i]) {
            serial_write("VMM: out of memory (page table)\n");
            for (;;) { __asm__ volatile("cli; hlt"); }
        }
    }

    /* 3. 启用分页前，直接用物理地址（= 虚拟地址）清零 */
    uint8_t* p = (uint8_t*)pd_phys;
    for (uint32_t i = 0; i < PAGE_SIZE; i++) p[i] = 0;
    for (int i = 0; i < PDE_COUNT; i++) {
        p = (uint8_t*)pts_phys[i];
        for (uint32_t j = 0; j < PAGE_SIZE; j++) p[j] = 0;
    }

    /* 4. 填 PDE[0..1023] -> 各自 page table 物理页，flags = present|writable */
    uint32_t* pd = (uint32_t*)pd_phys;
    for (int i = 0; i < PDE_COUNT; i++) {
        pd[i] = pts_phys[i] | PTE_PRESENT | PTE_WRITABLE;
    }

    /* 5. 预填 0–512MB 恒等映射（让 PMM bitmap / kernel / 栈 / 堆在启用分页后
          仍能通过原地址访问）。页表基址用 pts_phys[pdi] 直接写，正确。 */
    uint32_t mem_end = 0x20000000u;  /* 512MB，正好等于机器 RAM 上限 */
    for (uint32_t a = 0; a < mem_end; a += PAGE_SIZE) {
        uint32_t pdi = a >> 22;
        uint32_t pti = (a >> 12) & 0x3FFu;
        uint32_t* pt = (uint32_t*)pts_phys[pdi];
        pt[pti] = a | PTE_PRESENT | PTE_WRITABLE;
    }

    current_pd = (uint32_t*)pd_phys;

    /* 6. 加载 CR3，置位 CR0.PG 启用分页 */
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_phys));
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000u;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    /* 诊断：启用分页后立即自检关键页表项（地址必须落在 0-512MB 恒等映射内） */
    uint32_t self_cr3; __asm__ volatile("mov %%cr3, %0" : "=r"(self_cr3));
    serial_write("[VMM] cr3="); serial_puth(self_cr3); serial_write(" pd_phys="); serial_puth(pd_phys); serial_write("\n");

    uint32_t pde0 = current_pd[0];
    serial_write("[VMM] PDE[0]="); serial_puth(pde0); serial_write("\n");
    uint32_t pde10 = current_pd[0x10];
    serial_write("[VMM] PDE[0x10]="); serial_puth(pde10); serial_write("\n");

    /* 用修正后的取页表法读 PTE：内核 1MB(虚拟0x100000,pdi0/pti0x100) 与 heap 起点 2MB */
    volatile uint32_t* pt0 = (volatile uint32_t*)vmm_pt_of(0);
    serial_write("[VMM] PTE[0x100]="); serial_puth(pt0[0x100]); serial_write("\n");
    serial_write("[VMM] PTE[0x200]="); serial_puth(pt0[0x200]); serial_write("\n");

    serial_write("[VMM] identity mapped 0-512MB, 1024 PTs prebuilt\n");
}

uint32_t vmm_get_cr3(void) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void vmm_switch_pd(uint32_t pd_phys) {
    current_pd = (uint32_t*)pd_phys;
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_phys));
}
