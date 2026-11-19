#!/usr/bin/env python3
# compare.py <before-dir> <after-dir>: per configuration, the sorted build
# commands of the two snapshots, after mapping the moved paths (old -> new);
# prints what still differs.
import sys, os, re
W = '/home/dan/Desktop/micro-os-plu-iii-smp-pr-github'
K = W + '/micro-os-plus-iii/tests'
MAP = [
  # 6b-11, AArch64: board code -> platforms/aarch64-<b>/, board tests -> platforms/aarch64-<b>/tests/,
  # lwIP -> third_party/lwip/
  (r'/home/[^ ]*?micro-os-plus-iii-aarch64/test/boards/rpi3b/\.\./rpi-zero-2w/', K + '/platforms/aarch64-rpi-zero-2w/'),
  (r'/home/[^ ]*?micro-os-plus-iii-aarch64/test/boards/(rpi3b|rpi4b|rpi-zero-2w)/', K + r'/platforms/aarch64-\1/'),
  (r'/home/[^ ]*?micro-os-plus-iii-aarch64/test/third_party/', K + '/third_party/'),
  (r'/home/[^ ]*?micro-os-plus-iii-aarch64/test/(rpi3b|rpi4b|rpi-zero-2w)/', K + r'/platforms/aarch64-\1/tests/'),
  # board code: posix-arch test/boards/native -> platforms/native (also via the xpacks link)
  (r'/home/[^ ]*?micro-os-plus-iii-posix-arch/test/boards/native/', K + '/platforms/native/'),
  # board tests: posix-arch test/native -> platforms/native/tests
  (r'/home/[^ ]*?micro-os-plus-iii-posix-arch/test/native/', K + '/platforms/native/tests/'),
  # object names spell the source path
  (r'__/__/xpacks/@micro-os-plus/micro-os-plus-iii-posix-arch/test/boards/native/', 'PLATFORM_NATIVE/'),
  (r'/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/micro-os-plus-iii/tests/platforms/native/src/', 'PLATFORM_NATIVE/src/'),
  (r'/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/posix-arch/micro-os-plus-iii-posix-arch/test/native/', 'PLATFORM_NATIVE/tests/'),
]
def norm(l):
    # a path through a/../ is the same file
    prev = None
    while prev != l:
        prev = l; l = re.sub(r'/[^/\s.][^/\s]*/\.\./', '/', l)
    for a, b in MAP: l = re.sub(a, b, l)
    l = l.replace("'", '')
    # an object file is named after its source's path: keep the file name only
    l = re.sub(r'(\.dir/)\S*?([^/\s]+\.(?:c|cpp|S)\.obj?(?:\.d)?)\b', r'\1\2', l)
    # an -I that appears twice after the move counts once
    seen = set(); out = []; toks = l.split(); i = 0
    while i < len(toks):
        t = toks[i]
        if t in ('-I', '-isystem') and i + 1 < len(toks):
            t = t + toks[i + 1]; i += 1
        i += 1
        if t.startswith(('-I', '-isystem')):
            if t in seen: continue
            seen.add(t)
        out.append(t)
    return ' '.join(out)
b, a = sys.argv[1], sys.argv[2]
for f in sorted(os.listdir(b)):
    if not f.endswith('.commands'): continue
    x = sorted(norm(l) for l in open(os.path.join(b, f)))
    y = sorted(norm(l) for l in open(os.path.join(a, f)))
    if x == y: continue
    sx, sy = set(x), set(y)
    ox = {' '.join(sorted(l.split())) for l in sx - sy}
    oy = {' '.join(sorted(l.split())) for l in sy - sx}
    order_only = len(ox & oy)
    print(f'{f[:-9]}: {len(sx - sy)} only before, {len(sy - sx)} only after'
          f' ({order_only} of them the same tokens in another order)')
