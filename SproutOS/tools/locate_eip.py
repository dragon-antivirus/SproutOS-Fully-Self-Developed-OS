#!/usr/bin/env python3
# 纯标准库解析 ELF32 符号表，把崩溃 EIP 映射到函数名+偏移。
# 用法: python3 tools/locate_eip.py build/kernel.bin 0x0010abcd
import sys, struct

def read_str(data, str_off, idx):
    end = data.index(b'\x00', str_off + idx)
    return data[str_off + idx:end].decode('latin-1', 'replace')

def main():
    if len(sys.argv) < 3:
        print("usage: locate_eip.py <kernel.bin> <eip-hex>")
        sys.exit(1)
    path = sys.argv[1]
    eip = int(sys.argv[2], 16)
    with open(path, 'rb') as f:
        data = f.read()
    if data[:4] != b'\x7fELF':
        print("not an ELF file"); sys.exit(1)
    if data[4] != 1:
        print("only ELF32 supported"); sys.exit(1)
    e_shoff = struct.unpack_from('<I', data, 0x20)[0]
    e_shentsize = struct.unpack_from('<H', data, 0x2E)[0]
    e_shnum = struct.unpack_from('<H', data, 0x30)[0]
    shdrs = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        shdrs.append(struct.unpack_from('<IIIIIIIIII', data, off))
    symtab = strtab = None
    for sh in shdrs:
        if sh[1] == 2:  # SHT_SYMTAB
            symtab = sh
            strtab = shdrs[sh[6]]
            break
    if not symtab:
        print("no symtab found"); sys.exit(1)
    sym_off, sym_size = symtab[4], symtab[5]
    str_off = strtab[4]
    count = sym_size // 16
    syms = []
    for i in range(count):
        base = sym_off + i * 16
        st_name, st_value, st_size, st_info, _other, st_shndx = struct.unpack_from('<IIIBBH', data, base)
        if (st_info & 0xF) == 2 and st_shndx != 0:  # FUNC, defined
            syms.append((st_value, read_str(data, str_off, st_name), st_size))
    syms.sort()
    best = None
    for addr, name, sz in syms:
        if addr <= eip and (best is None or addr > best[0]):
            best = (addr, name, sz)
    if best:
        off = eip - best[0]
        tag = 'within' if (best[2] != 0 and off < best[2]) else 'OUT-OF-RANGE?'
        print("crash eip=0x%08x -> function '%s' +0x%x (size 0x%x) %s" %
              (eip, best[1], off, best[2], tag))
    else:
        print("crash eip=0x%08x -> no function contains it" % eip)

if __name__ == '__main__':
    main()
