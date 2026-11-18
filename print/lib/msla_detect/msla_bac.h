/*
  msla_detect — contrôle du bac avant impression, planéité, pesée, fuite
  ----------------------------------------------------------------------
  Portage de la logique embarquée du firmware (ckArm/ckUpdate/ckFinish/
  planeiteFinish, pesée/répartition/fuite) en une classe réutilisable, en flux,
  qui renvoie des verdicts sous forme de structs au lieu d'écrire sur le port série.
  C'est la référence canonique : le firmware formate ses messages à partir d'ici.

  float (comme le firmware) pour rester identique à l'embarqué, sauf l'accumulateur
  de maintien en double. Aucune allocation dynamique, aucune dépendance Arduino.
*/
#ifndef MSLA_BAC_H
#define MSLA_BAC_H

#include <cstdint>

namespace msla {

#define MSLA_CK_BINS 61     // 0 à 3 mm au-dessus de l'arrêt, pas de 0,05 mm
#define MSLA_CK_BUF  1600   // 20 s de descente à 80 mesures/s
#define MSLA_CK_HB   64     // fenêtre glissante de fin de maintien

// Référence enregistrée sur bac propre (persistée par le firmware).
struct BacRef {
  uint32_t magic;                        // 0xB4C0 quand valide
  float    curve[5][MSLA_CK_BINS];
  float    stat[5];
};
static const uint32_t MSLA_REF_MAGIC = 0xB4C0;

struct BacConfig {
  int   nCells;    // 1 (bras Z seul) ou 5 (bras Z + 4 coins)
  float holdS;     // durée du maintien en bas attendue (s)
  float ckStatN;   // seuil d'appui statique (N)
  float ckDescN;   // seuil d'écart pendant la descente (N)
  float vatW, vatH;// dimensions du bac entre cellules (mm)
  float zMaxN;     // force maximale du moteur Z (N)
  float layerMm;   // épaisseur de la première couche (mm)
};

enum BacMode { BAC_MODE_CONTROLE = 0, BAC_MODE_REFERENCE = 1, BAC_MODE_PLANEITE = 2 };
enum BacArm  { BAC_ARME, BAC_ARM_ERR_PLANEITE_CELLS };
enum BacEvent { BAC_RAS, BAC_EV_CONTROLE, BAC_EV_PLANEITE, BAC_EV_REFERENCE };
enum BacRisque { RISQUE_AUCUN, RISQUE_MODERE, RISQUE_ELEVE, RISQUE_DANGER };

struct ControleResultat {
  bool    erreur;        // maintien trop court pour conclure
  bool    maintienCourt; // maintien réel plus court que holdS (analyse quand même)
  float   maintienReel;  // s
  bool    residu;        // true = résidu vu
  float   appui;         // N
  bool    contact;       // contact détecté pendant la descente
  float   zContact;      // mm au-dessus de l'arrêt
  float   epaisseur;     // zContact + layerMm
  bool    position;      // position estimée disponible (5 cellules)
  float   px, py;        // mm
  BacRisque risque;
  bool    hasRef;        // une référence valide a servi
};

struct ReferenceResultat { float appuiRef; };   // stat[0] enregistré

struct PlaneiteResultat {
  bool  atteint5N;       // la somme des 4 cellules a atteint 5 N
  float dx, dy;          // décalage du centre d'effort à 5 N (mm)
  float zA5N;            // hauteur au-dessus de l'arrêt où 5 N est atteint (mm)
};

// Contrôleur de bac en flux. Une instance (globale dans le firmware).
class BacControleur {
 public:
  BacControleur() : ck_(CK_OFF), ckMode_(0), ref_(nullptr) {}

  // Arme le contrôleur pour le prochain test. ref peut être nullptr (pas de référence).
  BacArm armer(int mode, const BacConfig& cfg, BacRef* ref);

  // Un échantillon de mesure (t en s, z en mm, f[5] en N, uv allumé ?).
  // Renvoie un événement non nul quand un verdict vient d'être produit.
  BacEvent maj(float t, float z, const float f[5], bool uv);

  bool actif() const { return ck_ != CK_OFF; }
  int  mode()  const { return ckMode_; }
  void desarmer() { ck_ = CK_OFF; }     // annule un test en cours

  const ControleResultat&  controle()  const { return resCtrl_; }
  const ReferenceResultat& reference() const { return resRef_; }
  const PlaneiteResultat&  planeite()  const { return resPlan_; }

  // Avertissement « maintien plus court que prévu » du dernier verdict.
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
  bool  maintienCourt_ = false;   // mémorisé entre maj() et finir()
  float maintienReel_ = 0;

  ControleResultat  resCtrl_{};
  ReferenceResultat resRef_{};
  PlaneiteResultat  resPlan_{};
};

/* ---------- Pesée du bac et de la résine (analyse pure, 4 coins) ---------- */
static const float MSLA_GN = 0.00980665f;   // newtons par gramme

struct PeseeConfig { float vatW, vatH; };
// Référence : poids du bac vide, répartition par coin, densité de la résine.
struct PeseeRef { float vatG; float share[4]; float density; };

struct PeseeResultat {
  bool  present;
  float total;        // g
  float sh[4];        // répartition mesurée
  float dev;          // écart maximal à la répartition de référence
  int   coinFaible;   // coin le moins chargé (-1 si bac absent)
  bool  malPose;      // dev > 0,06 et coin < 12 %
  float cx, cy, off;  // position estimée (mm) et décalage au centre
  bool  decale;       // off > 3 mm (si pas mal posé)
  bool  resineConnue; // vatG connu
  float resineMl;
};
// g[4] : grammes par coin (avant G, avant D, arrière D, arrière G).
PeseeResultat analysePesee(const float g[4], const PeseeConfig& cfg, const PeseeRef& ref);

struct FuiteResultat { float lost; float rate; bool fuiteProbable; };
// m0, m1 : masse totale (g) avant/après ; minutes : durée de la mesure.
FuiteResultat analyseFuite(float m0, float m1, float density, float minutes);

}  // namespace msla

#endif  // MSLA_BAC_H
