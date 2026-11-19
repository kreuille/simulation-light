// Tests du modèle et des traductions du simulateur : node light-engine/tests/simulateur.test.js
// Le simulateur est une page HTML. On exécute ses scripts dans Node avec un faux DOM (aucune dépendance) :
// le script applicatif exporte son modèle quand il détecte `module`, sans lancer l'interface.
const fs = require('fs'), path = require('path'), assert = require('assert'), vm = require('vm');

const html = fs.readFileSync(path.join(__dirname, '..', 'simulateur', 'index.html'), 'utf8');
const scripts = [...html.matchAll(/<script[^>]*>([\s\S]*?)<\/script>/g)].map(m => m[1]);
assert.strictEqual(scripts.length, 2, 'deux scripts attendus : données (aide, traductions) puis application');

// faux DOM : tout accès renvoie un objet inerte
const stub = () => new Proxy(function () {}, {
  get: (t, k) => (k === Symbol.toPrimitive ? () => 0 : typeof k === 'symbol' ? undefined : k === 'length' ? 0 : stub()),
  set: () => true, apply: () => stub(), construct: () => stub(),
});
const mod = { exports: {} };
const win = { innerWidth: 1280, innerHeight: 900, addEventListener() {}, matchMedia: () => ({ matches: false, addEventListener() {} }) };
const ctx = {
  module: mod, console, window: win, document: stub(), navigator: { language: 'en-US' },
  localStorage: { getItem: () => null, setItem() {} }, getComputedStyle: () => ({ getPropertyValue: () => '#000' }),
  requestAnimationFrame() {}, setTimeout() {}, clearTimeout() {}, performance: { now: () => 0 },
  MutationObserver: function () { this.observe = () => {}; }, URL: { createObjectURL: () => '' },
};
vm.createContext(ctx);
vm.runInContext(scripts[0], ctx);                 // HELP et dictionnaire I18N (variables globales de la page)
vm.runInContext('var __data={HELP:HELP,I18N:I18N};', ctx);
vm.runInContext(scripts[1], ctx);                 // application
const m = mod.exports, data = ctx.__data;
assert.ok(m.P && m.sigmaOf, 'le modèle doit être exporté');

let n = 0;
const ok = (c, msg) => { assert.ok(c, msg); n++; };
const near = (a, b, tol, msg) => ok(Math.abs(a - b) <= tol, `${msg} (obtenu ${a}, attendu ${b} ± ${tol})`);
const withP = (o, fn) => { const old = {}; for (const k in o) { old[k] = m.P[k]; m.P[k] = o[k]; } try { return fn(); } finally { Object.assign(m.P, old); } };

/* ---------- Optique : cône, diffusion, flou ---------- */
withP({ gap: 2, div: 10, dmax: 10, wl: 405, blocker: 0.3 }, () => {
  const ph = m.physics(50);
  near(ph.R, 2000 * Math.tan(10 * Math.PI / 180) / 50, 1e-9, 'rayon du cône = jeu × tan(angle) / pas de pixel');
});
withP({ gap: 2, div: 10, dmax: 3, wl: 405, blocker: 0.3 }, () => {
  near(m.physics(50).R, 2000 * Math.tan(3 * Math.PI / 180) / 50, 1e-9, "l'angle est limité par l'angle maximal admis");
});
withP({ div: 10, dmax: 10 }, () => near(m.keepOf(), 1, 1e-12, 'aucune perte de lumière si l’angle est admis'));
withP({ div: 20, dmax: 5 }, () => ok(m.keepOf() > 0 && m.keepOf() < 1, 'une partie de la lumière est perdue si l’angle dépasse le maximum'));
withP({ wl: 405, blocker: 0.3, div: 5, dmax: 5 }, () => {
  const s = g => withP({ gap: g }, () => m.physics(50).sg);
  ok(s(0.5) < s(1) && s(1) < s(2) && s(2) < s(4), 'la diffusion augmente avec le jeu écran-résine');
});
withP({ gap: 0, wl: 405, div: 5, dmax: 5 }, () => {
  const s = b => withP({ blocker: b }, () => m.physics(50).sg);
  ok(s(0) > s(0.3) && s(0.3) > s(1), 'un photobloqueur réduit la diffusion dans la résine');
});
withP({ gap: 2, wl: 405, blocker: 0.3, div: 5, dmax: 5 }, () => {
  const blue = m.physics(50).sg, uv = withP({ wl: 365 }, () => m.physics(50).sg);
  ok(uv > blue, 'la diffusion de Rayleigh est plus forte à 365 nm qu’à 405 nm (diffusion en λ⁻⁴ dominante avec un jeu de 2 mm)');
});
withP({ blocker: 0.3, wl: 405, dlpS: 12, pitchD: 50, pitchL: 50, gap: 2, div: 5, dmax: 5 }, () => {
  const d = m.physicsDLP(), l = m.physics(50);
  ok(d.R === 0, 'le DLP n’a pas de cône de lumière');
  ok(d.sg < l.sg + l.R, 'à pas égal, le flou du DLP est plus faible que celui du LCD (jeu 2 mm)');
});
ok(m.transmission(365) === 0.25 && m.transmission(405) === 1 && m.transmission(450) === 1, 'transmission de l’écran LCD aux bornes');
ok(m.transmission(385) < m.transmission(395) && m.transmission(395) < m.transmission(405), 'transmission croissante entre 385 et 405 nm');

