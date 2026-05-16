package stigmergie;

// =============================================================================
// StigmergyNetwork.ned — PATCHED
//
// SUPPRIMÉ : imports INET (AdhocHost, Ieee80211ScalarRadioMedium,
//            IntegratedCanvasVisualizer, PhysicalEnvironment, Ipv4NetworkConfigurator)
// RAISON   : DomainNode est un "simple" module pur OMNeT++ qui communique
//            via sendDirect() uniquement. Les gates INET (socketOut, wlan, etc.)
//            n'existent pas sur ce type de module → erreur à l'init.
//
// Architecture retenue :
//   - DomainNode    : simple module, sendDirect() vers directIn
//   - PheromoneField, AdversaryModel, MetricsCollector : simples modules globaux
//   - Communication : sendDirect(msg, dest, "directIn") dans DomainNode.cc
// =============================================================================

network StigmergyNetwork
{
    parameters:
        int    numDomains      = default(6);
        double packetLoss      = default(0.0);   // abstrait : lu par DomainNode
        double jammingProb     = default(0.0);   // transmis à PheromoneField
        double nodeFailureRate = default(0.0);
        double latencyMeanMs   = default(100);
        double latencyStdMs    = default(30);
        double syncIntervalSec = default(2.0);
        @display("bgb=1050,750");

    submodules:
        pheromoneField: PheromoneField {
            numDomains  = parent.numDomains;
            jammingProb = parent.jammingProb;
            @display("p=820,150;i=block/table2");
        }
        adversary: AdversaryModel {
            numDomains = parent.numDomains;
            @display("p=820,280;i=block/cogwheel");
        }
        metricsCollector: MetricsCollector {
            numDomains = parent.numDomains;
            @display("p=820,410;i=block/table");
        }
        domain[numDomains]: DomainNode {
            domainId        = index;
            numDomains      = parent.numDomains;
            nodeFailureRate = parent.nodeFailureRate;
            syncIntervalSec = parent.syncIntervalSec;
            @display("i=device/server2;is=l");
        }

    connections allowunconnected:
        // Pas de connexion câblée : communication via sendDirect()
}


// =============================================================================
// StigmergyNetworkWired — version avec canaux explicites (backup)
// Utilise ethg++ sur DomainNode → nécessite d'ajouter la gate ethg dans DomainNode.ned
// si vous souhaitez l'activer.
// =============================================================================
network StigmergyNetworkWired
{
    parameters:
        int    numDomains      = default(6);
        double packetLoss      = default(0.0);
        double nodeFailureRate = default(0.0);
        double syncIntervalSec = default(2.0);
        @display("bgb=1050,750");

    types:
        channel StigmergyChannel extends ned.DatarateChannel
        {
            datarate = default(100Mbps);
            delay    = default(10ms);
            per      = default(0.0);
        }

    submodules:
        pheromoneField: PheromoneField {
            numDomains = parent.numDomains;
            @display("p=820,100;i=block/table2");
        }
        adversary: AdversaryModel {
            numDomains = parent.numDomains;
            @display("p=820,220;i=block/cogwheel");
        }
        metricsCollector: MetricsCollector {
            numDomains = parent.numDomains;
            @display("p=820,340;i=block/table");
        }
        domain[numDomains]: DomainNode {
            domainId        = index;
            numDomains      = parent.numDomains;
            nodeFailureRate = parent.nodeFailureRate;
            syncIntervalSec = parent.syncIntervalSec;
            @display("i=device/server2;is=l");
        }

    connections allowunconnected:
        // Connexions wired désactivées tant que ethg n'est pas dans DomainNode.ned
}
