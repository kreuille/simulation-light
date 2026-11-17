// Tests du modèle du simulateur : node print/tests/simulateur.test.js
const fs = require('fs'), path = require('path'), assert = require('assert'), vm = require('vm');
const html = fs.readFileSync(path.join(__dirname, '..', 'simulateur', 'index.html'), 'utf8');
const code = html.match(/<script>([\s\S]*)<\/script>/)[1];
const mod = { exports: {} };
vm.runInNewContext(code, { module: mod, console, Math, JSON, Array, Object, Number, String, isFinite, parseFloat, Error, Set });
const m = mod.exports;
const nz = { motor: true, fan: true, mains: true, shock: true, therm: true, glitch: true };
const P = (o = {}) => Object.assign({ machine: m.clone(m.MACHINES.m9), resin: m.clone(m.RESINS.std), proc: m.clone(m.PROC0), app: m.clone(m.APPS.fig),
  geom: m.GEOMS.fig.make(), scen: 'normal', L: 30, resT: 0.1, resD: 0.4, resOff: 0, temp: 25, noise: 1, rate: 80, median: true, shockRate: 1, nz, seed: 1 }, o);
let n = 0; const ok = (c, msg) => { assert.ok(c, msg); n++; };

// pannes simulées : la cellule Z détecte à la couche de l'incident, sans fausse alerte
let r = m.runAll(P());
ok(Object.values(r.ver).every(v => v.fa === 0), 'impression normale sans fausse alerte');
for (const scen of ['detach', 'support', 'residue']) {
  r = m.runAll(P({ scen }));
  ok(r.ver.z.first === 30 && r.ver.z.fa === 0, `cellule Z détecte « ${scen} » à la couche 30`);
}
r = m.runAll(P({ scen: 'fep' }));
ok(r.ver.z.first !== null && r.ver.z.lat <= 5, 'film détendu détecté en moins de 5 couches');

// contrôle du bac avant impression
const ck = (o) => m.residueCheck(P(), Object.assign({ t: 0.1, d: 0.4, pos: 'centre', v: 60, zs: 0.05, hold: 5 }, o));
ok(ck({ t: 0 }).risk === 'ok', 'bac propre');
ok(ck({}).det.z.seen && ck({}).risk === 'modere', 'résidu de 100 µm détecté');
ok(!ck({ t: 0.06 }).det.z.seen, 'résidu de 60 µm sous le seuil');
ok(ck({ t: 0.3, d: 2 }).risk === 'haut', 'résidu de 300 µm : risque élevé');
ok(ck({ t: 1, d: 3 }).risk === 'danger', 'débris de 1 mm : danger');
ok(ck({ pos: 'hors' }).risk === 'ok', 'résidu hors du plateau invisible');

// recalage sur l'enregistrement d'exemple (paramètres cachés)
const F = m.fitModel(m.parseRecording(m.exampleRecording(P())), P());
ok(F.n === 60, '60 couches reconnues');
ok(Math.abs(F.fit.adh.val - 1.35) < 0.08, 'adhérence retrouvée');
ok(Math.abs(F.fit.fepK.val - 21) < 1.5, 'raideur du film retrouvée');
ok(Math.abs(F.fit.squeezeTau.val - 1.7) < 0.15, 'écoulement retrouvé');
ok(Math.abs(F.fit.wait.val - 1.2) < 0.1, 'attente avant exposition retrouvée');
console.log(`${n} tests du simulateur passent.`);
