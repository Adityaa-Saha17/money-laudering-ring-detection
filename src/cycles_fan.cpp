// =====================================================================================
//  Money-Laundering Ring Detector
//  cycles_fan.cpp
//
//  Bounded-length temporal cycles (round-tripping) and fan-out / fan-in (smurfing).
//
//  Builds on ingest_scc.cpp (Graph, tarjanScc).
//  Build this module alone:  clang++ -std=c++17 -O2 cycles_fan.cpp -o cycles_fan && ./cycles_fan
// =====================================================================================
#ifndef AML_INCLUDED
#define AML_STANDALONE_CYCLES_FAN
#endif
#define AML_INCLUDED
#include "ingest_scc.cpp"

#ifndef AML_CYCLES_FAN_CPP
#define AML_CYCLES_FAN_CPP

// ------------------------------- temporal cycles ------------------------------------

struct CycleParams {
    int maxLength = 6;               // K: at most K edges per cycle
    long long window = 7 * 86400LL;  // W: last edge at most W seconds after the first
    long long maxCycles = 0;         // stop after this many cycles (0 = no limit)
};

// A temporal cycle is a list of edge indices e1..ek (2 <= k <= K) such that
//   1. consecutive edges are head-to-tail and ek returns to the tail of e1,
//   2. the k tails are distinct vertices,
//   3. timestamps are non-decreasing along the cycle,
//   4. timestamp(ek) - timestamp(e1) <= W,
//   5. e1 has the smallest edge index (so each cycle is reported exactly once).
struct CycleFinder {
    const Graph& g;
    const SccResult& scc;
    CycleParams params;
    std::vector<std::vector<int>> cycles;
    std::vector<char> onPath;
    std::vector<int> path;  // edge indices of the current path
    int rootIndex = 0;
    int start = 0;
    long long deadline = 0;
    bool stopped = false;

    CycleFinder(const Graph& graph, const SccResult& sccResult, CycleParams p)
        : g(graph), scc(sccResult), params(p), onPath(graph.n, 0) {}

    // Depth is at most K, so this recursion is shallow.
    void extend(int u, long long lastTime) {
        const std::vector<int>& adj = g.outAdj[u];
        int deg = (int)adj.size();
        for (int pos = g.firstOutAtOrAfter(u, lastTime); pos < deg && !stopped; pos++) {
            int idx = adj[pos];
            const Edge& e = g.edges[idx];
            if (e.timestamp > deadline) break;  // adjacency list is time-sorted
            if (idx <= rootIndex) continue;     // root must be the smallest index
            if (e.to == start) {                // closes the cycle
                path.push_back(idx);
                cycles.push_back(path);
                path.pop_back();
                if (params.maxCycles > 0 && (long long)cycles.size() >= params.maxCycles) stopped = true;
                continue;
            }
            if (onPath[e.to]) continue;                               // vertices must be distinct
            if (scc.comp[e.to] != scc.comp[start]) continue;          // cycle stays inside one SCC
            if ((int)path.size() + 1 >= params.maxLength) continue;   // no room left to close
            onPath[e.to] = 1;
            path.push_back(idx);
            extend(e.to, e.timestamp);
            path.pop_back();
            onPath[e.to] = 0;
        }
    }

    void run() {
        int m = (int)g.edges.size();
        for (int i = 0; i < m && !stopped; i++) {
            const Edge& e = g.edges[i];
            if (e.from == e.to) continue;                      // self loop
            if (scc.comp[e.from] != scc.comp[e.to]) continue;  // cannot lie on any cycle
            rootIndex = i;
            start = e.from;
            deadline = e.timestamp + params.window;
            onPath[e.from] = 1;
            onPath[e.to] = 1;
            path.push_back(i);
            extend(e.to, e.timestamp);
            path.pop_back();
            onPath[e.from] = 0;
            onPath[e.to] = 0;
        }
    }
};

std::vector<std::vector<int>> findTemporalCycles(const Graph& g, const SccResult& scc, CycleParams params,
                                                 bool* truncated = nullptr) {
    CycleFinder finder(g, scc, params);
    finder.run();
    if (truncated) *truncated = finder.stopped;
    return std::move(finder.cycles);
}

std::vector<char> cycleEdges(const Graph& g, const std::vector<std::vector<int>>& cycles) {
    std::vector<char> flagged(g.edges.size(), 0);
    for (const std::vector<int>& cycle : cycles)
        for (int idx : cycle) flagged[idx] = 1;
    return flagged;
}

// ------------------------------- fan-out / fan-in -----------------------------------

