#!/bin/sh
# Envoie le runtime (pas genj) vers une machine de build Linux : $FTL_VM:~/ftl/src   (ex. FTL_VM=user@192.0.2.10)
# Variables : FTL_VM (utilisateur@hote), FTL_VM_KEY (fichier de cle SSH, facultatif)
: "${FTL_VM:?definir FTL_VM=utilisateur@hote}"
K="-o IdentitiesOnly=yes -o BatchMode=yes"
[ -n "$FTL_VM_KEY" ] && K="-i $FTL_VM_KEY $K"
cd "$(dirname "$0")"
tar -czf - rt.c rt.h plat.h hle_*.c gl_host.c gl_sdl.inc main_game.c neon.c pvrtc.c make_stubs.py Makefile.linux | ssh $K "$FTL_VM" "mkdir -p ~/ftl/src && cd ~/ftl/src && tar -xmzf -"
