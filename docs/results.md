# Evaluation Results: `data/HI-Small_Trans.csv` (IBM Synthetic AML)

## 1. `./aml evaluate data/HI-Small_Trans.csv`

Dataset: `data/HI-Small_Trans.csv`

- Accounts: `515088`
- Transfers: `5078345` (rows skipped: `0`)
- Labelled laundering: `5166` (plus `11` on self loops, not counted)
- Parameters: cycles `K=6 W=168h`; fan `k=5 W=24h`; chains `L=3 D=24h eps=0.10`

| Detector | Flagged | True positives | Precision | Recall | F1 | Time (s) | Notes |
|---|---|---|---|---|---|---|---|
| Temporal cycles | 8592 | 531 | 0.0618 | 0.1028 | 0.0772 | 1.907 | 11959 cycles |
| Fan-out | 1169117 | 1338 | 0.0011 | 0.2590 | 0.0023 | 0.094 | 13443 centres |
| Fan-in | 102873 | 1021 | 0.0099 | 0.1976 | 0.0189 | 0.100 | 6906 centres |
| Pass-through chains | 8261 | 59 | 0.0071 | 0.0114 | 0.0088 | 1.759 | longest chain 4 |
| Max k-core | 115 | 66 | 0.5739 | 0.0128 | 0.0250 | 0.335 | k = 5 |
| Greedy densest | 227 | 66 | 0.2907 | 0.0128 | 0.0245 | 0.335 | 54 vertices, density 3.574 |
| Exact densest | 321 | 0 | 0.0000 | 0.0000 | 0.0000 | 0.007 | 81 vertices, density 3.889, 3 min cuts |
| Union of pattern detectors | 1277262 | 2820 | 0.0022 | 0.5459 | 0.0044 | 3.860 | cycles, fans, chains |

Running time (s) on the first x% of transfers in time order

| Transfers | Cycles | Fan-out | Fan-in | Chains | Peeling | Exact densest |
|---|---|---|---|---|---|---|
| 1269586 (25%) | 0.058 | 0.015 | 0.014 | 0.170 | 0.084 | 2.785 |
| 2539172 (50%) | 0.109 | 0.045 | 0.050 | 0.688 | 0.187 | 0.092 |
| 3808758 (75%) | 0.549 | 0.071 | 0.076 | 1.200 | 0.263 | 0.269 |
| 5078345 (100%) | 1.907 | 0.094 | 0.100 | 1.759 | 0.335 | 0.007 |

---

## 2. `./aml detect data/HI-Small_Trans.csv`

```text
== Ingestion and SCC ==
accounts (vertices)         515088
transfers (edges)           5078345
rows skipped (malformed)    0
self loops                  591212
labelled laundering         5177
max out-degree / in-degree  168672 / 1084
load time (s)               3.602
SCCs                        494769 (size > 1: 2928, largest: 17075)
SCC time (s)                0.116

== Temporal cycles and fan-out / fan-in ==
cycle params                K=6 W=168h
temporal cycles             11959
transfers on a cycle        8592 (labelled laundering: 531)
cycle time (s)              1.762
fan params                  k=5 W=24h
fan-out centres / transfers   13443 / 1169117 (labelled laundering: 1338), 0.117 s
fan-in  centres / transfers   6906 / 102873 (labelled laundering: 1021), 0.099 s

== Pass-through chains, k-core and greedy peeling ==
chain params                L=3 D=24h eps=0.10
longest chain               4 edges
transfers on a chain        8261 (labelled laundering: 59)
chain time (s)              1.713
undirected edges            644246
max core                    k=5, 26 vertices, 115 transfers inside (laundering: 66)
greedy densest set          54 vertices, 193 edges, density 3.5741
  transfers inside          227 (labelled laundering: 66)
peeling time (s)            0.331

== Exact densest subgraph ==
flow network                862 vertices, 2339 edges after core pruning
exact densest set           81 vertices, 315 edges, density 3.8889
greedy / exact density      0.9190
transfers inside            321 (labelled laundering: 0)
min cuts / time (s)         3 / 0.001
```
