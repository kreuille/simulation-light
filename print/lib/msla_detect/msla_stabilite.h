/*
  msla_detect — resin settle
  ---------------------------
  Measures, per layer, the time for the force to become stable again after the
  descent (wait before exposure) and after the peel (wait at the top). It helps to
  confirm that a sensor "sees" that the resin is still before exposure, and to
  compare with the wait setting of the profile.

  Faithful port of slopeAt/resStd/stability/stabSummary of the simulator.
  double everywhere, no dynamic memory, no Arduino dependency.

  中文：树脂稳定。逐层测量下降后（曝光前等待）与剥离后（顶部等待）力重新稳定所需时间，用于确认
  传感器能在曝光前“看到”树脂静止，并与配置的等待值比较。忠实移植模拟器的
  slopeAt/resStd/stability/stabSummary。全部 double，无动态内存，不依赖 Arduino。
*/
#ifndef MSLA_STABILITE_H
#define MSLA_STABILITE_H

#include "msla_couche.h"   // Serie, MSLA_MAX_COUCHES

namespace msla {

// Slope (linear regression) of f over [a,b] as a function of t.
double slopeAt(const double* t, const double* f, int a, int b);
// Standard deviation of the residuals of that regression over [a,b].
double resStd(const double* t, const double* f, int a, int b);

// Settle result of a layer.
struct Settle {
  double ts;        // settle time (s) relative to the start of the wait
  bool   found;     // true if the stability was detected
  bool   tooShort;  // true if the window was too short to conclude
  double sig;       // estimated noise
  double thr;       // slope threshold used
};

struct StabOpt { double rate; double noise; };   // rate = samples/s, noise = adapted noise
// Reference instants per layer (ground truth of the simulator); NaN = absent.
struct Starts { const double* rest; const double* top; };

// Fills down[1..n) (after the descent) and top[0..n) (after the peel).
// s.f must hold the filtered force (ff). down/top : buffers of n cells.
void stability(const Serie& s, const char* key, const StabOpt& opt,
               const Starts& starts, int nLayers, Settle* down, Settle* top);

// Sensitivity summary of a sensor compared with the ground truth.
struct SensSum { int ok, fa, miss, okT; double adMean; int adEarly; };
struct Truth { const double* settleDown; const double* settleTop; };
struct ProcWait { double wait; double topWait; };

// Counts the layers where the real settle exceeds the setting (trueEarly/trueTop),
// and, for a given sensor, the agreements/false alarms/misses.
int trueEarlyCount(const Truth& truth, const ProcWait& Q, double margin, int nLayers);
int trueTopCount(const Truth& truth, const ProcWait& Q, double margin, int nLayers);
SensSum sensSummary(const Settle* down, const Settle* top, const Truth& truth,
                    const ProcWait& Q, double margin, int nLayers);

}  // namespace msla

#endif  // MSLA_STABILITE_H
