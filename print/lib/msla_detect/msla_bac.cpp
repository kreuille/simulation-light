#include "msla_bac.h"
#include <algorithm>
#include <cmath>

namespace msla {

BacArm BacControleur::armer(int mode, const BacConfig& cfg, BacRef* ref) {
  cfg_ = cfg;
  ref_ = ref;
  ckMode_ = mode;
  ck_ = CK_WAIT;
  bn_ = 0; bhead_ = 0;
  stillSince_ = -1;
  holdN_ = 0;
  for (int i = 0; i < 5; i++) holdAcc_[i] = 0;
  maintienCourt_ = false; maintienReel_ = 0;
  // Remarque : vHist (vitesse) n'est pas remis à zéro, comme dans le firmware :
  // l'estimation de vitesse se poursuit d'un test au suivant.
  if (mode == BAC_MODE_PLANEITE && cfg_.nCells != 5) {
    ck_ = CK_OFF;
    return BAC_ARM_ERR_PLANEITE_CELLS;
  }
  return BAC_ARME;
}

BacEvent BacControleur::maj(float t, float z, const float f[5], bool uv) {
  if (ck_ == CK_OFF) return BAC_RAS;
  vHistT_[vIdx_] = t; vHistZ_[vIdx_] = z; vIdx_ = (vIdx_ + 1) % 9; if (vCount_ < 9) vCount_++;
  if (vCount_ < 9) return BAC_RAS;
  const int old = vIdx_;   // le plus ancien des 9
  const float dt = t - vHistT_[old];
  const float v = dt > 0 ? (z - vHistZ_[old]) / dt : 0;   // mm/s

  if (ck_ == CK_WAIT) {
    if (v < -0.05f) { ck_ = CK_DESC; bn_ = 0; bhead_ = 0; stillSince_ = -1; }
    return BAC_RAS;
  }
  if (ck_ == CK_DESC) {
    bz_[bhead_] = z;
    for (int c = 0; c < 5; c++) bf_[bhead_][c] = c < cfg_.nCells ? f[c] : 0;
    bhead_ = (bhead_ + 1) % MSLA_CK_BUF; if (bn_ < MSLA_CK_BUF) bn_++;
    if (v > 0.05f) { ck_ = CK_WAIT; return BAC_RAS; }
    if (std::fabs(v) < 0.02f && !uv) {
      if (stillSince_ < 0) stillSince_ = t;
      if (t - stillSince_ > 0.3f) { ck_ = CK_HOLD; zStop_ = z; tStop_ = stillSince_; hHead_ = 0; hCount_ = 0; }
    } else stillSince_ = -1;
    return BAC_RAS;
  }
  if (ck_ == CK_HOLD) {
    const float h = t - tStop_;
    const bool ended = uv || std::fabs(v) > 0.05f || h > cfg_.holdS;
    if (!ended && h >= 1.0f) {   // fenêtre glissante des 0,6 dernières secondes
      hT_[hHead_] = t;
      for (int c = 0; c < 5; c++) hF_[hHead_][c] = c < cfg_.nCells ? f[c] : 0;
      hHead_ = (hHead_ + 1) % MSLA_CK_HB; if (hCount_ < MSLA_CK_HB) hCount_++;
    }
    if (ended) {
      const float tEnd = hCount_ ? hT_[(hHead_ - 1 + MSLA_CK_HB) % MSLA_CK_HB] : t;
      holdN_ = 0; for (int c = 0; c < 5; c++) holdAcc_[c] = 0;
      for (int n = 0; n < hCount_; n++) {
        const int k = (hHead_ - 1 - n + MSLA_CK_HB) % MSLA_CK_HB;
        if (hT_[k] < tEnd - 0.6f) break;
        for (int c = 0; c < cfg_.nCells; c++) holdAcc_[c] += hF_[k][c];
        holdN_++;
      }
      maintienCourt_ = (h < cfg_.holdS - 0.2f && holdN_ >= 10);
      maintienReel_ = h;
      return finir();
    }
    return BAC_RAS;
  }
  return BAC_RAS;
}

BacEvent BacControleur::finir() {
  const int nch = cfg_.nCells;
  // tare dans la résine : moyenne autour de zStop + 3 mm, pendant la descente
  float tare[5] = {0, 0, 0, 0, 0}; int tn = 0;
  for (int n = 0; n < bn_; n++) {
    const int k = bidx(n); const float d = bz_[k] - zStop_;
    if (d <= 3.0f && d >= 2.75f) { for (int c = 0; c < nch; c++) tare[c] += bf_[k][c]; tn++; }
  }
  if (tn == 0) {
    const int lim = bn_ < 5 ? bn_ : 5;
    for (int n = 0; n < lim; n++) { const int k = bidx(n); for (int c = 0; c < nch; c++) tare[c] += bf_[k][c]; tn++; }
  }
  for (int c = 0; c < nch; c++) tare[c] /= std::max(tn, 1);

  if (ckMode_ == BAC_MODE_PLANEITE) { finirPlaneite(tare); return BAC_EV_PLANEITE; }

  resCtrl_ = ControleResultat{};
  resCtrl_.maintienCourt = maintienCourt_;
  resCtrl_.maintienReel = maintienReel_;
  if (holdN_ < 10) { resCtrl_.erreur = true; ck_ = CK_OFF; return BAC_EV_CONTROLE; }

  // courbe de descente par tranches de 0,05 mm
  float curve[5][MSLA_CK_BINS]; int cnt[MSLA_CK_BINS];
  for (int b = 0; b < MSLA_CK_BINS; b++) { cnt[b] = 0; for (int c = 0; c < 5; c++) curve[c][b] = 0; }
  for (int n = 0; n < bn_; n++) {
    const int k = bidx(n); const float d = bz_[k] - zStop_;
    const int b = (int)std::lround((float)(d / 0.05f));
    if (b >= 0 && b < MSLA_CK_BINS) { for (int c = 0; c < nch; c++) curve[c][b] += bf_[k][c] - tare[c]; cnt[b]++; }
  }
  for (int b = 0; b < MSLA_CK_BINS; b++) for (int c = 0; c < nch; c++) curve[c][b] = cnt[b] ? curve[c][b] / cnt[b] : NAN;
  float stat[5]; for (int c = 0; c < nch; c++) stat[c] = (float)(holdAcc_[c] / holdN_) - tare[c];

  if (ckMode_ == BAC_MODE_REFERENCE) {
    if (ref_) {
      ref_->magic = MSLA_REF_MAGIC;
      for (int c = 0; c < 5; c++) {
        ref_->stat[c] = c < nch ? stat[c] : 0;
        for (int b = 0; b < MSLA_CK_BINS; b++) ref_->curve[c][b] = c < nch ? curve[c][b] : NAN;
      }
    }
    resRef_.appuiRef = stat[0];
    ck_ = CK_OFF;
    return BAC_EV_REFERENCE;
  }

  const bool hasRef = ref_ && ref_->magic == MSLA_REF_MAGIC;
  // contact pendant la descente : 2 tranches consécutives sous le seuil, en partant du haut
  float zContact = -1; int run = 0;
  for (int b = MSLA_CK_BINS - 1; b >= 1; b--) {
    const float r = (hasRef && !std::isnan(ref_->curve[0][b])) ? ref_->curve[0][b] : 0;
    if (!std::isnan(curve[0][b]) && curve[0][b] - r < -cfg_.ckDescN) { if (++run >= 2) { zContact = (b + 1) * 0.05f; break; } }
    else run = 0;
  }
  const float appui = -(stat[0] - (hasRef ? ref_->stat[0] : 0));
  const bool seen = appui > cfg_.ckStatN || zContact >= 0;
  resCtrl_.residu = seen;
  resCtrl_.appui = appui;
  resCtrl_.contact = zContact >= 0;
  resCtrl_.zContact = zContact;
  resCtrl_.epaisseur = zContact + cfg_.layerMm;
  resCtrl_.hasRef = hasRef;
  if (nch == 5) {
    float dc[4], S = 0;
    for (int c = 0; c < 4; c++) { dc[c] = -(stat[c + 1] - (hasRef ? ref_->stat[c + 1] : 0)); S += dc[c]; }
    if (S > 1.0f) { resCtrl_.position = true; resCtrl_.px = cfg_.vatW * (dc[1] + dc[2]) / S; resCtrl_.py = cfg_.vatH * (dc[2] + dc[3]) / S; }
  }
  resCtrl_.risque = !seen ? RISQUE_AUCUN
                    : (appui >= 0.9f * cfg_.zMaxN ? RISQUE_DANGER
                       : (appui > 3 ? RISQUE_ELEVE : RISQUE_MODERE));
  ck_ = CK_OFF;
  return BAC_EV_CONTROLE;
}

void BacControleur::finirPlaneite(const float tare[5]) {
  resPlan_ = PlaneiteResultat{};
  for (int n = 0; n < bn_; n++) {
    const int k = bidx(n); float d[4], S = 0;
    for (int c = 0; c < 4; c++) { d[c] = bf_[k][c + 1] - tare[c + 1]; S += d[c]; }
    if (std::fabs(S) >= 5.0f) {
      // moyenne sur les 16 mesures suivantes pour réduire le bruit
      float a[4] = {0, 0, 0, 0}; int m = 0;
      for (int j = n; j < bn_ && m < 16; j++, m++) { const int kk = bidx(j); for (int c = 0; c < 4; c++) a[c] += bf_[kk][c + 1] - tare[c + 1]; }
      S = a[0] + a[1] + a[2] + a[3];
      resPlan_.atteint5N = true;
      resPlan_.dx = cfg_.vatW * (a[1] + a[2]) / S - cfg_.vatW / 2;
      resPlan_.dy = cfg_.vatH * (a[2] + a[3]) / S - cfg_.vatH / 2;
      resPlan_.zA5N = bz_[k] - zStop_;
      ck_ = CK_OFF;
      return;
    }
  }
  resPlan_.atteint5N = false;
  ck_ = CK_OFF;
}

PeseeResultat analysePesee(const float g[4], const PeseeConfig& cfg, const PeseeRef& ref) {
  PeseeResultat o{}; o.coinFaible = -1;
  const float S = g[0] + g[1] + g[2] + g[3]; o.total = S;
  const float minVat = ref.vatG > 0 ? 0.5f * ref.vatG : 100;
  if (S < minVat) { o.present = false; return o; }
  o.present = true;
  float dev = 0, mn = 1; int imn = 0;
  for (int c = 0; c < 4; c++) {
    o.sh[c] = g[c] / S;
    const float dd = std::fabs(o.sh[c] - ref.share[c]); if (dd > dev) dev = dd;
    if (o.sh[c] < mn) { mn = o.sh[c]; imn = c; }
  }
  o.dev = dev; o.coinFaible = imn;
  o.cx = cfg.vatW * (o.sh[1] + o.sh[2]);
  o.cy = cfg.vatH * (o.sh[2] + o.sh[3]);
  o.off = std::hypot(o.cx - cfg.vatW / 2, o.cy - cfg.vatH / 2);
  o.malPose = dev > 0.06f && mn < 0.12f;
  o.decale = !o.malPose && o.off > 3;
  if (ref.vatG > 0) { o.resineConnue = true; o.resineMl = (S - ref.vatG) / ref.density; }
  return o;
}

FuiteResultat analyseFuite(float m0, float m1, float density, float minutes) {
  const float lost = m0 - m1;
  const float rate = lost / density / (minutes / 60);
  return {lost, rate, lost > 3};
}

}  // namespace msla
