# Checks the Fusion 360 model without Fusion: no part overlaps, at several platform heights,
# et le berceau reste au-dessus de 100 Hz.  python3 print/tests/test_fusion_geometrie.py
import importlib.util, itertools, os, sys
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('m', os.path.join(here, '..', 'conception', 'fusion360', 'MSLA_10pouces', 'MSLA_10pouces.py'))
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)

def overlap(a, b, n=9, eps=0.02):
    A, B = m.bbox(a['shape']), m.bbox(b['shape'])
    lo = [max(A[0], B[0]) + eps, max(A[2], B[2]) + eps, max(A[4], B[4]) + eps]
    hi = [min(A[1], B[1]) - eps, min(A[3], B[3]) - eps, min(A[5], B[5]) - eps]
    if any(h <= l for l, h in zip(lo, hi)):
        return False
    for i, j, k in itertools.product(range(n + 1), repeat=3):
        x, y, z = (lo[0] + (hi[0] - lo[0]) * i / n, lo[1] + (hi[1] - lo[1]) * j / n, lo[2] + (hi[2] - lo[2]) * k / n)
        if m.solid(a, x, y, z) and m.solid(b, x, y, z):
            return (round(x, 1), round(y, 1), round(z, 1))
    return False

bad = 0
for zp in (0, 40, 125, m.TRAVEL):
    P = m.parts(zp)
    for a, b in itertools.combinations(P, 2):
        o = overlap(a, b)
        if o:
            bad += 1; print(f'z={zp}: {a["name"]} / {b["name"]} se chevauchent vers {o}')
P = m.parts(0)
mass = sum(m.volume_cm3(p) * m.RHO[p['mat']] for p in P if p['comp'] in ('Berceau', 'Bac'))
m0, m1, f0, f1 = m.cradle_report(mass)
top = max(m.bbox(p['shape'])[5] for p in P)
print(f'{len(P)} parts, {len({p["comp"] for p in P})} components, height {top:.0f} mm')
print(f'cradle {m0:.2f} kg empty, {m1:.2f} kg full ; {f0:.0f} Hz / {f1:.0f} Hz')
assert bad == 0, f'{bad} chevauchements'
assert f1 > 100, 'natural frequency too low'
print('Fusion 360 model geometry: no overlap.')
