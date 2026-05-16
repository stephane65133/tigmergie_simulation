// ─────────────────────────────────────────────────────────────────────────────
// MetricsCollector.cc
//
// Collecte centralisée des métriques, agrégation par fenêtre de 10s,
// et export CSV vers results/processed/metrics_aggregated.csv.
//
// Format de sortie (compatible pandas et plot_results.py) :
//   time,config,run,metric,domain,value,mean,std,min,max,count
// ─────────────────────────────────────────────────────────────────────────────

#include "MetricsCollector.h"
#include "PheromoneField.h"
#include "AdversaryModel.h"

Define_Module(MetricsCollector);

// ═════════════════════════════════════════════════════════════════════════════
// Destructeur
// ═════════════════════════════════════════════════════════════════════════════
MetricsCollector::~MetricsCollector()
{
    cancelAndDelete(collectTimer);
    closeCSVFile();
}

// ═════════════════════════════════════════════════════════════════════════════
// initialize()
// ═════════════════════════════════════════════════════════════════════════════
void MetricsCollector::initialize()
{
    // ── Paramètres ────────────────────────────────────────────────────────────
    numDomains    = par("numDomains");
    outputDir     = par("outputDir").stringValue();
    collectPeriod = par("collectPeriod");   // défaut 10s (§4 save_every=10)
    hThresh       = par("hThresh");         // défaut 1.0 bit
    omegaEpsilon  = par("omegaEpsilon");    // défaut 0.05
    writeCSV      = par("writeCSV");        // true par défaut

    // ── Récupération nom de config et run ─────────────────────────────────────
    configName = getConfigName();
    runIndex   = (int)getEnvir()->getConfigEx()->getActiveRunNumber();

    // ── Compteurs ─────────────────────────────────────────────────────────────
    totalCollectTicks       = 0;
    suspicionEvents         = 0;
    cumulativeOverhead      = 0.0;
    cumulativeInfoLeak      = 0.0;
    peakConsistencyViolation = 0.0;
    firstSuspicionLogged    = false;

    // ── Références aux modules frères ─────────────────────────────────────────
    // Accès via getModuleByPath relatif à StigmergyNetwork
    cModule *net = getParentModule();
    if (net) {
        pheromoneField = dynamic_cast<PheromoneField*>(
            net->getSubmodule("pheromoneField"));
        adversary = dynamic_cast<AdversaryModel*>(
            net->getSubmodule("adversary"));
    }

    if (!pheromoneField)
        EV_WARN << "[MetricsCollector] PheromoneField non trouvé" << endl;
    if (!adversary)
        EV_WARN << "[MetricsCollector] AdversaryModel non trouvé" << endl;

    // ── Enregistrement signaux ────────────────────────────────────────────────
    sig_globalH        = registerSignal("globalH");
    sig_globalOmega    = registerSignal("globalOmega");
    sig_globalOverhead = registerSignal("globalOverhead");
    sig_alertSuspicion = registerSignal("alertSuspicion");

    // ── Initialisation fenêtres d'agrégation ─────────────────────────────────
    // Une fenêtre par (métrique, domaine) — initialisées à la demande dans pushToWindow()

    // ── Ouverture du fichier CSV ──────────────────────────────────────────────
    if (writeCSV) {
        openCSVFile();
    }

    // ── Timer de collecte ─────────────────────────────────────────────────────
    collectTimer = new cMessage("collectTimer");
    scheduleAt(simTime() + collectPeriod, collectTimer);

    EV_INFO << "[MetricsCollector] init : D=" << numDomains
            << " outputDir=" << outputDir
            << " collectPeriod=" << collectPeriod << "s"
            << " CSV=" << (writeCSV ? csvPath : "désactivé")
            << endl;
}

