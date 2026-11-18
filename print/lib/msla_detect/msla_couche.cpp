#include "msla_couche.h"
#include "msla_filtres.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace msla {

double absente() { return NAN; }
bool   estAbsent(double v) { return std::isnan(v); }

Floor floorPour(const char* key) {
  // FLOOR du simulateur : {p: pic, c: compression, s: appui}
  if (!std::strcmp(key, "vat")) return {2.0, 2.5, 1.0, false};
  if (!std::strcmp(key, "sg"))  return {10.0, 12.0, 0.0, true};   // s = null
  return {0.4, 0.8, 0.25, false};                                 // "z" et "fus"
}

// Médiane destructive sur un tampon (le tri est fait sur place).
static double medSortInPlace(double* b, int n) {
  if (n <= 0) return 0.0;
  std::sort(b, b + n);
  const int m = n >> 1;
  return (n & 1) ? b[m] : (b[m - 1] + b[m]) / 2.0;
}

void prep(const double* f, int n, bool med, bool avg, double* ff, double* scratch) {
  if (med) med3(f, ff, n);
  else for (int i = 0; i < n; i++) ff[i] = f[i];
  if (avg) {
    for (int i = 0; i < n; i++) scratch[i] = ff[i];
    moy_glissante(scratch, ff, n, 3);
  }
}

void features(const Serie& s, int nLayers, Feature* out,
              double* scratch, int /*scratchLen*/) {
  double top[MSLA_MAX_COUCHES], pre[MSLA_MAX_COUCHES];
  for (int L = 0; L < nLayers; L++) {
    out[L].peak = out[L].comp = out[L].travel = out[L].stat = absente();
    out[L].iPeak = out[L].iComp = -1;
    top[L] = pre[L] = absente();
  }
  const double* f = s.f;

  // tare par couche : moyenne (médiane) en exposition (ph 3) et avant descente (ph 0)
  for (int L = 0; L < nLayers; L++) {
    // top[L] = médiane de toutes les valeurs ph==3 de la couche L
    int c = 0;
    for (int k = 0; k < s.n; k++) if (s.ly[k] == L && s.ph[k] == 3) scratch[c++] = f[k];
    if (c) top[L] = medSortInPlace(scratch, c);
    // pre[L] = médiane des 30 % derniers échantillons ph==0 de la couche L
    c = 0;
    for (int k = 0; k < s.n; k++) if (s.ly[k] == L && s.ph[k] == 0) scratch[c++] = f[k];
    if (c) {
      int start = (int)std::floor(c * 0.7);
      pre[L] = medSortInPlace(scratch + start, c - start);
    }
  }
  // appui statique : écart de fin d'exposition entre deux couches successives
  for (int L = 0; L + 1 < nLayers; L++)
    if (!estAbsent(pre[L + 1]) && !estAbsent(pre[L])) out[L].stat = pre[L + 1] - pre[L];

  // pic d'arrachement (ph 1) et compression (ph 2, ou ph 0/4 de la couche suivante)
  for (int k = 0; k < s.n; k++) {
    const int L = s.ly[k], ph = s.ph[k];
    if (L < 0 || L >= nLayers) continue;
    if (ph == 1) {
      const double v = f[k] - (estAbsent(pre[L]) ? 0.0 : pre[L]);
      Feature& r = out[L];
      if (estAbsent(r.peak) || v > r.peak) { r.peak = v; r.iPeak = k; r.travel = s.z[k]; }
    }
    const int cl = (ph == 2) ? L : (((ph == 0 || ph == 4) && L > 0) ? L - 1 : -1);
    if (cl >= 0) {
      const double v = f[k] - (estAbsent(top[cl]) ? 0.0 : top[cl]);
      Feature& r = out[cl];
      if (estAbsent(r.comp) || v < r.comp) { r.comp = v; r.iComp = k; }
    }
  }
}

static void pushFlag(LayerFlags& lf, Flag fl) { if (lf.n < 4) lf.f[lf.n++] = fl; }

