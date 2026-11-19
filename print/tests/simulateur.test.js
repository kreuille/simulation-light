// Tests of the simulator model: node print/tests/simulateur.test.js
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

// simulated faults: the Z cell detects at the incident layer, with no false alarm
let r = m.runAll(P());
ok(Object.values(r.ver).every(v => v.fa === 0), 'normal print with no false alarm');
for (const scen of ['detach', 'support', 'residue']) {
  r = m.runAll(P({ scen }));
  ok(r.ver.z.first === 30 && r.ver.z.fa === 0, `Z cell detects "${scen}" at layer 30`);
}
r = m.runAll(P({ scen: 'fep' }));
ok(r.ver.z.first !== null && r.ver.z.lat <= 5, 'loose film detected in less than 5 layers');

// vat control before a print
const ck = (o) => m.residueCheck(P(), Object.assign({ t: 0.1, d: 0.4, pos: 'centre', v: 60, zs: 0.05, hold: 5 }, o));
ok(ck({ t: 0 }).risk === 'ok', 'clean vat');
ok(ck({}).det.z.seen && ck({}).risk === 'modere', '100 um debris detected');
ok(!ck({ t: 0.06 }).det.z.seen, '60 um debris below the threshold');
ok(ck({ t: 0.3, d: 2 }).risk === 'haut', '300 um debris: high risk');
ok(ck({ t: 1, d: 3 }).risk === 'danger', '1 mm debris: danger');
ok(ck({ pos: 'hors' }).risk === 'ok', 'debris out of the platform is invisible');

// fit on the example record (hidden parameters)
const F = m.fitModel(m.parseRecording(m.exampleRecording(P())), P());
ok(F.n === 60, '60 layers recognized');
ok(Math.abs(F.fit.adh.val - 1.35) < 0.08, 'adhesion recovered');
ok(Math.abs(F.fit.fepK.val - 21) < 1.5, 'film stiffness recovered');
ok(Math.abs(F.fit.squeezeTau.val - 1.7) < 0.15, 'flow recovered');
ok(Math.abs(F.fit.wait.val - 1.2) < 0.1, 'wait before exposure recovered');
console.log(`${n} simulator tests pass.`);

// HeyGears-type profiles and Z arm + vat fusion
{
  const rrs = m.runAll(P({ machine: m.clone(m.MACHINES.rrs), scen: 'residue' }));
  ok(rrs.ver.fus.first === 30 && rrs.ver.fus.fa === 0, 'fusion detects the debris (Reflex RS type)');
  const base = m.runAll(P({ machine: m.clone(m.MACHINES.rf2) }));
  const pm = m.clone(m.MACHINES.rf2); pm.pulse = 'oui';
  const vib = m.runAll(P({ machine: pm }));
  ok(vib.feats.tr[30].peak < 0.6 * base.feats.tr[30].peak, 'vibration-assisted release: peak halved');
  console.log(`${n} simulator tests pass (with Reflex-type profiles).`);
}

// vat, resin and planarity (4 cells below the vat)
{
  const vc = (o) => m.vatCheck(P(), Object.assign({ state: 'ok', resin: 400, leak: 0 }, o));
  ok(!vc({ state: 'absent' }).present, 'absent vat detected');
  ok(vc({ state: 'leve' }).lifted === 1, 'front-right corner badly seated identified');
  ok(vc({ state: 'decale' }).shifted, 'shifted vat detected');
  const r = vc({});
  ok(Math.abs(r.resinEst - 400) <= r.resinUnc, 'resin quantity estimated within the stated margin');
  ok(vc({ leak: 40 }).leakSeen, '40 ml/h leak detected');
  const pl = m.planarityTest(P(), { tilt: 0.1, dir: 1, v: 30 });
  ok(Math.abs(pl.est.th - 0.1) <= 0.02 && pl.est.d === 1, '0.1 deg tilt toward the front-right recovered');
  ok(m.planarityTest(P(), { tilt: 0, dir: 0, v: 30 }).est.th <= 0.02, 'parallel platform recognized');
  console.log(`${n} simulator tests pass (with vat, resin and planarity).`);
}
