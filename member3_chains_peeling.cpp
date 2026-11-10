// =====================================================================================
//  Money-Laundering Ring Detector                      MFCS group project, part 3 of 4
//  member3_chains_peeling.cpp
//
//  Member 3: pass-through chains (layering), undirected projection,
//            k-core decomposition and Charikar's greedy peeling.
//  Proof owned: greedy peeling is a 1/2-approximation for the densest subgraph
//               (+ the chain dynamic-programming invariant).
//
//  Builds on member2_cycles_fan.cpp (and through it, member1).
//  Build this part alone:  clang++ -std=c++17 -O2 member3_chains_peeling.cpp -o part3 && ./part3
// =====================================================================================
#ifndef AML_PART_OF_LATER
#define AML_STANDALONE_3
#endif
#define AML_PART_OF_LATER
#include "member2_cycles_fan.cpp"

#ifndef AML_MEMBER3_CPP
#define AML_MEMBER3_CPP

// ------------------------------- pass-through chains --------------------------------

struct ChainParams {
    int minLength = 3;             // L: at least L edges (L - 1 intermediaries)
    long long maxDelay = 86400LL;  // D: money is forwarded within D seconds
    double tolerance = 0.10;       // eps: each hop keeps at most this fraction
};

// Edge f "follows" edge e when an intermediary forwards what it just received:
//   f leaves the account e entered, index(f) > index(e), 0 <= t(f) - t(e) <= D,
//   and (1 - eps) * a(e) <= a(f) <= a(e).
// A pass-through chain is a sequence e1..ek in which each edge follows the previous one.
// Self loops never take part.
bool follows(const Graph& g, int eIdx, int fIdx, const ChainParams& p) {
    const Edge& e = g.edges[eIdx];
    const Edge& f = g.edges[fIdx];
    if (fIdx <= eIdx || e.from == e.to || f.from == f.to) return false;
    if (f.from != e.to) return false;
    if (f.timestamp - e.timestamp > p.maxDelay) return false;  // t(f) >= t(e) since fIdx > eIdx
    return f.amount <= e.amount && f.amount >= (1.0 - p.tolerance) * e.amount;
}

struct ChainResult {
    std::vector<int> back;          // back[e] = edges in the longest chain ending at e
    std::vector<int> fwd;           // fwd[e]  = edges in the longest chain starting at e
    std::vector<int> prevEdge;      // predecessor achieving back[e], or -1
    std::vector<int> nextEdge;      // successor achieving fwd[e], or -1
    std::vector<char> flaggedEdge;  // 1 iff e lies on a chain of at least L edges
    int longest = 0;
    int longestEnd = -1;            // last edge of one longest chain

    // Edge indices of the longest chain through edge e, in order.
    std::vector<int> chainThrough(int e) const {
        std::vector<int> chain;
        for (int x = e; x != -1; x = prevEdge[x]) chain.push_back(x);
        std::reverse(chain.begin(), chain.end());
        for (int x = nextEdge[e]; x != -1; x = nextEdge[x]) chain.push_back(x);
        return chain;
    }
};

