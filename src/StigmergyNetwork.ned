package stigmergie;

import inet.environment.common.PhysicalEnvironment;
import inet.networklayer.configurator.ipv4.Ipv4NetworkConfigurator;
import inet.physicallayer.wireless.ieee80211.packetlevel.Ieee80211ScalarRadioMedium;
import inet.visualizer.canvas.integrated.IntegratedCanvasVisualizer;

network StigmergyNetwork
{
    parameters:
        int    numDomains      = default(6);
        double packetLoss      = default(0.0);
        double jammingProb     = default(0.0);
        double nodeFailureRate = default(0.0);
        double latencyMeanMs   = default(100);
        double latencyStdMs    = default(30);
        double syncIntervalSec = default(2.0);
        @display("bgb=1050,750");

    submodules:
        radioMedium: Ieee80211ScalarRadioMedium {
            @display("p=820,60;i=misc/sun");
        }
        configurator: Ipv4NetworkConfigurator {
            addStaticRoutes  = true;
            addDefaultRoutes = false;
            addSubnetRoutes  = false;
            @display("p=820,160;i=block/cogwheel");
        }
        visualizer: IntegratedCanvasVisualizer {
            @display("p=820,260;i=block/app");
        }
        physicalEnvironment: PhysicalEnvironment {
            @display("p=820,340;i=misc/building");
        }
        pheromoneField: PheromoneField {
            numDomains  = parent.numDomains;
            jammingProb = parent.jammingProb;
            @display("p=820,420;i=block/table2");
        }
        adversary: AdversaryModel {
            numDomains = parent.numDomains;
            @display("p=820,500;i=block/cogwheel");
        }
        metricsCollector: MetricsCollector {
            numDomains = parent.numDomains;
            @display("p=820,580;i=block/table");
        }
        domain[numDomains]: DomainNode {
            domainId        = index;
            numDomains      = parent.numDomains;
            nodeFailureRate = parent.nodeFailureRate;
            syncIntervalSec = parent.syncIntervalSec;
            @display("i=device/server2;is=l");
        }

    connections allowunconnected:
}


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
        configurator: Ipv4NetworkConfigurator {
            @display("p=820,60;i=block/cogwheel");
        }
        pheromoneField: PheromoneField {
            numDomains = parent.numDomains;
            @display("p=820,140;i=block/table2");
        }
        adversary: AdversaryModel {
            numDomains = parent.numDomains;
            @display("p=820,220;i=block/cogwheel");
        }
        metricsCollector: MetricsCollector {
            numDomains = parent.numDomains;
            @display("p=820,300;i=block/table");
        }
        domain[numDomains]: DomainNode {
            domainId        = index;
            numDomains      = parent.numDomains;
            nodeFailureRate = parent.nodeFailureRate;
            syncIntervalSec = parent.syncIntervalSec;
            @display("i=device/server2;is=l");
        }

    connections:
        for i=1..numDomains-1 {
            domain[0].ethg++ <--> StigmergyChannel { per = parent.packetLoss; }
                             <--> domain[i].ethg++;
        }
        domain[1].ethg++ <--> StigmergyChannel { per = packetLoss; }
                         <--> domain[2].ethg++ if numDomains > 2;
        domain[2].ethg++ <--> StigmergyChannel { per = packetLoss; }
                         <--> domain[3].ethg++ if numDomains > 3;
        domain[3].ethg++ <--> StigmergyChannel { per = packetLoss; }
                         <--> domain[4].ethg++ if numDomains > 4;
        domain[4].ethg++ <--> StigmergyChannel { per = packetLoss; }
                         <--> domain[5].ethg++ if numDomains > 5;
        domain[5].ethg++ <--> StigmergyChannel { per = packetLoss; }
                         <--> domain[1].ethg++ if numDomains > 5;
}
