package stigmergie;

simple MetricsCollector
{
    parameters:
        int    numDomains    = default(6);
        string outputDir     = default("results/processed");
        double collectPeriod = default(10.0);
        double hThresh       = default(1.0);
        double omegaEpsilon  = default(0.05);
        bool   writeCSV      = default(true);

        @display("i=block/table;p=820,580");

        @statistic[globalH](title="Global H(bt)"; source=globalH; record=vector,mean; unit="bits");
        @statistic[globalOmega](title="Global Omega_t"; source=globalOmega; record=vector,mean);
        @statistic[globalOverhead](title="Global comm overhead"; source=globalOverhead; record=vector,last; unit="bytes");
        @statistic[alertSuspicion](title="Suspicion alert time"; source=alertSuspicion; record=vector);
}
