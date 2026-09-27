#include "kernel.h"
#include "heap.h"

#define HEAP_MAGIC 0xC0FFEE01u
#define ALIGN8(n) (((n) + 7) & ~7u)

typedef struct block_header {
    uint32_t magic;
    uint32_t size;
    uint32_t free;
    uint32_t reserved;
    struct block_header* next;
} block_header_t;

extern uint8_t KHEAP_START;
extern uint8_t KHEAP_END;

static block_header_t* heap_first = 0;
static uint32_t heap_total = 0;
static uint32_t heap_used = 0;

void kmalloc_init(void) {
    uint32_t start = (uint32_t)&KHEAP_START;
    uint32_t end = (uint32_t)&KHEAP_END;
    start = (start + 7) & ~7u;

    heap_first = (block_header_t*)start;
    heap_first->magic = HEAP_MAGIC;
    heap_first->size = end - start - sizeof(block_header_t);
    heap_first->free = 1;
    heap_first->reserved = 0;
    heap_first->next = 0;

    heap_total = heap_first->size;
    heap_used = 0;
}

void* kmalloc(uint32_t size) {
    if (size == 0) return 0;
    size = ALIGN8(size);

    block_header_t* cur = heap_first;
    while (cur) {
        if (cur->magic != HEAP_MAGIC) return 0;
        if (cur->free && cur->size >= size) {
            if (cur->size > size + sizeof(block_header_t) + 8) {
                uint8_t* split_at = (uint8_t*)cur + sizeof(block_header_t) + size;
                block_header_t* nb = (block_header_t*)split_at;
                nb->magic = HEAP_MAGIC;
                nb->size = cur->size - size - sizeof(block_header_t);
                nb->free = 1;
                nb->next = cur->next;
                cur->next = nb;
                cur->size = size;
            }
            cur->free = 0;
            heap_used += cur->size;
            return (void*)((uint8_t*)cur + sizeof(block_header_t));
        }
        cur = cur->next;
    }
    return 0;
}

void kfree(void* ptr) {
    if (!ptr) return;
    block_header_t* blk = (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
    if (blk->magic != HEAP_MAGIC) return;
    if (blk->free) return;
    heap_used -= blk->size;
    blk->free = 1;

    block_header_t* cur = heap_first;
    while (cur) {
        if (cur->free && cur->next && cur->next->free) {
            block_header_t* n = cur->next;
            cur->size += sizeof(block_header_t) + n->size;
            cur->next = n->next;
        } else {
            cur = cur->next;
        }
    }
}

uint32_t kmalloc_used(void) { return heap_used; }
uint32_t kmalloc_total(void) { return heap_total; }
uint32_t kmalloc_free(void) { return heap_total >= heap_used ? heap_total - heap_used : 0; }
