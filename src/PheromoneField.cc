// ─────────────────────────────────────────────────────────────────────────────
// PheromoneField.cc
//
// Module OMNeT++ champ phéromonal global partagé.
//
// Cycle de vie du champ à chaque tick (période = syncInterval) :
//
//   ① Évaporation   : τ_d ← (1 - ρ_p) · τ_d          ∀d
//   ② Diffusion     : τ_d ← τ_d + α · (τ̄_N(d) - τ_d) ∀d  [optionnel]
//   ③ Bruit adverse : τ_d ← τ_d + ε_d                 ∀d  [E3 jamming]
//   ④ Stats         : émission des 6 signaux @statistic
//
// Entre les ticks, les DomainNode appellent deposit() pour déposer leur
// incrément local. Le champ converge ou diverge selon l'équilibre
// dépôt / évaporation / diffusion.
//
// Référence équations :
//   White paper §3.a  — Eq. phéromonale  τ_d(t+1) = (1-ρ)τ_d + ξ∇_d
//   White paper §3.b  — Cohérence Ω_t   = Σ ω_ij |τ_i - τ_j|
// ─────────────────────────────────────────────────────────────────────────────

#include "PheromoneField.h"

Define_Module(PheromoneField);

// ═════════════════════════════════════════════════════════════════════════════
// Destructeur
// ═════════════════════════════════════════════════════════════════════════════
PheromoneField::~PheromoneField()
{
    cancelAndDelete(evapTimer);
}

// ═════════════════════════════════════════════════════════════════════════════
// initialize()
// ═════════════════════════════════════════════════════════════════════════════
void PheromoneField::initialize()
{
    // ── Dimension ─────────────────────────────────────────────────────────────
    numDomains = par("numDomains");
    ASSERT(numDomains > 0);

    // ── Paramètres (§ default.yaml) ───────────────────────────────────────────
    rhoP           = par("rhoP");           // 0.1
    diffAlpha      = par("diffAlpha");      // 0.05  (diffusion douce)
    depositClamp   = par("depositClamp");   // 0.3   (amplitude max dépôt)
    noiseAmplitude = par("noiseAmplitude"); // 0.05  (bruit adversarial)
    jammingProb    = par("jammingProb");    // 0.0 → E3
    enableDiffusion = par("enableDiffusion"); // true
    evapPeriod     = par("evapPeriod");     // 2.0s = syncIntervalSec

    // ── Allocation du champ ───────────────────────────────────────────────────
    tau.assign(numDomains, 0.0);
    tauPrev.assign(numDomains, 0.0);
    tauInit.assign(numDomains, 0.0);

    // Initialisation avec légère asymétrie pour briser la symétrie du système
    for (int d = 0; d < numDomains; d++) {
        tau[d]     = 0.5 + 0.05 * std::sin(d * 1.2);  // ∈ [0.45, 0.55]
        tauInit[d] = tau[d];
    }
    tauPrev = tau;

    // ── Topologie par défaut ──────────────────────────────────────────────────
    // La topologie réelle est injectée par StigmergyNetwork::initialize()
    // via setTopology(). En attendant, on construit le graphe par défaut.
    buildDefaultTopology();

    // ── Enregistrement des signaux ────────────────────────────────────────────
    sig_fieldEntropy       = registerSignal("fieldEntropy");
    sig_evaporationLoss    = registerSignal("evaporationLoss");
    sig_diffusionGain      = registerSignal("diffusionGain");
    sig_adversarialNoise   = registerSignal("adversarialNoise");
    sig_globalConsistency  = registerSignal("globalConsistency");
    sig_fieldNorm          = registerSignal("fieldNorm");

    // ── Timer d'évaporation globale ───────────────────────────────────────────
    evapTimer = new cMessage("evapTimer");
    scheduleAt(simTime() + evapPeriod, evapTimer);

    EV_INFO << "[PheromoneField] init : D=" << numDomains
            << " ρ=" << rhoP
            << " α=" << diffAlpha
            << " jammingProb=" << jammingProb
            << " evapPeriod=" << evapPeriod << "s"
            << endl;

    // Première émission de stats à t=0
    emitFieldStats();
}