/* ---------- Résine : profondeur durcie ---------- */
withP({ wl: 405, blocker: 0, dose: Math.E }, () => { const r = m.resin(); near(r.Dp, 250, 1e-9, 'Dp = 250 µm à 405 nm sans photobloqueur'); near(r.Cd, r.Dp, 1e-9, 'Cd = Dp·ln(E0/Ec) vaut Dp pour une dose e'); });
withP({ wl: 405, blocker: 0.3 }, () => {
  const cd = d => withP({ dose: d }, () => m.resin().Cd);
  ok(cd(1) === 0 && cd(2) < cd(3) && cd(3) < cd(5), 'la profondeur durcie croît avec la dose, et vaut 0 à la dose seuil');
});
withP({ wl: 405, dose: 2.3 }, () => {
  const dp = b => withP({ blocker: b }, () => m.resin().Dp);
  ok(dp(0) > dp(0.3) && dp(0.3) > dp(1), 'le photobloqueur réduit la profondeur de pénétration');
});

/* ---------- Fonctions mathématiques ---------- */
near(m.erf(0), 0, 1e-9, 'erf(0)'); near(m.erf(1), 0.8427, 2e-4, 'erf(1)'); near(m.erf(-1), -m.erf(1), 1e-12, 'erf impaire');
near(m.Phi(0), 0.5, 1e-9, 'Φ(0)'); near(m.Phi(1.96), 0.975, 1e-3, 'Φ(1,96)');

/* ---------- Compensation par apprentissage (table mesurée) ---------- */
const REC = m.REC;
ok(Array.isArray(REC) && REC.length >= 8, 'table de récupérabilité présente');
ok(REC.every((r, i) => i === 0 || r[0] > REC[i - 1][0]), 'table triée par flou croissant');
ok(REC.every(r => r[2] <= r[1] + 1e-9), 'la correction optimale n’est jamais pire que l’absence de correction');
const row = s => REC.find(r => r[0] === s);
near(row(1)[1], 69.5, 1e-9, 'σ = 1 px sans correction : 69,5 µm RMS'); near(row(1)[2], 10.3, 1e-9, 'σ = 1 px correction optimale : 10,3 µm');
ok(row(3)[2] < row(3)[1] / 2, 'à σ = 3 px la correction optimale divise l’erreur par plus de 2');
ok(REC[REC.length - 1][1] > REC[0][1], 'l’erreur sans correction croît avec le flou');
withP({ mlq: 0 }, () => near(m.mlFactor(1), 1, 1e-12, 'qualité ML 0 % : aucun gain'));
withP({ mlq: 100 }, () => near(m.mlFactor(1), 10.3 / 69.5, 1e-9, 'qualité ML 100 % : gain mesuré (σ = 1 px)'));
withP({ mlq: 60 }, () => { const f = m.mlFactor(2); ok(f < 1 && f > 0, 'qualité ML intermédiaire : gain partiel'); });

