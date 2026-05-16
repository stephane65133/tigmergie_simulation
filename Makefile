# =============================================================================
#  Makefile — Stigmergie Multi-Domaine
#  Utilise le compilateur OMNeT++ natif via opp_makemake
# =============================================================================

INET_ROOT   ?= $(HOME)/Desktop/inet
OMNET_ROOT  ?= $(HOME)/Desktop/omnetpp-6.4.0
PROJECT     := stigmergie
OUT_DIR     := out/clang-release/src
NED_PATH    := src:simulations:$(INET_ROOT)/src
SIM_DIR     := simulations
RESULTS_DIR := results

# Objets compilés par le Makefile précédent
OBJS := $(OUT_DIR)/AdversaryModel.o \
        $(OUT_DIR)/DomainNode.o \
        $(OUT_DIR)/MetricsCollector.o \
        $(OUT_DIR)/PheromoneField.o

SO_FILE := $(OUT_DIR)/libstigmergie.so

# =============================================================================
# Cible principale : créer la .so depuis les .o existants
# =============================================================================
all: $(SO_FILE)

$(SO_FILE): $(OBJS)
	@echo "==> Linking libstigmergie.so..."
	g++ -shared -fPIC -o $@ $(OBJS) \
	    -L$(INET_ROOT)/src -lINET \
	    -L$(OMNET_ROOT)/lib -loppmain \
	    -Wl,-rpath,$(INET_ROOT)/src \
	    -Wl,-rpath,$(OMNET_ROOT)/lib
	@echo "==> OK : $@"

# =============================================================================
# Recompilation complète (si les .o sont absents ou modifiés)
# =============================================================================
compile:
	@echo "==> Compilation des sources..."
	@mkdir -p $(OUT_DIR)
	$(foreach src, \
	    src/AdversaryModel.cc src/DomainNode.cc \
	    src/MetricsCollector.cc src/PheromoneField.cc, \
	    $(CXX) -std=c++17 -fPIC -O2 \
	        -I$(INET_ROOT)/src \
	        -I$(OMNET_ROOT)/include \
	        -DINET_IMPORT \
	        -c $(src) -o $(OUT_DIR)/$(notdir $(src:.cc=.o));)
	$(MAKE) $(SO_FILE)

# =============================================================================
# Nettoyage
# =============================================================================
clean:
	rm -rf out/ $(SO_FILE)

# =============================================================================
# Lancement des simulations
# =============================================================================
dirs:
	mkdir -p $(RESULTS_DIR)/raw $(RESULTS_DIR)/processed $(RESULTS_DIR)/figures

define run_sim
	@mkdir -p $(RESULTS_DIR)/raw
	opp_run -n $(NED_PATH) \
	        -l $(INET_ROOT)/src/INET \
	        -l $(OUT_DIR)/stigmergie \
	        -c $(1) -r $(2) \
	        $(SIM_DIR)/omnetpp.ini
endef

run-debug: all dirs
	$(call run_sim,Debug_Quick,0)

run-E1: all dirs
	$(call run_sim,E1_Proposed,0..29)

run-E1-all: all dirs
	@for cfg in E1_CentralizedPPO E1_StaticAllocation \
	            E1_Independent E1_GNNCoordinated E1_Proposed; do \
	    opp_run -n $(NED_PATH) -l $(INET_ROOT)/src/INET \
	        -l $(OUT_DIR)/stigmergie \
	        -c $$cfg -r 0..29 $(SIM_DIR)/omnetpp.ini & \
	done; wait

run-E2: all dirs
	$(call run_sim,E2_Proposed,0..29)

run-E3: all dirs
	$(call run_sim,E3_Proposed,0..29)

run-E4: all dirs
	$(call run_sim,E4_Proposed,0..29)

run-E5: all dirs
	$(call run_sim,E5_Proposed,0..29)

run-all: run-E1-all run-E2 run-E3 run-E4 run-E5

export-csv: dirs
	@for sig in beliefEntropy deceptionEffort pheromoneLevel \
	            consistencyViolation syncDelay commOverhead \
	            falseGoalInduction performanceDrop; do \
	    for cfg in E1_Proposed E2_Proposed E3_Proposed E4_Proposed E5_Proposed; do \
	        out=$(RESULTS_DIR)/processed/$${cfg}_$${sig}.csv; \
	        vecs="$(RESULTS_DIR)/raw/vectors-$${cfg}-*.vec"; \
	        ls $$vecs 2>/dev/null | grep -q . && \
	        opp_scavetool export -f "name =~ $$sig" \
	            -F CSV-R $$vecs -o $$out 2>/dev/null || true; \
	    done; \
	done
	opp_scavetool export -F CSV-S \
	    $(RESULTS_DIR)/raw/scalars-*.sca \
	    -o $(RESULTS_DIR)/processed/all_scalars.csv 2>/dev/null || true

figures: export-csv
	python3 scripts/plot_results.py --all \
	    --input  $(RESULTS_DIR)/processed \
	    --output $(RESULTS_DIR)/figures

.PHONY: all compile clean dirs run-debug run-E1 run-E1-all \
        run-E2 run-E3 run-E4 run-E5 run-all export-csv figures
