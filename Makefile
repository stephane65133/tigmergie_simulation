# =============================================================================
# Makefile — Stigmergie (version sans opp_configfilepath)
# Remplace le Makefile existant dans stigmergie_fixed/
# =============================================================================

OMNET_ROOT  ?= $(HOME)/Desktop/omnetpp-6.4.0
INET_ROOT   ?= $(HOME)/Desktop/inet
PROJECT_DIR := $(shell pwd)
RESULTS_DIR := $(PROJECT_DIR)/results
OUT_DIR     := $(PROJECT_DIR)/out/clang-release/src
NED_PATH    := $(PROJECT_DIR)/src:$(PROJECT_DIR)/simulations:$(INET_ROOT)/src
PYTHON      ?= python3

# Tous les targets sourcent setenv via le script run.sh / build_and_run.sh
# car make ne peut pas sourcer un script shell directement

.PHONY: all build run-debug run-E1 run-E1-all run-E2 run-E3 run-E4 run-E5 \
        run-all export-csv figures clean help

# =============================================================================
# Build
# =============================================================================
all: build

build:
	bash build_and_run.sh --build-only 2>/dev/null || \
	bash -c "source $(OMNET_ROOT)/setenv && \
	    mkdir -p $(OUT_DIR) && \
	    for m in AdversaryModel DomainNode MetricsCollector PheromoneField; do \
	        g++ -std=c++17 -fPIC -O2 -DNDEBUG -DINET_IMPORT \
	            -I$(INET_ROOT)/src -I$(OMNET_ROOT)/include \
	            -c src/$$m.cc -o $(OUT_DIR)/$$m.o; \
	    done && \
	    g++ -shared -fPIC -o $(OUT_DIR)/libstigmergie.so \
	        $(OUT_DIR)/AdversaryModel.o $(OUT_DIR)/DomainNode.o \
	        $(OUT_DIR)/MetricsCollector.o $(OUT_DIR)/PheromoneField.o \
	        -L$(INET_ROOT)/src -lINET && \
	    echo 'Build OK'"

# =============================================================================
# Simulations
# =============================================================================
run-debug:
	bash run.sh Debug_Quick

run-E1:
	bash run.sh E1_Proposed

run-E1-all:
	@for cfg in E1_CentralizedPPO E1_StaticAllocation \
	            E1_Independent E1_GNNCoordinated E1_Proposed; do \
	    bash run.sh $$cfg & \
	done; wait
	@echo "E1 terminé"

run-E2:
	bash run.sh E2_Proposed

run-E3:
	bash run.sh E3_Proposed

run-E4:
	bash run.sh E4_Proposed

run-E5:
	bash run.sh E5_Proposed

run-all: run-E1-all run-E2 run-E3 run-E4 run-E5

# =============================================================================
# Export et figures — utilise le script autonome (source setenv inclus)
# =============================================================================
export-csv:
	bash export_results.sh

figures:
	bash export_results.sh

# =============================================================================
# Nettoyage
# =============================================================================
clean:
	rm -rf out/ results/raw/*.vec results/raw/*.sca

help:
	@echo ""
	@echo "Usage:"
	@echo "  make build         Compiler le projet"
	@echo "  make run-debug     Test rapide (1 run, 20s)"
	@echo "  make run-E1        Expérience E1 (30 runs)"
	@echo "  make run-E1-all    E1 toutes méthodes"
	@echo "  make run-E2/E3/E4/E5"
	@echo "  make run-all       Toutes les expériences"
	@echo "  make export-csv    Export CSV + figures"
	@echo ""
