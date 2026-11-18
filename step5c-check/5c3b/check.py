# check.py: 5c-3b command check, after3b/ against after3a/ (5c-3a).
# A changed command may only be for a storage test's or a portable test's
# target; ctest -N may only gain smp-shapes-test's QEMU card fixtures (rpi4b).
import os, re
S = '/home/dan/Desktop/micro-os-plu-iii-smp-pr-github/step5c-check'
APPS = ('sd_test|smp-mat-sdcard-test|smp-num-test|smp-pipeline-test|smp-shapes-test|'
        'usb_test|usb_big_test|eth-net|eth-tcp-server|wifi-net|wifi-tcp-server|'
        'smp_test5|smp_test7|smp_test_int4|smp_test[0-4]|smp-pro-cons-test|smp-mat-test')
ALLOWED = re.compile(r'(' + APPS + r')-(qemu|hwd|host)\b')
FIXT = re.compile(r'aarch64-rpi4b-smp-shapes-test-qemu-sd-(make|remove)$')
bad = 0
for f in sorted(os.listdir(f'{S}/after3a')):
    if not f.endswith('.cmds'):
        continue
    C = f[:-5]
    b = set(open(f'{S}/after3a/{f}').read().splitlines())
    a = set(open(f'{S}/after3b/{f}').read().splitlines())
    other = [l for l in (a ^ b) if not ALLOWED.search(l)]
    tb = set(open(f'{S}/after3a/{C}.ctestN').read().split('\n'))
    ta = set(open(f'{S}/after3b/{C}.ctestN').read().split('\n'))
    tother = [t for t in (ta ^ tb) if not FIXT.search(t)]
    print(f'{C}: changed={len(a ^ b)} outside={len(other)} ctestN-changes={len(ta ^ tb)} unexpected={len(tother)}')
    if other or tother:
        bad += 1
        for l in (other + tother)[:5]:
            print('   ', l[:300])
print('configs with unexpected changes:', bad)
