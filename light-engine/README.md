# Simulateur de précision d'impression LCD et DLP

**Simulateur en ligne : https://kreuille.github.io/simulation-light/light-engine/simulateur/**

Ce simulateur compare la précision de deux procédés d'impression 3D résine, **LCD (MSLA)** et **DLP**, sur une arcade dentaire synthétique ou sur un fichier STL. Il montre où et de combien l'impression s'écarte du modèle, en carte de couleur 3D, et teste des corrections de l'image projetée avant de toucher à la machine.

## Contenu

| Fichier | Rôle |
|---|---|
| `simulateur/index.html` | Le simulateur : une seule page HTML autonome (CSS et JavaScript inclus, aucune dépendance, aucun appel réseau). S'ouvre directement dans un navigateur, sur ordinateur ou téléphone |
| `tests/simulateur.test.js` | Tests du modèle et des traductions, lancés avec Node.js |

## Utilisation

1. Ouvrir la page en ligne, ou `simulateur/index.html` dans un navigateur.
2. Cliquer sur **« Lancer le guide pas à pas »** : le guide ouvre chaque groupe de réglages, entoure les paramètres concernés et explique ce qu'ils représentent. Les valeurs par défaut forment un exemple complet.
3. Sinon, régler directement les 6 groupes à gauche. Le bouton **i** à côté de chaque paramètre affiche son explication.
4. Lire les résultats à droite :
   - **Plan XY** : vue de dessus d'une coupe (orange : résine durcie en trop, turquoise : résine manquante) ;
   - **Coupe Z** : l'effet de l'axe Z, des couches et de la résine ;
   - **Modèle 3D** : l'erreur en chaque point, en couleur. Échelle automatique ou fixe, cartes LCD, DLP, côte à côte ou en différence ;
   - **Bilan** : le poids de chaque source d'erreur.
5. Choisir le modèle : arcade synthétique, ou **fichier STL** (ASCII ou binaire, en mm).
6. Choisir la langue avec le **globe** en haut à droite : français, English ou 中文. Le choix est mémorisé dans le navigateur (clé `light-engine-lang`) ; sans choix, la langue du système est utilisée, et l'anglais à défaut.

### Comparer deux réglages : onglet « Pixel A/B »

Pour voir l'effet d'un changement matériel, par exemple deux machines avec le même écran et la même source de lumière, l'une avec un verre sur l'écran et l'autre sans :

1. Ouvrir l'onglet **Pixel A/B**. Le bouton « Exemple : sans verre / avec verre » remplit A (sans verre) et B (verre de 1,1 mm). « Réglages principaux dans A et B » repart de vos réglages, que vous modifiez ensuite dans A ou B.
2. Régler chaque côté : distance écran → résine, épaisseur, indice et diffusion du verre, divergence, longueur d'onde, photobloqueur, dose.
3. Choisir la **zone éclairée** : un pixel, ou un bloc de 3 × 3 ou 7 × 7 pixels (un pixel isolé reçoit très peu de lumière dès que le flou dépasse sa taille).
4. Lire : l'image du pixel vue par la résine (même échelle de couleur pour A et B, contour orange = zone qui durcit), le profil d'intensité, un tableau (rayon du cône, flou de bord 10–90 %, intensité au centre, largeur durcie, lumière hors du pixel) et l'effet sur le motif XY imprimé (écart de contour, surface dans la tolérance, jeu entre dents, fissure).
5. « Utiliser A / B comme réglages principaux » reporte ce réglage dans les autres vues (3D, coupe Z, bilan).

Le verre ajouté est aussi réglable dans le groupe 2 (épaisseur, indice, diffusion), à 0 par défaut.

La compensation (mode « Machine learning » ou « Étape 3 / 4 ») modifie l'image projetée pour annuler une partie du flou. Le groupe 6 explore jusqu'où on peut éloigner le plan de polymérisation de l'écran LCD (≥ 1,5 mm) en restant proche de la précision du DLP.

