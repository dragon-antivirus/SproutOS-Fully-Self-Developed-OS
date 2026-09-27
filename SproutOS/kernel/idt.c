#include "kernel.h"

idt_entry idt[256];
idt_ptr idtp;

extern void load_idt(idt_ptr*);
extern void enable_int();
extern void disable_int();

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_lo = base & 0xFFFF;
    idt[num].base_hi = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

extern void isr0(), isr1(), isr2(), isr3(), isr4(), isr5(), isr6(), isr7();
extern void isr8(), isr9(), isr10(), isr11(), isr12(), isr13(), isr14(), isr15();
extern void isr16(), isr17(), isr18(), isr19(), isr20(), isr21(), isr22(), isr23();
extern void isr24(), isr25(), isr26(), isr27(), isr28(), isr29(), isr30(), isr31();
extern void irq0(), irq1(), irq2(), irq3(), irq4(), irq5(), irq6(), irq7();
extern void irq8(), irq9(), irq10(), irq11(), irq12(), irq13(), irq14(), irq15();

static void (*isr_routines[256])(regs*) = {0};

void register_interrupt_handler(uint8_t n, void (*handler)(regs*)) {
    isr_routines[n] = handler;
}

void idt_install(void) {
    idtp.limit = (sizeof(idt_entry) * 256) - 1;
    idtp.base = (uint32_t)&idt;
    for (int i = 0; i < 256; i++)
        idt_set_gate(i, (uint32_t)isr0 + 0, 0x08, 0x8E);
    idt_set_gate(0, (uint32_t)isr0, 0x08, 0x8E);
    idt_set_gate(1, (uint32_t)isr1, 0x08, 0x8E);
    idt_set_gate(2, (uint32_t)isr2, 0x08, 0x8E);
    idt_set_gate(3, (uint32_t)isr3, 0x08, 0x8E);
    idt_set_gate(4, (uint32_t)isr4, 0x08, 0x8E);
    idt_set_gate(5, (uint32_t)isr5, 0x08, 0x8E);
    idt_set_gate(6, (uint32_t)isr6, 0x08, 0x8E);
    idt_set_gate(7, (uint32_t)isr7, 0x08, 0x8E);
    idt_set_gate(8, (uint32_t)isr8, 0x08, 0x8E);
    idt_set_gate(9, (uint32_t)isr9, 0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);
    idt_set_gate(32, (uint32_t)irq0, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1, 0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2, 0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3, 0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4, 0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5, 0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6, 0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7, 0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8, 0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9, 0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);
    load_idt(&idtp);
}

static void sputc(char c) { serial_putc(c); }
static void sputs(const char* s) { while (*s) serial_putc(*s++); }
static void sputx(uint32_t n) {
    sputs("0x");
    const char* d = "0123456789abcdef";
    for (int i = 28; i >= 0; i -= 4) sputc(d[(n >> i) & 0xF]);
}

void isr_handler_main(regs* r) {
    if (r->int_no < 32) {
        sputs("\n*** CPU EXCEPTION int=");
        sputx(r->int_no);
        sputs(" err=");
        sputx(r->err_code);
        if (r->int_no == 14) {
            uint32_t cr2;
            __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
            sputs(" cr2=");
            sputx(cr2);
            /* isr_common: pusha; push ds/es/fs/gs; push esp(=r); call。
               regs 结构从 r 起: gs,fs,es,ds, edi,esi,ebp,esp,ebx,edx,ecx,eax,
               int_no(r+48), err_code(r+52)。其上方(更高地址)是 CPU 在异常
               入口压入的: eip(r+56), cs(r+60), eflags(r+64)。
               之前用 r-64/r-60 读反了方向，cs 读出垃圾值，特此修正。 */
            uint32_t* stk = (uint32_t*)((uint8_t*)r + 56);
            sputs(" eip="); sputx(stk[0]);
            sputs(" cs=");  sputx(stk[1]);
            sputs(" eflags="); sputx(stk[2]);
        }
        sputs(" (handlers are serial-only now) ***\n");
        for (;;) { __asm__ volatile("cli; hlt"); }
    }
    int irq = r->int_no - 32;
    switch (irq) {
        case 0: timer_tick(); break;
        case 1: keyboard_handler(); break;
        case 12: mouse_handler(); break;
        case 14: break;
        default: break;
    }
    /* Dispatch any driver-registered handler (NICs, etc.). The original
       specific cases above still run first; this is the generic path that
       previously was dead code. */
    if (isr_routines[r->int_no]) {
        isr_routines[r->int_no](r);
    }
    if (irq >= 8) outb(0xA0, 0x20);
    outb(0x20, 0x20);
}

void isr_handler(regs* r) {
    isr_handler_main(r);
}
