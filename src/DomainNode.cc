// ─────────────────────────────────────────────────────────────────────────────
// DomainNode.cc
//
// Implémentation de l'application stigmergique pour OMNeT++ / INET.
//
// Chaque instance représente un domaine d ∈ D du système de déception
// multi-domaine. La logique couvre :
//
//   (1) Mise à jour phéromonale     τ_d(t+1) = (1-ρ)·τ_d(t) + ξ·∇_d(t)
//   (2) Allocation budgétaire       x_d^t  ∝ τ_d(t) / Σ τ_j(t)
//   (3) Rotation narrative          quorum phéromonal → switch A/B/C
//   (4) Modèle bayésien adverse     b_t(g) ∝ p(o_t|g) · b_{t-1}(g)
//   (5) Collecte métriques          17 signaux @statistic → CSV via scavetool
//   (6) Gestion pannes (E4)         NodeStatus + timer exponentiel
//   (7) Détection injection         z-score sur historique τ
//   (8) Fuite informationnelle      L_leak = I(τ_{1:T} ; g*)
// ─────────────────────────────────────────────────────────────────────────────

#include "DomainNode.h"

Define_Module(DomainNode);

// ═════════════════════════════════════════════════════════════════════════════
// Destructeur
// ═════════════════════════════════════════════════════════════════════════════
DomainNode::~DomainNode()
{
    cancelAndDelete(syncTimer);
    cancelAndDelete(metricsTimer);
    cancelAndDelete(failureTimer);
    cancelAndDelete(neighborTimeout);
}

// ═════════════════════════════════════════════════════════════════════════════
// Initialisation en deux étapes (INET)
// ═════════════════════════════════════════════════════════════════════════════
int DomainNode::numInitStages() const
{
    return 2;
}

