# Supertonic Native TTS Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Supertonic 3 as a bundled, native, offline TTS engine beside Piper and espeak-ng, with host tests, a Flatpak probe and measured performance.

**Architecture:** Port the archived MIT C++ inference path into a `SupertonicApi` boundary and a `SupertonicEngine` adapter, using ONNX Runtime + Qt JSON. Wire it into the existing engine selector, settings, Flatpak manifest and benchmark harness without removing Piper or espeak.

**Tech Stack:** C++17, Qt6 Core/Widgets, ONNX Runtime 1.22, CMake 3.28, OBS libobs, Flatpak, Bash.

**Spec:** `docs/superpowers/specs/2026-11-03-supertonic-engine-design.md`

## Global Constraints

- Output contract is mono float PCM at exactly 22,050 Hz; model audio is 44,100 Hz and must be resampled.
- Supertonic 3 pinned at HF revision `aafc6e32416a594460b32413efc49d7fe4ce6d46`.
- Do not remove Piper or espeak-ng in this change.
- No Python, no nlohmann/json, no OpenMP in the shipped engine.
- ONNX Runtime intra-op threads: `qBound(1, QThread::idealThreadCount() / 2, 4)`; inter-op 1.
- Speed clamp 0.5–2.0; steps clamp 4–16; defaults 1.0 and 8.
- Default voice `F1`; all ten voices bundled.
- Model license (OpenRAIL-M) ships with the extension and is recorded in `THIRD_PARTY_NOTICES.md`.
- Every Flatpak model source is pinned by SHA-256.

## Review Focus

- A message containing only punctuation or whitespace must not crash or emit a zero-length tensor.
- A very long message must be chunked below the model's 300-character limit.
- Cancellation during denoising must return promptly and not leave the worker stuck.
- A missing or corrupt model must fall back to espeak with a readable log, not go silent.
- `44.1 kHz` model output must never reach the worker as `44,100 Hz`.

---

### Task 1: Text preprocessing, Unicode mapping and chunking

**Files:**
- Create: `src/supertonic-text.hpp`
- Create: `src/supertonic-text.cpp`
- Test: `tests/test_supertonic_text.cpp`

**Interfaces:**
- Produces: `namespace SupertonicText`
  - `QString preprocess(const QString &text, const QString &lang)`
  - `QStringList chunk(const QString &text, int maxLen = 300)`
  - `std::vector<uint16_t> unicodeValues(const QString &text)`

- [ ] **Step 1: Write failing tests** covering tags/period, dash and quote replacement, emoji removal, `café` NFKD decomposition, CJK preservation, 300-character chunking and empty input.
- [ ] **Step 2: Run `ctest -R supertonic_text` and confirm it fails to build.**
- [ ] **Step 3: Implement `supertonic-text.cpp`** using `QString::normalized(QString::NormalizationForm_KD)`, `QRegularExpression` for emoji and sentence splitting, and the archived helper's replacement list.
- [ ] **Step 4: Run the test and confirm pass.**
- [ ] **Step 5: Commit.**

### Task 2: Engine adapter over an injectable backend

**Files:**
- Create: `src/supertonic-api.hpp`
- Create: `src/supertonic-engine.hpp`
- Create: `src/supertonic-engine.cpp`
- Test: `tests/test_supertonic_engine.cpp`

**Interfaces:**
- Produces: `class SupertonicApi` with `synthesize`, `synthesizeStreaming`, `cancel`, `resetCancel`, `initializationFailed`.
- Produces: `class SupertonicEngine : public TtsEngine` constructed from `std::unique_ptr<SupertonicApi>`, with `setSpeed(float)` and `setSteps(int)`.
- Consumes: `AudioResampler::resample`.

- [ ] **Step 1: Write failing tests** for 44.1→22.05 conversion, speed/step clamping, API error propagation, wrong-rate rejection, cancellation forwarding and initialization failure.
- [ ] **Step 2: Run the test and confirm failure.**
- [ ] **Step 3: Implement the adapter** with a fake-friendly boundary and resampling.
- [ ] **Step 4: Run the test and confirm pass.**
- [ ] **Step 5: Commit.**

### Task 3: Settings and backend selection

**Files:**
- Modify: `src/settings.hpp`, `src/settings.cpp`
- Test: `tests/test_settings.cpp`, `tests/test_tts_backend.cpp`

