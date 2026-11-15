// =============================================================================
//  ingest_scc.cpp                                   Module 1 - Adityaa Saha
//
//  1. The transaction graph: accounts are vertices, transfers are directed edges
//  2. Reading the IBM CSV file into the graph
//  3. A small generator for synthetic test data
//  4. Binary search on a time-sorted list of edges
//  5. Kosaraju's algorithm for Strongly Connected Components (SCCs)
// =============================================================================
#pragma once

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>
using namespace std;

const long long HOUR = 3600;  // all times are stored in seconds
const long long DAY = 24 * HOUR;

// ----------------------------------------------------------------------------
// 1. The graph
// ----------------------------------------------------------------------------

struct Edge {
    int from, to;        // sender and receiver account
    double amount;       // "Amount Paid" column
    long long time;      // seconds since 1 Jan 1970
    bool isLaundering;   // label from the dataset (used only to measure accuracy)
};

struct Graph {
    int n = 0;                       // number of accounts
    vector<Edge> edges;              // all transfers (sorted by time after build())
    vector<vector<int>> outEdges;    // outEdges[u] = indices of transfers sent by u
    vector<vector<int>> inEdges;     // inEdges[v]  = indices of transfers received by v
    unordered_map<string, int> idOf; // account name "bank:account" -> vertex number

    // Returns the vertex number of an account, creating it the first time we see it.
    int getId(const string& name) {
        if (idOf.count(name) == 0) idOf[name] = n++;
        return idOf[name];
    }

    void addEdge(int from, int to, double amount, long long time, bool isLaundering) {
        edges.push_back({from, to, amount, time, isLaundering});
    }

    // Call once after adding all edges. Sorts transfers by time and fills the
    // adjacency lists. Because edges are sorted, every adjacency list is also
    // sorted by time.  O(m log m)
    void build() {
        stable_sort(edges.begin(), edges.end(),
                    [](const Edge& a, const Edge& b) { return a.time < b.time; });
        outEdges.assign(n, {});
        inEdges.assign(n, {});
        for (int i = 0; i < (int)edges.size(); i++) {
            outEdges[edges[i].from].push_back(i);
            inEdges[edges[i].to].push_back(i);
        }
    }
};

// ----------------------------------------------------------------------------
// 2. Reading the CSV file
// ----------------------------------------------------------------------------

bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int daysInMonth(int month, int year) {
    const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) return 29;
    return days[month - 1];
}

// "2022/09/01 00:20"  ->  seconds since 1970/01/01 00:00.  Returns false if malformed.
bool parseTime(const string& s, long long& seconds) {
    int year, month, day, hour, minute;
    if (sscanf(s.c_str(), "%d/%d/%d %d:%d", &year, &month, &day, &hour, &minute) != 5) return false;
    if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59) return false;

    long long days = 0;
    for (int y = 1970; y < year; y++) days += isLeapYear(y) ? 366 : 365;
    for (int m = 1; m < month; m++) days += daysInMonth(m, year);
    days += day - 1;
    seconds = days * DAY + hour * HOUR + minute * 60;
    return true;
}

vector<string> splitCsvLine(const string& line) {
    vector<string> fields;
    string current;
    for (char c : line) {
        if (c == ',') {
            fields.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current += c;
        }
    }
    fields.push_back(current);
    return fields;
}

struct LoadStats {
    long long rows = 0;       // data rows in the file
    long long skipped = 0;    // malformed rows
    long long selfLoops = 0;  // transfers from an account to itself (ignored)
};

// IBM AML columns:
//   0 Timestamp, 1 From Bank, 2 Account, 3 To Bank, 4 Account, 5 Amount Received,
//   6 Receiving Currency, 7 Amount Paid, 8 Payment Currency, 9 Payment Format, 10 Is Laundering
bool loadCsv(const string& path, Graph& g, LoadStats& stats) {
    ifstream in(path);
    if (!in) in.open("data/" + path);  // so "./aml detect synth.csv" also works
    if (!in) return false;

    string line;
    getline(in, line);  // skip the header
    while (getline(in, line)) {
        if (line.empty()) continue;
        stats.rows++;
        vector<string> f = splitCsvLine(line);
        long long time;
        if (f.size() != 11 || !parseTime(f[0], time)) {
            stats.skipped++;
            continue;
        }
        int from = g.getId(f[1] + ":" + f[2]);
        int to = g.getId(f[3] + ":" + f[4]);
        if (from == to) {  // money sent to yourself cannot be part of a ring
            stats.selfLoops++;
            continue;
        }
        g.addEdge(from, to, atof(f[7].c_str()), time, f[10] == "1");
    }
    g.build();
    return true;
}

