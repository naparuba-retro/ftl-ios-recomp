#!/bin/sh
# Lanceur de FTL (premier lancement : construction guidee ; ensuite : jouer). Sur Steam Deck : ajouter ce fichier comme jeu non-Steam.
cd "$(dirname "$(readlink -f "$0")")" || exit 1
exec python3 play.py "$@"
