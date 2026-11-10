// =====================================================================================
//  Money-Laundering Ring Detector                      MFCS group project, part 4 of 4
//  member4_exact_eval.cpp
//
//  Member 4: Dinic's maximum flow, exact densest subgraph by reduction to minimum cut,
//            evaluation harness (precision / recall / F1, running time vs graph size),
//            and the program's main().
//  Proof owned: the exact densest-subgraph reduction to minimum cut.
//
//  Builds on member3_chains_peeling.cpp (and through it, members 2 and 1).
//
//  Build the whole program (macOS):
//      clang++ -std=c++17 -O2 member4_exact_eval.cpp -o aml
//  Usage:
//      ./aml test                              run every member's self-tests
//      ./aml generate <out.csv> [accounts=20000] [background=200000] [seed=1]
//      ./aml detect   <transactions.csv> [params...]   one section per member
//      ./aml evaluate <transactions.csv> [params...]   precision/recall + timing tables
//  params, in order: K Whours maxCycles fanMin fanWhours chainMin chainDhours chainEps
//      defaults:     6 168    1000000   5      24        3        24          0.10
// =====================================================================================
#ifndef AML_PART_OF_LATER
#define AML_STANDALONE_4
#endif
#define AML_PART_OF_LATER
#include "member3_chains_peeling.cpp"

#ifndef AML_MEMBER4_CPP
#define AML_MEMBER4_CPP

#include <cstring>

// ------------------------------- Dinic's maximum flow -------------------------------

// Integer capacities. O(V^2 * E) in general. Both the BFS and the blocking-flow DFS are
// iterative, so long augmenting paths cannot overflow the call stack.
struct MaxFlow {
    struct Arc {
        int to;
        long long cap;  // residual capacity
    };
    int n;
    std::vector<Arc> arcs;  // arc i and arc i ^ 1 are a forward/backward pair
    std::vector<std::vector<int>> adj;
    std::vector<int> level, it;

    explicit MaxFlow(int vertices) : n(vertices), adj(vertices), level(vertices), it(vertices) {}

    // Arc u -> v with capacity cap, and v -> u with capacity revCap.
    void addArc(int u, int v, long long cap, long long revCap = 0) {
        adj[u].push_back((int)arcs.size());
        arcs.push_back({v, cap});
        adj[v].push_back((int)arcs.size());
        arcs.push_back({u, revCap});
    }

    bool buildLevels(int s, int t) {
        std::fill(level.begin(), level.end(), -1);
        std::vector<int> queue;
        queue.push_back(s);
        level[s] = 0;
        for (int head = 0; head < (int)queue.size(); head++) {
            int u = queue[head];
            for (int id : adj[u]) {
                if (arcs[id].cap > 0 && level[arcs[id].to] == -1) {
                    level[arcs[id].to] = level[u] + 1;
                    queue.push_back(arcs[id].to);
                }
            }
        }
        return level[t] != -1;
    }

    long long blockingFlow(int s, int t) {
        long long total = 0;
        std::vector<int> path;  // arc ids from s to the current vertex
        int u = s;
        while (true) {
            if (u == t) {  // augment along path, retreat to the first saturated arc
                long long push = arcs[path[0]].cap;
                for (int id : path) push = std::min(push, arcs[id].cap);
                int firstSaturated = -1;
                for (int i = 0; i < (int)path.size(); i++) {
                    arcs[path[i]].cap -= push;
                    arcs[path[i] ^ 1].cap += push;
                    if (firstSaturated == -1 && arcs[path[i]].cap == 0) firstSaturated = i;
                }
                total += push;
                u = arcs[path[firstSaturated] ^ 1].to;
                path.resize(firstSaturated);
                continue;
            }
            bool advanced = false;
            while (it[u] < (int)adj[u].size()) {
                int id = adj[u][it[u]];
                if (arcs[id].cap > 0 && level[arcs[id].to] == level[u] + 1) {
                    path.push_back(id);
                    u = arcs[id].to;
                    advanced = true;
                    break;
                }
                it[u]++;
            }
            if (advanced) continue;
            if (path.empty()) break;  // s is exhausted
            level[u] = -1;            // dead end: never enter u again in this phase
            u = arcs[path.back() ^ 1].to;
            path.pop_back();
            it[u]++;
        }
        return total;
    }

