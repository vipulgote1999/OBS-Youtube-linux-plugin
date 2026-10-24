# TTS bake-off harness

Host-only benchmark used to decide whether Pocket TTS or Kokoro should replace
Piper in the plugin. It runs every engine through the same sherpa-onnx runtime
so the only variable between rows is the model.

This is a development tool. It is not built by the plugin's CMake project and
nothing in `src/` depends on it.

## What it measures

Per utterance:

| Field | Meaning |
|---|---|
| `load_ms` | `OfflineTts::Create` → model ready. Decides whether the engine can be created lazily once, like `LibPiperApi`. |
| `warmup_ms` | Cost of a discarded warm-up utterance. `pocket-tts#91` shows the first generation after load can be garbled, so the probe always warms up. |
| `ttfa_ms` | Time to the first progress-callback invocation. **Note:** sherpa invokes the callback per sentence, not per audio chunk, so for a one-sentence input this equals `compute_ms`. Do not read it as streaming latency. |
| `compute_ms` | Wall-clock time for the whole utterance. This is what the plugin's worker actually waits for. |
| `rtf` | `compute_ms / audio_ms`. Hardware efficiency. |
| `audio_ms` | Duration of the generated speech. Voices differ in pace, so compare `compute_ms` for user-visible latency and `rtf` for cost. |
| `peak_rss_mb` | `getrusage(RUSAGE_SELF).ru_maxrss` after the run. |
| `click_ratio` | Largest sample-to-sample jump in the first 40 ms ÷ the 99th percentile jump in the remainder. A chunk-boundary click (`pocket-tts#171`) shows up as a large value. |
| `callback_calls` | How many times sherpa invoked the callback. Exposes whether a model produces sentence-level chunks. |
| `rms`, `peak` | Level of the generated audio. `rms < 0.005` or a very short clip is flagged as suspicious in the report. |
| `aborted`, `abort_reason` | The model hit the per-utterance cap. `audio` means it generated more audio than a chat message can justify (the `pocket-tts#113` no-EOS loop); `compute` means it was too slow. Both count as failures. |
| `edge step` (report) | The larger of the first and last sample, in full-scale units, read from the dumped WAV. A non-zero value steps to silence when the feeder runs dry, which is the click in `pocket-tts#171`. The plugin's `SpeechConditioner` fades every utterance to zero, so this is a property of the raw model output, not of shipped playback. |

## Corpus

`corpus.tsv` is deliberately adversarial for live chat:

- `short1`/`short2`/`short3` — 1–3 word messages. Pocket TTS is documented as
  weak here (`#91`, `#70`, `#313`), and this is most of a chat's traffic.
- `currency`/`number` — `$5.00`, `1943`, `1080p60`. Pocket TTS has no
  number-to-word frontend (`#162`, `#234`, `#395`), and the plugin renders
  superchats as `"Alice sent $5.00: ..."`.
- `repetition` — the end-of-sentence repeat repro from `#347`.
- `mixed`/`unicode` — URLs, emoji, mentions, accents, em dashes.
- `long_200` — the same 200-character phrase `piper-probe --bench` uses, so the
  existing 1.5 s packaging target stays comparable.

The script also runs a separate `pocket-short-stress` row: six repetitions of
each one-word message, so the short-message failure rate has a real denominator
instead of two lucky runs.

## Running

```bash
./scripts/bench-tts-host.sh \
    --cmake /path/to/cmake \
    --proxy https://gh-proxy.com   # only if GitHub release downloads stall
```

`--proxy` is optional. Networks that throttle
`release-assets.githubusercontent.com` stall on direct downloads; the proxy
route is verified against the SHA-256 digests pinned in the script, so a proxy
cannot change the input.

Outputs land in `build-host/tts-bench/` (git-ignored):

- `report.md` — per-configuration table, per-category latency, gate results
- `results.csv` — one row per utterance
- `json/*.jsonl` — raw probe output
- `wav/` — deterministic filenames
- `listen/` — randomised filenames plus `index.html` for a blind listen;
  `listen/map.csv` holds the mapping

## Pinned inputs

| Input | Version | SHA-256 pinned |
|---|---|---|
| sherpa-onnx | v1.13.8 (`linux-x64-shared`) | yes |
| Pocket TTS | `sherpa-onnx-pocket-tts-int8-2026-01-26` (legacy 100M English model) | yes |
| Kokoro | `kokoro-int8-en-v0_19` | yes |
| Piper reference | `vits-piper-en_US-kristin-medium-int8` | yes |
| Reference voices | VCTK `p228`, `p333` (CC-BY-4.0) | yes |

The Pocket bundle is the **January 2026** legacy model. It predates the
September/October 2026 checkpoints that fixed end-of-sentence repetition
(`#347`) and number truncation (`#393`, `#395`). There is currently no working
export path for the newer checkpoints: `PocketTTS.cpp#12` (exporter broken) and
`sherpa-onnx#3755` (newer exports do not load). Any "Pocket wins" result
therefore means *this* bundle, not upstream Pocket TTS today.

## Building the probe by hand

```bash
cmake -S tools/tts-bench -B build-host/tts-bench/build \
      -DCMAKE_BUILD_TYPE=Release \
      -DSHERPA_ONNX_ROOT=/path/to/sherpa-onnx-v1.13.8-linux-x64-shared
cmake --build build-host/tts-bench/build -j
```

The prebuilt sherpa-onnx libraries use the old libstdc++ ABI, so the probe is
compiled with `_GLIBCXX_USE_CXX11_ABI=0`. Without it, the `std::string`
parameters in the C++ wrapper fail to link.

## Interpreting the gates

`scripts/bench-tts-host.sh` writes objective gate results (speed, memory,
silent/truncated clips). Quality is not automated: open `listen/index.html` and
compare each text across engines. The decision rule used for this project is in
`report.md`.
