# Patch francais (version iOS -> textes officiels PC)

Source (LECTURE SEULE, jamais modifiee) : `C:\Games\FTL Advanced Edition\ftl.dat.vanilla` (francais officiel : data/text-fr.xml + text_*.xml anglais pour les identifiants).
Le `ftl.dat` Multiverse n'est pas utilise.

Chaine : `fr_build.py` (cherche chaque texte anglais iOS dans le PC -> identifiant -> francais) ;
`make_fonts_fr.py` (accents dans JustinFont7/10/11/12/12Bold) ; `apply_fr.py` (ajoute les fichiers modifies en fin de ftl.dat + corrige l'index).

    python fr_build.py "C:\Games\FTL Advanced Edition\ftl.dat.vanilla" ftl_ios_en.dat out
    python make_fonts_fr.py ftl_ios_en.dat out
    python apply_fr.py ftl_ios_en.dat out ftl_fr.dat

Resultat : `out\rapport_traduction.tsv` (chaque texte : fichier, balise, cle, palier, anglais, francais).
Installation dans `jeu_pc` : `ftl.dat` = francais, `ftl_en.dat` = anglais ; `langue_en.bat` / `langue_fr.bat` echangent les deux.

Taux (textes du jeu dans data/*.xml, 5203 chaines) : 4891 traduites = 94,0 %
  A exact 4770 | B quasi exact (ponctuation/casse, memes nombres) 72 | C fragments 49 | non traduites 312.
Non traduites : textes de developpement jamais affiches (Boss Laser, Dummy blueprint...), descriptions d'armes de l'iOS dont le texte
differe du PC (nombres/valeurs), 37 variantes nameEvents.xml propres a l'iOS, ~130 fragments de misc.xml que le code iOS assemble.
Hors perimetre (non traduit) : textes dessines dans les images (boutons CONTINUE/NEW GAME..., fenetre d'avertissement, etc.) et
quelques chaines codees en dur dans l'executable ("Continue...", "Version ", messages du piratage).

## Boutons et bandeaux (images)
Les boutons ne sont ni dans le code ni dans les XML : ce sont des images (.tex) dans ftl.dat. `make_buttons_fr.py` recompose les versions françaises
à partir des images fr du PC (img/loc-fr/*.png), au format iOS : 29 images (menu principal : CONTINUER / NOUVELLE PARTIE / TUTORIEL / STATS / OPTIONS / CRÉDITS
x 3 états ; bandeaux de pause : portes, téléporteur, piratage, contrôle mental, ciblage ; pause auto ; avertissement édition avancée).
Menu : taille d'origine conservée (position gauche fixe dans le jeu), texte aligné à droite et réduit s'il est trop long (NOUVELLE PARTIE).
Non traduit : texte dessiné dans les grands atlas (boutons du hangar RENAME/LIST/START..., HUD JUMP/SHIP/STORE/PAUSE, titres de panneaux, "SYSTEM NOT INSTALLED"),
écran de chargement, MISS/RESIST, texte de carte (pas d'image équivalente utilisable : le PC les dessine avec une police).
Chaîne complète : fr_build.py -> make_fonts_fr.py -> make_buttons_fr.py -> apply_fr.py.
