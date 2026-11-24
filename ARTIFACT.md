# Exact-on-accept retrieval for private RAG

The artifact includes the frozen SciFact and FiQA inputs, source code for BFV, two-server retrieval and page delivery, public experiment records, independent score checks, and the manuscript. Start in the extracted `review` directory. Paths in the following commands are relative to that directory. Each command takes a new output directory. Keep runs containing freshly generated caches and secrets private.

## Check the paper without a GPU

Use Python 3.12 and NumPy 2.2.6. Matplotlib 3.10.3 reproduces the communication plot. On Linux the commands use `sha256sum`. On macOS they can use `shasum`.

```bash
python3.12 -m venv .venv
.venv/bin/pip install numpy==2.2.6 matplotlib==3.10.3
export REVIEW_PYTHON="$PWD/.venv/bin/python"
runner=research/v23-certified-rag-revision/scripts/replay.sh
bash "$runner" check check-public
bash "$runner" figures regenerated
```

`check` verifies the package manifest, reconstructs the three BEIR layout reports, compares all 90 ordinary layout epochs across builds, and checks both historical pruning series. It verifies the public revision report and the weak-reader inputs reused by the 8B reader. It compares comparator rankings, timings and bytes with the frozen pages. Every query ID is available in the archived transcripts. `figures` regenerates the table bodies, numeric macros and the vector figure. The paper builds with `latexmk -pdf paper.tex` inside `research/v23-certified-rag-revision/paper` using TeX Live with the USENIX template dependencies, `breakurl`, Times math and Courier.

| Question | Data inside `research/v23-certified-rag-revision/results` |
|---|---|
| Does the upper-bound batch certify all feasible queries? | `prefix`, `priority-controls`, `eight-bit`, `independent-prefix.json`, `independent-eight.json` |
| Was a result ever delivered on an invalid score? | `projected-d383/fixed-rounds`, `signed-schedule/signed-check` |
| What does precision disclose and what does it cost? | `precision-full`, `heldout`, `n1000000` |
| Does a fresh BFV epoch preserve the outcome? | `beir`, `n100000`, `n1000000`, `reverse100000`, `reverse1000000` |
| What did a clean native rebuild reproduce? | `archive-rebuild` contains fresh BEIR and 383-coordinate transcripts, signed failure schedules, and one noise observation per trial. `controls.json` at the top level records the first run. |
| What changes after two-query hint reuse or a larger reader? | `warm-window`, `reader8` |
| Did the external comparator reproduce from its upstream sources? | `ragtime-rebuild` contains the source and executable hashes, plus 16 cold and 16 warm queries per dataset. |

The CPU check also inspects 13 SciFact block layouts in floating point. It recomputes document coverage and query upper bounds, checks the 300 query IDs per layout, and checks certified orders against a full scan. Twelve layouts pass the bound checks. The retained early isotropic layout has violations in 674 of 681 blocks. Its recorded 300 positive decisions are excluded from the paper's successful results. Both the original geometry checker and the portable reproduction are included.

The bundled PDF was built under pdfTeX 1.40.29 with `SOURCE_DATE_EPOCH=1795392000 FORCE_SOURCE_DATE=1`. With that toolchain and the included figure, `latexmk -pdf paper.tex` reproduces the file across extraction directories. The paper disables pdfTeX's path-dependent trailer identifier.

The ten-bit primary and independent 192-query measurement scripts are preserved in `snapshots`, as is the later four-priority comparison on that same 128-query cohort. The current implementation adds complete-frame validation handling and the eight-bit experiment. The verifier binds each set of measurements to the version that produced it. `COHORT_DESIGN.md` records why the separate eight-bit cohort was drawn. Timings include unsuccessful attempts and are specific to the measured machine.

## Build and run the native BEIR experiments

We used Linux x86-64, GCC 13 with AES-NI/AVX2 and OpenMP, CMake, libsodium with ristretto255, a CUDA compiler and an NVIDIA RTX PRO 5000 with 72 GB. SEAL 4.1.2 is included. The measured implementation has separate processes for the two roles and an optional shared immutable CUDA allocation. Replication on one host establishes execution costs under co-location. The privacy model assumes separate non-colluding operators.

```bash
.venv/bin/pip install cryptography==50.0.2 scipy==1.15.3
export REVIEW_CUDA_ARCH=sm_120
bash "$runner" build native-build
bash "$runner" beir beir-fresh "$PWD/native-build"
bash "$runner" layouts five-layouts
```

`REVIEW_NVCC` can select another CUDA installation. `beir` checks the two independent BEIR rankings, all three 383-coordinate BFV formats, signature-bearing score failures, a fixed-schedule control, the 288 ciphertext noise diagnostics, random bucket matching and the two DPF variants in forward, reverse and shaped-loopback runs. `beir-fresh/replayed.json` summarizes comparisons with the archived records. `layouts` rebuilds the five-format BEIR control from its original sources and emits its own checked report. It also needs `rsync` and GNU `sha256sum`.

Each BFV trial opens 288 field scores and records the smallest noise budget among its terminal ciphertexts. All 288 first-run minima were 17 bits. In the archived rebuild, 287 were 17 bits and one at dimension 769 with a separate reply was 16 bits. The modular oracle agreed on all lanes in both runs. A new run checks the deterministic profile and positive remaining budget, allowing its fresh secrets to change the observed distribution.

