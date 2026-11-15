// =============================================================================
//  exact_eval.cpp                                   Module 4 - Shatadeep De
//
//  1. Maximum flow with the Edmonds-Karp algorithm (BFS augmenting paths)
//  2. Exact densest subgraph, using minimum cuts
//  3. Evaluation: precision, recall and F1 score
// =============================================================================
#pragma once
#include "chains_peeling.cpp"

// ----------------------------------------------------------------------------
// 1. Edmonds-Karp maximum flow.  O(V * E^2)
//
//    While there is a path from s to t in the residual graph:
//      - find the SHORTEST such path with BFS,
//      - push as much flow as its smallest residual capacity allows.
//    Every arc is stored next to its reverse arc (positions i and i^1), so
//    "undoing" flow is just adding capacity to the reverse arc.
//    When no path is left, the vertices still reachable from s form the source
//    side of a minimum cut (max-flow min-cut theorem).
// ----------------------------------------------------------------------------

struct MaxFlow {
    struct Arc {
        int to;
        long long cap;  // remaining (residual) capacity
    };
    int n;
    vector<Arc> arcs;
    vector<vector<int>> adj;  // adj[u] = indices of arcs leaving u

    MaxFlow(int vertices) : n(vertices), adj(vertices) {}

    // Arc u -> v with capacity cap, and its reverse v -> u with capacity revCap.
    void addArc(int u, int v, long long cap, long long revCap = 0) {
        adj[u].push_back((int)arcs.size());
        arcs.push_back({v, cap});
        adj[v].push_back((int)arcs.size());
        arcs.push_back({u, revCap});
    }

    long long run(int s, int t) {
        long long flow = 0;
        while (true) {
            // BFS from s; parentArc[v] = arc used to reach v
            vector<int> parentArc(n, -1);
            vector<char> seen(n, 0);
            vector<int> queue = {s};
            seen[s] = 1;
            for (int head = 0; head < (int)queue.size() && !seen[t]; head++) {
                int u = queue[head];
                for (int id : adj[u]) {
                    int v = arcs[id].to;
                    if (arcs[id].cap > 0 && !seen[v]) {
                        seen[v] = 1;
                        parentArc[v] = id;
                        queue.push_back(v);
                    }
                }
            }
            if (!seen[t]) break;  // no augmenting path left: flow is maximum

            // smallest residual capacity on the path (walk back from t to s)
            long long push = LLONG_MAX;
            for (int v = t; v != s; v = arcs[parentArc[v] ^ 1].to) push = min(push, arcs[parentArc[v]].cap);
            // send the flow
            for (int v = t; v != s; v = arcs[parentArc[v] ^ 1].to) {
                arcs[parentArc[v]].cap -= push;
                arcs[parentArc[v] ^ 1].cap += push;
            }
            flow += push;
        }
        return flow;
    }

    // After run(): vertices reachable from s using arcs with capacity left.
    vector<char> sourceSide(int s) const {
        vector<char> seen(n, 0);
        vector<int> queue = {s};
        seen[s] = 1;
        for (int head = 0; head < (int)queue.size(); head++)
            for (int id : adj[queue[head]])
                if (arcs[id].cap > 0 && !seen[arcs[id].to]) {
                    seen[arcs[id].to] = 1;
                    queue.push_back(arcs[id].to);
                }
        return seen;
    }
};

// ----------------------------------------------------------------------------
// 2. Exact densest subgraph (Goldberg's construction)
//
//    Question: "is there a set S with density > p/q ?"
//    Build this flow network on s, t and the m-edge graph's vertices:
//        s -> v          capacity  m*q
//        v -> t          capacity  m*q + 2p - deg(v)*q
//        u -> v, v -> u  capacity  q                 (for every edge {u, v})
//    The cut with S on the source side has capacity
//        m*n*q + 2 * (p*|S| - q*edges(S)),
//    so the minimum cut picks the S that makes  q*edges(S) - p*|S|  as large as
//    possible. If that is > 0, S is denser than p/q.
//
//    Algorithm:
//      start with p/q = density of the greedy set from peeling;
//      repeat: find the min cut; if its set is denser, make that the new p/q;
//      stop when no denser set exists -> the current set is the densest.
//
//    Speed-up: the densest set lies inside the k-core with k = ceil(greedy
//    density), so the network only needs those vertices.
// ----------------------------------------------------------------------------

