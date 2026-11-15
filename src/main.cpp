// =============================================================================
//  main.cpp          Money-Laundering Ring Detector  -  command line program
//
//  Build:   make          (or:  clang++ -std=c++17 -O2 src/main.cpp -o aml)
//  Usage:
//      ./aml test                        run the small hand-checked tests
//      ./aml generate <out.csv> [accounts] [normal transfers] [seed]
//      ./aml detect   <transactions.csv> run every detector and print the results
//      ./aml evaluate <transactions.csv> precision / recall table + running times
//
//  The four modules are included in order; each one uses the ones before it:
//      ingest_scc -> cycles_fan -> chains_peeling -> exact_eval -> main
// =============================================================================
#include "ingest_scc.cpp"
#include "cycles_fan.cpp"
#include "chains_peeling.cpp"
#include "exact_eval.cpp"

// ---------------------------- settings (change and re-run `make`) ------------
const int CYCLE_MAX_LENGTH = 6;           // at most 6 transfers in a cycle
const long long CYCLE_WINDOW = 7 * DAY;   // whole cycle within one week
const long long CYCLE_LIMIT = 1000000;    // stop after this many cycles (safety)

const int FAN_MIN_PARTNERS = 5;           // at least 5 different accounts ...
const long long FAN_WINDOW = DAY;         // ... within one day

const int CHAIN_MIN_LENGTH = 3;           // at least 3 transfers in a chain
const long long CHAIN_MAX_DELAY = DAY;    // each hop forwarded within one day
const double CHAIN_TOLERANCE = 0.10;      // each hop keeps at most 10%

// ---------------------------- run every detector -----------------------------

struct Detection {
    string name;
    vector<char> flagged;  // flagged[i] = 1 if transfer i is suspicious
    double seconds;
    string note;
};

vector<Detection> runAllDetectors(const Graph& g) {
    vector<Detection> out;
    auto start = chrono::steady_clock::now();

    SccResult scc = kosarajuScc(g);
    vector<vector<int>> cycles = findTemporalCycles(g, scc, CYCLE_MAX_LENGTH, CYCLE_WINDOW, CYCLE_LIMIT);
    out.push_back({"Temporal cycles", edgesOnCycles(g, cycles), secondsSince(start),
                   to_string(cycles.size()) + " cycles"});

    start = chrono::steady_clock::now();
    FanResult fanOut = findFans(g, FAN_MIN_PARTNERS, FAN_WINDOW, true);
    out.push_back({"Fan-out", fanOut.flagged, secondsSince(start), to_string(fanOut.centres) + " centres"});

    start = chrono::steady_clock::now();
    FanResult fanIn = findFans(g, FAN_MIN_PARTNERS, FAN_WINDOW, false);
    out.push_back({"Fan-in", fanIn.flagged, secondsSince(start), to_string(fanIn.centres) + " centres"});

    start = chrono::steady_clock::now();
    ChainResult chains = findChains(g, CHAIN_MIN_LENGTH, CHAIN_MAX_DELAY, CHAIN_TOLERANCE);
    out.push_back({"Pass-through chains", chains.flagged, secondsSince(start),
                   "longest chain " + to_string(chains.longest)});

    start = chrono::steady_clock::now();
    SimpleGraph h = makeUndirected(g);
    PeelResult peeled = peel(h);
    double peelSeconds = secondsSince(start);
    out.push_back({"Max k-core", edgesInside(g, inKCore(peeled, peeled.maxCore)), peelSeconds,
                   "k = " + to_string(peeled.maxCore)});
    char note[100];
    snprintf(note, sizeof(note), "%d accounts, density %.3f", peeled.bestVertices, peeled.density());
    out.push_back({"Greedy densest", edgesInside(g, peeled.inDensest), peelSeconds, note});

    start = chrono::steady_clock::now();
    DensestResult exact = exactDensest(h, peeled);
    snprintf(note, sizeof(note), "%d accounts, density %.3f, %d min cuts", exact.vertices, exact.density(),
             exact.minCuts);
    out.push_back({"Exact densest", edgesInside(g, exact.inSet), secondsSince(start), note});

    // A transfer flagged by any of the first four detectors (cycles, fans, chains).
    vector<char> any(g.edges.size(), 0);
    double total = 0;
    for (int d = 0; d < 4; d++) {
        for (int i = 0; i < (int)g.edges.size(); i++) any[i] = any[i] || out[d].flagged[i];
        total += out[d].seconds;
    }
    out.push_back({"Union of pattern detectors", any, total, "cycles, fans, chains"});
    return out;
}

