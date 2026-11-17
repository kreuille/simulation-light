# simulation-light

Simulations et outils de mesure.

## print/ : capteurs pour imprimante résine MSLA

Instrumentation d'une imprimante 3D résine à écran LCD : mesure de la force pendant l'impression, détection des pannes (pièce décollée, supports cassés, résidu dans le bac, film détendu), vérification que la résine est immobile avant l'exposition, et contrôle du bac avant chaque impression.

| Dossier | Contenu |
|---|---|
| [`print/simulateur/`](print/simulateur/index.html) | Simulateur des 3 solutions de mesure (cellule dans le bras Z, 4 cellules sous le bac, StallGuard), profils machine / résine / procédé / application, import des mesures réelles et recalage du modèle |
| [`print/firmware/`](print/firmware/banc_msla/banc_msla.ino) | Firmware ESP32 du banc de mesure : HX711, position Z par STEP/DIR, capteur UV, contrôle du bac autonome |
| [`print/outils/`](print/outils) | `logger.py` (enregistrement CSV), `layers_from_images.py` (surface de chaque couche) |
| [`print/exemples/`](print/exemples) | Enregistrement et fichier de couches d'exemple |
| [`print/tests/`](print/tests) | Tests du simulateur et du firmware |

Guide complet (matériel, montage, câblage, étalonnage, essais) : [`print/README.md`](print/README.md).

Les profils de machines et de résines du simulateur sont des valeurs indicatives génériques ; le recalage sur des mesures réelles les remplace.
