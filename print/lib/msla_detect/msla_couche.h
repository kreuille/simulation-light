/*
  msla_detect — détection des pannes en cours d'impression (apprentissage par couche)
  -----------------------------------------------------------------------------------
  Portage fidèle des fonctions features/detect/verdict du simulateur.

  Principe : pour chaque couche on extrait des « features » (pic d'arrachement,
  compression à la descente, appui résiduel, course au pic). On apprend le niveau
  normal sur les 6 dernières couches saines, puis on signale toute couche qui s'en
  écarte (arrachement trop faible, obstacle, blocage, appui, film détendu).

  Entrées : séries échantillonnées (tableaux parallèles fournis par l'appelant).
  Sorties : un jeu de drapeaux par couche, puis un verdict (première couche en
  défaut, nature, nombre de fausses alertes, latence).

  double partout, aucune allocation dynamique, aucune dépendance Arduino.
*/
#ifndef MSLA_COUCHE_H
#define MSLA_COUCHE_H

#include <cstdint>

namespace msla {

#ifndef MSLA_MAX_COUCHES
#define MSLA_MAX_COUCHES 256   // nombre maximal de couches géré sans allocation
#endif

// Valeur « absente » (équivalent du null JS). Testée par estAbsent().
double absente();
bool   estAbsent(double v);

// Phases d'un échantillon (telles qu'émises par le simulateur et le firmware) :
//   0 = attente en haut / avant descente     1 = arrachement (remontée)
//   2 = descente                             3 = exposition (maintien en haut)
//   4 = maintien en bas
// La couche (ly) vaut -1 hors d'une couche identifiée.

// Série échantillonnée : tableaux parallèles de n cases, propriété de l'appelant.
struct Serie {
  const double* t;    // temps (s)
  const double* f;    // force déjà filtrée (voir prep())
  const double* z;    // position Z (mm)
  const int*    ph;   // phase 0..4
  const int*    ly;   // index de couche, ou -1
  int n;
};

// Features d'une couche. Un champ « absent » (NaN) signifie « non mesuré ».
struct Feature {
  double peak;    // pic d'arrachement (force max en phase 1, au-dessus de la tare haute)
  double comp;    // compression min (phase 2, ou phases 0/4 de la couche suivante)
  double travel;  // Z atteint au pic
  double stat;    // appui statique (différence de fin d'exposition entre couches successives)
  int    iPeak;   // index d'échantillon du pic (-1 si absent)
  int    iComp;   // index d'échantillon de la compression (-1 si absent)
};

// Drapeaux d'une couche, dans l'ordre d'apparition du simulateur.
enum Flag { F_NODATA = 0, F_CRASH, F_OBSTACLE, F_APPUI, F_FAIBLE, F_FEP };
struct LayerFlags {
  uint8_t n;       // nombre de drapeaux
  Flag    f[4];    // drapeaux ordonnés
};

// Seuils planancher par capteur (FLOOR du simulateur).
struct Floor { double p, c, s; bool sNull; };
Floor floorPour(const char* key);   // "z","fus","vat","sg"

// prep() : construit la force filtrée ff à partir de la force brute f.
//   med = true  -> ff = med3(f)        (sinon copie de f)
//   avg = true  -> ff = moy_glissante(ff, 3)
// ff et scratch : tampons de n cases fournis par l'appelant (scratch peut == nullptr si !avg&&!med).
void prep(const double* f, int n, bool med, bool avg, double* ff, double* scratch);

// features() : remplit out[0..nLayers) à partir de la série s.
//   scratch / scratchLen : espace de travail d'au moins la taille du plus grand
//   groupe (phase, couche) ; en pratique passer un tampon de taille s.n suffit.
void features(const Serie& s, int nLayers, Feature* out,
              double* scratch, int scratchLen);

// detect() : apprentissage par couche. Remplit flags[0..nLayers).
//   area    : surface par couche (cm²), nLayers cases
//   key     : "z","fus","vat","sg"
//   nm      : niveau de bruit adapté (noise * resFactor, ou noise pour "sg")
//   obstacleMul, lowRatio : paramètres de l'application (APPS)
//   fstall  : force de blocage du moteur (N)
void detect(const Feature* feat, const double* area, int nLayers,
            const char* key, double nm, double obstacleMul, double lowRatio,
            double fstall, LayerFlags* flags);

// Verdict d'une série de drapeaux.
struct Verdict {
  int  first;      // première couche en défaut (>= start), -1 si aucune
  Flag kind;       // nature du premier défaut
  int  fa;         // fausses alertes (couches en défaut avant start)
  int  nodata;     // couches sans données
  int  lat;        // latence (first - start), -1 si first == -1
  bool hasFirst;
};

// scen : "normal","detach","support","residue","fep" (table TRIGGER).
// start = (scen=="normal") ? nLayers : L.
Verdict verdict(const LayerFlags* flags, int nLayers, const char* scen, int L);

/* ================= Détection en flux (pour le firmware) ================= */
// Versions incrémentales de features/detect, pour tourner en direct couche par
// couche sans tout bufferiser. Elles produisent des résultats IDENTIQUES aux
// versions batch (validé par parité).

#ifndef MSLA_SUIVI_BUF
#define MSLA_SUIVI_BUF 1024   // échantillons bufferisés par couche pour les médianes (ph0, ph3)
#endif

// DetecteurCouche : apprentissage par couche en flux. On lui pousse les Feature
// finalisées dans l'ordre croissant ; il renvoie les drapeaux de chaque couche,
// identiques à detect().
class DetecteurCouche {
 public:
  void init(const char* key, double nm, double obstacleMul, double lowRatio, double fstall);
  LayerFlags pousser(const Feature& ft, double area);
 private:
  void ajouterBonne(const Feature& ft, double area);
  Floor  fl_{};
  bool   isSg_ = false;
  double nm_ = 0, obstacleMul_ = 0, lowRatio_ = 0, fstall_ = 0;
  Feature gFeat_[6]; double gArea_[6]; int gN_ = 0;   // fenêtre des 6 dernières bonnes couches
  Feature sFeat_[4]; double sArea_[4]; int sN_ = 0;   // série « faible » en attente (ré-apprentissage)
};

// SuiviCouches : construit les Feature couche par couche à partir du flux
// d'échantillons (force filtrée ff, position z, phase ph, couche ly), dans
// l'ordre du simulateur (bloc 4→0→1→3→2). pousser() renvoie l'index de la couche
// qui vient d'être finalisée (et remplit *out), ou -1 sinon. terminer() finalise
// la dernière couche en cours. Résultats identiques à features().
class SuiviCouches {
 public:
  void init();
  // area = aire exposée de la couche ly (mm²). Quand une couche est finalisée, son
  // aire est renvoyée dans *outArea (aire de la couche finalisée, pas de la courante).
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
  Acc* slotPour(int ly);          // existant ou nouveau
  Acc* slotExistant(int ly);
  void ensurePre(Acc* a);
  void ensureTop(Acc* a);
  void finaliser(Acc* a, Acc* suivant, Feature* out, double* outArea);
  void liberer(Acc* a);
};

}  // namespace msla

#endif  // MSLA_COUCHE_H
