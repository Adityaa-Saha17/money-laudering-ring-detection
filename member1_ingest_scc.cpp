// =====================================================================================
//  Money-Laundering Ring Detector                      MFCS group project, part 1 of 4
//  member1_ingest_scc.cpp
//
//  Member 1: transaction graph model, CSV ingestion, synthetic data generator,
//            Tarjan's strongly connected components.
//  Proof owned: correctness of Tarjan's algorithm (+ the sorted-adjacency invariant).
//
//  The project is one program split into four files that include each other in a chain:
//      member1  <-  member2  <-  member3  <-  member4 (contains main)
//  Build the whole program:   clang++ -std=c++17 -O2 member4_exact_eval.cpp -o aml
//  Build this part alone:     clang++ -std=c++17 -O2 member1_ingest_scc.cpp -o part1 && ./part1
//  (On its own, a file runs the self-tests of its own part and every part below it.)
// =====================================================================================
#ifndef AML_PART_OF_LATER
#define AML_STANDALONE_1
#endif

#ifndef AML_MEMBER1_CPP
#define AML_MEMBER1_CPP

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// ------------------------------- shared utilities -----------------------------------

// Test checks that still run when compiled with -DNDEBUG (unlike assert).
int checksRun = 0, checksFailed = 0;
#define CHECK(cond)                                                                      \
    do {                                                                                 \
        checksRun++;                                                                     \
        if (!(cond)) {                                                                   \
            checksFailed++;                                                              \
            std::fprintf(stderr, "  CHECK failed: %s  (%s:%d)\n", #cond, __FILE__, __LINE__); \
        }                                                                                \
    } while (0)

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// ------------------------------- graph model ----------------------------------------

// One transfer. Parallel edges between the same pair of accounts are kept.
struct Edge {
    int from;
    int to;
    double amount;        // "Amount Paid" column
    long long timestamp;  // seconds since 1970-01-01 00:00 UTC
    bool isLaundering;
};

// Directed temporal multigraph G = (V, E).
// Invariant (after finalize): edges is sorted by timestamp, and every adjacency list
// holds edge indices in increasing order, hence in non-decreasing timestamp order.
struct Graph {
    int n = 0;
    std::vector<Edge> edges;
    std::vector<std::vector<int>> outAdj;  // outAdj[u] = indices of edges leaving u
    std::vector<std::vector<int>> inAdj;   // inAdj[v]  = indices of edges entering v
    std::vector<std::string> accountName;  // vertex id -> "bank:account"
    std::unordered_map<std::string, int> accountId;

    int getVertex(const std::string& name) {
        auto it = accountId.find(name);
        if (it != accountId.end()) return it->second;
        accountId.emplace(name, n);
        accountName.push_back(name);
        return n++;
    }

    void addEdge(int from, int to, double amount, long long timestamp, bool isLaundering) {
        edges.push_back({from, to, amount, timestamp, isLaundering});
    }

    // Must be called once after the last addEdge. O(m log m).
    void finalize() {
        std::stable_sort(edges.begin(), edges.end(),
                         [](const Edge& a, const Edge& b) { return a.timestamp < b.timestamp; });
        outAdj.assign(n, {});
        inAdj.assign(n, {});
        int m = (int)edges.size();
        for (int i = 0; i < m; i++) {
            outAdj[edges[i].from].push_back(i);
            inAdj[edges[i].to].push_back(i);
        }
    }

    int outDegree(int u) const { return (int)outAdj[u].size(); }
    int inDegree(int v) const { return (int)inAdj[v].size(); }

    // Position in outAdj[u] of the first edge with timestamp >= t. O(log deg(u)).
    int firstOutAtOrAfter(int u, long long t) const {
        const std::vector<int>& adj = outAdj[u];
        int lo = 0, hi = (int)adj.size();
        while (lo < hi) {
            int mid = (lo + hi) / 2;
            if (edges[adj[mid]].timestamp < t) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    }
};

// Counts flagged transfers and how many of them carry the laundering label.
void countFlagged(const Graph& g, const std::vector<char>& flagged, long long& total, long long& laundering) {
    total = laundering = 0;
    for (int i = 0; i < (int)g.edges.size(); i++) {
        if (!flagged[i]) continue;
        total++;
        if (g.edges[i].isLaundering) laundering++;
    }
}

// ------------------------------- CSV ingestion --------------------------------------

struct IngestStats {
    long long rowsRead = 0;
    long long rowsSkipped = 0;  // malformed rows
    long long selfLoops = 0;
    long long launderingEdges = 0;
};

// Days from 1970-01-01 to y-m-d in the proleptic Gregorian calendar.
// Written by hand because timegm is not available on every platform.
long long daysFromCivil(long long y, int m, int d) {
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// "2022/09/01 00:20" -> epoch seconds. Returns false on a malformed string.
bool parseTimestamp(const std::string& s, long long& out) {
    if (s.size() < 16 || s[4] != '/' || s[7] != '/' || s[10] != ' ' || s[13] != ':') return false;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15})
        if (s[i] < '0' || s[i] > '9') return false;
    int year = std::atoi(s.substr(0, 4).c_str());
    int month = std::atoi(s.substr(5, 2).c_str());
    int day = std::atoi(s.substr(8, 2).c_str());
    int hour = std::atoi(s.substr(11, 2).c_str());
    int minute = std::atoi(s.substr(14, 2).c_str());
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59) return false;
    out = daysFromCivil(year, month, day) * 86400LL + hour * 3600LL + minute * 60LL;
    return true;
}

