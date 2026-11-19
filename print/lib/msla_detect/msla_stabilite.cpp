#include "msla_stabilite.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace msla {

double slopeAt(const double* t, const double* f, int a, int b) {
  const int n = b - a + 1;
  double st = 0, sf = 0, stt = 0, stf = 0;
  for (int k = a; k <= b; k++) {
    const double x = t[k] - t[a];
    st += x; sf += f[k]; stt += x * x; stf += x * f[k];
  }
  const double d = n * stt - st * st;
  return d > 0 ? (n * stf - st * sf) / d : 0.0;
}

double resStd(const double* t, const double* f, int a, int b) {
  const double sl = slopeAt(t, f, a, b);
  const int n = b - a + 1;
  double sum = 0, sum2 = 0;
  for (int k = a; k <= b; k++) {
    const double r = f[k] - sl * (t[k] - t[a]);
    sum += r; sum2 += r * r;
  }
  const double var = (sum2 - sum * sum / n) / std::max(1, n - 1);
  return std::sqrt(var > 0 ? var : 0.0);
}

void stability(const Serie& s, const char* key, const StabOpt& opt,
               const Starts& starts, int nLayers, Settle* down, Settle* top) {
  const double rate = opt.rate;
  const int nW = std::max(3, (int)std::lround(0.3 * rate));
  const int nH = std::max(2, (int)std::lround(0.15 * rate));
  const double W = (double)nW / rate;
  const double base = !std::strcmp(key, "vat") ? 0.6 : 0.3;

  int rdA[MSLA_MAX_COUCHES], rdB[MSLA_MAX_COUCHES], rtA[MSLA_MAX_COUCHES], rtB[MSLA_MAX_COUCHES];
  bool downComputed[MSLA_MAX_COUCHES];
  for (int L = 0; L < nLayers; L++) {
    rdA[L] = rtA[L] = -1;
    down[L] = {0, false, false, 0, 0};
    top[L]  = {0, false, false, 0, 0};
    downComputed[L] = false;
  }
  for (int k = 0; k < s.n; k++) {
    const int L = s.ly[k], ph = s.ph[k];
    if (L < 0 || L >= nLayers) continue;
    if (ph == 4 || ph == 0) { if (rdA[L] < 0) rdA[L] = k; rdB[L] = k; }
    else if (ph == 3)       { if (rtA[L] < 0) rtA[L] = k; rtB[L] = k; }
  }

  // analyse: finds the first instant where the slope stays below the threshold nH times in a row
  auto analyse = [&](int a, int b, double t0, bool hasSig, double sigIn) -> Settle {
    const int len = b - a + 1;
    double sig = hasSig ? sigIn : (len >= 3 ? resStd(s.t, s.f, b - std::min(len, nW * 2) + 1, b) : 0.0);
    Settle out = {0, false, false, sig, 0};
    if (len < nW + nH) { out.tooShort = true; return out; }
    const double thr = std::max(base * opt.noise, 3 * sig * std::sqrt(12.0 / (nW * W * W)));
    out.thr = thr;
    int run = 0;
    for (int k = a + nW - 1; k <= b; k++) {
      if (std::fabs(slopeAt(s.t, s.f, k - nW + 1, k)) < thr) {
        if (++run >= nH) {
          const int kf = k - nH + 1;
          out.ts = std::max(0.0, s.t[kf] - W / 2 - t0);
          out.found = true;
          return out;
        }
      } else run = 0;
    }
    return out;
  };

  for (int i = 1; i < nLayers; i++)
    if (rdA[i] >= 0 && !std::isnan(starts.rest[i])) { down[i] = analyse(rdA[i], rdB[i], starts.rest[i], false, 0); downComputed[i] = true; }
  for (int i = 0; i < nLayers; i++)
    if (rtA[i] >= 0 && !std::isnan(starts.top[i]))
      top[i] = analyse(rtA[i], rtB[i], starts.top[i], downComputed[i], down[i].sig);
}

int trueEarlyCount(const Truth& truth, const ProcWait& Q, double margin, int nLayers) {
  int c = 0;
  for (int i = 1; i < nLayers; i++) if (truth.settleDown[i] + margin > Q.wait) c++;
  return c;
}
int trueTopCount(const Truth& truth, const ProcWait& Q, double margin, int nLayers) {
  int c = 0;
  for (int i = 0; i < nLayers; i++) if (truth.settleTop[i] + margin > Q.topWait) c++;
  return c;
}

SensSum sensSummary(const Settle* down, const Settle* top, const Truth& truth,
                    const ProcWait& Q, double margin, int nLayers) {
  SensSum o{0, 0, 0, 0, 0, 0};
  double adSum = 0;
  for (int i = 1; i < nLayers; i++) {
    const Settle& d = down[i];
    const bool te = truth.settleDown[i] + margin > Q.wait;
    const bool se = !d.found || d.ts + margin > Q.wait;
    if (se == te) o.ok++; else if (se) o.fa++; else o.miss++;
    const double ad = d.found ? d.ts + 0.1 + margin : truth.settleDown[i] + 0.3 + margin;
    adSum += ad;
    if (truth.settleDown[i] > ad) o.adEarly++;
  }
  for (int i = 0; i < nLayers; i++) {
    const Settle& d = top[i];
    const bool te = truth.settleTop[i] + margin > Q.topWait;
    const bool se = !d.found || d.ts + margin > Q.topWait;
    if (se == te) o.okT++;
  }
  o.adMean = adSum / std::max(1, nLayers - 1);
  return o;
}

}  // namespace msla
