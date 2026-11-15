# Graph-Theoretic Money-Laundering Ring Detection

A deterministic, high-throughput Money-Laundering Ring Detector in standard C++17 (zero external dependencies). Developed for the Mathematical Foundations of Computer Science (MFCS) course project.

The system detects five classic money laundering topologies in financial transaction graphs without machine learning:
1. **Temporal Cycles** (Round-tripping funds within time windows)
2. **Fan-Out** (Layering / smurfing — one-to-many dispersion)
3. **Fan-In** (Aggregation — many-to-one pooling)
4. **Pass-Through Chains** (Layering walks with value conservation and bounded delays)
5. **Dense Subgraphs & $k$-Cores** (Bipartite rings, circular smurfing rings, and collusive fraud networks)

---

## 📁 Repository Structure

```
money-laundering-ring-detection/
├── src/                      # C++17 source code modules
│   ├── ingest_scc.cpp        # Module 1: Ingestion, time-indexed graph, Tarjan SCC
│   ├── cycles_fan.cpp        # Module 2: Bounded temporal cycles, sliding-window fan-out/in
│   ├── chains_peeling.cpp    # Module 3: Pass-through chains, k-core, greedy peeling
│   └── exact_eval.cpp        # Module 4: Dinic max-flow, exact densest min-cut, CLI & harness
├── data/                     # Transaction datasets
│   ├── HI-Small_Trans.csv    # Real IBM Synthetic AML dataset (5.08M transfers, ~454MB, gitignored)
│   └── synth.csv             # Generated synthetic benchmark (20K transfers with planted rings)
├── docs/                     # Documentation, guides, and benchmark results
│   ├── CODEBASE_ANALYSIS.md  # Comprehensive architecture & algorithm reference
│   ├── results.md            # Benchmark logs on IBM HI-Small_Trans.csv
│   ├── ingest_scc.md         # Learning guide for Module 1 (Adityaa Saha)
│   ├── cycles_fan.md         # Learning guide for Module 2 (Debtanu Das)
│   ├── chains_peeling.md     # Learning guide for Module 3 (Jeeban Jyoti Patra)
│   └── exact_eval.md         # Learning guide for Module 4 (Shatadeep De)
├── report/                   # Academic report
│   ├── report.tex            # Full 16-page LaTeX project report
│   └── report.pdf            # Compiled PDF report
├── presentation/             # Presentation slides
│   ├── presentation.tex      # 21-slide Beamer presentation with TikZ diagrams
│   └── presentation.pdf      # Compiled presentation slides
├── Makefile                  # Build and task automation
├── .gitignore                # Git ignore configuration
└── README.md                 # Project overview and navigation
```

---

## 👥 Team Members & Module Ownership

| Member | Name | Module | Key Algorithms & Contributions |
| :---: | :--- | :--- | :--- |
| **1** | **Adityaa Saha** | [`src/ingest_scc.cpp`](src/ingest_scc.cpp) | Multigraph data model, CSV loader with `data/` fallback, Gregorian epoch parser, synthetic generator, Tarjan's SCC |
| **2** | **Debtanu Das** | [`src/cycles_fan.cpp`](src/cycles_fan.cpp) | Bounded temporal cycle search ($K=6$, $W=168\text{h}$) with SCC pruning & binary search, two-pointer sliding window fan-out/in |
| **3** | **Jeeban Jyoti Patra** | [`src/chains_peeling.cpp`](src/chains_peeling.cpp) | Pass-through layering chains DP ($L=3$, $D=24\text{h}$, $\varepsilon=0.10$), undirected projection $H$, $k$-core decomposition, Charikar's greedy peeling |
| **4** | **Shatadeep De** | [`src/exact_eval.cpp`](src/exact_eval.cpp) | Dinic's blocking max-flow, exact densest subgraph via Goldberg min-cut reduction & Dinkelbach iteration with core pruning, evaluation harness, CLI `main()` |

---

## ⚙️ Quick Start

### 1. Build
Compile the detector using `make` (or standard `clang++`):
```bash
make
# Alternatively:
# clang++ -std=c++17 -O2 -Isrc src/exact_eval.cpp -o aml
```

### 2. Run Self-Tests
Verify correctness against independent brute-force oracles across 14,400 randomized graphs:
```bash
make test
# Output: ALL TESTS PASSED: 72 checks, 0 failed
```

### 3. Generate Synthetic Benchmark
```bash
make generate
# Writes data/synth.csv (20,000 background transfers + planted patterns)
```

### 4. Run Detection on IBM AML Dataset
```bash
make detect
# Or specify parameters:
# ./aml detect data/HI-Small_Trans.csv [K W maxCycles fanMin fanW chainMin chainD chainEps]
```
*(Note: `loadCsv()` automatically looks inside `data/` if a bare filename is passed, e.g. `./aml detect HI-Small_Trans.csv`)*

### 5. Run Full Evaluation & Benchmark
```bash
make evaluate
# Computes Precision, Recall, F1, and prefix scaling across 25%, 50%, 75%, 100% of data
```

### 6. Compile Report & Presentation
```bash
make report         # Generates report/report.pdf via Tectonic
make presentation   # Generates presentation/presentation.pdf via Tectonic
```

---

## 📊 Summary of Results on IBM `HI-Small_Trans.csv`

- **Scale:** 515,088 accounts, 5,078,345 transfers, 5,177 labelled laundering transfers (0.115% base rate)
- **Ingestion & SCC:** Ingested in 3.60s; Tarjan computed 494,769 SCCs in 0.116s.
- **Pattern Detector Recall:** Union of temporal cycles, fan-out/in, and pass-through chains flags **2,820 true positives (54.59% recall)** in **3.86 seconds**.
- **Max $k$-Core ($k=5$):** Isolates a 26-vertex component yielding **57.39% precision** (66 true positives among 115 transfers).
- **Exact Densest Subgraph:** Core pruning reduces the 515K-vertex graph to 862 vertices and 2,339 edges, solving the exact densest subgraph (density 3.8889 vs greedy 3.5741) in **3 min cuts (< 0.01s)**.
