#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// MetricsCollector.h
//
// Module OMNeT++ SIMPLE — Collecte, agrégation et export CSV des métriques.
//
// Rôle dans l'architecture :
//   StigmergyNetwork
//   ├── domain[0..D-1] : DomainNode
//   ├── pheromoneField : PheromoneField
//   ├── adversary      : AdversaryModel
//   └── metricsCollector : MetricsCollector  ← CE MODULE
//
// Fonctions :
//   (1) Interroger PheromoneField et AdversaryModel à intervalle régulier
//   (2) Collecter les signaux émis par tous les DomainNode via subscriptions
//   (3) Agréger sur la fenêtre courante (mean, std, min, max)
//   (4) Écrire les résultats dans results/processed/metrics_aggregated.csv
//   (5) Détecter les anomalies (H < θ_susp, Ω_t > ε) et logger les alertes
//
// Métriques collectées (§6 du document) :
//
//   Section A — Déception :
//     H_b_t                    Entropie b_t(g) en bits
//     false_goal_induction_rate  % épisodes adversaire trompé
//     deception_persistence    Pas où H(b_t) > H_thresh
//     time_to_suspicion        Premier t où H < θ_susp
//
//   Section B — Coordination :
//     consistency_violation_rate  % pas où Ω_t > ε
//     narrative_divergence       max ||ô_i - ô_j||
//     sync_delay_ms             Délai propagation phéromonale
//     quorum_trigger_success    % rotations sans divergence
//
//   Section C — Réseau / Sécurité :
//     comm_overhead_bytes       Total données échangées
//     L_leak_stig               Fuite informationnelle (nats)
//     R_spoof                   Détection injection adverse
//
//   Section D — Robustesse :
//     performance_drop_node_failure  ΔH(b_T) lors des pannes
//     graceful_degradation_slope    Pente perf vs pertes réseau
//
// Format CSV de sortie :
//   time,configName,run,metric,domainId,value
//   10.0,E1_Proposed,0,H_b_t,-1,1.452
//   10.0,E1_Proposed,0,consistency_violation,2,0.032
// ─────────────────────────────────────────────────────────────────────────────

#include <omnetpp.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <string>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <iomanip>

using namespace omnetpp;

// Forward declarations
class PheromoneField;
class AdversaryModel;

// ─────────────────────────────────────────────────────────────────────────────
// Entrée d'une fenêtre d'agrégation
// ─────────────────────────────────────────────────────────────────────────────
struct MetricWindow {
    std::vector<double> values;
    simtime_t           windowStart;

    void reset(simtime_t t) { values.clear(); windowStart = t; }
    void push(double v)     { values.push_back(v); }

    double mean()  const {
        if (values.empty()) return 0.0;
        return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
    }
    double stddev() const {
        if (values.size() < 2) return 0.0;
        double m = mean();
        double sq = 0.0;
        for (double v : values) sq += (v - m) * (v - m);
        return std::sqrt(sq / (values.size() - 1));
    }
    double minVal() const {
        return values.empty() ? 0.0 : *std::min_element(values.begin(), values.end());
    }
    double maxVal() const {
        return values.empty() ? 0.0 : *std::max_element(values.begin(), values.end());
    }
    int    count()  const { return (int)values.size(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// MetricsCollector : module OMNeT++ Simple
// ─────────────────────────────────────────────────────────────────────────────
class MetricsCollector : public cSimpleModule
{
  protected:
    // ── Paramètres ───────────────────────────────────────────────────────────
    int         numDomains;
    std::string outputDir;       // results/processed/
    std::string configName;      // Nom de config OMNeT++ (E1_Proposed, etc.)
    int         runIndex;        // Index du run (0..29)
    simtime_t   collectPeriod;   // Intervalle de collecte (défaut : 10s)
    double      hThresh;         // Seuil persistance H_thresh
    double      omegaEpsilon;    // Seuil violation cohérence ε
    bool        writeCSV;        // Activer l'écriture CSV

    // ── Modules frères ────────────────────────────────────────────────────────
    PheromoneField *pheromoneField = nullptr;
    AdversaryModel *adversary      = nullptr;

    // ── Fichier CSV de sortie ─────────────────────────────────────────────────
    std::ofstream csvFile;
    std::string   csvPath;

    // ── Fenêtres d'agrégation par métrique ───────────────────────────────────
    // Clé : "metricName_domainId" (domainId=-1 pour métriques globales)
    std::map<std::string, MetricWindow> windows;

    // ── Abonnements aux signaux des DomainNode ────────────────────────────────
    // Les métriques sont collectées par souscription aux signaux OMNeT++
    // via cIListener (pattern Observer)

    // ── Compteurs globaux ─────────────────────────────────────────────────────
    int    totalCollectTicks;
    int    suspicionEvents;       // Nb fois où H < θ_susp détecté
    double cumulativeOverhead;    // Bytes totaux échangés
    double cumulativeInfoLeak;    // L_leak cumulée (nats)
    double peakConsistencyViolation; // Max Ω_t observé
    bool   firstSuspicionLogged;  // time_to_suspicion déjà écrit

    // ── Timers ────────────────────────────────────────────────────────────────
    cMessage *collectTimer = nullptr;

    // ── Signaux de sortie (MetricsCollector → résultats globaux) ─────────────
    simsignal_t sig_globalH;
    simsignal_t sig_globalOmega;
    simsignal_t sig_globalOverhead;
    simsignal_t sig_alertSuspicion;

  public:
    MetricsCollector() {}
    virtual ~MetricsCollector();

  protected:
    // ── Lifecycle OMNeT++ ─────────────────────────────────────────────────────
    virtual void initialize() override;
    virtual void handleMessage(cMessage *msg) override;
    virtual void finish() override;

    // ── Collecte ──────────────────────────────────────────────────────────────
    void collectAll();
    void collectFromPheromoneField();
    void collectFromAdversary();
    void collectFromDomainNodes();
    void collectNetworkMetrics();

    // ── Agrégation et écriture ────────────────────────────────────────────────
    void flushWindowsToCSV();
    void writeCSVRow(simtime_t t, const std::string &metric,
                     int domainId, double value);
    void writeAggregatedRow(simtime_t t, const std::string &metric,
                            int domainId, const MetricWindow &w);

    // ── Détection d'anomalies ─────────────────────────────────────────────────
    void checkAnomalies(double h, double omega);

    // ── Initialisation du fichier CSV ─────────────────────────────────────────
    void openCSVFile();
    void closeCSVFile();
    void writeCSVHeader();

    // ── Utilitaires ───────────────────────────────────────────────────────────
    std::string windowKey(const std::string &metric, int domainId) const;
    void        pushToWindow(const std::string &metric, int domainId, double val);
    std::string getConfigName() const;
    double      log2safe(double x) const {
        return (x > 1e-12) ? std::log2(x) : 0.0;
    }
};
