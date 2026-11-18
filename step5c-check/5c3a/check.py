# check.py: 5c-3a command check, after3a/ (5c-3a) against after2/ (5c-2).
# A changed command may only be for the four storage tests' targets; ctest -N
# may only gain the host card fixtures of native's two new storage tests.
import os, re
S = '/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/step5c-check'
ALLOWED = re.compile(r'(sd_test|smp-mat-sdcard-test|smp-num-test|smp-pipeline-test)-(qemu|hwd|host)')
FIXT = re.compile(r'native-(smp-num-test|smp-pipeline-test)-host-sd-(make|remove)$')
bad = 0
for f in sorted(os.listdir(f'{S}/after2')):
    if not f.endswith('.cmds'):
        continue
    C = f[:-5]
    b = set(open(f'{S}/after2/{f}').read().splitlines())
    a = set(open(f'{S}/after3a/{f}').read().splitlines())
    other = [l for l in (a ^ b) if not ALLOWED.search(l)]
    tb = set(open(f'{S}/after2/{C}.ctestN').read().split('\n'))
    ta = set(open(f'{S}/after3a/{C}.ctestN').read().split('\n'))
    tother = [t for t in (ta ^ tb) if not FIXT.search(t)]
    print(f'{C}: changed={len(a ^ b)} outside={len(other)} ctestN-changes={len(ta ^ tb)} unexpected={len(tother)}')
    if other or tother:
        bad += 1
        for l in (other + tother)[:5]:
            print('   ', l[:300])
print('configs with unexpected changes:', bad)
