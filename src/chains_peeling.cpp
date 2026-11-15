// =============================================================================
//  chains_peeling.cpp                               Module 3 - Jeeban Jyoti Patra
//
//  1. Pass-through chains (layering: money is forwarded hop after hop)
//     -> dynamic programming = longest path in a DAG
//  2. Undirected simple graph built from the transfers
//  3. k-core decomposition and greedy densest subgraph
//     -> repeatedly remove the vertex of minimum degree (priority queue)
// =============================================================================
#pragma once
#include "cycles_fan.cpp"

// ----------------------------------------------------------------------------
// 1. Pass-through chains
//
//    Transfer f FOLLOWS transfer e when the receiver of e quickly passes the
//    money on, keeping only a small cut:
//      - f is sent by the account that received e,
//      - f comes after e in time order, at most `maxDelay` seconds later,
//      - (1 - tolerance) * amount(e)  <=  amount(f)  <=  amount(e).
//    A chain is a sequence of transfers where each one follows the previous one.
//
//    "follows" only points forward in time, so it never loops back: the transfers
//    form a DAG, and the edge list (already sorted by time) is a topological order.
//    Longest chain = longest path in a DAG, solved by dynamic programming:
//      before[f] = 1 + max(before[e])  over all e that f follows
//      after[e]  = 1 + max(after[f])   over all f that follow e
//    The longest chain THROUGH e has before[e] + after[e] - 1 transfers.
// ----------------------------------------------------------------------------

bool follows(const Graph& g, int e, int f, long long maxDelay, double tolerance) {
    const Edge& x = g.edges[e];
    const Edge& y = g.edges[f];
    if (f <= e) return false;                       // f must come later
    if (y.from != x.to) return false;               // f must start where e ended
    if (y.time - x.time > maxDelay) return false;   // forwarded quickly
    return y.amount <= x.amount && y.amount >= (1.0 - tolerance) * x.amount;
}

struct ChainResult {
    vector<int> before;    // before[e] = transfers in the longest chain ending at e
    vector<int> after;     // after[e]  = transfers in the longest chain starting at e
    vector<char> flagged;  // flagged[e] = 1 if e is on a chain with >= minLength transfers
    int longest = 0;       // length of the longest chain
};

ChainResult findChains(const Graph& g, int minLength, long long maxDelay, double tolerance) {
    int m = (int)g.edges.size();
    ChainResult res;
    res.before.assign(m, 1);  // every transfer alone is a chain of length 1
    res.after.assign(m, 1);
    res.flagged.assign(m, 0);

    // before[]: go forwards in time. The transfers f can follow were received by
    // f.from in the last maxDelay seconds; binary search finds the first of them.
    for (int f = 0; f < m; f++) {
        const vector<int>& list = g.inEdges[g.edges[f].from];
        for (int i = firstAtOrAfter(g, list, g.edges[f].time - maxDelay); i < (int)list.size(); i++) {
            int e = list[i];
            if (e >= f) break;
            if (follows(g, e, f, maxDelay, tolerance)) res.before[f] = max(res.before[f], res.before[e] + 1);
        }
    }
    // after[]: go backwards in time. The transfers that can follow e are sent by
    // e.to in the next maxDelay seconds.
    for (int e = m - 1; e >= 0; e--) {
        const vector<int>& list = g.outEdges[g.edges[e].to];
        for (int i = firstAtOrAfter(g, list, g.edges[e].time); i < (int)list.size(); i++) {
            int f = list[i];
            if (g.edges[f].time - g.edges[e].time > maxDelay) break;
            if (follows(g, e, f, maxDelay, tolerance)) res.after[e] = max(res.after[e], res.after[f] + 1);
        }
    }
    for (int e = 0; e < m; e++) {
        if (res.before[e] + res.after[e] - 1 >= minLength) res.flagged[e] = 1;
        res.longest = max(res.longest, res.before[e]);
    }
    return res;
}

// ----------------------------------------------------------------------------
// 2. Undirected simple graph
//    {u, v} is an edge if there is at least one transfer between u and v in
//    either direction. Duplicates are removed by sorting.  O(m log m)
// ----------------------------------------------------------------------------

struct SimpleGraph {
    int n = 0;
    long long m = 0;
    vector<vector<int>> adj;
};

SimpleGraph makeUndirected(const Graph& g) {
    vector<pair<int, int>> pairs;
    for (const Edge& e : g.edges) pairs.push_back({min(e.from, e.to), max(e.from, e.to)});
    sort(pairs.begin(), pairs.end());
    pairs.erase(unique(pairs.begin(), pairs.end()), pairs.end());

    SimpleGraph h;
    h.n = g.n;
    h.m = (long long)pairs.size();
    h.adj.assign(g.n, {});
    for (auto [u, v] : pairs) {
        h.adj[u].push_back(v);
        h.adj[v].push_back(u);
    }
    return h;
}

// ----------------------------------------------------------------------------
// 3. k-core decomposition and greedy densest subgraph (peeling)
//
//    density(S) = (edges with both ends in S) / |S|
//    The k-core is the largest set where every vertex has at least k neighbours
//    inside the set. Groups of accounts that all trade with each other form a
//    high core and a dense set.
//
//    Peeling: repeatedly remove the vertex with the smallest degree.
//      - core number of v = largest "smallest degree" seen up to v's removal
//      - after each removal, check the density of what is left; the best one is
//        the greedy densest set (proven to be at least half the best density).
//
//    The priority queue is a std::set of (degree, vertex) pairs, so we can find
//    the minimum and also change a vertex's degree.  O(m log n)
// ----------------------------------------------------------------------------

