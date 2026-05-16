// ─────────────────────────────────────────────────────────────────────────────
// AdversaryModel.cc
//
// Modèle adverse intelligent : fusion bayésienne multi-domaine + GNN attention.
//
// Flux de traitement à chaque tick :
//   ① Collecter les observations o_d^t de tous les domaines actifs
//   ② Mettre à jour les poids d'attention α_d^t (GNN léger)
//   ③ Fusion bayésienne pondérée : b_t(g) ← Σ_d α_d · Bayes(o_d^t, b_{t-1})
//   ④ Mettre à jour l'ordre de sondage (adaptatif ou shift E2)
//   ⑤ Émettre les métriques adverses
//
// Le module est PASSIF entre les ticks : les DomainNode appellent observe()
// à chaque réception de broadcast phéromonal.
// ─────────────────────────────────────────────────────────────────────────────

#include "AdversaryModel.h"

Define_Module(AdversaryModel);

// ═════════════════════════════════════════════════════════════════════════════
// Destructeur
// ═════════════════════════════════════════════════════════════════════════════
AdversaryModel::~AdversaryModel()
{
    cancelAndDelete(updateTimer);
    cancelAndDelete(probeTimer);
}

// ═════════════════════════════════════════════════════════════════════════════
// initialize()
// ═════════════════════════════════════════════════════════════════════════════
void AdversaryModel::initialize()
{
    // ── Paramètres (§4 adversary.yaml) ───────────────────────────────────────
    numDomains        = par("numDomains");
    numGoals          = ADV_NUM_GOALS;
    beliefInit        = par("beliefInit").stringValue();
    gnnHiddenDim      = par("gnnHiddenDim");
    attentionLr       = par("attentionLr");
    suspicionThreshold = par("suspicionThreshold");
    probingIntensity  = par("probingIntensity").stringValue();
    updatePeriod      = par("updatePeriod");

    // ── Initialisation de la croyance b_0(g) ─────────────────────────────────
    belief.resize(numGoals);
    if (beliefInit == "uniform") {
        std::fill(belief.begin(), belief.end(), 1.0 / numGoals);
    } else if (beliefInit == "concentrated") {
        // Concentrée sur l'objectif 0 : cas pessimiste pour le défenseur
        std::fill(belief.begin(), belief.end(), 0.05);
        belief[0] = 1.0 - 0.05 * (numGoals - 1);
    } else {
        std::fill(belief.begin(), belief.end(), 1.0 / numGoals);
    }

    // ── Initialisation des poids d'attention ──────────────────────────────────
    // Uniformes au départ : tous les domaines ont la même saillance
    attentionWeights.assign(numDomains, 1.0 / numDomains);

    // ── Matrice W_attention (gnnHiddenDim × 2) ───────────────────────────────
    // Initialisée avec de petites valeurs aléatoires (Xavier init)
    W_attention.assign(gnnHiddenDim, std::vector<double>(2, 0.0));
    double scale = std::sqrt(2.0 / (2 + gnnHiddenDim));
    for (int i = 0; i < gnnHiddenDim; i++) {
        W_attention[i][0] = uniform(-scale, scale); // w_tau
        W_attention[i][1] = uniform(-scale, scale); // w_entropy
    }

    // ── Ordre de sondage initial ──────────────────────────────────────────────
    probingOrder.resize(numDomains);
    std::iota(probingOrder.begin(), probingOrder.end(), 0);

    // ── Flags E2 ──────────────────────────────────────────────────────────────
    strategyShifted = false;
    shiftApplied    = false;

    // ── Enregistrement des signaux ────────────────────────────────────────────
    sig_suspicionLevel   = registerSignal("suspicionLevel");
    sig_beliefMax        = registerSignal("beliefMax");
    sig_entropyBelief    = registerSignal("entropyBelief");
    sig_attentionEntropy = registerSignal("attentionEntropy");
    sig_probingTarget    = registerSignal("probingTarget");
    sig_strategyShift    = registerSignal("strategyShift");

    // ── Timers ────────────────────────────────────────────────────────────────
    updateTimer = new cMessage("adversaryUpdateTimer");
    scheduleAt(simTime() + updatePeriod, updateTimer);

    // Timer de sondage : plus fréquent que la mise à jour de croyance
    probeTimer = new cMessage("adversaryProbeTimer");
    scheduleAt(simTime() + updatePeriod * 0.5, probeTimer);

    EV_INFO << "[AdversaryModel] init : D=" << numDomains
            << " |G|=" << numGoals
            << " beliefInit=" << beliefInit
            << " probing=" << probingIntensity
            << " suspThresh=" << suspicionThreshold
            << endl;
}

