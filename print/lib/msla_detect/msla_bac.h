/*
  msla_detect — vat control before a print, planarity, weigh, leak
  ----------------------------------------------------------------
  Port of the embedded firmware logic (ckArm/ckUpdate/ckFinish/planeiteFinish,
  weigh/share/leak) into a reusable streaming class that returns verdicts as structs
  instead of a write to the serial port. This is the canonical reference: the
  firmware formats its messages from here.

  float (like the firmware) to stay identical to the embedded code, except the hold
  accumulator in double. No dynamic memory, no Arduino dependency.

  中文：打印前料槽检查、平面度、称重、漏液。把固件内嵌逻辑移植为可复用的流式类，判定以结构体
  返回而非直接写串口；此为权威参考，固件据此格式化消息。用 float（与固件一致），保持等待累加器为
  double；无动态内存，不依赖 Arduino。
*/
#ifndef MSLA_BAC_H
#define MSLA_BAC_H

#include <cstdint>

namespace msla {

#define MSLA_CK_BINS 61     // 0 to 3 mm above the stop, step of 0.05 mm
#define MSLA_CK_BUF  1600   // 20 s of descent at 80 samples/s
#define MSLA_CK_HB   64     // sliding window at the end of the hold

// Reference recorded on a clean vat (kept by the firmware).
struct BacRef {
  uint32_t magic;                        // 0xB4C0 when valid
  float    curve[5][MSLA_CK_BINS];
  float    stat[5];
};
static const uint32_t MSLA_REF_MAGIC = 0xB4C0;

struct BacConfig {
  int   nCells;    // 1 (Z arm only) or 5 (Z arm + 4 corners)
  float holdS;     // expected bottom hold time (s)
  float ckStatN;   // static push threshold (N)
  float ckDescN;   // descent deviation threshold (N)
  float vatW, vatH;// vat dimensions between the cells (mm)
  float zMaxN;     // maximum Z motor force (N)
  float layerMm;   // first layer thickness (mm)
};

enum BacMode { BAC_MODE_CONTROLE = 0, BAC_MODE_REFERENCE = 1, BAC_MODE_PLANEITE = 2 };
enum BacArm  { BAC_ARME, BAC_ARM_ERR_PLANEITE_CELLS };
enum BacEvent { BAC_RAS, BAC_EV_CONTROLE, BAC_EV_PLANEITE, BAC_EV_REFERENCE };
enum BacRisque { RISQUE_AUCUN, RISQUE_MODERE, RISQUE_ELEVE, RISQUE_DANGER };

struct ControleResultat {
  bool    erreur;        // hold too short to conclude
  bool    maintienCourt; // real hold shorter than holdS (analyzed anyway)
  float   maintienReel;  // s
  bool    residu;        // true = debris seen
  float   appui;         // N
  bool    contact;       // contact detected during the descent
  float   zContact;      // mm above the stop
  float   epaisseur;     // zContact + layerMm
  bool    position;      // estimated position available (5 cells)
  float   px, py;        // mm
  BacRisque risque;
  bool    hasRef;        // a valid reference was used
};

struct ReferenceResultat { float appuiRef; };   // recorded stat[0]

struct PlaneiteResultat {
  bool  atteint5N;       // the sum of the 4 cells reached 5 N
  float dx, dy;          // shift of the force center at 5 N (mm)
  float zA5N;            // height above the stop where 5 N is reached (mm)
};

// Streaming vat controller. One instance (global in the firmware).
class BacControleur {
 public:
  BacControleur() : ck_(CK_OFF), ckMode_(0), ref_(nullptr) {}

  // Arms the controller for the next test. ref can be nullptr (no reference).
  BacArm armer(int mode, const BacConfig& cfg, BacRef* ref);

  // One measurement sample (t in s, z in mm, f[5] in N, uv on?).
  // Returns a non-zero event when a verdict was just produced.
  BacEvent maj(float t, float z, const float f[5], bool uv);

  bool actif() const { return ck_ != CK_OFF; }
  int  mode()  const { return ckMode_; }
  void desarmer() { ck_ = CK_OFF; }     // cancels a test in progress

  const ControleResultat&  controle()  const { return resCtrl_; }
  const ReferenceResultat& reference() const { return resRef_; }
  const PlaneiteResultat&  planeite()  const { return resPlan_; }

  // "Hold shorter than expected" warning of the last verdict.
  bool  maintienCourt() const { return maintienCourt_; }
  float maintienReel()  const { return maintienReel_; }

 private:
  enum CkState { CK_OFF, CK_WAIT, CK_DESC, CK_HOLD };
  int bidx(int n) const { return (bhead_ - bn_ + n + MSLA_CK_BUF) % MSLA_CK_BUF; }
  BacEvent finir();                     // ckFinish
  void     finirPlaneite(const float tare[5]);

  BacConfig cfg_{};
  CkState ck_;
  int ckMode_;
  BacRef* ref_;

  float bz_[MSLA_CK_BUF];
  float bf_[MSLA_CK_BUF][5];
  int   bn_ = 0, bhead_ = 0;
  float vHistT_[9], vHistZ_[9];
  int   vIdx_ = 0, vCount_ = 0;
  float stillSince_ = -1, zStop_ = 0, tStop_ = 0;
  double holdAcc_[5]{};
  int   holdN_ = 0;
  float hT_[MSLA_CK_HB];
  float hF_[MSLA_CK_HB][5];
  int   hHead_ = 0, hCount_ = 0;
  bool  maintienCourt_ = false;   // kept between maj() and finir()
  float maintienReel_ = 0;

  ControleResultat  resCtrl_{};
  ReferenceResultat resRef_{};
  PlaneiteResultat  resPlan_{};
};

/* ---------- Vat and resin weigh (pure analysis, 4 corners) ---------- */
static const float MSLA_GN = 0.00980665f;   // newtons per gram

struct PeseeConfig { float vatW, vatH; };
// Reference: empty vat weight, share per corner, resin density.
struct PeseeRef { float vatG; float share[4]; float density; };

struct PeseeResultat {
  bool  present;
  float total;        // g
  float sh[4];        // measured share
  float dev;          // maximum deviation from the reference share
  int   coinFaible;   // least loaded corner (-1 if no vat)
  bool  malPose;      // dev > 0.06 and corner < 12 %
  float cx, cy, off;  // estimated position (mm) and center offset
  bool  decale;       // off > 3 mm (if not badly placed)
  bool  resineConnue; // vatG known
  float resineMl;
};
// g[4] : grams per corner (front-L, front-R, rear-R, rear-L).
PeseeResultat analysePesee(const float g[4], const PeseeConfig& cfg, const PeseeRef& ref);

struct FuiteResultat { float lost; float rate; bool fuiteProbable; };
// m0, m1 : total mass (g) before/after; minutes : duration of the measurement.
FuiteResultat analyseFuite(float m0, float m1, float density, float minutes);

}  // namespace msla

#endif  // MSLA_BAC_H
