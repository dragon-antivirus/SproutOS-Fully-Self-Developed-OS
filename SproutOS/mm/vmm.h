#ifndef VMM_H
#define VMM_H

#include "kernel.h"

#define PTE_PRESENT  0x01
#define PTE_WRITABLE 0x02
#define PTE_USER     0x04
#define PTE_NOCACHE  0x10

void vmm_init(void);
void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags);
void vmm_map_region(uint32_t virt, uint32_t size, uint32_t flags);
void vmm_unmap_page(uint32_t virt);
uint32_t vmm_virt_to_phys(uint32_t virt);
uint32_t vmm_get_cr3(void);
void vmm_switch_pd(uint32_t pd_phys);

#endif
