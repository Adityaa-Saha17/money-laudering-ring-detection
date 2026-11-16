# Speaker's Notes: Money-Laundering Ring Detection with Graph Algorithms

**Context:** MFCS Group Presentation (IIT Bhubaneswar)  
**Total Target Duration:** 5 – 6 Minutes (300 – 360 seconds)  
**Presenters & Order:**
1. **Adityaa Saha (Speaker 1)**: Slides 1 – 7 (~85–90s / 1.5 mins) — *Introduction, Graph Model & Pipeline, Ingestion, Kosaraju SCC & Proof, Verification*
2. **Debtanu Das (Speaker 2)**: Slides 8 – 11 (~75–80s / 1.25 mins) — *Temporal Cycles, Pruning Proof, Smurfing (Fan-in/out), Verification*
3. **Jeeban Jyoti Patra (Speaker 3)**: Slides 12 – 15 (~75–80s / 1.25 mins) — *Pass-Through Layering Chains, Greedy $k$-Core Peeling, $\frac{1}{2}$-Approximation Proof, Verification*
4. **Shatadeep De (Speaker 4)**: Slides 16 – 21 (~95–100s / 1.6 mins) — *Exact Densest via Min-Cut, Dinkelbach Iteration, Verification, Real-World Benchmark on 5M Transfers, Conclusion & Q&A*

---

## Speaker 1: Adityaa Saha (Slides 1 – 7)
**Allocated Time:** ~1 min 30 sec (0:00 – 1:30)

---

### Slide 1: Title Slide (0:00 – 0:15)
- **Spoken Text:**  
  "Good morning everyone. I am Adityaa Saha, and together with my teammates Debtanu Das, Jeeban Jyoti Patra, and Shatadeep De, we are presenting our project: **Money-Laundering Ring Detection with Graph Algorithms**."

---

### Slide 2: The Problem (0:15 – 0:35)
- **Spoken Text:**  
  "Our input is a massive log of bank transfers: timestamps, senders, receivers, and amounts. Our goal is to detect illicit laundering topologies.  
  Crucially, our approach relies **strictly on graph theory**—no opaque machine learning or unverified heuristics. Every single detector in our pipeline comes with a formal definition, a mathematical proof of correctness, and a strict complexity bound."

---

### Slide 3: Graph Model and Detection Pipeline (0:35 – 0:50)
- **Spoken Text:**  
  "We model transactions as a directed temporal multigraph $G = (V, E)$, where accounts are vertices and timed transfers are directed edges. Dense detectors project this onto a simple undirected graph $H$ with density $\rho(S) = |E(S)| / |S|$.  
  Our detection pipeline processes transactions across four cohesive stages:
  - **Stage 1 (Ingestion & SCC):** Preprocesses transfers into time-ordered adjacency lists and extracts strongly connected components to establish a pruning backbone.
  - **Stage 2 (Temporal Patterns):** Flags closed-loop temporal cycles and smurfing hubs using windowed searches.
  - **Stage 3 (Layering & Peeling):** Traces rapid balance forwarding through pass-through chains and isolates dense clusters using greedy core peeling.
  - **Stage 4 (Exact Optimization & Evaluation):** Solves exact densest subgraphs via minimum cut and scores the full pipeline against ground-truth labels.  
  The entire engine compiles into a single, high-performance C++17 binary."

---

### Slide 4: Ingestion and the Graph (0:50 – 1:05)
- **Spoken Text:**  
  "For ingestion, we parse all eleven columns of the IBM AML dataset using a custom, zero-dependency epoch converter. Accounts are hashed to dense integer IDs $0$ to $n-1$, preserving parallel edges and self-loops.  
  Our `finalize()` step sorts edges chronologically and populates adjacency lists in $O(m \log m)$. This establishes a core invariant: every adjacency list is strictly time-ordered, enabling binary-search range queries across temporal windows for all downstream pattern detectors."

---

### Slide 5: Kosaraju's SCC Algorithm (1:05 – 1:18)
- **Spoken Text:**  
  "Next, we compute Strongly Connected Components using Kosaraju's algorithm in $O(n + m)$ time.  
  To guarantee that production-scale graphs never cause a call-stack overflow, we implemented our DFS iteratively using an explicit stack—safely handling paths exceeding 200,000 vertices.  
  Why are SCCs vital? Any cycle must reside entirely inside a single SCC, allowing downstream cycle detection to prune any search branch leaving the component immediately."

---

### Slide 6: Kosaraju 2-Pass SCC Proof (1:18 – 1:28)
- **Spoken Text:**  
  "The correctness hinges on the theorem that a vertex $v$ satisfies $\text{low}[v] = \text{disc}[v]$ if and only if $v$ is the root of its SCC. By showing that non-roots always reach an active ancestor on the stack, we prove the vertices popped above $v$ form exactly that SCC without losing or conflating components."

