# Evaluation Results: `data/HI-Small_Trans.csv` (IBM Synthetic AML)

## 1. `./aml evaluate data/HI-Small_Trans.csv`

Dataset: `data/HI-Small_Trans.csv`

- Accounts: `515088`
- Transfers: `4487133` (rows skipped: `0`, self-loops dropped: `591212`)
- Labelled laundering: `5166` (plus `11` on self-loops, dropped during ingestion)
- Parameters: cycles `K=6 W=168h`; fan `k=5 W=24h`; chains `L=3 D=24h eps=0.10`

| Detector | Flagged | Correct | Precision | Recall | F1 | Time (s) | Notes |
|---|---|---|---|---|---|---|---|
| Temporal cycles | 8592 | 531 | 0.0618 | 0.1028 | 0.0772 | 1.872 | 11959 cycles |
| Fan-out | 1169117 | 1338 | 0.0011 | 0.2590 | 0.0023 | 0.090 | 13443 centres |
| Fan-in | 102873 | 1021 | 0.0099 | 0.1976 | 0.0189 | 0.089 | 6906 centres |
| Pass-through chains | 8261 | 59 | 0.0071 | 0.0114 | 0.0088 | 1.333 | longest chain 4 |
| Max k-core | 115 | 66 | 0.5739 | 0.0128 | 0.0250 | 0.570 | k = 5 |
| Greedy densest | 70 | 66 | 0.9429 | 0.0128 | 0.0252 | 0.570 | 14 accounts, density 3.643 |
| Exact densest | 321 | 0 | 0.0000 | 0.0000 | 0.0000 | 0.012 | 81 accounts, density 3.889, 3 min cuts |
| Union of pattern detectors | 1277262 | 2820 | 0.0022 | 0.5459 | 0.0044 | 3.384 | cycles, fans, chains |

Running time (s) on the first x% of transfers in time order

| Transfers | Cycles | Fan-out | Fan-in | Chains | Peeling | Exact densest |
|---|---|---|---|---|---|---|
| 1121783 (25%) | 0.052 | 0.014 | 0.014 | 0.155 | 0.125 | 0.005 |
| 2243566 (50%) | 0.105 | 0.042 | 0.046 | 0.560 | 0.280 | 0.007 |
| 3365349 (75%) | 0.510 | 0.068 | 0.070 | 0.950 | 0.420 | 0.009 |
| 4487133 (100%) | 1.872 | 0.090 | 0.089 | 1.333 | 0.570 | 0.012 |

---

## 2. `./aml detect data/HI-Small_Trans.csv`

```text
== Module 1: ingestion and SCC ==
accounts                    515088
transfers                   4487133
rows skipped / self loops   0 / 591212
labelled laundering         5166
load time (s)               4.035
SCCs                        494769 (size > 1: 2928, largest: 17075)
SCC time (s)                0.169

== Module 2: temporal cycles and fan-out / fan-in ==
temporal cycles             11959
transfers on a cycle        8592 (labelled laundering: 531)
cycle time (s)              1.780
fan-out centres / transfers  13443 / 1169117 (labelled laundering: 1338), 0.089 s
fan-in  centres / transfers  6906 / 102873 (labelled laundering: 1021), 0.088 s

== Module 3: pass-through chains, k-core and greedy peeling ==
longest chain               4 transfers
transfers on a chain        8261 (labelled laundering: 59)
chain time (s)              1.351
undirected edges            644246
max core                    k=5, 26 accounts, 115 transfers inside (laundering: 66)
greedy densest set          14 accounts, 51 edges, density 3.6429
  transfers inside          70 (labelled laundering: 66)
peeling time (s)            0.572

== Module 4: exact densest subgraph ==
flow network                862 accounts, 2339 edges (after k-core speed-up)
exact densest set           81 accounts, 315 edges, density 3.8889
transfers inside            321 (labelled laundering: 0)
min cuts / time (s)         3 / 0.012
```
