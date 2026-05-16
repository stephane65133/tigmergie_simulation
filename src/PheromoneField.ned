package stigmergie;

simple PheromoneField
{
    parameters:
        int    numDomains      = default(6);
        double rhoP            = default(0.1);
        double omegaDefault    = default(0.8);
        double diffAlpha       = default(0.05);
        double depositClamp    = default(0.3);
        double noiseAmplitude  = default(0.05);
        double jammingProb     = default(0.0);
        bool   enableDiffusion = default(true);
        double evapPeriod      = default(2.0);

        @display("i=block/table2;p=820,420");

        @statistic[fieldEntropy](checkSignals=false; title="Field entropy H(tau)"; source=fieldEntropy; record=vector,mean; unit="bits");
        @statistic[evaporationLoss](checkSignals=false; title="Evaporation loss"; source=evaporationLoss; record=vector,sum);
        @statistic[diffusionGain](checkSignals=false; title="Diffusion gain"; source=diffusionGain; record=vector,mean);
        @statistic[adversarialNoise](checkSignals=false; title="Adversarial noise"; source=adversarialNoise; record=vector,sum);
        @statistic[globalConsistency](checkSignals=false; title="Global consistency"; source=globalConsistency; record=vector,mean; unit="ratio");
        @statistic[fieldNorm](checkSignals=false; title="Field norm ||tau||_2"; source=fieldNorm; record=vector,mean);
}
