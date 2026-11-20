# compare.py <config>...: step 6 check against step6-check/before/.
# Same meaning, spelled differently, is normalised; everything else counts:
#   - a port reached through its xpm link == the port's real folder;
#   - an object file == (its target, its source);
#   - a board linker script == (board, file name), wherever it lives.
import json, os, re, subprocess, sys
W = '/home/dan/Desktop/micro-os-plu-iii-smp-pr-github'
H = f'{W}/step6-check'; T = f'{W}/micro-os-plus-iii/tests'
LD_OLD = re.compile(r'[^\s"]*/micro-os-plus-iii-[a-z0-9-]+/test/boards/([^/\s"]+)/([^/\s"]+\.ld)')
LD_NEW = re.compile(r'[^\s"]*/micro-os-plus-iii/tests/platforms/[a-z0-9]+-([^/\s"]+)/device/linker-scripts/([^/\s"]+\.ld)')
def norm(s, C):
    s = re.sub(re.escape(W) + r'/(?:cortexm|aarch32|aarch64|posix-arch|common|micro-os-plus-iii-(?:cortexm|aarch32|aarch64|posix-arch))/(micro-os-plus-iii-[a-z0-9-]+)', W + r'/\1', s)  # new folder == old folder
    s = s.replace("'", '')
    s = re.sub(re.escape(f'{T}/build/{C}/xpacks/.bin/') + r'[^\s"]+', lambda m: os.path.realpath(m.group(0)), s)  # tool through its xpm link == the tool                # shell quoting of paths with '@'
    s = re.sub(re.escape(f'{T}/build/{C}/xpacks/@micro-os-plus/') + r'(micro-os-plus-iii-[a-z0-9-]+)', W + r'/\1', s)
    s = re.sub(r'/[^\s"]*/\.\./[^\s"]*', lambda m: os.path.normpath(m.group(0)), s)  # a/b/../c == a/c
    s = LD_OLD.sub(r"LD:\2", s); s = LD_NEW.sub(r"LD:\2", s)
    return s
def objmap(cc, C):
    m = {}
    for e in cc:
        o = e.get('output', ''); t = re.search(r'CMakeFiles/([^/]+)\.dir/', o)
        m[o] = f'OBJ[{t.group(1) if t else "?"}:{norm(e["file"], C)}]'
    return m
def cc_norm(cc, C):
    om = objmap(cc, C); out = {}
    for e in cc:
        t = e['command'].split()
        if '-o' in t: i = t.index('-o'); del t[i:i+2]
        out[om[e.get('output','')]] = norm(' '.join(t), C)
    return out, om
def cmds_norm(lines, om, C):
    res = []
    for l in lines:
        if ' -c ' in l: continue          # compile lines: compared through compile_commands
        for o, k in om.items():
            if o and o in l: l = l.replace(o, k)
        res.append(norm(l, C))
    return sorted(res)
import difflib
def order_moves(x, y):
    ops = difflib.SequenceMatcher(None, x, y, autojunk=False).get_opcodes()
    return tuple((o, tuple(x[a:b]), tuple(y[c:d])) for o, a, b, c, d in ops if o != 'equal')
for C in sys.argv[1:]:
    B = f'{T}/build/{C}'
    ca = json.load(open(f'{H}/before-rename2/{C}.cc.json')); cb = json.load(open(f'{B}/compile_commands.json'))
    na, oa = cc_norm(ca, C); nb, ob = cc_norm(cb, C)
    keys = set(na) | set(nb)
    real = [k for k in keys if sorted((na.get(k) or '').split()) != sorted((nb.get(k) or '').split())]
    order = [k for k in keys if k not in real and na.get(k) != nb.get(k)]
    N = f'{T}/xpacks/.bin/ninja'
    after = subprocess.run([N, '-C', B, '-t', 'commands'], capture_output=True, text=True).stdout
    open(f'{H}/{C}.ninja-cmds.after-rename2', 'w').write(after)
    la = cmds_norm(open(f'{H}/before-rename2/{C}.ninja-cmds').read().splitlines(), oa, C)
    lb = cmds_norm(after.splitlines(), ob, C)
    sa = sorted(' '.join(sorted(l.split())) for l in la); sb = sorted(' '.join(sorted(l.split())) for l in lb)
    lreal = len(set(sa) ^ set(sb)); lorder = len(set(la) ^ set(lb)) // 2 if not lreal else len(set(la) ^ set(lb))
    tn = subprocess.run(['ctest', '--test-dir', B, '-N'], capture_output=True, text=True).stdout
    tb = sorted(re.findall(r'Test +#\d+: (\S+)', tn)); ta = open(f'{H}/before-rename2/{C}.ctestN').read().split()
    moves = sorted({order_moves(na[k].split(), nb[k].split()) for k in order})
    print(f'{C}: compile {len(nb)}: {len(real)} differ, {len(order)} order only ({len(moves)} distinct); '
          f'other commands {len(lb)}: {lreal} differ, {0 if lreal else lorder} order only; ctest -N {len(tb)} ({len(set(ta) ^ set(tb))} differ)')
    if '-v' in os.environ.get('CMPV', ''):
        for m in moves:
            print('   move:', [(o, [t.split('/')[-2:] for t in a], [t.split('/')[-2:] for t in b]) for o, a, b in m])
        for k in sorted(real)[:2]:
            x = (na.get(k) or '').split(); y = (nb.get(k) or '').split()
            print('   DIFF', k[:110], 'before-only', [t[-90:] for t in x if t not in y][:4], 'after-only', [t[-90:] for t in y if t not in x][:4])
