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

/* ================= DetecteurCouche (apprentissage par couche en flux) ================= */
void DetecteurCouche::init(const char* key, double nm, double obstacleMul, double lowRatio, double fstall) {
  fl_ = floorPour(key);
  isSg_ = !std::strcmp(key, "sg");
  nm_ = nm; obstacleMul_ = obstacleMul; lowRatio_ = lowRatio; fstall_ = fstall;
  gN_ = 0; sN_ = 0;
}
void DetecteurCouche::ajouterBonne(const Feature& ft, double area) {
  if (gN_ < 6) { gFeat_[gN_] = ft; gArea_[gN_] = area; gN_++; }
  else { for (int i = 1; i < 6; i++) { gFeat_[i - 1] = gFeat_[i]; gArea_[i - 1] = gArea_[i]; } gFeat_[5] = ft; gArea_[5] = area; }
}
LayerFlags DetecteurCouche::pousser(const Feature& ft, double area) {
  LayerFlags lf; lf.n = 0;
  if (estAbsent(ft.peak)) { pushFlag(lf, F_NODATA); return lf; }
  if (gN_ < 3) { ajouterBonne(ft, area); return lf; }

  double tmpP[6], tmpC[6];
  for (int r = 0; r < gN_; r++) {
    tmpP[r] = gFeat_[r].peak / gArea_[r];
    const double cc = estAbsent(gFeat_[r].comp) ? 0.0 : gFeat_[r].comp;
    tmpC[r] = cc / gArea_[r];
  }
  const double cP = medSortInPlace(tmpP, gN_), cC = medSortInPlace(tmpC, gN_);
  const double expP = cP * area, expC = cC * area;

  bool bad = false;
  const double stallThr = isSg_ ? -std::min(95.0, 0.68 * fstall_) : -0.93 * fstall_;
  if (!estAbsent(ft.comp) && ft.comp <= stallThr) { pushFlag(lf, F_CRASH); bad = true; }
  else if (!estAbsent(ft.comp) && ft.comp < obstacleMul_ * std::min(expC, 0.0) - fl_.c * nm_) { pushFlag(lf, F_OBSTACLE); bad = true; }
  if (!bad && !fl_.sNull && !estAbsent(ft.stat) && ft.stat < -fl_.s * nm_) { pushFlag(lf, F_APPUI); bad = true; }
  if (expP > 2 * fl_.p * nm_ && ft.peak < lowRatio_ * expP - 0.5 * fl_.p * nm_) { pushFlag(lf, F_FAIBLE); bad = true; }
  double tmpT[6]; int mb = 0;
  for (int r = 0; r < gN_; r++) if (gFeat_[r].peak > 8 * fl_.p * nm_) tmpT[mb++] = gFeat_[r].travel / gFeat_[r].peak;
  if (mb >= 2 && expP > 8 * fl_.p * nm_ && ft.peak > 0) {
    const double comp = medSortInPlace(tmpT, mb);
    if (ft.travel / ft.peak > 1.7 * comp + 0.01) { pushFlag(lf, F_FEP); bad = true; }
  }

  if (!bad) { ajouterBonne(ft, area); sN_ = 0; }
  else if (lf.n == 1 && lf.f[0] == F_FAIBLE) {
    sFeat_[sN_] = ft; sArea_[sN_] = area; sN_++;
    if (sN_ >= 4) { for (int k = 0; k < sN_; k++) ajouterBonne(sFeat_[k], sArea_[k]); sN_ = 0; }
  } else sN_ = 0;
  return lf;
}

/* ================= SuiviCouches (construction des Feature en flux) ================= */
void SuiviCouches::init() { for (int i = 0; i < 3; i++) acc_[i].ly = -1; }

SuiviCouches::Acc* SuiviCouches::slotExistant(int ly) {
  for (int i = 0; i < 3; i++) if (acc_[i].ly == ly) return &acc_[i];
  return nullptr;
}
SuiviCouches::Acc* SuiviCouches::slotPour(int ly) {
  Acc* a = slotExistant(ly);
  if (a) return a;
  for (int i = 0; i < 3; i++) if (acc_[i].ly < 0) {
    a = &acc_[i];
    a->ly = ly; a->preN = 0; a->topN = 0; a->preFait = a->preData = a->topFait = false;
    a->pre = a->top = 0; a->peak = a->travel = a->comp = 0; a->hasPeak = a->hasComp = false;
    return a;
  }
  return &acc_[0];   // ne devrait pas arriver (au plus 2 couches ouvertes)
}
void SuiviCouches::liberer(Acc* a) { a->ly = -1; }