// ----------------------------------------------------------------------------
// 3. Synthetic data generator
//    Writes random "normal" transfers (label 0) plus a few planted laundering
//    patterns (label 1): cycles, fan-outs, fan-ins, chains and 8-account cliques.
// ----------------------------------------------------------------------------

mt19937 rng;
int randomBelow(int k) { return (int)(rng() % (unsigned)k); }

const int MINUTES_IN_MONTH = 30 * 24 * 60;

void writeRow(ofstream& out, int minute, int from, int to, double amount, int label) {
    if (minute >= MINUTES_IN_MONTH) minute = MINUTES_IN_MONTH - 1;
    char line[200];
    snprintf(line, sizeof(line), "2022/09/%02d %02d:%02d,%d,%08X,%d,%08X,%.2f,US Dollar,%.2f,US Dollar,ACH,%d\n",
             1 + minute / 1440, (minute / 60) % 24, minute % 60, from % 50, from, to % 50, to, amount, amount, label);
    out << line;
}

// `count` different random accounts.
vector<int> pickAccounts(int count, int accounts) {
    vector<int> picked;
    while ((int)picked.size() < count) {
        int a = randomBelow(accounts);
        if (find(picked.begin(), picked.end(), a) == picked.end()) picked.push_back(a);
    }
    return picked;
}

bool generateSynthetic(const string& path, int accounts, int background, int seed) {
    ofstream out(path);
    if (!out) return false;
    rng.seed(seed);
    out << "Timestamp,From Bank,Account,To Bank,Account,Amount Received,Receiving Currency,"
           "Amount Paid,Payment Currency,Payment Format,Is Laundering\n";

    for (int i = 0; i < background; i++) {  // normal random transfers
        int minute = randomBelow(MINUTES_IN_MONTH);
        int from = randomBelow(accounts);
        int to = randomBelow(accounts);
        double amount = 10 + randomBelow(999000) / 100.0;
        writeRow(out, minute, from, to, amount, 0);
    }

    int planted = max(1, background / 5000);  // how many of each pattern
    for (int i = 0; i < planted; i++) {       // cycle of 3..6 accounts
        int len = 3 + randomBelow(4);
        vector<int> v = pickAccounts(len, accounts);
        int minute = randomBelow(MINUTES_IN_MONTH - 2000);
        double amount = 5000 + randomBelow(5000);
        for (int j = 0; j < len; j++) {
            writeRow(out, minute, v[j], v[(j + 1) % len], amount, 1);
            minute += 1 + randomBelow(120);
            amount *= 0.98;
        }
    }
    for (int i = 0; i < planted; i++) {  // fan-out: 1 sender -> 8 receivers
        vector<int> v = pickAccounts(9, accounts);
        int minute = randomBelow(MINUTES_IN_MONTH - 2000);
        for (int j = 1; j <= 8; j++) {
            int t = minute + randomBelow(300);
            writeRow(out, t, v[0], v[j], 900 + randomBelow(90), 1);
        }
    }
    for (int i = 0; i < planted; i++) {  // fan-in: 8 senders -> 1 receiver
        vector<int> v = pickAccounts(9, accounts);
        int minute = randomBelow(MINUTES_IN_MONTH - 2000);
        for (int j = 1; j <= 8; j++) {
            int t = minute + randomBelow(300);
            writeRow(out, t, v[j], v[0], 900 + randomBelow(90), 1);
        }
    }
    for (int i = 0; i < planted; i++) {  // chain of 4..6 hops, each keeps 2-5%
        int len = 4 + randomBelow(3);
        vector<int> v = pickAccounts(len + 1, accounts);
        int minute = randomBelow(MINUTES_IN_MONTH - 2000);
        double amount = 20000 + randomBelow(20000);
        for (int j = 0; j < len; j++) {
            writeRow(out, minute, v[j], v[j + 1], amount, 1);
            minute += 1 + randomBelow(180);
            amount *= 1.0 - (2 + randomBelow(4)) / 100.0;
        }
    }
    int cliques = planted / 8 + 1;
    for (int i = 0; i < cliques; i++) {  // 8 accounts, every pair transacts
        vector<int> v = pickAccounts(8, accounts);
        for (int a = 0; a < 8; a++)
            for (int b = a + 1; b < 8; b++) {
                int t = randomBelow(MINUTES_IN_MONTH);
                writeRow(out, t, v[a], v[b], 300 + randomBelow(4000), 1);
            }
    }
    printf("wrote %s: %d normal transfers, %d of each pattern, %d cliques\n", path.c_str(), background, planted,
           cliques);
    return true;
}

