#!/usr/bin/env python3
"""Génère des enregistrements synthétiques du test de contrôle du bac (plateau qui descend à 1 mm/s
jusqu'à 0,05 mm, maintien 5 s, couche noire, remontée), avec ou sans résidu."""
import sys
import numpy as np

def gen(fn, thick, diam, pos=(106, 59), noise=1.0, seed=0, level=0.0):
    rng = np.random.default_rng(seed); W, H = 200, 125; Ap = 140; kc = 60 * diam; dt = 0.0125
    rows = []; t = 0.0; z = 10.0
    def force(z, phase, h=0.0):
        if phase == "desc": sq = -0.011 * Ap * (1 / 3) / ((max(z, 0.05) + 0.15) ** 2)
        elif phase == "hold": sq = -0.011 * Ap * (1 / 3) / ((0.05 + 0.15) ** 2) * np.exp(-h / 0.94)
        else: sq = 0.0
        return sq, -(kc * (thick - z) if thick > z else 0.0)
    def emit(t, z, sq, fc, uv):
        u = pos[0] / W; v = pos[1] / H; w = [(1-u)*(1-v), u*(1-v), u*v, (1-u)*v]
        fz = sq + fc - 0.15 * level + rng.normal(0, 0.04 * noise)
        cs = [sq / 4 + fc * w[i] - 0.15 * level / 4 + rng.normal(0, 0.15 * noise) for i in range(4)]
        rows.append(f"{t:.4f},{fz:.3f},{cs[0]:.3f},{cs[1]:.3f},{cs[2]:.3f},{cs[3]:.3f},{z:.4f},{uv},0")
    for _ in range(80): emit(t, z, 0, 0, 0); t += dt
    while z > 0.05:
        sq, fc = force(z, "desc"); emit(t, z, sq, fc, 0); t += dt; z -= 1.0 * dt
    z = 0.05; h = 0.0
    while h < 5: sq, fc = force(z, "hold", h); emit(t, z, sq, fc, 0); t += dt; h += dt
    for _ in range(160): sq, fc = force(z, "hold", h); emit(t, z, sq, fc, 1); t += dt; h += dt
    for _ in range(200): z += 0.05; emit(t, z, 0, 0, 0); t += dt
    open(fn, "w").write("t_s,fz_N,c0_N,c1_N,c2_N,c3_N,z_mm,uv,uv_raw\n" + "\n".join(rows) + "\n")

d = sys.argv[1] if len(sys.argv) > 1 else "."
gen(f"{d}/a_ref.csv", 0, 0.4, seed=1)
gen(f"{d}/b_propre.csv", 0, 0.4, seed=2)
gen(f"{d}/c_100um.csv", 0.1, 0.4, seed=3)
gen(f"{d}/d_100um_bord.csv", 0.1, 0.4, pos=(164, 98), seed=4)
gen(f"{d}/e_300um.csv", 0.3, 2, seed=5)
gen(f"{d}/f_60um.csv", 0.06, 0.4, seed=6)
gen(f"{d}/g_propre_niveau+2.csv", 0, 0.4, seed=7, level=2)
