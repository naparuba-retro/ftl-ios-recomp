#!/bin/sh
# Change la langue du jeu : ./langue.sh fr   ou   ./langue.sh en   (echange ftl.dat et ftl_<autre>.dat)
cd "$(dirname "$(readlink -f "$0")")" || exit 1
case "$1" in
  fr) if [ -f ftl_fr.dat ]; then mv ftl.dat ftl_en.dat && mv ftl_fr.dat ftl.dat; echo "Langue : francais"; else echo "Deja en francais."; fi ;;
  en) if [ -f ftl_en.dat ]; then mv ftl.dat ftl_fr.dat && mv ftl_en.dat ftl.dat; echo "Langue : anglais"; else echo "Deja en anglais."; fi ;;
  *) echo "usage : $0 fr|en"; exit 1 ;;
esac
