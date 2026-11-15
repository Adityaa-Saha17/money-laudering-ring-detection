// =============================================================================
//  cycles_fan.cpp                                   Module 2 - Debtanu Das
//
//  1. Temporal cycles  (round-tripping: money comes back to where it started)
//     -> DFS with backtracking, pruned with SCCs and binary search
//  2. Fan-out / fan-in (smurfing: one account sends to / receives from many)
//     -> sliding window with two pointers
// =============================================================================
#pragma once
#include "ingest_scc.cpp"

// ----------------------------------------------------------------------------
// 1. Temporal cycles
//
//    A cycle is a path of transfers A -> B -> C -> ... -> A where
//      - no account appears twice,
//      - each transfer happens at the same time or later than the previous one,
//      - the whole trip takes at most `window` seconds,
//      - it has at most `maxLength` transfers.
//
//    Idea: try every transfer as the FIRST hop, then DFS forward.
//    So that each cycle is counted once, the first hop must be the earliest
//    transfer of the cycle (the smallest index in the time-sorted edge list).
//
//    Two speed-ups:
//      - SCC pruning: a cycle never leaves its SCC, so we skip edges between SCCs.
//      - Binary search: in each adjacency list we jump straight to the first
//        transfer that is not earlier than the previous hop.
// ----------------------------------------------------------------------------

struct CycleSearch {
    const Graph& g;
    const vector<int>& comp;     // SCC number of every account
    int maxLength;
    long long maxCycles;         // safety limit so the search always finishes
    int start = 0;               // account where the current cycle starts
    int firstEdge = 0;           // index of the first transfer of the cycle
    long long deadline = 0;      // latest time allowed for this cycle
    vector<char> onPath;         // onPath[v] = 1 if v is already on the path
    vector<int> path;            // transfers on the current path
    vector<vector<int>> cycles;  // every cycle found (as lists of transfer indices)

    CycleSearch(const Graph& graph, const vector<int>& sccOf, int k, long long limit)
        : g(graph), comp(sccOf), maxLength(k), maxCycles(limit), onPath(graph.n, 0) {}
};

// We are at account u; the last transfer happened at lastTime.
void extendPath(CycleSearch& s, int u, long long lastTime) {
    const vector<int>& out = s.g.outEdges[u];
    for (int i = firstAtOrAfter(s.g, out, lastTime); i < (int)out.size(); i++) {
        if ((long long)s.cycles.size() >= s.maxCycles) return;
        int id = out[i];
        const Edge& e = s.g.edges[id];

        if (e.time > s.deadline) break;   // list is sorted, so the rest are too late
        if (id <= s.firstEdge) continue;  // the first hop must be the earliest one

        if (e.to == s.start) {            // back to the start: found a cycle
            s.path.push_back(id);
            s.cycles.push_back(s.path);
            s.path.pop_back();
            continue;
        }
        if (s.onPath[e.to]) continue;                      // account already used
        if (s.comp[e.to] != s.comp[s.start]) continue;     // left the SCC: can't come back
        if ((int)s.path.size() + 1 >= s.maxLength) continue;  // no hops left to get back

        s.onPath[e.to] = 1;               // choose
        s.path.push_back(id);
        extendPath(s, e.to, e.time);      // explore
        s.path.pop_back();                // un-choose (backtrack)
        s.onPath[e.to] = 0;
    }
}

vector<vector<int>> findTemporalCycles(const Graph& g, const SccResult& scc, int maxLength, long long window,
                                       long long maxCycles) {
    CycleSearch s(g, scc.comp, maxLength, maxCycles);
    for (int i = 0; i < (int)g.edges.size(); i++) {
        if ((long long)s.cycles.size() >= maxCycles) break;
        const Edge& e = g.edges[i];
        if (scc.comp[e.from] != scc.comp[e.to]) continue;  // this edge is on no cycle

        s.start = e.from;
        s.firstEdge = i;
        s.deadline = e.time + window;
        s.onPath[e.from] = s.onPath[e.to] = 1;
        s.path = {i};
        extendPath(s, e.to, e.time);
        s.onPath[e.from] = s.onPath[e.to] = 0;
    }
    return s.cycles;
}

// Marks every transfer that lies on at least one cycle.
vector<char> edgesOnCycles(const Graph& g, const vector<vector<int>>& cycles) {
    vector<char> flagged(g.edges.size(), 0);
    for (const vector<int>& cycle : cycles)
        for (int id : cycle) flagged[id] = 1;
    return flagged;
}

// ----------------------------------------------------------------------------
// 2. Fan-out and fan-in
//
//    Fan-out: an account sends money to at least `minPartners` DIFFERENT
//             accounts within `window` seconds.
//    Fan-in:  an account receives money from at least `minPartners` different
//             accounts within `window` seconds.
//
//    For each account we slide a time window over its (time-sorted) list:
//      - `right` adds the next transfer to the window,
//      - `left` removes old transfers until the window is short enough,
//      - count[p] = how many transfers with partner p are inside the window,
//        so `distinct` (partners with count > 0) is updated in O(1).
//    Each transfer enters and leaves the window once:  O(n + m) in total.
// ----------------------------------------------------------------------------

