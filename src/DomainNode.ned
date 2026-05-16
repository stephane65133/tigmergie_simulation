package stigmergie;

import inet.node.inet.AdhocHost;

//
// DomainNode : noeud domaine stigmergique
// Herite de AdhocHost (INET) qui fournit wlan, UDP, IP, mobilite.
// L'application stigmergique (DomainNode.cc) est registree via Define_Module.
//
// IMPORTANT : DomainNode.cc herite de ApplicationBase et est instancie
// directement comme simple module (pas compound).
// Le NED declare simple DomainNode pour que OMNeT++ associe
// DomainNode.cc a ce type.
//
simple DomainNode
{
    parameters:
        // Identite
        int    domainId         = default(0);
        int    numDomains       = default(6);
        string domainLabel      = default(
            domainId==0 ? "Cyber"    :
            domainId==1 ? "Air"      :
            domainId==2 ? "Sol"      :
            domainId==3 ? "Maritime" :
            domainId==4 ? "Spatial"  : "EM");

        // Parametres stigmergiques
        double rhoP             = default(0.1);
        double xi               = default(0.5);
        double omegaDefault     = default(0.8);
        double thetaTau         = default(0.7);
        double quorumRatio      = default(0.6);
        int    rotationCooldown = default(5);

        // Budget et deception
        double budgetTotal      = default(100.0);
        double costPerUnit      = default(1.0);
        string narrativeOptions = default("A B C");

        // Modele adversaire
        string beliefInit            = default("uniform");
        double suspicionThreshold    = default(0.3);
        string probingIntensity      = default("adaptive");
        int    gnnHiddenDim          = default(16);
        double attentionLr           = default(0.01);

        // Reseau / Lifecycle
        double syncIntervalSec  = default(2.0);
        double nodeFailureRate  = default(0.0);

        @display("i=device/server2;is=l");

        // Statistiques Results A
        @statistic[deceptionEffort](checkSignals=false; title="Effort deception x_d^t"; source=deceptionEffort; record=vector,mean,min,max,stats; unit="units"; interpolationmode=linear);
        @statistic[pheromoneLevel](checkSignals=false; title="Niveau pheromonal tau_d(t)"; source=pheromoneLevel; record=vector,mean,stats; unit="dimensionless"; interpolationmode=linear);
        @statistic[beliefEntropy](checkSignals=false; title="Entropie croyance H(bt)"; source=beliefEntropy; record=vector,mean,max,timeavg; unit="bits"; interpolationmode=linear);
        @statistic[falseGoalInduction](checkSignals=false; title="Taux induction faux objectif"; source=falseGoalInduction; record=vector,mean,sum; unit="ratio"; interpolationmode=none);
        @statistic[deceptionPersistence](checkSignals=false; title="Persistance deception"; source=deceptionPersistence; record=sum,last; unit="steps");
        @statistic[timeToSuspicion](checkSignals=false; title="Temps avant suspicion (s)"; source=timeToSuspicion; record=vector,mean,min; unit="s"; interpolationmode=none);

        // Statistiques Results B
        @statistic[consistencyViolation](checkSignals=false; title="Violation coherence Omega_t"; source=consistencyViolation; record=vector,count,mean; unit="dimensionless"; interpolationmode=none);
        @statistic[narrativeDivergence](checkSignals=false; title="Divergence narrative"; source=narrativeDivergence; record=vector,mean,max; unit="dimensionless"; interpolationmode=linear);
        @statistic[syncDelay](checkSignals=false; title="Delai synchronisation (ms)"; source=syncDelay; record=vector,mean,max,histogram; unit="ms"; interpolationmode=none);
        @statistic[quorumTriggerSuccess](checkSignals=false; title="Succes quorum narrative"; source=quorumTriggerSuccess; record=vector,mean,sum; unit="ratio"; interpolationmode=none);

        // Statistiques Results C
        @statistic[commOverhead](checkSignals=false; title="Surcharge reseau (bytes)"; source=commOverhead; record=vector,sum,mean; unit="bytes"; interpolationmode=linear);
        @statistic[infoLeak](checkSignals=false; title="Fuite informationnelle (nats)"; source=infoLeak; record=vector,mean,max; unit="nats"; interpolationmode=linear);
        @statistic[spoofDetection](checkSignals=false; title="Detection injection adverse"; source=spoofDetection; record=vector,mean; unit="ratio"; interpolationmode=none);

        // Statistiques Results D
        @statistic[performanceDrop](checkSignals=false; title="Chute performance (delta H)"; source=performanceDrop; record=vector,mean,stats; unit="bits"; interpolationmode=none);
        @statistic[gracefulDegradation](checkSignals=false; title="Degradation gracieuse"; source=gracefulDegradation; record=vector,mean; unit="bits/loss_unit"; interpolationmode=linear);

    gates:
        // Gate pour la reception de messages directs (sendDirect)
        input directIn @directIn;
}
