# cmpcc.py <config> <port>: compare compile_commands.json of the step build and the smp worktree build.
import json, sys, os
C, P = sys.argv[1], sys.argv[2]
W = '/home/dan/Desktop/micro-os-plu-iii-smp-pr-github'
def load(root, repl):
    out = {}
    for e in json.load(open(f'{root}/tests/build/{C}/compile_commands.json')):
        t = e['command'].split(); t[0] = os.path.basename(t[0])
        if '-o' in t: i = t.index('-o'); del t[i:i+2]
        c, f = ' '.join(t), e['file']
        for a, b in repl: c = c.replace(a, b); f = f.replace(a, b)
        out[f] = c
    return out
link = f'{W}/micro-os-plus-iii/tests/build/{C}/xpacks/@micro-os-plus/{P}'
new = load(W + '/micro-os-plus-iii', [(link, f'{W}/{P}'), (W + '/', '@/')])
old = load(W + '/wt-smp/micro-os-plus-iii', [(W + '/wt-smp/', '@/'), (W + '/', '@/')])
d = [k for k in sorted(set(new) | set(old)) if new.get(k) != old.get(k)]
print(C, len(new), len(old), 'differ', len(d))
for k in d[:3]:
    a = (new.get(k) or '').split(); b = (old.get(k) or '').split()
    print(' ', k, 'new-only', [x for x in a if x not in b][:6], 'old-only', [x for x in b if x not in a][:6])