    long long run(int s, int t) {
        long long flow = 0;
        while (buildLevels(s, t)) {
            std::fill(it.begin(), it.end(), 0);
            flow += blockingFlow(s, t);
        }
        return flow;
    }

    // After run(): vertices reachable from s in the residual graph = source side of a min cut.
    std::vector<char> sourceSide(int s) const {
        std::vector<char> seen(n, 0);
        std::vector<int> queue;
        queue.push_back(s);
        seen[s] = 1;
        for (int head = 0; head < (int)queue.size(); head++) {
            int u = queue[head];
            for (int id : adj[u]) {
                if (arcs[id].cap > 0 && !seen[arcs[id].to]) {
                    seen[arcs[id].to] = 1;
                    queue.push_back(arcs[id].to);
                }
            }
        }
        return seen;
    }
};

// ------------------------------- exact densest subgraph -----------------------------

struct ExactResult {
    long long edges = 0;      // |E(S)| of the optimal set
    int vertices = 0;         // |S|
    std::vector<char> inSet;  // inSet[v] = 1 iff v is in S
    int iterations = 0;       // number of min-cut computations
    int networkVertices = 0;  // vertices kept after core pruning
    long long networkEdges = 0;

    double density() const { return vertices == 0 ? 0.0 : (double)edges / vertices; }
};

long long gcdLL(long long a, long long b) {
    while (b != 0) { long long r = a % b; a = b; b = r; }
    return a;
}

// Goldberg's min-cut reduction. For a guess p/q build the network on {s, t} + V:
//     s -> v           capacity m*q
//     v -> t           capacity m*q + 2*p - deg(v)*q
//     u -> v, v -> u   capacity q            for every edge {u, v}
// The cut ({s} + S, rest) has capacity m*n*q + 2*(p*|S| - q*|E(S)|), so a minimum cut
// finds the S minimising p*|S| - q*|E(S)|, negative iff some set is denser than p/q.
// Start from the greedy density; replace p/q by the density of the set found until no
// denser set exists (Dinkelbach iteration). All arithmetic is in integers.
// Pruning: a densest set lies inside the k-core for k = ceil(greedy density).
ExactResult findDensestExact(const UndirectedGraph& h, const PeelResult& greedy, bool usePruning = true) {
    ExactResult res;
    res.edges = greedy.bestEdges;
    res.vertices = greedy.bestVertices;
    res.inSet = greedy.inDensest;
    if (h.m == 0) return res;

    int minCore = 0;
    if (usePruning) minCore = (int)((greedy.bestEdges + greedy.bestVertices - 1) / greedy.bestVertices);

    std::vector<int> newId(h.n, -1), oldId;  // induced subgraph on the minCore-core
    for (int v = 0; v < h.n; v++) {
        if (greedy.coreNumber[v] >= minCore) {
            newId[v] = (int)oldId.size();
            oldId.push_back(v);
        }
    }
    int n = (int)oldId.size();
    std::vector<int> deg(n, 0);
    long long m = 0;
    for (int i = 0; i < n; i++) {
        for (int u : h.adj[oldId[i]]) {
            if (newId[u] == -1) continue;
            deg[i]++;
            if (newId[u] > i) m++;
        }
    }
    res.networkVertices = n;
    res.networkEdges = m;
    if (m == 0) return res;

    long long divisor = gcdLL(res.edges, res.vertices);
    long long p = res.edges / divisor, q = res.vertices / divisor;
    int s = n, t = n + 1;

    while (true) {
        MaxFlow flow(n + 2);
        for (int i = 0; i < n; i++) {
            flow.addArc(s, i, m * q);
            flow.addArc(i, t, m * q + 2 * p - (long long)deg[i] * q);
            for (int u : h.adj[oldId[i]])
                if (newId[u] > i) flow.addArc(i, newId[u], q, q);
        }
        flow.run(s, t);
        res.iterations++;
        std::vector<char> side = flow.sourceSide(s);

        long long setEdges = 0;
        int setVertices = 0;
        for (int i = 0; i < n; i++) {
            if (!side[i]) continue;
            setVertices++;
            for (int u : h.adj[oldId[i]])
                if (newId[u] > i && side[newId[u]]) setEdges++;
        }
        if (setVertices == 0 || q * setEdges - p * setVertices <= 0) break;  // nothing denser than p/q

        res.edges = setEdges;
        res.vertices = setVertices;
        std::fill(res.inSet.begin(), res.inSet.end(), 0);
        for (int i = 0; i < n; i++)
            if (side[i]) res.inSet[oldId[i]] = 1;
        divisor = gcdLL(setEdges, setVertices);
        p = setEdges / divisor;
        q = setVertices / divisor;
    }
    return res;
}