// Position in adj of the first edge with timestamp >= t.
int lowerBoundTime(const Graph& g, const std::vector<int>& adj, long long t) {
    int lo = 0, hi = (int)adj.size();
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (g.edges[adj[mid]].timestamp < t) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

// Longest-path dynamic programming over the "follows" DAG, in edge-index order.
// O(m * (log d + c)) where c is the most edges one account has within D seconds.
ChainResult findChains(const Graph& g, ChainParams params) {
    int m = (int)g.edges.size();
    ChainResult res;
    res.back.assign(m, 1);
    res.fwd.assign(m, 1);
    res.prevEdge.assign(m, -1);
    res.nextEdge.assign(m, -1);
    res.flaggedEdge.assign(m, 0);

    // back: predecessors of f are in-edges of f.from with time in [t(f) - D, t(f)].
    for (int f = 0; f < m; f++) {
        const Edge& edge = g.edges[f];
        if (edge.from == edge.to) continue;
        const std::vector<int>& adj = g.inAdj[edge.from];
        for (int pos = lowerBoundTime(g, adj, edge.timestamp - params.maxDelay); pos < (int)adj.size(); pos++) {
            int e = adj[pos];
            if (e >= f) break;  // adjacency lists are in increasing index order
            if (follows(g, e, f, params) && res.back[e] + 1 > res.back[f]) {
                res.back[f] = res.back[e] + 1;
                res.prevEdge[f] = e;
            }
        }
    }
    // fwd: successors of e are out-edges of e.to with time in [t(e), t(e) + D].
    for (int e = m - 1; e >= 0; e--) {
        const Edge& edge = g.edges[e];
        if (edge.from == edge.to) continue;
        const std::vector<int>& adj = g.outAdj[edge.to];
        for (int pos = lowerBoundTime(g, adj, edge.timestamp); pos < (int)adj.size(); pos++) {
            int f = adj[pos];
            if (g.edges[f].timestamp - edge.timestamp > params.maxDelay) break;
            if (follows(g, e, f, params) && res.fwd[f] + 1 > res.fwd[e]) {
                res.fwd[e] = res.fwd[f] + 1;
                res.nextEdge[e] = f;
            }
        }
    }
    for (int e = 0; e < m; e++) {
        if (g.edges[e].from == g.edges[e].to) continue;
        if (res.back[e] + res.fwd[e] - 1 >= params.minLength) res.flaggedEdge[e] = 1;
        if (res.back[e] > res.longest) {
            res.longest = res.back[e];
            res.longestEnd = e;
        }
    }
    return res;
}

// ------------------------------- undirected projection ------------------------------

// Simple undirected projection H of the transaction multigraph: {u, v} is an edge iff
// u != v and at least one transfer goes between u and v in either direction.
struct UndirectedGraph {
    int n = 0;
    long long m = 0;
    std::vector<std::vector<int>> adj;
};

// O(m log m).
UndirectedGraph buildUndirected(const Graph& g) {
    std::vector<std::pair<int, int>> pairs;
    pairs.reserve(g.edges.size());
    for (const Edge& e : g.edges)
        if (e.from != e.to) pairs.push_back({std::min(e.from, e.to), std::max(e.from, e.to)});
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());

    UndirectedGraph h;
    h.n = g.n;
    h.m = (long long)pairs.size();
    h.adj.assign(g.n, {});
    for (const std::pair<int, int>& p : pairs) {
        h.adj[p.first].push_back(p.second);
        h.adj[p.second].push_back(p.first);
    }
    return h;
}

// ------------------------------- k-core and greedy peeling --------------------------

// Density of a vertex set S is |E(S)| / |S|, where E(S) are the edges with both ends in S.
struct PeelResult {
    std::vector<int> coreNumber;  // coreNumber[v] = largest k such that v is in the k-core
    int maxCore = 0;
    std::vector<int> order;       // vertices in removal order
    long long bestEdges = 0;      // |E(S)| and |S| of the densest set seen while peeling
    int bestVertices = 0;
    std::vector<char> inDensest;  // inDensest[v] = 1 iff v is in that set

    double density() const { return bestVertices == 0 ? 0.0 : (double)bestEdges / bestVertices; }
};

// Greedy peeling (Charikar): repeatedly delete a vertex of minimum degree. The same pass
// gives the k-core decomposition and a 1/2-approximate densest subgraph.
// Bucket queue with lazy deletion: O(n + m).
PeelResult peel(const UndirectedGraph& h) {
    int n = h.n;
    PeelResult res;
    res.coreNumber.assign(n, 0);
    res.inDensest.assign(n, 0);
    res.order.reserve(n);

    std::vector<int> deg(n);
    std::vector<char> removed(n, 0);
    int maxDeg = 0;
    for (int v = 0; v < n; v++) {
        deg[v] = (int)h.adj[v].size();
        maxDeg = std::max(maxDeg, deg[v]);
    }
    std::vector<std::vector<int>> bucket(maxDeg + 1);
    for (int v = 0; v < n; v++) bucket[deg[v]].push_back(v);

    long long edgesLeft = h.m;
    int verticesLeft = n;
    int bestStep = 0;  // the best set is everything not among the first bestStep removals
    res.bestEdges = edgesLeft;
    res.bestVertices = verticesLeft;

    int cur = 0, level = 0;
    for (int step = 0; step < n; step++) {
        int v = -1;
        while (v == -1) {
            while (bucket[cur].empty()) cur++;
            int cand = bucket[cur].back();
            bucket[cur].pop_back();
            if (!removed[cand] && deg[cand] == cur) v = cand;  // skip stale entries
        }
        level = std::max(level, deg[v]);
        res.coreNumber[v] = level;
        removed[v] = 1;
        res.order.push_back(v);
        edgesLeft -= deg[v];
        verticesLeft--;
        for (int u : h.adj[v]) {
            if (removed[u]) continue;
            deg[u]--;
            bucket[deg[u]].push_back(u);
        }
        if (cur > 0) cur--;  // a neighbour may have dropped one bucket below

        // compare edgesLeft / verticesLeft with bestEdges / bestVertices without division
        if (verticesLeft > 0 && edgesLeft * res.bestVertices > res.bestEdges * verticesLeft) {
            res.bestEdges = edgesLeft;
            res.bestVertices = verticesLeft;
            bestStep = step + 1;
        }
    }
    res.maxCore = level;
    for (int i = bestStep; i < n; i++) res.inDensest[res.order[i]] = 1;
    return res;
}

