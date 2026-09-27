; Multiboot1 头 — 同时兼容 BIOS (GRUB Legacy/BIOS) 和 UEFI (GRUB EFI)。
; GRUB EFI 在加载 multiboot1 内核前退出 UEFI Boot Services 并切换到
; 32 位保护模式，随后按 multiboot1 规范传入 eax=0x2BADB002 / ebx=info_ptr。
; Flags bit 2 (request video info) 关闭 — GRUB EFI 在 GOP 模式匹配失败时
; 会按规范不填 framebuffer 字段，但 GRUB 实现里会报错"no suitable video mode"。
; 关掉 bit 2 即可让 GRUB 完全跳过视频 setup；UEFI 下 kernel 走 PCI GOP fallback
; 直接读 VGA 设备的 BAR0 作为 framebuffer。BIOS 下 VBE 仍由 GRUB Legacy 设置，
; kernel 透传原有 framebuffer 字段即可。
BITS 32

section .multiboot align=8
MB_MAGIC  equ 0x1BADB002
MB_FLAGS  equ 0x00000003          ; 仅保留 bit 0 (align) + bit 1 (memmap)
                                     ; 关闭 bit 2 (video info request)，避免 GRUB EFI
                                     ; 在 GOP 模式匹配失败时不填 framebuffer 字段。
MB_CHKSUM equ -(MB_MAGIC + MB_FLAGS)
  dd MB_MAGIC
  dd MB_FLAGS
  dd MB_CHKSUM
  dd 0
  dd 0
  dd 0
  dd 0
  dd 0
  dd 0
  dd 0
  dd 0
  dd 0

section .text
global _start
extern kernel_main
extern isr_handler
extern __bss_start
extern __bss_end

_start:
  cli
  mov esp, stack_top
  mov edx, eax
  mov esi, ebx
  mov ecx, __bss_end
  sub ecx, __bss_start
  mov edi, __bss_start
  xor eax, eax
  cld
  rep stosb
  push esi
  push edx
  call kernel_main
  hlt

%macro ISR_NOERR 1
global isr%1
isr%1:
  push dword 0
  push dword %1
  jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
  push dword %1
  jmp isr_common
%endmacro

%macro IRQ 2
global irq%2
irq%2:
  push dword 0
  push dword %1
  jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_NOERR 30
ISR_NOERR 31

IRQ 32,0
IRQ 33,1
IRQ 34,2
IRQ 35,3
IRQ 36,4
IRQ 37,5
IRQ 38,6
IRQ 39,7
IRQ 40,8
IRQ 41,9
IRQ 42,10
IRQ 43,11
IRQ 44,12
IRQ 45,13
IRQ 46,14
IRQ 47,15

isr_common:
  pusha
  push ds
  push es
  push fs
  push gs
  mov ax, 0x10
  mov ds, ax
  mov es, ax
  mov fs, ax
  mov gs, ax
  mov eax, esp
  push eax
  call isr_handler
  add esp, 4
  pop gs
  pop fs
  pop es
  pop ds
  popa
  add esp, 8
  iret

global load_idt
load_idt:
  mov eax, [esp+4]
  lidt [eax]
  ret

global enable_int
enable_int:
  sti
  ret

global disable_int
disable_int:
  cli
  ret

section .bss
align 16
stack_bottom:
  resb 16384
stack_top:
