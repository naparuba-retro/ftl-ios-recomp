# FTL (iPad 1.5.10) — port PC / Linux par recompilation statique

Ce dépôt reconstruit **à partir de votre propre copie** de l'IPA de *FTL: Faster Than Light* (iPad, version 1.5.10) un exécutable natif
pour Windows et Linux (Steam Deck). Le code ARM est traduit en C (« recompilation statique ») puis compilé avec un runtime hôte
(fichiers, threads, OpenGL ES 2 → OpenGL de bureau, son, entrées). Une traduction française est incluse (voir plus bas).

> **Aucun contenu du jeu n'est dans ce dépôt** : ni l'IPA, ni `ftl.dat`, ni le code C généré à partir du binaire, ni les images/sons.
> Vous devez posséder le jeu. Tout ce qui est généré reste dans le dossier `build/` (ignoré par git).

## Prérequis
| | Windows | Linux |
|---|---|---|
| Python | 3.11+ | 3.11+ |
| Ghidra | 12.2 (https://ghidra-sre.org) | idem |
| JDK | 21+ (ex. Temurin) | idem |
| Compilateur | Visual Studio Build Tools (C++ x64) | gcc, make, `libsdl2-dev`, `libgl-dev` |
| L'IPA | `FTL__Faster_Than_Light_1.5.10_ios_6.0.ipa`, **déchiffré** | idem |

L'IPA doit avoir exactement le SHA-256 indiqué dans [`work/recomp/binary.toml`](work/recomp/binary.toml) (`62bfa1cc…ea3e81b9`) :
plusieurs adresses du binaire sont utilisées par le runtime. Ghidra et le JDK peuvent être posés dans `./tools/` (`ghidra_*/`, `jdk*/`)
ou indiqués par `--ghidra` / `--jdk` (ou `GHIDRA_INSTALL_DIR` / `JAVA_HOME`).

## Utilisation
```
python build_from_ipa.py --ipa FTL__Faster_Than_Light_1.5.10_ios_6.0.ipa
python build_from_ipa.py --ipa ... --pc-ftl-dat "C:/Games/FTL/ftl.dat.vanilla"     # avec la traduction française
```
Le jeu prêt à lancer est dans `build/game/` (`lancer_ftl.bat` sous Windows, `run.sh` sous Linux).
Étapes (`--only` / `--skip`) : `extract` → `ghidra` → `lift` → `patch` → `compile` → `game` → `fr`.
Les étapes Ghidra/Lift sont longues et mémorisées (`--force` pour les refaire).

## Traduction française
`--pc-ftl-dat` désigne le `ftl.dat` de **votre** version PC de FTL contenant le français officiel (vanilla, sans mod). Il n'est lu qu'en lecture seule.
Les textes (≈ 94 % des chaînes), les polices (accents) et 29 images de boutons/bandeaux sont repris ou recomposés ; voir [`work/fr/LISEZMOI.md`](work/fr/LISEZMOI.md).
Restent en anglais : le texte dessiné dans les grands atlas (hangar, HUD) et quelques chaînes codées dans l'exécutable.

## Contenu du dépôt
- `build_from_ipa.py` — chaîne complète.
- `work/scripts/` — scripts Ghidra : `Lift.java` (le traducteur ARM → C), `FixSwitch.java` (tables de commutation), outils d'analyse.
- `work/recomp/` — runtime hôte (`rt.c`, `hle_*.c`, `gl_host.c`+`gl_sdl.inc`, `plat.h` pour la portabilité Windows/Linux), `binary.toml`
  (adresses propres au binaire), `patches_apres_lift.py`, scripts de compilation.
- `work/fr/` — patch de traduction. `steamdeck/` — lanceurs Linux. `NOTES-ANDROID.md` — journal technique détaillé.

## Outils de test intégrés
Variables d'environnement : `FTL_FRAMES=n` (s'arrête après n images), `FTL_SEED`, `FTL_FIXEDTIME=1`, `FTL_SHOT=a,b,c` (captures),
`FTL_CLICK="image,x,y;…"`, `FTL_FPS=1`, `FTL_NOSOUND=1`, `FTL_WINDOWED=1` / `FTL_FULLSCREEN=1` (Linux).
**Enregistrement / relecture** : `FTL_RECORD=auto` enregistre les entrées et la graine dans `replays/` ; `FTL_REPLAY=<dossier>` rejoue la partie à l'identique
(utile pour reproduire un plantage).

## Licence et mentions
Le code de ce dépôt (traducteur, runtime, scripts) est destiné à la préservation et à l'interopérabilité ; la licence reste à définir par l'auteur.
*FTL: Faster Than Light* appartient à Subset Games. La bibliothèque SIL, utilisée par le jeu, a sa propre licence et n'est pas incluse.