// ---------------------------- ./aml detect -----------------------------------

void detect(const Graph& g, const LoadStats& stats, double loadSeconds) {
    long long total, laundering;
    printf("== Module 1: ingestion and SCC ==\n");
    printf("accounts                    %d\n", g.n);
    printf("transfers                   %zu\n", g.edges.size());
    printf("rows skipped / self loops   %lld / %lld\n", stats.skipped, stats.selfLoops);
    countFlagged(g, vector<char>(g.edges.size(), 1), total, laundering);
    printf("labelled laundering         %lld\n", laundering);
    printf("load time (s)               %.3f\n", loadSeconds);

    auto start = chrono::steady_clock::now();
    SccResult scc = kosarajuScc(g);
    int bigSccs = 0, largest = 0;
    for (int size : scc.compSize) {
        if (size > 1) bigSccs++;
        largest = max(largest, size);
    }
    printf("SCCs                        %d (size > 1: %d, largest: %d)\n", scc.count, bigSccs, largest);
    printf("SCC time (s)                %.3f\n\n", secondsSince(start));

    printf("== Module 2: temporal cycles and fan-out / fan-in ==\n");
    start = chrono::steady_clock::now();
    vector<vector<int>> cycles = findTemporalCycles(g, scc, CYCLE_MAX_LENGTH, CYCLE_WINDOW, CYCLE_LIMIT);
    countFlagged(g, edgesOnCycles(g, cycles), total, laundering);
    printf("temporal cycles             %zu\n", cycles.size());
    printf("transfers on a cycle        %lld (labelled laundering: %lld)\n", total, laundering);
    printf("cycle time (s)              %.3f\n", secondsSince(start));
    for (int outgoing = 1; outgoing >= 0; outgoing--) {
        start = chrono::steady_clock::now();
        FanResult fan = findFans(g, FAN_MIN_PARTNERS, FAN_WINDOW, outgoing);
        countFlagged(g, fan.flagged, total, laundering);
        printf("%s centres / transfers  %d / %lld (labelled laundering: %lld), %.3f s\n",
               outgoing ? "fan-out" : "fan-in ", fan.centres, total, laundering, secondsSince(start));
    }

    printf("\n== Module 3: pass-through chains, k-core and greedy peeling ==\n");
    start = chrono::steady_clock::now();
    ChainResult chains = findChains(g, CHAIN_MIN_LENGTH, CHAIN_MAX_DELAY, CHAIN_TOLERANCE);
    countFlagged(g, chains.flagged, total, laundering);
    printf("longest chain               %d transfers\n", chains.longest);
    printf("transfers on a chain        %lld (labelled laundering: %lld)\n", total, laundering);
    printf("chain time (s)              %.3f\n", secondsSince(start));

    start = chrono::steady_clock::now();
    SimpleGraph h = makeUndirected(g);
    PeelResult peeled = peel(h);
    double peelSeconds = secondsSince(start);
    int coreSize = 0;
    for (int v = 0; v < g.n; v++) coreSize += (peeled.core[v] == peeled.maxCore);
    countFlagged(g, edgesInside(g, inKCore(peeled, peeled.maxCore)), total, laundering);
    printf("undirected edges            %lld\n", h.m);
    printf("max core                    k=%d, %d accounts, %lld transfers inside (laundering: %lld)\n",
           peeled.maxCore, coreSize, total, laundering);
    countFlagged(g, edgesInside(g, peeled.inDensest), total, laundering);
    printf("greedy densest set          %d accounts, %lld edges, density %.4f\n", peeled.bestVertices,
           peeled.bestEdges, peeled.density());
    printf("  transfers inside          %lld (labelled laundering: %lld)\n", total, laundering);
    printf("peeling time (s)            %.3f\n", peelSeconds);

    printf("\n== Module 4: exact densest subgraph ==\n");
    start = chrono::steady_clock::now();
    DensestResult exact = exactDensest(h, peeled);
    countFlagged(g, edgesInside(g, exact.inSet), total, laundering);
    printf("flow network                %d accounts, %lld edges (after k-core speed-up)\n",
           exact.networkVertices, exact.networkEdges);
    printf("exact densest set           %d accounts, %lld edges, density %.4f\n", exact.vertices, exact.edges,
           exact.density());
    printf("transfers inside            %lld (labelled laundering: %lld)\n", total, laundering);
    printf("min cuts / time (s)         %d / %.3f\n", exact.minCuts, secondsSince(start));
}

