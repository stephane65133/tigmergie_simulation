#!/bin/bash
# =============================================================================
# build_and_run.sh — Build complet + lancement pour Stigmergie OMNeT++
# Usage : bash build_and_run.sh [Config]
# =============================================================================
set -e

OMNET_ROOT="${OMNET_ROOT:-$HOME/Desktop/omnetpp-6.4.0}"
INET_ROOT="${INET_ROOT:-$HOME/Desktop/inet}"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONFIG="${1:-Debug_Quick}"

echo "╔══════════════════════════════════════════════════════╗"
echo "║   Stigmergie — Build complet + lancement            ║"
echo "╚══════════════════════════════════════════════════════╝"
echo "  Config     : $CONFIG"
echo "  OMNET_ROOT : $OMNET_ROOT"
echo "  INET_ROOT  : $INET_ROOT"
echo ""

# ── Source setenv ────────────────────────────────────────────────────────────
source "$OMNET_ROOT/setenv" 2>/dev/null || true
echo "[✓] OMNeT++ env : $(which opp_run)"

# ── Chemins (FIXES, sans variables OMNeT++ non résolues) ────────────────────
OUT_DIR="$PROJECT_DIR/out/clang-release/src"
SO_FILE="$OUT_DIR/libstigmergie.so"
# NED path : chemins absolus séparés par :
NED_PATH="$PROJECT_DIR/src:$PROJECT_DIR/simulations:$INET_ROOT/src"

mkdir -p "$OUT_DIR"
mkdir -p "$PROJECT_DIR/results/raw"
mkdir -p "$PROJECT_DIR/results/processed"
mkdir -p "$PROJECT_DIR/results/figures"

# ── Compilation ──────────────────────────────────────────────────────────────
CXX_FLAGS="-std=c++17 -fPIC -O2 -DNDEBUG \
    -DINET_IMPORT \
    -I$INET_ROOT/src \
    -I$OMNET_ROOT/include"

echo ""
echo "[1/3] Compilation..."
cd "$PROJECT_DIR"
OBJS=""
for module in AdversaryModel DomainNode MetricsCollector PheromoneField; do
    src="src/${module}.cc"
    obj="$OUT_DIR/${module}.o"
    echo "  CC  $src"
    g++ $CXX_FLAGS -c "$src" -o "$obj"
    OBJS="$OBJS $obj"
done
echo "  [✓] Compilation OK"

# ── Link ─────────────────────────────────────────────────────────────────────
echo ""
echo "[2/3] Link → libstigmergie.so..."
g++ -shared -fPIC -o "$SO_FILE" $OBJS \
    -L"$INET_ROOT/src" -lINET \
    -Wl,-rpath,"$INET_ROOT/src"
echo "  [✓] $SO_FILE ($(du -sh $SO_FILE | cut -f1))"

# ── Lancement ────────────────────────────────────────────────────────────────
echo ""
echo "[3/3] Lancement : $CONFIG"
echo ""

# -l prend le chemin SANS le préfixe lib et SANS .so
# Donc libstigmergie.so → on passe $OUT_DIR/stigmergie
opp_run \
    -n "$NED_PATH" \
    -l "$INET_ROOT/src/INET" \
    -l "$OUT_DIR/stigmergie" \
    -c "$CONFIG" \
    "$PROJECT_DIR/simulations/omnetpp.ini"