struct PeelResult {
    vector<int> core;        // core[v] = core number of v
    int maxCore = 0;
    vector<char> inDensest;  // inDensest[v] = 1 if v is in the greedy densest set
    long long bestEdges = 0; // edges and vertices of that set
    int bestVertices = 0;

    double density() const { return bestVertices == 0 ? 0.0 : (double)bestEdges / bestVertices; }
};

PeelResult peel(const SimpleGraph& h) {
    int n = h.n;
    PeelResult res;
    res.core.assign(n, 0);
    res.inDensest.assign(n, 0);

    vector<int> deg(n);
    set<pair<int, int>> queue;  // (current degree, vertex), smallest first
    for (int v = 0; v < n; v++) {
        deg[v] = (int)h.adj[v].size();
        queue.insert({deg[v], v});
    }
    vector<char> removed(n, 0);
    vector<int> order;          // vertices in the order they were removed

    long long edgesLeft = h.m;
    int verticesLeft = n;
    res.bestEdges = edgesLeft;  // to start with, the whole graph is the best set
    res.bestVertices = verticesLeft;
    int bestStep = 0;           // the best set = vertices removed after step bestStep
    int level = 0;

    while (!queue.empty()) {
        int v = queue.begin()->second;  // vertex with the smallest degree
        queue.erase(queue.begin());

        level = max(level, deg[v]);
        res.core[v] = level;
        removed[v] = 1;
        order.push_back(v);
        edgesLeft -= deg[v];
        verticesLeft--;

        for (int u : h.adj[v]) {  // neighbours lose one degree
            if (removed[u]) continue;
            queue.erase({deg[u], u});
            deg[u]--;
            queue.insert({deg[u], u});
        }

        // Is what is left denser than the best so far?
        // Compare fractions without division:  a/b > c/d  <=>  a*d > c*b
        if (verticesLeft > 0 && edgesLeft * res.bestVertices > res.bestEdges * verticesLeft) {
            res.bestEdges = edgesLeft;
            res.bestVertices = verticesLeft;
            bestStep = (int)order.size();
        }
    }
    res.maxCore = level;
    for (int i = bestStep; i < n; i++) res.inDensest[order[i]] = 1;
    return res;
}

// Transfers with both accounts inside the given set.
vector<char> edgesInside(const Graph& g, const vector<char>& inSet) {
    vector<char> flagged(g.edges.size(), 0);
    for (int i = 0; i < (int)g.edges.size(); i++)
        if (inSet[g.edges[i].from] && inSet[g.edges[i].to]) flagged[i] = 1;
    return flagged;
}

// Accounts whose core number is at least k.
vector<char> inKCore(const PeelResult& p, int k) {
    vector<char> inCore(p.core.size(), 0);
    for (int v = 0; v < (int)p.core.size(); v++)
        if (p.core[v] >= k) inCore[v] = 1;
    return inCore;
}

// ----------------------------------------------------------------------------
// Tests for this module
// ----------------------------------------------------------------------------

void testChainsPeeling() {
    printf("Module 3: pass-through chains + k-core + greedy peeling\n");

    // A -1000-> B -980-> C -950-> D, ten seconds apart: a chain of 3 transfers.
    // D -500-> E keeps half the money: not a pass-through.
    // B -990-> F five days later: too slow.
    Graph g;
    int a = g.getId("A"), b = g.getId("B"), c = g.getId("C");
    int d = g.getId("D"), e = g.getId("E"), f = g.getId("F");
    g.addEdge(a, b, 1000, 0, true);
    g.addEdge(b, c, 980, 10, true);
    g.addEdge(c, d, 950, 20, true);
    g.addEdge(d, e, 500, 30, false);
    g.addEdge(b, f, 990, 5 * DAY, false);
    g.build();

    ChainResult res = findChains(g, 3, DAY, 0.10);
    long long flagged, laundering;
    countFlagged(g, res.flagged, flagged, laundering);
    check(res.longest == 3 && flagged == 3, "longest chain is A->B->C->D, its 3 transfers flagged");
    check(res.before[2] == 3 && res.after[0] == 3, "before[C->D] = 3 and after[A->B] = 3");
    check(findChains(g, 3, 9, 0.10).longest == 1, "max delay 9 s: hops are 10 s apart, no chain");
    check(findChains(g, 3, DAY, 0.02).longest == 2, "tolerance 2%: C->D keeps 3%, chain breaks");
    check(findChains(g, 3, DAY, 0.60).longest == 4, "tolerance 60%: D->E joins the chain");

    // 4-clique {A,B,C,D} with a tail D - E - F. A reverse transfer and a
    // repeated transfer must collapse into the same undirected edge.
    Graph k;
    int v[6];
    for (int i = 0; i < 6; i++) v[i] = k.getId(string(1, (char)('A' + i)));
    long long t = 0;
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++) k.addEdge(v[i], v[j], 100, t++, true);
    k.addEdge(v[1], v[0], 100, t++, true);  // reverse of A->B
    k.addEdge(v[0], v[1], 100, t++, true);  // repeat of A->B
    k.addEdge(v[3], v[4], 100, t++, false);
    k.addEdge(v[4], v[5], 100, t++, false);
    k.build();

    SimpleGraph h = makeUndirected(k);
    check(h.m == 8, "undirected graph has 6 clique edges + 2 tail edges = 8");
    PeelResult p = peel(h);
    check(p.maxCore == 3, "max core is 3 (the 4-clique)");
    check(p.core[v[0]] == 3 && p.core[v[4]] == 1 && p.core[v[5]] == 1, "core numbers: A = 3, E = F = 1");
    check(p.bestEdges == 6 && p.bestVertices == 4, "greedy densest set is the clique (6 edges / 4 vertices)");
}