std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string cur;
    for (char c : line) {
        if (c == ',') { fields.push_back(cur); cur.clear(); }
        else if (c != '\r') cur += c;
    }
    fields.push_back(cur);
    return fields;
}

// IBM AML columns:
// 0 Timestamp, 1 From Bank, 2 Account, 3 To Bank, 4 Account, 5 Amount Received,
// 6 Receiving Currency, 7 Amount Paid, 8 Payment Currency, 9 Payment Format, 10 Is Laundering
// Malformed rows are skipped and counted, never fatal. Returns false only if the file
// cannot be opened. O(m log m) overall (the sort in finalize).
bool loadCsv(const std::string& path, Graph& g, IngestStats& stats) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        stats.rowsRead++;
        std::vector<std::string> f = splitCsvLine(line);
        long long timestamp;
        if (f.size() != 11 || !parseTimestamp(f[0], timestamp)) { stats.rowsSkipped++; continue; }
        char* end = nullptr;
        double amount = std::strtod(f[7].c_str(), &end);
        if (end == f[7].c_str() || amount < 0) { stats.rowsSkipped++; continue; }
        int from = g.getVertex(f[1] + ":" + f[2]);
        int to = g.getVertex(f[3] + ":" + f[4]);
        bool isLaundering = (f[10] == "1");
        if (from == to) stats.selfLoops++;
        if (isLaundering) stats.launderingEdges++;
        g.addEdge(from, to, amount, timestamp, isLaundering);
    }
    g.finalize();
    return true;
}

// ------------------------------- synthetic data generator ---------------------------
// Writes a labelled file in the IBM column layout: random background transfers (label 0)
// plus planted cycles, fan-outs, fan-ins, pass-through chains and 8-account cliques
// (label 1). It checks the pipeline end to end; it is NOT a substitute for the IBM data.

struct SyntheticWriter {
    std::mt19937 rng;
    std::ofstream out;
    int accounts = 0;
    static const int minutesInMonth = 30 * 24 * 60;

    int randomBelow(int k) { return (int)(rng() % (unsigned)k); }

    void writeRow(int minute, int from, int to, double amount, int label) {
        if (minute >= minutesInMonth) minute = minutesInMonth - 1;
        char line[200];
        std::snprintf(line, sizeof(line), "2022/09/%02d %02d:%02d,%d,%08X,%d,%08X,%.2f,US Dollar,%.2f,US Dollar,ACH,%d\n",
                      1 + minute / 1440, (minute / 60) % 24, minute % 60, from % 50, from, to % 50, to, amount,
                      amount, label);
        out << line;
    }

    std::vector<int> distinctAccounts(int count) {
        std::vector<int> picked;
        while ((int)picked.size() < count) {
            int a = randomBelow(accounts);
            if (std::find(picked.begin(), picked.end(), a) == picked.end()) picked.push_back(a);
        }
        return picked;
    }
};

bool generateSynthetic(const std::string& path, int accounts, int backgroundEdges, int seed, std::string& summary) {
    SyntheticWriter w;
    w.rng.seed(seed);
    w.accounts = accounts;
    w.out.open(path);
    if (!w.out) return false;
    const int month = SyntheticWriter::minutesInMonth;
    w.out << "Timestamp,From Bank,Account,To Bank,Account,Amount Received,Receiving Currency,"
             "Amount Paid,Payment Currency,Payment Format,Is Laundering\n";

    for (int i = 0; i < backgroundEdges; i++)
        w.writeRow(w.randomBelow(month), w.randomBelow(accounts), w.randomBelow(accounts),
                   10 + w.randomBelow(999000) / 100.0, 0);

    int planted = std::max(1, backgroundEdges / 5000);  // patterns of each kind
    for (int i = 0; i < planted; i++) {  // cycles of 3..6 hops, minutes apart
        int len = 3 + w.randomBelow(4);
        std::vector<int> v = w.distinctAccounts(len);
        int minute = w.randomBelow(month - 2000);
        double amount = 5000 + w.randomBelow(5000);
        for (int j = 0; j < len; j++) {
            w.writeRow(minute, v[j], v[(j + 1) % len], amount, 1);
            minute += 1 + w.randomBelow(120);
            amount *= 0.98;
        }
    }
    for (int i = 0; i < planted; i++) {  // fan-out: one source, 8 recipients within a few hours
        std::vector<int> v = w.distinctAccounts(9);
        int minute = w.randomBelow(month - 2000);
        for (int j = 1; j <= 8; j++) w.writeRow(minute + w.randomBelow(300), v[0], v[j], 900 + w.randomBelow(90), 1);
    }
    for (int i = 0; i < planted; i++) {  // fan-in: 8 senders, one collector
        std::vector<int> v = w.distinctAccounts(9);
        int minute = w.randomBelow(month - 2000);
        for (int j = 1; j <= 8; j++) w.writeRow(minute + w.randomBelow(300), v[j], v[0], 900 + w.randomBelow(90), 1);
    }
    for (int i = 0; i < planted; i++) {  // pass-through chain of 4..6 hops, each keeping 2-5%
        int len = 4 + w.randomBelow(3);
        std::vector<int> v = w.distinctAccounts(len + 1);
        int minute = w.randomBelow(month - 2000);
        double amount = 20000 + w.randomBelow(20000);
        for (int j = 0; j < len; j++) {
            w.writeRow(minute, v[j], v[j + 1], amount, 1);
            minute += 1 + w.randomBelow(180);
            amount *= 1.0 - (2 + w.randomBelow(4)) / 100.0;
        }
    }
    int cliques = planted / 8 + 1;
    for (int i = 0; i < cliques; i++) {  // tight group: 8 accounts, every pair transacts
        std::vector<int> v = w.distinctAccounts(8);
        for (int a = 0; a < 8; a++)
            for (int b = a + 1; b < 8; b++)
                w.writeRow(w.randomBelow(month), v[a], v[b], 300 + w.randomBelow(4000), 1);
    }
    summary = std::to_string(backgroundEdges) + " background transfers, " + std::to_string(planted) +
              " each of cycle/fan-out/fan-in/chain, " + std::to_string(cliques) + " cliques";
    return true;
}

