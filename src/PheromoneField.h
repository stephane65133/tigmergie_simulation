#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// PheromoneField.h
//
// Module OMNeT++ SIMPLE — champ phéromonal global partagé entre tous les
// domaines du réseau stigmergique.
//
// Rôle dans l'architecture :
//   StigmergyNetwork
//   ├── domain[0..D-1] : DomainNode  ← lit/écrit via getField() / deposit()
//   └── pheromoneField : PheromoneField  ← ce module
//
// Le champ est un vecteur τ[0..D-1] ∈ [0,1]^D.
// Chaque domaine d dépose sa valeur locale τ_d(t) via deposit().
// Le module applique l'évaporation globale et les perturbations réseau.
//
// Équations implémentées :
//
//   Dépôt      : τ_d(t) ← clip( τ_d(t) + Δ_d , 0, 1 )
//   Évaporation: τ_d(t+1) ← (1 - ρ_p) · τ_d(t)          [global tick]
//   Diffusion  : τ_d ← τ_d + α · (τ̄_voisins - τ_d)       [lissage spatial]
//   Injection  : τ_d ← τ_d + ε_inject  si adversaire actif [E3/perturbation]
//
// Interface publique (appelée par DomainNode) :
//   deposit(domainId, delta)            → dépose un incrément sur τ_d
//   setField(domainId, value)           → écrase τ_d (init ou reset)
//   getField(domainId)       → double   → lit τ_d
//   getSnapshot()  → vector<double>     → copie du champ complet
//   getNeighborMean(domainId, edges)    → moyenne τ des voisins
//   getGlobalEntropy()       → double   → H du champ normalisé
//   getConsistencyPenalty(edges, w)     → double   → Ω_t
//
// Statistiques émises (→ résultats Results B et C) :
//   @statistic[fieldEntropy]         H du champ τ normalisé
//   @statistic[evaporationLoss]      énergie dissipée par évaporation
//   @statistic[diffusionGain]        homogénéisation par diffusion
//   @statistic[adversarialNoise]     amplitude des injections adverses
//   @statistic[globalConsistency]    1 - Ω_t normalisé
//   @statistic[fieldNorm]            ||τ||_2 global
// ─────────────────────────────────────────────────────────────────────────────

#include <omnetpp.h>
#include <vector>
#include <map>
#include <deque>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <stdexcept>

using namespace omnetpp;

// ─────────────────────────────────────────────────────────────────────────────
// Structure décrivant un lien du graphe inter-domaines
// ─────────────────────────────────────────────────────────────────────────────
struct DomainEdge {
    int    src;      // Index domaine source
    int    dst;      // Index domaine destination
    double weight;   // Poids ω_{ij} du lien (fiabilité)
};

// ─────────────────────────────────────────────────────────────────────────────
// Entrée de l'historique pour statistiques de dérive
// ─────────────────────────────────────────────────────────────────────────────
struct FieldSnapshot {
    simtime_t time;
    std::vector<double> tau; // Copie du champ à cet instant
};

// ─────────────────────────────────────────────────────────────────────────────
// PheromoneField : module OMNeT++ Simple
// ─────────────────────────────────────────────────────────────────────────────
class PheromoneField : public cSimpleModule
{
  protected:
    // ── Dimension ────────────────────────────────────────────────────────────
    int numDomains;

    // ── Champ phéromonal τ[0..D-1] ───────────────────────────────────────────
    std::vector<double> tau;        // Valeurs courantes
    std::vector<double> tauPrev;    // Valeurs au tick précédent
    std::vector<double> tauInit;    // Valeurs initiales (pour reset)

    // ── Paramètres (§ default.yaml → omnetpp.ini) ────────────────────────────
    double rhoP;            // Taux d'évaporation ρ_p
    double diffAlpha;       // Coefficient de diffusion spatiale α
    double depositClamp;    // Amplitude max d'un dépôt Δ_d
    double noiseAmplitude;  // Amplitude bruit adversarial (E3)
    double jammingProb;     // Probabilité de jamming par tick (E3)
    bool   enableDiffusion; // Activer la diffusion spatiale

    // ── Topologie (graphe de voisinage) ──────────────────────────────────────
    std::vector<DomainEdge> edges;                     // Liens inter-domaines
    std::vector<std::vector<int>> adjacency;           // Liste d'adjacence
    std::vector<std::vector<double>> edgeWeights;      // Poids ω_{ij}

    // ── Historique pour statistiques de dérive ────────────────────────────────
    std::deque<FieldSnapshot> history;
    static const int HISTORY_LEN = 20; // Fenêtre glissante

    // ── Timer d'évaporation globale ───────────────────────────────────────────
    cMessage *evapTimer   = nullptr;
    simtime_t evapPeriod;           // Période du tick global (= syncInterval)

    // ── Signaux @statistic ────────────────────────────────────────────────────
    simsignal_t sig_fieldEntropy;
    simsignal_t sig_evaporationLoss;
    simsignal_t sig_diffusionGain;
    simsignal_t sig_adversarialNoise;
    simsignal_t sig_globalConsistency;
    simsignal_t sig_fieldNorm;

  public:
    PheromoneField() {}
    virtual ~PheromoneField();

    // ── API publique (appelée par DomainNode) ─────────────────────────────────

    // Dépose un incrément Δ sur τ_d : τ_d ← clip(τ_d + delta, 0, 1)
    // Retourne la nouvelle valeur τ_d après dépôt.
    double deposit(int domainId, double delta);

    // Écrase directement τ_d (utilisé à l'init ou après une panne)
    void setField(int domainId, double value);

    // Lit τ_d courant
    double getField(int domainId) const;

    // Retourne une copie complète du vecteur τ
    std::vector<double> getSnapshot() const;

    // Moyenne pondérée τ des voisins de domainId selon la topologie
    double getNeighborMean(int domainId) const;

    // H du champ τ normalisé comme distribution : Σ τ_d / ||τ||_1
    double getGlobalEntropy() const;

    // Ω_t = Σ_{(i,j)∈E} ω_ij · |τ_i - τ_j|
    double getConsistencyPenalty() const;

    // Norme L2 du champ
    double getFieldNorm() const;

    // Enregistre la topologie (appelé depuis StigmergyNetwork au démarrage)
    void setTopology(const std::vector<DomainEdge> &edgeList);

  protected:
    // ── Lifecycle OMNeT++ ─────────────────────────────────────────────────────
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;

    // ── Mécanique du champ ────────────────────────────────────────────────────
    void  tickEvaporation();
    void  tickDiffusion();
    void  tickAdversarialNoise();
    void  pushHistory();
    void  emitFieldStats();

    // ── Topologie par défaut (hexagone pour D=6) ──────────────────────────────
    void buildDefaultTopology();
    void buildAdjacencyFromEdges();

    // ── Utilitaires ───────────────────────────────────────────────────────────
    double clip(double v, double lo=0.0, double hi=1.0) const {
        return std::max(lo, std::min(hi, v));
    }
    double log2safe(double x) const {
        return (x > 1e-12) ? std::log2(x) : 0.0;
    }
    bool   validId(int d) const { return d >= 0 && d < numDomains; }
};
