/*
  msla_detect — filtres de signal
  --------------------------------
  Filtres portés à l'identique du simulateur (print/simulateur/index.html) pour
  garantir des verdicts identiques entre le JS et la bibliothèque C++.

  Tout est en double précision, comme le Number de JavaScript, afin que les
  comparaisons de seuils donnent exactement le même résultat des deux côtés.

  Aucune allocation dynamique : les tampons de sortie et de travail sont fournis
  par l'appelant. Aucune dépendance Arduino.
*/
#ifndef MSLA_FILTRES_H
#define MSLA_FILTRES_H

namespace msla {

// Médiane d'un tableau de n valeurs, sans modifier « a ».
// « scratch » doit contenir au moins n cases ; il est utilisé comme espace de tri.
// Renvoie 0 si n <= 0 (comme median() du simulateur sur un tableau vide).
double mediane(const double* a, int n, double* scratch);

// Médiane glissante sur 3 points. Les extrémités sont recopiées telles quelles.
// « in » et « out » peuvent pointer vers des tampons différents de n cases.
void med3(const double* in, double* out, int n);

// Moyenne glissante de fenêtre « w » centrée (demi-fenêtre = w/2), bords inclus.
// Reproduit movAvg() du simulateur : moyenne des voisins valides seulement.
void moy_glissante(const double* in, double* out, int n, int w);

}  // namespace msla

#endif  // MSLA_FILTRES_H