---

### Slide 7: Ingestion and SCC: Verification (1:28 – 1:35)
- **Spoken Text:**  
  "All ingestion and SCC logic was validated against corner-case CSVs and 1,000 random graphs compared against transitive closure reachability, with zero failures.  
  Now, I'll hand over to **Debtanu** to present our temporal cycle and smurfing detectors."

---

## Speaker 2: Debtanu Das (Slides 8 – 11)
**Allocated Time:** ~1 min 20 sec (1:30 – 2:50)

---

### Slide 8: Temporal Cycles: Definition (1:30 – 1:50)
- **Spoken Text:**  
  "Thank you, Adityaa. A temporal laundering cycle is a closed, head-to-tail path of distinct vertices of length $2$ to $K$, occurring in non-decreasing time order within a time window $W$.  
  To avoid reporting rotations of the same cycle, we enforce that edge $e_1$ must have the minimum edge index.  
  Our search explores outward from each edge $r$ up to depth $K$. We prune immediately whenever an edge exceeds $t(r) + W$, has an index smaller than $r$, or crosses outside the starting SCC."

---

### Slide 9: Proof: The Bounded Search is Exact (1:50 – 2:10)
- **Spoken Text:**  
  "Since a temporal cycle forms a closed walk, all of its vertices must lie in the same SCC—so our SCC pruning causes zero false negatives.  
  Furthermore, our inductive invariant ensures each temporal cycle is discovered and emitted **exactly once** from its lowest-indexed edge. The worst-case search cost is $O(m \cdot d^{K-1})$, where $d$ is the windowed out-degree, making cycle detection fully exact and bounded."

---

### Slide 10: Fan-Out / Fan-In (Smurfing) (2:10 – 2:30)
- **Spoken Text:**  
  "Next is smurfing—where a central account disperses funds to, or aggregates funds from, at least $k$ distinct counterparties within window $W$.  
  Because our adjacency lists are time-sorted, we evaluate this using a two-pointer sliding window with an active frequency map. Each pointer moves at most $\deg(u)$ steps, giving an exact detection algorithm running in optimal $O(n + m)$ linear time."

---

### Slide 11: Cycles and Smurfing: Verification (2:30 – 2:45)
- **Spoken Text:**  
  "We thoroughly verified these detectors: window edge-cases at $19$ versus $20$ seconds, 400 random graphs against exhaustive cycle enumeration (recovering all 345 cycles with zero duplicates), and 2,000 random graphs against brute-force smurfing.  
  Next, **Jeeban** will cover pass-through layering chains and core peeling."

---

## Speaker 3: Jeeban Jyoti Patra (Slides 12 – 15)
**Allocated Time:** ~1 min 20 sec (2:45 – 4:05)

---

### Slide 12: Pass-Through Chains (Layering) (2:45 – 3:10)
- **Spoken Text:**  
  "Thank you, Debtanu. In layering chains, dirty money rapidly hops through intermediate shell accounts while keeping nearly the entire balance intact.  
  We define a valid hop if edge $f$ follows edge $e$ within $D$ seconds and retains at least $(1 - \varepsilon)$ of the amount.  
  Because valid hops must advance forward in time, the 'follows' relation defines a DAG. We use dynamic programming to compute the longest backward and forward chains for each transfer, tagging any edge where $\text{back}[e] + \text{fwd}[e] - 1 \ge L$ in exact polynomial time."

---

### Slide 13: $k$-Core and Charikar's Greedy Peeling (3:10 – 3:30)
- **Spoken Text:**  
  "To catch collusive laundering rings, we project to an undirected simple graph $H$ and search for the densest subgraph maximizing $\rho(S) = |E(S)| / |S|$.  
  We implement Charikar's greedy peeling: iteratively removing the vertex of minimum degree using a priority queue. A single pass identifies dense candidates across all core levels in $O(m \log n)$ time."

---

### Slide 14: Proof: Greedy is a 1/2-Approximation (3:30 – 3:50)
- **Spoken Text:**  
  "We formally proved that greedy peeling yields a guaranteed $\frac{1}{2}$-approximation.  
  In the optimal densest subgraph $S^*$ with density $\rho^*$, every vertex must have degree at least $\rho^*$. When the peeling algorithm first touches a vertex belonging to $S^*$, every remaining vertex in set $T$ must have degree $\ge \rho^*$.  
  Summing degrees yields $2|E(T)| \ge \rho^* |T|$, which directly proves $\rho(T) \ge \frac{1}{2}\rho^*$. The algorithm evaluates this set $T$, guaranteeing the bound."