// Transfers with both endpoints inside the vertex set (self loops excluded).
std::vector<char> edgesInside(const Graph& g, const std::vector<char>& inSet) {
    std::vector<char> flagged(g.edges.size(), 0);
    for (int i = 0; i < (int)g.edges.size(); i++) {
        const Edge& e = g.edges[i];
        if (e.from != e.to && inSet[e.from] && inSet[e.to]) flagged[i] = 1;
    }
    return flagged;
}

// Vertices of the k-core.
std::vector<char> coreMembers(const PeelResult& res, int k) {
    std::vector<char> inCore(res.coreNumber.size(), 0);
    for (int v = 0; v < (int)res.coreNumber.size(); v++)
        if (res.coreNumber[v] >= k) inCore[v] = 1;
    return inCore;
}

// ------------------------------- report section -------------------------------------

// Prints member 3's section and hands the projection and peeling result to member 4.
void reportMember3(const Graph& g, const ChainParams& chainParams, UndirectedGraph& h, PeelResult& peeled) {
    std::printf("== Member 3: pass-through chains, k-core and greedy peeling ==\n");
    auto start = std::chrono::steady_clock::now();
    ChainResult chains = findChains(g, chainParams);
    double chainSeconds = secondsSince(start);
    long long total, laundering;
    countFlagged(g, chains.flaggedEdge, total, laundering);
    std::printf("chain params                L=%d D=%lldh eps=%.2f\n", chainParams.minLength,
                chainParams.maxDelay / 3600, chainParams.tolerance);
    std::printf("longest chain               %d edges\n", chains.longest);
    std::printf("transfers on a chain        %lld (labelled laundering: %lld)\n", total, laundering);
    std::printf("chain time (s)              %.3f\n", chainSeconds);

    start = std::chrono::steady_clock::now();
    h = buildUndirected(g);
    peeled = peel(h);
    double peelSeconds = secondsSince(start);
    int coreVertices = 0;
    for (int v = 0; v < g.n; v++) coreVertices += peeled.coreNumber[v] == peeled.maxCore;
    countFlagged(g, edgesInside(g, coreMembers(peeled, peeled.maxCore)), total, laundering);
    std::printf("undirected edges            %lld\n", h.m);
    std::printf("max core                    k=%d, %d vertices, %lld transfers inside (laundering: %lld)\n",
                peeled.maxCore, coreVertices, total, laundering);
    countFlagged(g, edgesInside(g, peeled.inDensest), total, laundering);
    std::printf("greedy densest set          %d vertices, %lld edges, density %.4f\n", peeled.bestVertices,
                peeled.bestEdges, peeled.density());
    std::printf("  transfers inside          %lld (labelled laundering: %lld)\n", total, laundering);
    std::printf("peeling time (s)            %.3f\n\n", peelSeconds);
}

// ------------------------------- self-tests -----------------------------------------

// Brute force chains: enumerate every chain, record for each edge the longest one containing it.
void bruteChains(const Graph& g, const ChainParams& p, std::vector<int>& chain, std::vector<int>& best) {
    for (int x : chain) best[x] = std::max(best[x], (int)chain.size());
    for (int f = 0; f < (int)g.edges.size(); f++) {
        if (!follows(g, chain.back(), f, p)) continue;
        chain.push_back(f);
        bruteChains(g, p, chain, best);
        chain.pop_back();
    }
}