// ------------------------------- parameters -----------------------------------------

struct Params {
    CycleParams cycle;
    FanParams fan;
    ChainParams chain;
};

// Reads the optional parameters argv[first], argv[first + 1], ... in the documented order.
Params parseParams(int argc, char** argv, int first) {
    Params params;
    params.cycle.maxCycles = 1000000;  // safety cap
    int i = first;
    if (argc > i) params.cycle.maxLength = std::atoi(argv[i]);
    if (argc > i + 1) params.cycle.window = std::atoll(argv[i + 1]) * 3600LL;
    if (argc > i + 2) params.cycle.maxCycles = std::atoll(argv[i + 2]);
    if (argc > i + 3) params.fan.minDistinct = std::atoi(argv[i + 3]);
    if (argc > i + 4) params.fan.window = std::atoll(argv[i + 4]) * 3600LL;
    if (argc > i + 5) params.chain.minLength = std::atoi(argv[i + 5]);
    if (argc > i + 6) params.chain.maxDelay = std::atoll(argv[i + 6]) * 3600LL;
    if (argc > i + 7) params.chain.tolerance = std::atof(argv[i + 7]);
    return params;
}

// ------------------------------- evaluation harness ---------------------------------
// Scoring is per transfer: a detector's positives are the transfers it flags; the truth
// is the "Is Laundering" label. Labels on self loops are excluded from the denominator
// (no detector can flag a self loop). Recall is against ALL laundering labels, so a
// single-pattern detector cannot reach 1.0.

struct Detection {
    std::string name;
    std::vector<char> flagged;  // one entry per transfer
    double seconds;
    std::string note;
};

