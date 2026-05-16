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

        @statistic[fieldEntropy](title="Field entropy H(tau)"; source=fieldEntropy; record=vector,mean; unit="bits");
        @statistic[evaporationLoss](title="Evaporation loss"; source=evaporationLoss; record=vector,sum);
        @statistic[diffusionGain](title="Diffusion gain"; source=diffusionGain; record=vector,mean);
        @statistic[adversarialNoise](title="Adversarial noise"; source=adversarialNoise; record=vector,sum);
        @statistic[globalConsistency](title="Global consistency"; source=globalConsistency; record=vector,mean; unit="ratio");
        @statistic[fieldNorm](title="Field norm ||tau||_2"; source=fieldNorm; record=vector,mean);
}
