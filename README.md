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

## Reproduce the full results

The frozen experiment ZIP contains the per-query records, embeddings, build and replay scripts, and the file manifest. The published tables report measurements on one host. The ZIP is split into numbered files so each part fits the review host's download limit.

Download and extract the repository ZIP, then run the following commands in the directory containing `recover_artifact.py` with Python 3.12.

```sh
python3.12 recover_artifact.py
unzip certified-rag-public.zip
cd certified-rag-artifact
python3.12 restore.py review
```

The reconstructed ZIP is 191,876,035 bytes. Its SHA-256 is `ff6365a125a975e2b9b3256e3861bd65fc38ca011fe7144959595a4f04b854e6`. Each part and the combined ZIP are checked before the output is saved.

On the anonymous mirror, reviewers can also download just `recover_artifact.py` and pass `--mirror` followed by the repository URL printed in the paper. This fetches the same parts directly from the anonymous host. The guide in `certified-rag-artifact/README.md` describes how to restore the original BEIR inputs, verify the 2,338 manifest entries and regenerate the tables. The manuscript inside the frozen ZIP records the experiment snapshot before its artifact URL was assigned. The submitted manuscript supplies the review URL.
