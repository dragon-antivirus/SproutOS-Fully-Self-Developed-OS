#include "kernel.h"

static void sputu(uint32_t v) {
    char out[12];
    int j = 0;
    if (v == 0) { serial_write("0"); return; }
    while (v > 0 && j < 10) { out[j++] = (char)('0' + (v % 10)); v /= 10; }
    int i = 0;
    while (j > 0) { char t = out[--j]; out[i++] = t; }
    out[i] = 0;
    serial_write(out);
}

static void result(const char* name, int pass) {
    serial_write("[SELFTEST] ");
    serial_write(name);
    serial_write(pass ? ": PASS\n" : ": FAIL\n");
}

void kernel_selftest(void) {
    serial_write("=== [SELFTEST] begin ===\n");

    {
        uint32_t before = pmm_free_pages();
        uint32_t p = pmm_alloc_page();
        int ok = (p != 0);
        if (p) pmm_free_page(p);
        if (pmm_free_pages() != before) ok = 0;
        result("PMM alloc/free round-trip", ok);
    }

    {
        void* a = kmalloc(512);
        int ok = (a != 0);
        if (a) {
            uint8_t* b = (uint8_t*)a;
            for (int i = 0; i < 512; i++) b[i] = (uint8_t)(i & 0xFF);
            for (int i = 0; i < 512; i++) if (b[i] != (uint8_t)(i & 0xFF)) ok = 0;
            kfree(a);
        }
        result("HEAP kmalloc/kfree pattern", ok);
    }

    {
        uint32_t cr0, cr3;
        __asm__ volatile("mov %%cr0,%0" : "=r"(cr0));
        __asm__ volatile("mov %%cr3,%0" : "=r"(cr3));
        int ok = (cr0 & 0x80000000u) && (cr3 != 0);
        result("VMM paging enabled", ok);
    }

    {
        int n = pci_device_count;
        serial_write("[SELFTEST] PCI enum: found ");
        sputu((uint32_t)(n < 0 ? 0 : n));
        serial_write(" device(s)\n");
        result("PCI enum", n > 0);
    }

    {
        block_device_t* bd = ata_get_bdev(0);
        if (!bd) {
            serial_write("[SELFTEST] ATA: SKIP (no disk attached)\n");
        } else {
            uint8_t buf[512];
            int ok = (bd->read_sectors(0, 1, buf) == 0);
            result("ATA read sector0", ok);
        }
    }

    {
        int mounted = vfs_is_mounted();
        result("VFS mount", mounted);
        if (mounted) {
            vfs_dir_t d;
            int c = vfs_list(&d);
            int cnt = (c == 0) ? (int)d.count : 0;
            serial_write("[SELFTEST] VFS list /: ");
            sputu((uint32_t)cnt);
            serial_write(" entries\n");
            if (c == 0 && cnt == 0)
                serial_write("[SELFTEST] VFS list /: root appears empty (run 'make disk' to populate disk.img)\n");
            result("VFS list /", c == 0 && cnt >= 1);
        }
    }

    serial_write("=== [SELFTEST] end ===\n");
}
