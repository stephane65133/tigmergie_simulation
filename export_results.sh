#!/bin/bash
# =============================================================================
# export_results.sh — Export CSV + génération figures IEEE
# Usage : bash export_results.sh
# =============================================================================

OMNET_ROOT="${OMNET_ROOT:-$HOME/Desktop/omnetpp-6.4.0}"
INET_ROOT="${INET_ROOT:-$HOME/Desktop/inet}"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
PYTHON="${PYTHON:-python3}"

echo "╔══════════════════════════════════════════════════════╗"
echo "║   Export CSV + Figures IEEE — Stigmergie            ║"
echo "╚══════════════════════════════════════════════════════╝"

# ── Source setenv ────────────────────────────────────────────────────────────
source "$OMNET_ROOT/setenv" 2>/dev/null || true

if ! command -v opp_scavetool &>/dev/null; then
    echo "ERREUR : opp_scavetool non trouvé."
    exit 1
fi
echo "[✓] opp_scavetool : $(which opp_scavetool)"

# ── Répertoires de résultats ──────────────────────────────────────────────────
# OMNeT++ met les résultats dans le répertoire courant au moment du lancement.
# On cherche dans tous les endroits possibles.
mkdir -p "$PROJECT_DIR/results/raw"
mkdir -p "$PROJECT_DIR/results/processed"
mkdir -p "$PROJECT_DIR/results/figures"

# Chercher les .vec et .sca dans toute l'arborescence du projet
VEC_FILES=$(find "$PROJECT_DIR" -name "*.vec" -not -path "*/out/*" 2>/dev/null)
SCA_FILES=$(find "$PROJECT_DIR" -name "*.sca" -not -path "*/out/*" 2>/dev/null)

VEC_COUNT=$(echo "$VEC_FILES" | grep -c ".vec" 2>/dev/null || echo 0)
SCA_COUNT=$(echo "$SCA_FILES" | grep -c ".sca" 2>/dev/null || echo 0)

echo ""
echo "Fichiers trouvés : $VEC_COUNT .vec  |  $SCA_COUNT .sca"

if [ "$VEC_COUNT" -eq 0 ] && [ "$SCA_COUNT" -eq 0 ]; then
    echo "AVERTISSEMENT : Aucun fichier de résultats trouvé."
    echo "  → Lancer d'abord les simulations : bash run.sh E1_Proposed"
    exit 1
fi

# Afficher où ils sont
echo "Emplacements :"
echo "$VEC_FILES" | head -3 | sed 's|'"$PROJECT_DIR"'/||'
[ "$VEC_COUNT" -gt 3 ] && echo "  ... et $((VEC_COUNT-3)) autres"

PROCESSED_DIR="$PROJECT_DIR/results/processed"
FIGURES_DIR="$PROJECT_DIR/results/figures"

# ── Export des vecteurs temporels ─────────────────────────────────────────────
echo ""
echo "[1/3] Export des vecteurs (.vec → CSV)..."

SIGNALS="beliefEntropy deceptionEffort pheromoneLevel \
         consistencyViolation syncDelay commOverhead \
         falseGoalInduction performanceDrop infoLeak \
         fieldEntropy globalConsistency narrativeDivergence"

CONFIGS="E1_Proposed E1_CentralizedPPO E1_StaticAllocation \
         E1_Independent E1_GNNCoordinated \
         E2_Proposed E3_Proposed E4_Proposed E5_Proposed \
         Baseline_Proposed"

for cfg in $CONFIGS; do
    # Chercher les .vec de cette config dans toute l'arborescence
    CFG_VECS=$(find "$PROJECT_DIR" -name "vectors-${cfg}-*.vec" \
               -not -path "*/out/*" 2>/dev/null)
    [ -z "$CFG_VECS" ] && continue

    echo "  Config : $cfg"
    for sig in $SIGNALS; do
        OUT="$PROCESSED_DIR/${cfg}_${sig}.csv"
        opp_scavetool export \
            -f "name =~ $sig" \
            -F CSV-R \
            $CFG_VECS \
            -o "$OUT" 2>/dev/null
        [ -f "$OUT" ] && [ -s "$OUT" ] && echo "    ✓ ${cfg}_${sig}.csv"
    done
done

# ── Export toutes baselines E1 ensemble (pour Fig.4) ─────────────────────────
ALL_E1=$(find "$PROJECT_DIR" -name "vectors-E1_*.vec" -not -path "*/out/*" 2>/dev/null)
if [ -n "$ALL_E1" ]; then
    opp_scavetool export \
        -f "name =~ beliefEntropy" \
        -F CSV-R \
        $ALL_E1 \
        -o "$PROCESSED_DIR/E1_all_methods.csv" 2>/dev/null && \
    echo "  ✓ E1_all_methods.csv (toutes baselines — Fig.4)"
fi

# ── Export scalaires ──────────────────────────────────────────────────────────
echo ""
echo "[2/3] Export scalaires (.sca → CSV)..."

ALL_SCA=$(find "$PROJECT_DIR" -name "scalars-*.sca" -not -path "*/out/*" 2>/dev/null)
if [ -n "$ALL_SCA" ]; then
    opp_scavetool export \
        -F CSV-S \
        $ALL_SCA \
        -o "$PROCESSED_DIR/all_scalars.csv" 2>/dev/null && \
    echo "  ✓ all_scalars.csv (Table 3)"
fi

CSV_COUNT=$(ls "$PROCESSED_DIR/"*.csv 2>/dev/null | wc -l)
echo "  Total : $CSV_COUNT fichiers CSV dans results/processed/"

# ── Vérifier si les CSV déjà présents sont utilisables ───────────────────────
if [ "$CSV_COUNT" -eq 0 ]; then
    # Chercher dans results/processed/ directement (CSV déjà existants)
    EXISTING=$(find "$PROJECT_DIR/results/processed" -name "*.csv" 2>/dev/null | wc -l)
    if [ "$EXISTING" -gt 0 ]; then
        echo "  [INFO] $EXISTING CSV déjà présents dans results/processed/"
    fi
fi

# ── Génération des figures ────────────────────────────────────────────────────
echo ""
echo "[3/3] Génération des figures IEEE..."

PLOT_SCRIPT="$PROJECT_DIR/scripts/plot_results.py"
if [ ! -f "$PLOT_SCRIPT" ]; then
    PLOT_SCRIPT="$PROJECT_DIR/plot_results.py"
fi

if [ ! -f "$PLOT_SCRIPT" ]; then
    echo "  ERREUR : plot_results.py non trouvé"
    echo "  Chemins testés :"
    echo "    $PROJECT_DIR/scripts/plot_results.py"
    echo "    $PROJECT_DIR/plot_results.py"
    exit 1
fi

# Installer les dépendances Python si nécessaire
$PYTHON -c "import pandas, matplotlib, seaborn, numpy" 2>/dev/null || {
    echo "  Installation des dépendances Python..."
    $PYTHON -m pip install pandas matplotlib seaborn numpy scipy -q
}

$PYTHON "$PLOT_SCRIPT" --all \
    --input  "$PROCESSED_DIR" \
    --output "$FIGURES_DIR" && \
echo "  ✓ Figures générées" || \
echo "  AVERTISSEMENT : Erreur dans plot_results.py (figures synthétiques générées quand même)"

echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║   Export terminé                                    ║"
echo "╠══════════════════════════════════════════════════════╣"
printf "║  CSV    : %-42s║\n" "results/processed/ ($CSV_COUNT fichiers)"
echo "║  Fig.   : results/figures/                          ║"
echo "╚══════════════════════════════════════════════════════╝"
echo ""