// ------------------------------- Tarjan SCC -----------------------------------------

struct SccResult {
    int count = 0;
    std::vector<int> comp;      // comp[v] = id of the SCC containing v
    std::vector<int> compSize;  // compSize[c] = number of vertices in SCC c
};

// Tarjan's algorithm, O(n + m). Iterative (explicit stack of (vertex, next out-edge
// position)), because a DFS path through hundreds of thousands of accounts would
// overflow the call stack.
SccResult tarjanScc(const Graph& g) {
    int n = g.n;
    SccResult res;
    res.comp.assign(n, -1);
    std::vector<int> disc(n, -1), low(n, 0), st;
    std::vector<char> onStack(n, 0);
    std::vector<std::pair<int, int>> callStack;
    int timer = 0;

    for (int root = 0; root < n; root++) {
        if (disc[root] != -1) continue;
        disc[root] = low[root] = timer++;
        st.push_back(root);
        onStack[root] = 1;
        callStack.push_back({root, 0});

        while (!callStack.empty()) {
            int u = callStack.back().first;
            int pos = callStack.back().second;
            if (pos < g.outDegree(u)) {
                callStack.back().second++;
                int v = g.edges[g.outAdj[u][pos]].to;
                if (disc[v] == -1) {  // tree edge: descend
                    disc[v] = low[v] = timer++;
                    st.push_back(v);
                    onStack[v] = 1;
                    callStack.push_back({v, 0});
                } else if (onStack[v]) {
                    low[u] = std::min(low[u], disc[v]);
                }
            } else {  // u finishes
                callStack.pop_back();
                if (!callStack.empty()) {
                    int parent = callStack.back().first;
                    low[parent] = std::min(low[parent], low[u]);
                }
                if (low[u] == disc[u]) {  // u is the root of an SCC: pop it
                    int size = 0;
                    while (true) {
                        int w = st.back();
                        st.pop_back();
                        onStack[w] = 0;
                        res.comp[w] = res.count;
                        size++;
                        if (w == u) break;
                    }
                    res.compSize.push_back(size);
                    res.count++;
                }
            }
        }
    }
    return res;
}

// ------------------------------- report section -------------------------------------

void reportMember1(const Graph& g, const IngestStats& stats, double loadSeconds, const SccResult& scc,
                   double sccSeconds) {
    int maxOut = 0, maxIn = 0;
    for (int i = 0; i < g.n; i++) {
        maxOut = std::max(maxOut, g.outDegree(i));
        maxIn = std::max(maxIn, g.inDegree(i));
    }
    int nontrivial = 0, largest = 0;
    for (int size : scc.compSize) {
        if (size > 1) nontrivial++;
        largest = std::max(largest, size);
    }
    std::printf("== Member 1: ingestion and SCC ==\n");
    std::printf("accounts (vertices)         %d\n", g.n);
    std::printf("transfers (edges)           %zu\n", g.edges.size());
    std::printf("rows skipped (malformed)    %lld\n", stats.rowsSkipped);
    std::printf("self loops                  %lld\n", stats.selfLoops);
    std::printf("labelled laundering         %lld\n", stats.launderingEdges);
    std::printf("max out-degree / in-degree  %d / %d\n", maxOut, maxIn);
    std::printf("load time (s)               %.3f\n", loadSeconds);
    std::printf("SCCs                        %d (size > 1: %d, largest: %d)\n", scc.count, nontrivial, largest);
    std::printf("SCC time (s)                %.3f\n\n", sccSeconds);
}

// ------------------------------- self-tests -----------------------------------------