struct DensestResult {
    long long edges = 0;      // edges inside the densest set
    int vertices = 0;         // size of the densest set
    vector<char> inSet;       // inSet[v] = 1 if v is in the densest set
    int minCuts = 0;          // how many min cuts we computed
    int networkVertices = 0;  // size of the graph after the k-core speed-up
    long long networkEdges = 0;

    double density() const { return vertices == 0 ? 0.0 : (double)edges / vertices; }
};

DensestResult exactDensest(const SimpleGraph& h, const PeelResult& greedy) {
    DensestResult res;
    res.edges = greedy.bestEdges;
    res.vertices = greedy.bestVertices;
    res.inSet = greedy.inDensest;
    if (h.m == 0) return res;

    // keep only the vertices of the k-core, k = ceil(edges / vertices); renumber them 0..n-1
    int k = (int)((greedy.bestEdges + greedy.bestVertices - 1) / greedy.bestVertices);
    vector<int> newId(h.n, -1), oldId;
    for (int v = 0; v < h.n; v++)
        if (greedy.core[v] >= k) {
            newId[v] = (int)oldId.size();
            oldId.push_back(v);
        }
    int n = (int)oldId.size();
    vector<int> deg(n, 0);
    long long m = 0;
    for (int i = 0; i < n; i++)
        for (int u : h.adj[oldId[i]])
            if (newId[u] != -1) {
                deg[i]++;
                if (newId[u] > i) m++;  // count each edge once
            }
    res.networkVertices = n;
    res.networkEdges = m;
    if (m == 0) return res;

    long long p = res.edges, q = res.vertices;  // current best density p/q
    int s = n, t = n + 1;
    while (true) {
        MaxFlow flow(n + 2);
        for (int i = 0; i < n; i++) {
            flow.addArc(s, i, m * q);
            flow.addArc(i, t, m * q + 2 * p - deg[i] * q);
            for (int u : h.adj[oldId[i]])
                if (newId[u] > i) flow.addArc(i, newId[u], q, q);
        }
        flow.run(s, t);
        res.minCuts++;
        vector<char> side = flow.sourceSide(s);

        // size and edge count of the set S on the source side
        long long setEdges = 0;
        int setVertices = 0;
        for (int i = 0; i < n; i++) {
            if (!side[i]) continue;
            setVertices++;
            for (int u : h.adj[oldId[i]])
                if (newId[u] > i && side[newId[u]]) setEdges++;
        }
        // stop if S is empty or not denser:  setEdges/setVertices <= p/q
        if (setVertices == 0 || setEdges * q <= p * setVertices) break;

        res.edges = p = setEdges;
        res.vertices = setVertices;
        q = setVertices;
        fill(res.inSet.begin(), res.inSet.end(), 0);
        for (int i = 0; i < n; i++)
            if (side[i]) res.inSet[oldId[i]] = 1;
    }
    return res;
}

// ----------------------------------------------------------------------------
// 3. Evaluation
//    A detector "flags" transfers. Compare with the "Is Laundering" label:
//      precision = correct flags / all flags
//      recall    = correct flags / all laundering transfers
//      F1        = 2 * precision * recall / (precision + recall)
// ----------------------------------------------------------------------------

struct Score {
    long long flagged = 0, correct = 0;
    double precision = 0, recall = 0, f1 = 0;
};

