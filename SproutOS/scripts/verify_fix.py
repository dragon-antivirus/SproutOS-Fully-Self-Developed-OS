import re

files = [
    "kernel/kernel.h", "kernel/kernel.c", "ui/vga.c", "ui/gui.c", "fonts/font.c",
    "browser/browser.h", "browser/browser.c", "browser/browser_osdep.c",
    "browser/browser_osdep_stub.c",
]

def bal(fn):
    s = open(fn, encoding="utf-8", errors="replace").read()
    dep = 0; ins = None; i = 0; n = len(s)
    while i < n:
        c = s[i]
        if ins:
            if c == '\\':
                i += 2; continue
            if c == ins:
                ins = None
            i += 1; continue
        if c == '/' and i+1 < n and s[i+1] == '/':
            j = s.find('\n', i); i = n if j < 0 else j; continue
        if c == '/' and i+1 < n and s[i+1] == '*':
            j = s.find('*/', i+2); i = n if j < 0 else j+2; continue
        if c == "'" or c == '"':
            ins = c; i += 1; continue
        if c == '{':
            dep += 1
        elif c == '}':
            dep -= 1
        i += 1
    return dep

print("=== brace balance ===")
ok = True
for f in files:
    d = bal(f); ok = ok and (d == 0)
    print(("OK " if d == 0 else "BAD") + f" {f} net={d:+d}")

print("\n=== key fixes ===")
kh = open("kernel/kernel.h", encoding="utf-8").read()
print("kernel.h int8_t signed char :", "typedef signed char int8_t;" in kh)
bh = open("browser/browser.h", encoding="utf-8").read()
print("browser.h kprintf returns int:", "extern int kprintf(const char* fmt, ...);" in bh)
bc = open("browser/browser.c", encoding="utf-8").read()
print("browser.c no '===':", "===" not in bc)
print("browser.c struct rctx; forward:", "\nstruct rctx;\n" in bc or bc.lstrip().startswith("struct rctx;"))
print("browser.c render_node def uses rctx_t:", bool(re.search(r'static void\s+render_node\s*\(\s*html_node_t\*\s*n\s*,\s*rctx_t\*\s*rc\s*\)', bc)))
stub = open("browser/browser_osdep_stub.c", encoding="utf-8").read()
print("stub kprintf returns int:", "int kprintf(const char* f,...){" in stub)

print("\nALL BRACE OK:", ok)