std::vector<Detection> runAll(const Graph& g, const Params& params) {
    std::vector<Detection> out;
    int m = (int)g.edges.size();
    char buffer[120];

    auto start = std::chrono::steady_clock::now();
    SccResult scc = tarjanScc(g);
    bool truncated = false;
    std::vector<std::vector<int>> cycles = findTemporalCycles(g, scc, params.cycle, &truncated);
    out.push_back({"Temporal cycles", cycleEdges(g, cycles), secondsSince(start),
                   std::to_string(cycles.size()) + " cycles" + (truncated ? " (hit maxCycles)" : "")});

    start = std::chrono::steady_clock::now();
    FanResult fanOut = findFans(g, params.fan, true);
    out.push_back({"Fan-out", fanOut.flaggedEdge, secondsSince(start), std::to_string(fanOut.hits.size()) + " centres"});

    start = std::chrono::steady_clock::now();
    FanResult fanIn = findFans(g, params.fan, false);
    out.push_back({"Fan-in", fanIn.flaggedEdge, secondsSince(start), std::to_string(fanIn.hits.size()) + " centres"});

    start = std::chrono::steady_clock::now();
    ChainResult chains = findChains(g, params.chain);
    out.push_back({"Pass-through chains", chains.flaggedEdge, secondsSince(start),
                   "longest chain " + std::to_string(chains.longest)});

    start = std::chrono::steady_clock::now();
    UndirectedGraph h = buildUndirected(g);
    PeelResult peeled = peel(h);
    double peelSeconds = secondsSince(start);
    out.push_back({"Max k-core", edgesInside(g, coreMembers(peeled, peeled.maxCore)), peelSeconds,
                   "k = " + std::to_string(peeled.maxCore)});
    std::snprintf(buffer, sizeof(buffer), "%d vertices, density %.3f", peeled.bestVertices, peeled.density());
    out.push_back({"Greedy densest", edgesInside(g, peeled.inDensest), peelSeconds, buffer});

    start = std::chrono::steady_clock::now();
    ExactResult exact = findDensestExact(h, peeled);
    std::snprintf(buffer, sizeof(buffer), "%d vertices, density %.3f, %d min cuts", exact.vertices, exact.density(),
                  exact.iterations);
    out.push_back({"Exact densest", edgesInside(g, exact.inSet), secondsSince(start), buffer});

    // Union of the four pattern detectors; dense sets flag whole regions and are left out.
    std::vector<char> any(m, 0);
    double total = 0;
    for (int d = 0; d < 4; d++) {
        for (int i = 0; i < m; i++) any[i] = any[i] | out[d].flagged[i];
        total += out[d].seconds;
    }
    out.push_back({"Union of pattern detectors", any, total, "cycles, fans, chains"});
    return out;
}

struct Score {
    long long flagged = 0, truePositives = 0;
    double precision = 0, recall = 0, f1 = 0;
};

Score scoreDetection(const Graph& g, const std::vector<char>& flagged, long long positives) {
    Score sc;
    countFlagged(g, flagged, sc.flagged, sc.truePositives);
    sc.precision = sc.flagged > 0 ? (double)sc.truePositives / sc.flagged : 0.0;
    sc.recall = positives > 0 ? (double)sc.truePositives / positives : 0.0;
    sc.f1 = sc.precision + sc.recall > 0 ? 2 * sc.precision * sc.recall / (sc.precision + sc.recall) : 0.0;
    return sc;
}

// First `count` transfers in time order, on the same vertex set.
Graph prefixGraph(const Graph& g, int count) {
    Graph p;
    p.n = g.n;
    p.edges.assign(g.edges.begin(), g.edges.begin() + count);
    p.finalize();
    return p;
}

void evaluate(const Graph& g, const IngestStats& stats, const Params& params, const std::string& path) {
    int m = (int)g.edges.size();
    long long positives = 0, selfLoopPositives = 0;
    for (const Edge& e : g.edges) {
        if (!e.isLaundering) continue;
        if (e.from == e.to) selfLoopPositives++;
        else positives++;
    }
    std::printf("Dataset: %s\n\n", path.c_str());
    std::printf("- Accounts: %d\n- Transfers: %d (rows skipped: %lld)\n", g.n, m, stats.rowsSkipped);
    std::printf("- Labelled laundering: %lld (plus %lld on self loops, not counted)\n", positives, selfLoopPositives);
    std::printf("- Parameters: cycles K=%d W=%lldh; fan k=%d W=%lldh; chains L=%d D=%lldh eps=%.2f\n\n",
                params.cycle.maxLength, params.cycle.window / 3600, params.fan.minDistinct, params.fan.window / 3600,
                params.chain.minLength, params.chain.maxDelay / 3600, params.chain.tolerance);

    std::vector<Detection> full = runAll(g, params);
    std::printf("| Detector | Flagged | True positives | Precision | Recall | F1 | Time (s) | Notes |\n");
    std::printf("|---|---|---|---|---|---|---|---|\n");
    for (const Detection& d : full) {
        Score sc = scoreDetection(g, d.flagged, positives);
        std::printf("| %s | %lld | %lld | %.4f | %.4f | %.4f | %.3f | %s |\n", d.name.c_str(), sc.flagged,
                    sc.truePositives, sc.precision, sc.recall, sc.f1, d.seconds, d.note.c_str());
    }

    std::printf("\nRunning time (s) on the first x%% of transfers in time order\n\n");
    std::printf("| Transfers | Cycles | Fan-out | Fan-in | Chains | Peeling | Exact densest |\n");
    std::printf("|---|---|---|---|---|---|---|\n");
    for (int percent = 25; percent <= 100; percent += 25) {
        int count = (int)((long long)m * percent / 100);
        std::vector<Detection> part = percent == 100 ? full : runAll(prefixGraph(g, count), params);
        std::printf("| %d (%d%%) | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f |\n", count, percent, part[0].seconds,
                    part[1].seconds, part[2].seconds, part[3].seconds, part[4].seconds, part[6].seconds);
    }
}