void detect(const Feature* feat, const double* area, int nLayers,
            const char* key, double nm, double obstacleMul, double lowRatio,
            double fstall, LayerFlags* flags) {
  const Floor fl = floorPour(key);
  const bool isSg = !std::strcmp(key, "sg");
  for (int i = 0; i < nLayers; i++) flags[i].n = 0;

  int good[MSLA_MAX_COUCHES]; int nGood = 0;
  int streak[MSLA_MAX_COUCHES]; int nStreak = 0;

  for (int i = 0; i < nLayers; i++) {
    const Feature& ft = feat[i];
    if (estAbsent(ft.peak)) { pushFlag(flags[i], F_NODATA); continue; }

    int recStart = nGood - 6; if (recStart < 0) recStart = 0;
    const int recLen = nGood - recStart;
    if (recLen < 3) { good[nGood++] = i; continue; }

    double tmpP[6], tmpC[6]; int m = 0;
    for (int r = recStart; r < nGood; r++) {
      const int j = good[r];
      tmpP[m] = feat[j].peak / area[j];
      const double cc = estAbsent(feat[j].comp) ? 0.0 : feat[j].comp;
      tmpC[m] = cc / area[j];
      m++;
    }
    const double cP = medSortInPlace(tmpP, m), cC = medSortInPlace(tmpC, m);
    const double expP = cP * area[i], expC = cC * area[i];

    bool bad = false;
    const double stallThr = isSg ? -std::min(95.0, 0.68 * fstall) : -0.93 * fstall;
    if (!estAbsent(ft.comp) && ft.comp <= stallThr) { pushFlag(flags[i], F_CRASH); bad = true; }
    else if (!estAbsent(ft.comp) && ft.comp < obstacleMul * std::min(expC, 0.0) - fl.c * nm) {
      pushFlag(flags[i], F_OBSTACLE); bad = true;
    }
    if (!bad && !fl.sNull && !estAbsent(ft.stat) && ft.stat < -fl.s * nm) {
      pushFlag(flags[i], F_APPUI); bad = true;
    }
    if (expP > 2 * fl.p * nm && ft.peak < lowRatio * expP - 0.5 * fl.p * nm) {
      pushFlag(flags[i], F_FAIBLE); bad = true;
    }
    // film détendu : course au pic nettement plus longue que sur les grosses couches saines
    double tmpT[6]; int mb = 0;
    for (int r = recStart; r < nGood; r++) {
      const int j = good[r];
      if (feat[j].peak > 8 * fl.p * nm) tmpT[mb++] = feat[j].travel / feat[j].peak;
    }
    if (mb >= 2 && expP > 8 * fl.p * nm && ft.peak > 0) {
      const double comp = medSortInPlace(tmpT, mb);
      if (ft.travel / ft.peak > 1.7 * comp + 0.01) { pushFlag(flags[i], F_FEP); bad = true; }
    }

    if (!bad) { good[nGood++] = i; nStreak = 0; }
    else if (flags[i].n == 1 && flags[i].f[0] == F_FAIBLE) {
      streak[nStreak++] = i;
      if (nStreak >= 4) { for (int k = 0; k < nStreak; k++) good[nGood++] = streak[k]; nStreak = 0; }
    } else nStreak = 0;
  }
}

// Table TRIGGER : les drapeaux qui comptent comme « panne » selon le scénario.
static bool dansTrigger(const char* scen, Flag f) {
  if (!std::strcmp(scen, "normal")) return false;
  if (!std::strcmp(scen, "detach"))  return f == F_FAIBLE || f == F_OBSTACLE || f == F_CRASH || f == F_APPUI;
  if (!std::strcmp(scen, "support")) return f == F_FAIBLE;
  if (!std::strcmp(scen, "residue")) return f == F_OBSTACLE || f == F_CRASH || f == F_APPUI;
  if (!std::strcmp(scen, "fep"))     return f == F_FEP;
  return false;
}

Verdict verdict(const LayerFlags* flags, int nLayers, const char* scen, int L) {
  const int start = !std::strcmp(scen, "normal") ? nLayers : L;
  Verdict v{-1, F_NODATA, 0, 0, -1, false};
  for (int i = 0; i < nLayers; i++) {
    bool hasNon = false, nod = false;
    for (int j = 0; j < flags[i].n; j++) {
      if (flags[i].f[j] == F_NODATA) nod = true; else hasNon = true;
    }
    if (nod) v.nodata++;
    if (i < start) { if (hasNon) v.fa++; }
    else if (!v.hasFirst) {
      for (int j = 0; j < flags[i].n; j++) {
        const Flag f = flags[i].f[j];
        if (f != F_NODATA && dansTrigger(scen, f)) { v.first = i; v.kind = f; v.hasFirst = true; break; }
      }
    }
  }
  if (v.hasFirst) v.lat = v.first - start;
  return v;
}

}  // namespace msla