/* ---------- Verre ajouté sur l'écran et comparaison A/B ---------- */
const base = { gap: 1.8, glass: 0, gn: 1.52, gs: 0.3, div: 8, wl: 405, blocker: 0.3 };
withP({ dmax: 20 }, () => {
  const a = m.physicsOf(base, 50), g = m.physicsOf(Object.assign({}, base, { glass: 1.1 }), 50);
  near(a.R, 1800 * Math.tan(8 * Math.PI / 180) / 50, 1e-9, 'sans verre : rayon du cône inchangé');
  near(g.R - a.R, 1100 * Math.tan(Math.asin(Math.sin(8 * Math.PI / 180) / 1.52)) / 50, 1e-9, 'le verre ajoute un cône réduit par la réfraction');
  ok(g.R > a.R && g.sg > a.sg, 'avec verre : cône plus large et diffusion plus forte');
  const hi = m.physicsOf(Object.assign({}, base, { glass: 1.1, gn: 1.9 }), 50), lo = m.physicsOf(Object.assign({}, base, { glass: 1.1, gn: 1.4 }), 50);
  ok(hi.R < lo.R, 'un indice plus élevé redresse les rayons : cône plus étroit');
  const nos = m.physicsOf(Object.assign({}, base, { glass: 1.1, gs: 0 }), 50);
  near(nos.sg, a.sg, 1e-12, 'verre sans diffusion : même diffusion que sans verre');
  withP({ glass: 0, gap: 1.8, div: 8, wl: 405, blocker: 0.3 }, () => near(m.physics(50).R, a.R, 1e-12, 'physics() sans verre = ancien modèle'));
  withP({ glass: 2, glassN: 1.52, glassS: 0.3, gap: 1.8, div: 8, wl: 405, blocker: 0.3 }, () => ok(m.physics(50).R > a.R, 'physics() tient compte du verre principal'));
});
ok(m.abEdge(0, 0) === 0, 'flou de bord nul sans cône ni diffusion');
ok(m.abEdge(100, 20) < m.abEdge(200, 20) && m.abEdge(200, 20) < m.abEdge(200, 80), 'le flou de bord croît avec le cône et la diffusion');
near(m.abEdge(0, 50), 2 * 1.2816 * 50, 4, 'flou de bord d’une gaussienne seule : 2,56 σ');
m.abExample();
ok(m.AB.A.glass === 0 && m.AB.B.glass > 0, 'exemple A/B : sans verre contre avec verre');
m.AB.n = 3;
m.setPart('dent');
withP({ pitchL: 50, dmax: 20, tol: 50 }, () => {
  const half = 700, ma = m.abMap(m.AB.A, half), mb = m.abMap(m.AB.B, half);
  let sum = 0; for (const v of ma.Z) sum += v;
  near(sum * ma.cell * ma.cell / (150 * 150), 1, 0.03, 'l’énergie du pixel est conservée (bloc 3 × 3)');
  const sa = m.abStats(ma, m.AB.A.dose), sb = m.abStats(mb, m.AB.B.dose);
  ok(sa.peak <= 1 + 1e-9 && sb.peak <= sa.peak, 'intensité au centre ≤ 1, plus faible avec le verre');
  ok(sb.edge > sa.edge && sb.out > sa.out, 'avec verre : bord plus flou et plus de lumière hors de la zone');
  const pa = m.abPrint(m.AB.A), pb = m.abPrint(m.AB.B);
  ok(pa.res.cured.length === m.N * m.N, 'motif imprimé calculé');
  ok(pb.m.err >= pa.m.err - 0.2, 'avec verre, le motif n’est pas plus précis que sans verre');
  const same = m.abPrint(m.AB.A);
  ok(same.m.err === pa.m.err, 'calcul déterministe');
});
m.AB.n = 1;

