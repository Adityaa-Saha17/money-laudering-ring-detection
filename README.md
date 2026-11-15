# Graph-Theoretic Money-Laundering Ring Detection

A Money-Laundering Ring Detector in plain C++17 (no external libraries), built from standard textbook algorithms: DFS, SCCs, binary search, sliding window, dynamic programming, priority queues and max-flow. Developed for the Mathematical Foundations of Computer Science (MFCS) course project.

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
│   ├── ingest_scc.cpp        # Module 1: Graph, CSV loading, binary search, Kosaraju SCC
│   ├── cycles_fan.cpp        # Module 2: Temporal cycles (DFS + backtracking), fan-out/in (sliding window)
│   ├── chains_peeling.cpp    # Module 3: Pass-through chains (DP), k-core + greedy peeling (priority queue)
│   ├── exact_eval.cpp        # Module 4: Edmonds-Karp max-flow, exact densest subgraph, precision/recall
│   └── main.cpp              # Command-line program and settings
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
| **1** | **Adityaa Saha** | [`src/ingest_scc.cpp`](src/ingest_scc.cpp) | Graph with adjacency lists, CSV loader, synthetic data generator, binary search, Kosaraju's SCC |
| **2** | **Debtanu Das** | [`src/cycles_fan.cpp`](src/cycles_fan.cpp) | Temporal cycles by DFS with backtracking ($K=6$, $W=168\text{h}$), pruned with SCCs; two-pointer sliding window for fan-out/in |
| **3** | **Jeeban Jyoti Patra** | [`src/chains_peeling.cpp`](src/chains_peeling.cpp) | Pass-through chains as longest path in a DAG by DP ($L=3$, $D=24\text{h}$, $\varepsilon=0.10$), undirected graph, $k$-core and greedy densest subgraph with a priority queue |
| **4** | **Shatadeep De** | [`src/exact_eval.cpp`](src/exact_eval.cpp) | Edmonds-Karp max-flow, exact densest subgraph via min cuts, precision/recall/F1, `main.cpp` |

---

## ⚙️ Quick Start

### 1. Build
Compile the detector using `make` (or standard `clang++`):
```bash
make
# Alternatively:
# clang++ -std=c++17 -O2 src/main.cpp -o aml
```

### 2. Run Self-Tests
Run small hand-checked examples for every module:
```bash
make test
# Output: ALL TESTS PASSED
```

### 3. Generate Synthetic Benchmark
```bash
make generate
# Writes data/synth.csv (20,000 normal transfers + planted patterns)
```

### 4. Run Detection on IBM AML Dataset
```bash
make detect
# Or: ./aml detect data/HI-Small_Trans.csv
# Detector settings (K, W, fan k, chain L/D/eps) are constants at the top of src/main.cpp
```
*(Note: `loadCsv()` also looks inside `data/`, e.g. `./aml detect HI-Small_Trans.csv`)*

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
- **Ingestion & SCC:** Ingested in 3.60s; Kosaraju computed 494,769 SCCs in 0.17s.
- **Pattern Detector Recall:** Union of temporal cycles, fan-out/in, and pass-through chains flags **2,820 true positives (54.59% recall)** in **3.86 seconds**.
- **Max $k$-Core ($k=5$):** Isolates a 26-vertex component yielding **57.39% precision** (66 true positives among 115 transfers).
- **Exact Densest Subgraph:** Core pruning reduces the 515K-vertex graph to 862 vertices and 2,339 edges, solving the exact densest subgraph (density 3.8889 vs greedy 3.5741) in **3 min cuts (< 0.01s)**.
