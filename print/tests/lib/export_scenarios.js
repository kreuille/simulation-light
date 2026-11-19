#!/usr/bin/env node
/*
  Exports, for a set of simulator scenarios, the sampled traces
  (t, f brut, z, ph, ly) et le verdict attendu de l'apprentissage par couche,
  and the inputs/outputs of the stability. These files are replayed by the C++
  runner (parite.cpp) which must find exactly the same verdicts.

  Usage : node export_scenarios.js <dossier_de_sortie>

  Real numbers are written with toString() (shortest exact round trip), so that
  strtod() in C++ recovers the same double, and therefore the same thresholds.
*/
const fs = require('fs'), path = require('path'), vm = require('vm');

const html = fs.readFileSync(path.join(__dirname, '..', '..', 'simulateur', 'index.html'), 'utf8');
const code = html.match(/<script>([\s\S]*)<\/script>/)[1];
const mod = { exports: {} };
vm.runInNewContext(code, { module: mod, console, Math, JSON, Array, Object, Number, String, isFinite, parseFloat, Error, Set });
const m = mod.exports;

const nz = { motor: true, fan: true, mains: true, shock: true, therm: true, glitch: true };
const P = (o = {}) => Object.assign({
  machine: m.clone(m.MACHINES.m9), resin: m.clone(m.RESINS.std), proc: m.clone(m.PROC0), app: m.clone(m.APPS.fig),
  geom: m.GEOMS.fig.make(), scen: 'normal', L: 30, resT: 0.1, resD: 0.4, resOff: 0, temp: 25,
  noise: 1, rate: 80, median: true, shockRate: 1, nz, seed: 1
}, o);

// resFactor and detection nm, reproduced identically to runAll().
const resFactor = p => Math.max(1, (p.machine && p.machine.sensRes || 0.03) / 0.05);

// Exported scenarios (cover everything simulateur.test.js checks, and more).
const SCEN = [
  { nom: 'normal_m9', p: P({ scen: 'normal' }) },
  { nom: 'detach_m9', p: P({ scen: 'detach' }) },
  { nom: 'support_m9', p: P({ scen: 'support' }) },
  { nom: 'residue_m9', p: P({ scen: 'residue' }) },
  { nom: 'fep_m9', p: P({ scen: 'fep' }) },
  { nom: 'residue_rrs', p: P({ machine: m.clone(m.MACHINES.rrs), scen: 'residue' }) },
  { nom: 'normal_rrs', p: P({ machine: m.clone(m.MACHINES.rrs), scen: 'normal' }) },
  { nom: 'detach_c10', p: P({ machine: m.clone(m.MACHINES.c10), scen: 'detach' }) },
  { nom: 'residue_seed2', p: P({ scen: 'residue', seed: 2 }) },
  { nom: 'support_seed3', p: P({ scen: 'support', seed: 3 }) },
];

const out = process.argv[2] || '.';
const num = x => (x == null || !isFinite(x)) ? 'nan' : x.toString();
const arr = a => a.map(num).join(' ');

// key = detection/verdict key; serie = series key in r.s; stab = key in r.stab (null if none)
const CHAN = [
  { key: 'z', serie: 'zc', stab: 'z', avg: false },
  { key: 'vat', serie: 'vat', stab: 'vat', avg: true },
  { key: 'sg', serie: 'sg', stab: null, avg: true },
  { key: 'fus', serie: 'fus', stab: 'fus', avg: false },
];

for (const { nom, p } of SCEN) {
  const r = m.runAll(p);
  const n = p.geom.areas.length;
  const nmE = p.noise * resFactor(p);
  const L = [];
  L.push(`SCEN ${p.scen}`);
  L.push(`L ${p.L}`);
  L.push(`N ${n}`);
  L.push(`NOISE ${num(p.noise)}`);
  L.push(`NME ${num(nmE)}`);
  L.push(`RATE ${num(p.rate)}`);
  L.push(`WAIT ${num(p.proc.wait)}`);
  L.push(`TOPWAIT ${num(p.proc.topWait)}`);
  L.push(`MARGIN ${num(p.app.settleMargin)}`);
  L.push(`FSTALL ${num(p.machine.fstall)}`);
  L.push(`OBSTACLEMUL ${num(p.app.obstacleMul)}`);
  L.push(`LOWRATIO ${num(p.app.lowRatio)}`);
  L.push(`AREA ${arr(r.sim.truth.area)}`);
  L.push(`RESTSTART ${arr([...Array(n)].map((_, i) => r.sim.truth.restStart[i]))}`);
  L.push(`TOPSTART ${arr([...Array(n)].map((_, i) => r.sim.truth.topStart[i]))}`);
  L.push(`SETTLEDOWN ${arr([...Array(n)].map((_, i) => r.sim.truth.settleDown[i]))}`);
  L.push(`SETTLETOP ${arr([...Array(n)].map((_, i) => r.sim.truth.settleTop[i]))}`);

  for (const { key, serie, stab, avg } of CHAN) {
    const s = r.s[serie];
    const nmDet = key === 'sg' ? p.noise : nmE;
    const v = r.ver[key];
    L.push(`BEGIN_CHANNEL ${key} MED ${p.median ? 1 : 0} AVG ${avg ? 1 : 0} NMDET ${num(nmDet)}`);
    L.push(`VERDICT first ${v.first == null ? -1 : v.first} kind ${v.kind == null ? 'none' : v.kind} fa ${v.fa} nodata ${v.nodata} lat ${v.lat == null ? -1 : v.lat}`);
    // stability (sg has none)
    const st = stab ? r.stab[stab] : null;
    const sd = [], stp = [];
    for (let i = 0; i < n; i++) {
      const d = st ? st.down[i] : null, t = st ? st.top[i] : null;
      sd.push(d && d.found ? 1 : 0, d && d.found ? num(d.ts) : '0');
      stp.push(t && t.found ? 1 : 0, t && t.found ? num(t.ts) : '0');
    }
    L.push(`STABDOWN ${sd.join(' ')}`);
    L.push(`STABTOP ${stp.join(' ')}`);
    L.push(`SAMPLES ${s.t.length}`);
    const rows = new Array(s.t.length);
    for (let k = 0; k < s.t.length; k++) rows[k] = `${num(s.t[k])} ${num(s.f[k])} ${num(s.z[k])} ${s.ph[k]} ${s.ly[k]}`;
    L.push(rows.join('\n'));
    L.push('END_CHANNEL');
  }
  fs.writeFileSync(path.join(out, `${nom}.txt`), L.join('\n') + '\n');
}
// List of scenarios for the runner.
fs.writeFileSync(path.join(out, 'scenarios.txt'), SCEN.map(s => s.nom).join('\n') + '\n');
console.log(`${SCEN.length} scenarios exported to ${out}`);
