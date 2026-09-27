import sys
def strip_comments(src):
    out=[]
    i=0; n=len(src); in_s=None
    while i<n:
        c=src[i]
        if in_s:
            out.append(c)
            if c=='\\':
                if i+1<n:
                    out.append(src[i+1]); i+=2; continue
                i+=1; continue
            if c==in_s: in_s=None
            i+=1; continue
        if c=='"' or c=="'":
            in_s=c; out.append(c); i+=1; continue
        if c=='/' and i+1<n and src[i+1]=='/':
            j=src.find('\n', i)
            if j<0: j=n
            i=j; continue
        if c=='/' and i+1<n and src[i+1]=='*':
            j=src.find('*/', i+2)
            if j<0: j=n-1
            # collapse comment to a single space, but keep a newline if present
            seg=src[i:j+2]
            if '\n' in seg:
                out.append('\n')
            i=j+2; continue
        out.append(c); i+=1
    return ''.join(out)

for fn in sys.argv[1:]:
    s=open(fn,encoding="utf-8",errors="replace").read()
    stripped=strip_comments(s)
    open(fn,"w",encoding="utf-8").write(stripped)
    print("stripped", fn)