/* ---------- Impact de la collimation et machines du marché ---------- */
{
  const a = m.collPhys(1, 20, 1.0), b = m.collPhys(5, 20, 1.0), c = m.collPhys(10, 20, 1.0);
  ok(a.edge < b.edge && b.edge < c.edge, 'le flou de bord croît avec la divergence');
  ok(m.collPhys(5, 20, 0.4).edge < m.collPhys(5, 20, 2.0).edge, 'le flou de bord croît avec la distance');
  near(m.collPhys(20, 5, 1.0).R, m.collPhys(5, 20, 1.0).R, 1e-9, 'une grille de 5° limite le cône comme une source de 5°');
  ok(m.collPhys(20, 20, 0.4).edge - m.collPhys(0.2, 20, 0.4).edge < m.collPhys(20, 20, 2.0).edge - m.collPhys(0.2, 20, 2.0).edge, 'la collimation compte davantage quand la distance est grande');
  ok(m.MACH.length === 3 && m.MACH.every(x => x.n.includes('Fresnel') && x.pitch >= 19 && x.pitch <= 34 && x.wl === 405 && x.div <= 5 && x.gap <= 0.5), 'trois préréglages à lentille de Fresnel, pas de 19 à 34 µm, 405 nm, divergence ≤ 5°');
  ok(m.MACH.some(x => x.pitch === 34), 'un préréglage à 34 µm (hypothèse)');
  ok(m.MACH.every(x => x.pub && x.est), 'chaque machine distingue valeurs publiées et valeurs estimées');
}

/* ---------- Préréglages ajustables : pas propre à chaque côté A / B ---------- */
{
  const base2 = { pitch: 34, gap: 0.35, glass: 0, gn: 1.52, gs: 0.3, div: 2, wl: 405, blocker: 0.3, dose: 2.3 };
  ok(m.abFromMain().pitch === m.P.pitchL, 'abFromMain reprend le pas des pixels');
  m.setPart('dent');
  withP({ dmax: 20, tol: 50 }, () => {
    const p34 = m.abPrint(base2), p21 = m.abPrint(Object.assign({}, base2, { pitch: 21 }));
    ok(p34.pitch === 34 && p21.pitch === 21, 'chaque côté imprime avec son propre pas');
    const m34 = m.abMap(base2, 600), m21 = m.abMap(Object.assign({}, base2, { pitch: 21 }), 600);
    ok(m34.pt === 34 && m21.pt === 21, 'la carte du pixel utilise le pas du côté');
  });
}

/* ---------- Lentille de Fresnel : tolérances de positionnement ---------- */
{
  const p = { frF: 250, frS: 20, frR: 125, frDz: 2, frDx: 1, frWarp: 1, frRough: 0.3 };
  const r = m.fresnelModel(p, 0.35);
  near(r.tsrc, Math.atan(10 / 250) * 180 / Math.PI, 1e-9, 'divergence de la source = atan((taille/2) / focale)');
  near(r.tdef, 125 * 2 / 62500 * 180 / Math.PI, 1e-9, 'inclinaison due à la défocalisation = rayon × écart / focale²');
  near(r.shDef, 350 * Math.tan(0.004), 1e-9, 'décalage = distance × tan(inclinaison)');
  near(m.fresnelModel(p, 1.5).shDef / r.shDef, 1.5 / 0.35, 1e-6, 'le décalage croît proportionnellement à la distance écran → résine');
  ok(m.fresnelModel(Object.assign({}, p, { frDz: 0, frDx: 0, frWarp: 0, frRough: 0 }), 0.35).shTot === 0, 'sans erreur de positionnement, aucun décalage');
  ok(m.fresnelModel(Object.assign({}, p, { frRough: 1 }), 0.35).tsp > r.tsp, 'la rugosité des facettes élargit le cône');
  near(m.fresnelModel(Object.assign({}, p, { frDz: 8 }), 0.35).tsp, r.tsp, 1e-12, 'la défocalisation seule n’élargit pas le cône (premier ordre)');
  ok(m.fresnelModel(Object.assign({}, p, { frF: 500 }), 0.35).tsrc < r.tsrc, 'une focale plus longue réduit la divergence de la source');
}

/* ---------- Modèle 3D synthétique ---------- */
const mesh = m.genDentalMesh('arch');
ok(mesh && mesh.pos.length > 3000 && mesh.idx.length % 3 === 0, 'arcade dentaire synthétique générée (' + mesh.idx.length / 3 + ' triangles)');
ok(mesh.pos.every(Number.isFinite), 'sommets finis');
const geo = m.buildGeoSync(mesh, 0);
ok(geo.V === mesh.pos.length / 3 && geo.T === mesh.idx.length / 3 && geo.nSl > 10, 'géométrie 3D construite (sommets, triangles, couches)');
ok(Math.max(...geo.A) > 100, 'aire de coupe maximale > 100 mm² (' + Math.max(...geo.A).toFixed(0) + ' mm²)');
ok(geo.bb.z1 > 5 && geo.bb.z1 < 40, 'hauteur de l’arcade plausible (5 à 40 mm)');
ok(geo.bb.x1 - geo.bb.x0 > 30 && geo.bb.x1 - geo.bb.x0 < 90, 'largeur de l’arcade plausible (30 à 90 mm)');