Score score(const Graph& g, const vector<char>& flagged) {
    long long positives = 0;
    for (const Edge& e : g.edges) positives += e.isLaundering;

    Score sc;
    countFlagged(g, flagged, sc.flagged, sc.correct);
    if (sc.flagged > 0) sc.precision = (double)sc.correct / sc.flagged;
    if (positives > 0) sc.recall = (double)sc.correct / positives;
    if (sc.precision + sc.recall > 0) sc.f1 = 2 * sc.precision * sc.recall / (sc.precision + sc.recall);
    return sc;
}

// A graph with only the first `count` transfers (in time order), used to
// measure how the running time grows with the size of the input.
Graph firstTransfers(const Graph& g, int count) {
    Graph part;
    part.n = g.n;
    part.edges.assign(g.edges.begin(), g.edges.begin() + count);
    part.build();
    return part;
}

// ----------------------------------------------------------------------------
// Tests for this module
// ----------------------------------------------------------------------------

SimpleGraph simpleGraphFrom(int n, const vector<pair<int, int>>& pairs) {
    Graph g;
    for (int i = 0; i < n; i++) g.getId(to_string(i));
    long long t = 0;
    for (auto [u, v] : pairs) g.addEdge(u, v, 1, t++, false);
    g.build();
    return makeUndirected(g);
}

void testExactEval() {
    printf("Module 4: max flow + exact densest subgraph + evaluation\n");

    // Classic example: s=0, t=5.  Max flow is 23 (CLRS Figure 26.1).
    MaxFlow f(6);
    f.addArc(0, 1, 16);
    f.addArc(0, 2, 13);
    f.addArc(2, 1, 4);
    f.addArc(1, 3, 12);
    f.addArc(3, 2, 9);
    f.addArc(2, 4, 14);
    f.addArc(4, 3, 7);
    f.addArc(3, 5, 20);
    f.addArc(4, 5, 4);
    check(f.run(0, 5) == 23, "textbook network: max flow = 23");
    vector<char> side = f.sourceSide(0);
    check(side[0] && side[1] && side[2] && side[4] && !side[3] && !side[5], "min cut is {s,1,2,4} | {3,t}");

    // 4-clique {0,1,2,3} with a tail 3 - 4 - 5: densest set is the clique.
    SimpleGraph h = simpleGraphFrom(6, {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}, {3, 4}, {4, 5}});
    DensestResult d = exactDensest(h, peel(h));
    check(d.edges == 6 && d.vertices == 4 && !d.inSet[4], "densest set is the 4-clique (density 1.5)");

    // Two "bankers" 0 and 1 both trading with accounts 2..7 (density 12/8 = 1.5),
    // next to three separate triangles (density 1 each).
    vector<pair<int, int>> pairs;
    for (int v = 2; v <= 7; v++) {
        pairs.push_back({0, v});
        pairs.push_back({1, v});
    }
    for (int base = 8; base <= 14; base += 3) {
        pairs.push_back({base, base + 1});
        pairs.push_back({base + 1, base + 2});
        pairs.push_back({base, base + 2});
    }
    SimpleGraph k = simpleGraphFrom(17, pairs);
    DensestResult kd = exactDensest(k, peel(k));
    check(kd.edges == 12 && kd.vertices == 8, "densest set is the bipartite ring (12 edges / 8 vertices)");

    // Scoring: 2 laundering transfers, we flag one of them and one normal one.
    Graph g;
    for (int i = 0; i < 4; i++) g.getId(to_string(i));
    g.addEdge(0, 1, 1, 0, true);
    g.addEdge(1, 2, 1, 1, true);
    g.addEdge(2, 3, 1, 2, false);
    g.addEdge(3, 0, 1, 3, false);
    g.build();
    Score sc = score(g, {1, 0, 1, 0});
    check(sc.flagged == 2 && sc.correct == 1, "2 flagged, 1 correct");
    check(sc.precision == 0.5 && sc.recall == 0.5 && sc.f1 == 0.5, "precision = recall = F1 = 0.5");
}