**Interfaces:**
- Adds `Settings::supertonicVoice`, `supertonicSpeed`, `supertonicSteps`, `supertonicModelPath`.
- Adds `TtsBackend::Supertonic`.
- `chooseTtsBackend(const Settings &, bool piperModelPresent, bool supertonicModelPresent)`.

- [ ] **Step 1: Write failing tests** for defaults, clamping, JSON round-trip, explicit supertonic selection and missing-model fallback.
- [ ] **Step 2: Run the tests and confirm failure.**
- [ ] **Step 3: Implement settings normalization and selection.**
- [ ] **Step 4: Run the tests and confirm pass.**
- [ ] **Step 5: Commit.**

### Task 4: ONNX Runtime backend

**Files:**
- Create: `src/libsupertonic-api.hpp`
- Create: `src/libsupertonic-api.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `LibSupertonicApi(QString modelDir, QString voicePath, QString language, int threads)` implementing `SupertonicApi`.
- Consumes: Qt JSON, `SupertonicText`, ONNX Runtime C++ API.

- [ ] **Step 1: Add a host compile check** that includes the PIMPL header without ORT headers.
- [ ] **Step 2: Implement model/config/voice loading**, the four sessions, duration prediction, text encoding, the denoising loop, the vocoder, and the 0.3 s inter-chunk silence.
- [ ] **Step 3: Wire `find_path`/`find_library` for ORT independently of libpiper**, define `HAVE_SUPERTONIC`, and link the engine into the plugin.
- [ ] **Step 4: Build the plugin with `-DONNXRUNTIME_ROOT` and confirm no link errors.**
- [ ] **Step 5: Run the host test suite.**
- [ ] **Step 6: Commit.**

### Task 5: Plugin context and dock UI

**Files:**
- Modify: `src/plugin-context.cpp`, `src/plugin-context.hpp`
- Modify: `src/yt-chat-tts-dock.cpp`, `src/yt-chat-tts-dock.hpp`
- Modify: `data/locale/en-US.ini`

**Interfaces:**
- Resolves `supertonic/` via `resolveBundledPath`, validates the model directory and voice file, creates `SupertonicEngine`, and falls back to espeak on missing/corrupt resources.
- Adds the engine combo item, voice combo, steps spinbox, and shows/hides the right rows.

- [ ] **Step 1: Implement resource discovery and engine creation.**
- [ ] **Step 2: Implement the UI rows and settings load/save.**
- [ ] **Step 3: Run `scripts/test-host.sh` and the locale check.**
- [ ] **Step 4: Commit.**

### Task 6: Flatpak packaging

**Files:**
- Modify: `flatpak/com.obsproject.Studio.Plugin.YtChatTts.yml`
- Create: `flatpak/supertonic-model-sources.yml` (or inline module)
- Modify: `THIRD_PARTY_NOTICES.md`, `LICENSES/`, `README.md`

- [ ] **Step 1: Add the model module** with all 15 pinned sources and license installation.
- [ ] **Step 2: Keep the Piper module unchanged.**
- [ ] **Step 3: Build the Flatpak and verify the installed tree.**
- [ ] **Step 4: Commit.**

### Task 7: Probe and Flatpak verification

**Files:**
- Create: `tools/supertonic_probe.cpp`
- Create: `scripts/test-supertonic-flatpak.sh`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Implement `supertonic-probe`** with `--model-dir`, `--voice`, `--text`, `--steps`, `--speed`, `--bench`, `--cancel`; print `sample_rate=22050 samples=<n>` and timing.
- [ ] **Step 2: Build and run the probe against `/tmp/opencode/supertonic-assets`.**
- [ ] **Step 3: Add the Flatpak test script** and run it against the installed extension.
- [ ] **Step 4: Commit.**

### Task 8: Measure and document

**Files:**
- Modify: `docs/tts-bake-off-2026-10-24.md` or create `docs/supertonic-evaluation-2026-11-03.md`
- Modify: `README.md`

- [ ] **Step 1: Run the probe over `tools/tts-bench/corpus.tsv` at 4 and 8 steps.**
- [ ] **Step 2: Record wall time, RTF, peak RSS and sample-rate checks.**
- [ ] **Step 3: Add a blind listening page under `build-host/tts-bench/`.**
- [ ] **Step 4: Commit.**
