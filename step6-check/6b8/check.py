# check.py: 6b-8 command check, step6-check/after6b8/ against
# step5c-check/after3b/ (5c-3b). A changed command may only be for a portable
# test's target (the platform's include/ is new on their path; clock-test is
# new among them); ctest -N must be identical (clock-test keeps its names).
import os, re
W = '/home/dan/Desktop/micro-os-plu-iii-smp-pr-github'
A = f'{W}/step5c-check/after3b'; B = f'{W}/step6-check/after6b8'
PORTABLE = ('smp_test[0-4]|smp-num-test|smp-pipeline-test|smp-pro-cons-test|'
            'smp-mat-test|sd_test|smp-mat-sdcard-test|clock-test')
ALLOWED = re.compile(r'(' + PORTABLE + r')-(qemu|hwd|host)\b')
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
