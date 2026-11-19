/*
  msla_detect — signal filters
  ----------------------------
  Filters ported one-to-one from the simulator (print/simulateur/index.html) to keep
  the verdicts identical between the JavaScript reference and the C++ library.

  Everything uses double precision, like the JavaScript Number, so that the threshold
  comparisons give the same result on both sides.

  No dynamic memory: the caller owns the output and the work buffers. No Arduino.

  中文：信号滤波器。与模拟器逐一对应移植，以保证 JS 与 C++ 判定一致；全部使用 double
  精度（与 JS 的 Number 一致）；不分配内存，输出与临时缓冲区由调用方提供；不依赖 Arduino。
*/
#ifndef MSLA_FILTRES_H
#define MSLA_FILTRES_H

namespace msla {

// Median of an array of n values, without a change to "a".
// "scratch" must hold at least n cells; it is used as the sort space.
// Returns 0 if n <= 0 (like median() of the simulator on an empty array).
double mediane(const double* a, int n, double* scratch);

// 3-point running median. The ends are copied as is.
// "in" and "out" can point to different buffers of n cells.
void med3(const double* in, double* out, int n);

// Centered moving average with window "w" (half window = w/2), ends included.
// Reproduces movAvg() of the simulator: average of the valid neighbors only.
void moy_glissante(const double* in, double* out, int n, int w);

}  // namespace msla

#endif  // MSLA_FILTRES_H
