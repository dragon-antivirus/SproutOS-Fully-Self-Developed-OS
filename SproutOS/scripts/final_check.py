import os
files=["kernel/kernel.c","kernel/kernel.h","ui/gui.c","fonts/font.c","drivers/keyboard.c",
 "browser/browser.c","browser/browser_osdep.c","browser/browser_osdep.h","browser/browser.h"]
def bal(fn):
    s=open(fn,encoding="utf-8",errors="replace").read(); dep=0; ins=None; i=0; n=len(s)
    while i<n:
        c=s[i]
        if ins:
            if c=='\\': i+=2; continue
            if c==ins: ins=None
            i+=1; continue
        if c=='/' and i+1<n and s[i+1]=='/': j=s.find('\n',i); i=n if j<0 else j; continue
        if c=='/' and i+1<n and s[i+1]=='*': j=s.find('*/',i+2); i=n if j<0 else j+2; continue
        if c=="'" or c=='"': ins=c; i+=1; continue
        if c=='{': dep+=1
        elif c=='}': dep-=1
        i+=1
    return dep
ok=True
for f in files:
    d=bal(f); ok=ok and d==0
    print(("OK " if d==0 else "BAD")+f" {f} net={d:+d}")
fc=open("fonts/font.c",encoding="utf-8").read()
print("font glyph entries:", fc.count("/* ["), "(expect 304)")
bridge=open("browser/browser_osdep.c",encoding="utf-8").read()
need=["fb_get_buffer","fb_width","fb_height","fb_present","fb_fill_rect","fb_draw_rect",
 "fb_draw_ascii","fb_draw_cn","fb_ascii_w","fb_cn_w","fb_line_h","fb_draw_rgba",
 "mouse_x","mouse_y","mouse_down","kbd_poll","browser_vfs_open","browser_vfs_read","browser_vfs_close"]
missing=[x for x in need if ("void "+x+"(" not in bridge) and ("int "+x+"(" not in bridge) and ("uint32_t* "+x+"(" not in bridge)]
print("bridge missing impls:", missing if missing else "none")
for f in ["browser/home.html","browser/test.bmp","browser/search_config.txt","browser/test_page.html"]:
    print(("OK " if os.path.exists(f) else "MISSING ")+f)
print("ALL BRACE OK:", ok)