struct FanParams {
    int minDistinct = 5;         // k: at least k distinct counterparties
    long long window = 86400LL;  // W: all within W seconds
};

struct FanHit {
    int vertex;
    int bestDistinct;  // largest number of distinct counterparties in any window
    int firstEdge;     // edge indices bounding that best window
    int lastEdge;
};

struct FanResult {
    std::vector<FanHit> hits;       // one per flagged centre vertex
    std::vector<char> flaggedEdge;  // flaggedEdge[i] = 1 iff edge i lies in a qualifying window
};

// Fan-out (outgoing = true): centre u sends to >= k distinct accounts within W.
// Fan-in  (outgoing = false): centre u receives from >= k distinct accounts within W.
// Two-pointer sliding window over each time-sorted adjacency list. O(n + m).
FanResult findFans(const Graph& g, FanParams params, bool outgoing) {
    FanResult res;
    res.flaggedEdge.assign(g.edges.size(), 0);
    std::vector<int> count(g.n, 0);  // count[w] = edges to/from w inside the current window

    for (int u = 0; u < g.n; u++) {
        const std::vector<int>& adj = outgoing ? g.outAdj[u] : g.inAdj[u];
        int deg = (int)adj.size();
        if (deg < params.minDistinct) continue;

        int left = 0, distinct = 0, markedUpTo = -1;
        FanHit hit = {u, 0, -1, -1};
        for (int right = 0; right < deg; right++) {
            const Edge& e = g.edges[adj[right]];
            int other = outgoing ? e.to : e.from;
            if (other != u && count[other]++ == 0) distinct++;  // self loops never count

            while (e.timestamp - g.edges[adj[left]].timestamp > params.window) {
                const Edge& old = g.edges[adj[left]];
                int oldOther = outgoing ? old.to : old.from;
                if (oldOther != u && --count[oldOther] == 0) distinct--;
                left++;
            }

            if (distinct >= params.minDistinct) {
                for (int i = std::max(left, markedUpTo + 1); i <= right; i++) {
                    const Edge& f = g.edges[adj[i]];
                    if (f.from != f.to) res.flaggedEdge[adj[i]] = 1;
                }
                markedUpTo = right;
                if (distinct > hit.bestDistinct) {
                    hit.bestDistinct = distinct;
                    hit.firstEdge = adj[left];
                    hit.lastEdge = adj[right];
                }
            }
        }
        for (int i = left; i < deg; i++) {  // reset only the counters this vertex touched
            const Edge& e = g.edges[adj[i]];
            count[outgoing ? e.to : e.from] = 0;
        }
        if (hit.bestDistinct > 0) res.hits.push_back(hit);
    }
    return res;
}

// ------------------------------- report section -------------------------------------

void reportCyclesFan(const Graph& g, const SccResult& scc, const CycleParams& cycleParams, const FanParams& fanParams) {
    std::printf("== Temporal cycles and fan-out / fan-in ==\n");
    auto start = std::chrono::steady_clock::now();
    bool truncated = false;
    std::vector<std::vector<int>> cycles = findTemporalCycles(g, scc, cycleParams, &truncated);
    double cycleSeconds = secondsSince(start);
    long long total, laundering;
    countFlagged(g, cycleEdges(g, cycles), total, laundering);
    std::printf("cycle params                K=%d W=%lldh\n", cycleParams.maxLength, cycleParams.window / 3600);
    std::printf("temporal cycles             %zu%s\n", cycles.size(), truncated ? " (stopped at maxCycles)" : "");
    std::printf("transfers on a cycle        %lld (labelled laundering: %lld)\n", total, laundering);
    std::printf("cycle time (s)              %.3f\n", cycleSeconds);

    std::printf("fan params                  k=%d W=%lldh\n", fanParams.minDistinct, fanParams.window / 3600);
    for (int dir = 0; dir < 2; dir++) {
        start = std::chrono::steady_clock::now();
        FanResult fan = findFans(g, fanParams, dir == 0);
        double fanSeconds = secondsSince(start);
        countFlagged(g, fan.flaggedEdge, total, laundering);
        std::printf("%s centres / transfers   %zu / %lld (labelled laundering: %lld), %.3f s\n",
                    dir == 0 ? "fan-out" : "fan-in ", fan.hits.size(), total, laundering, fanSeconds);
    }
    std::printf("\n");
}

// ------------------------------- self-tests -----------------------------------------

