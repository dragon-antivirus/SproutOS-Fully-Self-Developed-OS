import os
files = [
 "kernel/kernel.c","kernel/kernel.h","ui/gui.c","fonts/font.c","drivers/keyboard.c",
 "browser/browser.c","browser/browser_osdep.c","browser/browser_osdep.h",
]
def check(fn):
    s=open(fn,encoding="utf-8",errors="replace").read()
    depth=0
    in_s=None
    i=0; n=len(s)
    while i<n:
        c=s[i]
        if in_s:
            if c=='\\': i+=2; continue
            if c==in_s: in_s=None
            i+=1; continue
        if c=='/' and i+1<n and s[i+1]=='/':
            j=s.find('\n',i); i= n if j<0 else j; continue
        if c=='/' and i+1<n and s[i+1]=='*':
            j=s.find('*/',i+2); i= n if j<0 else j+2; continue
        if c=="'" or c=='"': in_s=c; i+=1; continue
        if c=='{': depth+=1
        elif c=='}': depth-=1
        i+=1
    return depth
for f in files:
    if not os.path.exists(f):
        print("MISSING", f); continue
    d=check(f)
    print(("%s braces net=%+d  %s" % ("OK " if d==0 else "BAD", d, f)))