// |E(S)| for a vertex set given as a bitmask (n <= 30).
long long edgesWithin(const UndirectedGraph& h, int mask) {
    long long count = 0;
    for (int u = 0; u < h.n; u++)
        if (mask >> u & 1)
            for (int v : h.adj[u])
                if (v > u && (mask >> v & 1)) count++;
    return count;
}

// Naive core numbers: v is in the k-core iff it survives repeatedly deleting vertices of degree < k.
std::vector<int> naiveCores(const UndirectedGraph& h) {
    std::vector<int> core(h.n, 0);
    for (int k = 1; k <= h.n; k++) {
        std::vector<char> alive(h.n, 1);
        bool changed = true;
        while (changed) {
            changed = false;
            for (int v = 0; v < h.n; v++) {
                if (!alive[v]) continue;
                int d = 0;
                for (int u : h.adj[v]) d += alive[u];
                if (d < k) { alive[v] = 0; changed = true; }
            }
        }
        for (int v = 0; v < h.n; v++) if (alive[v]) core[v] = k;
    }
    return core;
}

// Best density over all non-empty subsets, as a fraction optEdges / optVertices.
void bruteDensest(const UndirectedGraph& h, long long& optEdges, int& optVertices) {
    optEdges = 0;
    optVertices = 1;
    for (int mask = 1; mask < (1 << h.n); mask++) {
        long long es = edgesWithin(h, mask);
        int vs = 0;
        for (int v = 0; v < h.n; v++) vs += mask >> v & 1;
        if (es * optVertices > optEdges * vs) { optEdges = es; optVertices = vs; }
    }
}

