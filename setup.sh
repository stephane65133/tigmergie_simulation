#!/bin/bash
# =============================================================================
#  setup.sh — Script de mise en route du projet Stigmergie
#  Lancer avec : bash setup.sh
# =============================================================================

set -e

OMNET_ROOT="${OMNET_ROOT:-$HOME/Desktop/omnetpp-6.4.0}"
INET_ROOT="${INET_ROOT:-$HOME/Desktop/inet}"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║   Setup Stigmergie Multi-Domaine                    ║"
echo "╚══════════════════════════════════════════════════════╝"
echo ""
echo "  Projet    : $PROJECT_DIR"
echo "  OMNeT++   : $OMNET_ROOT"
echo "  INET      : $INET_ROOT"
echo ""

# ── Étape 1 : vérifier OMNeT++ ──────────────────────────────────────────────
if [ ! -f "$OMNET_ROOT/setenv" ]; then
    echo "ERREUR : OMNeT++ non trouvé dans $OMNET_ROOT"
    echo "  → Modifier OMNET_ROOT dans ce script ou :"
    echo "    export OMNET_ROOT=/chemin/vers/omnetpp-6.x.x"
    exit 1
fi
echo "[1/5] Source OMNeT++ setenv..."
source "$OMNET_ROOT/setenv"
echo "      OK — opp_run : $(which opp_run)"

# ── Étape 2 : vérifier INET ─────────────────────────────────────────────────
if [ ! -d "$INET_ROOT/src" ]; then
    echo "ERREUR : INET non trouvé dans $INET_ROOT"
    echo "  → Modifier INET_ROOT dans ce script ou :"
    echo "    export INET_ROOT=/chemin/vers/inet"
    exit 1
fi
echo "[2/5] INET trouvé : $INET_ROOT/src"

# ── Étape 3 : créer les répertoires de résultats ────────────────────────────
echo "[3/5] Création des répertoires..."
mkdir -p "$PROJECT_DIR/results/raw"
mkdir -p "$PROJECT_DIR/results/processed"
mkdir -p "$PROJECT_DIR/results/figures"
echo "      OK"

# ── Étape 4 : générer le Makefile OMNeT++ ───────────────────────────────────
echo "[4/5] Génération du Makefile OMNeT++ (opp_makemake)..."
cd "$PROJECT_DIR"
opp_makemake -f --deep \
    -o stigmergie \
    -KINET_PROJ="$INET_ROOT" \
    -DINET_IMPORT \
    -I"$INET_ROOT/src" \
    -L"$INET_ROOT/src" \
    -lINET
echo "      OK — GNUmakefile généré"

# ── Étape 5 : compiler ──────────────────────────────────────────────────────
echo "[5/5] Compilation (make -j$(nproc))..."
make -j$(nproc) MODE=release
echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║   Compilation réussie !                             ║"
echo "╠══════════════════════════════════════════════════════╣"
echo "║  Test rapide :                                      ║"
echo "║    make run-debug                                   ║"
echo "║                                                     ║"
echo "║  Expérience E1 complète :                           ║"
echo "║    make run-E1                                      ║"
echo "╚══════════════════════════════════════════════════════╝"
echo ""