// ------------------------------- report section -------------------------------------

void reportMember4(const Graph& g, const UndirectedGraph& h, const PeelResult& peeled) {
    std::printf("== Member 4: exact densest subgraph ==\n");
    auto start = std::chrono::steady_clock::now();
    ExactResult exact = findDensestExact(h, peeled);
    double exactSeconds = secondsSince(start);
    long long total, laundering;
    countFlagged(g, edgesInside(g, exact.inSet), total, laundering);
    std::printf("flow network                %d vertices, %lld edges after core pruning\n", exact.networkVertices,
                exact.networkEdges);
    std::printf("exact densest set           %d vertices, %lld edges, density %.4f\n", exact.vertices, exact.edges,
                exact.density());
    std::printf("greedy / exact density      %.4f\n", exact.density() > 0 ? peeled.density() / exact.density() : 1.0);
    std::printf("transfers inside            %lld (labelled laundering: %lld)\n", total, laundering);
    std::printf("min cuts / time (s)         %d / %.3f\n\n", exact.iterations, exactSeconds);
}

// ------------------------------- self-tests -----------------------------------------

UndirectedGraph fromPairs(int n, const std::vector<std::pair<int, int>>& pairs) {
    Graph g;
    for (int i = 0; i < n; i++) g.getVertex(std::to_string(i));
    long long t = 0;
    for (const std::pair<int, int>& p : pairs) g.addEdge(p.first, p.second, 1.0, t++, false);
    g.finalize();
    return buildUndirected(g);
}

