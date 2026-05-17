# Stigmergie Multi-Domain Simulation

Simulation framework for distributed stigmergic coordination in multi-domain deception environments using OMNeT++ and INET.

This project implements:

* Distributed stigmergic coordination
* Pheromone-based synchronization
* Adversarial adaptation modeling
* Multi-domain deception strategies
* Robustness and scalability experiments
* IEEE-style result generation

---

# Project Structure

```text
stigmergie_fixed/
├── src/                  # C++ simulation modules
├── simulations/          # OMNeT++ .ini configurations
├── results/
│   ├── raw/              # Raw .vec and .sca outputs
│   ├── processed/        # CSV exports
│   └── figures/          # Generated figures
├── out/                  # Compiled binaries (ignored by git)
├── run.sh                # Helper execution script
├── Makefile
└── omnetpp.ini
```

---

# 1. Installing OMNeT++

## 1.1 Install Dependencies (Ubuntu / Debian / Kali)

```bash
sudo apt update

sudo apt install -y \
build-essential \
clang \
gcc \
g++ \
bison \
flex \
perl \
python3 \
python3-pip \
qtbase5-dev \
qtchooser \
qt5-qmake \
qtbase5-dev-tools \
libqt5opengl5-dev \
libxml2-dev \
zlib1g-dev \
default-jre \
doxygen \
graphviz \
libwebkit2gtk-4.0-dev \
openscenegraph-plugin-osgearth \
libopenscenegraph-dev
```

---

## 1.2 Download OMNeT++

Download OMNeT++ 6.x from:

```text
https://omnetpp.org/download/
```

Extract it:

```bash
tar xvf omnetpp-6.4.0-linux-x86_64.tgz
```

---

## 1.3 Build OMNeT++

```bash
cd omnetpp-6.4.0

source setenv

./configure
make -j$(nproc)
```

Verify installation:

```bash
omnetpp
```

---

# 2. Installing INET Framework

Clone INET:

```bash
cd ~/Desktop

git clone https://github.com/inet-framework/inet.git
```

Build INET:

```bash
cd inet

source /path/to/omnetpp/setenv

make makefiles
make -j$(nproc)
```

---

# 3. Preparing the Project

Move into the project directory:

```bash
cd stigmergie_fixed
```

Load OMNeT++ environment:

```bash
source /path/to/omnetpp/setenv
```

Set INET path:

```bash
export INET_ROOT=/path/to/inet4
```

Create result directories:

```bash
mkdir -p results/raw results/processed results/figures
```

Explanation:

* `results/raw` stores OMNeT++ `.vec` and `.sca` files
* `results/processed` stores exported CSV data
* `results/figures` stores generated IEEE figures

---

# 4. Building the Simulation

Generate the OMNeT++ Makefile:

```bash
opp_makemake -f --deep -o stigmergie \
    -KINET_PROJ=$INET_ROOT \
    -DINET_IMPORT \
    -I$INET_ROOT/src \
    -L$INET_ROOT/src -lINET
```

Explanation:

* `-f` forces Makefile generation
* `--deep` recursively scans source directories
* `-o stigmergie` sets the executable name
* `-KINET_PROJ` links the INET framework
* `-DINET_IMPORT` enables INET integration
* `-I` adds INET headers
* `-L` adds INET libraries
* `-lINET` links against INET

Compile the project:

```bash
make -j$(nproc)
```

Explanation:

* `-j$(nproc)` compiles using all CPU cores

---

# 5. Quick Debug Execution

Run a short validation simulation:

```bash
make -j$(nproc) && bash run.sh Debug_Quick
```

Explanation:

* recompiles the project
* launches the `Debug_Quick` OMNeT++ configuration
* useful for validating topology and module behavior

---

# 6. Running Experiments

## E1 — Synchronization Without Losses

Estimated duration: ~2h

```bash
./out/clang-release/stigmergie \
    -n src:simulations \
    -c E1_Proposed -r 0..29 \
    simulations/omnetpp.ini
```

Purpose:

* evaluates distributed synchronization
* validates stigmergic convergence
* compares coordination quality

---

## E2 — Adversarial Shift Adaptation

Estimated duration: ~3h

```bash
./out/clang-release/stigmergie \
    -n src:simulations \
    -c E2_Proposed -r 0..29 \
    simulations/omnetpp.ini
```