// Brute force cycles: try every ordered sequence of up to K distinct edges and test the
// five conditions of the definition directly. Shares no pruning with CycleFinder.
void bruteCycles(const Graph& g, const CycleParams& p, std::vector<int>& seq, std::set<std::vector<int>>& found) {
    int k = (int)seq.size();
    if (k >= 2) {
        bool ok = g.edges[seq[k - 1]].to == g.edges[seq[0]].from;
        std::set<int> tails;
        for (int i = 0; i < k && ok; i++) {
            const Edge& e = g.edges[seq[i]];
            if (i > 0 && g.edges[seq[i - 1]].to != e.from) ok = false;
            if (i > 0 && g.edges[seq[i - 1]].timestamp > e.timestamp) ok = false;
            if (i > 0 && seq[i] < seq[0]) ok = false;
            if (!tails.insert(e.from).second) ok = false;
        }
        if (ok && g.edges[seq[k - 1]].timestamp - g.edges[seq[0]].timestamp > p.window) ok = false;
        if (ok) found.insert(seq);
    }
    if (k == p.maxLength) return;
    for (int i = 0; i < (int)g.edges.size(); i++) {
        if (std::find(seq.begin(), seq.end(), i) != seq.end()) continue;
        seq.push_back(i);
        bruteCycles(g, p, seq, found);
        seq.pop_back();
    }
}

// Brute force fans: try every pair of positions (i, j) in a vertex's adjacency list.
FanResult bruteFans(const Graph& g, FanParams p, bool outgoing) {
    FanResult res;
    res.flaggedEdge.assign(g.edges.size(), 0);
    for (int u = 0; u < g.n; u++) {
        const std::vector<int>& adj = outgoing ? g.outAdj[u] : g.inAdj[u];
        int best = 0;
        for (int i = 0; i < (int)adj.size(); i++) {
            for (int j = i; j < (int)adj.size(); j++) {
                if (g.edges[adj[j]].timestamp - g.edges[adj[i]].timestamp > p.window) continue;
                std::set<int> others;
                for (int x = i; x <= j; x++) {
                    const Edge& e = g.edges[adj[x]];
                    int other = outgoing ? e.to : e.from;
                    if (other != u) others.insert(other);
                }
                if ((int)others.size() < p.minDistinct) continue;
                best = std::max(best, (int)others.size());
                for (int x = i; x <= j; x++)
                    if (g.edges[adj[x]].from != g.edges[adj[x]].to) res.flaggedEdge[adj[x]] = 1;
            }
        }
        if (best > 0) res.hits.push_back({u, best, -1, -1});
    }
    return res;
}