struct FanResult {
    int centres = 0;        // number of accounts that show the pattern
    vector<char> flagged;   // flagged[i] = 1 if transfer i is inside a suspicious window
};

FanResult findFans(const Graph& g, int minPartners, long long window, bool outgoing) {
    FanResult res;
    res.flagged.assign(g.edges.size(), 0);
    vector<int> count(g.n, 0);

    for (int u = 0; u < g.n; u++) {
        const vector<int>& list = outgoing ? g.outEdges[u] : g.inEdges[u];
        int size = (int)list.size();
        if (size < minPartners) continue;  // can never reach minPartners

        int left = 0, distinct = 0;
        int markedUpTo = -1;               // transfers up to here are already flagged
        bool isCentre = false;
        for (int right = 0; right < size; right++) {
            const Edge& e = g.edges[list[right]];
            int partner = outgoing ? e.to : e.from;
            if (count[partner] == 0) distinct++;
            count[partner]++;

            // shrink the window from the left until it spans at most `window` seconds
            while (e.time - g.edges[list[left]].time > window) {
                const Edge& old = g.edges[list[left]];
                int oldPartner = outgoing ? old.to : old.from;
                count[oldPartner]--;
                if (count[oldPartner] == 0) distinct--;
                left++;
            }

            if (distinct >= minPartners) {
                isCentre = true;
                for (int i = max(left, markedUpTo + 1); i <= right; i++) res.flagged[list[i]] = 1;
                markedUpTo = right;
            }
        }
        // reset the counters we used, ready for the next account
        for (int i = left; i < size; i++) {
            const Edge& e = g.edges[list[i]];
            count[outgoing ? e.to : e.from] = 0;
        }
        if (isCentre) res.centres++;
    }
    return res;
}

// ----------------------------------------------------------------------------
// Tests for this module
// ----------------------------------------------------------------------------

void testCyclesFan() {
    printf("Module 2: temporal cycles + fan-out / fan-in\n");

    // A -> B (t=10), B -> C (t=20), C -> A (t=30): a cycle lasting 20 seconds.
    // A second A -> B at t=500 gives a slower cycle B->C(20), C->A(30), A->B(500)
    // lasting 480 seconds. C -> D (t=1000) is on no cycle.
    Graph g;
    int a = g.getId("A"), b = g.getId("B"), c = g.getId("C"), d = g.getId("D");
    g.addEdge(a, b, 100, 10, true);
    g.addEdge(b, c, 95, 20, true);
    g.addEdge(c, a, 90, 30, true);
    g.addEdge(a, b, 50, 500, false);
    g.addEdge(c, d, 10, 1000, false);
    g.build();
    SccResult scc = kosarajuScc(g);

    check(findTemporalCycles(g, scc, 6, 100, 1000).size() == 1, "window 100 s: 1 cycle (A->B->C->A)");
    check(findTemporalCycles(g, scc, 6, 480, 1000).size() == 2, "window 480 s: also B->C->A->B(500)");
    check(findTemporalCycles(g, scc, 2, 100, 1000).empty(), "max length 2: the 3-hop cycle is too long");
    check(findTemporalCycles(g, scc, 3, 19, 1000).empty(), "window 19 s: the cycle takes 20 s, too slow");
    check(findTemporalCycles(g, scc, 3, 20, 1000).size() == 1, "window 20 s: exactly fits");

    // Hub H sends to R1..R4 at t = 0, 10, 20, 30 and to R1 again at t = 40.
    // Then R1..R4 all send to S at t = 100..130.
    Graph h;
    int hub = h.getId("H"), sink = h.getId("S");
    int r[5];
    for (int i = 1; i <= 4; i++) r[i] = h.getId("R" + to_string(i));
    for (int i = 1; i <= 4; i++) h.addEdge(hub, r[i], 900, (i - 1) * 10, true);
    h.addEdge(hub, r[1], 900, 40, true);
    for (int i = 1; i <= 4; i++) h.addEdge(r[i], sink, 890, 100 + (i - 1) * 10, true);
    h.build();

    FanResult out = findFans(h, 4, HOUR, true);
    long long flagged, laundering;
    countFlagged(h, out.flagged, flagged, laundering);
    check(out.centres == 1 && flagged == 5, "fan-out: H is the centre, its 5 transfers are flagged");
    check(findFans(h, 4, HOUR, false).centres == 1, "fan-in: S is the centre");
    check(findFans(h, 5, HOUR, true).centres == 0, "need 5 partners: H only has 4");
    check(findFans(h, 4, 29, true).centres == 0, "window 29 s: H's 4 sends take 30 s");
}
