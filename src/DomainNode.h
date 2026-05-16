#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// DomainNode.h
//
// Déclaration de la classe DomainNode : application stigmergique principale
// pour OMNeT++ / INET Framework.
//
// Implémente le cadre formel (white paper §2) :
//
//   J(π_D, π_A) = E[ Σ_{t=1}^T  γ^t (
//                      U_mission(s_t, a_t)
//                    + η · H(b_t)          ← entropie croyance adverse
//                    - λ · C(a_t)          ← coût des actions
//                    - μ · L_leak(o_t)     ← fuite informationnelle
//                    - ρ · Ω_t(x_t)        ← pénalité cohérence cross-domaine
//                 )]
//
// Correspondance avec les paramètres default.yaml :
//   rhoP          ← stigmergy.rho_p
//   xi            ← stigmergy.xi
//   omegaDefault  ← stigmergy.omega_default
//   thetaTau      ← stigmergy.theta_tau
//   quorumRatio   ← stigmergy.quorum_ratio
//   rotationCooldown ← deception.rotation_cooldown
// ─────────────────────────────────────────────────────────────────────────────

#include <omnetpp.h>
// INET supprimé : DomainNode communique via sendDirect() uniquement

#include <vector>
#include <map>
#include <string>
#include <deque>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>

using namespace omnetpp;

// ─────────────────────────────────────────────────────────────────────────────
// Constantes globales
// ─────────────────────────────────────────────────────────────────────────────
static const int    NUM_GOALS        = 3;      // |G| = {A, B, C}
static const int    UDP_PORT         = 4242;   // Port phéromonal
static const double TAU_MIN          = 0.0;
static const double TAU_MAX          = 1.0;
static const double H_THRESH_DEFAULT = 1.0;   // bits : seuil persistance déception
static const int    PAYLOAD_BYTES    = 48;    // Taille paquet phéromonal (bytes)
static const double GOAL_SIGS[NUM_GOALS] = {0.25, 0.55, 0.85}; // Signatures objectifs

// ─────────────────────────────────────────────────────────────────────────────
// Structure d'un paquet phéromonal (sérialisé dans le payload UDP)
// ─────────────────────────────────────────────────────────────────────────────
struct PheromonePayload {
    int    srcDomainId;        // Identifiant domaine source
    double tauValue;           // Valeur phéromonale τ_d(t)
    double deceptionEffort;    // x_d^t courant
    char   narrative;          // Narrative courante : 'A', 'B' ou 'C'
    double beliefEntropy;      // H(b_t) local (pour calcul Ω_t par voisins)
    simtime_t timestamp;       // Heure d'émission (calcul sync_delay)
};

// ─────────────────────────────────────────────────────────────────────────────
// Table des voisins
// ─────────────────────────────────────────────────────────────────────────────
struct NeighborState {
    int       domainId;
    double    tau;             // Dernier τ reçu
    double    effort;          // Dernier x_d^t reçu
    char      narrative;       // Narrative déclarée par le voisin
    double    omega;           // Fiabilité estimée du lien ω_{ij}
    double    beliefEntropy;   // H(b_t) du voisin (pour Ω_t)
    simtime_t lastSeen;        // Dernier temps de contact
    bool      alive;           // false si panne détectée (timeout)
};

// ─────────────────────────────────────────────────────────────────────────────
// Classe principale DomainNode
// ─────────────────────────────────────────────────────────────────────────────
class DomainNode : public cSimpleModule
{
  // ── Déclaration du module OMNeT++ ────────────────────────────────────────
  protected:

    // ── Identité ────────────────────────────────────────────────────────────
    int         domainId;
    int         numDomains;
    std::string domainLabel;

    // ── Paramètres stigmergiques ─────────────────────────────────────────────
    double rhoP;              // ρ_p : taux d'évaporation
    double xi;                // ξ   : poids gradient local
    double omegaDefault;      // ω   : fiabilité liens par défaut
    double thetaTau;          // θ_τ : seuil suspicion narrative
    double quorumRatio;       // Fraction quorum pour rotation narrative
    int    rotationCooldown;  // Pas min entre rotations
    double suspicionThreshold;// θ_susp : seuil suspicion adverse
    double hThresh;           // H_thresh : seuil persistance déception

    // ── Budget et déception ──────────────────────────────────────────────────
    double budgetTotal;       // B_t
    double costPerUnit;       // κ_d
    double deceptionEffort;   // x_d^t courant
    std::vector<char> narrativeOptions; // {A, B, C}
    char   currentNarrative;
    int    cooldownCounter;

    // ── État phéromonal ──────────────────────────────────────────────────────
    double tauD;              // τ_d(t) : champ phéromonal local
    double tauDPrev;          // τ_d(t-1) pour calcul gradient

