# check.py: 6b-9a command check, step6-check/after6b9a/ against after6b8/.
# A changed command may only be for a suite's or a portable test's target
# (the suites are now built from test::<suite> on the platform's start-up;
# portable-test-end.cpp gained the prologue hook); ctest -N identical.
import os, re
W = '/home/dan/Desktop/micro-os-plu-iii-smp-pr-github'
A = f'{W}/step6-check/after6b8'; B = f'{W}/step6-check/after6b9a'
APPS = ('mutex-stress|rtos-apis|cmsis-os-validator|smp_test[0-4]|smp-num-test|'
        'smp-pipeline-test|smp-pro-cons-test|smp-mat-test|sd_test|'
        'smp-mat-sdcard-test|clock-test')
ALLOWED = re.compile(r'(' + APPS + r')-(qemu|hwd|host)\b')
bad = 0
for f in sorted(os.listdir(A)):
    if not f.endswith('.cmds'):
        continue
    C = f[:-5]
    a = set(open(f'{A}/{f}').read().splitlines())
    b = set(open(f'{B}/{f}').read().splitlines())
    other = [l for l in (a ^ b) if not ALLOWED.search(l)]
    same = open(f'{A}/{C}.ctestN').read() == open(f'{B}/{C}.ctestN').read()
    print(f'{C}: changed={len(a ^ b)} outside={len(other)} ctestN={"same" if same else "DIFF"}')
    if other or not same:
        bad += 1
        for l in other[:5]:
            print('   ', l[:300])
print('configs with unexpected changes:', bad)
