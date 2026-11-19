#!/usr/bin/env node
/*
  Detection robustness to signal noise.

  Varies the sensor noise level and, for each level, measures over several draws (seeds):
    - on a NORMAL print: the number of false alarms;
    - on the faults (detachment, supports, debris, film): the detection rate and the
      latency (layers after the incident, layer 30).

  The noise is injected by the simulator model (HX711 noise, 50 Hz mains, motor
  vibration, thermal drift, spikes). The detection threshold adapts to the measured
  noise (nm proportional to noise), like after a calibration on the real bench. Since the
  C++ library detection is identical to the simulator (parity), these numbers apply to
  the firmware.

  Usage: node noise_sweep.js
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

const NIVEAUX = [0.5, 1, 2, 4, 6, 8];
const SEEDS = 6;
const PANNES = ['detach', 'support', 'residue', 'fep'];

function mesure(noise) {
  let faTot = 0, faSeeds = 0;
  const pann = {}; for (const s of PANNES) pann[s] = { det: 0, latSum: 0 };
  for (let seed = 1; seed <= SEEDS; seed++) {
    const vn = m.runAll(P({ scen: 'normal', noise, seed })).ver.z;
    faTot += vn.fa; if (vn.fa > 0) faSeeds++;
    for (const scen of PANNES) {
      const v = m.runAll(P({ scen, noise, seed })).ver.z;
      if (v.first !== null) { pann[scen].det++; pann[scen].latSum += v.lat; }
    }
  }
  return { faTot, faSeeds, pann };
}

console.log('=== Detection robustness to noise (9" machine, 0.03 N sensor, 80 SPS) ===');
console.log(`Draws per level: ${SEEDS}. Fault injected at layer 30.`);
console.log('');
console.log('noise | normal: false alarms         | detachment  | supports | debris | loose film');
console.log('      | total / affected draws       | detect. (lat) ...');
console.log('-'.repeat(92));
const fmt = (p) => p.det === 0 ? '  miss  ' : `${Math.round(100 * p.det / SEEDS)}% (${(p.latSum / p.det).toFixed(1)})`;
for (const noise of NIVEAUX) {
  const r = mesure(noise);
  console.log(
    `${noise.toString().padStart(5)} | ${r.faTot.toString().padStart(3)} over ${SEEDS} draws, ${r.faSeeds} affected`.padEnd(46) +
    `| ${fmt(r.pann.detach).padEnd(11)} | ${fmt(r.pann.support).padEnd(8)} | ${fmt(r.pann.residue).padEnd(6)} | ${fmt(r.pann.fep)}`
  );
}
console.log('-'.repeat(92));
console.log('Read: "100% (0.4)" = fault detected in every draw, 0.4 layer after the incident on average.');
console.log('Noise is a multiplier: 1 = nominal sensor level, 5 = sensor 5x noisier.');
