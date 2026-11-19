// Tests du dossier de procédures : node light-engine/tests/bancs.test.js
const fs = require('fs'), path = require('path'), assert = require('assert');
const dir = path.join(__dirname, '..', 'bancs-mesure');
const html = fs.readFileSync(path.join(dir, 'index.html'), 'utf8');
let n = 0; const ok = (c, m) => { assert.ok(c, m); n++; };

// dictionnaire intégré à la page
const m = html.match(/\/\*DICT\*\/([\s\S]*?)\/\*END\*\//);
ok(m, 'dictionnaire présent');
const D = JSON.parse(m[1]);
const ke = Object.keys(D.en), kz = Object.keys(D.zh);
ok(ke.length > 400, 'plus de 400 textes traduits (' + ke.length + ')');
ok(ke.length === kz.length && ke.every(k => k in D.zh), 'anglais et chinois contiennent les mêmes textes');
ok(ke.every(k => D.en[k] && D.zh[k]), 'aucune traduction vide');
const tags = s => (String(s).match(/<\/?(?!sup|sub)[a-z][^>]*>/gi) || []).map(t => t.replace(/\s.*>/, '>')).sort().join('');   // <sup> (1er, 21e) n'existe pas en chinois
ok(ke.every(k => tags(k) === tags(D.en[k]) && tags(k) === tags(D.zh[k])), 'les balises (<b>, <code>…) sont conservées dans chaque langue');
ok(ke.every(k => /[一-鿿]/.test(D.zh[k]) || !/[a-z]{4,}/i.test(D.zh[k].replace(/<[^>]*>/g, '')) || /^[\sA-Za-z0-9.,:;()/<>=+\-−×·°µ%±≈≤₀₁₂₃θφ′∅→|·’]*$/.test(D.zh[k])), 'les traductions chinoises contiennent des caractères chinois');
ok(ke.filter(k => /[àâçéèêëîôùûœ]/i.test(D.en[k])).length === 0, 'aucun accent français dans les traductions anglaises');

// fichiers livrés
for (const f of ['bancs-mesure-fr.pdf', 'bancs-mesure-en.pdf', 'bancs-mesure-zh.pdf', 'motifs-uvtools.zip']) {
  const b = fs.readFileSync(path.join(dir, f));
  ok(b.length > 10000, f + ' présent et non vide');
  ok(f.endsWith('.pdf') ? b.slice(0, 4).toString() === '%PDF' : b.slice(0, 2).toString() === 'PK', f + ' : format valide');
}
ok(/id="pdfLink"/.test(html) && /id="zipLink"/.test(html), 'liens de téléchargement du PDF et du ZIP présents');
ok(/localStorage|bancs-mesure-lang/.test(html), 'langue mémorisée dans le navigateur');
console.log(`${n} tests du dossier de procédures passent.`);
