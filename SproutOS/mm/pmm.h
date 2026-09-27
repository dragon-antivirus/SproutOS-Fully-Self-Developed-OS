#ifndef PMM_H
#define PMM_H

#include "kernel.h"

void pmm_init(uint32_t mboot_addr);
uint32_t pmm_alloc_page(void);
void pmm_free_page(uint32_t phys);
uint32_t pmm_total_pages(void);
uint32_t pmm_free_pages(void);
void pmm_mark_used(uint32_t base, uint32_t size);
void pmm_mark_free(uint32_t base, uint32_t size);

#endif