bool testCyclesFan() {
    int failedBefore = checksFailed;

    // ---- cycles, hand-made: 3-cycle A->B (10), B->C (20), C->A (30), late parallel A->B (500),
    //      tail C->D->E, self loop E->E, D->F.
    Graph g;
    int a = g.getVertex("A"), b = g.getVertex("B"), c = g.getVertex("C");
    int d = g.getVertex("D"), e = g.getVertex("E"), f = g.getVertex("F");
    g.addEdge(c, a, 90, 30, true);
    g.addEdge(a, b, 100, 10, true);
    g.addEdge(b, c, 95, 20, true);
    g.addEdge(a, b, 50, 500, false);
    g.addEdge(c, d, 10, 1000, false);
    g.addEdge(d, e, 5, 1100, false);
    g.addEdge(e, e, 5, 1200, false);
    g.addEdge(d, f, 4, 2000, false);
    g.finalize();
    SccResult scc = tarjanScc(g);

    CycleParams params;
    params.maxLength = 6;
    params.window = 100;
    std::vector<std::vector<int>> cycles = findTemporalCycles(g, scc, params);
    CHECK(cycles.size() == 1 && cycles[0].size() == 3);  // the late rotation spans 480 s
    CHECK(g.edges[cycles[0][0]].timestamp == 10 && g.edges[cycles[0][2]].timestamp == 30);
    params.window = 480;
    CHECK(findTemporalCycles(g, scc, params).size() == 2);
    params.window = 100;
    params.maxLength = 2;  // K too small
    CHECK(findTemporalCycles(g, scc, params).empty());
    params.maxLength = 3;
    params.window = 19;  // W too small: the cycle spans 20 seconds
    CHECK(findTemporalCycles(g, scc, params).empty());
    params.window = 20;
    CHECK(findTemporalCycles(g, scc, params).size() == 1);

    // ---- cycles, 400 random graphs vs brute force (many timestamp ties) ----
    std::mt19937 rng(12345);
    long long totalCycles = 0;
    int cycleMismatches = 0;
    for (int trial = 0; trial < 400; trial++) {
        Graph r;
        int n = 3 + rng() % 4;
        int m = 4 + rng() % 8;
        for (int i = 0; i < n; i++) r.getVertex(std::to_string(i));
        for (int i = 0; i < m; i++) r.addEdge(rng() % n, rng() % n, 1.0, rng() % 6, false);
        r.finalize();
        SccResult s = tarjanScc(r);
        CycleParams p;
        p.maxLength = 2 + rng() % 3;
        p.window = rng() % 6;
        std::vector<std::vector<int>> fast = findTemporalCycles(r, s, p);
        std::set<std::vector<int>> fastSet(fast.begin(), fast.end());
        std::set<std::vector<int>> brute;
        std::vector<int> seq;
        bruteCycles(r, p, seq, brute);
        if (fastSet.size() != fast.size() || fastSet != brute) cycleMismatches++;  // no duplicates, same set
        totalCycles += (long long)fast.size();
    }
    CHECK(cycleMismatches == 0 && totalCycles > 0);

    // ---- fans, hand-made: H -> R1..R4 at 0,10,20,30 and H -> R1 again at 40,
    //      H -> R5 two days later; R1..R4 -> S at 100..130.
    Graph h;
    int hub = h.getVertex("H"), sink = h.getVertex("S");
    int r[6];
    for (int i = 1; i <= 5; i++) r[i] = h.getVertex("R" + std::to_string(i));
    for (int i = 1; i <= 4; i++) h.addEdge(hub, r[i], 900, (i - 1) * 10, true);
    h.addEdge(hub, r[1], 900, 40, true);
    h.addEdge(hub, r[5], 900, 2 * 86400, false);
    for (int i = 1; i <= 4; i++) h.addEdge(r[i], sink, 890, 100 + (i - 1) * 10, true);
    h.finalize();

    FanParams fanParams;
    fanParams.minDistinct = 4;
    fanParams.window = 3600;
    FanResult out = findFans(h, fanParams, true);
    CHECK(out.hits.size() == 1 && out.hits[0].vertex == hub && out.hits[0].bestDistinct == 4);
    long long flagged, laundering;
    countFlagged(h, out.flaggedEdge, flagged, laundering);
    CHECK(flagged == 5 && laundering == 5);  // four recipients plus the parallel edge
    FanResult in = findFans(h, fanParams, false);
    CHECK(in.hits.size() == 1 && in.hits[0].vertex == sink && in.hits[0].bestDistinct == 4);
    fanParams.minDistinct = 5;  // 5 recipients overall, but never 5 within an hour
    CHECK(findFans(h, fanParams, true).hits.empty());
    fanParams.window = 3 * 86400;
    CHECK(findFans(h, fanParams, true).hits.size() == 1);
    fanParams.minDistinct = 4;
    fanParams.window = 29;  // the four sends span 30 seconds
    CHECK(findFans(h, fanParams, true).hits.empty());

    // ---- fans, 2000 random graphs vs brute force ----
    long long totalHits = 0;
    int fanMismatches = 0;
    for (int trial = 0; trial < 2000; trial++) {
        Graph q;
        int n = 3 + rng() % 5;
        int m = 5 + rng() % 20;
        for (int i = 0; i < n; i++) q.getVertex(std::to_string(i));
        for (int i = 0; i < m; i++) q.addEdge(rng() % n, rng() % n, 1.0, rng() % 12, false);
        q.finalize();
        FanParams p;
        p.minDistinct = 1 + rng() % 4;
        p.window = rng() % 8;
        for (int dir = 0; dir < 2; dir++) {
            FanResult fast = findFans(q, p, dir == 0);
            FanResult brute = bruteFans(q, p, dir == 0);
            bool same = fast.flaggedEdge == brute.flaggedEdge && fast.hits.size() == brute.hits.size();
            for (int i = 0; same && i < (int)fast.hits.size(); i++)
                same = fast.hits[i].vertex == brute.hits[i].vertex &&
                       fast.hits[i].bestDistinct == brute.hits[i].bestDistinct;
            if (!same) fanMismatches++;
            totalHits += (long long)fast.hits.size();
        }
    }
    CHECK(fanMismatches == 0 && totalHits > 0);

    bool ok = checksFailed == failedBefore;
    std::printf("[cycles_fan] cycle + fan tests: %s (%lld cycles and %lld fan centres vs brute force)\n",
                ok ? "PASS" : "FAIL", totalCycles, totalHits);
    return ok;
}

#endif  // AML_CYCLES_FAN_CPP

#ifdef AML_STANDALONE_CYCLES_FAN
int main() {
    bool ok = testIngestScc();
    ok = testCyclesFan() && ok;
    return ok ? 0 : 1;
}
#endif