Purpose:

* evaluates adaptation to adversarial strategy changes
* measures deception persistence
* measures recovery speed after attention shift

---

## E3 — Degraded Network Robustness

Estimated duration: ~4h

```bash
./out/clang-release/stigmergie \
    -n src:simulations \
    -c E3_Proposed -r 0..29 \
    simulations/omnetpp.ini
```

Purpose:

* evaluates robustness under packet loss
* evaluates robustness under jamming
* analyzes consistency degradation

---

## E4 — Node Failure Resilience

Estimated duration: ~3h

```bash
./out/clang-release/stigmergie \
    -n src:simulations \
    -c E4_Proposed -r 0..29 \
    simulations/omnetpp.ini
```

Purpose:

* evaluates graceful degradation
* evaluates resilience to node failures
* measures post-failure coordination quality

---

## E5 — Scalability Analysis

Estimated duration: ~4h

```bash
./out/clang-release/stigmergie \
    -n src:simulations \
    -c E5_Proposed -r 0..29 \
    simulations/omnetpp.ini
```

Purpose:

* evaluates scalability with increasing domain count
* measures communication overhead
* validates near-linear scaling behavior

---

# 7. Monitoring Running Simulations

## Real-Time Log Monitoring

Create a logs directory:

```bash
mkdir -p logs
```

Watch experiment progress:

```bash
tail -f logs/E1_Proposed.log
```

Purpose:

* displays live simulation output
* helps detect runtime errors
* useful for long batch executions

---

## View Running Processes

```bash
ps aux | grep stigmergie
```

Purpose:

* lists running simulation processes
* verifies active OMNeT++ executions

---

## Monitor Generated Result Files

```bash
watch -n 5 'ls -lh results/raw/ | tail -20'
```

Purpose:

* refreshes every 5 seconds
* monitors generated `.vec` and `.sca` files
* useful for verifying experiment progression

---

# 8. Exporting Results

Move to the project directory:

```bash
cd /home/lucky/Desktop/omnetpp-6.4.0/samples/stigmergie_fixed
```

Export CSV datasets:

```bash
make export-csv
```

Purpose:

* converts `.vec` and `.sca` outputs into CSV files
* prepares data for analysis
* generates files in `results/processed`

---

# 9. Generating Figures

Generate IEEE-ready figures:

```bash
make figures
```

Purpose:

* generates publication-quality plots
* stores figures in `results/figures`
* prepares graphics for papers and presentations

---

# 10. Useful Git Configuration

Ignore build artifacts:

```bash
echo "/out/" >> .gitignore
```

Recommended `.gitignore`:

```text
/out/
/results/
*.vec
*.sca
*.vci
*.elog
```

---

# 11. Common Issues and Fixes

## OMNeT++ Environment Not Loaded

Error:

```text
opp_makemake: command not found
```

Fix:

```bash
source /path/to/omnetpp/setenv
```

---

## Missing INET Path

Error:

```text
INET library not found
```

Fix:

```bash
export INET_ROOT=/path/to/inet
```

---

## Invalid OMNeT++ Configuration Option

Error:

```text
Unknown per-object configuration option
```

Fix:

* remove obsolete OMNeT++ 5.x options
* update configuration syntax for OMNeT++ 6.x

---

## Unit Conversion Errors

Error:

```text
Cannot convert unit 's' to none
```

Fix:

Declare parameters using units in `.ned` files:

```ned
double evapPeriod @unit(s);
```

---

# 12. Generated Outputs

The simulation produces:

* `.vec` files for vector metrics
* `.sca` files for scalar metrics
* CSV exports for analysis
* IEEE publication figures

Main metrics include:

* belief entropy
* pheromone field consistency
* deception persistence
* synchronization delay
* communication overhead
* adversarial robustness
* graceful degradation

---

# 13. Recommended Workflow

1. Load OMNeT++ environment
2. Set `INET_ROOT`
3. Generate Makefile
4. Compile project
5. Run `Debug_Quick`
6. Execute E1–E5 experiments
7. Export CSV data
8. Generate IEEE figures
9. Analyze results

---

# 14. License

Academic and research use only.

Built using:

* OMNeT++ 6.x
* INET Framework
* C++17

---

# 15. Authors

Université de Dschang

Distributed Stigmergic Coordination Research Project