bool testMember4() {
    int failedBefore = checksFailed;
    std::mt19937 rng(4242);

    // ---- max flow = brute-force min cut on 2000 random networks ----
    int flowMismatches = 0;
    for (int trial = 0; trial < 2000; trial++) {
        int n = 2 + rng() % 7;
        int arcCount = rng() % 20;
        std::vector<std::vector<long long>> cap(n, std::vector<long long>(n, 0));
        MaxFlow flow(n);
        for (int i = 0; i < arcCount; i++) {
            int u = rng() % n, v = rng() % n;
            if (u == v) continue;
            long long c = rng() % 10, rc = rng() % 3 == 0 ? rng() % 10 : 0;
            cap[u][v] += c;
            cap[v][u] += rc;
            flow.addArc(u, v, c, rc);
        }
        int s = 0, t = n - 1;
        long long best = -1;
        for (int mask = 0; mask < (1 << n); mask++) {
            if (!(mask >> s & 1) || (mask >> t & 1)) continue;
            long long cut = 0;
            for (int u = 0; u < n; u++)
                for (int v = 0; v < n; v++)
                    if ((mask >> u & 1) && !(mask >> v & 1)) cut += cap[u][v];
            if (best == -1 || cut < best) best = cut;
        }
        long long value = flow.run(s, t);
        std::vector<char> side = flow.sourceSide(s);
        long long sideCut = 0;
        for (int u = 0; u < n; u++)
            for (int v = 0; v < n; v++)
                if (side[u] && !side[v]) sideCut += cap[u][v];
        if (value != best || !side[s] || side[t] || sideCut != value) flowMismatches++;
    }
    CHECK(flowMismatches == 0);

    // ---- hand-made: 4-clique {0,1,2,3} with tail 3 - 4 - 5 ----
    UndirectedGraph h = fromPairs(6, {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}, {3, 4}, {4, 5}});
    ExactResult exact = findDensestExact(h, peel(h));
    CHECK(exact.edges == 6 && exact.vertices == 4);
    CHECK(exact.inSet[0] && exact.inSet[3] && !exact.inSet[4] && !exact.inSet[5]);
    CHECK(exact.networkVertices == 4);  // pruning keeps only the 2-core, which is the clique

    // ---- hand-made: K_{2,6} (density 3/2) next to three triangles (density 1) ----
    std::vector<std::pair<int, int>> pairs;
    for (int v = 2; v <= 7; v++) { pairs.push_back({0, v}); pairs.push_back({1, v}); }
    for (int base = 8; base <= 14; base += 3) {
        pairs.push_back({base, base + 1});
        pairs.push_back({base + 1, base + 2});
        pairs.push_back({base, base + 2});
    }
    UndirectedGraph k = fromPairs(17, pairs);
    ExactResult kExact = findDensestExact(k, peel(k));
    CHECK(kExact.edges * 2 == kExact.vertices * 3 && kExact.vertices == 8);

    // ---- 3000 random graphs: exact density equals the best over all subsets ----
    int improved = 0, maxIterations = 0, exactMismatches = 0;
    for (int trial = 0; trial < 3000; trial++) {
        int n = 2 + rng() % 9;
        int m = 1 + rng() % (3 * n);
        std::vector<std::pair<int, int>> randomPairs;
        for (int i = 0; i < m; i++) randomPairs.push_back({(int)(rng() % n), (int)(rng() % n)});
        UndirectedGraph u = fromPairs(n, randomPairs);
        PeelResult p = peel(u);
        long long optEdges;
        int optVertices;
        bruteDensest(u, optEdges, optVertices);
        for (int pruning = 0; pruning < 2; pruning++) {
            ExactResult e = findDensestExact(u, p, pruning == 1);
            if (e.edges * optVertices != optEdges * e.vertices) exactMismatches++;
            int mask = 0, size = 0;
            for (int v = 0; v < n; v++) if (e.inSet[v]) { mask |= 1 << v; size++; }
            if (size != e.vertices || edgesWithin(u, mask) != e.edges) exactMismatches++;
            maxIterations = std::max(maxIterations, e.iterations);
            if (pruning == 1 && e.edges * p.bestVertices > p.bestEdges * e.vertices) improved++;
        }
    }
    CHECK(exactMismatches == 0 && improved > 0);

    // ---- evaluation harness: hand-checked scores on a tiny labelled graph ----
    Graph g;
    for (int i = 0; i < 4; i++) g.getVertex(std::to_string(i));
    g.addEdge(0, 1, 1, 0, true);
    g.addEdge(1, 2, 1, 1, true);
    g.addEdge(2, 3, 1, 2, false);
    g.addEdge(3, 0, 1, 3, false);
    g.finalize();
    std::vector<char> flagged = {1, 0, 1, 0};  // one true positive, one false positive
    Score sc = scoreDetection(g, flagged, 2);
    CHECK(sc.flagged == 2 && sc.truePositives == 1);
    CHECK(sc.precision == 0.5 && sc.recall == 0.5 && sc.f1 == 0.5);
    Graph half = prefixGraph(g, 2);
    CHECK(half.n == 4 && half.edges.size() == 2 && half.outDegree(0) == 1);

    // ---- end to end on a small synthetic file: every detector catches its planted pattern ----
    std::string summary;
    const char* path = "aml_test_e2e.csv";
    CHECK(generateSynthetic(path, 2000, 10000, 3, summary));
    Graph s;
    IngestStats stats;
    CHECK(loadCsv(path, s, stats));
    std::remove(path);
    Params params;
    std::vector<Detection> all = runAll(s, params);
    CHECK(all.size() == 8);
    long long positives = 0;
    for (const Edge& e : s.edges) positives += e.isLaundering && e.from != e.to;
    for (int d = 0; d < 4; d++) CHECK(scoreDetection(s, all[d].flagged, positives).truePositives > 0);
    CHECK(scoreDetection(s, all[7].flagged, positives).recall > 0.5);

    bool ok = checksFailed == failedBefore;
    std::printf("[member 4] max flow + exact densest + evaluation tests: %s (exact beat greedy on %d graphs, "
                "at most %d min cuts)\n", ok ? "PASS" : "FAIL", improved, maxIterations);
    return ok;
}