// ----------------------------------------------------------------------------
// 4. Binary search
//    Position of the first edge in `list` whose time is >= t.
//    Works because every adjacency list is sorted by time.  O(log length)
// ----------------------------------------------------------------------------

int firstAtOrAfter(const Graph& g, const vector<int>& list, long long t) {
    int lo = 0, hi = (int)list.size();
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (g.edges[list[mid]].time < t) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

// ----------------------------------------------------------------------------
// 5. Kosaraju's algorithm for SCCs.  O(n + m)
//    Pass 1: DFS on the graph, record vertices in the order they FINISH.
//    Pass 2: go through vertices in REVERSE finish order and run a DFS on the
//            REVERSED graph. Every vertex reached in one such DFS is one SCC.
//
//    The DFS uses our own stack instead of recursion: with 5 lakh accounts a
//    recursive DFS can go so deep that the program's call stack overflows.
// ----------------------------------------------------------------------------

struct SccResult {
    int count = 0;
    vector<int> comp;      // comp[v] = which SCC vertex v belongs to
    vector<int> compSize;  // compSize[c] = number of vertices in SCC c
};

SccResult kosarajuScc(const Graph& g) {
    int n = g.n;

    // ---- Pass 1: finish order ----
    vector<char> visited(n, 0);
    vector<int> finishOrder;
    vector<pair<int, int>> stack;  // (vertex, how many of its out-edges we already tried)
    for (int s = 0; s < n; s++) {
        if (visited[s]) continue;
        visited[s] = 1;
        stack.push_back({s, 0});
        while (!stack.empty()) {
            int u = stack.back().first;
            int i = stack.back().second;
            if (i < (int)g.outEdges[u].size()) {
                stack.back().second++;  // next time, try the following edge
                int v = g.edges[g.outEdges[u][i]].to;
                if (!visited[v]) {
                    visited[v] = 1;
                    stack.push_back({v, 0});
                }
            } else {  // all edges of u tried: u finishes
                finishOrder.push_back(u);
                stack.pop_back();
            }
        }
    }

    // ---- Pass 2: DFS on the reversed graph (follow in-edges) ----
    SccResult res;
    res.comp.assign(n, -1);
    for (int k = n - 1; k >= 0; k--) {
        int s = finishOrder[k];
        if (res.comp[s] != -1) continue;
        int size = 0;
        vector<int> todo = {s};
        res.comp[s] = res.count;
        while (!todo.empty()) {
            int u = todo.back();
            todo.pop_back();
            size++;
            for (int id : g.inEdges[u]) {
                int v = g.edges[id].from;
                if (res.comp[v] == -1) {
                    res.comp[v] = res.count;
                    todo.push_back(v);
                }
            }
        }
        res.compSize.push_back(size);
        res.count++;
    }
    return res;
}

// ----------------------------------------------------------------------------
// Small helpers used by every module
// ----------------------------------------------------------------------------

double secondsSince(chrono::steady_clock::time_point start) {
    return chrono::duration<double>(chrono::steady_clock::now() - start).count();
}

// How many transfers are flagged, and how many of those are really laundering.
void countFlagged(const Graph& g, const vector<char>& flagged, long long& total, long long& laundering) {
    total = laundering = 0;
    for (int i = 0; i < (int)g.edges.size(); i++) {
        if (!flagged[i]) continue;
        total++;
        if (g.edges[i].isLaundering) laundering++;
    }
}

int testsFailed = 0;
void check(bool ok, const string& what) {
    printf("  [%s] %s\n", ok ? " ok " : "FAIL", what.c_str());
    if (!ok) testsFailed++;
}

// ----------------------------------------------------------------------------
// Tests for this module (small examples you can check by hand)
// ----------------------------------------------------------------------------

void testIngestScc() {
    printf("Module 1: ingestion + SCC\n");

    long long t;
    check(parseTime("1970/01/01 00:00", t) && t == 0, "1970/01/01 00:00 is time 0");
    check(parseTime("1970/01/02 01:01", t) && t == DAY + HOUR + 60, "one day, one hour, one minute later");
    check(parseTime("2024/02/29 23:59", t) && t == 1709251140LL, "leap day 2024 is handled");
    check(!parseTime("not-a-date", t), "a bad date is rejected");

    // A -> B -> C -> A is a cycle, C -> D, D -> D is a self loop, plus one bad row.
    // Rows are deliberately NOT in time order.
    const char* path = "aml_test_graph.csv";
    {
        ofstream out(path);
        out << "Timestamp,From Bank,Account,To Bank,Account,Amount Received,Receiving Currency,"
               "Amount Paid,Payment Currency,Payment Format,Is Laundering\n"
            << "2022/09/01 00:30,1,C,1,A,90,US Dollar,90,US Dollar,ACH,1\n"
            << "2022/09/01 00:10,1,A,1,B,100,US Dollar,100,US Dollar,ACH,1\n"
            << "2022/09/01 00:20,1,B,1,C,95,US Dollar,95,US Dollar,ACH,1\n"
            << "2022/09/02 09:00,1,C,1,D,10,US Dollar,10,US Dollar,Wire,0\n"
            << "2022/09/02 10:00,1,D,1,D,5,US Dollar,5,US Dollar,Wire,0\n"
            << "bad row\n";
    }
    Graph g;
    LoadStats stats;
    check(loadCsv(path, g, stats), "CSV file loads");
    remove(path);
    check(g.n == 4 && g.edges.size() == 4, "4 accounts and 4 transfers (self loop dropped)");
    check(stats.skipped == 1 && stats.selfLoops == 1, "1 bad row and 1 self loop counted");

    int a = g.idOf["1:A"], b = g.idOf["1:B"], c = g.idOf["1:C"], d = g.idOf["1:D"];
    check(g.edges[0].from == a && g.edges[0].to == b, "edges are sorted by time (A->B is first)");
    check(g.outEdges[c].size() == 2 && g.inEdges[a].size() == 1, "adjacency lists are correct");

    // Binary search on C's out-edges: C->A at 00:30 and C->D next day.
    long long cToA = g.edges[g.outEdges[c][0]].time;
    check(firstAtOrAfter(g, g.outEdges[c], cToA) == 0, "binary search: time equal to first edge -> 0");
    check(firstAtOrAfter(g, g.outEdges[c], cToA + 1) == 1, "binary search: just after first edge -> 1");
    check(firstAtOrAfter(g, g.outEdges[c], cToA + 10 * DAY) == 2, "binary search: after everything -> 2");

    SccResult scc = kosarajuScc(g);
    check(scc.count == 2, "2 SCCs: {A,B,C} and {D}");
    check(scc.comp[a] == scc.comp[b] && scc.comp[b] == scc.comp[c] && scc.comp[d] != scc.comp[a],
          "A, B, C are in the same SCC, D is alone");
    check(scc.compSize[scc.comp[a]] == 3, "the SCC of A has size 3");

    // A long cycle of 200000 accounts: the explicit stack must not overflow.
    Graph big;
    big.n = 200000;
    for (int i = 0; i < big.n; i++) big.addEdge(i, (i + 1) % big.n, 1, i, false);
    big.build();
    check(kosarajuScc(big).count == 1, "200000-account cycle is one SCC (no stack overflow)");
}
