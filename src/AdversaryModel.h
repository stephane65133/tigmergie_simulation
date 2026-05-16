#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// AdversaryModel.h
//
// Module OMNeT++ SIMPLE — Modèle adverse intelligent.
//
// Rôle dans l'architecture :
//   StigmergyNetwork
//   ├── domain[0..D-1] : DomainNode   ← envoie les observations o_d^t
//   ├── pheromoneField : PheromoneField
//   └── adversary      : AdversaryModel  ← CE MODULE
//
// Fonctions :
//   (1) Fusion bayésienne des observations multi-domaines
//         b_t(g) ∝ p(o_t | g) · b_{t-1}(g)
//
//   (2) Mécanisme d'attention GNN léger (§2 white paper)
//         α_d^t = softmax( W_a · [τ_d || H_d] )
//         Pondère les observations par domaine selon leur saillance
//
//   (3) Stratégies de sondage (probingIntensity)
//         "adaptive"      : sonde les domaines à forte variance de τ
//         "shift_at_t100" : change de stratégie à t=100s (E2)
//         "uniform"       : sonde tous les domaines uniformément
//
//   (4) Émission des métriques adverses
//         suspicionLevel  : niveau de suspicion courant θ_susp
//         beliefMax       : max_g b_t(g) (confiance dans le meilleur objectif)
//         entropyBelief   : H(b_t) vue côté adversaire
//
// Interface publique (appelée par DomainNode à chaque réception) :
//   observe(domainId, tauValue, narrative, timestamp)
//   getBelief()          → vecteur b_t(g)
//   getEntropy()         → H(b_t) en bits
//   getMostLikelyGoal()  → argmax_g b_t(g)
//   isSuspicious()       → bool : H(b_t) < suspicionThreshold
//   getAttentionWeights()→ vecteur α_d^t
//
// Correspondance default.yaml (§4 adversary.yaml) :
//   beliefInit         ← "uniform"
//   gnnHiddenDim       ← 16
//   attentionLr        ← 0.01
//   suspicionThreshold ← 0.3
//   probingIntensity   ← "adaptive"
// ─────────────────────────────────────────────────────────────────────────────

#include <omnetpp.h>
#include <vector>
#include <map>
#include <deque>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <functional>

using namespace omnetpp;

// ─────────────────────────────────────────────────────────────────────────────
// Constantes partagées (redéclarées localement pour éviter dépendance circulaire)
// ─────────────────────────────────────────────────────────────────────────────
static const int    ADV_NUM_GOALS   = 3;
static const double ADV_GOAL_SIGS[ADV_NUM_GOALS] = {0.25, 0.55, 0.85};
static const double ADV_BETA        = 8.0;   // Concentration vraisemblance
static const int    ADV_OBS_HISTORY = 20;    // Fenêtre historique observations

// ─────────────────────────────────────────────────────────────────────────────
// Observation reçue d'un domaine
// ─────────────────────────────────────────────────────────────────────────────
struct DomainObservation {
    int       domainId;
    double    tauValue;       // τ_d(t) observé
    char      narrative;      // Narrative déclarée
    double    effort;         // x_d^t déclaré (si visible)
    simtime_t timestamp;
};

// ─────────────────────────────────────────────────────────────────────────────
// AdversaryModel : module OMNeT++ Simple
// ─────────────────────────────────────────────────────────────────────────────
class AdversaryModel : public cSimpleModule
{
  protected:
    // ── Paramètres ───────────────────────────────────────────────────────────
    int         numDomains;
    int         numGoals;            // |G| = 3
    std::string beliefInit;          // "uniform" | "concentrated"
    int         gnnHiddenDim;        // Dimension cachée GNN (16)
    double      attentionLr;         // Learning rate attention (0.01)
    double      suspicionThreshold;  // θ_susp (0.3 bits)
    std::string probingIntensity;    // "adaptive" | "shift_at_t100" | "uniform"

    // ── État interne ─────────────────────────────────────────────────────────

    // Croyance adverse b_t(g) : distribution sur |G| objectifs
    std::vector<double> belief;

    // Poids d'attention α_d : saillance de chaque domaine pour l'adversaire
    std::vector<double> attentionWeights;

    // Matrice de paramètres GNN W_a (gnnHiddenDim × 2) — attention linéaire
    // Chaque ligne : [w_tau, w_entropy]
    std::vector<std::vector<double>> W_attention;

    // Historique des observations par domaine
    std::map<int, std::deque<DomainObservation>> obsHistory;

    // Dernières observations (une par domaine)
    std::map<int, DomainObservation> lastObs;

    // Variance des τ par domaine (pour sondage adaptatif)
    std::map<int, double> tauVariance;
    std::map<int, double> tauMean;
    std::map<int, int>    tauCount;

    // Shift de stratégie à t=100 (E2)
    bool strategyShifted;
    bool shiftApplied;

    // Ordre de sondage (domaines ciblés en priorité)
    std::vector<int> probingOrder;

    // ── Timers ───────────────────────────────────────────────────────────────
    cMessage *updateTimer  = nullptr;
    cMessage *probeTimer   = nullptr;
    simtime_t updatePeriod;

    // ── Signaux @statistic ────────────────────────────────────────────────────
    simsignal_t sig_suspicionLevel;
    simsignal_t sig_beliefMax;
    simsignal_t sig_entropyBelief;
    simsignal_t sig_attentionEntropy;
    simsignal_t sig_probingTarget;
    simsignal_t sig_strategyShift;

  public:
    AdversaryModel() {}
    virtual ~AdversaryModel();

    // ── API publique (appelée par DomainNode / MetricsCollector) ─────────────

    // Soumettre une nouvelle observation depuis un domaine
    void observe(int domainId, double tauValue, char narrative,
                 double effort, simtime_t ts);

    // Accesseurs état courant
    std::vector<double> getBelief()          const { return belief; }
    double              getEntropy()         const;
    int                 getMostLikelyGoal()  const;
    bool                isSuspicious()       const;
    std::vector<double> getAttentionWeights()const { return attentionWeights; }
    double              getSuspicionLevel()  const { return suspicionThreshold; }

  protected:
    // ── Lifecycle OMNeT++ ─────────────────────────────────────────────────────
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;

    // ── Fusion bayésienne ─────────────────────────────────────────────────────
    void bayesianUpdate(const DomainObservation &obs, double attWeight);
    void bayesianFuseAll();
    double likelihood(double observation, int goal) const;

    // ── Mécanisme d'attention GNN ─────────────────────────────────────────────
    void   updateAttentionWeights();
    double computeDomainSalience(int domainId) const;
    void   gradientStepAttention(int domainId, double loss);
    std::vector<double> softmax(const std::vector<double> &logits) const;

    // ── Stratégie de sondage ──────────────────────────────────────────────────
    void updateProbingOrder();
    void applyStrategyShift();

    // ── Mise à jour statistiques en ligne ─────────────────────────────────────
    void updateTauStats(int domainId, double tauValue);

    // ── Émission métriques ────────────────────────────────────────────────────
    void emitAdversaryMetrics();

    // ── Utilitaires ───────────────────────────────────────────────────────────
    double log2safe(double x) const { return (x > 1e-12) ? std::log2(x) : 0.0; }
    double clamp01(double v)  const { return std::max(0.0, std::min(1.0, v)); }
    void   normalizeInPlace(std::vector<double> &v) const;
};
