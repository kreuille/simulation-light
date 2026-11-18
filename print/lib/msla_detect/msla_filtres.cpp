#include "msla_filtres.h"
#include <algorithm>

namespace msla {

double mediane(const double* a, int n, double* scratch) {
  if (n <= 0) return 0.0;                 // median([]) == 0 dans le simulateur
  for (int i = 0; i < n; i++) scratch[i] = a[i];
  std::sort(scratch, scratch + n);        // tri croissant, comme (x,y)=>x-y
  const int m = n >> 1;
  return (n & 1) ? scratch[m] : (scratch[m - 1] + scratch[m]) / 2.0;
}

void med3(const double* in, double* out, int n) {
  if (n <= 0) return;
  out[0] = in[0];
  if (n > 1) out[n - 1] = in[n - 1];
  for (int i = 1; i < n - 1; i++) {
    const double x = in[i - 1], y = in[i], z = in[i + 1];
    // médiane de 3 = max(min(x,y), min(max(x,y), z))
    const double mnxy = x < y ? x : y;
    const double mxxy = x < y ? y : x;
    const double mnmxz = mxxy < z ? mxxy : z;
    out[i] = mnxy > mnmxz ? mnxy : mnmxz;
  }
}

void moy_glissante(const double* in, double* out, int n, int w) {
  const int half = w >> 1;
  for (int i = 0; i < n; i++) {
    double s = 0.0;
    int c = 0;
    for (int j = i - half; j <= i + half; j++) {
      if (j >= 0 && j < n) { s += in[j]; c++; }
    }
    out[i] = c ? s / c : 0.0;
  }
}

}  // namespace msla