// ═════════════════════════════════════════════════════════════════════════════
// handleMessage()
// ═════════════════════════════════════════════════════════════════════════════
void MetricsCollector::handleMessage(cMessage *msg)
{
    if (msg == collectTimer) {
        // ① Collecte depuis tous les modules
        collectAll();

        // ② Flush des fenêtres → CSV
        if (writeCSV) {
            flushWindowsToCSV();
        }

        totalCollectTicks++;
        scheduleAt(simTime() + collectPeriod, collectTimer);
    }
    else {
        EV_WARN << "[MetricsCollector] message inattendu : " << msg->getName() << endl;
        delete msg;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// collectAll() — point d'entrée de la collecte à chaque tick
// ═════════════════════════════════════════════════════════════════════════════
void MetricsCollector::collectAll()
{
    collectFromPheromoneField();
    collectFromAdversary();
    collectFromDomainNodes();
    collectNetworkMetrics();
}

// ═════════════════════════════════════════════════════════════════════════════
// (A) Collecte depuis PheromoneField
//
// Métriques globales du champ τ :
//   fieldEntropy       H du champ normalisé (bits)
//   consistencyPenalty Ω_t global
//   fieldNorm          ||τ||_2
//   tau_d              valeur τ par domaine
// ═════════════════════════════════════════════════════════════════════════════
void MetricsCollector::collectFromPheromoneField()
{
    if (!pheromoneField) return;

    // Métriques globales
    double fieldH   = pheromoneField->getGlobalEntropy();
    double omega    = pheromoneField->getConsistencyPenalty();
    double norm     = pheromoneField->getFieldNorm();

    pushToWindow("fieldEntropy",       -1, fieldH);
    pushToWindow("consistencyPenalty", -1, omega);
    pushToWindow("fieldNorm",          -1, norm);

    emit(sig_globalOmega, omega);

    // Pic de violation de cohérence
    peakConsistencyViolation = std::max(peakConsistencyViolation, omega);

    // Valeur τ par domaine
    auto snapshot = pheromoneField->getSnapshot();
    for (int d = 0; d < std::min((int)snapshot.size(), numDomains); d++) {
        pushToWindow("pheromoneLevel", d, snapshot[d]);
    }

    EV_DETAIL << "[MetricsCollector] PF: H=" << fieldH
              << " Ω=" << omega << " ||τ||=" << norm << endl;
}

// ═════════════════════════════════════════════════════════════════════════════
// (B) Collecte depuis AdversaryModel
//
// Métriques du modèle adverse :
//   beliefEntropy      H(b_t) vue adverse (bits)
//   beliefMax          max_g b_t(g)
//   isSuspicious       H < θ_susp
//   attentionEntropy   H des poids α_d
// ═════════════════════════════════════════════════════════════════════════════
void MetricsCollector::collectFromAdversary()
{
    if (!adversary) return;

    double h       = adversary->getEntropy();
    double bMax    = *std::max_element(adversary->getBelief().begin(),
                                       adversary->getBelief().end());
    bool   susp    = adversary->isSuspicious();

    pushToWindow("beliefEntropy",   -1, h);
    pushToWindow("beliefMax",       -1, bMax);
    pushToWindow("suspicionLevel",  -1, susp ? 1.0 : 0.0);

    emit(sig_globalH, h);

    // time_to_suspicion : premier tick où H < θ_susp
    if (susp && !firstSuspicionLogged) {
        if (writeCSV) {
            writeCSVRow(simTime(), "timeToSuspicion", -1, simTime().dbl());
        }
        firstSuspicionLogged = true;
        suspicionEvents++;
        emit(sig_alertSuspicion, simTime().dbl());
        EV_WARN << "[MetricsCollector] SUSPICION détectée à t=" << simTime()
                << " H=" << h << " bits" << endl;
    }

    // Entropie poids d'attention
    double hAtt = 0.0;
    for (double a : adversary->getAttentionWeights()) {
        if (a > 1e-12) hAtt -= a * log2safe(a);
    }
    pushToWindow("attentionEntropy", -1, hAtt);

    // Vérification anomalies combinées
    double omega = pheromoneField ? pheromoneField->getConsistencyPenalty() : 0.0;
    checkAnomalies(h, omega);
}

// ═════════════════════════════════════════════════════════════════════════════
// (C) Collecte depuis les DomainNode
//
// Pour chaque domaine d : lire les signaux via getSubmodule() + getDisplayString().
// En OMNeT++, la méthode propre est de s'abonner aux signaux via cIListener.
// Ici on interroge directement les scalaires disponibles via getStatistic().
// ═════════════════════════════════════════════════════════════════════════════
void MetricsCollector::collectFromDomainNodes()
{
    cModule *net = getParentModule();
    if (!net) return;

    for (int d = 0; d < numDomains; d++) {
        cModule *dom = net->getSubmodule("domain", d);
        if (!dom) continue;

        // Lire les statistiques déclarées dans DomainNode.ned via @statistic
        // En OMNeT++ 6.x : dom->getResultRecorders() ou via signals
        // Approche portable : lire les paramètres courants du module

        // Les @statistic sont collectés via les signaux OMNeT++.
        // Ici on lit uniquement les paramètres de configuration (cPar).

        // Lecture des paramètres configurés (stables pendant la sim)
        double costPerUnit = dom->par("costPerUnit");
        double budgetTotal = dom->par("budgetTotal");

        // Métrique dérivée : effort alloué normalisé
        double effortNorm = 1.0 / numDomains; // Valeur par défaut uniforme
        pushToWindow("deceptionEffortNorm", d, effortNorm);

        // Overhead par domaine (estimation basée sur syncInterval)
        double syncInterval = dom->par("syncIntervalSec");
        double overheadPerTick = 48.0; // PAYLOAD_BYTES
        double overheadRate    = overheadPerTick / syncInterval;
        pushToWindow("commOverheadRate", d, overheadRate);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// (D) Métriques réseau globales
//
// comm_overhead_bytes : total données échangées sur la période
// L_leak_stig         : fuite informationnelle cumulée
// ═════════════════════════════════════════════════════════════════════════════
void MetricsCollector::collectNetworkMetrics()
{
    // Estimation overhead : numDomains broadcasts × PAYLOAD_BYTES × collectPeriod/syncInterval
    double syncInterval = 2.0; // §4 sync_interval_sec
    double overhead = numDomains * 48.0 * (collectPeriod.dbl() / syncInterval);
    cumulativeOverhead += overhead;
    pushToWindow("commOverhead", -1, cumulativeOverhead);
    emit(sig_globalOverhead, cumulativeOverhead);

    // L_leak : fuite basée sur H(b_t) courant
    double hMax  = log2safe((double)3); // log2(|G|)
    double hCurr = adversary ? adversary->getEntropy() : hMax;
    double leak  = std::max(0.0, hMax - hCurr) * std::log(2.0); // nats
    cumulativeInfoLeak += leak * collectPeriod.dbl();
    pushToWindow("infoLeak", -1, cumulativeInfoLeak);
}

// ═════════════════════════════════════════════════════════════════════════════
// Détection d'anomalies
// ═════════════════════════════════════════════════════════════════════════════
void MetricsCollector::checkAnomalies(double h, double omega)
{
    // Alerte si cohérence violée ET adversaire non confus
    if (omega > omegaEpsilon && h < 1.0) {
        EV_WARN << "[MetricsCollector] ANOMALIE t=" << simTime()
                << " : Ω=" << omega << " > ε=" << omegaEpsilon
                << " ET H=" << h << " bits (bas)" << endl;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// flushWindowsToCSV() — écriture des agrégats dans le fichier CSV
// ═════════════════════════════════════════════════════════════════════════════
void MetricsCollector::flushWindowsToCSV()
{
    if (!csvFile.is_open()) return;

    simtime_t t = simTime();
    for (auto &[key, win] : windows) {
        if (win.count() == 0) continue;

        // Décomposer la clé "metricName_domainId"
        size_t sep = key.rfind('_');
        std::string metric   = (sep != std::string::npos) ? key.substr(0, sep) : key;
        int         domainId = (sep != std::string::npos)
                               ? std::stoi(key.substr(sep + 1)) : -1;

        writeAggregatedRow(t, metric, domainId, win);

        // Reset de la fenêtre pour le prochain intervalle
        win.reset(t);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// writeCSVRow() — écriture d'une seule valeur instantanée
// ─────────────────────────────────────────────────────────────────────────────
void MetricsCollector::writeCSVRow(simtime_t t, const std::string &metric,
                                    int domainId, double value)
{
    if (!csvFile.is_open()) return;
    csvFile << std::fixed << std::setprecision(6)
            << t.dbl()   << ","
            << configName << ","
            << runIndex  << ","
            << metric    << ","
            << domainId  << ","
            << value     << ","
            << value     << ","  // mean = value pour valeur instantanée
            << 0.0       << ","  // std  = 0
            << value     << ","  // min
            << value     << ","  // max
            << 1         << "\n";// count = 1
}

// ─────────────────────────────────────────────────────────────────────────────
// writeAggregatedRow() — écriture d'une fenêtre agrégée
// ─────────────────────────────────────────────────────────────────────────────
void MetricsCollector::writeAggregatedRow(simtime_t t, const std::string &metric,
                                           int domainId, const MetricWindow &w)
{
    if (!csvFile.is_open() || w.count() == 0) return;
    csvFile << std::fixed << std::setprecision(6)
            << t.dbl()    << ","
            << configName  << ","
            << runIndex    << ","
            << metric      << ","
            << domainId    << ","
            << w.values.back() << ","  // Dernière valeur brute
            << w.mean()    << ","
            << w.stddev()  << ","
            << w.minVal()  << ","
            << w.maxVal()  << ","
            << w.count()   << "\n";
}

// ═════════════════════════════════════════════════════════════════════════════
// finish() — scalaires de synthèse + fermeture CSV
// ═════════════════════════════════════════════════════════════════════════════
void MetricsCollector::finish()
{
    // Dernier flush
    if (writeCSV) {
        collectAll();
        flushWindowsToCSV();
        closeCSVFile();
    }

    // Scalaires OMNeT++ (.sca)
    recordScalar("total_collect_ticks",        (double)totalCollectTicks);
    recordScalar("suspicion_events",           (double)suspicionEvents);
    recordScalar("cumulative_overhead_bytes",  cumulativeOverhead);
    recordScalar("cumulative_info_leak_nats",  cumulativeInfoLeak);
    recordScalar("peak_consistency_violation", peakConsistencyViolation);
    recordScalar("first_suspicion_detected",   firstSuspicionLogged ? 1.0 : 0.0);

    EV_INFO << "═══ MetricsCollector::finish() ═══" << endl
            << "  Ticks collectés : " << totalCollectTicks << endl
            << "  Évts suspicion  : " << suspicionEvents << endl
            << "  Overhead total  : " << cumulativeOverhead << " bytes" << endl
            << "  L_leak cumulée  : " << cumulativeInfoLeak << " nats" << endl
            << "  Ω_t peak        : " << peakConsistencyViolation << endl
            << "  CSV écrit       : " << (writeCSV ? csvPath : "non") << endl;
}

// ═════════════════════════════════════════════════════════════════════════════
// Gestion du fichier CSV
// ═════════════════════════════════════════════════════════════════════════════
void MetricsCollector::openCSVFile()
{
    // Construire le nom du fichier
    std::ostringstream oss;
    oss << outputDir << "/metrics_"
        << configName << "_run" << runIndex << ".csv";
    csvPath = oss.str();

    // Créer le répertoire si nécessaire (portable C++17)
    // Note : en OMNeT++, le répertoire results/processed/ est créé par le Makefile
    csvFile.open(csvPath, std::ios::out | std::ios::trunc);

    if (!csvFile.is_open()) {
        EV_WARN << "[MetricsCollector] Impossible d'ouvrir : " << csvPath
                << " — écriture CSV désactivée" << endl;
        writeCSV = false;
        return;
    }

    writeCSVHeader();
    EV_INFO << "[MetricsCollector] CSV ouvert : " << csvPath << endl;
}

void MetricsCollector::writeCSVHeader()
{
    csvFile << "time,config,run,metric,domain,value,mean,std,min,max,count\n";
}

void MetricsCollector::closeCSVFile()
{
    if (csvFile.is_open()) {
        csvFile.flush();
        csvFile.close();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Utilitaires
// ═════════════════════════════════════════════════════════════════════════════

std::string MetricsCollector::windowKey(const std::string &metric,
                                         int domainId) const
{
    return metric + "_" + std::to_string(domainId);
}

void MetricsCollector::pushToWindow(const std::string &metric,
                                     int domainId, double val)
{
    std::string key = windowKey(metric, domainId);
    auto &win = windows[key];
    if (win.windowStart == SIMTIME_ZERO)
        win.reset(simTime());
    win.push(val);
}

std::string MetricsCollector::getConfigName() const
{
    const char *cfg = getEnvir()->getConfigEx()->getActiveConfigName();
    return cfg ? std::string(cfg) : "unknown";
}
