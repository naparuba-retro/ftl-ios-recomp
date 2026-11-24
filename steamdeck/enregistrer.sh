#!/bin/sh
# Lance FTL en enregistrant la partie (entrees + graine) dans replays/partie_<n>/ : permet de reproduire un plantage.
cd "$(dirname "$(readlink -f "$0")")" || exit 1
chmod +x ./ftl_linux 2>/dev/null
FTL_RECORD=auto exec ./ftl_linux . "$@"