---

### Slide 15: Chains and Peeling: Verification (3:50 – 4:05)
- **Spoken Text:**  
  "Our tests verified the DP against brute-force chains on 3,000 random graphs. On another 3,000 random graphs, greedy peeling was checked against an exhaustive search of all $2^n$ subsets: greedy found the exact global optimum in $2,902$ out of $3,000$ graphs, with a worst-case empirical ratio of $0.833$—far outperforming the theoretical $0.5$ floor.  
  Over to **Shatadeep** for exact densest subgraphs and our final results."

---

## Speaker 4: Shatadeep De (Slides 16 – 21)
**Allocated Time:** ~1 min 35 sec (4:05 – 5:40)

---

### Slide 16: Exact Densest Subgraph via Min-Cut (4:05 – 4:25)
- **Spoken Text:**  
  "Thank you, Jeeban. While greedy gives a fast approximation, we also implemented the **exact densest subgraph** algorithm using a reduction to $s$-$t$ minimum cut.  
  For a candidate density $p/q$, we construct flow network $N(p,q)$ with source capacities $mq$, sink capacities $mq + 2p - \deg(v)q$, and bidirectional edge capacities $q$.  
  By the degree-sum identity, the cut capacity simplifies directly to $mnq + 2(p|S| - q|E(S)|)$, meaning the min-cut precisely minimizes that deficit."

---

### Slide 17: Proof: Reduction and Dinkelbach Iteration (4:25 – 4:45)
- **Spoken Text:**  
  "A non-trivial min-cut exists if and only if there is a subset with density strictly greater than $p/q$.  
  Instead of slow binary search, we apply Dinkelbach's fractional programming: we initialize $p/q$ to the greedy peeling density and update it directly to the density of the discovered cut.  
  Crucially, by our core-degree lemma, the optimal subgraph must reside entirely inside the $\lceil \rho_{\text{greedy}} \rceil$-core. Pruning the graph down to this core slashes the flow network by orders of magnitude."

---

### Slide 18 & 19: Verification & Synthetic Sanity Run (4:45 – 5:05)
- **Spoken Text:**  
  "Our suite has 72 automated tests and 14,400 random instances checked against brute force under AddressSanitizer and UndefinedBehaviorSanitizer with zero errors.  
  On a synthetic sanity run of 200,000 transfers with planted rings, our union of pattern detectors captured 86% of all laundering transfers."

---

### Slide 20: Results on IBM HI-Small_Trans.csv (5:05 – 5:30)
- **Spoken Text:**  
  "Finally, we benchmarked our engine on the official IBM AML dataset: **5.15 lakh accounts and over 50.7 lakh transfers**.  
  The entire pipeline ingests, builds SCCs, and flags all patterns in **under 4 seconds**:
  - Our **5-core detector achieved 57.4% precision**, which is nearly **500 times higher** than the background base rate of $0.115\%$.
  - Our $k$-core pruning compressed the 515,000-vertex graph down to just 862 vertices, allowing Edmonds-Karp min-cut to find the exact optimum in **only 7 milliseconds**!
  - Combining pattern detectors achieved **54.6% recall** across the entire 5-million-edge dataset."

---

### Slide 21: Conclusion & Q&A (5:30 – 5:45)
- **Spoken Text:**  
  "In conclusion, we implemented five mathematically rigorous laundering detectors with exact correctness proofs and constant-factor guarantees. We verified them against exhaustive brute force and proved they scale effortlessly to millions of real-world transactions in a unified C++17 pipeline.  
  Thank you for your time. We are now open for questions."

---

## Quick Reference Summary Table for the Team

| Speaker | Slides | Topic | Duration | Key Talking Point / Metric to Hit |
| :--- | :---: | :--- | :---: | :--- |
| **Adityaa Saha** | 1 – 7 | Problem, Model & Pipeline, Ingestion, Kosaraju SCC & Proof | ~90s | Graph-only approach; time-sorted invariant; iterative SCC proof. |
| **Debtanu Das** | 8 – 11 | Temporal Cycles, Cycle Proof, Smurfing Fans | ~75s | SCC pruning preserves cycles; index symmetry; $O(n+m)$ sliding window. |
| **Jeeban Jyoti Patra** | 12 – 15 | Layering DP, $k$-Core Peeling, $\frac{1}{2}$-Approx Proof | ~75s | DAG dynamic programming; greedy peeling proof; 0.833 empirical ratio. |
| **Shatadeep De** | 16 – 21 | Exact Densest Min-Cut, Benchmarks on 5M rows | ~95s | Dinkelbach iteration; 57.4% precision on 5-core; 7ms exact min-cut. |
