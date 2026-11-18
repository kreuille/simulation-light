#!/usr/bin/env node
/*
  Robustesse de la détection au bruit du signal.

  Fait varier le niveau de bruit du capteur et, pour chaque niveau, mesure sur
  plusieurs tirages (seeds) :
    - sur une impression NORMALE : le nombre de fausses alertes ;
    - sur les pannes (décollement, supports, résidu, film) : le taux de détection
      et la latence (couches après l'incident, couche 30).

  Le bruit est injecté par le modèle du simulateur (bruit HX711, 50 Hz secteur,
  vibrations moteur, dérive thermique, pics). Le seuil de détection s'adapte au
  bruit mesuré (nm ∝ bruit), comme après un étalonnage sur le banc réel. La
  détection de la bibliothèque C++ étant identique au simulateur (parité), ces
  chiffres valent pour le firmware.

  Usage : node noise_sweep.js
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

console.log('=== Robustesse de la détection au bruit (machine 9″, capteur 0,03 N, 80 SPS) ===');
console.log(`Tirages par niveau : ${SEEDS}. Panne injectée à la couche 30.`);
console.log('');
console.log('bruit | normal: fausses alertes      | décollement | supports | résidu | film détendu');
console.log('      | total / tirages touchés      | détect. (lat) …');
console.log('-'.repeat(92));
const fmt = (p) => p.det === 0 ? '  raté  ' : `${Math.round(100 * p.det / SEEDS)}% (${(p.latSum / p.det).toFixed(1)})`;
for (const noise of NIVEAUX) {
  const r = mesure(noise);
  console.log(
    `${noise.toString().padStart(5)} | ${r.faTot.toString().padStart(3)} sur ${SEEDS} tirages, ${r.faSeeds} touché(s)`.padEnd(46) +
    `| ${fmt(r.pann.detach).padEnd(11)} | ${fmt(r.pann.support).padEnd(8)} | ${fmt(r.pann.residue).padEnd(6)} | ${fmt(r.pann.fep)}`
  );
}
console.log('-'.repeat(92));
console.log('Lecture : « 100% (0.4) » = panne détectée dans tous les tirages, 0,4 couche après l\'incident en moyenne.');
console.log('Le bruit est un multiplicateur : 1 = niveau nominal du capteur, 5 = capteur 5× plus bruyant.');
