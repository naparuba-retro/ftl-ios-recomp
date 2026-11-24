# FTL (iPad 1.5.10) : notes pour un port Android

État au 2026-11-22. Tout ce qui est marqué « à vérifier » est une hypothèse, pas un fait établi.

## 1. Ce qu'on a (dans `C:\SOURCES\ftl`)

| Élément | Où | État |
|---|---|---|
| IPA d'origine | `FTL__Faster_Than_Light_1.5.10_ios_6.0.ipa` | non modifié |
| Binaire `FTL` | `work\FTL` | armv7 32 bits, **déchiffré** (cryptid 0), 9412 symboles C++ |
| Projet Ghidra analysé | `work\proj` (Ghidra 12.2 + JDK 25 dans `tools\`) | analyse OK |
| Contenu de `ftl.dat` | `work\extracted` (1339 fichiers) | extraction validée |
| Images en PNG | `work\png` (950) | voir réserves §5 |
| Sprites d'interface | `work\sprites` (1135) | découpés depuis les atlas |
| Sources de SIL 0.5f | `sil_src\0.5f\SIL-0.5f` | GPLv3+, contient `sysdep/android` |
| Outils maison | `work\ftl_dat.py`, `convert_all.py`, `split_atlas.py`, `tex2png.py`, `work\scripts\*.java` | |

Pour relancer Ghidra en ligne de commande, il faut un dossier de réglages simple, sinon le composant OSGi plante avec le profil Windows :
`JAVA_TOOL_OPTIONS=-Dapplication.settingsdir=C:\SOURCES\ftl\work\gsettings` et `JAVA_HOME=tools\jdk25\jdk-25...`.

## 2. Architecture du jeu (établie)

- Le jeu = **code FTL en C++** + **bibliothèque SIL** (System Interface Library, Andrew Church) compilée dedans. Chemins de build visibles : `/Users/zasdarq/FTL/Project/{src,lib/SIL}`.
- **Les ressources passent par SIL** : `ResourceControl::OnInit` fait `pkg_create_instance("ftl.dat","")` puis `resource_register_package`. Les images se chargent avec `LoadImageFromResourceFile` (préfixe `img/`), les XML avec `LoadFile`.
- **Format `ftl.dat`** (PKG, gros-boutiste) : en-tête 16 o (`PKG\n`, u16 16, u16 20, u32 nb, u32 taille du pool de noms), entrées de 20 o (hash, drapeaux<<24 | offset du nom, offset, taille stockée, taille décompressée), pool de noms. Drapeau 1 = zlib. Confirmé par `package_pkg_init` (binaire) et `package-pkg.c` (SIL).
- **Contenu de jeu = celui du PC** (`blueprints.xml`, `events.xml`, DLC…). L'iPad ajoute surtout `img/ipad/` (75 fichiers) et des atlas.
- **Le tactile est déjà dans le code du jeu** : `SILView` (Objective-C) reçoit `touchesBegan/Moved/Ended/Cancelled` et appelle `CApp::OnTouchDown/Move/Up/Cancel`, qui dispatchent vers un `OnTouch(TouchAction, x, y, …)` par écran (≈40 classes). Il y a aussi `TouchTooltip`, `IsTouchInHitBox`, `CEvent::IsTouchActive`, `IsKeyboardPresent`.
- **Objective-C propre à l'app** (à remplacer côté Android) : `SILAppDelegate`, `SILView`, `SILViewController`, `AudioNotificationHandler`, `AuthChangeHandler`, `DialogAlertViewDelegate`, `OSKTextFieldDelegate`, `SILFileDataProtocol`.
- **Frameworks Apple importés** : OpenGLES (83 symboles, GL ES 2 + extensions VAO), AudioToolbox/AVFoundation (AudioUnit, AVAssetReader), UIKit (46), GameKit (15, succès Game Center), Foundation/CoreFoundation, CoreText, CoreGraphics, QuartzCore (CADisplayLink, CAEAGLLayer), libstdc++ **dynamique**, libvorbisidec/freetype/zlib **statiques**.

## 2 bis. Format `.tex` (confirmé par `include/SIL/texture.h` et `src/utility/tex-file.h`)

En-tête de 32 o : `TEX\n`, u8 version (2), u8 format, u8 mipmaps, u8 bitmap opaque, i16 largeur, i16 hauteur, i32 échelle (16.16), i32 offset pixels, i32 taille pixels, i32 offset bitmap, i32 taille bitmap.
Formats vus : `0x08` BGRA8888 (929 fichiers), `0x85` PVRTC4 RGBA (9), `0x86` PVRTC2 RGB (12).

## 3. Ce qui existe déjà pour Android (SIL 0.5f)

`src/sysdep/android/` : `SILActivity.java`, `activity.c`, `main.c`, `graphics.c`, `input.c`, `sound.c`, `files.c`, `thread.c`, `userdata.c`, `sysfont.c`, etc. Supporte arm64, fichiers d'extension Google Play (`android_expansion_file_path()` → utilisable directement avec `pkg_create_instance`), orientation paysage par défaut. Outils : `tools/build-pkg.c`, `extract-pkg.c`, `pngtotex.c`.

Prérequis annoncés dans `HISTORY.txt` (à revérifier dans `README-android.txt`) : NDK r14 ou plus récent, SDK récent, pas de GCC (Clang).

## 4. Ce qu'il faut faire (par ordre)

1. **Récupérer du code compilable pour arm64/x86-64.** Le binaire est **armv7 uniquement** ; beaucoup de téléphones récents n'exécutent plus l'ARM 32 bits. Trois voies, de la moins à la plus risquée :
   - a. **Émuler/traduire le binaire armv7** et fournir en HLE la libc, libstdc++, le runtime Objective-C, UIKit minimal, EAGL/OpenGL ES, AudioUnit (approche « touchHLE »). Sur PC, c'est aussi la voie du prototype ; sur Android, on peut charger l'ARM 32 bits seulement si l'appareil le permet.
   - b. **Recomp statique armv7 → C/arm64** guidée par les 9412 symboles. Long, fragile, mais produit du code natif.
   - c. **Réécrire la logique** en s'appuyant sur le moteur PC et le reverse existants (Hyperspace), en copiant l'UI tactile de l'iPad (layouts dans `img/ipad`, `data/tooltips.xml`, code `OnTouch*`). Pas de binaire iOS à traduire, mais énorme travail de réécriture.
2. **Remplacer `SILView`/`SILViewController`** par l'activité SIL Android : appeler `CApp::OnTouch*` avec les coordonnées converties.
3. **Brancher SIL Android** : rendu GL ES 2/3 (vérifier `glDiscardFramebufferEXT`, `GL_OES_vertex_array_object`), audio (SIL fournit `sound.c`), fichiers.
4. **Couper ou remplacer Game Center** (`GKLocalPlayer`, `GKAchievement`) : désactiver, ou Google Play Games.
5. **Textures PVRTC** (21 fichiers, planètes + fonds) : PVRTC n'est supporté que par les GPU PowerVR. Les recompresser en ETC2 ou ASTC, ou les décompresser en RGBA au chargement.
6. **Clavier virtuel** (`OSKTextFieldDelegate`, `IsKeyboardPresent`) pour la saisie des noms.
7. **Données** : fournir `ftl.dat` comme fichier d'extension (164 Mo, > limite APK de 100 Mo) ou le lire depuis le stockage de l'application.
8. **Tester sur appareil** avec un `ftl.dat` de l'utilisateur.

## 5. Réserves et risques connus

- **Décodeur PVRTC écrit de mémoire**, non comparé à une référence. Pour la version finale, recompresser depuis les originaux avec un outil officiel.
- **Version de SIL dans le binaire inconnue** (2014 environ) ; l'API de la 0.5f a pu changer. Comparer `core.c`/`package-pkg.c` avec les fonctions décompilées avant de réutiliser du code.
- **Pas de source de FTL.** Seul SIL est open source.
- **Mode 1 du PVRTC 2 bpp** (damier) implémenté de façon simplifiée.

## 6. Légal

- SIL : GPLv3+ (ou accord séparé avec l'auteur). `external/` a ses licences propres.
- Le code et les assets de FTL appartiennent à Subset Games. Un port reste à usage personnel avec les fichiers de l'utilisateur ; ne pas redistribuer `ftl.dat`, le binaire ni les PNG extraits. Idéalement, distribuer un patcher qui part de l'IPA acheté.
- Le déchiffrement FairPlay n'a pas été nécessaire ici (IPA déjà déchiffré).

## 7. Recompilation ARM -> C (version PC d'abord, puis Android)

Choix retenu : **recompilation statique du binaire armv7**, à partir du **p-code de Ghidra** (pas du décompilateur). Le même C sert ensuite pour Android (arm64 ou x86-64), sans ARM 32 bits.

### État (2026-11-22)
- `work\scripts\Lift.java` : traduit chaque instruction ARM/Thumb (via son p-code) en C. Une fonction C `f_<adresse>()` par fonction du binaire. Registres invités = tableau `REGS` (offsets de Ghidra), mémoire invitée = `MEM + adresse 32 bits`, appels directs = appels C, appels indirects = `rt_call_ind` (table adresse -> fonction), externes = `ext_<nom mangle>()` à fournir en HLE.
- `work\recomp\rt.h`, `rt.c` : runtime minimal (4 Go réservés, chargement de l'image, tas bump, `malloc/free/memcpy/memset/__umodsi3`).
- **Validé** : `decompress_to` (zlib : inflate, adler32, crc32, zalloc…) recompilé tel quel sur 73 fichiers de `ftl.dat` (38,9 Mo) -> **sortie identique à Python octet par octet** (`work\recomp\test_zlib.exe gen_zlib vec.bin`, compilé avec MSVC BuildTools, `build_zlib.bat`). Ça couvre boucles, drapeaux, appels directs/indirects, pile, Thumb, ARM et copies NEON.
- Traduction complète : `Lift.java ALL` -> **5831 fonctions, 600 336 instructions, 369 externes**. Version initiale : 326 Mo de C. **Avec l'élimination des drapeaux morts : 114 Mo (49 fichiers, 3,8 M de lignes)**, (1 526 551 opérations p-code gardées, 1 680 404 supprimées, soit 52 %). **Tout ce C compile sans erreur avec MSVC (`/Od`, 50 fichiers objets)** ; la compilation complète prend quelques minutes en parallèle (`/MP`), fichier de test `work
ecomp\cc_all.bat`. Le dossier `work
ecomp\gen` (~120 Mo) est reproductible : `Lift.java ALL`. Le test zlib passe toujours (73/73 identiques) et tourne 9 fois plus vite (0,5 s au lieu de 4,4 s).

### Ce qu'il reste (par ordre)
1. **Réduire encore le C généré** (déjà fait : vivacité des drapeaux `NG/ZR/CY/OV`, `tmpNG/tmpZR/tmpCY/tmpOV`, `shift_carry`, `Q`, `GE*`, par fonction ; les drapeaux sont considérés morts aux appels et retours selon l'ABI). Pistes restantes : repli de constantes, propagation de copies, promotion des registres r0-r12 en variables locales dans les fonctions sans appel indirect.
2. **Opérations non traduites** (comptées sur tout le binaire) : NEON/VFP vectoriel (`FloatVectorAdd/Sub/Mult…`, `FixedToFP`, `FPToFixed`, `Vector*` : une trentaine de `CALLOTHER`), registres de 16 et 32 octets (`sortie_taille_16/32`, ~400 ops), 101 `BRANCHIND` sans cible résolue. Chaque `CALLOTHER` manquant fait `UNSUP` (arrêt) à l'exécution.
3. **HLE des 369 fonctions externes** (liste dans `work\recomp\externs.txt`) : libc/libgcc (~130), OpenGL ES (73 -> OpenGL desktop/GLES Android), libstdc++ (`std::string` COW ABI de 2014, `rb_tree`, flux, `operator new/delete`, `__cxa_*`), runtime Objective-C et UIKit/GameKit/CoreFoundation/Audio (~60, à remplacer par SIL côté PC/Android). Les structures C++ invitées (`std::string`, `std::vector`…) gardent leur disposition ARM 32 bits en mémoire invitée.
4. **Pointeurs de données vers symboles externes** (`__nl_symbol_ptr`, vtables libstdc++, `___stack_chk_guard`, `OBJC_CLASS_$_*`) : à pré-remplir dans la mémoire invitée au chargement.
5. **Threads** : SIL en utilise (`sil_main_thread`, décompression en arrière-plan). `REGS` est déjà `thread-local` ; `ldrex/strex` (`hasExclusiveAccess`) est traduit en « toujours réussi », **à remplacer par de vrais atomiques**.
6. **Exceptions C++** : non gérées (tables de déroulement non exploitées). À vérifier si le jeu en lève.
7. **Remplacer la plateforme** : fournir un `main` qui appelle `sil_main`/`CApp::OnExecute` avec une fenêtre/GL (SDL ou SIL Windows/Linux, ou activité Android) et mappe les événements tactiles vers `CApp::OnTouch*`.
8. **Perf** : 9 Mo/s sur l'inflate en `/O1` sans élagage ; acceptable pour ce jeu mais à surveiller après l'étape 1.

### Avancement du démarrage sans fenêtre (2026-11-22, suite)
Construction : `work
ecompuild_game.bat` (compile les HLE + lie `ftl_pc.exe` avec `gen\obj`), `cc_gen.bat` (compile le C généré), régénération : `Lift.java ALL OVERRIDE:_sys_ OVERRIDE:_ios_`. Lancement : `FTL_FRAMES=60 ftl_pc.exe <dossier contenant ftl.dat>`.
- **Remplacement de la couche plateforme par des fonctions hôtes** : les 89 fonctions `sys_*` de SIL (fichiers, temps, threads, mutex, sémaphores, userdata ; graphisme/son/entrées « nuls ») et les 32 `ios_*` (dimensions d'écran, chemins, GameKit, clavier) sont remplacées au moment de la traduction (`OVERRIDE:`) par `hle_sys.c`. Le binaire est un SIL **0.3** ou antérieur (API `sys_sound_setdata`, `sys_framebuffer_apply`) : les signatures viennent de `sil_src\old .3`. `sil__main` prend `sil_main` en 3e argument dans cette version.
- **HLE écrits** : libc/libgcc/maths/mach (`hle_libc.c`), `std::string` COW de libstdc++ 4.2 sur la mémoire invitée (`hle_string.c`), NEON/VFP vectoriel (`neon.c`, ~30 opérations, type d'élément lu dans le mnémonique). 232 externes sur 491 implémentées, 259 en stubs qui arrêtent le programme avec le nom de la fonction et l'appelant (`make_stubs.py`).
- **Résultat** : les 102 initialiseurs statiques C++ du jeu s'exécutent, puis `sil__main` -> `init_all` -> `sil_main` démarre.
- **iostream émulé (fait).** `hle_stream.c` émule `std::stringstream/istringstream/ostringstream` de libstdc++ 4.2 sur la mémoire invitée : `ios_base`, `basic_ios::init/clear`, `locale`, `stringbuf::str/_M_sync`, `ostream::_M_insert<T>`/`operator<<`, `istream::_M_extract<T>`/`operator>>`/`getline`, `std::getline`. Les VTT/vtables de libstdc++ (symboles de données vides dans l'image) sont reconstruites à l'initialisation (`hle_stream_init_data`) : seuls les mots à `vptr-12` (décalage de la base virtuelle) comptent ; dispositions relevées dans le code du jeu : stringstream ios@0x34, istringstream ios@0x30, ostringstream ios@0x2c ; stringbuf = vptr, 6 pointeurs, locale, mode, std::string ; basic_ios : streambuf@0x78, iostate@0x14.
- **Autres HLE ajoutés** : pthread/semaphores mach/sched (`hle_thread.c`, workqueue de SIL), `std::_Rb_tree_*` (`hle_rbtree.c`), fichiers POSIX, setjmp (retour 0, longjmp fatal), 30+ opérations NEON, constantes et opérations binaires 16/32 octets (temporaires qui se chevauchent regroupés en tampon d'octets ; l'idiome SLEIGH `LOAD(const,…)` est une copie).
- **Pièges rencontrés** : les petites fonctions `sys_*` qui ne sont qu'un saut vers une fonction mach sont vues comme des thunks (la traduction suit les thunks : voir `semaphore_*`, redirigés vers la table de `hle_sys.c`) ; la position de fichier est sur **32 bits** dans cette version de SIL (`sys_file_read_async/read_at`).
- **RÉSULTAT (2026-11-22) : le jeu s'exécute de bout en bout sans fenêtre.** `FTL_FRAMES=600 ftl_pc.exe <dossier de ftl.dat>` charge toutes les ressources (3973 lectures de ftl.dat, XML parsés, atlas, polices), exécute **600 images de la boucle principale**, puis `sil__main` retourne 0. Graphisme/son/entrées sont « nuls » : rien n'est affiché.
- **Prochaines étapes** : (1) vrai rendu : implémenter `sys_graphics_*`/`sys_texture_*` (le jeu appelle `sys_graphics_create_primitive/draw_primitive`, textures RGBA/BGRA/PVRTC) avec OpenGL (WGL sous Windows, puis GLES pour Android) ; (2) entrées : `sys_input_*` + clavier/souris -> événements SIL, tactile -> `CApp::OnTouch*` ; (3) son : `sys_sound_setdata` appelle un décodeur Ogg (déjà dans le binaire) -> WASAPI/AAudio ; (4) sauvegardes (`sys_userdata_perform`) ; (5) perf : `/O2`, réduire encore le C ; (6) pour Android : même C généré en arm64 + couche plateforme SIL Android (voir §3).
- **Non traité** : 199 externes encore en stubs (OpenGL ES 73, Objective-C/UIKit/CoreFoundation/Audio ~90 : hors du chemin d'exécution actuel), 97 sauts indirects sans cible et `vuzp` non traduits, longjmp, exceptions C++, atomiques réels (`ldrex/strex` traité comme toujours réussi) alors que le jeu a des threads.

### RENDU OPENGL : le menu principal s'affiche (2026-11-22)
Capture : `menu_principal_pc.png` (à la racine). `FTL_FRAMES=130 FTL_SHOT=90 FTL_GLDEBUG=1 ftl_pc.exe <dossier de ftl.dat>` ouvre une fenêtre 1024x768, rend le jeu avec le **vrai code de rendu de SIL (opengl_*) recompilé** et les 73 appels OpenGL ES 2 traduits vers OpenGL de bureau (`gl_host.c`, contexte WGL en profil de compatibilité), puis écrit `shot_90.png`. Traces : `FTL_GLTRACE=1` (tous les appels GL), `FTL_TRACE=1` (fichiers/sémaphores).
- **Architecture** : on ne remplace plus `sys_graphics_*`/`sys_texture_*`/`sys_framebuffer_*` (code générique, recompilé tel quel) ; on ne remplace que les aides iOS (`ios_present_view` = SwapBuffers, `ios_vsync`, `ios_get_frame_counter` à 60 Hz) et les `sys_file/time/thread/mutex/semaphore/userdata/input/sound`. Les shaders GLSL ES 1.00 sont convertis en GLSL 1.20 à la volée (préfixe `#version 120`, qualificateurs de précision retirés). Les textures PVRTC (planètes, fonds) sont décodées en RGBA par `pvrtc.c` (le jeu croit que le GPU gère PVRTC).
- **Bogues du traducteur trouvés grâce au rendu** : (1) `vmov.f32 Dd,#imm` NEON : Ghidra n'expanse pas l'immédiat (imm8=0 devait donner 2.0), d'où une matrice de projection à échelle nulle et un écran noir -> décodage de l'imm8 depuis les octets (`emitVmovF32`) ; (2) un remplacement (`OVERRIDE`) doit primer sur le suivi des thunks ; (3) tables de commutation ARM non reconnues (`adr rT,table ; ldr r0,[r0,rT] ; add pc,r0,rT`) : script `FixSwitch.java` (à n'appliquer qu'aux sauts sans cible, sinon il abîme le projet Ghidra ; projet sain : `work\proj2`).
- **Reste visible à l'écran** : une boîte de dialogue vide au-dessus du menu (texte non rendu : polices/`sysfont`), pas d'entrées (souris/clavier -> événements SIL -> `OnTouch*`), pas de son, pas de sauvegardes.
- **Nettoyage conseillé (à faire par toi)** : les dossiers `work
ecomp\gen`, `genb`, `genc` sont des générations périodes (seul `gend` est utilisé) ; `work\proj` est un projet Ghidra abîmé par un script (remplacé par `work\proj2`).

### Entrées et nouvelle partie (2026-11-22, suite)
Capture : `hangar_pc.png` (écran de sélection du vaisseau après « New Game »).
- **Entrées** : la souris est traduite en événements tactiles SIL (`INPUT_TOUCH_DOWN/MOVE/UP`, coordonnées normalisées 0..1, `InputEvent` : type@0 detail@4 timestamp(double)@8 touch@16 = id,x,y), file dans `gl_host.c` (WM_LBUTTON*), livrée au rappel invité dans `sys_input_update`. Échap quitte. Test sans humain : `FTL_CLICK="image,x,y;image,x,y"` injecte des clics (appui à l'image n, relâché à n+1).
- **Corrections** : piles hôtes de 256 Mo (code généré en `/Od`, grosses trames) ; sémantique libstdc++ 4.2 des `stringbuf` : zones get/put créées **paresseusement à la première écriture** (sans cela, tout `stringstream` écrit puis relu semblait vide, ce qui faisait boucler `SystemNetwork::OnInit` à l'infini : ShipGraph sans pièces).
- **Outils de diagnostic ajoutés** : `FTL_CALLTRACE=<sous-chaîne|all>` (trace d'entrée de fonction, `RT_ENTER` + `gen_names.c`), `FTL_STREAMTRACE`, sauvegarde de pile invitée au plantage (`rt_backtrace`), tests unitaires isolés du code recompilé (`test_mat.c`, `test_stream.c`).
- **Reste** : texte TTF absent (étiquettes, noms d'équipage, texte des boîtes de dialogue : polices `freetype`/`sysfont`), son, sauvegardes, boîte de dialogue vide en superposition, clavier, performances (`/Od`).

### Texte rendu (polices FreeType) (2026-11-22, suite)
`hangar_pc.png` montre maintenant tout le texte : message d'accueil du hangar, nom « The Kestrel », équipage (Kusy, Caris, Maxwell), armes (Artemis, Burst Laser II). Les glyphes TrueType sortaient vides parce que Ghidra décrit **mal deux familles d'instructions NEON**, ce qui faussait le calcul du rectangle englobant dans le rasteriseur de FreeType (`gray_raster_render`) :
1. **`vmov.32 Dn[i],Rt`** : le p-code ne masque que 16 bits (`0xffff`) de la voie visée ; si ses 16 bits hauts n'étaient pas nuls, la voie était corrompue -> écriture directe de 4 octets (`emitVmovLane32`).
2. **Thumb NEON `vshr.s32/u32 Qd,Qm,#n`** (imm6 >= 32) affiché `vcvt.f16.s16/u16` par Ghidra -> exécuté comme un décalage à droite 32 bits (`neon.c`, l'opérande « fbits » est un petit temporaire, pas une constante).
Méthode de diagnostic qui a payé : sonde d'instruction (`Lift.java ... PROBE:0xADRESSE` + `FTL_PROBE=1` : registres avant chaque instruction de la fonction) ; `FTL_CALLTRACE` ; capture `FTL_SHOT` ; comptage d'octets non nuls des textures alpha (`FTL_GLTRACE`). **Conseil** : toute valeur incohérente dans du code vectoriel doit d'abord faire douter du décodage de Ghidra (consulter les octets de l'instruction).
Dossier de code généré courant : `workecomp\geng` (les `gen`, `genb`..`genf` sont périmés).

### Pièges déjà rencontrés (à ne pas refaire)
- Ghidra headless refuse de démarrer avec le profil Windows : toujours passer `-Dapplication.settingsdir=<dossier simple>`.
- Les noms externes démangés de Ghidra entrent en collision (`append`, `compare`…) : utiliser le nom d'origine (`getOriginalImportedName`), déjà fait dans `Lift.java`.
- Les pointeurs de fonction (callbacks zlib `zalloc/zfree`) ne sont pas des appels directs : le mode `ALL` les couvre, un sous-ensemble doit les lister en racines.
- La mémoire non initialisée du binaire (`__common`) doit être mappée.

## 8. Compilation fiable (PC) : contrôles
- Build normal : `cc_gen.bat` (/O2) + `cc_names.bat` + `build_game.bat`. Plus aucun contournement /Od.
- Contrôle 1 (tas) : `FTL_POISON=1 ftl_pc.exe` remplit tas et piles invitées avec 0xCD ; un bug de lecture non initialisée plante alors à coup sûr. `FTL_HEAPCHECK=1` détecte les écritures après free.
- Contrôle 2 (compilateur) : `cc_rtc.bat` + `rtc2.bat` produisent `ftl_rtc.exe` (/Od /RTCu, rapports dédupliqués avec fichier:ligne). Type #3 = variable locale non initialisée ; les #1 de hle_libc.c (RETD/RET64) sont des troncatures voulues.
- Pour Android (clang/arm64) : compiler avec -fsanitize=undefined,address et MSan si possible sur la version PC clang avant le portage.
- Bug de lifter corrigé : LOAD/STORE sur l'espace *registre* (vdup/vmov à indice dynamique) allait dans MEM au lieu de REGS (cassait libvorbis).
- Un plantage /O2 dans f_00147748 (rehash boost) observé une fois n'a pas pu être reproduit ; les contrôles ci-dessus n'ont rien relevé.

## 9. Partie lancee (PC) et bugs de lifter corriges
- Une vraie partie demarre (vaisseau, equipage, carte, dialogue d'intro) : `partie_pc.png`.
- Bugs de lifter trouves en chemin (tous generaux, a garder pour Android) :
  1. LOAD/STORE sur l'espace registre allaient dans MEM (vdup/vmov a indice dynamique).
  2. DCE des drapeaux : une instruction conditionnelle (cmpeq...) etait vue comme un kill ; le cmp precedent perdait NG/OV -> std::map<pair> casse -> generation de carte en boucle infinie.
  3. ldrex/strex "toujours reussi" -> remplaces par un vrai compare-and-swap (rt_ldrex/rt_strex).
  4. rand() MSVC = 15 bits, iOS = 31 bits : rand() recode (LCG 64 bits, 31 bits).
- Miscompilation MSVC /O2 observee (rehash boost, f_00147748) : disparait avec /Ob0, /Oi-, ou si les helpers de drapeaux (CARRY/SCARRY/SBORROW) ou flottants (F32/F64...) ne sont pas inlines. Cause exacte non prouvee (pas de clang ici). Build retenu : /O2 /DNI_FLAGS /DNI_FP + comparaison avec la reference /Od (cc_ref.bat -> ftl_ref.exe). Pour Android (clang) refaire la verification avec UBSan.
- Variable d'env : ne pas laisser de fichiers gen_funcs_*.c temporaires dans genj (le glob les compile).

## 10. Decision : build PC sans optimisation (/Od)
- Mesure en jeu (vaisseau au repos, 2000 images) : /O2 = 16,7 ms/image, /Od = 16,8 ms/image -> les deux tiennent 60 i/s (vsync). Seul le chargement initial change (~4 s).
- Comme /O2 a montre une miscompilation MSVC non expliquee, le build par defaut est /Od (cc_gen.bat, build_game.bat). `ftl_pc.exe` = ancienne `ftl_ref.exe`. L'ancien binaire /O2 est conserve sous `ftl_O2_instable.exe` pour reference.
- A re-mesurer en combat / saut ; pour Android (arm64) l'optimisation sera necessaire : compiler avec clang -O2 + UBSan.

## 11. Cause reelle du "plantage /O2" et du jeu fige (corrige)
- Macro `RETD` (rt.h) : `RET64(_v)` redeclarait `_v` initialise avec lui-meme -> variable non initialisee : tous les double renvoyes par les HLE (ceil, floor, pow, ___floatundidf...) etaient du bruit. Consequences : horloge `time_now()` = 0 -> `CFPS::GetSpeedFactor` = 0 -> monde fige (aucune animation, equipage immobile), et bucket count boost aberrant -> plantage /O2 (en /Od le bruit tombait "par chance"). Corrige (noms de variables uniques dans RETD/RETF).
- Le controle /RTCu avait pourtant signale hle_libc.c:118/119/128 : ne JAMAIS ecarter un rapport RTC. Apres correction, plus aucun rapport sur une partie de 1300 images.
- Les rustines /Ob0, NI_FLAGS, NI_FP etaient inutiles ; /O2 pur passe.
- Outils : FTL_FPS=1 (fps + image la plus lente), FTL_NOVSYNC=1, FTL_SEED=<n> (graine fixe), FTL_SHOT=a,b,c (plusieurs captures).
- Mesures (partie active : equipage deplace, 1300 images, vsync) : /O2 = 11,3 ms CPU/image (62 % d'un coeur), /Od = 18,8 ms (92 %). Les deux affichent ~59 fps (le jeu se limite a 60). /Od est donc juste ; /O2 donne de la marge (combats).
- Binaires : ftl_pc.exe = /Od (defaut), ftl_od.exe = copie, ftl_o2.exe = /O2 pur (cc_o2.bat).
- Le jeu demarre en pause ("AUTO-PAUSED") tant qu'un dialogue est ouvert ; le bouton PAUSE (928,722) bascule la pause manuelle (libelle PAUSED). Scenario de test : clics 300,285,420 ; 400,830,357 ; 520,285,400 ; 620,935,480 ; 760,285,400 ; puis equipage 820,413,341 et salle 850,560,390.

## 12. Validation du build PC (/O2 par defaut)
- Defaut : /O2 (cc_gen.bat -> genj\obj_o2 ; build_game.bat -> obj_o2 ; ftl_pc.exe). Reference /Od : rt_od.bat -> ftl_od.exe.
- Execution deterministe : FTL_FIXEDTIME=1 (horloge virtuelle 16,667 ms/image) + FTL_SEED=n. Scenario de reference (/tmp/scen.py, 14 captures) : menu, nouvelle partie, hangar, equipage, carte, saut vers un magasin, evenement.
- Verifications faites : (1) /Od et /O2 : 14/14 captures identiques pixel pour pixel ; (2) memoire invitee empoisonnee 0xCD + FTL_HEAPCHECK : 14/14 identiques ; (3) AddressSanitizer sur le code hote (ftl_asan.exe, asan.bat) : 0 erreur sur 1100 images ; (4) /analyze MSVC : aucun defaut memoire ; a corrige time() non declare et un chemin mal echappe (.\savedata\) ; (5) /RTCu : aucun rapport ; (6) 8 parties a graines differentes (1000 images) sans plantage ; (7) 3900 images en temps reel avec son : stable, 59 fps moyen.
- Limites : seuls menu/hangar/vaisseau/carte/saut/evenement sont couverts ; pas de combat, boutique, sauvegarde ni saisie de texte. La memoire invitee n'est pas surveillee par ASan (a faire avec un mode "garde de page" ou en recompilant avec clang+ASan pour Android).

## 13. Tests boutique et combat (PC)
- Boutique OK : ouverture auto apres l'evenement du magasin, onglets BUY/SELL, selection d'un article (panneau de detail), achat de carburant (fuel 15->16, scrap 10->7, stock x7->x6), SELL liste les armes, CLOSE.
- Combat : on arrive bien sur une balise hostile (Rebel Rigger : coque, boucliers, vaisseau ennemi, "IN DANGER", AUTO-PAUSED). Premier dialogue ("You stumble across...") se ferme ; le second ("They are powering up their FTL!", evenement <escape> de SQUAT_WARNING, sans <choice>) NE SE FERME PAS : le clic est bien enregistre (ChoiceBox::OnTouch -> GetChoice=0 -> WorldManager::CheckLoadEvent/CommandGui::LoadEvent -> SetEventReward) mais la boite reste affichee. Reproduit a l'identique en /Od, /O2, temps reel et temps virtuel -> pas un artefact de compilation ni de timing. CAUSE NON TROUVEE (probablement dans la logique d'evenements : fermeture de la boite sans SetOpen dans le code in-game).
- Rappel : les vrais combats (armes, degats, equipage ennemi) ne sont donc PAS encore testes.
- Aides de debug ajoutees dans rt.c (FTL_CALLTRACE=<nom>) : CommandGui::OnTouch, ChoiceBox::OnTouch/GetChoice, TutorialManager::OnTouch, CommandGui::LoadEvent, CFPS::OnLoop.

## 14. CORRECTION de la section 13 : le "dialogue bloque" etait mon outil de test
- FTL_CLICK ne gardait que 16 clics (tableau cl[16] dans gl_host.c) : les suivants etaient ignores en silence -> faux "bug". Limite portee a 128. Les dialogues de combat se ferment normalement.
- Combat valide (temps virtuel, graine 7, avec FTL_POISON + FTL_HEAPCHECK, 6400 images ~ 107 s de jeu, aucun plantage) : vaisseau ennemi, projectiles, boucliers qui encaissent, coque endommagee, "SHIELDS CRITICAL", panneau d'armes (Burst Laser II, Artemis, AUTO FIRE), puissance des systemes modifiable, l'ennemi s'enfuit (evenement "The scout jumps away... Fleet pursuit doubled"). Capture : combat_pc.png.
- Non teste a la main : tir effectif de mes armes sur l'ennemi (le ciblage a ete tente mais pas verifie visuellement), destruction du vaisseau ennemi, abordage/reparations.
- Le "StarMap::Close sur un clic" observe plus haut reste a verifier (carte potentiellement encore marquee ouverte apres un saut) mais n'a pas gene la suite.

## 15. Tir des armes et destruction d'un ennemi (PC)
- Scenario (temps virtuel, graine 7, FTL_POISON+FTL_HEAPCHECK) : combat contre un Rebel Rigger ; alimenter l'arme (tap colonne d'armes (127,643) puis l'arme (100,415)), tap de l'arme chargee puis d'une salle ennemie (855,388) ; AUTO FIRE (105,573).
- Verifie : mode ciblage ("TARGETING"), projectiles lances, missile Artemis consomme (8->7), bouclier ennemi mis a zero par le laser (barre de recharge), faisceau ennemi et degats sur ma coque/systemes (75 % de coque, systemes en rouge), l'ennemi s'enfuit apres ~40 s si on ne le detruit pas.
- NON observe : degats de mes armes sur la COQUE ennemie (tirs esquives ou absorbes par le bouclier avec 2 armes de depart avant sa fuite).
- Destruction : testee par raccourci -- FTL_KILLENEMY=<image> appelle ShipManager::DamageHull(ennemi, 200, 1) du jeu : explosions, coque a 0, evenement "Their ship breaks apart...", recompense (1 carburant, 1 missile, 17 scrap) creditee apres Continue (scrap 10->27, fuel 14->15, missiles 8->9). Capture : destruction_pc.png. Ce raccourci ne valide pas le calcul des degats d'armes.

## 16. Sauvegardes et clavier (PC)
- sys_userdata_perform (hle_sys.c) : vraies E/S dans ./savedata/ (continue.sav, prof.sav, settings.ini, save/save-NNNN.bin) ; chargements allouent via le mem_alloc invite (f_001e1190 avec align=0 : align=1 renvoyait NULL). Statistiques : acceptees sans stockage, RETRIEVE echoue (valeurs par defaut).
- BUG TROUVE : sys_thread_create n'a jamais libere ses emplacements (64 max, jamais rendus) -> apres 64 operations (le menu interroge continue.sav ~200 fois) plus aucun thread n'etait cree : les sauvegardes ne partaient plus. Corrige (1024 emplacements, liberes par thread_wait, pile invitee reutilisee).
- Le jeu sauvegarde sur demande de mise en veille (iOS) ou a la sortie : FTL_SUSPEND=<image> simule la veille ; test : continue.sav ecrit (4241 o), puis CONTINUE au menu restaure la partie (equipage, fuel 15, missiles 8, scrap 10, balise du magasin, en pause).
- Clavier/texte : emulation de l'OSK iOS (SysInputInfo : has_text, text_uses_custom_interface, text_has_prompt = 1). input_text_set_state(on) envoie TEXT_CLEAR + texte par defaut puis edition en direct (WM_CHAR -> TEXT_INPUT, Retour -> DONE, Echap -> CANCELLED, Suppr/Retour arriere/fleches/Debut/Fin -> evenements TEXT_*). Touches hors saisie -> INPUT_EVENT_KEYBOARD (codes SIL). Alt+F4 / fermeture de fenetre quittent (Echap ne quitte plus). Test scripte : FTL_TYPE="image:texte", FTL_SPECIAL="image:done|cancel|clear|back".
- Test : renommer un membre d'equipage au hangar (Notch -> NotchAurore), ACCEPT, START : le nom apparait en jeu ("NotchAu…").
- Non teste : renommage du vaisseau (bouton RENAME en haut a gauche, sans effet dans mon scenario), annulation (Echap), caracteres non ASCII, sauvegarde en combat, plusieurs emplacements.

## 17. Rename et accents (PC) -- version PC finie : dossier jeu_pc/
- Lancer : jeu_pc\lancer_ftl.bat (ftl_pc.exe + ftl.dat + genj\ (image memoire) + override\ ; sauvegardes dans jeu_pc\savedata\).
- Rename vaisseau/equipage : fonctionne (le premier "echec" etait un scenario mal cadence). Plafond du jeu : 16 octets par nom (un accent = 2 octets).
- Accents : l'hote envoie chaque caractere en UTF-8, octet par octet (le jeu stocke les noms dans un std::string d'octets), avec un miroir du texte pour que Retour arriere / Suppr / fleches traitent un caractere entier. Entree clavier reelle : WM_CHAR (paires de substitution UTF-16 recombinees) ; tests : FTL_TYPE="image:texte;..." (UTF-8) et FTL_SPECIAL="image:clear|done|cancel|back|del|left|right|home|end;...".
- Affichage : noms d'equipage OK partout (polices JustinFont8 / c&cnew ont les glyphes). Nom du vaisseau (hangar) : police bitmap fonts/24.font (ASCII seul) -> make_font24.py genere override/24.font avec 17 minuscules accentuees composees (a-grave, a-circ, a-trema, c-cedille, e-grave/aigu/circ/trema, i-circ/trema, o-circ/trema, u-grave/circ/trema, y-trema, n-tilde) et 17 majuscules accentuees affichees SANS accent (E pour E-aigu...). oe/OE non fournis. Le jeu charge ce fichier via le mecanisme "override" de hle_sys.c (extension virtuelle de ftl.dat + correction de l'entree d'index 1313, offset 26276).
- Correctif du code genere : patches_apres_lift.py (filtre de TextInput::OnLoop 0x11c0f0 : laisse passer les octets >= 0x80) ; A REJOUER apres tout relift de genj, puis recompiler gen_funcs_023.c.
- Piege evite : remplacer fonts/c&c.ttf par c&cnew.ttf changeait l'aspect des chiffres du HUD (EVADE/OXYGEN) -> abandonne.
- Verifie : scenario de reference 14/14 captures identiques (aucune regression visuelle) ; accents tapes et affiches dans le nom du vaisseau et des equipiers.
- Limites : majuscules accentuees sans accent dans le nom du vaisseau ; texte du jeu toujours en anglais (aucune traduction francaise dans l'IPA) ; nom du vaisseau non retrouve dans l'interface en jeu (non verifie).

## 18. Traduction francaise (PC) -- voir work/fr/LISEZMOI.md
- Source officielle : ftl.dat.vanilla du PC (lecture seule) ; correspondance par texte anglais -> id -> francais. 94,0 % des 5203 chaines de data/*.xml (exact 4770, quasi 72, fragments 49).
- Patch = fichiers XML recrits ajoutes en fin de ftl.dat + index corrige (flag compression 0). Le mecanisme override de 24.font reste valable (il suit la taille reelle du fichier).
- Polices : JustinFont7/10/11/12/12Bold n'avaient pas d'accents -> glyphes ajoutes (make_fonts_fr.py, accents en pixels ; c&c.ttf et num_font non touchees).
- Reste : images avec texte (menus, avertissements) ; chaines codees dans le binaire ("Continue..."). Pour Android : meme ftl.dat, rien a refaire.

## 19. Enregistrement / relecture d'une partie (PC)
- `FTL_RECORD=auto|<dossier>` : ecrit `replays\partie_<epoch>\actions.txt` (epoch de depart, graine FTL_SEED si fixee, chaque srand, toutes les entrees toucher/clavier/texte avec l'image ou le jeu les a recues, repere de fin) + copie des sauvegardes de depart (`savedata_start`). `FTL_REPLAY=<dossier>` rejoue : horloge virtuelle 16,667 ms/image, time() = epoch + image/60, memes entrees aux memes images, sauvegardes de depart restaurees dans `savedata_run` (les vraies ne sont pas touchees). `FTL_REPLAY_LIVE=1` rend la main a la fin ; `FTL_FAST=1` supprime la limite 60 images/s. Lanceurs : enregistrer_ftl.bat, rejouer_ftl.bat (glisser le dossier).
- Verifie : scenario de 1650 images enregistre puis rejoue sans aucun clic scripte -> captures identiques a l'octet (chemin relatif et absolu).
- Correctif : sans FTL_FRAMES le jeu s'arretait apres 300 images (g_max_frames=300 par defaut) ; 0 = illimite maintenant.
- Limites : le son n'est pas dans la boucle de determinisme ; une partie enregistree avec une autre version de l'exe ou de ftl.dat peut diverger.

## 20. Version Linux / Steam Deck
- Meme code genere (aucune relift) ; runtime porte : `work/recomp/plat.h` (equivalents POSIX des appels Win32 : threads pthread, semaphores, sections critiques, horloge, interlocked ; Windows inchange), `rt.c` (mmap 4 Go PROT_NONE + mprotect), `hle_sound.c` (SDL2 audio en file d'attente), `gl_host.c` + `gl_sdl.inc` (SDL2 : fenetre, contexte GL compat, souris/tactile, clavier+texte UTF-8, manette), `main_game.c` (thread 256 Mo, handler SIGSEGV). Build : `Makefile.linux` (gcc -O2 -std=gnu17 -fno-strict-aliasing -fwrapv -ffp-contract=off -fpermissive), `sync_linux.sh` envoie le runtime a la VM (tar -m : l'horloge de la VM differe).
- VM de build : <utilisateur>@<hote-vm> (cle SSH dediee, hors depot), dossiers ~/ftl/src (sources+obj), ~/ftl/game (donnees pour tests xvfb). Compilation complete ~4 min (4 coeurs).
- Test : xvfb-run + SDL_AUDIODRIVER=dummy (Mesa llvmpipe). Scenario 14 captures Linux vs Windows : ecarts <0,01 % des pixels (rasterisation GPU) ; enregistrement/relecture Linux : captures identiques ; Windows non regresse (14/14 identiques).
- Plein ecran Steam Deck : variable SteamDeck=1 (mode jeu) -> SDL_WINDOW_FULLSCREEN_DESKTOP ; le jeu dessine dans un FBO 1024x768 recopie avec bandes noires (glBlitFramebuffer), souris/tactile remappes. Teste en fenetre 1280x800 (FTL_WINSIZE) + capture FTL_WINSHOT.
- Manette : sticks = curseur, A/RT = appui, B/Start = Echap, X = Espace, Y = Entree.
- Exigence : glibc >= 2.38 (SteamOS >= 3.5). Livrable : dist/ftl_steamdeck.tar.gz (LISEZMOI.md dedans).
- Non verifie : performances sur un vrai Steam Deck (lancer avec FTL_FPS=1) ; manette et tactile reels ; son reel (teste seulement avec le pilote dummy).

## 21. Boutons en francais
- Boutons = images .tex dans ftl.dat (img/main_menus/*, img/pause_*.tex, autopause, advanced_warning), pas du code. En-tete .tex : 'TEX\n', version, format 8 (BGRA8), w,h (u16 BE @8), 00010000, 00000020, taille pixels (u32 @20), puis pixels BGRA. work/fr/make_buttons_fr.py convertit les img/loc-fr/*.png du PC (29 images). Menu : le jeu place chaque bouton a gauche fixe -> taille iOS conservee, texte aligne a droite et reduit si trop large.
- Restent en anglais : textes dessines dans les atlas (hangar, HUD, panneaux), chargement, MISS/RESIST, carte ; a traiter en redessinant avec une police pixel (a decider).

## 22. Reconstruction depuis l'IPA (build_from_ipa.py)
- `python build_from_ipa.py --ipa <IPA> [--pc-ftl-dat <ftl.dat vanilla PC>]` : extract -> ghidra (import + analyse auto + FixSwitch) -> lift -> patch (patches_apres_lift + binary_config.h) -> compile (VS ou gcc) -> game -> fr. Adresses propres au binaire dans work/recomp/binary.toml.
- Valide dans _test_build/ (copie jetable du depot, IPA/Ghidra/JDK en lecture seule) : les 86 fichiers generes (gen_funcs, tables, mem_*.bin) sont identiques a l'octet a work/recomp/genj ; l'executable reconstruit rejoue les 14 captures du scenario de reference de facon identique. ftl.dat francais et exe different en octets (horodatage fontTools / PDB) mais pas en comportement ; override/24.font identique.
- Duree mesuree : import+analyse+FixSwitch+Lift de l'ordre de quelques minutes ; compilation ~4 min ; total ~15 min.
- Chemin Linux de build_from_ipa.py valide sur la VM (dossier neuf ~/ftl/pipetest, gen + ftl.dat fournis, etapes compile,game : 101 s) : l'executable reproduit le scenario de 14 captures avec des ecarts <0,01 % des pixels par rapport a Windows (rasterisation GPU).
