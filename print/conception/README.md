# Conception — imprimante MSLA 10,1″ instrumentée

Étude de conception d'une machine complète : volume 218 × 123 × 250 mm, cellule de charge 50 kg à flexion
parallèle dans le bras Z, écran et bac posés sur un berceau porté par 4 cellules de 20 kg, source UV fixée au socle.

- `fusion360/MSLA_10pouces/` : script Fusion 360 (Utilitaires > Compléments > Scripts et compléments > « + »,
  choisir le dossier, Exécuter). Cotes en millimètres en tête du fichier ; `Z_POS` règle la hauteur du plateau.
  Le script affiche la masse du berceau et sa fréquence propre.
- Profil « Étude 10,1″ » dans le simulateur, avec les valeurs de cette machine.
- Firmware : saisir `bac 275 170` (espacement des appuis des cellules) pour la pesée et la planéité.

Chiffres clés : efforts dimensionnés à ±400 N (pire cas simulé ≈ 195 N), berceau de 2,0 à 3,1 kg,
fréquence propre 200 Hz à vide et 160 Hz avec 1 litre de résine, hauteur hors tout ≈ 660 mm.

Vérification sans Fusion (pas de chevauchement à 4 hauteurs du plateau, fréquence > 100 Hz) :
`python3 print/tests/test_fusion_geometrie.py`
