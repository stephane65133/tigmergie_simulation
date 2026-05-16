#!/bin/bash
# Lancer une simulation depuis le répertoire du projet
# Usage : bash run.sh [Config]
# Exemple : bash run.sh Debug_Quick

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
OMNET_ROOT="${OMNET_ROOT:-$HOME/Desktop/omnetpp-6.4.0}"
CONFIG="${1:-Debug_Quick}"

source "$OMNET_ROOT/setenv" 2>/dev/null || true

"$PROJECT_DIR/out/clang-release/stigmergie" \
    -n "$PROJECT_DIR/src:$PROJECT_DIR/simulations" \
    -c "$CONFIG" \
    "$PROJECT_DIR/simulations/omnetpp.ini"
