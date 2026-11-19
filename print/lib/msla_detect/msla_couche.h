/*
  msla_detect — in-print fault detection (per-layer learning)
  -----------------------------------------------------------
  Faithful port of the features/detect/verdict functions of the simulator.

  Principle: for each layer the code extracts "features" (peel peak, descent
  compression, residual push, travel at the peak). It learns the normal level from
  the last 6 good layers, then it flags any layer that moves away from it (peel too
  low, obstacle, block, push, loose film).

  Inputs: sampled series (parallel arrays supplied by the caller).
  Outputs: a set of flags per layer, then a verdict (first faulty layer, kind,
  number of false alarms, latency).

  double everywhere, no dynamic memory, no Arduino dependency.

  中文：打印中故障检测（逐层学习）。忠实移植模拟器的 features/detect/verdict。
  每层提取特征（剥离峰值、下降压缩、残余受力、峰值处行程），用最近 6 个正常层学习正常
  水平，再标记偏离的层（剥离过弱、障碍、卡住、受力、离型膜松弛）。输入为采样序列，输出为
  每层的标志及判定。全部 double，无动态内存，不依赖 Arduino。
*/
#ifndef MSLA_COUCHE_H
#define MSLA_COUCHE_H

#include <cstdint>

namespace msla {

#ifndef MSLA_MAX_COUCHES
#define MSLA_MAX_COUCHES 256   // maximum number of layers handled without allocation
#endif

// "Absent" value (equivalent of the JavaScript null). Tested by estAbsent().
double absente();
bool   estAbsent(double v);

// Phases of a sample (as sent by the simulator and the firmware):
//   0 = top wait / before descent          1 = peel (up move)
//   2 = descent                            3 = exposure (top hold)
//   4 = bottom hold
// The layer (ly) is -1 outside an identified layer.

// Sampled series: parallel arrays of n cells, owned by the caller.
struct Serie {
  const double* t;    // time (s)
  const double* f;    // force already filtered (see prep())
  const double* z;    // Z position (mm)
  const int*    ph;   // phase 0..4
  const int*    ly;   // layer index, or -1
  int n;
};

// Features of a layer. An "absent" field (NaN) means "not measured".
struct Feature {
  double peak;    // peel peak (max force in phase 1, above the top tare)
  double comp;    // min compression (phase 2, or phases 0/4 of the next layer)
  double travel;  // Z reached at the peak
  double stat;    // static push (end-of-exposure difference between successive layers)
  int    iPeak;   // sample index of the peak (-1 if absent)
  int    iComp;   // sample index of the compression (-1 if absent)
};

// Flags of a layer, in the order of the simulator.
enum Flag { F_NODATA = 0, F_CRASH, F_OBSTACLE, F_APPUI, F_FAIBLE, F_FEP };
struct LayerFlags {
  uint8_t n;       // number of flags
  Flag    f[4];    // ordered flags
};

// Floor thresholds per sensor (FLOOR of the simulator).
struct Floor { double p, c, s; bool sNull; };
Floor floorPour(const char* key);   // "z","fus","vat","sg"

// prep(): builds the filtered force ff from the raw force f.
//   med = true  -> ff = med3(f)        (else a copy of f)
//   avg = true  -> ff = moy_glissante(ff, 3)
// ff and scratch: buffers of n cells from the caller (scratch can be nullptr if !avg&&!med).
void prep(const double* f, int n, bool med, bool avg, double* ff, double* scratch);

// features(): fills out[0..nLayers) from the series s.
//   scratch / scratchLen: work space of at least the size of the largest
//   (phase, layer) group; in practice a buffer of size s.n is enough.
void features(const Serie& s, int nLayers, Feature* out,
              double* scratch, int scratchLen);

// detect(): per-layer learning. Fills flags[0..nLayers).
//   area    : area per layer (cm²), nLayers cells
//   key     : "z","fus","vat","sg"
//   nm      : adapted noise level (noise * resFactor, or noise for "sg")
//   obstacleMul, lowRatio : application parameters (APPS)
//   fstall  : motor block force (N)
void detect(const Feature* feat, const double* area, int nLayers,
            const char* key, double nm, double obstacleMul, double lowRatio,
            double fstall, LayerFlags* flags);

// Verdict of a set of flags.
struct Verdict {
  int  first;      // first faulty layer (>= start), -1 if none
  Flag kind;       // kind of the first fault
  int  fa;         // false alarms (faulty layers before start)
  int  nodata;     // layers with no data
  int  lat;        // latency (first - start), -1 if first == -1
  bool hasFirst;
};

// scen : "normal","detach","support","residue","fep" (TRIGGER table).
// start = (scen=="normal") ? nLayers : L.
Verdict verdict(const LayerFlags* flags, int nLayers, const char* scen, int L);

/* ================= Streaming detection (for the firmware) ================= */
// Incremental versions of features/detect, to run live layer by layer without a
// full buffer. They give results IDENTICAL to the batch versions (checked by parity).
//
// 中文：流式检测（供固件）。features/detect 的增量版本，逐层实时运行，结果与批处理版本完全一致。

#ifndef MSLA_SUIVI_BUF
#define MSLA_SUIVI_BUF 1024   // samples buffered per layer for the medians (ph0, ph3)
#endif

// DetecteurCouche: streaming per-layer learning. Push the finalized Feature values
// in increasing order; it returns the flags of each layer, identical to detect().
class DetecteurCouche {
 public:
  void init(const char* key, double nm, double obstacleMul, double lowRatio, double fstall);
  LayerFlags pousser(const Feature& ft, double area);
 private:
  void ajouterBonne(const Feature& ft, double area);
  Floor  fl_{};
  bool   isSg_ = false;
  double nm_ = 0, obstacleMul_ = 0, lowRatio_ = 0, fstall_ = 0;
  Feature gFeat_[6]; double gArea_[6]; int gN_ = 0;   // window of the last 6 good layers
  Feature sFeat_[4]; double sArea_[4]; int sN_ = 0;   // pending FAIBLE run (relearning)
};

// SuiviCouches: builds the Feature values layer by layer from the stream of samples
// (filtered force ff, position z, phase ph, layer ly), in the order of the simulator
// (block 4→0→1→3→2). pousser() returns the index of the layer that was just
// finalized (and fills *out), or -1. terminer() finalizes the last open layer.
// Results identical to features().
class SuiviCouches {
 public:
  void init();
  // area = exposed area of the layer ly (mm²). When a layer is finalized, its area
  // is returned in *outArea (area of the finalized layer, not of the current one).
  int  pousser(double ff, double z, int ph, int ly, double area, Feature* out, double* outArea);
  int  terminer(Feature* out, double* outArea);
 private:
  struct Acc {
    int    ly = -1;
    double area = 0;
    double preBuf[MSLA_SUIVI_BUF]; int preN = 0; bool preFait = false, preData = false; double pre = 0;
    double topBuf[MSLA_SUIVI_BUF]; int topN = 0; bool topFait = false; double top = 0;
    double peak = 0, travel = 0; bool hasPeak = false;
    double comp = 0; bool hasComp = false;
  };
  Acc  acc_[3];
  Acc* slotPour(int ly);          // existing or new
  Acc* slotExistant(int ly);
  void ensurePre(Acc* a);
  void ensureTop(Acc* a);
  void finaliser(Acc* a, Acc* suivant, Feature* out, double* outArea);
  void liberer(Acc* a);
};

}  // namespace msla

#endif  // MSLA_COUCHE_H
