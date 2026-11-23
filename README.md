# Exact-on-Accept Retrieval for Private RAG with Client-Visible Geometry

This public snapshot contains the BFV scoring implementation, the two-server distributed point function (DPF) implementation, two result tables from the manuscript, and the output of the independent BEIR input verifier. The experiments measure delivery of the ordered top ten under a pinned integer score. The client is allowed to see the index geometry, and the two server roles are assumed to be non-colluding.

## Run the DPF check

On Linux x86-64 with AES-NI, a C++17 compiler, and Python 3, run:

```sh
c++ -std=c++17 -O3 -maes -msse2 -shared -fPIC bucket_dpf.cpp -o bucket_dpf.so
python3 check_dpf.py ./bucket_dpf.so
```

The check generates fresh keys, evaluates each server share and verifies their XOR. It covers all selected positions in domains of up to 64 positions, including requests truncated before the selected position. The expected output is `377 two-server DPF reconstruction cases passed.`

## Inspect the paper results

| File | Contents |
| --- | --- |
| `hidden_bfv.cpp` | BFV key setup, encrypted scoring, reply packing, decryption, and noise diagnostics using Microsoft SEAL 4.1.2. |
| `bfv_layout.tex` | Five BFV reply layouts per corpus, with phase-specific timings, online bytes, and evaluation-key sizes. |
| `beir.tex` | BFV, full-vector and residual PIR, token modes, and OPRF controls on the same delivery benchmark. |
| `verified-inputs.json` | Verifier output for 300 SciFact queries, 256 FiQA queries, and 3,320 source-page checks. |

The large evidence archive contains the per-query records and frozen embeddings used by the full reproduction. Its public upload is pending. The published tables report measurements on one host.