// ---------------------------- ./aml evaluate ---------------------------------

void evaluate(const Graph& g) {
    printf("| Detector | Flagged | Correct | Precision | Recall | F1 | Time (s) | Notes |\n");
    printf("|---|---|---|---|---|---|---|---|\n");
    vector<Detection> all = runAllDetectors(g);
    for (const Detection& d : all) {
        Score sc = score(g, d.flagged);
        printf("| %s | %lld | %lld | %.4f | %.4f | %.4f | %.3f | %s |\n", d.name.c_str(), sc.flagged, sc.correct,
               sc.precision, sc.recall, sc.f1, d.seconds, d.note.c_str());
    }

    printf("\nRunning time (s) on the first 25%%, 50%%, 75%%, 100%% of transfers\n\n");
    printf("| Transfers | Cycles | Fan-out | Fan-in | Chains | Peeling | Exact densest |\n");
    printf("|---|---|---|---|---|---|---|\n");
    int m = (int)g.edges.size();
    for (int percent = 25; percent <= 100; percent += 25) {
        int count = (int)((long long)m * percent / 100);
        vector<Detection> part = (percent == 100) ? all : runAllDetectors(firstTransfers(g, count));
        printf("| %d (%d%%) | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f |\n", count, percent, part[0].seconds,
               part[1].seconds, part[2].seconds, part[3].seconds, part[4].seconds, part[6].seconds);
    }
}

// ---------------------------- main -------------------------------------------

int main(int argc, char** argv) {
    string mode = argc >= 2 ? argv[1] : "";

    if (mode == "test") {
        testIngestScc();
        testCyclesFan();
        testChainsPeeling();
        testExactEval();
        printf(testsFailed == 0 ? "\nALL TESTS PASSED\n" : "\n%d TEST(S) FAILED\n", testsFailed);
        return testsFailed == 0 ? 0 : 1;
    }

    if (mode == "generate" && argc >= 3) {
        int accounts = argc > 3 ? atoi(argv[3]) : 20000;
        int normal = argc > 4 ? atoi(argv[4]) : 20000;
        int seed = argc > 5 ? atoi(argv[5]) : 1;
        return generateSynthetic(argv[2], accounts, normal, seed) ? 0 : 1;
    }

    if ((mode == "detect" || mode == "evaluate") && argc >= 3) {
        Graph g;
        LoadStats stats;
        auto start = chrono::steady_clock::now();
        if (!loadCsv(argv[2], g, stats)) {
            printf("cannot open %s\n", argv[2]);
            return 1;
        }
        if (mode == "detect") detect(g, stats, secondsSince(start));
        else evaluate(g);
        return 0;
    }

    printf("usage:\n"
           "  ./aml test\n"
           "  ./aml generate <out.csv> [accounts=20000] [normal transfers=20000] [seed=1]\n"
           "  ./aml detect   <transactions.csv>\n"
           "  ./aml evaluate <transactions.csv>\n");
    return 1;
}
