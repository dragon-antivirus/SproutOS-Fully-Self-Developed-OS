import re, os

files = [
    "drivers/net.c", "drivers/net.h",
    "browser/browser.c", "browser/browser_osdep.c", "browser/browser_osdep.h",
    "ui/gui.c", "fonts/font.c", "ui/vga.c",
    "kernel/kernel.h", "kernel/kernel.c",
    "drivers/keyboard.c",
]

def bal(fn):
    s = open(fn, encoding="utf-8", errors="replace").read()
    dep = 0; ins = None; i = 0; n = len(s)
    while i < n:
        c = s[i]
        if ins:
            if c == '\\': i += 2; continue
            if c == ins: ins = None
            i += 1; continue
        if c == '/' and i + 1 < n and s[i + 1] == '/':
            j = s.find('\n', i); i = n if j < 0 else j; continue
        if c == '/' and i + 1 < n and s[i + 1] == '*':
            j = s.find('*/', i + 2); i = n if j < 0 else j + 2; continue
        if c in ("'", '"'): ins = c; i += 1; continue
        if c == '{': dep += 1
        elif c == '}': dep -= 1
        i += 1
    return dep

print("=== BRACE BALANCE ===")
ok = True
for f in files:
    try:
        d = bal(f)
        tag = "OK" if d == 0 else f"BAD net={d:+}"
        if d != 0: ok = False
        print(f"  {tag}  {f}")
    except FileNotFoundError:
        print(f"  MISS  {f}")

print(f"\n{'ALL OK' if ok else 'HAS ERRORS'}")

print("\n=== KEY DECLARATIONS IN net.h ===")
h = open("drivers/net.h", encoding="utf-8").read()
for sym in ["net_udp_send", "dns_resolve", "net_tcp_connect", "net_tcp_send",
            "net_tcp_recv", "net_tcp_close", "http_get", "IP_PROTO_TCP"]:
    print(f"  {'OK' + sym if sym in h else 'MISS ' + sym}")

print("\n=== KEY DECLARATIONS IN browser_osdep.h ===")
bh = open("browser/browser_osdep.h", encoding="utf-8").read()
for sym in ["net_http_get", "browser_vfs_open", "fb_draw_cn"]:
    print(f"  {'OK' + sym if sym in bh else 'MISS ' + sym}")

print("\n=== IMPLEMENTATIONS IN net.c ===")
nc = open("drivers/net.c", encoding="utf-8").read()
for sym in ["net_udp_send", "dns_resolve", "net_rx_udp", "net_rx_tcp",
            "net_tcp_connect", "net_tcp_send", "net_tcp_recv", "net_tcp_close",
            "http_get", "tcp_checksum", "tcp_tx", "starts_with"]:
    found = re.search(r'(?:void|int|uint32_t|uint16_t|static\s+(?:void|int|uint32_t|uint16_t))\s+' + re.escape(sym) + r'\s*\(', nc)
    print(f"  {'OK ' + sym if found else 'MISS ' + sym}")

print("\n=== IMPLEMENTATIONS IN browser_osdep.c ===")
bc = open("browser/browser_osdep.c", encoding="utf-8").read()
for sym in ["net_http_get", "browser_vfs_open", "fb_draw_cn"]:
    found = re.search(re.escape(sym) + r'\s*\(', bc)
    print(f"  {'OK ' + sym if found else 'MISS ' + sym}")

print("\n=== HTTP USAGE IN browser.c ===")
brc = open("browser/browser.c", encoding="utf-8").read()
for sym in ["net_http_get", "load_url(", "do_navigate("]:
    cnt = brc.count(sym)
    print(f"  {sym}: {cnt} refs")

print("\n=== FORWARD DECLS IN net.c ===")
fwd_patterns = [
    r"static void net_rx_udp\(",
    r"static void net_rx_tcp\(",
    r"static void serial_put_ip\(",
]
for pat in fwd_patterns:
    found = re.search(pat, nc)
    name = pat.split('(')[0].replace('static ', '').replace(' ', '')
    print(f"  {'OK fwd ' + name if found else 'MISS fwd ' + name}")

print("\n=== DNS BUFFER SIZE ===")
m = re.search(r'dns_rx_buf\[(\d+)\]', nc)
if m:
    print(f"  dns_rx_buf[{m.group(1)}] bytes")

m2 = re.search(r'TCP_RX_BUF_SIZE\s+(\d+)', nc)
if m2:
    print(f"  TCP_RX_BUF_SIZE = {m2.group(1)} bytes")

m3 = re.search(r'HTTP_MAX_RESP\s+\((\d+)\*(\d+)\)', nc)
if m3:
    print(f"  HTTP_MAX_RESP = {int(m3.group(1))*int(m3.group(2))} bytes ({m3.group(1)}*{m3.group(2)})")

print("\nDone.")
