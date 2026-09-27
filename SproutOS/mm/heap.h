#ifndef HEAP_H
#define HEAP_H

#include "kernel.h"

void kmalloc_init(void);
void* kmalloc(uint32_t size);
void kfree(void* ptr);
uint32_t kmalloc_used(void);
uint32_t kmalloc_total(void);
uint32_t kmalloc_free(void);

#endif
