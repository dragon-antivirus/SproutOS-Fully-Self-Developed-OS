ENTRY(_start)

SECTIONS {
    . = 1M;
    .text : {
        __kernel_start = .;
        *(.multiboot)
        *(.text)
    }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }
    __kernel_end = .;
    . = ALIGN(0x1000);
    KHEAP_START = .;
    /* 24 MB kernel heap：需容纳运行时加载的完整 TTF 字体（约 7.6 MB）
     * 以及 stb_truetype 的中间缓冲与字形缓存。0–512MB 已被 VMM 恒等映射，安全。 */
    . = . + 0x1800000;
    KHEAP_END = .;
    /DISCARD/ : { *(.eh_frame) }
}
