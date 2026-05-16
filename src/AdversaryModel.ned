package stigmergie;

simple AdversaryModel
{
    parameters:
        int    numDomains         = default(6);
        string beliefInit         = default("uniform");
        int    gnnHiddenDim       = default(16);
        double attentionLr        = default(0.01);
        double suspicionThreshold = default(0.3);
        string probingIntensity   = default("adaptive");
        double updatePeriod       = default(2.0);

        @display("i=block/cogwheel;p=820,500");

        @statistic[suspicionLevel](checkSignals=false; title="Suspicion level"; source=suspicionLevel; record=vector,mean);
        @statistic[beliefMax](checkSignals=false; title="Belief max_g b_t(g)"; source=beliefMax; record=vector,mean,max);
        @statistic[entropyBelief](checkSignals=false; title="H(b_t) adversary view"; source=entropyBelief; record=vector,mean; unit="bits");
        @statistic[attentionEntropy](checkSignals=false; title="Attention weight entropy"; source=attentionEntropy; record=vector,mean);
        @statistic[probingTarget](checkSignals=false; title="Probing target domain"; source=probingTarget; record=vector);
        @statistic[strategyShift](checkSignals=false; title="Strategy shift event"; source=strategyShift; record=vector,sum);
}