## Hypothèses du modèle

- **Optique.** L'image au plan de polymérisation est l'image du masque convoluée par une réponse impulsionnelle (PSF) = disque (cône de lumière, rayon = jeu × tan θ, θ limité par l'angle maximal admis) ⊗ gaussienne. Diffusion : 15 µm par mm de jeu × (405/λ)⁴, plus un terme de diffusion dans la résine 25/(1 + 3·photobloqueur) × (λ/405)³. Le DLP n'a pas de cône : son flou vient de l'objectif et du DMD.
- **Dose.** Un point durcit si la dose reçue dépasse le seuil. Profondeur durcie Cd = Dp·ln(E₀/Ec), avec Dp = 250 µm × (λ/405)³ / (1 + 3·photobloqueur). Surdurcissement sous les surplombs.
- **Couches.** Effet d'escalier ≈ 0,29 × épaisseur de couche × |nz|.
- **Retrait.** Uniforme (un tiers du retrait volumique en linéaire, dont 20 % reste après mise à l'échelle du fichier) et différentiel (selon la conversion et l'aire de section, non corrigeable par l'échelle).
- **Verre ajouté sur l'écran.** Son épaisseur s'ajoute à la distance écran → résine : le cône gagne `épaisseur × tan(θ')` avec `sin θ' = sin θ / indice` (réfraction dans le verre), et la diffusion `15 µm/mm × (405/λ)⁴` s'applique à l'épaisseur du verre multipliée par un facteur de diffusion du verre (0,3 par défaut : **hypothèse**, à ajuster avec une mesure). Les pertes par réflexion ne sont pas modélisées.
- **Décollement du film.** Force = adhérence + effet ventouse (Stefan) selon l'aire de chaque couche, dispersion en hauteur, supports pour les surplombs de plus de 45°.
- **Modèle 3D.** Pour chaque sommet : jeu extérieur et épaisseur de paroi par lancer de rayons, décalage de bord 1D, courbure, aires de coupe, force de décollement cumulée.
- **Compensation ML.** Le gain vient d'une table mesurée sur des simulations (erreur de contour en µm RMS pour un pas de 50 µm : flou σ = 1 px, 69,5 µm sans correction, 10,3 µm avec la correction optimale ; σ = 3 px, 252 → 102 µm). Le facteur appliqué est `1 − q·(1 − optimal / sans correction)`, avec q la qualité du modèle réglée à l'écran.

## Limites

- **Aucun paramètre n'est calibré sur une impression réelle.** Les valeurs par défaut sont indicatives. Les écarts en µm donnent des ordres de grandeur et des comparaisons entre réglages, pas une prédiction pour une machine et une résine précises. Seules des mesures sur des pièces imprimées permettent de recaler le modèle.
- La compensation par apprentissage est **modélisée** à partir de simulations. Elle n'a pas été confirmée par un réseau entraîné ni par une impression.
- Le modèle 3D est analytique par sommet : ce n'est pas une simulation optique 3D complète.
- Les fichiers STL très lourds ralentissent le calcul ; le maillage est raffiné automatiquement.
- - La comparaison A/B décrit le pixel tel que le modèle le calcule, pas une mesure : l'écart entre deux réglages est plus fiable que les valeurs absolues.
- Les traductions anglaise et chinoise n'ont pas été relues par des locuteurs natifs.

## Tests

```
node light-engine/tests/simulateur.test.js
```

Le test exécute les scripts de la page dans Node avec un faux DOM et vérifie :
- le modèle (rayon du cône, diffusion, photobloqueur, longueur d'onde, profondeur durcie, DLP contre LCD, table de compensation, arcade 3D synthétique) ;
- les traductions (mêmes phrases en anglais et en chinois, paramètres `{0}` conservés, aide complète dans les trois langues, aucune phrase du code sans traduction) ;
- la fonction `T()` et le format des nombres selon la langue.
