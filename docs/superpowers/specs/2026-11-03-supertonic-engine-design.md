# Supertonic native TTS engine — design

Date: 2026-11-03
Status: approved for implementation (additive first)

## Intent

Add Supertonic 3 as a bundled, native, offline TTS engine beside Piper and
espeak-ng. The purpose is to evaluate whether Supertonic gives better sound
quality and a smaller dependency graph than Piper. Piper and espeak-ng stay
installed and selectable until the measured result justifies removing them.

## Goals

- Synthesize chat messages fully offline, in-process, through ONNX Runtime.
- Expose Supertonic in the existing engine selector with its own voice and
  speed controls.
- Reuse the existing 22,050 Hz worker contract, speech conditioner and audio
  buffer.
- Bundle the model in the Flatpak so enabling it needs no user setup or
  network access.
- Measure wall time, real-time factor and peak RSS on the existing chat corpus
  so the Piper decision can be made from evidence.

## Non-goals

- Removing Piper or espeak-ng in this change.
- Voice cloning, SSML, per-word streaming or language auto-detection.
- Training, fine-tuning or re-exporting the model.
- Replacing the local OpenAI-compatible server engine.

## Anti-goals

- A first-run download or a Python dependency for the bundled engine.
- Letting Supertonic's 44.1 kHz audio reach the worker without resampling.
- Blocking the TTS worker for an unbounded time when a model is corrupt.
- Loading the model at plugin start when the user selected another engine.

## Chosen approach

Native engine + bundled model, additive.

Supertonic's C++ inference is adapted from the archived MIT implementation into
`src/supertonic-*`. ONNX Runtime is the only native runtime dependency and the
Flatpak already ships it for Piper. Qt's `QJsonDocument` replaces
`nlohmann/json`; Qt's `QString::normalized(NormalizationForm_KD)` replaces the
hand-written NFKD tables; OpenMP is not used.

The engine follows the existing `TtsEngine` / injectable backend pattern used
by Piper:

- `SupertonicApi` — abstract synthesis boundary, so host tests do not need
  ONNX Runtime.
- `LibSupertonicApi` — ONNX Runtime implementation (PIMPL; ORT headers stay out
  of public headers).
- `SupertonicEngine` — speed/step policy, 44.1 kHz → 22.05 kHz conversion,
  sample-rate validation, cancellation forwarding.
- `SupertonicText` — pure text preprocessing, Unicode mapping and chunking.

## Model and packaging

Supertonic 3, pinned at Hugging Face revision
`aafc6e32416a594460b32413efc49d7fe4ce6d46`:

| File | Size |
|---|---:|
| `onnx/vector_estimator.onnx` | 245 MB |
| `onnx/vocoder.onnx` | 97 MB |
| `onnx/text_encoder.onnx` | 35 MB |
| `onnx/duration_predictor.onnx` | 3.6 MB |
| `onnx/tts.json`, `onnx/unicode_indexer.json` | 280 KB |
| `voice_styles/{F1..F5,M1..M5}.json` | 2.8 MB |
| `LICENSE` | 15 KB |

Install layout:

```
share/obs/obs-plugins/yt-chat-tts/supertonic/onnx/*.onnx
share/obs/obs-plugins/yt-chat-tts/supertonic/onnx/tts.json
share/obs/obs-plugins/yt-chat-tts/supertonic/onnx/unicode_indexer.json
share/obs/obs-plugins/yt-chat-tts/supertonic/voice_styles/*.json
share/obs/obs-plugins/yt-chat-tts/supertonic/LICENSE
```

Every Flatpak source is pinned by SHA-256. The model license (OpenRAIL-M) is
installed with the extension and added to `THIRD_PARTY_NOTICES.md`.

## Settings and UI

- `ttsEngine = "supertonic"` selects the engine.
- `supertonicVoice` — one of the ten built-in voices; default `F1`.
- `supertonicSpeed` — playback multiplier; default `1.0`, clamped 0.5–2.0.
- `supertonicSteps` — denoising steps; default `8`, clamped 4–16.
- `supertonicModelPath` — development override; empty means the bundled path.

The engine combo gains `Supertonic — natural (CPU, experimental)`. Its rows
show the voice combo, speed, and denoising steps. Pitch/amplitude and the
espeak rate stay hidden. The existing "low-latency streaming" checkbox applies
to Supertonic too: audio is emitted per sentence chunk, not per model step.

## Inference contract

- Input: rendered chat text, speed, steps.
- Output: mono float PCM at 22,050 Hz, matching `TtsEngine`.
- Model output is 44,100 Hz; `AudioResampler` converts it 2:1.
- Speed divides the predicted duration (higher = faster), clamped to the
  model's practical 0.5–2.0 range.
- Cancellation is checked between denoising steps and between sentence
  chunks. A single ORT `Run` cannot be interrupted; the longest wait is one
  denoising step.
- Initialization is lazy on the first synthesis, cached for the worker's
  lifetime. A load failure is latched and reported through
  `initializationFailed()`, which triggers the existing espeak fallback.
- ONNX Runtime intra-op threads: `qBound(1, idealThreadCount() / 2, 4)`.
  Inter-op threads: 1. This keeps OBS room to render.

## Failure handling

- Missing model/voice file: descriptive error listing the unresolved paths;
  falls back to espeak like a missing Piper model.
- Corrupt ONNX: latched load failure; worker hands the message to espeak.
- Wrong sample rate from the backend: engine rejects it rather than feeding a
  mismatched buffer to the worker.
- Cancel during synthesis: returns a normal cancellation error; the worker
  swallows it during shutdown.

## Tests

Host tests (no ONNX Runtime needed):

- `test_supertonic_text`: symbol replacement, Unicode normalization, emoji
  removal, trailing punctuation, chunking limits, language tags.
- `test_supertonic_engine`: rate conversion, speed/step policy, error
  propagation, cancellation forwarding, initialization-failure forwarding.
- `test_settings` / `test_tts_backend`: new settings round-trip and engine
  selection, including missing-model fallback.

Flatpak tests (real runtime and models):

- `tools/supertonic_probe` synthesizes a short phrase and the 200-character
  benchmark phrase, prints `sample_rate=22050 samples=<n>`, wall time, RTF and
  peak RSS.
- `scripts/test-supertonic-flatpak.sh` runs the probe inside the installed
  extension and fails on a missing binary/model, wrong sample rate or empty
  audio.

## Measurement

Run the probe over `tools/tts-bench/corpus.tsv` at 4 and 8 steps and record:
wall time per utterance, RTF, peak RSS, and an ASR/spot-check of the output.
Compare with the existing Piper numbers in
`docs/tts-bake-off-2026-10-24.md`. The removal decision remains a follow-up.

## Risks

- Model size: +381 MB in the Flatpak and ~650 MB resident. Accepted for the
  evaluation; not hidden.
- Archived upstream: frozen code and weights, no security updates.
- OpenRAIL-M is not OSI-approved; redistribution is allowed with the license
  and use restrictions, which ship with the extension.
- Text normalization: Supertonic has no currency/number frontend. Chat
  currency strings may need plugin-side expansion after listening tests.