// ═════════════════════════════════════════════════════════════════════════════
// handleMessage()
// ═════════════════════════════════════════════════════════════════════════════
void AdversaryModel::handleMessage(cMessage *msg)
{
    if (msg == updateTimer) {
        // ① Vérifier shift de stratégie (E2)
        if (probingIntensity == "shift_at_t100"
            && simTime() > 100.0
            && !shiftApplied)
        {
            applyStrategyShift();
            shiftApplied = true;
            emit(sig_strategyShift, 1.0);
        }

        // ② Mise à jour des poids d'attention
        updateAttentionWeights();

        // ③ Fusion bayésienne de toutes les observations récentes
        bayesianFuseAll();

        // ④ Recalcul de l'ordre de sondage
        updateProbingOrder();

        // ⑤ Métriques
        emitAdversaryMetrics();

        scheduleAt(simTime() + updatePeriod, updateTimer);
    }
    else if (msg == probeTimer) {
        // Sondage actif : l'adversaire inspecte les domaines en priorité
        if (!probingOrder.empty()) {
            int target = probingOrder[0];
            emit(sig_probingTarget, (double)target);
            EV_DETAIL << "[AdversaryModel] sondage prioritaire : domaine "
                      << target << endl;
        }
        scheduleAt(simTime() + updatePeriod * 0.5, probeTimer);
    }
    else {
        EV_WARN << "[AdversaryModel] message inattendu : " << msg->getName() << endl;
        delete msg;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// API publique — observe()
//
// Appelée par DomainNode::socketDataArrived() à chaque paquet phéromonal reçu.
// Stocke l'observation et met à jour les statistiques en ligne de τ_d.
// ═════════════════════════════════════════════════════════════════════════════
void AdversaryModel::observe(int domainId, double tauValue, char narrative,
                              double effort, simtime_t ts)
{
    if (domainId < 0 || domainId >= numDomains) return;

    DomainObservation obs;
    obs.domainId  = domainId;
    obs.tauValue  = tauValue;
    obs.narrative = narrative;
    obs.effort    = effort;
    obs.timestamp = ts;

    // Stocker dans l'historique (fenêtre glissante)
    auto &hist = obsHistory[domainId];
    hist.push_back(obs);
    if ((int)hist.size() > ADV_OBS_HISTORY) hist.pop_front();

    // Mettre à jour la dernière observation
    lastObs[domainId] = obs;

    // Mise à jour statistiques en ligne : moyenne et variance de τ_d
    updateTauStats(domainId, tauValue);

    EV_DETAIL << "[AdversaryModel] obs d=" << domainId
              << " τ=" << tauValue
              << " narr=" << narrative
              << " t=" << ts << endl;
}

// ═════════════════════════════════════════════════════════════════════════════
// ① Fusion bayésienne de toutes les observations récentes
//
// Pour chaque domaine d avec une observation récente :
//   b_t(g) ← α_d · p(o_d^t | g) · b_{t-1}(g)  +  (1-α_d) · b_{t-1}(g)
//
// La pondération par α_d permet à l'adversaire de donner plus de poids
// aux domaines les plus informatifs (forte variance de τ).
// ═════════════════════════════════════════════════════════════════════════════
void AdversaryModel::bayesianFuseAll()
{
    if (lastObs.empty()) return;

    for (auto const &[domId, obs] : lastObs) {
        // Ignorer les observations trop anciennes (> 3 × updatePeriod)
        if (simTime() - obs.timestamp > 3.0 * updatePeriod) continue;

        double alpha = (domId < (int)attentionWeights.size())
                       ? attentionWeights[domId] : 1.0 / numDomains;

        bayesianUpdate(obs, alpha);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Mise à jour bayésienne pondérée pour une seule observation
//
//   b_t(g) ∝ [ α · p(o|g) + (1-α) ] · b_{t-1}(g)
//
// Le terme (1-α) empêche la dégénérescence quand α est faible.
// ─────────────────────────────────────────────────────────────────────────────
void AdversaryModel::bayesianUpdate(const DomainObservation &obs, double attWeight)
{
    double sumPosterior = 0.0;
    std::vector<double> posterior(numGoals);

    for (int g = 0; g < numGoals; g++) {
        double lik   = likelihood(obs.tauValue, g);
        // Mélange pondéré : attWeight · vraisemblance + (1-attWeight) · uniforme
        double mixed = attWeight * lik + (1.0 - attWeight) * (1.0 / numGoals);
        posterior[g] = mixed * belief[g];
        sumPosterior += posterior[g];
    }

    if (sumPosterior < 1e-12) {
        // Dégénérescence : reset uniforme
        std::fill(belief.begin(), belief.end(), 1.0 / numGoals);
        return;
    }

    for (int g = 0; g < numGoals; g++) {
        belief[g] = posterior[g] / sumPosterior;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Vraisemblance p(o | g)  ←  modèle gaussien
//
//   p(o | g) ∝ exp( -β · (o - sig(g))² )
//
// sig(g) ∈ {0.25, 0.55, 0.85} : signatures des 3 objectifs.
// ─────────────────────────────────────────────────────────────────────────────
double AdversaryModel::likelihood(double observation, int goal) const
{
    double diff = observation - ADV_GOAL_SIGS[goal];
    return std::exp(-ADV_BETA * diff * diff);
}

// ═════════════════════════════════════════════════════════════════════════════
// ② Mécanisme d'attention GNN léger
//
// Pour chaque domaine d, le score de saillance s_d est calculé par
// une couche linéaire + ReLU :
//
//   h_d = ReLU( W_a · [τ_d, H_d] )     (gnnHiddenDim-dimensionnel)
//   s_d = Σ_i h_d[i]                   (agrégation par somme)
//   α   = softmax(s)                   (normalisation)
//
// Les poids W_a sont mis à jour par gradient stochastique (§4 attention_lr).
// ═════════════════════════════════════════════════════════════════════════════
void AdversaryModel::updateAttentionWeights()
{
    if (lastObs.empty()) return;

    std::vector<double> scores(numDomains, 0.0);

    for (int d = 0; d < numDomains; d++) {
        scores[d] = computeDomainSalience(d);
    }

    // Normalisation softmax → α_d ∈ (0,1),  Σ α_d = 1
    attentionWeights = softmax(scores);

    // Mise à jour des paramètres W_a par gradient
    // Signal de supervision : erreur de prédiction de la croyance
    double hCurrent = getEntropy();
    double hMax     = log2safe((double)numGoals);
    double loss     = hMax - hCurrent; // On veut maximiser H → minimiser loss

    for (auto const &[domId, obs] : lastObs) {
        if (domId >= numDomains) continue;
        gradientStepAttention(domId, loss);
    }

    EV_DETAIL << "[AdversaryModel] attention α: ";
    for (int d = 0; d < numDomains; d++)
        EV_DETAIL << "d" << d << "=" << attentionWeights[d] << " ";
    EV_DETAIL << endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// Score de saillance d'un domaine : h_d = ReLU(W_a · [τ_d, H_d])
// ─────────────────────────────────────────────────────────────────────────────
double AdversaryModel::computeDomainSalience(int domainId) const
{
    auto itObs = lastObs.find(domainId);
    if (itObs == lastObs.end()) return 0.0;

    double tau_d = itObs->second.tauValue;

    // H_d estimée depuis la variance de τ : forte variance → forte incertitude
    double var_d = (tauVariance.count(domainId)) ? tauVariance.at(domainId) : 0.1;
    double H_d   = std::min(1.0, var_d * 5.0); // Normalisation heuristique

    // Feature vector : [τ_d, H_d]
    double feat[2] = {tau_d, H_d};

    // Couche linéaire + ReLU
    double score = 0.0;
    for (int i = 0; i < gnnHiddenDim && i < (int)W_attention.size(); i++) {
        double z = W_attention[i][0] * feat[0] + W_attention[i][1] * feat[1];
        score += std::max(0.0, z); // ReLU
    }
    return score;
}

// ─────────────────────────────────────────────────────────────────────────────
// Gradient step sur W_a (descente de gradient stochastique)
//
//   ∂L/∂W_a[i][j] ≈ loss · α_d · feat[j]   (gradient approché)
//   W_a[i][j]     ← W_a[i][j] - η · ∂L/∂W_a[i][j]
// ─────────────────────────────────────────────────────────────────────────────
void AdversaryModel::gradientStepAttention(int domainId, double loss)
{
    auto itObs = lastObs.find(domainId);
    if (itObs == lastObs.end()) return;

    double tau_d   = itObs->second.tauValue;
    double var_d   = tauVariance.count(domainId) ? tauVariance.at(domainId) : 0.1;
    double H_d     = std::min(1.0, var_d * 5.0);
    double feat[2] = {tau_d, H_d};

    double alpha_d = (domainId < (int)attentionWeights.size())
                     ? attentionWeights[domainId] : 1.0 / numDomains;

    for (int i = 0; i < gnnHiddenDim && i < (int)W_attention.size(); i++) {
        for (int j = 0; j < 2; j++) {
            double grad = loss * alpha_d * feat[j];
            W_attention[i][j] -= attentionLr * grad;
            // Clamp pour stabilité numérique
            W_attention[i][j] = std::max(-2.0, std::min(2.0, W_attention[i][j]));
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// softmax(logits) → distribution normalisée
// ─────────────────────────────────────────────────────────────────────────────
std::vector<double> AdversaryModel::softmax(const std::vector<double> &logits) const
{
    if (logits.empty()) return {};
    double maxLogit = *std::max_element(logits.begin(), logits.end());
    std::vector<double> expVals(logits.size());
    double sumExp = 0.0;
    for (size_t i = 0; i < logits.size(); i++) {
        expVals[i] = std::exp(logits[i] - maxLogit); // Stabilité numérique
        sumExp += expVals[i];
    }
    if (sumExp < 1e-12) {
        return std::vector<double>(logits.size(), 1.0 / logits.size());
    }
    for (double &v : expVals) v /= sumExp;
    return expVals;
}

// ═════════════════════════════════════════════════════════════════════════════
// ③ Stratégie de sondage — mise à jour de l'ordre de priorité
//
// "adaptive"  : trier les domaines par variance décroissante de τ
//               (les plus imprévisibles sont sondés en premier)
// "uniform"   : ordre circulaire
// "shift_at_t100" : adaptive avant t=100, puis reset + nouvelle stratégie
// ═════════════════════════════════════════════════════════════════════════════
void AdversaryModel::updateProbingOrder()
{
    probingOrder.resize(numDomains);
    std::iota(probingOrder.begin(), probingOrder.end(), 0);

    std::string strategy = probingIntensity;
    if (strategy == "shift_at_t100") {
        strategy = strategyShifted ? "uniform" : "adaptive";
    }

    if (strategy == "adaptive") {
        // Tri par variance décroissante
        std::sort(probingOrder.begin(), probingOrder.end(),
                  [this](int a, int b) {
                      double va = tauVariance.count(a) ? tauVariance.at(a) : 0.0;
                      double vb = tauVariance.count(b) ? tauVariance.at(b) : 0.0;
                      return va > vb; // Décroissant
                  });
    }
    // "uniform" → ordre naturel 0..D-1 (déjà fait par iota)
}

// ─────────────────────────────────────────────────────────────────────────────
// Appliquer le shift de stratégie à t=100s (E2)
//
// L'adversaire réinitialise sa croyance et adopte une stratégie uniforme
// pour explorer les domaines qu'il a peut-être négligés.
// ─────────────────────────────────────────────────────────────────────────────
void AdversaryModel::applyStrategyShift()
{
    EV_INFO << "[AdversaryModel] STRATEGY SHIFT à t=" << simTime()
            << " : reset croyance + passage à sondage uniforme" << endl;

    // Reset croyance vers uniforme (recommencer l'inférence)
    std::fill(belief.begin(), belief.end(), 1.0 / numGoals);

    // Reset poids d'attention
    std::fill(attentionWeights.begin(), attentionWeights.end(),
              1.0 / numDomains);

    strategyShifted = true;
}

// ═════════════════════════════════════════════════════════════════════════════
// Mise à jour statistiques en ligne de τ_d (moyenne et variance glissante)
//
// Algorithme de Welford (numériquement stable, O(1) par observation) :
//   count ← count + 1
//   delta ← x - mean
//   mean  ← mean + delta / count
//   M2    ← M2 + delta · (x - mean)
//   var   ← M2 / (count - 1)
// ═════════════════════════════════════════════════════════════════════════════
void AdversaryModel::updateTauStats(int domainId, double tauValue)
{
    tauCount[domainId]++;
    int    n     = tauCount[domainId];
    double delta = tauValue - tauMean[domainId];
    tauMean[domainId] += delta / n;

    // Pour la variance, on accumule M2 dans tauVariance temporairement
    double delta2 = tauValue - tauMean[domainId];
    tauVariance[domainId] += delta * delta2;

    // Convertir en variance non biaisée
    if (n > 1) {
        tauVariance[domainId] = tauVariance[domainId] / (n - 1);
    } else {
        tauVariance[domainId] = 0.0;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// API — getEntropy()  →  H(b_t) en bits
// ═════════════════════════════════════════════════════════════════════════════
double AdversaryModel::getEntropy() const
{
    double h = 0.0;
    for (double p : belief) {
        h -= p * log2safe(p);
    }
    return h;
}

// ═════════════════════════════════════════════════════════════════════════════
// API — getMostLikelyGoal()  →  argmax_g b_t(g)
// ═════════════════════════════════════════════════════════════════════════════
int AdversaryModel::getMostLikelyGoal() const
{
    return (int)(std::max_element(belief.begin(), belief.end()) - belief.begin());
}

// ═════════════════════════════════════════════════════════════════════════════
// API — isSuspicious()  →  H(b_t) < θ_susp
// ═════════════════════════════════════════════════════════════════════════════
bool AdversaryModel::isSuspicious() const
{
    return getEntropy() < suspicionThreshold;
}

// ═════════════════════════════════════════════════════════════════════════════
// Émission des métriques adverses
// ═════════════════════════════════════════════════════════════════════════════
void AdversaryModel::emitAdversaryMetrics()
{
    double h       = getEntropy();
    double beliefMax = *std::max_element(belief.begin(), belief.end());

    emit(sig_entropyBelief,  h);
    emit(sig_beliefMax,      beliefMax);
    emit(sig_suspicionLevel, isSuspicious() ? 1.0 : 0.0);

    // Entropie des poids d'attention (mesure de concentration du sondage)
    double hAtt = 0.0;
    for (double a : attentionWeights) {
        if (a > 1e-12) hAtt -= a * log2safe(a);
    }
    emit(sig_attentionEntropy, hAtt);

    EV_DETAIL << "[AdversaryModel] t=" << simTime()
              << " H(b)=" << h
              << " max_g=" << beliefMax
              << " suspicious=" << isSuspicious()
              << " H(α)=" << hAtt
              << endl;
}

// ═════════════════════════════════════════════════════════════════════════════
// finish() — scalaires de synthèse
// ═════════════════════════════════════════════════════════════════════════════
void AdversaryModel::finish()
{
    double hFinal = getEntropy();
    recordScalar("adversary_final_entropy",    hFinal);
    recordScalar("adversary_most_likely_goal", (double)getMostLikelyGoal());
    recordScalar("adversary_is_suspicious",    isSuspicious() ? 1.0 : 0.0);
    recordScalar("adversary_strategy_shifted", strategyShifted ? 1.0 : 0.0);

    // Attention weights finaux
    for (int d = 0; d < (int)attentionWeights.size(); d++) {
        std::string key = "attention_d" + std::to_string(d);
        recordScalar(key.c_str(), attentionWeights[d]);
    }

    EV_INFO << "═══ AdversaryModel::finish() ═══" << endl
            << "  H_final = " << hFinal << " bits" << endl
            << "  best_g  = " << getMostLikelyGoal() << endl
            << "  suspicious = " << isSuspicious() << endl;
}

// ─────────────────────────────────────────────────────────────────────────────
// normalizeInPlace : normalise un vecteur pour que Σ = 1
// ─────────────────────────────────────────────────────────────────────────────
void AdversaryModel::normalizeInPlace(std::vector<double> &v) const
{
    double s = 0.0;
    for (double x : v) s += x;
    if (s > 1e-12)
        for (double &x : v) x /= s;
    else
        std::fill(v.begin(), v.end(), 1.0 / v.size());
}