// ═════════════════════════════════════════════════════════════════════════════
// handleMessage() — uniquement le self-message evapTimer
// ═════════════════════════════════════════════════════════════════════════════
void PheromoneField::handleMessage(cMessage *msg)
{
    if (msg == evapTimer) {

        // Snapshot avant tick pour mesurer les variations
        tauPrev = tau;

        // ① Évaporation globale
        tickEvaporation();

        // ② Diffusion spatiale (lissage entre voisins)
        if (enableDiffusion) {
            tickDiffusion();
        }

        // ③ Perturbation adversariale (E3 : jamming)
        if (jammingProb > 0.0) {
            tickAdversarialNoise();
        }

        // ④ Historique + statistiques
        pushHistory();
        emitFieldStats();

        // Reschedule
        scheduleAt(simTime() + evapPeriod, evapTimer);
    }
    else {
        EV_WARN << "[PheromoneField] message inattendu : " << msg->getName() << endl;
        delete msg;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// ① Évaporation globale
//
//   τ_d(t+1) ← (1 - ρ_p) · τ_d(t)    ∀d ∈ D
//
// L'évaporation modélise l'oubli naturel des signaux phéromonaux :
// sans dépôt actif, le champ converge vers 0.
// La perte d'énergie est enregistrée pour Results B (evaporationLoss).
// ═════════════════════════════════════════════════════════════════════════════
void PheromoneField::tickEvaporation()
{
    double totalLoss = 0.0;

    for (int d = 0; d < numDomains; d++) {
        double before = tau[d];
        tau[d]  = clip(tau[d] * (1.0 - rhoP));
        totalLoss += (before - tau[d]);
    }

    emit(sig_evaporationLoss, totalLoss);

    EV_DETAIL << "[PheromoneField] évaporation : perte totale=" << totalLoss << endl;
}

// ═════════════════════════════════════════════════════════════════════════════
// ② Diffusion spatiale
//
//   τ_d ← τ_d + α · ( τ̄_{N(d)} - τ_d )
//
//   τ̄_{N(d)} = Σ_{j∈N(d)} ω_{dj} · τ_j / Σ_{j∈N(d)} ω_{dj}
//
// La diffusion homogénéise le champ entre voisins directs.
// Elle réduit les écarts narratifs (↓ Ω_t) mais peut diluer les
// signaux forts si α est trop grand.
//
// Note : on calcule d'abord tous les nouveaux τ dans un buffer,
//        puis on les applique — pas de contamination intra-tick.
// ═════════════════════════════════════════════════════════════════════════════
void PheromoneField::tickDiffusion()
{
    std::vector<double> tauNew = tau; // Buffer : on part de l'état post-évap
    double totalGain = 0.0;

    for (int d = 0; d < numDomains; d++) {
        if (adjacency[d].empty()) continue;

        // Moyenne pondérée des voisins
        double wSum = 0.0, wTauSum = 0.0;
        for (int j : adjacency[d]) {
            double w = edgeWeights[d][j]; // 0 si pas de lien
            wSum    += w;
            wTauSum += w * tau[j];
        }
        if (wSum < 1e-9) continue;

        double tauNeighMean = wTauSum / wSum;
        double delta = diffAlpha * (tauNeighMean - tau[d]);
        tauNew[d] = clip(tau[d] + delta);
        totalGain += std::abs(delta);
    }

    tau = tauNew;
    emit(sig_diffusionGain, totalGain);

    EV_DETAIL << "[PheromoneField] diffusion : gain total=" << totalGain << endl;
}

// ═════════════════════════════════════════════════════════════════════════════
// ③ Perturbation adversariale (E3 — jammingProb > 0)
//
// Modèle : à chaque tick, chaque domaine subit avec probabilité jammingProb
// une injection ε_d ∈ [-noiseAmplitude, +noiseAmplitude].
//
// Cela simule un adversaire qui tente d'injecter de faux signaux phéromonaux
// pour tromper le mécanisme de coordination (attaque sur τ_d).
//
// La perturbation est enregistrée via sig_adversarialNoise pour Results C.
// ═════════════════════════════════════════════════════════════════════════════
void PheromoneField::tickAdversarialNoise()
{
    double totalNoise = 0.0;

    for (int d = 0; d < numDomains; d++) {
        // Tirage Bernoulli : ce domaine est-il ciblé ce tick ?
        if (uniform(0.0, 1.0) < jammingProb) {
            // Bruit uniforme centré
            double epsilon = uniform(-noiseAmplitude, noiseAmplitude);
            tau[d] = clip(tau[d] + epsilon);
            totalNoise += std::abs(epsilon);

            EV_DETAIL << "[PheromoneField] injection sur domaine " << d
                      << " ε=" << epsilon
                      << " τ_new=" << tau[d] << endl;
        }
    }

    if (totalNoise > 0.0) {
        emit(sig_adversarialNoise, totalNoise);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// ④ Historique glissant
// ═════════════════════════════════════════════════════════════════════════════
void PheromoneField::pushHistory()
{
    FieldSnapshot snap;
    snap.time = simTime();
    snap.tau  = tau;
    history.push_back(snap);
    if ((int)history.size() > HISTORY_LEN)
        history.pop_front();
}

// ═════════════════════════════════════════════════════════════════════════════
// ④ Émission des statistiques globales du champ
// ═════════════════════════════════════════════════════════════════════════════
void PheromoneField::emitFieldStats()
{
    // fieldEntropy : H du champ τ normalisé comme distribution
    emit(sig_fieldEntropy, getGlobalEntropy());

    // globalConsistency : 1 - Ω_t normalisé par le nombre de liens
    double omega    = getConsistencyPenalty();
    double nEdges   = std::max(1, (int)edges.size());
    double consist  = 1.0 - (omega / nEdges);
    emit(sig_globalConsistency, clip(consist, 0.0, 1.0));

    // fieldNorm : ||τ||_2
    emit(sig_fieldNorm, getFieldNorm());

    EV_DETAIL << "[PheromoneField] t=" << simTime()
              << " H=" << getGlobalEntropy()
              << " Ω=" << omega
              << " ||τ||=" << getFieldNorm()
              << endl;
}

// ═════════════════════════════════════════════════════════════════════════════
// API publique — deposit()
//
// Dépose un incrément Δ sur le domaine d.
// Retourne la nouvelle valeur τ_d après dépôt.
//
// Utilisation depuis DomainNode::broadcastPheromoneUpdate() :
//   double newTau = pheromoneField->deposit(domainId, gradientLocal * xi);
// ═════════════════════════════════════════════════════════════════════════════
double PheromoneField::deposit(int domainId, double delta)
{
    if (!validId(domainId)) {
        EV_WARN << "[PheromoneField] deposit : domainId=" << domainId
                << " hors bornes (D=" << numDomains << ")" << endl;
        return 0.0;
    }

    // Clamp de l'incrément pour éviter les sauts brutaux
    double clampedDelta = clip(delta, -depositClamp, depositClamp);
    tau[domainId] = clip(tau[domainId] + clampedDelta);

    EV_DETAIL << "[PheromoneField] deposit d=" << domainId
              << " Δ=" << clampedDelta
              << " τ_new=" << tau[domainId] << endl;

    return tau[domainId];
}

// ═════════════════════════════════════════════════════════════════════════════
// API publique — setField()
// ═════════════════════════════════════════════════════════════════════════════
void PheromoneField::setField(int domainId, double value)
{
    if (!validId(domainId)) return;
    tau[domainId] = clip(value);
}

// ═════════════════════════════════════════════════════════════════════════════
// API publique — getField()
// ═════════════════════════════════════════════════════════════════════════════
double PheromoneField::getField(int domainId) const
{
    if (!validId(domainId)) return 0.0;
    return tau[domainId];
}

// ═════════════════════════════════════════════════════════════════════════════
// API publique — getSnapshot()
// ═════════════════════════════════════════════════════════════════════════════
std::vector<double> PheromoneField::getSnapshot() const
{
    return tau; // copie par valeur
}

// ═════════════════════════════════════════════════════════════════════════════
// API publique — getNeighborMean()
//
// Moyenne pondérée τ des voisins de domainId :
//   τ̄_{N(d)} = Σ_{j∈N(d)} ω_{dj} · τ_j / Σ_{j∈N(d)} ω_{dj}
//
// Utilisée par DomainNode::computeLocalGradient() pour calculer ∇_d.
// ═════════════════════════════════════════════════════════════════════════════
double PheromoneField::getNeighborMean(int domainId) const
{
    if (!validId(domainId) || adjacency[domainId].empty())
        return (validId(domainId) ? tau[domainId] : 0.0);

    double wSum = 0.0, wTauSum = 0.0;
    for (int j : adjacency[domainId]) {
        double w  = edgeWeights[domainId][j];
        wSum    += w;
        wTauSum += w * tau[j];
    }
    return (wSum > 1e-9) ? (wTauSum / wSum) : tau[domainId];
}

// ═════════════════════════════════════════════════════════════════════════════
// API publique — getGlobalEntropy()
//
// Entropie de Shannon du champ τ normalisé comme distribution :
//
//   p_d = τ_d / Σ_j τ_j
//   H   = -Σ_d p_d · log2(p_d)     [bits]
//
// H_max = log2(D) si tous les τ_d sont égaux (champ uniforme).
// H → 0  si toute l'énergie est concentrée sur un seul domaine.
//
// Interprétation : H élevé = énergie phéromonale bien distribuée
//                            = bonne coordination multi-domaine
// ═════════════════════════════════════════════════════════════════════════════
double PheromoneField::getGlobalEntropy() const
{
    double sum = 0.0;
    for (double v : tau) sum += v;
    if (sum < 1e-9) return 0.0;

    double h = 0.0;
    for (double v : tau) {
        double p = v / sum;
        h -= p * log2safe(p);
    }
    return h;
}

// ═════════════════════════════════════════════════════════════════════════════
// API publique — getConsistencyPenalty()
//
//   Ω_t = Σ_{(i,j)∈E} ω_{ij} · |τ_i - τ_j|
//
// Ω_t = 0  si tous les τ sont identiques (cohérence parfaite)
// Ω_t grand si les domaines ont des signaux très divergents
// ═════════════════════════════════════════════════════════════════════════════
double PheromoneField::getConsistencyPenalty() const
{
    double omega = 0.0;
    for (auto const &e : edges) {
        if (validId(e.src) && validId(e.dst)) {
            omega += e.weight * std::abs(tau[e.src] - tau[e.dst]);
        }
    }
    return omega;
}

// ═════════════════════════════════════════════════════════════════════════════
// API publique — getFieldNorm()
//
//   ||τ||_2 = sqrt( Σ_d τ_d² )
// ═════════════════════════════════════════════════════════════════════════════
double PheromoneField::getFieldNorm() const
{
    double sq = 0.0;
    for (double v : tau) sq += v * v;
    return std::sqrt(sq);
}

// ═════════════════════════════════════════════════════════════════════════════
// API publique — setTopology()
//
// Injectée par StigmergyNetwork::initialize() après que tous les nœuds
// sont créés. Construit les listes d'adjacence et matrices de poids.
// ═════════════════════════════════════════════════════════════════════════════
void PheromoneField::setTopology(const std::vector<DomainEdge> &edgeList)
{
    edges = edgeList;
    buildAdjacencyFromEdges();

    EV_INFO << "[PheromoneField] topologie mise à jour : "
            << edges.size() << " liens" << endl;
}

// ═════════════════════════════════════════════════════════════════════════════
// finish() — scalaires de synthèse
// ═════════════════════════════════════════════════════════════════════════════
void PheromoneField::finish()
{
    // Snapshot final du champ
    recordScalar("final_fieldEntropy",      getGlobalEntropy());
    recordScalar("final_consistencyPenalty", getConsistencyPenalty());
    recordScalar("final_fieldNorm",         getFieldNorm());

    // Statistiques de dérive sur la fenêtre d'historique
    if (history.size() >= 2) {
        // Variation moyenne du champ entre le premier et le dernier snapshot
        const auto &first = history.front().tau;
        const auto &last  = history.back().tau;
        double drift = 0.0;
        for (int d = 0; d < numDomains; d++) {
            drift += std::abs(last[d] - first[d]);
        }
        drift /= numDomains;
        recordScalar("mean_tau_drift_window", drift);
    }

    // Valeur finale de chaque τ_d
    for (int d = 0; d < numDomains; d++) {
        std::string key = std::string("tau_final_d") + std::to_string(d);
        recordScalar(key.c_str(), tau[d]);
    }

    EV_INFO << "═══ PheromoneField::finish() ═══" << endl
            << "  H_field   = " << getGlobalEntropy() << " bits" << endl
            << "  Omega_T   = " << getConsistencyPenalty() << endl
            << "  ||tau||_2 = " << getFieldNorm() << endl;
}

// ═════════════════════════════════════════════════════════════════════════════
// buildDefaultTopology()
//
// Graphe hexagonal pour D=6 (topologie par défaut du projet).
// Liens avec poids omegaDefault = 0.8 (§ default.yaml).
//
// Numérotation :
//   0=Cyber (centre), 1=Air, 2=Sol, 3=Maritime, 4=Spatial, 5=EM
//
// Topologie étoile + anneau :
//   Cyber ↔ {Air, Sol, Maritime, Spatial, EM}   (étoile)
//   Air ↔ Sol ↔ Maritime ↔ Spatial ↔ EM ↔ Air  (anneau)
//
// Pour D≠6 (expérience E5), setTopology() sera appelé avec la topologie réelle.
// ═════════════════════════════════════════════════════════════════════════════
void PheromoneField::buildDefaultTopology()
{
    edges.clear();
    double w = par("omegaDefault").doubleValue(); // 0.8

    if (numDomains == 6) {
        // Étoile : Cyber(0) ↔ tous
        for (int d = 1; d < numDomains; d++) {
            edges.push_back({0, d, w});
            edges.push_back({d, 0, w});
        }
        // Anneau : 1-2-3-4-5-1
        int ring[] = {1, 2, 3, 4, 5};
        for (int i = 0; i < 5; i++) {
            int a = ring[i], b = ring[(i+1) % 5];
            edges.push_back({a, b, w});
            edges.push_back({b, a, w});
        }
    }
    else {
        // Topologie chemin pour D quelconque (E5 : D ∈ {3, 9, 12})
        // Le nœud 0 est connecté à tous (étoile dégénérée)
        for (int d = 1; d < numDomains; d++) {
            edges.push_back({0, d, w});
            edges.push_back({d, 0, w});
        }
        // Anneau entre les nœuds non-centraux
        for (int d = 1; d < numDomains - 1; d++) {
            edges.push_back({d, d+1, w});
            edges.push_back({d+1, d, w});
        }
        // Fermeture de l'anneau
        if (numDomains > 2) {
            edges.push_back({1, numDomains-1, w});
            edges.push_back({numDomains-1, 1, w});
        }
    }

    buildAdjacencyFromEdges();

    EV_INFO << "[PheromoneField] topologie par défaut : D=" << numDomains
            << " → " << edges.size() << " arcs" << endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// buildAdjacencyFromEdges()
//
// Reconstruit les listes d'adjacence et la matrice de poids ω_{ij}
// à partir du vecteur edges. Appelé après chaque modification de topologie.
// ─────────────────────────────────────────────────────────────────────────────
void PheromoneField::buildAdjacencyFromEdges()
{
    // Réinitialisation
    adjacency.assign(numDomains, std::vector<int>());
    edgeWeights.assign(numDomains, std::vector<double>(numDomains, 0.0));

    for (auto const &e : edges) {
        if (!validId(e.src) || !validId(e.dst)) continue;
        if (e.src == e.dst) continue; // Pas d'auto-boucle

        // Ajout dans la liste d'adjacence si pas déjà présent
        auto &adjList = adjacency[e.src];
        if (std::find(adjList.begin(), adjList.end(), e.dst) == adjList.end()) {
            adjList.push_back(e.dst);
        }
        // Le poids est la moyenne si plusieurs arcs parallèles existent
        double &w = edgeWeights[e.src][e.dst];
        w = (w == 0.0) ? e.weight : 0.5 * (w + e.weight);
    }
}