void DomainNode::initialize(int stage)
{
    

    // ── Étape 1 : paramètres locaux ──────────────────────────────────────────
    if (stage == 0) {

        // Identité
        domainId    = par("domainId");
        numDomains  = par("numDomains");
        domainLabel = par("domainLabel").stringValue();

        // Paramètres stigmergiques
        rhoP              = par("rhoP");
        xi                = par("xi");
        omegaDefault      = par("omegaDefault");
        thetaTau          = par("thetaTau");
        quorumRatio       = par("quorumRatio");
        rotationCooldown  = par("rotationCooldown");
        suspicionThreshold = par("suspicionThreshold");
        hThresh           = H_THRESH_DEFAULT;

        // Budget
        budgetTotal  = par("budgetTotal");
        costPerUnit  = par("costPerUnit");
        parseNarrativeOptions(par("narrativeOptions").stringValue());

        // Adversaire
        probingIntensity = par("probingIntensity").stringValue();
        adversaryShifted = false;

        // Lifecycle / pannes
        nodeFailureRate = par("nodeFailureRate");
        nodeAlive       = true;

        // Intervalle de synchronisation
        syncInterval = par("syncIntervalSec");

        // ── État initial phéromonal ──────────────────────────────────────────
        // On randomise légèrement pour casser la symétrie entre domaines
        tauD     = uniform(0.35, 0.65);
        tauDPrev = tauD;

        // ── Croyance adverse initiale ────────────────────────────────────────
        beliefBt.assign(NUM_GOALS, 1.0 / NUM_GOALS); // uniforme

        // ── Narrative initiale ───────────────────────────────────────────────
        currentNarrative = narrativeOptions.empty() ? 'A' : narrativeOptions[0];
        cooldownCounter  = 0;

        // ── Budget initial : allocation uniforme naïve ────────────────────────
        deceptionEffort = budgetTotal / (numDomains * costPerUnit);

        // ── Compteurs métriques ──────────────────────────────────────────────
        suspicionRecorded    = false;
        deceptionPersistCount = 0;
        falseGoalCount       = 0;
        totalEpisodes        = 0;
        quorumAttempts       = 0;
        quorumSuccesses      = 0;
        cumulativeInfoLeak   = 0.0;
        hAtLastCheckpoint    = log2(NUM_GOALS); // H_max initial

        // ── Enregistrement des signaux @statistic ─────────────────────────────
        // Noms exactement identiques aux @statistic[...] dans DomainNode.ned
        sig_deceptionEffort    = registerSignal("deceptionEffort");
        sig_pheromoneLevel     = registerSignal("pheromoneLevel");
        sig_beliefEntropy      = registerSignal("beliefEntropy");
        sig_falseGoalInduction = registerSignal("falseGoalInduction");
        sig_deceptionPersistence = registerSignal("deceptionPersistence");
        sig_timeToSuspicion    = registerSignal("timeToSuspicion");

        sig_consistencyViolation = registerSignal("consistencyViolation");
        sig_narrativeDivergence  = registerSignal("narrativeDivergence");
        sig_syncDelay            = registerSignal("syncDelay");
        sig_quorumTriggerSuccess = registerSignal("quorumTriggerSuccess");

        sig_commOverhead    = registerSignal("commOverhead");
        sig_infoLeak        = registerSignal("infoLeak");
        sig_spoofDetection  = registerSignal("spoofDetection");

        sig_performanceDrop      = registerSignal("performanceDrop");
        sig_gracefulDegradation  = registerSignal("gracefulDegradation");

        EV_INFO << "[DomainNode] Stage LOCAL — domainId=" << domainId
                << " (" << domainLabel << ")"
                << " τ_0=" << tauD
                << " budget=" << budgetTotal
                << " κ=" << costPerUnit
                << endl;
    }

    // ── Étape 2 : réseau prêt → socket et timers ────────────────────────────
    if (stage == 1) {

        // Socket UDP broadcast
        socket.setOutputGate(gate("socketOut"));
        socket.setCallback(this);
        socket.bind(UDP_PORT);
        socket.setBroadcast(true);

        // Timer de synchronisation phéromonale
        // Jitter initial pour éviter les collisions au démarrage
        syncTimer = new cMessage("syncTimer");
        scheduleAt(simTime() + uniform(0.1, syncInterval.dbl()), syncTimer);

        // Timer de collecte métriques (toutes les secondes)
        metricsTimer = new cMessage("metricsTimer");
        scheduleAt(simTime() + 1.0, metricsTimer);

        // Timer de timeout voisins (3× syncInterval)
        neighborTimeout = new cMessage("neighborTimeout");
        scheduleAt(simTime() + 3.0 * syncInterval.dbl(), neighborTimeout);

        // Timer de panne (E4) : temps exponentiel si nodeFailureRate > 0
        if (nodeFailureRate > 0.0 && uniform(0.0, 1.0) < nodeFailureRate) {
            failureTimer = new cMessage("failureTimer");
            // Panne entre t=20s et t=180s pour toucher différentes phases
            failureTime = simTime() + uniform(20.0, 180.0);
            scheduleAt(failureTime, failureTimer);
            EV_INFO << "[DomainNode] domain=" << domainId
                    << " failure scheduled at t=" << failureTime << endl;
        }

        EV_INFO << "[DomainNode] Stage APP — domainId=" << domainId
                << " socket bound on port " << UDP_PORT << endl;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Dispatch des messages
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::handleMessage(cMessage *msg)
{
    if (!nodeAlive) {
        // Nœud en panne : ignorer tous les messages sauf les paquets entrants
        // (on les absorbe silencieusement pour ne pas saturer la file)
        if (!msg->isSelfMessage()) delete msg;
        return;
    }

    if (msg == syncTimer) {
        handleSyncTimer();
    }
    else if (msg == metricsTimer) {
        handleMetricsTimer();
    }
    else if (msg == neighborTimeout) {
        handleNeighborTimeout();
    }
    else if (msg == failureTimer) {
        triggerNodeFailure();
    }
    else {
        // Message réseau entrant → délégué au socket UDP
        socket.processMessage(msg);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Timer de synchronisation phéromonale
// Période : syncInterval secondes
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::handleSyncTimer()
{
    // ── (1) Mise à jour du champ phéromonal ──────────────────────────────────
    updatePheromoneField();

    // ── (2) Mise à jour de l'effort de déception x_d^t ──────────────────────
    updateDeceptionEffort();

    // ── (3) Vérification rotation narrative (quorum) ─────────────────────────
    if (cooldownCounter <= 0) {
        checkNarrativeRotation();
    } else {
        cooldownCounter--;
    }

    // ── (4) Adaptation à l'adversaire (E2 : shift_at_t100) ──────────────────
    if (probingIntensity == "shift_at_t100"
        && simTime() > 100.0
        && !adversaryShifted)
    {
        adaptToAdversaryShift();
        adversaryShifted = true;
    }

    // ── (5) Broadcast du vecteur phéromonal aux voisins ──────────────────────
    broadcastPheromoneUpdate();

    // Reschedule avec jitter minimal pour éviter la synchronisation parfaite
    scheduleAt(simTime() + syncInterval + uniform(-0.05, 0.05), syncTimer);
}

// ═════════════════════════════════════════════════════════════════════════════
// Timer de collecte métriques (1 fois par seconde)
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::handleMetricsTimer()
{
    emitDeceptionMetrics();
    emitCoordinationMetrics();
    emitRobustnessMetrics();
    // emitNetworkMetrics() est appelé à chaque broadcast (Results C)

    scheduleAt(simTime() + 1.0, metricsTimer);
}

// ═════════════════════════════════════════════════════════════════════════════
// Timeout voisins : marque les nœuds silencieux comme possiblement en panne
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::handleNeighborTimeout()
{
    simtime_t deadline = simTime() - 3.0 * syncInterval;
    for (auto &[id, nb] : neighbors) {
        if (nb.alive && nb.lastSeen < deadline) {
            nb.alive = false;
            EV_WARN << "[DomainNode] domain=" << domainId
                    << " : voisin " << id
                    << " présumé en panne (lastSeen=" << nb.lastSeen << ")" << endl;
        }
    }
    scheduleAt(simTime() + 3.0 * syncInterval.dbl(), neighborTimeout);
}

// ═════════════════════════════════════════════════════════════════════════════
// Panne nœud (E4)
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::triggerNodeFailure()
{
    nodeAlive = false;

    // Snapshot H(b_T) pour calcul ΔH dans Results D
    double hNow = computeBeliefEntropy();
    double deltaH = hAtLastCheckpoint - hNow;
    emit(sig_performanceDrop, deltaH);

    // Arrêt propre du socket
    socket.close();
    cancelEvent(syncTimer);
    cancelEvent(metricsTimer);
    cancelEvent(neighborTimeout);

    EV_WARN << "[DomainNode] domain=" << domainId
            << " (" << domainLabel << ") FAILED at t=" << simTime()
            << " ΔH=" << deltaH << " bits" << endl;
}

// ═════════════════════════════════════════════════════════════════════════════
// (1) Mise à jour du champ phéromonal
//
//   τ_d(t+1) = clamp( (1 - ρ_p) · τ_d(t) + ξ · ∇_d(t) , 0, 1 )
//
//   ∇_d(t) = ( H(b_t) - H_avg_voisins ) / H_max
//            → positif si on déçoit mieux que les voisins
//            → négatif sinon (les voisins renforcent leur phéromone)
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::updatePheromoneField()
{
    tauDPrev = tauD;
    double grad = computeLocalGradient();
    tauD = clamp((1.0 - rhoP) * tauD + xi * grad, TAU_MIN, TAU_MAX);

    // Mise à jour historique pour détection d'injection (R_spoof)
    tauHistory.push_back(tauD);
    if ((int)tauHistory.size() > TAU_HISTORY_LEN)
        tauHistory.pop_front();
}

// ─────────────────────────────────────────────────────────────────────────────
// ∇_d(t) : gradient local normalisé
// ─────────────────────────────────────────────────────────────────────────────
double DomainNode::computeLocalGradient()
{
    double hLocal  = computeBeliefEntropy();
    double hMax    = log2(NUM_GOALS); // H_max = log2(|G|) bits

    // Moyenne de l'entropie des voisins actifs
    double hNeighSum = 0.0;
    int    activeNeigh = 0;
    for (auto &[id, nb] : neighbors) {
        if (nb.alive) {
            hNeighSum += nb.beliefEntropy;
            activeNeigh++;
        }
    }
    double hNeighAvg = (activeNeigh > 0) ? (hNeighSum / activeNeigh) : hLocal;

    // Gradient : avantage relatif par rapport aux voisins, normalisé
    double grad = (hLocal - hNeighAvg) / std::max(hMax, 1e-6);
    return clamp(grad, -1.0, 1.0);
}

// ═════════════════════════════════════════════════════════════════════════════
// (2) Mise à jour de l'effort de déception x_d^t
//
//   Allocation proportionnelle au signal phéromonal relatif :
//
//   x_d^t = (τ_d(t) / Σ_{j alive} τ_j(t)) · (B_t / κ_d)
//
//   Contrainte budgétaire : Σ_d κ_d · x_d^t ≤ B_t
//   (respectée par construction via la normalisation)
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::updateDeceptionEffort()
{
    double sumTau = tauD;
    for (auto &[id, nb] : neighbors) {
        if (nb.alive) sumTau += nb.tau;
    }

    if (sumTau > 1e-9) {
        deceptionEffort = (tauD / sumTau) * (budgetTotal / costPerUnit);
    } else {
        // Fallback : allocation uniforme
        deceptionEffort = budgetTotal / (numDomains * costPerUnit);
    }

    // Clamp sécuritaire
    deceptionEffort = clamp(deceptionEffort, 0.0, budgetTotal / costPerUnit);
}

// ═════════════════════════════════════════════════════════════════════════════
// (3) Rotation narrative par quorum phéromonal
//
// Condition : fraction de voisins avec τ_j > θ_τ ≥ quorum_ratio
//             ET τ_d > θ_τ (ce nœud lui-même valide)
//
// Cohérence : on vérifie Ω_t < ε avant de valider (pas de divergence)
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::checkNarrativeRotation()
{
    if (neighbors.empty()) return;

    // Comptage des voisins actifs avec τ > θ_τ
    int totalActive = 0, votes = 0;
    for (auto &[id, nb] : neighbors) {
        if (nb.alive) {
            totalActive++;
            if (nb.tau > thetaTau) votes++;
        }
    }
    if (totalActive == 0) return;

    double voteFraction = (double)votes / totalActive;
    double omegaT       = computeConsistencyPenalty();
    bool   quorumMet    = (voteFraction >= quorumRatio) && (tauD > thetaTau);
    bool   coherent     = (omegaT < 0.1); // ε = 0.1

    quorumAttempts++;

    if (quorumMet && coherent) {
        char prev = currentNarrative;
        currentNarrative = nextNarrative(currentNarrative);
        cooldownCounter  = rotationCooldown;
        quorumSuccesses++;

        double successRate = (double)quorumSuccesses / quorumAttempts;
        emit(sig_quorumTriggerSuccess, successRate);

        EV_INFO << "[DomainNode] domain=" << domainId
                << " narrative " << prev << " → " << currentNarrative
                << " (votes=" << votes << "/" << totalActive
                << " Ω=" << omegaT << ")" << endl;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// (4a) Adaptation au shift adverse (E2 : probing_intensity="shift_at_t100")
//
// À t=100s, l'adversaire change de stratégie de sondage.
// Réponse défensive : uniformiser b_t → relancer la confusion,
//                     booster τ_d → mobiliser les voisins via phéromone.
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::adaptToAdversaryShift()
{
    EV_INFO << "[DomainNode] domain=" << domainId
            << " adversary shift détecté à t=" << simTime()
            << " — reset croyance + boost τ" << endl;

    // Réinitialisation de la croyance adverse
    beliefBt.assign(NUM_GOALS, 1.0 / NUM_GOALS);

    // Boost phéromonal pour signaler l'urgence aux voisins
    tauD = clamp(tauD + 0.20, TAU_MIN, TAU_MAX);

    // Forcer une rotation narrative immédiate si cooldown écoulé
    cooldownCounter = 0;
}

// ═════════════════════════════════════════════════════════════════════════════
// (4b) Mise à jour bayésienne de b_t(g)
//
//   b_t(g) ← p(o_t | g) · b_{t-1}(g)  /  Σ_{g'} p(o_t | g') · b_{t-1}(g')
//
//   Modèle de vraisemblance :
//     p(o | g) ∝ exp( -β · |o - sig(g)|² )
//
//   avec sig(g) ∈ {0.25, 0.55, 0.85} (signatures des 3 objectifs)
//   et   β = 8 (concentration — plus β grand, plus l'adversaire est précis)
//
//   L'observation o est la valeur τ reçue du voisin.
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::updateBeliefBayesian(const PheromonePayload &payload)
{
    const double beta = 8.0;
    double observation = payload.tauValue; // Observable adverse : τ_d(t)

    // Calcul des vraisemblances p(o|g)
    std::vector<double> likelihood(NUM_GOALS);
    double sumLik = 0.0;
    for (int g = 0; g < NUM_GOALS; g++) {
        double diff = observation - GOAL_SIGS[g];
        likelihood[g] = std::exp(-beta * diff * diff);
        sumLik += likelihood[g] * beliefBt[g];
    }

    // Normalisation de Bayes
    if (sumLik > 1e-12) {
        for (int g = 0; g < NUM_GOALS; g++) {
            beliefBt[g] = (likelihood[g] * beliefBt[g]) / sumLik;
        }
    } else {
        // Dégénérescence : retour à l'uniforme
        beliefBt.assign(NUM_GOALS, 1.0 / NUM_GOALS);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// H(b_t) = -Σ_g b_t(g) · log2(b_t(g))   [bits]
// ─────────────────────────────────────────────────────────────────────────────
double DomainNode::computeBeliefEntropy() const
{
    double h = 0.0;
    for (double p : beliefBt) {
        h -= p * log2safe(p);
    }
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
// Objectif le plus probable selon b_t(g)
// ─────────────────────────────────────────────────────────────────────────────
int DomainNode::mostLikelyGoal() const
{
    return (int)(std::max_element(beliefBt.begin(), beliefBt.end()) - beliefBt.begin());
}

// ─────────────────────────────────────────────────────────────────────────────
// Vrai si l'adversaire croit au mauvais objectif (objectif 0 = vrai but caché)
// ─────────────────────────────────────────────────────────────────────────────
bool DomainNode::isAdversaryMisled() const
{
    return (mostLikelyGoal() != 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// MÉTRIQUES — Section Results A : Déception
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::emitDeceptionMetrics()
{
    double h = computeBeliefEntropy();

    // deceptionEffort, pheromoneLevel
    emit(sig_deceptionEffort, deceptionEffort);
    emit(sig_pheromoneLevel,  tauD);

    // beliefEntropy H(b_t)
    emit(sig_beliefEntropy, h);
    hAtLastCheckpoint = h; // snapshot pour ΔH (Results D)

    // deceptionPersistence : nb de pas cumulés où H > H_thresh
    if (h > hThresh) {
        deceptionPersistCount++;
        emit(sig_deceptionPersistence, (double)deceptionPersistCount);
    }

    // falseGoalInduction : taux d'épisodes où l'adversaire se trompe
    totalEpisodes++;
    if (isAdversaryMisled()) falseGoalCount++;
    emit(sig_falseGoalInduction, (double)falseGoalCount / totalEpisodes);

    // timeToSuspicion : premier t où H < θ_susp (émis une seule fois)
    if (!suspicionRecorded && h < suspicionThreshold) {
        emit(sig_timeToSuspicion, simTime().dbl());
        suspicionRecorded = true;
        EV_INFO << "[DomainNode] domain=" << domainId
                << " SUSPICIOUS at t=" << simTime()
                << " H=" << h << " bits" << endl;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// MÉTRIQUES — Section Results B : Coordination
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::emitCoordinationMetrics()
{
    // consistencyViolation Ω_t
    double omegaT = computeConsistencyPenalty();
    if (omegaT > 0.05) { // ε = 0.05
        emit(sig_consistencyViolation, omegaT);
    }

    // narrativeDivergence : max_{(i,j)} ||ô_i - ô_j||
    double div = computeNarrativeDivergence();
    emit(sig_narrativeDivergence, div);

    // quorumTriggerSuccess : taux de succès cumulé (émis dans checkNarrativeRotation)
}

// ═════════════════════════════════════════════════════════════════════════════
// MÉTRIQUES — Section Results C : Réseau / Sécurité
// Appelée à chaque broadcast (bytesTransmitted = PAYLOAD_BYTES)
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::emitNetworkMetrics(int bytesTransmitted)
{
    // commOverhead
    emit(sig_commOverhead, (double)bytesTransmitted);

    // infoLeak L_leak : information mutuelle τ_{1:T} ↔ g*
    double leak = computeInfoLeak();
    cumulativeInfoLeak += leak;
    emit(sig_infoLeak, cumulativeInfoLeak);

    // spoofDetection R_spoof : probabilité de détecter une injection adverse
    // (calculée sur la dernière valeur reçue, non sur un broadcast local)
    // → émise dans socketDataArrived
}

// ═════════════════════════════════════════════════════════════════════════════
// MÉTRIQUES — Section Results D : Robustesse
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::emitRobustnessMetrics()
{
    // gracefulDegradation : pente H(b_T) vs. pertes réseau.
    // On estime la dégradation locale comme la dérivée de H par rapport
    // à la variation phéromonale causée par les pertes.
    double dhdt = (computeBeliefEntropy() - hAtLastCheckpoint);
    emit(sig_gracefulDegradation, dhdt);

    // performanceDrop est émis dans triggerNodeFailure()
}

// ─────────────────────────────────────────────────────────────────────────────
// Ω_t = Σ_{(i,j)∈E} ω_ij · |τ_i - τ_j|
// ─────────────────────────────────────────────────────────────────────────────
double DomainNode::computeConsistencyPenalty() const
{
    double omega = 0.0;
    for (auto const &[id, nb] : neighbors) {
        if (nb.alive) {
            omega += nb.omega * std::abs(tauD - nb.tau);
        }
    }
    return omega;
}

// ─────────────────────────────────────────────────────────────────────────────
// Divergence narrative : distance τ maximale entre voisins actifs
// ─────────────────────────────────────────────────────────────────────────────
double DomainNode::computeNarrativeDivergence() const
{
    if (neighbors.empty()) return 0.0;
    double maxDiff = 0.0;
    for (auto const &[id, nb] : neighbors) {
        if (nb.alive) {
            maxDiff = std::max(maxDiff, std::abs(tauD - nb.tau));
        }
    }
    return maxDiff;
}

// ─────────────────────────────────────────────────────────────────────────────
// L_leak : fuite informationnelle estimée par l'entropie conditionnelle
//
//   L_leak ≈ H_max - H(b_t)  si H_max >> H(b_t)  → fuite élevée
//           ≈ 0               si H(b_t) ≈ H_max   → adversaire confus
//
// Normalisation en nats : × ln(2)
// ─────────────────────────────────────────────────────────────────────────────
double DomainNode::computeInfoLeak() const
{
    double hMax  = log2(NUM_GOALS);
    double hCurr = computeBeliefEntropy();
    double leakBits = std::max(0.0, hMax - hCurr);
    return leakBits * std::log(2.0); // conversion bits → nats
}

// ─────────────────────────────────────────────────────────────────────────────
// R_spoof : détection d'injection par z-score sur l'historique τ
//
// Si la valeur τ reçue s'écarte de plus de 2σ de l'historique local,
// on la considère comme suspecte (possiblement injectée).
// ─────────────────────────────────────────────────────────────────────────────
double DomainNode::estimateSpoofProbability(double incomingTau) const
{
    if ((int)tauHistory.size() < 3) return 0.0; // Pas assez d'historique

    // Moyenne et écart-type de l'historique
    double sum = std::accumulate(tauHistory.begin(), tauHistory.end(), 0.0);
    double mean = sum / tauHistory.size();
    double sqSum = 0.0;
    for (double v : tauHistory) sqSum += (v - mean) * (v - mean);
    double stdDev = std::sqrt(sqSum / tauHistory.size());

    if (stdDev < 1e-9) return 0.0; // Historique constant : pas de référence

    double zScore = std::abs(incomingTau - mean) / stdDev;

    // Probabilité de détection sigmoïde sur le z-score
    // P(spoof) ≈ σ(z - 2)  : quasi-nul pour z < 2, monte vite au-dessus
    return 1.0 / (1.0 + std::exp(-(zScore - 2.0)));
}

// ═════════════════════════════════════════════════════════════════════════════
// Communication UDP — Broadcast du vecteur phéromonal
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::broadcastPheromoneUpdate()
{
    PheromonePayload pl = serializeLocalState();

    // Création du paquet OMNeT++
    auto pkt  = new Packet("PheromoneUpdate");
    auto data = makeShared<ByteCountChunk>(B(PAYLOAD_BYTES));
    pkt->insertAtBack(data);

    // Annotation du temps de création pour mesurer sync_delay côté récepteur
    pkt->setTimestamp(simTime());

    // Broadcast UDP 255.255.255.255
    socket.sendTo(pkt, Ipv4Address::ALLONES_ADDRESS, UDP_PORT);

    // Métriques réseau
    emitNetworkMetrics(PAYLOAD_BYTES);

    EV_DETAIL << "[DomainNode] domain=" << domainId
              << " BROADCAST τ=" << pl.tauValue
              << " x=" << pl.deceptionEffort
              << " narrative=" << pl.narrative
              << " H=" << pl.beliefEntropy
              << endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Sérialisation de l'état local en PheromonePayload
// En production : encoder dans un ByteArrayChunk via cMsgPar ou protobuf-lite.
// Ici : on utilise une structure en mémoire (simulation pure, pas de réseau réel).
// ─────────────────────────────────────────────────────────────────────────────
PheromonePayload DomainNode::serializeLocalState() const
{
    PheromonePayload pl;
    pl.srcDomainId    = domainId;
    pl.tauValue       = tauD;
    pl.deceptionEffort = deceptionEffort;
    pl.narrative      = currentNarrative;
    pl.beliefEntropy  = computeBeliefEntropy();
    pl.timestamp      = simTime();
    return pl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Désérialisation d'un paquet entrant
// Note : dans OMNeT++, les données applicatives sont transportées via des tags
// ou des chunks. Pour garder le code portable, on lit les métadonnées du paquet
// et on reconstitue un payload synthétique basé sur l'adresse source.
// Un vrai déploiement utiliserait un FieldsChunk personnalisé.
// ─────────────────────────────────────────────────────────────────────────────
PheromonePayload DomainNode::deserializePayload(Packet *pkt) const
{
    PheromonePayload pl;

    // Récupération de l'adresse source via le tag L3AddressInd (INET)
    if (auto *addrTag = pkt->findTag<L3AddressInd>()) {
        // Mapping adresse IP → domainId :
        // Par convention dans notre réseau 10.0.0.x/24, domainId = dernier octet - 1
        auto srcAddr = addrTag->getSrcAddress().toIpv4();
        int lastOctet = srcAddr.getInt() & 0xFF;
        pl.srcDomainId = std::max(0, lastOctet - 1);
    } else {
        pl.srcDomainId = -1; // Inconnu
    }

    // Heure d'émission (timestamp) pour calcul sync_delay
    pl.timestamp = pkt->getTimestamp();

    // Valeurs phéromonales : en simulation pure, on les injecte via
    // un paramètre de module virtuel ou via un FieldsChunk.
    // Ici on utilise une valeur synthétique dérivée du timestamp pour
    // avoir de la variabilité (à remplacer par une vraie désérialisation).
    pl.tauValue       = 0.5 + 0.3 * std::sin(pl.timestamp.dbl() * 0.1 + pl.srcDomainId);
    pl.deceptionEffort = budgetTotal / numDomains;
    pl.narrative      = 'A';
    pl.beliefEntropy  = log2(NUM_GOALS) * 0.8;

    return pl;
}

// ═════════════════════════════════════════════════════════════════════════════
// Réception d'un paquet phéromonal UDP
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::socketDataArrived(UdpSocket *sock, Packet *pkt)
{
    simtime_t rxTime = simTime();

    // Désérialisation
    PheromonePayload pl = deserializePayload(pkt);

    // Ignorer nos propres broadcasts
    if (pl.srcDomainId == domainId) {
        delete pkt;
        return;
    }

    // ── Détection d'injection adverse (R_spoof) ──────────────────────────────
    double spoofProb = estimateSpoofProbability(pl.tauValue);
    emit(sig_spoofDetection, spoofProb);

    if (spoofProb > 0.85) {
        // Paquet suspect : on l'ignore et on alerte
        EV_WARN << "[DomainNode] domain=" << domainId
                << " INJECTION SUSPECTÉE de domaine " << pl.srcDomainId
                << " τ=" << pl.tauValue
                << " P_spoof=" << spoofProb << endl;
        delete pkt;
        return;
    }

    // ── Mise à jour de la table des voisins ──────────────────────────────────
    NeighborState &nb = neighbors[pl.srcDomainId];
    nb.domainId      = pl.srcDomainId;
    nb.tau           = pl.tauValue;
    nb.effort        = pl.deceptionEffort;
    nb.narrative     = pl.narrative;
    nb.beliefEntropy = pl.beliefEntropy;
    nb.lastSeen      = rxTime;
    nb.alive         = true;

    // Mise à jour de la fiabilité ω via filtre exponentiel
    // ω_new = α·ω_old + (1-α)·1.0  (renforcement à chaque réception réussie)
    const double alpha = 0.9;
    nb.omega = alpha * nb.omega + (1.0 - alpha) * 1.0;
    if (nb.omega == 0.0) nb.omega = omegaDefault; // Init première fois

    // ── Mise à jour bayésienne b_t(g) ────────────────────────────────────────
    updateBeliefBayesian(pl);

    // ── sync_delay_ms ─────────────────────────────────────────────────────────
    double delayMs = (rxTime - pl.timestamp).dbl() * 1000.0;
    if (delayMs >= 0.0) {
        emit(sig_syncDelay, delayMs);
    }

    EV_DETAIL << "[DomainNode] domain=" << domainId
              << " ← voisin " << pl.srcDomainId
              << " τ=" << pl.tauValue
              << " narrative=" << pl.narrative
              << " delay=" << delayMs << "ms"
              << " H_local=" << computeBeliefEntropy() << " bits"
              << endl;

    delete pkt;
}

// ─────────────────────────────────────────────────────────────────────────────
void DomainNode::socketErrorArrived(UdpSocket *sock, Indication *ind)
{
    EV_WARN << "[DomainNode] domain=" << domainId
            << " socket error : " << ind->getName() << endl;
    delete ind;
}

void DomainNode::socketClosed(UdpSocket *sock)
{
    EV_INFO << "[DomainNode] domain=" << domainId << " socket closed" << endl;
}

// ═════════════════════════════════════════════════════════════════════════════
// Fin de simulation : scalaires de synthèse
// ═════════════════════════════════════════════════════════════════════════════
void DomainNode::finish()
{
    double hFinal = computeBeliefEntropy();

    // Enregistrement des scalaires finaux (→ fichier .sca)
    recordScalar("final_beliefEntropy",       hFinal);
    recordScalar("final_tauD",                tauD);
    recordScalar("final_deceptionEffort",     deceptionEffort);
    recordScalar("total_commOverhead_bytes",
        (double)totalEpisodes * PAYLOAD_BYTES);
    recordScalar("false_goal_induction_rate",
        totalEpisodes > 0 ? (double)falseGoalCount / totalEpisodes : 0.0);
    recordScalar("quorum_success_rate",
        quorumAttempts > 0 ? (double)quorumSuccesses / quorumAttempts : 0.0);
    recordScalar("deception_persistence_steps", (double)deceptionPersistCount);
    recordScalar("cumulative_info_leak_nats",  cumulativeInfoLeak);
    recordScalar("node_alive_at_end",          nodeAlive ? 1.0 : 0.0);

    EV_INFO << "═══ DomainNode::finish() domain=" << domainId
            << " (" << domainLabel << ") ═══" << endl
            << "  H_final        = " << hFinal << " bits" << endl
            << "  τ_final        = " << tauD << endl
            << "  x_d_final      = " << deceptionEffort << endl
            << "  FGI_rate       = "
            << (totalEpisodes > 0 ? (double)falseGoalCount/totalEpisodes : 0.0)
            << endl
            << "  quorum_rate    = "
            << (quorumAttempts > 0 ? (double)quorumSuccesses/quorumAttempts : 0.0)
            << endl
            << "  L_leak_cumul   = " << cumulativeInfoLeak << " nats" << endl
            << "  node_alive     = " << (nodeAlive ? "YES" : "NO") << endl;

    cSimpleModule::finish();
}

// ═════════════════════════════════════════════════════════════════════════════
// Lifecycle OMNeT++ (start / stop / crash)
// ═════════════════════════════════════════════════════════════════════════════
// REMOVED handleStartOperation
{
    if (!nodeAlive) return;
    if (!syncTimer->isScheduled())
        scheduleAt(simTime() + uniform(0.1, syncInterval.dbl()), syncTimer);
    if (!metricsTimer->isScheduled())
        scheduleAt(simTime() + 1.0, metricsTimer);
}

// REMOVED handleStopOperation
{
    cancelEvent(syncTimer);
    cancelEvent(metricsTimer);
    cancelEvent(neighborTimeout);
    socket.close();
}

// REMOVED handleCrashOperation
{
    cancelEvent(syncTimer);
    cancelEvent(metricsTimer);
    cancelEvent(neighborTimeout);
    socket.destroy();
}

// ═════════════════════════════════════════════════════════════════════════════
// Utilitaires
// ═════════════════════════════════════════════════════════════════════════════

// Parse "A B C" → {'A', 'B', 'C'}
void DomainNode::parseNarrativeOptions(const std::string &optStr)
{
    narrativeOptions.clear();
    std::istringstream iss(optStr);
    std::string tok;
    while (iss >> tok) {
        if (!tok.empty()) narrativeOptions.push_back(tok[0]);
    }
    if (narrativeOptions.empty()) narrativeOptions = {'A', 'B', 'C'};
}

// Rotation cyclique A→B→C→A
char DomainNode::nextNarrative(char current) const
{
    for (int i = 0; i < (int)narrativeOptions.size(); i++) {
        if (narrativeOptions[i] == current) {
            return narrativeOptions[(i + 1) % narrativeOptions.size()];
        }
    }
    return narrativeOptions.empty() ? 'A' : narrativeOptions[0];
}
