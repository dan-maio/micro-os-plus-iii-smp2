# check.py: 5c-2 command check. For every configuration, the commands in
# after2/ (5c-2) against before/ (the state before 5c): a command may differ
# only when its target is sd_test-* or smp-mat-sdcard-test-*; ctest -N equal.
import os, re, sys
S = '/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/step5c-check'
ALLOWED = re.compile(r'(sd_test|smp-mat-sdcard-test)-(qemu|hwd|host)')
bad = 0
for f in sorted(os.listdir(f'{S}/before')):
    if not f.endswith('.cmds'):
        continue
    C = f[:-5]
    b = set(open(f'{S}/before/{f}').read().splitlines())
    a = set(open(f'{S}/after2/{f}').read().splitlines())
    other = [l for l in (a ^ b) if not ALLOWED.search(l)]
    tb = open(f'{S}/before/{C}.ctestN').read(); ta = open(f'{S}/after2/{C}.ctestN').read()
    print(f'{C}: changed={len(a ^ b)} outside-sd-targets={len(other)} ctestN={"same" if ta == tb else "DIFF"}')
    if other or ta != tb:
        bad += 1
        for l in other[:5]:
            print('   ', l[:300])
print('configs with unexpected changes:', bad)
