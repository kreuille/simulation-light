/*
  msla_detect — stabilisation de la résine
  ----------------------------------------
  Mesure, par couche, le temps que met la force à redevenir stable après la
  descente (attente avant exposition) et après l'arrachement (attente en haut).
  Sert à vérifier qu'un capteur « voit » que la résine est immobile avant
  d'exposer, et à comparer au réglage d'attente du profil.

  Portage fidèle de slopeAt/resStd/stability/stabSummary du simulateur.
  double partout, aucune allocation dynamique, aucune dépendance Arduino.
*/
#ifndef MSLA_STABILITE_H
#define MSLA_STABILITE_H

#include "msla_couche.h"   // Serie, MSLA_MAX_COUCHES

namespace msla {

// Pente (régression linéaire) de f sur [a,b] en fonction de t.
double slopeAt(const double* t, const double* f, int a, int b);
// Écart-type des résidus de cette régression sur [a,b].
double resStd(const double* t, const double* f, int a, int b);

// Résultat de stabilité d'une couche.
struct Settle {
  double ts;        // temps de stabilisation (s) relatif au début de l'attente
  bool   found;     // true si la stabilité a été détectée
  bool   tooShort;  // true si la fenêtre était trop courte pour conclure
  double sig;       // bruit estimé
  double thr;       // seuil de pente utilisé
};

struct StabOpt { double rate; double noise; };   // rate = échant./s, noise = bruit adapté
// Instants de référence par couche (vérité terrain du simulateur) ; NaN = absent.
struct Starts { const double* rest; const double* top; };

// Remplit down[1..n) (après descente) et top[0..n) (après arrachement).
// s.f doit contenir la force filtrée (ff). down/top : tampons de n cases.
void stability(const Serie& s, const char* key, const StabOpt& opt,
               const Starts& starts, int nLayers, Settle* down, Settle* top);

// Bilan de sensibilité d'un capteur comparé à la vérité terrain.
struct SensSum { int ok, fa, miss, okT; double adMean; int adEarly; };
struct Truth { const double* settleDown; const double* settleTop; };
struct ProcWait { double wait; double topWait; };

// Compte les couches où la stabilisation réelle dépasse le réglage (trueEarly/trueTop),
// et, pour un capteur donné, les accords/fausses alertes/manqués.
int trueEarlyCount(const Truth& truth, const ProcWait& Q, double margin, int nLayers);
int trueTopCount(const Truth& truth, const ProcWait& Q, double margin, int nLayers);
SensSum sensSummary(const Settle* down, const Settle* top, const Truth& truth,
                    const ProcWait& Q, double margin, int nLayers);

}  // namespace msla

#endif  // MSLA_STABILITE_H