/* ---------- Traductions ---------- */
const { HELP, I18N } = data;
const keysEn = Object.keys(I18N.en), keysZh = Object.keys(I18N.zh);
ok(keysEn.length > 400, 'dictionnaire : plus de 400 phrases (' + keysEn.length + ')');
ok(keysEn.length === keysZh.length && keysEn.every(k => k in I18N.zh), 'anglais et chinois contiennent les mêmes phrases');
ok(keysEn.every(k => I18N.en[k] && I18N.zh[k]), 'aucune traduction vide');
const ph = s => (String(s).match(/\{\w+\}/g) || []).sort().join();
ok(keysEn.every(k => ph(k) === ph(I18N.en[k]) && ph(k) === ph(I18N.zh[k])), 'les paramètres {0}, {1}… sont conservés dans chaque langue');
const hk = Object.keys(HELP);
ok(hk.length >= 45, 'aide : au moins 45 paramètres expliqués (' + hk.length + ')');
ok(hk.every(k => ['fr', 'en', 'zh'].every(l => HELP[k][l] && HELP[k][l].length > (l === 'zh' ? 8 : 20))), 'chaque aide existe en français, anglais et chinois');
ok(hk.every(k => /[一-鿿]/.test(HELP[k].zh)), 'les aides chinoises contiennent des caractères chinois');
// toute phrase française passée à T('…') dans le code a une traduction (sauf les noms propres volontairement identiques)
const app = scripts[1], skip = new Set(['Français', 'English', 'E₀/Ec', 'DLP', 'LCD', 'nFEP']);
const used = new Set([...app.matchAll(/\bT\('((?:[^'\\\n]|\\.)*)'/g)].map(x => x[1].replace(/\\'/g, "'").replace(/\\\\/g, '\\')));
const miss = [...used].filter(s => /[a-zA-Zàéè]{3}/.test(s) && !skip.has(s) && !(s in I18N.en) && !/^[\s\d.,:;µ%×°±/()\-–—+·→]*$/.test(s) && !/^font|px/.test(s));
ok(used.size > 150, 'phrases traduisibles repérées dans le code : ' + used.size);
assert.deepStrictEqual(miss, [], 'phrases sans traduction : ' + JSON.stringify(miss.slice(0, 5)));
n++;

/* ---------- T() et nombres selon la langue ---------- */
const frSample = keysEn.find(k => !/\{/.test(k) && I18N.en[k] !== k && I18N.zh[k] !== k);
m.setLang('fr'); ok(m.T(frSample) === frSample, 'français : la phrase source est rendue telle quelle');
m.setLang('en'); ok(m.T(frSample) === I18N.en[frSample], 'anglais : phrase traduite');
m.setLang('zh'); ok(m.T(frSample) === I18N.zh[frSample], 'chinois : phrase traduite');
ok(m.T('Phrase inconnue') === 'Phrase inconnue', 'phrase inconnue : renvoyée telle quelle');
const withVars = keysEn.find(k => /\{0\}/.test(k));
if (withVars) { m.setLang('en'); const r = m.T(withVars, { 0: 7, 1: 11 }); ok(!/\{0\}/.test(r) && /7/.test(r), 'T() remplace les paramètres {0}, {1}'); }
m.setLang('en'); ok(m.NF(1234.5, 1) === '1,234.5', 'nombres anglais : 1,234.5');
m.setLang('fr'); ok(m.NF(1234.5, 1).replace(/\s/g, ' ') === '1 234,5', 'nombres français : 1 234,5');
m.setLang('zh'); ok(m.NF(1234.5, 1) === '1,234.5', 'nombres chinois : 1,234.5');
m.setLang('fr'); ok(m.NF(6.123, 1) === '6,1' && m.NF(NaN, 1) === 'NaN', 'virgule décimale en français, valeur non finie inchangée');

console.log(`${n} tests du simulateur passent.`);