## Million-passage experiments

`fetch_scale_inputs.py` downloads generation-pinned byte ranges from the authors' public Compass storage and checks SHA-256. It materializes a 3,076,000,000-byte fvecs prefix, the 6,980-query bank, the first million PID-prefixed TSV lines and byte offsets. To reuse existing files, pass `--mirror DIR` with `documents.fvecs`, `queries.fvecs` and `collection.prefix.tsv` under those names. The signed encoding, quantizer and query selection parameters are in the scripts and the manuscript.

```bash
"$REVIEW_PYTHON" research/v23-certified-rag-revision/scripts/fetch_scale_inputs.py compass-inputs
bash "$runner" scale-pages checked-passages "$PWD/compass-inputs/collection.prefix.tsv"
bash "$runner" scale scale-fresh "$PWD/native-build" "$PWD/compass-inputs"
```

`scale-pages` also works on a CPU with just the original TSV prefix. It checks its million original passage IDs and SHA-256 in one pass, and checks the page hashes for every accepted delivery in the scale, holdout and selection-policy experiments. It also checks that each refused attempt released zero pages.

Reserve space for the input and several retained fresh epochs (roughly 200 GB of disk). Each million-row BFV replica contains 18.43 GB of masked coefficients. The co-located variant requires space on the device for that immutable table and private per-role work buffers. The scripts use a four-thread client scan, CPU SEAL evaluation and GPU table reduction. The `scale` entry point recreates the geometric screen, two scale controls, precision sweep, prior holdout, primary two-policy experiment, stronger four-policy controls and both independent CPU checks. The paper reports application bytes for both roles. It charges preloads and evaluation keys separately. Latencies and random epoch roots will vary.

## Reader and external author code

The release includes all 300 SciFact labels, the pages captured at the authorized endpoint and a public-source page comparison. The 8B run used llama-cpp-python 0.3.16 compiled with CUDA, four CPU threads, all layers offloaded and 1,536 context tokens. The exact GGUF blob is 5,225,374,496 bytes with SHA-256 `a3de86cd1c132c822487ededd47a324c50491393e6565cd14bafa40d0b8e686f`. The Ollama `library/qwen3` registry serves it by content digest. Upstream Qwen GGUF quantizations can differ even when they share a quantization label.

```bash
curl -fL 'https://registry.ollama.ai/v2/library/qwen3/blobs/sha256:a3de86cd1c132c822487ededd47a324c50491393e6565cd14bafa40d0b8e686f' -o qwen3-8b.gguf
CMAKE_ARGS="-DGGML_CUDA=on" .venv/bin/pip install llama-cpp-python==0.3.16
bash "$runner" reader reader-fresh "$PWD/qwen3-8b.gguf"
```

For RAGtime, use Go 1.24 and Python with TenSEAL 0.3.16. Download both author source archives to a fresh directory and retain them beside the extracted trees. The cold measurement records the hashes of those exact gzip files. Run the commands from the archive root. The two checksums are also in `research/v22-oblivious-hyperfrontier-rag/results/gpu-delivery-02/author-ragtime-01/build.json`. The build applies the measured padded-database initialization fix and binds the experimental server to loopback. The cold run regenerates the two IVF populations and pages. The warm run reuses both hint sets across each pair and attributes measured setup bytes separately.

```bash
mkdir -p upstream/PIRRAG upstream/Piano-PIR-RAG-MODIFIED
(
  set -eu
  cd upstream
  curl -fL 'https://codeload.github.com/nd-dsp-lab/PIRRAG/tar.gz/faa6829a11e06f177a318e6db8ece1db32a695fc' -o PIRRAG.tar.gz
  curl -fL 'https://codeload.github.com/nd-dsp-lab/Piano-PIR-RAG-MODIFIED/tar.gz/34a61a4bed5f6303146247ebbb51bdd4dcdc8a0f' -o Piano-PIR-RAG-MODIFIED.tar.gz
  sha256sum -c <<'HASHES'
2539bfac09cb0da7761bda627d58bf0283480e59c5e40c1db4ba16d49188075b  PIRRAG.tar.gz
85c40a516ff213ed8f4252588506307f613cb4b558f8ec7b57756e4fde3f8b22  Piano-PIR-RAG-MODIFIED.tar.gz
HASHES
  for package in PIRRAG Piano-PIR-RAG-MODIFIED; do
    tar -xzf "$package.tar.gz" -C "$package" --strip-components=1
  done
)
.venv/bin/pip install tenseal==0.3.16
bash "$runner" ragtime ragtime-fresh "$PWD/upstream"
```

The source corpora, model and third-party code retain their upstream terms. The archive includes public per-query evidence for historical SQuAD, SealPIR and earlier MS MARCO controls. The optional external model weights, full Compass dataset and old build caches are obtained separately. The single-role PRAG and bisection numbers in the paper describe component measurements. They are outside the authenticated-delivery ranking. The package contains public verification keys and digests. The replay generates its own signing, OPRF, masking and encryption secrets.
