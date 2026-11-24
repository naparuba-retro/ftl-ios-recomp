#!/bin/sh
# Lance FTL (port iPad -> Linux). A utiliser comme "jeu hors Steam" : cible = ce fichier, ou ftl_linux avec "Demarrer dans" = ce dossier.
cd "$(dirname "$(readlink -f "$0")")" || exit 1
chmod +x ./ftl_linux 2>/dev/null
# Sur Steam Deck (mode jeu) la variable SteamDeck=1 active le plein ecran avec mise a l'echelle ; FTL_WINDOWED=1 pour une fenetre.
exec ./ftl_linux . "$@"