// Ajoute une valeur à un tampon glissant qui conserve au plus MSLA_SUIVI_BUF dernières
// valeurs. n compte TOUJOURS le total poussé (pas seulement le nombre retenu) : la
// fenêtre « derniers 30 % » a besoin du total exact.
static void pushGliss(double* buf, int& n, double v) {
  if (n < MSLA_SUIVI_BUF) buf[n] = v;
  else { for (int i = 1; i < MSLA_SUIVI_BUF; i++) buf[i - 1] = buf[i]; buf[MSLA_SUIVI_BUF - 1] = v; }
  n++;
}
void SuiviCouches::ensurePre(Acc* a) {
  if (a->preFait) return;
  a->preData = a->preN > 0;
  if (a->preData) {
    const int start = (int)std::floor(a->preN * 0.7);
    int count = a->preN - start;
    const int retained = a->preN < MSLA_SUIVI_BUF ? a->preN : MSLA_SUIVI_BUF;
    if (count > retained) count = retained;
    a->pre = medSortInPlace(&a->preBuf[retained - count], count);   // médiane des derniers 30 %
  } else a->pre = 0;
  a->preFait = true;
}
void SuiviCouches::ensureTop(Acc* a) {
  if (a->topFait) return;
  const int retained = a->topN < MSLA_SUIVI_BUF ? a->topN : MSLA_SUIVI_BUF;
  a->top = retained > 0 ? medSortInPlace(a->topBuf, retained) : 0.0;
  a->topFait = true;
}
void SuiviCouches::finaliser(Acc* a, Acc* suivant, Feature* out, double* outArea) {
  ensurePre(a); ensureTop(a);
  out->iPeak = out->iComp = -1;
  out->peak   = a->hasPeak ? a->peak : absente();
  out->travel = a->hasPeak ? a->travel : absente();
  out->comp   = a->hasComp ? a->comp : absente();
  out->stat   = (a->preData && suivant && suivant->preFait && suivant->preData) ? suivant->pre - a->pre : absente();
  if (outArea) *outArea = a->area;
}
int SuiviCouches::pousser(double ff, double z, int ph, int ly, double area, Feature* out, double* outArea) {
  int emis = -1;
  Acc* a = slotPour(ly);
  a->area = area;
  if (ph == 0 || ph == 4) {
    if (ph == 0) pushGliss(a->preBuf, a->preN, ff);
    if (ly > 0) { Acc* p = slotExistant(ly - 1); if (p) { ensureTop(p); const double v = ff - p->top; if (!p->hasComp || v < p->comp) { p->comp = v; p->hasComp = true; } } }
  } else if (ph == 3) {
    pushGliss(a->topBuf, a->topN, ff);
  } else if (ph == 1) {
    ensurePre(a);
    if (ly > 0) { Acc* p = slotExistant(ly - 1); if (p) { finaliser(p, a, out, outArea); emis = ly - 1; liberer(p); } }
    const double v = ff - a->pre;
    if (!a->hasPeak || v > a->peak) { a->peak = v; a->travel = z; a->hasPeak = true; }
  } else if (ph == 2) {
    ensureTop(a);
    const double v = ff - a->top;
    if (!a->hasComp || v < a->comp) { a->comp = v; a->hasComp = true; }
  }
  return emis;
}
int SuiviCouches::terminer(Feature* out, double* outArea) {
  // finalise la couche ouverte d'index le plus petit (dans l'ordre)
  int best = -1; Acc* ba = nullptr;
  for (int i = 0; i < 3; i++) if (acc_[i].ly >= 0 && (best < 0 || acc_[i].ly < best)) { best = acc_[i].ly; ba = &acc_[i]; }
  if (!ba) return -1;
  Acc* suivant = slotExistant(best + 1);
  finaliser(ba, suivant, out, outArea);
  liberer(ba);
  return best;
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
