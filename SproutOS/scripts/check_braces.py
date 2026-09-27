import re, sys

def check(path):
    src = open(path, encoding='utf-8', errors='replace').read()
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    src = re.sub(r'//[^\n]*', '', src)
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        if c == '"':
            i += 1
            while i < n and src[i] != '"':
                if src[i] == '\\': i += 2
                else: i += 1
            i += 1
            continue
        if c == "'":
            i += 1
            while i < n and src[i] != "'":
                if src[i] == '\\': i += 2
                else: i += 1
            i += 1
            continue
        out.append(c)
        i += 1
    s = ''.join(out)
    pairs = {'(': ')', '{': '}', '[': ']'}
    stack = []
    ok = True
    for c in s:
        if c in '({[':
            stack.append(c)
        elif c in ')}]':
            if not stack or pairs[stack.pop()] != c:
                ok = False
                break
    bal = {'()': s.count('(') - s.count(')'),
           '{}': s.count('{') - s.count('}'),
           '[]': s.count('[') - s.count(']')}
    print("%s: balanced=%s deltas=%s" % (path, ok and not stack and all(v == 0 for v in bal.values()), bal))

for p in sys.argv[1:]:
    check(p)
