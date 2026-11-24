#!/bin/sh
# Rejoue une partie enregistree : ./rejouer.sh replays/partie_xxx   (FTL_REPLAY_LIVE=1 pour reprendre la main a la fin)
cd "$(dirname "$(readlink -f "$0")")" || exit 1
[ -n "$1" ] || { echo "usage : $0 replays/partie_xxx"; exit 1; }
chmod +x ./ftl_linux 2>/dev/null
D="$1"; shift
FTL_REPLAY="$D" exec ./ftl_linux . "$@"