    // ── Modèle de croyance adverse b_t(g) ───────────────────────────────────
    std::vector<double> beliefBt;   // Distribution sur G objectifs
    std::string probingIntensity;
    bool adversaryShifted;          // Vrai après t=100 pour "shift_at_t100"

    // ── Table des voisins ────────────────────────────────────────────────────
    std::map<int, NeighborState> neighbors;

    // ── Lifecycle / Pannes (E4) ──────────────────────────────────────────────
    double   nodeFailureRate;
    bool     nodeAlive;
    simtime_t failureTime;

    // ── Détection d'injection adverse (R_spoof) ──────────────────────────────
    std::deque<double> tauHistory;  // Historique τ pour détection anomalie
    static const int   TAU_HISTORY_LEN = 10;

    // ── Fuite informationnelle cumulée (L_leak) ──────────────────────────────
    double cumulativeInfoLeak;

    // ── Suivi métriques Results A ─────────────────────────────────────────────
    bool     suspicionRecorded;     // timeToSuspicion déjà émis ce run
    int      deceptionPersistCount; // Compteur pas où H > H_thresh
    int      falseGoalCount;        // Compteur inductions réussies
    int      totalEpisodes;         // Dénominateur falseGoalInduction

    // ── Suivi métriques Results B ─────────────────────────────────────────────
    int  quorumAttempts;
    int  quorumSuccesses;

    // ── Performance de référence pour Results D ───────────────────────────────
    double hAtLastCheckpoint;       // H(b_t) avant panne pour ΔH

    // ── Synchronisation réseau ───────────────────────────────────────────────
    simtime_t syncInterval;


    // ── Timers ───────────────────────────────────────────────────────────────
    cMessage *syncTimer       = nullptr;
    cMessage *metricsTimer    = nullptr;
    cMessage *failureTimer    = nullptr;
    cMessage *neighborTimeout = nullptr;

    // ── Signaux statistiques (noms identiques aux @statistic du NED) ────────
    // Results A
    simsignal_t sig_deceptionEffort;
    simsignal_t sig_pheromoneLevel;
    simsignal_t sig_beliefEntropy;
    simsignal_t sig_falseGoalInduction;
    simsignal_t sig_deceptionPersistence;
    simsignal_t sig_timeToSuspicion;
    // Results B
    simsignal_t sig_consistencyViolation;
    simsignal_t sig_narrativeDivergence;
    simsignal_t sig_syncDelay;
    simsignal_t sig_quorumTriggerSuccess;
    // Results C
    simsignal_t sig_commOverhead;
    simsignal_t sig_infoLeak;
    simsignal_t sig_spoofDetection;
    // Results D
    simsignal_t sig_performanceDrop;
    simsignal_t sig_gracefulDegradation;

  public:
    DomainNode() {}
    virtual ~DomainNode();

  protected:
    // ── Lifecycle OMNeT++ ────────────────────────────────────────────────────
    virtual int  numInitStages() const override;
    virtual void initialize(int stage) override;
    virtual void finish() override;

    virtual void handleMessage(cMessage *msg) override;

    // ── Timers ────────────────────────────────────────────────────────────────
    void handleSyncTimer();
    void handleMetricsTimer();
    void handleNeighborTimeout();
    void triggerNodeFailure();

    // ── Logique phéromonale ───────────────────────────────────────────────────
    double computeLocalGradient();
    void   updatePheromoneField();
    void   updateDeceptionEffort();
    void   checkNarrativeRotation();
    void   adaptToAdversaryShift();

    // ── Modèle bayésien adverse ───────────────────────────────────────────────
    void   updateBeliefBayesian(const PheromonePayload &payload);
    double computeBeliefEntropy() const;
    int    mostLikelyGoal() const;
    bool   isAdversaryMisled() const;

    // ── Métriques section Results A/B/C/D ─────────────────────────────────────
    void   emitDeceptionMetrics();
    void   emitCoordinationMetrics();
    void   emitNetworkMetrics(int bytesTransmitted);
    void   emitRobustnessMetrics();
    double computeConsistencyPenalty() const;
    double computeNarrativeDivergence() const;
    double computeInfoLeak() const;
    double estimateSpoofProbability(double incomingTau) const;

    // ── Communication UDP ─────────────────────────────────────────────────────
    void broadcastPheromoneUpdate();  // via sendDirect()
    void handlePheromoneMessage(const PheromonePayload &pl);  // traitement message entrant
    PheromonePayload serializeLocalState() const;



    // ── Utilitaires ───────────────────────────────────────────────────────────
    void   parseNarrativeOptions(const std::string &optStr);
    char   nextNarrative(char current) const;
    double clamp(double v, double lo, double hi) const { return std::max(lo, std::min(hi, v)); }
    double log2safe(double x) const { return (x > 1e-12) ? std::log2(x) : 0.0; }
};