// Brute-force reachability (Floyd-Warshall style transitive closure).
std::vector<std::vector<char>> reachability(const Graph& g) {
    int n = g.n;
    std::vector<std::vector<char>> reach(n, std::vector<char>(n, 0));
    for (int i = 0; i < n; i++) reach[i][i] = 1;
    for (const Edge& e : g.edges) reach[e.from][e.to] = 1;
    for (int k = 0; k < n; k++)
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                if (reach[i][k] && reach[k][j]) reach[i][j] = 1;
    return reach;
}

bool testMember1() {
    int failedBefore = checksFailed;

    // 1. Timestamp parsing (known epoch values, leap day, bad format).
    long long t;
    CHECK(parseTimestamp("1970/01/01 00:00", t) && t == 0);
    CHECK(parseTimestamp("2022/09/01 00:20", t) && t == 1661991600LL);
    CHECK(parseTimestamp("2024/02/29 23:59", t) && t == 1709251140LL);
    CHECK(!parseTimestamp("2022-09-01 00:20", t));

    // 2. Hand-made CSV: A->B twice (parallel), B->C, C->A (a 3-cycle), C->D, D->E,
    //    E->E (self loop), D->F. Rows out of time order and one malformed row.
    const char* path = "aml_test_graph.csv";
    {
        std::ofstream out(path);
        out << "Timestamp,From Bank,Account,To Bank,Account,Amount Received,Receiving Currency,"
               "Amount Paid,Payment Currency,Payment Format,Is Laundering\n"
            << "2022/09/01 00:30,1,C,1,A,90.0,US Dollar,90.0,US Dollar,ACH,1\n"
            << "2022/09/01 00:10,1,A,1,B,100.0,US Dollar,100.0,US Dollar,ACH,1\n"
            << "2022/09/01 00:20,1,B,1,C,95.0,US Dollar,95.0,US Dollar,ACH,1\n"
            << "2022/09/01 00:40,1,A,1,B,50.0,US Dollar,50.0,US Dollar,Cheque,0\n"
            << "2022/09/02 09:00,1,C,1,D,10.0,US Dollar,10.0,US Dollar,Wire,0\n"
            << "2022/09/02 10:00,1,D,1,E,5.0,US Dollar,5.0,US Dollar,Wire,0\n"
            << "2022/09/02 11:00,1,E,1,E,5.0,US Dollar,5.0,US Dollar,Reinvestment,0\n"
            << "2022/09/03 12:00,1,D,2,F,4.0,Euro,4.5,US Dollar,Wire,0\n"
            << "not-a-date,1,A,1,B,1.0,US Dollar,1.0,US Dollar,ACH,0\n";
    }
    Graph g;
    IngestStats stats;
    CHECK(loadCsv(path, g, stats));
    std::remove(path);
    CHECK(g.n == 6 && g.edges.size() == 8);
    CHECK(stats.rowsRead == 9 && stats.rowsSkipped == 1);
    CHECK(stats.selfLoops == 1 && stats.launderingEdges == 3);

    int a = g.accountId.at("1:A"), b = g.accountId.at("1:B"), c = g.accountId.at("1:C");
    int d = g.accountId.at("1:D"), e = g.accountId.at("1:E"), f = g.accountId.at("2:F");
    CHECK(g.outDegree(a) == 2 && g.inDegree(a) == 1);
    CHECK(g.outDegree(b) == 1 && g.inDegree(b) == 2);  // parallel edges kept
    CHECK(g.outDegree(e) == 1 && g.inDegree(e) == 2);  // self loop counts on both sides
    CHECK(g.outDegree(f) == 0 && g.inDegree(f) == 1);

    int sumOut = 0, sumIn = 0;  // handshake: both sums equal |E|
    for (int i = 0; i < g.n; i++) { sumOut += g.outDegree(i); sumIn += g.inDegree(i); }
    CHECK(sumOut == 8 && sumIn == 8);

    bool sorted = true;  // invariant: edge list and every adjacency list are time-sorted
    for (int i = 1; i < (int)g.edges.size(); i++) sorted &= g.edges[i - 1].timestamp <= g.edges[i].timestamp;
    for (int u = 0; u < g.n; u++) {
        for (int i = 1; i < g.outDegree(u); i++)
            sorted &= g.edges[g.outAdj[u][i - 1]].timestamp <= g.edges[g.outAdj[u][i]].timestamp;
        for (int i = 1; i < g.inDegree(u); i++)
            sorted &= g.edges[g.inAdj[u][i - 1]].timestamp <= g.edges[g.inAdj[u][i]].timestamp;
    }
    CHECK(sorted);
    CHECK(g.edges[0].from == a && g.edges[0].to == b && g.edges[0].amount == 100.0);
    CHECK(g.edges.back().to == f && g.edges.back().amount == 4.5);  // "Amount Paid", not received

    long long base = 1661991600LL - 600;  // 2022/09/01 00:10
    CHECK(g.firstOutAtOrAfter(a, base) == 0);
    CHECK(g.firstOutAtOrAfter(a, base + 1) == 1);
    CHECK(g.firstOutAtOrAfter(a, base + 1801) == 2);

    // 3. Tarjan on the hand-made graph: {A,B,C}, {D}, {E}, {F}.
    SccResult scc = tarjanScc(g);
    CHECK(scc.count == 4);
    CHECK(scc.comp[a] == scc.comp[b] && scc.comp[b] == scc.comp[c]);
    CHECK(scc.comp[d] != scc.comp[a] && scc.comp[e] != scc.comp[d] && scc.comp[f] != scc.comp[d]);
    CHECK(scc.compSize[scc.comp[a]] == 3 && scc.compSize[scc.comp[e]] == 1);

    // 4. Tarjan on 1000 random graphs against brute-force mutual reachability.
    std::mt19937 rng(12345);
    int sccMismatches = 0;
    for (int trial = 0; trial < 1000; trial++) {
        Graph r;
        int n = 2 + rng() % 8;
        int m = rng() % 16;
        for (int i = 0; i < n; i++) r.getVertex(std::to_string(i));
        for (int i = 0; i < m; i++) r.addEdge(rng() % n, rng() % n, 1.0, rng() % 6, false);
        r.finalize();
        SccResult s = tarjanScc(r);
        std::vector<std::vector<char>> reach = reachability(r);
        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                if ((s.comp[i] == s.comp[j]) != (reach[i][j] && reach[j][i])) sccMismatches++;
    }
    CHECK(sccMismatches == 0);

    // 5. Deep path: 200000 vertices in one long cycle. A recursive DFS would overflow.
    Graph deep;
    deep.n = 200000;
    for (int i = 0; i < deep.n; i++) deep.addEdge(i, (i + 1) % deep.n, 1.0, i, false);
    deep.finalize();
    SccResult deepScc = tarjanScc(deep);
    CHECK(deepScc.count == 1 && deepScc.compSize[0] == deep.n);

    // 6. Generator round trip: every planted transfer is read back with its label.
    std::string summary;
    const char* synthPath = "aml_test_synth.csv";
    CHECK(generateSynthetic(synthPath, 500, 5000, 7, summary));
    Graph s;
    IngestStats synthStats;
    CHECK(loadCsv(synthPath, s, synthStats));
    std::remove(synthPath);
    // planted = 1: one cycle (3..6 edges), 8 + 8 fan edges, one chain (4..6), one clique (28)
    CHECK(synthStats.rowsSkipped == 0);
    CHECK(synthStats.launderingEdges >= 3 + 16 + 4 + 28 && synthStats.launderingEdges <= 6 + 16 + 6 + 28);

    bool ok = checksFailed == failedBefore;
    std::printf("[member 1] ingestion + SCC tests: %s (1000 random graphs vs brute force)\n", ok ? "PASS" : "FAIL");
    return ok;
}

#endif  // AML_MEMBER1_CPP

#ifdef AML_STANDALONE_1
int main() { return testMember1() ? 0 : 1; }
#endif