bool testMember3() {
    int failedBefore = checksFailed;

    // ---- chains, hand-made: A -1000-> B -980-> C -950-> D ten seconds apart (a 3-edge chain);
    //      D -500-> E (halves: not pass-through); B -990-> F five days later; C -> C self loop.
    Graph g;
    int a = g.getVertex("A"), b = g.getVertex("B"), c = g.getVertex("C");
    int d = g.getVertex("D"), e = g.getVertex("E"), f = g.getVertex("F");
    g.addEdge(a, b, 1000, 0, true);
    g.addEdge(b, c, 980, 10, true);
    g.addEdge(c, d, 950, 20, true);
    g.addEdge(d, e, 500, 30, false);
    g.addEdge(b, f, 990, 5 * 86400, false);
    g.addEdge(c, c, 960, 15, false);
    g.finalize();

    ChainParams params;  // L = 3, D = 1 day, eps = 0.10
    ChainResult res = findChains(g, params);
    long long flagged, laundering;
    countFlagged(g, res.flaggedEdge, flagged, laundering);
    CHECK(res.longest == 3 && flagged == 3 && laundering == 3);
    std::vector<int> chain = res.chainThrough(res.longestEnd);
    CHECK(chain.size() == 3 && g.edges[chain[0]].from == a && g.edges[chain[2]].to == d);
    params.maxDelay = 9;  // hops are 10 seconds apart
    CHECK(findChains(g, params).longest == 1);
    params.maxDelay = 10;
    params.tolerance = 0.02;  // C -> D keeps 3.06%
    CHECK(findChains(g, params).longest == 2);
    params.tolerance = 0.6;  // now D -> E joins
    CHECK(findChains(g, params).longest == 4);

    // ---- chains, 3000 random graphs vs brute-force enumeration ----
    std::mt19937 rng(777);
    long long flaggedTotal = 0;
    int chainMismatches = 0;
    for (int trial = 0; trial < 3000; trial++) {
        Graph r;
        int n = 3 + rng() % 4;
        int m = 4 + rng() % 12;
        for (int i = 0; i < n; i++) r.getVertex(std::to_string(i));
        for (int i = 0; i < m; i++) r.addEdge(rng() % n, rng() % n, 80 + rng() % 21, rng() % 8, false);
        r.finalize();
        ChainParams p;
        p.minLength = 2 + rng() % 3;
        p.maxDelay = rng() % 6;
        p.tolerance = (rng() % 4) * 0.1;
        std::vector<int> best(m, 0), seq;
        for (int i = 0; i < m; i++) {
            if (r.edges[i].from == r.edges[i].to) continue;
            seq.assign(1, i);
            bruteChains(r, p, seq, best);
        }
        ChainResult fast = findChains(r, p);
        for (int i = 0; i < m; i++) {
            bool selfLoop = r.edges[i].from == r.edges[i].to;
            if (!selfLoop && fast.back[i] + fast.fwd[i] - 1 != best[i]) chainMismatches++;
            if ((bool)fast.flaggedEdge[i] != (!selfLoop && best[i] >= p.minLength)) chainMismatches++;
            if (!selfLoop) {
                std::vector<int> through = fast.chainThrough(i);
                if ((int)through.size() != best[i]) chainMismatches++;
                for (int x = 1; x < (int)through.size(); x++)
                    if (!follows(r, through[x - 1], through[x], p)) chainMismatches++;
            }
            flaggedTotal += fast.flaggedEdge[i];
        }
    }
    CHECK(chainMismatches == 0 && flaggedTotal > 0);

    // ---- peeling, hand-made: 4-clique {A,B,C,D} with tail D - E - F, plus a reverse
    //      transfer, a parallel transfer and a self loop that must all collapse.
    Graph k;
    int v[6];
    for (int i = 0; i < 6; i++) v[i] = k.getVertex(std::string(1, (char)('A' + i)));
    long long t = 0;
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++) k.addEdge(v[i], v[j], 100, t++, true);
    k.addEdge(v[1], v[0], 100, t++, true);  // reverse
    k.addEdge(v[0], v[1], 100, t++, true);  // parallel
    k.addEdge(v[2], v[2], 100, t++, false); // self loop
    k.addEdge(v[3], v[4], 100, t++, false);
    k.addEdge(v[4], v[5], 100, t++, false);
    k.finalize();
    UndirectedGraph h = buildUndirected(k);
    CHECK(h.n == 6 && h.m == 8);
    PeelResult peeled = peel(h);
    CHECK(peeled.maxCore == 3);
    CHECK(peeled.coreNumber[v[0]] == 3 && peeled.coreNumber[v[4]] == 1 && peeled.coreNumber[v[5]] == 1);
    CHECK(peeled.bestEdges == 6 && peeled.bestVertices == 4);  // the clique, density 1.5
    countFlagged(k, edgesInside(k, peeled.inDensest), flagged, laundering);
    CHECK(flagged == 8 && laundering == 8);  // 6 clique transfers + reverse + parallel

    // ---- peeling, 3000 random graphs: core numbers vs naive, 1/2 bound vs brute force ----
    int exactHits = 0, peelMismatches = 0;
    double worstRatio = 1.0;
    for (int trial = 0; trial < 3000; trial++) {
        Graph r;
        int n = 2 + rng() % 9;
        int m = 1 + rng() % (3 * n);
        for (int i = 0; i < n; i++) r.getVertex(std::to_string(i));
        for (int i = 0; i < m; i++) r.addEdge(rng() % n, rng() % n, 1.0, i, false);
        r.finalize();
        UndirectedGraph u = buildUndirected(r);
        PeelResult p = peel(u);
        if (p.coreNumber != naiveCores(u)) peelMismatches++;
        int mask = 0, size = 0;
        for (int x = 0; x < n; x++) if (p.inDensest[x]) { mask |= 1 << x; size++; }
        if (size != p.bestVertices || edgesWithin(u, mask) != p.bestEdges) peelMismatches++;
        long long optEdges;
        int optVertices;
        bruteDensest(u, optEdges, optVertices);
        if (p.bestEdges * optVertices > optEdges * p.bestVertices) peelMismatches++;      // greedy <= opt
        if (2 * p.bestEdges * optVertices < optEdges * p.bestVertices) peelMismatches++;  // greedy >= opt/2
        if (p.bestEdges * optVertices == optEdges * p.bestVertices) exactHits++;
        if (optEdges > 0) worstRatio = std::min(worstRatio, p.density() / ((double)optEdges / optVertices));
    }
    CHECK(peelMismatches == 0);

    bool ok = checksFailed == failedBefore;
    std::printf("[member 3] chain + peeling tests: %s (greedy optimal on %d/3000 graphs, worst ratio %.3f)\n",
                ok ? "PASS" : "FAIL", exactHits, worstRatio);
    return ok;
}

#endif  // AML_MEMBER3_CPP

#ifdef AML_STANDALONE_3
int main() {
    bool ok = testMember1();
    ok = testMember2() && ok;
    ok = testMember3() && ok;
    return ok ? 0 : 1;
}
#endif