bool runAllTests() {
    bool ok = testMember1();
    ok = testMember2() && ok;
    ok = testMember3() && ok;
    ok = testMember4() && ok;
    std::printf("%s: %d checks, %d failed\n", ok ? "ALL TESTS PASSED" : "SOME TESTS FAILED", checksRun, checksFailed);
    return ok;
}

#endif  // AML_MEMBER4_CPP

// ------------------------------- main -----------------------------------------------

#ifdef AML_STANDALONE_4
void printUsage(const char* program) {
    std::fprintf(stderr,
                 "usage:\n"
                 "  %s test\n"
                 "  %s generate <out.csv> [accounts=20000] [background=200000] [seed=1]\n"
                 "  %s detect   <transactions.csv> [K W maxCycles fanMin fanW chainMin chainD chainEps]\n"
                 "  %s evaluate <transactions.csv> [same parameters]\n"
                 "  windows (W, fanW, chainD) are in hours; defaults 6 168 1000000 5 24 3 24 0.10\n",
                 program, program, program, program);
}

int main(int argc, char** argv) {
    if (argc < 2) { printUsage(argv[0]); return 1; }
    std::string mode = argv[1];

    if (mode == "test") return runAllTests() ? 0 : 1;

    if (mode == "generate") {
        if (argc < 3) { printUsage(argv[0]); return 1; }
        int accounts = argc > 3 ? std::atoi(argv[3]) : 20000;
        int background = argc > 4 ? std::atoi(argv[4]) : 200000;
        int seed = argc > 5 ? std::atoi(argv[5]) : 1;
        std::string summary;
        if (!generateSynthetic(argv[2], accounts, background, seed, summary)) {
            std::fprintf(stderr, "cannot write %s\n", argv[2]);
            return 1;
        }
        std::printf("wrote %s: %s\n", argv[2], summary.c_str());
        return 0;
    }

    if (mode != "detect" && mode != "evaluate") { printUsage(argv[0]); return 1; }
    if (argc < 3) { printUsage(argv[0]); return 1; }
    Params params = parseParams(argc, argv, 3);
    Graph g;
    IngestStats stats;
    auto start = std::chrono::steady_clock::now();
    if (!loadCsv(argv[2], g, stats)) {
        std::fprintf(stderr, "cannot open %s\n", argv[2]);
        return 1;
    }
    double loadSeconds = secondsSince(start);

    if (mode == "evaluate") {
        evaluate(g, stats, params, argv[2]);
        return 0;
    }

    start = std::chrono::steady_clock::now();
    SccResult scc = tarjanScc(g);
    double sccSeconds = secondsSince(start);
    reportMember1(g, stats, loadSeconds, scc, sccSeconds);
    reportMember2(g, scc, params.cycle, params.fan);
    UndirectedGraph h;
    PeelResult peeled;
    reportMember3(g, params.chain, h, peeled);
    reportMember4(g, h, peeled);
    return 0;
}
#endif
